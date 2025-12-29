/*
 * @file bes_filter.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <net/cfg80211.h>
#include <net/ip.h>
#include "bes_log.h"
#include "rwnx_msg_tx.h"
#include "bes_filter.h"
#include "bes_gtk_offload.h"

#define BES_FILTER_INVALID_OFFLOAD_IDX		0xff

const int bes_filter_item_size = sizeof(struct bes_filter_item);

int bes_filter_add_mac_da_match(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	struct mac_addr *addr, u8 match_mode, u8 action)
{
	struct mm_set_packet_filter_req filter;
	struct bes_filter_item *item = NULL;
	s8 f_idx = 0;
	u16 item_len = 0;

	// set mac dest match item
	item_len = bes_filter_item_size + (MAC_ADDR_LEN << 1);
	if (item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[0];
	item->action = action;
	item->len = item_len;
	item->match_mode = match_mode;
	item->offset = offsetof(struct ethhdr, h_dest);
	item->pattern_len = MAC_ADDR_LEN;
	memset(&item->content[0], 0xff, MAC_ADDR_LEN); // set mask
	memcpy(&item->content[MAC_ADDR_LEN], addr, MAC_ADDR_LEN); // set pattern

	// set filter header
	filter.vif_rxf_idx = vif_idx;
	filter.filter.id = FILTER_ID_ADD_FILTER; // map to command id
	filter.filter.addition_id = BES_FILTER_INVALID_OFFLOAD_IDX; // map to offload index
	filter.filter.state = true; // map to filter state
	filter.filter.param_len = item->len; // map to filter match length

	// send add filter request to device
	rwnx_send_packet_filter_request(rwnx_hw, &filter, &f_idx, sizeof(f_idx));

	return f_idx;
}

int bes_filter_add_mac_sa_match(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	struct mac_addr *addr, u8 match_mode, u8 action)
{
	struct mm_set_packet_filter_req filter;
	struct bes_filter_item *item = NULL;
	s8 f_idx = 0;
	u16 item_len = 0;

	// set mac source match item
	item_len = bes_filter_item_size + (MAC_ADDR_LEN << 1);
	if (item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[0];
	item->action = action;
	item->len = item_len;
	item->match_mode = match_mode;
	item->offset = offsetof(struct ethhdr, h_source);
	item->pattern_len = MAC_ADDR_LEN;
	memset(&item->content[0], 0xff, MAC_ADDR_LEN); // set mask
	memcpy(&item->content[MAC_ADDR_LEN], addr, MAC_ADDR_LEN); // set pattern

	// set filter header
	filter.vif_rxf_idx = vif_idx;
	filter.filter.id = FILTER_ID_ADD_FILTER; // map to command id
	filter.filter.addition_id = BES_FILTER_INVALID_OFFLOAD_IDX; // map to offload index
	filter.filter.state = true; // map to filter state
	filter.filter.param_len = item->len; // map to filter match length

	// send add filter request to device
	rwnx_send_packet_filter_request(rwnx_hw, &filter, &f_idx, sizeof(f_idx));

	return f_idx;
}

int bes_filter_add_eth_type_match(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u16 eth_type, u8 match_mode, u8 action)
{
	struct mm_set_packet_filter_req filter;
	struct bes_filter_item *item = NULL;
	s8 f_idx = 0;
	u16 item_len = 0;

	eth_type = cpu_to_be16(eth_type);

	// set ethernet type match item
	item_len = bes_filter_item_size + (2 << 1);
	if (item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[0];
	item->action = action;
	item->len = item_len;
	item->match_mode = match_mode;
	item->offset = offsetof(struct ethhdr, h_proto);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &eth_type, 2); // set pattern

	// set filter header
	filter.vif_rxf_idx = vif_idx;
	filter.filter.id = FILTER_ID_ADD_FILTER; // map to command id
	filter.filter.addition_id = BES_FILTER_INVALID_OFFLOAD_IDX; // map to offload index
	filter.filter.state = true; // map to filter state
	filter.filter.param_len = item->len; // map to filter match length

	// send add filter request to device
	rwnx_send_packet_filter_request(rwnx_hw, &filter, &f_idx, sizeof(f_idx));

	return f_idx;
}

int bes_filter_add_ipv4_src_match(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u32 ip, u8 match_mode, u8 action)
{
	struct mm_set_packet_filter_req filter;
	struct bes_filter_item *item = NULL;
	u16 ip_protp = cpu_to_be16(ETH_P_IP);
	s8 f_idx = 0;
	u8 rxf_item_oft = 0;
	u16 item_len = 0;

	// set rxf item to match ip packet
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[0];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = offsetof(struct ethhdr, h_proto);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &ip_protp, 2); // set pattern

	// set rxf item to match ip source address
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (4 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = action;
	item->len = item_len;
	item->match_mode = match_mode;
	item->offset = sizeof(struct ethhdr) + offsetof(struct iphdr, saddr);
	item->pattern_len = 4;
	memset(&item->content[0], 0xff, 4); // set mask
	memcpy(&item->content[4], &ip, 4); // set pattern

	// set filter header
	filter.vif_rxf_idx = vif_idx;
	filter.filter.id = FILTER_ID_ADD_FILTER; // map to command id
	filter.filter.addition_id = BES_FILTER_INVALID_OFFLOAD_IDX; // map to offload index
	filter.filter.state = true; // map to filter state
	filter.filter.param_len = rxf_item_oft + item->len; // map to filter match length

	// send add filter request to device
	rwnx_send_packet_filter_request(rwnx_hw, &filter, &f_idx, sizeof(f_idx));

	return f_idx;
}

int bes_filter_add_ipv4_dst_match(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u32 ip, u8 match_mode, u8 action)
{
	struct mm_set_packet_filter_req filter;
	struct bes_filter_item *item = NULL;
	u16 ip_protp = cpu_to_be16(ETH_P_IP);
	s8 f_idx = 0;
	u8 rxf_item_oft = 0;
	u16 item_len = 0;

	// set rxf item to match ip packet
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[0];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = offsetof(struct ethhdr, h_proto);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &ip_protp, 2); // set pattern

	// set rxf item to match ip destination address
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (4 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = action;
	item->len = item_len;
	item->match_mode = match_mode;
	item->offset = sizeof(struct ethhdr) + offsetof(struct iphdr, daddr);
	item->pattern_len = 4;
	memset(&item->content[0], 0xff, 4); // set mask
	memcpy(&item->content[4], &ip, 4); // set pattern

	// set filter header
	filter.vif_rxf_idx = vif_idx;
	filter.filter.id = FILTER_ID_ADD_FILTER; // map to command id
	filter.filter.addition_id = BES_FILTER_INVALID_OFFLOAD_IDX; // map to offload index
	filter.filter.state = true; // map to filter state
	filter.filter.param_len = rxf_item_oft + item->len; // map to filter match length

	// send add filter request to device
	rwnx_send_packet_filter_request(rwnx_hw, &filter, &f_idx, sizeof(f_idx));

	return f_idx;
}

int bes_filter_add_ipv4_proto_match(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u8 proto, u8 match_mode, u8 action)
{
	struct mm_set_packet_filter_req filter;
	struct bes_filter_item *item = NULL;
	u16 ip_protp = cpu_to_be16(ETH_P_IP);
	s8 f_idx = 0;
	u8 rxf_item_oft = 0;
	u16 item_len = 0;

	// set rxf item to match ip packet
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[0];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = offsetof(struct ethhdr, h_proto);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &ip_protp, 2); // set pattern

	// set rxf item to match ip protocol
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (1 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = action;
	item->len = item_len;
	item->match_mode = match_mode;
	item->offset = sizeof(struct ethhdr) + offsetof(struct iphdr, protocol);
	item->pattern_len = 1;
	memset(&item->content[0], 0xff, 1); // set mask
	memcpy(&item->content[1], &proto, 1); // set pattern

	// set filter header
	filter.vif_rxf_idx = vif_idx;
	filter.filter.id = FILTER_ID_ADD_FILTER; // map to command id
	filter.filter.addition_id = BES_FILTER_INVALID_OFFLOAD_IDX; // map to offload index
	filter.filter.state = true; // map to filter state
	filter.filter.param_len = rxf_item_oft + item->len; // map to filter match length

	// send add filter request to device
	rwnx_send_packet_filter_request(rwnx_hw, &filter, &f_idx, sizeof(f_idx));

	return f_idx;
}

int bes_filter_add_ipv4_arp_match(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 offload_idx)
{
	struct mm_set_packet_filter_req filter;
	struct bes_filter_item *item = NULL;
	u16 arp_protp = cpu_to_be16(ETH_P_ARP);
	u16 arp_op = cpu_to_be16(ARPOP_REQUEST);
	s8 f_idx = 0;
	u8 rxf_item_oft = 0;
	u16 item_len = 0;

	// set rxf item to match ARP packet
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[0];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = offsetof(struct ethhdr, h_proto);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &arp_protp, 2); // set pattern

	// set rxf item to match ARP Request packet
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = RXF_ACCEPT;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = sizeof(struct ethhdr) + offsetof(struct arphdr, ar_op);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &arp_op, 2); // set pattern

	// set filter header
	filter.vif_rxf_idx = vif_idx;
	filter.filter.id = FILTER_ID_ADD_FILTER; // map to command id
	filter.filter.addition_id = BES_FILTER_INVALID_OFFLOAD_IDX & offload_idx; // map to offload index
	filter.filter.state = true; // map to filter state
	filter.filter.param_len = rxf_item_oft + item->len; // map to filter match length

	// send add filter request to device
	rwnx_send_packet_filter_request(rwnx_hw, &filter, &f_idx, sizeof(f_idx));

	return f_idx;
}

int bes_filter_set_gtk_rekey_offload(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 offload_idx)
{
	struct mm_set_packet_filter_req filter;
	struct rwnx_vif *vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);
	struct bes_filter_item *item = NULL;
	struct eapol_pattern eapol_msg_pattern = {0};
	u16 pae_protp = cpu_to_be16(ETH_P_PAE);
	s8 f_idx = 0;
	u8 rxf_item_oft = 0;
	u16 item_len = 0;

	/* Check if the virtual interface is valid */
	if (vif == NULL)
		return -1;

	// set rxf item to match ETH_P_PAE
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[0];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = offsetof(struct ethhdr, h_proto);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &pae_protp, 2); // set pattern

	// set rxf item to match EAPOL packet
	bes_get_group_msg1_match_pattern(vif, &eapol_msg_pattern);
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (sizeof(struct eapol_header) << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = RXF_ACCEPT;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = sizeof(struct ethhdr);
	item->pattern_len = sizeof(struct eapol_header);
	memcpy(&item->content[0], &eapol_msg_pattern.mask, item->pattern_len); // set mask
	memcpy(&item->content[item->pattern_len], &eapol_msg_pattern.hdr, item->pattern_len); // set pattern

	// set filter header
	filter.vif_rxf_idx = vif->vif_index;
	filter.filter.id = FILTER_ID_ADD_FILTER; // map to command id
	filter.filter.addition_id = BES_FILTER_INVALID_OFFLOAD_IDX & offload_idx; // map to offload index
	filter.filter.state = true; // map to filter state
	filter.filter.param_len = rxf_item_oft + item->len; // map to filter match length

	// send add filter request to device
	rwnx_send_packet_filter_request(rwnx_hw, &filter, &f_idx, sizeof(f_idx));

	return f_idx;
}

