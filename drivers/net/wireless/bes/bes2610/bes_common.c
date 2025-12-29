/*
 * @file bes_common.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/ctype.h>
#include "rwnx_defs.h"
#include "bes_log.h"
#include "bes_common.h"

int bes_common_parse_line(char *line, char *argv[], int maxargs)
{
	int nargs = 0;

	while (nargs < maxargs) {
		/* skip any white space */
		while ((*line == ' ') || (*line == '\t')) {
			++line;
		}

		if (*line == '\0') {    /* end of line, no more args    */
			argv[nargs] = 0;
			return nargs;
		}

		/* Argument include space should be bracketed by quotation mark */
		if (*line == '\"') {
			/* Skip quotation mark */
			line++;

			/* Begin of argument string */
			argv[nargs++] = line;

			/* Until end of argument */
			while (*line && (*line != '\"')) {
				++line;
			}
		} else {
			argv[nargs++] = line;    /* begin of argument string    */

			/* find end of string */
			while (*line && (*line != ' ') && (*line != '\t')) {
				++line;
			}
		}

		if (*line == '\0') {    /* end of line, no more args    */
			argv[nargs] = 0;
			return nargs;
		}

		*line++ = '\0';         /* terminate current arg     */
	}

	bes_err(BES_DBG_COMMON, "** Too many args (max. %d) **\n", maxargs);

	return nargs;
}

u32 bes_common_get_vif_idx_map(struct rwnx_hw *rwnx_hw)
{
	u32 vif_idx_map = 0;
	struct rwnx_vif *rwnx_vif = NULL;

	spin_lock_bh(&rwnx_hw->cb_lock);
	list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
		u8 vif_index = rwnx_vif->vif_index;
		if (vif_index >= NX_ITF_MAX) {
			continue;
		}

		vif_idx_map |= BIT(vif_index);
	}
	spin_unlock_bh(&rwnx_hw->cb_lock);

	return vif_idx_map;
}

int bes_common_vif_idx(struct rwnx_hw *rwnx_hw, char *name)
{
	struct rwnx_vif *rwnx_vif = NULL;
	int ret = -ENODEV;

	/* Check the validity of parameters */
	if (!name)
		return -EINVAL;

	/* Get idx form name */
	spin_lock_bh(&rwnx_hw->cb_lock);
	list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
		if (!rwnx_vif->ndev ||
		    strcmp(name, rwnx_vif->ndev->name))
			continue;

		ret = rwnx_vif->vif_index;
		break;
	}
	spin_unlock_bh(&rwnx_hw->cb_lock);

	return ret;
}

