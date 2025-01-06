/**
 ****************************************************************************************
 *
 * @file rwnx_msg_tx.h
 *
 * @brief TX function declarations
 *
 * Copyright (C) RivieraWaves 2012-2021
 *
 ****************************************************************************************
 */

#ifndef _RWNX_MSG_TX_H_
#define _RWNX_MSG_TX_H_

#include "rwnx_defs.h"

/* Default MAC Rx filters that cannot be changed by mac80211 */
#define RWNX_MAC80211_NOT_CHANGEABLE                                                                                   \
	(NXMAC_ACCEPT_QO_S_NULL_BIT | NXMAC_ACCEPT_Q_DATA_BIT | NXMAC_ACCEPT_DATA_BIT |                                \
	 NXMAC_ACCEPT_OTHER_MGMT_FRAMES_BIT | NXMAC_ACCEPT_MY_UNICAST_BIT | NXMAC_ACCEPT_BROADCAST_BIT |               \
	 NXMAC_ACCEPT_BEACON_BIT | NXMAC_ACCEPT_PROBE_RESP_BIT)

#define MM_RX_FILTER_ACTIVE (NXMAC_ACCEPT_MULTICAST_BIT | NXMAC_ACCEPT_BROADCAST_BIT | \
	NXMAC_ACCEPT_MY_UNICAST_BIT | NXMAC_ACCEPT_PROBE_REQ_BIT | \
	NXMAC_ACCEPT_BEACON_BIT | NXMAC_ACCEPT_OTHER_MGMT_FRAMES_BIT | \
	NXMAC_ACCEPT_BAR_BIT | NXMAC_ACCEPT_BA_BIT | \
	NXMAC_ACCEPT_DATA_BIT | NXMAC_ACCEPT_Q_DATA_BIT | \
	NXMAC_ACCEPT_QO_S_NULL_BIT | NXMAC_ACCEPT_OTHER_DATA_FRAMES_BIT)

int rwnx_send_reset(struct rwnx_hw *rwnx_hw);
int rwnx_send_start(struct rwnx_hw *rwnx_hw);
int rwnx_send_version_req(struct rwnx_hw *rwnx_hw, struct mm_version_cfm *cfm);
int rwnx_send_add_if(struct rwnx_hw *rwnx_hw, const unsigned char *mac, enum nl80211_iftype iftype, bool p2p,
		     struct mm_add_if_cfm *cfm);
int rwnx_send_remove_if(struct rwnx_hw *rwnx_hw, u8 vif_index);
int rwnx_send_set_channel(struct rwnx_hw *rwnx_hw, int phy_idx, struct mm_set_channel_cfm *cfm);
int rwnx_send_key_add(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 sta_idx, bool pairwise, u8 *key, u8 key_len, u8 key_idx,
		      u8 cipher_suite, struct mm_key_add_cfm *cfm);
int rwnx_send_key_del(struct rwnx_hw *rwnx_hw, uint8_t hw_key_idx);
int rwnx_send_bcn_change(struct rwnx_hw *rwnx_hw, u8 vif_idx, struct rwnx_ipc_buf *buf, u16 tim_oft, u16 tim_len,
			 u16 *csa_oft);
int rwnx_send_tim_update(struct rwnx_hw *rwnx_hw, u8 vif_idx, u16 aid, u8 tx_status);
int rwnx_send_roc(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, struct ieee80211_channel *chan,
	   unsigned int duration, struct mm_remain_on_channel_cfm *cfm);
int rwnx_send_cancel_roc(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif);
int rwnx_send_set_power(struct rwnx_hw *rwnx_hw, u8 vif_idx, s8 pwr, struct mm_set_power_cfm *cfm);
int rwnx_send_set_edca(struct rwnx_hw *rwnx_hw, u8 hw_queue, u32 param, bool uapsd, u8 inst_nbr);
int rwnx_send_tdls_chan_switch_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, struct rwnx_sta *rwnx_sta,
				   bool sta_initiator, u8 oper_class, struct cfg80211_chan_def *chandef,
				   struct tdls_chan_switch_cfm *cfm);
