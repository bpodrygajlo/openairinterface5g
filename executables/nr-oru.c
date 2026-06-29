/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */
#include "common/config/config_userapi.h"
#include "common/utils/system.h"
#include "nr-oru.h"
#include "openair1/PHY/defs_nr_common.h"
#include "PHY/NR_TRANSPORT/nr_transport_proto.h"
#include "oru_packet_processor.h"
#include <time.h>
#include "openair1/PHY/MODULATION/nr_modulation.h"
#include "openair1/SCHED_NR/sched_nr.h"
#include "openair1/PHY/MODULATION/modulation_common.h"
#include "openair2/LAYER2/NR_MAC_COMMON/nr_mac_common.h"

#define CONFIG_SECTION_ORU "ORUs.[0]"

#define CONFIG_STRING_ORU_TX_BW_LIST "tx_bw"
#define CONFIG_STRING_ORU_RX_BW_LIST "rx_bw"
#define CONFIG_STRING_ORU_CARRIER_TX_LIST "carrier_tx"
#define CONFIG_STRING_ORU_CARRIER_RX_LIST "carrier_rx"
#define CONFIG_STRING_ORU_FRAME_TYPE "frame_type"
#define CONFIG_STRING_ORU_PRACH_CONFIGID "prach_config_index"
#define CONFIG_STRING_ORU_PRACH_MSG1FREQ "prach_msg1_start"
#define CONFIG_STRING_ORU_NUMEROLOGY "mu"
#define CONFIG_STRING_ORU_TDD_PERIOD "tdd_period"
#define CONFIG_STRING_ORU_NUM_DL_SLOTS "num_dl_slots"
#define CONFIG_STRING_ORU_NUM_UL_SLOTS "num_ul_slots"
#define CONFIG_STRING_ORU_NUM_DL_SYMBOLS "num_dl_symbols"
#define CONFIG_STRING_ORU_NUM_UL_SYMBOLS "num_ul_symbols"
#define CONFIG_STRING_ORU_TX_CORE "tx_core"

#define HLP_ORU_TX_BW "set the TX bandwidth list per component carrier"
#define HLP_ORU_RX_BW "set the RX bandwidth list per component carrier"
#define HLP_ORU_CARRIER_TX "set the TX carrier frequencies per component carrier"
#define HLP_ORU_CARRIER_RX "set the RX carrier frequencies per component carrier"
#define HLP_ORU_FRAMETYPE "set the Frame type TDD/FDD of all component carriers"
#define HLP_ORU_PRACH_CONFIGID "set the PRACH configuration id of all component carriers"
#define HLP_ORU_PRACH_MSG1FREQ "set the PRACH MSG1 frequency of all component carriers"
#define HLP_ORU_NUMEROLOGY "set the numerology of the RU"
#define HLP_ORU_TDD_PERIOD "set the 3GPP TDD periodificty 0-9"
#define HLP_ORU_NUM_DL_SLOTS "set the number of DL Slots in TDD"
#define HLP_ORU_NUM_UL_SLOTS "set the number of UL Slots in TDD"
#define HLP_ORU_NUM_DL_SYMBOLS "set the number of DL symbols in the mixed slot"
#define HLP_ORU_NUM_UL_SYMBOLS "set the number of UL symbols in the mixed slot"

// clang-format off
#define CMDLINE_PARAMS_DESC_ORU \
{ \
  {CONFIG_STRING_ORU_TX_BW_LIST,                HLP_ORU_TX_BW,                      0,    .iptr=NULL,       .defintarrayval=DEFBW,        TYPE_INTARRAY,    0}, \
  {CONFIG_STRING_ORU_RX_BW_LIST,                HLP_ORU_RX_BW,                      0,    .iptr=NULL,       .defintarrayval=DEFBW,        TYPE_INTARRAY,    0}, \
  {CONFIG_STRING_ORU_CARRIER_TX_LIST,           HLP_ORU_CARRIER_TX,                 0,    .iptr=NULL,       .defintarrayval=DEFCARRIER,   TYPE_INTARRAY,    0}, \
  {CONFIG_STRING_ORU_CARRIER_RX_LIST,           HLP_ORU_CARRIER_RX,                 0,    .iptr=NULL,       .defintarrayval=DEFCARRIER,   TYPE_INTARRAY,    0}, \
  {CONFIG_STRING_ORU_FRAME_TYPE,                HLP_ORU_FRAMETYPE,                  0,    .uptr=NULL,       .defintval=1,                 TYPE_UINT,         0}, \
  {CONFIG_STRING_ORU_PRACH_CONFIGID,            HLP_ORU_PRACH_CONFIGID,             0,    .uptr=NULL,       .defintval=152,               TYPE_UINT,         0}, \
  {CONFIG_STRING_ORU_PRACH_MSG1FREQ,            HLP_ORU_PRACH_MSG1FREQ,             0,    .uptr=NULL,       .defintval=0,                 TYPE_UINT,         0}, \
  {CONFIG_STRING_ORU_NUMEROLOGY,                HLP_ORU_NUMEROLOGY,                 0,    .uptr=NULL,       .defintval=1,                 TYPE_UINT,         0}, \
  {CONFIG_STRING_ORU_TDD_PERIOD,                HLP_ORU_TDD_PERIOD,                 0,    .uptr=NULL,       .defintval=5,                 TYPE_UINT,         0}, \
  {CONFIG_STRING_ORU_NUM_DL_SLOTS,              HLP_ORU_NUM_DL_SLOTS,               0,    .uptr=NULL,       .defintval=3,                 TYPE_UINT,         0}, \
  {CONFIG_STRING_ORU_NUM_UL_SLOTS,              HLP_ORU_NUM_UL_SLOTS,               0,    .uptr=NULL,       .defintval=1,                 TYPE_UINT,         0}, \
  {CONFIG_STRING_ORU_NUM_DL_SYMBOLS,            HLP_ORU_NUM_DL_SYMBOLS,             0,    .uptr=NULL,       .defintval=7,                 TYPE_UINT,         0}, \
  {CONFIG_STRING_ORU_NUM_UL_SYMBOLS,            HLP_ORU_NUM_UL_SYMBOLS,             0,    .uptr=NULL,       .defintval=3,                 TYPE_UINT,         0}, \
  {CONFIG_STRING_ORU_TX_CORE,                   "The CPU core to be used to deploy south write thread for O-RU.", 0, .iptr=NULL, .defintval=-1, TYPE_INT, 0}, \
}
// clang-format on

#define CONFIG_SECTION_ORU_FH "ORUs.[0].fronthaul"

