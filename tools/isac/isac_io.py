# SPDX-License-Identifier: LicenseRef-CSSL-1.0
"""Readers for the ISAC demo: the gNB's SRS estimate dump (--isac.dump_file), the CIR database the
rfsimulator replays (cirdb YAML + binary) and the ray tracer's ground truth (truth.npz, array.json).

Record layout: see openair1/PHY/NR_ESTIMATION/srs_est/isac_dump.h.
"""

import json
import re
import struct
from dataclasses import dataclass

import numpy as np
import yaml

HEADER_FMT = "<4sHHIIIHHHHHHHHHHi4i4iiQfffdddI"
HEADER_FIELDS = ("magic version header_bytes record_index frame slot rnti nb_rx n_ports M K_TC n_symb "
                 "first_symbol N_RB N_fft estimator first_sc k0_0 k0_1 k0_2 k0_3 n_cs_0 n_cs_1 n_cs_2 n_cs_3 "
                 "n_cs_max slot_timestamp noise_var signal_power c16_scale scs_hz fs_hz fc_hz payload_bytes").split()
HEADER_SIZE = struct.calcsize(HEADER_FMT)


@dataclass
class SrsRecord:
    hdr: dict
    h: np.ndarray  # [port][antenna][M] complex64, at the comb REs

    def subcarriers(self, port):
        """carrier subcarrier index of every comb RE of `port`, from the lowest subcarrier"""
        h = self.hdr
        return h["first_sc"] + h["k0"][port] + h["K_TC"] * np.arange(h["M"])

    def freq_offsets_hz(self, port):
        """frequency of every comb RE of `port` relative to the carrier centre (the DC subcarrier)"""
        h = self.hdr
        return (self.subcarriers(port) - 6 * h["N_RB"]) * h["scs_hz"]

    def srs_timestamp(self):
        """radio sample timestamp of the first SRS symbol (slot start plus its symbols)"""
        h = self.hdr
        slot_samples = h["fs_hz"] * 1e-3 / (h["scs_hz"] / 15e3)
        return h["slot_timestamp"] + h["first_symbol"] / 14.0 * slot_samples


def read_srs_dump(path):
    records = []
    with open(path, "rb") as f:
        while True:
            raw = f.read(HEADER_SIZE)
            if len(raw) < HEADER_SIZE:
                break
            vals = struct.unpack(HEADER_FMT, raw)
            hdr = dict(zip(HEADER_FIELDS, vals))
            assert hdr["magic"] == b"ISAC", f"bad magic in {path}"
            assert hdr["header_bytes"] == HEADER_SIZE, "record header size mismatch: rebuild the tools"
            hdr["k0"] = [hdr.pop(f"k0_{i}") for i in range(4)]
            hdr["n_cs"] = [hdr.pop(f"n_cs_{i}") for i in range(4)]
            payload = f.read(hdr["payload_bytes"])
            if len(payload) < hdr["payload_bytes"]:
                break  # truncated last record of a running gNB
            h = np.frombuffer(payload, dtype=np.complex64).reshape(hdr["n_ports"], hdr["nb_rx"], hdr["M"])
            records.append(SrsRecord(hdr, h))
    return records


class CirDb:
    """The replayed channel: taps[snapshot][link][tap], link = aarx + n_rx * aatx, tx = gNB element"""

    def __init__(self, directory, yaml_name="isac.yaml", bin_name="cir_db.bin", model_id=6):
        with open(f"{directory}/{yaml_name}") as f:
            entries = yaml.safe_load(f)["entries"]
        e = next(x for x in entries if x["model_id"] == model_id)
        self.meta = e
        self.n_tx, self.n_rx, self.L, self.S = e["n_tx"], e["n_rx"], e["L"], e["S"]
        self.fs = float(e["fs_hz"])
        self.dt = float(e["snapshot_dt_s"])
        self.taps = np.memmap(f"{directory}/{bin_name}", dtype=np.complex64, mode="r", offset=e["offset_bytes"],
                              shape=(self.S, self.n_tx * self.n_rx, self.L))

    def snapshot_at(self, timestamp, t0):
        """snapshot the rfsimulator applied at radio timestamp `timestamp`, t0 being its snapshot 0"""
        return int((timestamp - t0) // round(self.dt * self.fs)) % self.S

    def uplink_response(self, s, freqs_hz, gnb_ant, ue_ant):
        """H(f) from UE element ue_ant to gNB element gnb_ant at snapshot s (TDD reciprocity: the
        database's downlink link, same taps)"""
        link = ue_ant + self.n_rx * gnb_ant
        j = np.arange(self.L)
        return self.taps[s, link] @ np.exp(-2j * np.pi * np.outer(j, freqs_hz) / self.fs)


def cirdb_t0_from_log(gnb_log):
    """rfsimulator's snapshot origin, as it logs it on the gNB"""
    with open(gnb_log, errors="replace") as f:
        for line in f:
            m = re.search(r"CIR DB: snapshot 0 starts at timestamp (\d+)", line)
            if m:
                return int(m.group(1))
    raise RuntimeError(f"no 'CIR DB: snapshot 0' line in {gnb_log}")


def load_truth(directory):
    t = np.load(f"{directory}/truth.npz", allow_pickle=True)
    truth = {k: t[k] for k in t.files}
    truth["object_names"] = {int(k): v for k, v in json.loads(str(truth["object_names"])).items()}
    return truth


def load_array(directory):
    with open(f"{directory}/array.json") as f:
        return json.load(f)
