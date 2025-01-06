/*
 * @file bes_filter_map.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __BES_FILTER_MAP_H__
#define __BES_FILTER_MAP_H__

#include "rwnx_defs.h"

enum bes_ifl_map_disable_type {
	BES_IFL_MAP_DISABLE_TYPE_HOST_SUSPEND = 0,
	BES_IFL_MAP_DISABLE_TYPE_SCREEN_OFF,

	BES_IFL_MAP_DISABLE_TYPE_MAX
};

int bes_filter_map_fl_type_idx(const char *str);
void bes_filter_map_set_valid(struct bes_fl_map *fl_map, bool valid);
bool bes_filter_map_get_valid(struct bes_fl_map *fl_map);

/* show */
int bes_filter_map_show(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	char *bref_info, char *buf, int len);

/* judge whether it exists */
bool bes_filter_map_exist_filter(struct bes_fl_map *fl_map, u8 vif_idx, u8 wf_type);

/* create and insert */
int bes_filter_map_insert_mac_addr(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, u8 wf_type, struct mac_addr *addr, u8 match_mode, u8 action);
int bes_filter_map_insert_multicast_filter(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, u8 wf_type, u8 action);
int bes_filter_map_insert_magic_packet(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, struct mac_addr *addr, u8 action);
int bes_filter_map_insert_pattern(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, struct cfg80211_pkt_pattern *pattern, u8 match_mode, u8 action);
int bes_filter_map_insert_eth_type(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, u16 eth_type, u8 match_mode, u8 action);
int bes_filter_map_insert_ipv4_arp(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, u8 of_idx);
int bes_filter_map_insert_ipv6_ns(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, u8 of_idx);
int bes_filter_map_insert_gtk_rekey(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, u8 of_idx);
int bes_filter_map_insert_ipv4_addr(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, u8 wf_type, u32 *ip, u8 match_mode, u8 action);
int bes_filter_map_insert_ipv4_proto(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, u8 proto, u8 match_mode, u8 action);
int bes_filter_map_insert_to_port_list(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, u8 wf_type, u16 s_port, u16 d_port, u8 match_mode, u8 action);
int bes_filter_map_insert_ipv6_addr(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	int pos, u8 vif_idx, u8 wf_type, struct in6_addr *ip, u8 match_mode, u8 action);

/* delete */
int bes_filter_map_delete_by_pos(struct rwnx_hw *rwnx_hw, struct bes_fl_map *fl_map,
	struct bes_fl *fl, int pos);
void bes_filter_map_delete_by_vif(struct bes_fl_map *fl_map, u8 vif_idx);
void bes_filter_map_delete_all(struct bes_fl_map *fl_map);
void bes_filter_map_del_filter_by_type(struct rwnx_hw *rwnx_hw,
	struct bes_fl_map *fl_map, u8 vif_idx, u8 wf_type);
void bes_filter_map_del_multicast_related(struct rwnx_hw *rwnx_hw,
	struct bes_fl_map *fl_map, u8 vif_idx);
void bes_filter_map_del_gtk_rekey(struct rwnx_hw *rwnx_hw,
	struct bes_fl_map *fl_map, u8 vif_idx);

/* set to device */
int bes_filter_map_sync_ifl_map_to_device(struct rwnx_hw *rwnx_hw);
int bes_filter_map_sync_cfl_map_to_device(struct rwnx_hw *rwnx_hw, struct bes_fl_map *cfl_map);

/* reset f_idx */
void bes_filter_map_reset_f_idx(struct bes_fl_map *fl_map);

/* init and deinit */
void bes_filter_map_init(struct bes_fl_map *fl_map);
void bes_filter_map_deinit(struct bes_fl_map *fl_map);

#endif /* __BES_FILTER_MAP_H__ */