#define CONFIG_STRING_ORU_DPDK_DEVICES "dpdk_devices"
#define CONFIG_STRING_RX_CORE "rx_core"
#define CONFIG_STRING_EXTRA_EAL_ARGS "extra_eal_args"
#define CONFIG_STRING_DU_MAC_ADDRESSES "du_mac_addr"
#define CONFIG_STRING_MTU "mtu"
#define CONFIG_STRING_T2A_UP "T2a_up"
#define CONFIG_STRING_T2A_CP "T2a_cp"
#define CONFIG_STRING_PRACH_EAXC_OFFSET "prach_eaxc_offset"

#define HLP_DPDK_DEVICES "DPDK devices to use for the O-RU."
#define HLP_RX_CORE "The CPU core to be used to deploy dpdk RX worker for O-RU."
#define HLP_EXTRA_EAL_ARGS "Extra arguments passed to RTE_EAL_INIT."
#define HLP_DU_MAC_ADDRESSES "DU MAC addreses, used to prepare Ethernet headers."
#define HLP_MTU "MTU for RX and TX."
#define HLP_PRACH_EAXC_OFFSET "PRACH eAxC offset."

// clang-format off
#define CMDLINE_PARAMS_DESC_ORU_FH \
{ \
  {CONFIG_STRING_ORU_DPDK_DEVICES,           HLP_DPDK_DEVICES,      PARAMFLAG_MANDATORY,    .strptr=NULL,     .defstrval=NULL,              TYPE_STRINGLIST,   0}, \
  {CONFIG_STRING_RX_CORE,                    HLP_RX_CORE,           PARAMFLAG_MANDATORY,    .iptr=NULL,       .defintval=-1,                TYPE_INT,          0}, \
  {CONFIG_STRING_EXTRA_EAL_ARGS,             HLP_EXTRA_EAL_ARGS,    0,                      .strptr=NULL,     .defstrval=NULL,              TYPE_STRINGLIST,   0}, \
  {CONFIG_STRING_DU_MAC_ADDRESSES,           HLP_DU_MAC_ADDRESSES,  PARAMFLAG_MANDATORY,    .strptr=NULL,     .defstrval=NULL,              TYPE_STRINGLIST,   0}, \
  {CONFIG_STRING_MTU,                        HLP_MTU,               0,                      .iptr=NULL,       .defintval=9600,              TYPE_INT,          0}, \
  {CONFIG_STRING_T2A_UP,                     "",                    0,                      .iptr=NULL,       .defintarrayval=NULL,         TYPE_INTARRAY,     0}, \
  {CONFIG_STRING_T2A_CP,                     "",                    0,                      .iptr=NULL,       .defintarrayval=NULL,         TYPE_INTARRAY,     0}, \
  {CONFIG_STRING_PRACH_EAXC_OFFSET,          HLP_PRACH_EAXC_OFFSET, 0,                      .iptr=NULL,       .defintval=0,                 TYPE_INT,          0}  \
}
// clang-format on

extern void set_scs_parameters(NR_DL_FRAME_PARMS *fp, int mu, int N_RB_DL, int ssb_case);
int tx_rf_symbols(RU_t *ru, int frame, int slot, uint64_t timestamp, int start_symbol, int num_symbols);

void prepare_prach_item(ORU_t *oru)
{
  AssertFatal(oru->ru != NULL, "ORU not configured\n");
  AssertFatal(oru->ru->nr_frame_parms != NULL, "ORU not configured\n");
  NR_DL_FRAME_PARMS *fp = oru->ru->nr_frame_parms;
  RU_t *ru = oru->ru;
  prach_item_t *prach_item = &oru->prach_item;
  prach_item->num_slots = oru->prach_info.format < 4 ? get_long_prach_dur(oru->prach_info.format, fp->numerology_index) : 1;
  prach_item->msg1_frequencystart = oru->prach_msg1_freq;
  prach_item->mu = fp->numerology_index;
  nfapi_nr_config_request_scf_t *cfg = &ru->config;
  prach_item->prach_sequence_length = cfg->prach_config.prach_sequence_length.value;
  prach_item->restricted_set = 0;
  prach_item->numerology_index = fp->numerology_index;
  prach_item->nb_rx = ru->nb_rx;
  prach_item->rx_prach = &oru->rx_prach;

  // Fill PRACH PDU
  nfapi_nr_prach_pdu_t *prach_pdu = &prach_item->pdu;
  prach_pdu->prach_start_symbol = oru->prach_info.start_symbol;
  prach_pdu->num_prach_ocas = 1; // TODO: Hardcoded.

  uint16_t format0 = oru->prach_info.format & 0xff;
  uint16_t format1 = (oru->prach_info.format >> 8) & 0xff;
  if (format1 != 0xff) {
    switch (format0) {
      case 0xa1:
        prach_pdu->prach_format = 11;
        break;
      case 0xa2:
        prach_pdu->prach_format = 12;
        break;
      case 0xa3:
        prach_pdu->prach_format = 13;
        break;
      default:
        AssertFatal(1 == 0, "Only formats A1/B1 A2/B2 A3/B3 are valid for dual format");
    }
  } else {
    switch (format0) {
      case 0:
        prach_pdu->prach_format = 0;
        break;
      case 1:
        prach_pdu->prach_format = 1;
        break;
      case 2:
        prach_pdu->prach_format = 2;
        break;
      case 3:
        prach_pdu->prach_format = 3;
        break;
      case 0xa1:
        prach_pdu->prach_format = 4;
        break;
      case 0xa2:
        prach_pdu->prach_format = 5;
        break;
      case 0xa3:
        prach_pdu->prach_format = 6;
        break;
      case 0xb1:
        prach_pdu->prach_format = 7;
        break;
      case 0xb4:
        prach_pdu->prach_format = 8;
        break;
      case 0xc0:
        prach_pdu->prach_format = 9;
        break;
      case 0xc2:
        prach_pdu->prach_format = 10;
        break;
      default:
        AssertFatal(1 == 0, "Invalid PRACH format");
    }
  }
}

