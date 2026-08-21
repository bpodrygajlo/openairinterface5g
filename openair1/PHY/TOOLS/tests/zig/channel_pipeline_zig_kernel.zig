// Zig counterpart to ../mojo/channel_pipeline_mojo_kernel.mojo - same
// three-stage progression (basic auto-vec, explicit SIMD, SIMD +
// deinterleave-once), same algorithm, same flat interleaved-(r,i) buffer
// layout, so the two can be benchmarked side by side in
// benchmark_channel_pipeline.cpp under CHANNEL_SIM_ZIG. Written to answer
// a specific question: does Zig's `@Vector` + `std.simd.suggestVectorLength`
// give LLVM the same level of information Mojo's `SIMD[DType, W]` +
// `simd_width_of` does, and does it reach the same performance? On this
// machine: yes, and at the pre-deinterleave "explicit SIMD" stage, better -
// see the README for numbers and a disassembly-level explanation.
const std = @import("std");

// Native float32 SIMD width for the build target - the same query Mojo's
// `simd_width_of[Float32]()` performs (16 on this AVX-512 machine).
const W: comptime_int = std.simd.suggestVectorLength(f32) orelse 4;
const Vf = @Vector(W, f32);
const Vi = @Vector(W, i16);

// channel: interleaved (r,i) float32 pairs, size nb_rx*nb_tx*channel_length*2
// tx:      interleaved (r,i) int16 pairs, size nb_tx*num_input_samples*2
// rx:      interleaved (r,i) int16 pairs, size nb_rx*num_samples*2 (output)

// --- basic: plain scalar nested loops, whatever LLVM's auto-vectorizer finds ---
export fn zig_channel_convolution_basic(
    channel: [*]const f32,
    tx: [*]const i16,
    rx: [*]i16,
    num_input_samples: i32,
    num_samples: i32,
    channel_length: i32,
    nb_tx: i32,
    nb_rx: i32,
) callconv(.c) void {
    const ns: usize = @intCast(num_samples);
    const nis: usize = @intCast(num_input_samples);
    const cl: usize = @intCast(channel_length);
    const ntx: usize = @intCast(nb_tx);
    const nrx: usize = @intCast(nb_rx);

    for (0..nrx) |rx_ant| {
        for (0..ns) |i| {
            var acc_r: f32 = 0.0;
            var acc_i: f32 = 0.0;
            for (0..ntx) |tx_ant| {
                const tx_base = tx_ant * nis;
                const chan_base = (tx_ant + ntx * rx_ant) * cl;
                for (0..cl) |l| {
                    const idx = i + (cl - 1) - l;
                    const tx_r: f32 = @floatFromInt(tx[(tx_base + idx) * 2]);
                    const tx_i: f32 = @floatFromInt(tx[(tx_base + idx) * 2 + 1]);
                    const ch_r = channel[(chan_base + l) * 2];
                    const ch_i = channel[(chan_base + l) * 2 + 1];
                    acc_r += tx_r * ch_r - tx_i * ch_i;
                    acc_i += tx_r * ch_i + tx_i * ch_r;
                }
            }
            rx[(rx_ant * ns + i) * 2] = @intFromFloat(acc_r);
            rx[(rx_ant * ns + i) * 2 + 1] = @intFromFloat(acc_i);
        }
    }
}

// --- explicit SIMD: W output samples per tap-loop iteration, gathering
// (r,i) apart from interleaved memory via a manual stride-2 load per tap
// (the same shape as Mojo's strided_load - a plain scalar loop that casts
// to a vector, to see whether LLVM lowers it the same way through Zig as
// it did through Mojo's dedicated intrinsic). Falls back to the scalar
// kernel for the tail. ---
inline fn loadStrided2(ptr: [*]const i16, base: usize) Vi {
    var buf: [W]i16 = undefined;
    inline for (0..W) |k| {
        buf[k] = ptr[base + k * 2];
    }
    return buf;
}

inline fn storeStrided2(ptr: [*]i16, base: usize, v: Vi) void {
    inline for (0..W) |k| {
        ptr[base + k * 2] = v[k];
    }
}

