// Native CUDA counterpart to gpu_zerocopy.mojo: launches the same kernel
// directly on plain `malloc`'d host memory (no cudaMalloc/cudaMemcpy at
// all) to confirm the zero-copy result there isn't a Mojo-only trick -
// it's an ATS hardware/driver capability both languages get equally.
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cuda_runtime.h>

__global__ void conv_basic_kernel(const float *channel, const int16_t *tx, int16_t *rx,
  int num_input_samples, int num_samples, int channel_length, int nb_tx, int nb_rx) {
  int tid = blockIdx.x * blockDim.x + threadIdx.x;
  int total = nb_rx * num_samples;
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

int main() {
  int nb_rx = 4, nb_tx = 4, num_samples = 61440, channel_length = 16, iters = 30;
  int num_input_samples = num_samples + channel_length - 1;

  // plain malloc, no cudaMalloc / cudaMallocManaged / cudaMallocHost at all
  int16_t *h_tx = (int16_t*)malloc(nb_tx * num_input_samples * 2 * sizeof(int16_t));
  float *h_channel = (float*)malloc(nb_rx * nb_tx * channel_length * 2 * sizeof(float));
  int16_t *h_rx = (int16_t*)calloc(nb_rx * num_samples * 2, sizeof(int16_t));
  int16_t *h_rx_ref = (int16_t*)calloc(nb_rx * num_samples * 2, sizeof(int16_t));
  srand(1234);
  for (int i = 0; i < nb_tx * num_input_samples * 2; i++) h_tx[i] = (rand() % 40) - 20;
  for (int i = 0; i < nb_rx * nb_tx * channel_length * 2; i++) h_channel[i] = (float)((rand() % 40) - 20);

  int threads = 512;
  int total = nb_rx * num_samples;
  int blocks = (total + threads - 1) / threads;

  conv_basic_kernel<<<blocks, threads>>>(h_channel, h_tx, h_rx, num_input_samples, num_samples, channel_length, nb_tx, nb_rx);
  cudaError_t err = cudaDeviceSynchronize();
  if (err != cudaSuccess) { printf("CUDA error: %s\n", cudaGetErrorString(err)); return 1; }

  // cpu reference for correctness
  for (int rx_ant = 0; rx_ant < nb_rx; rx_ant++)
    for (int i = 0; i < num_samples; i++) {
      float acc_r = 0, acc_i = 0;
      for (int tx_ant = 0; tx_ant < nb_tx; tx_ant++) {
        int tx_base = tx_ant * num_input_samples;
        int chan_base = (tx_ant + nb_tx * rx_ant) * channel_length;
        for (int l = 0; l < channel_length; l++) {
          int idx = i + (channel_length - 1) - l;
          float tr = h_tx[(tx_base + idx) * 2], ti = h_tx[(tx_base + idx) * 2 + 1];
          float cr = h_channel[(chan_base + l) * 2], ci = h_channel[(chan_base + l) * 2 + 1];
          acc_r += tr * cr - ti * ci;
          acc_i += tr * ci + ti * cr;
        }
      }
      h_rx_ref[(rx_ant * num_samples + i) * 2] = (int16_t)acc_r;
      h_rx_ref[(rx_ant * num_samples + i) * 2 + 1] = (int16_t)acc_i;
    }
  int mism = 0, max_abs = 0;
  for (int i = 0; i < nb_rx * num_samples * 2; i++) {
    int d = h_rx_ref[i] - h_rx[i];
    if (d) { mism++; int ad = d < 0 ? -d : d; if (ad > max_abs) max_abs = ad; }
  }
  printf("zero-copy correctness: mismatches=%d/%d max_abs=%d\n", mism, nb_rx * num_samples * 2, max_abs);

  cudaEvent_t start, stop;
  cudaEventCreate(&start); cudaEventCreate(&stop);
  float best_ms = -1;
  for (int it = 0; it < iters; it++) {
    cudaEventRecord(start);
    conv_basic_kernel<<<blocks, threads>>>(h_channel, h_tx, h_rx, num_input_samples, num_samples, channel_length, nb_tx, nb_rx);
    cudaEventRecord(stop);
    cudaEventSynchronize(stop);
    float ms; cudaEventElapsedTime(&ms, start, stop);
    if (best_ms < 0 || ms < best_ms) best_ms = ms;
  }
  printf("zero-copy (plain malloc host pointers) best_ns=%.0f\n", best_ms * 1e6);
  return 0;
}
