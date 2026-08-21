from std.ffi import external_call
from std.memory import UnsafePointer
from std.sys import simd_width_of

alias FPtr = UnsafePointer[Float32, MutUnsafeAnyOrigin]
alias IPtr = UnsafePointer[Int16, MutUnsafeAnyOrigin]
# Native float32 SIMD width for the build target (16 lanes = AVX-512 on this
# machine). The compiler's auto-vectorizer only found a 2-lane (128-bit)
# packed complex-multiply trick on its own (see mojo/README.md) - this is
# what's left on the table by asking it explicitly instead.
alias HW = simd_width_of[Float32]()

# channel: interleaved (r,i) float32 pairs, size nb_rx*nb_tx*channel_length*2
#          (this is a `cf_t*` from C reinterpreted as float*)
# tx:      interleaved (r,i) int16 pairs, size nb_tx*num_input_samples*2
#          (a `c16_t*` from C reinterpreted as int16_t*)
# rx:      interleaved (r,i) int16 pairs, size nb_rx*num_samples*2 (output)
def convolve_one_antenna_samples(
    channel: FPtr,
    tx: IPtr,
    rx: IPtr,
    num_input_samples: Int,
    num_samples: Int,
    channel_length: Int,
    nb_tx: Int,
    rx_ant: Int,
    i_start: Int,
    i_end: Int,
):
    for i in range(i_start, i_end):
        var acc_r: Float32 = 0.0
        var acc_i: Float32 = 0.0
        for tx_ant in range(nb_tx):
            var tx_base = tx_ant * num_input_samples
            var chan_base = (tx_ant + nb_tx * rx_ant) * channel_length
            for l in range(channel_length):
                var idx = i + (channel_length - 1) - l
                var tx_r = Float32(tx[(tx_base + idx) * 2])
                var tx_i = Float32(tx[(tx_base + idx) * 2 + 1])
                var ch_r = channel[(chan_base + l) * 2]
                var ch_i = channel[(chan_base + l) * 2 + 1]
                acc_r += tx_r * ch_r - tx_i * ch_i
                acc_i += tx_r * ch_i + tx_i * ch_r
        rx[(rx_ant * num_samples + i) * 2] = Int16(acc_r)
        rx[(rx_ant * num_samples + i) * 2 + 1] = Int16(acc_i)


def convolve_range(
    channel: FPtr,
    tx: IPtr,
    rx: IPtr,
    num_input_samples: Int,
    num_samples: Int,
    channel_length: Int,
    nb_tx: Int,
    rx_start: Int,
    rx_end: Int,
):
    for rx_ant in range(rx_start, rx_end):
        convolve_one_antenna_samples(
            channel, tx, rx, num_input_samples, num_samples, channel_length, nb_tx, rx_ant, 0, num_samples
        )


# Explicit-SIMD version of convolve_one_antenna_samples: processes HW output
# samples per tap-loop iteration (a real FIR/sliding-window vectorization -
# for a fixed tap, HW consecutive output samples need HW *contiguous* input
# samples, loaded/stored via strided_load/strided_store since tx/rx store
# (r,i) interleaved). Falls back to the scalar version for the
# less-than-HW-samples tail.
def convolve_one_antenna_samples_simd(
    channel: FPtr,
    tx: IPtr,
    rx: IPtr,
    num_input_samples: Int,
    num_samples: Int,
    channel_length: Int,
    nb_tx: Int,
    rx_ant: Int,
    i_start: Int,
    i_end: Int,
):
    var i = i_start
    while i + HW <= i_end:
        var acc_r = SIMD[DType.float32, HW](0)
        var acc_i = SIMD[DType.float32, HW](0)
        for tx_ant in range(nb_tx):
            var tx_base = tx_ant * num_input_samples
            var chan_base = (tx_ant + nb_tx * rx_ant) * channel_length
            for l in range(channel_length):
                var idx0 = (tx_base + i + (channel_length - 1) - l) * 2
                var tx_r = (tx + idx0).strided_load[width=HW](2).cast[DType.float32]()
                var tx_i = (tx + idx0 + 1).strided_load[width=HW](2).cast[DType.float32]()
                var ch_r = channel[(chan_base + l) * 2]
                var ch_i = channel[(chan_base + l) * 2 + 1]
                acc_r += tx_r * ch_r - tx_i * ch_i
                acc_i += tx_r * ch_i + tx_i * ch_r
        var out_r = acc_r.cast[DType.int16]()
        var out_i = acc_i.cast[DType.int16]()
        var out_base = (rx_ant * num_samples + i) * 2
        (rx + out_base).strided_store[width=HW](out_r, 2)
        (rx + out_base + 1).strided_store[width=HW](out_i, 2)
        i += HW
    if i < i_end:
        convolve_one_antenna_samples(
            channel, tx, rx, num_input_samples, num_samples, channel_length, nb_tx, rx_ant, i, i_end
        )