fn convolveOneAntennaSimd(
    channel: [*]const f32,
    tx: [*]const i16,
    rx: [*]i16,
    nis: usize,
    ns: usize,
    cl: usize,
    ntx: usize,
    rx_ant: usize,
    i_start: usize,
    i_end: usize,
) void {
    var i = i_start;
    while (i + W <= i_end) : (i += W) {
        var acc_r: Vf = @splat(0.0);
        var acc_i: Vf = @splat(0.0);
        for (0..ntx) |tx_ant| {
            const tx_base = tx_ant * nis;
            const chan_base = (tx_ant + ntx * rx_ant) * cl;
            for (0..cl) |l| {
                const idx0 = (tx_base + i + (cl - 1) - l) * 2;
                const tx_r_i: Vi = loadStrided2(tx, idx0);
                const tx_i_i: Vi = loadStrided2(tx, idx0 + 1);
                const tx_r: Vf = @floatFromInt(tx_r_i);
                const tx_ii: Vf = @floatFromInt(tx_i_i);
                const ch_r: Vf = @splat(channel[(chan_base + l) * 2]);
                const ch_i: Vf = @splat(channel[(chan_base + l) * 2 + 1]);
                acc_r += tx_r * ch_r - tx_ii * ch_i;
                acc_i += tx_r * ch_i + tx_ii * ch_r;
            }
        }
        const out_r: Vi = @intFromFloat(acc_r);
        const out_i: Vi = @intFromFloat(acc_i);
        const out_base = (rx_ant * ns + i) * 2;
        storeStrided2(rx, out_base, out_r);
        storeStrided2(rx, out_base + 1, out_i);
    }
    // scalar tail
    while (i < i_end) : (i += 1) {
        var acc_r: f32 = 0.0;
        var acc_i: f32 = 0.0;
        for (0..ntx) |tx_ant| {
            const tx_base = tx_ant * nis;
            const chan_base = (tx_ant + ntx * rx_ant) * cl;
            for (0..cl) |l| {
                const idx = i + (cl - 1) - l;
                const tx_r: f32 = @floatFromInt(tx[(tx_base + idx) * 2]);
                const tx_i: f32 = @floatFromInt(tx[(tx_base + idx) * 2 + 1]);
                const ch_r = channel[(chan_base + l) * 2];
                const ch_i = channel[(chan_base + l) * 2 + 1];
                acc_r += tx_r * ch_r - tx_i * ch_i;
                acc_i += tx_r * ch_i + tx_i * ch_r;
            }
        }
        rx[(rx_ant * ns + i) * 2] = @intFromFloat(acc_r);
        rx[(rx_ant * ns + i) * 2 + 1] = @intFromFloat(acc_i);
    }
}

export fn zig_channel_convolution_simd(
    channel: [*]const f32,
    tx: [*]const i16,
    rx: [*]i16,
    num_input_samples: i32,
    num_samples: i32,
    channel_length: i32,
    nb_tx: i32,
    nb_rx: i32,
) callconv(.c) void {
    const ns: usize = @intCast(num_samples);
    const nis: usize = @intCast(num_input_samples);
    const cl: usize = @intCast(channel_length);
    const ntx: usize = @intCast(nb_tx);
    const nrx: usize = @intCast(nb_rx);
    for (0..nrx) |rx_ant| {
        convolveOneAntennaSimd(channel, tx, rx, nis, ns, cl, ntx, rx_ant, 0, ns);
    }
}

// --- SIMD + deinterleave-once: same as above, but tx real/imag parts are
// deinterleaved into flat contiguous scratch buffers once per call, so the
// tap loop does plain contiguous vector loads instead of a strided gather
// per tap per sample-block. Same algorithmic lever as
// mojo_channel_convolution_simd_deint. ---
fn deinterleaveTx(tx: [*]const i16, tx_r: [*]i16, tx_i: [*]i16, n: usize) void {
    var i: usize = 0;
    while (i + W <= n) : (i += W) {
        const r: Vi = loadStrided2(tx, i * 2);
        const im: Vi = loadStrided2(tx, i * 2 + 1);
        const rp: *[W]i16 = @ptrCast(tx_r + i);
        const ip: *[W]i16 = @ptrCast(tx_i + i);
        rp.* = r;
        ip.* = im;
    }
    while (i < n) : (i += 1) {
        tx_r[i] = tx[i * 2];
        tx_i[i] = tx[i * 2 + 1];
    }
}