bool bes_common_parse_dhcp_ack_frame(struct sk_buff *skb, u32 *gateway_ip)
{
	struct ethhdr ethhdr;
	struct ethhdr *ethhdr_ptr = NULL;
	struct iphdr iphdr;
	struct iphdr *iphdr_ptr = NULL;
	struct udphdr udphdr;
	struct udphdr *udphdr_ptr = NULL;
	struct dhcp_message dhcp_msg;
	struct dhcp_message *dhcp_msg_ptr = NULL;
	struct bes_sk_param bes_sk_param;
	struct dhcp_option *dhcp_option = NULL;
	u16 offset = 0;

	/* Check the validity of parameters */
	if (!gateway_ip)
		return false;

	/* Parse ethhdr */
	bes_sk_param.param_ptr = (void *)&ethhdr;
	bes_sk_param.offset = 0;
	bes_sk_param.size = sizeof(struct ethhdr);
	ethhdr_ptr = (struct ethhdr *)bes_common_get_param_ptr(skb, &bes_sk_param, FRAME_STATUS_RX);

	if (!ethhdr_ptr || ethhdr_ptr->h_proto != __constant_htons(ETH_P_IP))
		return false;

	/* Parse iphdr */
	bes_sk_param.param_ptr = (void *)&iphdr;
	bes_sk_param.offset = sizeof(struct ethhdr);
	bes_sk_param.size = sizeof(struct iphdr);
	iphdr_ptr = (struct iphdr *)bes_common_get_param_ptr(skb, &bes_sk_param, FRAME_STATUS_RX);
	if (!iphdr_ptr || iphdr_ptr->protocol != IP_PROT_UDP)
		return false;

	/* Parse udphdr */
	bes_sk_param.param_ptr = (void *)&udphdr;
	bes_sk_param.offset = sizeof(struct ethhdr) + sizeof(struct iphdr);
	bes_sk_param.size = sizeof(struct udphdr);
	udphdr_ptr = (struct udphdr *)bes_common_get_param_ptr(skb, &bes_sk_param, FRAME_STATUS_RX);
	if (!udphdr_ptr ||
	    (udphdr_ptr->source != __constant_htons(DHCP_SERVER_PORT) ||
	     udphdr_ptr->dest != __constant_htons(DHCP_CLIENT_PORT)))
		return false;

	/* Parse dhcp_message */
	bes_sk_param.param_ptr = (void *)&dhcp_msg;
	bes_sk_param.offset = sizeof(struct ethhdr) + sizeof(struct iphdr) + sizeof(struct udphdr);
	bes_sk_param.size = min_t(int, sizeof(struct dhcp_message), skb->len - bes_sk_param.offset);
	dhcp_msg_ptr = (struct dhcp_message *)bes_common_get_param_ptr(skb, &bes_sk_param, FRAME_STATUS_RX);
	if (!dhcp_msg_ptr || dhcp_msg_ptr->cookie != __constant_htonl(DHCP_MAGIC))
		return false;

	/* Parse dhcp_options */
	offset = sizeof(struct ethhdr) + sizeof(struct iphdr) + sizeof(struct udphdr);
	dhcp_option = (struct dhcp_option *)dhcp_msg_ptr->options;
	while (dhcp_option->code != DHCP_OPTION_END) {
		u16 total_len = dhcp_option->length + sizeof(struct dhcp_option);

		/* Check whether the remaining length in the skb is sufficient */
		if (total_len + offset > skb->len) {
			bes_info(BES_DBG_COMMON, "%s ERR: Malformed Packet,"
				 "dhcp_option:0x%x length:%d offset:%d skb->len:%d\n",
				 __func__, dhcp_option->code, dhcp_option->length, offset, skb->len);
			return false;
		}

		/* Check frame type */
		if (dhcp_option->code == DHCP_OPTION_MSG_TYPE && dhcp_option->length == 1) {
			u8 dhcp_msg_type = *(u8 *)dhcp_option->value;
			if (dhcp_msg_type != DHCPACK)
				return false;
		}

		/* Get gateway_ip value */
		if (dhcp_option->code == DHCP_OPTION_ROUTER && dhcp_option->length == 4) {
			*gateway_ip = *(u32 *)(dhcp_option->value);
			bes_dbg(BES_DBG_COMMON, "%s gateway_ip 0x%x\n", __func__, *gateway_ip);
			return true;
		}

		/* Next dhcp_option */
		dhcp_option = (struct dhcp_option *)((u8 *)dhcp_option + total_len);
		offset += total_len;
	}

	return false;
}

static int wpa_selector_to_bitfield(const u8 *s)
{
	if (RSN_SELECTOR_GET(s) == WPA_CIPHER_SUITE_NONE)
		return WPA_CIPHER_NONE;
	if (RSN_SELECTOR_GET(s) == WPA_CIPHER_SUITE_TKIP)
		return WPA_CIPHER_TKIP;
	if (RSN_SELECTOR_GET(s) == WPA_CIPHER_SUITE_CCMP)
		return WPA_CIPHER_CCMP;

	return 0;
}

static int wpa_key_mgmt_to_bitfield(const u8 *s)
{
	if (RSN_SELECTOR_GET(s) == WPA_AUTH_KEY_MGMT_UNSPEC_802_1X)
		return WPA_KEY_MGMT_IEEE8021X;
	if (RSN_SELECTOR_GET(s) == WPA_AUTH_KEY_MGMT_PSK_OVER_802_1X)
		return WPA_KEY_MGMT_PSK;
	if (RSN_SELECTOR_GET(s) == WPA_AUTH_KEY_MGMT_NONE)
		return WPA_KEY_MGMT_WPA_NONE;

	return 0;
}

