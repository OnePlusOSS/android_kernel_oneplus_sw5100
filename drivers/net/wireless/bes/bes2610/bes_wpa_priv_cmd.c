/*
 * @file bes_wpa_priv_cmd.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/etherdevice.h>
#include <linux/inetdevice.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <net/cfg80211.h>
#include <net/ip.h>
#include <linux/wireless.h>

#include "rwnx_main.h"
#include "bes_filter.h"
#include "bes_wowlan.h"
#include "bes_wpa_priv_cmd.h"
#include "bes_signal_test.h"

static int bes2610_wpa_cmd_default_handler(struct rwnx_vif *rwnx_vif, char *command, u32 max_len)
{
	int bytes_written = 0;
	bes_dbg(BES_DBG_WPA_PRI_CMD, "%s: wpa_supplicant private cmd %s\n", __FUNCTION__, command);

	return bytes_written;
}

static int bes2610_p2p_set_noa(struct rwnx_vif *rwnx_vif, char* buf, int len)
{
	int ret = 0;
	int count = 0;
	int start = 0;
	int duration = 0;
	int argc = 0;
	char *argv[4] = {0};

	/* Parsing command line */
	argc = bes_common_parse_line(buf, argv, 3);
	if (argc <= 0) {
		bes_err(BES_DBG_WPA_PRI_CMD,
			"%s ERROR: fail to parse line.\n", __func__);
		return -EINVAL;
	}

	if (strlen(argv[0]))
		count = (int)simple_strtol(argv[0], NULL, 10);

	if (strlen(argv[1]))
		start = (int)simple_strtol(argv[1], NULL, 10);

	if (strlen(argv[2]))
		duration = (int)simple_strtol(argv[2], NULL, 10);

	bes_info(BES_DBG_WPA_PRI_CMD, "%s. count: %d, start: %d, duration: %d\n", __func__, count, start, duration);

	//do something

	return ret;
}

static int bes2610_wpa_cmd_p2p_set_noa(struct rwnx_vif *rwnx_vif, char *command, u32 total_len)
{
	int skip = 0;

	skip = strlen("P2P_SET_NOA") + 1;

	return bes2610_p2p_set_noa(rwnx_vif, command + skip, total_len - skip);
}

static int bes2610_p2p_set_ps(struct rwnx_vif *rwnx_vif, char* buf, int len)
{
	int ret = 0;
	int ps = 0;
	int ctw = 0;
	int legacy_ps = 0;
	int argc = 0;
	char *argv[4] = {0};

	/* Parsing command line */
	argc = bes_common_parse_line(buf, argv, 3);
	if (argc <= 0) {
		bes_err(BES_DBG_WPA_PRI_CMD,
			"%s ERROR: fail to parse line.\n", __func__);
		return -EINVAL;
	}
	if (argv[0])
		ps = simple_strtol(argv[0], NULL, 10);

	if (argv[1])
		ctw = simple_strtol(argv[1], NULL, 10);

	if (argv[2])
		legacy_ps = simple_strtol(argv[2], NULL, 10);

	bes_info(BES_DBG_WPA_PRI_CMD, "%s. ps: %d, ctw: %d, legacy_ps: %d\n", __func__, ps, ctw, legacy_ps);

	//do something

	return ret;
}

static int bes2610_wpa_cmd_p2p_set_ps(struct rwnx_vif *rwnx_vif, char *command, u32 total_len)
{
	int skip = 0;

	skip = strlen("P2P_SET_PS") + 1;

	return bes2610_p2p_set_ps(rwnx_vif, command + skip, total_len - skip);
}

static int bes2610_wpa_cmd_rxfilter_start(struct rwnx_vif *rwnx_vif, char *command, u32 total_len)
{
	/* Command format: RXFILTER-START */
	int bytes_written = 0;

	bes_dbg(BES_DBG_WPA_PRI_CMD, "%s: rwnx_vif:%d(%s), command: %s\n",
		__FUNCTION__, rwnx_vif->vif_index, rwnx_vif->ndev->name,
		command);

	bytes_written = bes_filter_manager_sync_filter(rwnx_vif->rwnx_hw, true);

	return bytes_written;
}

