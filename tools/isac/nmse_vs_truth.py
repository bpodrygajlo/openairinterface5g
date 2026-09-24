#!/usr/bin/env python3
# SPDX-License-Identifier: LicenseRef-CSSL-1.0
"""NMSE of the gNB's exported SRS channel estimates against the channel the rfsimulator applied.

For every record, the true uplink response at the record's comb REs comes from the CIR database
snapshot active at the SRS symbol. The estimate differs from it by the UE's transmit amplitude
(one complex gain) and by the timing of the link (the UE's timing advance and the gNB's FFT
window: one delay); both are fitted per record over all antennas, ports and REs, and removed. The
relative delays and the spatial structure, what sensing uses, are not touched by that fit.

  nmse_vs_truth.py --dump isac.bin --cirdb <db dir> --gnb-log gnb.log [--out nmse.json]
"""

import argparse
import json
import sys

import numpy as np

import isac_io


def fit_gain_delay(h_true, h_est, freqs, oversample=64):
    """(alpha, tau) maximizing the fit of alpha exp(-j 2 pi f tau) h_true to h_est; freqs uniform
    per port, h_* [port][antenna][M]"""
    n_ports, _, M = h_est.shape
    df = freqs[0][1] - freqs[0][0]
    n = oversample * M
    corr = np.zeros(n, dtype=complex)
    for p in range(n_ports):
        # sum_a,k conj(h_true) h_est exp(+j 2 pi f_k tau): an inverse DFT over k on a fine tau grid
        x = np.sum(np.conj(h_true[p]) * h_est[p], axis=0)
        c = np.fft.ifft(x, n) * n
        corr += c * np.exp(2j * np.pi * freqs[p][0] * np.arange(n) / (n * df))
    i = int(np.argmax(np.abs(corr)))
    # parabolic refinement of the peak
    y0, y1, y2 = np.abs(corr[(i - 1) % n]), np.abs(corr[i]), np.abs(corr[(i + 1) % n])
    frac = 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2) if (y0 - 2 * y1 + y2) != 0 else 0
    tau = ((i + frac) % n) / (n * df)
    if tau > 0.5 / df:
        tau -= 1 / df
    num = 0j
    den = 0.0
    for p in range(n_ports):
        g = h_true[p] * np.exp(-2j * np.pi * freqs[p] * tau)
        num += np.sum(np.conj(g) * h_est[p])
        den += np.sum(np.abs(g) ** 2)
    return num / den, tau


def record_nmse(rec, db, t0):
    hdr = rec.hdr
    s = db.snapshot_at(rec.srs_timestamp(), t0)
    freqs = [rec.freq_offsets_hz(p) for p in range(hdr["n_ports"])]
    h_true = np.array([[db.uplink_response(s, freqs[p], a, p) for a in range(hdr["nb_rx"])] for p in range(hdr["n_ports"])])
    alpha, tau = fit_gain_delay(h_true, rec.h, freqs)
    err = 0.0
    ref = 0.0
    for p in range(hdr["n_ports"]):
        g = alpha * h_true[p] * np.exp(-2j * np.pi * freqs[p] * tau)
        err += np.sum(np.abs(rec.h[p] - g) ** 2)
        ref += np.sum(np.abs(g) ** 2)
    # the estimator's own view of its accuracy: noise per RE against the channel power
    snr_db = 10 * np.log10(hdr["signal_power"] / hdr["noise_var"]) if hdr["noise_var"] > 0 else np.inf
    return {
        "record": hdr["record_index"],
        "frame": hdr["frame"],
        "slot": hdr["slot"],
        "snapshot": s,
        "nmse_db": float(10 * np.log10(err / ref)),
        "gain_db": float(20 * np.log10(abs(alpha))),
        "delay_ns": float(tau * 1e9),
        "snr_db": float(snr_db),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dump", required=True)
    ap.add_argument("--cirdb", required=True)
    ap.add_argument("--gnb-log", help="gNB log, for the CIR DB snapshot origin")
    ap.add_argument("--t0", type=int, help="CIR DB snapshot origin (instead of --gnb-log)")
    ap.add_argument("--out", help="write per-record results and a summary to this JSON file")
    ap.add_argument("--gate-db", type=float, help="exit 1 unless the median NMSE is below this")
    args = ap.parse_args()

    t0 = args.t0 if args.t0 is not None else isac_io.cirdb_t0_from_log(args.gnb_log)
    db = isac_io.CirDb(args.cirdb)
    all_records = isac_io.read_srs_dump(args.dump)
    # no significant delay bin: the estimator found no SRS (e.g. before the UE applies the config)
    records = [r for r in all_records if r.hdr["signal_power"] > 0]
    if not records:
        sys.exit(f"no records with an SRS in {args.dump}")
    results = [record_nmse(r, db, t0) for r in records]
    nmse = np.array([r["nmse_db"] for r in results])
    summary = {
        "records": len(results),
        "records_without_srs": len(all_records) - len(records),
        "nmse_db_median": float(np.median(nmse)),
        "nmse_db_p10": float(np.percentile(nmse, 10)),
        "nmse_db_p90": float(np.percentile(nmse, 90)),
        "nmse_db_worst": float(nmse.max()),
        "snr_db_median": float(np.median([r["snr_db"] for r in results])),
        "snapshots": [int(results[0]["snapshot"]), int(results[-1]["snapshot"])],
    }
    print(json.dumps(summary, indent=1))
    for r in results[:: max(1, len(results) // 20)]:
        print(f"record {r['record']:5d} snapshot {r['snapshot']:5d}: NMSE {r['nmse_db']:7.2f} dB, "
              f"SNR {r['snr_db']:5.1f} dB, delay {r['delay_ns']:7.2f} ns, gain {r['gain_db']:6.1f} dB")
    if args.out:
        with open(args.out, "w") as f:
            json.dump({"summary": summary, "records": results}, f, indent=1)
    if args.gate_db is not None and not summary["nmse_db_median"] < args.gate_db:
        print(f"median NMSE {summary['nmse_db_median']:.2f} dB is not below {args.gate_db} dB")
        sys.exit(1)


if __name__ == "__main__":
    main()
