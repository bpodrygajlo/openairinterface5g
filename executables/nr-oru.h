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
#ifndef __NR_ORU_H__
#define __NR_ORU_H__
#include "openair1/PHY/defs_RU.h"
#include <pthread.h>
#include "oru_fh.h"
#include "openair2/LAYER2/NR_MAC_COMMON/nr_prach_config.h"
#include "openair1/PHY/defs_gNB.h"

typedef struct {
  RU_t *ru;
  /// tx carrier
  uint64_t carrier_freq_tx[MAX_BANDS_PER_RRU];
  /// rx carrier
  uint64_t carrier_freq_rx[MAX_BANDS_PER_RRU];
  /// tx BW in PRBs
  int bw_tx[MAX_BANDS_PER_RRU];
  /// rx BW in PRBs
  int bw_rx[MAX_BANDS_PER_RRU];
  /// 3GPP FRAME Type FDD/TDD
  int frame_type;
  /// 3GPP PRACH configuration index
  int prach_config_index;
  /// 3GPP MSG1 Start frequency
  int prach_msg1_freq;
  /// 3GPP TDD periodicity (0.5 ms, 1 0.625ms, 2 1ms, 3 1.25ms, 4 2ms,5 2.5ms, 6 5ms, 7 10ms, 8 3ms, 9 4ms
  int tdd_period;
  /// number of DL slots
  int num_DL_slots;
  /// number of UL slots
  int num_UL_slots;
  /// number of DL symbols
  int num_DL_symbols;
  /// number of UL symbols
  int num_UL_symbols;
  int numerology;

  pthread_t north_read_thread;
  pthread_t south_read_thread;
  oru_fh_config_t fh_config;
  void *fronthaul;

  // PRACH related
  nr_prach_info_t prach_info;
  time_stats_t rx_prach;
  time_stats_t rx;
  prach_item_t prach_item;
} ORU_t;

int get_oru_options(ORU_t *oru);
void oru_init_frame_parms(ORU_t *oru);
void *oru_north_read_thread(void *arg);
void *oru_south_read_thread(void *arg);
void prepare_prach_item(ORU_t *oru);

#endif
