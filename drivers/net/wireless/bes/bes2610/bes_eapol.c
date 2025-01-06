/*
 * @file bes_eapol.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "bes_eapol.h"
#include "bes_log.h"
#include "rwnx_msg_tx.h"

/* bes_eapol_get_pkt_type() - Obtain the type of current EAPOL frame */
enum msg_eapol_t bes_eapol_get_pkt_type(struct eapol_hdr *eapol)
{
	enum msg_eapol_t type = EAPOL_OTHER;
	u16 key_info, key_data_len;

	if (eapol->type != IEEE802_1X_TYPE_EAPOL_KEY)
		return type;

	if (eapol->key_type != EAPOL_WPA_KEY && eapol->key_type != EAPOL_WPA2_KEY)
		return type;

	key_info = __be16_to_cpu(eapol->key_info);
	key_data_len = __be16_to_cpu(eapol->data_len);

	if (key_info & WPA_KEY_INFO_REQUEST || key_info & WPA_KEY_INFO_ERROR) {
		return type;
	}

	if (!(key_info & WPA_KEY_INFO_KEY_TYPE)) { /* WPA group key handshake */
		if (key_info & WPA_KEY_INFO_ACK)
			type = EAPOL_GROUPKEY_M1;
		else
			type = EAPOL_GROUPKEY_M2;
	} else if (key_info & WPA_KEY_INFO_MIC) {
		if (key_data_len == 0)
			type = EAPOL_4WAY_M4;
		else if (key_info & WPA_KEY_INFO_ACK)
			type = EAPOL_4WAY_M3;
		else
			type = EAPOL_4WAY_M2;
	} else {
		type = EAPOL_4WAY_M1;
	}

	return type;
}

/* Intercepting EAPOL packets on the transmission path.
 * when MESSAGE4 successfully sends (receives ACK),
 * it can be considered that EAPOL has been completed. */
void bes_tx_eapol_completion_notify(struct rwnx_sw_txhdr *sw_txhdr, struct tx_cfm_tag *cfm)
{
	struct eapol_hdr *eapol_hdr_ptr = NULL;
	struct eapol_hdr eapol_hdr;
	struct rwnx_vif *rwnx_vif = sw_txhdr->rwnx_vif;
	enum msg_eapol_t type;
	struct bes_sta_stored_key *sta_stored_key;
	struct rwnx_sta *rwnx_sta;
	bool flag_schedule = false;
	struct txdesc_host *txdesc_host =  &sw_txhdr->desc;
	struct bes_sk_param bes_sk_param;

	if (!rwnx_vif || !rwnx_vif->rwnx_hw) {
		bes_err(BES_DBG_EAPOL, "%s ERROR: vif is not valid\n", __func__);
		return;
	}

	if (txdesc_host->api.host.flags & TXU_CNTRL_MGMT)
		return;

	if (__be16_to_cpu(sw_txhdr->desc.api.host.ethertype) != ETH_P_PAE)
		return;

	/* Parse data eapol_hdr */
	bes_sk_param.param_ptr = (void *)&eapol_hdr;
	bes_sk_param.offset = sizeof(struct ethhdr);
	bes_sk_param.size = sizeof(struct eapol_hdr);
	eapol_hdr_ptr = (struct eapol_hdr *)bes_common_get_param_ptr(sw_txhdr->skb, &bes_sk_param, FRAME_STATUS_TX_CFM);
	if (!eapol_hdr_ptr)
		return;

	if (eapol_hdr_ptr->type != IEEE802_1X_TYPE_EAPOL_KEY)
		return;

	if (RWNX_VIF_TYPE(sw_txhdr->rwnx_vif) == NL80211_IFTYPE_STATION ||
	     RWNX_VIF_TYPE(sw_txhdr->rwnx_vif) == NL80211_IFTYPE_P2P_CLIENT) {
		type = bes_eapol_get_pkt_type(eapol_hdr_ptr);
		bes_dbg(BES_DBG_EAPOL, "%s eapol_type:%d\n", __func__, type);

		switch (type) {
			case EAPOL_4WAY_M2:
				if (cfm->status.tx_done && cfm->status.acknowledged) {
					/* Reinitialize the completion of EAPOL */
					atomic_set(&rwnx_vif->sta.eapol_status, BES_EAPOL_PROCESSING);
				}
				break;
			case EAPOL_4WAY_M4:
				if (cfm->status.tx_done && cfm->status.acknowledged) {
					atomic_set(&rwnx_vif->sta.eapol_status, BES_EAPOL_SUCCESS);
					/* Get rwnx_sta and check it */
					rwnx_sta = rwnx_vif->sta.ap;
					if (!rwnx_sta) {
						bes_err(BES_DBG_EAPOL, "%s: rwnx_sta is NULL\n", __func__);
						return;
					}

					sta_stored_key = &rwnx_vif->sta.sta_stored_key;
					/* schedule work when defer_install is true and vif match */
					flag_schedule = (rwnx_sta->vif_idx == rwnx_vif->vif_index) &&
							 atomic_read(&sta_stored_key->defer_install);
					if (flag_schedule) {
						bes_dbg(BES_DBG_EAPOL, "%s schedule work\n", __func__);
						schedule_work(&sta_stored_key->key_install_defer_work);
					}
				} else {
					atomic_set(&rwnx_vif->sta.eapol_status, BES_EAPOL_FAIL);
				}
				break;
			default: break;
		}
	}
}

