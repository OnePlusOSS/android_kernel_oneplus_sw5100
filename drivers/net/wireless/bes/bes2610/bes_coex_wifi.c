/*
 * @file bes_coex_wifi.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "bes_coex.h"
#include "bes_psa.h"
#include "bes_filter.h"
#include "bes_hwio.h"
#include "bes_log.h"

/* wifi coex min/max duration */
#define BES_COEX_WIFI_MIN_DUR	20000
#define BES_COEX_WIFI_MAX_DUR	80000

typedef bool (*bes_coex_msg_rx_pfun)(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg);
typedef bool (*bes_coex_msg_tx_pfun)(struct rwnx_hw *rwnx_hw, struct rwnx_cmd_a2emsg *msg);

struct bes_coex_msg_rx_process_t {
	u16 id;
	bes_coex_msg_rx_pfun rx_pfun;
};

struct bes_coex_msg_tx_process_t {
	u16 id;
	bes_coex_msg_tx_pfun tx_pfun;
};

DEFINE_SPINLOCK(wifi_coex_lock);
static u32 wifi_coex_vif_state[NX_ITF_MAX];
static u32 wifi_coex_dev_status;

static bool bes_coex_wifi_check_idle(struct rwnx_hw *rwnx_hw, u8 cur_vif_idx)
{
	struct rwnx_vif *vif = NULL;

	/* check if there is an another vif that is working */
	list_for_each_entry(vif, &rwnx_hw->vifs, list) {
		if (vif->vif_index == cur_vif_idx)
			continue;

		if (!netif_running(vif->ndev))
			continue;

		if (netif_carrier_ok(vif->ndev))
			return false;
	}

	return true;
}

static bool bes_coex_wifi_scan_start_cfm(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	u8 idx = 0;
	bool changed = false;

	/* clear the flag of WIFI_2G4_SCAN when scan finished */
	idx = ((struct scanu_start_cfm *)msg->param)->vif_idx;
	if (idx >= ARRAY_SIZE(wifi_coex_vif_state))
		return false;

	spin_lock(&wifi_coex_lock);
	if (wifi_coex_vif_state[idx] & BIT(WIFI_2G4_SCAN)) {
		wifi_coex_vif_state[idx] &= ~BIT(WIFI_2G4_SCAN);
		changed = true;
	}
	spin_unlock(&wifi_coex_lock);

	bes_dbg(BES_DBG_COEX, "%s changed:%d", __func__, changed);

	return changed;
}

static bool bes_coex_wifi_scan_cancel_cfm(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	u8 idx = 0;
	bool changed = false;
	struct cfg80211_scan_request *request = rwnx_hw->scan_request;
	struct rwnx_vif *rwnx_vif = NULL;
	struct scan_cancel_cfm *cfm;

	cfm = (struct scan_cancel_cfm *)msg->param;
	/* scan is already finished, do nothing */
	if (cfm->status != CO_OK) {
		return false;
	}

	/* scan is already finished, do nothing */
	if (request == NULL)
		return false;

	if (!request->wdev) {
		bes_err(BES_DBG_MSG_RX, "%s ERROR: wdev is NULL\n", __func__);
		return false;
	}

	/* get vif instance from scan request */
	rwnx_vif = container_of(request->wdev, struct rwnx_vif, wdev);

	/* clear the flag of WIFI_2G4_SCAN when scan aborted */
	idx = rwnx_vif->vif_index;
	if (idx < NX_ITF_MAX) {
		spin_lock(&wifi_coex_lock);
		if (wifi_coex_vif_state[idx] & BIT(WIFI_2G4_SCAN)) {
			wifi_coex_vif_state[idx] &= ~BIT(WIFI_2G4_SCAN);
			changed = true;
		}
		spin_unlock(&wifi_coex_lock);
	} else {
		bes_err(BES_DBG_COEX, "%s unexpected vif_index: %d", __func__, idx);
		changed = false;
	}

	bes_dbg(BES_DBG_COEX, "%s changed:%d", __func__, changed);

	return changed;
}

static bool bex_coex_wifi_remain_on_channel_exp_ind(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	u8 idx = 0;
	bool changed = false;

	/* clear the flag of WIFI_2G4_ROC when roc expired */
	idx = ((struct mm_remain_on_channel_exp_ind *)msg->param)->vif_index;
	if (idx >= ARRAY_SIZE(wifi_coex_vif_state))
		return false;

	spin_lock(&wifi_coex_lock);
	if (wifi_coex_vif_state[idx] & BIT(WIFI_2G4_ROC)) {
		wifi_coex_vif_state[idx] &= ~BIT(WIFI_2G4_ROC);
		changed = true;
	}
	spin_unlock(&wifi_coex_lock);

	bes_dbg(BES_DBG_COEX, "%s changed:%d", __func__, changed);

	return changed;
}

