/*
 * @file bes_dbg.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/icmpv6.h>
#include <net/ipv6.h>
#include <net/ndisc.h>
#include <linux/udp.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/if_arp.h>
#include "bes_log.h"
#include "bes_eapol.h"
#include "bes_dbg.h"
#include "bes_gtk_offload.h"
#ifdef CONFIG_OPLUS_PLATFORM
#include "oplus_custom_wlan.h"
#endif

#define BES_DBG_KEY_FRAME_MAX 200
#define BES_DBG_KEY_FRAME_DATA_MAC_MAX 25
#define BES_DBG_KEY_FRAME_MGMT_MAC_MAX 60

#define bes_dbg_rate_limit_print(module, ...)					\
	do {									\
		static DEFINE_RATELIMIT_STATE(ratelimit_state,			\
					      BES_DBG_RATELIMIT_INTERVAL,	\
					      BES_DBG_RATELIMIT_BURST);		\
		if (!__ratelimit(&ratelimit_state))				\
			return;							\
		bes_dbg(module, __VA_ARGS__);				\
	} while(0)

static char *bes_dbg_get_mgmt_str(struct ieee80211_mgmt *mgmt)
{
	if (ieee80211_is_auth(mgmt->frame_control))
		return "Authentication";
	else if (ieee80211_is_deauth(mgmt->frame_control))
		return "Deauthentication";
	else if (ieee80211_is_assoc_req(mgmt->frame_control))
		return "Association Request";
	else if (ieee80211_is_assoc_resp(mgmt->frame_control))
		return "Association Response";
	else if (ieee80211_is_reassoc_req(mgmt->frame_control))
		return "Reassociation Request";
	else if (ieee80211_is_reassoc_resp(mgmt->frame_control))
		return "Reassociation Response";
	else if (ieee80211_is_disassoc(mgmt->frame_control))
		return "Disassociation";
	else if (ieee80211_is_action(mgmt->frame_control)) {
		if (mgmt->u.action.category == WLAN_CATEGORY_PUBLIC) {
			/* Category Code, Action Code, OUI, Subtype. Total 6 bytes*/
			u8 *p2p_oui_subtype = (u8 *)mgmt + sizeof(struct ieee80211_hdr_3addr) + 6;

			if (*p2p_oui_subtype == P2P_GO_NEGO_REQ)
				return "GO Negotiation Request";
			else if (*p2p_oui_subtype == P2P_GO_NEGO_RESP)
				return "GO Negotiation Response";
			else if (*p2p_oui_subtype == P2P_GO_NEGO_CONF)
				return "GO Negotiation Confirmation";
			else if (*p2p_oui_subtype == P2P_INVIT_REQ)
				return "GO Invitation Request";
			else if (*p2p_oui_subtype == P2P_INVIT_RESP)
				return "GO Invitation Response";
			else if (*p2p_oui_subtype == P2P_DEVDISC_REQ)
				return "Device Discoverability Request";
			else if (*p2p_oui_subtype == P2P_DEVDISC_RESP)
				return "Device Discoverability Response";
			else if (*p2p_oui_subtype == P2P_PROVISION_DISC_REQ)
				return "Provision Discovery Request";
			else if (*p2p_oui_subtype == P2P_PROVISION_DISC_RESP)
				return "Provision Discovery Response";
		}
	}

	return NULL;
}

static char *bes_dbg_get_icmp_type_str(u8 icmp_type)
{
	if (icmp_type == ICMP_ECHOREPLY) {
		return "ICMP EchoReply";
	} else if (icmp_type == ICMP_DEST_UNREACH) {
		return "ICMP Dest Unreach";
	} else if (icmp_type == ICMP_SOURCE_QUENCH) {
		return "ICMP Source Quench";
	} else if (icmp_type == ICMP_REDIRECT) {
		return "ICMP Redirect";
	} else if (icmp_type == ICMP_ECHO) {
		return "ICMP Echo";
	}

	return "ICMP Unknown";
}

static char *bes_dbg_get_icmp6_type_str(u8 icmp6_type)
{
	char *str;
	switch (icmp6_type) {
		case ICMPV6_ECHO_REQUEST:
			str = "ICMPv6 Echo";			break;
		case ICMPV6_ECHO_REPLY:
			str = "ICMPv6 Echo Reply";		break;
		case ICMPV6_DEST_UNREACH:
			str = "ICMPv6 Dest Unreach";		break;
		case ICMPV6_MGM_QUERY:
			str = "ICMPv6 Group Memb Queries";	break;
		case ICMPV6_MGM_REPORT:
			str = "ICMPv6 Group Memb Responses";	break;
		case ICMPV6_MGM_REDUCTION:
			str = "ICMPv6 Group Memb Reductions";	break;
		case NDISC_NEIGHBOUR_SOLICITATION:
			str = "ICMPv6 NS";			break;
		case NDISC_NEIGHBOUR_ADVERTISEMENT:
			str = "ICMPv6 NA";			break;
		case NDISC_ROUTER_SOLICITATION:
			str = "ICMPv6 RS";			break;
		case NDISC_ROUTER_ADVERTISEMENT:
			str = "ICMPv6 RA";			break;
		case NDISC_REDIRECT:
			str = "ICMPv6 Redirect";		break;
		default:
			str = "ICMPv6 Unknown";			break;
	}
	return str;
}

