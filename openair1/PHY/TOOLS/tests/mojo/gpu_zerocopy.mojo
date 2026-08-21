# Minimal, focused demonstration that a plain host-allocated pointer can
# be handed directly to a Mojo GPU kernel launch (no DeviceContext buffer,
# no enqueue_copy) and get correct results, on ATS hardware (see
# gpu_large_mojo.mojo's header for the full explanation). Verifies
# correctness against a CPU reference, then times the zero-copy launch
# directly. Companion to gpu_zerocopy_cuda.cu (the identical trick in
# native CUDA, confirming this is a hardware/driver capability, not a
# Mojo-specific one) and gpu_copy_cost.mojo (which measures how much the
# discrete-copy alternative actually costs on this hardware).
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

def cpu_ref(channel: FPtr, tx: IPtr, rx: IPtr, num_input_samples: Int, num_samples: Int, channel_length: Int, nb_tx: Int, nb_rx: Int):
    for rx_ant in range(nb_rx):
        for i in range(num_samples):
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
    var h_rx = List[Int16]()
    var h_rx_ref = List[Int16]()
    for _ in range(nb_rx * num_samples * 2):
        h_rx.append(0)
        h_rx_ref.append(0)

    # correctness: run kernel directly on HOST pointers, no device buffers, no copies at all
    var channel_ptr = FPtr(unsafe_from_address=Int(h_channel.unsafe_ptr()))
    var tx_ptr = IPtr(unsafe_from_address=Int(h_tx.unsafe_ptr()))
    var rx_ptr = IPtr(unsafe_from_address=Int(h_rx.unsafe_ptr()))

    var threads = 512
    var total = nb_rx * num_samples
    var blocks = (total + threads - 1) // threads
    ctx.enqueue_function[conv_basic_kernel](
        channel_ptr, tx_ptr, rx_ptr,
        Int32(num_input_samples), Int32(num_samples), Int32(channel_length), Int32(nb_tx), Int32(nb_rx),
        grid_dim=blocks, block_dim=threads,
    )
    ctx.synchronize()

    cpu_ref(FPtr(unsafe_from_address=Int(h_channel.unsafe_ptr())), IPtr(unsafe_from_address=Int(h_tx.unsafe_ptr())),
            IPtr(unsafe_from_address=Int(h_rx_ref.unsafe_ptr())), num_input_samples, num_samples, channel_length, nb_tx, nb_rx)

    var mism = 0
    var max_abs = 0
    for idx in range(nb_rx * num_samples * 2):
        var d = Int(h_rx_ref[idx]) - Int(h_rx[idx])
        if d != 0:
            mism += 1
            var ad = d if d > 0 else -d
            if ad > max_abs:
                max_abs = ad
    print("zero-copy correctness: mismatches=", mism, "/", nb_rx * num_samples * 2, " max_abs=", max_abs)

    # time it: kernel launch directly on host pointers, no enqueue_copy at all
    var best_zc: Int = -1
    for _ in range(iterations):
        var t0 = perf_counter_ns()
        ctx.enqueue_function[conv_basic_kernel](
            channel_ptr, tx_ptr, rx_ptr,
            Int32(num_input_samples), Int32(num_samples), Int32(channel_length), Int32(nb_tx), Int32(nb_rx),
            grid_dim=blocks, block_dim=threads,
        )
        ctx.synchronize()
        var t1 = perf_counter_ns()
        var e = t1 - t0
        if best_zc < 0 or e < best_zc:
            best_zc = e
    print("zero-copy (host pointers directly) best_ns=", best_zc)
