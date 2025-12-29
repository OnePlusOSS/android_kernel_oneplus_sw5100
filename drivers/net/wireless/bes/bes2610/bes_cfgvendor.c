/*
 * @file bes_cfgvendor.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <net/cfg80211.h>
#include <net/netlink.h>
#include <linux/ctype.h>
#include <linux/module.h>
#include <linux/rtnetlink.h>
#include "rwnx_defs.h"
#include "bes_sysfs.h"
#include "bes_log.h"
#include "rwnx_version_gen.h"
#include "rwnx_msg_tx.h"
#include "bes_pwr.h"
#include "bes_linkinfo.h"
#include "bes_regd.h"

static const struct nla_policy mkeep_alive_attr_policy[MKEEP_ALIVE_ATTRIBUTE_MAX] = {
	[MKEEP_ALIVE_ATTRIBUTE_ID] = {.type = NLA_U8},
	[MKEEP_ALIVE_ATTRIBUTE_IP_PKT] = {.type = NLA_BINARY},
	[MKEEP_ALIVE_ATTRIBUTE_IP_PKT_LEN] = {.type = NLA_U16},
	[MKEEP_ALIVE_ATTRIBUTE_SRC_MAC_ADDR] = {.type = NLA_BINARY},
	[MKEEP_ALIVE_ATTRIBUTE_DST_MAC_ADDR] = {.type = NLA_BINARY},
	[MKEEP_ALIVE_ATTRIBUTE_PERIOD_MSEC] = {.type = NLA_U32},
};

#ifdef CONFIG_OPLUS_PLATFORM
const struct nla_policy andr_wifi_attr_policy[ANDR_WIFI_ATTRIBUTE_MAX] = {
	[ANDR_WIFI_ATTRIBUTE_NUM_FEATURE_SET] = { .type = NLA_U32 },
	[ANDR_WIFI_ATTRIBUTE_FEATURE_SET] = { .type = NLA_U32 },
	[ANDR_WIFI_ATTRIBUTE_PNO_RANDOM_MAC_OUI] = { .type = NLA_NUL_STRING, .len = 3 },
	[ANDR_WIFI_ATTRIBUTE_NODFS_SET] = { .type = NLA_U32 },
	[ANDR_WIFI_ATTRIBUTE_COUNTRY] = { .type = NLA_NUL_STRING, .len = 3 },
	[ANDR_WIFI_ATTRIBUTE_ND_OFFLOAD_VALUE] = { .type = NLA_U8 },
	[ANDR_WIFI_ATTRIBUTE_TCPACK_SUP_VALUE] = { .type = NLA_U32 },
	[ANDR_WIFI_ATTRIBUTE_LATENCY_MODE] = { .type = NLA_U32, .len = sizeof(u32) },
	[ANDR_WIFI_ATTRIBUTE_RANDOM_MAC] = { .type = NLA_U32 },
	[ANDR_WIFI_ATTRIBUTE_TX_POWER_SCENARIO] = { .type = NLA_S8 },
	[ANDR_WIFI_ATTRIBUTE_THERMAL_MITIGATION] = { .type = NLA_S8 },
	[ANDR_WIFI_ATTRIBUTE_THERMAL_COMPLETION_WINDOW] = { .type = NLA_U32 },
	[ANDR_WIFI_ATTRIBUTE_VOIP_MODE] = { .type = NLA_U32, .len = sizeof(u32) },
	[ANDR_WIFI_ATTRIBUTE_DTIM_MULTIPLIER] = { .type = NLA_U32, .len = sizeof(u32) },
};

const struct nla_policy logger_attr_policy[LOGGER_ATTRIBUTE_MAX] = {
	[LOGGER_ATTRIBUTE_DRIVER_VER] = { .type = NLA_BINARY },
	[LOGGER_ATTRIBUTE_FW_VER] = { .type = NLA_BINARY },
	[LOGGER_ATTRIBUTE_RING_ID] = { .type = NLA_U32 },
	[LOGGER_ATTRIBUTE_RING_NAME] = { .type = NLA_NUL_STRING },
	[LOGGER_ATTRIBUTE_RING_FLAGS] = { .type = NLA_U32 },
	[LOGGER_ATTRIBUTE_LOG_LEVEL] = { .type = NLA_U32 },
	[LOGGER_ATTRIBUTE_LOG_TIME_INTVAL] = { .type = NLA_U32 },
	[LOGGER_ATTRIBUTE_LOG_MIN_DATA_SIZE] = { .type = NLA_U32 },
	[LOGGER_ATTRIBUTE_FW_DUMP_LEN] = { .type = NLA_U32 },
	[LOGGER_ATTRIBUTE_FW_DUMP_DATA] = { .type = NLA_U64 },
	[LOGGER_ATTRIBUTE_FW_ERR_CODE] = { .type = NLA_U32 },
	[LOGGER_ATTRIBUTE_RING_DATA] = { .type = NLA_BINARY },
	[LOGGER_ATTRIBUTE_RING_STATUS] = { .type = NLA_BINARY },
	[LOGGER_ATTRIBUTE_RING_NUM] = { .type = NLA_U32 },
	[LOGGER_ATTRIBUTE_DRIVER_DUMP_LEN] = { .type = NLA_U32 },
	[LOGGER_ATTRIBUTE_DRIVER_DUMP_DATA] = { .type = NLA_BINARY },
	[LOGGER_ATTRIBUTE_PKT_FATE_NUM] = { .type = NLA_U32 },
	[LOGGER_ATTRIBUTE_PKT_FATE_DATA] = { .type = NLA_U64 },
	[LOGGER_ATTRIBUTE_HANG_REASON] = { .type = NLA_BINARY },
};
#else
static const struct nla_policy andr_wifi_attr_policy[ANDR_WIFI_ATTRIBUTE_MAX] = {
	[ANDR_WIFI_ATTRIBUTE_COUNTRY] = { .type = NLA_STRING },
};
#endif

static const struct nla_policy rssi_monitor_attr_policy[RSSI_MONITOR_ATTRIBUTE_MAX] = {
	[RSSI_MONITOR_ATTRIBUTE_MAX_RSSI] = { .type = NLA_U32 },
	[RSSI_MONITOR_ATTRIBUTE_MIN_RSSI] = { .type = NLA_U32 },
	[RSSI_MONITOR_ATTRIBUTE_START] = { .type = NLA_U32 },
};

#ifndef CONFIG_OPLUS_PLATFORM
static const struct nla_policy logger_attr_policy[LOGGER_ATTRIBUTE_MAX] = {
	[LOGGER_ATTRIBUTE_DRIVER_VER] = { .type = NLA_BINARY },
	[LOGGER_ATTRIBUTE_FW_VER] = { .type = NLA_BINARY },
	[LOGGER_ATTRIBUTE_RING_ID] = { .type = NLA_U32 },
	[LOGGER_ATTRIBUTE_RING_NAME] = { .type = NLA_NUL_STRING },
	[LOGGER_ATTRIBUTE_RING_FLAGS] = { .type = NLA_U32 },
	[LOGGER_ATTRIBUTE_LOG_LEVEL] = { .type = NLA_U32 },
	[LOGGER_ATTRIBUTE_LOG_TIME_INTVAL] = { .type = NLA_U32 },
	[LOGGER_ATTRIBUTE_LOG_MIN_DATA_SIZE] = { .type = NLA_U32 },
	[LOGGER_ATTRIBUTE_FW_DUMP_LEN] = { .type = NLA_U32 },
	[LOGGER_ATTRIBUTE_FW_DUMP_DATA] = { .type = NLA_U64 },
	[LOGGER_ATTRIBUTE_FW_ERR_CODE] = { .type = NLA_U32 },
	[LOGGER_ATTRIBUTE_RING_DATA] = { .type = NLA_BINARY },
	[LOGGER_ATTRIBUTE_RING_STATUS] = { .type = NLA_BINARY },
	[LOGGER_ATTRIBUTE_RING_NUM] = { .type = NLA_U32 },
	[LOGGER_ATTRIBUTE_DRIVER_DUMP_LEN] = { .type = NLA_U32 },
	[LOGGER_ATTRIBUTE_DRIVER_DUMP_DATA] = { .type = NLA_BINARY },
	[LOGGER_ATTRIBUTE_PKT_FATE_NUM] = { .type = NLA_U32 },
	[LOGGER_ATTRIBUTE_PKT_FATE_DATA] = { .type = NLA_U64 },
	[LOGGER_ATTRIBUTE_HANG_REASON] = { .type = NLA_BINARY },
};
#endif

static const struct nla_policy get_channel_attr_policy[GSCAN_ATTRIBUTE_MAX] = {
	[GSCAN_ATTRIBUTE_BAND] = { .type = NLA_U32 },
};

static const struct nla_policy rtt_attr_policy[RTT_ATTRIBUTE_MAX] = {
	[RTT_ATTRIBUTE_TARGET_CNT] = { .type = NLA_U8 },
	[RTT_ATTRIBUTE_TARGET_INFO] = { .type = NLA_NESTED },
	[RTT_ATTRIBUTE_TARGET_MAC] = { .type = NLA_BINARY },
	[RTT_ATTRIBUTE_TARGET_TYPE] = { .type = NLA_U8 },
	[RTT_ATTRIBUTE_TARGET_PEER] = { .type = NLA_U8 },
	[RTT_ATTRIBUTE_TARGET_CHAN] = { .type = NLA_BINARY },
	[RTT_ATTRIBUTE_TARGET_PERIOD] = { .type = NLA_U32 },
	[RTT_ATTRIBUTE_TARGET_NUM_BURST] = { .type = NLA_U32 },
	[RTT_ATTRIBUTE_TARGET_NUM_FTM_BURST] = { .type = NLA_U32 },
	[RTT_ATTRIBUTE_TARGET_NUM_RETRY_FTM] = { .type = NLA_U32 },
	[RTT_ATTRIBUTE_TARGET_NUM_RETRY_FTMR] = { .type = NLA_U32 },
	[RTT_ATTRIBUTE_TARGET_LCI] = { .type = NLA_U8 },
	[RTT_ATTRIBUTE_TARGET_LCR] = { .type = NLA_U8 },
	[RTT_ATTRIBUTE_TARGET_BURST_DURATION] = { .type = NLA_U32 },
	[RTT_ATTRIBUTE_TARGET_PREAMBLE] = { .type = NLA_U8 },
	[RTT_ATTRIBUTE_TARGET_BW] = { .type = NLA_U8 },
	[RTT_ATTRIBUTE_RESULTS_COMPLETE] = { .type = NLA_U32 },
	[RTT_ATTRIBUTE_RESULTS_PER_TARGET] = { .type = NLA_NESTED },
	[RTT_ATTRIBUTE_RESULT_CNT] = { .type = NLA_U32 },
	[RTT_ATTRIBUTE_RESULT] = { .type = NLA_BINARY },
};

#define BES_CFGVENDOR_EVENT_MEMBER(evt) [evt] = {OUI_GOOGLE, evt}
#define RSSI_MONITOR_EVT_VERSION	1

struct rssi_monitor_evt {
	u8 version;
	s8 cur_rssi;
	u8 bssid[ETH_ALEN];
};

static struct wifi_ring_buffer_status ring_buffer[] = {
	{
		.name               = "bes2610_ring_buffer0",
		.flags              = 0,
		.ring_id            = 0,
		.verbose_level      = 0,
		.written_bytes      = 0,
		.read_bytes         = 0,
		.written_records    = 0,
	},
};

static int bes_cfgvendor_send_cmd_reply(struct wiphy *wiphy,
					struct net_device *dev, const void *data, int len)
{
	struct sk_buff *skb;

	/* Alloc the SKB for vendor_reply */
	skb = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, len);
	if (unlikely(!skb)) {
		bes_err(BES_DBG_VENDOR_CMD, "skb alloc failed\n");
		return -ENOMEM;
	}

	/* Push the data to the skb */
	nla_put_nohdr(skb, len, data);

	return cfg80211_vendor_cmd_reply(skb);
}

