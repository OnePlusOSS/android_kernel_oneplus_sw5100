/*
 * @file bes_conn_state.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "rwnx_main.h"
#include "bes_conn_state.h"

extern struct rwnx_plat g_rwnx_plat;

const u32 sta_conn_flag_mask = BIT(STA_CONN_FLAG_CONN) | BIT(STA_CONN_FLAG_PORT_AUTH);

static char *bes_conn_state_get_action_name(u8 action)
{
	if (action == STA_CONN_ACTION_INIT)
		return "INIT";
	else if (action == STA_CONN_ACTION_CONN_REQ)
		return "CONN_REQ";
	else if (action == STA_CONN_ACTION_CONN_IND)
		return "CONN_IND";
	else if (action == STA_CONN_ACTION_PORT_AUTH)
		return "PORT_AUTH";
	else if (action == STA_CONN_ACTION_GET_IP)
		return "GET_IP";
	else if (action == STA_CONN_ACTION_DISCONN_REQ)
		return "DISCONN_REQ";
	else if (action == STA_CONN_ACTION_DEINIT)
		return "DEINIT";
	else if (action == STA_CONN_ACTION_DISCONN_IND)
		return "DISCONN_IND";
	else
		return "UNKOWN";
}

static void bes_conn_state_ip_tmo_work_func(struct work_struct *work)
{
	struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)(g_rwnx_plat.drvdata);
	struct rwnx_sta *rwnx_sta = container_of(work, struct rwnx_sta, ip_tmo_work.work);
	struct rwnx_vif *rwnx_vif = NULL;

	/* Check if rwnx_hw and rwnx_sta is valid */
	if (!rwnx_hw || !rwnx_sta) {
		bes_warn(BES_DBG_CONN_STATE, "%s WARNING: rwnx_hw:%p rwnx_sta:%p\n", __func__, rwnx_hw, rwnx_sta);
		return;
	}

	/* Check if the parameters of vif is valid */
	spin_lock_bh(&rwnx_hw->cb_lock);
	rwnx_vif = rwnx_rx_get_vif(rwnx_hw, rwnx_sta->vif_idx);
	spin_unlock_bh(&rwnx_hw->cb_lock);
	if (!rwnx_vif) {
		bes_warn(BES_DBG_CONN_STATE, "%s WARNING: {VIF-%d STA-%d} Fail to get rwnx_vif\n",
			 __func__, rwnx_sta->vif_idx, rwnx_sta->sta_idx);
		return;
	}

	/* Reset parameters when obtaining IP timeout */
	spin_lock_bh(&rwnx_sta->conn_state_lock);
	if (rwnx_sta->wait_conn_end == sta_conn_flag_mask) {
		rwnx_sta->wait_conn_end = 0;
		spin_unlock_bh(&rwnx_sta->conn_state_lock);
		bes_dbg(BES_DBG_CONN_STATE, "%s {VIF-%d STA-%d} ip_tmo work is timeout\n",
			__func__, rwnx_sta->vif_idx, rwnx_sta->sta_idx);
		if (rwnx_wf_status_test_and_clear(rwnx_hw, RWNX_DEV_CONNECTING))
			up(&rwnx_hw->phy_op_sema);
	} else {
		spin_unlock_bh(&rwnx_sta->conn_state_lock);
		bes_warn(BES_DBG_CONN_STATE, "%s {VIF-%d STA-%d} wait_conn_end is 0x%x !!\n",
			__func__, rwnx_sta->vif_idx, rwnx_sta->sta_idx, rwnx_sta->wait_conn_end);
	}
}

static void bes_conn_state_init_handler(struct rwnx_vif *rwnx_vif, struct rwnx_sta *rwnx_sta)
{
	/* Check if rwnx_sta is valid */
	if (!rwnx_sta) {
		bes_warn(BES_DBG_CONN_STATE, "%s WARNING: rwnx_sta is NULL\n", __func__);
		return;
	}

	/* Init the parameters */
	spin_lock_init(&rwnx_sta->conn_state_lock);
	spin_lock_bh(&rwnx_sta->conn_state_lock);
	rwnx_sta->wait_conn_end = 0;
	spin_unlock_bh(&rwnx_sta->conn_state_lock);
	INIT_DELAYED_WORK(&rwnx_sta->ip_tmo_work, bes_conn_state_ip_tmo_work_func);
}

