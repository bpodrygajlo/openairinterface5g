# Standalone Mojo GPU benchmark (not wired into the CMake/gtest build - run
# directly with `mojo run` on a machine with an NVIDIA GPU and Mojo/MAX
# installed, e.g. the ARM/GB10 box this was developed on). Companion to
# gpu_large_cuda.cu, which implements the identical algorithm in native
# CUDA for comparison, and gpu_large_check.mojo, which verifies both
# kernels here bit-exact against a CPU reference.
#
# Two kernels, same as gpu_large_cuda.cu:
#   conv_basic_kernel - one thread per (rx_ant, sample), direct global
#                        memory reads, no shared-memory tiling.
#   conv_tiled_kernel  - shared-memory-tiled port of OAI's actual production
#                        kernel (multipath_channel_kernel in
#                        ../../../SIMULATION/TOOLS/multipath_channel.cu):
#                        each block stages a tx tile into shared memory once
#                        per tx antenna instead of re-reading global memory
#                        per tap per thread.
#
# Runs zero-copy: the kernels operate directly on ordinary host-allocated
# List buffers, no DeviceBuffer/enqueue_copy at all. This works correctly
# on ATS (Address Translation Services) hardware - Grace-Blackwell-class
# systems where the GPU walks the CPU's page tables directly over
# NVLink-C2C, so an ordinary host pointer is already valid, coherent GPU
# memory. Confirmed via `nvidia-smi -q` reporting `Addressing Mode: ATS`
# on the GB10 this was tested on. See gpu_copy_cost.mojo for why this
# matters (the discrete-copy round trip costs roughly 2x the kernel time
# for this workload) and gpu_zerocopy.mojo/gpu_zerocopy_cuda.cu for the
# minimal before/after comparison. On non-ATS (regular discrete PCIe GPU)
# hardware this pattern would not be safe - use enqueue_create_buffer +
# enqueue_copy there instead.
#
# IMPORTANT Mojo footgun hit here: this compiler destroys a List at its
# last *textual* reference, not at its last *use*. Since these buffers are
# only referenced via `.unsafe_ptr()` once (to build the FPtr/IPtr passed
# into enqueue_function), the List can be freed by the compiler while the
# asynchronous GPU kernel is still reading/writing through it via ATS -
# `ctx.synchronize()` doesn't count as a "use" from the destruction
# analysis's point of view. This silently corrupted results (no crash,
# just wrong data) once nb_tx/nb_rx or num_samples got large enough for
# the race to actually matter - see gpu_large_check.mojo's antenna sweep,
# which showed clean results at nb_rx=nb_tx<=4 and reproducible garbage
# above that, before this fix. The fix is the `_ = len(...)` calls after
# `ctx.synchronize()` below: they're a no-op, but they move each buffer's
# last textual reference past the point where every kernel touching it has
# finished. Forgetting this is the single most reproducible way to get
# silently-wrong results out of an otherwise-correct-looking Mojo kernel
# launch in this version.
from std.gpu import thread_idx, block_idx, block_dim
from max.gpu import barrier
from max.gpu.memory import external_memory, AddressSpace
from std.memory import UnsafePointer
from std.time import perf_counter_ns
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


