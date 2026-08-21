/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <algorithm>
#include <time.h>
#include <getopt.h>
#include "oai_cuda.h"
#include "common/config/config_userapi.h"
#include <memory>
#include "benchmark/benchmark.h"
#include "test_channel_pipeline_tools.h"
#include "channel_pipeline.h"
extern "C" {
#include "openair1/SIMULATION/TOOLS/sim.h"
}
#ifdef CHANNEL_SIM_MOJO
#include "mojo_channel_pipeline.h"
#endif
#ifdef CHANNEL_SIM_ZIG
#include "zig_channel_pipeline.h"
#endif
#if defined(CHANNEL_SIM_MOJO) || defined(CHANNEL_SIM_ZIG)
#include "task_ans.h" // must precede thread-pool.h: defines the _Atomic(X) macro for C++ that thread-pool.h relies on
extern "C" {
#include "thread-pool.h"
}
#endif
configmodule_interface_t *uniqCfg = NULL;

extern "C" void exit_function(const char *file, const char *function, const int line, const char *s, const int assert)
{
  fprintf(stderr, "FATAL: %s at %s:%s:%d\n", s, file, function, line);
  exit(EXIT_FAILURE);
}

#ifdef CHANNEL_SIM_CUDA
static void BM_channel_convolution_gpu(benchmark::State &state)
{
  int nb_rx = state.range(0);
  int nb_tx = state.range(1);
  int num_samples = state.range(2);
  int channel_length = 16;

  size_t num_input_samples = num_samples + channel_length - 1;
  std::vector<c16_t *> input(nb_tx);
  for (int i = 0; i < nb_tx; ++i) {
    input[i] = new c16_t[num_input_samples];
  }

  std::vector<c16_t *> output(nb_rx);
  for (int i = 0; i < nb_rx; ++i) {
    output[i] = new c16_t[num_samples];
  }

  std::vector<cf_t *> channel(nb_rx * nb_tx);
  for (int i = 0; i < nb_rx * nb_tx; ++i) {
    channel[i] = new cf_t[channel_length];
  }

  for (int i = 0; i < nb_rx * nb_tx; ++i) {
    generate_random_signal_float(channel[i], channel_length);
  }

  void *gpu_context = cuda_channel_pipeline_init(61440 * 4);

  for (int aatx = 0; aatx < nb_tx; aatx++) {
    generate_random_signal(input[aatx], num_input_samples);
  }

  size_t total_samples = 0;
  for (auto _ : state) {
    cuda_channel_pipeline(gpu_context,
                          (const cf_t **)channel.data(),
                          (const c16_t **)input.data(),
                          nullptr,
                          num_input_samples,
                          output.data(),
                          nullptr,
                          num_samples,
                          num_samples,
                          channel_length,
                          nb_tx,
                          nb_rx,
                          0.0f);
    total_samples += num_samples;
  }
  state.counters["MSPS"] = benchmark::Counter(total_samples / 1000000.f, benchmark::Counter::kIsRate);

  cuda_channel_pipeline_shutdown(gpu_context);
}
#endif

static void BM_channel_convolution_cpu(benchmark::State &state)
{
  int nb_rx = state.range(0);
  int nb_tx = state.range(1);
  int num_samples = state.range(2);
  int channel_length = 16;

  size_t num_input_samples = num_samples + channel_length - 1;
  std::vector<c16_t *> input(nb_tx);
  for (int i = 0; i < nb_tx; ++i) {
    input[i] = new c16_t[num_input_samples];
  }

  std::vector<c16_t *> output(nb_rx);
  for (int i = 0; i < nb_rx; ++i) {
    output[i] = new c16_t[num_samples];
  }

  std::vector<cf_t *> channel(nb_rx * nb_tx);
  for (int i = 0; i < nb_rx * nb_tx; ++i) {
    channel[i] = new cf_t[channel_length];
  }

  for (int i = 0; i < nb_rx * nb_tx; ++i) {
    generate_random_signal_float(channel[i], channel_length);
  }
  for (int aatx = 0; aatx < nb_tx; aatx++) {
    generate_random_signal(input[aatx], num_input_samples);
  }

  size_t total_samples = 0;
  for (auto _ : state) {
    channel_convolution_cpu((const cf_t **)channel.data(),
                            (const c16_t **)input.data(),
                            nullptr,
                            num_input_samples,
                            output.data(),
                            nullptr,
                            num_samples,
                            num_samples,
                            channel_length,
                            nb_tx,
                            nb_rx);
    total_samples += num_samples;
  }
  state.counters["MSPS"] = benchmark::Counter(total_samples / 1000000.0f, benchmark::Counter::kIsRate);

  for (int i = 0; i < nb_tx; ++i)
    delete[] input[i];
  for (int i = 0; i < nb_rx * nb_tx; ++i)
    delete[] channel[i];
  for (int i = 0; i < nb_rx; ++i) {
    delete[] output[i];
  }
}