static bool bes_coex_wifi_disconnect_ind(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	u8 idx = 0;
	bool changed = false;

	/* clear WIFI_2G4_CONNECTED and WIFI_2G4_CONNECTING when sta link is break */
	idx = ((struct sm_disconnect_ind *)msg->param)->vif_idx;
	if (idx >= ARRAY_SIZE(wifi_coex_vif_state))
		return false;

	spin_lock(&wifi_coex_lock);
	if (wifi_coex_vif_state[idx] & BIT(WIFI_2G4_CONNECTED)) {
		wifi_coex_vif_state[idx] &= ~BIT(WIFI_2G4_CONNECTED);
		changed = true;
	}

	if (wifi_coex_vif_state[idx] & BIT(WIFI_2G4_CONNECTING)) {
		wifi_coex_vif_state[idx] &= ~BIT(WIFI_2G4_CONNECTING);
		changed = true;
	}
	spin_unlock(&wifi_coex_lock);

	/* switch coex schedule based on wifi state */
	if (bes_power_ctrl_bt_enabled()) {
		u32 wifi_state = bes_coex_wifi_get_status();
		u32 actions = 0;

		if (bes_coex_wifi_check_idle(rwnx_hw, idx))
			actions |= BIT(BES_ARB_OPER_COEX_STOP);
		else
			actions |= BIT(BES_ARB_OPER_COEX_START);

		/* trigger coex work */
		bes_coex_arb_set_wifi_state(rwnx_hw, wifi_state, actions);
	}

	bes_dbg(BES_DBG_COEX, "%s changed:%d", __func__, changed);

	return changed;
}

static bool bes_coex_wifi_apm_start_cfm(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	u8 idx = 0;
	bool changed = false;
	struct apm_start_cfm *cfm = NULL;

	cfm = (struct apm_start_cfm *)msg->param;
	idx = cfm->vif_idx;
	if (idx >= ARRAY_SIZE(wifi_coex_vif_state))
		return false;

	spin_lock(&wifi_coex_lock);
	if (wifi_coex_vif_state[idx] & BIT(WIFI_2G4_AP_SETTING) && cfm->status != 0) {
		wifi_coex_vif_state[idx] &= ~BIT(WIFI_2G4_AP_SETTING);
		changed = true;
	} else if(wifi_coex_vif_state[idx] & BIT(WIFI_2G4_AP_SETTING) && cfm->status == 0) {
		wifi_coex_vif_state[idx] &= ~BIT(WIFI_2G4_AP_SETTING);
		wifi_coex_vif_state[idx] |= BIT(WIFI_2G4_AP_SETTED);
		changed = true;
	}
	spin_unlock(&wifi_coex_lock);

	/* trigger coex work when bt is enabled */
	if (bes_power_ctrl_bt_enabled()) {
		u32 wifi_state = bes_coex_wifi_get_status();
		u32 actions = BIT(BES_ARB_OPER_DUR_SET);

		if (wifi_coex_vif_state[idx] & BIT(WIFI_2G4_AP_SETTED)) {
			actions |= BIT(BES_ARB_OPER_COEX_START);
		} else {
			if (bes_coex_wifi_check_idle(rwnx_hw, idx))
				actions |= BIT(BES_ARB_OPER_COEX_STOP);
			else
				actions |= BIT(BES_ARB_OPER_COEX_START);
		}

		/* trigger coex work */
		bes_coex_arb_set_wifi_state(rwnx_hw, wifi_state, actions);
	}

	bes_dbg(BES_DBG_COEX, "%s changed:%d", __func__, changed);

	return changed;
}

