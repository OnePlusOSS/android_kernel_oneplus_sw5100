/*
 * @file bes_filter_manager.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "rwnx_defs.h"
#include "bes_gtk_offload.h"
#include "bes_log.h"
#include "bes_filter_manager.h"
#include "bes_filter_map.h"
#include "bes_filter.h"
#include "bes_wpa_priv_cmd.h"
#include "bes_signal_test.h"

int bes_filter_manager_filter_size(u8 wf_type, u8 action, struct filter_item_param *param)
{
	int size = 0;
	const int bes_filter_item_size = sizeof(struct bes_filter_item);

	switch (wf_type) {
		case FILTER_MAC_DA:
		case FILTER_MAC_SA:
			size = bes_filter_item_size +
				(MAC_ADDR_LEN << 1) /* mac address match item */;
			break;
		case FILTER_ETH_TYPE:
			size = bes_filter_item_size +
				(2 << 1) /* ethernet type match item */;
			break;
		case FILTER_IPV4_SRC:
		case FILTER_IPV4_DST:
			size = 2 * bes_filter_item_size +
				(2 << 1) /* ip type match item */ +
				(4 << 1) /* ip address type match item */;
			break;
		case FILTER_IPV4_PROTO:
			size = 2 * bes_filter_item_size +
				(2 << 1) /* ip type match item */ +
				(1 << 1) /* ip protocol match item */;
			break;
		case FILTER_IPV4_ARP:
			size = 2 * bes_filter_item_size +
				(2 << 1) /* match ARP packet */ +
				(2 << 1) /* match ARP Request packet */;
			break;
		case FILTER_EAPOL_G_MSG1:
			size = 2 * bes_filter_item_size +
				(2 << 1) /* match ETH_P_PAE */ +
				(sizeof(struct eapol_header) << 1) /* match EAPOL packet */;
			break;
		case FILTER_MAGIC_PACKET:
			size = 4 * bes_filter_item_size +
				(1 << 1) /* udp type match item */ +
				(MAC_ADDR_LEN << 1) /* sync stream match item */ +
				(MAC_ADDR_LEN << 1) * 2 /* match destination mac twice */;
			break;
		case FILTER_IPV4_MULTICAST:
			size = bes_filter_item_size +
				(3 << 1) /* mac dest match item */;
			break;
		case FILTER_IPV6_MULTICAST:
			size = bes_filter_item_size +
				(2 << 1) /* mac dest match item */;
			break;
		case FILTER_ALL_MULTICAST:
			if (action == RXF_ACCEPT)
				size = bes_filter_item_size +
					(1 << 1) /* multicast and boardcast match item */;
			else if (action == RXF_REJECT)
				size = 2 * bes_filter_item_size +
					(1 << 1) /* multicast and boardcast match item */ +
					(1 << 1) /* not equel to boardcast */;
			else
				size = -1;
			break;
		case FILTER_PATTERN:
			if (param)
				size = bes_filter_item_size +
					(param->pattern->pattern_len << 1); // pattern match item
			else
				size = -1;
			break;
		case FILTER_IPV4_UDP_DST_SINGLE:
		case FILTER_IPV4_UDP_SRC_SINGLE:
		case FILTER_IPV4_TCP_DST_SINGLE:
		case FILTER_IPV4_TCP_SRC_SINGLE:
			size = 3 * bes_filter_item_size +
				(2 << 1) /* ip type match item*/ +
				(1 << 1) /* udp/tcp packet match item */+
				(2 << 1) /* port value match item */;
			break;
		case FILTER_IPV4_UDP_DST_RANGE:
		case FILTER_IPV4_UDP_SRC_RANGE:
		case FILTER_IPV4_TCP_DST_RANGE:
		case FILTER_IPV4_TCP_SRC_RANGE:
			size = 3 * bes_filter_item_size +
				(2 << 1) /* ip type match item*/ +
				(1 << 1) /* udp/tcp packet match item */ +
				6 /* port range match item */;
			break;
		case FILTER_IPV4_UDP_SRC_DST_SINGLE:
		case FILTER_IPV6_UDP_SRC_DST_SINGLE:
			size = 4 * bes_filter_item_size +
				(2 << 1) /* ip type match item */ +
				(1 << 1) /* match udp Request packet */ +
				(2 << 1) /* match udp src port */ +
				(2 << 1) /* match udp dst port */;
			break;
		case FILTER_IPV6_SRC:
		case FILTER_IPV6_DST:
			size = 2 * bes_filter_item_size +
				(2 << 1) /* ipv6 type match item */ +
				(16 << 1) /* ipv6 address type match item */;
			break;
		case FILTER_IPV6_NS:
			size = 4 * bes_filter_item_size +
				(2 << 1) /* ipv6 type match item*/ +
				(1 << 1) /* icmpv6 type match item */ +
				(1 << 1) /* icmpv6 neighbor solicitation type match item */;
			break;
		default:
			size = -1 /* error, no match */;
			break;
	}

	return size;
}

struct filter_item *bes_filter_manager_create(u8 vif_idx, u8 type, u8 match_mode, u8 action)
{
	struct filter_item *item = NULL;

	/* Malloc memory */
	item = (struct filter_item *)kzalloc(sizeof(struct filter_item), GFP_ATOMIC);
	if (!item) {
		bes_err(BES_DBG_FILTER_MANAGER,
			"%s: bes filter allocation failed\n", __func__);
		return NULL;
	}

	/* Initialize general parameters */
	INIT_LIST_HEAD(&item->list);
	INIT_LIST_HEAD(&item->list_set);
	item->ref_count = 0;
	item->f_idx = -1;
	item->type = type;
	item->enable = true;
	item->vif_idx = vif_idx;
	item->match_mode = match_mode;
	item->action = action;
	memset(&item->param, 0x00, sizeof(struct filter_item_param));

	return item;
}

void bes_filter_manager_delete(struct bes_capacity *stats, struct bes_fl *fl,
			       struct filter_item *item, struct list_head *list_set_head)
{
	int wf_size = 0;

	/* Check the validity of parameters */
	if (!stats || !fl || !item || !list_set_head)
		return;

	/* Obtain the size to be deleted and determine if it can be deleted */
	wf_size = bes_filter_manager_filter_size(item->type, item->action, &item->param);
	BUG_ON(stats->rxf_idx < 1 ||
		wf_size < 0 || stats->rxf_arry_idx < wf_size);

	/* Delete filter item from filter list */
	list_del(&item->list);
	fl->count--;
	item->ref_count--;
	BUG_ON(fl->count < 0 || item->ref_count < 0);
	if (!item->ref_count) {
		list_add_tail(&item->list_set, list_set_head);
		item->ref_count++;
	}

	/* Update stats value */
	stats->rxf_idx--;
	stats->rxf_arry_idx -= wf_size;
}

static int bes_filter_manager_filter_ctrl(struct rwnx_hw *rwnx_hw, u8 vif_idx, bool en)
{
	int ret = 0;

	ret = bes_packet_filter_ctrl(rwnx_hw, vif_idx, en);
	if (ret)
		return ret;
	rwnx_hw->filter.filter_en[vif_idx] = en;

	return 0;
}

int bes_filter_manager_enable(struct rwnx_hw *rwnx_hw, u8 vif_idx)
{
	int ret = 0;
	struct rwnx_vif *rwnx_vif = NULL;

	/* Check the validity of parameters */
	if (!rwnx_hw)
		return -EINVAL;

	/* Obtaining rwnx_vif, and check if it is connected */
	rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);
	if (!rwnx_vif) {
		bes_err(BES_DBG_FILTER_MANAGER,
			"%s ERROR: vif:%d is not find\n", __func__, vif_idx);
		return -EINVAL;
	}

	/* Avoiding repeated enabling */
	if (rwnx_hw->filter.filter_en[vif_idx])
		return -EALREADY;

	/* Enable filter of a certain interface */
	ret = bes_filter_manager_filter_ctrl(rwnx_hw, vif_idx, true);
	if (ret)
		return ret;

	return 0;
}