int bes_filter_add_magic_packet_match(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	struct mac_addr *addr, u8 action)
{
	struct mm_set_packet_filter_req filter;
	struct bes_filter_item *item = NULL;
	u8 udp_protp = IP_PROT_UDP;
	s8 f_idx = 0;
	u8 rxf_item_oft = 0;
	u16 item_len = 0;

	// set rxf item to match udp packet
	item_len = bes_filter_item_size + (1 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[0];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = sizeof(struct ethhdr) + offsetof(struct iphdr, protocol);
	item->pattern_len = 1;
	memset(&item->content[0], 0xff, 1); // set mask
	memcpy(&item->content[1], &udp_protp, 1); // set pattern

	// set rxf item to match sync stream
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (MAC_ADDR_LEN << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = sizeof(struct ethhdr) + sizeof(struct iphdr) + sizeof(struct udphdr);
	item->pattern_len = MAC_ADDR_LEN;
	memset(&item->content[0], 0xff, MAC_ADDR_LEN); // set mask
	memset(&item->content[MAC_ADDR_LEN], 0xff, MAC_ADDR_LEN); // set pattern

	// set rxf item to match destination mac
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (MAC_ADDR_LEN << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = sizeof(struct ethhdr) + sizeof(struct iphdr) +
			sizeof(struct udphdr) + MAC_ADDR_LEN;
	item->pattern_len = MAC_ADDR_LEN;
	memset(&item->content[0], 0xff, MAC_ADDR_LEN); // set mask
	memcpy(&item->content[MAC_ADDR_LEN], addr, MAC_ADDR_LEN); // set pattern

	// set rxf item to match destination mac
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (MAC_ADDR_LEN << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = action;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = sizeof(struct ethhdr) + sizeof(struct iphdr) +
			sizeof(struct udphdr) + (MAC_ADDR_LEN << 1);
	item->pattern_len = MAC_ADDR_LEN;
	memset(&item->content[0], 0xff, MAC_ADDR_LEN); // set mask
	memcpy(&item->content[MAC_ADDR_LEN], addr, MAC_ADDR_LEN); // set pattern

	// set filter header
	filter.vif_rxf_idx = vif_idx;
	filter.filter.id = FILTER_ID_ADD_FILTER; // map to command id
	filter.filter.addition_id = BES_FILTER_INVALID_OFFLOAD_IDX; // map to offload index
	filter.filter.state = true; // map to filter state
	filter.filter.param_len = rxf_item_oft + item->len; // map to filter match length

	// send add filter request to device
	rwnx_send_packet_filter_request(rwnx_hw, &filter, &f_idx, sizeof(f_idx));

	return f_idx;
}

int bes_filter_add_ipv4_multicast_match(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 action)
{
	struct mm_set_packet_filter_req filter;
	struct bes_filter_item *item = NULL;
	s8 f_idx = 0;
	const u8 ipv4_multicast[3] = {
		0x01, 0x00, 0x5e
	};
	u16 item_len = 0;

	// set multicast mac dest match item
	item_len = bes_filter_item_size + (3 << 1);
	if (item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[0];
	item->action = action;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = offsetof(struct ethhdr, h_dest);
	item->pattern_len = 3;
	memset(&item->content[0], 0xff, 3); // set mask
	memcpy(&item->content[3], ipv4_multicast, 3); // set pattern

	// set filter header
	filter.vif_rxf_idx = vif_idx;
	filter.filter.id = FILTER_ID_ADD_FILTER; // map to command id
	filter.filter.addition_id = BES_FILTER_INVALID_OFFLOAD_IDX; // map to offload index
	filter.filter.state = true; // map to filter state
	filter.filter.param_len = item->len; // map to filter match length

	// send add filter request to device
	rwnx_send_packet_filter_request(rwnx_hw, &filter, &f_idx, sizeof(f_idx));

	return f_idx;
}

int bes_filter_add_ipv6_multicast_match(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 action)
{
	struct mm_set_packet_filter_req filter;
	struct bes_filter_item *item = NULL;
	s8 f_idx = 0;
	const u8 ipv6_multicast[2] = {
		0x33, 0x33
	};
	u16 item_len = 0;

	// set multicast mac dest match item
	item_len = bes_filter_item_size + (2 << 1);
	if (item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[0];
	item->action = action;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = offsetof(struct ethhdr, h_dest);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], ipv6_multicast, 2); // set pattern

	// set filter header
	filter.vif_rxf_idx = vif_idx;
	filter.filter.id = FILTER_ID_ADD_FILTER; // map to command id
	filter.filter.addition_id = BES_FILTER_INVALID_OFFLOAD_IDX; // map to offload index
	filter.filter.state = true; // map to filter state
	filter.filter.param_len = item->len; // map to filter match length

	// send add filter request to device
	rwnx_send_packet_filter_request(rwnx_hw, &filter, &f_idx, sizeof(f_idx));

	return f_idx;
}

int bes_filter_add_all_multicast_match(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 action)
{
	struct mm_set_packet_filter_req filter;
	struct bes_filter_item *item = NULL;
	s8 f_idx = 0;
	u8 rxf_item_oft = 0;
	u16 item_len = 0;

	if (action == RXF_ACCEPT) { /* Receives all multicast packets(include boardcast) */
		// set multicast and boardcast match item
		item_len = bes_filter_item_size + (1 << 1);
		if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
			return -ENOMEM;
		item = (struct bes_filter_item *)&filter.filter_param[0];
		item->action = action;
		item->len = item_len;
		item->match_mode = RXF_MATCH_EQUAL;
		item->offset = offsetof(struct ethhdr, h_dest);
		item->pattern_len = 1;
		memset(&item->content[0], 0x01, 1); // set mask
		memset(&item->content[1], 0x01, 1); // set pattern
	} else { /* Rejects all multicast packets(exclude boardcast) */
		// set multicast and boardcast match item
		item_len = bes_filter_item_size + (1 << 1);
		if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
			return -ENOMEM;
		item = (struct bes_filter_item *)&filter.filter_param[0];
		item->action = RXF_CONTINUE;
		item->len = item_len;
		item->match_mode = RXF_MATCH_EQUAL;
		item->offset = offsetof(struct ethhdr, h_dest);
		item->pattern_len = 1;
		memset(&item->content[0], 0x01, 1); // set mask
		memset(&item->content[1], 0x01, 1); // set pattern

		// set not equel to boardcast
		rxf_item_oft += item->len;
		item_len = bes_filter_item_size + (1 << 1);
		if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
			return -ENOMEM;
		item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
		item->action = action;
		item->len = item_len;
		item->match_mode = RXF_MATCH_NOT_EQUAL;
		item->offset =  offsetof(struct ethhdr, h_dest);
		item->pattern_len = 1;
		memset(&item->content[0], 0xff, 1); // set mask
		memset(&item->content[1], 0xff, 1); // set pattern
	}

	// set filter header
	filter.vif_rxf_idx = vif_idx;
	filter.filter.id = FILTER_ID_ADD_FILTER; // map to command id
	filter.filter.addition_id = BES_FILTER_INVALID_OFFLOAD_IDX; // map to offload index
	filter.filter.state = true; // map to filter state
	filter.filter.param_len = rxf_item_oft + item->len; // map to filter match length

	// send add filter request to device
	rwnx_send_packet_filter_request(rwnx_hw, &filter, &f_idx, sizeof(f_idx));

	return f_idx;
}

int bes_filter_add_pattern_match(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	const struct cfg80211_pkt_pattern *pattern, u8 match_mode, u8 action)
{
	struct mm_set_packet_filter_req filter;
	struct bes_filter_item *item = NULL;
	s8 f_idx = 0;
	int i = 0;
	u8 *mask = NULL;
	u16 item_len = 0;

	mask = kmalloc(pattern->pattern_len, GFP_ATOMIC);
	if (!mask) {
		return -ENOMEM;
	}

	for (i = 0; i < pattern->pattern_len; i++) {
		if (pattern->mask[i / 8] & (1 << (i % 8))) {
			mask[i] = 0xff;
		} else {
			mask[i] = 0x00;
		}
	}

	// set pattern match item
	item_len = bes_filter_item_size + (pattern->pattern_len << 1);
	if (item_len > BES_FILTER_PARAM_MAX_LEN) {
		kfree(mask);
		return -ENOMEM;
	}
	item = (struct bes_filter_item *)&filter.filter_param[0];
	item->action = action;
	item->len = item_len;
	item->match_mode = match_mode;
	item->offset = pattern->pkt_offset;
	item->pattern_len = pattern->pattern_len;
	memcpy(&item->content[0], mask, pattern->pattern_len); // set mask
	memcpy(&item->content[pattern->pattern_len], pattern->pattern, pattern->pattern_len); // set pattern

	// set filter header
	filter.vif_rxf_idx = vif_idx;
	filter.filter.id = FILTER_ID_ADD_FILTER; // map to command id
	filter.filter.addition_id = BES_FILTER_INVALID_OFFLOAD_IDX; // map to offload index
	filter.filter.state = true; // map to filter state
	filter.filter.param_len = item->len; // map to filter match length

	// send add filter request to device
	rwnx_send_packet_filter_request(rwnx_hw, &filter, &f_idx, sizeof(f_idx));

	kfree(mask);

	return f_idx;
}

int bes_filter_add_ipv4_udp_dst_match_single(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u16 port, u8 match_mode, u8 action)
{
	struct mm_set_packet_filter_req filter;
	struct bes_filter_item *item = NULL;
	u16 ip_proto = cpu_to_be16(ETH_P_IP);
	u8 udp_protp = IP_PROT_UDP;
	s8 f_idx = 0;
	u8 rxf_item_oft = 0;
	u16 item_len = 0;

	port = cpu_to_be16(port);

	// set rxf item to match ip packet
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[0];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = offsetof(struct ethhdr, h_proto);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &ip_proto, 2); // set pattern

	// set rxf item to match udp packet
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (1 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = sizeof(struct ethhdr) + offsetof(struct iphdr, protocol);
	item->pattern_len = 1;
	memset(&item->content[0], 0xff, 1); // set mask
	memcpy(&item->content[1], &udp_protp, 1); // set pattern

	// set rxf item to match udp destination port
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = action;
	item->len = item_len;
	item->match_mode = match_mode;
	item->offset = sizeof(struct ethhdr) + sizeof(struct iphdr) + offsetof(struct udphdr, dest);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &port, 2); // set pattern

	// set filter header
	filter.vif_rxf_idx = vif_idx;
	filter.filter.id = FILTER_ID_ADD_FILTER;			// map to command id
	filter.filter.addition_id = BES_FILTER_INVALID_OFFLOAD_IDX; // map to offload index
	filter.filter.state = true;				// map to filter state
	filter.filter.param_len = rxf_item_oft + item->len; // map to filter match length

	// send add filter request to device
	rwnx_send_packet_filter_request(rwnx_hw, &filter, &f_idx, sizeof(f_idx));

	return f_idx;
}

int bes_filter_add_ipv4_udp_dst_match_range(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u16 s_port, u16 d_port, u8 match_mode, u8 action)
{
	struct mm_set_packet_filter_req filter;
	struct bes_filter_item *item = NULL;
	u16 ip_proto = cpu_to_be16(ETH_P_IP);
	u8 udp_proto = IP_PROT_UDP;
	s8 f_idx = 0;
	u8 rxf_item_oft = 0;
	u16 item_len = 0;

	s_port = cpu_to_be16(s_port);
	d_port = cpu_to_be16(d_port);

	// set rxf item to match ip packet
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[0];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = offsetof(struct ethhdr, h_proto);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &ip_proto, 2); // set pattern

	// set rxf item to match udp packet
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (1 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = sizeof(struct ethhdr) + offsetof(struct iphdr, protocol);
	item->pattern_len = 1;
	memset(&item->content[0], 0xff, 1); // set mask
	memcpy(&item->content[1], &udp_proto, 1); // set pattern

	// set rxf item to match udp destination port
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + 6;
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = action;
	item->len = item_len;
	item->match_mode = match_mode;
	item->offset = sizeof(struct ethhdr) + sizeof(struct iphdr) + offsetof(struct udphdr, dest);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &s_port, 2); // set low pattern
	memcpy(&item->content[4], &d_port, 2); // set high pattern

	// set filter header
	filter.vif_rxf_idx = vif_idx;
	filter.filter.id = FILTER_ID_ADD_FILTER; // map to command id
	filter.filter.addition_id = BES_FILTER_INVALID_OFFLOAD_IDX; // map to offload index
	filter.filter.state = true; // map to filter state
	filter.filter.param_len = rxf_item_oft + item->len; // map to filter match length

	// send add filter request to device
	rwnx_send_packet_filter_request(rwnx_hw, &filter, &f_idx, sizeof(f_idx));

	return f_idx;
}

int bes_filter_add_ipv4_udp_src_match_single(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u16 port, u8 match_mode, u8 action)
{
	struct mm_set_packet_filter_req filter;
	struct bes_filter_item *item = NULL;
	u16 ip_proto = cpu_to_be16(ETH_P_IP);
	u8 udp_proto = IP_PROT_UDP;
	s8 f_idx = 0;
	u8 rxf_item_oft = 0;
	u16 item_len = 0;

	port = cpu_to_be16(port);

	// set rxf item to match ip packet
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[0];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = offsetof(struct ethhdr, h_proto);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &ip_proto, 2); // set pattern

	// set rxf item to match udp packet
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (1 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = sizeof(struct ethhdr) + offsetof(struct iphdr, protocol);
	item->pattern_len = 1;
	memset(&item->content[0], 0xff, 1); // set mask
	memcpy(&item->content[1], &udp_proto, 1); // set pattern

	// set rxf item to match udp source port
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = action;
	item->len = item_len;
	item->match_mode = match_mode;
	item->offset = sizeof(struct ethhdr) + sizeof(struct iphdr) + offsetof(struct udphdr, source);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &port, 2); // set pattern

	// set filter header
	filter.vif_rxf_idx = vif_idx;
	filter.filter.id = FILTER_ID_ADD_FILTER; // map to command id
	filter.filter.addition_id = BES_FILTER_INVALID_OFFLOAD_IDX; // map to offload index
	filter.filter.state = true; // map to filter state
	filter.filter.param_len = rxf_item_oft + item->len; // map to filter match length

	// send add filter request to device
	rwnx_send_packet_filter_request(rwnx_hw, &filter, &f_idx, sizeof(f_idx));

	return f_idx;
}

int bes_filter_add_ipv4_udp_src_match_range(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u16 s_port, u16 d_port, u8 match_mode, u8 action)
{
	struct mm_set_packet_filter_req filter;
	struct bes_filter_item *item = NULL;
	u16 ip_proto = cpu_to_be16(ETH_P_IP);
	u8 udp_protp = IP_PROT_UDP;
	s8 f_idx = 0;
	u8 rxf_item_oft = 0;
	u16 item_len = 0;

	s_port = cpu_to_be16(s_port);
	d_port = cpu_to_be16(d_port);

	// set rxf item to match ip packet
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[0];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = offsetof(struct ethhdr, h_proto);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &ip_proto, 2); // set pattern

	// set rxf item to match udp packet
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (1 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = sizeof(struct ethhdr) + offsetof(struct iphdr, protocol);
	item->pattern_len = 1;
	memset(&item->content[0], 0xff, 1); // set mask
	memcpy(&item->content[1], &udp_protp, 1); // set pattern

	// set rxf item to match udp source port
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + 6;
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = action;
	item->len = item_len;
	item->match_mode = match_mode;
	item->offset = sizeof(struct ethhdr) + sizeof(struct iphdr) + offsetof(struct udphdr, source);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &s_port, 2); // set low pattern
	memcpy(&item->content[4], &d_port, 2); // set high pattern

	// set filter header
	filter.vif_rxf_idx = vif_idx;
	filter.filter.id = FILTER_ID_ADD_FILTER; // map to command id
	filter.filter.addition_id = BES_FILTER_INVALID_OFFLOAD_IDX; // map to offload index
	filter.filter.state = true; // map to filter state
	filter.filter.param_len = rxf_item_oft + item->len; // map to filter match length

	// send add filter request to device
	rwnx_send_packet_filter_request(rwnx_hw, &filter, &f_idx, sizeof(f_idx));

	return f_idx;
}

