# Mojo channel-convolution kernels

Three things live here:

1. `channel_pipeline_basic.mojo` — a standalone, `mojo run`-able comparison
   against `channel_convolution_cpu()`. List-based, not linked into anything.
2. `gpu_*.mojo` / `gpu_*.cu` — standalone GPU benchmarks (Mojo vs. native
   CUDA) developed and run on a separate ARM/NVIDIA machine (an NVIDIA
   GB10/Grace-Blackwell box), not this one and not wired into the CMake
   build here. See "GPU: Mojo vs CUDA" below.
3. `channel_pipeline_mojo_kernel.mojo` — a real Mojo shared library, wired
   into `benchmark_channel_pipeline` (see `../CMakeLists.txt` and
   `../benchmark_channel_pipeline.cpp`), exporting a C ABI so the gbenchmark
   binary calls straight into Mojo:
   - `mojo_channel_convolution_basic` — single-threaded, plain nested loops,
     whatever the compiler's auto-vectorizer finds on its own.
   - `mojo_channel_convolution_simd` — single-threaded, explicitly
     vectorized to the build target's native float32 SIMD width (16
     lanes/AVX-512 on this machine) via `strided_load`/`strided_store`
     instead of relying on auto-vectorization.
   - `mojo_channel_convolution_simd_deint` — same vectorization, but
     deinterleaves tx into scratch real/imag buffers once per call instead
     of gathering `(r,i)` apart out of interleaved memory on every tap of
     every sample block. By far the fastest single-threaded kernel here -
     see below.
   - `mojo_channel_convolution_parallel` — Mojo spawns/joins its own OS
     threads (via `pthread_create`/`pthread_join` through
     `std.ffi.external_call`, since this build's stdlib has no higher-level
     CPU parallel-for - `std.algorithm` only has `vectorize`/`map`, not
     `parallelize`), splitting rx antennas into contiguous shards, fresh
     threads on every call.
   - `mojo_convolve_range` / `mojo_convolve_samples` / their `_simd` and
     `_simd_deint` counterparts, plus `mojo_deinterleave_tx` — no threading
     at all; compute a shard (a range of rx antennas, or a sample sub-range
     of one rx antenna) synchronously. Meant to be called concurrently from
     an *externally*-managed, persistent thread pool - see
     `BM_channel_convolution_mojo_tpool[_simd][_deint]` below, which reuse
     the same `init_tpool`/`pushTpool`/`task_ans_t` machinery
     `BM_channel_convolution_tpool` already uses. The `_deint` variant's
     benchmark calls `mojo_deinterleave_tx` once per call, on the
     dispatching thread, before farming shards out to
     `mojo_convolve_samples_simd_deint` - the deinterleave is shared work
     across every job, so it can't be redone per job.

All kernels operate on flat, interleaved-(r,i) `float32`/`int16_t` buffers
passed as raw pointers - `cf_t`/`c16_t` have no padding, so a `cf_t*`/`c16_t*`
from C is bit-for-bit the same thing.

## Setup

Mojo is not preinstalled on this machine. Install it into a venv and put it
on PATH before running `cmake` (the build picks it up via
`find_program(mojo)` and enables the extra benchmark cases only if found -
no mojo, no problem, the rest of the build is unaffected):

```
uv venv /path/to/mojoenv
source /path/to/mojoenv/bin/activate
uv pip install modular --index-url https://dl.modular.com/public/nightly/python/simple/ \
    --extra-index-url https://pypi.org/simple/
cmake <build-dir>   # re-run configure with mojo on PATH
ninja -C <build-dir> benchmark_channel_pipeline
```

This pulled Mojo 1.0.0 / MAX 26.5.0. Notes on this version's stdlib, which
differs a fair bit from older Mojo docs/tutorials:
- `fn` was removed; use `def` everywhere.
- No top-level `time`/`collections` packages - `from std.time import
  perf_counter_ns`, and `List`/`Int16`/etc. are in the builtin prelude.
- `UnsafePointer` is deprecated in favor of `Pointer`, but still works and is
  what FFI code needs; a bound origin is required - `MutUnsafeAnyOrigin` is
  the replacement for the old `MutableAnyOrigin`.
