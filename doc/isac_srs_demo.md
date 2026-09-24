# ISAC task: a GPU SRS channel estimator for sensing

A monolithic OAI gNB with 32 antennas receives the 4-port SRS of a 4-antenna OAI UE over the
rfsimulator. The radio channel between them is a ray-traced street canyon (Sionna RT), replayed by
the rfsimulator as a CIR database. Sensing (locating the UE and the walls, tracking what moves)
needs a far better estimate of that channel than communication does. The gNB's fixed-point SRS
estimator is not precise enough. **The task is to write the GPU estimator that is.**

```
Sionna scene ─► CIR DB (32 x 4 links, 192 taps, 122.88 Msps, 10 ms snapshots) + truth
                      │                                   │
nrUE (4 ant) ◄─ rfsim, CIR DB on the GPU at both ends ─► gNB 32T32R, 273 PRB, 30 kHz
                                                          │ SRS REs
                                         libsrs_est_cuda.so  ◄── your work
                                                  │ float estimate           │ c16 as before
                                              --isac.dump_file         TA, SRS.indication, MAC
                                                  │
                                   tools/isac/nmse_vs_truth.py
```

[[_TOC_]]

## What is there

| Piece | Where | What |
|---|---|---|
| 32T32R gNB | `ci-scripts/conf_files/gnb.sa.band78.273prb.rfsim.32x32.isac.conf` | n78, 273 PRB mu=1 (100 MHz, 122.88 Msps), 32 RU antennas, `pusch_AntennaPorts = 32` (SRS on every antenna), 4 logical DL ports, one DL layer, periodic SRS every 10 ms (`srs_period = 20` slots) |
| CIR DB channel | `radio/rfsimulator/rfsim_cirdb*`, `radio/COMMON/cirdb/` | `--rfsimulator.[0].cirdb_yaml/cirdb_file`: each end convolves what it receives with the database (the gNB with its transpose: TDD reciprocity), snapshot from the shared sample timestamp, CUDA kernel |
| Module interface and glue | `openair1/PHY/NR_ESTIMATION/srs_est/srs_est_interface.h`, `srs_module_estimation()` in `openair1/SCHED_NR/phy_procedures_nr_gNB.c` | `--loader.srs_est.shlibversion _cuda` loads the module and calls it for every SRS; on failure the gNB uses its fixed-point estimator for that SRS |
| Module skeleton | `openair1/PHY/NR_ESTIMATION/srs_est/srs_est_cuda.cu` | builds with `-DENABLE_SRS_CUDA=ON`, is loaded and called, estimates nothing yet |
| Export | `openair1/PHY/NR_ESTIMATION/srs_est/isac_dump.h` | `--isac.dump_file`: header + `complex64 h[port][antenna][comb RE]` for every SRS the module estimated |
| Scene and database | raytracing-channel-emulator, `server/isac/` (branch `isac-demo`) | `scene_street.py`, `generate_isac_cirdb.py`: street canyon, gNB 4x8 UPA at 10 m, UE walking at 1.4 m/s, a metal vehicle at 8 m/s; `cir_db.bin`, `isac.yaml`, `truth.npz`, `array.json` |
| Tools | `tools/isac/` | `isac_io.py`: readers for the export, the CIR DB and the ray tracer's truth; `nmse_vs_truth.py`: the exported estimate against the channel that was applied |

## The task

Implement `srs_est_init()`, `srs_est_run()` and `srs_est_shutdown()` in `srs_est_cuda.cu`.
`srs_est_interface.h` says what the gNB hands over (the received comb REs of every port, antenna
and SRS symbol, the base sequence, the cyclic shifts) and what it expects back:

- the channel of every port at every antenna, at the comb REs and at every subcarrier of the band;
- the noise variance per RE and the mean signal power.

The rest of the L1 gets the estimate in its fixed-point layout, so timing advance, SNR,
SRS.indication and the MAC keep working, and the float estimate is exported.

Things to keep in mind:

- The ports on one comb share the same REs; they are told apart only by their cyclic shift.
- The UE's timing advance moves in steps of about 0.26 µs, so the channel does not sit at delay 0.
- The street has paths more than 1 µs long, some of them 40 dB below the line of sight.
- One SRS is 32 antennas × 4 ports × 1632 comb REs, every 10 ms.

### Targets

