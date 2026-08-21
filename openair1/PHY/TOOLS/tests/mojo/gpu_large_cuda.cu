// Native CUDA counterpart to gpu_large_mojo.mojo - same two kernels
// (basic: direct global-memory reads; tiled: verbatim port of OAI's real
// multipath_channel_kernel from
// ../../../SIMULATION/TOOLS/multipath_channel.cu), same benchmark grid,
// same zero-copy design (plain `malloc`, no cudaMalloc/cudaMemcpy - see
// gpu_large_mojo.mojo's header for why that's correct on this ATS
// hardware). Build: nvcc -O3 -arch=sm_121 gpu_large_cuda.cu -o gpu_large_cuda
// (adjust -arch for the target GPU's compute capability).
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cuda_runtime.h>

__global__ void conv_basic_kernel(const float *__restrict__ channel,
                                  const int16_t *__restrict__ tx,
                                  int16_t *__restrict__ rx,
                                  int num_input_samples, int num_samples,
                                  int channel_length, int nb_tx, int nb_rx) {
  int tid = blockIdx.x * blockDim.x + threadIdx.x;
  long total = (long)nb_rx * num_samples;
  if (tid >= total) return;
  int rx_ant = tid / num_samples;
  int i = tid % num_samples;
  float acc_r = 0.0f, acc_i = 0.0f;
  for (int tx_ant = 0; tx_ant < nb_tx; tx_ant++) {
    int tx_base = tx_ant * num_input_samples;
    int chan_base = (tx_ant + nb_tx * rx_ant) * channel_length;
    for (int l = 0; l < channel_length; l++) {
      int idx = i + (channel_length - 1) - l;
      float tx_r = tx[(tx_base + idx) * 2];
      float tx_i = tx[(tx_base + idx) * 2 + 1];
      float ch_r = channel[(chan_base + l) * 2];
      float ch_i = channel[(chan_base + l) * 2 + 1];
      acc_r += tx_r * ch_r - tx_i * ch_i;
      acc_i += tx_r * ch_i + tx_i * ch_r;
    }
  }
  rx[(rx_ant * num_samples + i) * 2] = (int16_t)acc_r;
  rx[(rx_ant * num_samples + i) * 2 + 1] = (int16_t)acc_i;
}

__global__ void conv_tiled_kernel(const float *__restrict__ channel,
                                  const int16_t *__restrict__ tx,
                                  int16_t *__restrict__ rx,
                                  int num_input_samples, int num_samples,
                                  int channel_length, int nb_tx, int nb_rx) {
  extern __shared__ float2 tx_shared[];
  const int i = blockIdx.x * blockDim.x + threadIdx.x;
  const int rx_ant = blockIdx.y;
  if (i >= num_samples) return;

  float2 rx_tmp = make_float2(0.0f, 0.0f);
  for (int j = 0; j < nb_tx; j++) {
    const int tid = threadIdx.x;
    const int block_start = blockIdx.x * blockDim.x;
    const int shared_size = blockDim.x + channel_length - 1;
    for (int k = tid; k < shared_size; k += blockDim.x) {
      int load_idx = block_start + k;
      int base = (j * num_input_samples + load_idx) * 2;
      tx_shared[k] = make_float2((float)tx[base], (float)tx[base + 1]);
    }
    __syncthreads();
    int chan_base = (j + nb_tx * rx_ant) * channel_length;
    for (int l = 0; l < channel_length; l++) {
      float2 s = tx_shared[tid + (channel_length - 1) - l];
      int cb = (chan_base + l) * 2;
      float2 w = make_float2(channel[cb], channel[cb + 1]);
      rx_tmp.x += s.x * w.x - s.y * w.y;
      rx_tmp.y += s.x * w.y + s.y * w.x;
    }
    __syncthreads();
  }
  rx[(rx_ant * num_samples + i) * 2] = (int16_t)rx_tmp.x;
  rx[(rx_ant * num_samples + i) * 2 + 1] = (int16_t)rx_tmp.y;
}

