/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/* GPU direct-form convolution of rfsimulator input streams with a CIR DB snapshot: one thread per
 * output sample of one rx antenna, taps and an input tile with its L - 1 history in shared memory */

#include <cuda_runtime.h>
#include <stdio.h>
#include <stdlib.h>
#include "rfsim_cirdb.h"

#define CONV_BLOCK 256

#define CUDA_CHECK(x)                                                                             \
  do {                                                                                            \
    cudaError_t err_ = (x);                                                                       \
    if (err_ != cudaSuccess) {                                                                    \
      fprintf(stderr, "rfsim CIR DB CUDA error %s at %s:%d\n", cudaGetErrorString(err_), __FILE__, __LINE__); \
      abort();                                                                                    \
    }                                                                                             \
  } while (0)

typedef struct {
  int max_ntx, max_nrx, L;
  size_t cap_in; // samples per input stream the device buffers can hold
  size_t cap_out;
  float2 *d_taps; // [nrx][ntx][L], reordered on the host so each (rx, tx) pair is contiguous
  short2 *d_in; // [ntx][cap_in]
  float2 *d_out; // [nrx][cap_out]
  float2 *h_taps;
  const cf_t *last_taps; // snapshot currently on the device
  rfsim_cirdb_conv_t last_p;
  cudaStream_t stream;
} rfsim_cirdb_gpu_t;

__global__ void cirdb_conv_kernel(const float2 *__restrict__ taps,
                                  const short2 *__restrict__ in,
                                  size_t in_stride,
                                  float2 *__restrict__ out,
                                  size_t out_stride,
                                  int ntx,
                                  int L,
                                  int nsamps)
{
  extern __shared__ float2 smem[];
  float2 *s_taps = smem; // [L]
  float2 *s_x = smem + L; // [CONV_BLOCK + L - 1]
  const int r = blockIdx.y;
  const int n0 = blockIdx.x * CONV_BLOCK;
  const int n = n0 + threadIdx.x;
  float re = 0, im = 0;
  for (int t = 0; t < ntx; t++) {
    const float2 *h = taps + ((size_t)r * ntx + t) * L;
    for (int i = threadIdx.x; i < L; i += CONV_BLOCK)
      s_taps[i] = h[i];
    // input sample index j of the tile maps to stream sample n0 + j; output n uses [n, n + L - 1]
    const short2 *x = in + t * in_stride;
    const int tile = CONV_BLOCK + L - 1;
    for (int j = threadIdx.x; j < tile; j += CONV_BLOCK) {
      const int k = n0 + j;
      const short2 v = k < nsamps + L - 1 ? x[k] : make_short2(0, 0);
      s_x[j] = make_float2(v.x, v.y);
    }
    __syncthreads();
    if (n < nsamps) {
      const float2 *xs = s_x + threadIdx.x + L - 1;
#pragma unroll 8
      for (int l = 0; l < L; l++) {
        const float2 hl = s_taps[l];
        const float2 xl = xs[-l];
        re += hl.x * xl.x - hl.y * xl.y;
        im += hl.x * xl.y + hl.y * xl.x;
      }
    }
    __syncthreads();
  }
  if (n < nsamps) {
    float2 *y = out + r * out_stride + n;
    y->x = re;
    y->y = im;
  }
}

extern "C" void *rfsim_cirdb_gpu_init(int max_ntx, int max_nrx, int L)
{
  rfsim_cirdb_gpu_t *g = (rfsim_cirdb_gpu_t *)calloc(1, sizeof(*g));
  g->max_ntx = max_ntx;
  g->max_nrx = max_nrx;
  g->L = L;
  CUDA_CHECK(cudaStreamCreateWithFlags(&g->stream, cudaStreamNonBlocking));
  CUDA_CHECK(cudaMalloc(&g->d_taps, sizeof(float2) * max_ntx * max_nrx * L));
  CUDA_CHECK(cudaMallocHost(&g->h_taps, sizeof(float2) * max_ntx * max_nrx * L));
  return g;
}

static void ensure_capacity(rfsim_cirdb_gpu_t *g, size_t in_len, size_t out_len)
{
  if (in_len > g->cap_in) {
    cudaFree(g->d_in);
    g->cap_in = in_len * 2;
    CUDA_CHECK(cudaMalloc(&g->d_in, sizeof(short2) * g->max_ntx * g->cap_in));
  }
  if (out_len > g->cap_out) {
    cudaFree(g->d_out);
    g->cap_out = out_len * 2;
    CUDA_CHECK(cudaMalloc(&g->d_out, sizeof(float2) * g->max_nrx * g->cap_out));
  }
}