static struct sk_buff *bes_cfg80211_vendor_event_alloc(struct wiphy *wiphy,
						       struct wireless_dev *wdev, int len,
						       int event_id, gfp_t gfp)
{
	struct sk_buff *skb;

#if (LINUX_VERSION_CODE < KERNEL_VERSION(4, 1, 0))
	skb = cfg80211_vendor_event_alloc(wiphy, NLA_ALIGN(len), event_id, gfp);
#else
	skb = cfg80211_vendor_event_alloc(wiphy, wdev, NLA_ALIGN(len), event_id, gfp);
#endif

	return skb;
}

static int bes_set_country_code(struct wiphy *wiphy,
				struct wireless_dev *wdev,
				const void *data, int len)
{
#define CNTRY_BUF_SZ	4	/* Country string is 3 bytes + NULL */
	int rem, type;
	char country_code[CNTRY_BUF_SZ] = {0};
	const struct nlattr *iter;
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);

	nla_for_each_attr(iter, data, len, rem) {
		type = nla_type(iter);
		switch (type) {
			case ANDR_WIFI_ATTRIBUTE_COUNTRY:
				memcpy(country_code, nla_data(iter), min(nla_len(iter),
				       CNTRY_BUF_SZ));
				break;
			default:
				bes_err(BES_DBG_VENDOR_CMD, "%s(%d), Unknown type: %d\n",
					__func__, __LINE__, type);
				return -EINVAL;
		}
	}

	/* check whether the country is valid or not */
	if(!isalpha(country_code[0]) ||
	   !isalpha(country_code[1])) {
		return -EINVAL;
	}

	if (rwnx_hw->mod_params->custregd) {
		bes_regd_update_custregd(rwnx_hw, wiphy, country_code);

		return 0;
	} else {
		/* notify cfg80211 to update database */
		return regulatory_hint(wiphy, country_code);
	}
}

