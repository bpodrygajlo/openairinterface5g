# Correctness verification for gpu_large_mojo.mojo's two kernels, swept
# across the same antenna/sample grid as the performance benchmark there.
# Both kernels' output is compared against a plain CPU reference.
#
# This is the script that caught the "ASAP destruction" bug documented in
# gpu_large_mojo.mojo's header: before the `_ = len(...)` keep-alive calls
# below, this sweep showed 0 mismatches at nb_rx=nb_tx<=4 (misleadingly
# looking fine) and reproducible, large mismatch counts above that - e.g.
# nb_rx=nb_tx=16 at num_samples=61440 showed >100k/2M elements wrong. The
# pattern (some larger configs clean, some smaller ones broken, no
# relationship to data magnitude) was the tell that this was a race rather
# than an algorithmic bug. After the fix: 0 mismatches everywhere tested,
# up to 64 antennas and 983040 samples.
from std.gpu import thread_idx, block_idx, block_dim
from max.gpu import barrier
from max.gpu.memory import external_memory, AddressSpace
from std.memory import UnsafePointer
from std.random import random_si64, seed
from max.gpu.host import DeviceContext, Dim

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

def conv_tiled_kernel(
    channel: FPtr, tx: IPtr, rx: IPtr,
    num_input_samples: Int32, num_samples: Int32, channel_length: Int32, nb_tx: Int32, nb_rx: Int32,
):
    var tx_shared = external_memory[Float32, address_space=AddressSpace.SHARED, alignment=4]()
    var i = Int(block_idx.x * block_dim.x + thread_idx.x)
    var rx_ant = Int(block_idx.y)
    var tid = Int(thread_idx.x)
    var block_start = Int(block_idx.x * block_dim.x)
    var cl = Int(channel_length)
    var shared_size = Int(block_dim.x) + cl - 1
    var acc_r: Float32 = 0.0
    var acc_i: Float32 = 0.0
    for j in range(Int(nb_tx)):
        var k = tid
        while k < shared_size:
            var load_idx = block_start + k
            var base = (j * Int(num_input_samples) + load_idx) * 2
            tx_shared[k * 2] = Float32(tx[base])
            tx_shared[k * 2 + 1] = Float32(tx[base + 1])
            k += Int(block_dim.x)
        barrier()
        if i < Int(num_samples):
            var chan_base = (j + Int(nb_tx) * rx_ant) * cl
            for l in range(cl):
                var s_idx = tid + (cl - 1) - l
                var s_r = tx_shared[s_idx * 2]
                var s_i = tx_shared[s_idx * 2 + 1]
                var ch_r = channel[(chan_base + l) * 2]
                var ch_i = channel[(chan_base + l) * 2 + 1]
                acc_r += s_r * ch_r - s_i * ch_i
                acc_i += s_r * ch_i + s_i * ch_r
        barrier()
    if i < Int(num_samples):
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

def check(ctx: DeviceContext, nb_rx: Int, nb_tx: Int, num_samples: Int, channel_length: Int) raises:
    var num_input_samples = num_samples + channel_length - 1
    var h_tx = List[Int16]()
    for _ in range(nb_tx * num_input_samples * 2):
        h_tx.append(Int16(random_si64(-4, 4)))
    var h_channel = List[Float32]()
    for _ in range(nb_rx * nb_tx * channel_length * 2):
        h_channel.append(Float32(random_si64(-4, 4)))
    var h_rx_basic = List[Int16]()
    var h_rx_tiled = List[Int16]()
    var h_rx_ref = List[Int16]()
    for _ in range(nb_rx * num_samples * 2):
        h_rx_basic.append(0); h_rx_tiled.append(0); h_rx_ref.append(0)

    cpu_ref(FPtr(unsafe_from_address=Int(h_channel.unsafe_ptr())), IPtr(unsafe_from_address=Int(h_tx.unsafe_ptr())),
            IPtr(unsafe_from_address=Int(h_rx_ref.unsafe_ptr())), num_input_samples, num_samples, channel_length, nb_tx, nb_rx)

    var channel_ptr = FPtr(unsafe_from_address=Int(h_channel.unsafe_ptr()))
    var tx_ptr = IPtr(unsafe_from_address=Int(h_tx.unsafe_ptr()))

    var threads_b = 512
    var total_b = nb_rx * num_samples
    var blocks_b = (total_b + threads_b - 1) // threads_b
    ctx.enqueue_function[conv_basic_kernel](
        channel_ptr, tx_ptr, IPtr(unsafe_from_address=Int(h_rx_basic.unsafe_ptr())),
        Int32(num_input_samples), Int32(num_samples), Int32(channel_length), Int32(nb_tx), Int32(nb_rx),
        grid_dim=blocks_b, block_dim=threads_b,
    )
    var block_size = 512
    var grid_x = (num_samples + block_size - 1) // block_size
    var shmem_bytes = (block_size + channel_length - 1) * 2 * 4
    ctx.enqueue_function[conv_tiled_kernel](
        channel_ptr, tx_ptr, IPtr(unsafe_from_address=Int(h_rx_tiled.unsafe_ptr())),
        Int32(num_input_samples), Int32(num_samples), Int32(channel_length), Int32(nb_tx), Int32(nb_rx),
        grid_dim=Dim(grid_x, nb_rx), block_dim=Dim(block_size), shared_mem_bytes=shmem_bytes,
    )
    ctx.synchronize()
    _ = len(h_tx)
    _ = len(h_channel)
    _ = len(h_rx_basic)
    _ = len(h_rx_tiled)

    var mism_b = 0
    var mism_t = 0
    for idx in range(nb_rx * num_samples * 2):
        if h_rx_ref[idx] != h_rx_basic[idx]:
            mism_b += 1
        if h_rx_ref[idx] != h_rx_tiled[idx]:
            mism_t += 1
    print("rx=", nb_rx, "tx=", nb_tx, "ns=", num_samples, " basic_mismatches=", mism_b, " tiled_mismatches=", mism_t, " / ", nb_rx * num_samples * 2)

def main() raises:
    seed(1234)
    var ctx = DeviceContext()

    print("=== scaling antennas (num_samples=61440) ===")
    var ants = List[Int]()
    ants.append(1); ants.append(2); ants.append(3); ants.append(4); ants.append(6)
    ants.append(8); ants.append(10); ants.append(12); ants.append(14); ants.append(16)
    ants.append(20); ants.append(24); ants.append(32); ants.append(48); ants.append(64)
    for a in ants:
        check(ctx, a, a, 61440, 16)

    print("=== scaling samples (nb_rx=nb_tx=8) ===")
    var samples = List[Int]()
    samples.append(61440); samples.append(122880); samples.append(245760)
    samples.append(491520); samples.append(983040)
    for s in samples:
        check(ctx, 8, 8, s, 16)