static void BM_channel_convolution_tpool(benchmark::State &state)
{
  int nb_rx = state.range(0);
  int nb_tx = state.range(1);
  int num_samples = state.range(2);
  int channel_length = 16;

  size_t num_input_samples = num_samples + channel_length - 1;
  std::vector<c16_t *> input(nb_tx);
  for (int i = 0; i < nb_tx; ++i) {
    input[i] = new c16_t[num_input_samples];
  }

  std::vector<c16_t *> output(nb_rx);
  for (int i = 0; i < nb_rx; ++i) {
    output[i] = new c16_t[num_samples];
  }

  std::vector<cf_t *> channel(nb_rx * nb_tx);
  for (int i = 0; i < nb_rx * nb_tx; ++i) {
    channel[i] = new cf_t[channel_length];
  }

  for (int i = 0; i < nb_rx * nb_tx; ++i) {
    generate_random_signal_float(channel[i], channel_length);
  }
  for (int aatx = 0; aatx < nb_tx; aatx++) {
    generate_random_signal(input[aatx], num_input_samples);
  }

  void *tpool = init_tpool(16);
  channel_pipeline_init(0.0f);

  size_t total_samples = 0;
  for (auto _ : state) {
    channel_pipeline(tpool,
                     (const cf_t **)channel.data(),
                     (const c16_t **)input.data(),
                     nullptr,
                     num_input_samples,
                     output.data(),
                     nullptr,
                     num_samples,
                     num_samples,
                     channel_length,
                     nb_tx,
                     nb_rx,
                     0.0f);
    total_samples += num_samples;
  }
  state.counters["MSPS"] = benchmark::Counter(total_samples / 1000000.f, benchmark::Counter::kIsRate);

  channel_pipeline_shutdown();

  destroy_tpool(tpool);

  for (int i = 0; i < nb_tx; ++i)
    delete[] input[i];
  for (int i = 0; i < nb_rx * nb_tx; ++i)
    delete[] channel[i];
  for (int i = 0; i < nb_rx; ++i) {
    delete[] output[i];
  }
}

#ifdef CHANNEL_SIM_MOJO
// Both Mojo variants use one flat contiguous buffer per antenna group
// (rather than test_channel_pipeline_tools.h's vector-of-pointers), since
// that's what the Mojo kernel's flat-pointer-plus-stride ABI expects.
static void BM_channel_convolution_mojo_basic(benchmark::State &state)
{
  int nb_rx = state.range(0);
  int nb_tx = state.range(1);
  int num_samples = state.range(2);
  int channel_length = 16;
  int num_input_samples = num_samples + channel_length - 1;

  std::vector<c16_t> tx(nb_tx * num_input_samples);
  std::vector<cf_t> channel(nb_rx * nb_tx * channel_length);
  std::vector<c16_t> rx(nb_rx * num_samples);

  generate_random_signal_float(channel.data(), channel.size());
  for (int aatx = 0; aatx < nb_tx; aatx++) {
    generate_random_signal(tx.data() + aatx * num_input_samples, num_input_samples);
  }

  size_t total_samples = 0;
  for (auto _ : state) {
    mojo_channel_convolution_basic(reinterpret_cast<const float *>(channel.data()),
                                   reinterpret_cast<const int16_t *>(tx.data()),
                                   reinterpret_cast<int16_t *>(rx.data()),
                                   num_input_samples,
                                   num_samples,
                                   channel_length,
                                   nb_tx,
                                   nb_rx);
    total_samples += num_samples;
  }
  state.counters["MSPS"] = benchmark::Counter(total_samples / 1000000.f, benchmark::Counter::kIsRate);
}