static int bes_dbg_get_icmp6_ns_na(struct sk_buff *skb, u8 tx_rx_status,
				   char *buf, int bufsz)
{
	int len = 0, offset = 0;
	struct in6_addr target_addr;
	struct in6_addr *target_addr_ptr;
	struct icmpv6_option_lla option_lla;
	struct icmpv6_option_lla *option_lla_ptr;
	struct bes_sk_param bes_sk_param;

	/* Parse ns/na target address */
	bes_sk_param.param_ptr = (void *)&target_addr;
	bes_sk_param.offset = sizeof(struct ethhdr) + sizeof(struct ipv6hdr) + sizeof(struct icmp6hdr);
	bes_sk_param.size = sizeof(struct in6_addr);
	target_addr_ptr = bes_common_get_param_ptr(skb, &bes_sk_param, tx_rx_status);
	if (!target_addr_ptr)
		return 0;

	len += scnprintf(&buf[len], bufsz - len - 1,
			 " Target addr:%pI6c", target_addr_ptr);

	/* Verify link-layer address is present */
	offset = sizeof(struct ethhdr) + sizeof(struct ipv6hdr) + sizeof(struct icmp6hdr) +
		 sizeof(struct in6_addr) + sizeof(struct icmpv6_option_lla);
	if (tx_rx_status == FRAME_STATUS_TX || tx_rx_status == FRAME_STATUS_TX_CFM)
		offset += RWNX_TX_HEADROOM;
	if (offset > skb->len)
		return len;

	/* Parse ns/na link-layer address */
	bes_sk_param.param_ptr = (void *)&option_lla;
	bes_sk_param.offset = sizeof(struct ethhdr) + sizeof(struct ipv6hdr) +
			sizeof(struct icmp6hdr) + sizeof(struct in6_addr);
	bes_sk_param.size = sizeof(struct icmpv6_option_lla);
	option_lla_ptr = bes_common_get_param_ptr(skb, &bes_sk_param, tx_rx_status);
	if (!option_lla_ptr)
		return len;

	if (option_lla_ptr->len != 1 ||
	    (option_lla_ptr->type != ND_OPT_SOURCE_LL_ADDR &&
	     option_lla_ptr->type != ND_OPT_TARGET_LL_ADDR))
		return len;

#ifdef CONFIG_OPLUS_PLATFORM
	len += scnprintf(&buf[len], bufsz - len - 1,
			 " Link-layer address:"OPLUSMACDBG"", OPLUSMAC2STRDBG(option_lla_ptr->addr));
#else
	len += scnprintf(&buf[len], bufsz - len - 1,
			 " Link-layer address:%pM", option_lla_ptr->addr);
#endif

	return len;
}

static char *bes_dbg_get_dhcp_type_str(u8 dhcp_type)
{
	if (dhcp_type == DHCPDISCOVER) {
		return "DHCP Discover";
	} else if (dhcp_type == DHCPOFFER) {
		return "DHCP Offer";
	} else if (dhcp_type == DHCPREQUEST) {
		return "DHCP Request";
	} else if (dhcp_type == DHCPACK) {
		return "DHCP Ack";
	} else if (dhcp_type == DHCPNAK) {
		return "DHCP Nak";
	} else if (dhcp_type == DHCPRELEASE) {
		return "DHCP Release";
	}

	return "DHCP Unknown";
}

static char *bes_dbg_get_arp_type_str(u16 arpop_type)
{
	if (cpu_to_be16(arpop_type) == ARPOP_REQUEST) {
		return "ARP request";
	} else if (cpu_to_be16(arpop_type) == ARPOP_REPLY) {
		return "ARP reply";
	} else if (cpu_to_be16(arpop_type) == ARPOP_RREQUEST) {
		return "RARP request";
	} else if (cpu_to_be16(arpop_type) == ARPOP_RREPLY) {
		return "RARP reply";
	} else if (cpu_to_be16(arpop_type) == ARPOP_InREQUEST) {
		return "InARP request";
	} else if (cpu_to_be16(arpop_type) == ARPOP_InREPLY) {
		return "InARP reply";
	} else if (cpu_to_be16(arpop_type) == ARPOP_NAK) {
		return "(ATM)ARP NAK";
	}

	return "ARPOP Unknown";
}

static char *bes_dbg_get_tx_rx_status_str(u8 tx_rx_status)
{
	if (tx_rx_status == FRAME_STATUS_TX) {
		return "Tx";
	} else if (tx_rx_status == FRAME_STATUS_TX_CFM) {
		return "Tx cfm for";
	} else if (tx_rx_status == FRAME_STATUS_RX) {
		return "Rx";
	}

	return "Tx/Rx Unknown";
}

static char *bes_dbg_get_tx_rx_action_str(u8 tx_rx_status)
{
	if (tx_rx_status == FRAME_STATUS_TX || tx_rx_status == FRAME_STATUS_TX_CFM)
		return "to";
	else if (tx_rx_status == FRAME_STATUS_RX)
		return "from";

	return "Tx/Rx Action Unknown";
}

static void bes_dbg_get_mgmt_mac_addr_str(struct ieee80211_mgmt *mgmt, u8 tx_rx_status, char *mac_addr)
{
	if (tx_rx_status == FRAME_STATUS_TX || tx_rx_status == FRAME_STATUS_TX_CFM) {
#ifdef CONFIG_OPLUS_PLATFORM
		snprintf(mac_addr, BES_DBG_KEY_FRAME_MGMT_MAC_MAX - 1,
			"Ra:["OPLUSMACDBG"] Bssid:["OPLUSMACDBG"]", OPLUSMAC2STRDBG(mgmt->da),OPLUSMAC2STRDBG(mgmt->bssid));
#else
		snprintf(mac_addr, BES_DBG_KEY_FRAME_MGMT_MAC_MAX - 1,
			"Ra:[%pM] Bssid:[%pM]", mgmt->da, mgmt->bssid);
#endif
	} else if (tx_rx_status == FRAME_STATUS_RX) {
#ifdef CONFIG_OPLUS_PLATFORM
		snprintf(mac_addr, BES_DBG_KEY_FRAME_MGMT_MAC_MAX - 1,
			"Ta:["OPLUSMACDBG"] Bssid:["OPLUSMACDBG"]", OPLUSMAC2STRDBG(mgmt->sa), OPLUSMAC2STRDBG(mgmt->bssid));
#else
		snprintf(mac_addr, BES_DBG_KEY_FRAME_MGMT_MAC_MAX - 1,
			"Ta:[%pM] Bssid:[%pM]", mgmt->sa, mgmt->bssid);
#endif
	}
}

