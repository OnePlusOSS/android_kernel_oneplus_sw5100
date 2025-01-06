/*
 * @file bes_sleep_manager.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/inetdevice.h>
#include "rwnx_defs.h"
#include "bes_pwr.h"
#include "bes_log.h"
#include "reg_access.h"
#include "rwnx_msg_tx.h"
#include "rwnx_testmode.h"
#include "bes_sleep_manager.h"
#include "bes_filter.h"

static bool lp_dyn_cb_rgt = false;

int bes_sleep_manager_set_bcn_lost_thres(struct rwnx_hw *rwnx_hw, u8 thres)
{
	struct lp_bcn_lost_thres_set_req req;

	// Set hdr
	req.hdr.id = CUSTOM_ID_LP_SLEEP_CONFIG;
	req.hdr.length = sizeof(struct lp_bcn_lost_thres_set_req);
	req.hdr.ops = LP_SLEEP_OPS_BCN_LOST_THRES_SET;

	// Set param
	req.thres = thres;

	// Send custom request to device
	return rwnx_send_custom_request(rwnx_hw, &req.hdr, NULL, 0);
}

static int bes_sleep_manager_set_null_keep_alive(struct rwnx_hw *rwnx_hw, u8 vif_index, u16 period)
{
	struct lp_null_keepalive_set_req req;

	// Set hdr
	req.hdr.id = CUSTOM_ID_LP_SLEEP_CONFIG;
	req.hdr.length = sizeof(struct lp_null_keepalive_set_req);
	req.hdr.ops = LP_SLEEP_OPS_NULL_KEEPALIVE_SET;

	// Set param
	req.vif_idx = vif_index;
	req.period = period;

	// Send custom request to device
	return rwnx_send_custom_request(rwnx_hw, &req.hdr, NULL, 0);
}

static bool bes_sleep_manager_is_start_twt(struct rwnx_hw *rwnx_hw)
{
	int i = 0;
	struct rwnx_sta *sta = NULL;
	struct rwnx_vif *rwnx_vif = NULL;

	if (!rwnx_hw->mod_params->twt)
		return false;

	for (i = 0; i < NX_REMOTE_STA_MAX; i++) {
		/* sta should be valid */
		sta = rwnx_rx_get_sta(rwnx_hw, i);
		if (!sta || !sta->valid)
			continue;

		/* vif is sta */
		rwnx_vif = rwnx_rx_get_vif(rwnx_hw, sta->vif_idx);
		if (!rwnx_vif)
			continue;

		if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_STATION)
			continue;

		/* twt should start */
		if (sta->twt_ind.sta_idx == RWNX_INVALID_STA)
			continue;

		return true;
	}

	return false;
}

static void bes_sleep_manager_ps_mode_handler(void *arg, u8 status)
{
	struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)arg;

	if (status == PWR_LP_CB_STATUS_BEFORE_WIFI_SLEEP || status == PWR_LP_CB_STATUS_AFTER_WIFI_WAKEUP) {
		if (bes_sleep_manager_is_start_twt(rwnx_hw))
			rwnx_hw->user_ps_mode = MM_PS_MODE_ON;
		else if (status == PWR_LP_CB_STATUS_BEFORE_WIFI_SLEEP)
			rwnx_hw->user_ps_mode = MM_PS_MODE_ON_DYN;
		else
			rwnx_hw->user_ps_mode = MM_PS_MODE_OFF;
	}

	if (rwnx_hw->user_ps_mode != rwnx_hw->cur_ps_mode) {
		rwnx_send_me_set_ps_mode(rwnx_hw, rwnx_hw->user_ps_mode);
		rwnx_hw->cur_ps_mode = rwnx_hw->user_ps_mode;
		bes_dbg(BES_DBG_PWR, "change pm setting, mode: %s\n",
				(rwnx_hw->user_ps_mode == MM_PS_MODE_OFF) ?
				("OFF") : ((rwnx_hw->user_ps_mode == MM_PS_MODE_ON) ? "ON" : "ON_DYN"));
	}
}