# Deinterleaves nb_tx*num_input_samples complex tx samples (interleaved
# r,i,r,i,... in `tx`) into two flat contiguous buffers `tx_r`/`tx_i`.
def deinterleave_tx(tx: IPtr, tx_r: IPtr, tx_i: IPtr, n: Int):
    var i = 0
    while i + HW <= n:
        var r = (tx + i * 2).strided_load[width=HW](2)
        var im = (tx + i * 2 + 1).strided_load[width=HW](2)
        (tx_r + i).store(r)
        (tx_i + i).store(im)
        i += HW
    while i < n:
        tx_r[i] = tx[i * 2]
        tx_i[i] = tx[i * 2 + 1]
        i += 1


# Same vectorization as convolve_one_antenna_samples_simd, but reads tx
# real/imag parts from separate pre-deinterleaved contiguous buffers
# (see deinterleave_tx) instead of gathering them out of interleaved memory
# with strided_load on every tap of every sample block. strided_load with a
# stride is dramatically more expensive per element than a plain contiguous
# load, and here it was being repeated channel_length*nb_tx times per
# sample block for data that's the same across the whole antenna - doing it
# once per call instead is a ~4-7x win on top of convolve_one_antenna_samples_simd
# (see mojo/README.md).
def convolve_one_antenna_samples_simd_deint(
    channel: FPtr,
    tx_r: IPtr,
    tx_i: IPtr,
    rx: IPtr,
    num_input_samples: Int,
    num_samples: Int,
    channel_length: Int,
    nb_tx: Int,
    rx_ant: Int,
    i_start: Int,
    i_end: Int,
):
    var i = i_start
    while i + HW <= i_end:
        var acc_r = SIMD[DType.float32, HW](0)
        var acc_i = SIMD[DType.float32, HW](0)
        for tx_ant in range(nb_tx):
            var tx_base = tx_ant * num_input_samples
            var chan_base = (tx_ant + nb_tx * rx_ant) * channel_length
            for l in range(channel_length):
                var idx0 = tx_base + i + (channel_length - 1) - l
                var vr = (tx_r + idx0).load[width=HW]().cast[DType.float32]()
                var vi = (tx_i + idx0).load[width=HW]().cast[DType.float32]()
                var ch_r = channel[(chan_base + l) * 2]
                var ch_i = channel[(chan_base + l) * 2 + 1]
                acc_r += vr * ch_r - vi * ch_i
                acc_i += vr * ch_i + vi * ch_r
        var out_r = acc_r.cast[DType.int16]()
        var out_i = acc_i.cast[DType.int16]()
        var out_base = (rx_ant * num_samples + i) * 2
        (rx + out_base).strided_store[width=HW](out_r, 2)
        (rx + out_base + 1).strided_store[width=HW](out_i, 2)
        i += HW
    for ii in range(i, i_end):
        var acc_r: Float32 = 0.0
        var acc_i: Float32 = 0.0
        for tx_ant in range(nb_tx):
            var tx_base = tx_ant * num_input_samples
            var chan_base = (tx_ant + nb_tx * rx_ant) * channel_length
            for l in range(channel_length):
                var idx = tx_base + ii + (channel_length - 1) - l
                var vr = Float32(tx_r[idx])
                var vi = Float32(tx_i[idx])
                var ch_r = channel[(chan_base + l) * 2]
                var ch_i = channel[(chan_base + l) * 2 + 1]
                acc_r += vr * ch_r - vi * ch_i
                acc_i += vr * ch_i + vi * ch_r
        rx[(rx_ant * num_samples + ii) * 2] = Int16(acc_r)
        rx[(rx_ant * num_samples + ii) * 2 + 1] = Int16(acc_i)