static void bes_conn_state_connect_req_handler(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, struct rwnx_sta *rwnx_sta)
{
	/* After being connected, if connecting again, the initial state should be restored */
	if (rwnx_sta) {
		spin_lock_bh(&rwnx_sta->conn_state_lock);
		rwnx_sta->wait_conn_end = 0;
		spin_unlock_bh(&rwnx_sta->conn_state_lock);

		bes_dbg(BES_DBG_CONN_STATE, "%s {STA-%d} cancel ip_tmo work and wait for it to finish\n",
			__func__, rwnx_sta->sta_idx);
		cancel_delayed_work_sync(&rwnx_sta->ip_tmo_work);
	}

	/* Release phy_op_sema when it is connecting */
	if (rwnx_wf_status_test_and_clear(rwnx_hw, RWNX_DEV_CONNECTING)) {
		bes_dbg(BES_DBG_CONN_STATE, "%s Release phy_op_sema\n", __func__);
		up(&rwnx_hw->phy_op_sema);
	}
}

static void bes_conn_state_connect_ind_handler(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, struct rwnx_sta *rwnx_sta)
{
	/* Releasing the semaphore if connection is failed. */
	if (!rwnx_sta) {
		bes_dbg(BES_DBG_CONN_STATE, "%s {VIF-%d} connection is failed.\n",
			__func__, rwnx_vif->vif_index);
		if (rwnx_wf_status_test_and_clear(rwnx_hw, RWNX_DEV_CONNECTING))
			up(&rwnx_hw->phy_op_sema);
		return;
	}

	spin_lock_bh(&rwnx_sta->conn_state_lock);
	/* If connection is aborted, releasing the semaphore.
	 * "rwnx_vif->sta.conn_abort": indicates that the connection was aborted before completion.
	 * "(rwnx_sta->wait_conn_end & BIT(STA_CONN_FLAG_CONN_ABORT))": indicates that the connection was aborted
	 * 								when the processing of connect_ind msg.
	 */
	if (rwnx_vif->sta.conn_abort ||
	    (rwnx_sta->wait_conn_end & BIT(STA_CONN_FLAG_CONN_ABORT))) {
		rwnx_sta->wait_conn_end = 0;
		spin_unlock_bh(&rwnx_sta->conn_state_lock);

		bes_dbg(BES_DBG_CONN_STATE, "%s {VIF-%d} connection aborted.\n",
			__func__, rwnx_vif->vif_index);

		if (rwnx_wf_status_test_and_clear(rwnx_hw, RWNX_DEV_CONNECTING))
			up(&rwnx_hw->phy_op_sema);
		return;
	}

	/* If not settinng AUTH flag, marking the entry into the 4-ways handshake and dhcp phase for sucessful connection.
	   Otherwise, it still need to marke the end of connection when obtained IP, and queue ip_tmo work when not obtained IP. */
	rwnx_sta->wait_conn_end |= BIT(STA_CONN_FLAG_CONN);
	if (rwnx_sta->wait_conn_end != sta_conn_flag_mask) {
		spin_unlock_bh(&rwnx_sta->conn_state_lock);
		return;
	}
	if (rwnx_vif->ipv4_addr) { // for roam case
		rwnx_sta->wait_conn_end = 0;
		spin_unlock_bh(&rwnx_sta->conn_state_lock);
		bes_dbg(BES_DBG_CONN_STATE, "%s {VIF-%d STA-%d} IP has been getted\n",
			__func__, rwnx_sta->vif_idx, rwnx_sta->sta_idx);
		if (rwnx_wf_status_test_and_clear(rwnx_hw, RWNX_DEV_CONNECTING))
			up(&rwnx_hw->phy_op_sema);
	} else { // for normal case
		spin_unlock_bh(&rwnx_sta->conn_state_lock);
		bes_dbg(BES_DBG_CONN_STATE, "%s {VIF-%d STA-%d} queue ip_tmo work.timeout:%dms\n",
			__func__, rwnx_sta->vif_idx, rwnx_sta->sta_idx, BES_CONN_STATE_GET_IP_TIMEOUT);
		queue_delayed_work(rwnx_hw->rwnx_wq, &rwnx_sta->ip_tmo_work,
				   msecs_to_jiffies(BES_CONN_STATE_GET_IP_TIMEOUT));
	}
}