static int rsn_selector_to_bitfield(const u8 *s)
{
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_NONE)
		return WPA_CIPHER_NONE;
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_TKIP)
		return WPA_CIPHER_TKIP;
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_CCMP)
		return WPA_CIPHER_CCMP;
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_AES_128_CMAC)
		return WPA_CIPHER_AES_128_CMAC;
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_GCMP)
		return WPA_CIPHER_GCMP;
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_CCMP_256)
		return WPA_CIPHER_CCMP_256;
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_GCMP_256)
		return WPA_CIPHER_GCMP_256;
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_BIP_GMAC_128)
		return WPA_CIPHER_BIP_GMAC_128;
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_BIP_GMAC_256)
		return WPA_CIPHER_BIP_GMAC_256;
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_BIP_CMAC_256)
		return WPA_CIPHER_BIP_CMAC_256;
	if (RSN_SELECTOR_GET(s) == RSN_CIPHER_SUITE_NO_GROUP_ADDRESSED)
		return WPA_CIPHER_GTK_NOT_USED;

	return 0;
}

static int wpa_cipher_valid_pairwise(int cipher)
{
	return  cipher == WPA_CIPHER_CCMP_256 ||
		cipher == WPA_CIPHER_GCMP_256 ||
		cipher == WPA_CIPHER_CCMP ||
		cipher == WPA_CIPHER_GCMP ||
		cipher == WPA_CIPHER_TKIP;
}

static int wpa_cipher_valid_group(int cipher)
{
	return wpa_cipher_valid_pairwise(cipher) ||
		cipher == WPA_CIPHER_GTK_NOT_USED;
}

static int wpa_cipher_valid_mgmt_group(int cipher)
{
	return  cipher == WPA_CIPHER_AES_128_CMAC ||
		cipher == WPA_CIPHER_BIP_GMAC_128 ||
		cipher == WPA_CIPHER_BIP_GMAC_256 ||
		cipher == WPA_CIPHER_BIP_CMAC_256;
}

static int rsn_key_mgmt_to_bitfield(const u8 *s)
{
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_UNSPEC_802_1X)
		return WPA_KEY_MGMT_IEEE8021X;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_PSK_OVER_802_1X)
		return WPA_KEY_MGMT_PSK;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_FT_802_1X)
		return WPA_KEY_MGMT_FT_IEEE8021X;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_FT_PSK)
		return WPA_KEY_MGMT_FT_PSK;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_FT_802_1X_SHA384)
		return WPA_KEY_MGMT_FT_IEEE8021X_SHA384;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_802_1X_SHA256)
		return WPA_KEY_MGMT_IEEE8021X_SHA256;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_PSK_SHA256)
		return WPA_KEY_MGMT_PSK_SHA256;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_SAE)
		return WPA_KEY_MGMT_SAE;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_FT_SAE)
		return WPA_KEY_MGMT_FT_SAE;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_802_1X_SUITE_B)
		return WPA_KEY_MGMT_IEEE8021X_SUITE_B;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_802_1X_SUITE_B_192)
		return WPA_KEY_MGMT_IEEE8021X_SUITE_B_192;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_FILS_SHA256)
		return WPA_KEY_MGMT_FILS_SHA256;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_FILS_SHA384)
		return WPA_KEY_MGMT_FILS_SHA384;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_FT_FILS_SHA256)
		return WPA_KEY_MGMT_FT_FILS_SHA256;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_FT_FILS_SHA384)
		return WPA_KEY_MGMT_FT_FILS_SHA384;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_OWE)
		return WPA_KEY_MGMT_OWE;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_DPP)
		return WPA_KEY_MGMT_DPP;
	if (RSN_SELECTOR_GET(s) == RSN_AUTH_KEY_MGMT_OSEN)
		return WPA_KEY_MGMT_OSEN;

	return 0;
}

int wpa_parse_wpa_ie_wpa(const u8 *wpa_ie, size_t wpa_ie_len,
			 struct wpa_ie_data *data)
{
	const struct wpa_ie_hdr *hdr;
	const u8 *pos;
	int left;
	int i, count;

