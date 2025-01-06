/*
 * @file bes_filter.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __BES_FILTER_H__
#define __BES_FILTER_H__

/*
* ARP related DECLARATIONS
* ***************************************************************************************
*/

#define FILTER_ID_ARP_NS_OFFLOAD			((u8) 0x00)
#define FILTER_ID_DISABLE_ALL				((u8) 0x40)
#define FILTER_ID_ENABLE_ALL				((u8) 0x41)
#define FILTER_ID_SET_TO_DEFAULT			((u8) 0x42)
#define FILTER_ID_DELETE_DEFAULT			((u8) 0x43)
#define FILTER_ID_ADD_FILTER				((u8) 0x44)
#define FILTER_ID_SWITCH_FILTER				((u8) 0x45)
#define FILTER_ID_BIND_OFFLOAD				((u8) 0x46)
#define FILTER_ID_FILTER_RESET				((u8) 0x47)
#define FILTER_ID_MISMATCH_ACTION			((u8) 0x48)
#define FILTER_ID_GET_CONFIG				((u8) 0x49)

#define FILTER_PACKET_TYPE_UNICAST			((u8) 0x01)
#define FILTER_PACKET_TYPE_MULTICAST			((u8) 0x02)
#define FILTER_PACKET_TYPE_BROADCAST			((u8) 0x03)
#define FILTER_PACKET_TYPE_ALL_VALID			((u8) 0x04)

#define FILTER_CONDITION_MIB_MASK			((u8) 0x80)
#define FILTER_CONDITION_MIB_GROUP			((u8) 0x81)
#define FILTER_CONDITION_MIB_MAGIC			((u8) 0x82)
#define FILTER_CONDITION_MIB_ETHERNET			((u8) 0x83)
#define FILTER_CONDITION_MIB_UDP			((u8) 0x84)
#define FILTER_CONDITION_MIB_ARP			((u8) 0x85)

#define FILTER_CONDITION_MIB_MAC_ADDRESS		((u8) 0x86)
#define FILTER_CONDITION_MIB_IPV4_ADDRESS		((u8) 0x87)
#define FILTER_CONDITION_MIB_IPV6_ADDRESS		((u8) 0x88)
#define FILTER_CONDITION_MIB_NS				((u8) 0x89)

#define FILTER_OPERATION_EQUAL				((u8) 0x01)
#define FILTER_OPERATION_MASKED				((u8) 0x02)
#define FILTER_OPERATION_NOT_EQUAL			((u8) 0x03)

#define FILTER_HEADER_OFFSET_TYPE_MASK			((u16) 0xC000)
#define FILTER_HEADER_OFFSET_TYPE_MAC			((u16) 0x0000)
#define FILTER_HEADER_OFFSET_TYPE_LLC			((u16) 0x8000)
#define FILTER_HEADER_OFFSET_TYPE_PROTOCOL		((u16) 0x4000)

#define ARP_FLAG_FILTER					(1<<0)
#define ARP_FLAG_REPLY					(1<<1)

#define NS_FLAG_FILTER					(1<<0)
#define NS_FLAG_REPLY					(1<<1)

#define RXF_OFFLOAD_FUNC_MAX				(8)
#define ARP_OFFLOAD_FUNC_IDX				(0)
#define NS_OFFLOAD_FUNC_IDX				(1)
#define GTK_OFFLOAD_FUNC_IDX				(2)

struct arp_ip_addr_tag
{
	uint32_t enable;	/* bitset of flags: filter, reply */
	uint8_t ipv4_addr[4];	/* IPv4 address in uint32 big endian format */
};

struct ns_ip_addr_tag
{
	uint32_t enable;	/* bitset of flags: filter, reply */
	uint8_t  ipv6_addr[16];	/* IPv6 address stored in big endian format */
};

enum bes_filter_match_mode
{
	RXF_MATCH_EQUAL = 0,
	RXF_MATCH_NOT_EQUAL,
	RXF_MATCH_RANGE,
	RXF_MATCH_OUT_OF_RANGE,
};

enum bes_filter_action
{
	RXF_ACCEPT = 0,
	RXF_REJECT,
	RXF_CONTINUE,
	RXF_MISMATCH
};

typedef enum
{
	FILTER_ACTION_DISCARD,
	FILTER_ACTION_COALESCING,
	FILTER_ACTION_WAKEONLAN,
	FILTER_ACTION_ACCEPT
} eFilterAction;

struct bes_filter_item
{
	u16 len;
	u8 match_mode;
	u8 action;
	u16 offset;
	u16 pattern_len;
	u8 content[0];
};

#define RXF_BEACON_FILTER_SIZE      20
struct rxf_beacon_filter
{
	bool valid;
	u8 vif_idx;
	u8 elt_num;
	u8 velt_num;
	u8 elt_ext_num;
	u8 elt_ids[RXF_BEACON_FILTER_SIZE];
};

struct beacon_filter_set_req {
	struct mm_custom_req_hdr hdr;
	struct rxf_beacon_filter filter;
};

struct beacon_filter_switch_req {
	struct mm_custom_req_hdr hdr;
	bool valid;
};