// Same as BM_channel_convolution_mojo_basic, but calling the explicit-SIMD
// kernel instead of relying on the auto-vectorizer - see
// mojo_channel_convolution_simd's doc comment for the +-1 LSB note.
static void BM_channel_convolution_mojo_simd(benchmark::State &state)
{
  int nb_rx = state.range(0);
  int nb_tx = state.range(1);
  int num_samples = state.range(2);
  int channel_length = 16;
  int num_input_samples = num_samples + channel_length - 1;

  std::vector<c16_t> tx(nb_tx * num_input_samples);
  std::vector<cf_t> channel(nb_rx * nb_tx * channel_length);
  std::vector<c16_t> rx(nb_rx * num_samples);

  generate_random_signal_float(channel.data(), channel.size());
  for (int aatx = 0; aatx < nb_tx; aatx++) {
    generate_random_signal(tx.data() + aatx * num_input_samples, num_input_samples);
  }

  size_t total_samples = 0;
  for (auto _ : state) {
    mojo_channel_convolution_simd(reinterpret_cast<const float *>(channel.data()),
                                  reinterpret_cast<const int16_t *>(tx.data()),
                                  reinterpret_cast<int16_t *>(rx.data()),
                                  num_input_samples,
                                  num_samples,
                                  channel_length,
                                  nb_tx,
                                  nb_rx);
    total_samples += num_samples;
  }
  state.counters["MSPS"] = benchmark::Counter(total_samples / 1000000.f, benchmark::Counter::kIsRate);
}

// Same as BM_channel_convolution_mojo_simd, but the kernel deinterleaves tx
// into scratch real/imag buffers once instead of gathering (r,i) apart out
// of interleaved memory on every tap - see mojo_channel_convolution_simd_deint's
// doc comment.
static void BM_channel_convolution_mojo_simd_deint(benchmark::State &state)
{
  int nb_rx = state.range(0);
  int nb_tx = state.range(1);
  int num_samples = state.range(2);
  int channel_length = 16;
  int num_input_samples = num_samples + channel_length - 1;

  std::vector<c16_t> tx(nb_tx * num_input_samples);
  std::vector<cf_t> channel(nb_rx * nb_tx * channel_length);
  std::vector<c16_t> rx(nb_rx * num_samples);

  generate_random_signal_float(channel.data(), channel.size());
  for (int aatx = 0; aatx < nb_tx; aatx++) {
    generate_random_signal(tx.data() + aatx * num_input_samples, num_input_samples);
  }

  size_t total_samples = 0;
  for (auto _ : state) {
    mojo_channel_convolution_simd_deint(reinterpret_cast<const float *>(channel.data()),
                                        reinterpret_cast<const int16_t *>(tx.data()),
                                        reinterpret_cast<int16_t *>(rx.data()),
                                        num_input_samples,
                                        num_samples,
                                        channel_length,
                                        nb_tx,
                                        nb_rx);
    total_samples += num_samples;
  }
  state.counters["MSPS"] = benchmark::Counter(total_samples / 1000000.f, benchmark::Counter::kIsRate);
}

static void BM_channel_convolution_mojo_parallel(benchmark::State &state)
{
  int nb_rx = state.range(0);
  int nb_tx = state.range(1);
  int num_samples = state.range(2);
  int channel_length = 16;
  int num_input_samples = num_samples + channel_length - 1;

  std::vector<c16_t> tx(nb_tx * num_input_samples);
  std::vector<cf_t> channel(nb_rx * nb_tx * channel_length);
  std::vector<c16_t> rx(nb_rx * num_samples);

  generate_random_signal_float(channel.data(), channel.size());
  for (int aatx = 0; aatx < nb_tx; aatx++) {
    generate_random_signal(tx.data() + aatx * num_input_samples, num_input_samples);
  }

  size_t total_samples = 0;
  for (auto _ : state) {
    mojo_channel_convolution_parallel(reinterpret_cast<const float *>(channel.data()),
                                      reinterpret_cast<const int16_t *>(tx.data()),
                                      reinterpret_cast<int16_t *>(rx.data()),
                                      num_input_samples,
                                      num_samples,
                                      channel_length,
                                      nb_tx,
                                      nb_rx,
                                      16);
    total_samples += num_samples;
  }
  state.counters["MSPS"] = benchmark::Counter(total_samples / 1000000.f, benchmark::Counter::kIsRate);
}

// Same idea as BM_channel_convolution_tpool: a thread pool created once,
// outside the timed loop, so the timed region only pays for enqueue/wait,
// not thread creation. The pool's worker threads are plain pthreads managed
// entirely on the C side; each task calls straight into the threading-free
// mojo_convolve_samples() for its shard. This sidesteps
// mojo_channel_convolution_parallel's own pthread_create/join entirely, so
// none of the Mojo-side-threading fast-path/bug workaround above is
// exercised here.
//
// Job split mirrors channel_pipeline.c's channel_pipeline(): each rx
// antenna is cut into ceil(num_threads/nb_rx) sample-range jobs, so the
// pool always has ~num_threads jobs to run regardless of nb_rx - splitting
// only by whole antennas (mojo_convolve_range) would leave most of a
// 16-thread pool idle whenever nb_rx is small.
struct MojoTpoolJobArgs {
  const float *channel;
  const int16_t *tx;
  int16_t *rx;
  int32_t num_input_samples;
  int32_t num_samples;
  int32_t channel_length;
  int32_t nb_tx;
  int32_t rx_ant;
  int32_t i_start;
  int32_t i_end;
  task_ans_t *task_ans;
};

