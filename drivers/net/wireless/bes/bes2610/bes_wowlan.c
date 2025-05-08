/*
 * @file bes_wowlan.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/ctype.h>
#include <linux/types.h>
#include <net/cfg80211.h>
#include <net/ip.h>
#include <linux/completion.h>
#include <linux/bitops.h>
#include "rwnx_defs.h"
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <net/ipv6.h>
#include "bes_log.h"
#include "rwnx_strs.h"
#include "bes_custom_filter.h"
#include "bes_pwr.h"
#include "bes_wowlan.h"
#include "bes_filter.h"
#include "rwnx_msg_tx.h"
#include "rwnx_testmode.h"
#include "bes_psa.h"
#include "reg_access.h"
#include "rwnx_main.h"
#include "bes_sched_scan.h"
#include "bes_conn_state.h"
#include "bes_wpa_priv_cmd.h"

static void bes_wowlan_config_show(struct cfg80211_wowlan *wow)
{
	gfp_t flags;
	u8 *buf = NULL, *mask = NULL, *pat = NULL;
	int len = 0;
	int n = 0, t = 0, patlen = 0, pkt_offset = 0;
	const int buf_len = 512;

	/* Check dbg_lvl */
	if (bes_get_dbg_lvl(BES_DBG_WOWLAN) < BES_LOG_DBG ||
	    bes_get_dbg_lvl(BES_DBG_WOWLAN) == BES_LOG_NONE ||
	    !wow)
		return;

	/* Malloc temporary memory */
	if (in_atomic())
		flags = GFP_ATOMIC;
	else
		flags = GFP_KERNEL;
	buf = kmalloc(buf_len, flags);
	if (!buf) {
		bes_err(BES_DBG_WOWLAN, "%s Error: fail to malloc", __func__);
		return;
	}

	/* Construct strings according to wowlan config */
	len += snprintf(&buf[len], buf_len - len - 1, "WoWLAN config:\n");
	if (wow->any)
		len += snprintf(&buf[len], buf_len - len - 1, " * wake up on special any trigger\n");

	if (wow->disconnect)
		len += snprintf(&buf[len], buf_len - len - 1, " * wake up on disconnect\n");

	if (wow->magic_pkt)
		len += snprintf(&buf[len], buf_len - len - 1, " * wake up on magic packet\n");

	if (wow->gtk_rekey_failure)
		len += snprintf(&buf[len], buf_len - len - 1, " * wake up on GTK rekeying failure\n");

	if (wow->eap_identity_req)
		len += snprintf(&buf[len], buf_len - len - 1, " * eap_identity_req\n");

	if (wow->four_way_handshake)
		len += snprintf(&buf[len], buf_len - len - 1, " * wake up on 4-way handshake\n");

	if (wow->four_way_handshake)
		len += snprintf(&buf[len], buf_len - len - 1, " * wake up on 4-way handshake\n");

	if (wow->rfkill_release)
		len += snprintf(&buf[len], buf_len - len - 1, " * wake up on RF-kill release\n");

	if (wow->tcp)
		len += snprintf(&buf[len], buf_len - len - 1, " * wake up on TCP connection\n");

	if ((wow->n_patterns > 0) && (wow->patterns)) {
		for (n = 0; n < wow->n_patterns; n++) {
			patlen = wow->patterns[n].pattern_len;
			pkt_offset = wow->patterns[n].pkt_offset;
			mask = (u8 *)wow->patterns[n].mask;
			pat = (u8 *)wow->patterns[n].pattern;

			if (!mask || !pat) {
				len += snprintf(&buf[len], buf_len - len - 1, " * (invalid pattern specification)\n");
				continue;
			}

			len += snprintf(&buf[len], buf_len - len - 1, " * wake up on packet offset: %d", pkt_offset);
			len += snprintf(&buf[len], buf_len - len - 1, " pattern: ");

			for (t = 0; t < patlen; t++) {
				if (mask[t / 8] & (1 << (t % 8)))
					len += snprintf(&buf[len], buf_len - len - 1, "%.2x", pat[t]);
				else
					len += snprintf(&buf[len], buf_len - len - 1, "--");

				if (t != patlen - 1)
					len += snprintf(&buf[len], buf_len - len - 1, ":");
			}
			len += snprintf(&buf[len], buf_len - len - 1, "\n");
		}
	}

	if (wow->nd_config) {
		len += snprintf(&buf[len], buf_len - len - 1, " * wake up on network detection\n");
		len += snprintf(&buf[len], buf_len - len - 1, "\tscan interval: %u secs\n", wow->nd_config->scan_plans[0].interval);
		len += snprintf(&buf[len], buf_len - len - 1, "\tinitial scan delay: %u secs\n", wow->nd_config->delay);
		if (wow->nd_config->relative_rssi_set) {
			len += snprintf(&buf[len], buf_len - len - 1, "\trelative_rssi:%d\n", wow->nd_config->relative_rssi);
			len += snprintf(&buf[len], buf_len - len - 1, "\trssi_adjust:\n");
			len += snprintf(&buf[len], buf_len - len - 1, "\t\tband: %d delta: %d\n",
						wow->nd_config->rssi_adjust.band, wow->nd_config->rssi_adjust.delta);
		}

		len += snprintf(&buf[len], buf_len - len - 1, "\tmatches:\n");
		for (n = 0; n < wow->nd_config->n_match_sets; n++)
			len += snprintf(&buf[len], buf_len - len - 1, "\t\tSSID: %s bssid: %pM rssi: %d dBm\n",
					wow->nd_config->match_sets[n].ssid.ssid,
					wow->nd_config->match_sets[n].bssid,
					wow->nd_config->match_sets[n].rssi_thold);

		if (!(wow->nd_config->n_ssids == 0 ||
		      (wow->nd_config->n_ssids == 1 && wow->nd_config->ssids[0].ssid_len == 0))) {
			len += snprintf(&buf[len], buf_len - len - 1, "\tactive scans:\n");
			for (n = 0; n < wow->nd_config->n_ssids; n++)
				len += snprintf(&buf[len], buf_len - len - 1, "\t\tSSID: %s\n", wow->nd_config->ssids[n].ssid);
		}

		len += snprintf(&buf[len], buf_len - len - 1, "\tfrequencies:");
		for (n = 0; n < wow->nd_config->n_channels; n++) {
			if (n >= 3) {
				len += snprintf(&buf[len], buf_len - len - 1, " ...");
				break;
			}
			len += snprintf(&buf[len], buf_len - len - 1, " %d", wow->nd_config->channels[n]->center_freq);
		}
		len += snprintf(&buf[len], buf_len - len - 1, "\n");
	}

	/* Print and free memory */
	bes_dbg(BES_DBG_WOWLAN, "%s", buf);
	kfree(buf);
}

int bes_wowlan_insert_at_head(struct rwnx_hw *rwnx_hw, struct list_head *list_set_head,
	struct bes_capacity *stats, struct filter_item *item)
{
	int wf_size = 0;

	/* Get filter size */
	wf_size = bes_filter_manager_filter_size(item->type, item->action, &item->param);
	if (wf_size < 0)
		return -EINVAL;