struct unmatch_dst_mgmt_filter_switch_req {
	// Message header
	struct mm_custom_req_hdr hdr;
	// Filter enable control.
	u8 filter_en;
};

struct mm_hw_filter
{
	u32 bssid_low;
	u32 bssid_hi;
	u32 umac_filter;
	u32 lmac_filter;
};

struct hw_filter_set_req {
	struct mm_custom_req_hdr hdr;
	struct mm_hw_filter filter;
};

struct hw_filter_get_req {
	struct mm_custom_req_hdr hdr;
};

/* ipv4 filter */
int bes_filter_add_mac_da_match(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	struct mac_addr *addr, u8 match_mode, u8 action);
int bes_filter_add_mac_sa_match(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	struct mac_addr *addr, u8 match_mode, u8 action);
int bes_filter_add_eth_type_match(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u16 eth_type, u8 match_mode, u8 action);
int bes_filter_add_ipv4_src_match(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u32 ip, u8 match_mode, u8 action);
int bes_filter_add_ipv4_dst_match(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u32 ip, u8 match_mode, u8 action);
int bes_filter_add_ipv4_proto_match(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u8 proto, u8 match_mode, u8 action);
int bes_filter_add_ipv4_arp_match(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 offload_idx);
int bes_filter_add_magic_packet_match(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	struct mac_addr *addr, u8 action);
int bes_filter_add_ipv4_multicast_match(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 action);
int bes_filter_add_all_multicast_match(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 action);
int bes_filter_add_pattern_match(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	const struct cfg80211_pkt_pattern *pattern, u8 match_mode, u8 action);
int bes_filter_add_ipv4_udp_dst_match_single(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u16 port, u8 match_mode, u8 action);
int bes_filter_add_ipv4_udp_dst_match_range(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u16 s_port, u16 d_port, u8 match_mode, u8 action);
int bes_filter_add_ipv4_udp_src_match_single(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u16 port, u8 match_mode, u8 action);
int bes_filter_add_ipv4_udp_src_match_range(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u16 s_port, u16 d_port, u8 match_mode, u8 action);
int bes_filter_add_ipv4_udp_src_and_dst_match_single(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u16 s_port, u16 d_port, u8 action);
int bes_filter_add_ipv4_tcp_dst_match_single(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u16 port, u8 match_mode, u8 action);
int bes_filter_add_ipv4_tcp_dst_match_range(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u16 s_port, u16 d_port, u8 match_mode, u8 action);
int bes_filter_add_ipv4_tcp_src_match_single(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u16 port, u8 match_mode, u8 action);
int bes_filter_add_ipv4_tcp_src_match_range(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u16 s_port, u16 d_port, u8 match_mode, u8 action);

/* ipv6 filter */
int bes_filter_add_ipv6_src_match(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	const struct in6_addr *addr, u8 match_mode, u8 action);
int bes_filter_add_ipv6_dst_match(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	const struct in6_addr *addr, u8 match_mode, u8 action);
int bes_filter_add_ipv6_multicast_match(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 action);
int bes_filter_add_ipv6_ns_match(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 offload_idx);
int bes_filter_add_ipv6_udp_src_and_dst_match_single(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u16 s_port, u16 d_port, u8 action);

/* filter ctrl */
int bes_packet_filter_ctrl(struct rwnx_hw *rwnx_hw, u8 vif_idx, bool enable);
int bes_packet_filter_set_arp_offload(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, const __be32 ifa_local_ip);
int bes_packet_filter_clear_arp_offload(struct rwnx_hw *rwnx_hw, const u8 vif_idx);
int bes_packet_filter_set_ns_offload(struct rwnx_hw *rwnx_hw, const struct rwnx_vif *vif, const struct in6_addr *addr);
int bes_packet_filter_clear_ns_offload(struct rwnx_hw *rwnx_hw, const u8 vif_idx);
int bes_filter_set_gtk_rekey_offload(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 offload_idx);
int bes_filter_reset(struct rwnx_hw *rwnx_hw);
int bes_filter_set_offload(struct rwnx_hw *rwnx_hw, u8 f_idx, u8 of_idx);
int bes_filter_switch(struct rwnx_hw *rwnx_hw, u8 f_idx, bool enable);
int bes_filter_set_mismatch_action(struct rwnx_hw *rwnx_hw, eFilterAction action);
int bes_filter_get_config_param(struct rwnx_hw *rwnx_hw,
	u8 *max_rxf_idx, u16 *max_rxf_arry_idx, u8 *mismatch_action);

/* hw and beacon filter */
int bes_filter_get_hw_filter(struct rwnx_hw *rwnx_hw, struct mm_hw_filter *hw_filter);
int bes_filter_set_hw_filter(struct rwnx_hw *rwnx_hw, struct mm_hw_filter *hw_filter);
int bes_filter_set_beacon_filter(struct rwnx_hw *rwnx_hw, bool valid, u8 vif_idx,
	const u8 *elt_ids, const u32 *velt_ids, const u8 *elt_ext_ids);
int bes_filter_switch_beacon_filter(struct rwnx_hw *rwnx_hw, bool valid);
int bes_filter_unmatch_dst_mgmt_set(struct rwnx_hw *rwnx_hw, bool enable);

#endif