static void bes_eapol_sta_stored_key_free(struct rwnx_vif *rwnx_vif)
{
	struct bes_sta_stored_key *sta_stored_key;

	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_STATION &&
	    RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_P2P_CLIENT) {
		return;
	}

	sta_stored_key = &rwnx_vif->sta.sta_stored_key;

	/* Free sta_vif or gc_vif's sta_stored_key */
	if (sta_stored_key->key_param.key != NULL)
		kfree(sta_stored_key->key_param.key);
	memset(&sta_stored_key->key_param, 0, sizeof(struct stored_key_param));
	atomic_set(&sta_stored_key->defer_install, 0);
}

/**
 * @brief Install delayed key when MSG_4 send successfully
 */
static void bes_eapol_key_install_defer_work(struct work_struct *work) {
	struct rwnx_vif *vif = container_of(work, struct rwnx_vif,
					    sta.sta_stored_key.key_install_defer_work);
	struct rwnx_hw *rwnx_hw = vif->rwnx_hw;
	struct bes_sta_stored_key *sta_stored_key = &vif->sta.sta_stored_key;
	struct mm_key_add_cfm key_add_cfm;
	struct rwnx_sta *rwnx_sta;
	int error = 0;

	/* Get rwnx_sta and check it */
	rwnx_sta = vif->sta.ap;
	if (!rwnx_sta) {
		bes_err(BES_DBG_EAPOL, "%s: rwnx_sta is NULL\n", __func__);
		return;
	}

	mutex_lock(&sta_stored_key->mutex);

	/* Check defer_install */
	if (!atomic_read(&sta_stored_key->defer_install)) {
		bes_err(BES_DBG_EAPOL, "%s defer_install is cleared\n", __func__);
		mutex_unlock(&sta_stored_key->mutex);
		return;
	}
	bes_dbg(BES_DBG_EAPOL, "%s vif_id:%d, sta_id:%d, key_len: %d \n", __func__,
		rwnx_sta->vif_idx, rwnx_sta->sta_idx, sta_stored_key->key_param.key_len);

	/* Send stored key to LMAC */
	if ((error = rwnx_send_key_add(rwnx_hw, rwnx_sta->vif_idx,
				       rwnx_sta->sta_idx, true,
				       sta_stored_key->key_param.key,
				       sta_stored_key->key_param.key_len,
				       sta_stored_key->key_param.key_idx,
				       sta_stored_key->key_param.cipher_suite,
				       &key_add_cfm))) {
		bes_err(BES_DBG_EAPOL, "%s install delayed key error:%d", __func__, error);
		goto free_stored_key;
	}

	bes_dbg(BES_DBG_EAPOL, "%s install delayed key successfully", __func__);

	if (key_add_cfm.status != 0) {
		bes_err(BES_DBG_EAPOL, "%s: Status Error(%d)\n", __func__,(&key_add_cfm)->status);
		goto free_stored_key;
	}

	/* Save the index received from LMAC */
	rwnx_sta->key.hw_idx = key_add_cfm.hw_key_idx;
	rwnx_sta->key.valid = true;

free_stored_key:

	bes_eapol_sta_stored_key_free(vif);
	mutex_unlock(&sta_stored_key->mutex);
}