def run_case(ctx: DeviceContext, nb_rx: Int, nb_tx: Int, num_samples: Int, channel_length: Int, iterations: Int) raises:
    var num_input_samples = num_samples + channel_length - 1

    var h_tx = List[Int16]()
    for _ in range(nb_tx * num_input_samples * 2):
        h_tx.append(Int16(random_si64(-20, 20)))
    var h_channel = List[Float32]()
    for _ in range(nb_rx * nb_tx * channel_length * 2):
        h_channel.append(Float32(random_si64(-20, 20)))
    var h_rx = List[Int16]()
    for _ in range(nb_rx * num_samples * 2):
        h_rx.append(0)

    # zero-copy: kernel operates directly on host memory via ATS
    var channel_ptr = FPtr(unsafe_from_address=Int(h_channel.unsafe_ptr()))
    var tx_ptr = IPtr(unsafe_from_address=Int(h_tx.unsafe_ptr()))
    var rx_ptr = IPtr(unsafe_from_address=Int(h_rx.unsafe_ptr()))

    var threads_b = 512
    var total_b = nb_rx * num_samples
    var blocks_b = (total_b + threads_b - 1) // threads_b

    ctx.enqueue_function[conv_basic_kernel](
        channel_ptr, tx_ptr, rx_ptr, Int32(num_input_samples), Int32(num_samples), Int32(channel_length), Int32(nb_tx), Int32(nb_rx),
        grid_dim=blocks_b, block_dim=threads_b,
    )
    ctx.synchronize()
    var best_basic: Int = -1
    for _ in range(iterations):
        var t0 = perf_counter_ns()
        ctx.enqueue_function[conv_basic_kernel](
            channel_ptr, tx_ptr, rx_ptr, Int32(num_input_samples), Int32(num_samples), Int32(channel_length), Int32(nb_tx), Int32(nb_rx),
            grid_dim=blocks_b, block_dim=threads_b,
        )
        ctx.synchronize()
        var t1 = perf_counter_ns()
        var e = t1 - t0
        if best_basic < 0 or e < best_basic:
            best_basic = e

    var block_size = 512
    var grid_x = (num_samples + block_size - 1) // block_size
    var shmem_bytes = (block_size + channel_length - 1) * 2 * 4

    ctx.enqueue_function[conv_tiled_kernel](
        channel_ptr, tx_ptr, rx_ptr, Int32(num_input_samples), Int32(num_samples), Int32(channel_length), Int32(nb_tx), Int32(nb_rx),
        grid_dim=Dim(grid_x, nb_rx), block_dim=Dim(block_size), shared_mem_bytes=shmem_bytes,
    )
    ctx.synchronize()
    var best_tiled: Int = -1
    for _ in range(iterations):
        var t0 = perf_counter_ns()
        ctx.enqueue_function[conv_tiled_kernel](
            channel_ptr, tx_ptr, rx_ptr, Int32(num_input_samples), Int32(num_samples), Int32(channel_length), Int32(nb_tx), Int32(nb_rx),
            grid_dim=Dim(grid_x, nb_rx), block_dim=Dim(block_size), shared_mem_bytes=shmem_bytes,
        )
        ctx.synchronize()
        var t1 = perf_counter_ns()
        var e = t1 - t0
        if best_tiled < 0 or e < best_tiled:
            best_tiled = e

    _ = len(h_tx)
    _ = len(h_channel)
    _ = len(h_rx)
    var msps_basic = Float64(num_samples) / (Float64(best_basic) / 1000.0)
    var msps_tiled = Float64(num_samples) / (Float64(best_tiled) / 1000.0)
    print("nb_rx=", nb_rx, "nb_tx=", nb_tx, "num_samples=", num_samples,
          " basic_ns=", best_basic, "MSPS=", msps_basic,
          " tiled_ns=", best_tiled, "MSPS=", msps_tiled,
          " tiled/basic=", Float64(best_basic) / Float64(best_tiled))


def main() raises:
    seed(1234)
    var ctx = DeviceContext()
    var channel_length = 16

    print("=== scaling antennas (num_samples=61440) ===")
    var ants = List[Int]()
    ants.append(1); ants.append(2); ants.append(4); ants.append(8); ants.append(16); ants.append(32); ants.append(64)
    for a in ants:
        run_case(ctx, a, a, 61440, channel_length, 20)

    print("=== scaling samples (nb_rx=nb_tx=8) ===")
    var samples = List[Int]()
    samples.append(61440); samples.append(122880); samples.append(245760); samples.append(491520); samples.append(983040)
    for s in samples:
        run_case(ctx, 8, 8, s, channel_length, 20)
