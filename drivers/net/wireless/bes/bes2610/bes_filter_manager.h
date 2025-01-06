/*
 * @file bes_filter_manager.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __BES_FILTER_MANAGER_H__
#define __BES_FILTER_MANAGER_H__

/* wowlan multicast threshold */
#define WOWLAN_MUTICAST_THRESHOLD    (3)

#define BES_FILTER_PARAM_UNCARE_MATCH_MODE 0

enum bes_filter_type {
	FILTER_MAC_DA = 0,
	FILTER_MAC_SA,
	FILTER_ETH_TYPE,
	FILTER_IPV4_SRC,
	FILTER_IPV4_DST,
	FILTER_IPV4_PROTO,
	FILTER_IPV4_ARP,
	FILTER_EAPOL_G_MSG1,
	FILTER_MAGIC_PACKET,
	FILTER_IPV4_MULTICAST,
	FILTER_IPV6_MULTICAST,
	FILTER_ALL_MULTICAST,
	FILTER_PATTERN,
	FILTER_IPV4_UDP_DST_SINGLE,
	FILTER_IPV4_UDP_DST_RANGE,
	FILTER_IPV4_UDP_SRC_SINGLE,
	FILTER_IPV4_UDP_SRC_RANGE,
	FILTER_IPV4_UDP_SRC_DST_SINGLE,
	FILTER_IPV6_UDP_SRC_DST_SINGLE,
	FILTER_IPV4_TCP_DST_SINGLE,
	FILTER_IPV4_TCP_DST_RANGE,
	FILTER_IPV4_TCP_SRC_SINGLE,
	FILTER_IPV4_TCP_SRC_RANGE,
	FILTER_IPV6_SRC,
	FILTER_IPV6_DST,
	FILTER_IPV6_NS,
	FILTER_MAX
};

enum bes_fl_type {
	FL_ETH_TYPE = 0,
	FL_IP_ADDR,
	FL_PROTO,
	FL_UDP_PORT,
	FL_TCP_PORT,
	FL_MAC_ADDR,
	FL_MAX
};

struct bes_filter_type_t {
	int index;
	char *name;
};

struct bes_fl_type_t {
	int index;
	char *name;
};

struct filter_item_param {
	union {
		struct mac_addr mac_addr;
		union {
			u16 eth_type;
			u8 of_idx;
		};
		union {
			u32 ip_addr;
			struct in6_addr ipv6_addr;
		};
		u8 proto;
		struct cfg80211_pkt_pattern *pattern;
		struct param_port {
			u16 s_port;
			u16 d_port;
		} port;
	};
};

struct filter_item {
	struct list_head list;
	struct list_head list_set;
	int ref_count;
	s8 f_idx;
	u8 type;
	u8 enable;
	u8 vif_idx;
	u8 match_mode;
	u8 action;
	struct filter_item_param param;
};

/* bes filter list */
struct bes_fl {
	struct list_head list;
	int count;
};

/* bes bes_capacity */
struct bes_capacity {
	u8 rxf_idx;
	u16 rxf_arry_idx;
};

enum bes_filter_work_type {
	WORK_TYPE_SYNC_FILTER = 0,
	WORK_TYPE_SET_ARP_OFFLOAD,
	WORK_TYPE_CLEAR_ARP_OFFLOAD,
	WORK_TYPE_SET_NS_OFFLOAD,
	WORK_TYPE_CLEAR_NS_OFFLOAD,
};

/* filter work item */
struct filter_work_item {
	struct list_head list;
	u8 type;
	union {
		u8 vif_idx;
		int sync_cnt;
	};
};

struct mc_addr_item {
	struct list_head list;
	struct mac_addr addr;
};

struct mc_addr_list {
	struct list_head list;
	u8 count;
};

/* create filter */
struct filter_item *bes_filter_manager_create(u8 vif_idx, u8 type, u8 match_mode, u8 action);
static inline struct filter_item *bes_filter_manager_create_mac_addr(u8 vif_idx, u8 wf_type,
	struct mac_addr *addr, u8 match_mode, u8 action)
{
	struct filter_item *item = NULL;
	struct filter_item_param *param = NULL;

	/* Create filter item */
	item = bes_filter_manager_create(vif_idx, wf_type, match_mode, action);
	if (!item) {
		return item;
	}

	/* Init filter parameters */
	param = &item->param;
	memcpy(&param->mac_addr, addr, MAC_ADDR_LEN);

	return item;
}