void bes_eapol_sta_stored_key_init(struct rwnx_vif *rwnx_vif)
{
	struct bes_sta_stored_key *sta_stored_key;

	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_STATION &&
	    RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_P2P_CLIENT) {
		return;
	}

	atomic_set(&rwnx_vif->sta.eapol_status, BES_EAPOL_NONE);
	sta_stored_key = &rwnx_vif->sta.sta_stored_key;

	/* Init sta_vif or gc_vif's sta_stored_key */
	memset(&sta_stored_key->key_param, 0, sizeof(struct stored_key_param));
	atomic_set(&sta_stored_key->defer_install, 0);
	INIT_WORK(&sta_stored_key->key_install_defer_work, bes_eapol_key_install_defer_work);
	mutex_init(&sta_stored_key->mutex);
}

/* In some cases, the operation of ADDKEY takes place before EAPOL is completed.
 * Causing EAPOL frames to be sent in encrypted form, solving these cases by storing
 * key, add key later when EAPOL_MSG_4 send successfully */
bool bes_eapol_key_install_defer_store(struct rwnx_vif *vif, bool pairwise, u8 key_index,
				       u8 cipher, struct key_params *params) {
	struct bes_sta_stored_key *sta_stored_key;

	if ((RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_STATION &&
	    RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_P2P_CLIENT) || !pairwise)
		return false;

	/* Trigger wait eapol flow only when MSG_4 send unsuccessfully */
	if (atomic_read(&vif->sta.eapol_status) != BES_EAPOL_PROCESSING) {
		return false;
	}

	/* Solving this bug by sending ADDKEY when EAPOL_MSG_4 send successfully */
	sta_stored_key = &vif->sta.sta_stored_key;
	mutex_lock(&sta_stored_key->mutex);

	/* Free old sta_stored_key */
	bes_eapol_sta_stored_key_free(vif);

	/* Store add key param */
	sta_stored_key->key_param.key_idx = key_index;
	sta_stored_key->key_param.cipher_suite = cipher;
	sta_stored_key->key_param.key_len = params->key_len;
	sta_stored_key->key_param.key = kzalloc(params->key_len, GFP_KERNEL);
	memcpy(sta_stored_key->key_param.key, (u8 *)params->key, params->key_len);
	atomic_set(&sta_stored_key->defer_install, 1);

	mutex_unlock(&sta_stored_key->mutex);

	return true;
}

/* Cancel key_install_defer_work or wait it finish before deleting key to avoid
 * this work run after sta or gc delete key */
void bes_eapol_cancel_key_install_defer_work(struct rwnx_vif *rwnx_vif) {
	struct bes_sta_stored_key *sta_stored_key;

	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_STATION &&
	    RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_P2P_CLIENT) {
		return;
	}

	sta_stored_key = &rwnx_vif->sta.sta_stored_key;

	/* Clear delay_send_key flag */
	if (atomic_xchg(&sta_stored_key->defer_install, 0)) {
		bes_dbg(BES_DBG_EAPOL, "%s: clear defer_install\n", __func__);
	}

	/* Cancel scheduled key_install_defer_work or wait running work finish */
	cancel_work_sync(&sta_stored_key->key_install_defer_work);

	/* Clear sta and gc's eapol_status */
	atomic_set(&rwnx_vif->sta.eapol_status, BES_EAPOL_NONE);
}