static int bes_filter_manager_enable_all(struct rwnx_hw *rwnx_hw)
{
	int ret = 0;
	u8 vif_idx = 0;
	struct rwnx_vif *rwnx_vif = NULL;

	for (vif_idx = 0; vif_idx < NX_ITF_MAX; vif_idx++) {
		rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);
		/* If it has been enable, go to the next one */
		if (!rwnx_vif || rwnx_hw->filter.filter_en[vif_idx])
			continue;

		/* Enable filter of a certain interface */
		ret = bes_filter_manager_filter_ctrl(rwnx_hw, vif_idx, true);
		if (ret)
			return ret;
	}

	return 0;
}

int bes_filter_manager_disable(struct rwnx_hw *rwnx_hw, u8 vif_idx)
{
	int ret = 0;
	struct rwnx_vif *rwnx_vif = NULL;

	/* Check the validity of parameters */
	if (!rwnx_hw)
		return -EINVAL;

	/* Obtaining rwnx_vif */
	rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);
	if (!rwnx_vif)
		return -EINVAL;

	/*Avoiding repeated disability */
	if (rwnx_hw->filter.filter_en[vif_idx] != true)
		return -EALREADY;

	/* Disable filter of a certain interface */
	ret = bes_filter_manager_filter_ctrl(rwnx_hw, vif_idx, false);
	if (ret)
		return ret;

	return 0;
}

int bes_filter_manager_disable_all(struct rwnx_hw *rwnx_hw)
{
	int ret = 0;
	u8 vif_idx = 0;
	struct rwnx_vif *rwnx_vif = NULL;

	/* Check the validity of parameters */
	if (!rwnx_hw)
		return -EINVAL;

	for (vif_idx = 0; vif_idx < NX_ITF_MAX; vif_idx++) {
		/* If not enable, go to the next one */
		if (!rwnx_hw->filter.filter_en[vif_idx])
			continue;

		/* Obtaining rwnx_vif, and don't care if it is connected */
		rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);
		if (!rwnx_vif) {
			bes_err(BES_DBG_FILTER_MANAGER, "%s vif invalid, vif_index:%d \n", __func__, vif_idx);
			return -EINVAL;
		}

		/* Disable filter of a certain interface */
		ret = bes_filter_manager_filter_ctrl(rwnx_hw, vif_idx, false);
		if (ret)
			return ret;
	}

	return 0;
}

static void bes_filter_manager_del_from_list_set(struct filter_item *item)
{
	list_del(&item->list_set);
	item->ref_count--;
	BUG_ON(item->ref_count < 0);
	if (!item->ref_count)
		kfree(item);
}

static int bes_filter_manager_set_to_device(struct rwnx_hw *rwnx_hw, struct filter_item *item)
{
	int f_idx = 0;
	struct filter_item_param *param = (struct filter_item_param*)(&(item->param));

	/* Set filter to device */
	switch (item->type) {
		case FILTER_MAC_DA:
			f_idx = bes_filter_add_mac_da_match(rwnx_hw, item->vif_idx,
					&param->mac_addr, item->match_mode, item->action);
			break;
		case FILTER_MAC_SA:
			f_idx = bes_filter_add_mac_sa_match(rwnx_hw, item->vif_idx,
					&param->mac_addr, item->match_mode, item->action);
			break;
		case FILTER_ETH_TYPE:
			f_idx = bes_filter_add_eth_type_match(rwnx_hw, item->vif_idx,
					param->eth_type, item->match_mode, item->action);
			break;
		case FILTER_IPV4_SRC:
			f_idx = bes_filter_add_ipv4_src_match(rwnx_hw, item->vif_idx,
					param->ip_addr, item->match_mode, item->action);
			break;
		case FILTER_IPV4_DST:
			f_idx = bes_filter_add_ipv4_dst_match(rwnx_hw, item->vif_idx,
					param->ip_addr, item->match_mode, item->action);
			break;
		case FILTER_IPV4_PROTO:
			f_idx = bes_filter_add_ipv4_proto_match(rwnx_hw, item->vif_idx,
					param->proto, item->match_mode, item->action);
			break;
		case FILTER_IPV4_ARP:
			f_idx = bes_filter_add_ipv4_arp_match(rwnx_hw, item->vif_idx,
					param->of_idx);
			break;
		case FILTER_EAPOL_G_MSG1:
			f_idx = bes_filter_set_gtk_rekey_offload(rwnx_hw, item->vif_idx,
					param->of_idx);
			break;
		case FILTER_MAGIC_PACKET:
			f_idx = bes_filter_add_magic_packet_match(rwnx_hw, item->vif_idx,
					&param->mac_addr, item->action);
			break;
		case FILTER_IPV4_MULTICAST:
			f_idx = bes_filter_add_ipv4_multicast_match(rwnx_hw, item->vif_idx,
					item->action);
			break;
		case FILTER_IPV6_MULTICAST:
			f_idx = bes_filter_add_ipv6_multicast_match(rwnx_hw, item->vif_idx,
					item->action);
			break;
		case FILTER_ALL_MULTICAST:
			f_idx = bes_filter_add_all_multicast_match(rwnx_hw, item->vif_idx,
					item->action);
			bes_info(BES_DBG_FILTER_MANAGER,
				 "%s, set all multicast filter on vif-%d.action:%d\n",
				 __func__, item->vif_idx, item->action);
			break;
		case FILTER_PATTERN:
			f_idx = bes_filter_add_pattern_match(rwnx_hw, item->vif_idx,
					param->pattern, item->match_mode, item->action);
			break;
		case FILTER_IPV4_UDP_DST_SINGLE:
			f_idx = bes_filter_add_ipv4_udp_dst_match_single(rwnx_hw, item->vif_idx,
					param->port.s_port, item->match_mode, item->action);
			break;
		case FILTER_IPV4_UDP_DST_RANGE:
			f_idx = bes_filter_add_ipv4_udp_dst_match_range(rwnx_hw, item->vif_idx,
					param->port.s_port, param->port.d_port, item->match_mode,
					item->action);
			break;
		case FILTER_IPV4_UDP_SRC_SINGLE:
			f_idx = bes_filter_add_ipv4_udp_src_match_single(rwnx_hw, item->vif_idx,
					param->port.s_port, item->match_mode, item->action);
			break;
		case FILTER_IPV4_UDP_SRC_RANGE:
			f_idx = bes_filter_add_ipv4_udp_src_match_range(rwnx_hw, item->vif_idx,
					param->port.s_port, param->port.d_port, item->match_mode,
					item->action);
			break;
		case FILTER_IPV4_UDP_SRC_DST_SINGLE:
			f_idx = bes_filter_add_ipv4_udp_src_and_dst_match_single(rwnx_hw, item->vif_idx,
					param->port.s_port, param->port.d_port, item->action);
			break;
		case FILTER_IPV6_UDP_SRC_DST_SINGLE:
			f_idx = bes_filter_add_ipv6_udp_src_and_dst_match_single(rwnx_hw, item->vif_idx,
					param->port.s_port, param->port.d_port, item->action);
			break;
		case FILTER_IPV4_TCP_DST_SINGLE:
			f_idx = bes_filter_add_ipv4_tcp_dst_match_single(rwnx_hw, item->vif_idx,
					param->port.s_port, item->match_mode, item->action);
			break;
		case FILTER_IPV4_TCP_DST_RANGE:
			f_idx = bes_filter_add_ipv4_tcp_dst_match_range(rwnx_hw, item->vif_idx,
					param->port.s_port, param->port.d_port, item->match_mode,
					item->action);
			break;
		case FILTER_IPV4_TCP_SRC_SINGLE:
			f_idx = bes_filter_add_ipv4_tcp_src_match_single(rwnx_hw, item->vif_idx,
					param->port.s_port, item->match_mode, item->action);
			break;
		case FILTER_IPV4_TCP_SRC_RANGE:
			f_idx = bes_filter_add_ipv4_tcp_src_match_range(rwnx_hw, item->vif_idx,
					param->port.s_port, param->port.d_port, item->match_mode,
					item->action);
			break;
		case FILTER_IPV6_SRC:
			f_idx = bes_filter_add_ipv6_src_match(rwnx_hw, item->vif_idx,
					&param->ipv6_addr, item->match_mode, item->action);
			break;
		case FILTER_IPV6_DST:
			f_idx = bes_filter_add_ipv6_dst_match(rwnx_hw, item->vif_idx,
					&param->ipv6_addr, item->match_mode, item->action);
			break;
		case FILTER_IPV6_NS:
			f_idx = bes_filter_add_ipv6_ns_match(rwnx_hw, item->vif_idx,
					param->of_idx);
			break;
		default:
			f_idx = -1;
			break;
	}

	/* Successfully set and update f_idx */
	if (f_idx >= 0)
		item->f_idx = f_idx;

	return f_idx;
}