static int bes2610_wpa_cmd_rxfilter_stop(struct rwnx_vif *rwnx_vif, char *command, u32 total_len)
{
	/* Command format: RXFILTER-STOP */
	int bytes_written = 0;

	bes_dbg(BES_DBG_WPA_PRI_CMD, "%s: command: %s\n", __FUNCTION__, command);

	/* Because rxfilter_add and rxfilter_remove modify the filter list on the driver side and
	   do not directly operate on the lmac filter, there is no need to do anything in rxfilter_stop */

	return bytes_written;
}

static int bes2610_rxfilter_add_and_remove(struct rwnx_vif *rwnx_vif, bool add, int filter_num)
{
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	u8 filter_type = 0;
	int ret = 0;

	/* Check the validity of parameters */
	BUG_ON((filter_num != RX_FILTER_TYPE_V4_MULTICAST && filter_num != RX_FILTER_TYPE_V6_MULTICAST) ||
	       !rwnx_hw || RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_STATION);

	/* Ignore when interface is donw */
	if (!rwnx_vif->up)
		return -EFAULT;

	do {
		/* Get wpa priv cmd type */
		filter_type = (filter_num  == RX_FILTER_TYPE_V4_MULTICAST) ? PRIV_CMD_FILTER_TYPE_V4_MULTICAST :
									PRIV_CMD_FILTER_TYPE_V6_MULTICAST;

		/* Aviod repeating to add or delete */
		if (((rwnx_vif->priv_cmd.rxfilter & BIT(filter_type)) && add) ||
		    (!(rwnx_vif->priv_cmd.rxfilter & BIT(filter_type)) && !add))
			break;

		/* Update priv cmd rxfilter */
		if (add)
			rwnx_vif->priv_cmd.rxfilter |= BIT(filter_type);
		else
			rwnx_vif->priv_cmd.rxfilter &= ~BIT(filter_type);

		/* Ctrl multicast filter in ifl_map */
		ret = bes_filter_manager_ctrl_multicast_filter(rwnx_vif, rwnx_vif->priv_cmd.rxfilter);
	} while(0);
	bes_err_with_cond(ret, BES_DBG_WPA_PRI_CMD,
			"%s ERROR(%d): Fail to add/remove rxfilter\n", __func__, ret);

	return ret;
}

static int bes2610_wpa_cmd_rxfilter_add(struct rwnx_vif *rwnx_vif, char *command, u32 total_len)
{
	/* Command format: RXFILTER-ADD 2 / RXFILTER-ADD 3 */
	int filter_num = *(command + strlen("RXFILTER-ADD") + 1) - '0';

	bes_dbg(BES_DBG_WPA_PRI_CMD, "%s: rwnx_vif:%d(%s), command: %s , filter_num: %d\n",
		__FUNCTION__, rwnx_vif->vif_index, rwnx_vif->ndev->name,
		command, filter_num);

	if (filter_num != RX_FILTER_TYPE_V4_MULTICAST && filter_num != RX_FILTER_TYPE_V6_MULTICAST) {
		bes_err(BES_DBG_WPA_PRI_CMD, "%s Fail to get filter_num. filter_num: %d\n",
			__FUNCTION__, filter_num);
		return -EINVAL;
	}

	return bes2610_rxfilter_add_and_remove(rwnx_vif, false, filter_num);
}

static int bes2610_wpa_cmd_rxfilter_remove(struct rwnx_vif *rwnx_vif, char *command, u32 total_len)
{
	/* Command format: RXFILTER-REMOVE 2 / RXFILTER-REMOVE 3 */
	int filter_num = *(command + strlen("RXFILTER-REMOVE") + 1) - '0';

	bes_dbg(BES_DBG_WPA_PRI_CMD, "%s: rwnx_vif:%d(%s), command: %s , filter_num: %d\n",
		__FUNCTION__, rwnx_vif->vif_index, rwnx_vif->ndev->name,
		command, filter_num);

	if (filter_num != RX_FILTER_TYPE_V4_MULTICAST && filter_num != RX_FILTER_TYPE_V6_MULTICAST) {
		bes_err(BES_DBG_WPA_PRI_CMD, "%s Fail to get filter_num. filter_num: %d\n",
			__FUNCTION__, filter_num);
		return -EINVAL;
	}

	return bes2610_rxfilter_add_and_remove(rwnx_vif, true, filter_num);
}