static void mojo_tpool_job(void *args)
{
  MojoTpoolJobArgs *a = (MojoTpoolJobArgs *)args;
  mojo_convolve_samples(a->channel,
                       a->tx,
                       a->rx,
                       a->num_input_samples,
                       a->num_samples,
                       a->channel_length,
                       a->nb_tx,
                       a->rx_ant,
                       a->i_start,
                       a->i_end);
  completed_task_ans(a->task_ans);
}

static void mojo_tpool_job_simd(void *args)
{
  MojoTpoolJobArgs *a = (MojoTpoolJobArgs *)args;
  mojo_convolve_samples_simd(a->channel,
                             a->tx,
                             a->rx,
                             a->num_input_samples,
                             a->num_samples,
                             a->channel_length,
                             a->nb_tx,
                             a->rx_ant,
                             a->i_start,
                             a->i_end);
  completed_task_ans(a->task_ans);
}

static void BM_channel_convolution_mojo_tpool(benchmark::State &state)
{
  int nb_rx = state.range(0);
  int nb_tx = state.range(1);
  int num_samples = state.range(2);
  int channel_length = 16;
  int num_input_samples = num_samples + channel_length - 1;
  constexpr int num_pool_threads = 16;

  std::vector<c16_t> tx(nb_tx * num_input_samples);
  std::vector<cf_t> channel(nb_rx * nb_tx * channel_length);
  std::vector<c16_t> rx(nb_rx * num_samples);

  generate_random_signal_float(channel.data(), channel.size());
  for (int aatx = 0; aatx < nb_tx; aatx++) {
    generate_random_signal(tx.data() + aatx * num_input_samples, num_input_samples);
  }

  void *tpool = init_tpool(num_pool_threads);
  int jobs_per_rx = (num_pool_threads + nb_rx - 1) / nb_rx;
  int njobs = nb_rx * jobs_per_rx;
  std::vector<MojoTpoolJobArgs> jobs(njobs);

  size_t total_samples = 0;
  for (auto _ : state) {
    task_ans_t task_ans;
    init_task_ans(&task_ans, njobs);
    for (int rx_ant = 0; rx_ant < nb_rx; rx_ant++) {
      for (int j = 0; j < jobs_per_rx; j++) {
        int idx = rx_ant * jobs_per_rx + j;
        jobs[idx] = {reinterpret_cast<const float *>(channel.data()),
                    reinterpret_cast<const int16_t *>(tx.data()),
                    reinterpret_cast<int16_t *>(rx.data()),
                    num_input_samples,
                    num_samples,
                    channel_length,
                    nb_tx,
                    rx_ant,
                    (num_samples * j) / jobs_per_rx,
                    (num_samples * (j + 1)) / jobs_per_rx,
                    &task_ans};
        task_t task;
        task.args = &jobs[idx];
        task.func = mojo_tpool_job;
        pushTpool((tpool_t *)tpool, task);
      }
    }
    join_task_ans(&task_ans);
    total_samples += num_samples;
  }
  state.counters["MSPS"] = benchmark::Counter(total_samples / 1000000.f, benchmark::Counter::kIsRate);

  destroy_tpool(tpool);
}

