from std.random import random_si64, seed
from std.time import perf_counter_ns

# Basic, unoptimized Mojo port of channel_convolution_cpu()
# (openair1/PHY/TOOLS/tests/test_channel_pipeline_tools.c), same algorithm,
# same problem sizes as BM_channel_convolution_cpu in
# benchmark_channel_pipeline.cpp. Deliberately plain nested loops, flat
# lists, scalar arithmetic: no SIMD types, no manual unrolling, no threads.
# This is the "what you get for free" baseline to compare against the
# hand-tuned/auto-vectorized C and CUDA implementations.

def convolve(
    channel_r: List[Float32],
    channel_i: List[Float32],
    tx_r: List[Int16],
    tx_i: List[Int16],
    mut rx_r: List[Int16],
    mut rx_i: List[Int16],
    num_input_samples: Int,
    num_samples: Int,
    channel_length: Int,
    nb_tx: Int,
    nb_rx: Int,
):
    for rx_ant in range(nb_rx):
        for i in range(num_samples):
            var acc_r: Float32 = 0.0
            var acc_i: Float32 = 0.0
            for tx_ant in range(nb_tx):
                var tx_base = tx_ant * num_input_samples
                var chan_base = (tx_ant + nb_tx * rx_ant) * channel_length
                for l in range(channel_length):
                    var idx = i + (channel_length - 1) - l
                    var tx_sample_r = Float32(tx_r[tx_base + idx])
                    var tx_sample_i = Float32(tx_i[tx_base + idx])
                    var ch_r = channel_r[chan_base + l]
                    var ch_i = channel_i[chan_base + l]
                    acc_r += tx_sample_r * ch_r - tx_sample_i * ch_i
                    acc_i += tx_sample_r * ch_i + tx_sample_i * ch_r
            rx_r[rx_ant * num_samples + i] = Int16(acc_r)
            rx_i[rx_ant * num_samples + i] = Int16(acc_i)


def run_case(nb_rx: Int, nb_tx: Int, num_samples: Int, channel_length: Int, iterations: Int):
    var num_input_samples = num_samples + channel_length - 1

    var tx_r = List[Int16]()
    var tx_i = List[Int16]()
    for _ in range(nb_tx * num_input_samples):
        tx_r.append(Int16(random_si64(-1000, 999)))
        tx_i.append(Int16(random_si64(-1000, 999)))

    var channel_r = List[Float32]()
    var channel_i = List[Float32]()
    for _ in range(nb_rx * nb_tx * channel_length):
        channel_r.append(Float32(random_si64(-1000, 999)))
        channel_i.append(Float32(random_si64(-1000, 999)))

    var rx_r = List[Int16]()
    var rx_i = List[Int16]()
    for _ in range(nb_rx * num_samples):
        rx_r.append(Int16(0))
        rx_i.append(Int16(0))

    # warm-up
    convolve(channel_r, channel_i, tx_r, tx_i, rx_r, rx_i, num_input_samples, num_samples, channel_length, nb_tx, nb_rx)

    var best_ns: Int = -1
    for _ in range(iterations):
        var t0 = perf_counter_ns()
        convolve(channel_r, channel_i, tx_r, tx_i, rx_r, rx_i, num_input_samples, num_samples, channel_length, nb_tx, nb_rx)
        var t1 = perf_counter_ns()
        var elapsed = t1 - t0
        if best_ns < 0 or elapsed < best_ns:
            best_ns = elapsed

    var msps = Float64(num_samples) / (Float64(best_ns) / 1000.0)
    print(
        "nb_rx=",
        nb_rx,
        " nb_tx=",
        nb_tx,
        " num_samples=",
        num_samples,
        " best_ns=",
        best_ns,
        " MSPS=",
        msps,
    )


def main():
    seed(1234)
    var num_samples = 61440
    var channel_length = 16
    var iterations = 20
    var rx_counts = List[Int]()
    rx_counts.append(1)
    rx_counts.append(2)
    rx_counts.append(4)
    var tx_counts = List[Int]()
    tx_counts.append(1)
    tx_counts.append(2)
    tx_counts.append(4)

    for nb_rx in rx_counts:
        for nb_tx in tx_counts:
            run_case(nb_rx, nb_tx, num_samples, channel_length, iterations)