	/* Check if there is still enough bes_capacity */
	if ((stats->rxf_idx + 1 > rwnx_hw->filter.max.rxf_idx) ||
	    (stats->rxf_arry_idx + wf_size > rwnx_hw->filter.max.rxf_arry_idx))
		return -ERANGE;

	/* Insert item at head */
	list_add(&item->list_set, list_set_head);
	item->ref_count++;
	stats->rxf_idx++;
	stats->rxf_arry_idx += wf_size;

	return 0;
}

static int bes_wowlan_set_standard_filter(struct rwnx_hw *rwnx_hw, struct cfg80211_wowlan *wow,
	struct list_head *list_set_head, struct bes_capacity *stats)
{
	int ret = 0;
	u8 n = 0, i = 0;
	u32 vif_idx_map = 0;
	struct rwnx_vif *rwnx_vif = NULL;
	struct filter_item *item = NULL;

	/* Get vif_idx_map value */
	vif_idx_map = bes_common_get_vif_idx_map(rwnx_hw);
	if (!vif_idx_map)
		return 0;

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

		/* Add wowlan filter to list_set_head */
		do {
			/* Arp offload filter */
			if (rwnx_hw->filter.arp_offload[vif_index]) {
				item = bes_filter_manager_create_ipv4_arp(vif_index, FILTER_IPV4_ARP, ARP_OFFLOAD_FUNC_IDX, 0, RXF_ACCEPT);
				if (!item) {
					ret = -EINVAL;
					bes_err(BES_DBG_WOWLAN,
						"%s ERROR(%d): Fail to create arp filter on vif %s at head\n",
						__func__, ret, rwnx_vif->ndev->name);
					break;
				}
				ret = bes_wowlan_insert_at_head(rwnx_hw, list_set_head, stats, item);
				if (ret) {
					bes_err(BES_DBG_WOWLAN,
						"%s ERROR(%d): Fail to add arp filter on vif %s at head\n",
						__func__, ret, rwnx_vif->ndev->name);
					break;
				}
			}

			/* Ns offload filter */
			if (rwnx_hw->filter.ns_offload[vif_index]) {
				item = bes_filter_manager_create_ipv6_ns(vif_index, FILTER_IPV6_NS, NS_OFFLOAD_FUNC_IDX, 0, RXF_ACCEPT);
				if (!item) {
					ret = -EINVAL;
					bes_err(BES_DBG_WOWLAN,
						"%s ERROR(%d): Fail to create ns filter on vif %s at head\n",
						__func__, ret, rwnx_vif->ndev->name);
					break;
				}
				ret = bes_wowlan_insert_at_head(rwnx_hw, list_set_head, stats, item);
				if (ret) {
					bes_err(BES_DBG_WOWLAN,
						"%s ERROR(%d): Fail to add arp filter on vif %s at head\n",
						__func__, ret, rwnx_vif->ndev->name);
					break;
				}
			}

			/* Magic packet filter */
			if (wow->magic_pkt) {
				item = bes_filter_manager_create_magic_packet(vif_index, FILTER_MAGIC_PACKET,
						(struct mac_addr *)rwnx_vif->ndev->dev_addr, 0, RXF_ACCEPT);
				if (!item) {
					ret = -EINVAL;
					bes_err(BES_DBG_WOWLAN,
						"%s ERROR(%d): Fail to create magic packet filter with mac %pM on vif %s at head\n",
						__func__, ret,
						rwnx_vif->ndev->dev_addr,
						rwnx_vif->ndev->name);
					break;
				}
				ret = bes_wowlan_insert_at_head(rwnx_hw, list_set_head, stats, item);
				if (ret) {
					bes_err(BES_DBG_WOWLAN,
						"%s ERROR(%d): Fail to add magic packet filter with mac %pM on vif %s at head\n",
					__func__, ret,
					rwnx_vif->ndev->dev_addr,
					rwnx_vif->ndev->name);
					break;
				}
			}

			/* Four_way_handshake filter */
			if (wow->four_way_handshake) {
				item = bes_filter_manager_create_eth_type(vif_index, FILTER_ETH_TYPE, ETH_P_PAE, RXF_MATCH_EQUAL, RXF_ACCEPT);
				if (!item) {
					ret = -EINVAL;
					bes_err(BES_DBG_WOWLAN,
						"%s ERROR(%d): Fail to create four_way_handshake filter on vif %s at head\n",
						__func__, ret, rwnx_vif->ndev->name);
					break;
				}
				ret = bes_wowlan_insert_at_head(rwnx_hw, list_set_head, stats, item);
				if (ret) {
					bes_err(BES_DBG_WOWLAN,
						"%s ERROR(%d): Fail to add four_way_handshake filter on vif %s at head\n",
						__func__, ret, rwnx_vif->ndev->name);
					break;
				}
			}

			/* N_patterns filter */
			if (wow->n_patterns > 0) {
				for (n = 0; n < wow->n_patterns; n++) {
					item = bes_filter_manager_create_pattern(vif_index, FILTER_PATTERN,
						&wow->patterns[n], RXF_MATCH_EQUAL, RXF_ACCEPT);
					if (!item) {
						ret = -EINVAL;
						bes_err(BES_DBG_WOWLAN,
							"%s ERROR(%d): Fail to create pattern filter with n=%d on vif %s at head\n",
							__func__, ret, n, rwnx_vif->ndev->name);
						break;
					}
					ret = bes_wowlan_insert_at_head(rwnx_hw, list_set_head, stats, item);
					if (ret) {
						bes_err(BES_DBG_WOWLAN,
							"%s ERROR(%d): Fail to add pattern filter with n=%d on vif %s at head\n",
							__func__, ret, n, rwnx_vif->ndev->name);
						break;
					}
				}
			}
		} while(0);

		/* Flush list_set_head list */
		if (ret) {
			bes_filter_manager_flush_list_set(list_set_head);

			return ret;
		}
	}

	return 0;
}

static void *bes_wowlan_get_vif_for_net_detect(struct rwnx_hw *rwnx_hw)
{
	struct rwnx_vif *rwnx_vif = NULL, *vif = NULL;

	spin_lock_bh(&rwnx_hw->cb_lock);
	list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
		if (!netif_running(rwnx_vif->ndev))
			continue;

		if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION) {
			vif = rwnx_vif;
			break;
		}
	}
	spin_unlock_bh(&rwnx_hw->cb_lock);

	return vif;
}

static int bes_wowlan_net_detect_wake_start(struct rwnx_hw *rwnx_hw, struct cfg80211_sched_scan_request *nd_config)
{
	struct rwnx_vif *rwnx_vif = NULL;
	u8 vif_index;

	if (!nd_config)
		return 0;

	if (nd_config->dev)
		rwnx_vif = netdev_priv(nd_config->dev);
	else
		rwnx_vif = bes_wowlan_get_vif_for_net_detect(rwnx_hw);
	if (!rwnx_vif) {
		bes_warn(BES_DBG_WOWLAN, "%s can't get vif for net-detect\n", __func__);
		return 0;
	}

	vif_index = rwnx_vif->vif_index;
	if (vif_index >= NX_ITF_MAX) {
		bes_warn(BES_DBG_WOWLAN, "%s invlid vif index\n", __func__);
		return 0;
	}

	bes_dbg(BES_DBG_WOWLAN, "%s get vif(%d) for net-detect\n", __func__, vif_index);

	return bes_sched_scan_start(rwnx_hw, vif_index, nd_config);
}

