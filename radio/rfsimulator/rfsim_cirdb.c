/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <math.h>
#include "common/utils/LOG/log.h"
#include "common/utils/assertions.h"
#include "common/utils/utils.h"
#include "cirdb_db.h"
#include "rfsim_cirdb.h"

struct rfsim_cirdb_s {
  cirdb_db_t *db;
  cirdb_db_info_t info;
  rfsim_cirdb_orientation_t orientation;
  int own_rx;
  double sample_rate;
  void *gpu;
  int64_t last_snapshot;
  /// timestamp of snapshot 0: the first samples received, rounded down to a snapshot boundary
  /// (the peers start at different times, but their snapshot boundaries stay common)
  uint64_t t0;
  bool t0_set;
};

rfsim_cirdb_t *rfsim_cirdb_init(const char *yaml_path,
                                const char *bin_path,
                                int model_id,
                                rfsim_cirdb_orientation_t orientation,
                                int own_rx,
                                double sample_rate,
                                bool use_gpu)
{
  rfsim_cirdb_t *c = calloc_or_fail(1, sizeof(*c));
  c->db = cirdb_db_open(&(cirdb_db_select_t){.yaml_path = yaml_path, .bin_path = bin_path, .model_id = model_id});
  c->info = *cirdb_db_info(c->db);
  AssertFatal(fabs(c->info.fs_hz - sample_rate) < 1e-3 * sample_rate,
              "CIR DB taps are sampled at %.0f Hz but the device runs at %.0f Hz\n",
              c->info.fs_hz,
              sample_rate);
  c->orientation = orientation;
  c->own_rx = own_rx;
  c->sample_rate = sample_rate;
  c->last_snapshot = -1;
  if (orientation == RFSIM_CIRDB_DIRECT)
    AssertFatal(own_rx == c->info.n_rx, "CIR DB used as recorded needs %d rx antennas, not %d\n", c->info.n_rx, own_rx);
  if (orientation == RFSIM_CIRDB_TRANSPOSED)
    AssertFatal(own_rx == c->info.n_tx, "transposed CIR DB needs %d rx antennas, not %d\n", c->info.n_tx, own_rx);
  if (use_gpu) {
#ifdef CHANNEL_SIM_CUDA
    const int n_max = max(c->info.n_tx, c->info.n_rx);
    c->gpu = rfsim_cirdb_gpu_init(n_max, n_max, c->info.L);
#else
    LOG_W(HW, "rfsimulator built without CUDA (ENABLE_CHANNEL_SIM_CUDA), applying the CIR DB on the CPU\n");
#endif
  }
  LOG_I(HW, "rfsimulator: channel from CIR DB, %s\n", c->gpu ? "on the GPU" : "on the CPU");
  return c;
}

int rfsim_cirdb_length(const rfsim_cirdb_t *c)
{
  return c->info.L;
}

void rfsim_cirdb_conv_cpu(const rfsim_cirdb_conv_t *p, c16_t *const *in, int in_offset, int nsamps, cf_t *const *out, int out_offset)
{
  const int L = p->L;
  for (int r = 0; r < p->nrx; r++) {
    cf_t *y = out[r] + out_offset;
    for (int t = 0; t < p->ntx; t++) {
      const cf_t *h = p->taps + (size_t)(r * p->link_stride_rx + t * p->link_stride_tx) * L;
      const c16_t *x = in[t] + in_offset + L - 1;
      for (int n = 0; n < nsamps; n++) {
        float re = 0, im = 0;
        for (int l = 0; l < L; l++) {
          const c16_t xs = x[n - l];
          re += h[l].r * xs.r - h[l].i * xs.i;
          im += h[l].r * xs.i + h[l].i * xs.r;
        }
        y[n].r += re;
        y[n].i += im;
      }
    }
  }
}

/* The database stores link = aarx + n_rx * aatx for its own tx -> rx direction. Used as recorded,
 * our rx antenna r is its rx element and the peer's tx antenna t its tx element; transposed, the
 * roles swap: r is its tx element and t its rx element */