int get_oru_options(ORU_t *oru)
{
  int DEFBW[] = {273};
  int DEFCARRIER[] = {3430560};
  paramdef_t param[] = CMDLINE_PARAMS_DESC_ORU;
  int nump = sizeofArray(param);

  int ret = config_get(config_get_if(), param, nump, CONFIG_SECTION_ORU);
  if (ret <= 0) {
    LOG_E(NR_PHY, "problem reading section \"%s\"\n", CONFIG_SECTION_ORU);
    return -1;
  }

  for (int i = 0; i < oru->ru->num_bands; i++) {
    oru->bw_tx[i] = gpd(param, nump, CONFIG_STRING_ORU_TX_BW_LIST)->iptr[i];
    oru->bw_rx[i] = gpd(param, nump, CONFIG_STRING_ORU_RX_BW_LIST)->iptr[i];
    oru->carrier_freq_tx[i] = gpd(param, nump, CONFIG_STRING_ORU_CARRIER_TX_LIST)->iptr[i];
    oru->carrier_freq_rx[i] = gpd(param, nump, CONFIG_STRING_ORU_CARRIER_RX_LIST)->iptr[i];
  }
  oru->frame_type = *gpd(param, nump, CONFIG_STRING_ORU_FRAME_TYPE)->iptr;
  oru->prach_config_index = *gpd(param, nump, CONFIG_STRING_ORU_PRACH_CONFIGID)->iptr;
  oru->prach_msg1_freq = *gpd(param, nump, CONFIG_STRING_ORU_PRACH_MSG1FREQ)->iptr;
  oru->numerology = *gpd(param, nump, CONFIG_STRING_ORU_NUMEROLOGY)->iptr;
  oru->tdd_period = *gpd(param, nump, CONFIG_STRING_ORU_TDD_PERIOD)->iptr;
  oru->num_DL_slots = *gpd(param, nump, CONFIG_STRING_ORU_NUM_DL_SLOTS)->iptr;
  oru->num_UL_slots = *gpd(param, nump, CONFIG_STRING_ORU_NUM_UL_SLOTS)->iptr;
  oru->num_DL_symbols = *gpd(param, nump, CONFIG_STRING_ORU_NUM_DL_SYMBOLS)->iptr;
  oru->num_UL_symbols = *gpd(param, nump, CONFIG_STRING_ORU_NUM_UL_SYMBOLS)->iptr;
  oru->tx_core = *gpd(param, nump, CONFIG_STRING_ORU_TX_CORE)->iptr;

  paramdef_t fh_param[] = CMDLINE_PARAMS_DESC_ORU_FH;
  nump = sizeofArray(fh_param);
  oru_fh_config_t *fh_cfg = &oru->fh_config;
  ret = config_get(config_get_if(), fh_param, nump, CONFIG_SECTION_ORU_FH);
  if (ret <= 0) {
    printf("problem reading section \"%s\"\n", CONFIG_SECTION_ORU_FH);
    return -1;
  }

  oru_fh_dpdk_config_t *dpdk_conf = &fh_cfg->dpdk_conf;
  int num_dpdk_devices = gpd(fh_param, nump, CONFIG_STRING_ORU_DPDK_DEVICES)->numelt;
  dpdk_conf->num_dpdk_devices = num_dpdk_devices;
  AssertFatal(num_dpdk_devices > 0 && num_dpdk_devices <= 2,
              "Invalid number of DPDK devices (%d). Configure 1 or 2 devices\n",
              num_dpdk_devices);
  for (int i = 0; i < num_dpdk_devices; i++) {
    dpdk_conf->dpdk_devices[i] = gpd(fh_param, nump, CONFIG_STRING_ORU_DPDK_DEVICES)->strlistptr[i];
  }
  dpdk_conf->extra_eal_args = gpd(fh_param, nump, CONFIG_STRING_EXTRA_EAL_ARGS)->strlistptr;
  dpdk_conf->num_extra_eal_args = gpd(fh_param, nump, CONFIG_STRING_EXTRA_EAL_ARGS)->numelt;

  fh_cfg->num_du_mac_addrs = gpd(fh_param, nump, CONFIG_STRING_DU_MAC_ADDRESSES)->numelt;
  for (int i = 0; i < fh_cfg->num_du_mac_addrs; i++) {
    fh_cfg->du_mac_addrs[i] = gpd(fh_param, nump, CONFIG_STRING_DU_MAC_ADDRESSES)->strlistptr[i];
    AssertFatal(strlen(fh_cfg->du_mac_addrs[i]) == 17, "Invalid MAC address\n");
  }
  fh_cfg->enable_compression = false;
  fh_cfg->rx_core = *gpd(fh_param, nump, CONFIG_STRING_RX_CORE)->iptr;
  fh_cfg->mtu = *gpd(fh_param, nump, CONFIG_STRING_MTU)->iptr;
  fh_cfg->num_prbs = oru->bw_tx[0];
  fh_cfg->numerology = oru->numerology;
  fh_cfg->prach_eaxc_offset = *gpd(fh_param, nump, CONFIG_STRING_PRACH_EAXC_OFFSET)->iptr;

  AssertFatal(gpd(fh_param, nump, CONFIG_STRING_T2A_UP)->numelt == 2, "Two parameters required for %s\n", CONFIG_STRING_T2A_UP);
  fh_cfg->T2a_up_min_uS = gpd(fh_param, nump, CONFIG_STRING_T2A_UP)->iptr[0];
  fh_cfg->T2a_up_max_uS = gpd(fh_param, nump, CONFIG_STRING_T2A_UP)->iptr[1];
  AssertFatal(fh_cfg->T2a_up_min_uS <= fh_cfg->T2a_up_max_uS,
              "T2a max (%d) has to be greater than T2a min (%d)\n",
              fh_cfg->T2a_up_max_uS,
              fh_cfg->T2a_up_min_uS);

  AssertFatal(gpd(fh_param, nump, CONFIG_STRING_T2A_CP)->numelt == 2, "Two parameters required for %s\n", CONFIG_STRING_T2A_CP);
  fh_cfg->T2a_cp_min_uS = gpd(fh_param, nump, CONFIG_STRING_T2A_CP)->iptr[0];
  fh_cfg->T2a_cp_max_uS = gpd(fh_param, nump, CONFIG_STRING_T2A_CP)->iptr[1];
  AssertFatal(fh_cfg->T2a_cp_min_uS <= fh_cfg->T2a_cp_max_uS,
              "T2a max (%d) has to be greater than T2a min (%d)\n",
              fh_cfg->T2a_cp_max_uS,
              fh_cfg->T2a_cp_min_uS);

  oru_fh_tdd_pattern_t *tdd_pattern = &fh_cfg->tdd_pattern;
  tdd_pattern->num_dl_slots = oru->num_DL_slots;
  tdd_pattern->num_ul_slots = oru->num_UL_slots;
  tdd_pattern->num_dl_symbols = oru->num_DL_symbols;
  tdd_pattern->num_ul_symbols = oru->num_UL_symbols;
  int num_slots_frame = (1 << oru->numerology) * NR_NUMBER_OF_SUBFRAMES_PER_FRAME;
  int num_period_frame = get_nb_periods_per_frame(oru->tdd_period);
  int num_slots_period = num_slots_frame / num_period_frame;
  tdd_pattern->tdd_pattern_length_slots = num_slots_period;

  return 0;
}