int rwnx_send_tdls_cancel_chan_switch_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, struct rwnx_sta *rwnx_sta,
					  struct tdls_cancel_chan_switch_cfm *cfm);

int rwnx_send_country_code_cfg(struct rwnx_hw *rwnx_hw);
int rwnx_send_get_delay_stats_info_req(struct rwnx_hw *rwnx_hw, struct mcu_get_delay_stats_info_cfm *cfm);

/*
 * Parse the ampdu density to retrieve the value in usec, according to the
 * values defined in ieee80211.h
 */
static inline u8 rwnx_ampdudensity2usec(u8 ampdudensity)
{
	switch (ampdudensity) {
	case IEEE80211_HT_MPDU_DENSITY_NONE:
		return 0;
		/* 1 microsecond is our granularity */
	case IEEE80211_HT_MPDU_DENSITY_0_25:
	case IEEE80211_HT_MPDU_DENSITY_0_5:
	case IEEE80211_HT_MPDU_DENSITY_1:
		return 1;
	case IEEE80211_HT_MPDU_DENSITY_2:
		return 2;
	case IEEE80211_HT_MPDU_DENSITY_4:
		return 4;
	case IEEE80211_HT_MPDU_DENSITY_8:
		return 8;
	case IEEE80211_HT_MPDU_DENSITY_16:
		return 16;
	default:
		return 0;
	}
}

static inline bool use_pairwise_key(struct cfg80211_crypto_settings *crypto)
{
	if ((crypto->cipher_group == WLAN_CIPHER_SUITE_WEP40) || (crypto->cipher_group == WLAN_CIPHER_SUITE_WEP104))
		return false;

	return true;
}

static inline bool is_non_blocking_msg(int id)
{
	return ((id == MM_TIM_UPDATE_REQ) || (id == ME_RC_SET_RATE_REQ) || (id == MM_BFMER_ENABLE_REQ) ||
		(id == ME_TRAFFIC_IND_REQ) || (id == TDLS_PEER_TRAFFIC_IND_REQ) || (id == MESH_PATH_CREATE_REQ) ||
		(id == MESH_PROXY_ADD_REQ) || (id == SM_EXTERNAL_AUTH_REQUIRED_RSP));
}

static inline u8_l get_chan_flags(uint32_t flags)
{
	u8_l chan_flags = 0;
	if (flags & IEEE80211_CHAN_NO_IR)
		chan_flags |= CHAN_NO_IR;
	if (flags & IEEE80211_CHAN_RADAR)
		chan_flags |= CHAN_RADAR;
	return chan_flags;
}

static inline s8_l chan_to_fw_pwr(int power)
{
	return power > 127 ? 127 : (s8_l)power;
}

#ifdef CONFIG_RWNX_P2P_DEBUGFS
int rwnx_send_p2p_oppps_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, u8 ctw,
			    struct mm_set_p2p_oppps_cfm *cfm);
int rwnx_send_p2p_noa_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, int count, int interval, int duration,
			  bool dyn_noa, struct mm_set_p2p_noa_cfm *cfm);
#endif /* CONFIG_RWNX_P2P_DEBUGFS */

#ifdef CONFIG_RWNX_SOFTMAC
int rwnx_send_sta_add(struct rwnx_hw *rwnx_hw, struct ieee80211_sta *sta, u8 inst_nbr, struct mm_sta_add_cfm *cfm);
int rwnx_send_sta_del(struct rwnx_hw *rwnx_hw, u8 sta_idx);
int rwnx_send_add_chanctx(struct rwnx_hw *rwnx_hw, struct ieee80211_chanctx_conf *ctx,
			  struct mm_chan_ctxt_add_cfm *cfm);