static int bes_dbg_get_data_mac_addr_str(struct sk_buff *skb, u8 tx_rx_status, char *mac_addr)
{
	struct ethhdr ethhdr;
	struct ethhdr *ethhdr_ptr = NULL;
	struct bes_sk_param bes_sk_param;

	/* Parse mgmt ethhdr */
	bes_sk_param.param_ptr = (void *)&ethhdr;
	bes_sk_param.offset = 0;
	bes_sk_param.size = sizeof(struct ethhdr);
	ethhdr_ptr = (struct ethhdr *)bes_common_get_param_ptr(skb, &bes_sk_param, tx_rx_status);
	if (!ethhdr_ptr)
		return -EFAULT;

	if (tx_rx_status == FRAME_STATUS_TX || tx_rx_status == FRAME_STATUS_TX_CFM) {
		struct rwnx_sw_txhdr *sw_txhdr = ((struct rwnx_txhdr *)skb->data)->sw_hdr;
		struct hostdesc *host = &sw_txhdr->desc.api.host;
#ifdef CONFIG_RWNX_FULLMAC
#ifdef CONFIG_OPLUS_PLATFORM
		snprintf(mac_addr, BES_DBG_KEY_FRAME_DATA_MAC_MAX - 1,
			"Da:["OPLUSMACDBG"]", OPLUSMAC2STRDBG(host->eth_dest_addr.array));
#else
		snprintf(mac_addr, BES_DBG_KEY_FRAME_DATA_MAC_MAX - 1,
			"Da:[%pM]", host->eth_dest_addr.array);
#endif
#endif
	} else if (tx_rx_status == FRAME_STATUS_RX) {
#ifdef CONFIG_OPLUS_PLATFORM
		snprintf(mac_addr, BES_DBG_KEY_FRAME_DATA_MAC_MAX - 1,
			"Sa:["OPLUSMACDBG"]", OPLUSMAC2STRDBG(ethhdr_ptr->h_source));
#else
		snprintf(mac_addr, BES_DBG_KEY_FRAME_DATA_MAC_MAX - 1,
			"Sa:[%pM]", ethhdr_ptr->h_source);
#endif
	}

	return 0;
}

static char *bes_dbg_get_eapol_type_str(u8 eapol_type)
{
	if (eapol_type == EAPOL_OTHER) {
		return "EAPOL Other";
	} else if (eapol_type == EAPOL_4WAY_M1) {
		return "EAPOL 4way_M1";
	} else if (eapol_type == EAPOL_4WAY_M2) {
		return "EAPOL 4way_M2";
	} else if (eapol_type == EAPOL_4WAY_M3) {
		return "EAPOL 4Way_M3";
	} else if (eapol_type == EAPOL_4WAY_M4) {
		return "EAPOL 4Way_M4";
	} else if (eapol_type == EAPOL_GROUPKEY_M1) {
		return "EAPOL GroupKey_M1";
	} else if (eapol_type == EAPOL_GROUPKEY_M2) {
		return "EAPOL GroupKey_M2";
	}

	return "EAPOL Unknown";
}

static char *bes_dbg_get_eapol_wsc_type_str(struct ieee802_1x_hdr *ieee802_1x_hdr,
					    struct sk_buff *skb, u8 tx_rx_status)
{
	struct exte_auth_protocol exte_auth_protocol;
	struct exte_auth_protocol *exte_auth_protocol_ptr = NULL;
	struct bes_sk_param bes_sk_param;

	/* Parse data exte_auth_protocol */
	bes_sk_param.param_ptr = (void *)&exte_auth_protocol;
	bes_sk_param.offset = sizeof(struct ethhdr) + sizeof(struct ieee802_1x_hdr);
	bes_sk_param.size = sizeof(struct exte_auth_protocol);
	exte_auth_protocol_ptr = (struct exte_auth_protocol *)bes_common_get_param_ptr(skb,
				 &bes_sk_param, tx_rx_status);
	if (!exte_auth_protocol_ptr)
		return "";

	if (exte_auth_protocol_ptr->code == EAP_CODE_REQUEST ||
	    exte_auth_protocol_ptr->code == EAP_CODE_RESPONSE) {
		if (exte_auth_protocol_ptr->type == EAP_TYPE_IDENTITY) {
			if (exte_auth_protocol_ptr->code == EAP_CODE_REQUEST)
				return "EAPOL Identity Request";
			else if (exte_auth_protocol_ptr->code == EAP_CODE_RESPONSE)
				return "EAPOL Identity Response";
		} else if (exte_auth_protocol_ptr->type == EAP_TYPE_EXPANDED) {
			u8 eapol_wsc_op_code;
			u8 *eapol_wsc_op_code_ptr = NULL;

			/* Vendor ID, Vendor Type, Total 6 bytes*/
			bes_sk_param.param_ptr = (void *)&eapol_wsc_op_code;
			bes_sk_param.offset = sizeof(struct ethhdr) + sizeof(struct ieee802_1x_hdr) +
						  sizeof(struct exte_auth_protocol) + 6;
			bes_sk_param.size = sizeof(u8);
			eapol_wsc_op_code_ptr = (u8 *)bes_common_get_param_ptr(skb, &bes_sk_param,
						tx_rx_status);
			if (!eapol_wsc_op_code_ptr)
				return "";

			if (*eapol_wsc_op_code_ptr == WSC_START)
				return "EAPOL WSC Start";
			else if (*eapol_wsc_op_code_ptr == WSC_MSG) {
				u8 eapol_wsc_msg_type;
				u8 *eapol_wsc_msg_type_ptr = NULL;

				/* Op-Code, Flags, Version, Data element Type,
				 Data element length, Total 11 bytes*/
				bes_sk_param.param_ptr = (void *)&eapol_wsc_msg_type;
				bes_sk_param.offset = sizeof(struct ethhdr) + sizeof(struct ieee802_1x_hdr) +
							   sizeof(struct exte_auth_protocol) + 6 + 11;
				bes_sk_param.size = sizeof(u8);
				eapol_wsc_msg_type_ptr = (u8 *)bes_common_get_param_ptr(skb, &bes_sk_param,
							 tx_rx_status);
				if (!eapol_wsc_msg_type_ptr)
					return "";

				if (*eapol_wsc_msg_type_ptr == WPS_M1)
					return "EAPOL WSC_M1";
				if (*eapol_wsc_msg_type_ptr == WPS_M2)
					return "EAPOL WSC_M2";
				if (*eapol_wsc_msg_type_ptr == WPS_M2D)
					return "EAPOL WSC_M2D";
				if (*eapol_wsc_msg_type_ptr == WPS_M3)
					return "EAPOL WSC_M3";
				if (*eapol_wsc_msg_type_ptr == WPS_M4)
					return "EAPOL WSC_M4";
				if (*eapol_wsc_msg_type_ptr == WPS_M5)
					return "EAPOL WSC_M5";
				if (*eapol_wsc_msg_type_ptr == WPS_M6)
					return "EAPOL WSC_M6";
				if (*eapol_wsc_msg_type_ptr == WPS_M7)
					return "EAPOL WSC_M7";
				if (*eapol_wsc_msg_type_ptr == WPS_M8)
					return "EAPOL WSC_M8";
			} else if (*eapol_wsc_op_code_ptr == WSC_DONE) {
				return "EAPOL WSC Done";
			}
		}
	} else if (exte_auth_protocol_ptr->code == EAP_CODE_FAILURE) {
		return "EAPOL WSC Failure";
	}