static int bes_cfgvendor_gscan_get_capabilities(struct wiphy *wiphy,
						struct wireless_dev *wdev,
						const void *data, int len)
{
	int err = 0;

	// TODO:do something
	return err;
}

static int bes_cfgvendor_set_scan_cfg(struct wiphy *wiphy,
				      struct wireless_dev *wdev,
				      const void *data, int len)
{
	int err = 0, rem, type;
	const struct nlattr *iter;

	// TODO:do something

	nla_for_each_attr(iter, data, len, rem) {
		type = nla_type(iter);
		switch (type) {
			case GSCAN_ATTRIBUTE_BASE_PERIOD:
				break;
			case GSCAN_ATTRIBUTE_NUM_BUCKETS:
				break;
			default:
				bes_err(BES_DBG_VENDOR_CMD, "%s(%d), Unknown type: %d\n",
					__func__, __LINE__, type);
				return -EINVAL;
		}
	}
	return err;
}

static int bes_cfgvendor_set_batch_scan_cfg(struct wiphy *wiphy,
					    struct wireless_dev *wdev,
					    const void *data, int len)
{
	int err = 0, rem, type;
	const struct nlattr *iter;

	// TODO:do something

	nla_for_each_attr(iter, data, len, rem) {
		type = nla_type(iter);
		switch (type) {
			case GSCAN_ATTRIBUTE_NUM_AP_PER_SCAN:
				break;
			case GSCAN_ATTRIBUTE_NUM_SCANS_TO_CACHE:
				break;
			case GSCAN_ATTRIBUTE_REPORT_THRESHOLD:
				break;
			default:
				bes_err(BES_DBG_VENDOR_CMD, "%s(%d), Unknown type: %d\n",
					__func__, __LINE__, type);
				return -EINVAL;
		}
	}
	return err;
}

static int bes_cfgvendor_initiate_gscan(struct wiphy *wiphy,
					struct wireless_dev *wdev,
					const void *data, int len)
{
	int err = 0, rem, type;
	const struct nlattr *iter;

	// TODO:do something

	nla_for_each_attr(iter, data, len, rem) {
		type = nla_type(iter);
		switch (type) {
			case GSCAN_ATTRIBUTE_ENABLE_FEATURE:
				break;
			case GSCAN_ATTRIBUTE_FLUSH_FEATURE:
				break;
			default:
				bes_err(BES_DBG_VENDOR_CMD, "%s(%d), Unknown type: %d\n",
					__func__, __LINE__, type);
				return -EINVAL;
		}
	}
	return err;
}

static int bes_cfgvendor_enable_full_scan_result(struct wiphy *wiphy,
						 struct wireless_dev *wdev,
						 const void *data, int len)
{
	int err = 0, rem, type;
	const struct nlattr *iter;

	// TODO:do something

	nla_for_each_attr(iter, data, len, rem) {
		type = nla_type(iter);
		switch (type) {
			case GSCAN_ATTRIBUTE_ENABLE_FULL_SCAN_RESULTS:
				break;
			default:
				bes_err(BES_DBG_VENDOR_CMD, "%s(%d), Unknown type: %d\n",
					__func__, __LINE__, type);
				return -EINVAL;
		}
	}
	return err;
}

static int bes_cfgvendor_hotlist_cfg(struct wiphy *wiphy,
				     struct wireless_dev *wdev,
				     const void *data, int len)
{
	int err = 0, rem, type;
	const struct nlattr *iter;

	// TODO:do something

	nla_for_each_attr(iter, data, len, rem) {
		type = nla_type(iter);
		switch (type) {
			case GSCAN_ATTRIBUTE_HOTLIST_BSSIDS:
				break;
			case GSCAN_ATTRIBUTE_BSSID:
				break;
			case GSCAN_ATTRIBUTE_RSSI_LOW:
				break;
			case GSCAN_ATTRIBUTE_RSSI_HIGH:
				break;
			default:
				bes_err(BES_DBG_VENDOR_CMD, "%s(%d), Unknown type: %d\n",
					__func__, __LINE__, type);
				return -EINVAL;
		}
	}
	return err;
}

static int bes_cfgvendor_significant_change_cfg(struct wiphy *wiphy,
						struct wireless_dev *wdev,
						const void *data, int len)
{
	int err = 0, rem, type;
	const struct nlattr *iter;

	// TODO:do something

	nla_for_each_attr(iter, data, len, rem) {
		type = nla_type(iter);
		switch (type) {
			case GSCAN_ATTRIBUTE_SIGNIFICANT_CHANGE_FLUSH:
				break;
			case GSCAN_ATTRIBUTE_RSSI_SAMPLE_SIZE:
				break;
			case GSCAN_ATTRIBUTE_LOST_AP_SAMPLE_SIZE:
				break;
			case GSCAN_ATTRIBUTE_MIN_BREACHING:
				break;
			case GSCAN_ATTRIBUTE_SIGNIFICANT_CHANGE_BSSIDS:
				break;
			default:
				bes_err(BES_DBG_VENDOR_CMD, "%s(%d), Unknown type: %d\n",
					__func__, __LINE__, type);
				return -EINVAL;
			}
	}
	return err;
}