// Same as BM_channel_convolution_mojo_tpool, but each job calls the
// explicit-SIMD kernel instead of the auto-vectorized one.
static void BM_channel_convolution_mojo_tpool_simd(benchmark::State &state)
{
  int nb_rx = state.range(0);
  int nb_tx = state.range(1);
  int num_samples = state.range(2);
  int channel_length = 16;
  int num_input_samples = num_samples + channel_length - 1;
  constexpr int num_pool_threads = 16;

  std::vector<c16_t> tx(nb_tx * num_input_samples);
  std::vector<cf_t> channel(nb_rx * nb_tx * channel_length);
  std::vector<c16_t> rx(nb_rx * num_samples);

  generate_random_signal_float(channel.data(), channel.size());
  for (int aatx = 0; aatx < nb_tx; aatx++) {
    generate_random_signal(tx.data() + aatx * num_input_samples, num_input_samples);
  }

  void *tpool = init_tpool(num_pool_threads);
  int jobs_per_rx = (num_pool_threads + nb_rx - 1) / nb_rx;
  int njobs = nb_rx * jobs_per_rx;
  std::vector<MojoTpoolJobArgs> jobs(njobs);

  size_t total_samples = 0;
  for (auto _ : state) {
    task_ans_t task_ans;
    init_task_ans(&task_ans, njobs);
    for (int rx_ant = 0; rx_ant < nb_rx; rx_ant++) {
      for (int j = 0; j < jobs_per_rx; j++) {
        int idx = rx_ant * jobs_per_rx + j;
        jobs[idx] = {reinterpret_cast<const float *>(channel.data()),
                    reinterpret_cast<const int16_t *>(tx.data()),
                    reinterpret_cast<int16_t *>(rx.data()),
                    num_input_samples,
                    num_samples,
                    channel_length,
                    nb_tx,
                    rx_ant,
                    (num_samples * j) / jobs_per_rx,
                    (num_samples * (j + 1)) / jobs_per_rx,
                    &task_ans};
        task_t task;
        task.args = &jobs[idx];
        task.func = mojo_tpool_job_simd;
        pushTpool((tpool_t *)tpool, task);
      }
    }
    join_task_ans(&task_ans);
    total_samples += num_samples;
  }
  state.counters["MSPS"] = benchmark::Counter(total_samples / 1000000.f, benchmark::Counter::kIsRate);

  destroy_tpool(tpool);
}

// Same job split as BM_channel_convolution_mojo_tpool[_simd], but the
// dispatching thread deinterleaves tx into scratch real/imag buffers once
// per call (mojo_deinterleave_tx - shared across every job, so it can't be
// redone per job) before farming shards out to mojo_convolve_samples_simd_deint.
struct MojoTpoolJobArgsDeint {
  const float *channel;
  const int16_t *tx_r;
  const int16_t *tx_i;
  int16_t *rx;
  int32_t num_input_samples;
  int32_t num_samples;
  int32_t channel_length;
  int32_t nb_tx;
  int32_t rx_ant;
  int32_t i_start;
  int32_t i_end;
  task_ans_t *task_ans;
};

static void mojo_tpool_job_simd_deint(void *args)
{
  MojoTpoolJobArgsDeint *a = (MojoTpoolJobArgsDeint *)args;
  mojo_convolve_samples_simd_deint(a->channel,
                                   a->tx_r,
                                   a->tx_i,
                                   a->rx,
                                   a->num_input_samples,
                                   a->num_samples,
                                   a->channel_length,
                                   a->nb_tx,
                                   a->rx_ant,
                                   a->i_start,
                                   a->i_end);
  completed_task_ans(a->task_ans);
}

static void BM_channel_convolution_mojo_tpool_simd_deint(benchmark::State &state)
{
  int nb_rx = state.range(0);
  int nb_tx = state.range(1);
  int num_samples = state.range(2);
  int channel_length = 16;
  int num_input_samples = num_samples + channel_length - 1;
  constexpr int num_pool_threads = 16;

  std::vector<c16_t> tx(nb_tx * num_input_samples);
  std::vector<cf_t> channel(nb_rx * nb_tx * channel_length);
  std::vector<c16_t> rx(nb_rx * num_samples);
  std::vector<int16_t> tx_r(nb_tx * num_input_samples);
  std::vector<int16_t> tx_i(nb_tx * num_input_samples);

  generate_random_signal_float(channel.data(), channel.size());
  for (int aatx = 0; aatx < nb_tx; aatx++) {
    generate_random_signal(tx.data() + aatx * num_input_samples, num_input_samples);
  }

  void *tpool = init_tpool(num_pool_threads);
  int jobs_per_rx = (num_pool_threads + nb_rx - 1) / nb_rx;
  int njobs = nb_rx * jobs_per_rx;
  std::vector<MojoTpoolJobArgsDeint> jobs(njobs);

  size_t total_samples = 0;
  for (auto _ : state) {
    mojo_deinterleave_tx(reinterpret_cast<const int16_t *>(tx.data()), tx_r.data(), tx_i.data(), nb_tx * num_input_samples);

    task_ans_t task_ans;
    init_task_ans(&task_ans, njobs);
    for (int rx_ant = 0; rx_ant < nb_rx; rx_ant++) {
      for (int j = 0; j < jobs_per_rx; j++) {
        int idx = rx_ant * jobs_per_rx + j;
        jobs[idx] = {reinterpret_cast<const float *>(channel.data()),
                    tx_r.data(),
                    tx_i.data(),
                    reinterpret_cast<int16_t *>(rx.data()),
                    num_input_samples,
                    num_samples,
                    channel_length,
                    nb_tx,
                    rx_ant,
                    (num_samples * j) / jobs_per_rx,
                    (num_samples * (j + 1)) / jobs_per_rx,
                    &task_ans};
        task_t task;
        task.args = &jobs[idx];
        task.func = mojo_tpool_job_simd_deint;
        pushTpool((tpool_t *)tpool, task);
      }
    }
    join_task_ans(&task_ans);
    total_samples += num_samples;
  }
  state.counters["MSPS"] = benchmark::Counter(total_samples / 1000000.f, benchmark::Counter::kIsRate);

  destroy_tpool(tpool);
}
#endif