static int bes2610_enable_btcoex_mode(struct rwnx_vif *rwnx_vif, bool enable)
{
	int ret = -1;

	if (enable) {
		//do something to enable
	}
	else {
		//do something to close
	}

	ret = 0;

	return ret;
}

static int bes2610_wpa_cmd_btcoex_mode(struct rwnx_vif *rwnx_vif, char *command, u32 total_len)
{
	int bytes_written;
	uint mode = *(command + strlen("BTCOEXMODE") + 1) - '0';

	bes_dbg(BES_DBG_WPA_PRI_CMD, "%s: command: %s , mode: %u\n", __FUNCTION__, command, mode);

	if (mode == 1)
		bytes_written = bes2610_enable_btcoex_mode(rwnx_vif, false); /* DHCP starts */
	else
		bytes_written = bes2610_enable_btcoex_mode(rwnx_vif, true); /* DHCP ends */

	return bytes_written;
}

static int bes2610_enable_btcoex_scan(struct rwnx_vif *rwnx_vif, bool enable)
{
	int ret = -1;

	if (enable) {
		//do something to enable
	}
	else {
		//do something to close
	}

	ret = 0;

	return ret;
}

static int bes2610_wpa_cmd_btcoex_scan_start(struct rwnx_vif *rwnx_vif, char *command, u32 total_len)
{
	bes_dbg(BES_DBG_WPA_PRI_CMD, "%s: command: %s\n", __FUNCTION__, command);

	return bes2610_enable_btcoex_scan(rwnx_vif, true);
}

static int bes2610_wpa_cmd_btcoex_scan_stop(struct rwnx_vif *rwnx_vif, char *command, u32 total_len)
{
	bes_dbg(BES_DBG_WPA_PRI_CMD, "%s: command: %s\n", __FUNCTION__, command);

	return bes2610_enable_btcoex_scan(rwnx_vif, false);
}

int bes_wpa_priv_cmd_add_mc_and_mdns_filter(struct rwnx_hw *rwnx_hw, struct list_head *list_set_head,
					   struct bes_capacity *stats)
{
	int ret = 0, i = 0;
	u8 vif_idx = 0;
	u32 vif_idx_map = 0;
	struct rwnx_vif *rwnx_vif = NULL;
	struct filter_item *item = NULL;
	struct mac_addr bc_addr = {
		.array = {0xffff, 0xffff, 0xffff},
	};
	uint8_t ipv6_ip[][16] = {
		{ 0xff, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 },
	};

	/* Get vif_idx_map value */
	vif_idx_map = bes_common_get_vif_idx_map(rwnx_hw);
	if (!vif_idx_map)
		return 0;

	for_each_set_bit(vif_idx, (const unsigned long *)&vif_idx_map, NX_ITF_MAX) {
		/* vif needs to be connected */
		rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);
		if (!rwnx_vif || !netif_carrier_ok(rwnx_vif->ndev))
			continue;