static int bes_cfgvendor_gscan_get_batch_results(struct wiphy *wiphy,
						 struct wireless_dev *wdev,
						 const void *data, int len)
{
	int err = 0;

	// TODO:do something
	return err;
}

static int bes_cfgvendor_rtt_set_config(struct wiphy *wiphy,
					struct wireless_dev *wdev,
					const void *data, int len)
{
	int err = 0, rem, type;
	const struct nlattr *iter;

	// TODO:do something

	nla_for_each_attr(iter, data, len, rem) {
		type = nla_type(iter);
		switch (type) {
			case RTT_ATTRIBUTE_TARGET_CNT:
				break;
			case RTT_ATTRIBUTE_TARGET_INFO:
				break;
			default:
				bes_err(BES_DBG_VENDOR_CMD, "%s(%d), Unknown type: %d\n",
					__func__, __LINE__, type);
				return -EINVAL;
		}
	}
	return err;
}

static int bes_cfgvendor_rtt_cancel_config(struct wiphy *wiphy,
					   struct wireless_dev *wdev,
					   const void *data, int len)
{
	int err = 0, rem, type;
	const struct nlattr *iter;

	// TODO:do something

	nla_for_each_attr(iter, data, len, rem) {
		type = nla_type(iter);
		switch (type) {
			case RTT_ATTRIBUTE_TARGET_CNT:
				break;
			case RTT_ATTRIBUTE_TARGET_MAC:
				break;
			default:
				bes_err(BES_DBG_VENDOR_CMD, "%s(%d), Unknown type: %d\n",
					__func__, __LINE__, type);
				return -EINVAL;
		}
	}
	return err;
}

static int bes_cfgvendor_rtt_get_capability(struct wiphy *wiphy,
					    struct wireless_dev *wdev,
					    const void *data, int len)
{
	int err = 0;

	// TODO:do something
	return err;
}

static int bes_cfgvendor_lstats_get_info(struct wiphy *wiphy,
					 struct wireless_dev *wdev,
					 const void *data, int len)
{
	int i, ret = 0;
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct wifi_iface_stat *iface_stat;
	struct wifi_radio_stat *radio_stat;
	struct rwnx_vif *rwnx_vif;
	char *lstats;
	struct bes_peer_rate_num num_stats = {0};
	u32 alloc_size;

	bes_dbg(BES_DBG_VENDOR_CMD, RWNX_FN_ENTRY_STR);

	if (!wdev) {
		bes_err(BES_DBG_MSG_RX, "%s ERROR: wdev is NULL\n", __func__);
		return -EINVAL;
	}

	rwnx_vif = container_of(wdev, struct rwnx_vif, wdev);

	spin_lock_bh(&rwnx_hw->cb_lock);

	bes_get_sta_txrx_rate_nums(rwnx_vif, &num_stats);
	alloc_size = sizeof(*radio_stat) + sizeof(*iface_stat) +
		     num_stats.num_peers * sizeof(struct wifi_peer_info);

	for (i = 0; i < num_stats.num_peers; i++)
		alloc_size += num_stats.num_rate[i] * sizeof(struct wifi_rate_stat);

	lstats = kzalloc(alloc_size, GFP_ATOMIC);
	if (!lstats) {
		spin_unlock_bh(&rwnx_hw->cb_lock);
		bes_err(BES_DBG_VENDOR_CMD, "allocate lstats info buffer fail!\n");
		return -ENOMEM;
	}

	radio_stat = (struct wifi_radio_stat *)lstats;
	radio_stat->radio = 1;

	iface_stat = (struct wifi_iface_stat *)(lstats + sizeof(*radio_stat));

	bes_link_layer_get_radio_stats(rwnx_hw, rwnx_vif, radio_stat);
	bes_link_layer_get_iface_stats(rwnx_hw, rwnx_vif, iface_stat);
	bes_link_layer_get_peer_info(rwnx_vif, iface_stat, &num_stats);

	spin_unlock_bh(&rwnx_hw->cb_lock);

	ret = bes_cfgvendor_send_cmd_reply(wiphy, wdev_to_ndev(wdev), lstats, alloc_size);
	if (ret)
		bes_err(BES_DBG_VENDOR_CMD, "%s, reply failed ret:%d\n", __func__, ret);

	kfree(lstats);

	return ret;
}

static int bes_cfgvendor_set_rssi_monitor(struct wiphy *wiphy,
					  struct wireless_dev *wdev,
					  const void *data, int len)
{
	int rem, type;
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	const struct nlattr *attr;
	struct rwnx_vif *rwnx_vif;
	int ret = 0;
	struct bes_cfgvendor_rssi_monitor *rssi_monitor;

	if (!wdev) {
		bes_err(BES_DBG_MSG_RX, "%s ERROR: wdev is NULL\n", __func__);
		return -EINVAL;
	}

	rwnx_vif = container_of(wdev, struct rwnx_vif, wdev);

	rssi_monitor = &rwnx_vif->lstats_vif.rssi_monitor;

	if (wdev->iftype != NL80211_IFTYPE_STATION &&
	    wdev->iftype != NL80211_IFTYPE_P2P_CLIENT)
		return -EOPNOTSUPP;

	nla_for_each_attr(attr, data, len, rem) {
		type = nla_type(attr);
		switch (type) {
		case RSSI_MONITOR_ATTRIBUTE_MAX_RSSI:
			rssi_monitor->rssi_max = nla_get_s8(attr);
			break;
		case RSSI_MONITOR_ATTRIBUTE_MIN_RSSI:
			rssi_monitor->rssi_min = nla_get_s8(attr);
			break;
		case RSSI_MONITOR_ATTRIBUTE_START:
			rssi_monitor->enable = !!nla_get_u8(attr);
			break;
		default:
			bes_err(BES_DBG_VENDOR_CMD, "%s(%d), unknown type: %d\n",
				__func__, __LINE__, type);
			return -EINVAL;
		}
	}

	if (rssi_monitor->rssi_max > 0 || rssi_monitor->rssi_min > 0)
		return -EINVAL;

	if (rssi_monitor->rssi_max < rssi_monitor->rssi_min)
		return -EINVAL;

	if (rssi_monitor->enable) {
		int rssi_thold;
		u32 rssi_hyst;
		u8 vif_index = rwnx_vif->vif_index;

		rssi_thold = (rssi_monitor->rssi_max + rssi_monitor->rssi_min) / 2;
		rssi_hyst = rssi_monitor->rssi_max - rssi_thold;
		if (vif_index < NX_ITF_MAX) {
			ret = rwnx_send_cfg_rssi_req(rwnx_hw, vif_index, rssi_thold, rssi_hyst);
		}
	}

	return ret;
}