#ifdef CHANNEL_SIM_ZIG
// Same three-stage progression as the CHANNEL_SIM_MOJO block above
// (basic auto-vec, explicit SIMD, SIMD + deinterleave-once), same buffer
// layout, same benchmark grid - see ../zig/channel_pipeline_zig_kernel.zig
// and ../mojo/README.md for the comparison writeup.
static void BM_channel_convolution_zig_basic(benchmark::State &state)
{
  int nb_rx = state.range(0);
  int nb_tx = state.range(1);
  int num_samples = state.range(2);
  int channel_length = 16;
  int num_input_samples = num_samples + channel_length - 1;

  std::vector<c16_t> tx(nb_tx * num_input_samples);
  std::vector<cf_t> channel(nb_rx * nb_tx * channel_length);
  std::vector<c16_t> rx(nb_rx * num_samples);

  generate_random_signal_float(channel.data(), channel.size());
  for (int aatx = 0; aatx < nb_tx; aatx++) {
    generate_random_signal(tx.data() + aatx * num_input_samples, num_input_samples);
  }

  size_t total_samples = 0;
  for (auto _ : state) {
    zig_channel_convolution_basic(reinterpret_cast<const float *>(channel.data()),
                                  reinterpret_cast<const int16_t *>(tx.data()),
                                  reinterpret_cast<int16_t *>(rx.data()),
                                  num_input_samples,
                                  num_samples,
                                  channel_length,
                                  nb_tx,
                                  nb_rx);
    total_samples += num_samples;
  }
  state.counters["MSPS"] = benchmark::Counter(total_samples / 1000000.f, benchmark::Counter::kIsRate);
}

static void BM_channel_convolution_zig_simd(benchmark::State &state)
{
  int nb_rx = state.range(0);
  int nb_tx = state.range(1);
  int num_samples = state.range(2);
  int channel_length = 16;
  int num_input_samples = num_samples + channel_length - 1;

  std::vector<c16_t> tx(nb_tx * num_input_samples);
  std::vector<cf_t> channel(nb_rx * nb_tx * channel_length);
  std::vector<c16_t> rx(nb_rx * num_samples);

  generate_random_signal_float(channel.data(), channel.size());
  for (int aatx = 0; aatx < nb_tx; aatx++) {
    generate_random_signal(tx.data() + aatx * num_input_samples, num_input_samples);
  }

  size_t total_samples = 0;
  for (auto _ : state) {
    zig_channel_convolution_simd(reinterpret_cast<const float *>(channel.data()),
                                 reinterpret_cast<const int16_t *>(tx.data()),
                                 reinterpret_cast<int16_t *>(rx.data()),
                                 num_input_samples,
                                 num_samples,
                                 channel_length,
                                 nb_tx,
                                 nb_rx);
    total_samples += num_samples;
  }
  state.counters["MSPS"] = benchmark::Counter(total_samples / 1000000.f, benchmark::Counter::kIsRate);
}

static void BM_channel_convolution_zig_simd_deint(benchmark::State &state)
{
  int nb_rx = state.range(0);
  int nb_tx = state.range(1);
  int num_samples = state.range(2);
  int channel_length = 16;
  int num_input_samples = num_samples + channel_length - 1;

  std::vector<c16_t> tx(nb_tx * num_input_samples);
  std::vector<cf_t> channel(nb_rx * nb_tx * channel_length);
  std::vector<c16_t> rx(nb_rx * num_samples);

  generate_random_signal_float(channel.data(), channel.size());
  for (int aatx = 0; aatx < nb_tx; aatx++) {
    generate_random_signal(tx.data() + aatx * num_input_samples, num_input_samples);
  }

  size_t total_samples = 0;
  for (auto _ : state) {
    zig_channel_convolution_simd_deint(reinterpret_cast<const float *>(channel.data()),
                                       reinterpret_cast<const int16_t *>(tx.data()),
                                       reinterpret_cast<int16_t *>(rx.data()),
                                       num_input_samples,
                                       num_samples,
                                       channel_length,
                                       nb_tx,
                                       nb_rx);
    total_samples += num_samples;
  }
  state.counters["MSPS"] = benchmark::Counter(total_samples / 1000000.f, benchmark::Counter::kIsRate);
}