def convolve_range_simd(
    channel: FPtr,
    tx: IPtr,
    rx: IPtr,
    num_input_samples: Int,
    num_samples: Int,
    channel_length: Int,
    nb_tx: Int,
    rx_start: Int,
    rx_end: Int,
):
    for rx_ant in range(rx_start, rx_end):
        convolve_one_antenna_samples_simd(
            channel, tx, rx, num_input_samples, num_samples, channel_length, nb_tx, rx_ant, 0, num_samples
        )


# ---------------------------------------------------------------------------
# Single-threaded variant, exported with a C ABI for direct use from C/C++.
# ---------------------------------------------------------------------------
@export(ABI="C")
def mojo_channel_convolution_basic(
    channel: FPtr,
    tx: IPtr,
    rx: IPtr,
    num_input_samples: Int32,
    num_samples: Int32,
    channel_length: Int32,
    nb_tx: Int32,
    nb_rx: Int32,
):
    convolve_range(
        channel,
        tx,
        rx,
        Int(num_input_samples),
        Int(num_samples),
        Int(channel_length),
        Int(nb_tx),
        0,
        Int(nb_rx),
    )


# ---------------------------------------------------------------------------
# Single-threaded, explicit-SIMD variant: same math as
# mojo_channel_convolution_basic, but explicitly vectorized to the target's
# native float32 SIMD width (see convolve_one_antenna_samples_simd above)
# instead of relying on what the compiler's auto-vectorizer finds on its
# own. A separate entry point (rather than replacing
# mojo_channel_convolution_basic) so the two are directly comparable.
# ---------------------------------------------------------------------------
@export(ABI="C")
def mojo_channel_convolution_simd(
    channel: FPtr,
    tx: IPtr,
    rx: IPtr,
    num_input_samples: Int32,
    num_samples: Int32,
    channel_length: Int32,
    nb_tx: Int32,
    nb_rx: Int32,
):
    convolve_range_simd(
        channel,
        tx,
        rx,
        Int(num_input_samples),
        Int(num_samples),
        Int(channel_length),
        Int(nb_tx),
        0,
        Int(nb_rx),
    )


# ---------------------------------------------------------------------------
# Single-threaded, pre-deinterleaved-SIMD variant: deinterleaves tx into
# scratch real/imag buffers once, then runs convolve_one_antenna_samples_simd_deint
# across all rx antennas. Allocates its own scratch (freed when the List
# goes out of scope at the end of the call).
# ---------------------------------------------------------------------------
@export(ABI="C")
def mojo_channel_convolution_simd_deint(
    channel: FPtr,
    tx: IPtr,
    rx: IPtr,
    num_input_samples: Int32,
    num_samples: Int32,
    channel_length: Int32,
    nb_tx: Int32,
    nb_rx: Int32,
):
    var n = Int(nb_tx) * Int(num_input_samples)
    var tx_r_buf = List[Int16]()
    var tx_i_buf = List[Int16]()
    for _ in range(n):
        tx_r_buf.append(0)
        tx_i_buf.append(0)
    var tx_r = IPtr(unsafe_from_address=Int(tx_r_buf.unsafe_ptr()))
    var tx_i = IPtr(unsafe_from_address=Int(tx_i_buf.unsafe_ptr()))

    deinterleave_tx(tx, tx_r, tx_i, n)
    for rx_ant in range(Int(nb_rx)):
        convolve_one_antenna_samples_simd_deint(
            channel,
            tx_r,
            tx_i,
            rx,
            Int(num_input_samples),
            Int(num_samples),
            Int(channel_length),
            Int(nb_tx),
            rx_ant,
            0,
            Int(num_samples),
        )
    _ = len(tx_r_buf)
    _ = len(tx_i_buf)


# Exported so an external caller (e.g. the tpool benchmark) can do the
# deinterleave once, on the dispatching thread, before farming per-shard
# jobs out to the pool - deinterleaving is shared work across all rx
# antennas/jobs, so it must happen once per call, not once per job.
@export(ABI="C")
def mojo_deinterleave_tx(tx: IPtr, tx_r: IPtr, tx_i: IPtr, n: Int32):
    deinterleave_tx(tx, tx_r, tx_i, Int(n))