void bes_cfgvendor_rssi_monitor_event(struct rwnx_vif *rwnx_vif, s8 rssi)
{
	struct wiphy *wiphy = rwnx_vif->rwnx_hw->wiphy;
	struct wireless_dev *wdev = &rwnx_vif->wdev;
	struct rssi_monitor_evt rssi_evt;
	struct sk_buff *skb;
	gfp_t kflags;
	struct bes_cfgvendor_rssi_monitor *rssi_monitor = &rwnx_vif->lstats_vif.rssi_monitor;

	if (!rssi_monitor->enable)
		return;

	if (rssi >= rssi_monitor->rssi_min && rssi <= rssi_monitor->rssi_max)
		return;

	kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	skb = bes_cfg80211_vendor_event_alloc(wiphy, wdev, (int)sizeof(rssi_evt),
					      GOOGLE_RSSI_MONITOR_EVENT, kflags);
	if (!skb)
		return;

	memset(&rssi_evt, 0, sizeof(rssi_evt));
	rssi_evt.version = RSSI_MONITOR_EVT_VERSION;
	rssi_evt.cur_rssi = rssi;
	ether_addr_copy(rssi_evt.bssid, wdev->address);

	if (nla_append(skb, sizeof(rssi_evt), &rssi_evt))
		goto nla_put_failure;

	cfg80211_vendor_event(skb, kflags);

	return;

nla_put_failure:
	kfree_skb(skb);
	return;
}

static int bes_cfgvendor_logger_start_logging(struct wiphy *wiphy,
					      struct wireless_dev *wdev,
					      const void *data, int len)
{
	int ret = 0, rem, type, intval, size, i;
	const struct nlattr *iter;
	struct wifi_ring_buffer_status rb = {0};

	nla_for_each_attr(iter, data, len, rem) {
		type = nla_type(iter);
		switch (type) {
			case LOGGER_ATTRIBUTE_LOG_LEVEL:
				rb.verbose_level = nla_get_u32(iter);
				break;
			case LOGGER_ATTRIBUTE_RING_FLAGS:
				rb.flags = nla_get_u32(iter);
				break;
			case LOGGER_ATTRIBUTE_LOG_TIME_INTVAL:
				intval = nla_get_u32(iter);
				break;
			case LOGGER_ATTRIBUTE_LOG_MIN_DATA_SIZE:
				size = nla_get_u32(iter);
				break;
			case LOGGER_ATTRIBUTE_RING_NAME:
				strncpy(rb.name, nla_data(iter), sizeof(rb.name) - 1);
				rb.name[sizeof(rb.name) - 1] = '\0';
				break;
			default:
				bes_err(BES_DBG_VENDOR_CMD, "%s(%d), Unknown type: %d\n",
					__func__, __LINE__, type);
				return -EINVAL;
		}
	}

	ret = -EINVAL;
	for (i = 0; i < sizeof(ring_buffer) / sizeof(ring_buffer[0]); i++) {
		if (strcmp(rb.name, ring_buffer[i].name) == 0) {
			ret = 0;
			break;
		}
	}

	/* TODO
	 * Add handle in the future
	 */

	return ret;
}

static int bes_cfgvendor_logger_get_feature(struct wiphy *wiphy,
					    struct wireless_dev *wdev,
					    const void *data, int len)
{
	int ret;
	struct sk_buff *reply;
	uint32_t feature = 0, payload;

	payload = sizeof(feature);
	reply = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, payload);

	if (!reply)
		return -ENOMEM;

	feature |= WIFI_LOGGER_MEMORY_DUMP_SUPPORTED;
	feature |= WIFI_LOGGER_CONNECT_EVENT_SUPPORTED;

	/*vts will test wake reason state function*/
	feature |= WIFI_LOGGER_WAKE_LOCK_SUPPORTED;

	if (nla_put_u32(reply, ANDR_WIFI_ATTRIBUTE_NUM_FEATURE_SET, feature)) {
		wiphy_err(wiphy, "put skb u32 failed\n");
		goto out_put_fail;
	}

	ret = cfg80211_vendor_cmd_reply(reply);
	if (ret)
		wiphy_err(wiphy, "reply cmd error\n");

	return ret;

out_put_fail:
	kfree_skb(reply);
	return -EMSGSIZE;
}

static int bes_cfgvendor_gscan_get_channel_list(struct wiphy *wiphy,
			   struct wireless_dev *wdev, const void *data, int len)
{
	int ret = 0, rem, type;
	const struct nlattr *iter;
	struct sk_buff *reply;
	int num_channels = 0;
	int *channel_list = NULL;
	int payload;
	int i = 0;
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct ieee80211_supported_band *bes_band_2GHz = rwnx_hw->wiphy->bands[NL80211_BAND_2GHZ];
	struct ieee80211_supported_band *bes_band_5GHz = rwnx_hw->wiphy->bands[NL80211_BAND_5GHZ];

	num_channels += bes_band_2GHz->n_channels;
	num_channels += bes_band_5GHz->n_channels;

	channel_list = (int *)kzalloc(sizeof(int) * num_channels, GFP_KERNEL);
	if (!channel_list)
		return -ENOMEM;

	for (i = 0; i < bes_band_2GHz->n_channels; i++){
		channel_list[i] = bes_band_2GHz->channels[i].center_freq;
	}

	for (; i < num_channels; i++){
		channel_list[i] = bes_band_5GHz->channels[i].center_freq;
	}


	payload = sizeof(num_channels) + sizeof(int) * num_channels + 4;