		/* Add wowlan filter to list_set_head */
		do {
			/* IGMP filter */
			item = bes_filter_manager_create_ipv4_proto(vif_idx, FILTER_IPV4_PROTO,
					IP_PROT_IGMP, RXF_MATCH_EQUAL, RXF_REJECT);
			if (!item) {
				ret = -EINVAL;
				bes_err(BES_DBG_WPA_PRI_CMD,
					"%s ERROR(%d): Fail to create IGMP filter on vif %s at head\n",
					__func__, ret, rwnx_vif->ndev->name);
				break;
			}
			ret = bes_wowlan_insert_at_head(rwnx_hw, list_set_head, stats, item);
			if (ret) {
				bes_err(BES_DBG_WPA_PRI_CMD,
					"%s ERROR(%d): Fail to add IGMP filter on vif %s at head\n",
					__func__, ret, rwnx_vif->ndev->name);
				break;
			}

			/* broadcast filter */
			item =  bes_filter_manager_create_mac_addr(vif_idx, FILTER_MAC_DA,
					&bc_addr, RXF_MATCH_EQUAL, RXF_REJECT);
			if (!item) {
				ret = -EINVAL;
				bes_err(BES_DBG_WPA_PRI_CMD,
					"%s ERROR(%d): Fail to create broadcast filter on vif %s at head\n",
					__func__, ret, rwnx_vif->ndev->name);
				break;
			}
			ret = bes_wowlan_insert_at_head(rwnx_hw, list_set_head, stats, item);
			if (ret) {
				bes_err(BES_DBG_WPA_PRI_CMD,
					"%s ERROR(%d): Fail to add broadcast filter on vif %s at head\n",
					__func__, ret, rwnx_vif->ndev->name);
				break;
			}

			/* ipv4/ipv6 multicast filter */
			item =  bes_filter_manager_create_multicast(vif_idx, FILTER_ALL_MULTICAST,
					RXF_MATCH_EQUAL, RXF_REJECT);
			if (!item) {
				ret = -EINVAL;
				bes_err(BES_DBG_WPA_PRI_CMD,
					"%s ERROR(%d): Fail to create ipv4/ipv6 multicast filter on vif %s at head\n",
					__func__, ret, rwnx_vif->ndev->name);
				break;
			}
			ret = bes_wowlan_insert_at_head(rwnx_hw, list_set_head, stats, item);
			if (ret) {
				bes_err(BES_DBG_WPA_PRI_CMD,
					"%s ERROR(%d): Fail to add ipv4/ipv6 multicast filter on vif %s at head\n",
					__func__, ret, rwnx_vif->ndev->name);
				break;
			} else {
				bes_info(BES_DBG_WPA_PRI_CMD,
					 "%s Add multicast filter on vif %s when screen off\n",
					 __func__, rwnx_vif->ndev->name);
			}

			/* specified IPv6 address */
			for (i = 0; i < sizeof(ipv6_ip) / sizeof(ipv6_ip[0]); i++)
			{
				item = bes_filter_manager_create_ipv6_ip_addr(vif_idx, FILTER_IPV6_DST,
									      (struct in6_addr *)&ipv6_ip[i],
									      RXF_MATCH_EQUAL, RXF_REJECT);
				if (!item) {
					ret = -EINVAL;
					bes_err(BES_DBG_WPA_PRI_CMD,
						"%s ERROR(%d): Fail to create specified IPv6 address filter on vif %s at head. i:%d\n",
						__func__, ret, rwnx_vif->ndev->name, i);
					break;
				}
				ret = bes_wowlan_insert_at_head(rwnx_hw, list_set_head, stats, item);
				if (ret) {
					bes_err(BES_DBG_WPA_PRI_CMD,
						"%s ERROR(%d): Fail to add specified IPv6 address filter on vif %s at head. i:%d\n",
						__func__, ret, rwnx_vif->ndev->name, i);
					break;
				}
			}
			if (ret)
				break;

			/* Arp offload filter */
			if (rwnx_hw->filter.arp_offload[vif_idx]) {
				item = bes_filter_manager_create_ipv4_arp(vif_idx, FILTER_IPV4_ARP, ARP_OFFLOAD_FUNC_IDX, 0, RXF_ACCEPT);
				if (!item) {
					ret = -EINVAL;
					bes_err(BES_DBG_WPA_PRI_CMD,
						"%s ERROR(%d): Fail to create arp filter on vif %s at head\n",
						__func__, ret, rwnx_vif->ndev->name);
					break;
				}
				ret = bes_wowlan_insert_at_head(rwnx_hw, list_set_head, stats, item);
				if (ret) {
					bes_err(BES_DBG_WPA_PRI_CMD,
						"%s ERROR(%d): Fail to add arp filter on vif %s at head\n",
						__func__, ret, rwnx_vif->ndev->name);
					break;
				} else {
					bes_info(BES_DBG_WPA_PRI_CMD,
						"%s Add arp offload filter on vif %s when screen off\n",
						__func__, rwnx_vif->ndev->name);
				}
			}

			/* Ns offload filter */
			if (rwnx_hw->filter.ns_offload[vif_idx]) {
				item = bes_filter_manager_create_ipv6_ns(vif_idx, FILTER_IPV6_NS, NS_OFFLOAD_FUNC_IDX, 0, RXF_ACCEPT);
				if (!item) {
					ret = -EINVAL;
					bes_err(BES_DBG_WPA_PRI_CMD,
						"%s ERROR(%d): Fail to create ns filter on vif %s at head\n",
						__func__, ret, rwnx_vif->ndev->name);
					break;
				}
				ret = bes_wowlan_insert_at_head(rwnx_hw, list_set_head, stats, item);
				if (ret) {
					bes_err(BES_DBG_WPA_PRI_CMD,
						"%s ERROR(%d): Fail to add ns filter on vif %s at head\n",
						__func__, ret, rwnx_vif->ndev->name);
					break;
				}
			}

			/* Signal test filter */
			ret = bes_signal_test_add_filter_when_screen_off(rwnx_hw, rwnx_vif, list_set_head, stats);
		} while(0);