int rwnx_send_del_chanctx(struct rwnx_hw *rwnx_hw, u8 index);
int rwnx_send_link_chanctx(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 chan_idx, u8 chan_switch);
int rwnx_send_unlink_chanctx(struct rwnx_hw *rwnx_hw, u8 vif_idx);
int rwnx_send_update_chanctx(struct rwnx_hw *rwnx_hw, struct ieee80211_chanctx_conf *ctx);
int rwnx_send_sched_chanctx(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 chan_idx, u8 type);

int rwnx_send_dtim_req(struct rwnx_hw *rwnx_hw, u8 dtim_period);
int rwnx_send_set_br(struct rwnx_hw *rwnx_hw, u32 basic_rates, u8 vif_idx, u8 band);
int rwnx_send_set_beacon_int(struct rwnx_hw *rwnx_hw, u16 beacon_int, u8 vif_idx);
int rwnx_send_set_bssid(struct rwnx_hw *rwnx_hw, const u8 *bssid, u8 vif_idx);
int rwnx_send_set_vif_state(struct rwnx_hw *rwnx_hw, bool active, u16 aid, u8 vif_idx);
int rwnx_send_set_mode(struct rwnx_hw *rwnx_hw, u8 abgmode);
int rwnx_send_set_idle(struct rwnx_hw *rwnx_hw, int idle);
int rwnx_send_set_ps_mode(struct rwnx_hw *rwnx_hw, u8 ps_mode);
int rwnx_send_set_ps_options(struct rwnx_hw *rwnx_hw, bool listen_bcmc, u16 listen_interval, u8 vif_idx);
int rwnx_send_set_slottime(struct rwnx_hw *rwnx_hw, int use_short_slot);
int rwnx_send_ba_add(struct rwnx_hw *rwnx_hw, uint8_t type, uint8_t sta_idx, u16 tid, uint8_t bufsz, uint16_t ssn,
		     struct mm_ba_add_cfm *cfm);
int rwnx_send_ba_del(struct rwnx_hw *rwnx_hw, uint8_t sta_idx, u16 tid, struct mm_ba_del_cfm *cfm);
int rwnx_send_scan_req(struct rwnx_hw *rwnx_hw, struct ieee80211_vif *vif, struct cfg80211_scan_request *param,
		       struct scan_start_cfm *cfm);
int rwnx_send_scan_cancel_req(struct rwnx_hw *rwnx_hw, struct scan_cancel_cfm *cfm);
void rwnx_send_tdls_ps(struct rwnx_hw *rwnx_hw, bool ps_mode);
#endif /* CONFIG_RWNX_SOFTMAC */

#ifdef CONFIG_RWNX_FULLMAC
int rwnx_send_me_config_req(struct rwnx_hw *rwnx_hw);
int rwnx_send_me_chan_config_req(struct rwnx_hw *rwnx_hw);
int rwnx_send_me_set_control_port_req(struct rwnx_hw *rwnx_hw, bool opened, u8 sta_idx);
int rwnx_send_me_sta_add(struct rwnx_hw *rwnx_hw, struct station_parameters *params, const u8 *mac, u8 inst_nbr,
			 struct me_sta_add_cfm *cfm);
int rwnx_send_me_sta_del(struct rwnx_hw *rwnx_hw, u8 sta_idx, bool tdls_sta);
int rwnx_send_me_traffic_ind(struct rwnx_hw *rwnx_hw, u8 sta_idx, bool uapsd, u8 tx_status);
int rwnx_send_twt_request(struct rwnx_hw *rwnx_hw, u8 setup_type, u8 vif_idx, u8 flow_id, struct twt_conf_tag *conf,
			  struct twt_setup_cfm *cfm);
int rwnx_send_twt_teardown(struct rwnx_hw *rwnx_hw, struct twt_teardown_req *twt_teardown,
			   struct twt_teardown_cfm *cfm);