# No threading: computes samples [i_start, i_end) of one rx antenna from
# already-deinterleaved tx_r/tx_i (see mojo_deinterleave_tx). Used by
# BM_channel_convolution_mojo_tpool_simd_deint.
@export(ABI="C")
def mojo_convolve_samples_simd_deint(
    channel: FPtr,
    tx_r: IPtr,
    tx_i: IPtr,
    rx: IPtr,
    num_input_samples: Int32,
    num_samples: Int32,
    channel_length: Int32,
    nb_tx: Int32,
    rx_ant: Int32,
    i_start: Int32,
    i_end: Int32,
):
    convolve_one_antenna_samples_simd_deint(
        channel,
        tx_r,
        tx_i,
        rx,
        Int(num_input_samples),
        Int(num_samples),
        Int(channel_length),
        Int(nb_tx),
        Int(rx_ant),
        Int(i_start),
        Int(i_end),
    )


# ---------------------------------------------------------------------------
# Plain range kernel, no threading at all: lets an external (C-managed)
# thread pool call straight into the Mojo compute kernel per shard, so the
# thread pool's setup/teardown cost is paid once by the caller instead of
# per call - see BM_channel_convolution_mojo_tpool in
# benchmark_channel_pipeline.cpp, which reuses the same persistent tpool
# that BM_channel_convolution_tpool uses.
# ---------------------------------------------------------------------------
@export(ABI="C")
def mojo_convolve_range(
    channel: FPtr,
    tx: IPtr,
    rx: IPtr,
    num_input_samples: Int32,
    num_samples: Int32,
    channel_length: Int32,
    nb_tx: Int32,
    rx_start: Int32,
    rx_end: Int32,
):
    convolve_range(
        channel,
        tx,
        rx,
        Int(num_input_samples),
        Int(num_samples),
        Int(channel_length),
        Int(nb_tx),
        Int(rx_start),
        Int(rx_end),
    )


# Finer-grained no-threading kernel: one rx antenna, a sample sub-range.
# channel_pipeline.c's tpool job splits each rx antenna into
# ceil(num_threads/nb_rx) sample-range jobs so the pool always has ~16 jobs
# to chew on regardless of nb_rx; BM_channel_convolution_mojo_tpool mirrors
# that using this entry point instead of mojo_convolve_range's
# whole-antennas-only split, so small nb_rx doesn't leave most of the pool
# idle.
@export(ABI="C")
def mojo_convolve_samples(
    channel: FPtr,
    tx: IPtr,
    rx: IPtr,
    num_input_samples: Int32,
    num_samples: Int32,
    channel_length: Int32,
    nb_tx: Int32,
    rx_ant: Int32,
    i_start: Int32,
    i_end: Int32,
):
    convolve_one_antenna_samples(
        channel,
        tx,
        rx,
        Int(num_input_samples),
        Int(num_samples),
        Int(channel_length),
        Int(nb_tx),
        Int(rx_ant),
        Int(i_start),
        Int(i_end),
    )


# Explicit-SIMD counterpart to mojo_convolve_samples, for
# BM_channel_convolution_mojo_tpool_simd.
@export(ABI="C")
def mojo_convolve_samples_simd(
    channel: FPtr,
    tx: IPtr,
    rx: IPtr,
    num_input_samples: Int32,
    num_samples: Int32,
    channel_length: Int32,
    nb_tx: Int32,
    rx_ant: Int32,
    i_start: Int32,
    i_end: Int32,
):
    convolve_one_antenna_samples_simd(
        channel,
        tx,
        rx,
        Int(num_input_samples),
        Int(num_samples),
        Int(channel_length),
        Int(nb_tx),
        Int(rx_ant),
        Int(i_start),
        Int(i_end),
    )