static int bes_wowlan_net_detect_wake_stop(struct rwnx_hw *rwnx_hw, struct cfg80211_sched_scan_request *nd_config)
{
	struct rwnx_vif *rwnx_vif = NULL;

	if (!nd_config)
		return 0;

	if (nd_config->dev)
		rwnx_vif = netdev_priv(nd_config->dev);
	else
		rwnx_vif = bes_wowlan_get_vif_for_net_detect(rwnx_hw);
	if (!rwnx_vif) {
		bes_warn(BES_DBG_WOWLAN, "%s can't get vif for net-detect\n", __func__);
		return 0;
	}
	bes_dbg(BES_DBG_WOWLAN, "%s get vif(%d) for net-detect\n", __func__, rwnx_vif->vif_index);

	return bes_sched_scan_stop(rwnx_hw, rwnx_vif->vif_index, nd_config->reqid);
}

static int bes_wowlan_suspend_standard_cfg(struct rwnx_hw *rwnx_hw, struct cfg80211_wowlan *wow)
{
	int ret = 0;
	struct list_head list_set_head;
	struct bes_capacity stats;

	/* rwnx_hw must be valid */
	if (!rwnx_hw)
		return -EINVAL;

	/* Wowlan filter mode is not WOWLAN_MODE_STANDARD, return directly */
	if (rwnx_hw->wowlan.mode != WOWLAN_MODE_STANDARD)
		return 0;

	/* Wowlan filetr disable, and return directly */
	if (!wow) {
		bes_warn(BES_DBG_SDIO, "%s Wowlan filetr is disable\n", __func__);
		return 0;
	}

	/* Show wowlan operation config */
	bes_wowlan_config_show(wow);

	/* Wowlan config is any, also retuen directly */
	if (wow->any)
		return 0;

	/* Init list and stats parameter */
	INIT_LIST_HEAD(&list_set_head);
	stats.rxf_idx = 0;
	stats.rxf_arry_idx = 0;

	/* Add wowlan filter to list_set_head according to wowlan config */
	ret = bes_wowlan_set_standard_filter(rwnx_hw, wow, &list_set_head, &stats);
	if (ret)
		return ret;

	/* Set wowlan filters in list_set_head to device */
	ret = bes_filter_manager_suspend_cfg(rwnx_hw, &list_set_head);
	if (ret)
		return ret;

	/* delate all filter */
	bes_filter_manager_flush_list_set(&list_set_head);

	/* Start the scan to be used for net detect wake */
	return bes_wowlan_net_detect_wake_start(rwnx_hw, wow->nd_config);
}

static void bes_wowlan_flush_delayed_work_handler(void *arg, u8 status)
{
	struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)arg;
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	int ret = 0;

	if (status == PWR_LP_CB_STATUS_WIFI_SLEEP) {
		/* Notify mcu to configure wake-up GPIO before host sleep */
		ret = rwnx_plat_notify_host_suspend(rwnx_plat);
		bes_err_with_cond(ret, BES_DBG_SDIO, "%s Error: setting hostwake gpio config. ret:%d\n", __func__, ret);

		/* wifi lp delayed work completed successfully */
		complete(&rwnx_hw->wowlan.complete);
	}
}

static void bes_wowlan_filter_probe_req_frame(struct mm_hw_filter *hw_filter, bool filter)
{
	const u32 filter_bits = NXMAC_ACCEPT_PROBE_REQ_BIT | NXMAC_ACCEPT_OTHER_DATA_FRAMES_BIT;

	if (filter)
		hw_filter->umac_filter &= (~filter_bits);
	else
		hw_filter->umac_filter |= filter_bits;
}

static u8* bes_wowlan_get_current_bssid(struct rwnx_hw *rwnx_hw)
{
	int count = 0;
	u8 *mac_addr = NULL;
	struct rwnx_vif *rwnx_vif = NULL;
	struct rwnx_sta *rwnx_sta = NULL;

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

		/* connected ap is valid */
		rwnx_sta = rwnx_vif->sta.ap;
		if (!rwnx_sta || !rwnx_sta->valid)
			continue;

		/* set mac_addr value */
		count++;
		if (count == 1) {
			mac_addr = rwnx_sta->mac_addr;
		} else {
			mac_addr = NULL;
			break;
		}
	}
	spin_unlock_bh(&rwnx_hw->cb_lock);

	return mac_addr;
}

static void bes_wowlan_filter_bssid(struct rwnx_hw *rwnx_hw, struct mm_hw_filter *hw_filter, bool host_suspend)
{
	u8 *bssid = NULL;

	/* The count of opened vifs need be more than one */
	if (bes_pwr_get_vif_up_cnt(rwnx_hw) < 2)
		return;

	/* Only one vif used and is sta/p2p gc */
	bssid = bes_wowlan_get_current_bssid(rwnx_hw);
	if (!bssid)
		return;

	/*
	 * Workaround: the hardware can only filter one bssid. If we open more than one
	 * interface but we just use one interface actually, We will receive all packets
	 * on air. In order to enter sleep in these case, we modify hw filter directly.
	 */
	hw_filter->bssid_low = ((u32 *)bssid)[0];
	hw_filter->bssid_hi = ((u32 *)bssid)[1] & 0x0000ffff;
	if (host_suspend)
		hw_filter->lmac_filter &= ~NXMAC_ACCEPT_OTHER_BSSID_BIT;
	else
		hw_filter->lmac_filter |= NXMAC_ACCEPT_OTHER_BSSID_BIT;
}

static int bes_wowlan_ctrl_hw_filter(struct rwnx_hw *rwnx_hw, bool host_suspend)
{
	int ret = 0;
	struct mm_hw_filter hw_filter;

	do {
		/* Get hw filter */
		ret = bes_filter_get_hw_filter(rwnx_hw, &hw_filter);
		bes_dbg(BES_DBG_WOWLAN, "[get hw filter when %s] bssid_low:0x%08x bssid_hi:0x%08x umac_filter:0x%08x lmac_filter:0x%08x",
			host_suspend ? "suspend" : "resume",
			hw_filter.bssid_low, hw_filter.bssid_hi, hw_filter.umac_filter, hw_filter.lmac_filter);
		if (ret)
			break;

		/* Filter bssid */
		bes_wowlan_filter_bssid(rwnx_hw, &hw_filter, host_suspend);

		/* Filter probe_req */
		bes_wowlan_filter_probe_req_frame(&hw_filter, host_suspend);

		/* Set hw filter */
		ret = bes_filter_set_hw_filter(rwnx_hw, &hw_filter);
		bes_dbg(BES_DBG_WOWLAN, "[set hw filter when %s] bssid_low:0x%08x bssid_hi:0x%08x umac_filter:0x%08x lmac_filter:0x%08x",
			host_suspend ? "suspend" : "resume",
			hw_filter.bssid_low, hw_filter.bssid_hi, hw_filter.umac_filter, hw_filter.lmac_filter);
	} while(0);

	return ret;
}

