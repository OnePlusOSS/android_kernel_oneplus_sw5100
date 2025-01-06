/*
 * @file bes_filter_map.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "rwnx_defs.h"
#include "bes_log.h"
#include "bes_custom_filter.h"
#include "bes_filter_map.h"
#include "bes_filter.h"

const struct bes_filter_type_t bes_filter_type_tbl[] = {
	{.index = FILTER_MAC_DA,		.name = "mac_da"},
	{.index = FILTER_MAC_SA,		.name = "mac_sa"},
	{.index = FILTER_ETH_TYPE,		.name = "eth_type"},
	{.index = FILTER_IPV4_SRC,		.name = "ipv4_src"},
	{.index = FILTER_IPV4_DST,		.name = "ipv4_dst"},
	{.index = FILTER_IPV4_PROTO,		.name = "ipv4_proto"},
	{.index = FILTER_IPV4_ARP,		.name = "ipv4_arp"},
	{.index = FILTER_EAPOL_G_MSG1,		.name = "eapol_g_msg1"},
	{.index = FILTER_MAGIC_PACKET,		.name = "magic_packet"},
	{.index = FILTER_IPV4_MULTICAST,	.name = "ipv4_multicast"},
	{.index = FILTER_IPV6_MULTICAST,	.name = "ipv6_multicast"},
	{.index = FILTER_ALL_MULTICAST,		.name = "all_multicast"},
	{.index = FILTER_PATTERN,		.name = "pattern"},
	{.index = FILTER_IPV4_UDP_DST_SINGLE,	.name = "ipv4_udp_dst_single"},
	{.index = FILTER_IPV4_UDP_DST_RANGE,	.name = "ipv4_udp_dst_range"},
	{.index = FILTER_IPV4_UDP_SRC_SINGLE,	.name = "ipv4_udp_src_single"},
	{.index = FILTER_IPV4_UDP_SRC_RANGE,	.name = "ipv4_udp_src_range"},
	{.index = FILTER_IPV4_UDP_SRC_DST_SINGLE,.name = "ipv4_udp_s/d_single"},
	{.index = FILTER_IPV6_UDP_SRC_DST_SINGLE,.name = "ipv6_udp_s/d_single"},
	{.index = FILTER_IPV4_TCP_DST_SINGLE,	.name = "ipv4_tcp_dst_single"},
	{.index = FILTER_IPV4_TCP_DST_RANGE,	.name = "ipv4_tcp_dst_range"},
	{.index = FILTER_IPV4_TCP_SRC_SINGLE,	.name = "ipv4_tcp_src_single"},
	{.index = FILTER_IPV4_TCP_SRC_RANGE,	.name = "ipv4_tcp_src_range"},
	{.index = FILTER_IPV6_SRC,		.name = "ipv6_src"},
	{.index = FILTER_IPV6_DST,		.name = "ipv6_dst"},
	{.index = FILTER_IPV6_NS,		.name = "ipv6_ns"},
};

static char *bes_filter_map_filter_type_str(const int f_idx)
{
	/* Check the validity of parameters */
	if ((f_idx < 0) || (f_idx >= FILTER_MAX))
		return "UNKNOW";

	/* Get str based on idx */
	return bes_filter_type_tbl[f_idx].name;
}

const struct bes_fl_type_t bes_fl_type_tbl[] = {
	{.index = FL_ETH_TYPE,			.name = "eth_type"},
	{.index = FL_IP_ADDR,			.name = "ip_addr"},
	{.index = FL_PROTO,			.name = "proto"},
	{.index = FL_UDP_PORT,			.name = "udp_port"},
	{.index = FL_TCP_PORT,			.name = "tcp_port"},
	{.index = FL_MAC_ADDR,			.name = "mac_addr"},
};

static char *bes_filter_map_fl_type_str(const int tbl_idx)
{
	/* Check the validity of parameters */
	if (tbl_idx < 0 || tbl_idx >= FL_MAX)
		return "UNKNOW";

	/* Get str based on idx */
	return bes_fl_type_tbl[tbl_idx].name;
}

int bes_filter_map_fl_type_idx(const char *str)
{
	int fl_idx = 0;

	/* Check the validity of parameters */
	if (!str)
		return -EINVAL;

	/* Get idx based on str */
	for (fl_idx = 0; fl_idx < sizeof(bes_fl_type_tbl) / sizeof(bes_fl_type_tbl[0]); fl_idx++) {
		if (strcmp(str, bes_fl_type_tbl[fl_idx].name) == 0)
			return bes_fl_type_tbl[fl_idx].index;
	}

	return -EINVAL;
}

bool bes_filter_map_get_valid(struct bes_fl_map *fl_map)
{
	BUG_ON(!fl_map);

	return fl_map->valid;
}

void bes_filter_map_set_valid(struct bes_fl_map *fl_map, bool valid)
{
	BUG_ON(!fl_map);

	fl_map->valid = valid;
}

