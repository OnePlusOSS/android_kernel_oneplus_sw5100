/*
 * @file bes_gtk_offload.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <net/cfg80211.h>
#include "bes_gtk_offload.h"
#include "bes_log.h"
#include "rwnx_msg_tx.h"

int wpa_cipher_key_len(int cipher)
{
	switch (cipher) {
		case WPA_CIPHER_CCMP_256:
		case WPA_CIPHER_GCMP_256:
		case WPA_CIPHER_BIP_GMAC_256:
		case WPA_CIPHER_BIP_CMAC_256:
			return 32;
		case WPA_CIPHER_CCMP:
		case WPA_CIPHER_GCMP:
		case WPA_CIPHER_AES_128_CMAC:
		case WPA_CIPHER_BIP_GMAC_128:
			return 16;
		case WPA_CIPHER_TKIP:
			return 32;
	}

	return 0;
}


int wpa_cipher_rsc_len(int cipher)
{
	switch (cipher) {
		case WPA_CIPHER_CCMP_256:
		case WPA_CIPHER_GCMP_256:
		case WPA_CIPHER_CCMP:
		case WPA_CIPHER_GCMP:
		case WPA_CIPHER_TKIP:
			return 6;
	}

	return 0;
}

static int wpa_cipher_to_lmac_cs(int cipher)
{
	switch (cipher) {
		case WPA_CIPHER_WEP40:
			return MAC_CIPHER_WEP40;
		case WPA_CIPHER_WEP104:
			return MAC_CIPHER_WEP104;
		case WPA_CIPHER_TKIP:
			return MAC_CIPHER_TKIP;
		case WPA_CIPHER_CCMP:
			return MAC_CIPHER_CCMP;
		case WPA_CIPHER_AES_128_CMAC:
			return MAC_CIPHER_BIP_CMAC_128;
		case WPA_CIPHER_GCMP:
			return MAC_CIPHER_GCMP_128;
		case WPA_CIPHER_SMS4:
			return MAC_CIPHER_WPI_SMS4;
		case WPA_CIPHER_GCMP_256:
			return MAC_CIPHER_GCMP_256;
		case WPA_CIPHER_CCMP_256:
			return MAC_CIPHER_CCMP_256;
		case WPA_CIPHER_BIP_GMAC_128:
			return MAC_CIPHER_BIP_GMAC_128;
		case WPA_CIPHER_BIP_GMAC_256:
			return MAC_CIPHER_BIP_GMAC_256;
		case WPA_CIPHER_BIP_CMAC_256:
			return MAC_CIPHER_BIP_CMAC_256;
		default:
			return MAC_CIPHER_INVALID;
	}

	return MAC_CIPHER_INVALID;
}

static unsigned int wpa_pmk_len(int akmp, u16 group)
{
	switch (akmp) {
		case WPA_KEY_MGMT_OWE:
		case WPA_KEY_MGMT_DPP:
			if (group == 19) {
				return SHA256_MAC_LEN;
			} else if (group == 20) {
				return SHA384_MAC_LEN;
			} else if (group == 21) {
				return SHA512_MAC_LEN;
			} else {
				bes_warn(BES_DBG_GTK_OFL, "unexpect group(%d) for akmp(%x)\n", group, akmp);

				return 0;
			}
		default:
			return 0;
	}

	return 0;
}

static unsigned int wpa_kck_len(int akmp, size_t pmk_len)
{
	switch (akmp) {
		case WPA_KEY_MGMT_IEEE8021X_SUITE_B_192:
		case WPA_KEY_MGMT_FT_IEEE8021X_SHA384:
			return 24;
		case WPA_KEY_MGMT_FILS_SHA256:
		case WPA_KEY_MGMT_FT_FILS_SHA256:
		case WPA_KEY_MGMT_FILS_SHA384:
		case WPA_KEY_MGMT_FT_FILS_SHA384:
			return 0;
		case WPA_KEY_MGMT_DPP:
			return pmk_len / 2;
		case WPA_KEY_MGMT_OWE:
			return pmk_len / 2;
		default:
			return 16;
	}
}

static unsigned int wpa_kek_len(int akmp, size_t pmk_len)
{
	switch (akmp) {
		case WPA_KEY_MGMT_FILS_SHA384:
		case WPA_KEY_MGMT_FT_FILS_SHA384:
			return 64;
		case WPA_KEY_MGMT_IEEE8021X_SUITE_B_192:
		case WPA_KEY_MGMT_FILS_SHA256:
		case WPA_KEY_MGMT_FT_FILS_SHA256:
		case WPA_KEY_MGMT_FT_IEEE8021X_SHA384:
			return 32;
		case WPA_KEY_MGMT_DPP:
			return pmk_len <= 32 ? 16 : 32;
		case WPA_KEY_MGMT_OWE:
			return pmk_len <= 32 ? 16 : 32;
		default:
			return 16;
	}
}

static int wpa_key_mgmt_sae(int akm)
{
	return !!(akm & (WPA_KEY_MGMT_SAE |
			 WPA_KEY_MGMT_FT_SAE));
}

static int wpa_key_mgmt_suite_b(int akm)
{
	return !!(akm & (WPA_KEY_MGMT_IEEE8021X_SUITE_B |
			 WPA_KEY_MGMT_IEEE8021X_SUITE_B_192));
}

static int wpa_key_mgmt_fils(int akm)
{
	return !!(akm & (WPA_KEY_MGMT_FILS_SHA256 |
			 WPA_KEY_MGMT_FILS_SHA384 |
			 WPA_KEY_MGMT_FT_FILS_SHA256 |
			 WPA_KEY_MGMT_FT_FILS_SHA384));
}

int wpa_use_akm_defined(int akmp)
{
	return akmp == WPA_KEY_MGMT_OSEN ||
		akmp == WPA_KEY_MGMT_OWE ||
		akmp == WPA_KEY_MGMT_DPP ||
		akmp == WPA_KEY_MGMT_FT_IEEE8021X_SHA384 ||
		wpa_key_mgmt_sae(akmp) ||
		wpa_key_mgmt_suite_b(akmp) ||
		wpa_key_mgmt_fils(akmp);
}

static int wpa_key_mgmt_ft(int akm)
{
	return !!(akm & WPA_KEY_MGMT_FT);
}

static int wpa_key_mgmt_sha256(int akm)
{
	return !!(akm & (WPA_KEY_MGMT_PSK_SHA256 |
			 WPA_KEY_MGMT_IEEE8021X_SHA256 |
			 WPA_KEY_MGMT_SAE |
			 WPA_KEY_MGMT_FT_SAE |
			 WPA_KEY_MGMT_OSEN |
			 WPA_KEY_MGMT_IEEE8021X_SUITE_B |
			 WPA_KEY_MGMT_FILS_SHA256 |
			 WPA_KEY_MGMT_FT_FILS_SHA256));
}

int wpa_use_aes_key_wrap(int akmp)
{
	return akmp == WPA_KEY_MGMT_OSEN ||
		akmp == WPA_KEY_MGMT_OWE ||
		akmp == WPA_KEY_MGMT_DPP ||
		wpa_key_mgmt_ft(akmp) ||
		wpa_key_mgmt_sha256(akmp) ||
		wpa_key_mgmt_sae(akmp) ||
		wpa_key_mgmt_suite_b(akmp);
}

int wpa_use_cmac(int akmp)
{
	return akmp == WPA_KEY_MGMT_OSEN ||
		akmp == WPA_KEY_MGMT_OWE ||
		akmp == WPA_KEY_MGMT_DPP ||
		wpa_key_mgmt_ft(akmp) ||
		wpa_key_mgmt_sha256(akmp) ||
		wpa_key_mgmt_sae(akmp) ||
		wpa_key_mgmt_suite_b(akmp);
}

unsigned int wpa_mic_len(int akmp, size_t pmk_len)
{
	switch (akmp) {
		case WPA_KEY_MGMT_IEEE8021X_SUITE_B_192:
		case WPA_KEY_MGMT_FT_IEEE8021X_SHA384:
			return 24;
		case WPA_KEY_MGMT_FILS_SHA256:
		case WPA_KEY_MGMT_FILS_SHA384:
		case WPA_KEY_MGMT_FT_FILS_SHA256:
		case WPA_KEY_MGMT_FT_FILS_SHA384:
			return 0;
		case WPA_KEY_MGMT_DPP:
			return pmk_len / 2;
		case WPA_KEY_MGMT_OWE:
			return pmk_len / 2;
		default:
			return 16;
	}
}

static int wpa_key_desc_ver(int key_mgmt, int pairwise_cipher)
{
	int ver = 0;

	if (wpa_use_akm_defined(key_mgmt))
		ver = WPA_KEY_INFO_TYPE_AKM_DEFINED;
	else if (wpa_key_mgmt_ft(key_mgmt) ||
		 wpa_key_mgmt_sha256(key_mgmt))
		ver = WPA_KEY_INFO_TYPE_AES_128_CMAC;
	else if (pairwise_cipher != WPA_CIPHER_TKIP)
		ver = WPA_KEY_INFO_TYPE_HMAC_SHA1_AES;
	else
		ver = WPA_KEY_INFO_TYPE_HMAC_MD5_RC4;

	return ver;
}

enum mic_alg wpa_key_mgmt_mic_alg(int akmp, int ver, size_t kck_len)
{
	switch (ver) {
	case WPA_KEY_INFO_TYPE_HMAC_MD5_RC4:
		return MIC_ALG_MD5;
	case WPA_KEY_INFO_TYPE_HMAC_SHA1_AES:
		return MIC_ALG_SHA1;
	case WPA_KEY_INFO_TYPE_AES_128_CMAC:
		return MIC_ALG_AES_CMAC;
	case WPA_KEY_INFO_TYPE_AKM_DEFINED:
		switch (akmp) {
		case WPA_KEY_MGMT_SAE:
		case WPA_KEY_MGMT_FT_SAE:
			return MIC_ALG_AES_CMAC;
		case WPA_KEY_MGMT_OSEN:
			return MIC_ALG_AES_CMAC;
		case WPA_KEY_MGMT_IEEE8021X_SUITE_B:
			return MIC_ALG_SHA256;
		case WPA_KEY_MGMT_IEEE8021X_SUITE_B_192:
			return MIC_ALG_SHA384;
		case WPA_KEY_MGMT_OWE:
			if (kck_len == 128 / 8) {
				return MIC_ALG_SHA256;
			} else if (kck_len == 192 / 8) {
				return MIC_ALG_SHA384;
			} else if (kck_len == 256 / 8) {
				return MIC_ALG_SHA512;
			} else {
				bes_err(BES_DBG_GTK_OFL,
					"OWE: Unsupported KCK length: %u",
					(unsigned int) kck_len);
				return -1;
			}
			break;
		case WPA_KEY_MGMT_DPP:
			if (kck_len == 128 / 8) {
				return MIC_ALG_SHA256;
			} else if (kck_len == 192 / 8) {
				return MIC_ALG_SHA384;
			} else if (kck_len == 256 / 8) {
				return MIC_ALG_SHA512;
			} else {
				bes_err(BES_DBG_GTK_OFL,
					"DPP: Unsupported KCK length: %u",
					(unsigned int) kck_len);
				return -1;
			}
			break;
		case WPA_KEY_MGMT_FT_IEEE8021X_SHA384:
			return MIC_ALG_SHA384;
		default:
			bes_err(BES_DBG_GTK_OFL,
				"WPA: EAPOL-Key MIC algorithm not known (AKM-defined - akmp=0x%x)",
				akmp);
			return -1;
		}
		break;
	default:
		bes_err(BES_DBG_GTK_OFL,
			"WPA: EAPOL-Key MIC algorithm not known (ver=%d)",
			ver);
		return -1;
	}

	return -1;
}

enum dec_alg wpa_key_mgmt_dec_alg(int key_mgmt, int ver, int kek_len)
{
	/* Decrypt key data here so that this operation does not need
	 * to be implemented separately for each message type. */
	if (ver == WPA_KEY_INFO_TYPE_HMAC_MD5_RC4 && kek_len == 16) {
		return DEC_ALG_RC4;
	} else if (ver == WPA_KEY_INFO_TYPE_HMAC_SHA1_AES ||
		   ver == WPA_KEY_INFO_TYPE_AES_128_CMAC ||
		   wpa_use_aes_key_wrap(key_mgmt)) {
		return DEC_ALG_NIST_AES;
	} else {
		bes_err(BES_DBG_GTK_OFL,
			"WPA: Unsupported key_info type %d", ver);
		return -1;
	}

	return -1;
}