int bes_wowlan_set_filter(struct rwnx_hw *rwnx_hw, struct cfg80211_wowlan *wow)
{
	int ret = 0;

	do {
		/* Show wowlan filter mode */
		bes_info(BES_DBG_WOWLAN, "Wowlan filter mode: %s\n",
			rwnx_hw->wowlan.mode ==  WOWLAN_MODE_CUSTOM ? "CUSTOM" : "STANDARD");

		/* Set standard wowlan filter when wowlan filter mode is WOWLAN_MODE_STANDARD */
		ret = bes_wowlan_suspend_standard_cfg(rwnx_hw, wow);
		bes_err_with_cond(ret, BES_DBG_WOWLAN, "%s ERROR(%d): Fail to set suspend cfg\n", __func__, ret);
		if (ret)
			break;

		/* Set hw filter before host suspend */
		ret = bes_wowlan_ctrl_hw_filter(rwnx_hw, true);
		bes_err_with_cond(ret, BES_DBG_WOWLAN, "%s ERROR(%d): Fail to set hw filter when host suspend\n", __func__, ret);
	} while(0);

	return ret;
}

static int bes_wowlan_wait_delayed_work_finish(struct rwnx_hw *rwnx_hw)
{
	int ret = 0;
	char *buf = NULL;
	int len = 0;

	/* If there is an interface waiting for the completion of obtaining IP,
	   and host exits suspend directly */
	if (test_bit(RWNX_DEV_CONNECTING, &rwnx_hw->flags)) {
		bes_info(BES_DBG_WOWLAN, "%s Waiting for the completion of obtaining IP and exiting host suspend directly\n", __func__);
		return -EBUSY;
	}

	/* Deal with lp event before host suspend */
	if (bes_pwr_handle_lp_evt_before_suspend(rwnx_hw)) {
		bes_info(BES_DBG_WOWLAN, "%s Exiting host suspend due to deal lp event before host suspend\n", __func__);
		ret = -EBUSY;
		goto out;
	}

	/* Set the event to ensure that the completion varable of lp_delayed_work can be initialized and
	   bes_wowlan_flush_delayed_work_handler function can be called before the host suspend */
	bes_pwr_set_busy_event(rwnx_hw, RWNX_INVALID_VIF, PHY_CONST_EVENT_DLY_WORK_FLUSH);
	reinit_completion(&rwnx_hw->wowlan.complete);
	bes_pwr_register_lp_cb(rwnx_hw, bes_wowlan_flush_delayed_work_handler, rwnx_hw);
	bes_pwr_clear_busy_event(rwnx_hw, RWNX_INVALID_VIF, PHY_CONST_EVENT_DLY_WORK_FLUSH);

	/* Wait for delayed work to end */
	ret = wait_for_completion_interruptible_timeout(&rwnx_hw->wowlan.complete,
							msecs_to_jiffies(BES_WOWLAN_DELAY_WORK_END_MAX_TIMEOUT));
	if (ret <= 0) {
		bes_info(BES_DBG_WOWLAN, "Waiting for the delayed work to end timeout\n");
		ret = -EBUSY;
	} else {
		bes_dbg(BES_DBG_WOWLAN, "%s Timeout left:%dms(%d)\n",
			__func__, jiffies_to_msecs(ret), ret);
		ret = 0;
	}

out:
	if (ret && bes_get_dbg_lvl(BES_DBG_WOWLAN) == BES_LOG_DBG) {
		buf = (char *)kmalloc(BES_DBGFS_PWR_STATUS_BUFSIZE, GFP_KERNEL);
		if (!buf) {
			bes_err(BES_DBG_WOWLAN, "Waiting for the delayed work to end timeout and fail to alloc dbg memory\n");
			ret = -ENOMEM;
		} else {
			len = bes_pwr_event_dump(rwnx_hw, buf, BES_DBGFS_PWR_STATUS_BUFSIZE, BES_DBGFS_PWR_STATUS_BUFSIZE);

			if (len)
				bes_dbg(BES_DBG_WOWLAN, "%s", buf);

			kfree(buf);
		}
	}

	/* Clearing the flag for dealing event before host suspend */
	bes_pwr_set_flag_suspend(rwnx_hw, false);

	return ret;
}

int bes_wowlan_suspend_cfg(struct rwnx_hw *rwnx_hw, struct cfg80211_wowlan *wow)
{
	int ret = 0;

	/* rwnx_hw must be valid */
	if (rwnx_hw == NULL)
		return -EINVAL;

	/* platform is closed */
	if (!rwnx_wlan_enabled(rwnx_hw))
		return 0;

	do {
		/* May need to refresh the filters for screen off */
		ret = bes_wpa_priv_cmd_update_screen_suspend_filters(rwnx_hw, true);
		if (ret)
			break;

		/* Set filter before host suspend */
		ret = bes_wowlan_set_filter(rwnx_hw, wow);
		if (ret)
			break;

		/* Flush delayed work before suspend */
		ret = bes_wowlan_wait_delayed_work_finish(rwnx_hw);
	} while(0);

	/* If an exception occurs, call the resume process manually.
	   Otherwise, dump the first RX information after host suspend */
	if (ret)
		bes_wowlan_resume_cfg(rwnx_hw);
#if defined(CONFIG_BES_MSPI)
	else
		bes_wowlan_set_wakeup_source(rwnx_hw, true);
#endif

	return ret;
}

static int bes_wowlan_resume_standard_cfg(struct rwnx_hw *rwnx_hw)
{
	struct wiphy *wiphy = NULL;
	struct cfg80211_wowlan *wow = NULL;
	int ret = 0;

	/* rwnx_hw must be valid */
	if (!rwnx_hw)
		return -EINVAL;

	/* Wowlan filter mode is not WOWLAN_MODE_STANDARD, return directly */
	if (rwnx_hw->wowlan.mode != WOWLAN_MODE_STANDARD)
		return 0;

	/* Wowlan filetr disable, and return directly */
	if ((wiphy = rwnx_hw->wiphy) == NULL ||
	    (wow = wiphy->wowlan_config) == NULL) {
		bes_warn(BES_DBG_SDIO, "%s Wowlan filetr is disable\n", __func__);
		return 0;
	}

	/* Show wowlan operation config */
	bes_wowlan_config_show(wow);

	/* Wowlan config is any, also retuen directly */
	if (wow->any)
		return 0;

	/* Restore filter after resume */
	ret =  bes_filter_manager_resume_cfg(rwnx_hw);
	if (ret)
		return ret;

	return bes_wowlan_net_detect_wake_stop(rwnx_hw, wow->nd_config);
}

int bes_wowlan_resume_filter(struct rwnx_hw *rwnx_hw)
{
	int ret = 0;

	/* rwnx_hw must be valid */
	if (!rwnx_hw)
		return -EINVAL;

	do {
		/* Show wowlan filter mode */
		bes_info(BES_DBG_WOWLAN, "Wowlan filter mode: %s\n",
			rwnx_hw->wowlan.mode ==  WOWLAN_MODE_CUSTOM ? "CUSTOM" : "STANDARD");

		/* Resume standard wowlan filter when wowlan filter mode is WOWLAN_MODE_STANDARD */
		ret = bes_wowlan_resume_standard_cfg(rwnx_hw);
		bes_err_with_cond(ret, BES_DBG_WOWLAN, "%s ERROR(%d): Fail to set resume cfg\n", __func__, ret);
		if (ret)
			break;

		/* Resume hw filter after host resume */
		ret = bes_wowlan_ctrl_hw_filter(rwnx_hw, false);
		bes_err_with_cond(ret, BES_DBG_WOWLAN, "%s ERROR(%d): Fail to set hw filter when host resume\n", __func__, ret);
	} while(0);

	return ret;
}