static char *bes_filter_map_brief_info(char *buf, struct filter_item *item)
{
	int len = 0;
	struct filter_item_param *param = NULL;

	/* Add filter type info */
	len += sprintf(&buf[len], " %s", bes_filter_map_filter_type_str(item->type));

	/* Add filter item param info， such as match_mode, mac_addr, etc */
	param = (struct filter_item_param*)(&(item->param));
	switch (item->type) {
		case FILTER_MAC_DA:
		case FILTER_MAC_SA:
			len += sprintf(&buf[len], " %s(%pM)",
					(item->match_mode == RXF_MATCH_EQUAL) ? "Value" : "NValue",
					&param->mac_addr);
			break;
		case FILTER_ETH_TYPE:
			len += sprintf(&buf[len], " %s(0x%04x)",
					(item->match_mode == RXF_MATCH_EQUAL) ? "Value" : "NValue",
					param->eth_type);
			break;
		case FILTER_IPV4_SRC:
		case FILTER_IPV4_DST:
			len += sprintf(&buf[len], " %s(%pI4)",
					(item->match_mode == RXF_MATCH_EQUAL) ? "Value" : "NValue",
					&param->ip_addr);
			break;
		case FILTER_IPV4_PROTO:
			len += sprintf(&buf[len], " %s(%d)",
					(item->match_mode == RXF_MATCH_EQUAL) ? "Value" : "NValue",
					param->proto);
			break;
		case FILTER_IPV4_ARP:
			len += sprintf(&buf[len], " %d", param->of_idx);
			break;
		case FILTER_EAPOL_G_MSG1:
			len += sprintf(&buf[len], " %d", param->of_idx);
			break;
		case FILTER_MAGIC_PACKET:
			break;
		case FILTER_IPV4_MULTICAST:
			break;
		case FILTER_IPV6_MULTICAST:
			break;
		case FILTER_ALL_MULTICAST:
			break;
		case FILTER_PATTERN:
			break;
		case FILTER_IPV4_UDP_DST_SINGLE:
		case FILTER_IPV4_UDP_SRC_SINGLE:
		case FILTER_IPV4_TCP_DST_SINGLE:
		case FILTER_IPV4_TCP_SRC_SINGLE:
			len += sprintf(&buf[len], " %s(%d)",
					(item->match_mode == RXF_MATCH_EQUAL) ? "Value" : "NValue",
					param->port.s_port);
			break;
		case FILTER_IPV4_UDP_DST_RANGE:
		case FILTER_IPV4_UDP_SRC_RANGE:
		case FILTER_IPV4_TCP_DST_RANGE:
		case FILTER_IPV4_TCP_SRC_RANGE:
			len += sprintf(&buf[len], " %s(%d,%d)",
					(item->match_mode == RXF_MATCH_RANGE) ? "Range" : "ORange",
					param->port.s_port, param->port.d_port);
			break;
		case FILTER_IPV4_UDP_SRC_DST_SINGLE:
		case FILTER_IPV6_UDP_SRC_DST_SINGLE:
			len += sprintf(&buf[len], " Value(%d,%d)",
					param->port.s_port, param->port.d_port);
			break;
		case FILTER_IPV6_SRC:
		case FILTER_IPV6_DST:
			len += sprintf(&buf[len], " %s(%pI6)",
					(item->match_mode == RXF_MATCH_EQUAL) ? "Value" : "NValue",
					&param->ipv6_addr);
			break;
		case FILTER_IPV6_NS:
			len += sprintf(&buf[len], " %d", param->of_idx);
			break;
	}

	/* Add filter common info, such as f_idx, action, etc */
	len += sprintf(&buf[len], " %s %s ref=%d f_idx=%d\n",
			item->action == RXF_ACCEPT ? "accept" : "reject",
			item->enable ? "enable" : "diasble",
			item->ref_count, item->f_idx);

	return buf;
}

int bes_filter_map_show(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
		char *bref_info, char *buf, int len)
{
	struct rwnx_vif *rwnx_vif = NULL;
	struct filter_item *item = NULL;
	int cnt = 0;
	u8 vif_idx = 0, fl_idx = 0;

	spin_lock(&fl_map->list_lock);
	/* Add capacity info of fl_map */
	len += sprintf(&buf[len], "Capacity stats: rxf_idx=%d rxf_arry_idx=%d\n",
			fl_map->stats.rxf_idx, fl_map->stats.rxf_arry_idx);

	/* Add info of filters in fl_map */
	for (vif_idx = 0; vif_idx < NX_ITF_MAX; vif_idx++) {
		rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);
		if (!rwnx_vif || !rwnx_vif->ndev)
			continue;

		len += sprintf(&buf[len], "VIF: %s, Connect Status: %s, Filter Status: %s\n",
				rwnx_vif->ndev->name,
				(rwnx_vif->ch_index != RWNX_CH_NOT_SET) ? "Conn" : "Disconn",
				rwnx_hw->filter.filter_en[vif_idx] ? "Enable" :"Disable");
		for (fl_idx = 0; fl_idx < FL_MAX; fl_idx++) {
			if (!fl_map->fl_tbl[vif_idx][fl_idx].count)
				continue;

			len += sprintf(&buf[len], "%s(len=%d)\n",
					bes_filter_map_fl_type_str(fl_idx),
					fl_map->fl_tbl[vif_idx][fl_idx].count);
			cnt = 0;
			list_for_each_entry(item, &fl_map->fl_tbl[vif_idx][fl_idx].list, list) {
				cnt++;
				len += sprintf(&buf[len], "\t%d%s", cnt,
						bes_filter_map_brief_info(bref_info, item));
			}
		}
		len += sprintf(&buf[len], "\n");
	}
	spin_unlock(&fl_map->list_lock);

	return len;
}

