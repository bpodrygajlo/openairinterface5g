/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/* The SRS estimation module on a synthetic uplink: a few paths at off-grid delays seen by every
 * gNB antenna with its own phase, one cyclic shift per UE port, white noise. Checks the estimate
 * at the comb REs against the exact frequency response. */

#include <gtest/gtest.h>
#include <dlfcn.h>
#include <cmath>
#include <complex>
#include <random>
#include <string>
#include <vector>

extern "C" {
#include "srs_est_interface.h"
}

using cd = std::complex<double>;

namespace {

struct Module {
  void *handle = nullptr;
  srs_est_init_t *init = nullptr;
  srs_est_run_t *run = nullptr;
  srs_est_shutdown_t *shutdown = nullptr;
  explicit Module(const std::string &version)
  {
    handle = dlopen(("./libsrs_est" + version + ".so").c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle)
      return;
    init = (srs_est_init_t *)dlsym(handle, "srs_est_init");
    run = (srs_est_run_t *)dlsym(handle, "srs_est_run");
    shutdown = (srs_est_shutdown_t *)dlsym(handle, "srs_est_shutdown");
  }
  ~Module()
  {
    if (handle)
      dlclose(handle);
  }
  bool ok() const
  {
    return init && run && shutdown;
  }
};

struct Path {
  double delay_s; // excess delay
  double power;
};

struct Case {
  int nb_rx, n_ports, M, K_TC, n_symb;
  double snr_db; // per RE of one port and one symbol; inf for no noise
  srs_est_method_t method;
  double timing_s = 0; // common delay of all paths: the UE's timing advance error
};

struct Scenario {
  Case c;
  double scs = 30e3;
  int n_cs_max;
  std::vector<cd> H; // true channel at the comb REs [p][a][M]
  std::vector<c16_t> y; // [p][a][l][M]
  std::vector<cf_t> rbar; // [l][M]
  srs_est_in_t in{};

  explicit Scenario(const Case &cs, unsigned seed) : c(cs)
  {
    n_cs_max = c.K_TC == 2 ? 8 : 12;
    std::mt19937 gen(seed);
    std::uniform_real_distribution<double> uph(0, 2 * M_PI);
    std::normal_distribution<double> nd(0, 1);
    const std::vector<Path> paths = {{0.0, 1.0}, {37e-9, 0.5}, {181.3e-9, 0.2}, {612.7e-9, 0.05}, {1003.1e-9, 0.01}};
    // every (antenna, port, path) has its own phase: a rich array channel
    H.assign((size_t)c.n_ports * c.nb_rx * c.M, 0);
    for (int p = 0; p < c.n_ports; p++)
      for (int a = 0; a < c.nb_rx; a++)
        for (const Path &pa : paths) {
          const cd g = std::polar(std::sqrt(pa.power), uph(gen));
          for (int k = 0; k < c.M; k++) {
            const double f = c.K_TC * k * scs; // relative to comb RE 0
            H[((size_t)p * c.nb_rx + a) * c.M + k] += g * std::polar(1.0, -2 * M_PI * f * (pa.delay_s + c.timing_s));
          }
        }
    // base sequence: a Zadoff-Chu-like unit modulus sequence per symbol
    rbar.resize((size_t)c.n_symb * c.M);
    for (int l = 0; l < c.n_symb; l++)
      for (int k = 0; k < c.M; k++) {
        const double ph = -M_PI * (7 + l) * (double)k * (k + 1) / 1021.0;
        rbar[(size_t)l * c.M + k] = cf_t{(float)cos(ph), (float)sin(ph)};
      }
    // received REs: all ports on one comb, each with its cyclic shift, scaled to use c16 well
    const double amp = 2000;
    const double noise_std = std::isinf(c.snr_db) ? 0 : amp * std::sqrt(0.5 * std::pow(10, -c.snr_db / 10));
    y.resize((size_t)c.n_ports * c.nb_rx * c.n_symb * c.M);
    for (int a = 0; a < c.nb_rx; a++)
      for (int l = 0; l < c.n_symb; l++)
        for (int k = 0; k < c.M; k++) {
          cd v = 0;
          const cd rb(rbar[(size_t)l * c.M + k].r, rbar[(size_t)l * c.M + k].i);
          for (int p = 0; p < c.n_ports; p++) {
            const int n_cs = p * n_cs_max / c.n_ports;
            v += H[((size_t)p * c.nb_rx + a) * c.M + k] * rb * std::polar(1.0, 2 * M_PI * n_cs * k / n_cs_max);
          }
          v = amp * v + cd(noise_std * nd(gen), noise_std * nd(gen));
          const c16_t q = {(int16_t)lround(v.real()), (int16_t)lround(v.imag())};
          for (int p = 0; p < c.n_ports; p++) // ports share the comb: same REs
            y[(((size_t)p * c.nb_rx + a) * c.n_symb + l) * c.M + k] = q;
        }
    for (auto &h : H)
      h *= amp;
    in.nb_rx = c.nb_rx;
    in.n_ports = c.n_ports;
    in.n_symb = c.n_symb;
    in.M = c.M;
    in.K_TC = c.K_TC;
    in.n_cs_max = n_cs_max;
    for (int p = 0; p < c.n_ports; p++) {
      in.n_cs[p] = p * n_cs_max / c.n_ports;
      in.comb[p] = 0;
    }
    in.y = y.data();
    in.rbar = rbar.data();
    in.method = c.method;
    const double bin_s = 1.0 / (c.K_TC * scs * c.M);
    in.win_pre = 8;
    in.win_post = std::min((int)ceil(1.5e-6 / bin_s), c.M / c.n_ports - in.win_pre - 8);
    in.oversampling = 4;
    in.scs_hz = scs;
    in.pdp_threshold = 4;
  }