void bes_filter_manager_flush_list_set(struct list_head *list_set_head)
{
	struct filter_item *item = NULL, *tmp = NULL;

	/* Check the validity of parameters */
	if (!list_set_head)
		return;

	/* Delete items in list_set */
	list_for_each_entry_safe(item, tmp, list_set_head, list_set)
		bes_filter_manager_del_from_list_set(item);
}

static int bes_filter_manager_list_set_to_device(struct rwnx_hw *rwnx_hw,
						 struct list_head *list_set_head)
{
	int ret = 0;
	s8 f_idx = 0;
	struct filter_item *item = NULL, *tmp = NULL;

	list_for_each_entry_safe(item, tmp, list_set_head, list_set) {
		/* Set filter to device */
		f_idx = bes_filter_manager_set_to_device(rwnx_hw, item);
		if (f_idx < 0) {
			ret = -EINVAL;
			goto err;
		}

		/* Delete items from list_set_head */
		bes_filter_manager_del_from_list_set(item);
	}

	return 0;

err:
	/* Flush list_set_head list */
	bes_filter_manager_flush_list_set(list_set_head);

	return ret;
}

int bes_filter_manager_reset(struct rwnx_hw *rwnx_hw)
{
	int ret = 0;

	/* Check the validity of parameters */
	if (!rwnx_hw)
		return -EINVAL;

	/* Reset rxf */
	ret = bes_filter_reset(rwnx_hw);
	if (ret)
		return ret;

	/* Seting the item->f_idx of the elements in the ifl_map to -1 */
	bes_filter_map_reset_f_idx(&rwnx_hw->filter.ifl_map);

	return 0;
}

static int bes_filter_manager_bind_arp_offload(struct rwnx_hw *rwnx_hw,
					       struct filter_item *item, int of_idx)
{
	int ret = 0;

	ret = bes_filter_set_offload(rwnx_hw, item->f_idx, of_idx);
	if (ret)
		return ret;
	item->param.of_idx = of_idx;

	return 0;
}

int bes_filter_manager_bind_fl_arp_offload(struct rwnx_hw *rwnx_hw,
					   struct list_head *list_set_head, int of_idx)
{
	int ret = 0;
	struct filter_item *item = NULL, *tmp = NULL;

	/* Check the validity of parameters */
	if (!rwnx_hw || !list_set_head)
		return -EINVAL;

	list_for_each_entry_safe(item, tmp, list_set_head, list_set) {
		// Set offload to device
		ret = bes_filter_manager_bind_arp_offload(rwnx_hw, item, of_idx);
		if (ret)
			goto err;

		// Delete item from list_set_head
		bes_filter_manager_del_from_list_set(item);
	}

	return 0;

err:
	/* Flush list_set_head list */
	bes_filter_manager_flush_list_set(list_set_head);

	return ret;
}

int bes_filter_manager_switch_rxf(struct rwnx_hw *rwnx_hw, struct filter_item *item, bool on)
{
	int ret = 0;

	/* Check the validity of parameters */
	if (!rwnx_hw || !item)
		return -EINVAL;

	/* Check if it repeat to switch rxf */
	if ((item->enable && on) || (!item->enable && !on)) {
		bes_warn(BES_DBG_FILTER_MANAGER,"%s WARN: Repeat to switch rxf %d %s\n",
			__func__, item->f_idx, on ? "enable" : "disable");

		return 0;
	}

	/* Switch rxf */
	ret = bes_filter_switch(rwnx_hw, item->f_idx, on);
	if (ret)
		return ret;
	item->enable = on;

	return 0;
}

int bes_filter_manager_set_mismatch_action(struct rwnx_hw *rwnx_hw, u8 action)
{
	int ret = 0;

	/* Check the validity of parameters */
	if (!rwnx_hw)
		return -EINVAL;

	ret = bes_filter_set_mismatch_action(rwnx_hw, action);
	if (ret)
		return ret;
	rwnx_hw->filter.mismatch_action = action;

	return 0;
}

int bes_filter_manager_sync_to_device(struct rwnx_hw *rwnx_hw, struct list_head *list_set_head)
{
	int ret = 0;

	/* Check the validity of parameters */
	if (!rwnx_hw || !list_set_head)
		return -EINVAL;

	do {
		/* Disable rxf on all vif */
		ret = bes_filter_manager_disable_all(rwnx_hw);
		if (ret)
			break;

		/* Reset rxf */
		ret = bes_filter_manager_reset(rwnx_hw);
		if (ret)
			break;

		/* Set rxf to device */
		ret = bes_filter_manager_list_set_to_device(rwnx_hw, list_set_head);
		if (ret)
			break;

		/* Enable rxf for all connected interfaces */
		ret = bes_filter_manager_enable_all(rwnx_hw);
	} while(0);

	return ret;
}

static struct filter_work_item *bes_filter_manager_create_work_item(u8 type)
{
	struct filter_work_item *item = NULL;

	/* Malloc memory */
	item = (struct filter_work_item *)kzalloc(sizeof(struct filter_work_item), GFP_ATOMIC);
	if (!item) {
		bes_err(BES_DBG_FILTER_MANAGER,
			"%s: bes filter work allocation failed\n", __func__);
		return NULL;
	}

	/* Initialize General Parameters */
	INIT_LIST_HEAD(&item->list);
	item->type = type;
	item->vif_idx = RWNX_INVALID_VIF;

	return item;
}

int bes_filter_manager_add_sync_work_item(struct rwnx_hw *rwnx_hw)
{
	struct filter_work_item *item = NULL;

	/* Check the validity of parameters */
	if (!rwnx_hw)
		return -EINVAL;

	/* Create work item for sync_filter and increase sync_cnt value */
	item = bes_filter_manager_create_work_item(WORK_TYPE_SYNC_FILTER);
	if (!item)
		return -EINVAL;
	atomic_inc(&rwnx_hw->filter.sync_cnt);
	item->sync_cnt = atomic_read(&rwnx_hw->filter.sync_cnt);

	/* Add work item to list */
	spin_lock_bh(&rwnx_hw->filter.filter_work_lock);
	list_add_tail(&item->list, &rwnx_hw->filter.filter_work_list);
	spin_unlock_bh(&rwnx_hw->filter.filter_work_lock);

	return 0;
}