static bool bes_coex_wifi_sta_connect_ind(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	u8 idx = 0;
	bool changed = false;
	struct sm_connect_ind *ind = NULL;

	ind = (struct sm_connect_ind *)msg->param;
	idx = ind->vif_idx;
	if (idx >= ARRAY_SIZE(wifi_coex_vif_state))
		return false;

	spin_lock(&wifi_coex_lock);
	if (wifi_coex_vif_state[idx] & BIT(WIFI_2G4_CONNECTING) && ind->status_code != 0) {
		wifi_coex_vif_state[idx] &= ~BIT(WIFI_2G4_CONNECTING);
		changed = true;
	} else if(wifi_coex_vif_state[idx] & BIT(WIFI_2G4_CONNECTING) && ind->status_code == 0) {
		wifi_coex_vif_state[idx] &= ~BIT(WIFI_2G4_CONNECTING);
		wifi_coex_vif_state[idx] |= BIT(WIFI_2G4_CONNECTED);
		changed = true;
	}
	spin_unlock(&wifi_coex_lock);

	/* trigger coex work when bt is enabled */
	if (bes_power_ctrl_bt_enabled() && ind->status_code != 0) {
		u32 wifi_state = bes_coex_wifi_get_status();
		u32 actions = 0;

		if (!bes_coex_wifi_check_idle(rwnx_hw, idx))
			actions |= BIT(BES_ARB_OPER_COEX_START);

		/* trigger coex work */
		bes_coex_arb_set_wifi_state(rwnx_hw, wifi_state, actions);
	}
	bes_dbg(BES_DBG_COEX, "%s changed:%d", __func__, changed);

	return changed;
}

static bool bes_coex_wifi_bt_evt_ind(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	struct coex_evt_hdr *ind = (struct coex_evt_hdr *)msg->param;

	if (ind->id == COEX_EVT_SCHE_ALLOW) {
		/* trigger coex work when bt is enabled */
		if (bes_power_ctrl_bt_enabled()) {
			u32 wifi_state = bes_coex_wifi_get_status();
			u32 actions = BIT(BES_ARB_OPER_COEX_START);

			/* trigger coex work */
			bes_coex_arb_set_wifi_state(rwnx_hw, wifi_state, actions);
		}
	}

	bes_dbg(BES_DBG_COEX, "%s triggered", __func__);

	return true;
}

static bool bes_coex_wifi_csa_evt_ind(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	struct mm_csa_finish_ind *ind = (struct mm_csa_finish_ind *)msg->param;
	struct rwnx_vif *vif;
	bool found = false;
	struct rwnx_csa *csa = NULL;
	u8 idx = 0;

	/* look for vif entry */
	list_for_each_entry(vif, &rwnx_hw->vifs, list) {
		if (vif->vif_index == ind->vif_index) {
			found = true;
			break;
		}
	}

	if (!found)
		return false;

	csa = vif->ap.csa;
	idx = vif->vif_index;
	if (idx >= ARRAY_SIZE(wifi_coex_vif_state))
		return false;

	/* only one vif is in ap mode anyway, and update coex mode */
	if ((wifi_coex_vif_state[idx] & BIT(WIFI_2G4_AP_SETTED)) &&
	    (csa != NULL) &&
	    (csa->chandef.chan->band == NL80211_BAND_5GHZ)) {
		u32 wifi_state = bes_coex_wifi_get_status();
		u32 actions = BIT(BES_ARB_OPER_COEX_STOP);

		spin_lock(&wifi_coex_lock);
		wifi_coex_vif_state[idx] &= ~BIT(WIFI_2G4_AP_SETTED);
		spin_unlock(&wifi_coex_lock);

		bes_coex_arb_set_wifi_state(rwnx_hw, wifi_state, actions);
	}

	return true;
}

const struct bes_coex_msg_rx_process_t bes_coex_wifi_rx_process_tbl[] = {
	{.id = SCANU_START_CFM,			.rx_pfun = bes_coex_wifi_scan_start_cfm},
	{.id = SCAN_CANCEL_CFM,			.rx_pfun = bes_coex_wifi_scan_cancel_cfm},
	{.id = MM_REMAIN_ON_CHANNEL_EXP_IND,	.rx_pfun = bex_coex_wifi_remain_on_channel_exp_ind},
	{.id = SM_DISCONNECT_IND,		.rx_pfun = bes_coex_wifi_disconnect_ind},
	{.id = APM_START_CFM,			.rx_pfun = bes_coex_wifi_apm_start_cfm},
	{.id = SM_CONNECT_IND,			.rx_pfun = bes_coex_wifi_sta_connect_ind},
	{.id = MM_COEX_EVENT_IND,		.rx_pfun = bes_coex_wifi_bt_evt_ind},
	{.id = MM_CSA_FINISH_IND,		.rx_pfun = bes_coex_wifi_csa_evt_ind},
};

