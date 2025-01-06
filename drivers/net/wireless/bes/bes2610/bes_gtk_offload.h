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

#ifndef __GTK_OFFLOAD_H__
#define __GTK_OFFLOAD_H__

#include <linux/module.h>
#include <linux/kernel.h>
#include "rwnx_defs.h"

#define WPA_REPLAY_COUNTER_LEN		8
#define WPA_KCK_MAX_LEN			32
#define WPA_KEK_MAX_LEN			64

struct akm_info_suit {
	u32 akm;
	u32 mic_alg;
	u32 dec_alg;
	u8 kck_len;
	u8 kek_len;
	u8 mic_len;
	u8 kck2_len;
	u8 kek2_len;
};

struct gtk_rekey_tag {
	bool enable;	 /* GTK offload is enabled */
	bool beacon_prot;/* Beacon Protection is enabled */
	u8 proto;	/* protocol type, WPA/RSN */
	u8 rx_replay_counter[WPA_REPLAY_COUNTER_LEN]; /* reply counter */
	u8 kck[WPA_KCK_MAX_LEN]; /* EAPOL-Key Key Confirmation Key (KCK) */
	u8 kek[WPA_KEK_MAX_LEN]; /* EAPOL-Key Key Encryption Key (KEK) */
	u8 gtk_len;	/* the length of group temporal key */
	u8 igtk_len;	/* the length of group temporal key */
	u8 bigtk_len;	/* the length of beacon group temporal key */
	u8 gtk_rsc_len; /* the length of gtk receive sequence counter */
	u8 kck_len; 	/* the length of key check key */
	u8 kek_len;	/* the length of key encryption key */
	u8 dec_alg;	/* the algorithm of decrypting key data, RC4/AES */
	u8 mic_alg;	/* the algorithm of integrity check */
	u8 mic_len;	/* the length of mic result */
	u8 ptk_cs;	/* Pairwise Temporal Key Cipher Suit  */
	u8 gtk_cs;	/* Group Temporal Key Cipher Suit */
	u8 igtk_cs;	/* Integrity Group Temporal Key Cipher Suit */
} __packed;

struct ieee802_1x_hdr {
	u8 version;
	u8 type;
	u16 length;
	/* followed by length octets of data */
} __packed;

struct eapol_header {
	u8 version;
	u8 pkt_type;
	u16 length;
	u8 desc_type;
	u16 key_info;
} __packed;

struct eapol_pattern {
	struct eapol_header hdr;
	struct eapol_header mask;
};

struct lp_gtk_rekey_set_req {
	struct mm_custom_req_hdr hdr;
	struct gtk_rekey_tag info;
} __packed;

struct lp_gtk_rekey_get_req {
	struct mm_custom_req_hdr hdr;
};

#ifdef CONFIG_GTK_OFFLOAD
int bes_set_rekey_data(struct wiphy *wiphy, struct net_device *dev,
		       struct cfg80211_gtk_rekey_data *data);
int bes_get_group_msg1_match_pattern(struct rwnx_vif *vif, struct eapol_pattern *pattern);
void bes_get_security_info(struct rwnx_vif *rwnx_vif, const u8 *req_ie,
			   const u16 assoc_req_ie_len);
#else
static inline int bes_set_rekey_data(struct wiphy *wiphy, struct net_device *dev,
				     struct cfg80211_gtk_rekey_data *data) { return 0; }
static inline int bes_get_group_msg1_match_pattern(struct rwnx_vif *vif,
						   struct eapol_pattern *pattern) { return 0; }
static inline void bes_get_security_info(struct rwnx_vif *rwnx_vif,
					 const u8 *req_ie, const u16 assoc_req_ie_len) {}
#endif

#endif