| | fixed point (the default) | target |
|---|---|---|
| `test_srs_est`: 8 rx, 1–4 ports, 20 dB, off-grid multipath | about -20 dB (plain LS) | every case under its gate, -29 to -37 dB |
| `test_srs_est`: no noise | | below -70 dB |
| `nr_srssim` module3: 51 PRB, 8 rx, 4 ports, TDL-A, 20 dB | -25 dB | below -30 dB |
| `nr_srssim` module4: 51 PRB, 8 rx, 1 port, AWGN, 20 dB | -27.5 dB | below -37 dB |
| `nr_srssim` module5: 51 PRB, 8 rx, 2 ports, comb 4, TDL-C, 20 dB | -16 dB | below -27.5 dB |
| End to end: `nmse_vs_truth.py`, median over the walk | | below -30 dB |
| Time per SRS on the GB10, 32 × 4 × 273 PRB | | well under the 10 ms SRS period |

For reference, the fixed-point estimator's NMSE with `nr_srssim`, 32 rx antennas, 4 UE ports, 273 PRB,
TDL-A is -4.1 dB at 0 dB SNR, -23.8 dB at 20 dB and -32.9 dB at 40 dB.

## Measuring

Unit test, against the exact frequency response of a synthetic channel (it fails until
`srs_est_run()` succeeds and meets the gates):

```bash
ninja srs_est_cuda test_srs_est && ctest -R test_srs_est --output-on-failure
```

Link-level simulator, NMSE against the true channel (`-N` is the gate in dB; without the loader
option you get the fixed-point estimator's figure):

```bash
./nr_srssim -R 273 -z 32 -y 4 -g A,l,0 -s 0 -S 40 -N 0 -n 3 --loader.srs_est.shlibversion _cuda
ctest -R nr_srssim.module --output-on-failure
```

End to end, the export against the channel the rfsimulator applied (one complex gain and one delay
fitted per record, for the UE's transmit amplitude and timing):

```bash
cd tools/isac
python3 nmse_vs_truth.py --dump isac.bin --cirdb $DB --gnb-log gnb.log --out nmse.json
```

## Running it

The rfsimulator carries 32 streams of 122.88 Msps between the two ends. A slot no longer fits in
the socket buffers the kernel allows by default, and both ends would deadlock writing:

```bash
sudo sysctl -w net.core.wmem_max=100000000 net.core.rmem_max=100000000
```

Build with CUDA (the GB10 is sm_121):

```bash
cmake -GNinja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_CHANNEL_SIM_CUDA=ON -DENABLE_SRS_CUDA=ON \
      -DCMAKE_CUDA_ARCHITECTURES=121 -DENABLE_TESTS=ON ..
ninja nr-softmodem nr-uesoftmodem rfsimulator srs_est_cuda ldpc params_libconfig dfts
```

Core network: the containers of `ci-scripts/yaml_files/5g_rfsimulator` except the gNB and UE
(`docker compose up -d mysql oai-amf oai-smf oai-upf oai-ext-dn`). The config expects the gNB on the
host, at 192.168.71.129.

gNB and UE (`DB` is the directory with `isac.yaml` and `cir_db.bin`):

```bash
CH="--rfsimulator.[0].cirdb_yaml $DB/isac.yaml --rfsimulator.[0].cirdb_file $DB/cir_db.bin --channelmod.noise_power_dBFS -63"
sudo ./nr-softmodem -O gnb.sa.band78.273prb.rfsim.32x32.isac.conf --rfsim $CH \
     --loader.srs_est.shlibversion _cuda --isac.dump_file isac.bin
sudo ./nr-uesoftmodem -O nrue.uicc.conf -C 3450720000 -r 273 --numerology 1 --band 78 --ssb 1518 --rfsim \
     --rfsimulator.[0].serveraddr 127.0.0.1 --ue-nb-ant-tx 4 --ue-nb-ant-rx 4 \
     --uecap_file targets/PROJECTS/GENERIC-NR-5GC/CONF/uecap_ports4.xml $CH
```

`uecap_ports4.xml` makes the UE advertise 4 SRS ports. Without `--loader.srs_est.shlibversion` the
same setup runs on the fixed-point estimator: the UE attaches and SRS works, but nothing is
exported. With the skeleton as it is, the gNB logs that the module failed and falls back for every
SRS.

The database starts with the link: see "CIR DB: snapshot 0 starts at timestamp" in the gNB log.
It has 5 s of a static scene for the attach, then 30 s of motion. The simulation runs about 60 times
slower than real time on a GB10, so the whole walk takes about 40 minutes; `--isac.min_interval_ms`
and `--isac.max_records` thin or bound the export.

## Limits

- The database snapshots are 10 ms apart: the Doppler of the vehicle echo is not represented.
- The rfsimulator applies the one database to every peer: this is a single-UE setup.
- The UE has been seen to crash in its MAC (`get_pucch_start_symbol_length()` on a NULL PUCCH
  resource) when applying the reconfiguration after an RRC re-establishment. With four DL layers
  allowed the link failed in the ray-traced street (the UE reported rank 3); with one DL layer it
  holds for the whole run.