void oru_init_frame_parms(ORU_t *oru)
{
  RU_t *ru = oru->ru;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;

  fp->frame_type = oru->frame_type;
  ru->config.cell_config.frame_duplex_type.value = oru->frame_type;
  ru->config.cell_config.frame_duplex_type.tl.tag = 0x100D;
  fp->N_RB_DL = oru->bw_tx[0];
  ru->config.ssb_config.scs_common.value = ru->numerology;
  ru->config.carrier_config.dl_grid_size[ru->config.ssb_config.scs_common.value].value = oru->bw_tx[0];
  fp->N_RB_UL = oru->bw_rx[0];
  ru->config.carrier_config.ul_grid_size[ru->config.ssb_config.scs_common.value].value = oru->bw_rx[0];
  fp->numerology_index = ru->numerology;
  LOG_I(NR_PHY,
        "Set RU frame type to %s, N_RB_DL %d, N_RB_UL %d, mu %d\n",
        oru->frame_type == TDD ? "TDD" : "FDD",
        oru->bw_tx[0],
        oru->bw_rx[0],
        ru->numerology);
  set_scs_parameters(fp, fp->numerology_index, oru->bw_tx[0], 0);
  fp->slots_per_frame = 10 * fp->slots_per_subframe;
  fp->nb_antennas_rx = ru->nb_rx;
  fp->nb_antennas_tx = ru->nb_tx;
  fp->symbols_per_slot = 14;
  fp->samples_per_subframe_wCP = fp->ofdm_symbol_size * fp->symbols_per_slot * fp->slots_per_subframe;
  fp->samples_per_frame_wCP = 10 * fp->samples_per_subframe_wCP;
  fp->samples_per_slot_wCP = fp->symbols_per_slot * fp->ofdm_symbol_size;
  fp->samples_per_slotN0 = (fp->nb_prefix_samples + fp->ofdm_symbol_size) * fp->symbols_per_slot;
  fp->samples_per_slot0 =
      fp->nb_prefix_samples0 + ((fp->symbols_per_slot - 1) * fp->nb_prefix_samples) + (fp->symbols_per_slot * fp->ofdm_symbol_size);
  fp->samples_per_subframe = (fp->nb_prefix_samples0 + fp->ofdm_symbol_size) * 2
                             + (fp->nb_prefix_samples + fp->ofdm_symbol_size) * (fp->symbols_per_slot * fp->slots_per_subframe - 2);
  fp->samples_per_frame = 10 * fp->samples_per_subframe;
  fp->freq_range = (oru->carrier_freq_tx[0] < 6e6) ? FR1 : FR2;

  fp->dl_CarrierFreq = (double)oru->carrier_freq_tx[0] * 1000;
  fp->ul_CarrierFreq = (double)oru->carrier_freq_rx[0] * 1000;
  fp->Ncp = NORMAL;
  fp->ofdm_offset_divisor = 8;

  // Split 7.2 parameters
  ru->config.prach_config.num_prach_fd_occasions.value = 1;
  ru->config.prach_config.prach_ConfigurationIndex.value = oru->prach_config_index;
  ru->config.prach_config.prach_ConfigurationIndex.tl.tag = 0x1029;
  ru->config.prach_config.num_prach_fd_occasions_list = malloc(sizeof(*ru->config.prach_config.num_prach_fd_occasions_list));
  ru->config.prach_config.num_prach_fd_occasions_list[0].k1.value = oru->prach_msg1_freq;
  if (ru->config.cell_config.frame_duplex_type.value == 1 /* TDD */) {
    ru->config.tdd_table.tdd_period.value = oru->tdd_period;
    ru->config.tdd_table.tdd_period.tl.tag = 0x1026;
    int numb_slots_frame = (1 << ru->numerology) * NR_NUMBER_OF_SUBFRAMES_PER_FRAME;
    int numb_period_frame = get_nb_periods_per_frame(oru->tdd_period);
    int numb_slots_period = numb_slots_frame / numb_period_frame;
    ru->config.tdd_table.max_tdd_periodicity_list =
        malloc(sizeof(*ru->config.tdd_table.max_tdd_periodicity_list) * (numb_slots_frame));
    for (int n = 0; n < numb_slots_frame; n++) {
      int s = 0;
      int p = n % numb_slots_period;
      if (p < oru->num_DL_slots) {
        ru->config.tdd_table.max_tdd_periodicity_list[n].max_num_of_symbol_per_slot_list =
            malloc(sizeof(*ru->config.tdd_table.max_tdd_periodicity_list[n].max_num_of_symbol_per_slot_list) * NR_SYMBOLS_PER_SLOT);
        for (s = 0; s < 14; s++)
          ru->config.tdd_table.max_tdd_periodicity_list[n].max_num_of_symbol_per_slot_list[s].slot_config.value = 0;
      } else if (p == oru->num_DL_slots) {
        ru->config.tdd_table.max_tdd_periodicity_list[n].max_num_of_symbol_per_slot_list =
            malloc(sizeof(*ru->config.tdd_table.max_tdd_periodicity_list[n].max_num_of_symbol_per_slot_list) * NR_SYMBOLS_PER_SLOT);
        for (s = 0; s < oru->num_DL_symbols; s++)
          ru->config.tdd_table.max_tdd_periodicity_list[n].max_num_of_symbol_per_slot_list[s].slot_config.value = 0;
        for (; s < NR_SYMBOLS_PER_SLOT - oru->num_UL_symbols; s++)
          ru->config.tdd_table.max_tdd_periodicity_list[n].max_num_of_symbol_per_slot_list[s].slot_config.value = 2;
        for (; s < NR_SYMBOLS_PER_SLOT; s++)
          ru->config.tdd_table.max_tdd_periodicity_list[n].max_num_of_symbol_per_slot_list[s].slot_config.value = 1;
      } else {
        ru->config.tdd_table.max_tdd_periodicity_list[n].max_num_of_symbol_per_slot_list =
            malloc(sizeof(*ru->config.tdd_table.max_tdd_periodicity_list[n].max_num_of_symbol_per_slot_list) * NR_SYMBOLS_PER_SLOT);
        for (s = 0; s < NR_SYMBOLS_PER_SLOT; s++)
          ru->config.tdd_table.max_tdd_periodicity_list[n].max_num_of_symbol_per_slot_list[s].slot_config.value = 1;
      }
    }
  }
}

