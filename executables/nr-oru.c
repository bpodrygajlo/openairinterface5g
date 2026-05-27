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
#include "nr-oru.h"
#include "openair1/PHY/defs_nr_common.h"
#include <time.h>
#include "openair1/PHY/MODULATION/nr_modulation.h"
#include "openair1/SCHED_NR/sched_nr.h"
#include "openair1/PHY/MODULATION/modulation_common.h"

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
void tx_rf_symbols(RU_t *ru, int frame, int slot, uint64_t timestamp, int start_symbol, int num_symbols);

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

static void dl_symbol_process(RU_t *ru, int frame, int slot, int symbol, c16_t **txDataF, int64_t timestamp)
{
  NR_DL_FRAME_PARMS *fp = ru->nr_frame_parms;
  uint32_t slot_offset = get_samples_slot_timestamp(fp, slot);
  uint32_t symbol_offset = get_samples_symbol_timestamp(fp, slot, symbol);

  __attribute__((aligned(64))) c16_t txdataF_shifted[fp->ofdm_symbol_size];
  memset(txdataF_shifted, 0, sizeof(txdataF_shifted));
  c16_t *rotation = fp->symbol_rotation[0] + (slot % fp->slots_per_subframe) * fp->symbols_per_slot + symbol;
  for (int aatx = 0; aatx < ru->nb_tx; aatx++) {
    // Phase compensation
    rotate_cpx_vector(txDataF[aatx], rotation, txDataF[aatx], fp->N_RB_DL * NR_NB_SC_PER_RB, 15);
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
  tx_rf_symbols(ru, frame, slot, timestamp, symbol, 1);
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
  struct timespec utc_anchor_point;
  oru_fh_get_utc_anchor_point(oru->fronthaul, &start_hyper_frame, &start_frame, &start_slot, &utc_anchor_point);
  AssertFatal(ru->rfdevice.get_timestamp != NULL, "rfdevice has no capability to translate UTC timestamp to sample index\n");
  int64_t start_timestamp = ru->rfdevice.get_timestamp(&ru->rfdevice, &utc_anchor_point);
  // subtract the start_frame and start_slot from the timestamp simplify calculation below.
  start_timestamp -= (start_frame * fp->samples_per_frame + get_samples_slot_timestamp(fp, start_slot));
  // Now start_timestamp points to the start sample of the frame 0 slot 0 symbol 0 of hyperframe 0
  LOG_A(PHY, "DL thread started: start_timestamp %ld, start_frame %d, start_slot %d\n", start_timestamp, start_frame, start_slot);

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
    dl_symbol_process(ru, frame, slot, symbol, txDataF_ptr, timestamp);
    if (frame % 256 == 0 && slot == 0 && symbol == 0) {
      LOG_I(PHY, "[RU_thread] read data: frame %d, slot %d, symbol %d\n", frame, slot, symbol);
    }
  }
  return NULL;
}

void *oru_south_read_thread(void *arg)
{
  ORU_t *oru = arg;
  RU_t *ru = oru->ru;

  const int num_samples = 3000;
  c16_t throwaway_samples[ru->nb_rx][num_samples];
  void *rxp[ru->nb_rx];
  for (int i = 0; i < ru->nb_rx; i++)
    rxp[i] = throwaway_samples[i];

  openair0_timestamp_t timestamp;
  while (!oai_exit) {
    ru->rfdevice.trx_read_func(&ru->rfdevice, &timestamp, rxp, num_samples, ru->nb_rx);
  }

  // Perform RX processing
  return NULL;
}