static void bes_sleep_manager_start_keepalive(struct rwnx_hw *rwnx_hw)
{
	int ret = 0;
	u8 i = 0;
	u32 vif_idx_map = 0;
	struct rwnx_vif *rwnx_vif = NULL;
	bool arp_keepalive = !!CONFIG_BES_ARP_KEEP_ALIVE;

	/* Get vif_idx_map value */
	vif_idx_map = bes_common_get_vif_idx_map(rwnx_hw);
	if (!vif_idx_map)
		return;

	for_each_set_bit(i, (const unsigned long *)&vif_idx_map, NX_ITF_MAX) {
		u8 vif_index;

		/* vif needs to be connected */
		rwnx_vif = rwnx_rx_get_vif(rwnx_hw, i);
		if (!rwnx_vif || !netif_carrier_ok(rwnx_vif->ndev))
			continue;

		/* Check the validity of vif_index */
		vif_index = rwnx_vif->vif_index;
		if (vif_index >= NX_ITF_MAX)
			continue;

		/* vif is sta or gc */
		if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_STATION &&
		    RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_P2P_CLIENT)
			continue;

		if (arp_keepalive && rwnx_vif->ipv4_addr != 0 && rwnx_vif->sta.gateway_ip != 0) {
			ret = rwnx_send_arp_keepalive_request(rwnx_hw, rwnx_mod_params.keepalive_period,
				vif_index, rwnx_vif->ipv4_addr,
				rwnx_vif->sta.gateway_ip);
			if(ret)
				bes_err(BES_DBG_PWR,
					"ERROR(%d): start arp keep alive(period:%d). vif_idx:%d. sta_ip: %pI4, gateway_ip: %pI4\n",
					ret, rwnx_mod_params.keepalive_period,
					vif_index, &rwnx_vif->ipv4_addr, &rwnx_vif->sta.gateway_ip);
			else
				bes_dbg(BES_DBG_PWR,
					"start arp keep alive(period:%d). vif_idx:%d. sta_ip: %pI4, gateway_ip: %pI4\n",
					rwnx_mod_params.keepalive_period,
					vif_index, &rwnx_vif->ipv4_addr, &rwnx_vif->sta.gateway_ip);
		} else {
			ret = bes_sleep_manager_set_null_keep_alive(rwnx_hw, vif_index, rwnx_mod_params.keepalive_period);
			if(ret)
				bes_err(BES_DBG_PWR, "ERROR(%d): start null keep alive(period:%d). vif_idx:%d\n",
					ret, rwnx_mod_params.keepalive_period, vif_index);
			else
				bes_dbg(BES_DBG_PWR, "start null keep alive(period:%d). vif_idx:%d\n",
					rwnx_mod_params.keepalive_period, vif_index);
		}
	}
}