	memset(data, 0, sizeof(*data));
	data->proto = WPA_PROTO_WPA;
	data->pairwise_cipher = WPA_CIPHER_TKIP;
	data->group_cipher = WPA_CIPHER_TKIP;
	data->key_mgmt = WPA_KEY_MGMT_IEEE8021X;
	data->capabilities = 0;
	data->pmkid = NULL;
	data->num_pmkid = 0;
	data->mgmt_group_cipher = 0;

	if (wpa_ie_len < sizeof(struct wpa_ie_hdr)) {
		bes_err(BES_DBG_GTK_OFL, "%s: ie len too short %lu",
				__func__, (unsigned long) wpa_ie_len);
		return -1;
	}

	hdr = (const struct wpa_ie_hdr *) wpa_ie;

	if (hdr->elem_id != WLAN_EID_VENDOR_SPECIFIC ||
	    hdr->len != wpa_ie_len - 2 ||
	    RSN_SELECTOR_GET(hdr->oui) != WPA_OUI_TYPE ||
	    WPA_GET_LE16(hdr->version) != WPA_VERSION) {
		bes_err(BES_DBG_GTK_OFL, "%s: malformed ie or unknown version",
				__func__);
		return -2;
	}

	pos = (const u8 *) (hdr + 1);
	left = wpa_ie_len - sizeof(*hdr);

	if (left >= WPA_SELECTOR_LEN) {
		data->group_cipher = wpa_selector_to_bitfield(pos);
		pos += WPA_SELECTOR_LEN;
		left -= WPA_SELECTOR_LEN;
	} else if (left > 0) {
		bes_err(BES_DBG_GTK_OFL, "%s: ie length mismatch, %u too much",
				__func__, left);
		return -3;
	}

	if (left >= 2) {
		data->pairwise_cipher = 0;
		count = WPA_GET_LE16(pos);
		pos += 2;
		left -= 2;
		if (count == 0 || count > left / WPA_SELECTOR_LEN) {
			bes_err(BES_DBG_GTK_OFL, "%s: ie count botch (pairwise), "
					"count %u left %u", __func__, count, left);
			return -4;
		}
		for (i = 0; i < count; i++) {
			data->pairwise_cipher |= wpa_selector_to_bitfield(pos);
			pos += WPA_SELECTOR_LEN;
			left -= WPA_SELECTOR_LEN;
		}
	} else if (left == 1) {
		bes_err(BES_DBG_GTK_OFL, "%s: ie too short (for key mgmt)",
				__func__);
		return -5;
	}

	if (left >= 2) {
		data->key_mgmt = 0;
		count = WPA_GET_LE16(pos);
		pos += 2;
		left -= 2;
		if (count == 0 || count > left / WPA_SELECTOR_LEN) {
			bes_err(BES_DBG_GTK_OFL, "%s: ie count botch (key mgmt), "
					"count %u left %u", __func__, count, left);
			return -6;
		}
		for (i = 0; i < count; i++) {
			data->key_mgmt |= wpa_key_mgmt_to_bitfield(pos);
			pos += WPA_SELECTOR_LEN;
			left -= WPA_SELECTOR_LEN;
		}
	} else if (left == 1) {
		bes_err(BES_DBG_GTK_OFL, "%s: ie too short (for capabilities)",
				__func__);
		return -7;
	}

	if (left >= 2) {
		data->capabilities = WPA_GET_LE16(pos);
		pos += 2;
		left -= 2;
	}

	if (left > 0) {
		bes_dbg_dump(BES_DBG_GTK_OFL,
				"wpa_parse_wpa_ie_wpa: ignore trailing bytes",
				pos, left);
	}

	return 0;
}

int wpa_parse_wpa_ie_rsn(const u8 *rsn_ie, size_t rsn_ie_len,
			struct wpa_ie_data *data)
{
	const u8 *pos;
	int left;
	int i, count;

