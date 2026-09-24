/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common/utils/LOG/log.h"
#include "common/utils/assertions.h"
#include "common/utils/utils.h"
#include "cirdb_db.h"
#include "cirdb_yaml.h"

struct cirdb_db_s {
  cirdb_db_info_t info;
  void *map;
  size_t map_len;
  const cf_t *taps; ///< start of the selected entry inside map
  size_t snapshot_len; ///< complex taps per snapshot
};

cirdb_db_t *cirdb_db_open(const cirdb_db_select_t *sel)
{
  AssertFatal(sel && sel->yaml_path && sel->bin_path, "CIR DB needs both a YAML and a binary file\n");
  cirdb_select_req_t req = {.want_model_id = sel->model_id,
                            .want_tx = sel->n_tx,
                            .want_rx = sel->n_rx,
                            .want_ds_ns = -1,
                            .want_speed_mps = -1,
                            .want_aoa_deg = 0,
                            .allow_shape_swap = 0,
                            .w_ds = 1.0f,
                            .w_speed = 0.2f,
                            .yaml_path = sel->yaml_path};
  cirdb_entry_meta_t m = {0};
  AssertFatal(cirdb_yaml_select(&req, &m) > 0,
              "no entry with model_id %d and shape %dx%d in CIR DB %s\n",
              sel->model_id,
              sel->n_tx,
              sel->n_rx,
              sel->yaml_path);
  AssertFatal(m.pair_order == 0, "CIR DB entry has pair_order %d, only 0 (link = aarx + n_rx * aatx) is supported\n", m.pair_order);
  AssertFatal(m.n_tx > 0 && m.n_rx > 0 && m.L > 0 && m.S > 0 && m.fs_hz > 0 && m.snapshot_dt_s > 0,
              "invalid CIR DB entry: %dx%d L %d S %d fs %f dt %f\n",
              m.n_tx,
              m.n_rx,
              m.L,
              m.S,
              m.fs_hz,
              m.snapshot_dt_s);

  cirdb_db_t *db = calloc_or_fail(1, sizeof(*db));
  db->info = (cirdb_db_info_t){.model_id = m.model_id,
                               .n_tx = m.n_tx,
                               .n_rx = m.n_rx,
                               .L = m.L,
                               .S = m.S,
                               .fs_hz = m.fs_hz,
                               .snapshot_dt_s = m.snapshot_dt_s};
  db->snapshot_len = (size_t)m.n_tx * m.n_rx * m.L;
  const uint64_t entry_bytes = (uint64_t)m.S * db->snapshot_len * sizeof(cf_t);
  AssertFatal(m.nbytes == 0 || m.nbytes == entry_bytes,
              "CIR DB entry size %lu does not match its dimensions (%lu bytes)\n",
              m.nbytes,
              entry_bytes);

  int fd = open(sel->bin_path, O_RDONLY);
  AssertFatal(fd >= 0, "cannot open CIR DB %s: %s\n", sel->bin_path, strerror(errno));
  struct stat st;
  AssertFatal(fstat(fd, &st) == 0, "cannot stat %s: %s\n", sel->bin_path, strerror(errno));
  AssertFatal((uint64_t)st.st_size >= m.offset_bytes + entry_bytes,
              "CIR DB %s is %ld bytes, entry needs %lu bytes at offset %lu\n",
              sel->bin_path,
              st.st_size,
              entry_bytes,
              m.offset_bytes);
  db->map_len = st.st_size;
  db->map = mmap(NULL, db->map_len, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
  close(fd);
  AssertFatal(db->map != MAP_FAILED, "cannot map CIR DB %s: %s\n", sel->bin_path, strerror(errno));
  db->taps = (const cf_t *)((const char *)db->map + m.offset_bytes);

  LOG_I(HW,
        "CIR DB %s: model %d, %d tx x %d rx, %d taps at %.0f Hz, %d snapshots every %.3f ms (%.1f s)\n",
        sel->bin_path,
        m.model_id,
        m.n_tx,
        m.n_rx,
        m.L,
        m.fs_hz,
        m.S,
        m.snapshot_dt_s * 1e3,
        m.S * m.snapshot_dt_s);
  return db;
}

const cirdb_db_info_t *cirdb_db_info(const cirdb_db_t *db)
{
  return &db->info;
}

const cf_t *cirdb_db_snapshot(const cirdb_db_t *db, uint32_t s)
{
  DevAssert(s < (uint32_t)db->info.S);
  return db->taps + (size_t)s * db->snapshot_len;
}

/* snapshot_dt_s in samples, kept in double: 10 ms at 122.88 MHz is exactly 1228800 samples, but a
 * database recorded at another rate would give a fractional count, which must not drift */
static uint64_t snapshot_step(const cirdb_db_t *db, uint64_t ts, double sample_rate)
{
  return (uint64_t)floor((double)ts / (db->info.snapshot_dt_s * sample_rate));
}

uint32_t cirdb_db_snapshot_at(const cirdb_db_t *db, uint64_t ts, double sample_rate)
{
  return snapshot_step(db, ts, sample_rate) % db->info.S;
}

uint64_t cirdb_db_next_switch(const cirdb_db_t *db, uint64_t ts, double sample_rate)
{
  const double samples_per_snapshot = db->info.snapshot_dt_s * sample_rate;
  uint64_t next = (uint64_t)ceil((snapshot_step(db, ts, sample_rate) + 1) * samples_per_snapshot);
  // floating point rounding: the switch must be strictly after ts and land on the next step
  while (next <= ts)
    next++;
  while (next > ts + 1 && snapshot_step(db, next - 1, sample_rate) != snapshot_step(db, ts, sample_rate))
    next--;
  return next;
}

void cirdb_db_close(cirdb_db_t *db)
{
  if (!db)
    return;
  munmap(db->map, db->map_len);
  free(db);
}