extern "C" void rfsim_cirdb_gpu_conv(void *gpu,
                                     const rfsim_cirdb_conv_t *p,
                                     c16_t *const *in,
                                     int in_offset,
                                     int nsamps,
                                     cf_t *const *out,
                                     int out_offset)
{
  rfsim_cirdb_gpu_t *g = (rfsim_cirdb_gpu_t *)gpu;
  const int L = p->L;
  if (p->ntx > g->max_ntx || p->nrx > g->max_nrx || L != g->L) {
    fprintf(stderr, "rfsim CIR DB GPU: %d tx x %d rx x %d taps exceeds %d x %d x %d\n", p->ntx, p->nrx, L, g->max_ntx, g->max_nrx, g->L);
    abort();
  }
  const size_t in_len = nsamps + L - 1;
  ensure_capacity(g, in_len, nsamps);

  // taps change only on a snapshot switch (or a new peer shape): reorder to [rx][tx][L] and upload
  if (p->taps != g->last_taps || p->ntx != g->last_p.ntx || p->nrx != g->last_p.nrx
      || p->link_stride_tx != g->last_p.link_stride_tx || p->link_stride_rx != g->last_p.link_stride_rx) {
    CUDA_CHECK(cudaStreamSynchronize(g->stream)); // h_taps may still be read by a pending copy
    for (int r = 0; r < p->nrx; r++)
      for (int t = 0; t < p->ntx; t++) {
        const cf_t *src = p->taps + (size_t)(r * p->link_stride_rx + t * p->link_stride_tx) * L;
        float2 *dst = g->h_taps + ((size_t)r * p->ntx + t) * L;
        for (int l = 0; l < L; l++)
          dst[l] = make_float2(src[l].r, src[l].i);
      }
    CUDA_CHECK(cudaMemcpyAsync(g->d_taps, g->h_taps, sizeof(float2) * p->ntx * p->nrx * L, cudaMemcpyHostToDevice, g->stream));
    g->last_taps = p->taps;
    g->last_p = *p;
  }

  for (int t = 0; t < p->ntx; t++)
    CUDA_CHECK(cudaMemcpyAsync(g->d_in + t * g->cap_in, in[t] + in_offset, sizeof(short2) * in_len, cudaMemcpyHostToDevice, g->stream));

  const dim3 grid((nsamps + CONV_BLOCK - 1) / CONV_BLOCK, p->nrx);
  const size_t smem = sizeof(float2) * (L + CONV_BLOCK + L - 1);
  cirdb_conv_kernel<<<grid, CONV_BLOCK, smem, g->stream>>>(g->d_taps, g->d_in, g->cap_in, g->d_out, g->cap_out, p->ntx, L, nsamps);
  CUDA_CHECK(cudaGetLastError());

  // accumulate into the caller's buffers: bring the result back and add on the host
  static __thread float2 *h_out = NULL;
  static __thread size_t h_out_cap = 0;
  const size_t need = (size_t)p->nrx * nsamps;
  if (need > h_out_cap) {
    if (h_out)
      cudaFreeHost(h_out);
    h_out_cap = need * 2;
    CUDA_CHECK(cudaMallocHost(&h_out, sizeof(float2) * h_out_cap));
  }
  CUDA_CHECK(cudaMemcpy2DAsync(h_out,
                               sizeof(float2) * nsamps,
                               g->d_out,
                               sizeof(float2) * g->cap_out,
                               sizeof(float2) * nsamps,
                               p->nrx,
                               cudaMemcpyDeviceToHost,
                               g->stream));
  CUDA_CHECK(cudaStreamSynchronize(g->stream));
  for (int r = 0; r < p->nrx; r++) {
    cf_t *y = out[r] + out_offset;
    const float2 *src = h_out + (size_t)r * nsamps;
    for (int n = 0; n < nsamps; n++) {
      y[n].r += src[n].x;
      y[n].i += src[n].y;
    }
  }
}

extern "C" void rfsim_cirdb_gpu_end(void *gpu)
{
  rfsim_cirdb_gpu_t *g = (rfsim_cirdb_gpu_t *)gpu;
  if (!g)
    return;
  cudaStreamSynchronize(g->stream);
  cudaFree(g->d_taps);
  cudaFree(g->d_in);
  cudaFree(g->d_out);
  cudaFreeHost(g->h_taps);
  cudaStreamDestroy(g->stream);
  free(g);
}