static void bes_conn_state_port_authorized_handler(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, struct rwnx_sta *rwnx_sta)
{
	/* Check if rwnx_sta is valid */
	if (!rwnx_sta) {
		bes_warn(BES_DBG_CONN_STATE, "%s WARNING: {VIF-%d} Fail to get rwnx_sta\n",
			 __func__, rwnx_vif->vif_index);
		return;
	}

	spin_lock_bh(&rwnx_sta->conn_state_lock);
	/* If an active disconnection has occurred, return directly.
	   And releasing the semaphore when the processing of connect_ind msg */
	if (rwnx_sta->wait_conn_end & BIT(STA_CONN_FLAG_CONN_ABORT)) {
		spin_unlock_bh(&rwnx_sta->conn_state_lock);
		return;
	}

	/* If not settinng CONN flag, marking authentication completed.
	   Otherwise, it still need to marke the end of connection when obtained IP, and queue ip_tmo work when not obtained IP. */
	rwnx_sta->wait_conn_end |= BIT(STA_CONN_FLAG_PORT_AUTH);
	if (rwnx_sta->wait_conn_end != sta_conn_flag_mask) {
		spin_unlock_bh(&rwnx_sta->conn_state_lock);
		return;
	}
	if (rwnx_vif->ipv4_addr) { // for roam case
		rwnx_sta->wait_conn_end = 0;
		spin_unlock_bh(&rwnx_sta->conn_state_lock);
		bes_dbg(BES_DBG_CONN_STATE, "%s {VIF-%d STA-%d} IP has been getted\n",
			__func__, rwnx_sta->vif_idx, rwnx_sta->sta_idx);
		if (rwnx_wf_status_test_and_clear(rwnx_hw, RWNX_DEV_CONNECTING))
			up(&rwnx_hw->phy_op_sema);
	} else { // for normal case
		spin_unlock_bh(&rwnx_sta->conn_state_lock);
		bes_dbg(BES_DBG_CONN_STATE, "%s {VIF-%d STA-%d} queue ip_tmo work.timeout:%dms\n",
			__func__, rwnx_sta->vif_idx, rwnx_sta->sta_idx, BES_CONN_STATE_GET_IP_TIMEOUT);
		queue_delayed_work(rwnx_hw->rwnx_wq, &rwnx_sta->ip_tmo_work,
				   msecs_to_jiffies(BES_CONN_STATE_GET_IP_TIMEOUT));
	}
}

static void bes_conn_state_got_ip_handler(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, struct rwnx_sta *rwnx_sta)
{
	/* Check if rwnx_sta is NULL */
	if (!rwnx_sta) {
		bes_warn(BES_DBG_CONN_STATE, "%s WARNING: rwnx_sta is NULL\n", __func__);
		return;
	}

	/* Check if the end of CONN and AUTH . And clear both */
	spin_lock_bh(&rwnx_sta->conn_state_lock);
	if (rwnx_sta->wait_conn_end != sta_conn_flag_mask && rwnx_sta->wait_conn_end != 0) {
		spin_unlock_bh(&rwnx_sta->conn_state_lock);
		bes_warn(BES_DBG_CONN_STATE, "%s {STA-%d} WARNING: wait_conn_end is 0x%x and not equel to 0x%x !!\n",
			 __func__, rwnx_sta->sta_idx, rwnx_sta->wait_conn_end, sta_conn_flag_mask);
		return;
	}
	rwnx_sta->wait_conn_end = 0;
	spin_unlock_bh(&rwnx_sta->conn_state_lock);

	/* Cancel ip_tmo work and wait for it to finish after getting IP */
	bes_dbg(BES_DBG_CONN_STATE, "%s {STA-%d} cancel ip_tmo work and wait for it to finish\n",
		__func__, rwnx_sta->sta_idx);
	cancel_delayed_work_sync(&rwnx_sta->ip_tmo_work);

	/* Release phy_op_sema when it is connecting */
	if (rwnx_wf_status_test_and_clear(rwnx_hw, RWNX_DEV_CONNECTING)) {
		bes_dbg(BES_DBG_CONN_STATE, "%s  {STA-%d} Release phy_op_sema\n",
			__func__, rwnx_sta->sta_idx);
		up(&rwnx_hw->phy_op_sema);
	}
}