// Same job split and same thread-pool machinery as
// BM_channel_convolution_mojo_tpool_simd_deint - Zig does no threading of
// its own here either, so this is the direct Zig-vs-Mojo comparison under
// the exact same OAI C thread pool.
struct ZigTpoolJobArgsDeint {
  const float *channel;
  const int16_t *tx_r;
  const int16_t *tx_i;
  int16_t *rx;
  int32_t num_input_samples;
  int32_t num_samples;
  int32_t channel_length;
  int32_t nb_tx;
  int32_t rx_ant;
  int32_t i_start;
  int32_t i_end;
  task_ans_t *task_ans;
};

static void zig_tpool_job_simd_deint(void *args)
{
  ZigTpoolJobArgsDeint *a = (ZigTpoolJobArgsDeint *)args;
  zig_convolve_samples_simd_deint(a->channel,
                                  a->tx_r,
                                  a->tx_i,
                                  a->rx,
                                  a->num_input_samples,
                                  a->num_samples,
                                  a->channel_length,
                                  a->nb_tx,
                                  a->rx_ant,
                                  a->i_start,
                                  a->i_end);
  completed_task_ans(a->task_ans);
}

static void BM_channel_convolution_zig_tpool_simd_deint(benchmark::State &state)
{
  int nb_rx = state.range(0);
  int nb_tx = state.range(1);
  int num_samples = state.range(2);
  int channel_length = 16;
  int num_input_samples = num_samples + channel_length - 1;
  constexpr int num_pool_threads = 16;

  std::vector<c16_t> tx(nb_tx * num_input_samples);
  std::vector<cf_t> channel(nb_rx * nb_tx * channel_length);
  std::vector<c16_t> rx(nb_rx * num_samples);
  std::vector<int16_t> tx_r(nb_tx * num_input_samples);
  std::vector<int16_t> tx_i(nb_tx * num_input_samples);

  generate_random_signal_float(channel.data(), channel.size());
  for (int aatx = 0; aatx < nb_tx; aatx++) {
    generate_random_signal(tx.data() + aatx * num_input_samples, num_input_samples);
  }

  void *tpool = init_tpool(num_pool_threads);
  int jobs_per_rx = (num_pool_threads + nb_rx - 1) / nb_rx;
  int njobs = nb_rx * jobs_per_rx;
  std::vector<ZigTpoolJobArgsDeint> jobs(njobs);

  size_t total_samples = 0;
  for (auto _ : state) {
    zig_deinterleave_tx(reinterpret_cast<const int16_t *>(tx.data()), tx_r.data(), tx_i.data(), nb_tx * num_input_samples);

    task_ans_t task_ans;
    init_task_ans(&task_ans, njobs);
    for (int rx_ant = 0; rx_ant < nb_rx; rx_ant++) {
      for (int j = 0; j < jobs_per_rx; j++) {
        int idx = rx_ant * jobs_per_rx + j;
        jobs[idx] = {reinterpret_cast<const float *>(channel.data()),
                    tx_r.data(),
                    tx_i.data(),
                    reinterpret_cast<int16_t *>(rx.data()),
                    num_input_samples,
                    num_samples,
                    channel_length,
                    nb_tx,
                    rx_ant,
                    (num_samples * j) / jobs_per_rx,
                    (num_samples * (j + 1)) / jobs_per_rx,
                    &task_ans};
        task_t task;
        task.args = &jobs[idx];
        task.func = zig_tpool_job_simd_deint;
        pushTpool((tpool_t *)tpool, task);
      }
    }
    join_task_ans(&task_ans);
    total_samples += num_samples;
  }
  state.counters["MSPS"] = benchmark::Counter(total_samples / 1000000.f, benchmark::Counter::kIsRate);

  destroy_tpool(tpool);
}
#endif

#ifdef CHANNEL_SIM_CUDA
BENCHMARK(BM_channel_convolution_gpu)
    ->ArgsProduct({
        {1, 2, 4, 16, 64}, // nb_rx
        {1, 2, 4, 16, 64}, // nb_tx
        {61440}, // num_samples
    })
    ->Iterations(100);
#endif

BENCHMARK(BM_channel_convolution_cpu)
    ->ArgsProduct({
        {1, 2, 4}, // nb_rx
        {1, 2, 4}, // nb_tx
        {61440}, // num_samples
    })
    ->Iterations(50);