void fft_and_cp_insertion(NR_DL_FRAME_PARMS *fp, c16_t *txdataF, c16_t *txdata, int slot, int symbol)
{
  if (fp->Ncp == 1) {
    PHY_ofdm_mod((int *)txdataF, (int *)txdata, fp->ofdm_symbol_size, 1, fp->nb_prefix_samples, CYCLIC_PREFIX);
  } else {
    if (fp->numerology_index != 0) {
      if (!(slot % (fp->slots_per_subframe / 2)) && (symbol == 0)) {
        PHY_ofdm_mod((int *)txdataF, (int *)txdata, fp->ofdm_symbol_size, 1, fp->nb_prefix_samples0, CYCLIC_PREFIX);
      } else {
        PHY_ofdm_mod((int *)txdataF, (int *)txdata, fp->ofdm_symbol_size, 1, fp->nb_prefix_samples, CYCLIC_PREFIX);
      }
    } else {
      if (symbol % 0x7) {
        PHY_ofdm_mod((int *)txdataF, (int *)txdata, fp->ofdm_symbol_size, 1, fp->nb_prefix_samples, CYCLIC_PREFIX);
      } else {
        PHY_ofdm_mod((int *)txdataF, (int *)txdata, fp->ofdm_symbol_size, 1, fp->nb_prefix_samples0, CYCLIC_PREFIX);
      }
    }
  }
}

static void dl_symbol_process(ORU_t *oru, int frame, int slot, int symbol, c16_t **txDataF, int64_t timestamp, uint64_t abs_symbol)
{
  RU_t *ru = oru->ru;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;
  uint32_t slot_offset = get_samples_slot_timestamp(fp, slot);
  uint32_t symbol_offset = get_samples_symbol_timestamp(fp, slot, symbol);

  __attribute__((aligned(64))) c16_t txdataF_shifted[fp->ofdm_symbol_size];
  memset(txdataF_shifted, 0, sizeof(txdataF_shifted));
  c16_t *rotation = fp->symbol_rotation[0] + (slot % fp->slots_per_subframe) * fp->symbols_per_slot + symbol;
  for (int aatx = 0; aatx < ru->nb_tx; aatx++) {
    // Phase compensation
    rotate_cpx_vector(txDataF[aatx], *rotation, txDataF[aatx], fp->N_RB_DL * NR_NB_SC_PER_RB, 15);
    // FFT Shift
    const int num_samp_half = fp->N_RB_DL * NR_NB_SC_PER_RB / 2;
    const int first_carrier_offset = fp->ofdm_symbol_size - num_samp_half;
    memcpy(txdataF_shifted + first_carrier_offset, txDataF[aatx], num_samp_half * sizeof(c16_t));
    memcpy(txdataF_shifted, txDataF[aatx] + num_samp_half, num_samp_half * sizeof(c16_t));
    fft_and_cp_insertion(ru->nr_frame_parms,
                         txdataF_shifted,
                         (c16_t *)&ru->common.txdata[aatx][slot_offset + symbol_offset],
                         slot,
                         symbol);
  }

  pthread_mutex_lock(&oru->tx_write_mutex);
  oru->latest_written_symbol_index = abs_symbol;
  pthread_cond_signal(&oru->tx_write_cond);
  pthread_mutex_unlock(&oru->tx_write_mutex);
}

static pthread_mutex_t south_read_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t south_read_cond = PTHREAD_COND_INITIALIZER;
static bool south_read_ready = false;
static uint64_t notified_hyper_frame = 0;
static uint32_t notified_start_frame = 0;
static uint32_t notified_start_slot = 0;
static openair0_timestamp_t notified_timestamp = 0;

void notify_north_read(uint64_t hyper_frame, uint32_t start_frame, uint32_t start_slot, openair0_timestamp_t timestamp)
{
  pthread_mutex_lock(&south_read_mutex);
  notified_hyper_frame = hyper_frame;
  notified_start_frame = start_frame;
  notified_start_slot = start_slot;
  notified_timestamp = timestamp;
  south_read_ready = true;
  pthread_cond_signal(&south_read_cond);
  pthread_mutex_unlock(&south_read_mutex);
}

void wait_for_south_read(uint64_t *hyper_frame, uint32_t *start_frame, uint32_t *start_slot, int64_t *timestamp)
{
  pthread_mutex_lock(&south_read_mutex);
  while (!south_read_ready) {
    pthread_cond_wait(&south_read_cond, &south_read_mutex);
  }
  *hyper_frame = notified_hyper_frame;
  *start_frame = notified_start_frame;
  *start_slot = notified_start_slot;
  *timestamp = notified_timestamp;
  pthread_mutex_unlock(&south_read_mutex);
}

void *oru_north_read_thread(void *arg)
{
  ORU_t *oru = (ORU_t *)arg;

  RU_t *ru = (RU_t *)oru->ru;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;

  __attribute__((aligned(64))) c16_t txDataF[ru->nb_tx][fp->N_RB_DL * NR_NB_SC_PER_RB];
  memset(txDataF, 0, sizeof(txDataF));
  c16_t *txDataF_ptr[ru->nb_tx];
  for (int aatx = 0; aatx < ru->nb_tx; aatx++) {
    txDataF_ptr[aatx] = txDataF[aatx];
  }
  uint32_t start_frame, start_slot;
  uint64_t start_hyper_frame;
  int64_t start_timestamp;
  if (ru->rfdevice.get_timestamp) {
    struct timespec utc_anchor_point;
    oru_fh_get_utc_anchor_point(oru->fronthaul, &start_hyper_frame, &start_frame, &start_slot, &utc_anchor_point);
    start_timestamp = ru->rfdevice.get_timestamp(&ru->rfdevice, &utc_anchor_point);
  } else {
    wait_for_south_read(&start_hyper_frame, &start_frame, &start_slot, &start_timestamp);
  }
    // subtract the start_frame and start_slot from the timestamp simplify calculation below.
  start_timestamp -= (start_frame * fp->samples_per_frame + get_samples_slot_timestamp(fp, start_slot));
  // Now start_timestamp points to the start sample of the frame 0 slot 0 symbol 0 of hyperframe 0
  LOG_A(PHY, "DL thread started: start_timestamp %ld, start_frame %d, start_slot %d\n", start_timestamp, start_frame, start_slot);

  pthread_mutex_lock(&oru->tx_write_mutex);
  oru->start_timestamp = start_timestamp;
  oru->start_symbol_index = start_frame * (fp->slots_per_frame * fp->symbols_per_slot) + start_slot * fp->symbols_per_slot;
  oru->latest_written_symbol_index = oru->start_symbol_index - 1;
  oru->tx_write_initialized = true;
  pthread_cond_signal(&oru->tx_write_cond);
  pthread_mutex_unlock(&oru->tx_write_mutex);

  while (!oai_exit) {
    int frame = -1, slot = -1, symbol = -1;
    uint64_t hyper_frame;
    int ret = oru_fh_tx_read_symbol(oru->fronthaul, (uint32_t **)txDataF_ptr, ru->nb_tx, &hyper_frame, &frame, &slot, &symbol);
    if (ret != 0) {
      LOG_E(PHY, "[RU_thread] read data error: frame %d, slot %d, symbol %d\n", frame, slot, symbol);
      continue;
    }
    if (start_hyper_frame > hyper_frame) {
      continue;
    }
    uint64_t num_frames = (hyper_frame - start_hyper_frame) * 1024 + frame;
    int64_t timestamp = start_timestamp + num_frames * fp->samples_per_frame + get_samples_slot_timestamp(fp, slot)
                        + get_samples_symbol_timestamp(fp, slot, symbol);
    if (timestamp < 0) {
      continue;
    }
    uint64_t abs_symbol = num_frames * (fp->slots_per_frame * fp->symbols_per_slot) + slot * fp->symbols_per_slot + symbol;
    dl_symbol_process(oru, frame, slot, symbol, txDataF_ptr, timestamp, abs_symbol);
    if (frame % 256 == 0 && slot == 0 && symbol == 0) {
      LOG_I(PHY, "[RU_thread] read data: frame %d, slot %d, symbol %d\n", frame, slot, symbol);
    }
  }
  return NULL;
}

