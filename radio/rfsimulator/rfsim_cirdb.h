/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*! \file rfsim_cirdb.h
 * \brief rfsimulator channel from a CIR database (e.g. a ray-traced scene).
 *
 * The database holds one direction of the link (n_tx elements of one node to n_rx elements of the
 * other). Both ends apply it to what they receive: the end whose antennas are the database's rx
 * elements uses it as recorded, the other end uses the transpose (TDD reciprocity, same carrier).
 * The snapshot is selected from the sample timestamp, which both ends share, counted from the
 * first samples received (rounded down to a snapshot boundary): the recording starts when the
 * link does, not when the first node started.
 */

#ifndef RFSIM_CIRDB_H
#define RFSIM_CIRDB_H

#include <stdbool.h>
#include <stdint.h>
#include "common/platform_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { RFSIM_CIRDB_AUTO, RFSIM_CIRDB_DIRECT, RFSIM_CIRDB_TRANSPOSED } rfsim_cirdb_orientation_t;

typedef struct rfsim_cirdb_s rfsim_cirdb_t;

rfsim_cirdb_t *rfsim_cirdb_init(const char *yaml_path,
                                const char *bin_path,
                                int model_id,
                                rfsim_cirdb_orientation_t orientation,
                                int own_rx,
                                double sample_rate,
                                bool use_gpu);
/// number of taps: the input of rfsim_cirdb_apply() starts that many samples minus one before ts
int rfsim_cirdb_length(const rfsim_cirdb_t *c);
/**
 * Adds the channel output for one peer to out.
 * @param in    peer_ntx input streams, each nsamps + length - 1 samples, starting at ts - (length - 1)
 * @param ts    timestamp of the first output sample
 * @param out   own_rx output streams of nsamps samples, accumulated into
 */
void rfsim_cirdb_apply(rfsim_cirdb_t *c, int peer_ntx, c16_t *const *in, uint64_t ts, int nsamps, cf_t *const *out);
void rfsim_cirdb_end(rfsim_cirdb_t *c);

/* Direct-form convolution of one block against one snapshot, shared by the CPU and GPU paths:
 * out[r][n] += sum_t sum_l taps[link(t, r) * L + l] * in[t][n + L - 1 - l]
 * with link(t, r) = r * link_stride_rx + t * link_stride_tx */
typedef struct {
  const cf_t *taps;
  int L;
  int ntx;
  int nrx;
  int link_stride_tx;
  int link_stride_rx;
} rfsim_cirdb_conv_t;

void rfsim_cirdb_conv_cpu(const rfsim_cirdb_conv_t *p, c16_t *const *in, int in_offset, int nsamps, cf_t *const *out, int out_offset);
#ifdef CHANNEL_SIM_CUDA
void *rfsim_cirdb_gpu_init(int max_ntx, int max_nrx, int L);
void rfsim_cirdb_gpu_conv(void *gpu, const rfsim_cirdb_conv_t *p, c16_t *const *in, int in_offset, int nsamps, cf_t *const *out, int out_offset);
void rfsim_cirdb_gpu_end(void *gpu);
#endif

#ifdef __cplusplus
}
#endif

#endif