	memset(data, 0, sizeof(*data));
	data->proto = WPA_PROTO_RSN;
	data->pairwise_cipher = WPA_CIPHER_CCMP;
	data->group_cipher = WPA_CIPHER_CCMP;
	data->key_mgmt = WPA_KEY_MGMT_IEEE8021X;
	data->capabilities = 0;
	data->pmkid = NULL;
	data->num_pmkid = 0;
	data->mgmt_group_cipher = WPA_CIPHER_AES_128_CMAC;

	if (rsn_ie_len == 0) {
		/* No RSN IE - fail silently */
		return -1;
	}

	if (rsn_ie_len < sizeof(struct rsn_ie_hdr)) {
		bes_err(BES_DBG_GTK_OFL, "%s: ie len too short %lu",
				__func__, (unsigned long) rsn_ie_len);
		return -1;
	}

	if (rsn_ie_len >= 6 && rsn_ie[1] >= 4 &&
	    rsn_ie[1] == rsn_ie_len - 2 &&
	    WPA_GET_BE32(&rsn_ie[2]) == OSEN_IE_VENDOR_TYPE) {
		pos = rsn_ie + 6;
		left = rsn_ie_len - 6;

		data->group_cipher = WPA_CIPHER_GTK_NOT_USED;
		data->has_group = 1;
		data->key_mgmt = WPA_KEY_MGMT_OSEN;
		data->proto = WPA_PROTO_OSEN;
	} else {
		const struct rsn_ie_hdr *hdr;

		hdr = (const struct rsn_ie_hdr *) rsn_ie;

		if (hdr->elem_id != WLAN_EID_RSN ||
		    hdr->len != rsn_ie_len - 2 ||
		    WPA_GET_LE16(hdr->version) != RSN_VERSION) {
			bes_err(BES_DBG_GTK_OFL, "%s: malformed ie or unknown version",
					__func__);
			return -2;
		}

		pos = (const u8 *) (hdr + 1);
		left = rsn_ie_len - sizeof(*hdr);
	}

	if (left >= RSN_SELECTOR_LEN) {
		data->group_cipher = rsn_selector_to_bitfield(pos);
		data->has_group = 1;
		if (!wpa_cipher_valid_group(data->group_cipher)) {
			bes_err(BES_DBG_GTK_OFL, "%s: invalid group cipher 0x%x (%08x)",
					__func__, data->group_cipher,
					WPA_GET_BE32(pos));
			return -1;
		}
		pos += RSN_SELECTOR_LEN;
		left -= RSN_SELECTOR_LEN;
	} else if (left > 0) {
		bes_err(BES_DBG_GTK_OFL, "%s: ie length mismatch, %u too much",
				__func__, left);
		return -3;
	}

	if (left >= 2) {
		data->pairwise_cipher = 0;
		count = WPA_GET_LE16(pos);
		pos += 2;
		left -= 2;
		if (count == 0 || count > left / RSN_SELECTOR_LEN) {
			bes_err(BES_DBG_GTK_OFL, "%s: ie count botch (pairwise), "
					"count %u left %u", __func__, count, left);
			return -4;
		}
		if (count)
			data->has_pairwise = 1;
		for (i = 0; i < count; i++) {
			data->pairwise_cipher |= rsn_selector_to_bitfield(pos);
			pos += RSN_SELECTOR_LEN;
			left -= RSN_SELECTOR_LEN;
		}
		if (data->pairwise_cipher & WPA_CIPHER_AES_128_CMAC) {
			bes_err(BES_DBG_GTK_OFL, "%s: AES-128-CMAC used as "
					"pairwise cipher", __func__);
			return -1;
		}
	} else if (left == 1) {
		bes_err(BES_DBG_GTK_OFL, "%s: ie too short (for key mgmt)",
				__func__);
		return -5;
	}

	if (left >= 2) {
		data->key_mgmt = 0;
		count = WPA_GET_LE16(pos);
		pos += 2;
		left -= 2;
		if (count == 0 || count > left / RSN_SELECTOR_LEN) {
			bes_err(BES_DBG_GTK_OFL, "%s: ie count botch (key mgmt), "
					"count %u left %u", __func__, count, left);
			return -6;
		}
		for (i = 0; i < count; i++) {
			data->key_mgmt |= rsn_key_mgmt_to_bitfield(pos);
			pos += RSN_SELECTOR_LEN;
			left -= RSN_SELECTOR_LEN;
		}
	} else if (left == 1) {
		bes_err(BES_DBG_GTK_OFL, "%s: ie too short (for capabilities)",
				__func__);
		return -7;
	}