static void run_case(int nb_rx, int nb_tx, int num_samples, int channel_length, int iters) {
  int num_input_samples = num_samples + channel_length - 1;

  // plain malloc - zero-copy via ATS, no cudaMalloc/cudaMemcpy at all
  int16_t *h_tx = (int16_t *)malloc((size_t)nb_tx * num_input_samples * 2 * sizeof(int16_t));
  float *h_channel = (float *)malloc((size_t)nb_rx * nb_tx * channel_length * 2 * sizeof(float));
  int16_t *h_rx = (int16_t *)calloc((size_t)nb_rx * num_samples * 2, sizeof(int16_t));
  srand(1234);
  for (long i = 0; i < (long)nb_tx * num_input_samples * 2; i++) h_tx[i] = (rand() % 40) - 20;
  for (long i = 0; i < (long)nb_rx * nb_tx * channel_length * 2; i++) h_channel[i] = (float)((rand() % 40) - 20);

  cudaEvent_t start, stop;
  cudaEventCreate(&start);
  cudaEventCreate(&stop);

  int threads = 512;
  long total = (long)nb_rx * num_samples;
  int blocks = (int)((total + threads - 1) / threads);
  conv_basic_kernel<<<blocks, threads>>>(h_channel, h_tx, h_rx, num_input_samples, num_samples, channel_length, nb_tx, nb_rx);
  cudaError_t err = cudaDeviceSynchronize();
  if (err != cudaSuccess) { printf("CUDA error (basic): %s\n", cudaGetErrorString(err)); return; }

  float best_basic_ms = -1;
  for (int it = 0; it < iters; it++) {
    cudaEventRecord(start);
    conv_basic_kernel<<<blocks, threads>>>(h_channel, h_tx, h_rx, num_input_samples, num_samples, channel_length, nb_tx, nb_rx);
    cudaEventRecord(stop);
    cudaEventSynchronize(stop);
    float ms;
    cudaEventElapsedTime(&ms, start, stop);
    if (best_basic_ms < 0 || ms < best_basic_ms) best_basic_ms = ms;
  }

  dim3 threads_t(512, 1);
  dim3 blocks_t((num_samples + threads_t.x - 1) / threads_t.x, nb_rx);
  size_t shmem = (threads_t.x + channel_length - 1) * sizeof(float2);
  conv_tiled_kernel<<<blocks_t, threads_t, shmem>>>(h_channel, h_tx, h_rx, num_input_samples, num_samples, channel_length, nb_tx, nb_rx);
  err = cudaDeviceSynchronize();
  if (err != cudaSuccess) { printf("CUDA error (tiled): %s\n", cudaGetErrorString(err)); return; }

  float best_tiled_ms = -1;
  for (int it = 0; it < iters; it++) {
    cudaEventRecord(start);
    conv_tiled_kernel<<<blocks_t, threads_t, shmem>>>(h_channel, h_tx, h_rx, num_input_samples, num_samples, channel_length, nb_tx, nb_rx);
    cudaEventRecord(stop);
    cudaEventSynchronize(stop);
    float ms;
    cudaEventElapsedTime(&ms, start, stop);
    if (best_tiled_ms < 0 || ms < best_tiled_ms) best_tiled_ms = ms;
  }

  double msps_basic = num_samples / (best_basic_ms * 1000.0);
  double msps_tiled = num_samples / (best_tiled_ms * 1000.0);
  printf("nb_rx=%-3d nb_tx=%-3d num_samples=%-8d  basic_ns=%-10.0f MSPS=%-9.1f  tiled_ns=%-10.0f MSPS=%-9.1f  tiled/basic=%.2fx\n",
         nb_rx, nb_tx, num_samples, best_basic_ms * 1e6, msps_basic, best_tiled_ms * 1e6, msps_tiled, best_basic_ms / best_tiled_ms);

  free(h_tx); free(h_channel); free(h_rx);
}

int main() {
  int channel_length = 16;

  printf("=== scaling antennas (num_samples=61440) ===\n");
  int ants[] = {1, 2, 4, 8, 16, 32, 64};
  for (int a : ants) run_case(a, a, 61440, channel_length, 20);

  printf("=== scaling samples (nb_rx=nb_tx=8) ===\n");
  int samples[] = {61440, 122880, 245760, 491520, 983040};
  for (int s : samples) run_case(8, 8, s, channel_length, 20);

  return 0;
}