static void bes_sleep_manager_stop_keepalive(struct rwnx_hw *rwnx_hw)
{
	int ret = 0;
	u8 i = 0;
	u32 vif_idx_map = 0;
	struct rwnx_vif *rwnx_vif = NULL;
	bool arp_keepalive = !!CONFIG_BES_ARP_KEEP_ALIVE;

	/* Get vif_idx_map value */
	vif_idx_map = bes_common_get_vif_idx_map(rwnx_hw);
	if (!vif_idx_map)
		return;

	for_each_set_bit(i, (const unsigned long *)&vif_idx_map, NX_ITF_MAX) {
		u8 vif_index;

		/* vif needs to be connected */
		rwnx_vif = rwnx_rx_get_vif(rwnx_hw, i);
		if (!rwnx_vif || !netif_carrier_ok(rwnx_vif->ndev))
			continue;

		/* vi is sta or gc */
		if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_STATION &&
		    RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_P2P_CLIENT)
			continue;

		/* Check the validity of vif_index */
		vif_index = rwnx_vif->vif_index;
		if (vif_index >= NX_ITF_MAX)
			continue;

		if (arp_keepalive && rwnx_vif->ipv4_addr != 0 && rwnx_vif->sta.gateway_ip != 0) {
			ret = rwnx_send_arp_keepalive_request(rwnx_hw, 0,
				vif_index, rwnx_vif->ipv4_addr,
				rwnx_vif->sta.gateway_ip);
			if(ret)
				bes_err(BES_DBG_PWR, "%s ERROR(%d): stop arp keep alive. vif_idx:%d",
					__func__, ret, vif_index);
			else
				bes_dbg(BES_DBG_PWR, "stop arp keep alive. vif_idx:%d",
					vif_index);
		} else {
			ret = bes_sleep_manager_set_null_keep_alive(rwnx_hw, vif_index, 0);
			if(ret)
				bes_err(BES_DBG_PWR, "%s ERROR(%d): stop null keep alive. vif_idx:%d",
					__func__, ret, vif_index);
			else
				bes_dbg(BES_DBG_PWR, "stop null keep alive. vif_idx:%d",
					vif_index);
		}
	}
}

static void bes_sleep_manager_set_keepalive_handler(void *arg, u8 status)
{
	struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)arg;

	if (status == PWR_LP_CB_STATUS_BEFORE_WIFI_SLEEP) {
		/* Start keep alive for sta before wifi sleep */
		bes_sleep_manager_start_keepalive(rwnx_hw);
	} else if (status == PWR_LP_CB_STATUS_AFTER_WIFI_WAKEUP) {
		/* Stop keep alive after wifi wakeup */
		bes_sleep_manager_stop_keepalive(rwnx_hw);
	} else if (status == PWR_LP_CB_STATUS_IO_SLEEP && !bes_pwr_get_vif_link_cnt(rwnx_hw)) {
		bes_pwr_unregister_lp_cb(rwnx_hw, bes_sleep_manager_set_keepalive_handler);
	}
}

static void *bes_sleep_manager_get_vif_for_beacon_filter(struct rwnx_hw *rwnx_hw)
{
	struct rwnx_vif *rwnx_vif = NULL, *vif = NULL;
	int count = 0;

	spin_lock_bh(&rwnx_hw->cb_lock);
	list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
		/* vif needs to be connected */
		if (!netif_running(rwnx_vif->ndev) ||
		    !netif_carrier_ok(rwnx_vif->ndev))
			continue;

		/* vif is sta or gc */
		if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_STATION &&
		    RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_P2P_CLIENT)
			continue;

		count++;
		if (count == 1) {
			vif = rwnx_vif;
		} else {
			vif = NULL;
			break;
		}
	}
	spin_unlock_bh(&rwnx_hw->cb_lock);

	return vif;
}

static int bes_sleep_manager_set_beacon_filter(struct rwnx_hw *rwnx_hw)
{
	const u8 elt_ids[] = {
		WLAN_EID_CHANNEL_SWITCH,
		WLAN_EID_EXT_CHANSWITCH_ANN,
		WLAN_EID_DS_PARAMS,
		WLAN_EID_HT_OPERATION,
		WLAN_EID_VHT_OPERATION,
		0xff // End with 0xff
	};
	struct rwnx_vif *rwnx_vif;
	u8 vif_index;

	/* Obtain the vif for setting beacon filter */
	if (!(rwnx_vif = bes_sleep_manager_get_vif_for_beacon_filter(rwnx_hw))) {
		bes_dbg(BES_DBG_PWR, "%s Not get vif for beacon filter\n", __func__);
		return 0;
	}

	/* Check the validity of vif_index */
	vif_index = rwnx_vif->vif_index;
	if (vif_index >= NX_ITF_MAX)
		return -EINVAL;

	/* Set beacon filter */
	return bes_filter_set_beacon_filter(rwnx_hw, true, vif_index,
			elt_ids, NULL, NULL);
}