int bes_wowlan_resume_cfg(struct rwnx_hw *rwnx_hw)
{
	int ret = 0;
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();

	/* rwnx_hw must be valid */
	if (!rwnx_hw)
		return -EINVAL;

	/* platform is closed */
	if (!rwnx_wlan_enabled(rwnx_hw))
		return 0;

	/* Notify mcu host is awake */
	ret = rwnx_plat_notify_host_resume(rwnx_plat);
	bes_err_with_cond(ret, BES_DBG_SDIO, "%s Error: notify mcu hostwake. ret:%d\n", __func__, ret);

	/* Reinit lp_delayed_work completion varable and
	   unregister bes_wowlan_flush_delayed_work_handler */
	reinit_completion(&rwnx_hw->wowlan.complete);
	bes_pwr_unregister_lp_cb(rwnx_hw, bes_wowlan_flush_delayed_work_handler);

	/* Resume filter after host wakeup */
	return  bes_wowlan_resume_filter(rwnx_hw);
}

void bes_wowlan_set_wakeup_source(struct rwnx_hw *rwnx_hw, bool on)
{
	if (!rwnx_hw)
		return;

	rwnx_hw->wowlan.wakeup_source = on;
}

static bool bes_wowlan_wakeup_source(struct rwnx_hw *rwnx_hw)
{
	if (!rwnx_hw->wowlan.wakeup_source)
		return false;

	bes_wowlan_set_wakeup_source(rwnx_hw, false);

	return true;
}

static bool check_filter_item(const struct sk_buff *skb, u16 offset, const u8 *match, u16 len)
{
	u8 filter_item[BES_MAX_CHECK_ITEM_LEN];
	u8 *filter_item_ptr = NULL;
	struct bes_sk_param bes_sk_param;

	if (len > BES_MAX_CHECK_ITEM_LEN) {
		WARN_ON(1);
		return false;
	}

	/* Check the validity of parameters */
	if (!skb || !match)
		return false;

	/* Parse filter_item */
	bes_sk_param.param_ptr = (void *)&(filter_item[0]);
	bes_sk_param.offset = offset;
	bes_sk_param.size = len;
	filter_item_ptr = (u8 *)bes_common_get_param_ptr((struct sk_buff *)skb, &bes_sk_param, FRAME_STATUS_RX);
	if (!filter_item_ptr)
		return false;

	/* Compare for equality */
	if (memcmp(filter_item_ptr, match, len) == 0)
		return true;
	else
		return false;
}