// Returns PRACH symbol that was received in current frame, slot and symbol.
// If no PRACH symbol was received, returns -1
int get_prach_symbol(ORU_t *oru, int frame, int slot, int symbol, int numerology)
{
  uint16_t RA_sfn_index;
  AssertFatal(oru->ru->nr_frame_parms->frame_type == TDD, "Only supports TDD\n");
  if (get_nr_prach_sched_from_info(oru->prach_info, oru->prach_config_index, frame, slot, numerology, FR1, &RA_sfn_index, true)) {
    int format = oru->prach_item.pdu.prach_format;
    int start_symbol = oru->prach_item.pdu.prach_start_symbol;
    symbol -= start_symbol;
    // TODO: Support more PRACH formats
    AssertFatal(format == 8, "only support format B4\n");
    // TODO: This is not exactly the case but it is correct
    if (symbol >= 0 && symbol < 12) {
      return symbol;
    }
  }
  return -1;
}

void receive_prach(ORU_t *oru, int frame, int slot, int symbol, int prach_symbol)
{
  RU_t *ru = oru->ru;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;
  oru->prach_item.frame = frame;
  oru->prach_item.slot = slot;

  c16_t rxdataF[ru->nb_rx][NR_PRACH_SEQ_LEN_L];
  memset(rxdataF, 0, sizeof(rxdataF));

  rx_nr_prach_ru_rep(&oru->prach_item,
                     ru->common.rxdata,
                     fp,
                     ru->N_TA_offset,
                     prach_symbol,
                     0, // prachOccasion
                     rxdataF);

  c16_t *rxdataF_ptr[ru->nb_rx];
  for (int aarx = 0; aarx < ru->nb_rx; aarx++) {
    rxdataF_ptr[aarx] = rxdataF[aarx];
  }
  oru_fh_rx_send_prach(oru->fronthaul, (uint32_t **)rxdataF_ptr, ru->nb_rx, frame, slot, symbol);
}

#define MAX_PENDING_UL_JOBS 64

typedef struct {
  ul_job_t job;
  bool active;
  int symbols_sent;
} ul_pending_t;

static void receive_pusch(ORU_t *oru, int frame, int slot, int symbol, ul_job_t *job)
{
  RU_t *ru = oru->ru;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;
  int aarx = job->antenna_id;

  if (aarx < 0 || aarx >= fp->nb_antennas_rx) {
    LOG_W(PHY, "[ORU south] receive_pusch: invalid antenna_id %d\n", aarx);
    return;
  }

  // CP removal + FFT → full ofdm_symbol_size frequency-domain output
  c16_t rxdataF_fft[fp->ofdm_symbol_size] __attribute__((aligned(32)));
  nr_symbol_fep_ul(fp, (c16_t *)ru->common.rxdata[aarx], rxdataF_fft, symbol, slot, ru->N_TA_offset);

  // Phase decompensation (conjugate rotation for UL)
  apply_nr_rotation_symbol_RX(fp->symbols_per_slot,
                              fp->slots_per_subframe,
                              fp->timeshift_symbol_rotation,
                              fp->first_carrier_offset,
                              rxdataF_fft,
                              fp->symbol_rotation[link_type_ul],
                              fp->N_RB_UL,
                              slot,
                              symbol);

  // Inverse FFT shift: split format → contiguous PRB format sent to DU.
  // DL TX shift:   contiguous[0..N/2-1]   → FFT_input[first_carrier_offset..]  (negative freqs)
  //                contiguous[N/2..N-1]   → FFT_input[0..N/2-1]               (positive freqs)
  // UL RX inverse: FFT_out[first_carrier_offset..] → contiguous[0..N/2-1]
  //                FFT_out[0..N/2-1]              → contiguous[N/2..N-1]
  const int num_samp_half = fp->N_RB_UL * NR_NB_SC_PER_RB / 2;
  const int first_carrier_offset = fp->ofdm_symbol_size - num_samp_half;
  c16_t rxdataF[fp->N_RB_UL * NR_NB_SC_PER_RB];
  memcpy(rxdataF, rxdataF_fft + first_carrier_offset, num_samp_half * sizeof(c16_t));
  memcpy(rxdataF + num_samp_half, rxdataF_fft, num_samp_half * sizeof(c16_t));

  oru_fh_rx_send_pusch(oru->fronthaul, (uint32_t *)rxdataF, symbol, job);
}

#define UL_WORK_QUEUE_DEPTH 128

typedef struct {
  ORU_t *oru;
  int frame;
  int slot;
  int symbol;
  ul_job_t job;
} ul_work_item_t;

typedef struct {
  ul_work_item_t ring[UL_WORK_QUEUE_DEPTH];
  int head;
  int tail;
  int count;
  pthread_mutex_t lock;
  pthread_cond_t work_available;
  pthread_cond_t space_available;
  bool running;
} ul_work_queue_t;

static void *ul_worker_thread(void *arg)
{
  ul_work_queue_t *q = arg;
  while (1) {
    pthread_mutex_lock(&q->lock);
    while (q->count == 0 && q->running)
      pthread_cond_wait(&q->work_available, &q->lock);
    if (!q->running && q->count == 0) {
      pthread_mutex_unlock(&q->lock);
      break;
    }
    ul_work_item_t item = q->ring[q->head];
    q->head = (q->head + 1) % UL_WORK_QUEUE_DEPTH;
    q->count--;
    pthread_cond_signal(&q->space_available);
    pthread_mutex_unlock(&q->lock);

    receive_pusch(item.oru, item.frame, item.slot, item.symbol, &item.job);
  }
  return NULL;
}