int bes_filter_add_ipv4_udp_src_and_dst_match_single(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u16 s_port, u16 d_port, u8 action)
{
	struct mm_set_packet_filter_req filter;
	struct bes_filter_item *item = NULL;
	u16 h_proto = cpu_to_be16(ETH_P_IP);
	u8 udp_protp = IP_PROT_UDP; /* UDP */
	s8 f_idx = 0;
	u8 rxf_item_oft = 0;
	u16 item_len = 0;

	s_port = cpu_to_be16(s_port);
	d_port = cpu_to_be16(d_port);

	// set rxf item to match ipv4 packet
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[0];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = offsetof(struct ethhdr, h_proto);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &h_proto, 2); // set pattern

	// set rxf item to match UDP
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (1 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = sizeof(struct ethhdr) + offsetof(struct iphdr, protocol);
	item->pattern_len = 1;
	memset(&item->content[0], 0xff, 1); // set mask
	memcpy(&item->content[1], &udp_protp, 1); // set pattern

	// set rxf item to match udp src port
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = sizeof(struct ethhdr) + sizeof(struct iphdr) + offsetof(struct udphdr, source);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &s_port, 2); // set pattern

	// set rxf item to match udp dst port
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = action;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = sizeof(struct ethhdr) + sizeof(struct iphdr) + offsetof(struct udphdr, dest);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &d_port, 2); // set pattern

	// set filter header
	filter.vif_rxf_idx = vif_idx;
	filter.filter.id = FILTER_ID_ADD_FILTER;// map to command id
	filter.filter.addition_id = BES_FILTER_INVALID_OFFLOAD_IDX;// map to offload index
	filter.filter.state = true;// map to filter state
	filter.filter.param_len = rxf_item_oft + item->len;// map to filter match length

	// send add filter request to device
	rwnx_send_packet_filter_request(rwnx_hw, &filter, &f_idx, sizeof(f_idx));

	return f_idx;
}

