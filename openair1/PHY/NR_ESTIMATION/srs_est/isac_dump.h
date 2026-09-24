/*
 * SPDX-License-Identifier: LicenseRef-CSSL-1.0
 */

/*! \file isac_dump.h
 * \brief Export of the float SRS channel estimates for sensing (ISAC).
 *
 * --isac.dump_file <path> appends one record per exported SRS: an isac_record_header_t followed
 * by payload_bytes of little-endian complex float32 h[port][antenna][M], the estimate at the comb
 * REs (port p's RE k is carrier subcarrier first_sc + k0[p] + K_TC * k, counted from the lowest
 * subcarrier of the carrier). --isac.min_interval_ms (default 50) thins the records out,
 * --isac.max_records (default 0, unlimited) stops the export. A writer thread does the I/O: when
 * it falls behind, records are dropped (and counted), the L1 never waits.
 */

#ifndef ISAC_DUMP_H
#define ISAC_DUMP_H

#include <stdint.h>
#include "common/platform_types.h"
#include "srs_est_interface.h"

#define ISAC_RECORD_MAGIC "ISAC"
#define ISAC_RECORD_VERSION 1

typedef struct __attribute__((packed)) {
  char magic[4]; ///< "ISAC"
  uint16_t version;
  uint16_t header_bytes; ///< sizeof(isac_record_header_t): the payload starts there
  uint32_t record_index; ///< counts every exported SRS, dropped ones included
  uint32_t frame; ///< frame number unwrapped since start (no modulo 1024)
  uint32_t slot;
  uint16_t rnti;
  uint16_t nb_rx; ///< gNB antennas
  uint16_t n_ports; ///< UE SRS ports
  uint16_t M; ///< comb REs per port
  uint16_t K_TC; ///< comb size
  uint16_t n_symb; ///< SRS symbols averaged into the estimate
  uint16_t first_symbol; ///< first SRS symbol of the slot
  uint16_t N_RB; ///< carrier bandwidth in PRBs
  uint16_t N_fft; ///< OFDM symbol size
  uint16_t estimator; ///< srs_est_method_t
  int32_t first_sc; ///< carrier subcarrier of comb RE 0 of port 0
  int32_t k0[SRS_EST_MAX_PORTS]; ///< comb RE 0 of each port, relative to first_sc
  int32_t n_cs[SRS_EST_MAX_PORTS]; ///< cyclic shift index of each port
  int32_t n_cs_max;
  /// sample timestamp of the start of the slot on the radio device: RU timestamp offset plus
  /// frame * samples_per_frame + slot offset (for rfsim, the time base of the CIR database)
  uint64_t slot_timestamp;
  float noise_var; ///< per-RE noise variance of the estimate's input (after symbol averaging)
  float signal_power; ///< mean |h|^2
  float c16_scale; ///< the L1's c16 estimate is h * c16_scale
  double scs_hz;
  double fs_hz; ///< sample rate
  double fc_hz; ///< uplink carrier frequency
  uint32_t payload_bytes;
} isac_record_header_t;

typedef struct isac_dump_s isac_dump_t;

/// reads the isac.* configuration; NULL if no dump file is configured
isac_dump_t *isac_dump_init(void);
/// queues one record: header fields not about the estimate itself are filled by the caller
void isac_dump_record(isac_dump_t *d, const isac_record_header_t *hdr, const cf_t *h);
void isac_dump_end(isac_dump_t *d);

#endif