static int bes_filter_map_filter_list_type(u8 wf_type)
{
	int wfl_type = 0;

	switch (wf_type) {
		case FILTER_ETH_TYPE:
		case FILTER_IPV4_ARP:
		case FILTER_EAPOL_G_MSG1:
		case FILTER_IPV6_NS:
			wfl_type = FL_ETH_TYPE;
			break;
		case FILTER_IPV4_SRC:
		case FILTER_IPV4_DST:
		case FILTER_IPV6_SRC:
		case FILTER_IPV6_DST:
			wfl_type = FL_IP_ADDR;
			break;
		case FILTER_IPV4_PROTO:
			wfl_type = FL_PROTO;
			break;
		case FILTER_IPV4_UDP_DST_SINGLE:
		case FILTER_IPV4_UDP_DST_RANGE:
		case FILTER_IPV4_UDP_SRC_SINGLE:
		case FILTER_IPV4_UDP_SRC_RANGE:
		case FILTER_IPV4_UDP_SRC_DST_SINGLE:
		case FILTER_IPV6_UDP_SRC_DST_SINGLE:
			wfl_type = FL_UDP_PORT;
			break;
		case FILTER_IPV4_TCP_DST_SINGLE:
		case FILTER_IPV4_TCP_DST_RANGE:
		case FILTER_IPV4_TCP_SRC_SINGLE:
		case FILTER_IPV4_TCP_SRC_RANGE:
			wfl_type = FL_TCP_PORT;
			break;
		case FILTER_MAC_DA:
		case FILTER_MAC_SA:
		case FILTER_MAGIC_PACKET:
		case FILTER_ALL_MULTICAST:
		case FILTER_IPV4_MULTICAST:
		case FILTER_IPV6_MULTICAST:
		case FILTER_PATTERN:
			wfl_type = FL_MAC_ADDR;
			break;
		default:
			wfl_type = -EINVAL;
			break;
	}

	return wfl_type;
}

static struct bes_fl *bes_filter_map_get_list(struct bes_fl_map *fl_map,
	u8 vif_idx, u8 wf_type)
{
	int wfl_type = 0;

	/* Get filter list type */
	wfl_type = bes_filter_map_filter_list_type(wf_type);
	if (wfl_type < 0)
		return NULL;

	/* Return filter list */
	return (&fl_map->fl_tbl[vif_idx][wfl_type]);
}

bool bes_filter_map_exist_filter(struct bes_fl_map *fl_map, u8 vif_idx, u8 wf_type)
{
	struct bes_fl *fl = bes_filter_map_get_list(fl_map, vif_idx, wf_type);
	struct filter_item *tmp = NULL;

	/* Check the validity of parameters and the count of filter list */
	BUG_ON(!fl);
	if (!fl->count)
		return false;

	/* Traverse search */
	list_for_each_entry(tmp, &fl->list, list) {
		if (tmp->type == wf_type)
			return true;
	}

	return false;
}

static int bes_filter_map_insert(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	u8 vif_idx, struct filter_item *item, int pos)
{
	struct bes_fl *fl = bes_filter_map_get_list(fl_map, vif_idx, item->type);
	struct filter_item *tmp = NULL;
	int wf_size = bes_filter_manager_filter_size(item->type, item->action, &item->param);
	int cnt = 0;

	/* Check the validity of parameters */
	if (!fl || wf_size < 0)
		return -EINVAL;

	/* Check if there is still enough bes_capacity */
	if ((pos < 0) || (pos > fl->count) ||
	    (fl_map->stats.rxf_idx + 1 > rwnx_hw->filter.max.rxf_idx) ||
	    (fl_map->stats.rxf_arry_idx + wf_size > rwnx_hw->filter.max.rxf_arry_idx))
		return -ERANGE;

	/* Insert item after pos */
	if (pos == 0) {
		list_add(&item->list,  &fl->list);
	} else if (pos == fl->count) {
		list_add_tail(&item->list, &fl->list);
	} else {
		list_for_each_entry(tmp, &fl->list, list) {
			cnt++;
			if (cnt == pos) {
				list_add(&item->list,  &tmp->list);
				break;
			}
		}
	}
	item->ref_count++;
	fl->count++;
	fl_map->stats.rxf_idx++;
	fl_map->stats.rxf_arry_idx += wf_size;

	return 0;
}

static int bes_filter_map_update_pos(struct bes_fl_map *fl_map, u8 vif_idx, u8 wf_type)
{
	struct bes_fl *fl = bes_filter_map_get_list(fl_map, vif_idx, wf_type);
	struct filter_item *tmp = NULL;
	int pos = 0;

	/* Check the validity of parameters */
	BUG_ON(!fl);

	/* Check if there are filter items in the list */
	if (!fl->count)
		return 0;

	/* Update pos to position after FILTER_IPV4_MULTICAST and FILTER_IPV6_MULTICAST */
	list_for_each_entry(tmp, &fl->list, list) {
		if (tmp->type != FILTER_IPV4_MULTICAST &&
		    tmp->type != FILTER_IPV6_MULTICAST)
			break;
		pos++;
	}

	return pos;
}

