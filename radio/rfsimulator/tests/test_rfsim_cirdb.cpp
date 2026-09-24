/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

#include <gtest/gtest.h>
#include <complex>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "common/config/config_userapi.h"
configmodule_interface_t *uniqCfg = NULL;

extern "C" {
#include "rfsim_cirdb.h"
#include "common/utils/LOG/log.h"
void exit_function(const char *file, const char *function, const int line, const char *s, const int assert_flag)
{
  (void)assert_flag;
  fprintf(stderr, "FATAL: %s at %s:%s:%d\n", s, file, function, line);
  exit(EXIT_FAILURE);
}
}

namespace fs = std::filesystem;
using cd = std::complex<double>;

namespace {

// A database of n_tx x n_rx links, L taps, S snapshots, where every tap is distinct so that any
// link, tap or snapshot mix-up shows in the output
struct TestDb {
  int n_tx = 3, n_rx = 2, L = 16, S = 3;
  double fs = 1e6, dt = 1e-3; // 1000 samples per snapshot
  std::vector<cf_t> taps; // [S][link][L], link = aarx + n_rx * aatx
  std::string yaml, bin;

  explicit TestDb(const std::string &name, int ntx = 3, int nrx = 2, int ntaps = 16) : n_tx(ntx), n_rx(nrx), L(ntaps)
  {
    fs::path dir = fs::temp_directory_path() / ("rfsim_cirdb_" + name + "_" + std::to_string(getpid()));
    fs::create_directories(dir);
    yaml = (dir / "db.yaml").string();
    bin = (dir / "db.bin").string();
    std::mt19937 gen(42);
    std::normal_distribution<float> nd(0, 0.3);
    taps.resize((size_t)S * n_tx * n_rx * L);
    for (auto &t : taps)
      t = {nd(gen), nd(gen)};
    std::ofstream(bin, std::ios::binary).write(reinterpret_cast<const char *>(taps.data()), taps.size() * sizeof(cf_t));
    std::ofstream y(yaml);
    y << "entries:\n"
      << "  - model_id: 6\n    n_tx: " << n_tx << "\n    n_rx: " << n_rx << "\n    L: " << L << "\n    S: " << S
      << "\n    fs_hz: " << fs << "\n    snapshot_dt_s: " << dt << "\n    pair_order: 0\n    offset_bytes: 0\n    nbytes: "
      << taps.size() * sizeof(cf_t) << "\n";
  }
  cd tap(int s, int aatx, int aarx, int l) const
  {
    const cf_t v = taps[((size_t)s * n_tx * n_rx + aarx + n_rx * aatx) * L + l];
    return {v.r, v.i};
  }
};

std::vector<std::vector<c16_t>> random_input(int nstreams, int len, unsigned seed)
{
  std::mt19937 gen(seed);
  std::uniform_int_distribution<int> ud(-2000, 2000);
  std::vector<std::vector<c16_t>> in(nstreams, std::vector<c16_t>(len));
  for (auto &s : in)
    for (auto &x : s)
      x = {(int16_t)ud(gen), (int16_t)ud(gen)};
  return in;
}

// Reference: double precision, snapshot chosen per output sample from its own timestamp, counted
// from the first block's timestamp rounded down to a snapshot boundary
std::vector<std::vector<cd>> reference(const TestDb &db,
                                       const std::vector<std::vector<c16_t>> &in,
                                       uint64_t ts,
                                       int nsamps,
                                       int own_rx,
                                       bool transposed)
{
  const int L = db.L;
  std::vector<std::vector<cd>> out(own_rx, std::vector<cd>(nsamps));
  const uint64_t per_snapshot = (uint64_t)(db.fs * db.dt);
  const uint64_t t0 = ts / per_snapshot * per_snapshot;
  for (int n = 0; n < nsamps; n++) {
    const int s = (int)((ts + n - t0) / per_snapshot) % db.S;
    for (int r = 0; r < own_rx; r++)
      for (int t = 0; t < (int)in.size(); t++)
        for (int l = 0; l < L; l++) {
          const c16_t x = in[t][n + L - 1 - l];
          const cd h = transposed ? db.tap(s, r, t, l) : db.tap(s, t, r, l);
          out[r][n] += h * cd(x.r, x.i);
        }
  }
  return out;
}

double max_rel_error(const std::vector<std::vector<cf_t>> &got, const std::vector<std::vector<cd>> &ref)
{
  double err = 0, peak = 0;
  for (size_t r = 0; r < ref.size(); r++)
    for (size_t n = 0; n < ref[r].size(); n++) {
      err = std::max(err, std::abs(cd(got[r][n].r, got[r][n].i) - ref[r][n]));
      peak = std::max(peak, std::abs(ref[r][n]));
    }
  return err / peak;
}

double run(const TestDb &db, bool gpu, rfsim_cirdb_orientation_t orientation, int own_rx, int peer_tx, uint64_t ts, int nsamps)
{
  const bool transposed = own_rx == db.n_tx && peer_tx == db.n_rx;
  rfsim_cirdb_t *c = rfsim_cirdb_init(db.yaml.c_str(), db.bin.c_str(), 6, orientation, own_rx, db.fs, gpu);
  EXPECT_EQ(rfsim_cirdb_length(c), db.L);
  auto in = random_input(peer_tx, nsamps + db.L - 1, 7 + ts);
  std::vector<c16_t *> in_p;
  for (auto &s : in)
    in_p.push_back(s.data());
  // start from a non-zero output: rfsim_cirdb_apply() must accumulate
  std::vector<std::vector<cf_t>> out(own_rx, std::vector<cf_t>(nsamps, cf_t{1.0f, -1.0f}));
  std::vector<cf_t *> out_p;
  for (auto &s : out)
    out_p.push_back(s.data());
  rfsim_cirdb_apply(c, peer_tx, in_p.data(), ts, nsamps, out_p.data());
  rfsim_cirdb_end(c);
  auto ref = reference(db, in, ts, nsamps, own_rx, transposed);
  for (auto &s : ref)
    for (auto &x : s)
      x += cd(1.0, -1.0);
  return max_rel_error(out, ref);
}

class CirdbTest : public ::testing::TestWithParam<bool> {};

TEST_P(CirdbTest, DirectMatchesReference)
{
  TestDb db("direct");
  EXPECT_LT(run(db, GetParam(), RFSIM_CIRDB_AUTO, db.n_rx, db.n_tx, 100, 500), 1e-5);
}

TEST_P(CirdbTest, TransposedMatchesReference)
{
  TestDb db("transposed");
  EXPECT_LT(run(db, GetParam(), RFSIM_CIRDB_AUTO, db.n_tx, db.n_rx, 100, 500), 1e-5);
}

TEST_P(CirdbTest, SwitchesSnapshotAtTheExactSample)
{
  TestDb db("switch");
  // block 700..1699 straddles the switch to snapshot 1 at sample 1000
  EXPECT_LT(run(db, GetParam(), RFSIM_CIRDB_AUTO, db.n_rx, db.n_tx, 700, 1000), 1e-5);
}

TEST_P(CirdbTest, LoopsAfterTheLastSnapshot)
{
  TestDb db("wrap");
  // snapshots 0, 1, 2 and 0 again from sample 3000
  EXPECT_LT(run(db, GetParam(), RFSIM_CIRDB_TRANSPOSED, db.n_tx, db.n_rx, 500, 3200), 1e-5);
}

TEST_P(CirdbTest, StartsWithTheFirstBlock)
{
  TestDb db("origin");
  // the link starts at 5300: snapshot 0 covers 5000..5999, snapshot 1 starts at 6000
  EXPECT_LT(run(db, GetParam(), RFSIM_CIRDB_AUTO, db.n_rx, db.n_tx, 5300, 1500), 1e-5);
}

TEST_P(CirdbTest, DemoShape)
{
  // the ISAC demo's 32 gNB x 4 UE elements with 128 taps, on the gNB side (transposed)
  TestDb db("demo", 32, 4, 128);
  EXPECT_LT(run(db, GetParam(), RFSIM_CIRDB_AUTO, db.n_tx, db.n_rx, 900, 4000), 1e-5);
}

INSTANTIATE_TEST_SUITE_P(Backends,
                         CirdbTest,
#ifdef CHANNEL_SIM_CUDA
                         ::testing::Values(false, true),
#else
                         ::testing::Values(false),
#endif
                         [](const ::testing::TestParamInfo<bool> &info) { return info.param ? "gpu" : "cpu"; });

} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  char *cfg_argv[] = {argv[0]};
  uniqCfg = load_configmodule(1, cfg_argv, CONFIG_ENABLECMDLINEONLY);
  logInit();
  return RUN_ALL_TESTS();
}