bool bes_coex_wifi_msg_rx_proc(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	int i = 0;
	int tbl_len = sizeof(bes_coex_wifi_rx_process_tbl) / sizeof(bes_coex_wifi_rx_process_tbl[0]);

	/* Return directly when wifi is uninitalised */
	if (rwnx_hw == NULL)
		return false;

	/* Don't capture message when coex mode is fdd */
	if (rwnx_hw->coex.hw_fdd)
		return false;

	/* lookup rx processor and execute it */
	for (i = 0; i < tbl_len; i++) {
		if (bes_coex_wifi_rx_process_tbl[i].id == msg->id) {
			return (bes_coex_wifi_rx_process_tbl[i].rx_pfun)(rwnx_hw, msg);
		}
	}

	return false;
}

static bool bes_coex_wifi_check_dev_stat_valid(struct dev_status_t *dev_status)
{
#ifndef CONFIG_RWNX_PCIE
	return (dev_status->header.size == sizeof(struct dev_status_t));
#else
	return ((dev_status->id == MCU_DEV_STATE_IND) && (dev_status->param_len == sizeof(u32)));
#endif
}

static void bes_coex_wifi_dev_status_ind(struct rwnx_hw *rwnx_hw, const struct sk_buff *skb)
{
	struct dev_status_t *dev_status = (struct dev_status_t *)skb->data;
	bool old_bt_on_ready, new_bt_on_ready;
	bool bt_self_mgmd = !!CONFIG_BES_POWER_CTRL_BT_SELF_MANAGED;

	/* check if the message is valid */
	if (!bes_coex_wifi_check_dev_stat_valid(dev_status))
		return;

	/* get old and new bt status */
	old_bt_on_ready = wifi_coex_dev_status & BES_SLAVE_STATUS_BT_ON_READY;
	new_bt_on_ready = dev_status->stat & BES_SLAVE_STATUS_BT_ON_READY;

	/* check if bt status is changed */
	if (rwnx_hw && !rwnx_hw->coex.hw_fdd &&
	    !bt_self_mgmd &&
	    rwnx_hw->coex.inited &&
		(old_bt_on_ready != new_bt_on_ready)) {
		u32 wifi_state = bes_coex_wifi_get_status();
		u32 actions = 0;

		if (new_bt_on_ready) {
			/* channel switch when 2.4g ap opened */
			if ((bes_coex_wifi_get_status() & BIT(WIFI_2G4_AP_SETTED)) &&
			    !rwnx_hw->coex.hw_fdd)
				actions |= BIT(BES_ARB_OPER_AP_CSA);
		}

		/* set coex schedule based on wifi status when bt is opened */
		if (bes_coex_wifi_check_idle(rwnx_hw, -1))
			actions |= BIT(BES_ARB_OPER_COEX_STOP);
		else
			actions |= BIT(BES_ARB_OPER_COEX_START);

		/* trigger coex work */
		bes_coex_arb_set_wifi_state(rwnx_hw, wifi_state, actions);

		bes_dbg(BES_DBG_COEX, "%s bt:%d", __func__, new_bt_on_ready);
	}

	/* update coex device status */
	wifi_coex_dev_status = dev_status->stat;
}

void bes_coex_wifi_rx_process(struct rwnx_hw *rwnx_hw, const struct sk_buff *skb)
{
	struct ipc_header *pHdr = (struct ipc_header *)skb->data;
	struct ipc_e2a_msg *msg = (struct ipc_e2a_msg *)(skb->data + 4);

	/* Process according to receiving type */
	switch (pHdr->type) {
		case EVENT_E2A_MSG:
			bes_coex_wifi_msg_rx_proc(rwnx_hw, msg);
			break;
		case EVENT_E2A_DEV_READY_IND:
			bes_coex_wifi_dev_status_ind(rwnx_hw, skb);
			break;
		default:
			break;
	}
}

static bool bes_coex_wifi_scan_start_req(struct rwnx_hw *rwnx_hw, struct rwnx_cmd_a2emsg *msg)
{
	u8 idx = 0, i = 0;
	bool changed = false, band_2g4 = false;
	struct scanu_start_req *req = NULL;

	/* get scanu start request */
	req = (struct scanu_start_req *)(msg->param);

	/* check if there is a 2.4g channel in channel list */
	for (i = 0 ; i < req->chan_cnt; i++) {
		if (req->chan[i].band == PHY_BAND_2G4) {
			band_2g4 = true;
			break;
		}
	}

	/* set the flag of WIFI_2G4_SCAN if the scan works on 2.4g band */
	idx = req->vif_idx;
	spin_lock(&wifi_coex_lock);
	if (band_2g4 && !(wifi_coex_vif_state[idx] & BIT(WIFI_2G4_SCAN))) {
		wifi_coex_vif_state[idx] |= BIT(WIFI_2G4_SCAN);
		changed = true;
	}
	spin_unlock(&wifi_coex_lock);

	bes_dbg(BES_DBG_COEX, "%s changed:%d", __func__, changed);

	return changed;
}

