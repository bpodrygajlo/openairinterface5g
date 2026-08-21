# Measures the three pieces of a discrete-GPU-style call separately, at
# nb_rx=nb_tx=4/num_samples=61440: the H2D copy of tx alone, the kernel
# alone (both timed via DeviceContext buffers, the traditional model), and
# the full copy-in + kernel + copy-out round trip. On this ATS hardware
# the H2D copy of tx (983KB) alone cost more than half the kernel's own
# time, and the full round trip cost roughly 2x the kernel-only time -
# this is what motivated checking whether the copy could be skipped
# entirely (see gpu_zerocopy.mojo/gpu_zerocopy_cuda.cu: yes, and it's
# exactly as fast as the kernel-only number here, because there's no copy
# left to pay for).
from std.gpu import thread_idx, block_idx, block_dim
from std.memory import UnsafePointer
from std.time import perf_counter_ns
from std.random import random_si64, seed
from max.gpu.host import DeviceContext

alias FPtr = UnsafePointer[Float32, MutUnsafeAnyOrigin]
alias IPtr = UnsafePointer[Int16, MutUnsafeAnyOrigin]

def conv_basic_kernel(
    channel: FPtr, tx: IPtr, rx: IPtr,
    num_input_samples: Int32, num_samples: Int32, channel_length: Int32, nb_tx: Int32, nb_rx: Int32,
):
    var tid = Int(block_idx.x * block_dim.x + thread_idx.x)
    var total = Int(nb_rx) * Int(num_samples)
    if tid >= total:
        return
    var rx_ant = tid // Int(num_samples)
    var i = tid % Int(num_samples)
    var acc_r: Float32 = 0.0
    var acc_i: Float32 = 0.0
    for tx_ant in range(Int(nb_tx)):
        var tx_base = tx_ant * Int(num_input_samples)
        var chan_base = (tx_ant + Int(nb_tx) * rx_ant) * Int(channel_length)
        for l in range(Int(channel_length)):
            var idx = i + (Int(channel_length) - 1) - l
            var tx_r = Float32(tx[(tx_base + idx) * 2])
            var tx_i = Float32(tx[(tx_base + idx) * 2 + 1])
            var ch_r = channel[(chan_base + l) * 2]
            var ch_i = channel[(chan_base + l) * 2 + 1]
            acc_r += tx_r * ch_r - tx_i * ch_i
            acc_i += tx_r * ch_i + tx_i * ch_r
    rx[(rx_ant * Int(num_samples) + i) * 2] = Int16(acc_r)
    rx[(rx_ant * Int(num_samples) + i) * 2 + 1] = Int16(acc_i)

def main() raises:
    seed(1234)
    var ctx = DeviceContext()
    var nb_rx = 4
    var nb_tx = 4
    var num_samples = 61440
    var channel_length = 16
    var num_input_samples = num_samples + channel_length - 1
    var iterations = 30

    var h_tx = List[Int16]()
    for _ in range(nb_tx * num_input_samples * 2):
        h_tx.append(Int16(random_si64(-20, 20)))
    var h_channel = List[Float32]()
    for _ in range(nb_rx * nb_tx * channel_length * 2):
        h_channel.append(Float32(random_si64(-20, 20)))

    var d_tx = ctx.enqueue_create_buffer[DType.int16](nb_tx * num_input_samples * 2)
    var d_channel = ctx.enqueue_create_buffer[DType.float32](nb_rx * nb_tx * channel_length * 2)
    var d_rx = ctx.enqueue_create_buffer[DType.int16](nb_rx * num_samples * 2)

    # time just the H2D copy of tx (the part that changes every real call)
    ctx.enqueue_copy(d_tx, h_tx.unsafe_ptr())
    ctx.synchronize()
    var best_copy: Int = -1
    for _ in range(iterations):
        var t0 = perf_counter_ns()
        ctx.enqueue_copy(d_tx, h_tx.unsafe_ptr())
        ctx.synchronize()
        var t1 = perf_counter_ns()
        var e = t1 - t0
        if best_copy < 0 or e < best_copy:
            best_copy = e
    print("H2D copy of tx (", nb_tx * num_input_samples * 2 * 2, "bytes) best_ns=", best_copy)

    ctx.enqueue_copy(d_channel, h_channel.unsafe_ptr())
    ctx.synchronize()

    var threads = 512
    var total = nb_rx * num_samples
    var blocks = (total + threads - 1) // threads
    ctx.enqueue_function[conv_basic_kernel](
        d_channel.unsafe_ptr(), d_tx.unsafe_ptr(), d_rx.unsafe_ptr(),
        Int32(num_input_samples), Int32(num_samples), Int32(channel_length), Int32(nb_tx), Int32(nb_rx),
        grid_dim=blocks, block_dim=threads,
    )
    ctx.synchronize()
    var best_kernel: Int = -1
    for _ in range(iterations):
        var t0 = perf_counter_ns()
        ctx.enqueue_function[conv_basic_kernel](
            d_channel.unsafe_ptr(), d_tx.unsafe_ptr(), d_rx.unsafe_ptr(),
            Int32(num_input_samples), Int32(num_samples), Int32(channel_length), Int32(nb_tx), Int32(nb_rx),
            grid_dim=blocks, block_dim=threads,
        )
        ctx.synchronize()
        var t1 = perf_counter_ns()
        var e = t1 - t0
        if best_kernel < 0 or e < best_kernel:
            best_kernel = e
    print("kernel-only best_ns=", best_kernel)

    # copy+kernel+copy-back, the full "realistic per-call" round trip
    var h_rx = List[Int16]()
    for _ in range(nb_rx * num_samples * 2):
        h_rx.append(0)
    var best_full: Int = -1
    for _ in range(iterations):
        var t0 = perf_counter_ns()
        ctx.enqueue_copy(d_tx, h_tx.unsafe_ptr())
        ctx.enqueue_function[conv_basic_kernel](
            d_channel.unsafe_ptr(), d_tx.unsafe_ptr(), d_rx.unsafe_ptr(),
            Int32(num_input_samples), Int32(num_samples), Int32(channel_length), Int32(nb_tx), Int32(nb_rx),
            grid_dim=blocks, block_dim=threads,
        )
        ctx.enqueue_copy(h_rx.unsafe_ptr(), d_rx)
        ctx.synchronize()
        var t1 = perf_counter_ns()
        var e = t1 - t0
        if best_full < 0 or e < best_full:
            best_full = e
    print("full round trip (copy-in + kernel + copy-out) best_ns=", best_full)