- To export a C-callable symbol: `@export(ABI="C")` (or the non-deprecated
  `@export(abi("C"))`, which additionally requires an explicit symbol-name
  string) above a top-level `def`.
- `std.algorithm` has no `parallelize`/`sync_parallelize` in this build - it's
  a MAX/kernels-oriented nightly, not the older general-purpose-CPU-parallel
  Mojo. `std.ffi.external_call` does exist, so raw `pthread_create`/
  `pthread_join` works fine as a substitute.
- `std.sys.simd_width_of[Float32]()` reports the target's native float32
  SIMD width (16 = AVX-512 on this machine). `UnsafePointer.strided_load`/
  `.strided_store` (stride, explicit `width=`) are the easiest way to
  gather/scatter a de-interleaved `SIMD[T, W]` out of `(r,i)`-interleaved
  memory; `SIMD.deinterleave()`/`.interleave()` also exist but this
  compiler's dependent-type unifier doesn't recognize `(2*W)/2 == W` as the
  same type, so any width you get from a `deinterleave()` call has to keep
  flowing through inferred `var`s rather than being re-declared against an
  independently-computed width expression - `strided_load`/`strided_store`
  sidestep that entirely by asking for width `W` directly.

## A bug we hit and worked around

`mojo_channel_convolution_parallel` reliably segfaulted whenever it computed
exactly **one** thread's worth of work (`nthreads == 1`, whether because
`nb_rx == 1` or because `max_threads` was set to 1) - the single
`pthread_create`/`pthread_join` round trip through the `List[ConvArgs]`-backed
context/thread-id arrays would crash inside the worker. Two or more threads
worked reliably and reproducibly. A minimal repro with a trivial worker body
did *not* crash, and removing the real convolution math changed the failure
mode again, so this looks like a genuine compiler/runtime edge case in this
nightly rather than an obvious use-after-free in this file - we didn't chase
it further. The workaround: when `nthreads == 1`, just call the shared
`convolve_range()` helper directly on the calling thread instead of spawning
a single pthread for it. Correctness and the `nthreads >= 2` path were
checked against a hand-computed 2-antenna case in `test_mojo_kernel.c` used
during development. The `mojo_tpool*` benchmarks never spawn pthreads from
Mojo at all, so they don't exercise this bug or its workaround.

## Is the single-threaded gain real? (checked the binary)

`mojo_channel_convolution_basic` beats `channel_convolution_cpu` by a
real, explainable margin - confirmed by disassembling both:

```
objdump -d libmojo_channel_kernel.so --disassemble=mojo_channel_convolution_basic
objdump -d benchmark_channel_pipeline.dir/test_channel_pipeline_tools.c.o --disassemble=channel_convolution_cpu
```

The C version (GCC, `-O2 -march=native`, and still true at `-O3`, and with
`clang -O3 -march=native` too) computes the complex multiply-accumulate
(`rx_r += tx.r*ch.r - tx.i*ch.i; rx_i += tx.r*ch.i + tx.i*ch.r`) as four
independent **scalar** FMAs (`vfmadd132ss`, one float lane at a time).
Mojo's version instead lowers it to a **packed** `vfmaddsub231ps` on a
128-bit `xmm` register (with a `vshufps`/`vbroadcastss` setup) - a real SIMD
trick for complex multiplication that computes the real and imaginary
cross-terms in one fused instruction on two lanes at once. Neither GCC nor
clang finds this idiom for this exact C source at any optimization level
tried, so this isn't a `-O2` vs. `-O3` build-flag artifact - Mojo's compiler
is doing something GCC's/clang's auto-vectorizer doesn't for this specific
loop shape. Correctness was independently re-checked (hand-computed small
case, see `test_mojo_kernel.c`), and the call crosses a real shared-library
boundary the C++ compiler can't see into or eliminate, so this isn't a
dead-code-elimination artifact either. The gain is real.

## A measurement pitfall in the multithreaded numbers (also checked)