		/* Return when an error ocurred */
		if (ret)
			return ret;
	}

	return 0;
}

void bes_wpa_priv_cmd_increase_event_cnt(struct rwnx_hw *rwnx_hw)
{
	if (!rwnx_hw)
		return;

	spin_lock_bh(&rwnx_hw->wpa_priv_cmd.lock);
	rwnx_hw->wpa_priv_cmd.event_cnt++;
	spin_unlock_bh(&rwnx_hw->wpa_priv_cmd.lock);
}

int bes_wpa_priv_cmd_update_screen_suspend_filters(struct rwnx_hw *rwnx_hw, bool suspend)
{
	u16 event_cnt = 0;

	if (!rwnx_hw)
		return -EINVAL;

	spin_lock_bh(&rwnx_hw->wpa_priv_cmd.lock);
	if (!suspend) {
		/* Increases the value of event_cnt if the screen state is set to on */
		rwnx_hw->wpa_priv_cmd.event_cnt++;
	} else {
		/* Record the current value of event count */
		event_cnt = rwnx_hw->wpa_priv_cmd.event_cnt;

		/* If the screen state is set to off and no event occurred after
		   the last update of screen-off filters, do nothing. Otherwise,
		   recording the last value of event count when refreshing screen-off filters */
		if (rwnx_hw->wpa_priv_cmd.last_event_cnt == event_cnt) {
			spin_unlock_bh(&rwnx_hw->wpa_priv_cmd.lock);
			bes_dbg(BES_DBG_WPA_PRI_CMD, "%s do nothing. event_cnt:%d\n",
				__func__, event_cnt);
			return 0;
		} else {
			rwnx_hw->wpa_priv_cmd.last_event_cnt = event_cnt;
		}
	}
	spin_unlock_bh(&rwnx_hw->wpa_priv_cmd.lock);

	bes_info(BES_DBG_WPA_PRI_CMD, "%s screen filter to %s. event_cnt:%d last_event_cnt:%d\n",
		 __func__, suspend ? "ON" : "OFF",
		 rwnx_hw->wpa_priv_cmd.event_cnt,
		 rwnx_hw->wpa_priv_cmd.last_event_cnt);

	/* Update the filters when screen status changed */
	return bes_filter_manager_update_screen_suspend_filters(rwnx_hw, suspend);
}

static int bes2610_wpa_cmd_set_suspendmode(struct rwnx_vif *rwnx_vif, char *command, u32 total_len)
{
	int bytes_written;
	int suspend_flag;

	suspend_flag = *(command + strlen("SETSUSPENDMODE") + 1) - '0';
	if (suspend_flag != 0)
		suspend_flag = 1;

	bes_info(BES_DBG_WPA_PRI_CMD, "%s: command: %s , suspend_flag: %d\n", __FUNCTION__, command, suspend_flag);

	if (!(bytes_written = bes_wpa_priv_cmd_update_screen_suspend_filters(rwnx_vif->rwnx_hw, !!suspend_flag)))
		bes_dbg(BES_DBG_WPA_PRI_CMD, "%s: Suspend Mode %d\n", __FUNCTION__, suspend_flag);
	else
		bes_err(BES_DBG_WPA_PRI_CMD, "%s: failed %d\n", __FUNCTION__, bytes_written);

	return bytes_written;
}