	nla_for_each_attr(iter, data, len, rem) {
		type = nla_type(iter);
		switch (type) {
			case GSCAN_ATTRIBUTE_BAND:
				reply = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, payload);

				if (!reply) {
					ret = -ENOMEM;
					goto out;
				}

				if (nla_put_u32(reply, GSCAN_ATTRIBUTE_NUM_CHANNELS, num_channels))
					goto out_put_fail;

				if (nla_put(reply, GSCAN_ATTRIBUTE_CHANNEL_LIST,
					    sizeof(int) * num_channels, channel_list)) {
					goto out_put_fail;
				}

				ret = cfg80211_vendor_cmd_reply(reply);
				if (ret)
					wiphy_err(wiphy, "%s reply cmd error\n", __func__);
				break;
			default:
				bes_err(BES_DBG_VENDOR_CMD, "%s(%d), Unknown type: %d\n",
					__func__, __LINE__, type);
				ret = -EINVAL;
				goto out;
		}
	}

out:
	kfree(channel_list);
	return ret;

out_put_fail:
	kfree(channel_list);
	kfree_skb(reply);
	return -EMSGSIZE;
}

static int bes_cfgvendor_logger_get_version(struct wiphy *wiphy,
					    struct wireless_dev *wdev,
					    const void *data, int len)
{
	int ret = 0, rem, type;
	int buf_len = 256;
	char *buf_ptr;
	const struct nlattr *iter;
	gfp_t kflags;
	size_t vers_len;

	kflags = in_atomic() ? GFP_ATOMIC : GFP_KERNEL;
	buf_ptr = kzalloc(buf_len, kflags);
	if (!buf_ptr) {
		bes_err(BES_DBG_VENDOR_CMD, "failed to allocate the buffer for version\n");
		ret = -ENOMEM;
		goto exit;
	}
	nla_for_each_attr(iter, data, len, rem) {
		type = nla_type(iter);
		switch (type) {
			case LOGGER_ATTRIBUTE_DRIVER_VER:
				vers_len = sizeof(RWNX_VERS_BANNER) < buf_len ? sizeof(RWNX_VERS_BANNER) : buf_len - 1;
				strncpy(buf_ptr, RWNX_VERS_BANNER, vers_len);
				buf_ptr[vers_len] = '\0';
				break;
			case LOGGER_ATTRIBUTE_FW_VER:
				vers_len = strlen(bes_sysfs_get_fw_ver()) < buf_len ? strlen(bes_sysfs_get_fw_ver()) : buf_len - 1;
				strncpy(buf_ptr, bes_sysfs_get_fw_ver(), vers_len);
				buf_ptr[vers_len] = '\0';
				break;
			default:
				bes_err(BES_DBG_VENDOR_CMD, "%s(%d), Unknown type: %d\n",
					__func__, __LINE__, type);
				ret = -EINVAL;
				goto exit;
		}
	}
	if (ret < 0) {
		bes_err(BES_DBG_VENDOR_CMD, "failed to get the version %d\n", ret);
		goto exit;
	}

	ret =  bes_cfgvendor_send_cmd_reply(wiphy, wdev_to_ndev(wdev), buf_ptr, strlen(buf_ptr));
exit:
	kfree(buf_ptr);
	return ret;
}

static int bes_cfgvendor_logger_get_ring_status(struct wiphy *wiphy,
						struct wireless_dev *wdev,
						const void *data, int len)
{
	int ret;
	struct sk_buff *reply;
	uint32_t payload;
	uint32_t ring_buffer_nums = sizeof(ring_buffer) / sizeof(ring_buffer[0]);

	payload = sizeof(ring_buffer_nums) + sizeof(ring_buffer);
	reply = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, payload);

	if (!reply)
		return -ENOMEM;

	if (nla_put_u32(reply, LOGGER_ATTRIBUTE_RING_NUM, ring_buffer_nums)) {
		wiphy_err(wiphy, "put skb u32 failed\n");
		goto out_put_fail;
	}

	if (nla_put(reply, LOGGER_ATTRIBUTE_RING_STATUS, sizeof(ring_buffer), ring_buffer)) {
		wiphy_err(wiphy, "put skb failed\n");
		goto out_put_fail;
	}

	ret = cfg80211_vendor_cmd_reply(reply);
	if (ret)
		wiphy_err(wiphy, "reply cmd error\n");

	return ret;

out_put_fail:
	kfree_skb(reply);
	return -EMSGSIZE;
}

static int bes_cfgvendor_logger_get_ring_data(struct wiphy *wiphy,
					      struct wireless_dev *wdev,
					      const void *data, int len)
{
	int ret = 0, rem, type, i;
	const struct nlattr *iter;
	struct wifi_ring_buffer_status rb = {0};

	nla_for_each_attr(iter, data, len, rem) {
		type = nla_type(iter);
		switch (type) {
			case LOGGER_ATTRIBUTE_RING_NAME:
				strncpy(rb.name, nla_data(iter), sizeof(rb.name) - 1);
				rb.name[sizeof(rb.name) - 1] = '\0';
				break;
			default:
				bes_err(BES_DBG_VENDOR_CMD, "%s(%d), Unknown type: %d\n",
					__func__, __LINE__, type);
				return -EINVAL;
		}
	}

	ret = -EINVAL;
	for (i = 0; i < sizeof(ring_buffer) / sizeof(ring_buffer[0]); i++) {
		if (strcmp(rb.name, ring_buffer[i].name) == 0) {
			ret = 0;
			break;
		}
	}

	/* TODO
	 * Add handle in the future
	 */

	return ret;
}

static int bes_cfgvendor_logger_get_firmware_memory_dump(struct wiphy *wiphy,
							 struct wireless_dev *wdev,
							 const void *data, int len)
{
	int err = 0;

	// TODO:do something
	return err;
}

static int bes_cfgvendor_logger_start_pkt_fate_monitoring(struct wiphy *wiphy,
							  struct wireless_dev *wdev,
							  const void *data, int len)
{
	int err = 0;

	// TODO:do something
	return err;
}

static int bes_cfgvendor_logger_get_tx_pkt_fates(struct wiphy *wiphy,
						 struct wireless_dev *wdev,
						 const void *data, int len)
{
	int err = 0;

	// TODO:do something
	return err;
}

static int bes_cfgvendor_logger_get_rx_pkt_fates(struct wiphy *wiphy,
						 struct wireless_dev *wdev,
						 const void *data, int len)
{
	int err = 0;

	// TODO:do something
	return err;
}