Google Benchmark's `Counter::kIsRate` divides by the benchmark's measured
**CPU time**, i.e. only the calling/fixture thread's own CPU usage - not
wall-clock time. For `BM_channel_convolution_tpool` and the Mojo
multithreaded variants, almost all the real work happens on *other* threads
(the pool workers), so the fixture thread's own CPU time is tiny (just
enqueue + wait), and the printed "MSPS" counter is inflated by roughly the
thread count. This inflation is present in every tpool-based variant
identically, so it doesn't bias one against another, but the raw benchmark
output should not be read as literal MSPS. Every table below uses
**wall-clock MSPS**, computed as `num_samples * 1000 / <Time column, ns>`.

## Why did the single-thread gain disappear in the first multithreaded pass?

It didn't get lost - it got diluted by fixed per-task dispatch overhead. The
tpool job split cuts each rx antenna into `ceil(16/nb_rx)` sample-range
jobs so the pool always has ~16 jobs regardless of nb_rx. At small nb_rx
that means many *tiny* jobs, and the OS-thread-pool's fixed per-task cost
(semaphore wake, queue push) is the same size regardless of how much compute
is in the job - so it swamps the ~15-50% per-core kernel advantage. Measured
the floor directly with a throwaway harness pushing empty (no-op) tasks
through the exact same `pushTpool`/`task_ans_t` machinery: dispatching and
joining 16 no-op tasks costs **~10.5us** on this machine, regardless of
Mojo. That's not a Mojo problem, and it's not really fixable by writing a
different pool - it's the fixed cost of *any* thread pool dispatching this
many jobs, and 16 jobs is what `channel_pipeline.c`'s own job-count formula
already produces. Confirmed by checking the coarsest-grain case in our own
grid (nb_rx=16, `jobs_per_rx=1`, one task = one whole antenna - the same
granularity as the single-threaded number): there, the per-core kernel
advantage reappears at 1.14x-1.52x, matching the single-threaded numbers
almost exactly. Once `mojo_convolve_samples_simd` replaced
`mojo_convolve_samples` as the per-job kernel (see below), the gain shows up
plainly across the whole grid instead of only at coarse granularity, because
it's now large enough to survive being diluted by the fixed per-task cost.

## Explicit SIMD: was there more on the table?