static void dispatch_ul_work(ul_work_queue_t *q, ORU_t *oru, int frame, int slot, int symbol, const ul_job_t *job)
{
  pthread_mutex_lock(&q->lock);
  while (q->count == UL_WORK_QUEUE_DEPTH)
    pthread_cond_wait(&q->space_available, &q->lock);
  q->ring[q->tail] = (ul_work_item_t){.oru = oru, .frame = frame, .slot = slot, .symbol = symbol, .job = *job};
  q->tail = (q->tail + 1) % UL_WORK_QUEUE_DEPTH;
  q->count++;
  pthread_cond_signal(&q->work_available);
  pthread_mutex_unlock(&q->lock);
}

void *oru_south_write_thread(void *arg)
{
  ORU_t *oru = (ORU_t *)arg;
  RU_t *ru = oru->ru;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;

  LOG_A(PHY, "South write thread started on core %d\n", oru->tx_core);

  uint64_t total_write_calls = 0;
  uint64_t total_symbols_sent = 0;
  uint64_t coalesce_histogram[NR_SYMBOLS_PER_SLOT + 1] = {0}; // max coalesced is 14

  pthread_mutex_lock(&oru->tx_write_mutex);
  while (!oru->tx_write_initialized && !oai_exit) {
    pthread_cond_wait(&oru->tx_write_cond, &oru->tx_write_mutex);
  }
  if (oai_exit) {
    pthread_mutex_unlock(&oru->tx_write_mutex);
    return NULL;
  }
  uint64_t next_symbol_to_send = oru->start_symbol_index;
  pthread_mutex_unlock(&oru->tx_write_mutex);

  while (!oai_exit) {
    pthread_mutex_lock(&oru->tx_write_mutex);
    while (oru->latest_written_symbol_index < next_symbol_to_send && !oai_exit) {
      pthread_cond_wait(&oru->tx_write_cond, &oru->tx_write_mutex);
    }
    if (oai_exit) {
      pthread_mutex_unlock(&oru->tx_write_mutex);
      break;
    }

    uint64_t latest = oru->latest_written_symbol_index;
    int64_t start_ts = oru->start_timestamp;
    pthread_mutex_unlock(&oru->tx_write_mutex);

    // Process all symbols from next_symbol_to_send to latest
    while (next_symbol_to_send <= latest) {
      int symbols_per_slot = fp->symbols_per_slot;
      int symbols_per_frame = fp->slots_per_frame * symbols_per_slot;

      uint64_t abs_frame = next_symbol_to_send / symbols_per_frame;
      int frame = abs_frame % 1024;
      int slot = (next_symbol_to_send % symbols_per_frame) / symbols_per_slot;
      int symbol = next_symbol_to_send % symbols_per_slot;

      // Calculate timestamp for the first symbol in this batch
      int64_t timestamp = start_ts + abs_frame * fp->samples_per_frame + get_samples_slot_timestamp(fp, slot)
                          + get_samples_symbol_timestamp(fp, slot, symbol);

      // How many symbols can we send in the same slot?
      // Since they are contiguous, the maximum number is up to the end of the slot
      uint64_t remaining_in_batch = latest - next_symbol_to_send + 1;
      int max_in_slot = symbols_per_slot - symbol;
      int num_coalesced = (remaining_in_batch < (uint64_t)max_in_slot) ? (int)remaining_in_batch : max_in_slot;

      // Send the coalesced symbols
      int sent = tx_rf_symbols(ru, frame, slot, timestamp, symbol, num_coalesced);

      if (sent > 0) {
        total_write_calls++;
        total_symbols_sent += sent;
        if (sent >= 1 && sent <= NR_SYMBOLS_PER_SLOT) {
          coalesce_histogram[sent]++;
        }
      }

      if (total_write_calls % (NR_SYMBOLS_PER_SLOT * 1000) == 0) {
        LOG_I(PHY, "O-RU South Write Coalescing Stats: calls=%lu, symbols=%lu, avg_coalesced=%.2f\n",
              total_write_calls, total_symbols_sent, (double)total_symbols_sent / total_write_calls);
        if (total_symbols_sent > total_write_calls) {
          uint64_t c3_4 = coalesce_histogram[3] + coalesce_histogram[4];
          uint64_t c5_6 = coalesce_histogram[5] + coalesce_histogram[6];
          uint64_t c7_9 = coalesce_histogram[7] + coalesce_histogram[8] + coalesce_histogram[9];
          uint64_t c10_plus = 0;
          for (int c = 10; c <= NR_SYMBOLS_PER_SLOT; c++) c10_plus += coalesce_histogram[c];
          LOG_I(PHY, "  Coalesced distribution: [1]:%lu, [2]:%lu, [3-4]:%lu, [5-6]:%lu, [7-9]:%lu, [10+]:%lu\n",
                coalesce_histogram[1], coalesce_histogram[2], c3_4, c5_6, c7_9, c10_plus);
        }
      }

      next_symbol_to_send += num_coalesced;
    }
  }

  return NULL;
}

