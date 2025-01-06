/*
 * @file bes_dbac_cali.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __BES_DBAC_CALI_H__
#define __BES_DBAC_CALI_H__

#include "rwnx_defs.h"

#ifdef CONFIG_WIFI_CALI
bool bes_check_dpd_cali_data(struct rwnx_hw *rwnx_hw);

void bes_dbac_temp_compensation_start(struct rwnx_hw *rwnx_hw);
void bes_dbac_cali_check(struct rwnx_hw *rwnx_hw);
void bes_dbac_disconn_while_csa(struct rwnx_vif *rwnx_vif);
void bes_dbac_scan_sta_csa(struct rwnx_hw *rwnx_hw, const struct scanu_result_ind *ind);
void bes_dbac_sta_csa_finish_ind(struct rwnx_vif *rwnx_vif, const struct mm_csa_finish_ind *ind);
void bes_dbac_sta_csa_init(struct rwnx_vif *rwnx_vif);
void bes_dbac_cali_after_csa(struct rwnx_vif *rwnx_vif);
void bes_dbac_rx_sta_csa(struct rwnx_vif *rwnx_vif, const struct sk_buff *skb, struct hw_rxhdr *hw_rxhdr);
void bes_free_dpd_buffer(struct rwnx_plat *rwnx_plat);
void bes_dbac_dyna_cali_check(struct rwnx_hw *rwnx_hw);
void bes_dbac_defer_cali_check(struct rwnx_hw *rwnx_hw);

int bes_dbac_cali_connect(struct rwnx_vif *rwnx_vif, struct cfg80211_connect_params *sme);
int bes_dbac_cali_start_ap(struct rwnx_vif *rwnx_vif, struct cfg80211_ap_settings *settings);
int bes_dbac_cali_init(struct rwnx_hw *rwnx_hw);
int bes_send_dpd_cail_data(struct rwnx_hw *rwnx_hw);
int bes_get_dpd_cail_data(struct rwnx_hw *rwnx_hw);
int bes_dbac_pre_cali(struct rwnx_vif *rwnx_vif, const struct cfg80211_chan_def *chandef);
int bes_dbac_pre_cali_invalid(struct rwnx_hw *rwnx_hw, u8 idx);
#else
static inline bool bes_check_dpd_cali_data(struct rwnx_hw *rwnx_hw) { return false; }
static inline void bes_dbac_temp_compensation_start(struct rwnx_hw *rwnx_hw) {}
static inline void bes_dbac_cali_check(struct rwnx_hw *rwnx_hw) {}
static inline void bes_dbac_disconn_while_csa(struct rwnx_vif *rwnx_vif) {}
static inline void bes_dbac_scan_sta_csa(struct rwnx_hw *rwnx_hw, const struct scanu_result_ind *ind) {}
static inline void bes_dbac_sta_csa_finish_ind(struct rwnx_vif *rwnx_vif, const struct mm_csa_finish_ind *ind) {}
static inline void bes_dbac_sta_csa_init(struct rwnx_vif *rwnx_vif) {}
static inline void bes_dbac_cali_after_csa(struct rwnx_vif *rwnx_vif) {}
static inline void bes_dbac_rx_sta_csa(struct rwnx_vif *rwnx_vif, const struct sk_buff *skb, struct hw_rxhdr *hw_rxhdr) {}
static inline void bes_free_dpd_buffer(struct rwnx_plat *rwnx_plat) {}
static inline void bes_dbac_dyna_cali_check(struct rwnx_hw *rwnx_hw) {}
static inline void bes_dbac_defer_cali_check(struct rwnx_hw *rwnx_hw) {}

static inline int bes_dbac_cali_start_ap(struct rwnx_vif *rwnx_vif, struct cfg80211_ap_settings *settings) { return 0; }
static inline int bes_dbac_cali_connect(struct rwnx_vif *rwnx_vif, struct cfg80211_connect_params *sme) { return 0; }
static inline int bes_dbac_cali_init(struct rwnx_hw *rwnx_hw) { return 0; }
static inline int bes_send_dpd_cail_data(struct rwnx_hw *rwnx_hw) { return 0; }
static inline int bes_get_dpd_cail_data(struct rwnx_hw *rwnx_hw) { return 0; }
static inline int bes_dbac_pre_cali(struct rwnx_vif *rwnx_vif, const struct cfg80211_chan_def *chandef) { return 0; }
static inline int bes_dbac_pre_cali_invalid(struct rwnx_hw *rwnx_hw, u8 idx) { return 0; }
#endif

#endif /* __BES_DBAC_CALI_H__ */