static int bes_sleep_manager_disable_beacon_filter(struct rwnx_hw *rwnx_hw)
{
	return bes_filter_switch_beacon_filter(rwnx_hw, false);
}

void bes_sleep_manager_ctrl_beacon_filter(struct rwnx_hw *rwnx_hw, bool enable_filter)
{
	int ret = 0;
	static bool beacon_filter = false;

	if (enable_filter == beacon_filter) {
		return;
	}

	if (enable_filter) {
		ret = bes_sleep_manager_set_beacon_filter(rwnx_hw);
	} else {
		ret = bes_sleep_manager_disable_beacon_filter(rwnx_hw);
	}

	if (!ret) {
		beacon_filter = enable_filter;
		bes_dbg(BES_DBG_PWR, "[%s] beacon filter\n",
			enable_filter ? "set" : "disable");
	} else {
		bes_err(BES_DBG_PWR, "%s ERROR(%d): Fail to %s beacon filter\n",
			__func__, ret, enable_filter ? "set" : "disable");
	}
}

static void bes_sleep_manager_set_beacon_filter_handler(void *arg, u8 status)
{
	struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)arg;

	if (status == PWR_LP_CB_STATUS_BEFORE_WIFI_SLEEP) {
		/* Set beacon filter before wifi sleep */
		bes_sleep_manager_ctrl_beacon_filter(rwnx_hw, true);
	} else if (status == PWR_LP_CB_STATUS_AFTER_WIFI_WAKEUP) {
		/* Disable beacon filter for sta after wifi wakeup */
		bes_sleep_manager_ctrl_beacon_filter(rwnx_hw, false);
	} else if (status == PWR_LP_CB_STATUS_IO_SLEEP && bes_pwr_get_vif_link_cnt(rwnx_hw) == 0) {
		bes_pwr_unregister_lp_cb(rwnx_hw, bes_sleep_manager_set_beacon_filter_handler);
	}
}

static void bes_sleep_manager_dyn_ctrl_lp_cb_handler(void *arg, u8 status)
{
	struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)arg;

	if (status == PWR_LP_CB_STATUS_BEFORE_WIFI_SLEEP) {
		if (!lp_dyn_cb_rgt && bes_pwr_get_vif_link_cnt(rwnx_hw)) {
			if (rwnx_mod_params.keepalive_period > 0)
				bes_pwr_register_lp_cb(rwnx_hw, bes_sleep_manager_set_keepalive_handler, rwnx_hw);
			bes_pwr_register_lp_cb(rwnx_hw, bes_sleep_manager_set_beacon_filter_handler, rwnx_hw);
			bes_dbg(BES_DBG_PWR, "%s dyn register cb\n", __func__);
			lp_dyn_cb_rgt = true;
		}
	} else if (status == PWR_LP_CB_STATUS_IO_SLEEP) {
		if (lp_dyn_cb_rgt && !bes_pwr_get_vif_link_cnt(rwnx_hw)) {
			lp_dyn_cb_rgt = false;
			bes_dbg(BES_DBG_PWR, "%s dyn unregister cb\n", __func__);
		}
	}
}