# ---------------------------------------------------------------------------
# Multithreaded variant: Mojo itself spawns/joins the worker threads (via
# pthread_create/pthread_join through std.ffi.external_call - this build's
# stdlib does not expose a higher-level CPU parallel-for), splitting the rx
# antennas into contiguous shards, one per OS thread.
# ---------------------------------------------------------------------------
struct ConvArgs(Copyable, Movable):
    var channel_addr: Int
    var tx_addr: Int
    var rx_addr: Int
    var num_input_samples: Int
    var num_samples: Int
    var channel_length: Int
    var nb_tx: Int
    var rx_start: Int
    var rx_end: Int

    def __init__(
        out self,
        channel_addr: Int,
        tx_addr: Int,
        rx_addr: Int,
        num_input_samples: Int,
        num_samples: Int,
        channel_length: Int,
        nb_tx: Int,
        rx_start: Int,
        rx_end: Int,
    ):
        self.channel_addr = channel_addr
        self.tx_addr = tx_addr
        self.rx_addr = rx_addr
        self.num_input_samples = num_input_samples
        self.num_samples = num_samples
        self.channel_length = channel_length
        self.nb_tx = nb_tx
        self.rx_start = rx_start
        self.rx_end = rx_end


@export(ABI="C")
def mojo_conv_worker(raw_arg: Int64) -> Int64:
    var p = UnsafePointer[ConvArgs, MutUnsafeAnyOrigin](unsafe_from_address=Int(raw_arg))
    var a = p[0].copy()
    var channel = FPtr(unsafe_from_address=a.channel_addr)
    var tx = IPtr(unsafe_from_address=a.tx_addr)
    var rx = IPtr(unsafe_from_address=a.rx_addr)
    convolve_range(
        channel,
        tx,
        rx,
        a.num_input_samples,
        a.num_samples,
        a.channel_length,
        a.nb_tx,
        a.rx_start,
        a.rx_end,
    )
    return 0


@export(ABI="C")
def mojo_channel_convolution_parallel(
    channel: FPtr,
    tx: IPtr,
    rx: IPtr,
    num_input_samples: Int32,
    num_samples: Int32,
    channel_length: Int32,
    nb_tx: Int32,
    nb_rx: Int32,
    max_threads: Int32,
):
    var nb_rx_i = Int(nb_rx)
    var nthreads = nb_rx_i
    if nthreads > Int(max_threads):
        nthreads = Int(max_threads)
    if nthreads < 1:
        nthreads = 1

    if nthreads == 1:
        # A single shard covering all rx antennas: run it directly on the
        # calling thread instead of going through pthread_create/join for
        # just one worker. (A lone pthread_create/join round trip through
        # this List[ConvArgs]-backed plumbing was observed to segfault
        # intermittently in this Mojo 1.0.0/MAX 26.5 nightly - see
        # mojo/README.md. Two or more threads reliably works, so the
        # threaded path below is unchanged for nthreads >= 2.)
        convolve_range(
            channel,
            tx,
            rx,
            Int(num_input_samples),
            Int(num_samples),
            Int(channel_length),
            Int(nb_tx),
            0,
            nb_rx_i,
        )
        return

    var channel_addr = Int(channel)
    var tx_addr = Int(tx)
    var rx_addr = Int(rx)

    var contexts = List[ConvArgs]()
    for t in range(nthreads):
        var rx_start = (nb_rx_i * t) // nthreads
        var rx_end = (nb_rx_i * (t + 1)) // nthreads
        contexts.append(
            ConvArgs(
                channel_addr,
                tx_addr,
                rx_addr,
                Int(num_input_samples),
                Int(num_samples),
                Int(channel_length),
                Int(nb_tx),
                rx_start,
                rx_end,
            )
        )
    var ctx_base = contexts.unsafe_ptr()

    var tids = List[Int64]()
    for _ in range(nthreads):
        tids.append(0)
    var tids_base = tids.unsafe_ptr()

    for t in range(nthreads):
        var addr: Int64 = Int64(Int(ctx_base + t))
        _ = external_call["pthread_create", Int32](
            (tids_base + t), Int64(0), mojo_conv_worker, addr
        )
    for t in range(nthreads):
        _ = external_call["pthread_join", Int32](tids[t], Int64(0))
    # Keep `contexts`/`tids` alive (i.e. not destroyed under Mojo's
    # last-use-based ASAP destruction) until every worker has joined and is
    # done reading through ctx_base/tids_base.
    _ = len(contexts)
    _ = len(tids)