int bes_filter_add_ipv4_tcp_dst_match_single(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u16 port, u8 match_mode, u8 action)
{
	struct mm_set_packet_filter_req filter;
	struct bes_filter_item *item = NULL;
	u16 ip_proto = cpu_to_be16(ETH_P_IP);
	u8 tcp_protp = IP_PROT_TCP;
	s8 f_idx = 0;
	u8 rxf_item_oft = 0;
	u16 item_len = 0;

	port = cpu_to_be16(port);

	// set rxf item to match ip packet
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[0];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = offsetof(struct ethhdr, h_proto);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &ip_proto, 2); // set pattern

	// set rxf item to match tcp packet
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (1 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = sizeof(struct ethhdr) + offsetof(struct iphdr, protocol);
	item->pattern_len = 1;
	memset(&item->content[0], 0xff, 1); // set mask
	memcpy(&item->content[1], &tcp_protp, 1); // set pattern

	// set rxf item to match tcp destination port
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = action;
	item->len = item_len;
	item->match_mode = match_mode;
	item->offset = sizeof(struct ethhdr) + sizeof(struct iphdr) + offsetof(struct tcphdr, dest);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &port, 2); // set pattern

	// set filter header
	filter.vif_rxf_idx = vif_idx;
	filter.filter.id = FILTER_ID_ADD_FILTER; // map to command id
	filter.filter.addition_id = BES_FILTER_INVALID_OFFLOAD_IDX; // map to offload index
	filter.filter.state = true; // map to filter state
	filter.filter.param_len = rxf_item_oft + item->len; // map to filter match length

	// send add filter request to device
	rwnx_send_packet_filter_request(rwnx_hw, &filter, &f_idx, sizeof(f_idx));

	return f_idx;
}