static void bes_conn_state_disconnect_req_handler(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif)
{
	struct rwnx_sta *rwnx_sta = NULL;

	/* If the disconnect process is entered before the connect
	 * process is done, this connect process need to be aborted
	 */
	if (!netif_carrier_ok(rwnx_vif->ndev)) {
		rwnx_vif->sta.conn_abort = true;
	}

	/* Proactively disconnect before the connect indication process is completed */
	spin_lock_bh(&rwnx_hw->cb_lock);
	rwnx_sta = rwnx_vif->sta.ap;
	if (!rwnx_sta) {
		spin_unlock_bh(&rwnx_hw->cb_lock);
		bes_dbg(BES_DBG_CONN_STATE, "%s {VIF-%d} Proactively disconnect before the connect indication process is completed\n",
			__func__, rwnx_vif->vif_index);
		return;
	}
	spin_unlock_bh(&rwnx_hw->cb_lock);

	/* The handling of disconnection occurring during the connection process:
	   1) When disconnection after the completion of the processing of connect_ind msg,
	      reseting the parameters and releasing phy_op_sema.
	   2) Otherwise, set the flag to indicate that an active disconnection has occurred */
	spin_lock_bh(&rwnx_sta->conn_state_lock);
	if (rwnx_sta->wait_conn_end != 0) {
		rwnx_sta->wait_conn_end = 0;
		spin_unlock_bh(&rwnx_sta->conn_state_lock);
		cancel_delayed_work_sync(&rwnx_sta->ip_tmo_work);
		if (rwnx_wf_status_test_and_clear(rwnx_hw, RWNX_DEV_CONNECTING)) {
			bes_dbg(BES_DBG_CONN_STATE, "%s {VIF-%d STA-%d} Release phy_op_sema\n",
				__func__, rwnx_sta->vif_idx, rwnx_sta->sta_idx);
			up(&rwnx_hw->phy_op_sema);
		}
		return;
	} else {
		rwnx_sta->wait_conn_end |= BIT(STA_CONN_FLAG_CONN_ABORT);
	}
	spin_unlock_bh(&rwnx_sta->conn_state_lock);
}

static void bes_conn_state_deinit_handler(struct rwnx_vif *rwnx_vif, struct rwnx_sta *rwnx_sta)
{
	/* Check if rwnx_sta is NULL */
	if (!rwnx_sta) {
		bes_warn(BES_DBG_CONN_STATE, "%s WARNING: rwnx_sta is NULL\n", __func__);
		return;
	}

	/* Cancel ip_tmo work and wait for it to finish when disconencting */
	spin_lock_bh(&rwnx_sta->conn_state_lock);
	if (rwnx_sta->wait_conn_end != 0) {
		rwnx_sta->wait_conn_end = 0;
		spin_unlock_bh(&rwnx_sta->conn_state_lock);
		bes_dbg(BES_DBG_CONN_STATE, "%s {VIF-%d STA-%d} cancel ip_tmo work and wait for it to finish\n",
			__func__, rwnx_sta->vif_idx, rwnx_sta->sta_idx);
		cancel_delayed_work_sync(&rwnx_sta->ip_tmo_work);
		return;
	}
	spin_unlock_bh(&rwnx_sta->conn_state_lock);
}