int bes_filter_manager_add_ctrl_offload_work_item(struct rwnx_hw *rwnx_hw, u8 type, u8 vif_idx)
{
	struct filter_work_item *item = NULL;

	/* Check the validity of parameters */
	if (!rwnx_hw)
		return -EINVAL;

	/* Create work item and set vif_idx value */
	item = bes_filter_manager_create_work_item(type);
	if (!item)
		return -EINVAL;
	item->vif_idx = vif_idx;

	/* Add work item to list */
	spin_lock_bh(&rwnx_hw->filter.filter_work_lock);
	list_add_tail(&item->list, &rwnx_hw->filter.filter_work_list);
	spin_unlock_bh(&rwnx_hw->filter.filter_work_lock);

	return 0;
}

int bes_filter_manager_sync_filter(struct rwnx_hw *rwnx_hw, bool blocked)
{
	int ret = 0;

	/* Check the validity of parameters */
	BUG_ON(!rwnx_hw);

	if (!blocked) {
		/* Add SYNC_FILTER item */
		ret = bes_filter_manager_add_sync_work_item(rwnx_hw);

		/* Queue work */
		if (!ret)
			queue_work(rwnx_hw->rwnx_wq, &rwnx_hw->filter.filter_work);
	} else {
		mutex_lock(&rwnx_hw->filter.mutex);
		ret = bes_filter_map_sync_ifl_map_to_device(rwnx_hw);
		mutex_unlock(&rwnx_hw->filter.mutex);
	}

	return ret;
}

static void bes_filter_manager_work_fun(struct work_struct *work)
{
	int ret = 0;
	struct rwnx_hw *rwnx_hw = container_of(work, struct rwnx_hw, filter.filter_work);
	struct rwnx_vif *rwnx_vif = NULL;
	struct list_head work_head;
	struct filter_work_item *item = NULL, *tmp = NULL;

	/* Check wlan_power_ctrl.power_status */
	if (!bes_get_wlan_enabled(rwnx_hw)) {
		bes_err(BES_DBG_FILTER_MANAGER,
			"%s ERROR: wlan_power_ctrl.power_status is false\n", __func__);
		return;
	}

	/* Init list_set_head */
	INIT_LIST_HEAD(&work_head);

	spin_lock_bh(&rwnx_hw->filter.filter_work_lock);
	/* Traverse adding item to work_head and reinitialise the emptied list */
	list_splice_init(&rwnx_hw->filter.filter_work_list, &work_head);
	spin_unlock_bh(&rwnx_hw->filter.filter_work_lock);

	/* Traverse work_head and perform operations */
	list_for_each_entry_safe(item, tmp, &work_head, list) {
		switch (item->type) {
			case WORK_TYPE_SYNC_FILTER:
				/* There are other sync operations present */
				if (item->sync_cnt != atomic_read(&rwnx_hw->filter.sync_cnt))
					break;

				mutex_lock(&rwnx_hw->filter.mutex);
				ret = bes_filter_map_sync_ifl_map_to_device(rwnx_hw);
				mutex_unlock(&rwnx_hw->filter.mutex);
				break;
			case WORK_TYPE_SET_ARP_OFFLOAD:
				rwnx_vif = rwnx_rx_get_vif(rwnx_hw, item->vif_idx);
				if (!rwnx_vif || !rwnx_vif->ipv4_addr) {
					ret = -EINVAL;
					break;
				}
				ret = bes_packet_filter_set_arp_offload(rwnx_hw, rwnx_vif, rwnx_vif->ipv4_addr);
				break;
			case WORK_TYPE_CLEAR_ARP_OFFLOAD:
				ret = bes_packet_filter_clear_arp_offload(rwnx_hw, item->vif_idx);
				break;
			case WORK_TYPE_SET_NS_OFFLOAD:
				rwnx_vif = rwnx_rx_get_vif(rwnx_hw, item->vif_idx);
				if (!rwnx_vif) {
					ret = -EINVAL;
					break;
				}
				ret = bes_packet_filter_set_ns_offload(rwnx_hw, rwnx_vif, &rwnx_vif->ipv6_addr);
				break;
			case WORK_TYPE_CLEAR_NS_OFFLOAD:
				ret = bes_packet_filter_clear_ns_offload(rwnx_hw, item->vif_idx);
				break;
			default:
				ret = -EINVAL;
				break;
		}

		if (ret) {
			bes_err(BES_DBG_FILTER_MANAGER, "%s fail to run work.type:%d ret:%d",
				__func__, item->type, ret);
		}

		list_del(&item->list);
		kfree(item);

		if (ret)
			goto err;
	}

	return;

err:
	/* Running error, clearing remaining work items */
	list_for_each_entry_safe(item, tmp, &work_head, list) {
		list_del(&item->list);
		kfree(item);
	}
}

static int bes_filter_manager_add_arp_offload_filter(struct rwnx_hw *rwnx_hw, u8 vif_idx, int of_idx)
{
	int ret = 0;

	/* Check the validity of of_idx */
	if (of_idx < 0)
		return -EINVAL;
	else if (of_idx != ARP_OFFLOAD_FUNC_IDX)
		bes_warn(BES_DBG_FILTER_MANAGER,
				"%s WARN: add the of_idx of arp offload filter is %d\n",
				__func__, of_idx);

	/* Check if repeating to add arp offload filter */
	if (rwnx_hw->filter.arp_offload[vif_idx]) {
		bes_warn(BES_DBG_FILTER_MANAGER,
			 "%s WARN: repeating add arp offload filter\n", __func__);
		return 0;
	}

	/* Insert arp offload filter to the head of eth_type list in ifl_map */
	ret = bes_filter_map_insert_ipv4_arp(rwnx_hw, &rwnx_hw->filter.ifl_map,
			0, vif_idx, of_idx);
	if (ret)
		return ret;
	rwnx_hw->filter.arp_offload[vif_idx] = true;

	return 0;
}

static void bes_filter_manager_del_arp_offload_filter(struct rwnx_hw *rwnx_hw, u8 vif_idx)
{
	/* Check if repeating to delete arp offload filter */
	if (!rwnx_hw->filter.arp_offload[vif_idx]) {
		bes_warn(BES_DBG_FILTER_MANAGER,
			 "%s WARN: repeating del arp offload\n", __func__);
		return;
	}

	/* Delete arp offload filter in ifl_map */
	bes_filter_map_del_filter_by_type(rwnx_hw, &rwnx_hw->filter.ifl_map,
					  vif_idx, FILTER_IPV4_ARP);
}

int bes_filter_manager_set_arp_offload(struct rwnx_hw *rwnx_hw,
				       struct rwnx_vif *rwnx_vif, u32 ipv4_addr)
{
	int ret = 0;
	u8 vif_index;

	/* Determine if it is effective to set arp offload */
	if (!rwnx_hw || !rwnx_vif)
		return -EINVAL;

	/* Check the validity of vif_index */
	vif_index = rwnx_vif->vif_index;
	if (vif_index >= NX_ITF_MAX)
		return -EINVAL;

	/* If the obtained IP is invalid or the same IP has already been set, return directly */
	if(!ipv4_addr || (rwnx_vif->ipv4_addr && rwnx_vif->ipv4_addr == ipv4_addr))
		return 0;