static int bes_filter_map_insert_to_mac_addr_list(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, u8 wf_type, void *addr, u8 match_mode, u8 action)
{
	struct filter_item *item = NULL;
	int ret = 0;

	/* Create filter item */
	if (wf_type == FILTER_MAC_DA || wf_type == FILTER_MAC_SA) {
		item = bes_filter_manager_create_mac_addr(vif_idx, wf_type,
				(struct mac_addr *)addr, match_mode, action);
	} else if (wf_type == FILTER_IPV4_MULTICAST ||
		   wf_type == FILTER_IPV6_MULTICAST ||
		   wf_type == FILTER_ALL_MULTICAST) {
		item = bes_filter_manager_create_multicast(vif_idx, wf_type,
				match_mode, action);
	} else if (wf_type == FILTER_MAGIC_PACKET) {
		item = bes_filter_manager_create_magic_packet(vif_idx, wf_type,
				(struct mac_addr *)addr, match_mode, action);
	} else if (wf_type == FILTER_PATTERN) {
		item = bes_filter_manager_create_pattern(vif_idx, wf_type,
				(struct cfg80211_pkt_pattern *)addr, match_mode, action);
	}
	if (!item)
		return -EINVAL;

	spin_lock(&fl_map->list_lock);
	/* When adding filters other than FILTER_IPV4_MULTICAST and FILTER_IPV6_MULTICAST
	   in the header of the ifl_map's mac_addr list, it is necessary to ensure that
	   they are added after them. */
	if (pos == 0 && fl_map == &rwnx_hw->filter.ifl_map &&
	    wf_type != FILTER_IPV4_MULTICAST && wf_type != FILTER_IPV6_MULTICAST)
		pos = bes_filter_map_update_pos(fl_map, vif_idx, wf_type);

	/* Insert filter to ifl_map list */
	ret = bes_filter_map_insert(rwnx_hw, fl_map, vif_idx, item, pos);
	spin_unlock(&fl_map->list_lock);
	if (ret)
		kfree(item);

	return ret;
}

int bes_filter_map_insert_mac_addr(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, u8 wf_type, struct mac_addr *addr, u8 match_mode, u8 action)
{
	BUG_ON(wf_type != FILTER_MAC_DA && wf_type != FILTER_MAC_SA);

	return bes_filter_map_insert_to_mac_addr_list(rwnx_hw, fl_map, pos,
		vif_idx, wf_type, addr, match_mode, action);
}

int bes_filter_map_insert_multicast_filter(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, u8 wf_type, u8 action)
{
	BUG_ON(wf_type != FILTER_IPV4_MULTICAST && wf_type != FILTER_IPV6_MULTICAST &&
	       wf_type != FILTER_ALL_MULTICAST);

	/* When inserting multicast filter to mac_addr list,
	   addr parameter is set to NULL and match_mode is set to 0 */
	return bes_filter_map_insert_to_mac_addr_list(rwnx_hw, fl_map,
		pos, vif_idx, wf_type, NULL,
		BES_FILTER_PARAM_UNCARE_MATCH_MODE, action);
}

int bes_filter_map_insert_magic_packet(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, struct mac_addr *addr, u8 action)
{
	/* When inserting magic_packet filter to mac_addr list, match_mode is set to 0 */
	return bes_filter_map_insert_to_mac_addr_list(rwnx_hw, fl_map,
		pos, vif_idx, FILTER_MAGIC_PACKET, addr,
		BES_FILTER_PARAM_UNCARE_MATCH_MODE, action);
}

int bes_filter_map_insert_pattern(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, struct cfg80211_pkt_pattern *pattern, u8 match_mode, u8 action)
{
	return bes_filter_map_insert_to_mac_addr_list(rwnx_hw, fl_map,
		pos, vif_idx, FILTER_PATTERN, pattern, match_mode, action);
}

static int bes_filter_map_insert_to_eth_type_list(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, u8 wf_type, u16 eth_type, u8 match_mode, u8 action)
{
	struct filter_item *item = NULL;
	int ret = 0;

	/* Create filter item */
	if (wf_type == FILTER_ETH_TYPE) {
		item = bes_filter_manager_create_eth_type(vif_idx, wf_type, eth_type, match_mode, action);
	} else if (wf_type == FILTER_IPV4_ARP) {
		item = bes_filter_manager_create_ipv4_arp(vif_idx, wf_type, (u8)eth_type, match_mode, action);
	} else if (wf_type == FILTER_IPV6_NS) {
		item = bes_filter_manager_create_ipv6_ns(vif_idx, wf_type, (u8)eth_type, match_mode, action);
	} else if (wf_type == FILTER_EAPOL_G_MSG1) {
		item = bes_filter_manager_create_gtk_rekey(vif_idx, wf_type, (u8)eth_type, match_mode, action);
	}
	if (!item)
		return -EINVAL;

	/* Insert filter to ifl_map list */
	spin_lock(&fl_map->list_lock);
	ret = bes_filter_map_insert(rwnx_hw, fl_map, vif_idx, item, pos);
	spin_unlock(&fl_map->list_lock);
	if (ret)
		kfree(item);

	return ret;
}