static int bes_wpa_parse_wpa_ie(const u8 *wpa_ie, size_t wpa_ie_len,
		     struct wpa_ie_data *data)
{
	if (wpa_ie_len >= 1 && wpa_ie[0] == WLAN_EID_RSN)
		return wpa_parse_wpa_ie_rsn(wpa_ie, wpa_ie_len, data);
	if (wpa_ie_len >= 6 && wpa_ie[0] == WLAN_EID_VENDOR_SPECIFIC &&
	    wpa_ie[1] >= 4 && WPA_GET_BE32(&wpa_ie[2]) == OSEN_IE_VENDOR_TYPE)
		return wpa_parse_wpa_ie_rsn(wpa_ie, wpa_ie_len, data);
	else
		return wpa_parse_wpa_ie_wpa(wpa_ie, wpa_ie_len, data);
}

static int bes_get_gtk_rekey_feat(struct rwnx_hw *rwnx_hw, u32 *feat)
{
	struct lp_gtk_rekey_get_req req;

	/* Set command header */
	req.hdr.id = CUSTOM_ID_LP_SLEEP_CONFIG;
	req.hdr.length = sizeof(struct lp_gtk_rekey_get_req);
	req.hdr.ops = LP_SLEEP_OPS_GTK_REKEY_GET;

	return rwnx_send_custom_request(rwnx_hw, &req.hdr, feat, sizeof(*feat));
}