	do {
		/* Save ipv4 addr */
		rwnx_vif->ipv4_addr = ipv4_addr;

		/* Add SET_ARP_OFFLOAD work item */
		ret = bes_filter_manager_add_ctrl_offload_work_item(rwnx_hw,
				WORK_TYPE_SET_ARP_OFFLOAD, vif_index);
		if (ret)
			break;

		/* Add arp offload filter to ifl_map */
		ret = bes_filter_manager_add_arp_offload_filter(rwnx_hw,
				vif_index, ARP_OFFLOAD_FUNC_IDX);
		if (ret)
			break;

		/* Queue work to sync filter */
		ret = bes_filter_manager_sync_filter(rwnx_hw, false);
	} while(0);

	return ret;
}

int bes_filter_manager_clear_arp_offload(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif)
{
	int ret = 0;
	u8 vif_index;

	/* Check the validity of parameters */
	if (!rwnx_hw || !rwnx_vif)
		return -EINVAL;

	/* Check the validity of vif_index */
	vif_index = rwnx_vif->vif_index;
	if (vif_index >= NX_ITF_MAX)
		return -EINVAL;

	do {
		/* Clear ipv4 addr */
		rwnx_vif->ipv4_addr = 0;

		/* Add CLEAR_ARP_OFFLOAD work item */
		ret = bes_filter_manager_add_ctrl_offload_work_item(rwnx_hw,
				WORK_TYPE_CLEAR_ARP_OFFLOAD, vif_index);
		if (ret)
			break;

		/* Delete arp offload filter in ifl_map */
		bes_filter_manager_del_arp_offload_filter(rwnx_hw, vif_index);

		/* Queue work to sync filter */
		ret = bes_filter_manager_sync_filter(rwnx_hw, false);
	} while(0);

	return ret;
}

static int bes_filter_manager_add_ns_offload_filter(struct rwnx_hw *rwnx_hw, u8 vif_idx, int of_idx)
{
	int ret = 0;

	/* Check the validity of of_idx and vif_idx */
	if (of_idx < 0 || vif_idx >= NX_ITF_MAX)
		return -EINVAL;
	else if (of_idx != NS_OFFLOAD_FUNC_IDX)
		bes_warn(BES_DBG_FILTER_MANAGER,
			 "%s WARN: add the of_idx of ns offload filter is %d\n",
			 __func__, of_idx);

	/* Check if repeating to add ns offload filter */
	if (rwnx_hw->filter.ns_offload[vif_idx]) {
		bes_warn(BES_DBG_FILTER_MANAGER,
			 "%s WARN: repeating add ns offload\n", __func__);
		return 0;
	}

	/* Insert ns offload filter to the head of eth_type list in ifl_map */
	ret = bes_filter_map_insert_ipv6_ns(rwnx_hw, &rwnx_hw->filter.ifl_map, 0, vif_idx, of_idx);
	if (ret)
		return ret;
	rwnx_hw->filter.ns_offload[vif_idx] = true;

	return 0;
}

static void bes_filter_manager_del_ns_offload_filter(struct rwnx_hw *rwnx_hw, u8 vif_idx)
{
	/* Check if repeating to delete ns offload */
	if (!rwnx_hw->filter.ns_offload[vif_idx]) {
		bes_warn(BES_DBG_FILTER_MANAGER, "%s WARN: repeating del ns offload\n", __func__);
		return;
	}

	/* Delete ns offload filter in ifl_map */
	bes_filter_map_del_filter_by_type(rwnx_hw, &rwnx_hw->filter.ifl_map, vif_idx, FILTER_IPV6_NS);
}

int bes_filter_manager_set_ns_offload(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
				      struct in6_addr *ipv6_addr)
{
	int ret = 0;
	u8 vif_index;

	/* Determine if it is effective to set arp offload */
	if (!rwnx_hw || !rwnx_vif || !ipv6_addr)
		return -EINVAL;

	/* Check the validity of vif_index */
	vif_index = rwnx_vif->vif_index;
	if (vif_index >= NX_ITF_MAX)
		return -EINVAL;

	do {
		/* Save ipv6 addr */
		memcpy(&rwnx_vif->ipv6_addr, ipv6_addr, sizeof(struct in6_addr));

		/* Add SET_NS_OFFLOAD work item */
		ret = bes_filter_manager_add_ctrl_offload_work_item(rwnx_hw,
				WORK_TYPE_SET_NS_OFFLOAD, vif_index);
		if (ret)
			break;

		/* Add ns offload filter to ifl_map */
		ret = bes_filter_manager_add_ns_offload_filter(rwnx_hw,
				vif_index, NS_OFFLOAD_FUNC_IDX);
		if (ret) {
			break;
		}

		/* Queue work to sync filter */
		ret = bes_filter_manager_sync_filter(rwnx_hw, false);
	} while(0);

	return ret;
}

int bes_filter_manager_clear_ns_offload(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif)
{
	int ret = 0;
	u8 vif_index;

	/* Check the validity of parameters */
	if (!rwnx_hw || !rwnx_vif)
		return -EINVAL;

	/* Check the validity of vif_index */
	vif_index = rwnx_vif->vif_index;
	if (vif_index >= NX_ITF_MAX)
		return -EINVAL;

	do {
		/* Clear ipv6 addr */
		memset(&rwnx_vif->ipv6_addr, 0, sizeof(struct in6_addr));

		/* Add CLEAR_NS_OFFLOAD work item */
		ret = bes_filter_manager_add_ctrl_offload_work_item(rwnx_hw,
				WORK_TYPE_CLEAR_NS_OFFLOAD, vif_index);
		if (ret)
			break;

		/* Delete ns offload filter in ifl_map */
		bes_filter_manager_del_ns_offload_filter(rwnx_hw, vif_index);

		/* Queue work to sync filter */
		ret = bes_filter_manager_sync_filter(rwnx_hw, false);
	} while(0);

	return ret;
}

int bes_filter_manager_ctrl_ipv4_or_ipv6_mc_filter(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
						   u8 wf_type, bool add)
{
	int ret = 0;
	bool filter_exist = false;
	u8 vif_index;

	/* Check the validity of parameters */
	BUG_ON(wf_type != FILTER_IPV4_MULTICAST &&
	       wf_type != FILTER_IPV6_MULTICAST);
	if (!rwnx_hw || !rwnx_vif)
		return -EINVAL;

	/* Check the validity of vif_index */
	vif_index = rwnx_vif->vif_index;
	if (vif_index >= NX_ITF_MAX)
		return -EINVAL;

	/* Aviod repeating to add or delete */
	filter_exist = bes_filter_map_exist_filter(&rwnx_hw->filter.ifl_map,
				vif_index, wf_type);
	if ((filter_exist && add) || (!filter_exist && !add))
		return 0;

	/* Insert/Delete ipv4 or ipv6 multicast filter */
	if (add)
		ret = bes_filter_map_insert_multicast_filter(rwnx_hw, &rwnx_hw->filter.ifl_map, 0,
				vif_index, wf_type, RXF_REJECT);
	else
		bes_filter_map_del_filter_by_type(rwnx_hw, &rwnx_hw->filter.ifl_map,
				vif_index, wf_type);

	return ret;
}

static int bes_filter_manager_mc_addr_list_add_item(struct mc_addr_list *mc_addr_l, const u8 *addr)
{
	struct mc_addr_item *item = NULL;

	/* Malloc mc addr item memory and init */
	item = (struct mc_addr_item *)kzalloc(sizeof(struct mc_addr_item), GFP_ATOMIC);
	if (!item) {
		bes_err(BES_DBG_FILTER_MANAGER,
			"%s: mc addr item allocation failed\n", __func__);
		return -ENOMEM;
	}
	memcpy(&item->addr, addr, MAC_ADDR_LEN);

	/* Add it to list */
	list_add_tail(&item->list, &mc_addr_l->list);
	mc_addr_l->count++;

	return 0;
}