	return "EAPOL WSC Unknown";
}

static char *bes_dbg_get_eapol_key_type_str(u8 eapol_key_type)
{
	if (eapol_key_type == EAPOL_WPA2_KEY) {
		return "WPA2/WPA3";
	} else if (eapol_key_type == EAPOL_WPA_KEY) {
		return "WPA";
	}

	return "EapolKey Unknown";
}

static int bes_dbg_process_ipv4_icmp(struct rwnx_vif *rwnx_vif, struct sk_buff *skb,
				     u8 tx_rx_status, char *buf)
{
	int len = 0;
	struct iphdr iphdr;
	struct iphdr *iphdr_ptr = NULL;
	struct icmphdr icmphdr;
	struct icmphdr *icmphdr_ptr = NULL;
	struct bes_sk_param bes_sk_param;
	char mac_addr[BES_DBG_KEY_FRAME_DATA_MAC_MAX];

	/* Parse data iphdr */
	bes_sk_param.param_ptr = (void *)&iphdr;
	bes_sk_param.offset = sizeof(struct ethhdr);
	bes_sk_param.size = sizeof(struct iphdr);
	iphdr_ptr = (struct iphdr *)bes_common_get_param_ptr(skb, &bes_sk_param, tx_rx_status);
	if (!iphdr_ptr)
		return 0;

	/* Parse data icmphdr */
	bes_sk_param.param_ptr = (void *)&icmphdr;
	bes_sk_param.offset = sizeof(struct ethhdr) + sizeof(struct iphdr);
	bes_sk_param.size = sizeof(struct icmphdr);
	icmphdr_ptr = (struct icmphdr *)bes_common_get_param_ptr(skb, &bes_sk_param, tx_rx_status);
	if (!icmphdr_ptr)
		return 0;

	/* Parse data da/sa mac addr */
	if (bes_dbg_get_data_mac_addr_str(skb, tx_rx_status, mac_addr))
		return 0;

	/* Show the types of ICMP */
	len += scnprintf(&buf[len], BES_DBG_KEY_FRAME_MAX - len - 1,
			 "%s: [%s %s %s %s] %pI4 to %pI4",
			 rwnx_vif->ndev->name,
			 bes_dbg_get_tx_rx_status_str(tx_rx_status),
			 bes_dbg_get_icmp_type_str(icmphdr_ptr->type),
			 bes_dbg_get_tx_rx_action_str(tx_rx_status),
			 mac_addr,
			 &iphdr_ptr->saddr, &iphdr_ptr->daddr);

	/* Show the identifier and sequence number. */
	if (icmphdr_ptr->type == ICMP_ECHO ||
	    icmphdr_ptr->type == ICMP_ECHOREPLY)
		len += scnprintf(&buf[len], BES_DBG_KEY_FRAME_MAX - len - 1,
				 "Id=%d Seq=%d", __be16_to_cpu(icmphdr_ptr->un.echo.id),
				 __be16_to_cpu(icmphdr_ptr->un.echo.sequence));

	return len;
}

static int bes_dbg_process_ipv6_icmp(struct rwnx_vif *rwnx_vif, struct sk_buff *skb,
				     u8 tx_rx_status, char *buf)
{
	int len = 0;
	struct ipv6hdr ip6h;
	struct ipv6hdr *ip6h_ptr;
	struct icmp6hdr icmp6hdr;
	struct icmp6hdr *icmp6hdr_ptr = NULL;
	struct bes_sk_param bes_sk_param;
	char mac_addr[BES_DBG_KEY_FRAME_DATA_MAC_MAX];

	/* Parse data ipv6hdr */
	bes_sk_param.param_ptr = (void *)&ip6h;
	bes_sk_param.offset = sizeof(struct ethhdr);
	bes_sk_param.size = sizeof(struct ipv6hdr);
	ip6h_ptr = (struct ipv6hdr *)bes_common_get_param_ptr(skb, &bes_sk_param, tx_rx_status);
	if (!ip6h_ptr)
		return 0;

	/* Parse data icmp6hdr */
	bes_sk_param.param_ptr = (void *)&icmp6hdr;
	bes_sk_param.offset = sizeof(struct ethhdr) + sizeof(struct ipv6hdr);
	bes_sk_param.size = sizeof(struct icmp6hdr);
	icmp6hdr_ptr = (struct icmp6hdr *)bes_common_get_param_ptr(skb, &bes_sk_param, tx_rx_status);
	if (!icmp6hdr_ptr)
		return 0;

	/* Parse the destination/source address in the frame. */
	if (bes_dbg_get_data_mac_addr_str(skb, tx_rx_status, mac_addr))
		return 0;

	/* Show the types of ICMPv6 */
	len += scnprintf(&buf[len], BES_DBG_KEY_FRAME_MAX - len - 1,
			 "%s: [%s %s %s %s] %pI6c to %pI6c",
			 rwnx_vif->ndev->name,
			 bes_dbg_get_tx_rx_status_str(tx_rx_status),
			 bes_dbg_get_icmp6_type_str(icmp6hdr_ptr->icmp6_type),
			 bes_dbg_get_tx_rx_action_str(tx_rx_status),
			 mac_addr,
			 &ip6h_ptr->saddr, &ip6h_ptr->daddr);

	/* Show info */
	switch (icmp6hdr_ptr->icmp6_type) {
		case ICMPV6_ECHO_REQUEST:
		case ICMPV6_ECHO_REPLY:
			len += scnprintf(&buf[len], BES_DBG_KEY_FRAME_MAX - len - 1,
					 " Id=%d Seq=%d", __be16_to_cpu(icmp6hdr_ptr->icmp6_identifier),
					 __be16_to_cpu(icmp6hdr_ptr->icmp6_sequence));
			break;

		case NDISC_NEIGHBOUR_SOLICITATION:
		case NDISC_NEIGHBOUR_ADVERTISEMENT:
			len += bes_dbg_get_icmp6_ns_na(skb, tx_rx_status, &buf[len],
						       BES_DBG_KEY_FRAME_MAX - len - 1);
			break;
		default: break;
	}

	return len;
}