int bes_set_rekey_data(struct wiphy *wiphy, struct net_device *dev,
		       struct cfg80211_gtk_rekey_data *data)
{
	struct lp_gtk_rekey_set_req req;
	struct rwnx_vif *vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	u8 mic_alg, dec_alg;
	int ver = 0, ret = 0;
	u32 gtk_feat;
	u32 pmk_len;
	u8 vif_index = vif->vif_index;

	bes_info(BES_DBG_GTK_OFL, RWNX_FN_ENTRY_STR);

	/* Check the validity of vif_index */
	if (vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_LINK, "%s, vif_index %u invalid\n", __func__, vif_index);
		return -EINVAL;
	}

	/* Only support gtk rekey offlad for sta */
	if (vif->wdev.iftype != NL80211_IFTYPE_STATION) {
		bes_dbg(BES_DBG_GTK_OFL, "only support gtk rekey offlad for sta\n");
		return 0;
	}

	/* Check if the device can offload gtk rekey */
	pmk_len = wpa_pmk_len(vif->sta.sec.key_mgmt, vif->sta.sec.group);
	ver = wpa_key_desc_ver(vif->sta.sec.key_mgmt, vif->sta.sec.pairwise_cipher);
	req.info.kck_len = wpa_kck_len(vif->sta.sec.key_mgmt, pmk_len);
	memcpy(req.info.kck, data->kck, req.info.kck_len);
	req.info.kek_len = wpa_kek_len(vif->sta.sec.key_mgmt, pmk_len);
	memcpy(req.info.kek, data->kek, req.info.kek_len);
	mic_alg = wpa_key_mgmt_mic_alg(vif->sta.sec.key_mgmt, ver, req.info.kck_len);
	dec_alg = wpa_key_mgmt_dec_alg(vif->sta.sec.key_mgmt, ver, req.info.kek_len);
	if ((ret = bes_get_gtk_rekey_feat(rwnx_hw, &gtk_feat)) < 0 ||
	    (gtk_feat & BIT(mic_alg)) == 0 ||
	    (gtk_feat & BIT(dec_alg + 16)) == 0) {
		bes_dbg(BES_DBG_GTK_OFL, "device doesn't support gtk rekey offlad\n");
		return -1;
	}

	/* Set command header */
	req.hdr.id = CUSTOM_ID_LP_SLEEP_CONFIG;
	req.hdr.length = sizeof(struct lp_gtk_rekey_set_req);
	req.hdr.ops = LP_SLEEP_OPS_GTK_REKEY_SET;

	/* Set Rekey Information */
	req.info.enable = true;
	req.info.beacon_prot = false;
	req.info.proto = vif->sta.sec.proto;
	memcpy(req.info.rx_replay_counter, data->replay_ctr, WPA_REPLAY_COUNTER_LEN);
	req.info.gtk_len = wpa_cipher_key_len(vif->sta.sec.group_cipher);
	req.info.igtk_len = wpa_cipher_key_len(vif->sta.sec.mgmt_group_cipher);
	req.info.bigtk_len = 0;
	req.info.gtk_rsc_len = wpa_cipher_rsc_len(vif->sta.sec.group_cipher);
	req.info.mic_alg = mic_alg;
	req.info.dec_alg = dec_alg;
	req.info.mic_len = wpa_mic_len(vif->sta.sec.key_mgmt, pmk_len);
	req.info.ptk_cs = wpa_cipher_to_lmac_cs(vif->sta.sec.pairwise_cipher);
	req.info.gtk_cs = wpa_cipher_to_lmac_cs(vif->sta.sec.group_cipher);
	req.info.igtk_cs = wpa_cipher_to_lmac_cs(vif->sta.sec.mgmt_group_cipher);

	/* Add eapol message 1 filter */
	ret = bes_filter_manager_ctrl_gtk_rekey_offload_filter(rwnx_hw, vif, true);
	if (ret) {
		bes_err(BES_DBG_GTK_OFL, "%s ERROR(%d): fail to set gtk rekey offload filter\n",
			__func__, ret);
		return ret;
	}

	return rwnx_send_custom_request(rwnx_hw, &req.hdr, NULL, 0);
}