static void bes_filter_manager_flush_mc_addr_list(struct mc_addr_list *mc_addr_l)
{
	struct mc_addr_item *item = NULL, *tmp = NULL;

	/* If count is zore, return directly */
	if (!mc_addr_l->count)
		return;

	/* Delete all item in list */
	list_for_each_entry_safe(item, tmp, &mc_addr_l->list, list) {
		BUG_ON(mc_addr_l->count == 0);
		list_del(&item->list);
		mc_addr_l->count--;
		kfree(item);
	}
}

static int bes_filter_manager_update_mc_addr_list(struct rwnx_vif *rwnx_vif, u8 rxfilter)
{
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	bool allmulti = false, multicast = false;
	int ret = 0;
	struct netdev_hw_addr *ha = NULL;
	struct mc_addr_list mc_addr_l;
	struct mc_addr_item *item = NULL;
	u8 vif_index = rwnx_vif->vif_index;

	/* Only set multicast filter for sta when it is connected and vif_index is expected*/
	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_STATION ||
	    !rwnx_vif->ndev || !rwnx_vif_is_connected(rwnx_vif) || vif_index >= NX_ITF_MAX) {
		return 0;
	}

	/* Init mc_addr_l and */
	INIT_LIST_HEAD(&mc_addr_l.list);
	mc_addr_l.count = 0;

	/* IFF_ALLMULTI: Indicates that interface receives all multicast packets.
	   IFF_MULTICAST: Indicates that interface supporting multicast packets.
			  This is a prerequisite for joining multicast groups. */
	allmulti = !!(rwnx_hw->filter.local_flags[vif_index] & IFF_ALLMULTI);
	multicast = !!(rwnx_hw->filter.local_flags[vif_index] & IFF_MULTICAST);
	bes_dbg(BES_DBG_FILTER_MANAGER, "%s vif_index:%d rxfilter:0x%02x allmulti:%d multicast:%d mc.count:%d\n", __func__,
		vif_index, rxfilter, allmulti, multicast, rwnx_vif->ndev->mc.count);

	if (rxfilter == (BIT(PRIV_CMD_FILTER_TYPE_V4_MULTICAST) | BIT(PRIV_CMD_FILTER_TYPE_V6_MULTICAST)))
		goto reject_all; // Filter all ipv6 and ipv4 multicast
	else if (allmulti)
		goto out; // Indicates no ipv6 and ipv4 multicast filter

	if (!multicast) {
		goto mc_filter; // Update filter due to rxfilter
	} else {
		// Reject all reject when the count of ndev->mc is 0
		if (rwnx_vif->ndev->mc.count <= 0)
			goto reject_all; // Filter all ipv6 and ipv4 multicast

		netdev_hw_addr_list_for_each(ha, &rwnx_vif->ndev->mc) {
			/* Not multicast mac, next one directly */
			if (!is_multicast_ether_addr(ha->addr))
				continue;

			/* Check if add based on wpa cmd rxfilter */
			if ((rxfilter & BIT(PRIV_CMD_FILTER_TYPE_V4_MULTICAST)) &&
			    is_ipv4_multicast_ether_addr((const u8 *)ha->addr))
				continue;

			if ((rxfilter & BIT(PRIV_CMD_FILTER_TYPE_V6_MULTICAST)) &&
			    is_ipv6_multicast_ether_addr((const u8 *)ha->addr))
				continue;

			ret = bes_filter_manager_mc_addr_list_add_item(&mc_addr_l, (const u8 *)ha->addr);
			if (ret)
				goto out;

			if (mc_addr_l.count > WOWLAN_MUTICAST_THRESHOLD)
				break;
		}

		if (!mc_addr_l.count)
			goto reject_all; // Filter all ipv6 and ipv4 multicast
		else if (mc_addr_l.count >= WOWLAN_MUTICAST_THRESHOLD)
			goto mc_filter;
	}

	/* If mc_addr_l.count between 1 and WOWLAN_MUTICAST_THRESHOLD,
	   only accept data from multicast macs that are joined */
	ret = bes_filter_map_insert_multicast_filter(rwnx_hw, &rwnx_hw->filter.ifl_map, 0,
		vif_index, FILTER_ALL_MULTICAST, RXF_REJECT);
	if(ret)
		goto out;

	list_for_each_entry(item, &mc_addr_l.list, list) {
		bes_dbg(BES_DBG_FILTER_MANAGER, "%s joined item->addr: %pM\n", __func__, &item->addr);
		ret = bes_filter_map_insert_mac_addr(rwnx_hw, &rwnx_hw->filter.ifl_map, 0, vif_index,
			FILTER_MAC_DA, &item->addr, RXF_MATCH_EQUAL, RXF_ACCEPT);
		if (ret)
			break;
	}
out:
	/* flush mc_addr_list */
	bes_filter_manager_flush_mc_addr_list(&mc_addr_l);
	return ret;
mc_filter:
	/* flush mc_addr_list */
	bes_filter_manager_flush_mc_addr_list(&mc_addr_l);

	if (rxfilter & BIT(PRIV_CMD_FILTER_TYPE_V4_MULTICAST))
		ret = bes_filter_manager_ctrl_ipv4_or_ipv6_mc_filter(rwnx_hw,
				rwnx_vif, FILTER_IPV4_MULTICAST, true);
	else if (rxfilter & BIT(PRIV_CMD_FILTER_TYPE_V6_MULTICAST))
		ret = bes_filter_manager_ctrl_ipv4_or_ipv6_mc_filter(rwnx_hw,
				rwnx_vif, FILTER_IPV6_MULTICAST, true);
	else
		goto out; // Indicates no ipv6 and ipv4 multicast filter

	return ret;
reject_all:
	/* Add FILTER_ALL_MULTICAST and FILTER_MAC_DA filter to only accept data from multicast macs that are joined */
	return bes_filter_map_insert_multicast_filter(rwnx_hw, &rwnx_hw->filter.ifl_map, 0,
		vif_index, FILTER_ALL_MULTICAST, RXF_REJECT);
}

int bes_filter_manager_ctrl_multicast_filter(struct rwnx_vif *rwnx_vif, u8 rxfilter)
{
	struct rwnx_hw *rwnx_hw;
	u8 vif_index;

	if (!rwnx_vif || !rwnx_vif->rwnx_hw || !rwnx_vif->ndev)
		return -EINVAL;

	rwnx_hw = rwnx_vif->rwnx_hw;
	/* Check the validity of vif_index */
	vif_index = rwnx_vif->vif_index;
	if (!rwnx_hw || vif_index >= NX_ITF_MAX)
		return -EINVAL;

	/* Delete all multicast filter in ifl_map on this vif */
	bes_filter_map_del_multicast_related(rwnx_hw, &rwnx_hw->filter.ifl_map, vif_index);

	/* update the multicast filter */
	return bes_filter_manager_update_mc_addr_list(rwnx_vif, rxfilter);
}

static int bes_filter_manager_add_gtk_rekey_offload_filter(struct rwnx_hw *rwnx_hw, u8 vif_idx, int of_idx)
{
	int ret = 0;

	/* Del filter before add */
	bes_filter_map_del_gtk_rekey(rwnx_hw,
				&rwnx_hw->filter.ifl_map,
				rwnx_hw->filter.gtk_rekey_vif_idx);

	/* Insert gtk_rekey offload filter to the head of eth_type list in ifl_map */
	ret = bes_filter_map_insert_gtk_rekey(rwnx_hw,
					&rwnx_hw->filter.ifl_map, 0,
					vif_idx, of_idx);
	if (!ret)
		rwnx_hw->filter.gtk_rekey_vif_idx = vif_idx;

	return ret;
}