static rfsim_cirdb_conv_t conv_params(const rfsim_cirdb_t *c, int peer_ntx, bool transposed, const cf_t *taps)
{
  const int n_rx = c->info.n_rx;
  if (transposed)
    return (rfsim_cirdb_conv_t){.taps = taps, .L = c->info.L, .ntx = peer_ntx, .nrx = c->own_rx, .link_stride_tx = 1, .link_stride_rx = n_rx};
  return (rfsim_cirdb_conv_t){.taps = taps, .L = c->info.L, .ntx = peer_ntx, .nrx = c->own_rx, .link_stride_tx = n_rx, .link_stride_rx = 1};
}

static bool is_transposed(const rfsim_cirdb_t *c, int peer_ntx)
{
  const bool direct_fits = c->own_rx == c->info.n_rx && peer_ntx == c->info.n_tx;
  const bool transposed_fits = c->own_rx == c->info.n_tx && peer_ntx == c->info.n_rx;
  switch (c->orientation) {
    case RFSIM_CIRDB_DIRECT:
      AssertFatal(direct_fits, "CIR DB is %dx%d, link is %d peer tx x %d own rx\n", c->info.n_tx, c->info.n_rx, peer_ntx, c->own_rx);
      return false;
    case RFSIM_CIRDB_TRANSPOSED:
      AssertFatal(transposed_fits, "CIR DB is %dx%d, link is %d peer tx x %d own rx\n", c->info.n_tx, c->info.n_rx, peer_ntx, c->own_rx);
      return true;
    default:
      AssertFatal(direct_fits != transposed_fits,
                  "CIR DB %dx%d with %d peer tx x %d own rx: %s, set rfsimulator.cirdb_orientation\n",
                  c->info.n_tx,
                  c->info.n_rx,
                  peer_ntx,
                  c->own_rx,
                  direct_fits ? "ambiguous (square link)" : "does not fit either way");
      return transposed_fits;
  }
}

void rfsim_cirdb_apply(rfsim_cirdb_t *c, int peer_ntx, c16_t *const *in, uint64_t ts, int nsamps, cf_t *const *out)
{
  const bool transposed = is_transposed(c, peer_ntx);
  if (!c->t0_set) {
    const uint64_t samples_per_snapshot = llround(c->info.snapshot_dt_s * c->sample_rate);
    c->t0 = ts / samples_per_snapshot * samples_per_snapshot;
    c->t0_set = true;
    LOG_I(HW, "CIR DB: snapshot 0 starts at timestamp %lu\n", c->t0);
  }
  ts -= c->t0;
  int done = 0;
  while (done < nsamps) {
    // a block can straddle a snapshot switch: split it there
    const uint64_t seg_ts = ts + done;
    const uint64_t next = cirdb_db_next_switch(c->db, seg_ts, c->sample_rate);
    const int len = (int)min((uint64_t)(nsamps - done), next - seg_ts);
    const uint32_t s = cirdb_db_snapshot_at(c->db, seg_ts, c->sample_rate);
    if ((int64_t)s != c->last_snapshot) {
      LOG_D(HW, "CIR DB snapshot %u/%d at timestamp %lu\n", s, c->info.S, seg_ts + c->t0);
      c->last_snapshot = s;
    }
    const rfsim_cirdb_conv_t p = conv_params(c, peer_ntx, transposed, cirdb_db_snapshot(c->db, s));
#ifdef CHANNEL_SIM_CUDA
    if (c->gpu)
      rfsim_cirdb_gpu_conv(c->gpu, &p, in, done, len, out, done);
    else
#endif
      rfsim_cirdb_conv_cpu(&p, in, done, len, out, done);
    done += len;
  }
}

void rfsim_cirdb_end(rfsim_cirdb_t *c)
{
  if (!c)
    return;
#ifdef CHANNEL_SIM_CUDA
  if (c->gpu)
    rfsim_cirdb_gpu_end(c->gpu);
#endif
  cirdb_db_close(c->db);
  free(c);
}
