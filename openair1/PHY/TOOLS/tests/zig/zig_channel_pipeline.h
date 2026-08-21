/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#ifndef _ZIG_CHANNEL_PIPELINE_H_
#define _ZIG_CHANNEL_PIPELINE_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Symbols exported from channel_pipeline_zig_kernel.zig. Same buffer
// layout as ../mojo/mojo_channel_pipeline.h: `channel` is a `cf_t*` and
// `tx`/`rx` are `c16_t*` reinterpreted as flat interleaved (r,i)
// float32/int16_t pairs.

// Single-threaded reference kernel: plain nested loops, no manual SIMD.
void zig_channel_convolution_basic(const float *channel,
                                    const int16_t *tx,
                                    int16_t *rx,
                                    int32_t num_input_samples,
                                    int32_t num_samples,
                                    int32_t channel_length,
                                    int32_t nb_tx,
                                    int32_t nb_rx);

// Single-threaded, explicitly vectorized to the build target's native
// float32 SIMD width (`std.simd.suggestVectorLength`) via a manual
// stride-2 gather per tap - the Zig analogue of
// mojo_channel_convolution_simd.
void zig_channel_convolution_simd(const float *channel,
                                   const int16_t *tx,
                                   int16_t *rx,
                                   int32_t num_input_samples,
                                   int32_t num_samples,
                                   int32_t channel_length,
                                   int32_t nb_tx,
                                   int32_t nb_rx);

// Same explicit-SIMD vectorization, but deinterleaves tx into scratch
// real/imag buffers once per call instead of gathering per tap - the Zig
// analogue of mojo_channel_convolution_simd_deint.
void zig_channel_convolution_simd_deint(const float *channel,
                                        const int16_t *tx,
                                        int16_t *rx,
                                        int32_t num_input_samples,
                                        int32_t num_samples,
                                        int32_t channel_length,
                                        int32_t nb_tx,
                                        int32_t nb_rx);

// Deinterleaves `n` complex int16 samples from `tx` (interleaved r,i) into
// separate contiguous `tx_r`/`tx_i` buffers (each length `n`). Mirror of
// mojo_deinterleave_tx - exposed so a caller managing its own thread pool
// can do this once per call, shared across every rx antenna/job.
void zig_deinterleave_tx(const int16_t *tx, int16_t *tx_r, int16_t *tx_i, int32_t n);

// No threading: computes samples [i_start, i_end) of one rx antenna from
// already-deinterleaved tx_r/tx_i (see zig_deinterleave_tx). Mirror of
// mojo_convolve_samples_simd_deint.
void zig_convolve_samples_simd_deint(const float *channel,
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

#ifdef __cplusplus
}
#endif

#endif