fn convolveOneAntennaSimdDeint(
    channel: [*]const f32,
    tx_r: [*]const i16,
    tx_i: [*]const i16,
    rx: [*]i16,
    nis: usize,
    ns: usize,
    cl: usize,
    ntx: usize,
    rx_ant: usize,
    i_start: usize,
    i_end: usize,
) void {
    var i = i_start;
    while (i + W <= i_end) : (i += W) {
        var acc_r: Vf = @splat(0.0);
        var acc_i: Vf = @splat(0.0);
        for (0..ntx) |tx_ant| {
            const tx_base = tx_ant * nis;
            const chan_base = (tx_ant + ntx * rx_ant) * cl;
            for (0..cl) |l| {
                const idx0 = tx_base + i + (cl - 1) - l;
                const vr_p: *const [W]i16 = @ptrCast(tx_r + idx0);
                const vi_p: *const [W]i16 = @ptrCast(tx_i + idx0);
                const vr: Vf = @floatFromInt(@as(Vi, vr_p.*));
                const vi: Vf = @floatFromInt(@as(Vi, vi_p.*));
                const ch_r: Vf = @splat(channel[(chan_base + l) * 2]);
                const ch_i: Vf = @splat(channel[(chan_base + l) * 2 + 1]);
                acc_r += vr * ch_r - vi * ch_i;
                acc_i += vr * ch_i + vi * ch_r;
            }
        }
        const out_r: Vi = @intFromFloat(acc_r);
        const out_i: Vi = @intFromFloat(acc_i);
        const out_base = (rx_ant * ns + i) * 2;
        storeStrided2(rx, out_base, out_r);
        storeStrided2(rx, out_base + 1, out_i);
    }
    while (i < i_end) : (i += 1) {
        var acc_r: f32 = 0.0;
        var acc_i: f32 = 0.0;
        for (0..ntx) |tx_ant| {
            const tx_base = tx_ant * nis;
            const chan_base = (tx_ant + ntx * rx_ant) * cl;
            for (0..cl) |l| {
                const idx = tx_base + i + (cl - 1) - l;
                const vr: f32 = @floatFromInt(tx_r[idx]);
                const vi: f32 = @floatFromInt(tx_i[idx]);
                const ch_r = channel[(chan_base + l) * 2];
                const ch_i = channel[(chan_base + l) * 2 + 1];
                acc_r += vr * ch_r - vi * ch_i;
                acc_i += vr * ch_i + vi * ch_r;
            }
        }
        rx[(rx_ant * ns + i) * 2] = @intFromFloat(acc_r);
        rx[(rx_ant * ns + i) * 2 + 1] = @intFromFloat(acc_i);
    }
}

export fn zig_channel_convolution_simd_deint(
    channel: [*]const f32,
    tx: [*]const i16,
    rx: [*]i16,
    num_input_samples: i32,
    num_samples: i32,
    channel_length: i32,
    nb_tx: i32,
    nb_rx: i32,
) callconv(.c) void {
    const ns: usize = @intCast(num_samples);
    const nis: usize = @intCast(num_input_samples);
    const cl: usize = @intCast(channel_length);
    const ntx: usize = @intCast(nb_tx);
    const nrx: usize = @intCast(nb_rx);

    var gpa = std.heap.page_allocator;
    const n = ntx * nis;
    const tx_r = gpa.alloc(i16, n) catch unreachable;
    defer gpa.free(tx_r);
    const tx_i = gpa.alloc(i16, n) catch unreachable;
    defer gpa.free(tx_i);

    deinterleaveTx(tx, tx_r.ptr, tx_i.ptr, n);
    for (0..nrx) |rx_ant| {
        convolveOneAntennaSimdDeint(channel, tx_r.ptr, tx_i.ptr, rx, nis, ns, cl, ntx, rx_ant, 0, ns);
    }
}

// --- exports for an external thread pool (see
// BM_channel_convolution_zig_tpool_simd_deint in benchmark_channel_pipeline.cpp)
// - the mirror of mojo_deinterleave_tx/mojo_convolve_samples_simd_deint.
// Zig does no threading of its own here either: deinterleave is shared
// work done once by the dispatching thread, then each pool worker calls
// zig_convolve_samples_simd_deint for its own [i_start, i_end) shard of a
// single rx antenna. ---
export fn zig_deinterleave_tx(tx: [*]const i16, tx_r: [*]i16, tx_i: [*]i16, n: i32) callconv(.c) void {
    deinterleaveTx(tx, tx_r, tx_i, @intCast(n));
}

export fn zig_convolve_samples_simd_deint(
    channel: [*]const f32,
    tx_r: [*]const i16,
    tx_i: [*]const i16,
    rx: [*]i16,
    num_input_samples: i32,
    num_samples: i32,
    channel_length: i32,
    nb_tx: i32,
    rx_ant: i32,
    i_start: i32,
    i_end: i32,
) callconv(.c) void {
    convolveOneAntennaSimdDeint(
        channel,
        tx_r,
        tx_i,
        rx,
        @intCast(num_input_samples),
        @intCast(num_samples),
        @intCast(channel_length),
        @intCast(nb_tx),
        @intCast(rx_ant),
        @intCast(i_start),
        @intCast(i_end),
    );
}