static bool bes_wowlan_wakeup_by_magic_pkt(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, const struct sk_buff *skb)
{
	u8 udp_protp = IP_PROT_UDP;
	const u8 mac[MAC_ADDR_LEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

	if (check_filter_item(skb, sizeof(struct ethhdr) + offsetof(struct iphdr, protocol), &udp_protp, 1) &&
	    check_filter_item(skb, sizeof(struct ethhdr) + sizeof(struct iphdr) +
	    		      sizeof(struct udphdr), mac, MAC_ADDR_LEN) &&
	    check_filter_item(skb, sizeof(struct ethhdr) + sizeof(struct iphdr) +
			      sizeof(struct udphdr) + MAC_ADDR_LEN, (const u8 *)rwnx_vif->ndev->dev_addr, MAC_ADDR_LEN) &&
	    check_filter_item(skb, sizeof(struct ethhdr) + sizeof(struct iphdr) +
	    		      sizeof(struct udphdr) + (MAC_ADDR_LEN << 1),
			      (const u8 *)rwnx_vif->ndev->dev_addr, MAC_ADDR_LEN))
		return true;

	return false;
}

static bool bes_wowlan_wakeup_by_four_way_handshake(struct rwnx_hw *rwnx_hw,
	struct rwnx_vif *rwnx_vif, const struct sk_buff *skb)
{
	u16 eth_type = cpu_to_be16(ETH_P_PAE);

	if (check_filter_item(skb, offsetof(struct ethhdr, h_proto), (const u8 *)&eth_type, 2))
		return true;

	return false;
}

static bool bes_wowlan_wakeup_by_n_patterns(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
	const struct sk_buff *skb, struct cfg80211_wowlan *wow, struct cfg80211_wowlan_wakeup *wakeup_data)
{
	int n = 0, t = 0;
	struct bes_sk_param bes_sk_param;

	for (n = 0; n < wow->n_patterns; n++) {
		u8 ptk[BES_MAX_PATTERN_LEN];
		u8 *ptk_ptr = NULL;

		if (wow->patterns[n].pattern_len > BES_MAX_PATTERN_LEN) {
			WARN_ON(1);
			return false;
		}

		/* Parse ptk */
		bes_sk_param.param_ptr = (void *)&(ptk[0]);
		bes_sk_param.offset = wow->patterns[n].pkt_offset;
		bes_sk_param.size = wow->patterns[n].pattern_len;
		ptk_ptr = (u8 *)bes_common_get_param_ptr((struct sk_buff *)skb, &bes_sk_param, FRAME_STATUS_RX);
		if (!ptk_ptr)
			return false;

		for (t = 0; t < wow->patterns[n].pattern_len; t++) {
			if ((wow->patterns[n].mask[t / 8] & (1 << (t % 8))) &&
			    (ptk_ptr[t] != wow->patterns[n].pattern[t]))
				return false;
			if (t == wow->patterns[n].pattern_len - 1) {
				wakeup_data->pattern_idx = n;

				return true;
			}
		}
	}

	return false;
}

void bes_wowlan_parse_data_pkt(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, struct sk_buff *skb)
{
	struct wiphy *wiphy;
	struct cfg80211_wowlan *wow;
	struct cfg80211_wowlan_wakeup wakeup_data;
	struct cfg80211_wowlan_wakeup *wakeup_report = &wakeup_data;
	u8 *filter_item, *filter_item_ptr;
	struct bes_sk_param bes_sk_param;

	/* Check the validity of parameters */
	if (!rwnx_hw || !rwnx_vif || !rwnx_vif->up || rwnx_vif->ch_index == RWNX_CH_NOT_SET || !skb)
		return;

	/* Initialize wiphy and wow */
	wiphy = rwnx_hw->wiphy;
	if (!wiphy)
		return;

	wow = wiphy->wowlan_config;
	if (!wow)
		return;

	/* Check whether to parse */
	if (!bes_wowlan_wakeup_source(rwnx_hw))
		return;

	/* Init wakeup_data */
	memset(&wakeup_data, 0, sizeof(wakeup_data));
	wakeup_data.pattern_idx = -1;

	/* Get the real skb data */
	filter_item = (u8 *)kmalloc(skb->len, GFP_KERNEL);
	if (!filter_item) {
		bes_err(BES_DBG_WOWLAN, "kmalloc error. %s\n", __func__);
		return;
	}
	bes_sk_param.param_ptr = (void *)&(filter_item[0]);
	bes_sk_param.offset = 0;
	bes_sk_param.size = skb->len;
	filter_item_ptr = (u8 *)bes_common_get_param_ptr((struct sk_buff *)skb, &bes_sk_param, FRAME_STATUS_RX);
	if (!filter_item_ptr) {
		bes_err(BES_DBG_WOWLAN, "Fail to get filter_item_ptr. %s\n", __func__);
		goto out;
	}

	/* Set wakeup packet and get wakeup reason */
	wakeup_data.packet_len = skb->len;
	wakeup_data.packet_80211 = false;
	wakeup_data.packet_present_len = wakeup_data.packet_len;
	wakeup_data.packet = filter_item_ptr;
	if (wow->magic_pkt && bes_wowlan_wakeup_by_magic_pkt(rwnx_hw, rwnx_vif, skb)) {
		wakeup_data.magic_pkt = true;
		bes_info(BES_DBG_WOWLAN, "Host may be waked by magic_pkt\n");
	} else if (wow->four_way_handshake && bes_wowlan_wakeup_by_four_way_handshake(rwnx_hw, rwnx_vif, skb)) {
		wakeup_data.four_way_handshake = true;
		bes_info(BES_DBG_WOWLAN, "Host may be waked by four_way_handshake\n");
	} else if ((wow->n_patterns > 0) &&
		   bes_wowlan_wakeup_by_n_patterns(rwnx_hw, rwnx_vif, skb, wow, &wakeup_data)) {
		bes_info(BES_DBG_WOWLAN, "Host may be waked by n_patterns(idx:%d)\n", wakeup_data.pattern_idx);
	} else {
		bes_info(BES_DBG_WOWLAN, "Host may be waked by a data packet\n");
		bes_info_dump(BES_DBG_WOWLAN, "skb->data: ", filter_item_ptr, skb->len);
	}

	/* Report wakeup reason */
	cfg80211_report_wowlan_wakeup(&rwnx_vif->wdev, wakeup_report, GFP_KERNEL);

out:
	/* Free memory */
	kfree(filter_item);
}

void bes_wowlan_parse_mgmt_pkt(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, const struct sk_buff *skb)
{
	struct cfg80211_wowlan_wakeup wakeup_data;
	struct cfg80211_wowlan_wakeup *wakeup_report = &wakeup_data;
	struct ieee80211_mgmt *mgmt = NULL;
	u8 *filter_item, *filter_item_ptr;
	struct bes_sk_param bes_sk_param;

	/* Check the validity of parameters */
	if (!rwnx_hw || !rwnx_vif || !skb)
		return;

	/* Check whether to parse */
	if (!bes_wowlan_wakeup_source(rwnx_hw))
		return;

	/* Init wakeup_data */
	memset(&wakeup_data, 0, sizeof(wakeup_data));
	wakeup_data.pattern_idx = -1;

	/* Get the real skb data */
	filter_item = (u8 *)kmalloc(skb->len, GFP_KERNEL);
	if (!filter_item) {
		bes_err(BES_DBG_WOWLAN, "kmalloc error. %s\n", __func__);
		return;
	}
	bes_sk_param.param_ptr = (void *)&(filter_item[0]);
	bes_sk_param.offset = 0;
	bes_sk_param.size = skb->len;
	filter_item_ptr = (u8 *)bes_common_get_param_ptr((struct sk_buff *)skb, &bes_sk_param, FRAME_STATUS_RX);
	if (!filter_item_ptr) {
		bes_err(BES_DBG_WOWLAN, "Fail to get filter_item_ptr. %s\n", __func__);
		goto out;
	}

	/* Set wakeup packet */
	wakeup_data.packet_len = skb->len;
	wakeup_data.packet_80211 = true;
	wakeup_data.packet_present_len = wakeup_data.packet_len;
	wakeup_data.packet = filter_item_ptr;

	mgmt = (struct ieee80211_mgmt *)filter_item_ptr;
	bes_info(BES_DBG_WOWLAN, "Host may be waked by a mgmt packet. fctl:0x%04x\n", mgmt->frame_control);
	bes_info_dump(BES_DBG_WOWLAN, "skb->data: ", filter_item_ptr, skb->len);

	/* Report wakeup reason */
	cfg80211_report_wowlan_wakeup(&rwnx_vif->wdev, wakeup_report, GFP_KERNEL);

out:
	/* Free memory */
	kfree(filter_item);
}

void bes_wowlan_parse_msg(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	struct cfg80211_wowlan_wakeup wakeup_data;
	struct cfg80211_wowlan_wakeup *wakeup_report = &wakeup_data;
	u8 vif_idx = 0;
	struct rwnx_vif *rwnx_vif = NULL;
	static u16 msg_id = 0;
	struct cfg80211_wowlan_nd_info net_detect;

	/* Check the validity of parameters */
	if (!rwnx_hw || !msg)
		return;

	/* Get wakeup reason and check whether to report */
	if (bes_wowlan_wakeup_source(rwnx_hw)) {
		if (msg->id == SM_DISCONNECT_IND ||
		    msg->id == MM_SCHED_SCAN_RESULT_IND || msg->id == SCANU_RESULT_IND)
			goto report;

		// Record msg->id, calculate the overflow time and update parse_step
		msg_id = msg->id;
		rwnx_hw->wowlan.parse_msg_tjiff = jiffies;
		rwnx_hw->wowlan.parse_msg_step = PARSE_MSG_STEP_SET;
	} else if (rwnx_hw->wowlan.parse_msg_step != PARSE_MSG_STEP_NONE) {
		if (time_after(jiffies, rwnx_hw->wowlan.parse_msg_tjiff + msecs_to_jiffies(10))) {
			rwnx_hw->wowlan.parse_msg_step = PARSE_MSG_STEP_NONE;
			bes_info(BES_DBG_WOWLAN, "Host may be waked by an unkown msg. msg:%d(%s)\n",
				msg_id, RWNX_ID2STR(msg_id));
		} else if (msg->id == SM_DISCONNECT_IND) {
			rwnx_hw->wowlan.parse_msg_step = PARSE_MSG_STEP_NONE;
			goto report;
		}
	}

	return;

report:
	/* Init wakeup_data */
	memset(&wakeup_data, 0, sizeof(wakeup_data));
	wakeup_data.pattern_idx = -1;

	/* Set and Report wakeup reason */
	switch (msg->id) {
		case SM_DISCONNECT_IND:
			vif_idx = ((struct sm_disconnect_ind *)msg->param)->vif_idx;
			rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);
			if (!rwnx_vif) {
				bes_info(BES_DBG_WOWLAN, "%s Fail to get vif for disconnect.vif_idx:%d\n", __func__, vif_idx);
				break;
			}

			wakeup_data.disconnect = true;

			bes_info(BES_DBG_WOWLAN, "Host may be waked by disconnect. interval:%dms\n",
					jiffies_to_msecs((jiffies >= rwnx_hw->wowlan.parse_msg_tjiff) ?
							 (jiffies - rwnx_hw->wowlan.parse_msg_tjiff) :
							 (ULONG_MAX - rwnx_hw->wowlan.parse_msg_tjiff + jiffies)));
			break;
		case MM_SCHED_SCAN_RESULT_IND:
		case SCANU_RESULT_IND:
			if (msg->id == MM_SCHED_SCAN_RESULT_IND)
				vif_idx = ((struct sched_scan_result_ind *)msg->param)->vif_idx;
			else
				vif_idx = ((struct scanu_result_ind *)msg->param)->inst_nbr;

			rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);
			if (!rwnx_vif) {
				bes_info(BES_DBG_WOWLAN, "%s Fail to get vif for net-detect.vif_idx:%d\n", __func__, vif_idx);
				break;
			}

			wakeup_data.net_detect = &net_detect;
			net_detect.n_matches = 0; /* zero means the driver can't provide match information */

			bes_info(BES_DBG_WOWLAN, "Host may be waked by net-detect.\n");
			break;
	}

	/* Report wakeup reason */
	if (rwnx_vif)
		cfg80211_report_wowlan_wakeup(&rwnx_vif->wdev, wakeup_report, GFP_KERNEL);
}