void *oru_south_read_thread(void *arg)
{
  ORU_t *oru = arg;
  RU_t *ru = oru->ru;
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;
  int slot, frame;
  uint32_t start_frame = 0, start_slot = 0;
  uint64_t hyper_frame = 0;
  struct timespec utc_anchor_point;
  if (ru->rfdevice.get_timestamp) {
    AssertFatal(ru->rfdevice.get_timestamp != NULL, "rfdevice has no capability to translate UTC timestamp to sample index\n");
    oru_fh_get_utc_anchor_point(oru->fronthaul, &hyper_frame, &start_frame, &start_slot, &utc_anchor_point);
    int64_t start_timestamp = ru->rfdevice.get_timestamp(&ru->rfdevice, &utc_anchor_point);

    const int num_samples = 3000;
    c16_t throwaway_samples[ru->nb_rx][num_samples];
    void *rxp[ru->nb_rx];
    for (int i = 0; i < ru->nb_rx; i++)
      rxp[i] = throwaway_samples[i];

    openair0_timestamp_t timestamp;
    int num_samples_read = ru->rfdevice.trx_read_func(&ru->rfdevice, &timestamp, rxp, num_samples, ru->nb_rx);
    AssertFatal(num_samples_read == num_samples, "Unexpected number of samples received\n");
    openair0_timestamp_t next_timestamp = timestamp + num_samples_read;
    while (next_timestamp > start_timestamp) {
      start_timestamp += get_samples_slot_duration(fp, start_slot, 1);
      start_slot++;
      if (start_slot == fp->slots_per_frame) {
        start_slot = 0;
        start_frame++;
        if (start_frame == 1024) {
          start_frame = 0;
        }
      }
    }
    while (next_timestamp < start_timestamp) {
      int num_samples_to_read = min(num_samples, (int)(start_timestamp - next_timestamp));
      int num_samples_read = ru->rfdevice.trx_read_func(&ru->rfdevice, &timestamp, rxp, num_samples_to_read, ru->nb_rx);
      AssertFatal(num_samples_read == num_samples_to_read, "Unexpected number of samples received\n");
      next_timestamp += num_samples_read;
    }

    AssertFatal(next_timestamp == start_timestamp, "O-RU South thread could not sync to UTC anchor point\n");
  } else {
    int num_iter = 100;
    const int num_samples = 3000;
    openair0_timestamp_t timestamp;
    c16_t throwaway_samples[ru->nb_rx][num_samples];
    void *rxp[ru->nb_rx];
    for (int i = 0; i < ru->nb_rx; i++) {
      rxp[i] = throwaway_samples[i];
    }
    while (!oai_exit && num_iter-- > 0) {
      int num_samples_read = ru->rfdevice.trx_read_func(&ru->rfdevice, &timestamp, rxp, num_samples, ru->nb_rx);
      AssertFatal(num_samples_read == num_samples, "Unexpected number of samples received\n");
    }
    oru_fh_get_utc_anchor_point(oru->fronthaul, &hyper_frame, &start_frame, &start_slot, &utc_anchor_point);
    timestamp += num_samples;
    notify_north_read(hyper_frame, start_frame, start_slot, timestamp);
  }
  slot = start_slot;
  frame = start_frame;

  // Worker pool: one thread per RX antenna so all antennas in a symbol process in parallel.
  const int num_workers = ru->nb_rx;
  pthread_t workers[num_workers];
  ul_work_queue_t work_queue = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .work_available = PTHREAD_COND_INITIALIZER,
    .space_available = PTHREAD_COND_INITIALIZER,
    .running = true,
  };
  for (int i = 0; i < num_workers; i++) {
    char name[32];
    snprintf(name, sizeof(name), "ul_worker_%d", i);
    threadCreate(&workers[i], ul_worker_thread, &work_queue, name, -1, OAI_PRIORITY_RT_MAX);
  }

  ul_pending_t pending_ul[MAX_PENDING_UL_JOBS] = {0};

  while (!oai_exit) {
    int rx_slot_type = nr_slot_select(&ru->config, frame, slot);
    for (int symbol = 0; symbol < 14; symbol++) {
      int samples_to_read = get_samples_symbol_duration(fp, slot, symbol, 1);
      size_t offset = get_samples_slot_timestamp(fp, slot) + get_samples_symbol_timestamp(fp, slot, symbol);
      c16_t *rxp[fp->nb_antennas_rx];
      for (int aarx = 0; aarx < fp->nb_antennas_rx; aarx++) {
        rxp[aarx] = (c16_t *)&ru->common.rxdata[aarx][offset];
      }

      openair0_timestamp_t timestamp;
      int num_samples_read = ru->rfdevice.trx_read_func(&ru->rfdevice, &timestamp, (void **)rxp, samples_to_read, ru->nb_rx);
      AssertFatal(num_samples_read == samples_to_read, "Unexpected number of samples received\n");
      LOG_D(PHY,
            "[ORU south] read data: frame %d, slot %d, symbol %d, timestamp %ld num_symbols %d, samples %d\n",
            frame,
            slot,
            symbol,
            timestamp,
            1,
            num_samples_read);

      // Drain the UL job ring
      ul_job_t new_job;
      while (oru_fh_poll_ul_job(oru->fronthaul, &new_job) == 0) {
        bool added = false;
        for (int i = 0; i < MAX_PENDING_UL_JOBS; i++) {
          if (!pending_ul[i].active) {
            pending_ul[i] = (ul_pending_t){.job = new_job, .active = true, .symbols_sent = 0};
            added = true;
            break;
          }
        }
        if (!added)
          LOG_W(PHY, "[ORU south] UL pending queue full, dropping job frame=%d slot=%d sym=%d\n",
                new_job.frame, new_job.slot_in_frame, new_job.symbol);
      }

      if (rx_slot_type == NR_UPLINK_SLOT || rx_slot_type == NR_MIXED_SLOT) {

        // Process pending jobs whose next symbol matches the current one
        for (int i = 0; i < MAX_PENDING_UL_JOBS; i++) {
          if (!pending_ul[i].active)
            continue;
          ul_job_t *j = &pending_ul[i].job;

          // Skip jobs scheduled for a future slot or frame, and drop jobs in the past
          int slots_per_frame = fp->slots_per_frame;
          int total_slots = 1024 * slots_per_frame;
          int diff_slots = (j->frame * slots_per_frame + j->slot_in_frame) - (frame * slots_per_frame + slot);
          if (diff_slots < -total_slots / 2) {
            diff_slots += total_slots;
          } else if (diff_slots > total_slots / 2) {
            diff_slots -= total_slots;
          }

          if (diff_slots > 0) {
            // Future slot: skip
            continue;
          }
          if (diff_slots < 0) {
            // Past slot: drop
            LOG_W(PHY, "[ORU south] missed UL slot %d.%d (now %d.%d), dropping job ant=%d\n",
                  j->frame, j->slot_in_frame, frame, slot, j->antenna_id);
            pending_ul[i].active = false;
            continue;
          }

          // Same slot: check symbol
          int expected_symbol = j->symbol + pending_ul[i].symbols_sent;
          if (expected_symbol < symbol) {
            LOG_W(PHY, "[ORU south] missed UL symbol %d (now %d), dropping job ant=%d\n",
                  expected_symbol, symbol, j->antenna_id);
            pending_ul[i].active = false;
          } else if (expected_symbol == symbol) {
            dispatch_ul_work(&work_queue, oru, frame, slot, symbol, j);
            if (++pending_ul[i].symbols_sent == j->num_symbols)
              pending_ul[i].active = false;
          }
          // expected_symbol > symbol: job spans multiple symbols, revisit next iteration
        }
      }
      int prach_symbol = get_prach_symbol(oru, frame, slot, symbol, ru->numerology);
      if (prach_symbol != -1)
        receive_prach(oru, frame, slot, symbol, prach_symbol);
    }
    slot++;
    if (slot == fp->slots_per_frame) {
      slot = 0;
      frame++;
      if (frame == 1024) {
        frame = 0;
      }
    }
  }

  pthread_mutex_lock(&work_queue.lock);
  work_queue.running = false;
  pthread_cond_broadcast(&work_queue.work_available);
  pthread_mutex_unlock(&work_queue.lock);
  for (int i = 0; i < num_workers; i++)
    pthread_join(workers[i], NULL);
  pthread_cond_destroy(&work_queue.work_available);
  pthread_cond_destroy(&work_queue.space_available);
  pthread_mutex_destroy(&work_queue.lock);

  return NULL;
}