static int bes_dbg_dhcp_frame(struct rwnx_vif *rwnx_vif, struct sk_buff *skb,
			      u8 tx_rx_status, char *buf)
{
	int len = 0;
	struct udphdr udphdr;
	struct udphdr *udphdr_ptr = NULL;
	struct dhcp_message dhcp_message;
	struct dhcp_message *dhcp_message_ptr = NULL;
	char mac_addr[BES_DBG_KEY_FRAME_DATA_MAC_MAX];
	struct dhcp_option *dhcp_option = NULL;
	struct bes_sk_param bes_sk_param;
	u16 offset = 0;

	/* Parse data udphdr */
	bes_sk_param.param_ptr = (void *)&udphdr;
	bes_sk_param.offset = sizeof(struct ethhdr) + sizeof(struct iphdr);
	bes_sk_param.size = sizeof(struct udphdr);
	udphdr_ptr = (struct udphdr *)bes_common_get_param_ptr(skb, &bes_sk_param, tx_rx_status);
	if (!udphdr_ptr)
		return 0;

	/* Parse data da/sa mac addr */
	if (bes_dbg_get_data_mac_addr_str(skb, tx_rx_status, mac_addr))
		return 0;

	/* Parse udphdr */
	if ((udphdr_ptr->source != __constant_htons(DHCP_CLIENT_PORT) &&
	     udphdr_ptr->source != __constant_htons(DHCP_SERVER_PORT)) ||
	    (udphdr_ptr->dest != __constant_htons(DHCP_CLIENT_PORT) &&
	     udphdr_ptr->dest != __constant_htons(DHCP_SERVER_PORT))) {
		return len;
	}

	/* Parse dhcp_message */
	bes_sk_param.param_ptr = (void *)&dhcp_message;
	bes_sk_param.offset = sizeof(struct ethhdr) + sizeof(struct iphdr) + sizeof(struct udphdr);
	bes_sk_param.size = min_t(int, sizeof(struct dhcp_message), skb->len - bes_sk_param.offset);
	dhcp_message_ptr = (struct dhcp_message *)bes_common_get_param_ptr(skb, &bes_sk_param, tx_rx_status);
	if (!dhcp_message_ptr)
		return 0;

	if (dhcp_message_ptr->cookie != __constant_htonl(DHCP_MAGIC)) {
		return len;
	}

	/* Parse dhcp_options */
	offset = sizeof(struct ethhdr) + sizeof(struct iphdr) + sizeof(struct udphdr);
	if (tx_rx_status == FRAME_STATUS_TX || tx_rx_status == FRAME_STATUS_TX_CFM)
		offset += RWNX_TX_HEADROOM;
	dhcp_option = (struct dhcp_option *)dhcp_message_ptr->options;
	while (dhcp_option->code != DHCP_OPTION_END) {
		u16 total_len = dhcp_option->length + sizeof(struct dhcp_option);

		/* Check whether the remaining length in the skb is sufficient */
		if (total_len + offset > skb->len) {
			bes_info(BES_DBG_COMMON, "%s ERR: Malformed Packet,"
				 "dhcp_options:0x%x length:%d offset:%d skb->len:%d\n",
				 __func__, dhcp_option->code, dhcp_option->length, offset, skb->len);
			return len;
		}

		if (dhcp_option->code == DHCP_OPTION_MSG_TYPE && dhcp_option->length == 1) {
			u8 dhcp_msg_type = *(u8 *)dhcp_option->value;
			len += snprintf(&buf[len], BES_DBG_KEY_FRAME_MAX - len - 1,
					"%s: [%s %s %s %s]",
					rwnx_vif->ndev->name,
					bes_dbg_get_tx_rx_status_str(tx_rx_status),
					bes_dbg_get_dhcp_type_str(dhcp_msg_type),
					bes_dbg_get_tx_rx_action_str(tx_rx_status),
					mac_addr);

			if (dhcp_msg_type == DHCPACK) {
				len += snprintf(&buf[len], BES_DBG_KEY_FRAME_MAX - len - 1,
						" Yiaddr:%pI4", &dhcp_message_ptr->yiaddr);
			}

			return len;
		}

		/* Next dhcp_option */
		dhcp_option = (struct dhcp_option *)((u8 *)dhcp_option + total_len);
		offset += total_len;
	}

	return len;
}

static int bes_dbg_process_ipv4_udp(struct rwnx_vif *rwnx_vif, struct sk_buff *skb,
				    u8 tx_rx_status, char *buf)
{
	return bes_dbg_dhcp_frame(rwnx_vif, skb, tx_rx_status, buf);
}