int bes_filter_add_ipv4_tcp_dst_match_range(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u16 s_port, u16 d_port, u8 match_mode, u8 action)
{
	struct mm_set_packet_filter_req filter;
	struct bes_filter_item *item = NULL;
	u16 ip_proto = cpu_to_be16(ETH_P_IP);
	u8 tcp_proto = IP_PROT_TCP;
	s8 f_idx = 0;
	u8 rxf_item_oft = 0;
	u16 item_len = 0;

	s_port = cpu_to_be16(s_port);
	d_port = cpu_to_be16(d_port);

	// set rxf item to match ip packet
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[0];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = offsetof(struct ethhdr, h_proto);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &ip_proto, 2); // set pattern

	// set rxf item to match tcp packet
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (1 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = sizeof(struct ethhdr) + offsetof(struct iphdr, protocol);
	item->pattern_len = 1;
	memset(&item->content[0], 0xff, 1); // set mask
	memcpy(&item->content[1], &tcp_proto, 1); // set pattern

	// set rxf item to match tcp source port
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + 6;
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = action;
	item->len = item_len;
	item->match_mode = match_mode;
	item->offset = sizeof(struct ethhdr) + sizeof(struct iphdr) + offsetof(struct tcphdr, dest);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &s_port, 2); // set low pattern
	memcpy(&item->content[4], &d_port, 2); // set high pattern

	// set filter header
	filter.vif_rxf_idx = vif_idx;
	filter.filter.id = FILTER_ID_ADD_FILTER; // map to command id
	filter.filter.addition_id = BES_FILTER_INVALID_OFFLOAD_IDX; // map to offload index
	filter.filter.state = true; // map to filter state
	filter.filter.param_len = rxf_item_oft + item->len; // map to filter match length

	// send add filter request to device
	rwnx_send_packet_filter_request(rwnx_hw, &filter, &f_idx, sizeof(f_idx));

	return f_idx;
}

int bes_filter_add_ipv4_tcp_src_match_single(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u16 port, u8 match_mode, u8 action)
{
	struct mm_set_packet_filter_req filter;
	struct bes_filter_item *item = NULL;
	u16 ip_proto = cpu_to_be16(ETH_P_IP);
	u8 tcp_proto = IP_PROT_TCP;
	s8 f_idx = 0;
	u8 rxf_item_oft = 0;
	u16 item_len = 0;

	port = cpu_to_be16(port);

	// set rxf item to match ip packet
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[0];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = offsetof(struct ethhdr, h_proto);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &ip_proto, 2); // set pattern

	// set rxf item to match tcp packet
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (1 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = sizeof(struct ethhdr) + offsetof(struct iphdr, protocol);
	item->pattern_len = 1;
	memset(&item->content[0], 0xff, 1); // set mask
	memcpy(&item->content[1], &tcp_proto, 1); // set pattern

	// set rxf item to match tcp source port
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = action;
	item->len = item_len;
	item->match_mode = match_mode;
	item->offset = sizeof(struct ethhdr) + sizeof(struct iphdr) + offsetof(struct tcphdr, source);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &port, 2); // set pattern

	// set filter header
	filter.vif_rxf_idx = vif_idx;
	filter.filter.id = FILTER_ID_ADD_FILTER; // map to command id
	filter.filter.addition_id = BES_FILTER_INVALID_OFFLOAD_IDX; // map to offload index
	filter.filter.state = true; // map to filter state
	filter.filter.param_len = rxf_item_oft + item->len; // map to filter match length

	// send add filter request to device
	rwnx_send_packet_filter_request(rwnx_hw, &filter, &f_idx, sizeof(f_idx));

	return f_idx;
}