int bes_filter_map_insert_eth_type(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, u16 eth_type, u8 match_mode, u8 action)
{
	return bes_filter_map_insert_to_eth_type_list(rwnx_hw, fl_map,
		pos, vif_idx, FILTER_ETH_TYPE, eth_type, match_mode, action);
}

int bes_filter_map_insert_ipv4_arp(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, u8 of_idx)
{
	/* When inserting arp offload filter to_eth_type list, eth_type parameter is used to receive of_idx,
	   match_mode is set to 0 and action is set to RXF_ACCEPT*/
	return bes_filter_map_insert_to_eth_type_list(rwnx_hw, fl_map,
		pos, vif_idx, FILTER_IPV4_ARP, of_idx,
		BES_FILTER_PARAM_UNCARE_MATCH_MODE, RXF_ACCEPT);
}

int bes_filter_map_insert_ipv6_ns(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, u8 of_idx)
{
	/* When inserting ns offload filter to_eth_type list, eth_type parameter is used to receive of_idx,
	   match_mode is set to 0 and action is set to RXF_ACCEPT*/
	return bes_filter_map_insert_to_eth_type_list(rwnx_hw, fl_map,
		pos, vif_idx, FILTER_IPV6_NS, of_idx,
		BES_FILTER_PARAM_UNCARE_MATCH_MODE, RXF_ACCEPT);
}

int bes_filter_map_insert_gtk_rekey(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, u8 of_idx)
{
	/* When inserting arp offload filter to_eth_type list, eth_type parameter is used to receive of_idx,
	 * match_mode is set to 0 and action is set to RXF_ACCEPT
	 */
	return bes_filter_map_insert_to_eth_type_list(rwnx_hw, fl_map,
		pos, vif_idx, FILTER_EAPOL_G_MSG1, of_idx,
		BES_FILTER_PARAM_UNCARE_MATCH_MODE, RXF_ACCEPT);
}

static int bes_filter_map_insert_to_ip_addr_list(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, u8 wf_type, void *ip, u8 match_mode, u8 action)
{
	struct filter_item *item = NULL;
	int ret = 0;

	/* Create filter item */
	if (wf_type == FILTER_IPV4_SRC || wf_type == FILTER_IPV4_DST) {
		item = bes_filter_manager_create_ipv4_ip_addr(vif_idx, wf_type,
				*((u32 *)ip), match_mode, action);
	} else if (wf_type == FILTER_IPV6_SRC || wf_type == FILTER_IPV6_DST) {
		item = bes_filter_manager_create_ipv6_ip_addr(vif_idx, wf_type,
				((struct in6_addr *)ip), match_mode, action);
	}
	if (!item)
		return -EINVAL;

	/* Insert filter to ifl_map list */
	spin_lock(&fl_map->list_lock);
	ret = bes_filter_map_insert(rwnx_hw, fl_map, vif_idx, item, pos);
	spin_unlock(&fl_map->list_lock);
	if (ret)
		kfree(item);

	return ret;
}

int bes_filter_map_insert_ipv4_addr(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, u8 wf_type, u32 *ip, u8 match_mode, u8 action)
{
	BUG_ON(wf_type != FILTER_IPV4_SRC && wf_type != FILTER_IPV4_DST);

	return bes_filter_map_insert_to_ip_addr_list(rwnx_hw, fl_map,
		pos, vif_idx, wf_type, ip, match_mode, action);
}

int bes_filter_map_insert_ipv6_addr(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, u8 wf_type, struct in6_addr *ip, u8 match_mode, u8 action)
{
	BUG_ON(wf_type != FILTER_IPV6_SRC && wf_type != FILTER_IPV6_DST);

	return bes_filter_map_insert_to_ip_addr_list(rwnx_hw, fl_map,
		pos, vif_idx, wf_type, ip, match_mode, action);
}

static int bes_filter_map_insert_to_proto_list(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, u8 wf_type, u8 proto, u8 match_mode, u8 action)
{
	struct filter_item *item = NULL;
	int ret = 0;

	/* Create filter item */
	if (wf_type == FILTER_IPV4_PROTO) {
		item = bes_filter_manager_create_ipv4_proto(vif_idx, wf_type, proto, match_mode, action);
	}
	if (!item)
		return -EINVAL;

	/* Insert filter to ifl_map list */
	spin_lock(&fl_map->list_lock);
	ret = bes_filter_map_insert(rwnx_hw, fl_map, vif_idx, item, pos);
	spin_unlock(&fl_map->list_lock);
	if (ret)
		kfree(item);

	return ret;
}

int bes_filter_map_insert_ipv4_proto(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, u8 proto, u8 match_mode, u8 action)
{
	return bes_filter_map_insert_to_proto_list(rwnx_hw, fl_map,
		pos, vif_idx, FILTER_IPV4_PROTO, proto, match_mode, action);
}