	if (left >= 2) {
		data->capabilities = WPA_GET_LE16(pos);
		pos += 2;
		left -= 2;
	}

	if (left >= 2) {
		u16 num_pmkid = WPA_GET_LE16(pos);
		pos += 2;
		left -= 2;
		if (num_pmkid > (unsigned int) left / PMKID_LEN) {
			bes_err(BES_DBG_GTK_OFL, "%s: PMKID underflow "
					"(num_pmkid=%u left=%d)",
					__func__, num_pmkid, left);
			data->num_pmkid = 0;
			return -9;
		} else {
			data->num_pmkid = num_pmkid;
			data->pmkid = pos;
			pos += data->num_pmkid * PMKID_LEN;
			left -= data->num_pmkid * PMKID_LEN;
		}
	}

	if (left >= 4) {
		data->mgmt_group_cipher = rsn_selector_to_bitfield(pos);
		if (!wpa_cipher_valid_mgmt_group(data->mgmt_group_cipher)) {
			bes_err(BES_DBG_GTK_OFL, "%s: Unsupported management group cipher 0x%x (%08x)",
					__func__, data->mgmt_group_cipher,
					WPA_GET_BE32(pos));
			return -10;
		}
		pos += RSN_SELECTOR_LEN;
		left -= RSN_SELECTOR_LEN;
	}

	if (left > 0) {
		bes_dbg_dump(BES_DBG_GTK_OFL,
				"wpa_parse_wpa_ie_rsn: ignore trailing bytes",
				pos, left);
	}

	return 0;
}