static int bes_dbg_process_arp(struct rwnx_vif *rwnx_vif, struct sk_buff *skb,
			       u8 tx_rx_status, char *buf)
{
	int len = 0;
	struct arphdr arphdr;
	struct arphdr *arphdr_ptr = NULL;
	struct mac_addr target_hw_mac;
	struct mac_addr *target_hw_mac_ptr = NULL;
	u32 target_inter_ip;
	u32 *target_inter_ip_ptr = NULL;
	char mac_addr[BES_DBG_KEY_FRAME_DATA_MAC_MAX];
	struct bes_sk_param bes_sk_param;

	/* Parse data arphdr */
	bes_sk_param.param_ptr = (void *)&arphdr;
	bes_sk_param.offset = sizeof(struct ethhdr);
	bes_sk_param.size = sizeof(struct arphdr);
	arphdr_ptr = (struct arphdr *)bes_common_get_param_ptr(skb, &bes_sk_param, tx_rx_status);
	if (!arphdr_ptr)
		return 0;

	/* Parse data hw_mac */
	bes_sk_param.param_ptr = (void *)&target_hw_mac;
	if (cpu_to_be16(arphdr_ptr->ar_op) == ARPOP_REPLY)
		bes_sk_param.offset = sizeof(struct ethhdr) + sizeof(struct arphdr);
	else
		bes_sk_param.offset = sizeof(struct ethhdr) + sizeof(struct arphdr) + sizeof(struct mac_addr) + sizeof(u32);
	bes_sk_param.size = sizeof(struct mac_addr);
	target_hw_mac_ptr = (struct mac_addr *)bes_common_get_param_ptr(skb, &bes_sk_param, tx_rx_status);
	if (!target_hw_mac_ptr)
		return 0;

	/* Parse data inter_ip */
	bes_sk_param.param_ptr = (void *)&target_inter_ip;
	if (cpu_to_be16(arphdr_ptr->ar_op) == ARPOP_REPLY)
		bes_sk_param.offset = sizeof(struct ethhdr) + sizeof(struct arphdr) + sizeof(struct mac_addr);
	else
		bes_sk_param.offset = sizeof(struct ethhdr) + sizeof(struct arphdr) + (sizeof(struct mac_addr) * 2) + sizeof(u32);
	bes_sk_param.size = sizeof(u32);
	target_inter_ip_ptr = (u32 *)bes_common_get_param_ptr(skb, &bes_sk_param, tx_rx_status);
	if (!target_inter_ip_ptr)
		return 0;

	/* Parse data da/sa mac addr */
	if (bes_dbg_get_data_mac_addr_str(skb, tx_rx_status, mac_addr))
		return 0;

	if (cpu_to_be16(arphdr_ptr->ar_op) == ARPOP_REQUEST) {
#ifdef CONFIG_OPLUS_PLATFORM
		len += snprintf(&buf[len], BES_DBG_KEY_FRAME_MAX - len - 1,
				 "%s: [%s %s %s %s] %pI4 = "OPLUSMACDBG"",
				 rwnx_vif->ndev->name,
				 bes_dbg_get_tx_rx_status_str(tx_rx_status),
				 bes_dbg_get_arp_type_str(arphdr_ptr->ar_op),
				 bes_dbg_get_tx_rx_action_str(tx_rx_status),
				 mac_addr,
				 target_inter_ip_ptr, OPLUSMAC2STRDBG(target_hw_mac_ptr));
#else
		len += snprintf(&buf[len], BES_DBG_KEY_FRAME_MAX - len - 1,
				 "%s: [%s %s %s %s] %pI4 = %pM",
				 rwnx_vif->ndev->name,
				 bes_dbg_get_tx_rx_status_str(tx_rx_status),
				 bes_dbg_get_arp_type_str(arphdr_ptr->ar_op),
				 bes_dbg_get_tx_rx_action_str(tx_rx_status),
				 mac_addr,
				 target_inter_ip_ptr, target_hw_mac_ptr);
#endif
	} else if (cpu_to_be16(arphdr_ptr->ar_op) == ARPOP_REPLY) {
#ifdef CONFIG_OPLUS_PLATFORM
		len += snprintf(&buf[len], BES_DBG_KEY_FRAME_MAX - len - 1,
				 "%s: [%s %s %s %s] %pI4 = "OPLUSMACDBG"",
				 rwnx_vif->ndev->name,
				 bes_dbg_get_tx_rx_status_str(tx_rx_status),
				 bes_dbg_get_arp_type_str(arphdr_ptr->ar_op),
				 bes_dbg_get_tx_rx_action_str(tx_rx_status),
				 mac_addr,
				 target_inter_ip_ptr, OPLUSMAC2STRDBG(target_hw_mac_ptr));
#else
		len += snprintf(&buf[len], BES_DBG_KEY_FRAME_MAX - len - 1,
				 "%s: [%s %s %s %s] %pI4 = %pM",
				 rwnx_vif->ndev->name,
				 bes_dbg_get_tx_rx_status_str(tx_rx_status),
				 bes_dbg_get_arp_type_str(arphdr_ptr->ar_op),
				 bes_dbg_get_tx_rx_action_str(tx_rx_status),
				 mac_addr,
				 target_inter_ip_ptr, target_hw_mac_ptr);
#endif
	} else {
		len += snprintf(&buf[len], BES_DBG_KEY_FRAME_MAX - len - 1,
				 "%s: [%s %s %s %s]",
				 rwnx_vif->ndev->name,
				 bes_dbg_get_tx_rx_status_str(tx_rx_status),
				 bes_dbg_get_arp_type_str(arphdr_ptr->ar_op),
				 bes_dbg_get_tx_rx_action_str(tx_rx_status),
				 mac_addr);
	}
	return len;
}

static int bes_dbg_process_p_pae(struct rwnx_vif *rwnx_vif,  struct sk_buff *skb,
				 u8 tx_rx_status, char *buf)
{
	int len = 0;
	struct ieee802_1x_hdr ieee802_1x_hdr;
	struct ieee802_1x_hdr *ieee802_1x_hdr_ptr = NULL;
	char mac_addr[BES_DBG_KEY_FRAME_DATA_MAC_MAX];
	struct bes_sk_param bes_sk_param;

	/* Parse data ieee802_1x_hdr */
	bes_sk_param.param_ptr = (void *)&ieee802_1x_hdr;
	bes_sk_param.offset = sizeof(struct ethhdr);
	bes_sk_param.size = sizeof(struct ieee802_1x_hdr);
	ieee802_1x_hdr_ptr = (struct ieee802_1x_hdr *)bes_common_get_param_ptr(skb,
			     &bes_sk_param, tx_rx_status);
	if (!ieee802_1x_hdr_ptr)
		return 0;

	/* Parse data da/sa mac addr */
	if (bes_dbg_get_data_mac_addr_str(skb, tx_rx_status, mac_addr))
		return 0;

	if (ieee802_1x_hdr_ptr->type == IEEE802_1X_TYPE_EAP_PACKET) {
		len += snprintf(&buf[len], BES_DBG_KEY_FRAME_MAX - len - 1,
				"%s: [%s %s %s %s] KeyType: %s",
				rwnx_vif->ndev->name,
				bes_dbg_get_tx_rx_status_str(tx_rx_status),
				bes_dbg_get_eapol_wsc_type_str(ieee802_1x_hdr_ptr, skb, tx_rx_status),
				bes_dbg_get_tx_rx_action_str(tx_rx_status),
				mac_addr,
				"WSC");
	} else if (ieee802_1x_hdr_ptr->type == IEEE802_1X_TYPE_EAPOL_KEY) {
		u8 eapol_type;
		struct eapol_hdr eapol_hdr;
		struct eapol_hdr *eapol_hdr_ptr = NULL;

		/* Parse data eapol_hdr */
		bes_sk_param.param_ptr = (void *)&eapol_hdr;
		bes_sk_param.offset = sizeof(struct ethhdr);
		bes_sk_param.size = sizeof(struct eapol_hdr);
		eapol_hdr_ptr = (struct eapol_hdr *)bes_common_get_param_ptr(skb, &bes_sk_param, tx_rx_status);
		if (!eapol_hdr_ptr)
			return 0;

		eapol_type = bes_eapol_get_pkt_type(eapol_hdr_ptr);

		len += snprintf(&buf[len], BES_DBG_KEY_FRAME_MAX - len - 1,
				"%s: [%s %s %s %s] KeyType: %s",
				rwnx_vif->ndev->name,
				bes_dbg_get_tx_rx_status_str(tx_rx_status),
				bes_dbg_get_eapol_type_str(eapol_type),
				bes_dbg_get_tx_rx_action_str(tx_rx_status),
				mac_addr,
				bes_dbg_get_eapol_key_type_str(eapol_hdr_ptr->key_type));
	}

	return len;
}