int bes_filter_add_ipv4_tcp_src_match_range(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u16 s_port, u16 d_port, u8 match_mode, u8 action)
{
	struct mm_set_packet_filter_req filter;
	struct bes_filter_item *item = NULL;
	u16 ip_proto = cpu_to_be16(ETH_P_IP);
	u8 tcp_proto = IP_PROT_TCP;
	s8 f_idx = 0;
	u8 rxf_item_oft = 0;
	u16 item_len = 0;

	s_port = cpu_to_be16(s_port);
	d_port = cpu_to_be16(d_port);

	// set rxf item to match ip packet
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[0];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = offsetof(struct ethhdr, h_proto);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &ip_proto, 2); // set pattern

	// set rxf item to match tcp packet
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (1 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = sizeof(struct ethhdr) + offsetof(struct iphdr, protocol);
	item->pattern_len = 1;
	memset(&item->content[0], 0xff, 1); // set mask
	memcpy(&item->content[1], &tcp_proto, 1); // set pattern

	// set rxf item to match tcp source port
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + 6;
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = action;
	item->len = item_len;
	item->match_mode = match_mode;
	item->offset = sizeof(struct ethhdr) + sizeof(struct iphdr) + offsetof(struct tcphdr, source);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &s_port, 2); // set low pattern
	memcpy(&item->content[4], &d_port, 2); // set high pattern

	// set filter header
	filter.vif_rxf_idx = vif_idx;
	filter.filter.id = FILTER_ID_ADD_FILTER; // map to command id
	filter.filter.addition_id = BES_FILTER_INVALID_OFFLOAD_IDX; // map to offload index
	filter.filter.state = true; // map to filter state
	filter.filter.param_len = rxf_item_oft + item->len; // map to filter match length

	// send add filter request to device
	rwnx_send_packet_filter_request(rwnx_hw, &filter, &f_idx, sizeof(f_idx));

	return f_idx;
}

int bes_filter_add_ipv6_src_match(struct rwnx_hw *rwnx_hw, u8 vif_idx, const struct in6_addr *addr, u8 match_mode, u8 action)
{
	struct mm_set_packet_filter_req filter;
	struct bes_filter_item *item = NULL;
	u16 h_proto = cpu_to_be16(ETH_P_IPV6); /* IPv6 over bluebook */
	s8 f_idx = 0;
	u8 rxf_item_oft = 0;
	u16 item_len = 0;

	// set rxf item to match ipv6 packet
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[0];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = offsetof(struct ethhdr, h_proto);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &h_proto, 2); // set pattern

	// set rxf item to match ipv6 source address
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (16 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = action;
	item->len = item_len;
	item->match_mode = match_mode;
	item->offset = sizeof(struct ethhdr) + offsetof(struct ipv6hdr, saddr);
	item->pattern_len = 16;
	memset(&item->content[0], 0xff, 16); // set mask
	memcpy(&item->content[16], addr, 16); // set pattern

	// set filter header
	filter.vif_rxf_idx = vif_idx;
	filter.filter.id = FILTER_ID_ADD_FILTER; // map to command id
	filter.filter.addition_id = BES_FILTER_INVALID_OFFLOAD_IDX; // map to offload index
	filter.filter.state = true; // map to filter state
	filter.filter.param_len = rxf_item_oft + item->len; // map to filter match length

	// send add filter request to device
	rwnx_send_packet_filter_request(rwnx_hw, &filter, &f_idx, sizeof(f_idx));

	return f_idx;
}

int bes_filter_add_ipv6_dst_match(struct rwnx_hw *rwnx_hw, u8 vif_idx, const struct in6_addr *addr, u8 match_mode, u8 action)
{
	struct mm_set_packet_filter_req filter;
	struct bes_filter_item *item = NULL;
	u16 h_proto = cpu_to_be16(ETH_P_IPV6); /* IPv6 over bluebook */
	s8 f_idx = 0;
	u8 rxf_item_oft = 0;
	u16 item_len = 0;

	// set rxf item to match ipv6 packet
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[0];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = offsetof(struct ethhdr, h_proto);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &h_proto, 2); // set pattern

	// set rxf item to match ipv6 source address
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (16 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = action;
	item->len = item_len;
	item->match_mode = match_mode;
	item->offset = sizeof(struct ethhdr) + offsetof(struct ipv6hdr, daddr);
	item->pattern_len = 16;
	memset(&item->content[0], 0xff, 16); // set mask
	memcpy(&item->content[16], addr, 16); // set pattern

	// set filter header
	filter.vif_rxf_idx = vif_idx;
	filter.filter.id = FILTER_ID_ADD_FILTER; // map to command id
	filter.filter.addition_id = BES_FILTER_INVALID_OFFLOAD_IDX; // map to offload index
	filter.filter.state = true; // map to filter state
	filter.filter.param_len = rxf_item_oft + item->len; // map to filter match length

	// send add filter request to device
	rwnx_send_packet_filter_request(rwnx_hw, &filter, &f_idx, sizeof(f_idx));

	return f_idx;
}

int bes_filter_add_ipv6_ns_match(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 offload_idx)
{
	struct mm_set_packet_filter_req filter;
	struct bes_filter_item *item = NULL;
	u16 h_proto = cpu_to_be16(ETH_P_IPV6); /* IPv6 over bluebook */
	u8 nexthdr = IPPROTO_ICMPV6; /* ICMPv6 */
	u8 icmp6_type = 135; /* ICMPV6_NEIGHBOR_SOLICITATIO */
	s8 f_idx = 0;
	u8 rxf_item_oft = 0;
	u16 item_len = 0;

	// set rxf item to match ipv6 packet
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[0];
	item->action = RXF_ACCEPT;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = offsetof(struct ethhdr, h_proto);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &h_proto, 2); // set pattern

	// set rxf item to match icmpv6
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (1 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = sizeof(struct ethhdr) + offsetof(struct ipv6hdr, nexthdr);
	item->pattern_len = 1;
	memset(&item->content[0], 0xff, 1); // set mask
	memcpy(&item->content[1], &nexthdr, 1); // set pattern

	// set rxf item to match icmpv6 neighbor solicitation
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (1 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = sizeof(struct ethhdr) + sizeof(struct ipv6hdr) + offsetof(struct icmp6hdr, icmp6_type);
	item->pattern_len = 1;
	memset(&item->content[0], 0xff, 1); // set mask
	memcpy(&item->content[1], &icmp6_type, 1); // set pattern

	// set filter header
	filter.vif_rxf_idx = vif_idx;
	filter.filter.id = FILTER_ID_ADD_FILTER;// map to command id
	filter.filter.addition_id = BES_FILTER_INVALID_OFFLOAD_IDX & offload_idx;// map to offload index
	filter.filter.state = true;// map to filter state
	filter.filter.param_len = rxf_item_oft + item->len;// map to filter match length

	// send add filter request to device
	rwnx_send_packet_filter_request(rwnx_hw, &filter, &f_idx, sizeof(f_idx));

	return f_idx;
}

int bes_filter_add_ipv6_udp_src_and_dst_match_single(struct rwnx_hw *rwnx_hw, u8 vif_idx,
	u16 s_port, u16 d_port, u8 action)
{
	struct mm_set_packet_filter_req filter;
	struct bes_filter_item *item = NULL;
	u16 h_proto = cpu_to_be16(ETH_P_IPV6); /* IPv6 over bluebook */
	u8 nexthdr = IP_PROT_UDP; /* UDP */
	s8 f_idx = 0;
	u8 rxf_item_oft = 0;
	u16 item_len = 0;

	s_port = cpu_to_be16(s_port);
	d_port = cpu_to_be16(d_port);

	// set rxf item to match ipv6 packet
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[0];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = offsetof(struct ethhdr, h_proto);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &h_proto, 2); // set pattern

	// set rxf item to match UDP
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (1 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = sizeof(struct ethhdr) + offsetof(struct ipv6hdr, nexthdr);
	item->pattern_len = 1;
	memset(&item->content[0], 0xff, 1); // set mask
	memcpy(&item->content[1], &nexthdr, 1); // set pattern

	// set rxf item to match udp src port
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = RXF_CONTINUE;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = sizeof(struct ethhdr) + sizeof(struct ipv6hdr) + offsetof(struct udphdr, source);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &s_port, 2); // set pattern

	// set rxf item to match udp dst port
	rxf_item_oft += item->len;
	item_len = bes_filter_item_size + (2 << 1);
	if (rxf_item_oft + item_len > BES_FILTER_PARAM_MAX_LEN)
		return -ENOMEM;
	item = (struct bes_filter_item *)&filter.filter_param[rxf_item_oft];
	item->action = action;
	item->len = item_len;
	item->match_mode = RXF_MATCH_EQUAL;
	item->offset = sizeof(struct ethhdr) + sizeof(struct ipv6hdr) + offsetof(struct udphdr, dest);
	item->pattern_len = 2;
	memset(&item->content[0], 0xff, 2); // set mask
	memcpy(&item->content[2], &d_port, 2); // set pattern

	// set filter header
	filter.vif_rxf_idx = vif_idx;
	filter.filter.id = FILTER_ID_ADD_FILTER;// map to command id
	filter.filter.addition_id = BES_FILTER_INVALID_OFFLOAD_IDX;// map to offload index
	filter.filter.state = true;// map to filter state
	filter.filter.param_len = rxf_item_oft + item->len;// map to filter match length

	// send add filter request to device
	rwnx_send_packet_filter_request(rwnx_hw, &filter, &f_idx, sizeof(f_idx));

	return f_idx;
}