Yes. `std.sys.simd_width_of[Float32]()` reports **16** on this machine
(AVX-512), but the compiler's auto-vectorizer only found a 2-lane (128-bit)
trick on its own (the packed complex-multiply above). `convolve_one_antenna_samples_simd`
vectorizes explicitly across *output samples* instead: for a fixed tap, `HW`
(=16) consecutive output samples need `HW` contiguous input samples (a
straightforward FIR/sliding-window vectorization), loaded/stored via
`strided_load`/`strided_store` since the data is `(r,i)`-interleaved.
Disassembly confirms real 512-bit `vfmadd231ps` on `%zmm` registers, not
just a wider version of the same 128-bit trick. Correctness was checked
against the scalar kernel on non-16-aligned sizes and multiple antennas: it
matches everywhere except ~1.3% of samples that differ by **exactly ±1**
LSB out of the int16 output - floating-point re-association/FMA-fusion
noise from a different instruction sequence computing the same sum (the two
kernels don't do the exact same rounding at every intermediate step), not a
bug. This is the same class of noise you'd see comparing any two
differently-vectorized floating-point implementations, and it's far below
the noise floor of a real channel simulation.

## Deinterleave once: the biggest single win

`convolve_one_antenna_samples_simd` reads tx via `strided_load` (stride 2)
on *every tap of every sample block*, gathering the real and imaginary
parts back apart out of `(r,i)`-interleaved memory - and since a fixed tap
range is revisited for every output-sample block as the window slides, the
same input region gets re-gathered `channel_length` times over. Since tx is
the same data for every rx antenna and every sample block within a call,
deinterleaving it into two flat contiguous scratch buffers *once* per call,
then doing plain contiguous loads inside the tap loop instead of a strided
gather, turned into the single biggest lever tried here - a further
**4.3x-7x** on top of `mojo_channel_convolution_simd`, scaling up with
`channel_length*nb_tx` (more taps = more redundant gathers eliminated).
Verified bit-for-bit identical to `mojo_channel_convolution_simd` (not just
"close" - the deinterleave is a pure memory-access change, not a numerical
one), and correctness re-checked against the scalar kernel on non-16-aligned
sizes.

## Results (AMD Ryzen 9 9955HX, num_samples=61440, channel_length=16, wall-clock MSPS)

Single-threaded:

| nb_rx | nb_tx | C `-O2 -march=native` | Mojo basic | Mojo SIMD | Mojo SIMD, deinterleaved | deint / C |
|---|---|---|---|---|---|---|
| 1 | 1 | 77.7 | 122.8 | 161.9 | 508.9 | 6.55x |
| 1 | 2 | 50.1 | 59.9 | 85.3 | 313.8 | 6.26x |
| 1 | 4 | 25.4 | 29.1 | 43.6 | 165.6 | 6.52x |
| 2 | 1 | 45.1 | 61.4 | 81.0 | 328.7 | 7.29x |
| 2 | 2 | 25.0 | 29.8 | 42.7 | 201.2 | 8.04x |
| 2 | 4 | 12.7 | 14.6 | 21.8 | 111.9 | 8.82x |
| 4 | 1 | 24.3 | 30.7 | 40.5 | 186.2 | 7.67x |
| 4 | 2 | 12.5 | 15.0 | 21.3 | 117.1 | 9.37x |
| 4 | 4 | 6.4 | 7.3 | 10.9 | 65.9 | 10.38x |

Multithreaded, 16-thread pool created once outside the timed loop on both
sides, job split mirroring `channel_pipeline.c`'s (`ceil(16/nb_rx)`
sample-range jobs per antenna, so the pool always has ~16 jobs regardless of
nb_rx - splitting only by whole antennas leaves most of the pool idle
whenever nb_rx is small):

| nb_rx | nb_tx | C tpool | Mojo tpool SIMD, deinterleaved | deint / tpool |
|---|---|---|---|---|
| 1 | 1 | 496.4 | 1426.3 | 2.87x |
| 1 | 2 | 315.0 | 900.4 | 2.86x |
| 1 | 4 | 176.5 | 526.1 | 2.98x |
| 2 | 1 | 314.4 | 1030.1 | 3.28x |
| 2 | 2 | 172.0 | 690.0 | 4.01x |
| 2 | 4 | 85.0 | 430.8 | 5.07x |
| 4 | 1 | 159.9 | 906.2 | 5.67x |
| 4 | 2 | 83.4 | 518.6 | 6.21x |
| 4 | 4 | 44.2 | 309.0 | 7.00x |

(Run-to-run noise is real for the non-`_deint` tpool variants -
`mojo_tpool`/`tpool` ratios have moved between ~0.9x and ~1.5x across
separate runs in this session depending on system load; `_simd_deint` has
been consistently, dramatically the fastest of every variant in every run,
since it's fast enough to not be as sensitive to the fixed per-task
dispatch overhead described above.)

`mojo_channel_convolution_parallel` (Mojo spawns fresh pthreads every call,
no persistent pool) is left in place for comparison/documentation of that
threading style; it's substantially slower than any tpool-based variant at
these problem sizes because pthread_create/join overhead dominates.

## Is Mojo's SIMD gain specific to Mojo? A Zig comparison

`../zig/channel_pipeline_zig_kernel.zig` reimplements the same
basic/explicit-SIMD/deinterleave-once progression in Zig, wired into this
same benchmark under `CHANNEL_SIM_ZIG`, to check whether Zig's `@Vector` +
`std.simd.suggestVectorLength` gives LLVM the same information Mojo's
`SIMD[DType, W]` + `simd_width_of` does. Short answer: yes, and Zig comes
out ahead at every stage - by 1.1-1.3x on the scalar/auto-vec stage,
1.8-1.9x on the explicit-SIMD stage (the widest gap - see
`../zig/README.md` for the disassembly-level explanation), and 1.1-1.5x
once both are deinterleaved. Full numbers and methodology in
`../zig/README.md`.