static int bes_cfgvendor_set_rand_mac_oui(struct wiphy *wiphy,
					  struct wireless_dev *wdev,
					  const void *data, int len)
{
	int err = 0;

	int type, mac_len;
	u8 pno_random_mac_oui[3];
	// TODO:do something

	type = nla_type(data);
	mac_len = nla_len(data);
	if (mac_len != 3) {
	// TODO:do something
		return -1;
	}

	if (type == ANDR_WIFI_ATTRIBUTE_PNO_RANDOM_MAC_OUI) {
		memcpy(pno_random_mac_oui, nla_data(data), 3);
		print_hex_dump(KERN_DEBUG, "pno_random_mac_oui: ",
			       DUMP_PREFIX_OFFSET, 16, 1, pno_random_mac_oui, 3, 1);

 	// TODO:do something
	} else {
	// TODO:do something
		err = -1;
	}
	return err;
}

static int bes_cfgvendor_set_nodfs_flag(struct wiphy *wiphy,
					struct wireless_dev *wdev,
					const void *data, int len)
{
	int err = 0;
	int type;
	u32 nodfs = 0;

	// TODO:do something

	type = nla_type(data);
	if (type == ANDR_WIFI_ATTRIBUTE_NODFS_SET) {
		nodfs = nla_get_u32(data);
	} else {
		err = -EINVAL;
	}

	// TODO:do something
	return err;
}

static int bes_cfgvendor_set_nd_offload(struct wiphy *wiphy,
					struct wireless_dev *wdev,
					const void *data, int len)
{
	int err = 0;
	int type;
	u8 nd_en = 0;

	// TODO:do something

	type = nla_type(data);
	if (type == ANDR_WIFI_ATTRIBUTE_ND_OFFLOAD_VALUE) {
		nd_en = nla_get_u8(data);
	/* ND has been enabled when wow is enabled */
	} else {
		err = -EINVAL;
	}
	return err;
}

static int bes_cfgvendor_get_feature_set(struct wiphy *wiphy,
					 struct wireless_dev *wdev,
					 const void *data, int len)
{
	int ret;
	struct sk_buff *reply;
	uint32_t feature = 0, payload;

	payload = sizeof(feature);
	reply = cfg80211_vendor_cmd_alloc_reply_skb(wiphy, payload);

	if (!reply)
		return -ENOMEM;

	/* TODO
	 * Add handle in the future!
	 */
	/*bit 1:Basic infrastructure mode*/
	if (wiphy->interface_modes & BIT(NL80211_IFTYPE_STATION))
		feature |= WIFI_FEATURE_INFRA;

	/*bit 2:Support for 5 GHz Band*/
	feature |= WIFI_FEATURE_INFRA_5G;

	/*bit3:HOTSPOT is a supplicant feature, enable it by default*/
	feature |= WIFI_FEATURE_HOTSPOT;

	/*bit 4:P2P*/
	if ((wiphy->interface_modes & BIT(NL80211_IFTYPE_P2P_CLIENT)) &&
		(wiphy->interface_modes & BIT(NL80211_IFTYPE_P2P_GO)))
		feature |= WIFI_FEATURE_P2P;

	/*bit 5:soft AP feature supported*/
	if (wiphy->interface_modes & BIT(NL80211_IFTYPE_AP))
		feature |= WIFI_FEATURE_SOFT_AP;

	/*bit 18:WiFi Logger*/
	feature |= WIFI_FEATURE_LOGGER;

	/*bit 21:WiFi mkeep_alive*/
	feature |= WIFI_FEATURE_MKEEP_ALIVE;

	if (nla_put_u32(reply, ANDR_WIFI_ATTRIBUTE_NUM_FEATURE_SET, feature)) {
		wiphy_err(wiphy, "%s put u32 error\n", __func__);
		goto out_put_fail;
	}

	ret = cfg80211_vendor_cmd_reply(reply);
	if (ret)
		wiphy_err(wiphy, "%s reply cmd error\n", __func__);

	return ret;

out_put_fail:
	kfree_skb(reply);
	return -EMSGSIZE;
}

static int bes_cfgvendor_get_feature_set_matrix(struct wiphy *wiphy,
						struct wireless_dev *wdev,
						const void *data, int len)
{
	int err = 0;
	struct sk_buff *skb = NULL;
	int *reply = NULL;
	int num = 0;
	int i = 0;

 	// TODO:do something

	if (!reply) {
	// TODO:do something
		err = -EINVAL;
		return err;
	}

	/* Alloc the SKB for vendor_event */
	// TODO:do something
	if (unlikely(!skb)) {
	// TODO:do something
		err = -ENOMEM;
		goto exit;
	}

	nla_put_u32(skb, ANDR_WIFI_ATTRIBUTE_NUM_FEATURE_SET, num);
	for (i = 0; i < num; i++)
		nla_put_u32(skb, ANDR_WIFI_ATTRIBUTE_FEATURE_SET, reply[i]);

	// TODO:do something

	if (unlikely(err)) {
		// TODO:do something
	}

exit:
	// TODO:do something
	return err;
}

static int bes_cfgvendor_default_handler(struct wiphy *wiphy,
					 struct wireless_dev *wdev,
					 const void *data, int len)
{
	return 0;
}

static int bes_cfgvendor_start_mkeep_alive(struct wiphy *wiphy, struct wireless_dev *wdev,
					   const void *data, int len) {
	return 0;
}
static int bes_cfgvendor_stop_mkeep_alive(struct wiphy *wiphy, struct wireless_dev *wdev,
					  const void *data, int len) {
	return 0;
}