static bool bes_coex_wifi_remian_on_channel_req(struct rwnx_hw *rwnx_hw, struct rwnx_cmd_a2emsg *msg)
{
	u8 idx = 0;
	bool changed = false, band_2g4 = false;
	struct mm_remain_on_channel_req *req = NULL;

	/* get roc request */
	req = (struct mm_remain_on_channel_req *)(msg->param);

	/* check if the roc works on 2.4g band */
	if (req->chan.band == PHY_BAND_2G4)
		band_2g4 = true;

	/* set the flag of WIFI_2G4_ROC if the roc works on 2.4g band */
	idx = req->vif_index;
	spin_lock(&wifi_coex_lock);
	if (band_2g4 && !(wifi_coex_vif_state[idx] & BIT(WIFI_2G4_ROC))) {
		wifi_coex_vif_state[idx] |= BIT(WIFI_2G4_ROC);
		changed = true;
	}
	spin_unlock(&wifi_coex_lock);

	bes_dbg(BES_DBG_COEX, "%s changed:%d", __func__, changed);

	return changed;
}

static bool bes_coex_wifi_connect_req(struct rwnx_hw *rwnx_hw, struct rwnx_cmd_a2emsg *msg)
{
	u8 idx = 0;
	bool changed = false, band_2g4 = false;
	struct sm_connect_req *req = NULL;

	/* get connect req */
	req = (struct sm_connect_req *)(msg->param);

	/* check if the connecting works on 2.4g band */
	if (req->chan.band == PHY_BAND_2G4)
		band_2g4 = true;

	/* set the flag of WIFI_2G4_CONNECTING if the sta works on 2.4g band */
	idx = req->vif_idx;
	spin_lock(&wifi_coex_lock);
	if (band_2g4 && !(wifi_coex_vif_state[idx] & BIT(WIFI_2G4_CONNECTING))) {
		wifi_coex_vif_state[idx] |= BIT(WIFI_2G4_CONNECTING);
		changed = true;
	}
	spin_unlock(&wifi_coex_lock);

	bes_dbg(BES_DBG_COEX, "%s changed:%d", __func__, changed);

	return changed;
}

static bool bes_coex_wifi_ap_start_req(struct rwnx_hw *rwnx_hw, struct rwnx_cmd_a2emsg *msg)
{
	u8 idx = 0;
	bool changed = false, band_2g4 = false;
	struct apm_start_req *req = NULL;

	/* get ap start request */
	req = (struct apm_start_req *)(msg->param);

	/* check if the ap works on 2.4g band */
	if (req->chan.band == PHY_BAND_2G4)
		band_2g4 = true;

	/* set the flag of WIFI_2G4_AP_SETTING if the ap works on 2.4g band */
	idx = req->vif_idx;
	spin_lock(&wifi_coex_lock);
	if (band_2g4 && !(wifi_coex_vif_state[idx] & BIT(WIFI_2G4_AP_SETTING))) {
		wifi_coex_vif_state[idx] |= BIT(WIFI_2G4_AP_SETTING);
		changed = true;
	}
	spin_unlock(&wifi_coex_lock);

	bes_dbg(BES_DBG_COEX, "%s changed:%d", __func__, changed);

	return changed;
}

static bool bes_coex_wifi_ap_stop_req(struct rwnx_hw *rwnx_hw, struct rwnx_cmd_a2emsg *msg)
{
	u8 idx = 0;
	bool changed = false;
	struct apm_stop_req *req = NULL;

	/* get ap stop request */
	req = (struct apm_stop_req *)(msg->param);

	/* clear the flag of WIFI_2G4_AP if the ap works on 2.4g band */
	idx = req->vif_idx;
	spin_lock(&wifi_coex_lock);
	if (wifi_coex_vif_state[idx] & BIT(WIFI_2G4_AP_SETTED)) {
		wifi_coex_vif_state[idx] &= ~BIT(WIFI_2G4_AP_SETTED);
		changed = true;
	}
	spin_unlock(&wifi_coex_lock);

	/* trigger coex work wifi state is changed */
	if (changed && bes_power_ctrl_bt_enabled()) {
		u32 wifi_state = bes_coex_wifi_get_status();
		u32 actions = 0;

		if (bes_coex_wifi_check_idle(rwnx_hw, idx))
			actions |= BIT(BES_ARB_OPER_COEX_STOP);
		else
			actions |= BIT(BES_ARB_OPER_COEX_START);
		bes_coex_arb_set_wifi_state(rwnx_hw, wifi_state, actions);
	}

	bes_dbg(BES_DBG_COEX, "%s changed:%d", __func__, changed);

	return changed;
}