static int bes_encryp_ie_txt(char *pos, u8 max_size, const char *proto,
			     const u8 *ie, size_t ie_len)
{
	char *start_pos;
	struct wpa_ie_data data = {0};
	int ret;

	ret = snprintf(pos, max_size, "[%s-", proto);
	pos += ret;
	max_size -= ret;

	if (memcmp(proto, "WPA", sizeof("WPA")) == 0) {
		wpa_parse_wpa_ie_wpa(ie, ie_len, &data);
	} else if ((memcmp(proto, "RSN", sizeof("RSN")) == 0)
		   || (memcmp(proto, "WPA2", sizeof("WPA2")) == 0)) {
		wpa_parse_wpa_ie_rsn(ie, ie_len, &data);
	}

	if (data.key_mgmt & WPA_KEY_MGMT_IEEE8021X) {
		ret = snprintf(pos, max_size, "EAP");

		pos += ret;
		max_size -= ret;
	}
	if (data.key_mgmt & WPA_KEY_MGMT_PSK) {
		ret = snprintf(pos, max_size, "PSK");

		pos += ret;
		max_size -= ret;
	}
	if (data.key_mgmt & WPA_KEY_MGMT_WPA_NONE) {
		ret = snprintf(pos, max_size, "None");

		pos += ret;
		max_size -= ret;
	}
	if (data.key_mgmt & WPA_KEY_MGMT_SAE) {
		ret = snprintf(pos, max_size, "SAE");

		pos += ret;
		max_size -= ret;
	}
	if (data.key_mgmt & WPA_KEY_MGMT_FT_IEEE8021X) {
		ret = snprintf(pos, max_size, "FT/EAP");

		pos += ret;
		max_size -= ret;
	}
	if (data.key_mgmt & WPA_KEY_MGMT_FT_PSK) {
		ret = snprintf(pos, max_size, "FT/PSK");

		pos += ret;
		max_size -= ret;
	}
	if (data.key_mgmt & WPA_KEY_MGMT_FT_SAE) {
		ret = snprintf(pos, max_size, "+FT/SAE");

		pos += ret;
		max_size -= ret;
	}
	if (data.key_mgmt & WPA_KEY_MGMT_IEEE8021X_SHA256) {
		ret = snprintf(pos, max_size, "EAP-SHA256");

		pos += ret;
		max_size -= ret;
	}
	if (data.key_mgmt & WPA_KEY_MGMT_PSK_SHA256) {
		ret = snprintf(pos, max_size, "PSK-SHA256");

		pos += ret;
		max_size -= ret;
	}
	if (data.key_mgmt & WPA_KEY_MGMT_IEEE8021X_SUITE_B) {
		ret = snprintf(pos, max_size, "EAP-SUITE-B");

		pos += ret;
		max_size -= ret;
	}
	if (data.key_mgmt & WPA_KEY_MGMT_IEEE8021X_SUITE_B_192) {
		ret = snprintf(pos, max_size, "EAP-SUITE-B-192");

		pos += ret;
		max_size -= ret;
	}
	if (data.key_mgmt & WPA_KEY_MGMT_FILS_SHA256) {
		ret = snprintf(pos, max_size, "FILS-SHA256");

		pos += ret;
		max_size -= ret;
	}
	if (data.key_mgmt & WPA_KEY_MGMT_FILS_SHA384) {
		ret = snprintf(pos, max_size, "FILS-SHA384");

		pos += ret;
		max_size -= ret;
	}
	if (data.key_mgmt & WPA_KEY_MGMT_FT_FILS_SHA256) {
		ret = snprintf(pos, max_size, "FT-FILS-SHA256");

		pos += ret;
		max_size -= ret;
	}
	if (data.key_mgmt & WPA_KEY_MGMT_FT_FILS_SHA384) {
		ret = snprintf(pos, max_size, "FT-FILS-SHA384");

		pos += ret;
		max_size -= ret;
	}
	if (data.key_mgmt & WPA_KEY_MGMT_OWE) {
		ret = snprintf(pos, max_size, "OWE");

		pos += ret;
		max_size -= ret;
	}
	if (data.key_mgmt & WPA_KEY_MGMT_DPP) {
		ret = snprintf(pos, max_size, "DPP");

		pos += ret;
		max_size -= ret;
	}
	if (data.key_mgmt & WPA_KEY_MGMT_OSEN) {
		ret = snprintf(pos, max_size, "OSEN");

		pos += ret;
		max_size -= ret;
	}

	ret = snprintf(pos, max_size, "-");
	pos += ret;
	max_size -= ret;

	start_pos = pos;
	if (data.pairwise_cipher & WPA_CIPHER_CCMP_256) {
		ret = snprintf(pos, max_size, "%sCCMP-256",
			       pos == start_pos ? "" : "+");

		pos += ret;
		max_size -= ret;
	}
	if (data.pairwise_cipher & WPA_CIPHER_GCMP_256) {
		ret = snprintf(pos, max_size, "%sGCMP-256",
			       pos == start_pos ? "" : "+");

		pos += ret;
		max_size -= ret;
	}
	if (data.pairwise_cipher & WPA_CIPHER_CCMP) {
		ret = snprintf(pos, max_size, "%sCCMP",
			       pos == start_pos ? "" : "+");

		pos += ret;
		max_size -= ret;
	}
	if (data.pairwise_cipher & WPA_CIPHER_GCMP) {
		ret = snprintf(pos, max_size, "%sGCMP",
			       pos == start_pos ? "" : "+");
		pos += ret;
		max_size -= ret;
	}
	if (data.pairwise_cipher & WPA_CIPHER_TKIP) {
		ret = snprintf(pos, max_size, "%sTKIP",
			       pos == start_pos ? "" : "+");

		pos += ret;
		max_size -= ret;
	}
	if (data.pairwise_cipher & WPA_CIPHER_AES_128_CMAC) {
		ret = snprintf(pos, max_size, "%sAES-128-CMAC",
			       pos == start_pos ? "" : "+");

		pos += ret;
		max_size -= ret;
	}
	if (data.pairwise_cipher & WPA_CIPHER_BIP_GMAC_128) {
		ret = snprintf(pos, max_size, "%sBIP-GMAC-128",
			       pos == start_pos ? "" : "+");

		pos += ret;
		max_size -= ret;
	}
	if (data.pairwise_cipher & WPA_CIPHER_BIP_GMAC_256) {
		ret = snprintf(pos, max_size, "%sBIP-GMAC-256",
			       pos == start_pos ? "" : "+");

		pos += ret;
		max_size -= ret;
	}
	if (data.pairwise_cipher & WPA_CIPHER_BIP_CMAC_256) {
		ret = snprintf(pos, max_size, "%sBIP-CMAC-256",
			       pos == start_pos ? "" : "+");

		pos += ret;
		max_size -= ret;
	}
	if (data.pairwise_cipher & WPA_CIPHER_NONE) {
		ret = snprintf(pos, max_size, "%sNONE",
			       pos == start_pos ? "" : "+");

		pos += ret;
		max_size -= ret;
	}

	if (data.capabilities & WPA_CAPABILITY_PREAUTH) {
		ret = snprintf(pos, max_size, "preauth");

		pos += ret;
		max_size -= ret;
	}

	ret = snprintf(pos, max_size, "]");

	pos += ret;
	max_size -= ret;

	return (ENCRYP_MODE_CHARACTER_SIZE - max_size);
}

