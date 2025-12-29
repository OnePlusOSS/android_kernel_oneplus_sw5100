/*
 * @file bes_vif_state.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "rwnx_main.h"
#include "bes_vif_state.h"

void bes_cancel_get_ip_timeout_work(struct rwnx_vif *rwnx_vif)
{
	/* Check if rwnx_vif is valid */
	if (!rwnx_vif)
		return;

	/* Cancel the delyaed work of getting ip timeout and reset the related parameters */
	cancel_delayed_work_sync(&rwnx_vif->get_ip_timeout_work);
	rwnx_vif->wait_conn_end = false;
	if (test_bit(RWNX_DEV_CONNECTING, &rwnx_vif->rwnx_hw->flags)) {
		rwnx_wf_status_clear(rwnx_vif->rwnx_hw, RWNX_DEV_CONNECTING);
		up(&rwnx_vif->rwnx_hw->phy_op_sema);
	}
}

static void bes_get_ip_timeout_work_func(struct work_struct *work)
{
	struct rwnx_vif *rwnx_vif = container_of(work, struct rwnx_vif, get_ip_timeout_work.work);
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;

	/* Check if rwnx_vif is valid */
	if (!rwnx_vif->up)
		return;

	/* Check if vif is STA or GC  */
	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_STATION &&
	    RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_P2P_CLIENT)
		return;

	/* Check if vif is not waitting the end of connection */
	if (!rwnx_vif->wait_conn_end)
		return;

	/* Clear the RWNX_DEV_CONNECTING bit when obtaining IP timeout */
	rwnx_vif->wait_conn_end = false;
	rwnx_wf_status_clear(rwnx_hw, RWNX_DEV_CONNECTING);
	up(&rwnx_hw->phy_op_sema);
}

bool bes_vif_state_check_conn_finished(struct rwnx_hw *rwnx_hw)
{
	struct rwnx_vif *rwnx_vif = NULL;

	spin_lock_bh(&rwnx_hw->cb_lock);
	list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
		/* Check if rwnx_vif is up */
		if (!netif_running(rwnx_vif->ndev))
			continue;

		/* Check if vif is STA or GC  */
		if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_STATION &&
		    RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_P2P_CLIENT)
			continue;

		/* Check if vif is not waitting the end of connection */
		if (!rwnx_vif->wait_conn_end)
			continue;

		spin_unlock_bh(&rwnx_hw->cb_lock);
		bes_warn(BES_DBG_WOWLAN,
			"The vif-%d(%s) is waiting for the completion of obtaining IP, and host exits suspend directly\n",
			rwnx_vif->vif_index, rwnx_vif->ndev->name);
		return false;
	}
	spin_unlock_bh(&rwnx_hw->cb_lock);

	return true;
}

static void bes_vif_state_reset_param(struct rwnx_vif *rwnx_vif)
{
	rwnx_vif->ipv4_addr = 0;
	memset(&rwnx_vif->ipv6_addr, 0, sizeof(struct in6_addr));
	rwnx_vif->sta.gateway_ip = 0;
	rwnx_vif->wait_conn_end = false;
}

void bes_vif_state_process(struct rwnx_vif *rwnx_vif, u8 state)
{
	struct rwnx_hw *rwnx_hw = NULL;

	/* Check if the parameters are valid */
	if (!rwnx_vif || !(rwnx_hw = rwnx_vif->rwnx_hw) ||
	    state >= VIF_STATE_MAX)
		return;

	/* Process according to parameters */
	switch(state) {
		case VIF_STATE_OPEN:
			bes_vif_state_reset_param(rwnx_vif);
			break;
		case VIF_STATE_CONNECTED:
			if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION ||
			    RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_CLIENT) {
				if (rwnx_vif->sta.conn_abort ||
				    !netif_carrier_ok(rwnx_vif->ndev)) {
					rwnx_vif->wait_conn_end = false;
					rwnx_wf_status_clear(rwnx_hw, RWNX_DEV_CONNECTING);
					up(&rwnx_hw->phy_op_sema);
				} else {
					rwnx_vif->wait_conn_end = true;
				}
			} else {
				rwnx_wf_status_clear(rwnx_hw, RWNX_DEV_CONNECTING);
				up(&rwnx_hw->phy_op_sema);
			}
			break;
		case VIF_STATE_AUTHORIZED:
			if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION ||
			    RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_CLIENT) {
				if (rwnx_vif->wait_conn_end) {
					if (!rwnx_vif->ipv4_addr) {
						queue_delayed_work(rwnx_hw->rwnx_wq,
								   &rwnx_vif->get_ip_timeout_work,
								   msecs_to_jiffies(1500));
					} else {
						rwnx_vif->wait_conn_end = false;
						rwnx_wf_status_clear(rwnx_hw, RWNX_DEV_CONNECTING);
						up(&rwnx_hw->phy_op_sema);
					}
				}
			}
			break;
		case VIF_STATE_GETTED_IP:
			if (rwnx_vif->wait_conn_end)
				bes_cancel_get_ip_timeout_work(rwnx_vif);
			break;
		case VIF_STATE_DISCONNECTED:
			if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION ||
			    RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_CLIENT) {
				if (rwnx_vif->wait_conn_end)
					bes_cancel_get_ip_timeout_work(rwnx_vif);
			}
			break;
		case VIF_STATE_CLOSE:
			bes_vif_state_reset_param(rwnx_vif);
			break;
	}
}

void bes_vif_state_init(struct rwnx_vif *rwnx_vif)
{
	/* Check if rwnx_vif is valid */
	if (!rwnx_vif)
		return;

	bes_vif_state_reset_param(rwnx_vif);
	INIT_DELAYED_WORK(&rwnx_vif->get_ip_timeout_work, bes_get_ip_timeout_work_func);
}