int bes_filter_map_insert_to_port_list(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, u8 wf_type, u16 s_port, u16 d_port, u8 match_mode, u8 action)
{
	struct filter_item *item = NULL;
	int ret = 0;

	/* Create filter item */
	if (wf_type == FILTER_IPV4_UDP_DST_SINGLE || wf_type == FILTER_IPV4_UDP_DST_RANGE ||
	    wf_type == FILTER_IPV4_UDP_SRC_SINGLE || wf_type == FILTER_IPV4_UDP_SRC_RANGE ||
	    wf_type == FILTER_IPV4_TCP_DST_SINGLE || wf_type == FILTER_IPV4_TCP_DST_RANGE ||
	    wf_type == FILTER_IPV4_TCP_SRC_SINGLE || wf_type == FILTER_IPV4_TCP_SRC_RANGE)
		item = bes_filter_manager_create_port(vif_idx, wf_type,
				s_port, d_port, match_mode, action);
	else if (wf_type == FILTER_IPV4_UDP_SRC_DST_SINGLE || wf_type == FILTER_IPV6_UDP_SRC_DST_SINGLE)
		item = bes_filter_manager_create_port(vif_idx, wf_type,
				s_port, d_port, BES_FILTER_PARAM_UNCARE_MATCH_MODE, action); // match_mode is set to 0
	if (!item)
		return -EINVAL;

	/* Insert filter to ifl_map list */
	spin_lock(&fl_map->list_lock);
	ret = bes_filter_map_insert(rwnx_hw, fl_map, vif_idx, item, pos);
	spin_unlock(&fl_map->list_lock);
	if (ret)
		kfree(item);

	return ret;
}

int bes_filter_map_delete_by_pos(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	struct bes_fl *fl, int pos)
{
	int ret = -EINVAL;
	struct filter_item *item = NULL, *tmp = NULL;
	int cnt = 0;
	struct list_head list_set_head;

	/* Check the validity of parameters */
	if (!rwnx_hw || !fl_map || !fl)
		return -EINVAL;
	if (pos < 1 || pos > fl->count)
		return -ERANGE;

	/* Init list_set_head */
	INIT_LIST_HEAD(&list_set_head);

	spin_lock(&fl_map->list_lock);
	/* Delete specified item from ifl_map */
	list_for_each_entry_safe(item, tmp, &fl->list, list) {
		cnt++;
		if (cnt == pos) {
			bes_filter_manager_delete(&fl_map->stats,
					fl, item, &list_set_head);

			ret = 0;
			break;
		}
	}
	spin_unlock(&fl_map->list_lock);

	/* Flush list_set_head list */
	if (!ret)
		bes_filter_manager_flush_list_set(&list_set_head);

	return ret;
}

void bes_filter_map_delete_by_vif(struct bes_fl_map *fl_map, u8 vif_idx)
{
	int fl_idx = 0;
	struct bes_fl *fl = NULL;
	struct filter_item *item = NULL, *tmp = NULL;
	struct list_head list_set_head;

	/* Check the validity of parameters */
	if (!fl_map)
		return;

	/* Init list_set_head */
	INIT_LIST_HEAD(&list_set_head);

	spin_lock(&fl_map->list_lock);
	/* Add all filters of vif in fl_map to list_set_head */
	for (fl_idx = 0; fl_idx < FL_MAX; fl_idx++) {
		fl = &fl_map->fl_tbl[vif_idx][fl_idx];
		if (!fl || !fl->count)
			continue;

		list_for_each_entry_safe(item, tmp, &fl->list, list)
			bes_filter_manager_delete(&fl_map->stats,
					fl, item, &list_set_head);
	}
	spin_unlock(&fl_map->list_lock);

	/* Flush list_set_head list */
	bes_filter_manager_flush_list_set(&list_set_head);
}

void bes_filter_map_delete_all(struct bes_fl_map *fl_map)
{
	u8 vif_idx = 0, fl_idx = 0;
	struct bes_fl *fl = NULL;
	struct filter_item *item = NULL, *tmp = NULL;

	/* Check the validity of parameters */
	if (!fl_map)
		return;

	/* Delete all filters in fl_map */
	spin_lock(&fl_map->list_lock);
	for (vif_idx = 0; vif_idx < NX_ITF_MAX; vif_idx++) {
		for (fl_idx = 0; fl_idx < FL_MAX; fl_idx++) {
			fl = &fl_map->fl_tbl[vif_idx][fl_idx];
			if (!fl || !fl->count)
				continue;

			list_for_each_entry_safe(item, tmp, &fl->list, list) {
				list_del(&item->list);
				kfree(item);
			}
			fl->count = 0;
		}
	}

	/* Reset stats */
	fl_map->stats.rxf_idx = 0;
	fl_map->stats.rxf_arry_idx = 0;
	spin_unlock(&fl_map->list_lock);
}

