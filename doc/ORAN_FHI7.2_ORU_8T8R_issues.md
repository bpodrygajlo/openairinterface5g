# OAI O-RU 8T8R 100 MHz: open issues

Issues found while testing the OAI O-RU (`nr-oru`) with 8 TX / 8 RX antennas,
273 PRB at 30 kHz SCS, 9-bit BFP, no beamforming (CI testcase
`container_5g_oai_nr_oru_8x8.xml`), when trying to push throughput beyond the
1T1R CI configuration.

## Test host

All O-RU, O-DU (gNB) and UE processes ran on a single machine:

* AMD Ryzen 9 9955HX, 16 cores / 32 threads, one NUMA node
* Intel X710 (i40e), fronthaul over VFs of the same PF (VF-to-VF switching,
  no cable), O-RU on 2 VFs, O-DU on 2 VFs
* vrtsim between O-RU and UE, OAI 5GC in docker (`cn.yml`)
* CPU split by physical core: O-RU 6 cores, gNB 6 cores, UE 3 cores (plus
  their SMT siblings)

Numbers below are specific to this host; a split over several hosts (as in
CI) may behave differently.

## Results that work

With a 1T1R UE: attach, ping 0% loss both ways, TCP DL 85-95 Mbps, TCP UL
71-75 Mbps, MCS 28 in both directions, DL BLER < 1%, O-RU self-diagnosis
`PASS`.

## 1. UDP DL throughput capped at ~100-110 Mbps

UDP DL saturates at ~100-110 Mbps regardless of offered load. The same cap is
seen with the 1x1 106 PRB O-RU configuration (0% BLER, MCS 28), so it is not
caused by the 8T8R O-RU; the limit is elsewhere in the local path (UE, tun
interface or core network containers) and was not investigated further.

## 2. UL U-plane packets arriving late at the O-DU

The xran `RX Timing` counters on the O-DU report late UL U-plane packets:

| Condition | Late UL packets |
|---|---|
| 1T1R UE, idle | ~0.35% |
| 1T1R UE, full-band UL iperf | ~1-2% |
| 2T2R UE, DL iperf | 3-8% |

The O-RU's UL workers are not the bottleneck: after the BFP speed-up they are
~13% busy (~10-12 us per antenna-symbol with 6 workers), yet the O-RU reports
a mean UL OTA delay of 2-5 symbols and occasionally drops UL jobs "declared
more than 56 symbols ahead (beyond calendar horizon)", i.e. its south
(vrtsim) reader lags wall-clock time. Moving the vrtsim timing thread off the
DPDK busy-poll core's SMT sibling did not help. The late packets correlate
with PUCCH DTX and DL retransmissions; the backlog grows towards the end of
the UL slot, where PUCCH is, but the per-symbol distribution was not
measured.

Late UL was also the main cause of DL BLER before the BFP speed-up: with the
bit-by-bit BFP code, 8 x 273 PRB UL did not fit in the symbol budget and DL
BLER sat around 9%, capping DL MCS at ~15.

## 3. Rank 2 with 8 DL ports: UE PMI not implemented

With `pdsch_AntennaPorts_XP = 2`, `pdsch_AntennaPorts_N1 = 4` and a 2T2R UE,
the UE computes RI 2 but logs `PMI not implemented for 8 ports and rank 2`
and reports CQI 0; rank-2 DL then fails. The OAI UE supports PMI for rank 2
only with 2 and 4 CSI-RS ports (`nr_csi_rs_pmi_2ports`,
`nr_csi_rs_pmi_4ports`); `nr_csi_rs_pmi_8ports` is rank 1 only.

## 4. Rank 2 with 4 DL ports: rank-deficient vrtsim channel

With 4 DL ports (`N1 = 2`) the UE reports CQI 15, RI 2, PMI (8,0), but every
rank-2 transport block fails (BLER 0.375, MCS 0). Without channel modelling,
vrtsim maps O-RU antenna *i* to UE RX antenna *i* only, so the UE never sees
ports 2-3. For the XP = 2 codebook the rank-2 precoder rows for ports 0-1 are
identical for both layers, so the two layers are not separable at the UE.

## 5. vrtsim channel modelling not real-time at 8x2, 273 PRB

A static flat 8x2 channel (`Rayleigh1`, `--vrtsim.chanmod 1`), which would
give the UE a full-rank view of all 8 antennas, cannot run in real time on
this host: 60-140 us per symbol against a 35.7 us budget, 90% of vrtsim TX
samples late, even with 4 threads pinned via `--vrtsim.thread-pool`. The UE
cannot decode PBCH/SIB1.

## 6. Rank 2 with 2 DL ports: collapses under load

With 2 DL ports (`N1 = 1`), ports 0-1 land on the two antennas the UE sees
directly. Idle, the link is clean (RI 2, CQI 15, BLER 0). Under DL iperf it
collapses: TCP DL ~12 Mbps, DL BLER 0.18-0.30, thousands of PUCCH DTX,
UL BLER ~0.26 and 3-8% late UL packets at the O-DU. The UE's own vrtsim TX
lateness rises from 0.32% (1T1R) to 2.2% (2T2R). The UE has only 3 physical
cores on this host for rank-2 decoding at 273 PRB, which is the prime
suspect, but this was not confirmed.

## 7. PRACH "C-Plane Missing - Never Received" counter

The O-RU counts PRACH U-plane attempts for slots without a matching PRACH
C-plane section (`PRACH C-Plane Missing Errors ... Never Received`). The same
counter grows with the 1x1 106 PRB configuration, so it is not specific to
8T8R; attach is not affected.

## 8. CI resource assumptions

The CI compose files give the O-RU cpuset `2-13` (its conf pins RX, south, TX,
2 DL readers and 6 UL workers inside it) and the UE `14-19`, on the same host.
The 1x1 test uses 5 cores for the O-RU; the core count of the CI hosts was not
verified.
