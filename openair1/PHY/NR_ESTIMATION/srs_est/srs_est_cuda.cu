/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/* GPU SRS channel estimator: the module the gNB loads with --loader.srs_est.shlibversion _cuda.
 *
 * This is the skeleton to fill in: it is loaded, initialized and called exactly like a finished
 * module, but srs_est_run() does not estimate anything yet and reports a failure, so the gNB falls
 * back to its fixed-point estimator for every SRS (and exports nothing). See srs_est_interface.h
 * for what goes in and what has to come out, doc/isac_srs_demo.md for the task and how to measure
 * the result. */

#include <cuda_runtime.h>
#include <stdio.h>

extern "C" {
#include "srs_est_interface.h"
}

#define CK(x)                                                                                     \
  do {                                                                                            \
    cudaError_t e_ = (x);                                                                         \
    if (e_ != cudaSuccess) {                                                                      \
      fprintf(stderr, "srs_est_cuda: %s at %s:%d\n", cudaGetErrorString(e_), __FILE__, __LINE__); \
      return -1;                                                                                  \
    }                                                                                             \
  } while (0)

/* the largest problem srs_est_init() was asked to support: size your buffers, plans and handles
 * for it once, srs_est_run() is called for every SRS */
static struct {
  int max_rx, max_ports, max_M, max_K;
  cudaStream_t stream;
  bool warned;
} g;

static int check_input(const srs_est_in_t *in)
{
  if (in->nb_rx < 1 || in->nb_rx > g.max_rx || in->M < 1 || in->M > g.max_M || in->K_TC > g.max_K)
    return -1;
  if (in->n_ports < 1 || in->n_ports > g.max_ports || in->n_ports > SRS_EST_MAX_PORTS)
    return -1;
  if (in->n_symb < 1 || in->n_symb > SRS_EST_MAX_SYMBOLS || in->n_cs_max < 1)
    return -1;
  return 0;
}

extern "C" int32_t srs_est_init(int max_rx, int max_ports, int max_M, int max_K_TC)
{
  g.max_rx = max_rx;
  g.max_ports = max_ports;
  g.max_M = max_M;
  g.max_K = max_K_TC;
  CK(cudaStreamCreate(&g.stream));
  // TODO: allocate the device buffers, create the FFT plans and solver handles you need
  return 0;
}

extern "C" void srs_est_shutdown(void)
{
  // TODO: free what srs_est_init() allocated
  if (g.stream)
    cudaStreamDestroy(g.stream);
  g.stream = 0;
}

extern "C" int32_t srs_est_run(const srs_est_in_t *in, srs_est_out_t *out)
{
  if (check_input(in) != 0)
    return -1;
  // TODO: estimate the channel of every port at every antenna from in->y and in->rbar, and fill
  // out->h_comb, out->h_full, out->noise_var, out->signal_power and out->timing_offset_s.
  // Return 0 once they are filled: the gNB then uses them instead of its fixed-point estimate.
  (void)out;
  if (!g.warned) {
    fprintf(stderr, "srs_est_cuda: srs_est_run() is not implemented yet\n");
    g.warned = true;
  }
  return -1;
}