  struct Result {
    std::vector<cf_t> h_comb, h_full;
    float noise_var = 0;
  };
  Result run(const Module &m) const
  {
    Result r;
    r.h_comb.resize((size_t)c.n_ports * c.nb_rx * c.M);
    r.h_full.resize((size_t)c.n_ports * c.nb_rx * c.M * c.K_TC);
    srs_est_out_t out = {.h_comb = r.h_comb.data(), .h_full = r.h_full.data()};
    EXPECT_EQ(m.init(c.nb_rx, c.n_ports, c.M, c.K_TC), 0);
    EXPECT_EQ(m.run(&in, &out), 0);
    m.shutdown();
    r.noise_var = out.noise_var;
    return r;
  }

  double nmse_db(const std::vector<cf_t> &h_comb) const
  {
    double err = 0, ref = 0;
    for (size_t i = 0; i < H.size(); i++) {
      err += std::norm(cd(h_comb[i].r, h_comb[i].i) - H[i]);
      ref += std::norm(H[i]);
    }
    return 10 * log10(err / ref);
  }
};

struct Expect {
  Case c;
  double max_nmse_db;
};

class SrsEstPrecision : public ::testing::TestWithParam<std::tuple<std::string, Expect>> {};

TEST_P(SrsEstPrecision, AgainstTrueChannel)
{
  const auto &[version, e] = GetParam();
  Module m(version);
  if (!m.ok())
    GTEST_SKIP() << "libsrs_est" << version << ".so not built";
  Scenario s(e.c, 1);
  const auto r = s.run(m);
  const double nmse = s.nmse_db(r.h_comb);
  printf("%s rx %d ports %d M %d comb %d symbols %d SNR %.0f method %d: NMSE %.2f dB, noise var %.3g (true %.3g)\n",
         version.c_str(),
         e.c.nb_rx,
         e.c.n_ports,
         e.c.M,
         e.c.K_TC,
         e.c.n_symb,
         e.c.snr_db,
         e.c.method,
         nmse,
         r.noise_var,
         std::isinf(e.c.snr_db) ? 0.0 : 2000.0 * 2000.0 * std::pow(10, -e.c.snr_db / 10) / e.c.n_symb);
  EXPECT_LT(nmse, e.max_nmse_db);
}

// Targets. At 20 dB SNR a plain LS estimate is at about -20 dB NMSE; these ask for 10 to 17 dB
// better, which a good estimator reaches by exploiting the structure of the channel. Without noise
// the only limit is the c16 quantization of y. SRS_EST_DFT is the simpler method, with lower
// targets.
// M = m_SRS * 12 / K_TC: 48 PRB is 288 REs on comb 2, 144 on comb 4
const Expect kCases[] = {
    {{8, 1, 288, 2, 1, 20.0, SRS_EST_LMMSE}, -32.0},
    {{8, 4, 288, 2, 1, 20.0, SRS_EST_LMMSE}, -31.5},
    {{8, 2, 144, 4, 1, 20.0, SRS_EST_LMMSE}, -29.0},
    {{8, 4, 288, 2, 4, 20.0, SRS_EST_LMMSE}, -37.0},
    {{8, 4, 288, 2, 1, INFINITY, SRS_EST_LMMSE}, -70.0},
    {{8, 1, 288, 2, 1, 20.0, SRS_EST_DFT}, -22.0},
    // the UE's timing advance moves the channel by a fraction of a us either way
    {{8, 4, 288, 2, 1, 20.0, SRS_EST_LMMSE, -150e-9}, -31.5},
    {{8, 4, 288, 2, 1, 20.0, SRS_EST_LMMSE, 400e-9}, -31.5},
    {{8, 1, 288, 2, 1, 20.0, SRS_EST_DFT, -150e-9}, -18.0}, // integer-bin alignment on the M grid
};

INSTANTIATE_TEST_SUITE_P(Modules,
                         SrsEstPrecision,
                         ::testing::Combine(::testing::Values(std::string("_cuda")), ::testing::ValuesIn(kCases)));

} // namespace