## Portability: the same kernel on ARM (NEON) with zero source changes

The identical `channel_pipeline_mojo_kernel.mojo` was copied unmodified to
an ARM box (NVIDIA GB10, Cortex-X925/A725 CPU cores) and rebuilt with one
`mojo build` invocation - no `#ifdef`s, no separate code path.
`simd_width_of[Float32]()` correctly reports **4** there (128-bit NEON)
instead of 16 (AVX-512), and the kernel picks that width up automatically:

| nb_rx | nb_tx | C `-O2 -march=native` | Mojo basic | Mojo SIMD (4-wide) | Mojo SIMD, deinterleaved |
|---|---|---|---|---|---|
| 1 | 1 | 76.3 | 1.11x | 1.18x | 2.35x |
| 2 | 2 | 25.2 | 1.08x | 1.18x | 2.59x |
| 4 | 4 | 6.4 | 1.07x | 1.18x | 2.74x |

(ratios vs. the C column; full grid in prior session notes.) The
auto-vectorizer and explicit-SIMD gains shrink a lot on the narrower vector
unit - expected, since there's only 4 lanes to exploit instead of 16 - but
the deinterleave win survives almost intact (2.35-2.74x vs. 6.3-10.4x on
x86): it's an algorithmic/memory-layout fix, not a SIMD-width-dependent one.

## GPU: Mojo vs CUDA on NVIDIA GB10 (Grace-Blackwell)

Standalone (not CMake-wired) benchmarks comparing a from-scratch Mojo GPU
kernel against native CUDA C++, run on the same GB10 box used for the ARM
CPU numbers above. Requires Mojo/MAX plus a CUDA toolkit new enough for the
target compute capability (this GPU is sm_121/Blackwell; CUDA 13.0 here).
Run each with `mojo run <file>.mojo` / `nvcc -O3 -arch=sm_121 <file>.cu -o
<file> && ./<file>`.

- `gpu_large_mojo.mojo` / `gpu_large_cuda.cu` — the main comparison: two
  kernels each (`conv_basic_kernel`: one thread per output sample, direct
  global-memory reads; `conv_tiled_kernel`: shared-memory-tiled, a port of
  OAI's actual production kernel, `multipath_channel_kernel` in
  `../../../SIMULATION/TOOLS/multipath_channel.cu`), swept across
  1-64 antennas and 61440-983040 samples.
- `gpu_large_check.mojo` — correctness verification for the above, same
  grid, against a plain CPU reference.
- `gpu_copy_cost.mojo` — measures H2D-copy-only, kernel-only, and full
  round-trip time separately, to quantify the discrete-GPU-model copy tax.
- `gpu_zerocopy.mojo` / `gpu_zerocopy_cuda.cu` — minimal zero-copy
  demonstration (kernel launched directly on host memory, no
  `DeviceBuffer`/`enqueue_copy`/`cudaMalloc` at all) in both languages.

### Does Mojo support unified memory, and were we doing unnecessary copies?

`nvidia-smi -q` reports `Addressing Mode: ATS` for this GPU - Grace-class
hardware where the GPU walks the CPU's page tables directly over
NVLink-C2C, so an *ordinary* host pointer (a Mojo `List`'s buffer, a plain
C `malloc`) is already valid, coherent GPU memory. No special "unified
memory" allocator call is needed at all - we verified this directly: a
kernel launched straight on host pointers (no buffers, no copies)
matched a CPU reference bit-for-bit, in both Mojo and native CUDA.

This mattered because our first pass used the traditional discrete-GPU
model (`enqueue_create_buffer`+`enqueue_copy` / `cudaMalloc`+`cudaMemcpy`),
which pays for a copy this hardware doesn't need: at nb_rx=nb_tx=4, the
H2D copy of tx alone (983KB) cost more than half the kernel's own time,
and the full copy-in+kernel+copy-out round trip cost **~2x** the
kernel-only time. Skipping the buffers and operating directly on host
memory recovered the kernel-only number with no copy tax, identically in
Mojo and CUDA (`gpu_zerocopy.mojo`: 41488ns; `gpu_zerocopy_cuda.cu`:
39168ns) - confirming this is a hardware/driver capability, not a
Mojo-specific trick.