static int bes_wowlan_data_tx_process_ipv4_tcp(struct rwnx_vif *rwnx_vif, struct sk_buff *skb, char *buf)
{
	int len = 0;
	struct tcphdr tcphdr;
	struct tcphdr *tcphdr_ptr = NULL;
	struct bes_sk_param bes_sk_param;

	bes_sk_param.param_ptr = (void *)&tcphdr;
	bes_sk_param.offset = sizeof(struct ethhdr) + sizeof(struct iphdr);
	bes_sk_param.size = sizeof(struct tcphdr);
	tcphdr_ptr = (struct tcphdr *)bes_common_get_param_ptr(skb, &bes_sk_param, FRAME_STATUS_TX);

	if (!tcphdr_ptr) {
		len += snprintf(&buf[len], BES_WOWLAN_DATA_TX_DBG_BUF_MAX - len - 1,
				"%s DATA ethertype:ETH_P_IP protocol:IP_PROT_TCP",
				rwnx_vif->ndev->name);
	} else {
		len += snprintf(&buf[len], BES_WOWLAN_DATA_TX_DBG_BUF_MAX - len - 1,
				"%s DATA ethertype:ETH_P_IP protocol:IP_PROT_TCP port_src:%d port_dst:%d",
				rwnx_vif->ndev->name,
				tcphdr_ptr->source, tcphdr_ptr->dest);
	}

	return len;
}

static int bes_wowlan_data_tx_process_ipv4_udp(struct rwnx_vif *rwnx_vif, struct sk_buff *skb, char *buf)
{
	int len = 0;
	struct udphdr udphdr;
	struct udphdr *udphdr_ptr = NULL;
	struct bes_sk_param bes_sk_param;

	bes_sk_param.param_ptr = (void *)&udphdr;
	bes_sk_param.offset = sizeof(struct ethhdr) + sizeof(struct iphdr);
	bes_sk_param.size = sizeof(struct udphdr);
	udphdr_ptr = (struct udphdr *)bes_common_get_param_ptr(skb, &bes_sk_param, FRAME_STATUS_TX);

	if (!udphdr_ptr) {
		len += snprintf(&buf[len], BES_WOWLAN_DATA_TX_DBG_BUF_MAX - len - 1,
				"%s DATA ethertype:ETH_P_IP protocol:IP_PROT_UDP",
				rwnx_vif->ndev->name);
	} else {
		len += snprintf(&buf[len], BES_WOWLAN_DATA_TX_DBG_BUF_MAX - len - 1,
				"%s DATA ethertype:ETH_P_IP protocol:IP_PROT_UDP port_src:%d port_dst:%d",
				rwnx_vif->ndev->name,
				udphdr_ptr->source, udphdr_ptr->dest);
	}

	return len;
}

static int bes_wowlan_data_tx_process_ipv4(struct rwnx_vif *rwnx_vif, struct sk_buff *skb, char *buf)
{
	int len = 0;
	struct iphdr iphdr;
	struct iphdr *iphdr_ptr = NULL;
	struct bes_sk_param bes_sk_param;

	bes_sk_param.param_ptr = (void *)&iphdr;
	bes_sk_param.offset = sizeof(struct ethhdr);
	bes_sk_param.size = sizeof(struct iphdr);
	iphdr_ptr = (struct iphdr *)bes_common_get_param_ptr(skb, &bes_sk_param, FRAME_STATUS_TX);

	if (!iphdr_ptr) {
		len += snprintf(&buf[len], BES_WOWLAN_DATA_TX_DBG_BUF_MAX - len - 1,
				"%s DATA ethertype:ETH_P_IP",
				rwnx_vif->ndev->name);
		goto out;
	}

	switch (iphdr_ptr->protocol) {
		case IP_PROT_TCP:
			len += bes_wowlan_data_tx_process_ipv4_tcp(rwnx_vif, skb, buf);
			break;

		case IP_PROT_UDP:
			len += bes_wowlan_data_tx_process_ipv4_udp(rwnx_vif, skb, buf);
			break;

		default:
			len += snprintf(&buf[len], BES_WOWLAN_DATA_TX_DBG_BUF_MAX - len - 1,
				"%s DATA ethertype:ETH_P_IP protocol:%d",
				rwnx_vif->ndev->name,
				iphdr_ptr->protocol);
			break;
	}

out:
	return len;
}

static int bes_wowlan_data_tx_process_ipv6_tcp(struct rwnx_vif *rwnx_vif, struct sk_buff *skb, char *buf)
{
	int len = 0;
	struct tcphdr tcphdr;
	struct tcphdr *tcphdr_ptr = NULL;
	struct bes_sk_param bes_sk_param;

	bes_sk_param.param_ptr = (void *)&tcphdr;
	bes_sk_param.offset = sizeof(struct ethhdr) + sizeof(struct ipv6hdr);
	bes_sk_param.size = sizeof(struct tcphdr);
	tcphdr_ptr = (struct tcphdr *)bes_common_get_param_ptr(skb, &bes_sk_param, FRAME_STATUS_TX);

	if (!tcphdr_ptr) {
		len += snprintf(&buf[len], BES_WOWLAN_DATA_TX_DBG_BUF_MAX - len - 1,
				"%s DATA ethertype:ETH_P_IPV6 nexthdr:NEXTHDR_TCP",
				rwnx_vif->ndev->name);
	} else {
		len += snprintf(&buf[len], BES_WOWLAN_DATA_TX_DBG_BUF_MAX - len - 1,
				"%s DATA ethertype:ETH_P_IPV6 nexthdr:NEXTHDR_TCP port_src:%d port_dst:%d",
				rwnx_vif->ndev->name,
				tcphdr_ptr->source, tcphdr_ptr->dest);
	}

	return len;
}