static inline struct filter_item *bes_filter_manager_create_magic_packet(u8 vif_idx, u8 wf_type,
	struct mac_addr *addr, u8 match_mode, u8 action)
{
	struct filter_item *item = NULL;
	struct filter_item_param *param = NULL;

	/* Create filter item */
	item = bes_filter_manager_create(vif_idx, wf_type, match_mode, action);
	if (!item) {
		return item;
	}

	/* Init filter parameters */
	param = &item->param;
	memcpy(&param->mac_addr, addr, MAC_ADDR_LEN);

	return item;
}

static inline struct filter_item *bes_filter_manager_create_multicast(u8 vif_idx, u8 wf_type,
	u8 match_mode, u8 action)
{
	struct filter_item *item = NULL;

	/* Create filter item */
	item = bes_filter_manager_create(vif_idx, wf_type, match_mode, action);
	if (!item) {
		return item;
	}

	return item;
}

static inline struct filter_item *bes_filter_manager_create_pattern(u8 vif_idx, u8 wf_type,
	struct cfg80211_pkt_pattern *addr, u8 match_mode, u8 action)
{
	struct filter_item *item = NULL;
	struct filter_item_param *param = NULL;

	/* Create filter item */
	item = bes_filter_manager_create(vif_idx, wf_type, match_mode, action);
	if (!item) {
		return item;
	}

	/* Init filter parameters */
	param = &item->param;
	param->pattern = addr;

	return item;
}

static inline struct filter_item *bes_filter_manager_create_eth_type(u8 vif_idx, u8 wf_type,
	u16 eth_type, u8 match_mode, u8 action)
{
	struct filter_item *item = NULL;
	struct filter_item_param *param = NULL;

	/* Create filter item */
	item = bes_filter_manager_create(vif_idx, wf_type, match_mode, action);
	if (!item) {
		return item;
	}

	/* Init filter parameters */
	param = &item->param;
	param->eth_type = eth_type;

	return item;
}

static inline struct filter_item *bes_filter_manager_create_offload_filter(u8 vif_idx, u8 wf_type,
	u8 of_idx, u8 match_mode, u8 action)
{
	struct filter_item *item = NULL;
	struct filter_item_param *param = NULL;

	/* Create filter item */
	item = bes_filter_manager_create(vif_idx, wf_type, match_mode, action);
	if (!item) {
		return item;
	}

	/* Init filter parameters */
	param = &item->param;
	param->of_idx = of_idx;

	return item;
}

static inline struct filter_item *bes_filter_manager_create_ipv4_arp(u8 vif_idx, u8 wf_type,
	u8 of_idx, u8 match_mode, u8 action)
{
	return bes_filter_manager_create_offload_filter(vif_idx, wf_type,
		of_idx, match_mode, action);
}

static inline struct filter_item *bes_filter_manager_create_gtk_rekey(u8 vif_idx, u8 wf_type,
	u8 of_idx, u8 match_mode, u8 action)
{
	return bes_filter_manager_create_offload_filter(vif_idx, wf_type,
		of_idx, match_mode, action);
}

static inline struct filter_item *bes_filter_manager_create_ipv4_ip_addr(u8 vif_idx, u8 wf_type,
	u32 ip, u8 match_mode, u8 action)
{
	struct filter_item *item = NULL;
	struct filter_item_param *param = NULL;

	/* Create filter item */
	item = bes_filter_manager_create(vif_idx, wf_type, match_mode, action);
	if (!item) {
		return item;
	}

	/* Init filter parameters */
	param = &item->param;
	param->ip_addr = ip;

	return item;
}

static inline struct filter_item *bes_filter_manager_create_ipv4_proto(u8 vif_idx, u8 wf_type,
	u8 proto, u8 match_mode, u8 action)
{
	struct filter_item *item = NULL;
	struct filter_item_param *param = NULL;

	/* Create filter item */
	item = bes_filter_manager_create(vif_idx, wf_type, match_mode, action);
	if (!item) {
		return item;
	}

	/* Init filter parameters */
	param = &item->param;
	param->proto = proto;

	return item;
}

static inline struct filter_item *bes_filter_manager_create_port(u8 vif_idx, u8 wf_type,
	u16 s_port, u16 d_port, u8 match_mode, u8 action)
{
	struct filter_item *item = NULL;
	struct filter_item_param *param = NULL;

	/* Create filter item */
	item = bes_filter_manager_create(vif_idx, wf_type, match_mode, action);
	if (!item) {
		return item;
	}

	/* Init filter parameters */
	param = &item->param;
	param->port.s_port = s_port;
	param->port.d_port = d_port;

	return item;
}