### A second instance of the "ASAP destruction" bug

Scaling the benchmark up to more antennas and samples surfaced a real
correctness bug, caught by `gpu_large_check.mojo`'s sweep: mismatch counts
up to ~12% of the output, with no relationship to data magnitude and no
obvious pattern across antenna counts (2,2 broken; 8,8 clean; 16,16
broken; 64,64 clean). This is the *same* root cause as the
`mojo_channel_convolution_parallel` pthread issue elsewhere in this
file, just in a new context: Mojo destroys a `List` at its last *textual*
reference, not its last *use*, and `ctx.synchronize()` doesn't count as a
"use" - so the host buffers backing our zero-copy pointers were being
freed while the asynchronous GPU kernel was still reading/writing them via
ATS. Fixed identically to before: an explicit `_ = len(...)` on each
buffer, placed after the point where every kernel touching it has been
synchronized. After the fix: 0 mismatches across 1-64 antennas and
61440-983040 samples (see `gpu_large_mojo.mojo`'s and
`gpu_large_check.mojo`'s file headers for the full writeup). The timing
numbers below were unaffected by the fix - a data race doesn't change how
long the kernel takes to run, only what it computes.

### Results (zero-copy, both languages, `conv_basic_kernel`)

Scaling antennas (num_samples=61440) — the interesting trend is that Mojo
trails CUDA at small scale but reaches parity (and slightly exceeds it)
once the kernel is large enough that per-launch overhead stops dominating:

| nb_rx=nb_tx | CUDA basic (MSPS) | Mojo basic (MSPS) | ratio |
|---|---|---|---|
| 1 | 11034.5 | 7204.5 | 0.65x |
| 2 | 4935.7 | 4020.9 | 0.81x |
| 4 | 1576.4 | 1476.4 | 0.94x |
| 8 | 415.9 | 413.3 | 0.99x |
| 16 | 106.4 | 107.9 | 1.01x |
| 32 | 26.6 | 27.3 | 1.03x |
| 64 | 6.6 | 6.8 | 1.04x |

Scaling samples (nb_rx=nb_tx=8 fixed) confirms the parity holds across
problem size once past the small-kernel regime: ratio sits at 0.99x-1.01x
from 61440 to 983040 samples.

`conv_tiled_kernel` (shared-memory) is the exception to this good news: it
stays at **0.55x-0.77x of CUDA's tiled kernel at every scale tested**, and
- more tellingly - *never beats Mojo's own basic kernel* anywhere in the
grid (0.87x-0.98x of it), while CUDA's tiled kernel consistently beats
CUDA's basic kernel by 15-21%. So the underlying idea (stage a tx tile
into shared memory once instead of re-reading global memory per tap) is
still a real win on this hardware - CUDA proves it - but this first-pass
Mojo port of it doesn't capture that win, and the gap doesn't close with
scale the way the basic-kernel gap did. This looks like a genuine
codegen/lowering gap in Mojo's shared-memory/`barrier()` path rather than
an algorithmic problem, but we didn't have visibility (no PTX/SASS-level
tooling used) to pin down exactly where.

## Next step

CPU: the tap loop (`channel_length` x `nb_tx`) is still scalar even in the
deinterleaved kernel - only the sample dimension is vectorized, and each
tap still does its own pair of loads for `tx_r`/`tx_i` plus two scalar
channel reads. Vectorizing across taps as well (e.g. loading several taps'
worth of channel coefficients at once) or restructuring as an explicit
overlap-save/FFT-based convolution for large `channel_length` would be the
next lever if pushing further - though at `channel_length=16` the
diminishing returns of more complexity here should be weighed against
simply calling this good enough.

GPU: `conv_tiled_kernel`'s underperformance vs. its own basic kernel is
the open question - would need PTX/SASS inspection of the generated code
to find out whether it's the cooperative-load loop, the `barrier()`
lowering, or something else eating the win that CUDA's equivalent kernel
clearly gets on this hardware.