void bes_filter_map_del_filter_by_type(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map, u8 vif_idx, u8 wf_type)
{
	int fl_type = bes_filter_map_filter_list_type(wf_type);
	struct bes_fl *fl = NULL;
	struct filter_item *item = NULL, *tmp = NULL;
	struct list_head list_set_head;

	/* Check the validity of parameters */
	if (!rwnx_hw || !fl_map || vif_idx >= NX_VIRT_DEV_MAX || fl_type < 0)
		return;

	/* Init list_set_head */
	INIT_LIST_HEAD(&list_set_head);

	spin_lock(&fl_map->list_lock);
	/* Get filter list and check its count */
	fl = &fl_map->fl_tbl[vif_idx][fl_type];
	if (fl->count == 0) {
		spin_unlock(&fl_map->list_lock);
		return;
	}

	list_for_each_entry_safe(item, tmp, &fl->list, list) {
		/* Not we find, go to the next */
		if (item->type != wf_type)
			continue;

		/* Add the found filter to list_set_head */
		bes_filter_manager_delete(&fl_map->stats,
					fl, item, &list_set_head);

		/* If the filter to be deleted is arp/ns offload filter, clear flags */
		if (item->type == FILTER_IPV4_ARP)
			rwnx_hw->filter.arp_offload[vif_idx] = false;
		else if (item->type == FILTER_IPV6_NS)
			rwnx_hw->filter.ns_offload[vif_idx] = false;
		break;
	}
	spin_unlock(&fl_map->list_lock);

	/* Flush list_set_head list */
	bes_filter_manager_flush_list_set(&list_set_head);
}

void bes_filter_map_del_multicast_related(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map, u8 vif_idx)
{
	struct bes_fl *fl = NULL;
	struct filter_item *item = NULL, *tmp = NULL;
	struct filter_item_param *param = NULL;
	struct list_head list_set_head;

	/* Check the validity of parameter */
	if (!rwnx_hw || !fl_map || vif_idx >= NX_VIRT_DEV_MAX)
		return;

	/* Init list_set_head */
	INIT_LIST_HEAD(&list_set_head);

	spin_lock(&fl_map->list_lock);
	/* Get mac addr filter list and check its count */
	fl = &rwnx_hw->filter.ifl_map.fl_tbl[vif_idx][FL_MAC_ADDR];
	if (fl->count == 0) {
		spin_unlock(&fl_map->list_lock);
		return;
	}

	list_for_each_entry_safe(item, tmp, &fl->list, list) {
		/* Skip filters that are not multicast related */
		if ((item->type != FILTER_IPV4_MULTICAST) &&
		    (item->type != FILTER_IPV6_MULTICAST) &&
		    (item->type != FILTER_ALL_MULTICAST) &&
		    (item->type != FILTER_MAC_DA)) {
			continue;
		} else if (item->type == FILTER_MAC_DA) {
			param = &(item->param);
			if (!is_multicast_ether_addr((const u8 *)&param->mac_addr))
				continue;
		}

		/* Add the found filter to list_set_head */
		bes_filter_manager_delete(&fl_map->stats,
					fl, item, &list_set_head);
	}
	spin_unlock(&fl_map->list_lock);

	/* Flush list_set_head list */
	bes_filter_manager_flush_list_set(&list_set_head);
}

void bes_filter_map_del_gtk_rekey(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map, u8 vif_idx)
{
	struct bes_fl *fl= NULL;
	struct filter_item *item = NULL, *tmp = NULL;
	struct list_head list_set_head;

	/* Check the validity of parameter */
	if (!rwnx_hw || !fl_map || vif_idx >= NX_VIRT_DEV_MAX)
		return;

	/* Init list_set_head */
	INIT_LIST_HEAD(&list_set_head);

	spin_lock(&fl_map->list_lock);
	/* Get eth_type filter list and check its count */
	fl= &fl_map->fl_tbl[vif_idx][FL_ETH_TYPE];
	if (fl->count == 0) {
		spin_unlock(&fl_map->list_lock);
		return;
	}

	/* Add item to list */
	list_for_each_entry_safe(item, tmp, &fl->list, list) {
		if (item->type != FILTER_EAPOL_G_MSG1)
			continue;

		/* Add the found filter to list_set_head */
		bes_filter_manager_delete(&fl_map->stats,
				fl, item, &list_set_head);
		rwnx_hw->filter.gtk_rekey_vif_idx = RWNX_INVALID_VIF;
		break;
	}
	spin_unlock(&fl_map->list_lock);

	/* Flush list_set_head list */
	bes_filter_manager_flush_list_set(&list_set_head);
}

static void bes_filter_map_set_to_device(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	struct list_head *list_set_head)
{
	u8 vif_idx = 0, fl_type = 0;
	struct rwnx_vif *rwnx_vif = NULL;
	struct filter_item *item = NULL;

	for (vif_idx = 0; vif_idx < NX_ITF_MAX; vif_idx++) {
		rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);
		if (!rwnx_vif)
			continue;

		for (fl_type = 0; fl_type < FL_MAX; fl_type++) {
			if (!fl_map->fl_tbl[vif_idx][fl_type].count)
				continue;

			list_for_each_entry(item, &fl_map->fl_tbl[vif_idx][fl_type].list, list) {
				list_add_tail(&item->list_set, list_set_head);
				item->ref_count++;
			}
		}
	}
}