static int bes2610_wpa_cmd_country_set(struct rwnx_vif *rwnx_vif, char *command, u32 total_len)
{
	int bytes_written = 0;
	char *country_code;
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	struct wiphy *wiphy = rwnx_hw->wiphy;

	country_code = command + strlen("COUNTRY") + 1;

	bes_dbg(BES_DBG_WPA_PRI_CMD, "%s: command: %s , country_code: %s\n", __FUNCTION__, command, country_code);

	/* notify cfg80211 to update database */
	if (rwnx_hw->mod_params->custregd) {
		bes_regd_update_custregd(rwnx_hw, wiphy, country_code);
	} else {
		bytes_written = regulatory_hint(rwnx_hw->wiphy, country_code);
	}

	return bytes_written;
}

static struct bes_wpa_cmd_map bes_wpa_cmd_map_tab[] ={
	/*cmd			cmd_len cmd_handler			*/
	{"P2P_SET_NOA",		11,	bes2610_wpa_cmd_p2p_set_noa},
	{"P2P_SET_PS",		10,	bes2610_wpa_cmd_p2p_set_ps},
	{"SET_AP_WPS_P2P_IE",	17,	bes2610_wpa_cmd_default_handler},
	{"LINKSPEED",		9,	bes2610_wpa_cmd_default_handler},
	{"RSSI",		4,	bes2610_wpa_cmd_default_handler},
	{"GETBAND",		7,	bes2610_wpa_cmd_default_handler},
	{"WLS_BATCHING",	12,	bes2610_wpa_cmd_default_handler},
	{"MACADDR",		7,	bes2610_wpa_cmd_default_handler},
	{"RXFILTER-START",	14,	bes2610_wpa_cmd_rxfilter_start},
	{"RXFILTER-STOP",	13,	bes2610_wpa_cmd_rxfilter_stop},
	{"RXFILTER-ADD",	12,	bes2610_wpa_cmd_rxfilter_add},
	{"RXFILTER-REMOVE",	15,	bes2610_wpa_cmd_rxfilter_remove},
	{"BTCOEXMODE",		10,	bes2610_wpa_cmd_btcoex_mode},
	{"BTCOEXSCAN-START",	16,	bes2610_wpa_cmd_btcoex_scan_start},
	{"BTCOEXSCAN-STOP",	15,	bes2610_wpa_cmd_btcoex_scan_stop},
	{"SETSUSPENDMODE",	14,	bes2610_wpa_cmd_set_suspendmode},
	{"COUNTRY",		7,	bes2610_wpa_cmd_country_set},
	{"MIRACAST",		8,	bes2610_wpa_cmd_default_handler},
};

static int handle_private_cmd(struct net_device *net, char *command, u32 cmd_len, u32 max_len)
{
	int i = 0;
	int bytes_written = 0;
	struct rwnx_vif *vif = netdev_priv(net);
	int cmd_num = ARRAY_SIZE(bes_wpa_cmd_map_tab);

	do {
		/* match operation item and execure its handler */
		for(i = 0; i < cmd_num; i++) {
			if(cmd_len < bes_wpa_cmd_map_tab[i].cmd_len)
				continue;

			if(strncasecmp(command, bes_wpa_cmd_map_tab[i].cmd, bes_wpa_cmd_map_tab[i].cmd_len) == 0) {
				bytes_written = bes_wpa_cmd_map_tab[i].cmd_handler(vif, command, max_len);
				break;
			}
		}

		/* operation item mismatch */
		if(i == cmd_num) {
			bes_err(BES_DBG_WPA_PRI_CMD, "%s: cmd(%s) mismatch\n", __FUNCTION__, command);
			bytes_written = -1;
		}
	} while (0);
	return bytes_written;
}