static void bes_conn_state_disconnect_ind_handler(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif)
{
	/* Release phy_op_sema when it is connecting */
	if (rwnx_wf_status_test_and_clear(rwnx_hw, RWNX_DEV_CONNECTING)) {
		bes_dbg(BES_DBG_CONN_STATE, "%s {VIF-%d} Release phy_op_sema\n",
			__func__, rwnx_vif->vif_index);
		up(&rwnx_hw->phy_op_sema);
	}
}

void bes_conn_state_process(struct rwnx_vif *rwnx_vif, struct rwnx_sta *rwnx_sta, u8 action)
{
	struct rwnx_hw *rwnx_hw = NULL;

	/* Check if the parameters are valid */
	if (!rwnx_vif || action >= STA_CONN_ACTION_MAX) {
		bes_warn(BES_DBG_CONN_STATE, "%s WARNING: rwnx_vif is NULL or action(%d) is invalid\n",
			 __func__, action);
		return;
	}

	/*  Do nothing if vif is not sta or gc */
	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_STATION &&
	    RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_P2P_CLIENT)
		return;

	/* Get rwnx_sta and rwnx_hw pointer */
	rwnx_hw = rwnx_vif->rwnx_hw;
	if (!rwnx_hw) {
		bes_warn(BES_DBG_CONN_STATE, "%s WARNING: rwnx_hw is NULL\n", __func__);
		return;
	}

	/* Process according to parameters */
	switch(action) {
		case STA_CONN_ACTION_INIT:
			bes_conn_state_init_handler(rwnx_vif, rwnx_sta);
			break;
		case STA_CONN_ACTION_CONN_REQ:
			bes_conn_state_connect_req_handler(rwnx_hw, rwnx_vif, rwnx_sta);
			break;
		case STA_CONN_ACTION_CONN_IND:
			bes_conn_state_connect_ind_handler(rwnx_hw, rwnx_vif, rwnx_sta);
			break;
		case STA_CONN_ACTION_PORT_AUTH:
			bes_conn_state_port_authorized_handler(rwnx_hw, rwnx_vif, rwnx_sta);
			break;
		case STA_CONN_ACTION_GET_IP:
			bes_conn_state_got_ip_handler(rwnx_hw, rwnx_vif, rwnx_sta);
			break;
		case STA_CONN_ACTION_DISCONN_REQ:
			bes_conn_state_disconnect_req_handler(rwnx_hw, rwnx_vif);
			break;
		case STA_CONN_ACTION_DEINIT:
			bes_conn_state_deinit_handler(rwnx_vif, rwnx_sta);
			break;
		case STA_CONN_ACTION_DISCONN_IND:
			bes_conn_state_disconnect_ind_handler(rwnx_hw, rwnx_vif);
			break;
	}

	/* Print conn state information */
	if (rwnx_sta)
		bes_dbg(BES_DBG_CONN_STATE, "%s {VIF-%d STA-%d} action:%s wait_conn_end:%x\n",
			__func__, rwnx_sta->vif_idx, rwnx_sta->sta_idx, bes_conn_state_get_action_name(action),
			rwnx_sta->wait_conn_end);
	else
		bes_dbg(BES_DBG_CONN_STATE, "%s {VIF-%d} action:%s rwnx_sta is NULL\n",
			__func__, rwnx_vif->vif_index, bes_conn_state_get_action_name(action));
}

void bes_conn_state_init(struct rwnx_vif *rwnx_vif)
{
	/* Check if rwnx_vif is valid */
	if (!rwnx_vif) {
		bes_warn(BES_DBG_CONN_STATE, "%s WARNING: rwnx_vif is NULL\n", __func__);
		return;
	}

	/* Init the parameters of ip */
	rwnx_vif->ipv4_addr = 0;
	memset(&rwnx_vif->ipv6_addr, 0, sizeof(struct in6_addr));
	rwnx_vif->sta.gateway_ip = 0;
}