int rwnx_send_me_rc_stats(struct rwnx_hw *rwnx_hw, u8 sta_idx, struct me_rc_stats_cfm *cfm);
int rwnx_send_me_rc_set_rate(struct rwnx_hw *rwnx_hw, u8 sta_idx, u16 rate_idx);
int rwnx_send_me_set_ps_mode(struct rwnx_hw *rwnx_hw, u8 ps_mode);
int rwnx_send_11b_only_mode(struct rwnx_hw *rwnx_hw, u8 is_11b_only);
#ifdef DUMP_FW_LOG_ENABLE
int bes_send_dump_fw_log(struct rwnx_hw *rwnx_hw, u8 type);
int bes_send_get_fw_log_type_req(struct rwnx_hw *rwnx_hw, u8 *cfm);
#endif
void rwnx_connect_retry_work_func(struct work_struct *work);
int rwnx_send_sm_connect_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, struct cfg80211_connect_params *sme,
			     struct sm_connect_cfm *cfm);
int rwnx_send_sm_disconnect_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, u16 reason);
int rwnx_send_sm_external_auth_required_rsp(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, u16 status);
int rwnx_send_sm_ft_auth_rsp(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, uint8_t *ie, int ie_len);
int rwnx_send_apm_start_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, struct cfg80211_ap_settings *settings,
			    struct apm_start_cfm *cfm);
int rwnx_send_apm_stop_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif);
int rwnx_send_apm_probe_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, struct rwnx_sta *sta,
			    struct apm_probe_client_cfm *cfm);
int rwnx_send_scanu_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, struct cfg80211_scan_request *param);
int rwnx_send_apm_start_cac_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, struct cfg80211_chan_def *chandef,
				struct apm_start_cac_cfm *cfm);
int rwnx_send_apm_stop_cac_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif);
int rwnx_send_tdls_peer_traffic_ind_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif);
int rwnx_send_config_monitor_req(struct rwnx_hw *rwnx_hw, struct cfg80211_chan_def *chandef,
				 struct me_config_monitor_cfm *cfm);
int rwnx_send_mesh_start_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, const struct mesh_config *conf,
			     const struct mesh_setup *setup, struct mesh_start_cfm *cfm);
int rwnx_send_mesh_stop_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, struct mesh_stop_cfm *cfm);
int rwnx_send_mesh_update_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, u32 mask,
			      const struct mesh_config *p_mconf, struct mesh_update_cfm *cfm);
int rwnx_send_mesh_peer_info_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, u8 sta_idx,
				 struct mesh_peer_info_cfm *cfm);
void rwnx_send_mesh_peer_update_ntf(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, u8 sta_idx, u8 mlink_state);
void rwnx_send_mesh_path_create_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, u8 *tgt_addr);
int rwnx_send_mesh_path_update_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, const u8 *tgt_addr,
				   const u8 *p_nhop_addr, struct mesh_path_update_cfm *cfm);
void rwnx_send_mesh_proxy_add_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, u8 *ext_addr);
#endif /* CONFIG_RWNX_FULLMAC */

#ifdef CONFIG_RWNX_BFMER
#ifdef CONFIG_RWNX_SOFTMAC
void rwnx_send_bfmer_enable(struct rwnx_hw *rwnx_hw, struct ieee80211_sta *sta);
#else
void rwnx_send_bfmer_enable(struct rwnx_hw *rwnx_hw, struct rwnx_sta *rwnx_sta,
			    const struct ieee80211_vht_cap *vht_cap);
#endif /* CONFIG_RWNX_SOFTMAC*/
#ifdef CONFIG_RWNX_MUMIMO_TX
int rwnx_send_mu_group_update_req(struct rwnx_hw *rwnx_hw, struct rwnx_sta *rwnx_sta);
#endif /* CONFIG_RWNX_MUMIMO_TX */
#endif /* CONFIG_RWNX_BFMER */

