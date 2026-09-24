/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*! \file cirdb_db.h
 * \brief Read-only, memory-mapped access to one entry of a channel impulse response database (CIR DB).
 *
 * The database is a YAML sidecar listing entries and a binary file of little-endian complex float32
 * taps. One entry holds S snapshots of n_tx * n_rx links of L sample-spaced taps, laid out
 * [snapshot][link][tap] with link = aarx + n_rx * aatx. Snapshot s is valid from time s * snapshot_dt_s
 * and the recording loops after S snapshots.
 *
 * Unlike vrtsim's cirdb_provider, the whole entry is mapped at open time: looking up a snapshot is
 * pointer arithmetic, with no I/O on the sample path.
 */

#ifndef CIRDB_DB_H
#define CIRDB_DB_H

#include <stdint.h>
#include "common/platform_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  const char *yaml_path;
  const char *bin_path;
  int model_id; ///< required model id (e.g. 6 = ray-traced ISAC)
  int n_tx; ///< required number of tx elements, or 0 for any
  int n_rx; ///< required number of rx elements, or 0 for any
} cirdb_db_select_t;

typedef struct {
  int model_id;
  int n_tx;
  int n_rx;
  int L; ///< taps per link
  int S; ///< snapshots
  double fs_hz; ///< tap sampling rate
  double snapshot_dt_s; ///< time between snapshots
} cirdb_db_info_t;

typedef struct cirdb_db_s cirdb_db_t;

/// Opens the entry matching sel. Fatal if none matches or the file is shorter than the entry.
cirdb_db_t *cirdb_db_open(const cirdb_db_select_t *sel);
const cirdb_db_info_t *cirdb_db_info(const cirdb_db_t *db);
/// Taps of snapshot s (0 <= s < S): n_tx * n_rx links of L taps, link = aarx + n_rx * aatx
const cf_t *cirdb_db_snapshot(const cirdb_db_t *db, uint32_t s);
/// Snapshot index valid at sample timestamp ts of a device running at sample_rate, looping after S
uint32_t cirdb_db_snapshot_at(const cirdb_db_t *db, uint64_t ts, double sample_rate);
/// First sample timestamp after ts at which the snapshot changes
uint64_t cirdb_db_next_switch(const cirdb_db_t *db, uint64_t ts, double sample_rate);
void cirdb_db_close(cirdb_db_t *db);

#ifdef __cplusplus
}
#endif

#endif