static int bes_wowlan_data_tx_process_ipv6_udp(struct rwnx_vif *rwnx_vif, struct sk_buff *skb, char *buf)
{
	int len = 0;
	struct udphdr udphdr;
	struct udphdr *udphdr_ptr = NULL;
	struct bes_sk_param bes_sk_param;

	bes_sk_param.param_ptr = (void *)&udphdr;
	bes_sk_param.offset = sizeof(struct ethhdr) + sizeof(struct ipv6hdr);
	bes_sk_param.size = sizeof(struct udphdr);
	udphdr_ptr = (struct udphdr *)bes_common_get_param_ptr(skb, &bes_sk_param, FRAME_STATUS_TX);
	if (!udphdr_ptr) {
		len += snprintf(&buf[len], BES_WOWLAN_DATA_TX_DBG_BUF_MAX - len - 1,
				"%s DATA ethertype:ETH_P_IPV6 nexthdr:NEXTHDR_UDP",
				rwnx_vif->ndev->name);
	} else {
		len += snprintf(&buf[len], BES_WOWLAN_DATA_TX_DBG_BUF_MAX - len - 1,
				"%s DATA ethertype:ETH_P_IPV6 nexthdr:NEXTHDR_TCP port_src:%d port_dst:%d",
				rwnx_vif->ndev->name,
				udphdr_ptr->source, udphdr_ptr->dest);
	}

	return len;
}

static int bes_wowlan_data_tx_process_ipv6(struct rwnx_vif *rwnx_vif, struct sk_buff *skb, char *buf)
{
	int len = 0;
	struct ipv6hdr ip6h;
	struct ipv6hdr *ip6h_ptr = NULL;
	struct bes_sk_param bes_sk_param;

	bes_sk_param.param_ptr = (void *)&ip6h;
	bes_sk_param.offset = sizeof(struct ethhdr);
	bes_sk_param.size = sizeof(struct ipv6hdr);
	ip6h_ptr = (struct ipv6hdr *)bes_common_get_param_ptr(skb, &bes_sk_param, FRAME_STATUS_TX);

	if (!ip6h_ptr) {
		len += snprintf(&buf[len], BES_WOWLAN_DATA_TX_DBG_BUF_MAX - len - 1,
				"%s DATA ethertype:ETH_P_IPV6",
				rwnx_vif->ndev->name);
		goto out;
	}

	switch (ip6h_ptr->nexthdr) {
		case NEXTHDR_TCP:
			len += bes_wowlan_data_tx_process_ipv6_tcp(rwnx_vif, skb, buf);
			break;

		case NEXTHDR_UDP:
			len += bes_wowlan_data_tx_process_ipv6_udp(rwnx_vif, skb, buf);
			break;

		default:
			len += snprintf(&buf[len], BES_WOWLAN_DATA_TX_DBG_BUF_MAX - len - 1,
				"%s DATA ethertype:ETH_P_IPV6 nexthdr:%d",
				rwnx_vif->ndev->name,
				ip6h_ptr->nexthdr);
			break;
	}

out:
	return len;
}

static int bes_wowlan_data_tx_process_ether_type(struct rwnx_vif *rwnx_vif, struct sk_buff *skb, char *buf)
{
	int len = 0;
	u16 ethertype = 0;
	struct ethhdr ethhdr;
	struct ethhdr *ethhdr_ptr = NULL;
	struct bes_sk_param bes_sk_param;
	struct rwnx_sw_txhdr *sw_txhdr = ((struct rwnx_txhdr *)skb->data)->sw_hdr;

	bes_sk_param.param_ptr = (void *)&ethhdr;
	bes_sk_param.offset = 0;
	bes_sk_param.size = sizeof(struct ethhdr);
	ethhdr_ptr = (struct ethhdr *)bes_common_get_param_ptr(skb, &bes_sk_param, FRAME_STATUS_TX);
	if (!ethhdr_ptr) {
		len += snprintf(&buf[len], BES_WOWLAN_DATA_TX_DBG_BUF_MAX - len - 1,
				"%s DATA", rwnx_vif->ndev->name);
		goto out;
	}

	ethertype = __be16_to_cpu(sw_txhdr->desc.api.host.ethertype);
	switch (ethertype) {
		case ETH_P_IP: /* Internet Protocol version 4 (IPv4) */
			len += bes_wowlan_data_tx_process_ipv4(rwnx_vif, skb, buf);
			break;

		case ETH_P_IPV6: /* Internet Protocol version 6 (IPv6)*/
			len += bes_wowlan_data_tx_process_ipv6(rwnx_vif, skb, buf);
			break;

		default:
			len += snprintf(&buf[len], BES_WOWLAN_DATA_TX_DBG_BUF_MAX - len - 1,
					"%s DATA ethertype:0x%04x",
					rwnx_vif->ndev->name, ethertype);
			break;
	}

out:
	return len;
}

void bes_wowlan_msg_tx_info_print(struct rwnx_cmd_a2emsg *cmd_a2e_msg)
{
	bes_info(BES_DBG_WOWLAN, "%s id:0x%x(%s)\n", __func__,
		 cmd_a2e_msg->id, RWNX_ID2STR(cmd_a2e_msg->id));
}

void bes_wowlan_data_tx_info_print(struct sk_buff *skb)
{
	int len = 0;
	char buf[BES_WOWLAN_DATA_TX_DBG_BUF_MAX];
	struct rwnx_sw_txhdr *sw_txhdr = ((struct rwnx_txhdr *)skb->data)->sw_hdr;
	struct txdesc_host *txdesc_host = &sw_txhdr->desc;
	struct rwnx_vif *rwnx_vif = sw_txhdr->rwnx_vif;

	if (!rwnx_vif) {
		bes_err(BES_DBG_MSG_RX, "%s ERROR: vif is not find\n", __func__);
		return;
	}

	if (txdesc_host->api.host.flags & TXU_CNTRL_MGMT) {
		struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)((u8 *)skb->data + RWNX_TX_HEADROOM);

		len += snprintf(&buf[len], BES_WOWLAN_DATA_TX_DBG_BUF_MAX - len - 1,
				"%s MGMT frame_control:0x%04x Da:[%pM] SN:%d",
				rwnx_vif->ndev->name, mgmt->frame_control, mgmt->da,
				IEEE80211_SEQ_TO_SN(mgmt->seq_ctrl));
	} else {
		len += bes_wowlan_data_tx_process_ether_type(rwnx_vif, skb, buf);
	}

	/* Print buf */
	if (len > 0)
		bes_info(BES_DBG_WOWLAN, "%s", buf);
}

void bes_wowlan_init(struct rwnx_hw *rwnx_hw)
{
	bes_custom_filter_init(rwnx_hw);
	rwnx_hw->wowlan.mode = WOWLAN_MODE_STANDARD;
	rwnx_hw->wowlan.wakeup_source = false;
	rwnx_hw->wowlan.parse_msg_step = PARSE_MSG_STEP_NONE;
	rwnx_hw->wowlan.parse_msg_tjiff = jiffies;
	init_completion(&rwnx_hw->wowlan.complete);
}

void bes_wowlan_deinit(struct rwnx_hw *rwnx_hw)
{
	bes_custom_filter_deinit(rwnx_hw);
	rwnx_hw->wowlan.mode = WOWLAN_MODE_STANDARD;
	rwnx_hw->wowlan.wakeup_source = false;
	rwnx_hw->wowlan.parse_msg_step = PARSE_MSG_STEP_NONE;
	rwnx_hw->wowlan.parse_msg_tjiff = jiffies;
}