static int bes_dbg_process_ipv4(struct rwnx_vif *rwnx_vif, struct sk_buff *skb,
				u8 tx_rx_status, char *buf)
{
	int len = 0;
	struct iphdr iphdr;
	struct iphdr *iphdr_ptr = NULL;
	struct bes_sk_param bes_sk_param;

	/* Parse data iphdr */
	bes_sk_param.param_ptr = (void *)&iphdr;
	bes_sk_param.offset = sizeof(struct ethhdr);
	bes_sk_param.size = sizeof(struct iphdr);
	iphdr_ptr = (struct iphdr *)bes_common_get_param_ptr(skb, &bes_sk_param, tx_rx_status);
	if (!iphdr_ptr)
		return 0;

	switch (iphdr_ptr->protocol) {
		case IP_PROT_ICMP: /* ICMP */
			len = bes_dbg_process_ipv4_icmp(rwnx_vif, skb, tx_rx_status, buf);
			break;

		case IP_PROT_UDP: /* UDP */
			len = bes_dbg_process_ipv4_udp(rwnx_vif, skb, tx_rx_status, buf);
			break;

		default:
			break;
	}
	return len;
}

static int bes_dbg_process_ipv6(struct rwnx_vif *rwnx_vif, struct sk_buff *skb,
				u8 tx_rx_status, char *buf)
{
	int len = 0;
	struct ipv6hdr ip6h;
	struct ipv6hdr *ip6h_ptr;
	struct bes_sk_param bes_sk_param;

	/* Parse data iphdr */
	bes_sk_param.param_ptr = (void *)&ip6h;
	bes_sk_param.offset = sizeof(struct ethhdr);
	bes_sk_param.size = sizeof(struct ipv6hdr);
	ip6h_ptr = (struct ipv6hdr *)bes_common_get_param_ptr(skb, &bes_sk_param, tx_rx_status);
	if (!ip6h_ptr)
		return 0;

	switch (ip6h_ptr->nexthdr) {
		case NEXTHDR_ICMP: /* ICMP for IPv6. */
			len = bes_dbg_process_ipv6_icmp(rwnx_vif, skb, tx_rx_status, buf);
			break;
		default:
			break;
	}
	return len;
}

int bes_dbg_mgmt_process_mgmt_type(struct rwnx_vif *rwnx_vif, struct ieee80211_mgmt *mgmt, u8 tx_rx_status, char *buf)
{
	int len = 0;
	char mac_addr[BES_DBG_KEY_FRAME_MGMT_MAC_MAX];
	char *mgmt_type_str = bes_dbg_get_mgmt_str(mgmt);

	/* Parse mgmt da/sa mac addr */
	bes_dbg_get_mgmt_mac_addr_str(mgmt, tx_rx_status, mac_addr);

	if (mgmt_type_str) {
		len += snprintf(&buf[len], BES_DBG_KEY_FRAME_MAX - len - 1,
				"%s: [%s %s %s %s] KeyType: %s SN:%d",
				rwnx_vif->ndev->name,
				bes_dbg_get_tx_rx_status_str(tx_rx_status),
				mgmt_type_str,
				bes_dbg_get_tx_rx_action_str(tx_rx_status),
				mac_addr,
				"MGMT",
				IEEE80211_SEQ_TO_SN(mgmt->seq_ctrl));
	}

	return len;
}

int bes_dbg_data_process_ether_type(struct rwnx_vif *rwnx_vif, struct sk_buff *skb,
				    u8 tx_rx_status, char *buf)
{
	int len = 0;
	u16 ethertype = 0;
	struct ethhdr ethhdr;
	struct ethhdr *ethhdr_ptr = NULL;
	struct bes_sk_param bes_sk_param;
	struct rwnx_sw_txhdr *sw_txhdr = ((struct rwnx_txhdr *)skb->data)->sw_hdr;

	/* Parse mgmt ethhdr */
	bes_sk_param.param_ptr = (void *)&ethhdr;
	bes_sk_param.offset = 0;
	bes_sk_param.size = sizeof(struct ethhdr);
	ethhdr_ptr = (struct ethhdr *)bes_common_get_param_ptr(skb, &bes_sk_param, tx_rx_status);
	if (!ethhdr_ptr)
		return 0;

	if (tx_rx_status == FRAME_STATUS_TX || tx_rx_status == FRAME_STATUS_TX_CFM) {
		ethertype = __be16_to_cpu(sw_txhdr->desc.api.host.ethertype);
	} else if (tx_rx_status == FRAME_STATUS_RX) {
		ethertype = __be16_to_cpu(ethhdr_ptr->h_proto);
	}

	/* Parse frame */
	switch (ethertype) {
		case ETH_P_IP: /* Internet Protocol version 4 (IPv4) */
			len = bes_dbg_process_ipv4(rwnx_vif, skb, tx_rx_status, buf);
			break;

		case ETH_P_ARP:/* Address Resolution Protocol (ARP) */
			len = bes_dbg_process_arp(rwnx_vif, skb, tx_rx_status, buf);
			break;

		case ETH_P_PAE: /* IEEE Std 802.1X - Port-based network access control */
			len = bes_dbg_process_p_pae(rwnx_vif, skb, tx_rx_status, buf);
			break;

		case ETH_P_IPV6: /* Internet Protocol version 6 (IPv6)*/
			len = bes_dbg_process_ipv6(rwnx_vif, skb, tx_rx_status, buf);
			break;

		default:
			break;
	}

	return len;
}

void bes_dbg_key_frame_tx(struct sk_buff *skb)
{
	int len = 0;
	char buf[BES_DBG_KEY_FRAME_MAX];
	struct ieee80211_mgmt *mgmt = NULL;
	struct rwnx_sw_txhdr *sw_txhdr = ((struct rwnx_txhdr *)skb->data)->sw_hdr;
	struct txdesc_host *txdesc_host = &sw_txhdr->desc;

	/* Check dbg_lvl */
	if (bes_get_dbg_lvl(BES_DBG_KEY_FRAME) < BES_LOG_DBG ||
	    bes_get_dbg_lvl(BES_DBG_KEY_FRAME) == BES_LOG_NONE ||
	    !sw_txhdr)
		return;

	if (txdesc_host->api.host.flags & TXU_CNTRL_MGMT) {
		/* Get Mgmt Frame Parameter */
		mgmt = (struct ieee80211_mgmt *)((u8 *)skb->data + RWNX_TX_HEADROOM);
		len = bes_dbg_mgmt_process_mgmt_type(sw_txhdr->rwnx_vif, mgmt, FRAME_STATUS_TX, buf);
	} else {
		/* Get Data Frame Parameter */
		len = bes_dbg_data_process_ether_type(sw_txhdr->rwnx_vif, skb, FRAME_STATUS_TX, buf);
	}

	/* Print buf */
	if (len > 0)
		bes_dbg_rate_limit_print(BES_DBG_KEY_FRAME, "%s", buf);
}