int bes_packet_filter_ctrl(struct rwnx_hw *rwnx_hw, u8 vif_idx, bool enable)
{
	struct mm_set_packet_filter_req f_ctl = {0};

	f_ctl.vif_rxf_idx = vif_idx;
	if (enable)
		f_ctl.filter.id = FILTER_ID_ENABLE_ALL;
	else
		f_ctl.filter.id = FILTER_ID_DISABLE_ALL;

	return rwnx_send_packet_filter_request(rwnx_hw, &f_ctl, NULL, 0);
}

int bes_packet_filter_set_arp_offload(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif,
				      const __be32 ifa_local_ip)
{
	struct mm_set_packet_filter_req arp_offload = {0};
	struct arp_ip_addr_tag *ip_addr = NULL;

	arp_offload.vif_rxf_idx = vif->vif_index;
	arp_offload.filter.id = FILTER_ID_ARP_NS_OFFLOAD;
	arp_offload.filter.state = true;
	arp_offload.filter.param_len = FILTER_PACKET_TYPE_ALL_VALID;
	arp_offload.filter.addition_id = FILTER_CONDITION_MIB_ARP;

	ip_addr = (struct arp_ip_addr_tag *)&arp_offload.filter_param[0];
	ip_addr->enable = ARP_FLAG_REPLY | ARP_FLAG_FILTER;
	memcpy(ip_addr->ipv4_addr, &ifa_local_ip, 4);

	return rwnx_send_packet_filter_request(rwnx_hw, &arp_offload, NULL, 0);
}

int bes_packet_filter_clear_arp_offload(struct rwnx_hw *rwnx_hw, const u8 vif_idx)
{
	struct mm_set_packet_filter_req arp_offload = {0};

	arp_offload.vif_rxf_idx = vif_idx;
	arp_offload.filter.id = FILTER_ID_ARP_NS_OFFLOAD;
	arp_offload.filter.state = false;
	arp_offload.filter.param_len = FILTER_PACKET_TYPE_ALL_VALID;
	arp_offload.filter.addition_id = FILTER_CONDITION_MIB_ARP;

	return rwnx_send_packet_filter_request(rwnx_hw, &arp_offload, NULL, 0);
}

int bes_packet_filter_set_ns_offload(struct rwnx_hw *rwnx_hw, const struct rwnx_vif *vif,
				     const struct in6_addr *addr)
{
	struct mm_set_packet_filter_req ns_offload = {0};
	struct ns_ip_addr_tag *ip_addr;

	ns_offload.vif_rxf_idx = vif->vif_index;
	ns_offload.filter.id = FILTER_ID_ARP_NS_OFFLOAD;
	ns_offload.filter.state = true;
	ns_offload.filter.param_len = FILTER_PACKET_TYPE_ALL_VALID;
	ns_offload.filter.addition_id = FILTER_CONDITION_MIB_NS;

	ip_addr = (struct ns_ip_addr_tag *)&ns_offload.filter_param[0];
	ip_addr->enable = NS_FLAG_REPLY | NS_FLAG_FILTER;
	memcpy(ip_addr->ipv6_addr, addr, 16);

	return rwnx_send_packet_filter_request(rwnx_hw, &ns_offload, NULL, 0);
}

int bes_packet_filter_clear_ns_offload(struct rwnx_hw *rwnx_hw, const u8 vif_idx)
{
	struct mm_set_packet_filter_req ns_offload = {0};

	ns_offload.vif_rxf_idx = vif_idx;
	ns_offload.filter.id = FILTER_ID_ARP_NS_OFFLOAD;
	ns_offload.filter.state = false;
	ns_offload.filter.param_len = FILTER_PACKET_TYPE_ALL_VALID;
	ns_offload.filter.addition_id = FILTER_CONDITION_MIB_NS;

	return rwnx_send_packet_filter_request(rwnx_hw, &ns_offload, NULL, 0);
}

int bes_filter_reset(struct rwnx_hw *rwnx_hw)
{
	struct mm_set_packet_filter_req reset = {0};

	reset.filter.id = FILTER_ID_FILTER_RESET;

	return rwnx_send_packet_filter_request(rwnx_hw, &reset, NULL, 0);
}

int bes_filter_set_offload(struct rwnx_hw *rwnx_hw, u8 f_idx, u8 of_idx)
{
	struct mm_set_packet_filter_req offload = {0};

	offload.vif_rxf_idx = f_idx; // map to filter index
	offload.filter.id = FILTER_ID_BIND_OFFLOAD;
	offload.filter.addition_id = of_idx;

	return rwnx_send_packet_filter_request(rwnx_hw, &offload, NULL, 0);
}

int bes_filter_switch(struct rwnx_hw *rwnx_hw, u8 f_idx, bool enable)
{
	struct mm_set_packet_filter_req switch_ctl = {0};

	switch_ctl.vif_rxf_idx = f_idx; // map to filter index
	switch_ctl.filter.id = FILTER_ID_SWITCH_FILTER;
	switch_ctl.filter.state = enable;

	return rwnx_send_packet_filter_request(rwnx_hw, &switch_ctl, NULL, 0);
}

int bes_filter_set_mismatch_action(struct rwnx_hw *rwnx_hw, eFilterAction action)
{
	struct mm_set_packet_filter_req mismatch_act = {0};

	mismatch_act.filter.id = FILTER_ID_MISMATCH_ACTION;
	mismatch_act.filter.addition_id = action;

	return rwnx_send_packet_filter_request(rwnx_hw, &mismatch_act, NULL, 0);
}