static bool bes_coex_wifi_control_port_req(struct rwnx_hw *rwnx_hw, struct rwnx_cmd_a2emsg *msg)
{
	u8 idx = 0;
	bool changed = false;
	struct me_set_control_port_req *req = (struct me_set_control_port_req *)(msg->param);
	struct rwnx_sta *rwnx_sta = rwnx_rx_get_sta(rwnx_hw, req->sta_idx);

	if (!req->control_port_open)
		return false;

	if (!rwnx_sta)
		return false;

	idx = rwnx_sta->vif_idx;

	if (idx < NX_ITF_MAX) {
		spin_lock(&wifi_coex_lock);
		if (wifi_coex_vif_state[idx] & BIT(WIFI_2G4_CONNECTING)) {
			wifi_coex_vif_state[idx] &= ~BIT(WIFI_2G4_CONNECTING);
			wifi_coex_vif_state[idx] |= BIT(WIFI_2G4_CONNECTED);
			changed = true;
		}
		spin_unlock(&wifi_coex_lock);
	} else {
		bes_err(BES_DBG_COEX, "%s unexpected vif_index: %d", __func__, idx);
		changed = false;
	}

	bes_dbg(BES_DBG_COEX, "%s changed:%d", __func__, changed);

	return changed;
}

const struct bes_coex_msg_tx_process_t bes_coex_tx_process_tbl[] = {
	{.id = SCANU_START_REQ,          .tx_pfun = bes_coex_wifi_scan_start_req},
	{.id = MM_REMAIN_ON_CHANNEL_REQ, .tx_pfun = bes_coex_wifi_remian_on_channel_req},
	{.id = SM_CONNECT_REQ,           .tx_pfun = bes_coex_wifi_connect_req},
	{.id = APM_START_REQ,            .tx_pfun = bes_coex_wifi_ap_start_req},
	{.id = APM_STOP_REQ,             .tx_pfun = bes_coex_wifi_ap_stop_req},
	{.id = ME_SET_CONTROL_PORT_REQ,  .tx_pfun = bes_coex_wifi_control_port_req},
};

void bes_coex_wifi_msg_tx_proc(struct rwnx_hw *rwnx_hw, struct rwnx_cmd_a2emsg *msg)
{
	int i = 0;
	int tbl_len = sizeof(bes_coex_tx_process_tbl) / sizeof(bes_coex_tx_process_tbl[0]);

	/* Don't capture message when coex mode is fdd */
	if (rwnx_hw->coex.hw_fdd)
		return;

	/* lookup message capture function and execute it */
	for (i = 0; i < tbl_len; i++) {
		if (bes_coex_tx_process_tbl[i].id == msg->id) {
			(bes_coex_tx_process_tbl[i].tx_pfun)(rwnx_hw, msg);
			break;
		}
	}
}

u32 bes_coex_wifi_get_status(void)
{
	u32 i, wifi_coex_state = 0;

	spin_lock(&wifi_coex_lock);
	for (i = 0; i < NX_ITF_MAX; i++) {
		wifi_coex_state |= wifi_coex_vif_state[i];
	}
	spin_unlock(&wifi_coex_lock);

	return wifi_coex_state;
}

void bes_coex_wifi_set_state(u8 vif_idx, u8 event, bool set)
{
	BUG_ON(event < WIFI_2G4_COEX_MAX);

	if (vif_idx < NX_ITF_MAX) {
		spin_lock(&wifi_coex_lock);
		if (set)
			wifi_coex_vif_state[vif_idx] |= BIT(event);
		else
			wifi_coex_vif_state[vif_idx] &= ~BIT(event);
		spin_unlock(&wifi_coex_lock);
	} else {
		bes_err(BES_DBG_COEX, "%s unexpected vif_index: %d", __func__, vif_idx);
	}
}

void bes_coex_wifi_init(void)
{
	memset(wifi_coex_vif_state, 0, sizeof(wifi_coex_vif_state));
}