static void bes_dbg_add_cfm_info(union rwnx_hw_txstatus tx_st, char *buf, int *len)
{
	if (*len >= BES_DBG_KEY_FRAME_MAX - 1)
		return;

	*len += snprintf(&buf[*len], BES_DBG_KEY_FRAME_MAX - *len - 1,
			 " D:%d/RR:%d/SWRR:%d/A:%d/sn:%d/rc:%d",
			 tx_st.tx_done, tx_st.retry_required,
			 tx_st.sw_retry_required, tx_st.acknowledged,
			 tx_st.sn, tx_st.retry_cnt);
}

static void bes_dbg_add_rx_rssi(s8 rssi, char *buf, int *len)
{
	if (*len >= BES_DBG_KEY_FRAME_MAX - 1)
		return;

	*len += scnprintf(&buf[*len], BES_DBG_KEY_FRAME_MAX - *len - 1,
			 " rssi:%ddBm", rssi);
}

void bes_dbg_key_frame_tx_cfm(struct sk_buff *skb, struct tx_cfm_tag *cfm)
{
	int len = 0;
	char buf[BES_DBG_KEY_FRAME_MAX];
	struct ieee80211_mgmt *mgmt = NULL;
	struct rwnx_sw_txhdr *sw_txhdr = ((struct rwnx_txhdr *)skb->data)->sw_hdr;
	struct txdesc_host *txdesc_host = &sw_txhdr->desc;

	/* Check dbg_lvl */
	if (bes_get_dbg_lvl(BES_DBG_KEY_FRAME) < BES_LOG_DBG ||
	    bes_get_dbg_lvl(BES_DBG_KEY_FRAME) == BES_LOG_NONE ||
	    !sw_txhdr || !cfm)
		return;

	if (txdesc_host->api.host.flags & TXU_CNTRL_MGMT) {
		/* Get Mgmt Frame Parameter */
		mgmt = (struct ieee80211_mgmt *)((u8 *)sw_txhdr->skb->data + RWNX_TX_HEADROOM);
		len = bes_dbg_mgmt_process_mgmt_type(sw_txhdr->rwnx_vif, mgmt, FRAME_STATUS_TX_CFM, buf);
	} else {
		/* Get Data Frame Parameter */
		len = bes_dbg_data_process_ether_type(sw_txhdr->rwnx_vif, skb, FRAME_STATUS_TX_CFM, buf);
	}

	if (len <= 0)
		return;

	/* Add cfm info */
	bes_dbg_add_cfm_info(cfm->status, buf, &len);

	/* Print buf */
	bes_dbg_rate_limit_print(BES_DBG_KEY_FRAME, "%s", buf);
}

void bes_dbg_key_frame_rx(struct rwnx_vif *rwnx_vif, bool is_80211_mpdu,
			  struct sk_buff *skb, struct rx_vector_1 *rxvect1)
{
	int len = 0;
	char buf[BES_DBG_KEY_FRAME_MAX];
	struct ieee80211_mgmt *mgmt = NULL;

	/* Check dbg_lvl */
	if (bes_get_dbg_lvl(BES_DBG_KEY_FRAME) < BES_LOG_DBG ||
	    bes_get_dbg_lvl(BES_DBG_KEY_FRAME) == BES_LOG_NONE ||
	    !skb)
		return;

	if (is_80211_mpdu) {
		/* Get Mgmt Frame Parameter */
		mgmt = (struct ieee80211_mgmt *)skb->data;
		len = bes_dbg_mgmt_process_mgmt_type(rwnx_vif, mgmt, FRAME_STATUS_RX, buf);
	} else  {
		/* Get Data Frame Parameter */
		len = bes_dbg_data_process_ether_type(rwnx_vif, skb, FRAME_STATUS_RX, buf);
	}

	if (len <= 0)
		return;

	/* Add rx rssi */
	bes_dbg_add_rx_rssi(rxvect1->rssi1, buf, &len);

	/* Print buf */
	bes_dbg_rate_limit_print(BES_DBG_KEY_FRAME, "%s", buf);
}

void bes_dbg_mgt_trace(struct rwnx_hw *rwnx_hw, struct sm_mgt_frame_ind *ind)
{
	int len = 0;
	char buf[BES_DBG_KEY_FRAME_MAX];
	struct rwnx_vif *rwnx_vif = rwnx_rx_get_vif(rwnx_hw, ind->vif_idx);
	static unsigned long caller_jiffies;

	if (!rwnx_vif) {
		bes_err(BES_DBG_KEY_FRAME, "%s ERROR: vif:%d is not find\n", __func__, ind->vif_idx);
		return;
	}

	/* Check dbg_lvl */
	if (bes_get_dbg_lvl(BES_DBG_KEY_FRAME) < BES_LOG_DBG ||
	    bes_get_dbg_lvl(BES_DBG_KEY_FRAME) == BES_LOG_NONE)
		return;

	/* Only print tx cfm info when the interval is greater than the minimum value
	   or the packet is acknowledged */
	if (!printk_timed_ratelimit(&caller_jiffies, BES_DBG_MGMT_TX_ST_PRINT_MIN_INTERVAL) &&
	    !((union rwnx_hw_txstatus)ind->tx_st).acknowledged)
		return;

	/* Get Mgmt Frame Parameter For Tx Or Rx */
	len += bes_dbg_mgmt_process_mgmt_type(rwnx_vif, (struct ieee80211_mgmt *)ind->payload,
					      ind->tx ? FRAME_STATUS_TX_CFM : FRAME_STATUS_RX, buf);

	if (len <= 0)
		return;

	if (ind->tx) {
		/* Add tx cfm info */
		bes_dbg_add_cfm_info((union rwnx_hw_txstatus)ind->tx_st, buf, &len);
	} else {
		/* Add rx rssi */
		bes_dbg_add_rx_rssi(ind->rx_rssi, buf, &len);
	}

	/* Print buf */
	bes_dbg(BES_DBG_KEY_FRAME, "%s", buf);
}