int bes_filter_get_config_param(struct rwnx_hw *rwnx_hw,
	u8 *max_rxf_idx, u16 *max_rxf_arry_idx, u8 *mismatch_action)
{
	struct mm_set_packet_filter_req param = {0};
	u8 cfm[4];
	int ret;

	param.filter.id = FILTER_ID_GET_CONFIG;
	ret = rwnx_send_packet_filter_request(rwnx_hw, &param, cfm, sizeof(cfm));

	if (!ret) {
		*max_rxf_idx = cfm[0];
		*max_rxf_arry_idx = (cfm[2] << 8) | cfm[1];
		*mismatch_action = cfm[3];
	}

	return ret;
}

int bes_filter_get_hw_filter(struct rwnx_hw *rwnx_hw, struct mm_hw_filter *hw_filter)
{
	int ret = 0;
	struct hw_filter_get_req req;

	if (!hw_filter) {
		return -EINVAL;
	}

	// set hdr
	req.hdr.id = CUSTOM_ID_HW_FILTER;
	req.hdr.length = sizeof(struct hw_filter_get_req);
	req.hdr.ops = HW_FILTER_OPS_GET;

	// send custom request to device
	ret = rwnx_send_custom_request(rwnx_hw, &req.hdr, hw_filter, sizeof(*hw_filter));

	bes_err_with_cond(ret, BES_DBG_MSG_TX, "%s get hw filter failed(%d)\n", __func__, ret);

	return ret;
}

int bes_filter_set_hw_filter(struct rwnx_hw *rwnx_hw, struct mm_hw_filter *hw_filter)
{
	struct hw_filter_set_req req;

	if (!hw_filter) {
		return -EINVAL;
	}

	// set hdr
	req.hdr.id = CUSTOM_ID_HW_FILTER;
	req.hdr.length = sizeof(struct hw_filter_set_req);
	req.hdr.ops = HW_FILTER_OPS_SET;

	// set param
	memcpy(&req.filter, hw_filter, sizeof(struct mm_hw_filter));

	// send custom request to device
	return rwnx_send_custom_request(rwnx_hw, &req.hdr, NULL, 0);
}

int bes_filter_set_beacon_filter(struct rwnx_hw *rwnx_hw, bool valid, u8 vif_idx,
				 const u8 *elt_ids, const u32 *velt_ids, const u8 *elt_ext_ids)
{
	u8 cnt = 0;
	struct beacon_filter_set_req req;

	// set hdr
	req.hdr.id = CUSTOM_ID_MGMT_FILTER;
	req.hdr.length = sizeof(struct beacon_filter_set_req);
	req.hdr.ops = BEACON_FILTER_OPS_SET;

	// set general param
	req.filter.valid = valid;
	req.filter.vif_idx = vif_idx;
	req.filter.elt_num = 0;
	req.filter.velt_num = 0;
	req.filter.elt_ext_num = 0;

	// set elt_ids param
	if (elt_ids) {
		for (cnt = 0; cnt <= RXF_BEACON_FILTER_SIZE; cnt++) {
			if (elt_ids[cnt] == 0xff) {
				break;
			}
		}
		if (elt_ids[cnt] != 0xff) {
			return -EINVAL; // Not end with 0xff
		} else if (cnt > RXF_BEACON_FILTER_SIZE) {
			return -ERANGE; // Out of range
		}
		req.filter.elt_num = cnt;
		if (req.filter.elt_num != 0) {
			memcpy(&req.filter.elt_ids[0], elt_ids, req.filter.elt_num);
		}
	}

	// set velt_ids param
	if (velt_ids) {
		for (cnt = 0; req.filter.elt_num + cnt * 4 <= RXF_BEACON_FILTER_SIZE; cnt++) {
			if (velt_ids[cnt] == 0xffffffff) {
				break;
			}
		}
		if (velt_ids[cnt] != 0xffffffff) {
			return -EINVAL; // Not end with 0xff
		} else if (req.filter.elt_num + cnt * 4 > RXF_BEACON_FILTER_SIZE) {
			return -ERANGE; // Out of range
		}
		req.filter.velt_num = cnt;
		if (req.filter.velt_num != 0) {
			if (req.filter.elt_num == 0) {
				memcpy(&req.filter.elt_ids[0], velt_ids, req.filter.velt_num * 4);
			} else {
				memcpy(&req.filter.elt_ids[req.filter.elt_num],
					velt_ids, req.filter.velt_num * 4);
			}
		}
	}

	// set elt_ext_ids param
	if (elt_ext_ids) {
		for (cnt = 0;
		     req.filter.elt_num + req.filter.velt_num * 4 + cnt <= RXF_BEACON_FILTER_SIZE;
		     cnt++) {
			if (elt_ext_ids[cnt] == 0xff) {
				break;
			}
		}
		if (elt_ext_ids[cnt] != 0xff) {
			return -EINVAL; // Not end with 0xff
		} else if (req.filter.elt_num + req.filter.velt_num * 4 + cnt > RXF_BEACON_FILTER_SIZE) {
			return -ERANGE; // Out of range
		}
		req.filter.elt_ext_num = cnt;
		if (req.filter.elt_ext_num != 0) {
			if (req.filter.elt_num + req.filter.velt_num == 0) {
				memcpy(&req.filter.elt_ids[0], elt_ext_ids, req.filter.elt_ext_num);
			} else {
				memcpy(&req.filter.elt_ids[req.filter.elt_num + req.filter.velt_num * 4],
					elt_ext_ids, req.filter.elt_ext_num);
			}
		}
	}

	// send custom request to device
	return rwnx_send_custom_request(rwnx_hw, &req.hdr, NULL, 0);
}

int bes_filter_switch_beacon_filter(struct rwnx_hw *rwnx_hw, bool valid)
{
	struct beacon_filter_switch_req req;

	// set hdr
	req.hdr.id = CUSTOM_ID_MGMT_FILTER;
	req.hdr.length = sizeof(struct beacon_filter_switch_req);
	req.hdr.ops = BEACON_FILTER_OPS_SWITCH;

	// set param
	req.valid = valid;

	// send custom request to device
	return rwnx_send_custom_request(rwnx_hw, &req.hdr, NULL, 0);
}

int bes_filter_unmatch_dst_mgmt_set(struct rwnx_hw *rwnx_hw, bool enable)
{
	struct unmatch_dst_mgmt_filter_switch_req req;
	struct mm_custom_general_status_cfm cfm;
	int ret;

	req.hdr.id = CUSTOM_ID_MGMT_FILTER;
	req.hdr.length = sizeof(struct unmatch_dst_mgmt_filter_switch_req);
	req.hdr.ops = UNMATCH_DST_MGMT_FILTER_SWITCH;
	req.filter_en = enable ? 1U : 0;
	memset(&cfm, 0, sizeof(struct mm_custom_general_status_cfm));

	ret = rwnx_send_custom_request(rwnx_hw, &req.hdr, &cfm, sizeof(cfm));
	if (ret || cfm.status) {
		bes_err(BES_DBG_MSG_TX, "%s failed(%d, %d)\n",
			__func__, ret, cfm.status);
		return ret;
	}
	return 0;
}
