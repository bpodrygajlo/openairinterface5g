/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef _MOJO_CHANNEL_PIPELINE_H_
#define _MOJO_CHANNEL_PIPELINE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Symbols exported (via Mojo's `@export(ABI="C")`) from
// channel_pipeline_mojo_kernel.mojo. `channel` is a `cf_t*` and `tx`/`rx`
// are `c16_t*` reinterpreted as flat interleaved (r,i) float32/int16_t
// pairs - `cf_t`/`c16_t` have no padding, so the layouts are identical.
//
// channel: nb_rx*nb_tx*channel_length complex taps, tap l of (tx_ant,
//          rx_ant) at channel[(tx_ant + nb_tx*rx_ant)*channel_length + l]
// tx:      nb_tx*num_input_samples complex samples, antenna-major
// rx:      nb_rx*num_samples complex samples (output), antenna-major

// Single-threaded reference kernel: plain nested loops, no manual SIMD.
void mojo_channel_convolution_basic(const float *channel,
                                     const int16_t *tx,
                                     int16_t *rx,
                                     int32_t num_input_samples,
                                     int32_t num_samples,
                                     int32_t channel_length,
                                     int32_t nb_tx,
                                     int32_t nb_rx);

// Single-threaded, explicitly vectorized to the build target's native
// float32 SIMD width (16 lanes/AVX-512 on this machine) instead of relying
// on what the compiler's auto-vectorizer finds on its own for
// mojo_channel_convolution_basic (a narrower 2-lane trick - see
// mojo/README.md). Values can differ from mojo_channel_convolution_basic by
// +-1 LSB on the int16 output: floating-point re-association/FMA-fusion
// noise from a different instruction sequence computing the same sum, not
// a bug (verified against a scalar reference).
void mojo_channel_convolution_simd(const float *channel,
                                    const int16_t *tx,
                                    int16_t *rx,
                                    int32_t num_input_samples,
                                    int32_t num_samples,
                                    int32_t channel_length,
                                    int32_t nb_tx,
                                    int32_t nb_rx);

// Single-threaded, same explicit-SIMD vectorization as
// mojo_channel_convolution_simd, but deinterleaves tx into scratch real/imag
// buffers once up front instead of gathering (r,i) apart out of interleaved
// memory on every tap of every sample block - a plain contiguous load is
// dramatically cheaper than a strided one per element, and the gather here
// was being repeated channel_length*nb_tx times over the same data. ~4-7x
// faster than mojo_channel_convolution_simd on this machine (see
// mojo/README.md) and bit-for-bit identical to it (verified) - the win is
// purely a memory-access pattern change, not a numerical one.
void mojo_channel_convolution_simd_deint(const float *channel,
                                          const int16_t *tx,
                                          int16_t *rx,
                                          int32_t num_input_samples,
                                          int32_t num_samples,
                                          int32_t channel_length,
                                          int32_t nb_tx,
                                          int32_t nb_rx);

// Deinterleaves `n` complex int16 samples from `tx` (interleaved r,i) into
// separate contiguous `tx_r`/`tx_i` buffers (each length `n`). Exposed so a
// caller managing its own thread pool (e.g. BM_channel_convolution_mojo_tpool_simd_deint)
// can do this once per call, on the dispatching thread, before farming
// per-shard jobs out to the pool - the deinterleave is shared work across
// every rx antenna/job, so it must not be redone per job.
void mojo_deinterleave_tx(const int16_t *tx, int16_t *tx_r, int16_t *tx_i, int32_t n);

// No threading: computes samples [i_start, i_end) of one rx antenna from
// already-deinterleaved tx_r/tx_i (see mojo_deinterleave_tx).
void mojo_convolve_samples_simd_deint(const float *channel,
                                      const int16_t *tx_r,
                                      const int16_t *tx_i,
                                      int16_t *rx,
                                      int32_t num_input_samples,
                                      int32_t num_samples,
                                      int32_t channel_length,
                                      int32_t nb_tx,
                                      int32_t rx_ant,
                                      int32_t i_start,
                                      int32_t i_end);

// Multithreaded kernel: Mojo spawns/joins up to `max_threads` OS threads
// (via pthread_create/pthread_join) and splits the rx antennas into
// contiguous shards, one per thread.
void mojo_channel_convolution_parallel(const float *channel,
                                        const int16_t *tx,
                                        int16_t *rx,
                                        int32_t num_input_samples,
                                        int32_t num_samples,
                                        int32_t channel_length,
                                        int32_t nb_tx,
                                        int32_t nb_rx,
                                        int32_t max_threads);

// No threading at all: computes rx antennas [rx_start, rx_end) only. Meant
// to be called concurrently, once per shard, from an externally-managed
// (C-side) thread pool.
void mojo_convolve_range(const float *channel,
                          const int16_t *tx,
                          int16_t *rx,
                          int32_t num_input_samples,
                          int32_t num_samples,
                          int32_t channel_length,
                          int32_t nb_tx,
                          int32_t rx_start,
                          int32_t rx_end);

// No threading at all: computes samples [i_start, i_end) of one rx antenna.
// Used by BM_channel_convolution_mojo_tpool to split each antenna into
// ceil(num_threads/nb_rx) jobs, mirroring channel_pipeline.c's job split so
// the pool is fully utilized regardless of nb_rx.
void mojo_convolve_samples(const float *channel,
                           const int16_t *tx,
                           int16_t *rx,
                           int32_t num_input_samples,
                           int32_t num_samples,
                           int32_t channel_length,
                           int32_t nb_tx,
                           int32_t rx_ant,
                           int32_t i_start,
                           int32_t i_end);

// Explicit-SIMD counterpart to mojo_convolve_samples, for
// BM_channel_convolution_mojo_tpool_simd.
void mojo_convolve_samples_simd(const float *channel,
                                const int16_t *tx,
                                int16_t *rx,
                                int32_t num_input_samples,
                                int32_t num_samples,
                                int32_t channel_length,
                                int32_t nb_tx,
                                int32_t rx_ant,
                                int32_t i_start,
                                int32_t i_end);

#ifdef __cplusplus
}
#endif

#endif
