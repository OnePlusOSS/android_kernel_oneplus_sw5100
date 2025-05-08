/**
 ******************************************************************************
 *
 * @file rwnx_msg_tx.c
 *
 * @brief TX function definitions
 *
 * Copyright (C) RivieraWaves 2012-2021
 *
 ******************************************************************************
 */

#include "rwnx_msg_tx.h"
#include "reg_access.h"
#include "rwnx_mod_params.h"
#ifdef CONFIG_RWNX_BFMER
#include "rwnx_bfmer.h"
#endif //(CONFIG_RWNX_BFMER)
#include "bes_sysfs.h"
#include "bes_log.h"
#include "bes_pwr.h"
#include "bes_vif_info.h"
#include "bes_csi.h"
#ifdef CONFIG_OPLUS_PLATFORM
#include "oplus_custom_wlan.h"
#endif

const struct mac_addr mac_addr_bcst = {{0xFFFF, 0xFFFF, 0xFFFF}};

/* Default MAC Rx filters that can be changed by mac80211
 * (via the configure_filter() callback) */
#define RWNX_MAC80211_CHANGEABLE                                                                                       \
	(NXMAC_ACCEPT_BA_BIT | NXMAC_ACCEPT_BAR_BIT | NXMAC_ACCEPT_OTHER_DATA_FRAMES_BIT |                             \
	 NXMAC_ACCEPT_PROBE_REQ_BIT | NXMAC_ACCEPT_PS_POLL_BIT)

/* Default MAC Rx filter */
#define RWNX_DEFAULT_RX_FILTER (RWNX_MAC80211_CHANGEABLE | RWNX_MAC80211_NOT_CHANGEABLE)

const int bw2chnl[] = {
	[NL80211_CHAN_WIDTH_20_NOHT] = PHY_CHNL_BW_20,
	[NL80211_CHAN_WIDTH_20] = PHY_CHNL_BW_20,
	[NL80211_CHAN_WIDTH_40] = PHY_CHNL_BW_40,
	[NL80211_CHAN_WIDTH_80] = PHY_CHNL_BW_80,
	[NL80211_CHAN_WIDTH_160] = PHY_CHNL_BW_160,
	[NL80211_CHAN_WIDTH_80P80] = PHY_CHNL_BW_80P80,
};

const int chnl2bw[] = {
	[PHY_CHNL_BW_20] = NL80211_CHAN_WIDTH_20,
	[PHY_CHNL_BW_40] = NL80211_CHAN_WIDTH_40,
	[PHY_CHNL_BW_80] = NL80211_CHAN_WIDTH_80,
	[PHY_CHNL_BW_160] = NL80211_CHAN_WIDTH_160,
	[PHY_CHNL_BW_80P80] = NL80211_CHAN_WIDTH_80P80,
};

/*****************************************************************************/
#ifdef CONFIG_RWNX_FULLMAC
/**
 * copy_connect_ies -- Copy Association Elements in the the request buffer
 * send to the firmware
 *
 * @vif: Vif that received the connection request
 * @req: Connection request to send to the firmware
 * @sme: Connection info
 *
 * For driver that do not use userspace SME (like this one) the host connection
 * request doesn't explicitly mentions that the connection can use FT over the
 * air. if FT is possible, send the FT elements (as received in update_ft_ies callback)
 * to the firmware
 *
 * In all other cases simply copy the list povided by the user space in the
 * request buffer
 */
static void copy_connect_ies(struct rwnx_vif *vif, struct sm_connect_req *req, struct cfg80211_connect_params *sme)
{
	if ((sme->auth_type == NL80211_AUTHTYPE_FT) && !(vif->sta.flags & RWNX_STA_FT_OVER_DS)) {
		const struct element *rsne, *fte, *mde;
		uint8_t *pos;
		rsne = cfg80211_find_elem(WLAN_EID_RSN, vif->sta.ft_assoc_ies, vif->sta.ft_assoc_ies_len);
		fte = cfg80211_find_elem(WLAN_EID_FAST_BSS_TRANSITION, vif->sta.ft_assoc_ies,
					 vif->sta.ft_assoc_ies_len);
		mde = cfg80211_find_elem(WLAN_EID_MOBILITY_DOMAIN, vif->sta.ft_assoc_ies, vif->sta.ft_assoc_ies_len);
		pos = (uint8_t *)req->ie_buf;

		// We can use FT over the air
		if (sme->bssid)
			memcpy(&vif->sta.ft_target_ap, sme->bssid, ETH_ALEN);

		if (rsne) {
			memcpy(pos, rsne, sizeof(struct element) + rsne->datalen);
			pos += sizeof(struct element) + rsne->datalen;
		}
		memcpy(pos, mde, sizeof(struct element) + mde->datalen);
		pos += sizeof(struct element) + mde->datalen;
		if (fte) {
			memcpy(pos, fte, sizeof(struct element) + fte->datalen);
			pos += sizeof(struct element) + fte->datalen;
		}

		req->ie_len = pos - (uint8_t *)req->ie_buf;
	} else {
		memcpy(req->ie_buf, sme->ie, sme->ie_len);
		req->ie_len = sme->ie_len;
	}
}

/**
 * update_connect_req -- Return the length of the association request IEs
 *
 * @vif: Vif that received the connection request
 * @sme: Connection info
 *
 * Return the ft_ie_len in case of FT.
 * FT over the air is possible if:
 * - auth_type = AUTOMATIC (if already set to FT then it means FT over DS)
 * - already associated to a FT BSS
 * - Target Mobility domain is the same as the curent one
 *
 * If FT is not possible return ie length of the connection info
 */
static int update_connect_req(struct rwnx_vif *vif, struct cfg80211_connect_params *sme)
{
	if ((vif->sta.ap) && (vif->sta.ft_assoc_ies) && (sme->auth_type == NL80211_AUTHTYPE_AUTOMATIC)) {
		const struct element *rsne, *fte, *mde, *mde_req;
		int ft_ie_len = 0;

		mde_req = cfg80211_find_elem(WLAN_EID_MOBILITY_DOMAIN, sme->ie, sme->ie_len);
		mde = cfg80211_find_elem(WLAN_EID_MOBILITY_DOMAIN, vif->sta.ft_assoc_ies, vif->sta.ft_assoc_ies_len);
		if (!mde || !mde_req || memcmp(mde, mde_req, sizeof(struct element) + mde->datalen)) {
			return sme->ie_len;
		}

		ft_ie_len += sizeof(struct element) + mde->datalen;

		rsne = cfg80211_find_elem(WLAN_EID_RSN, vif->sta.ft_assoc_ies, vif->sta.ft_assoc_ies_len);
		fte = cfg80211_find_elem(WLAN_EID_FAST_BSS_TRANSITION, vif->sta.ft_assoc_ies,
					 vif->sta.ft_assoc_ies_len);

		if (rsne && fte) {
			ft_ie_len += 2 * sizeof(struct element) + rsne->datalen + fte->datalen;
			sme->auth_type = NL80211_AUTHTYPE_FT;
			return ft_ie_len;
		} else if (rsne || fte) {
			netdev_warn(vif->ndev, "Missing RSNE or FTE element, skip FT over air");
		} else {
			sme->auth_type = NL80211_AUTHTYPE_FT;
			return ft_ie_len;
		}
	}
	return sme->ie_len;
}
#endif

static void cfg80211_to_rwnx_chan(const struct cfg80211_chan_def *chandef, struct mac_chan_op *chan)
{
	chan->band = chandef->chan->band;
	chan->type = bw2chnl[chandef->width];
	chan->prim20_freq = chandef->chan->center_freq;
	chan->center1_freq = chandef->center_freq1;
	chan->center2_freq = chandef->center_freq2;
	chan->flags = get_chan_flags(chandef->chan->flags);
	chan->tx_power = chan_to_fw_pwr(chandef->chan->max_power);
}

static inline void limit_chan_bw(u8_l *bw, u16_l primary, u16_l *center1)
{
	int oft, new_oft = 10;

	if (*bw <= PHY_CHNL_BW_40)
		return;

	oft = *center1 - primary;
	*bw = PHY_CHNL_BW_40;

	if (oft < 0)
		new_oft = new_oft * -1;
	if (abs(oft) == 10 || abs(oft) == 50)
		new_oft = new_oft * -1;

	*center1 = primary + new_oft;
}

/**
 ******************************************************************************
 * @brief Allocate memory for a message
 *
 * This primitive allocates memory for a message that has to be sent. The memory
 * is allocated dynamically on the heap and the length of the variable parameter
 * structure has to be provided in order to allocate the correct size.
 *
 * Several additional parameters are provided which will be preset in the message
 * and which may be used internally to choose the kind of memory to allocate.
 *
 * The memory allocated will be automatically freed by the kernel, after the
 * pointer has been sent to ke_msg_send(). If the message is not sent, it must
 * be freed explicitly with ke_msg_free().
 *
 * Allocation failure is considered critical and should not happen.
 *
 * @param[in] id        Message identifier
 * @param[in] dest_id   Destination Task Identifier
 * @param[in] src_id    Source Task Identifier
 * @param[in] param_len Size of the message parameters to be allocated
 *
 * @return Pointer to the parameter member of the ke_msg. If the parameter
 *         structure is empty, the pointer will point to the end of the message
 *         and should not be used (except to retrieve the message pointer or to
 *         send the message)
 ******************************************************************************
 */
static inline void *rwnx_msg_zalloc(lmac_msg_id_t const id, lmac_task_id_t const dest_id, lmac_task_id_t const src_id,
				    uint16_t const param_len)
{
	struct lmac_msg *msg;
	gfp_t flags;

	if (is_non_blocking_msg(id) && in_atomic())
		flags = GFP_ATOMIC;
	else
		flags = GFP_KERNEL;

	if (sizeof(struct lmac_msg) + param_len > RWNX_MSG_MAX_LEN) {
		bes_err(BES_DBG_MSG_TX, "%s: param_len %d is too big\n", __func__, param_len);
		return NULL;
	}
	msg = (struct lmac_msg *)kzalloc(sizeof(struct lmac_msg) + param_len, flags);
	if (msg == NULL) {
		bes_err(BES_DBG_MSG_TX, "%s: msg allocation failed\n", __func__);
		return NULL;
	}

	msg->id = id;
	msg->dest_id = dest_id;
	msg->src_id = src_id;
	msg->param_len = param_len;

	return msg->param;
}

static void rwnx_msg_free(struct rwnx_hw *rwnx_hw, const void *msg_params)
{
	struct lmac_msg *msg = container_of((void *)msg_params, struct lmac_msg, param);

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Free the message */
	kfree(msg);
}

static bool rwnx_dev_closed_msg_allow_send(struct rwnx_hw *rwnx_hw, u32 msg_id)
{
	bool ret = false;

	switch (msg_id) {
	case MM_RESET_CFM:
		/* Allow sending MM_RESET_REQ, thus mcu platform_on */
		ret = true;
		break;
	case MM_VERSION_CFM:
	case MM_START_CFM:
	case MM_SET_IDLE_CFM:
	case ME_CONFIG_CFM:
	case MM_SET_PS_MODE_CFM:
	case ME_CHAN_CONFIG_CFM:
	case MM_PTA_CFG_CFM:
		/* Only allow sending these msg when platform is on */
		ret = bes_get_wlan_enabled(rwnx_hw);
		break;
	case MCU_WIFI_CALI_CFM:
	case MCU_WIFI_ON_OFF_CFM:
	case MCU_WIFI_GET_DPD_SIZE_CFM:
	case MCU_WIFI_GET_DPD_DATA_CFM:
	case MCU_WIFI_SEND_DPD_DATA_CFM:
#ifdef DUMP_FW_LOG_ENABLE
	case MCU_DUMP_FW_LOG_CFM:
#endif
	case MCU_BUF_CNT_CFM:
	case MCU_GET_MAC_ADDR_CFM:
	case MCU_DBAC_CALI_CFM:
	case MCU_REG_READ_CFM:
	case MCU_REG_WRITE_CFM:
	case MCU_FW_CHANGE_MODE_CFM:
	case MCU_HOST_LP_SUSPEND_CFM:
	case MCU_HOST_LP_RESUME_CFM:
	case MCU_GET_BT_STATUS_CFM:
	case MCU_SEND_COUNTRY_CODE_CFM:
		ret = true;
		break;
	default:
		ret = false;
		break;
	}

	return ret;
}

static int rwnx_send_msg(struct rwnx_hw *rwnx_hw, const void *msg_params, int reqcfm, lmac_msg_id_t reqid, void *cfm, u16 max_len)
{
	struct lmac_msg *msg;
	struct rwnx_cmd *cmd;
	bool nonblock;
	int ret;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	msg = container_of((void *)msg_params, struct lmac_msg, param);

	if (!test_bit(RWNX_DEV_STARTED, &rwnx_hw->flags) &&
	    !rwnx_dev_closed_msg_allow_send(rwnx_hw, reqid)) {
		bes_err(BES_DBG_MSG_TX, "%s: bypassing (RWNX_DEV_RESTARTING set) 0x%02x\n", __func__, reqid);
		kfree(msg);
		return -EBUSY;
	}
#if defined(CONFIG_RWNX_PCIE) || defined(CONFIG_BES_IBUS)
	else if (!rwnx_hw->ipc_env) {
		bes_warn(BES_DBG_MSG_TX,  "%s: bypassing (restart must have failed)\n", __func__);
		kfree(msg);
		return -EBUSY;
	}
#endif

	nonblock = is_non_blocking_msg(msg->id);

	cmd = kzalloc(sizeof(struct rwnx_cmd), nonblock ? GFP_ATOMIC : GFP_KERNEL);
	if (!cmd) {
		bes_err(BES_DBG_MSG_TX, "%s alloc cmd failed\n", __func__);
		kfree(msg);
		return -ENOMEM;
	}
	cmd->result = -EINTR;
	cmd->id = msg->id;
	cmd->reqid = reqid;
	cmd->a2e_msg = msg;
	bes_dbg(BES_DBG_MSG_TX, "%s addr:%p.\n", __func__, msg);
	cmd->e2a_msg = cfm;
	cmd->e2a_buf_len = max_len;
	if (nonblock)
		cmd->flags = RWNX_CMD_FLAG_NONBLOCK;
	if (reqcfm)
		cmd->flags |= RWNX_CMD_FLAG_REQ_CFM;
	if (bes_pwr_msg_check_skip(rwnx_hw)) {
		cmd->flags |= RWNX_CMD_FLAG_LP_NOT_CARE_ACK;
		if (cmd->flags & RWNX_CMD_FLAG_REQ_CFM)
			cmd->flags |= RWNX_CMD_FLAG_LP_NOT_CARE_CFM;
	}

	ret = rwnx_hw->cmd_mgr.queue(&rwnx_hw->cmd_mgr, cmd);

	if (!nonblock && !ret)
		ret = cmd->result;

	if (!nonblock || ret < 0) {
		kfree(cmd);
		kfree(msg);
	}

	return ret;
}

/******************************************************************************
 *    Control messages handling functions (SOFTMAC and  FULLMAC)
 *****************************************************************************/
int rwnx_send_reset(struct rwnx_hw *rwnx_hw)
{
	void *void_param;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* RESET REQ has no parameter */
	void_param = rwnx_msg_zalloc(MM_RESET_REQ, TASK_MM, DRV_TASK_ID, 0);
	if (!void_param)
		return -ENOMEM;

	return rwnx_send_msg(rwnx_hw, void_param, 1, MM_RESET_CFM, NULL, 0);
}

int rwnx_send_start(struct rwnx_hw *rwnx_hw)
{
	struct mm_start_req *start_req_param;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the START REQ message */
	start_req_param = rwnx_msg_zalloc(MM_START_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_start_req));
	if (!start_req_param)
		return -ENOMEM;

	/* Set parameters for the START message */
	memcpy(&start_req_param->phy_cfg, &rwnx_hw->phy.cfg, sizeof(rwnx_hw->phy.cfg));
	start_req_param->uapsd_timeout = (u32_l)rwnx_hw->mod_params->uapsd_timeout;
	start_req_param->lp_clk_accuracy = (u16_l)rwnx_hw->mod_params->lp_clk_ppm;
	start_req_param->tx_timeout[AC_BK] = (u16_l)rwnx_hw->mod_params->tx_to_bk;
	start_req_param->tx_timeout[AC_BE] = (u16_l)rwnx_hw->mod_params->tx_to_be;
	start_req_param->tx_timeout[AC_VI] = (u16_l)rwnx_hw->mod_params->tx_to_vi;
	start_req_param->tx_timeout[AC_VO] = (u16_l)rwnx_hw->mod_params->tx_to_vo;
#if defined(CONFIG_RWNX_PCIE) || defined(CONFIG_BES_IBUS)
	/* Set host receive buffer size.
	 * For SDIO and USB, MCU is the host. */
	start_req_param->rx_hostbuf_size = (u16_l)rwnx_hw->ipc_env->rxbuf_sz;
#endif

	/* Send the START REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, start_req_param, 1, MM_START_CFM, NULL, 0);
}

int rwnx_send_version_req(struct rwnx_hw *rwnx_hw, struct mm_version_cfm *cfm)
{
	void *void_param;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* VERSION REQ has no parameter */
	void_param = rwnx_msg_zalloc(MM_VERSION_REQ, TASK_MM, DRV_TASK_ID, 0);
	if (!void_param)
		return -ENOMEM;

	return rwnx_send_msg(rwnx_hw, void_param, 1, MM_VERSION_CFM, cfm, sizeof(*cfm));
}