void bes_encryp_analyze(struct cfg80211_bss *bss, struct ieee80211_mgmt *mgmt, u8 *security)
{
	u8 used_size = 0;
	const u8 *mesh_ie = NULL;
	const u8 *rsn_ie = NULL;
	const u8 *wpa_ie = NULL;
	const struct cfg80211_bss_ies *ies = NULL;

	rcu_read_lock();
	// get the mesh ie
	mesh_ie = ieee80211_bss_get_ie(bss, WLAN_EID_MESH_ID);
	// get the rsn ie
	rsn_ie = ieee80211_bss_get_ie(bss, WLAN_EID_RSN);
	// get the wpa ie
	ies = rcu_dereference(bss->ies);
	if (ies)
		wpa_ie = cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT,
						 WLAN_OUI_TYPE_MICROSOFT_WPA,
						 ies->data,
						 ies->len);
	rcu_read_unlock();

	if (mgmt->u.beacon.capab_info & WLAN_CAPABILITY_PRIVACY) {
		if (wpa_ie)
			used_size = bes_encryp_ie_txt(security, ENCRYP_MODE_CHARACTER_SIZE,
						      "WPA", wpa_ie, 2 + wpa_ie[1]);
		if (rsn_ie)
			bes_encryp_ie_txt(security + used_size, ENCRYP_MODE_CHARACTER_SIZE - used_size,
					  mesh_ie ? "RSN" : "WPA2", rsn_ie, 2 + rsn_ie[1]);
		if (wpa_ie == NULL && rsn_ie == NULL)
			snprintf(security, ENCRYP_MODE_CHARACTER_SIZE, "WEP");
	} else {
		snprintf(security, ENCRYP_MODE_CHARACTER_SIZE, "OPEN SYSTEM");
	}
}

void *bes_common_get_param_ptr(struct sk_buff *skb, struct bes_sk_param *sk_param_ptr, u8 tx_rx_status)
{
	void *param_ptr = NULL;
	int data_offset = 0;

	if (tx_rx_status == FRAME_STATUS_TX || tx_rx_status == FRAME_STATUS_TX_CFM)
		data_offset = RWNX_TX_HEADROOM;

	if (!skb_is_nonlinear(skb)) {
		if (skb->len >= data_offset + sk_param_ptr->offset + sk_param_ptr->size)
			param_ptr = (u8*)(skb->data) + data_offset + sk_param_ptr->offset;
	} else if (!skb_copy_bits(skb, data_offset + sk_param_ptr->offset,
				  sk_param_ptr->param_ptr, sk_param_ptr->size)) {
		param_ptr = sk_param_ptr->param_ptr;
	}

	if (!param_ptr) {
		bes_warn(BES_DBG_COMMON, "%s failed, tx_rx_status:%d offset:%d size:%d\n",
			 __func__, tx_rx_status, sk_param_ptr->offset, sk_param_ptr->size);
	}

	return param_ptr;
}