/* Debug messages */
int rwnx_send_dbg_trigger_req(struct rwnx_hw *rwnx_hw, char *msg);
int rwnx_send_dbg_mem_read_req(struct rwnx_hw *rwnx_hw, u32 mem_addr, struct dbg_mem_read_cfm *cfm);
int rwnx_send_dbg_mem_write_req(struct rwnx_hw *rwnx_hw, u32 mem_addr, u32 mem_data);
int rwnx_send_dbg_set_mod_filter_req(struct rwnx_hw *rwnx_hw, u32 filter);
int rwnx_send_dbg_set_sev_filter_req(struct rwnx_hw *rwnx_hw, u32 filter);
int rwnx_send_dbg_get_sys_stat_req(struct rwnx_hw *rwnx_hw, struct dbg_get_sys_stat_cfm *cfm);
int rwnx_send_cfg_rssi_req(struct rwnx_hw *rwnx_hw, u8 vif_index, int rssi_thold, u32 rssi_hyst);

int bes_send_drv2mcu_msg(struct rwnx_hw *rwnx_hw, u8 *msg, u32 buf_len, u32 req_id, u32 cfm_id, void *cfm, u16 len);
int bes_send_wf_cali_req(struct rwnx_hw *rwnx_hw, struct mcu_wf_cali_req *cali_req, struct mcu_wf_cali_cfm *cfm);
#ifdef CONFIG_BES_POWER_CTRL
int bes_send_wifi_on_off_req(struct rwnx_hw *rwnx_hw, bool on);
#endif
int bes_send_wake_ctrl_req(struct rwnx_hw *rwnx_hw, bool wakeup);
int bes_send_recover_wifi_dpd_data(struct rwnx_hw *rwnx_hw, void *recover_dpd, u8 *data);
int bes_send_mcu_buf_cnt_req(struct rwnx_hw *rwnx_hw, struct mcu_buf_cnt_cfm *cfm);
int bes_set_umac_filter(struct rwnx_hw *rwnx_hw, u32 umac_filter);
int rwnx_reconnect_req(struct rwnx_vif *rwnx_vif, struct sm_connect_ind *ind);
int bes_send_dbac_invalid_cali_data(struct rwnx_hw *rwnx_hw, u8 invalid_ch);
int bes_send_dbac_temp_compensation_req(struct rwnx_hw *rwnx_hw, u8 ch_idx);
int rwnx_send_scan_cancel_req(struct rwnx_hw *rwnx_hw, struct scan_cancel_cfm *cfm);
int rwnx_send_packet_filter_request(struct rwnx_hw *rwnx_hw, struct mm_set_packet_filter_req *req, void *cfm, u16 cfm_len);
int rwnx_send_custom_request(struct rwnx_hw *rwnx_hw, struct mm_custom_req_hdr *req, void *cfm, u16 len);
int rwnx_send_vif_info_ext_get_req(struct rwnx_vif *vif, int offset, size_t value_length, void *value);
int rwnx_send_vif_info_ext_set_req(struct rwnx_vif *vif, int offset, size_t value_length, void *value);
int rwnx_send_arp_keepalive_request(struct rwnx_hw *rwnx_hw, u16 period,
				   u8 vif_idx, u32 sender_ip, u32 target_ip);
int bes_send_bt_status_get(struct rwnx_hw *rwnx_hw, u32 *status);
int bes_send_temprtr_get(struct rwnx_hw *rwnx_hw, int32_t *value);
int rwnx_send_pta_cfg(struct rwnx_hw *rwnx_hw, struct bes_coex_cmd *cmd);
int bes_send_chan_noa_update(struct rwnx_vif *vif);
int bes_send_ignore_bcn_edca_req(struct rwnx_hw *rwnx_hw, bool ignore);
int bes_send_set_tx_max_pwr_req(struct rwnx_hw *rwnx_hw, struct wifi_tx_max_pwr *max_pwr,
				struct wifi_pwr_range *pwr_range);
int rwnx_send_csi_cap_req(struct rwnx_hw *rwnx_hw, struct mm_csi_cap_req *req);
int rwnx_send_system_state_req(struct rwnx_hw *rwnx_hw);

int rwnx_send_cca_enable(struct rwnx_hw *rwnx_hw, uint8_t enable);
int rwnx_send_cca_config(struct rwnx_hw *rwnx_hw, struct adaptive_cca_config* cca_cfg);
#endif /* _RWNX_MSG_TX_H_ */