int android_priv_cmd(struct net_device *net, struct ifreq *ifr, int cmd)
{
#define PRIVATE_COMMAND_MAX_LEN 8192
#define PRIVATE_COMMAND_DEF_LEN 4096
	int ret = 0;
	int bytes_written = 0;
	int max_len = 0;
	char *command = NULL;
	android_wifi_priv_cmd priv_cmd;
	int buf_size = 0;

	if (!capable(CAP_NET_ADMIN)) {
		ret = -EPERM;
		goto exit;
	}
	if (!ifr->ifr_data) {
		ret = -EINVAL;
		goto exit;
	}

#ifdef CONFIG_COMPAT
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 6, 0))
	if (in_compat_syscall())
#else
	if (is_compat_task())
#endif
	{
		compat_android_wifi_priv_cmd compat_priv_cmd;
		if (copy_from_user(&compat_priv_cmd, ifr->ifr_data, sizeof(compat_android_wifi_priv_cmd))) {
			ret = -EFAULT;
			goto exit;
		}
		priv_cmd.buf = compat_ptr(compat_priv_cmd.buf);
		priv_cmd.used_len = compat_priv_cmd.used_len;
		priv_cmd.total_len = compat_priv_cmd.total_len;
	} else
#endif /* CONFIG_COMPAT */
	{
		if (copy_from_user(&priv_cmd, ifr->ifr_data, sizeof(android_wifi_priv_cmd))) {
			ret = -EFAULT;
			goto exit;
		}
	}
	if ((priv_cmd.total_len > PRIVATE_COMMAND_MAX_LEN) || (priv_cmd.total_len < 0)) {
		bes_err(BES_DBG_WPA_PRI_CMD, "%s: buf length invalid:%d\n", __FUNCTION__, priv_cmd.total_len);
		ret = -EINVAL;
		goto exit;
	}

	buf_size = max(priv_cmd.total_len, PRIVATE_COMMAND_DEF_LEN);
	max_len = buf_size + 1;
	command = kmalloc((max_len), GFP_KERNEL);

	if (!command) {
		bes_err(BES_DBG_WPA_PRI_CMD, "%s: failed to allocate memory\n", __FUNCTION__);
		ret = -ENOMEM;
		goto exit;
	}
	if (copy_from_user(command, priv_cmd.buf, priv_cmd.total_len)) {
		ret = -EFAULT;
		goto exit;
	}
	command[priv_cmd.total_len] = '\0';
	/* outputs */
	bes_dbg(BES_DBG_WPA_PRI_CMD, "%s: Android private cmd \"%s\" on %s\n", __FUNCTION__, command, ifr->ifr_name);
	bes_dbg(BES_DBG_WPA_PRI_CMD, "cmd = %d\n", cmd);
	bes_dbg(BES_DBG_WPA_PRI_CMD, "buf_size=%d\n", buf_size);

	bytes_written = handle_private_cmd(net, command, priv_cmd.total_len, max_len);

	if (bytes_written >= 0) {
		if ((bytes_written == 0) && (priv_cmd.total_len > 0)) {
			command[0] = '\0';
		}
		if (bytes_written >= priv_cmd.total_len) {
			bes_err(BES_DBG_WPA_PRI_CMD, "%s: err. bytes_written:%d >= buf_size:%d \n",
				__FUNCTION__, bytes_written, buf_size);
			ret = -EFAULT;
			goto exit;
		}
		bytes_written++;
		priv_cmd.used_len = bytes_written;
		if (copy_to_user(priv_cmd.buf, command, bytes_written)) {
			bes_err(BES_DBG_WPA_PRI_CMD, "%s: failed to copy data to user buffer\n", __FUNCTION__);
			ret = -EFAULT;
		}
	} else {
		bes_err(BES_DBG_WPA_PRI_CMD, "%s: Propagate the error\n", __FUNCTION__);
		ret = bytes_written;
	}
exit:
	kfree(command);
	return ret;
}

void bes_wpa_priv_cmd_init(struct rwnx_hw *rwnx_hw)
{
	spin_lock_init(&rwnx_hw->wpa_priv_cmd.lock);
	rwnx_hw->wpa_priv_cmd.event_cnt = 0;
	rwnx_hw->wpa_priv_cmd.last_event_cnt = 0;
}