static void bes_filter_manager_del_gtk_rekey_offload_filter(struct rwnx_hw *rwnx_hw, u8 vif_idx)
{
	bes_filter_map_del_gtk_rekey(rwnx_hw,
				&rwnx_hw->filter.ifl_map, vif_idx);
}

int bes_filter_manager_ctrl_gtk_rekey_offload_filter(struct rwnx_hw *rwnx_hw,
						     struct rwnx_vif *rwnx_vif, bool add)
{
	int ret = 0;
	u8 vif_index;

	bes_dbg(BES_DBG_FILTER_MANAGER, RWNX_FN_ENTRY_STR);

	/* Check the validity of parameters */
	if (!rwnx_hw || !rwnx_vif || (add && !rwnx_vif->up))
		return -EINVAL;

	/* Check the validity of vif_index */
	vif_index = rwnx_vif->vif_index;
	if (vif_index >= NX_ITF_MAX)
		return -EINVAL;

	/* Open identity authentication */
	if (!rwnx_vif->sta.sec.key_mgmt)
		return 0;

	/* Check the validity of vif */
	if (!add && rwnx_hw->filter.gtk_rekey_vif_idx != vif_index) {
		bes_err(BES_DBG_FILTER_MANAGER,
				"%s ERROR: vif mismatch %d (%d)\n", __func__,
				rwnx_hw->filter.gtk_rekey_vif_idx, vif_index);
		return -EINVAL;
	}

	do {
		/* Insert/Delete gtk rekey offload filter in ifl_map */
		if (add)
			ret = bes_filter_manager_add_gtk_rekey_offload_filter(rwnx_hw,
				vif_index, GTK_OFFLOAD_FUNC_IDX);
		else
			bes_filter_manager_del_gtk_rekey_offload_filter(rwnx_hw,
				vif_index);
		if (ret)
			break;

		/* Queue work to sync filter */
		ret = bes_filter_manager_sync_filter(rwnx_hw, false);
	} while(0);
	bes_err_with_cond(ret, BES_DBG_FILTER_MANAGER,
		"%s ERROR(%d): Fail to ctrl gtk rekey offload filter!!\n", __func__, ret);

	return ret;
}

static void bes_filter_manager_ifl_map_enable(struct bes_filter *filter, u8 type)
{
	filter->ifl_map_disable_rcd &= ~BIT(type);
	if (!filter->ifl_map_disable_rcd)
		bes_filter_map_set_valid(&filter->ifl_map, true);
}

static void bes_filter_manager_ifl_map_disable(struct bes_filter *filter, u8 type)
{
	filter->ifl_map_disable_rcd |= BIT(type);
	if (filter->ifl_map_disable_rcd)
		bes_filter_map_set_valid(&filter->ifl_map, false);
}

int bes_filter_manager_suspend_cfg(struct rwnx_hw *rwnx_hw, struct list_head *list_set_head)
{
	int ret = 0;

	/* Check the validity of parameters */
	if (!rwnx_hw || !list_set_head)
		return -EINVAL;

	mutex_lock(&rwnx_hw->filter.mutex);
	do {
		/* Set ifl_map invalid */
		bes_filter_manager_ifl_map_disable(&rwnx_hw->filter, BES_IFL_MAP_DISABLE_TYPE_HOST_SUSPEND);

		/* Set mismatch action to reject */
		ret = bes_filter_manager_set_mismatch_action(rwnx_hw, FILTER_ACTION_DISCARD);
		if (ret)
			break;

		// Disable rxf on all vif
		ret = bes_filter_manager_disable_all(rwnx_hw);
		if (ret)
			break;

		// Reset rxf
		ret = bes_filter_manager_reset(rwnx_hw);
		if (ret)
			break;

		/* Set filter to device */
		ret = bes_filter_manager_list_set_to_device(rwnx_hw, list_set_head);
		if (ret)
			break;

		// Enable rxf for all connected interfaces
		ret = bes_filter_manager_enable_all(rwnx_hw);
	} while(0);
	mutex_unlock(&rwnx_hw->filter.mutex);

	return ret;
}

int bes_filter_manager_resume_cfg(struct rwnx_hw *rwnx_hw)
{
	int ret = 0;

	/* Check the validity of parameters */
	if (!rwnx_hw)
		return -EINVAL;

	mutex_lock(&rwnx_hw->filter.mutex);
	do {
		/* Set ifl_map valid */
		bes_filter_manager_ifl_map_enable(&rwnx_hw->filter, BES_IFL_MAP_DISABLE_TYPE_HOST_SUSPEND);

		/* Sync rxf of ifl_map to device */
		ret = bes_filter_map_sync_ifl_map_to_device(rwnx_hw);
		if (ret)
			break;

		/* Set mismatch action to accept */
		if (rwnx_hw->filter.mismatch_action == FILTER_ACTION_DISCARD) {
			ret = bes_filter_set_mismatch_action(rwnx_hw, FILTER_ACTION_ACCEPT);
			if (ret)
				break;
			rwnx_hw->filter.mismatch_action = FILTER_ACTION_ACCEPT;
		}
	} while(0);
	mutex_unlock(&rwnx_hw->filter.mutex);

	return ret;
}

static int bes_filter_manager_sceen_off(struct rwnx_hw *rwnx_hw)
{
	int ret = 0;
	struct list_head list_set_head;
	struct bes_capacity stats;

	do {
		/* Init list and stats parameter */
		INIT_LIST_HEAD(&list_set_head);
		stats.rxf_idx = 0;
		stats.rxf_arry_idx = 0;

		/* Add filter when sceen off  */
		ret = bes_wpa_priv_cmd_add_mc_and_mdns_filter(rwnx_hw, &list_set_head, &stats);
		if (ret)
			break;

		/* Set ifl_map invalid */
		bes_filter_manager_ifl_map_disable(&rwnx_hw->filter, BES_IFL_MAP_DISABLE_TYPE_SCREEN_OFF);

		// Disable rxf on all vif
		ret = bes_filter_manager_disable_all(rwnx_hw);
		if (ret)
			break;

		// Reset rxf
		ret = bes_filter_manager_reset(rwnx_hw);
		if (ret)
			break;

		/* Set filter to device */
		ret = bes_filter_manager_list_set_to_device(rwnx_hw, &list_set_head);
		if (ret)
			break;

		// Enable rxf for all connected interfaces
		ret = bes_filter_manager_enable_all(rwnx_hw);
	} while(0);

	/* Flush the filter list */
	bes_filter_manager_flush_list_set(&list_set_head);

	return ret;
}

static int bes_filter_manager_screen_on(struct rwnx_hw *rwnx_hw)
{
	/* Set ifl_map valid */
	bes_filter_manager_ifl_map_enable(&rwnx_hw->filter, BES_IFL_MAP_DISABLE_TYPE_SCREEN_OFF);

	/* Sync rxf of ifl_map to device */
	return bes_filter_map_sync_ifl_map_to_device(rwnx_hw);
}