BENCHMARK(BM_channel_convolution_tpool)
    ->ArgsProduct({
        {1, 2, 4, 8, 16}, // nb_rx
        {1, 2, 4, 8, 16}, // nb_tx
        {61440}, // num_samples
    })
    ->Iterations(50);

#ifdef CHANNEL_SIM_MOJO
// Same grid as BM_channel_convolution_cpu: both are single-threaded, so
// this is the apples-to-apples "basic Mojo vs. hand-tuned/auto-vectorized
// C" comparison.
BENCHMARK(BM_channel_convolution_mojo_basic)
    ->ArgsProduct({
        {1, 2, 4}, // nb_rx
        {1, 2, 4}, // nb_tx
        {61440}, // num_samples
    })
    ->Iterations(50);

// Same grid, explicit-SIMD kernel instead of auto-vectorized.
BENCHMARK(BM_channel_convolution_mojo_simd)
    ->ArgsProduct({
        {1, 2, 4}, // nb_rx
        {1, 2, 4}, // nb_tx
        {61440}, // num_samples
    })
    ->Iterations(50);

// Same grid, deinterleave-once kernel instead of per-tap gather.
BENCHMARK(BM_channel_convolution_mojo_simd_deint)
    ->ArgsProduct({
        {1, 2, 4}, // nb_rx
        {1, 2, 4}, // nb_tx
        {61440}, // num_samples
    })
    ->Iterations(50);

// Same grid as BM_channel_convolution_tpool: both spawn up to 16 worker
// threads split across rx antennas.
BENCHMARK(BM_channel_convolution_mojo_parallel)
    ->ArgsProduct({
        {1, 2, 4, 8, 16}, // nb_rx
        {1, 2, 4, 8, 16}, // nb_tx
        {61440}, // num_samples
    })
    ->Iterations(50);

// Same grid as BM_channel_convolution_tpool: the fair comparison, since
// both use a thread pool created once outside the timed loop.
BENCHMARK(BM_channel_convolution_mojo_tpool)
    ->ArgsProduct({
        {1, 2, 4, 8, 16}, // nb_rx
        {1, 2, 4, 8, 16}, // nb_tx
        {61440}, // num_samples
    })
    ->Iterations(50);

// Same grid, explicit-SIMD kernel instead of auto-vectorized.
BENCHMARK(BM_channel_convolution_mojo_tpool_simd)
    ->ArgsProduct({
        {1, 2, 4, 8, 16}, // nb_rx
        {1, 2, 4, 8, 16}, // nb_tx
        {61440}, // num_samples
    })
    ->Iterations(50);

// Same grid, deinterleave-once kernel instead of per-tap gather.
BENCHMARK(BM_channel_convolution_mojo_tpool_simd_deint)
    ->ArgsProduct({
        {1, 2, 4, 8, 16}, // nb_rx
        {1, 2, 4, 8, 16}, // nb_tx
        {61440}, // num_samples
    })
    ->Iterations(50);
#endif

#ifdef CHANNEL_SIM_ZIG
// Same grid as BM_channel_convolution_mojo_basic/_simd/_simd_deint - the
// direct Mojo-vs-Zig-vs-C comparison.
BENCHMARK(BM_channel_convolution_zig_basic)
    ->ArgsProduct({
        {1, 2, 4}, // nb_rx
        {1, 2, 4}, // nb_tx
        {61440}, // num_samples
    })
    ->Iterations(50);

BENCHMARK(BM_channel_convolution_zig_simd)
    ->ArgsProduct({
        {1, 2, 4}, // nb_rx
        {1, 2, 4}, // nb_tx
        {61440}, // num_samples
    })
    ->Iterations(50);

BENCHMARK(BM_channel_convolution_zig_simd_deint)
    ->ArgsProduct({
        {1, 2, 4}, // nb_rx
        {1, 2, 4}, // nb_tx
        {61440}, // num_samples
    })
    ->Iterations(50);

// Same grid as BM_channel_convolution_mojo_tpool_simd_deint - the direct
// thread-pool comparison, both under the same OAI C thread pool.
BENCHMARK(BM_channel_convolution_zig_tpool_simd_deint)
    ->ArgsProduct({
        {1, 2, 4, 8, 16}, // nb_rx
        {1, 2, 4, 8, 16}, // nb_tx
        {61440}, // num_samples
    })
    ->Iterations(50);
#endif

int main(int argc, char **argv)
{
  logInit();
  randominit();
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  return 0;
}