int rwnx_send_add_if(struct rwnx_hw *rwnx_hw, const unsigned char *mac, enum nl80211_iftype iftype, bool p2p,
		     struct mm_add_if_cfm *cfm)
{
	struct mm_add_if_req *add_if_req_param;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the ADD_IF_REQ message */
	add_if_req_param = rwnx_msg_zalloc(MM_ADD_IF_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_add_if_req));
	if (!add_if_req_param)
		return -ENOMEM;

	/* Set parameters for the ADD_IF_REQ message */
	memcpy(&(add_if_req_param->addr.array[0]), mac, ETH_ALEN);
	switch (iftype) {
#ifdef CONFIG_RWNX_FULLMAC
	case NL80211_IFTYPE_P2P_CLIENT:
		add_if_req_param->p2p = true;
		// no break
#endif /* CONFIG_RWNX_FULLMAC */
	case NL80211_IFTYPE_STATION:
		add_if_req_param->type = MM_STA;
		break;

	case NL80211_IFTYPE_ADHOC:
		add_if_req_param->type = MM_IBSS;
		break;

#ifdef CONFIG_RWNX_FULLMAC
	case NL80211_IFTYPE_P2P_GO:
		add_if_req_param->p2p = true;
		// no break
#endif /* CONFIG_RWNX_FULLMAC */
	case NL80211_IFTYPE_AP:
		add_if_req_param->type = MM_AP;
		break;
	case NL80211_IFTYPE_MESH_POINT:
		add_if_req_param->type = MM_MESH_POINT;
		break;
	case NL80211_IFTYPE_AP_VLAN:
		return -1;
	case NL80211_IFTYPE_MONITOR:
		add_if_req_param->type = MM_MONITOR;
		break;
	default:
		add_if_req_param->type = MM_STA;
		break;
	}

#ifdef CONFIG_RWNX_SOFTMAC
	add_if_req_param->p2p = p2p;
#endif /* CONFIG_RWNX_SOFTMAC */

	/* Send the ADD_IF_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, add_if_req_param, 1, MM_ADD_IF_CFM, cfm, sizeof(*cfm));
}

int rwnx_send_remove_if(struct rwnx_hw *rwnx_hw, u8 vif_index)
{
	struct mm_remove_if_req *remove_if_req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_REMOVE_IF_REQ message */
	remove_if_req = rwnx_msg_zalloc(MM_REMOVE_IF_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_remove_if_req));
	if (!remove_if_req)
		return -ENOMEM;

	/* Set parameters for the MM_REMOVE_IF_REQ message */
	remove_if_req->inst_nbr = vif_index;

	/* Send the MM_REMOVE_IF_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, remove_if_req, 1, MM_REMOVE_IF_CFM, NULL, 0);
}

int rwnx_send_set_channel(struct rwnx_hw *rwnx_hw, int phy_idx, struct mm_set_channel_cfm *cfm)
{
#ifdef CONFIG_RWNX_SOFTMAC
	struct cfg80211_chan_def *chandef = &rwnx_hw->hw->conf.chandef;
#endif /* CONFIG_RWNX_SOFTMAC */
	struct mm_set_channel_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	if (phy_idx >= rwnx_hw->phy.cnt)
		return -ENOTSUPP;

	req = rwnx_msg_zalloc(MM_SET_CHANNEL_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_set_channel_req));
	if (!req)
		return -ENOMEM;

	if (phy_idx == 0) {
#ifdef CONFIG_RWNX_SOFTMAC
		cfg80211_to_rwnx_chan(chandef, &req->chan);
#else
		/* On FULLMAC only setting channel of secondary chain */
		wiphy_err(rwnx_hw->wiphy, "Trying to set channel of primary chain");
		return 0;
#endif /* CONFIG_RWNX_SOFTMAC */
	} else {
		req->chan = rwnx_hw->phy.sec_chan;
	}

	req->index = phy_idx;

	if (rwnx_hw->phy.limit_bw)
		limit_chan_bw(&req->chan.type, req->chan.prim20_freq, &req->chan.center1_freq);

	/* Send the MM_SET_CHANNEL_REQ REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, MM_SET_CHANNEL_CFM, cfm, sizeof(*cfm));
}

int rwnx_send_key_add(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 sta_idx, bool pairwise, u8 *key, u8 key_len, u8 key_idx,
		      u8 cipher_suite, struct mm_key_add_cfm *cfm)
{
	struct mm_key_add_req *key_add_req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_KEY_ADD_REQ message */
	key_add_req = rwnx_msg_zalloc(MM_KEY_ADD_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_key_add_req));
	if (!key_add_req)
		return -ENOMEM;

	/* Set parameters for the MM_KEY_ADD_REQ message */
	key_add_req->sta_idx = sta_idx;
	key_add_req->key_idx = key_idx;
	key_add_req->pairwise = pairwise;
	key_add_req->inst_nbr = vif_idx;
	key_add_req->key.length = key_len;
	memcpy(&(key_add_req->key.array[0]), key, key_len);

	key_add_req->cipher_suite = cipher_suite;

	bes_dbg(BES_DBG_MSG_TX, "%s: sta_idx:%d key_idx:%d inst_nbr:%d cipher:%d key_len:%d\n", __func__, key_add_req->sta_idx,
		 key_add_req->key_idx, key_add_req->inst_nbr, key_add_req->cipher_suite, key_add_req->key.length);
#if defined(CONFIG_BES_DBG) || defined(CONFIG_DYNAMIC_DEBUG)
	print_hex_dump_bytes("key: ", DUMP_PREFIX_OFFSET, key_add_req->key.array, key_add_req->key.length);
#endif

	/* Send the MM_KEY_ADD_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, key_add_req, 1, MM_KEY_ADD_CFM, cfm, sizeof(*cfm));
}

int rwnx_send_key_del(struct rwnx_hw *rwnx_hw, uint8_t hw_key_idx)
{
	struct mm_key_del_req *key_del_req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_KEY_DEL_REQ message */
	key_del_req = rwnx_msg_zalloc(MM_KEY_DEL_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_key_del_req));
	if (!key_del_req)
		return -ENOMEM;

	/* Set parameters for the MM_KEY_DEL_REQ message */
	key_del_req->hw_key_idx = hw_key_idx;

	/* Send the MM_KEY_DEL_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, key_del_req, 1, MM_KEY_DEL_CFM, NULL, 0);
}

int rwnx_send_bcn_change(struct rwnx_hw *rwnx_hw, u8 vif_idx, struct rwnx_ipc_buf *buf, u16 tim_oft, u16 tim_len,
			 u16 *csa_oft)
{
	struct mm_bcn_change_req *req;
	size_t payload_len = 0;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

#if !defined(CONFIG_RWNX_PCIE) && !defined(CONFIG_BES_IBUS)
	payload_len = buf->size;
#endif
	/* Build the MM_BCN_CHANGE_REQ message */
	req = rwnx_msg_zalloc(MM_BCN_CHANGE_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_bcn_change_req) + payload_len);
	if (!req)
		return -ENOMEM;

	/* Set parameters for the MM_BCN_CHANGE_REQ message */
#if defined(CONFIG_RWNX_PCIE) || defined(CONFIG_BES_IBUS)
	req->bcn_ptr = buf->dma_addr;
#endif
	req->bcn_len = buf->size;
	req->tim_oft = tim_oft;
	req->tim_len = tim_len;
	req->inst_nbr = vif_idx;

#if !defined(CONFIG_RWNX_PCIE) && !defined(CONFIG_BES_IBUS)
	memcpy(req->payload, buf->addr, payload_len);
#endif

#if defined(CONFIG_RWNX_SOFTMAC)
	BUILD_BUG_ON_MSG(IEEE80211_MAX_CNTDWN_COUNTERS_NUM != BCN_MAX_CSA_CPT,
			 "BCN_MAX_CSA_CPT and IEEE80211_MAX_CNTDWN_COUNTERS_NUM "
			 "have different value");
#endif /* CONFIG_RWNX_SOFTMAC */
	if (csa_oft) {
		int i;
		for (i = 0; i < BCN_MAX_CSA_CPT; i++) {
			req->csa_oft[i] = csa_oft[i];
		}
	}

	/* Send the MM_BCN_CHANGE_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, MM_BCN_CHANGE_CFM, NULL, 0);
}

int rwnx_send_roc(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, struct ieee80211_channel *chan,
	   unsigned int duration, struct mm_remain_on_channel_cfm *cfm)
{
	struct mm_remain_on_channel_req *req;
	struct cfg80211_chan_def chandef;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Create channel definition structure */
	cfg80211_chandef_create(&chandef, chan, NL80211_CHAN_NO_HT);

	/* Build the MM_REMAIN_ON_CHANNEL_REQ message */
	req = rwnx_msg_zalloc(MM_REMAIN_ON_CHANNEL_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_remain_on_channel_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the MM_REMAIN_ON_CHANNEL_REQ message */
	req->op_code = MM_ROC_OP_START;
	req->vif_index = vif->vif_index;
	req->duration_ms = duration;
	cfg80211_to_rwnx_chan(&chandef, &req->chan);

	/* Send the MM_REMAIN_ON_CHANNEL_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, MM_REMAIN_ON_CHANNEL_CFM, cfm, sizeof(*cfm));
}

int rwnx_send_cancel_roc(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif)
{
	struct mm_remain_on_channel_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_REMAIN_ON_CHANNEL_REQ message */
	req = rwnx_msg_zalloc(MM_REMAIN_ON_CHANNEL_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_remain_on_channel_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the MM_REMAIN_ON_CHANNEL_REQ message */
	req->op_code = MM_ROC_OP_CANCEL;
	req->vif_index = vif->vif_index;

	/* Send the MM_REMAIN_ON_CHANNEL_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 0, 0, NULL, 0);
}

int rwnx_send_set_power(struct rwnx_hw *rwnx_hw, u8 vif_idx, s8 pwr, struct mm_set_power_cfm *cfm)
{
	struct mm_set_power_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_SET_POWER_REQ message */
	req = rwnx_msg_zalloc(MM_SET_POWER_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_set_power_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the MM_SET_POWER_REQ message */
	req->inst_nbr = vif_idx;
	req->power = pwr;

	/* Send the MM_SET_POWER_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, MM_SET_POWER_CFM, cfm, sizeof(*cfm));
}

int rwnx_send_set_edca(struct rwnx_hw *rwnx_hw, u8 hw_queue, u32 param, bool uapsd, u8 inst_nbr)
{
	struct mm_set_edca_req *set_edca_req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_SET_EDCA_REQ message */
	set_edca_req = rwnx_msg_zalloc(MM_SET_EDCA_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_set_edca_req));
	if (!set_edca_req)
		return -ENOMEM;

	/* Set parameters for the MM_SET_EDCA_REQ message */
	set_edca_req->ac_param = param;
	set_edca_req->uapsd = uapsd;
	set_edca_req->hw_queue = hw_queue;
	set_edca_req->inst_nbr = inst_nbr;

	/* Send the MM_SET_EDCA_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, set_edca_req, 1, MM_SET_EDCA_CFM, NULL, 0);
}

#ifdef CONFIG_RWNX_P2P_DEBUGFS
int rwnx_send_p2p_oppps_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, u8 ctw,
			    struct mm_set_p2p_oppps_cfm *cfm)
{
	struct mm_set_p2p_oppps_req *p2p_oppps_req;
	int error;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_SET_P2P_OPPPS_REQ message */
	p2p_oppps_req =
		rwnx_msg_zalloc(MM_SET_P2P_OPPPS_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_set_p2p_oppps_req));

	if (!p2p_oppps_req) {
		return -ENOMEM;
	}

	/* Fill the message parameters */
	p2p_oppps_req->vif_index = rwnx_vif->vif_index;
	p2p_oppps_req->ctwindow = ctw;

	/* Send the MM_P2P_OPPPS_REQ message to LMAC FW */
	error = rwnx_send_msg(rwnx_hw, p2p_oppps_req, 1, MM_SET_P2P_OPPPS_CFM, cfm, sizeof(*cfm));

	return (error);
}

int rwnx_send_p2p_noa_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, int count, int interval, int duration,
			  bool dyn_noa, struct mm_set_p2p_noa_cfm *cfm)
{
	struct mm_set_p2p_noa_req *p2p_noa_req;
	int error;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Param check */
	if (count > 255)
		count = 255;

	if (duration >= interval) {
		dev_err(rwnx_hw->dev, "Invalid p2p NOA config: interval=%d <= duration=%d\n", interval, duration);
		return -EINVAL;
	}

	/* Build the MM_SET_P2P_NOA_REQ message */
	p2p_noa_req = rwnx_msg_zalloc(MM_SET_P2P_NOA_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_set_p2p_noa_req));

	if (!p2p_noa_req) {
		return -ENOMEM;
	}

	/* Fill the message parameters */
	p2p_noa_req->vif_index = rwnx_vif->vif_index;
	p2p_noa_req->noa_inst_nb = 0;
	p2p_noa_req->count = count;

	if (count) {
		p2p_noa_req->duration_us = duration * 1024;
		p2p_noa_req->interval_us = interval * 1024;
		p2p_noa_req->start_offset = (interval - duration - 4) * 1024;
		p2p_noa_req->dyn_noa = dyn_noa;
	}

	/* Send the MM_SET_2P_NOA_REQ message to LMAC FW */
	error = rwnx_send_msg(rwnx_hw, p2p_noa_req, 1, MM_SET_P2P_NOA_CFM, cfm, sizeof(*cfm));

	return (error);
}
#endif /* CONFIG_RWNX_P2P_DEBUGFS */

/******************************************************************************
 *    Control messages handling functions (SOFTMAC only)
 *****************************************************************************/
#ifdef CONFIG_RWNX_SOFTMAC
int rwnx_send_sta_add(struct rwnx_hw *rwnx_hw, struct ieee80211_sta *sta, u8 inst_nbr, struct mm_sta_add_cfm *cfm)
{
	struct mm_sta_add_req *sta_add_req;
	union rwnx_thd_phy_ctrl_info *phy;
	struct rwnx_sta *rwnx_sta;
	int error;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_STA_ADD_REQ message */
	sta_add_req = rwnx_msg_zalloc(MM_STA_ADD_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_sta_add_req));
	if (!sta_add_req)
		return -ENOMEM;

	/* Set parameters for the MM_STA_ADD_REQ message */
	memcpy(&(sta_add_req->mac_addr.array[0]), &(sta->addr[0]), ETH_ALEN);

	if (sta->wme)
		sta_add_req->capa_flags |= STA_QOS_CAPA;

	/* TODO: check if a density of 0 microseconds is OK or not for LMAC */
	if (sta->ht_cap.ht_supported) {
		int ht_exp = sta->ht_cap.ampdu_factor;
		int vht_exp = 0;

		sta_add_req->capa_flags |= STA_HT_CAPA;
		sta_add_req->ampdu_size_max_ht = (1 << (IEEE80211_HT_MAX_AMPDU_FACTOR + ht_exp)) - 1;
		sta_add_req->ampdu_spacing_min = rwnx_ampdudensity2usec(sta->ht_cap.ampdu_density);

		if (sta->vht_cap.vht_supported) {
			sta_add_req->capa_flags |= STA_VHT_CAPA;
			vht_exp = (sta->vht_cap.cap & IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_MASK) >>
				  IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT;
			sta_add_req->ampdu_size_max_vht = (1 << (IEEE80211_HT_MAX_AMPDU_FACTOR + vht_exp)) - 1;
		}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
		if (sta->he_cap.has_he) {
			int he_exp_ext = (sta->he_cap.he_cap_elem.mac_cap_info[3] &
					  IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_MASK) >>
					 3;

			sta_add_req->capa_flags |= STA_HE_CAPA;
			if (sta->vht_cap.vht_supported) {
				if ((vht_exp == 7) && he_exp_ext)
					sta_add_req->ampdu_size_max_he =
						(1 << (IEEE80211_HT_MAX_AMPDU_FACTOR + vht_exp + he_exp_ext)) - 1;
				else
					sta_add_req->ampdu_size_max_he = sta_add_req->ampdu_size_max_vht;
			} else {
				if ((ht_exp == 3) && he_exp_ext)
					sta_add_req->ampdu_size_max_he =
						(1 << (IEEE80211_HT_MAX_AMPDU_FACTOR + ht_exp + he_exp_ext)) - 1;
				else
					sta_add_req->ampdu_size_max_he = sta_add_req->ampdu_size_max_ht;
			}
		}
#endif
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)
	if (sta->mfp)
		sta_add_req->capa_flags |= STA_MFP_CAPA;
#endif

	rwnx_sta = (struct rwnx_sta *)sta->drv_priv;
	/* TODO Set the interface index from the vif structure */
	sta_add_req->inst_nbr = inst_nbr;
	sta_add_req->tdls_sta = sta->tdls;
	sta_add_req->tdls_sta_initiator = STA_TDLS_INITIATOR(sta);
	sta_add_req->bssid_index = 0;
	sta_add_req->max_bssid_ind = 0;
	phy = (union rwnx_thd_phy_ctrl_info *)&sta_add_req->paid_gid;
	phy->partialAIDTx = rwnx_sta->paid;
	phy->groupIDTx = rwnx_sta->gid;

	/* Send the MM_STA_ADD_REQ message to LMAC FW */
	error = rwnx_send_msg(rwnx_hw, sta_add_req, 1, MM_STA_ADD_CFM, cfm, sizeof(*cfm));

	return (error);
}