static inline struct filter_item *bes_filter_manager_create_ipv6_ip_addr(u8 vif_idx, u8 wf_type,
	struct in6_addr *ip, u8 match_mode, u8 action)
{
	struct filter_item *item = NULL;
	struct filter_item_param *param = NULL;

	/* Create filter item */
	item = bes_filter_manager_create(vif_idx, wf_type, match_mode, action);
	if (!item) {
		return item;
	}

	/* Init filter parameters */
	param = &item->param;
	memcpy(&param->ipv6_addr, ip, 16);

	return item;
}

static inline struct filter_item *bes_filter_manager_create_ipv6_ns(u8 vif_idx, u8 wf_type,
	u8 of_idx, u8 match_mode, u8 action)
{
	return bes_filter_manager_create_offload_filter(vif_idx, wf_type,
			of_idx, match_mode, action);
}

/* filter work */
int bes_filter_manager_add_sync_work_item(struct rwnx_hw *rwnx_hw);
int bes_filter_manager_add_ctrl_offload_work_item(struct rwnx_hw *rwnx_hw, u8 type, u8 vif_idx);
int bes_filter_manager_sync_filter(struct rwnx_hw *rwnx_hw, bool blocked);

/* get filter config */
int bes_filter_manager_filter_size(u8 wf_type, u8 action, struct filter_item_param *wf_param);
int bes_filter_manager_get_config(struct rwnx_hw *rwnx_hw);

/* normal filter ctrl */
void bes_filter_manager_delete(struct bes_capacity *stats, struct bes_fl *fl,
	struct filter_item *item, struct list_head *list_set_head);
int bes_filter_manager_enable(struct rwnx_hw *rwnx_hw, u8 vif_idx);
int bes_filter_manager_disable(struct rwnx_hw *rwnx_hw, u8 vif_idx);
int bes_filter_manager_disable_all(struct rwnx_hw *rwnx_hw);
void bes_filter_manager_flush_list_set(struct list_head *list_set_head);
int bes_filter_manager_reset(struct rwnx_hw *rwnx_hw);
int bes_filter_manager_bind_fl_arp_offload(struct rwnx_hw *rwnx_hw,
	struct list_head *list_set_head, int of_idx);
int bes_filter_manager_switch_rxf(struct rwnx_hw *rwnx_hw, struct filter_item *item, bool on);
int bes_filter_manager_set_mismatch_action(struct rwnx_hw *rwnx_hw, u8 action);
int bes_filter_manager_sync_to_device(struct rwnx_hw *rwnx_hw, struct list_head *list_set_head);

/* offload and multicast filter */
int bes_filter_manager_set_arp_offload(struct rwnx_hw *rwnx_hw,
	struct rwnx_vif *rwnx_vif, u32 ipv4_addr);
int bes_filter_manager_clear_arp_offload(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif);
int bes_filter_manager_set_ns_offload(struct rwnx_hw *rwnx_hw,
	struct rwnx_vif *rwnx_vif, struct in6_addr *ipv6_addr);
int bes_filter_manager_clear_ns_offload(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif);
int bes_filter_manager_ctrl_multicast_filter(struct rwnx_vif *rwnx_vif, u8 rxfilter);
int bes_filter_manager_ctrl_ipv4_or_ipv6_mc_filter(struct rwnx_hw *rwnx_hw,
	struct rwnx_vif *rwnx_vif, u8 wf_type, bool add);
int bes_filter_manager_ctrl_gtk_rekey_offload_filter(struct rwnx_hw *rwnx_hw,
	struct rwnx_vif *rwnx_vif, bool add);

/* filter cfg whne host suspend and resume */
int bes_filter_manager_suspend_cfg(struct rwnx_hw *rwnx_hw, struct list_head *list_set_head);
int bes_filter_manager_resume_cfg(struct rwnx_hw *rwnx_hw);

/* Update the filters when screen status changed */
int bes_filter_manager_update_screen_suspend_filters(struct rwnx_hw *rwnx_hw, bool suspend);

/* filter init and deinit */
void bes_filter_manager_interface_open(struct rwnx_vif *rwnx_vif);
void bes_filter_manager_interface_close(struct rwnx_hw *rwnx_hw, u8 vif_idx);
void bes_filter_manager_init(struct rwnx_hw *rwnx_hw);
void bes_filter_manager_deinit(struct rwnx_hw *rwnx_hw);

#endif /* __BES_FILTER_MANAGER_H__ */