int bes_get_group_msg1_match_pattern(struct rwnx_vif *vif, struct eapol_pattern *pattern)
{
	int version;
	u32 pmk_len;

	if (vif->wdev.iftype != NL80211_IFTYPE_STATION)
		return -1;

	if (pattern == NULL)
		return -1;

	/* Get key descriptor version */
	if (wpa_use_akm_defined(vif->sta.sec.key_mgmt))
		version = WPA_KEY_INFO_TYPE_AKM_DEFINED;
	else if (wpa_use_cmac(vif->sta.sec.key_mgmt))
		version = WPA_KEY_INFO_TYPE_AES_128_CMAC;
	else if (vif->sta.sec.pairwise_cipher != WPA_CIPHER_TKIP)
		version = WPA_KEY_INFO_TYPE_HMAC_SHA1_AES;
	else
		version = WPA_KEY_INFO_TYPE_HMAC_MD5_RC4;

	/* Get pmk len*/
	pmk_len = wpa_pmk_len(vif->sta.sec.key_mgmt, vif->sta.sec.group);

	/* Set Pattern */
	pattern->hdr.version = EAPOL_VERSION;
	pattern->hdr.pkt_type = IEEE802_1X_TYPE_EAPOL_KEY;
	pattern->hdr.desc_type = (vif->sta.sec.proto == WPA_PROTO_RSN) ?
		EAPOL_KEY_TYPE_RSN : EAPOL_KEY_TYPE_WPA;
	pattern->hdr.length = 0x00; /* not care */
	pattern->hdr.key_info = WPA_KEY_INFO_SECURE				|
				(wpa_mic_len(vif->sta.sec.key_mgmt, pmk_len) ?
					WPA_KEY_INFO_MIC : 0) 			|
				(vif->sta.sec.proto == WPA_PROTO_RSN ?
					WPA_KEY_INFO_ENCR_KEY_DATA : 0) 	|
				WPA_KEY_INFO_ACK 				|
				version;
	pattern->hdr.key_info = cpu_to_be16(pattern->hdr.key_info);

	/* Set Mask */
	pattern->mask.version = 0xff;
	pattern->mask.pkt_type = 0xff;
	pattern->mask.desc_type = 0xff;
	pattern->mask.length = 0x00;
	pattern->mask.key_info = 0x1fff & (~WPA_KEY_INFO_KEY_INDEX_MASK);
	pattern->mask.key_info = cpu_to_be16(pattern->mask.key_info);

	return 0;
}

void bes_get_security_info(struct rwnx_vif *rwnx_vif, const u8 *req_ie, const u16 assoc_req_ie_len)
{
	const u8 *sec_ie, *owe_dh_ie;

	sec_ie = cfg80211_find_ie(WLAN_EID_RSN, req_ie, assoc_req_ie_len);
	if (sec_ie == NULL)
		sec_ie = cfg80211_find_vendor_ie(WLAN_OUI_MICROSOFT,
				WLAN_OUI_TYPE_MICROSOFT_WPA, req_ie, assoc_req_ie_len);

	memset(&rwnx_vif->sta.sec, 0, sizeof(struct wpa_ie_data));

	if (sec_ie) {
		bes_wpa_parse_wpa_ie(sec_ie, 2 + sec_ie[1], &rwnx_vif->sta.sec);

		if (rwnx_vif->sta.sec.key_mgmt & WPA_KEY_MGMT_OWE) {
			owe_dh_ie = cfg80211_find_ext_ie(WLAN_EID_EXT_OWE_DH, req_ie, assoc_req_ie_len);
			if (owe_dh_ie)
				rwnx_vif->sta.sec.group = le16_to_cpu(*((u16 *)&owe_dh_ie[3]));
		}
	}
}