int bes_filter_manager_update_screen_suspend_filters(struct rwnx_hw *rwnx_hw, bool suspend)
{
	int ret = 0;

	/* Check the validity of parameters */
	if (!rwnx_hw)
		return -EINVAL;

	mutex_lock(&rwnx_hw->filter.mutex);
	/* Set the filters for screen off */
	if (suspend) {
		ret = bes_filter_manager_sceen_off(rwnx_hw);
		if (!ret)
			goto out;
	}
	bes_err_with_cond(ret, BES_DBG_FILTER_MANAGER,
			  "%s ERROR(%d): Fail to set screen-off filters!!\n",
			  __func__, ret);

	/* Set the filters when screen on or failing to set the filters for screen off */
	ret = bes_filter_manager_screen_on(rwnx_hw);
	bes_err_with_cond(ret, BES_DBG_FILTER_MANAGER,
			  "%s ERROR(%d): Fail to set screen-on filters!! suspend:%d\n",
			  __func__, ret, suspend);

out:
	mutex_unlock(&rwnx_hw->filter.mutex);

	return ret;
}

static void bes_filter_manager_flush_work_when_vif_clsoe(struct rwnx_hw *rwnx_hw, u8 vif_idx)
{
	struct filter_work_item *item = NULL, *tmp = NULL;

	spin_lock_bh(&rwnx_hw->filter.filter_work_lock);
	list_for_each_entry_safe(item, tmp, &rwnx_hw->filter.filter_work_list, list) {
		spin_unlock_bh(&rwnx_hw->filter.filter_work_lock);
		if (item->type == WORK_TYPE_SYNC_FILTER || item->vif_idx == vif_idx) {
			list_del(&item->list);
			kfree(item);
		}
		spin_lock_bh(&rwnx_hw->filter.filter_work_lock);
	}
	spin_unlock_bh(&rwnx_hw->filter.filter_work_lock);
}

void bes_filter_manager_interface_open(struct rwnx_vif *rwnx_vif)
{
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	struct bes_filter *bes_filter = &rwnx_hw->filter;
	int ret = 0;
	u8 vif_index = rwnx_vif->vif_index;

	/* Check the validity of vif_index */
	if (vif_index >= NX_ITF_MAX)
		return;

	/* When VIF is enabled, the filter also needs to be enabled.
	 * Because oversize packet filtering needs to be enabled
	 * before receiving the first packet. */
	mutex_lock(&bes_filter->mutex);
	ret = bes_filter_manager_filter_ctrl(rwnx_hw, vif_index, true);
	mutex_unlock(&bes_filter->mutex);
	bes_err_with_cond(ret, BES_DBG_FILTER_MANAGER,
			  "%s ERROR(%d): Fail to config filter when vif(idx-%d) open!!\n",
			  __func__, ret, vif_index);
}

void bes_filter_manager_interface_close(struct rwnx_hw *rwnx_hw, u8 vif_idx)
{
	int ret = 0, idx = 0;

	/* Check the validity of parameters */
	if (!rwnx_hw || vif_idx >= NX_ITF_MAX)
		return;

	/* Cancel and flush filter_work */
	cancel_work_sync(&rwnx_hw->filter.filter_work);
	flush_work(&rwnx_hw->filter.filter_work);

	do {
		mutex_lock(&rwnx_hw->filter.mutex);
		/* Delete filter items of this vif and all sync filter items when vif close */
		bes_filter_manager_flush_work_when_vif_clsoe(rwnx_hw, vif_idx);

		/* Delete all filter of this vif in ifl_map */
		bes_filter_map_delete_by_vif(&rwnx_hw->filter.ifl_map, vif_idx);

		/* If close last interface, juest reset filter_en. Otherwise, syncing rxf to device */
		if (rwnx_hw->vif_started <= 0) {
			for (idx = 0; idx < NX_ITF_MAX; idx++) {
				if (!rwnx_hw->filter.filter_en[idx])
					continue;

				rwnx_hw->filter.filter_en[idx] = false;
			}
			mutex_unlock(&rwnx_hw->filter.mutex);
		} else {
			rwnx_hw->filter.filter_en[vif_idx] = false;
			mutex_unlock(&rwnx_hw->filter.mutex);

			/* Queue work to sync filter */
			ret = bes_filter_manager_sync_filter(rwnx_hw, true);
		}
	} while(0);
	bes_err_with_cond(ret, BES_DBG_FILTER_MANAGER,
		"%s ERROR(%d): Fail to config filter when vif close!!\n", __func__, ret);
}

int bes_filter_manager_get_config(struct rwnx_hw *rwnx_hw)
{
	int ret = 0;

	/* Check the validity of parameters */
	if (!rwnx_hw)
		return -EINVAL;

	/* Get config form lmac */
	ret = bes_filter_get_config_param(rwnx_hw,
		&rwnx_hw->filter.max.rxf_idx, &rwnx_hw->filter.max.rxf_arry_idx,
		(u8 *)&rwnx_hw->filter.mismatch_action);
	bes_err_with_cond(ret, BES_DBG_FILTER_MANAGER,
		"%s ERROR(%d): Fail to get config param\n", __func__, ret);

	return ret;
}

void bes_filter_manager_init(struct rwnx_hw *rwnx_hw)
{
	u8 vif_idx = 0;

	bes_filter_map_init(&rwnx_hw->filter.ifl_map);
	bes_filter_map_set_valid(&rwnx_hw->filter.ifl_map, true);
	for (vif_idx = 0; vif_idx < NX_ITF_MAX; vif_idx++) {
		rwnx_hw->filter.filter_en[vif_idx] = false;
		rwnx_hw->filter.local_flags[vif_idx] = IFF_MULTICAST;
		rwnx_hw->filter.arp_offload[vif_idx] = false;
		rwnx_hw->filter.ns_offload[vif_idx] = false;
	}
	bes_signal_test_init(rwnx_hw);
	rwnx_hw->filter.max.rxf_idx = 0;
	rwnx_hw->filter.max.rxf_arry_idx = 0;
	rwnx_hw->filter.mismatch_action = FILTER_ACTION_ACCEPT;
	rwnx_hw->filter.gtk_rekey_vif_idx = RWNX_INVALID_VIF;
	rwnx_hw->filter.ifl_map_disable_rcd = 0;
	mutex_init(&rwnx_hw->filter.mutex);
	spin_lock_init(&rwnx_hw->filter.filter_work_lock);
	INIT_LIST_HEAD(&rwnx_hw->filter.filter_work_list);
	INIT_WORK(&rwnx_hw->filter.filter_work, bes_filter_manager_work_fun);
}

void bes_filter_manager_deinit(struct rwnx_hw *rwnx_hw)
{
	u8 vif_idx = 0;

	cancel_work_sync(&rwnx_hw->filter.filter_work);
	flush_work(&rwnx_hw->filter.filter_work);

	bes_filter_map_deinit(&rwnx_hw->filter.ifl_map);
	bes_filter_map_set_valid(&rwnx_hw->filter.ifl_map, false);
	for (vif_idx = 0; vif_idx < NX_ITF_MAX; vif_idx++) {
		rwnx_hw->filter.filter_en[vif_idx] = false;
		rwnx_hw->filter.local_flags[vif_idx] = IFF_MULTICAST;
		rwnx_hw->filter.arp_offload[vif_idx] = false;
		rwnx_hw->filter.ns_offload[vif_idx] = false;
	}
	bes_signal_test_deinit(rwnx_hw);
	rwnx_hw->filter.max.rxf_idx = 0;
	rwnx_hw->filter.max.rxf_arry_idx = 0;
	rwnx_hw->filter.mismatch_action = FILTER_ACTION_ACCEPT;
	rwnx_hw->filter.gtk_rekey_vif_idx = RWNX_INVALID_VIF;
	rwnx_hw->filter.ifl_map_disable_rcd = 0;
}