static void bes_sleep_manager_set_arp_offload_lp_cb_handler(void *arg, u8 status)
{
	struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)arg;
	u8 i = 0;
	u32 vif_idx_map = 0;
	struct rwnx_vif *rwnx_vif = NULL;
	struct in_device *in_dev = NULL;
	struct in_ifaddr *ifa = NULL;
	u32 ipv4_addr = 0;
	int ret = 0;
	unsigned long flags;

	if (status == PWR_LP_CB_STATUS_WIFI_SLEEP) {
		/* Get vif_idx_map value */
		vif_idx_map = bes_common_get_vif_idx_map(rwnx_hw);
		if (!vif_idx_map)
			return;

		for_each_set_bit(i, (const unsigned long *)&vif_idx_map, NX_ITF_MAX) {
			u8 vif_index;

			/* vif needs to be connected */
			rwnx_vif = rwnx_rx_get_vif(rwnx_hw, i);
			if (!rwnx_vif || !netif_carrier_ok(rwnx_vif->ndev))
				continue;

			/* Check the validity of vif_index */
			vif_index = rwnx_vif->vif_index;
			if (vif_index >= NX_ITF_MAX)
				continue;

			/* Aoid repeating to set */
			if (rwnx_hw->filter.arp_offload[vif_index])
				continue;

			/* Get ip */
			local_irq_save(flags);
			if ((in_dev = rwnx_vif->ndev->ip_ptr) &&
			    (ifa = in_dev->ifa_list))
				ipv4_addr = ifa->ifa_local;
			local_irq_restore(flags);

			/* Add arp offload function for static ip */
			ret = bes_filter_manager_set_arp_offload(rwnx_hw, rwnx_vif, ipv4_addr);
			bes_err_with_cond(ret, BES_DBG_FILTER_MANAGER,
				"%s ERROR(%d): fail to ctrl arp offload. vif: %d ip:%pI4\n",
				__func__, ret, vif_index, &rwnx_vif->ipv4_addr);
		}
	}
}

int bes_sleep_manager_chan_switch_notify_trigger(struct rwnx_hw *rwnx_hw)
{
	struct mm_custom_req_hdr req;

	req.id = CUSTOM_ID_CS_NOTIFY_TRIGGER;
	req.length = sizeof(struct mm_custom_req_hdr);

	return rwnx_send_custom_request(rwnx_hw, &req, NULL, 0);
}

static void bes_sleep_manager_cs_notify_trigger_handler(void *arg, u8 status)
{
	struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)arg;

	if (status == PWR_LP_CB_STATUS_AFTER_WIFI_WAKEUP) {
		bes_sleep_manager_chan_switch_notify_trigger(rwnx_hw);
	}
}

void bes_sleep_manager_init(struct rwnx_hw *rwnx_hw)
{
	bes_dbg(BES_DBG_PWR, "%s\n", __func__);

	if (rwnx_mod_params.uapsd_timeout <= 0)
		bes_pwr_register_lp_cb(rwnx_hw, bes_sleep_manager_ps_mode_handler, rwnx_hw);
	bes_pwr_register_lp_cb(rwnx_hw, bes_sleep_manager_dyn_ctrl_lp_cb_handler, rwnx_hw);
	bes_pwr_register_lp_cb(rwnx_hw, bes_sleep_manager_set_arp_offload_lp_cb_handler, rwnx_hw);
	bes_pwr_register_lp_cb(rwnx_hw, bes_sleep_manager_cs_notify_trigger_handler, rwnx_hw);
}

void bes_sleep_manager_deinit(struct rwnx_hw *rwnx_hw)
{
	bes_dbg(BES_DBG_PWR, "%s\n", __func__);

	if (rwnx_mod_params.uapsd_timeout <= 0)
		bes_pwr_unregister_lp_cb(rwnx_hw, bes_sleep_manager_ps_mode_handler);
	lp_dyn_cb_rgt = false;
	bes_pwr_unregister_lp_cb(rwnx_hw, bes_sleep_manager_set_keepalive_handler);
	bes_pwr_unregister_lp_cb(rwnx_hw, bes_sleep_manager_set_beacon_filter_handler);
	bes_pwr_unregister_lp_cb(rwnx_hw, bes_sleep_manager_dyn_ctrl_lp_cb_handler);
	bes_pwr_unregister_lp_cb(rwnx_hw, bes_sleep_manager_set_arp_offload_lp_cb_handler);
	bes_pwr_unregister_lp_cb(rwnx_hw, bes_sleep_manager_cs_notify_trigger_handler);
}