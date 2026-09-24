/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*! \file srs_est_interface.h
 * \brief Loadable SRS channel estimator: what the gNB hands to the module and expects back.
 *
 * With --loader.srs_est.shlibversion _cuda the gNB loads libsrs_est_cuda.so and calls it for every
 * SRS instead of its fixed-point estimator (LS, FIR interpolation, c16 output: nr_srs_channel_
 * interpolation()). If srs_est_run() fails the gNB falls back to the fixed-point estimator for
 * that SRS. When it succeeds its float estimate is converted to the fixed-point layout for the rest
 * of the L1 (timing advance, SNR, SRS.indication, MAC) and exported with --isac.dump_file.
 *
 * The received signal, for gNB antenna a, SRS symbol l and comb RE k of UE port p, is
 *
 *     Y_p[a][l][k] = sum_q H_q[a][k] rbar[l][k] exp(j 2 pi n_cs_q k / n_cs_max) + noise
 *
 * the sum running over the ports q on the same comb as p (they share the REs: y holds the same
 * values for each of them), H_q being the channel of port q at those REs and rbar the base
 * sequence, without cyclic shift. The module estimates every H_p at the comb REs and at every
 * subcarrier of the band, and the noise variance.
 *
 * Units: Y is in rxdataF units and rbar has unit modulus, so H is the channel in rxdataF units per
 * unit-amplitude transmitted symbol and the noise variance is per RE, in the same units.
 */

#ifndef SRS_EST_INTERFACE_H
#define SRS_EST_INTERFACE_H

#include <stdint.h>
#include "common/platform_types.h"

#define SRS_EST_MAX_PORTS 4
#define SRS_EST_MAX_SYMBOLS 4

typedef enum { SRS_EST_LMMSE = 0, SRS_EST_DFT = 1 } srs_est_method_t;

typedef struct {
  int nb_rx; ///< gNB antennas
  int n_ports; ///< UE SRS ports
  int n_symb; ///< SRS symbols (averaged)
  int M; ///< comb REs per port and symbol
  int K_TC; ///< comb size (2 or 4)
  int n_cs_max; ///< number of cyclic shifts (8 or 12)
  int n_cs[SRS_EST_MAX_PORTS]; ///< cyclic shift index of each port
  int comb[SRS_EST_MAX_PORTS]; ///< comb offset of each port: ports only interfere within a comb
  /// received comb REs [port][antenna][symbol][M] (ports on the same comb carry the same REs)
  const c16_t *y;
  /// unit-modulus base sequence (no cyclic shift) [symbol][M]
  const cf_t *rbar;
  /// the tuning below comes from the gNB (and nr_srssim's -x, -D, -T): use it, or ignore it
  srs_est_method_t method; ///< which of your methods to run (e.g. a precise one and a fast one)
  int win_pre; ///< delay window before n = 0, in M-point delay bins
  int win_post; ///< delay window after n = 0 (the expected excess delay), in M-point delay bins
  int oversampling; ///< a finer delay grid, if your method uses one
  double scs_hz; ///< subcarrier spacing, for the timing offset in seconds
  /// a significance threshold in standard deviations of the noise, if your method selects delay bins
  float pdp_threshold;
} srs_est_in_t;

typedef struct {
  /// estimate at the comb REs [port][antenna][M]
  cf_t *h_comb;
  /// estimate at every subcarrier of the band [port][antenna][K_TC * M], starting at the port's
  /// first comb RE: subcarrier j is comb RE j / K_TC when j % K_TC == 0
  cf_t *h_full;
  float noise_var; ///< per-RE noise variance
  float signal_power; ///< mean |h_comb|^2 over ports, antennas and REs
  /// the timing the estimator found (for information: the estimates keep the true timing)
  double timing_offset_s;
} srs_est_out_t;

typedef int32_t(srs_est_init_t)(int max_rx, int max_ports, int max_M, int max_K_TC);
typedef int32_t(srs_est_run_t)(const srs_est_in_t *in, srs_est_out_t *out);
typedef void(srs_est_shutdown_t)(void);

typedef struct {
  srs_est_init_t *init;
  srs_est_run_t *run;
  srs_est_shutdown_t *shutdown;
  char version[16];
} srs_est_interface_t;

/// Loads the module named by --loader.srs_est.shlibversion; returns -1 (and leaves itf empty) if
/// none is configured, so that the gNB keeps the fixed-point estimator
int load_srs_est_interface(srs_est_interface_t *itf, int max_rx, int max_ports, int max_M, int max_K_TC);
void free_srs_est_interface(srs_est_interface_t *itf);

#endif
