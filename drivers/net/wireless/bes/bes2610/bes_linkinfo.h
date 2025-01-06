/*
 * @file bes_linkinfo.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BES_LINKINFO_H_
#define _BES_LINKINFO_H_

struct mm_custom_req_hdr;

/* Link info ops type */
enum linkinfo_ops {
	/* Get the information of the transmission rate */
	LINKINFO_OPS_TX_RATE_GET = 0,
	/* Get the signal strength of the beacon frame */
	LINKINFO_OPS_BEACON_RSSI_GET,
	/* Flush buffered data per station */
	LINKINFO_OPS_FLUSH_DATA,
	/* Get tx delay statistics */
	LINKINFO_OPS_TX_DELAY_GET,
};

struct linkinfo_get_rssi_req {
	struct mm_custom_req_hdr hdr;
	uint8_t sta_index;
};

struct linkinfo_get_rssi_cfm {
	int8_t beacon_rssi;
};

struct linkinfo_flush_data_req {
	struct mm_custom_req_hdr hdr;
	uint8_t sta_index;
};

struct linkinfo_get_tx_rate_req {
	struct mm_custom_req_hdr hdr;
	uint8_t sta_index;
};

struct linkinfo_get_tx_rate_cfm {
	struct tx_rate_vector tx_rate;
};

struct lstats_time {
	u32 hour;
	u8 min;
	u8 sec;
};

struct linkinfo_get_tx_delay_req {
	struct mm_custom_req_hdr hdr;
	u8 sta_index;
};

struct linkinfo_get_tx_delay_cfm {
	u32 delay;
};

int bes_get_beacon_rssi(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta);
int bes_link_flush_data(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta);
int bes_get_tx_rate(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta,
		    struct tx_rate_vector *tx_rate_vector);
int bes_get_tx_delay_stat(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta, u32 *delay);
void bes_tx_uldelay_update(struct rwnx_sw_txhdr *sw_txhdr);
u32 bes_tx_uldelay_get(struct rwnx_sta *rwnx_sta);

/* Initializesta tx and rx rate statistics */
int bes_sta_txrx_rate_stat_init(struct rwnx_hw *rwnx_hw, struct rwnx_sta *rwnx_sta);
void bes_sta_txrx_rate_stat_deinit(struct rwnx_hw *rwnx_hw, struct rwnx_sta *rwnx_sta);

/* reset tx or rx rate statistics */
void bes_sta_rx_rate_stat_reset(struct rwnx_sta *sta);
void bes_sta_tx_rate_stat_reset(struct rwnx_sta *sta);

/* tx and rx rate sort by attempts or mpdu */
void txrx_rate_sort(int *rate, int num, int cmp_idx, void *rate_stats, bool tx);
bool txrx_rate_idx_is_most_used(int *rate, int num, int cmp_idx);

/* convert the index to rc config */
void idx_to_rate_cfg(int idx, union rwnx_rate_ctrl_info *r_cfg, int *ru_size);
/* convert the tx rate index to rc config index */
int tx_rate_table_idx_to_cfg_idx(struct rwnx_tx_rate_stats *rate_stats, int idx);
/* convert rc config index to the tx rate index */
int cfg_idx_to_tx_rate_table_idx(struct rwnx_tx_rate_stats *rate_stats, int idx);

/* update tx rate statistics */
void bes_tx_update_peer_info(struct rwnx_hw *rwnx_hw, struct rwnx_sta *rwnx_sta, struct me_rc_stats_cfm *rc_stats);

/* vif link status statistics */
void bes_lstats_vif_start(struct rwnx_vif *rwnx_vif);
void bes_lstats_vif_stop(struct rwnx_vif *rwnx_vif);
void bes_lstats_beacon_statistic(struct rwnx_vif *rwnx_vif, struct sk_buff *skb);
void bes_lstats_ac_rx_statistic(struct rwnx_vif *rwnx_vif, struct hw_rxhdr *hw_rxhdr);
void bes_lstats_ac_tx_statistic(struct rwnx_vif *rwnx_vif, struct rwnx_txq *txq, union rwnx_hw_txstatus status);
void bes_lstats_rssi_mgmt_update(struct rwnx_vif *rwnx_vif, struct ieee80211_mgmt *mgmt, int rssi);
struct lstats_time bes_lstats_ktime_to_tm(ktime_t kt);
void bes_get_vif_time_stats(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif);

/* phy link status statistics */
void bes_lstats_phy_start(struct rwnx_hw *rwnx_hw);
void bes_lstats_phy_stop(struct rwnx_hw *rwnx_hw);

/* vif link status statistics init/deinit */
void bes_lstats_init(struct rwnx_hw *rwnx_hw);
void bes_lstats_deinit(struct rwnx_hw *rwnx_hw);

#ifdef CONFIG_VENDOR_CMD
/* vendor hal get link stats API */
void bes_get_sta_txrx_rate_nums(struct rwnx_vif *rwnx_vif, struct bes_peer_rate_num *num_stats);
void bes_link_layer_get_peer_info(struct rwnx_vif *rwnx_vif, struct wifi_iface_stat *iface_stat,
				  struct bes_peer_rate_num *num_stats);
void bes_link_layer_get_iface_stats(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
				    struct wifi_iface_stat *iface_stat);
void bes_link_layer_get_radio_stats(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
				    struct wifi_radio_stat *radio_stat);
#endif

#endif /* _BES_LINKINFO_H_ */