int rwnx_send_sta_del(struct rwnx_hw *rwnx_hw, u8 sta_idx)
{
	struct mm_sta_del_req *sta_del_req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_STA_DEL_REQ message */
	sta_del_req = rwnx_msg_zalloc(MM_STA_DEL_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_sta_del_req));
	if (!sta_del_req)
		return -ENOMEM;

	/* Set parameters for the MM_STA_DEL_REQ message */
	sta_del_req->sta_idx = sta_idx;

	/* Send the MM_STA_DEL_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, sta_del_req, 1, MM_STA_DEL_CFM, NULL, 0);
}

int rwnx_send_add_chanctx(struct rwnx_hw *rwnx_hw, struct ieee80211_chanctx_conf *ctx, struct mm_chan_ctxt_add_cfm *cfm)
{
	struct mm_chan_ctxt_add_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_CHAN_CTXT_ADD_REQ message */
	req = rwnx_msg_zalloc(MM_CHAN_CTXT_ADD_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_chan_ctxt_add_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the CHAN_CTXT_ADD_REQ message */
	cfg80211_to_rwnx_chan(&ctx->def, &req->chan);

	if (rwnx_hw->phy.limit_bw)
		limit_chan_bw(&req->chan.type, req->chan.prim20_freq, &req->chan.center1_freq);

	bes_dbg(BES_DBG_MSG_TX, "mac80211:   freq=%d(c1:%d - c2:%d)/width=%d - band=%d\n"
		 "          prim20=%d(c1:%d - c2:%d)/ type=%d - band=%d\n",
		 ctx->def.chan->center_freq, ctx->def.center_freq1, ctx->def.center_freq2, ctx->def.width,
		 ctx->def.chan->band, req->chan.prim20_freq, req->chan.center1_freq, req->chan.center2_freq,
		 req->chan.type, req->chan.band);
	/* Send the CHAN_CTXT_ADD_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, MM_CHAN_CTXT_ADD_CFM, cfm, sizeof(*cfm));
}

int rwnx_send_del_chanctx(struct rwnx_hw *rwnx_hw, u8 index)
{
	struct mm_chan_ctxt_del_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_CHAN_CTXT_DEL_REQ message */
	req = rwnx_msg_zalloc(MM_CHAN_CTXT_DEL_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_chan_ctxt_del_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the MM_REMOVE_IF_REQ message */
	req->index = index;

	/* Send the MM_CHAN_CTXT_DEL_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, MM_CHAN_CTXT_DEL_CFM, NULL, 0);
}

int rwnx_send_link_chanctx(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 chan_idx, u8 chan_switch)
{
	struct mm_chan_ctxt_link_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_CHAN_CTXT_LINK_REQ message */
	req = rwnx_msg_zalloc(MM_CHAN_CTXT_LINK_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_chan_ctxt_link_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the MM_CHAN_CTXT_LINK_REQ message */
	req->vif_index = vif_idx;
	req->chan_index = chan_idx;
	req->chan_switch = chan_switch;

	/* Send the MM_CHAN_CTXT_LINK_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, MM_CHAN_CTXT_LINK_CFM, NULL, 0);
}

int rwnx_send_unlink_chanctx(struct rwnx_hw *rwnx_hw, u8 vif_idx)
{
	struct mm_chan_ctxt_unlink_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_CHAN_CTXT_UNLINK_REQ message */
	req = rwnx_msg_zalloc(MM_CHAN_CTXT_UNLINK_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_chan_ctxt_unlink_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the MM_CHAN_CTXT_UNLINK_REQ message */
	req->vif_index = vif_idx;

	/* Send the MM_CHAN_CTXT_UNLINK_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, MM_CHAN_CTXT_UNLINK_CFM, NULL, 0);
}

int rwnx_send_update_chanctx(struct rwnx_hw *rwnx_hw, struct ieee80211_chanctx_conf *ctx)
{
	struct mm_chan_ctxt_update_req *req;
	struct rwnx_chanctx *rwnx_chanctx;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_CHAN_CTXT_UPDATE_REQ message */
	req = rwnx_msg_zalloc(MM_CHAN_CTXT_UPDATE_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_chan_ctxt_update_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the MM_CHAN_CTXT_UPDATE_REQ message */
	rwnx_chanctx = (struct rwnx_chanctx *)ctx->drv_priv;
	req->chan_index = rwnx_chanctx->index;
	cfg80211_to_rwnx_chan(&ctx->def, &req->chan);

	if (rwnx_hw->phy.limit_bw)
		limit_chan_bw(&req->chan.type, req->chan.prim20_freq, &req->chan.center1_freq);

	bes_dbg(BES_DBG_MSG_TX, "mac80211:   freq=%d(c1:%d - c2:%d)/width=%d - band=%d\n"
		 "          prim20=%d(c1:%d - c2:%d)/ type=%d - band=%d\n",
		 ctx->def.chan->center_freq, ctx->def.center_freq1, ctx->def.center_freq2, ctx->def.width,
		 ctx->def.chan->band, req->chan.prim20_freq, req->chan.center1_freq, req->chan.center2_freq,
		 req->chan.type, req->chan.band);
	/* Send the MM_CHAN_CTXT_UPDATE_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, MM_CHAN_CTXT_UPDATE_CFM, NULL, 0);
}

int rwnx_send_sched_chanctx(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 chan_idx, u8 type)
{
	struct mm_chan_ctxt_sched_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_CHAN_CTXT_SCHED_REQ message */
	req = rwnx_msg_zalloc(MM_CHAN_CTXT_SCHED_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_chan_ctxt_sched_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the MM_CHAN_CTXT_SCHED_REQ message */
	req->vif_index = vif_idx;
	req->chan_index = chan_idx;
	req->type = type;

	/* Send the MM_CHAN_CTXT_SCHED_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, MM_CHAN_CTXT_SCHED_CFM, NULL, 0);
}

int rwnx_send_dtim_req(struct rwnx_hw *rwnx_hw, u8 dtim_period)
{
	struct mm_set_dtim_req *set_dtim_req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_SET_DTIM_REQ message */
	set_dtim_req = rwnx_msg_zalloc(MM_SET_DTIM_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_set_dtim_req));
	if (!set_dtim_req)
		return -ENOMEM;

	/* Set parameters for the MM_SET_DTIM_REQ message */
	set_dtim_req->dtim_period = dtim_period;

	/* Send the MM_SET_DTIM_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, set_dtim_req, 1, MM_SET_DTIM_CFM, NULL, 0);
}

int rwnx_send_set_br(struct rwnx_hw *rwnx_hw, u32 basic_rates, u8 vif_idx, u8 band)
{
	struct mm_set_basic_rates_req *set_basic_rates_req_param;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_SET_BASIC_RATES_REQ message */
	set_basic_rates_req_param =
		rwnx_msg_zalloc(MM_SET_BASIC_RATES_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_set_basic_rates_req));
	if (!set_basic_rates_req_param)
		return -ENOMEM;

	/* Set parameters for the MM_SET_BASIC_RATES_REQ message */
	set_basic_rates_req_param->rates = basic_rates;
	set_basic_rates_req_param->inst_nbr = vif_idx;
	set_basic_rates_req_param->band = band;

	/* Send the MM_SET_BASIC_RATES_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, set_basic_rates_req_param, 1, MM_SET_BASIC_RATES_CFM, NULL, 0);
}

int rwnx_send_set_beacon_int(struct rwnx_hw *rwnx_hw, u16 beacon_int, u8 vif_idx)
{
	struct mm_set_beacon_int_req *set_beacon_int_req_param;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_SET_BEACON_INT_REQ message */
	set_beacon_int_req_param =
		rwnx_msg_zalloc(MM_SET_BEACON_INT_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_set_beacon_int_req));
	if (!set_beacon_int_req_param)
		return -ENOMEM;

	/* Set parameters for the MM_SET_BEACON_INT_REQ message */
	set_beacon_int_req_param->beacon_int = beacon_int;
	set_beacon_int_req_param->inst_nbr = vif_idx;

	/* Send the MM_SET_BEACON_INT_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, set_beacon_int_req_param, 1, MM_SET_BEACON_INT_CFM, NULL, 0);
}

int rwnx_send_set_bssid(struct rwnx_hw *rwnx_hw, const u8 *bssid, u8 vif_idx)
{
	struct mm_set_bssid_req *set_bssid_req_param;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_SET_BSSID_REQ message */
	set_bssid_req_param = rwnx_msg_zalloc(MM_SET_BSSID_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_set_bssid_req));
	if (!set_bssid_req_param)
		return -ENOMEM;

	/* Set parameters for the MM_SET_BSSID_REQ message */
	memcpy(&(set_bssid_req_param->bssid.array[0]), bssid, ETH_ALEN);
	set_bssid_req_param->inst_nbr = vif_idx;

	/* Send the MM_SET_BSSID_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, set_bssid_req_param, 1, MM_SET_BSSID_CFM, NULL, 0);
}

int rwnx_send_set_vif_state(struct rwnx_hw *rwnx_hw, bool active, u16 aid, u8 vif_idx)
{
	struct mm_set_vif_state_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_SET_VIF_STATE_REQ message */
	req = rwnx_msg_zalloc(MM_SET_VIF_STATE_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_set_vif_state_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the MM_SET_VIF_STATE_REQ message */
	req->active = active;
	req->aid = aid;
	req->inst_nbr = vif_idx;

	/* Send the MM_SET_VIF_STATE_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, MM_SET_VIF_STATE_CFM, NULL, 0);
}

int rwnx_send_set_mode(struct rwnx_hw *rwnx_hw, u8 abgnmode)
{
	struct mm_set_mode_req *set_mode_req_param;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_SET_MODE_REQ message */
	set_mode_req_param = rwnx_msg_zalloc(MM_SET_MODE_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_set_mode_req));
	if (!set_mode_req_param)
		return -ENOMEM;

	set_mode_req_param->abgnmode = abgnmode;

	/* Send the MM_SET_MODE_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, set_mode_req_param, 1, MM_SET_MODE_CFM, NULL, 0);
}

int rwnx_send_set_idle(struct rwnx_hw *rwnx_hw, int idle)
{
	struct mm_set_idle_req *set_idle_req_param;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	set_idle_req_param = rwnx_msg_zalloc(MM_SET_IDLE_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_set_idle_req));
	if (!set_idle_req_param)
		return -ENOMEM;

	set_idle_req_param->hw_idle = idle;

	return rwnx_send_msg(rwnx_hw, set_idle_req_param, 1, MM_SET_IDLE_CFM, NULL, 0);
}

int rwnx_send_set_ps_mode(struct rwnx_hw *rwnx_hw, u8 ps_mode)
{
	struct mm_set_ps_mode_req *set_ps_mode_req_param;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	set_ps_mode_req_param =
		rwnx_msg_zalloc(MM_SET_PS_MODE_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_set_ps_mode_req));
	if (!set_ps_mode_req_param)
		return -ENOMEM;

	set_ps_mode_req_param->new_state = ps_mode;

	return rwnx_send_msg(rwnx_hw, set_ps_mode_req_param, 1, MM_SET_PS_MODE_CFM, NULL, 0);
}

int rwnx_send_set_ps_options(struct rwnx_hw *rwnx_hw, bool listen_bcmc, u16 listen_interval, u8 vif_idx)
{
	struct mm_set_ps_options_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_SET_PS_OPTIONS_REQ message */
	req = rwnx_msg_zalloc(MM_SET_PS_OPTIONS_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_set_ps_options_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the MM_SET_VIF_STATE_REQ message */
	req->listen_interval = listen_interval;
	req->dont_listen_bc_mc = !listen_bcmc;
	req->vif_index = vif_idx;

	/* Send the MM_SET_PS_OPTIONS_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, MM_SET_PS_OPTIONS_CFM, NULL, 0);
}

int rwnx_send_set_slottime(struct rwnx_hw *rwnx_hw, int use_short_slot)
{
	struct mm_set_slottime_req *set_slottime_req_param;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_SET_SLOTTIME_REQ message */
	set_slottime_req_param =
		rwnx_msg_zalloc(MM_SET_SLOTTIME_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_set_slottime_req));
	if (!set_slottime_req_param)
		return -ENOMEM;

	/* Set parameters for the MM_SET_SLOTTIME_REQ message */
	set_slottime_req_param->slottime = use_short_slot ? 9 : 20;

	/* Send the MM_SET_SLOTTIME_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, set_slottime_req_param, 1, MM_SET_SLOTTIME_CFM, NULL, 0);
}

int rwnx_send_ba_add(struct rwnx_hw *rwnx_hw, uint8_t type, uint8_t sta_idx, u16 tid, uint8_t bufsz, uint16_t ssn,
		     struct mm_ba_add_cfm *cfm)
{
	struct mm_ba_add_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_BA_ADD_REQ message */
	req = rwnx_msg_zalloc(MM_BA_ADD_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_ba_add_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the MM_BA_ADD_REQ message */
	req->type = type;
	req->sta_idx = sta_idx;
	req->tid = (u8_l)tid;
	req->bufsz = bufsz;
	req->ssn = ssn;

	/* Send the MM_BA_ADD_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, MM_BA_ADD_CFM, cfm, sizeof(*cfm));
}

int rwnx_send_ba_del(struct rwnx_hw *rwnx_hw, uint8_t sta_idx, u16 tid, struct mm_ba_del_cfm *cfm)
{
	struct mm_ba_del_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_BA_DEL_REQ message */
	req = rwnx_msg_zalloc(MM_BA_DEL_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_ba_del_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters */
	req->type = 0;
	req->sta_idx = sta_idx;
	req->tid = (u8_l)tid;

	/* Send the MM_BA_DEL_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, MM_BA_DEL_CFM, cfm, sizeof(*cfm));
}

int rwnx_send_scan_req(struct rwnx_hw *rwnx_hw, struct ieee80211_vif *vif, struct cfg80211_scan_request *param,
		       struct scan_start_cfm *cfm)
{
	struct scan_start_req *req;
	int i;
	struct rwnx_vif *rwnx_vif;
	uint8_t chan_flags = 0;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	rwnx_vif = (struct rwnx_vif *)vif->drv_priv;

	/* Build the SCAN_START_REQ message */
	req = rwnx_msg_zalloc(SCAN_START_REQ, TASK_SCAN, DRV_TASK_ID, sizeof(struct scan_start_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters */
	req->vif_idx = rwnx_vif->vif_index;
	req->chan_cnt = (u8)min_t(int, SCAN_CHANNEL_MAX, param->n_channels);
	req->ssid_cnt = (u8)min_t(int, SCAN_SSID_MAX, param->n_ssids);
	req->bssid = mac_addr_bcst;
	req->no_cck = param->no_cck;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
	if (param->duration_mandatory)
		req->duration = ieee80211_tu_to_usec(param->duration);
#endif

	if (req->ssid_cnt == 0)
		chan_flags |= CHAN_NO_IR;
	for (i = 0; i < req->ssid_cnt; i++) {
		int j;
		for (j = 0; j < param->ssids[i].ssid_len; j++)
			req->ssid[i].array[j] = param->ssids[i].ssid[j];
		req->ssid[i].length = param->ssids[i].ssid_len;
	}

	if (param->ie) {
		if (rwnx_ipc_buf_a2e_alloc(rwnx_hw, &rwnx_hw->scan_ie, param->ie_len, param->ie)) {
			dev_err(rwnx_hw->dev, "Failed to allocate IPC buffer for Scan IEs\n");
			goto error;
		}

		req->add_ie_len = param->ie_len;
		req->add_ies = rwnx_hw->scan_ie.dma_addr;
	} else {
		req->add_ie_len = 0;
		req->add_ies = 0;
	}

	for (i = 0; i < req->chan_cnt; i++) {
		struct ieee80211_channel *chan = param->channels[i];

		req->chan[i].band = chan->band;
		req->chan[i].freq = chan->center_freq;
		req->chan[i].flags = chan_flags | get_chan_flags(chan->flags);
		req->chan[i].tx_power = chan_to_fw_pwr(chan->max_reg_power);
	}

	/* Send the SCAN_START_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, SCAN_START_CFM, cfm, sizeof(*cfm));
error:
	if (req != NULL)
		rwnx_msg_free(rwnx_hw, req);
	return -ENOMEM;
}

int rwnx_send_scan_cancel_req(struct rwnx_hw *rwnx_hw, struct scan_cancel_cfm *cfm)
{
	struct scan_cancel_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the SCAN_CANCEL_REQ message */
	req = rwnx_msg_zalloc(SCAN_CANCEL_REQ, TASK_SCAN, DRV_TASK_ID, sizeof(struct scan_cancel_req));
	if (!req)
		return -ENOMEM;

	/* Send the SCAN_CANCEL_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, SCAN_CANCEL_CFM, cfm, sizeof(*cfm));
}

void rwnx_send_tdls_ps(struct rwnx_hw *rwnx_hw, bool ps_mode)
{
	if (!rwnx_hw->mod_params->ps_on)
		return;

	rwnx_send_set_ps_mode(rwnx_hw, ps_mode);
}

int rwnx_send_tim_update(struct rwnx_hw *rwnx_hw, u8 vif_idx, u16 aid, u8 tx_status)
{
	struct mm_tim_update_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_TIM_UPDATE_REQ message */
	req = rwnx_msg_zalloc(MM_TIM_UPDATE_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_tim_update_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the MM_TIM_UPDATE_REQ message */
	req->aid = aid;
	req->tx_avail = tx_status;
	req->inst_nbr = vif_idx;

	/* Send the MM_TIM_UPDATE_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, MM_TIM_UPDATE_CFM, NULL, 0);
}
#endif /* CONFIG_RWNX_SOFTMAC */

/******************************************************************************
 *    Control messages handling functions (FULLMAC only)
 *****************************************************************************/
#ifdef CONFIG_RWNX_FULLMAC
int rwnx_send_me_config_req(struct rwnx_hw *rwnx_hw)
{
	struct me_config_req *req;
	struct wiphy *wiphy = rwnx_hw->wiphy;
	struct ieee80211_sta_ht_cap *ht_cap = &wiphy->bands[NL80211_BAND_5GHZ]->ht_cap;
	struct ieee80211_sta_vht_cap *vht_cap = &wiphy->bands[NL80211_BAND_5GHZ]->vht_cap;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
	struct ieee80211_sta_he_cap const *he_cap;
#endif
	uint8_t *ht_mcs = (uint8_t *)&ht_cap->mcs;
	int i;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the ME_CONFIG_REQ message */
	req = rwnx_msg_zalloc(ME_CONFIG_REQ, TASK_ME, DRV_TASK_ID, sizeof(struct me_config_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the ME_CONFIG_REQ message */
	req->ht_supp = ht_cap->ht_supported;
	req->vht_supp = vht_cap->vht_supported;
	req->ht_cap.ht_capa_info = cpu_to_le16(ht_cap->cap);
	req->ht_cap.a_mpdu_param =
		ht_cap->ampdu_factor | (ht_cap->ampdu_density << IEEE80211_HT_AMPDU_PARM_DENSITY_SHIFT);
	for (i = 0; i < sizeof(ht_cap->mcs); i++)
		req->ht_cap.mcs_rate[i] = ht_mcs[i];
	req->ht_cap.ht_extended_capa = 0;
	req->ht_cap.tx_beamforming_capa = 0;
	req->ht_cap.asel_capa = 0;

	req->vht_cap.vht_capa_info = cpu_to_le32(vht_cap->cap);
	req->vht_cap.rx_highest = cpu_to_le16(vht_cap->vht_mcs.rx_highest);
	req->vht_cap.rx_mcs_map = cpu_to_le16(vht_cap->vht_mcs.rx_mcs_map);
	req->vht_cap.tx_highest = cpu_to_le16(vht_cap->vht_mcs.tx_highest);
	req->vht_cap.tx_mcs_map = cpu_to_le16(vht_cap->vht_mcs.tx_mcs_map);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
	if (wiphy->bands[NL80211_BAND_5GHZ]->iftype_data != NULL) {
		he_cap = &wiphy->bands[NL80211_BAND_5GHZ]->iftype_data->he_cap;

		req->he_supp = he_cap->has_he;
		for (i = 0; i < ARRAY_SIZE(he_cap->he_cap_elem.mac_cap_info); i++) {
			req->he_cap.mac_cap_info[i] = he_cap->he_cap_elem.mac_cap_info[i];
		}
		for (i = 0; i < ARRAY_SIZE(he_cap->he_cap_elem.phy_cap_info); i++) {
			req->he_cap.phy_cap_info[i] = he_cap->he_cap_elem.phy_cap_info[i];
		}
		req->he_cap.mcs_supp.rx_mcs_80 = cpu_to_le16(he_cap->he_mcs_nss_supp.rx_mcs_80);
		req->he_cap.mcs_supp.tx_mcs_80 = cpu_to_le16(he_cap->he_mcs_nss_supp.tx_mcs_80);
		req->he_cap.mcs_supp.rx_mcs_160 = cpu_to_le16(he_cap->he_mcs_nss_supp.rx_mcs_160);
		req->he_cap.mcs_supp.tx_mcs_160 = cpu_to_le16(he_cap->he_mcs_nss_supp.tx_mcs_160);
		req->he_cap.mcs_supp.rx_mcs_80p80 = cpu_to_le16(he_cap->he_mcs_nss_supp.rx_mcs_80p80);
		req->he_cap.mcs_supp.tx_mcs_80p80 = cpu_to_le16(he_cap->he_mcs_nss_supp.tx_mcs_80p80);
		for (i = 0; i < MAC_HE_PPE_THRES_MAX_LEN; i++) {
			req->he_cap.ppe_thres[i] = he_cap->ppe_thres[i];
		}
		req->he_ul_on = rwnx_hw->mod_params->he_ul_on;
	}
#else
	req->he_supp = false;
	req->he_ul_on = false;
#endif
	req->ps_on = rwnx_hw->mod_params->ps_on;
	req->dpsm = rwnx_hw->mod_params->dpsm;
	req->tx_lft = rwnx_hw->mod_params->tx_lft;
	req->amsdu_tx = rwnx_hw->mod_params->amsdu_tx;
	req->ant_div_on = rwnx_hw->mod_params->ant_div;
	if (rwnx_hw->mod_params->use_80)
		req->phy_bw_max = PHY_CHNL_BW_80;
	else if (rwnx_hw->mod_params->use_2040)
		req->phy_bw_max = PHY_CHNL_BW_40;
	else
		req->phy_bw_max = PHY_CHNL_BW_20;

	wiphy_info(wiphy, "HT supp %d, VHT supp %d, HE supp %d\n", req->ht_supp, req->vht_supp, req->he_supp);

	/* Send the ME_CONFIG_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, ME_CONFIG_CFM, NULL, 0);
}

int rwnx_send_me_chan_config_req(struct rwnx_hw *rwnx_hw)
{
	struct me_chan_config_req *req;
	struct wiphy *wiphy = rwnx_hw->wiphy;
	int i;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the ME_CHAN_CONFIG_REQ message */
	req = rwnx_msg_zalloc(ME_CHAN_CONFIG_REQ, TASK_ME, DRV_TASK_ID, sizeof(struct me_chan_config_req));
	if (!req)
		return -ENOMEM;

	req->chan2G4_cnt = 0;
	if (wiphy->bands[NL80211_BAND_2GHZ] != NULL) {
		struct ieee80211_supported_band *b = wiphy->bands[NL80211_BAND_2GHZ];
		for (i = 0; i < b->n_channels; i++) {
			req->chan2G4[req->chan2G4_cnt].flags = 0;
			if (b->channels[i].flags & IEEE80211_CHAN_DISABLED)
				req->chan2G4[req->chan2G4_cnt].flags |= CHAN_DISABLED;
			req->chan2G4[req->chan2G4_cnt].flags |= get_chan_flags(b->channels[i].flags);
			req->chan2G4[req->chan2G4_cnt].band = NL80211_BAND_2GHZ;
			req->chan2G4[req->chan2G4_cnt].freq = b->channels[i].center_freq;
			req->chan2G4[req->chan2G4_cnt].tx_power = chan_to_fw_pwr(b->channels[i].max_power);
			req->chan2G4_cnt++;
			if (req->chan2G4_cnt == MAC_DOMAINCHANNEL_24G_MAX)
				break;
		}
	}

	req->chan5G_cnt = 0;
	if (wiphy->bands[NL80211_BAND_5GHZ] != NULL) {
		struct ieee80211_supported_band *b = wiphy->bands[NL80211_BAND_5GHZ];
		for (i = 0; i < b->n_channels; i++) {
			req->chan5G[req->chan5G_cnt].flags = 0;
			if (b->channels[i].flags & IEEE80211_CHAN_DISABLED)
				req->chan5G[req->chan5G_cnt].flags |= CHAN_DISABLED;
			req->chan5G[req->chan5G_cnt].flags |= get_chan_flags(b->channels[i].flags);
			req->chan5G[req->chan5G_cnt].band = NL80211_BAND_5GHZ;
			req->chan5G[req->chan5G_cnt].freq = b->channels[i].center_freq;
			req->chan5G[req->chan5G_cnt].tx_power = chan_to_fw_pwr(b->channels[i].max_power);
			req->chan5G_cnt++;
			if (req->chan5G_cnt == MAC_DOMAINCHANNEL_5G_MAX)
				break;
		}
	}

	/* Send the ME_CHAN_CONFIG_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, ME_CHAN_CONFIG_CFM, NULL, 0);
}

int rwnx_send_me_set_control_port_req(struct rwnx_hw *rwnx_hw, bool opened, u8 sta_idx)
{
	struct me_set_control_port_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the ME_SET_CONTROL_PORT_REQ message */
	req = rwnx_msg_zalloc(ME_SET_CONTROL_PORT_REQ, TASK_ME, DRV_TASK_ID, sizeof(struct me_set_control_port_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the ME_SET_CONTROL_PORT_REQ message */
	req->sta_idx = sta_idx;
	req->control_port_open = opened;

	/* Send the ME_SET_CONTROL_PORT_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, ME_SET_CONTROL_PORT_CFM, NULL, 0);
}

int rwnx_send_me_sta_add(struct rwnx_hw *rwnx_hw, struct station_parameters *params, const u8 *mac, u8 inst_nbr,
			 struct me_sta_add_cfm *cfm)
{
	struct me_sta_add_req *req;
	u8 *ht_mcs = (u8 *)&RWNX_CFG80211_STA_PARA(params, ht_capa)->mcs;
	int i;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_STA_ADD_REQ message */
	req = rwnx_msg_zalloc(ME_STA_ADD_REQ, TASK_ME, DRV_TASK_ID, sizeof(struct me_sta_add_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the MM_STA_ADD_REQ message */
	memcpy(&(req->mac_addr.array[0]), mac, ETH_ALEN);

	req->rate_set.length = RWNX_CFG80211_STA_PARA(params, supported_rates_len);
	for (i = 0; i < RWNX_CFG80211_STA_PARA(params, supported_rates_len); i++)
		req->rate_set.array[i] = RWNX_CFG80211_STA_PARA(params, supported_rates)[i];

	req->flags = 0;

	if (params->capability & WLAN_CAPABILITY_SHORT_PREAMBLE)
		req->flags |= STA_SHORT_PREAMBLE_CAPA;

	if (RWNX_CFG80211_STA_PARA(params, ht_capa)) {
		const struct ieee80211_ht_cap *ht_capa = RWNX_CFG80211_STA_PARA(params, ht_capa);

		req->flags |= STA_HT_CAPA;
		req->ht_cap.ht_capa_info = cpu_to_le16(ht_capa->cap_info);
		req->ht_cap.a_mpdu_param = ht_capa->ampdu_params_info;
		for (i = 0; i < sizeof(ht_capa->mcs); i++)
			req->ht_cap.mcs_rate[i] = ht_mcs[i];
		req->ht_cap.ht_extended_capa = cpu_to_le16(ht_capa->extended_ht_cap_info);
		req->ht_cap.tx_beamforming_capa = cpu_to_le32(ht_capa->tx_BF_cap_info);
		req->ht_cap.asel_capa = ht_capa->antenna_selection_info;
	}

	if (RWNX_CFG80211_STA_PARA(params, vht_capa)) {
		const struct ieee80211_vht_cap *vht_capa = RWNX_CFG80211_STA_PARA(params, vht_capa);

		req->flags |= STA_VHT_CAPA;
		req->vht_cap.vht_capa_info = cpu_to_le32(vht_capa->vht_cap_info);
		req->vht_cap.rx_highest = cpu_to_le16(vht_capa->supp_mcs.rx_highest);
		req->vht_cap.rx_mcs_map = cpu_to_le16(vht_capa->supp_mcs.rx_mcs_map);
		req->vht_cap.tx_highest = cpu_to_le16(vht_capa->supp_mcs.tx_highest);
		req->vht_cap.tx_mcs_map = cpu_to_le16(vht_capa->supp_mcs.tx_mcs_map);
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
	if (RWNX_CFG80211_STA_PARA(params, he_capa)) {
		u8 *he_capa_arry = (u8 *)(RWNX_CFG80211_STA_PARA(params, he_capa));
		u8 he_phy_cap0 = he_capa_arry[MAC_HE_MAC_CAPA_LEN];
		struct ieee80211_he_mcs_nss_supp *mcs_nss_supp =
			(struct ieee80211_he_mcs_nss_supp *)(he_capa_arry + MAC_HE_MAC_CAPA_LEN + MAC_HE_PHY_CAPA_LEN);

		req->flags |= STA_HE_CAPA;

		/* copy mac and phy capability information */
		memcpy(&req->he_cap, RWNX_CFG80211_STA_PARA(params, he_capa), MAC_HE_MAC_CAPA_LEN + MAC_HE_PHY_CAPA_LEN);

		/* copy rate support information of <=80M */
		req->he_cap.mcs_supp.rx_mcs_80 = mcs_nss_supp->rx_mcs_80;
		req->he_cap.mcs_supp.tx_mcs_80 = mcs_nss_supp->tx_mcs_80;

		/* copy rate support information of 160M */
		if (he_phy_cap0 | IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G) {
			req->he_cap.mcs_supp.rx_mcs_160 = mcs_nss_supp->rx_mcs_160;
			req->he_cap.mcs_supp.tx_mcs_160 = mcs_nss_supp->tx_mcs_160;
		}

		/* copy rate support information of 80M+80M */
		if (he_phy_cap0 | IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_80PLUS80_MHZ_IN_5G) {
			req->he_cap.mcs_supp.rx_mcs_80p80 = mcs_nss_supp->rx_mcs_80p80;
			req->he_cap.mcs_supp.tx_mcs_80p80 = mcs_nss_supp->tx_mcs_80p80;
		}
	}
#endif

	if (params->sta_flags_set & BIT(NL80211_STA_FLAG_WME))
		req->flags |= STA_QOS_CAPA;

	if (params->sta_flags_set & BIT(NL80211_STA_FLAG_MFP))
		req->flags |= STA_MFP_CAPA;

	if (RWNX_CFG80211_STA_PARA(params, opmode_notif_used)) {
		req->flags |= STA_OPMOD_NOTIF;
		req->opmode = RWNX_CFG80211_STA_PARA(params, opmode_notif);
	}

	req->aid = cpu_to_le16(params->aid);
	req->uapsd_queues = params->uapsd_queues;
	req->max_sp_len = params->max_sp * 2;
	req->vif_idx = inst_nbr;

	if (params->sta_flags_set & BIT(NL80211_STA_FLAG_TDLS_PEER)) {
		struct rwnx_vif *rwnx_vif = rwnx_rx_get_vif(rwnx_hw, inst_nbr);
		if (!WARN_ON(rwnx_vif == NULL)) {
			req->tdls_sta = true;
			if ((params->ext_capab[3] & WLAN_EXT_CAPA4_TDLS_CHAN_SWITCH) && !rwnx_vif->tdls_chsw_prohibited)
				req->tdls_chsw_allowed = true;
			if (rwnx_vif->tdls_status == TDLS_SETUP_RSP_TX)
				req->tdls_sta_initiator = true;
		}
	}

	/* Send the ME_STA_ADD_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, ME_STA_ADD_CFM, cfm, sizeof(*cfm));
}

int rwnx_send_me_sta_del(struct rwnx_hw *rwnx_hw, u8 sta_idx, bool tdls_sta)
{
	struct me_sta_del_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_STA_DEL_REQ message */
	req = rwnx_msg_zalloc(ME_STA_DEL_REQ, TASK_ME, DRV_TASK_ID, sizeof(struct me_sta_del_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the MM_STA_DEL_REQ message */
	req->sta_idx = sta_idx;
	req->tdls_sta = tdls_sta;

	/* Send the ME_STA_DEL_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, ME_STA_DEL_CFM, NULL, 0);
}

int rwnx_send_me_traffic_ind(struct rwnx_hw *rwnx_hw, u8 sta_idx, bool uapsd, u8 tx_status)
{
	struct me_traffic_ind_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the ME_UTRAFFIC_IND_REQ message */
	req = rwnx_msg_zalloc(ME_TRAFFIC_IND_REQ, TASK_ME, DRV_TASK_ID, sizeof(struct me_traffic_ind_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the ME_TRAFFIC_IND_REQ message */
	req->sta_idx = sta_idx;
	req->tx_avail = tx_status;
	req->uapsd = uapsd;

	/* Send the ME_TRAFFIC_IND_REQ to UMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, ME_TRAFFIC_IND_CFM, NULL, 0);
}

int rwnx_send_twt_request(struct rwnx_hw *rwnx_hw, u8 setup_type, u8 vif_idx, u8 flow_id, struct twt_conf_tag *conf,
			  struct twt_setup_cfm *cfm)
{
	struct twt_setup_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the TWT_SETUP_REQ message */
	req = rwnx_msg_zalloc(TWT_SETUP_REQ, TASK_TWT, DRV_TASK_ID, sizeof(struct twt_setup_req));
	if (!req)
		return -ENOMEM;

	memcpy(&req->conf, conf, sizeof(req->conf));
	req->setup_type = setup_type;
	req->vif_idx = vif_idx;
	req->flow_id = flow_id;

	/* Send the TWT_SETUP_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, TWT_SETUP_CFM, cfm, sizeof(*cfm));
}

int rwnx_send_twt_teardown(struct rwnx_hw *rwnx_hw, struct twt_teardown_req *twt_teardown, struct twt_teardown_cfm *cfm)
{
	struct twt_teardown_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the TWT_TEARDOWN_REQ message */
	req = rwnx_msg_zalloc(TWT_TEARDOWN_REQ, TASK_TWT, DRV_TASK_ID, sizeof(struct twt_teardown_req));
	if (!req)
		return -ENOMEM;

	memcpy(req, twt_teardown, sizeof(struct twt_teardown_req));

	/* Send the TWT_TEARDOWN_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, TWT_TEARDOWN_CFM, cfm, sizeof(*cfm));
}

int rwnx_send_me_rc_stats(struct rwnx_hw *rwnx_hw, u8 sta_idx, struct me_rc_stats_cfm *cfm)
{
	struct me_rc_stats_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the ME_RC_STATS_REQ message */
	req = rwnx_msg_zalloc(ME_RC_STATS_REQ, TASK_ME, DRV_TASK_ID, sizeof(struct me_rc_stats_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the ME_RC_STATS_REQ message */
	req->sta_idx = sta_idx;

	/* Send the ME_RC_STATS_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, ME_RC_STATS_CFM, cfm, sizeof(*cfm));
}

int rwnx_send_me_rc_set_rate(struct rwnx_hw *rwnx_hw, u8 sta_idx, u16 rate_cfg)
{
	struct me_rc_set_rate_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the ME_RC_SET_RATE_REQ message */
	req = rwnx_msg_zalloc(ME_RC_SET_RATE_REQ, TASK_ME, DRV_TASK_ID, sizeof(struct me_rc_set_rate_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the ME_RC_SET_RATE_REQ message */
	req->sta_idx = sta_idx;
	req->fixed_rate_cfg = rate_cfg;

	/* Send the ME_RC_SET_RATE_REQ message to FW */
	return rwnx_send_msg(rwnx_hw, req, 0, 0, NULL, 0);
}

int rwnx_send_me_set_ps_mode(struct rwnx_hw *rwnx_hw, u8 ps_mode)
{
	struct me_set_ps_mode_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	bes_dbg(BES_DBG_MSG_TX, "%s %d.\n", __func__, ps_mode);
	/* Build the ME_SET_PS_MODE_REQ message */
	req = rwnx_msg_zalloc(ME_SET_PS_MODE_REQ, TASK_ME, DRV_TASK_ID, sizeof(struct me_set_ps_mode_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the ME_SET_PS_MODE_REQ message */
	req->ps_state = ps_mode;

	/* Send the ME_SET_PS_MODE_REQ message to FW */
	return rwnx_send_msg(rwnx_hw, req, 1, ME_SET_PS_MODE_CFM, NULL, 0);
}

int rwnx_send_11b_only_mode(struct rwnx_hw *rwnx_hw, u8 is_11b_only)
{
	struct mcu_11b_mode *req;

	bes_dbg(BES_DBG_MSG_TX, "%s is_11b_only = %d\n", __func__, is_11b_only);

	/* Build the IEEE80211b package message */
	req = rwnx_msg_zalloc(MCU_WIFI_IEEE80211B_RATE_REQ, TASK_MCU, DRV_TASK_ID, sizeof(struct mcu_11b_mode));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the IEEE80211b package message */
	req->ieee80211b_mode = is_11b_only;

	/* Send the IEEE80211b package message to FW */
	return rwnx_send_msg(rwnx_hw, req, 0, MCU_WIFI_IEEE80211B_RATE_REQ, NULL, 0);
}

#ifdef DUMP_FW_LOG_ENABLE
static int rwnx_send_fw_log_type_req(struct rwnx_hw *rwnx_hw, u8 type, enum fw_log_cmd_type cmd_type, u8 *cfm)
{
	struct mcu_dump_fw_log_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);
	/* Build the dump fw log message */
	req = rwnx_msg_zalloc(MCU_DUMP_FW_LOG_REQ, TASK_MCU, DRV_TASK_ID, sizeof(struct mcu_dump_fw_log_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the dump fw log message */
	req->cmd_type = cmd_type;
	req->type = type;

	/* Send the dump fw log message to FW */
	return rwnx_send_msg(rwnx_hw, req, 1, MCU_DUMP_FW_LOG_CFM, cfm, sizeof(*cfm));
}

int bes_send_dump_fw_log(struct rwnx_hw *rwnx_hw, u8 type)
{
	return rwnx_send_fw_log_type_req(rwnx_hw, type, DUMP_FW_LOG, NULL);
}

int bes_send_get_fw_log_type_req(struct rwnx_hw *rwnx_hw, u8 *cfm)
{
	return rwnx_send_fw_log_type_req(rwnx_hw, 0, GET_FW_LOG_TYPE, cfm);
}
#endif

/* Clear PMKID in connect req's RSN IE */
static void bes_clear_pmkid_in_connect_param(struct sm_connect_req *last_req, struct wpa_ie_data *sec, u8 *rsn_ie)
{
	u16_l pmkid_total_length = sec->num_pmkid * PMKID_LEN;

	if (sec->num_pmkid == 0)
		return;

	if (sec->pmkid + pmkid_total_length > (u8 *)last_req->ie_buf + last_req->ie_len) {
		bes_warn(BES_DBG_LINK, "%s invalid RSN ie\n", __func__);
		last_req->auth_type = WLAN_AUTH_OPEN;
		return;
	}

	/* Clear PMKID in RSN IE */
	memmove((u8 *)sec->pmkid, (u8 *)sec->pmkid + pmkid_total_length,
		last_req->ie_len - (sec->pmkid + pmkid_total_length - (u8 *)last_req->ie_buf));

	/* Set PMKID count to 0 */
	WPA_PUT_LE16((u8 *)sec->pmkid - 2, 0);

	/* Change RSN IE length */
	rsn_ie[1] -= pmkid_total_length;

	/* Change last_req's ie_len */
	memset((u8 *)last_req->ie_buf + last_req->ie_len - pmkid_total_length, 0,
	       pmkid_total_length);
	last_req->ie_len -= pmkid_total_length;
}

/* STA can not connect some specific WPA3-SAE AP by using OPEN auth type
 * with cached PMKID. When reconnecting in driver, changing auth type from
 * OPEN to SAE and deleting PMKID to improve the success rate of connecting. */
static void bes_check_reconnect_param(struct rwnx_vif *rwnx_vif)
{
	struct sm_connect_req *last_req = (struct sm_connect_req *)rwnx_vif->sta.connect_param;
	struct wpa_ie_data sec = {0};
	const u8 *rsn_ie = cfg80211_find_ie(WLAN_EID_RSN, (const u8 *)last_req->ie_buf, last_req->ie_len);

	if (rsn_ie == NULL) {
		return;
	}

	/* Change param if sta connect SAE ap by open auth type */
	if (last_req->auth_type != WLAN_AUTH_OPEN)
		return;

	wpa_parse_wpa_ie_rsn(rsn_ie, 2 + rsn_ie[1], &sec);
	bes_dbg(BES_DBG_LINK, "key_mgmt is 0x%x\n", sec.key_mgmt);

	/* Change param if sta connect SAE ap by open auth type */
	if ((sec.key_mgmt & WPA_KEY_MGMT_SAE) == 0)
		return;

	/* Change reconnect auth type from OPEN TO SAE */
	last_req->auth_type = WLAN_AUTH_SAE;

	/* Clear PMKID in connect req's RSN IE */
	bes_clear_pmkid_in_connect_param(last_req, &sec, (u8 *)rsn_ie);
	bes_info(BES_DBG_LINK, "vif %d, before reconnecting, change auth type to SAE and clear PMKID\n",
		 rwnx_vif->vif_index);
}

int rwnx_reconnect_req(struct rwnx_vif *rwnx_vif, struct sm_connect_ind *ind)
{
	uint16_t status_code = ind->status_code & 0xFFF;

	if (status_code == WLAN_STATUS_NOT_SUPPORTED_AUTH_ALG &&
		(rwnx_vif->sta.flags & (RWNX_STA_AUTO_AUTH | RWNX_STA_HAS_KEY))) {
		struct sm_connect_req *last_req = (struct sm_connect_req *)rwnx_vif->sta.connect_param;
		if (last_req->auth_type == WLAN_AUTH_OPEN) {
			struct sm_connect_cfm cfm = {0};
			int ret;
			u16 len = sizeof(struct sm_connect_req) + last_req->ie_len;
			struct sm_connect_req *req = rwnx_msg_zalloc(SM_CONNECT_REQ, TASK_SM, DRV_TASK_ID, len);
			if (!req)
				return -ENOMEM;

			// Don't call rwnx_send_sm_connect_req again before reconnection
			last_req->auth_type = WLAN_AUTH_SHARED_KEY;
			memcpy(req, last_req, len);
			bes_info(BES_DBG_LINK, "vif %d reconnect, try WLAN_AUTH_SHARED_KEY...\n", rwnx_vif->vif_index);
			ret = rwnx_send_msg(rwnx_vif->rwnx_hw, req, 1, SM_CONNECT_CFM, &cfm, sizeof(cfm));
			if (ret)
				return ret;

			return cfm.status;
		}
	} else if (status_code == WLAN_STATUS_ASSOC_REJECTED_TEMPORARILY) {
		u32 tu_timeout, ms_timeout;
		const u8 *req_ie, *rsp_ie;
		struct timeout_interval_ie *timeout_interval;

		/* Retrieve IE addresses and lengths */
		req_ie = (const u8 *)ind->assoc_ie_buf;
		rsp_ie = req_ie + ind->assoc_req_ie_len;

		timeout_interval = (struct timeout_interval_ie *)cfg80211_find_ie(WLAN_EID_TIMEOUT_INTERVAL,
			rsp_ie, ind->assoc_rsp_ie_len);

		if (timeout_interval && timeout_interval->timeout_interval_info.type == WLAN_TIMEOUT_ASSOC_COMEBACK) {
			tu_timeout = le32_to_cpu(timeout_interval->timeout_interval_info.value);
			ms_timeout = tu_timeout * 1024 / 1000;
			bes_info(BES_DBG_LINK, "AP rejected association temporarily, comeback duration %u TU (%u ms)\n",
				tu_timeout, ms_timeout);

			if (delayed_work_pending(&rwnx_vif->connect_retry_work) == 0) {
				/* Queue work */
				queue_delayed_work(rwnx_vif->rwnx_hw->rwnx_wq, &rwnx_vif->connect_retry_work,
					ms_timeout * HZ / 1000);
			}
			return 0;
		}
	} else if (status_code == WLAN_STATUS_UNSPECIFIED_FAILURE || status_code == WLAN_STATUS_INVALID_PMKID) {
		if (rwnx_vif->sta.retry_cnt < STA_CONN_RETRY_CNT) {
			struct sm_connect_req *last_req = (struct sm_connect_req *)rwnx_vif->sta.connect_param;
			struct sm_connect_cfm cfm = {0};
			int ret = 0;
			u16 len = 0;
			struct sm_connect_req *req = NULL;

			/* STA can not connect some specific WPA3-SAE AP by using OPEN auth type
			 * with cached PMKID. Check these cases and change these reconnect param. */
			bes_check_reconnect_param(rwnx_vif);

			len = sizeof(struct sm_connect_req) + last_req->ie_len;
			req = rwnx_msg_zalloc(SM_CONNECT_REQ, TASK_SM, DRV_TASK_ID, len);
			if (!req)
				return -ENOMEM;

			memcpy(req, last_req, len);
			rwnx_vif->sta.retry_cnt++;
			bes_info(BES_DBG_LINK, "vif %d connect retry:%d\n", rwnx_vif->vif_index, rwnx_vif->sta.retry_cnt);
			ret = rwnx_send_msg(rwnx_vif->rwnx_hw, req, 1, SM_CONNECT_CFM, &cfm, sizeof(cfm));
			if (ret)
				return ret;

			return cfm.status;
		}
		bes_info(BES_DBG_LINK, "vif %d stop reconnect as connect retry reach max num\n", rwnx_vif->vif_index);
	}

	return -ENOTSUPP;
}

void rwnx_connect_retry_work_func(struct work_struct *work)
{
	struct rwnx_vif *rwnx_vif = container_of(work, struct rwnx_vif, connect_retry_work.work);
	struct sm_connect_req *last_req = (struct sm_connect_req *)rwnx_vif->sta.connect_param;
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	struct sm_connect_cfm cfm = {0};

	u16 len = sizeof(struct sm_connect_req) + last_req->ie_len;
	struct sm_connect_req *req = rwnx_msg_zalloc(SM_CONNECT_REQ, TASK_SM, DRV_TASK_ID, len);
	if (!req) {
		bes_err(BES_DBG_MSG_TX, "%s zalloc for SM_CONNECT_REQ failed\n", __func__);
		return;
	}

	// Don't call rwnx_send_sm_connect_req again before reconnection
	memcpy(req, last_req, len);
	rwnx_send_msg(rwnx_hw, req, 1, SM_CONNECT_CFM, &cfm, sizeof(cfm));
}

int rwnx_send_sm_connect_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, struct cfg80211_connect_params *sme,
			     struct sm_connect_cfm *cfm)
{
	struct sm_connect_req *req;
	int i, ie_len;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	ie_len = update_connect_req(rwnx_vif, sme);
	bes_dbg(BES_DBG_MSG_TX, "%s %d.\n", __func__, __LINE__);
	/* Build the SM_CONNECT_REQ message */
	req = rwnx_msg_zalloc(SM_CONNECT_REQ, TASK_SM, DRV_TASK_ID, (sizeof(struct sm_connect_req) + ie_len));

	if (!req)
		return -ENOMEM;

	/* Set parameters for the SM_CONNECT_REQ message */
	if (sme->crypto.n_ciphers_pairwise && ((sme->crypto.ciphers_pairwise[0] == WLAN_CIPHER_SUITE_WEP40) ||
					       (sme->crypto.ciphers_pairwise[0] == WLAN_CIPHER_SUITE_TKIP) ||
					       (sme->crypto.ciphers_pairwise[0] == WLAN_CIPHER_SUITE_WEP104)))
		req->flags |= DISABLE_HT;

	if (sme->crypto.control_port)
		req->flags |= CONTROL_PORT_HOST;

	if (sme->crypto.control_port_no_encrypt)
		req->flags |= CONTROL_PORT_NO_ENC;

	if (use_pairwise_key(&sme->crypto))
		req->flags |= WPA_WPA2_IN_USE;

	if (sme->mfp == NL80211_MFP_REQUIRED)
		req->flags |= MFP_IN_USE;

	req->ctrl_port_ethertype = sme->crypto.control_port_ethertype;

	if (sme->bssid)
		memcpy(&req->bssid, sme->bssid, ETH_ALEN);
	else
		req->bssid = mac_addr_bcst;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0)
	if (sme->prev_bssid)
		req->flags |= REASSOCIATION;
#else
	if (rwnx_vif->sta.ap)
		req->flags |= REASSOCIATION;
#endif

	if ((sme->auth_type == NL80211_AUTHTYPE_FT) && (rwnx_vif->sta.flags & RWNX_STA_FT_OVER_DS))
		req->flags |= (REASSOCIATION | FT_OVER_DS);

	req->vif_idx = rwnx_vif->vif_index;
	if (sme->channel) {
		req->chan.band = sme->channel->band;
		req->chan.freq = sme->channel->center_freq;
		req->chan.flags = get_chan_flags(sme->channel->flags);
	} else {
		req->chan.freq = (u16_l)-1;
	}
	for (i = 0; i < sme->ssid_len; i++)
		req->ssid.array[i] = sme->ssid[i];
	req->ssid.length = sme->ssid_len;

	req->listen_interval = rwnx_mod_params.listen_itv;
	req->dont_wait_bcmc = !rwnx_mod_params.listen_bcmc;

	/* Set auth_type */
	if (sme->auth_type == NL80211_AUTHTYPE_AUTOMATIC)
		req->auth_type = WLAN_AUTH_OPEN;
	else if (sme->auth_type == NL80211_AUTHTYPE_OPEN_SYSTEM)
		req->auth_type = WLAN_AUTH_OPEN;
	else if (sme->auth_type == NL80211_AUTHTYPE_SHARED_KEY)
		req->auth_type = WLAN_AUTH_SHARED_KEY;
	else if (sme->auth_type == NL80211_AUTHTYPE_FT)
		req->auth_type = WLAN_AUTH_FT;
	else if (sme->auth_type == NL80211_AUTHTYPE_SAE)
		req->auth_type = WLAN_AUTH_SAE;
	else
		goto invalid_param;

	copy_connect_ies(rwnx_vif, req, sme);

	/* Set UAPSD queues */
	req->uapsd_queues = rwnx_mod_params.uapsd_queues;
	bes_info(BES_DBG_LINK, "CONNECT_REQ: " TR_VIF_STR ", bssid " TR_MAC_STR ", ssid %s, auth_type %d, flags 0x%x\n",
		TR_VIF(rwnx_vif), TR_MAC(&req->bssid), TR_SSID(&req->ssid),
		req->auth_type, req->flags);

	// save connect_param for reconnection
	memcpy(rwnx_vif->sta.connect_param, req, sizeof(struct sm_connect_req) + ie_len);
	/* Send the SM_CONNECT_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, SM_CONNECT_CFM, cfm, sizeof(*cfm));

invalid_param:
	rwnx_msg_free(rwnx_hw, req);
	return -EINVAL;
}

int rwnx_send_sm_disconnect_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, u16 reason)
{
	struct sm_disconnect_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the SM_DISCONNECT_REQ message */
	req = rwnx_msg_zalloc(SM_DISCONNECT_REQ, TASK_SM, DRV_TASK_ID, sizeof(struct sm_disconnect_req));
	bes_dbg(BES_DBG_MSG_TX, "%s,req=%d,rea=%d\n", __func__, (req == NULL) ? 0 : 1, reason);
	if (!req)
		return -ENOMEM;

	/* Set parameters for the SM_DISCONNECT_REQ message */
	req->reason_code = reason;
	req->vif_idx = rwnx_vif->vif_index;
	bes_info(BES_DBG_LINK, "DISCONNECT_REQ: " TR_VIF_STR ", reason %d\n",
		TR_VIF(rwnx_vif), reason);

	/* Send the SM_DISCONNECT_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, SM_DISCONNECT_CFM, NULL, 0);
}

int rwnx_send_sm_external_auth_required_rsp(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, u16 status)
{
	struct sm_external_auth_required_rsp *rsp;

	/* Build the SM_EXTERNAL_AUTH_CFM message */
	rsp = rwnx_msg_zalloc(SM_EXTERNAL_AUTH_REQUIRED_RSP, TASK_SM, DRV_TASK_ID,
			      sizeof(struct sm_external_auth_required_rsp));
	if (!rsp)
		return -ENOMEM;

	rsp->status = status;
	rsp->vif_idx = rwnx_vif->vif_index;

	/* send the SM_EXTERNAL_AUTH_REQUIRED_RSP message UMAC FW */
	return rwnx_send_msg(rwnx_hw, rsp, 0, 0, NULL, 0);
}

int rwnx_send_sm_ft_auth_rsp(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, uint8_t *ie, int ie_len)
{
	struct sm_connect_req *rsp;

	rsp = rwnx_msg_zalloc(SM_FT_AUTH_RSP, TASK_SM, DRV_TASK_ID, (sizeof(struct sm_connect_req) + ie_len));
	if (!rsp)
		return -ENOMEM;

	rsp->vif_idx = rwnx_vif->vif_index;
	rsp->ie_len = ie_len;
	memcpy(rsp->ie_buf, ie, rsp->ie_len);

	return rwnx_send_msg(rwnx_hw, rsp, 0, 0, NULL, 0);
}

int rwnx_send_apm_start_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, struct cfg80211_ap_settings *settings,
			    struct apm_start_cfm *cfm)
{
	struct apm_start_req *req;
	struct rwnx_bcn *bcn = &vif->ap.bcn;
	struct rwnx_ipc_buf buf;
	u8 *bcn_buf;
	u32 flags = 0;
	const u8 *rate_ie;
	u8 rate_len = 0;
	int var_offset = offsetof(struct ieee80211_mgmt, u.beacon.variable);
	const u8 *var_pos;
	int len, i, error;
	size_t payload_len = 0;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	// Build the beacon
	bcn->dtim = (u8)settings->dtim_period;
	bcn_buf = rwnx_build_bcn(bcn, &settings->beacon);
	if (!bcn_buf) {
		return -ENOMEM;
	}
#if !defined(CONFIG_RWNX_PCIE) && !defined(CONFIG_BES_IBUS)
	payload_len = bcn->len;
#endif
	/* Build the APM_START_REQ message */
	req = rwnx_msg_zalloc(APM_START_REQ, TASK_APM, DRV_TASK_ID, sizeof(struct apm_start_req) + payload_len);
	if (!req) {
		// free bcn_buf
		kfree(bcn_buf);
		return -ENOMEM;
	}

	// Retrieve the basic rate set from the beacon buffer
	len = bcn->len - var_offset;
	var_pos = bcn_buf + var_offset;

// Assume that rate higher that 54 Mbps are BSS membership
#define IS_BASIC_RATE(r) (r & 0x80) && ((r & ~0x80) <= (54 * 2))

	rate_ie = cfg80211_find_ie(WLAN_EID_SUPP_RATES, var_pos, len);
	if (rate_ie) {
		const u8 *rates = rate_ie + 2;
		for (i = 0; (i < rate_ie[1]) && (rate_len < MAC_RATESET_LEN); i++) {
			if (IS_BASIC_RATE(rates[i]))
				req->basic_rates.array[rate_len++] = rates[i];
		}
	}
	rate_ie = cfg80211_find_ie(WLAN_EID_EXT_SUPP_RATES, var_pos, len);
	if (rate_ie) {
		const u8 *rates = rate_ie + 2;
		for (i = 0; (i < rate_ie[1]) && (rate_len < MAC_RATESET_LEN); i++) {
			if (IS_BASIC_RATE(rates[i]))
				req->basic_rates.array[rate_len++] = rates[i];
		}
	}
	req->basic_rates.length = rate_len;
#undef IS_BASIC_RATE

	// Sync buffer for FW
	if ((error = rwnx_ipc_buf_a2e_init(rwnx_hw, &buf, bcn_buf, bcn->len))) {
		netdev_err(vif->ndev, "Failed to allocate IPC buf for AP Beacon\n");
		kfree(bcn_buf);
		return -EIO;
	}
#if !defined(CONFIG_RWNX_PCIE) && !defined(CONFIG_BES_IBUS)
	memcpy(req->payload, bcn_buf, payload_len);
#endif
	/* Set parameters for the APM_START_REQ message */
	req->vif_idx = vif->vif_index;
	req->bcn_addr = buf.dma_addr;
	req->bcn_len = bcn->len;
	req->tim_oft = bcn->head_len;
	req->tim_len = bcn->tim_len;
	cfg80211_to_rwnx_chan(&settings->chandef, &req->chan);
	req->bcn_int = settings->beacon_interval;
	if (settings->crypto.control_port)
		flags |= CONTROL_PORT_HOST;

	if (settings->crypto.control_port_no_encrypt)
		flags |= CONTROL_PORT_NO_ENC;

	if (use_pairwise_key(&settings->crypto))
		flags |= WPA_WPA2_IN_USE;

	if (settings->crypto.control_port_ethertype)
		req->ctrl_port_ethertype = settings->crypto.control_port_ethertype;
	else
		req->ctrl_port_ethertype = ETH_P_PAE;
	req->flags = flags;

	/* Send the APM_START_REQ message to LMAC FW */
	error = rwnx_send_msg(rwnx_hw, req, 1, APM_START_CFM, cfm, sizeof(*cfm));

	rwnx_ipc_buf_dealloc(rwnx_hw, &buf);

	bes_dbg(BES_DBG_MSG_TX, "%s, ret=%d\n", __func__, error);
	return error;
}

int rwnx_send_apm_stop_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif)
{
	struct apm_stop_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the APM_STOP_REQ message */
	req = rwnx_msg_zalloc(APM_STOP_REQ, TASK_APM, DRV_TASK_ID, sizeof(struct apm_stop_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the APM_STOP_REQ message */
	req->vif_idx = vif->vif_index;

	/* Send the APM_STOP_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, APM_STOP_CFM, NULL, 0);
}

int rwnx_send_apm_probe_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, struct rwnx_sta *sta,
			    struct apm_probe_client_cfm *cfm)
{
	struct apm_probe_client_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	req = rwnx_msg_zalloc(APM_PROBE_CLIENT_REQ, TASK_APM, DRV_TASK_ID, sizeof(struct apm_probe_client_req));
	if (!req)
		return -ENOMEM;

	req->vif_idx = vif->vif_index;
	req->sta_idx = sta->sta_idx;

	/* Send the APM_PROBE_CLIENT_REQ message to UMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, APM_PROBE_CLIENT_CFM, cfm, sizeof(*cfm));
}

int rwnx_send_scanu_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, struct cfg80211_scan_request *param)
{
	struct scanu_start_req *req;
	int i;
	uint8_t chan_flags = 0;
	size_t payload_len = 0;

	bes_dbg(BES_DBG_MSG_TX, "%s len:%zu.\n", __func__, param->ie_len);
#if !defined(CONFIG_RWNX_PCIE) && !defined(CONFIG_BES_IBUS)
	payload_len = param->ie_len;
#endif

	/* Build the SCANU_START_REQ message */
	req = rwnx_msg_zalloc(SCANU_START_REQ, TASK_SCANU, DRV_TASK_ID, sizeof(struct scanu_start_req) + payload_len);
	if (!req)
		return -ENOMEM;

	/* Set parameters */
	req->vif_idx = rwnx_vif->vif_index;
	req->chan_cnt = (u8)min_t(int, SCAN_CHANNEL_MAX, param->n_channels);
	req->ssid_cnt = (u8)min_t(int, SCAN_SSID_MAX, param->n_ssids);
	req->bssid = mac_addr_bcst;
	req->no_cck = param->no_cck;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
	/* set active duration */
	if (param->duration_mandatory) {
		req->duration = ieee80211_tu_to_usec(param->duration);
	} else {
#ifdef CONFIG_OPLUS_PLATFORM
		if (rwnx_vif->sta.ap != NULL)
			req->duration = OPLUS_CONNECT_SCAN_DUR;
		else
#endif		
			req->duration = rwnx_hw->conf_file.scan_active_dur;
		req->ext.passive_duration = rwnx_hw->conf_file.scan_passive_dur;
	}
#else
#ifdef CONFIG_OPLUS_PLATFORM
	if (rwnx_vif->sta.ap != NULL)
		req->duration = OPLUS_CONNECT_SCAN_DUR;
	else
#endif
		req->duration = rwnx_hw->conf_file.scan_active_dur;
	req->ext.passive_duration = rwnx_hw->conf_file.scan_passive_dur;
#endif

	/* Use scan extend param to send probe request twice */
	req->ext.probe_interval_us = NX_SCAN_PROBE_INTERVAL;
	req->ext.max_probes        = NX_SCAN_PROBE_MAX_CNT;
	bes_info(BES_DBG_MSG_TX, "%s probe interval(us):%d probe max cnt:%d duration:%d.\n",
		__func__, req->ext.probe_interval_us, req->ext.max_probes, req->duration);

	if (req->ssid_cnt == 0)
		chan_flags |= CHAN_NO_IR;
	for (i = 0; i < req->ssid_cnt; i++) {
		int j;
		for (j = 0; j < param->ssids[i].ssid_len; j++)
			req->ssid[i].array[j] = param->ssids[i].ssid[j];
		req->ssid[i].length = param->ssids[i].ssid_len;
	}

	if (param->ie) {
#if defined(CONFIG_RWNX_PCIE) || defined(CONFIG_BES_IBUS)
		if (rwnx_ipc_buf_a2e_alloc(rwnx_hw, &rwnx_hw->scan_ie, param->ie_len, param->ie)) {
			netdev_err(rwnx_vif->ndev, "Failed to allocate IPC buf for SCAN IEs\n");
			goto error;
		}
#else
		memcpy(req->payload, param->ie, payload_len);
#endif
		req->add_ie_len = param->ie_len;
		req->add_ies = rwnx_hw->scan_ie.dma_addr;
	} else {
		req->add_ie_len = 0;
		req->add_ies = 0;
	}

	for (i = 0; i < req->chan_cnt; i++) {
		struct ieee80211_channel *chan = param->channels[i];

		req->chan[i].band = chan->band;
		req->chan[i].freq = chan->center_freq;
		req->chan[i].flags = chan_flags | get_chan_flags(chan->flags);
		req->chan[i].tx_power = chan_to_fw_pwr(chan->max_reg_power);
	}

	/* Send the SCANU_START_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 0, 0, NULL, 0);

#if defined(CONFIG_RWNX_PCIE) || defined(CONFIG_BES_IBUS)
error:
	if (req != NULL)
		rwnx_msg_free(rwnx_hw, req);
	return -ENOMEM;
#endif
}

int rwnx_send_apm_start_cac_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, struct cfg80211_chan_def *chandef,
				struct apm_start_cac_cfm *cfm)
{
	struct apm_start_cac_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the APM_START_CAC_REQ message */
	req = rwnx_msg_zalloc(APM_START_CAC_REQ, TASK_APM, DRV_TASK_ID, sizeof(struct apm_start_cac_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the APM_START_CAC_REQ message */
	req->vif_idx = vif->vif_index;
	cfg80211_to_rwnx_chan(chandef, &req->chan);

	/* Send the APM_START_CAC_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, APM_START_CAC_CFM, cfm, sizeof(*cfm));
}

int rwnx_send_apm_stop_cac_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif)
{
	struct apm_stop_cac_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the APM_STOP_CAC_REQ message */
	req = rwnx_msg_zalloc(APM_STOP_CAC_REQ, TASK_APM, DRV_TASK_ID, sizeof(struct apm_stop_cac_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the APM_STOP_CAC_REQ message */
	req->vif_idx = vif->vif_index;

	/* Send the APM_STOP_CAC_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, APM_STOP_CAC_CFM, NULL, 0);
}

int rwnx_send_mesh_start_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, const struct mesh_config *conf,
			     const struct mesh_setup *setup, struct mesh_start_cfm *cfm)
{
	// Message to send
	struct mesh_start_req *req;
	// Supported basic rates
	struct ieee80211_supported_band *band = rwnx_hw->wiphy->bands[setup->chandef.chan->band];
	/* Counter */
	int i;
	/* Return status */
	int status;
	/* DMA Address to be unmapped after confirmation reception */
	u32 dma_addr = 0;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MESH_START_REQ message */
	req = rwnx_msg_zalloc(MESH_START_REQ, TASK_MESH, DRV_TASK_ID, sizeof(struct mesh_start_req));
	if (!req) {
		return -ENOMEM;
	}

	req->vif_index = vif->vif_index;
	req->bcn_int = setup->beacon_interval;
	req->dtim_period = setup->dtim_period;
	req->mesh_id_len = setup->mesh_id_len;

	for (i = 0; i < setup->mesh_id_len; i++) {
		req->mesh_id[i] = *(setup->mesh_id + i);
	}

	req->user_mpm = setup->user_mpm;
	req->is_auth = setup->is_authenticated;
	req->auth_id = setup->auth_id;
	req->ie_len = setup->ie_len;

	if (setup->ie_len) {
		/*
		 * Need to provide a Virtual Address to the MAC so that it can download the
		 * additional information elements.
		 */
		req->ie_addr = dma_map_single(rwnx_hw->dev, (void *)setup->ie, setup->ie_len, DMA_FROM_DEVICE);

		/* Check DMA mapping result */
		if (dma_mapping_error(rwnx_hw->dev, req->ie_addr)) {
			bes_err(BES_DBG_MSG_TX, "%s - DMA Mapping error on additional IEs\n", __func__);

			/* Consider there is no Additional IEs */
			req->ie_len = 0;
		} else {
			/* Store DMA Address so that we can unmap the memory section once MESH_START_CFM is received */
			dma_addr = req->ie_addr;
		}
	}

	/* Provide rate information */
	req->basic_rates.length = 0;
	for (i = 0; i < band->n_bitrates; i++) {
		u16 rate = band->bitrates[i].bitrate;

		/* Read value is in in units of 100 Kbps, provided value is in units
		 * of 1Mbps, and multiplied by 2 so that 5.5 becomes 11 */
		rate = (rate << 1) / 10;

		if (setup->basic_rates & CO_BIT(i)) {
			rate |= 0x80;
		}

		req->basic_rates.array[i] = (u8)rate;
		req->basic_rates.length++;
	}

	/* Provide channel information */
	cfg80211_to_rwnx_chan(&setup->chandef, &req->chan);

	/* Send the MESH_START_REQ message to UMAC FW */
	status = rwnx_send_msg(rwnx_hw, req, 1, MESH_START_CFM, cfm, sizeof(*cfm));

	/* Unmap DMA area */
	if (setup->ie_len) {
		dma_unmap_single(rwnx_hw->dev, dma_addr, setup->ie_len, DMA_TO_DEVICE);
	}

	/* Return the status */
	return (status);
}

int rwnx_send_mesh_stop_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, struct mesh_stop_cfm *cfm)
{
	// Message to send
	struct mesh_stop_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MESH_STOP_REQ message */
	req = rwnx_msg_zalloc(MESH_STOP_REQ, TASK_MESH, DRV_TASK_ID, sizeof(struct mesh_stop_req));
	if (!req) {
		return -ENOMEM;
	}

	req->vif_idx = vif->vif_index;

	/* Send the MESH_STOP_REQ message to UMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, MESH_STOP_CFM, cfm, sizeof(*cfm));
}

int rwnx_send_mesh_update_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, u32 mask,
			      const struct mesh_config *p_mconf, struct mesh_update_cfm *cfm)
{
	// Message to send
	struct mesh_update_req *req;
	// Keep only bit for fields which can be updated
	u32 supp_mask =
		(mask << 1) & (CO_BIT(NL80211_MESHCONF_GATE_ANNOUNCEMENTS) | CO_BIT(NL80211_MESHCONF_HWMP_ROOTMODE) |
			       CO_BIT(NL80211_MESHCONF_FORWARDING) | CO_BIT(NL80211_MESHCONF_POWER_MODE));

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	if (!supp_mask) {
		return -ENOENT;
	}

	/* Build the MESH_UPDATE_REQ message */
	req = rwnx_msg_zalloc(MESH_UPDATE_REQ, TASK_MESH, DRV_TASK_ID, sizeof(struct mesh_update_req));

	if (!req) {
		return -ENOMEM;
	}

	req->vif_idx = vif->vif_index;

	if (supp_mask & CO_BIT(NL80211_MESHCONF_GATE_ANNOUNCEMENTS)) {
		req->flags |= CO_BIT(MESH_UPDATE_FLAGS_GATE_MODE_BIT);
		req->gate_announ = p_mconf->dot11MeshGateAnnouncementProtocol;
	}

	if (supp_mask & CO_BIT(NL80211_MESHCONF_HWMP_ROOTMODE)) {
		req->flags |= CO_BIT(MESH_UPDATE_FLAGS_ROOT_MODE_BIT);
		req->root_mode = p_mconf->dot11MeshHWMPRootMode;
	}

	if (supp_mask & CO_BIT(NL80211_MESHCONF_FORWARDING)) {
		req->flags |= CO_BIT(MESH_UPDATE_FLAGS_MESH_FWD_BIT);
		req->mesh_forward = p_mconf->dot11MeshForwarding;
	}

	if (supp_mask & CO_BIT(NL80211_MESHCONF_POWER_MODE)) {
		req->flags |= CO_BIT(MESH_UPDATE_FLAGS_LOCAL_PSM_BIT);
		req->local_ps_mode = p_mconf->power_mode;
	}

	/* Send the MESH_UPDATE_REQ message to UMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, MESH_UPDATE_CFM, cfm, sizeof(*cfm));
}

int rwnx_send_mesh_peer_info_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, u8 sta_idx,
				 struct mesh_peer_info_cfm *cfm)
{
	// Message to send
	struct mesh_peer_info_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MESH_PEER_INFO_REQ message */
	req = rwnx_msg_zalloc(MESH_PEER_INFO_REQ, TASK_MESH, DRV_TASK_ID, sizeof(struct mesh_peer_info_req));
	if (!req) {
		return -ENOMEM;
	}

	req->sta_idx = sta_idx;

	/* Send the MESH_PEER_INFO_REQ message to UMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, MESH_PEER_INFO_CFM, cfm, sizeof(*cfm));
}

void rwnx_send_mesh_peer_update_ntf(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, u8 sta_idx, u8 mlink_state)
{
	// Message to send
	struct mesh_peer_update_ntf *ntf;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MESH_PEER_UPDATE_NTF message */
	ntf = rwnx_msg_zalloc(MESH_PEER_UPDATE_NTF, TASK_MESH, DRV_TASK_ID, sizeof(struct mesh_peer_update_ntf));

	if (ntf) {
		ntf->vif_idx = vif->vif_index;
		ntf->sta_idx = sta_idx;
		ntf->state = mlink_state;

		/* Send the MESH_PEER_INFO_REQ message to UMAC FW */
		rwnx_send_msg(rwnx_hw, ntf, 0, 0, NULL, 0);
	}
}

void rwnx_send_mesh_path_create_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, u8 *tgt_addr)
{
	struct mesh_path_create_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Check if we are already waiting for a confirmation */
	if (vif->ap.flags & RWNX_AP_CREATE_MESH_PATH)
		return;

	/* Build the MESH_PATH_CREATE_REQ message */
	req = rwnx_msg_zalloc(MESH_PATH_CREATE_REQ, TASK_MESH, DRV_TASK_ID, sizeof(struct mesh_path_create_req));
	if (!req)
		return;

	req->vif_idx = vif->vif_index;
	memcpy(&req->tgt_mac_addr, tgt_addr, ETH_ALEN);

	vif->ap.flags |= RWNX_AP_CREATE_MESH_PATH;

	/* Send the MESH_PATH_CREATE_REQ message to UMAC FW */
	rwnx_send_msg(rwnx_hw, req, 0, 0, NULL, 0);
}

int rwnx_send_mesh_path_update_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, const u8 *tgt_addr,
				   const u8 *p_nhop_addr, struct mesh_path_update_cfm *cfm)
{
	// Message to send
	struct mesh_path_update_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MESH_PATH_UPDATE_REQ message */
	req = rwnx_msg_zalloc(MESH_PATH_UPDATE_REQ, TASK_MESH, DRV_TASK_ID, sizeof(struct mesh_path_update_req));
	if (!req) {
		return -ENOMEM;
	}

	req->delete = (p_nhop_addr == NULL);
	req->vif_idx = vif->vif_index;
	memcpy(&req->tgt_mac_addr, tgt_addr, ETH_ALEN);

	if (p_nhop_addr) {
		memcpy(&req->nhop_mac_addr, p_nhop_addr, ETH_ALEN);
	}

	/* Send the MESH_PATH_UPDATE_REQ message to UMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, MESH_PATH_UPDATE_CFM, cfm, sizeof(*cfm));
}

void rwnx_send_mesh_proxy_add_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif, u8 *ext_addr)
{
	// Message to send
	struct mesh_proxy_add_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MESH_PROXY_ADD_REQ message */
	req = rwnx_msg_zalloc(MESH_PROXY_ADD_REQ, TASK_MESH, DRV_TASK_ID, sizeof(struct mesh_proxy_add_req));

	if (req) {
		req->vif_idx = vif->vif_index;
		memcpy(&req->ext_sta_addr, ext_addr, ETH_ALEN);

		/* Send the MESH_PROXY_ADD_REQ message to UMAC FW */
		rwnx_send_msg(rwnx_hw, req, 0, 0, NULL, 0);
	}
}

int rwnx_send_tdls_peer_traffic_ind_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif)
{
	struct tdls_peer_traffic_ind_req *tdls_peer_traffic_ind_req;

	if (!rwnx_vif->sta.tdls_sta)
		return -ENOLINK;

	/* Build the TDLS_PEER_TRAFFIC_IND_REQ message */
	tdls_peer_traffic_ind_req = rwnx_msg_zalloc(TDLS_PEER_TRAFFIC_IND_REQ, TASK_TDLS, DRV_TASK_ID,
						    sizeof(struct tdls_peer_traffic_ind_req));

	if (!tdls_peer_traffic_ind_req)
		return -ENOMEM;

	/* Set parameters for the TDLS_PEER_TRAFFIC_IND_REQ message */
	tdls_peer_traffic_ind_req->vif_index = rwnx_vif->vif_index;
	tdls_peer_traffic_ind_req->sta_idx = rwnx_vif->sta.tdls_sta->sta_idx;
	memcpy(&(tdls_peer_traffic_ind_req->peer_mac_addr.array[0]), rwnx_vif->sta.tdls_sta->mac_addr, ETH_ALEN);
	tdls_peer_traffic_ind_req->dialog_token = 0; // check dialog token value
	tdls_peer_traffic_ind_req->last_tid = rwnx_vif->sta.tdls_sta->tdls.last_tid;
	tdls_peer_traffic_ind_req->last_sn = rwnx_vif->sta.tdls_sta->tdls.last_sn;

	/* Send the TDLS_PEER_TRAFFIC_IND_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, tdls_peer_traffic_ind_req, 0, 0, NULL, 0);
}

int rwnx_send_config_monitor_req(struct rwnx_hw *rwnx_hw, struct cfg80211_chan_def *chandef,
				 struct me_config_monitor_cfm *cfm)
{
	struct me_config_monitor_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the ME_CONFIG_MONITOR_REQ message */
	req = rwnx_msg_zalloc(ME_CONFIG_MONITOR_REQ, TASK_ME, DRV_TASK_ID, sizeof(struct me_config_monitor_req));
	if (!req)
		return -ENOMEM;

	if (chandef) {
		req->chan_set = true;
		cfg80211_to_rwnx_chan(chandef, &req->chan);

		if (rwnx_hw->phy.limit_bw)
			limit_chan_bw(&req->chan.type, req->chan.prim20_freq, &req->chan.center1_freq);
	} else {
		req->chan_set = false;
	}

	req->uf = rwnx_hw->mod_params->uf;

	/* Send the ME_CONFIG_MONITOR_REQ message to FW */
	return rwnx_send_msg(rwnx_hw, req, 1, ME_CONFIG_MONITOR_CFM, cfm, sizeof(*cfm));
}
#endif /* CONFIG_RWNX_FULLMAC */

int rwnx_send_tdls_chan_switch_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, struct rwnx_sta *rwnx_sta,
				   bool sta_initiator, u8 oper_class, struct cfg80211_chan_def *chandef,
				   struct tdls_chan_switch_cfm *cfm)
{
	struct tdls_chan_switch_req *tdls_chan_switch_req;

#ifdef CONFIG_RWNX_SOFTMAC
	/* Check if channel switch already enabled on a TDLS peer */
	if (rwnx_hw->tdls_info.chsw_en) {
		bes_err(BES_DBG_MSG_TX, "TDLS channel switch already enabled for another TDLS station\n");
		return -ENOTSUPP;
	}
#endif

	/* Build the TDLS_CHAN_SWITCH_REQ message */
	tdls_chan_switch_req =
		rwnx_msg_zalloc(TDLS_CHAN_SWITCH_REQ, TASK_TDLS, DRV_TASK_ID, sizeof(struct tdls_chan_switch_req));

	if (!tdls_chan_switch_req)
		return -ENOMEM;

	/* Set parameters for the TDLS_CHAN_SWITCH_REQ message */
	tdls_chan_switch_req->vif_index = rwnx_vif->vif_index;
	tdls_chan_switch_req->sta_idx = rwnx_sta->sta_idx;
	memcpy(&(tdls_chan_switch_req->peer_mac_addr.array[0]), rwnx_sta_addr(rwnx_sta), ETH_ALEN);
	tdls_chan_switch_req->initiator = sta_initiator;
	cfg80211_to_rwnx_chan(chandef, &tdls_chan_switch_req->chan);
	tdls_chan_switch_req->op_class = oper_class;

	/* Send the TDLS_CHAN_SWITCH_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, tdls_chan_switch_req, 1, TDLS_CHAN_SWITCH_CFM, cfm, sizeof(*cfm));
}

int rwnx_send_tdls_cancel_chan_switch_req(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, struct rwnx_sta *rwnx_sta,
					  struct tdls_cancel_chan_switch_cfm *cfm)
{
	struct tdls_cancel_chan_switch_req *tdls_cancel_chan_switch_req;

	/* Build the TDLS_CHAN_SWITCH_REQ message */
	tdls_cancel_chan_switch_req = rwnx_msg_zalloc(TDLS_CANCEL_CHAN_SWITCH_REQ, TASK_TDLS, DRV_TASK_ID,
						      sizeof(struct tdls_cancel_chan_switch_req));
	if (!tdls_cancel_chan_switch_req)
		return -ENOMEM;

	/* Set parameters for the TDLS_CHAN_SWITCH_REQ message */
	tdls_cancel_chan_switch_req->vif_index = rwnx_vif->vif_index;
	tdls_cancel_chan_switch_req->sta_idx = rwnx_sta->sta_idx;
	memcpy(&(tdls_cancel_chan_switch_req->peer_mac_addr.array[0]), rwnx_sta_addr(rwnx_sta), ETH_ALEN);

	/* Send the TDLS_CHAN_SWITCH_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, tdls_cancel_chan_switch_req, 1, TDLS_CANCEL_CHAN_SWITCH_CFM, cfm, sizeof(*cfm));
}

#ifdef CONFIG_RWNX_BFMER
#ifdef CONFIG_RWNX_SOFTMAC
void rwnx_send_bfmer_enable(struct rwnx_hw *rwnx_hw, struct ieee80211_sta *sta)
#else
void rwnx_send_bfmer_enable(struct rwnx_hw *rwnx_hw, struct rwnx_sta *rwnx_sta, const struct ieee80211_vht_cap *vht_cap)
#endif /* CONFIG_RWNX_SOFTMAC*/
{
	struct mm_bfmer_enable_req *bfmer_en_req;
#ifdef CONFIG_RWNX_SOFTMAC
	struct rwnx_sta *rwnx_sta = (struct rwnx_sta *)&sta->drv_priv;
	u32 vht_capability;
#else
	__le32 vht_capability;
	u8 rx_nss = 0;
#endif /* CONFIG_RWNX_SOFTMAC */

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

#ifdef CONFIG_RWNX_SOFTMAC
	if (!sta->vht_cap.vht_supported) {
#else
	if (!vht_cap) {
#endif /* CONFIG_RWNX_SOFTMAC */
		goto end;
	}

#ifdef CONFIG_RWNX_SOFTMAC
	vht_capability = sta->vht_cap.cap;
#else
	vht_capability = vht_cap->vht_cap_info;
#endif /* CONFIG_RWNX_SOFTMAC */

	if (!(vht_capability & IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE)) {
		goto end;
	}

#ifdef CONFIG_RWNX_FULLMAC
	rx_nss = rwnx_bfmer_get_rx_nss(vht_cap);
#endif /* CONFIG_RWNX_FULLMAC */

	/* Allocate a structure that will contain the beamforming report */
	if (rwnx_bfmer_report_add(rwnx_hw, rwnx_sta, RWNX_BFMER_REPORT_SPACE_SIZE)) {
		goto end;
	}

	/* Build the MM_BFMER_ENABLE_REQ message */
	bfmer_en_req = rwnx_msg_zalloc(MM_BFMER_ENABLE_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_bfmer_enable_req));

	/* Check message allocation */
	if (!bfmer_en_req) {
		/* Free memory allocated for the report */
		rwnx_bfmer_report_del(rwnx_hw, rwnx_sta);

		/* Do not use beamforming */
		goto end;
	}

	/* Provide DMA address to the MAC */
	bfmer_en_req->host_bfr_addr = rwnx_sta->bfm_report->dma_addr;
	bfmer_en_req->host_bfr_size = RWNX_BFMER_REPORT_SPACE_SIZE;
	bfmer_en_req->sta_idx = rwnx_sta->sta_idx;
#ifdef CONFIG_RWNX_SOFTMAC
	bfmer_en_req->aid = sta->aid;
	bfmer_en_req->rx_nss = sta->rx_nss;
#else
	bfmer_en_req->aid = rwnx_sta->aid;
	bfmer_en_req->rx_nss = rx_nss;
#endif /* CONFIG_RWNX_SOFTMAC */

	if (vht_capability & IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE) {
		bfmer_en_req->vht_mu_bfmee = true;
	} else {
		bfmer_en_req->vht_mu_bfmee = false;
	}

	/* Send the MM_BFMER_EN_REQ message to LMAC FW */
	rwnx_send_msg(rwnx_hw, bfmer_en_req, 0, 0, NULL, 0);

end:
	return;
}

#ifdef CONFIG_RWNX_MUMIMO_TX
int rwnx_send_mu_group_update_req(struct rwnx_hw *rwnx_hw, struct rwnx_sta *rwnx_sta)
{
	struct mm_mu_group_update_req *req;
	int group_id, i = 0;
	u64 map;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_MU_GROUP_UPDATE_REQ message */
	req = rwnx_msg_zalloc(MM_MU_GROUP_UPDATE_REQ, TASK_MM, DRV_TASK_ID,
			      sizeof(struct mm_mu_group_update_req) +
				      rwnx_sta->group_info.cnt * sizeof(req->groups[0]));

	/* Check message allocation */
	if (!req)
		return -ENOMEM;

	/* Go through the groups the STA belongs to */
	group_sta_for_each(rwnx_sta, group_id, map) {
		int user_pos = rwnx_mu_group_sta_get_pos(rwnx_hw, rwnx_sta, group_id);

		if (WARN((i >= rwnx_sta->group_info.cnt), "STA%d: Too much group (%d)\n", rwnx_sta->sta_idx, i + 1))
			break;

		req->groups[i].group_id = group_id;
		req->groups[i].user_pos = user_pos;

		i++;
	}

	req->group_cnt = rwnx_sta->group_info.cnt;
	req->sta_idx = rwnx_sta->sta_idx;

	/* Send the MM_MU_GROUP_UPDATE_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, MM_MU_GROUP_UPDATE_CFM, NULL, 0);
}
#endif /* CONFIG_RWNX_MUMIMO_TX */
#endif /* CONFIG_RWNX_BFMER */

/**********************************************************************
 *    Debug Messages
 *********************************************************************/
int rwnx_send_dbg_trigger_req(struct rwnx_hw *rwnx_hw, char *msg)
{
	struct mm_dbg_trigger_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_DBG_TRIGGER_REQ message */
	req = rwnx_msg_zalloc(MM_DBG_TRIGGER_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_dbg_trigger_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the MM_DBG_TRIGGER_REQ message */
	strncpy(req->error, msg, sizeof(req->error) - 1);

	/* Send the MM_DBG_TRIGGER_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 0, -1, NULL, 0);
}

int rwnx_send_dbg_mem_read_req(struct rwnx_hw *rwnx_hw, u32 mem_addr, struct dbg_mem_read_cfm *cfm)
{
	struct dbg_mem_read_req *mem_read_req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the DBG_MEM_READ_REQ message */
	mem_read_req = rwnx_msg_zalloc(DBG_MEM_READ_REQ, TASK_DBG, DRV_TASK_ID, sizeof(struct dbg_mem_read_req));
	if (!mem_read_req)
		return -ENOMEM;

	/* Set parameters for the DBG_MEM_READ_REQ message */
	mem_read_req->memaddr = mem_addr;

	/* Send the DBG_MEM_READ_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, mem_read_req, 1, DBG_MEM_READ_CFM, cfm, sizeof(*cfm));
}

int rwnx_send_dbg_mem_write_req(struct rwnx_hw *rwnx_hw, u32 mem_addr, u32 mem_data)
{
	struct dbg_mem_write_req *mem_write_req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the DBG_MEM_WRITE_REQ message */
	mem_write_req = rwnx_msg_zalloc(DBG_MEM_WRITE_REQ, TASK_DBG, DRV_TASK_ID, sizeof(struct dbg_mem_write_req));
	if (!mem_write_req)
		return -ENOMEM;

	/* Set parameters for the DBG_MEM_WRITE_REQ message */
	mem_write_req->memaddr = mem_addr;
	mem_write_req->memdata = mem_data;

	/* Send the DBG_MEM_WRITE_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, mem_write_req, 1, DBG_MEM_WRITE_CFM, NULL, 0);
}

int rwnx_send_dbg_set_mod_filter_req(struct rwnx_hw *rwnx_hw, u32 filter)
{
	struct dbg_set_mod_filter_req *set_mod_filter_req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the DBG_SET_MOD_FILTER_REQ message */
	set_mod_filter_req =
		rwnx_msg_zalloc(DBG_SET_MOD_FILTER_REQ, TASK_DBG, DRV_TASK_ID, sizeof(struct dbg_set_mod_filter_req));
	if (!set_mod_filter_req)
		return -ENOMEM;

	/* Set parameters for the DBG_SET_MOD_FILTER_REQ message */
	set_mod_filter_req->mod_filter = filter;

	/* Send the DBG_SET_MOD_FILTER_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, set_mod_filter_req, 1, DBG_SET_MOD_FILTER_CFM, NULL, 0);
}

int rwnx_send_dbg_set_sev_filter_req(struct rwnx_hw *rwnx_hw, u32 filter)
{
	struct dbg_set_sev_filter_req *set_sev_filter_req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the DBG_SET_SEV_FILTER_REQ message */
	set_sev_filter_req =
		rwnx_msg_zalloc(DBG_SET_SEV_FILTER_REQ, TASK_DBG, DRV_TASK_ID, sizeof(struct dbg_set_sev_filter_req));
	if (!set_sev_filter_req)
		return -ENOMEM;

	/* Set parameters for the DBG_SET_SEV_FILTER_REQ message */
	set_sev_filter_req->sev_filter = filter;

	/* Send the DBG_SET_SEV_FILTER_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, set_sev_filter_req, 1, DBG_SET_SEV_FILTER_CFM, NULL, 0);
}

int rwnx_send_dbg_get_sys_stat_req(struct rwnx_hw *rwnx_hw, struct dbg_get_sys_stat_cfm *cfm)
{
	void *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Allocate the message */
	req = rwnx_msg_zalloc(DBG_GET_SYS_STAT_REQ, TASK_DBG, DRV_TASK_ID, 0);
	if (!req)
		return -ENOMEM;

	/* Send the DBG_MEM_READ_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, DBG_GET_SYS_STAT_CFM, cfm, sizeof(*cfm));
}

int rwnx_send_cfg_rssi_req(struct rwnx_hw *rwnx_hw, u8 vif_index, int rssi_thold, u32 rssi_hyst)
{
	struct mm_cfg_rssi_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_CFG_RSSI_REQ message */
	req = rwnx_msg_zalloc(MM_CFG_RSSI_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_cfg_rssi_req));
	if (!req)
		return -ENOMEM;

	/* Set parameters for the MM_CFG_RSSI_REQ message */
	req->vif_index = vif_index;
	req->rssi_thold = (s8)rssi_thold;
	req->rssi_hyst = (u8)rssi_hyst;

	/* Send the MM_CFG_RSSI_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 0, 0, NULL, 0);
}

int bes_send_drv2mcu_msg(struct rwnx_hw *rwnx_hw, u8 *msg, u32 buf_len, u32 req_id, u32 cfm_id, void *cfm, u16 len)
{
	u32 *req;
	int reqcfm = (cfm != NULL) ? 1 : 0;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the to mcu message */
	req = rwnx_msg_zalloc(req_id, TASK_MCU, DRV_TASK_ID, buf_len);
	if (!req)
		return -ENOMEM;

	if (buf_len)
		memcpy((u8 *)req, msg, buf_len);

	/* Send the common cmd message to MCU */
	return rwnx_send_msg(rwnx_hw, req, reqcfm, cfm_id, cfm, len);
}

int bes_send_wf_cali_req(struct rwnx_hw *rwnx_hw, struct mcu_wf_cali_req *cali_req, struct mcu_wf_cali_cfm *cfm)
{
	struct mcu_wf_cali_req *req;

	/* Build the MCU_WIFI_CALI_REQ message */
	req = rwnx_msg_zalloc(MCU_WIFI_CALI_REQ, TASK_MCU, DRV_TASK_ID, sizeof(struct mcu_wf_cali_req));

	if (!req)
		return -ENOMEM;

	memcpy((void *)req, (void *)cali_req, sizeof(struct mcu_wf_cali_req));

	return rwnx_send_msg(rwnx_hw, req, 1, MCU_WIFI_CALI_CFM, cfm, sizeof(*cfm));
}

#ifdef CONFIG_BES_POWER_CTRL
int bes_send_wifi_on_off_req(struct rwnx_hw *rwnx_hw, bool on)
{
	u8 *req = NULL;
	int status = 0;
	int *cfm = &status, ret;
	int reqcfm = 1;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	req = rwnx_msg_zalloc(MCU_WIFI_ON_OFF_REQ, TASK_MCU, DRV_TASK_ID, sizeof(u8));
	if (!req)
		return -ENOMEM;

	*req = on;
	ret = rwnx_send_msg(rwnx_hw, req, reqcfm, MCU_WIFI_ON_OFF_CFM, cfm, sizeof(*cfm));

	return ret;
}
#endif

int bes_send_wake_ctrl_req(struct rwnx_hw *rwnx_hw, bool wakeup)
{
	u8 *req = NULL;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	req = rwnx_msg_zalloc(MCU_WIFI_WAKE_CTRL_REQ, TASK_MCU, DRV_TASK_ID, sizeof(u8));
	if (!req)
		return -ENOMEM;

	*req = wakeup;

	return rwnx_send_msg(rwnx_hw, req, 1, MCU_WIFI_WAKE_CTRL_CFM, NULL, 0);
}

int bes_send_recover_wifi_dpd_data(struct rwnx_hw *rwnx_hw, void *recover_dpd, u8 *data)
{
	u8 *req = NULL;
	int status = 0, ret;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	req = rwnx_msg_zalloc(MCU_WIFI_SEND_DPD_DATA_REQ, TASK_MCU, DRV_TASK_ID,
			     ((struct mcu_recover_dpd *)recover_dpd)->size + sizeof(struct mcu_recover_dpd));
	if (!req)
		return -ENOMEM;

	memcpy(req, (u8 *)recover_dpd, sizeof(struct mcu_recover_dpd));
	memcpy(req + sizeof(struct mcu_recover_dpd), data, ((struct mcu_recover_dpd *)recover_dpd)->size);

	ret = rwnx_send_msg(rwnx_hw, req, 1, MCU_WIFI_SEND_DPD_DATA_CFM, &status, sizeof(status));
	if (!ret && status)
		ret = -EINVAL; /* invalid data */

	return ret;
}

int bes_send_mcu_buf_cnt_req(struct rwnx_hw *rwnx_hw, struct mcu_buf_cnt_cfm *cfm)
{
#if defined(CONFIG_RWNX_PCIE) || defined(CONFIG_BES_IBUS)
	return 0;
#else
	void *req = NULL;
	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	req = rwnx_msg_zalloc(MCU_BUF_CNT_REQ, TASK_MCU, DRV_TASK_ID, 0);
	if (!req)
		return -ENOMEM;

	return rwnx_send_msg(rwnx_hw, req, 1, MCU_BUF_CNT_CFM, cfm, sizeof(*cfm));
#endif
}

int bes_set_umac_filter(struct rwnx_hw *rwnx_hw, u32 umac_filter)
{
	struct mm_set_filter_req *req;
	int ret;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	req = rwnx_msg_zalloc(MM_SET_FILTER_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_set_filter_req));
	if (!req)
		return -ENOMEM;

	/* Now copy all the flags into the message parameter */
	req->filter = umac_filter;

	ret = rwnx_send_msg(rwnx_hw, req, 1, MM_SET_FILTER_CFM, NULL, 0);

	return ret;
}

int bes_send_dbac_invalid_cali_data(struct rwnx_hw *rwnx_hw, u8 invalid_ch)
{
	struct mcu_dbac_cali_cmd *req;

	/* Build the MCU_DBAC_CALI_REQ message */
	req = rwnx_msg_zalloc(MCU_DBAC_CALI_REQ, TASK_MCU, DRV_TASK_ID,
			sizeof(struct mcu_dbac_cali_cmd) + sizeof(struct mcu_cali_inval_ch));
	if (!req)
		return -ENOMEM;

	req->sub_cmd = MCU_CALI_INVAL_CHAN_REQ;
	req->data_len = sizeof(struct mcu_cali_inval_ch);
	((struct mcu_cali_inval_ch *)(req->data))->channel = invalid_ch;

	return rwnx_send_msg(rwnx_hw, req, 1, MCU_DBAC_CALI_CFM, NULL, 0);
}

int bes_send_dbac_temp_compensation_req(struct rwnx_hw *rwnx_hw, u8 chan)
{
	struct mcu_dbac_cali_cmd *req;
	struct mcu_temp_compensation_data *data = NULL;
	u8 data_len = sizeof(struct mcu_temp_compensation_data);

	/* Build the MCU_DBAC_CALI_REQ message */
	req = rwnx_msg_zalloc(MCU_DBAC_CALI_REQ, TASK_MCU, DRV_TASK_ID,
			sizeof(struct mcu_dbac_cali_cmd) + data_len);
	if (!req)
		return -ENOMEM;

	req->sub_cmd = MCU_TEMP_COMPENSATION_REQ;
	req->data_len = data_len;
	data = (struct mcu_temp_compensation_data *)(req->data);
	data->roc_chan = chan;

	return rwnx_send_msg(rwnx_hw, req, 1, MCU_DBAC_CALI_CFM, NULL, 0);
}

int rwnx_send_scan_cancel_req(struct rwnx_hw *rwnx_hw, struct scan_cancel_cfm *cfm)
{
	struct scan_cancel_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the SCAN_CANCEL_REQ message */
	req = rwnx_msg_zalloc(SCAN_CANCEL_REQ, TASK_SCAN, DRV_TASK_ID, sizeof(struct scan_cancel_req));
	if (!req)
		return -ENOMEM;

	/* Send the SCAN_CANCEL_REQ message to LMAC FW */
	return rwnx_send_msg(rwnx_hw, req, 1, SCAN_CANCEL_CFM, cfm, sizeof(*cfm));
}

int rwnx_send_packet_filter_request(struct rwnx_hw *rwnx_hw, struct mm_set_packet_filter_req *req, void *cfm, u16 cfm_len)
{
	struct mm_set_packet_filter_req *param = NULL;

	if (!bes_sysfs_is_signal_mode())
		return -EPERM;

	param = rwnx_msg_zalloc(MM_SET_PACKET_FILTER_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_set_packet_filter_req));
	if (!param) {
		return -ENOMEM;
	}

	memcpy(param, req, sizeof(struct mm_set_packet_filter_req));

	return rwnx_send_msg(rwnx_hw, param, 1, MM_SET_PACKET_FILTER_CFM, cfm, cfm_len);
}

int rwnx_send_custom_request(struct rwnx_hw *rwnx_hw, struct mm_custom_req_hdr *req, void *cfm, u16 len)
{
	uint8_t *param = NULL;
	int ret;

	param = rwnx_msg_zalloc(MM_CUSTOM_REQ, TASK_MM, DRV_TASK_ID, req->length);
	if (!param)
		return -ENOMEM;

	memcpy(param, req, req->length);

	ret = rwnx_send_msg(rwnx_hw, param, 1, MM_CUSTOM_CFM, cfm, len);

	return ret;
}


int rwnx_send_vif_info_ext_get_req(struct rwnx_vif *vif, int offset, size_t value_length, void *value)
{
	int ret;
	struct vif_info_ext_req *req;

	if (value_length > 0xFF)
		return -EPERM;

	req = rwnx_msg_zalloc(MM_CUSTOM_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct vif_info_ext_req));
	if (!req)
		return -ENOMEM;

	req->hdr.id = CUSTOM_ID_VIF_INFO_EXT;
	req->hdr.length = sizeof(struct vif_info_ext_req);
	req->hdr.ops = VIF_INFO_EXT_OPS_GET;

	req->vif_index = vif->vif_index;

	req->item.offset = (u8)offset;
	req->item.length = (u8)value_length;

	ret = rwnx_send_msg(vif->rwnx_hw, req, 1, MM_CUSTOM_CFM, value, value_length);

	return ret;
}

int rwnx_send_vif_info_ext_set_req(struct rwnx_vif *vif, int offset, size_t value_length, void *value)
{
	int ret;
	struct vif_info_ext_req *req;

	if (value_length > 0xFF)
		return -EPERM;

	req = rwnx_msg_zalloc(MM_CUSTOM_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct vif_info_ext_req) + value_length);
	if (!req)
		return -ENOMEM;

	req->hdr.id = CUSTOM_ID_VIF_INFO_EXT;
	req->hdr.length = sizeof(struct vif_info_ext_req) + value_length;
	req->hdr.ops = VIF_INFO_EXT_OPS_SET;

	req->vif_index = vif->vif_index;

	req->item.offset = (u8)offset;
	req->item.length = (u8)value_length;
	memcpy(req->item.value, value, value_length);

	ret = rwnx_send_msg(vif->rwnx_hw, req, 1, MM_CUSTOM_CFM, NULL, 0);

	return ret;
}

int rwnx_send_arp_keepalive_request(struct rwnx_hw *rwnx_hw, u16 period,
		u8 vif_idx, u32 sender_ip, u32 target_ip)
{
	struct mm_set_arp_keepalive_req *req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	req = rwnx_msg_zalloc(MM_SET_ARP_KEEPALIVE_REQ, TASK_MM, DRV_TASK_ID,
			sizeof(struct mm_set_arp_keepalive_req));
	if (!req)
		return -ENOMEM;

	req->period = period;
	req->vif_idx = vif_idx;
	memcpy(req->sender_ip, &sender_ip, 4);
	memcpy(req->target_ip, &target_ip, 4);

	return rwnx_send_msg(rwnx_hw, req, 1, MM_SET_ARP_KEEPALIVE_CFM, NULL, 0);
}

int bes_send_bt_status_get(struct rwnx_hw *rwnx_hw, u32 *status)
{
	void *req = NULL;
	u32 cfm;
	int ret = 0;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Allocate memory to requests */
	if (!(req = rwnx_msg_zalloc(MCU_GET_BT_STATUS_REQ, TASK_MCU, DRV_TASK_ID, 0))) {
		bes_err(BES_DBG_MSG_TX, "%s req zalloc fail\n", __func__);
		return -ENOMEM;
	}

	/* send command to device */
	ret = rwnx_send_msg(rwnx_hw, req, 1, MCU_GET_BT_STATUS_CFM, &cfm, sizeof(cfm));

	/* export bt status information */
	*status = cfm;

	return ret;
}

int bes_send_temprtr_get(struct rwnx_hw *rwnx_hw, int32_t *value)
{
	void *req = NULL;
	int32_t cfm;
	int ret = 0;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Allocate memory to requests */
	if (!(req = rwnx_msg_zalloc(MCU_GET_TEMPRTR_REQ, TASK_MCU, DRV_TASK_ID, 0))) {
		bes_err(BES_DBG_MSG_TX, "%s req zalloc fail\n", __func__);
		return -ENOMEM;
	}

	/* send command to device */
	ret = rwnx_send_msg(rwnx_hw, req, 1, MCU_GET_TEMPRTR_CFM, &cfm, sizeof(cfm));

	/* export bt status information */
	*value = cfm;

	return ret;
}

int rwnx_send_pta_cfg(struct rwnx_hw *rwnx_hw, struct bes_coex_cmd *cmd)
{
	struct bes_coex_cmd *req = NULL;

	/* allocate message send buffer */
	req = rwnx_msg_zalloc(MM_PTA_CFG_REQ, TASK_MM, DRV_TASK_ID, sizeof(*cmd));
	if (!req)
		return -ENOMEM;

	/* copy content to send buffer */
	memcpy(req, cmd, sizeof(*cmd));

	/* send command to device */
	return rwnx_send_msg(rwnx_hw, req, 1, MM_PTA_CFG_CFM, NULL, 0);
}

int bes_send_chan_noa_update(struct rwnx_vif *vif)
{
	struct vif_info_noa_update_req *req = NULL;
	int ret = 0;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Allocate memory to requests */
	if (!(req = rwnx_msg_zalloc(MM_CUSTOM_REQ, TASK_MM, DRV_TASK_ID,
				    sizeof(struct vif_info_noa_update_req)))) {
		bes_err(BES_DBG_MSG_TX, "%s req zalloc fail\n", __func__);
		return -ENOMEM;
	}

	/* set header */
	req->hdr.id = CUSTOM_ID_VIF_INFO;
	req->hdr.length = sizeof(struct vif_info_noa_update_req);
	req->hdr.ops = VIF_INFO_OPS_GO_NOA_UPDATE;
	req->vif_index = vif->vif_index;

	/* send custom request to device */
	ret = rwnx_send_msg(vif->rwnx_hw, req, 1, MM_CUSTOM_CFM, NULL, 0);

	return ret;
}

int bes_send_ignore_bcn_edca_req(struct rwnx_hw *rwnx_hw, bool ignore)
{
	return bes_send_drv2mcu_msg(rwnx_hw, (u8 *)&ignore, sizeof(ignore),
				    MCU_IGNORE_BCN_EDCA_REQ, 0, NULL, 0);
}

int bes_send_set_tx_max_pwr_req(struct rwnx_hw *rwnx_hw, struct wifi_tx_max_pwr *max_pwr,
                                struct wifi_pwr_range *pwr_range)
{
	return bes_send_drv2mcu_msg(rwnx_hw, (u8 *)max_pwr, sizeof(*max_pwr),
				    MCU_SET_TX_MAX_POWER_REQ, MCU_SET_TX_MAX_POWER_CFM,
                                    pwr_range, sizeof(*pwr_range));
}

int rwnx_send_csi_cap_req(struct rwnx_hw *rwnx_hw, struct mm_csi_cap_req *req)
{
	struct mm_csi_cap_req *csi_req;
	int ret;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MM_CSI_CAP_REQ message */
	csi_req = rwnx_msg_zalloc(MM_CSI_CAP_REQ, TASK_MM, DRV_TASK_ID, sizeof(struct mm_csi_cap_req));
	if (!csi_req)
		return -ENOMEM;

	/* Set parameters for the MM_CSI_CAP_REQ message */
	memcpy(csi_req, req, sizeof(struct mm_csi_cap_req));

	bes_dbg(BES_DBG_MSG_TX, "%s csi_sniff=%u bw=0x%x type=0x%x stbc=0x%x mac_match=0x%x lmin=0x%x lmax=0x%x\n",
		__func__, csi_req->csi_sniff, csi_req->bw, csi_req->type, csi_req->stbc,
		csi_req->mac_match, csi_req->lsig_len_min, csi_req->lsig_len_max);

	/* Send the MM_CSI_CAP_REQ message to LMAC FW */
	ret = rwnx_send_msg(rwnx_hw, csi_req, 1, MM_CSI_CAP_CFM, NULL, 0);

	return ret;
}

int rwnx_send_system_state_req(struct rwnx_hw *rwnx_hw)
{
	struct wifi_csi_cap_req *csi_req;
	int ret;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Build the MCU_SYS_STATE_REQ message */
	csi_req = rwnx_msg_zalloc(MCU_SYS_STATE_REQ, TASK_MCU, DRV_TASK_ID, 0);
	if (!csi_req)
		return -ENOMEM;

	/* Send the MCU_SYS_STATE_REQ message to LMAC FW */
	ret = rwnx_send_msg(rwnx_hw, csi_req, 1, MCU_SYS_STATE_CFM, NULL, 0);

	return ret;
}

int rwnx_send_country_code_cfg(struct rwnx_hw *rwnx_hw)
{
	struct bes_notify_country_code_req *req = NULL;

	/* allocate message send buffer */
	req = rwnx_msg_zalloc(MCU_SEND_COUNTRY_CODE_REQ, TASK_MCU, DRV_TASK_ID, sizeof(*req));
	if (!req)
		return -ENOMEM;

	/* copy content to send buffer */
	memcpy(req->country_num, rwnx_hw->custregd.working_country, 2);

	/* send command to device */
	return rwnx_send_msg(rwnx_hw, req, 1, MCU_SEND_COUNTRY_CODE_CFM, NULL, 0);
}

int rwnx_send_get_delay_stats_info_req(struct rwnx_hw *rwnx_hw, struct mcu_get_delay_stats_info_cfm *cfm)
{
	void *void_param;

	/* allocate message send buffer */
	void_param = rwnx_msg_zalloc(MCU_GET_DELAY_STATS_INFO_REQ, TASK_MCU, DRV_TASK_ID, 0);
	if (!void_param)
		return -ENOMEM;

	/* send command to device */
	return rwnx_send_msg(rwnx_hw, void_param, 1, MCU_GET_DELAY_STATS_INFO_CFM, cfm, sizeof(*cfm));
}

int rwnx_send_cca_enable(struct rwnx_hw *rwnx_hw, uint8_t enable)
{
	int ret = 0;
	struct adaptive_cca_enable_req req;

	req.hdr.id = CUSTOM_ID_ADAPTIVE_CCA;
	req.hdr.length = sizeof(req);
	req.hdr.ops = ADAPTIVE_CCA_ENABLE;

	req.adaptive_cca_en = enable;

	ret = rwnx_send_custom_request(rwnx_hw, &req.hdr, NULL, 0);
	if (ret) {
		bes_err(BES_DBG_MSG_TX, "%s enable cca self adapt failed(%d)\n", __func__, ret);
		return -1;
	}

	return 0;
}

int rwnx_send_cca_config(struct rwnx_hw *rwnx_hw, struct adaptive_cca_config* cca_cfg)
{
	int ret = 0;
	struct adaptive_cca_config_req req;

	req.hdr.id = CUSTOM_ID_ADAPTIVE_CCA;
	req.hdr.length = sizeof(struct adaptive_cca_config_req);
	req.hdr.ops = ADAPTIVE_CCA_CONFIG;

	memcpy(&req.cca_cfg, cca_cfg, sizeof(struct adaptive_cca_config));

	ret = rwnx_send_custom_request(rwnx_hw, &req.hdr, NULL, 0);
	if (ret) {
		bes_err(BES_DBG_MSG_TX, "%s config cca self adapt failed(%d)\n", __func__, ret);
		return -1;
	}

	return 0;
}