static const struct wiphy_vendor_command bes_own_commands[] = {

	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = WIFI_SUBCMD_SET_COUNTRY_CODE
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_set_country_code,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = andr_wifi_attr_policy,
#ifdef CONFIG_OPLUS_PLATFORM
		.maxattr = ANDR_WIFI_ATTRIBUTE_MAX,
#endif
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = WIFI_OFFLOAD_START_MKEEP_ALIVE
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_start_mkeep_alive,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0))
		.policy = mkeep_alive_attr_policy,
		.maxattr = MKEEP_ALIVE_ATTRIBUTE_MAX
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = WIFI_OFFLOAD_STOP_MKEEP_ALIVE
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_stop_mkeep_alive,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0))
                .policy = mkeep_alive_attr_policy,
                .maxattr = MKEEP_ALIVE_ATTRIBUTE_MAX
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = GSCAN_SUBCMD_GET_CAPABILITIES
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_gscan_get_capabilities,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = GSCAN_SUBCMD_SET_CONFIG
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_set_scan_cfg,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = GSCAN_SUBCMD_SET_SCAN_CONFIG
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_set_batch_scan_cfg,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = GSCAN_SUBCMD_ENABLE_GSCAN
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_initiate_gscan,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = GSCAN_SUBCMD_ENABLE_FULL_SCAN_RESULTS
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_enable_full_scan_result,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = GSCAN_SUBCMD_SET_HOTLIST
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_hotlist_cfg,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = GSCAN_SUBCMD_SET_SIGNIFICANT_CHANGE_CONFIG
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_significant_change_cfg,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = GSCAN_SUBCMD_GET_SCAN_RESULTS
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_gscan_get_batch_results,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = RTT_SUBCMD_SET_CONFIG
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_rtt_set_config,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = rtt_attr_policy,
		.maxattr = RTT_ATTRIBUTE_MAX,
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = RTT_SUBCMD_CANCEL_CONFIG
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_rtt_cancel_config,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = RTT_SUBCMD_GETCAPABILITY
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_rtt_get_capability,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = LSTATS_SUBCMD_GET_INFO
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_lstats_get_info,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = LSTATS_SUBCMD_SET_INFO
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_default_handler,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = LSTATS_SUBCMD_CLEAR_INFO
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_default_handler,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = WIFI_SUBCMD_SET_RSSI_MONITOR
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_set_rssi_monitor,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = rssi_monitor_attr_policy,
		.maxattr = RSSI_MONITOR_ATTRIBUTE_MAX,
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = LOGGER_START_LOGGING
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_logger_start_logging,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = logger_attr_policy,
#ifdef CONFIG_OPLUS_PLATFORM
		.maxattr = LOGGER_ATTRIBUTE_MAX,
#endif
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = LOGGER_GET_FEATURE
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_logger_get_feature,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = GSCAN_SUBCMD_GET_CHANNEL_LIST
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_gscan_get_channel_list,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = get_channel_attr_policy,
		.maxattr = GSCAN_ATTRIBUTE_MAX,
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = LOGGER_GET_VER
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_logger_get_version,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = logger_attr_policy,
#ifdef CONFIG_OPLUS_PLATFORM
		.maxattr = LOGGER_ATTRIBUTE_MAX,
#endif
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = LOGGER_GET_RING_STATUS
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_logger_get_ring_status,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = LOGGER_GET_RING_DATA
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_logger_get_ring_data,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = logger_attr_policy,
#ifdef CONFIG_OPLUS_PLATFORM
		.maxattr = LOGGER_ATTRIBUTE_MAX,
#endif
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = LOGGER_TRIGGER_MEM_DUMP
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_logger_get_firmware_memory_dump,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = LOGGER_START_PKT_FATE_MONITORING
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_logger_start_pkt_fate_monitoring,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = LOGGER_GET_TX_PKT_FATES
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_logger_get_tx_pkt_fates,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = LOGGER_GET_RX_PKT_FATES
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_logger_get_rx_pkt_fates,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = VENDOR_CMD_RAW_DATA,
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = WIFI_SUBCMD_SET_PNO_RANDOM_MAC_OUI
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_set_rand_mac_oui,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = andr_wifi_attr_policy,
		.maxattr = ANDR_WIFI_ATTRIBUTE_MAX,
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = WIFI_SUBCMD_NODFS_SET
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_set_nodfs_flag,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = andr_wifi_attr_policy,
		.maxattr = ANDR_WIFI_ATTRIBUTE_MAX,
#endif

	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = WIFI_SUBCMD_CONFIG_ND_OFFLOAD
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_set_nd_offload,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = andr_wifi_attr_policy,
		.maxattr = ANDR_WIFI_ATTRIBUTE_MAX,
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = WIFI_SUBCMD_GET_FEATURE_SET
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_get_feature_set,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = andr_wifi_attr_policy,
		.maxattr = ANDR_WIFI_ATTRIBUTE_MAX,
#endif
	},
	{
		{
			.vendor_id = OUI_GOOGLE,
			.subcmd = WIFI_SUBCMD_GET_FEATURE_SET_MATRIX
		},
		.flags = WIPHY_VENDOR_CMD_NEED_WDEV | WIPHY_VENDOR_CMD_NEED_NETDEV,
		.doit = bes_cfgvendor_get_feature_set_matrix,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,3,0))
		.policy = andr_wifi_attr_policy,
		.maxattr = ANDR_WIFI_ATTRIBUTE_MAX,
#endif
	}
};

static const struct  nl80211_vendor_cmd_info bes_own_events[] = {
	BES_CFGVENDOR_EVENT_MEMBER(GSCAN_EVENT_SIGNIFICANT_CHANGE_RESULTS),
	BES_CFGVENDOR_EVENT_MEMBER(GSCAN_EVENT_HOTLIST_RESULTS_FOUND),
	BES_CFGVENDOR_EVENT_MEMBER(GSCAN_EVENT_SCAN_RESULTS_AVAILABLE),
	BES_CFGVENDOR_EVENT_MEMBER(GSCAN_EVENT_FULL_SCAN_RESULTS),
	BES_CFGVENDOR_EVENT_MEMBER(RTT_EVENT_COMPLETE),
	BES_CFGVENDOR_EVENT_MEMBER(GSCAN_EVENT_COMPLETE_SCAN),
	BES_CFGVENDOR_EVENT_MEMBER(GSCAN_EVENT_HOTLIST_RESULTS_LOST),
	BES_CFGVENDOR_EVENT_MEMBER(GOOGLE_RSSI_MONITOR_EVENT),
};

void bes_set_vendor_command(struct wiphy *wiphy)
{
	wiphy->vendor_commands = bes_own_commands;
	wiphy->n_vendor_commands = ARRAY_SIZE(bes_own_commands);
	wiphy->vendor_events = bes_own_events;
	wiphy->n_vendor_events = ARRAY_SIZE(bes_own_events);
}

void bes_vendor_command_detach(struct wiphy *wiphy)
{
	wiphy->vendor_commands = NULL;
	wiphy->vendor_events = NULL;
	wiphy->n_vendor_commands = 0;
	wiphy->n_vendor_events = 0;
}