int bes_filter_map_sync_ifl_map_to_device(struct rwnx_hw *rwnx_hw)
{
	struct list_head list_set_head;
	struct bes_fl_map *ifl_map = NULL;

	/* Check the validity of parameters */
	if (!rwnx_hw)
		return -EINVAL;

	/* Check the validity of ifl_map */
	ifl_map = &rwnx_hw->filter.ifl_map;
	if (!bes_filter_map_get_valid(ifl_map)) {
		bes_warn(BES_DBG_FILTER_MAP, "%s ifl map is invalid", __func__);
		return 0;
	}


	/* Init list_set_head */
	INIT_LIST_HEAD(&list_set_head);

	spin_lock(&ifl_map->list_lock);
	/* Add filters in ifl_map to the list_set_head */
	bes_filter_map_set_to_device(rwnx_hw, ifl_map, &list_set_head);

	spin_unlock(&ifl_map->list_lock);

	/* Sync filters in list_set_head to device */
	return bes_filter_manager_sync_to_device(rwnx_hw, &list_set_head);
}

int bes_filter_map_sync_cfl_map_to_device(struct rwnx_hw *rwnx_hw, struct bes_fl_map *cfl_map)
{
	struct list_head list_set_head;
	struct bes_fl_map *ifl_map = &rwnx_hw->filter.ifl_map;

	/* Check the validity of parameters */
	if (!rwnx_hw || !cfl_map)
		return -EINVAL;

	/* Check the validity of ifl_map and cfl_map */
	if (!bes_filter_map_get_valid(ifl_map) &&
	    !bes_filter_map_get_valid(cfl_map)) {
		bes_warn(BES_DBG_FILTER_MAP, "%s ifl and cfl map are both invalid", __func__);
		return 0;
	}

	/* Init list_set_head */
	INIT_LIST_HEAD(&list_set_head);

	/* If ifl_map is valid, add filters in ifl_map to the list_set_head */
	if (bes_filter_map_get_valid(ifl_map)) {
		spin_lock(&ifl_map->list_lock);
		bes_filter_map_set_to_device(rwnx_hw, ifl_map, &list_set_head);
		spin_unlock(&ifl_map->list_lock);
	}

	/* If cfl_map is valid, add filters in cfl_map to the list_set_head */
	if (bes_filter_map_get_valid(cfl_map)) {
		spin_lock(&cfl_map->list_lock);
		bes_filter_map_set_to_device(rwnx_hw, cfl_map, &list_set_head);
		spin_unlock(&cfl_map->list_lock);
	}

	/* Sync filters in list_set_head to device */
	return bes_filter_manager_sync_to_device(rwnx_hw, &list_set_head);
}

void bes_filter_map_reset_f_idx(struct bes_fl_map *fl_map)
{
	u8 vif_idx = 0, fl_idx = 0;
	struct filter_item *item = NULL;
	struct bes_fl *fl = NULL;

	/* Check the validity of parameters */
	if (!fl_map)
		return;

	/* reset f_idx of all filters in fl_map */
	spin_lock(&fl_map->list_lock);
	for (vif_idx = 0; vif_idx < NX_ITF_MAX; vif_idx++) {
		for (fl_idx = 0; fl_idx < FL_MAX; fl_idx++) {
			fl = &fl_map->fl_tbl[vif_idx][fl_idx];
			if (!fl || !fl->count)
				continue;

			list_for_each_entry(item, &fl->list, list)
				item->f_idx = -1;
		}
	}
	spin_unlock(&fl_map->list_lock);
}

static void bes_filter_list_init(struct bes_fl *fl)
{
	INIT_LIST_HEAD(&fl->list);
	fl->count = 0;
}

void bes_filter_map_init(struct bes_fl_map *fl_map)
{
	u8 vif_idx = 0, fl_idx = 0;

	for (vif_idx = 0; vif_idx < NX_ITF_MAX; vif_idx++) {
		for (fl_idx = 0; fl_idx < FL_MAX; fl_idx++)
			bes_filter_list_init(&fl_map->fl_tbl[vif_idx][fl_idx]);
	}
	fl_map->stats.rxf_idx = 0;
	fl_map->stats.rxf_arry_idx = 0;
	fl_map->valid = false;
	spin_lock_init(&fl_map->list_lock);
}

void bes_filter_map_deinit(struct bes_fl_map *fl_map)
{
	u8 vif_idx = 0, fl_idx = 0;
	struct bes_fl *fl = NULL;
	struct filter_item *item = NULL, *tmp = NULL;

	for (vif_idx = 0; vif_idx < NX_ITF_MAX; vif_idx++) {
		for (fl_idx = 0; fl_idx < FL_MAX; fl_idx++) {
			fl = &fl_map->fl_tbl[vif_idx][fl_idx];
			if (!fl || !fl->count)
				continue;

			list_for_each_entry_safe(item, tmp, &fl->list, list) {
				list_del(&item->list);
				kfree(item);
			}

			fl->count = 0;
		}
	}
	fl_map->stats.rxf_idx = 0;
	fl_map->valid = false;
	fl_map->stats.rxf_arry_idx = 0;
}