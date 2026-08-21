# Zig channel-convolution kernels

`channel_pipeline_zig_kernel.zig` reimplements the same three-stage
progression as `../mojo/channel_pipeline_mojo_kernel.mojo`
(`mojo_channel_convolution_basic`/`_simd`/`_simd_deint`), wired into
`benchmark_channel_pipeline` the same way, under `CHANNEL_SIM_ZIG`. The
question this answers: does Zig's `@Vector` + `std.simd.suggestVectorLength`
give LLVM the same level of information Mojo's `SIMD[DType, W]` +
`simd_width_of` does, and does it reach the same performance?

- `zig_channel_convolution_basic` — plain scalar nested loops, whatever
  LLVM's auto-vectorizer finds on its own.
- `zig_channel_convolution_simd` — explicitly vectorized to
  `std.simd.suggestVectorLength(f32)` (16 = AVX-512 on this machine, same
  width Mojo's `simd_width_of` reports), gathering `(r,i)` apart from
  interleaved memory via a plain scalar loop that fills a `[W]i16` array
  and casts it to `@Vector(W, i16)` - the direct analogue of Mojo's
  `strided_load`, but without a dedicated intrinsic: just an ordinary loop
  and a vector cast, to see what LLVM does with it unassisted.
- `zig_channel_convolution_simd_deint` — same vectorization, but
  deinterleaves tx into scratch real/imag buffers once per call first (via
  `std.heap.page_allocator`), so the tap loop does plain contiguous vector
  loads instead of a strided gather - the analogue of
  `mojo_channel_convolution_simd_deint`.

## Setup

Needs a `zig` compiler on PATH before running `cmake` (same
`find_program` pattern as Mojo - no zig, no problem, rest of the build is
unaffected):

```
# zig 0.16.0 used here; install via https://ziglang.org/download/ or a
# package manager, then:
cmake <build-dir>   # re-run configure with zig on PATH
ninja -C <build-dir> benchmark_channel_pipeline
```

One thing to know if hand-building outside CMake: `zig build-lib
foo.zig -dynamic -femit-bin=some/path/libfoo.so` embeds a SONAME derived
from the *source filename* (`libchannel_pipeline_zig_kernel.so` here),
independent of whatever path you pass to `-femit-bin`. If the two don't
match, the binary that links against it will fail to find it at runtime
(`cannot open shared object file`) even though the link step succeeded.
The CMake rule here names the output to match, so this doesn't bite the
normal build - only hand-building/renaming the .so would.

## Results (AMD Ryzen 9 9955HX, num_samples=61440, channel_length=16, MSPS)

All from one `benchmark_channel_pipeline` run (official Google Benchmark
timing, not a hand-rolled harness), so C, Mojo, and Zig are directly
comparable - same binary, same iteration count, same random data:

| nb_rx | nb_tx | C `-O2 -march=native` | Mojo basic / C | Zig basic / C | Mojo SIMD / C | Zig SIMD / C | Mojo deint / C | Zig deint / C |
|---|---|---|---|---|---|---|---|---|
| 1 | 1 | 77.7 | 1.30x | 1.57x | 2.08x | 4.01x | 6.55x | 9.98x |
| 1 | 2 | 50.3 | 1.04x | 1.19x | 1.69x | 3.14x | 6.20x | 8.03x |
| 1 | 4 | 25.5 | 1.03x | 1.14x | 1.72x | 3.20x | 6.56x | 8.21x |
| 2 | 1 | 46.1 | 1.06x | 1.33x | 1.75x | 3.38x | 7.11x | 10.62x |
| 2 | 2 | 25.1 | 1.02x | 1.19x | 1.70x | 3.04x | 8.03x | 10.06x |
| 2 | 4 | 12.7 | 1.03x | 1.14x | 1.71x | 3.20x | 8.80x | 10.20x |
| 4 | 1 | 24.3 | 1.01x | 1.26x | 1.66x | 3.21x | 7.61x | 11.62x |
| 4 | 2 | 12.6 | 1.02x | 1.19x | 1.69x | 3.13x | 9.45x | 11.51x |
| 4 | 4 | 6.4 | 1.03x | 1.14x | 1.72x | 3.20x | 10.52x | 11.59x |

Zig beats Mojo at every stage of the progression here, not just matches
it. Direct Zig/Mojo ratio at each stage:

| stage | Zig / Mojo |
|---|---|
| basic (scalar, auto-vec) | 1.10x-1.26x |
| explicit SIMD (strided gather) | 1.79x-1.93x |
| SIMD + deinterleave-once | 1.10x-1.53x (narrows as nb_tx/nb_rx grows) |

## Thread pool: Zig under the same OAI C tpool as Mojo

`zig_deinterleave_tx`/`zig_convolve_samples_simd_deint` mirror
`mojo_deinterleave_tx`/`mojo_convolve_samples_simd_deint` exactly: Zig does
no threading of its own either - `BM_channel_convolution_zig_tpool_simd_deint`
in `benchmark_channel_pipeline.cpp` uses the identical `init_tpool(16)` +
`ceil(16/nb_rx)`-jobs-per-antenna + `pushTpool`/`join_task_ans` dispatch as
`BM_channel_convolution_mojo_tpool_simd_deint`, just calling the Zig kernel
per shard instead of the Mojo one. Correctness verified separately
(0 mismatches vs both a scalar reference and the single-call
`zig_channel_convolution_simd_deint`, across non-power-of-2 shard counts).

Wall-clock MSPS (`num_samples*1000/Time_ns`, not the CPU-time-based
`kIsRate` counter - see the methodology note in `../mojo/README.md`),
16-thread pool, num_samples=61440:

| nb_rx | nb_tx | Mojo tpool MSPS | Zig tpool MSPS | Zig / Mojo |
|---|---|---|---|---|
| 1 | 1 | 1053.8 | 2172.4 | 2.06x |
| 1 | 2 | 729.2 | 1560.0 | 2.14x |
| 1 | 4 | 427.1 | 806.2 | 1.89x |
| 1 | 8 | 280.0 | 458.9 | 1.64x |
| 1 | 16 | 145.1 | 244.5 | 1.69x |
| 2 | 1 | 888.6 | 1831.4 | 2.06x |
| 2 | 2 | 614.2 | 1178.8 | 1.92x |
| 2 | 4 | 348.1 | 532.0 | 1.53x |
| 4 | 1 | 644.1 | 989.5 | 1.54x |
| 4 | 4 | 268.9 | 402.9 | 1.50x |
| 8 | 1 | 458.4 | 813.3 | 1.77x |
| 8 | 8 | 106.9 | 88.1 | 0.82x |
| 16 | 1 | 265.8 | 404.7 | 1.52x |
| 16 | 16 | 30.2 | 31.7 | 1.05x |

Zig wins across nearly the whole grid, by the same kind of margin as the
single-threaded `simd_deint` stage - unsurprising since the per-shard
compute is the same kernel, just called with a sub-range instead of the
full antenna. The gap **narrows sharply as `nb_rx*nb_tx` grows**, same
trend as the single-threaded table above: at 16 threads oversubscribed by
`nb_rx=nb_tx=16` (256 antenna pairs, way more jobs than pool threads),
the two converge to within 5%, and at `8/8` Mojo briefly comes out ahead
(0.82x) - the only point in either table where that happens. This looks
like scheduling-noise/pool-contention territory rather than a compute
difference: at high thread-and-job counts the bottleneck shifts from
per-shard vector throughput (where Zig's compiled code has a real edge)
to pool dispatch/join overhead and memory-bandwidth contention shared by
both languages equally, so the language-level gap that dominates at
low antenna counts gets drowned out.

Correctness: `zig_channel_convolution_simd`/`_simd_deint` are **bit-exact**
against the scalar reference across the whole grid (0 mismatches, not
just "close") - unlike the Mojo kernels, which differ from their own
scalar reference by up to ±1 LSB from FMA-fusion/re-association noise (see
`../mojo/README.md`). Not obviously better or worse, just a different
rounding path through a different set of fused/unfused instructions.

## Why is the explicit-SIMD stage so much wider a gap than the others?

Disassembly of `zig_channel_convolution_simd` (`objdump -d
libchannel_pipeline_zig_kernel.so --disassemble=zig_channel_convolution_simd`)
confirms real 512-bit AVX-512 (`%zmm0`-`%zmm9`, `vmulps`/`vsubps`/`vaddps`
at full width) - LLVM vectorized the plain "fill a `[16]i16` array in a
loop, then cast to `@Vector(16, i16)`" pattern into genuine wide SIMD with
no help beyond the type annotation, the same way it vectorized Mojo's
purpose-built `strided_load` intrinsic - but apparently found a
cheaper/better instruction sequence doing it (no fused multiply-add was
emitted here either - LLVM chose separate `vmulps`+`vaddps`/`vsubps`
- so the gap isn't about FMA use). The basic and deinterleaved stages,
where both languages are closer, are the ones with less room for
instruction-selection differences to matter: `basic` is dominated by
whatever narrow trick the compiler finds on scalar code (small,
similar-magnitude gains either way), and `deint` is dominated by the
memory-bound contiguous-load pattern once the gather is gone (both
languages saturate on that in a similar way as `nb_tx`/`nb_rx` grow, per
the narrowing Zig/Mojo ratio there). The strided-gather stage is the one
place where the two compilers' code for the *same conceptual operation*
diverged the most, and neither Mojo's dedicated intrinsic nor an
unassisted Zig loop needed hand-written intrinsics to get there.
