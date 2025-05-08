/*
 * @file bes_pwr.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "bes_psa.h"
#include "bes_log.h"
#include "rwnx_strs.h"
#include "lmac_msg.h"
#include "bes_sleep_manager.h"
#include "bes_pwr.h"
#include "bes_wowlan.h"
#include "bes_filter.h"
#include "rwnx_main.h"
#include "rwnx_msg_tx.h"
#include "bes_vif_info.h"
#include "bes_data_monitor.h"

/* Wifi lp event mask */
#define PHY_EVENT_MASK			((1 << PHY_EVENT_MAX) - 1)
#define PHY_DELAY_EVENT_MASK		((1 << PHY_CONST_EVENT_MIN) - 1)
#define PHY_CONST_EVENT_MASK		(PHY_EVENT_MASK - PHY_DELAY_EVENT_MASK)
#define VIF_EVENT_MASK			((1 << VIF_EVENT_MAX) - 1)
#define VIF_DELAY_EVENT_MASK		((1 << VIF_CONST_EVENT_MIN) - 1)
#define VIF_CONST_EVENT_MASK		(VIF_EVENT_MASK - VIF_DELAY_EVENT_MASK)

/* In order to speed up the calculation */
const u32 phy_delay_event_mask = PHY_DELAY_EVENT_MASK;
const u32 phy_const_event_mask = PHY_CONST_EVENT_MASK;
const u32 vif_delay_event_mask = VIF_DELAY_EVENT_MASK;
const u32 vif_const_event_mask = VIF_CONST_EVENT_MASK;

/* Current schedule time for delay_event */
static unsigned long phy_timeout[PHY_CONST_EVENT_MIN];
static unsigned long vif_timeout[NX_ITF_MAX][VIF_CONST_EVENT_MIN];

/* Default deleyed event schedule timeout(ms) */
const struct pwr_phy_d_evt_timeout phy_d_evt_t_default[PHY_CONST_EVENT_MIN] = {
	{.id = PHY_DELAY_EVENT_CONST_CLR,	.timeout = 100},
};
const struct pwr_vif_d_evt_timeout vif_d_evt_t_default[VIF_CONST_EVENT_MIN] = {
	{.id = VIF_DELAY_EVENT_DATA_TX_CLR,	.timeout = 1500},
	{.id = VIF_DELAY_EVENT_DATA_RX,		.timeout = DATA_MONITOR_SAMPLE_CYCLE + 500},
	{.id = VIF_DELAY_EVENT_TX_KEY_FRM,	.timeout = 1500},
	{.id = VIF_DELAY_EVENT_ADD_VIF,		.timeout = 100},
	{.id = VIF_DELAY_EVENT_DEL_VIF,		.timeout = 100},
	{.id = VIF_DELAY_EVENT_CH_LOAD_MEASURE,	.timeout = 100},
};

/* Uapsd deleyed event schedule timeout(ms) */
const struct pwr_phy_d_evt_timeout phy_d_evt_t_uapsd[PHY_CONST_EVENT_MIN] = {
	{.id = PHY_DELAY_EVENT_CONST_CLR,	.timeout = 100},
};
const struct pwr_vif_d_evt_timeout vif_d_evt_t_uapsd[VIF_CONST_EVENT_MIN] = {
	{.id = VIF_DELAY_EVENT_DATA_TX_CLR,	.timeout = 0},
	{.id = VIF_DELAY_EVENT_DATA_RX,		.timeout = 0},
	{.id = VIF_DELAY_EVENT_TX_KEY_FRM,	.timeout = 1500},
	{.id = VIF_DELAY_EVENT_ADD_VIF,		.timeout = 100},
	{.id = VIF_DELAY_EVENT_DEL_VIF,		.timeout = 100},
	{.id = VIF_DELAY_EVENT_CH_LOAD_MEASURE,	.timeout = 100},
};

/* timeout(jiffies) for delay_event */
static struct pwr_phy_d_evt_timeout phy_d_evt_timeout[PHY_CONST_EVENT_MIN];
static struct pwr_vif_d_evt_timeout vif_d_evt_timeout[VIF_CONST_EVENT_MIN];

int bes_pwr_register_lp_cb(struct rwnx_hw *rwnx_hw, bes_pwr_lp_cb cb, void *arg)
{
	int i = 0, ret = 0;
	struct bes_pwr_lp_cb_item *item = NULL, *assign = NULL;

	/* Check if rwnx_hw is invalid */
	if (!rwnx_hw || !rwnx_hw->pwr.cb_tbl) {
		ret = -EINVAL;
		goto out;
	}

	spin_lock_bh(&rwnx_hw->pwr.pwr_cb_lock);
	/* The maximum value has been reached */
	if (rwnx_hw->pwr.cb_cnt >= PWR_CB_CNT_MAX) {
		spin_unlock_bh(&rwnx_hw->pwr.pwr_cb_lock);
		ret = -ERANGE;
		goto out;
	}

	/* Check if cb is already exist */
	for (i = 0; i < PWR_CB_CNT_MAX; i++) {
		item = &rwnx_hw->pwr.cb_tbl[i];

		if (assign == NULL && item->cb == NULL)
			assign = item;

		if (item->cb == cb) {
			spin_unlock_bh(&rwnx_hw->pwr.pwr_cb_lock);
			ret = -EEXIST;
			goto out;

		}
	}

	/* Not find the postion to assign */
	if (!assign) {
		spin_unlock_bh(&rwnx_hw->pwr.pwr_cb_lock);
		ret = -EFAULT;
		goto out;
	}

	/* Add cb to tbl */
	assign->cb = cb;
	assign->arg = arg;
	rwnx_hw->pwr.cb_cnt++;
	spin_unlock_bh(&rwnx_hw->pwr.pwr_cb_lock);

out:
	bes_err_with_cond(ret, BES_DBG_PWR, "%s ret=%d\n", __func__, ret);
	return ret;
}

void bes_pwr_unregister_lp_cb(struct rwnx_hw *rwnx_hw, bes_pwr_lp_cb cb)
{
	int i = 0;
	struct bes_pwr_lp_cb_item *item = NULL;

	/* Check if rwnx_hw is invalid */
	if (!rwnx_hw || !rwnx_hw->pwr.cb_tbl)
		return;

	spin_lock_bh(&rwnx_hw->pwr.pwr_cb_lock);
	/* No callback function registed, return directly */
	if (rwnx_hw->pwr.cb_cnt == 0) {
		spin_unlock_bh(&rwnx_hw->pwr.pwr_cb_lock);
		return;
	}

	/* Delete cb from tbl */
	for (i = 0; i < PWR_CB_CNT_MAX; i++) {
		item = &rwnx_hw->pwr.cb_tbl[i];
		if (cb != item->cb)
			continue;

		item->cb = NULL;
		item->arg = NULL;
		rwnx_hw->pwr.cb_cnt--;
		break;
	}
	spin_unlock_bh(&rwnx_hw->pwr.pwr_cb_lock);
}

void bes_pwr_call_lp_cb(struct rwnx_hw *rwnx_hw, enum pwr_lp_cb_status status)
{
	int i = 0;
	struct bes_pwr_lp_cb_item item;

	/* Check if rwnx_hw is invalid */
	if (!rwnx_hw || !rwnx_hw->pwr.cb_tbl)
		return;

	spin_lock_bh(&rwnx_hw->pwr.pwr_cb_lock);
	/* No callback function registed, return directly */
	if (rwnx_hw->pwr.cb_cnt == 0) {
		spin_unlock_bh(&rwnx_hw->pwr.pwr_cb_lock);
		return;
	}

	/* Traverse and execute callback functions */
	for (i = 0; i < PWR_CB_CNT_MAX; i++) {
		item.arg = rwnx_hw->pwr.cb_tbl[i].arg;
		item.cb = rwnx_hw->pwr.cb_tbl[i].cb;
		spin_unlock_bh(&rwnx_hw->pwr.pwr_cb_lock);
		if (item.cb != NULL)
			item.cb(item.arg, status);
		spin_lock_bh(&rwnx_hw->pwr.pwr_cb_lock);
	}
	spin_unlock_bh(&rwnx_hw->pwr.pwr_cb_lock);
}

static char *bes_pwr_get_lp_state_name(enum pwr_wifi_lp_state st)
{
	char *name = NULL;

	switch(st) {
		case WIFI_LP_WAKING:			name = "WAKING";	break;
		case WIFI_LP_WAKE_CONFIG:		name = "WAKE_CONFIG";	break;
		case WIFI_LP_WAKE:			name = "WAKE";		break;
		case WIFI_LP_SLEEP_CONFIG:		name = "SLEEP_CONFIG";	break;
		case WIFI_LP_SLEEPING:			name = "SLEEPING";	break;
		case WIFI_LP_SLEEPED:			name = "SLEEPED";	break;
		default:				name = "UNKNOW";	break;
	}

	return name;
}

static char *bes_pwr_get_action_name(enum pwr_action_type ac)
{
	char *name = NULL;

	switch(ac) {
		case PWR_ACTION_WAKEUP:			name = "WAKEUP";	break;
		case PWR_ACTION_WAKEUP_NOTIFY:		name = "WAKEUP_NOTIFY";	break;
		case PWR_ACTION_SLEEP_NOTIFY:		name = "SLEEP_NOTIFY";	break;
		case PWR_ACTION_SLEEP:			name = "SLEEP";		break;
		case PWR_ACTION_SCHED:			name = "SCHED";		break;
		case PWR_ACTION_WAIT_WAKE:		name = "WAIT_WAKE";	break;
		default:				name = "UNKNOW";	break;
	}

	return name;
}

static char *bes_pwr_get_phy_event_name(enum pwr_phy_event_type event)
{
	char *name = NULL;

	switch(event) {
		case PHY_DELAY_EVENT_CONST_CLR:		name = "P_C_CLR";	break;
		case PHY_CONST_EVENT_CONCURRENT:	name = "CONCURRENT";	break;
		case PHY_CONST_EVENT_SCAN:		name = "SCAN";		break;
		case PHY_CONST_EVENT_ROC:		name = "ROC";		break;
		case PHY_CONST_EVENT_MSG_TX:		name = "MSG_TX";	break;
		case PHY_CONST_EVENT_MSG_TX_PENDING:	name = "MSG_TX_P";	break;
		case PHY_CONST_EVENT_DATA_TX:		name = "P_DATA_TX";	break;
		case PHY_CONST_EVENT_INIT:		name = "INIT";		break;
		case PHY_CONST_EVENT_DLY_WORK_FLUSH:	name = "DLY_WORK_FLUSH";break;
		default:				name = "UNKNOW";	break;
	}

	return name;
}

static char *bes_pwr_get_vif_event_name(enum pwr_vif_event_type event)
{
	char *name = NULL;

	switch(event) {
		case VIF_DELAY_EVENT_DATA_TX_CLR:	name = "V_D_T_CLR";	break;
		case VIF_DELAY_EVENT_DATA_RX:		name = "V_DATA_RX";	break;
		case VIF_DELAY_EVENT_TX_KEY_FRM:	name = "TX_KEY_FRM";	break;
		case VIF_DELAY_EVENT_ADD_VIF:		name = "ADD_VIF";	break;
		case VIF_DELAY_EVENT_DEL_VIF:		name = "DEL_VIF";	break;
		case VIF_DELAY_EVENT_CH_LOAD_MEASURE:	name = "CH_LOAD_M";	break;
		case VIF_CONST_EVENT_CONNECT:		name = "CONNECT";	break;
		case VIF_CONST_EVENT_AP:		name = "AP";		break;
		case VIF_CONST_EVENT_CSA:		name = "CSA";		break;
		case VIF_CONST_EVENT_ROAMED:		name = "ROAMED";	break;
		case VIF_CONST_EVENT_DATA_TX:		name = "V_DATA_TX";	break;
		case VIF_CONST_EVENT_GO_PRESENT:	name = "GO_PRESENT";	break;
		case VIF_CONST_EVENT_PSM:		name = "PSM";		break;
		case VIF_CONST_EVENT_MONITOR:		name = "MONITOR";	break;
		default:				name = "UNKNOW";	break;
	}

	return name;
}

static bool bes_pwr_exist_vif_const_event(struct rwnx_hw *rwnx_hw)
{
	u8 vif_idx = 0;
	struct rwnx_vif *rwnx_vif = NULL;
	bool exist = false;

	for (vif_idx = 0; vif_idx < NX_ITF_MAX; vif_idx++) {
		u8 idx;

		/* vif needs to be open */
		rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);
		if (!rwnx_vif)
			continue;

		/* Check the validity of idx */
		idx = rwnx_vif->vif_index;
		if (idx >= NX_ITF_MAX)
			continue;

		if (!rwnx_vif->in_csa && /* rwnx_vif->ch_index may be invalid in the CSA process */
		    rwnx_vif->ch_index != rwnx_hw->pwr.cur_chanctx && /* chanctx is equal */
		    rwnx_vif->ch_index != RWNX_CH_NOT_SET && /* chanctx is already set */
		    rwnx_hw->pwr.cur_chanctx < NX_CHAN_CTXT_CNT /* cur chanctx is valid */)
			continue;

		/* Determine if there is an const_event on this vif */
		if (bes_pwr_get_vif_const_event(&rwnx_hw->pwr, idx)) {
			exist = true;
			break;
		}
	}

	return exist;
}

static bool bes_pwr_handle_const_evt_before_suspend(struct rwnx_hw *rwnx_hw)
{
	u8 vif_idx = 0;
	struct rwnx_vif *rwnx_vif = NULL;
	bool exist = false;
	u32 phy_const_event_for_exit = BIT(PHY_CONST_EVENT_CONCURRENT) | BIT(PHY_CONST_EVENT_ROC) |
				       BIT(PHY_CONST_EVENT_INIT);
	u32 vif_const_event_for_exit = BIT(VIF_CONST_EVENT_CONNECT) | BIT(VIF_CONST_EVENT_AP) |
				       BIT(VIF_CONST_EVENT_CSA) | BIT(VIF_CONST_EVENT_ROAMED) |
				       BIT(VIF_CONST_EVENT_GO_PRESENT) | BIT(VIF_CONST_EVENT_PSM) |
				       BIT(VIF_CONST_EVENT_MONITOR);

#ifdef CONFIG_SCAN_BLOCK_SUSPEND
	phy_const_event_for_exit |= BIT(PHY_CONST_EVENT_SCAN);
#endif

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	do {
		if (bes_pwr_get_phy_const_event(&rwnx_hw->pwr) &
		    phy_const_event_for_exit) {
			exist = true;
			break;
		}

		for (vif_idx = 0; vif_idx < NX_ITF_MAX; vif_idx++) {
			/* vif needs to be open */
			rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);
			if (!rwnx_vif)
				continue;

			/* Determine if there is the presence of const events those need be processed for a long time,
			   such as CSA, ROAMED, etc */
			if (bes_pwr_get_vif_const_event(&rwnx_hw->pwr, vif_idx) &
			    vif_const_event_for_exit) {
				exist = true;
				break;
			}
		}
	} while(0);
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

	return exist;
}

static bool bes_pwr_no_event_exist(struct rwnx_hw *rwnx_hw)
{
	u8 vif_idx = 0;
	struct rwnx_vif *rwnx_vif = NULL;
	bool no_event = true;

	/* Determine if phy events exist */
	if (rwnx_hw->pwr.phy_event != 0)
		return false;

	/* Determine if vif events exist */
	for (vif_idx = 0; vif_idx < NX_ITF_MAX; vif_idx++) {
		u8 idx;

		/* vif needs to be open */
		rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);
		if (!rwnx_vif)
			continue;

		/* Check the validity of idx */
		idx = rwnx_vif->vif_index;
		if (idx >= NX_ITF_MAX)
			continue;

		if (!rwnx_vif->in_csa && /* rwnx_vif->ch_index may be invalid in the CSA process */
		    rwnx_vif->ch_index != rwnx_hw->pwr.cur_chanctx && /* chanctx is equal */
		    rwnx_vif->ch_index != RWNX_CH_NOT_SET && /* chanctx is already set */
		    rwnx_hw->pwr.cur_chanctx < NX_CHAN_CTXT_CNT /* cur chanctx is valid */)
			continue;

		/* Determine if there is an event on this vif */
		if (rwnx_hw->pwr.vif_event[idx]) {
			no_event = false;
			break;
		}
	}

	return no_event;
}

static bool bes_pwr_set_vif_const_event(struct rwnx_hw *rwnx_hw, u8 vif_idx, enum pwr_vif_event_type event)
{
	bool first = bes_pwr_no_event_exist(rwnx_hw);

	/* Determine whether the value is reasonable */
	ASSERT_ERR(event >= VIF_CONST_EVENT_MIN && event < VIF_EVENT_MAX);

	/* Set vif event bit */
	rwnx_hw->pwr.vif_event[vif_idx] |= BIT(event);

	/* Record first wake-up event */
	if (first) {
		rwnx_hw->pwr.wakeup_event = event;
		rwnx_hw->pwr.wakeup_vif = vif_idx;
	}

	return first;
}

static bool bes_pwr_set_vif_delay_event(struct rwnx_hw *rwnx_hw, u8 vif_idx, enum pwr_vif_event_type event)
{
	bool first = false;

	/* Determine whether the value is reasonable */
	ASSERT_ERR(event >= VIF_DELAY_EVENT_MIN && event < VIF_CONST_EVENT_MIN);

	/* Set event and schedule time */
	if (((struct pwr_vif_d_evt_timeout *)rwnx_hw->pwr.vif_d_evt_timeout)[event].timeout) {
		first = bes_pwr_no_event_exist(rwnx_hw);

		rwnx_hw->pwr.vif_event[vif_idx] |= BIT(event);
		rwnx_hw->pwr.vif_timeout[vif_idx][event] = jiffies +
			((struct pwr_vif_d_evt_timeout *)rwnx_hw->pwr.vif_d_evt_timeout)[event].timeout;
	}

	/* Record first wake-up event */
	if (first) {
		rwnx_hw->pwr.wakeup_event = event;
		rwnx_hw->pwr.wakeup_vif = vif_idx;
	}

	return first;
}

static bool bes_pwr_set_phy_const_event(struct rwnx_hw *rwnx_hw, enum pwr_phy_event_type event)
{
	bool first = bes_pwr_no_event_exist(rwnx_hw);

	/* Determine whether the value is reasonable */
	ASSERT_ERR(event >= PHY_CONST_EVENT_MIN && event < PHY_EVENT_MAX);

	/* Set event */
	rwnx_hw->pwr.phy_event |= BIT(event);

	/* Record first wake-up event */
	if (first) {
		rwnx_hw->pwr.wakeup_event = event;
		rwnx_hw->pwr.wakeup_vif = RWNX_INVALID_VIF;
	}

	return first;
}

static bool bes_pwr_set_phy_delay_event(struct rwnx_hw *rwnx_hw, enum pwr_phy_event_type event)
{
	bool first = false;

	/* Determine whether the value is reasonable */
	ASSERT_ERR(event >= PHY_DELAY_EVENT_MIN && event < PHY_CONST_EVENT_MIN);

	/* Set event and schedule time */
	if (((struct pwr_phy_d_evt_timeout *)rwnx_hw->pwr.phy_d_evt_timeout)[event].timeout) {
		first = bes_pwr_no_event_exist(rwnx_hw);

		rwnx_hw->pwr.phy_event |= BIT(event);
		rwnx_hw->pwr.phy_timeout[event] = jiffies +
			((struct pwr_phy_d_evt_timeout *)rwnx_hw->pwr.phy_d_evt_timeout)[event].timeout;
	}

	/* Record first wake-up event */
	if (first) {
		rwnx_hw->pwr.wakeup_event = event;
		rwnx_hw->pwr.wakeup_vif = RWNX_INVALID_VIF;
	}

	return first;
}

static void bes_pwr_clear_vif_const_event(struct rwnx_hw *rwnx_hw, u8 vif_idx, enum pwr_vif_event_type event)
{
	/* Determine whether the value is reasonable */
	ASSERT_ERR(event >= VIF_CONST_EVENT_MIN && event < VIF_EVENT_MAX);

	/* Clear vif const event if set */
	if (rwnx_hw->pwr.vif_event[vif_idx] & BIT(event)) {
		rwnx_hw->pwr.vif_event[vif_idx] &= ~BIT(event);

		if (event == VIF_CONST_EVENT_DATA_TX && (!rwnx_hw->pwr.flag_suspend))
			bes_pwr_set_vif_delay_event(rwnx_hw, vif_idx, VIF_DELAY_EVENT_DATA_TX_CLR);
	}
}

static void bes_pwr_clear_phy_const_event(struct rwnx_hw *rwnx_hw, enum pwr_phy_event_type event)
{
	/* Determine whether the value is reasonable */
	ASSERT_ERR(event >= PHY_CONST_EVENT_MIN && event < PHY_EVENT_MAX);

	/* Clear phy const event if set */
	if (rwnx_hw->pwr.phy_event & BIT(event)) {
		rwnx_hw->pwr.phy_event &= ~BIT(event);

		/* The clearing init_event does not need to be set const_clr_event flag */
		if (event != PHY_CONST_EVENT_INIT && (!rwnx_hw->pwr.flag_suspend))
			bes_pwr_set_phy_delay_event(rwnx_hw, PHY_DELAY_EVENT_CONST_CLR);
	}
}

static inline bool bes_pwr_check_if_any_ap_or_p2p_go(struct rwnx_hw *rwnx_hw)
{
	u8 vif_idx = 0;
	struct rwnx_vif *rwnx_vif = NULL;

	for (vif_idx = 0; vif_idx < NX_ITF_MAX; vif_idx++) {
		/* vif needs to be open */
		rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);
		if (!rwnx_vif)
			continue;

		/* check vif type */
		if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_AP &&
		    RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_P2P_GO)
			continue;

		return true;
	}

	return false;
}

static void bes_pwr_set_vif_link_map(struct rwnx_hw *rwnx_hw, u8 vif_idx, bool async)
{
	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	/* Update vif_link_map */
	rwnx_hw->pwr.vif_link_map |= BIT(vif_idx);

	/* When the count of connected VIFs is more than 1 and there are AP or GO,
	   set coexistence flag */
	if (hweight32(rwnx_hw->pwr.vif_link_map) < 2 ||
	    !bes_pwr_check_if_any_ap_or_p2p_go(rwnx_hw)) {
		spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);
		return;
	}
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

	if (async)
		bes_pwr_set_busy_event_async(rwnx_hw, RWNX_INVALID_VIF, PHY_CONST_EVENT_CONCURRENT);
	else
		bes_pwr_set_busy_event(rwnx_hw, RWNX_INVALID_VIF, PHY_CONST_EVENT_CONCURRENT);
}

static void bes_pwr_clear_vif_link_map(struct rwnx_hw *rwnx_hw, u8 vif_idx)
{
	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	/* Update vif_link_map */
	rwnx_hw->pwr.vif_link_map &= ~BIT(vif_idx);

	/* When the count of connected VIFs is less than 2 and there is not AP or GO,
	   clear coexistence flag */
	if (hweight32(rwnx_hw->pwr.vif_link_map) > 1) {
		spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);
		return;
	}
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

	bes_pwr_clear_busy_event(rwnx_hw, RWNX_INVALID_VIF, PHY_CONST_EVENT_CONCURRENT);
}

static bool bes_pwr_update_delay_event_and_get_max_timeout(struct rwnx_hw *rwnx_hw, unsigned long *mtjiff)
{
	u8 vif_idx = 0;
	struct rwnx_vif *rwnx_vif = NULL;
	u32 j = 0;
	bool d_evt_exist = false;
	unsigned long cur_jiffies = *mtjiff;

	/* Update phy delay event and get max timout*/
	if (bes_pwr_get_phy_delay_event(&rwnx_hw->pwr)) {
		for (j = PHY_DELAY_EVENT_MIN; j < PHY_CONST_EVENT_MIN; j++) {
			/* Determine if this phy_delay_event exists */
			if (!val_get_bit(rwnx_hw->pwr.phy_event, j))
				continue;

			/* The timeout of this phy_delay_event has expired */
			if (time_after_eq(cur_jiffies, rwnx_hw->pwr.phy_timeout[j])) {
				bes_pwr_clear_phy_delay_event(rwnx_hw, j);
				continue;
			}

			/* Get maximum timeout */
			if (time_after_eq(rwnx_hw->pwr.phy_timeout[j], *mtjiff))
				*mtjiff = rwnx_hw->pwr.phy_timeout[j];
			d_evt_exist = true;
		}
	}

	/* Update vif delay event and get max timout*/
	for (vif_idx = 0; vif_idx < NX_ITF_MAX; vif_idx++) {
		u8 idx;

		/* vif needs to be open */
		rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);
		if (!rwnx_vif)
			continue;

		/* Check the validity of idx */
		idx = rwnx_vif->vif_index;
		if (idx >= NX_ITF_MAX)
			continue;

		/* Determine if there is an delay_event on this vif */
		if (!bes_pwr_get_vif_delay_event(&rwnx_hw->pwr, idx))
			continue;

		for (j = VIF_DELAY_EVENT_MIN; j < VIF_CONST_EVENT_MIN; j++) {
			/* Determine if this delay_event exists on this vif */
			if (!val_get_bit(rwnx_hw->pwr.vif_event[idx], j))
				continue;

			/* The timeout of this delay_event has expired */
			if (time_after_eq(cur_jiffies, rwnx_hw->pwr.vif_timeout[idx][j])) {
				bes_pwr_clear_vif_delay_event(rwnx_hw, idx, j);
				continue;
			}

			if (!rwnx_vif->in_csa && /* rwnx_vif->ch_index may be invalid in the CSA process */
			    rwnx_vif->ch_index != rwnx_hw->pwr.cur_chanctx && /* chanctx is equal */
			    rwnx_vif->ch_index != RWNX_CH_NOT_SET && /* chanctx is already set */
			    rwnx_hw->pwr.cur_chanctx < NX_CHAN_CTXT_CNT /* cur chanctx is valid */)
				continue;

			/* Get maximum timeout */
			if (time_after_eq(rwnx_hw->pwr.vif_timeout[idx][j], *mtjiff))
				*mtjiff = rwnx_hw->pwr.vif_timeout[idx][j];
			d_evt_exist = true;
		}
	}

	return d_evt_exist;
}

static bool bes_pwr_update_schedule_state(struct rwnx_hw *rwnx_hw, unsigned long *mtjiff)
{
	unsigned long cur_jiffies = jiffies, mtjiff_tmp = cur_jiffies;

	/* Exists const events */
	if (bes_pwr_get_phy_const_event(&rwnx_hw->pwr) ||
	    bes_pwr_exist_vif_const_event(rwnx_hw)) {
		return false;
	}

	/* Only exists delay events */
	if (bes_pwr_update_delay_event_and_get_max_timeout(rwnx_hw, &mtjiff_tmp)) {
		/* Current delay_events exist, update scheduling time */
		*mtjiff = mtjiff_tmp - cur_jiffies;
		if (delayed_work_pending(&rwnx_hw->pwr.lp_work) == 0)
			return true;
	}

	/* No event exist */
	if (bes_pwr_in_wakeup_process(rwnx_hw)) {
		*mtjiff = 0;
		if (delayed_work_pending(&rwnx_hw->pwr.lp_work) == 0)
			return true;
	}

	return false;
}

static void bes_pwr_send_defer_cmd(struct rwnx_hw *rwnx_hw)
{
	struct rwnx_cmd *defer_cmd = NULL, *tmp = NULL;
	struct list_head defer_cmds;

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	if (list_empty(&rwnx_hw->pwr.defer_cmds)) {
		spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);
		return;
	}

	INIT_LIST_HEAD(&defer_cmds);

	list_splice_init(&rwnx_hw->pwr.defer_cmds, &defer_cmds);
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

	/* Traverse the list to send defer cmds */
	list_for_each_entry_safe(defer_cmd, tmp, &defer_cmds, list) {
		list_del(&defer_cmd->list);
		rwnx_hw->cmd_mgr.queue(&rwnx_hw->cmd_mgr, defer_cmd);
	}
}

static u32 bes_pwr_update_schedule_and_sleep_state(struct rwnx_hw *rwnx_hw, unsigned long *mtjiff)
{
	u32 actions = 0;
	unsigned long cur_jiffies = jiffies, mtjiff_tmp = cur_jiffies;

	BUG_ON(rwnx_hw->pwr.state != WIFI_LP_SLEEP_CONFIG &&
	       rwnx_hw->pwr.state != WIFI_LP_SLEEPED &&
	       rwnx_hw->pwr.state != WIFI_LP_WAKE);

	/* Exist const event */
	if (bes_pwr_get_phy_const_event(&rwnx_hw->pwr) ||
	    bes_pwr_exist_vif_const_event(rwnx_hw)) {
		/*
		 * If the wake-up configuration has already been done,
		 * there is no need to set PWR_ACTION_WAKEUP_NOTIFY flag.
		 */
		if (rwnx_hw->pwr.state != WIFI_LP_WAKE)
			actions |= BIT(PWR_ACTION_WAKEUP_NOTIFY);

		/*
		 * If the WiFi is not currently being awakened (by raising the GPIO),
		 * it is necessary to first awaken the WiFi
		 */
		if (rwnx_hw->pwr.state == WIFI_LP_SLEEPED)
			actions |= BIT(PWR_ACTION_WAKEUP);
	} else if (bes_pwr_update_delay_event_and_get_max_timeout(rwnx_hw, &mtjiff_tmp)) {
		/* Calculate timeout */
		*mtjiff = mtjiff_tmp - cur_jiffies;

		/* Restart scheduling */
		if (delayed_work_pending(&rwnx_hw->pwr.lp_work) == 0)
			actions |= BIT(PWR_ACTION_SCHED);

		/*
		 * If the wake-up configuration has already been done,
		 * there is no need to set PWR_ACTION_WAKEUP_NOTIFY flag.
		 */
		if (rwnx_hw->pwr.state != WIFI_LP_WAKE)
			actions |= BIT(PWR_ACTION_WAKEUP_NOTIFY);

		/*
		 * If the WiFi is not currently being awakened (by raising the GPIO),
		 * it is necessary to first awaken the WiFi
		 */
		if (rwnx_hw->pwr.state == WIFI_LP_SLEEPED)
			actions |= BIT(PWR_ACTION_WAKEUP);
	} else {
		/* No event exist */
		if (rwnx_hw->pwr.state == WIFI_LP_SLEEP_CONFIG)
			actions |= BIT(PWR_ACTION_SLEEP);
		else if (rwnx_hw->pwr.state == WIFI_LP_WAKE)
			actions |= BIT(PWR_ACTION_SLEEP_NOTIFY) | BIT(PWR_ACTION_SLEEP);
	}

	/* Avoiding repeated wakeup */
	if ((actions & BIT(PWR_ACTION_WAKEUP)) && rwnx_hw->pwr.lp_wakeup_wait)
		actions &= ~BIT(PWR_ACTION_WAKEUP);

	return actions;
}

static void bes_pwr_wake_ctrl(struct rwnx_hw *rwnx_hw, bool wakeup)
{
	if (rwnx_hw->pwr.wake_ctrl)
		rwnx_hw->pwr.wake_ctrl(rwnx_hw, wakeup);
}

static void bes_pwr_wifi_wake_ctrl(struct rwnx_hw *rwnx_hw, bool wakeup)
{
	int ret = 0;

	if (!test_bit(RWNX_DEV_STARTED, &rwnx_hw->flags))
		return;

	ret = bes_send_wake_ctrl_req(rwnx_hw, wakeup);
	bes_err_with_cond(ret, BES_DBG_PWR, "%s fail to send MCU_WIFI_WAKE_CTRL_REQ.ret:%d\n", __func__, ret);
}

static void bes_pwr_wifi_lp_lock(struct rwnx_hw *rwnx_hw)
{
	u8 wakeup_event, wakeup_vif;

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	/*
	 * If the WIFI_LP_SLEEP_CONFIG state in the sleep process is interrupted
	 * by the wake-up process (WIFI_LP_WAKING), it will directly enter the
	 * WIFI_LP_WAKE_CONFIG process
	 */
	if (rwnx_hw->pwr.state == WIFI_LP_SLEEP_CONFIG) {
		spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

		bes_dbg(BES_DBG_PWR,
			"%s lp_state(%s) is interrupted by WAKING, go to WAKE_CONFIG directly!!\n",
			__func__, bes_pwr_get_lp_state_name(rwnx_hw->pwr.state));

		return;
	}

	/*
	 * Before entering the wake-up process, it is necessary to wait for the
	 * sleep process to be completed, and that is enter WIFI_LP_SLEEPED
	 */
	if (rwnx_hw->pwr.state != WIFI_LP_SLEEPED) {
		rwnx_hw->pwr.lp_wakeup_wait = true;
		spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

		bes_dbg(BES_DBG_PWR,
			"%s lp_state is %s, and [start] waiting for sleep process to be completed!!\n",
			__func__, bes_pwr_get_lp_state_name(rwnx_hw->pwr.state));

		wait_event(rwnx_hw->pwr.switch_wq, bes_pwr_wifi_lp_state_check(rwnx_hw, WIFI_LP_SLEEPED));

		bes_dbg(BES_DBG_PWR,
			"%s lp_state is %s, and [end] waiting for sleep process to be completed!!\n",
			__func__, bes_pwr_get_lp_state_name(rwnx_hw->pwr.state));

		spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
		rwnx_hw->pwr.lp_wakeup_wait = false;
	}

	/* Get wakeup source info */
	wakeup_event = rwnx_hw->pwr.wakeup_event;
	wakeup_vif = rwnx_hw->pwr.wakeup_vif;
	rwnx_hw->pwr.wakeup_event = 0xff;
	rwnx_hw->pwr.wakeup_vif = RWNX_INVALID_VIF;

	rwnx_hw->pwr.state = WIFI_LP_WAKING;
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

	/* Print wakeup source */
	if (wakeup_event == 0xff) {
		bes_info(BES_DBG_PWR, "%s wifi wakeup by switch chanctx.cur_chanctx:%d count:%d\n",
			 __func__, rwnx_hw->pwr.cur_chanctx,
			(rwnx_hw->pwr.cur_chanctx >= NX_CHAN_CTXT_CNT) ? (0xFF) :
		 		(rwnx_hw->chanctx_table[rwnx_hw->pwr.cur_chanctx].count));
	} else {
		if (wakeup_vif == RWNX_INVALID_VIF)
			bes_info(BES_DBG_PWR, "%s wifi wakeup by phy_event: %s\n", __func__,
				bes_pwr_get_phy_event_name(wakeup_event));
		else
			bes_info(BES_DBG_PWR, "%s wifi wakeup by vif_event: %s(%d)\n", __func__,
				bes_pwr_get_vif_event_name(wakeup_event), wakeup_vif);
	}

	/* Wakeup operation and update state */
	bes_pwr_wake_ctrl(rwnx_hw, true);
	bes_pwr_wifi_wake_ctrl(rwnx_hw, true);
	bes_pwr_wifi_lp_state_set(rwnx_hw, WIFI_LP_WAKE_CONFIG);
	wake_up(&rwnx_hw->pwr.switch_wq);

	/* Sending non-blocked and defered msg after wakeup */
	bes_pwr_send_defer_cmd(rwnx_hw);
}

static void bes_pwr_action_wakeup(struct rwnx_hw *rwnx_hw,
				  struct pwr_action_param *param)
{
	bes_pwr_wifi_lp_lock(rwnx_hw);
}

static void bes_pwr_action_wakeup_notify(struct rwnx_hw *rwnx_hw,
					 struct pwr_action_param *param)
{
	/* The entire wake-up process is complete */
	bes_pwr_wifi_lp_state_set(rwnx_hw, WIFI_LP_WAKE);

	/* Wifi lp call back for seting ps mode */
	bes_pwr_call_lp_cb(rwnx_hw, PWR_LP_CB_STATUS_AFTER_WIFI_WAKEUP);

	wake_up(&rwnx_hw->pwr.switch_wq);
}

static void bes_pwr_action_sleep_notify(struct rwnx_hw *rwnx_hw,
					struct pwr_action_param *param)
{
	/* Wifi lp call back for seting ps mode */
	bes_pwr_call_lp_cb(rwnx_hw, PWR_LP_CB_STATUS_BEFORE_WIFI_SLEEP);
}

static void bes_pwr_action_sleep(struct rwnx_hw *rwnx_hw,
				 struct pwr_action_param *param)
{
	/*
	 * Determine if it is possible to enter sleep.
	 * Because it will directly enter the WIFI_LP_WAKE_CONFIG process
	 * if the WIFI_LP_SLEEP_CONFIG state in the sleep process is interrupted
	 * by the wake-up process (WIFI_LP_WAKING).
	 */
	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	if (!bes_pwr_no_event_exist(rwnx_hw)) {
		spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

		bes_dbg(BES_DBG_PWR,
			"%s lp_state(%s) is interrupted by WAKING, go to WAKE_CONFIG directly!!\n",
			__func__, bes_pwr_get_lp_state_name(rwnx_hw->pwr.state));

		return;
	}
	rwnx_hw->pwr.state = WIFI_LP_SLEEPING;
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

	/* Sleep operation and update state */
	bes_pwr_wifi_wake_ctrl(rwnx_hw, false);
	bes_pwr_wake_ctrl(rwnx_hw, false);
	bes_pwr_wifi_lp_state_set(rwnx_hw, WIFI_LP_SLEEPED);
	bes_info(BES_DBG_PWR, "%s wifi state to sleep.cur_chanctx:%d count:%d\n",
		 __func__, rwnx_hw->pwr.cur_chanctx,
		 (rwnx_hw->pwr.cur_chanctx >= NX_CHAN_CTXT_CNT) ? (0xFF) :
		 	(rwnx_hw->chanctx_table[rwnx_hw->pwr.cur_chanctx].count));
	wake_up(&rwnx_hw->pwr.switch_wq);
}

static void bes_pwr_action_sched(struct rwnx_hw *rwnx_hw,
				 struct pwr_action_param *param)
{
	bool ret = false;

	/* Queue work */
	if (delayed_work_pending(&rwnx_hw->pwr.lp_work)) {
		if (param->cancel_sync)
			ret = cancel_delayed_work_sync(&rwnx_hw->pwr.lp_work);
		else
			ret = cancel_delayed_work(&rwnx_hw->pwr.lp_work);

		bes_dbg(BES_DBG_PWR, "%s cancel pending lp_work(%s) ret:%d\n",
			__func__, param->cancel_sync ? "sync" : "async", ret);
	}

	ret = queue_delayed_work(rwnx_hw->rwnx_wq, &rwnx_hw->pwr.lp_work, param->sched_time);
	bes_dbg(BES_DBG_PWR, "%s queue lp_work(timeout:%d).ret:%d\n", __func__,
		jiffies_to_msecs(param->sched_time), ret);
}

static void bes_pwr_action_wait_wake(struct rwnx_hw *rwnx_hw,
				     struct pwr_action_param *param)
{
	/* Waiting for the last WiFi wake-up operation to complete */
	bes_dbg(BES_DBG_PWR,
		"%s now state: %s, and [start] waiting for lp_state to LP_WAKE\n",
		__func__, bes_pwr_get_lp_state_name(rwnx_hw->pwr.state));

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	rwnx_hw->pwr.wait_wake = true;
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

	wait_event(rwnx_hw->pwr.switch_wq,
		bes_pwr_wifi_lp_state_check(rwnx_hw, WIFI_LP_WAKE));

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	rwnx_hw->pwr.wait_wake = false;
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

	bes_dbg(BES_DBG_PWR,
		"%s now state: %s, and [end] waiting for lp_state to LP_WAKE\n",
		__func__, bes_pwr_get_lp_state_name(rwnx_hw->pwr.state));
}

const struct bes_pwr_action_process_t pwr_action_process_tbl[] = {
	{.action = PWR_ACTION_WAKEUP,		.pfun = bes_pwr_action_wakeup},
	{.action = PWR_ACTION_WAKEUP_NOTIFY,	.pfun = bes_pwr_action_wakeup_notify},
	{.action = PWR_ACTION_SLEEP_NOTIFY,	.pfun = bes_pwr_action_sleep_notify},
	{.action = PWR_ACTION_SLEEP,		.pfun = bes_pwr_action_sleep},
	{.action = PWR_ACTION_SCHED,		.pfun = bes_pwr_action_sched},
	{.action = PWR_ACTION_WAIT_WAKE,	.pfun = bes_pwr_action_wait_wake},
};

static void bes_pwr_actions_process(struct rwnx_hw *rwnx_hw,
				    struct pwr_action_param *param)
{
	u8 ac = 0;

	/* No action, return directly */
	if (!param->actions)
		return;

	/* Traverse execution callback function */
	bes_dbg(BES_DBG_PWR, "%s actions:%x\n", __func__, param->actions);
	for (ac = PWR_ACTION_MIN; ac < PWR_ACTION_MAX; ac++) {
		if (!test_bit(ac, (unsigned long *)&param->actions))
			continue;

		if (pwr_action_process_tbl[ac].action != ac) {
			bes_warn(BES_DBG_PWR, "%s pwr_action_process_tbl[%d].action:%d\n",
				 __func__, ac, pwr_action_process_tbl[ac].action);
			continue;
		}

		bes_dbg(BES_DBG_PWR, "%s %s in.now:%s\n",
			__func__, bes_pwr_get_action_name(ac),
			bes_pwr_get_lp_state_name(rwnx_hw->pwr.state));

		(pwr_action_process_tbl[ac].pfun)(rwnx_hw, param);

		bes_dbg(BES_DBG_PWR, "%s %s out.now:%s\n",
			__func__, bes_pwr_get_action_name(ac),
			bes_pwr_get_lp_state_name(rwnx_hw->pwr.state));
	}
}

void bes_pwr_set_busy_event(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 event)
{
	bool schdle = false;
	bool first = false;
	unsigned long sched_time;
	struct pwr_action_param action_param = {0};

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	/* Update event */
	if (vif_idx == RWNX_INVALID_VIF) {
		if (event < PHY_CONST_EVENT_MIN)
			first = bes_pwr_set_phy_delay_event(rwnx_hw, event);
		else
			first = bes_pwr_set_phy_const_event(rwnx_hw, event);
	} else {
		if (event < VIF_CONST_EVENT_MIN)
			first = bes_pwr_set_vif_delay_event(rwnx_hw, vif_idx, event);
		else
			first = bes_pwr_set_vif_const_event(rwnx_hw, vif_idx, event);
	}

	/* Get schedule timeout and lp action */
	schdle = bes_pwr_update_schedule_state(rwnx_hw, &sched_time);
	if (rwnx_hw->pwr.state != WIFI_LP_WAKE) {
		/* if interrupt sleep_cfg, do nothing, and ensure wake-up when an event exists*/
		if (!bes_pwr_in_wakeup_process(rwnx_hw)) {
			action_param.cancel_sync = true;
			action_param.sched_time = 0;
			action_param.actions |= BIT(PWR_ACTION_SCHED);
		}

		/* Ensure wake-up completion */
		action_param.actions |= BIT(PWR_ACTION_WAIT_WAKE);
	} else if (schdle) {
		action_param.cancel_sync = true;
		action_param.sched_time = sched_time;
		action_param.actions |= BIT(PWR_ACTION_SCHED);
	}

	if (rwnx_hw->pwr.wait_wake)
		action_param.actions |= BIT(PWR_ACTION_WAIT_WAKE);
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);
	bes_dbg(BES_DBG_PWR, "%s vif_idx:%d,event:%d(%s),first:%d,schdle:%d,actions:%x,"
		"SCt:%d,lp_state:%s,evt_ext:%d,already_wait_wake:%d\n", __func__, vif_idx, event,
		(vif_idx == RWNX_INVALID_VIF) ? bes_pwr_get_phy_event_name(event) : bes_pwr_get_vif_event_name(event),
		first, schdle, action_param.actions, jiffies_to_msecs(action_param.sched_time),
		bes_pwr_get_lp_state_name(rwnx_hw->pwr.state),
		!bes_pwr_no_event_exist(rwnx_hw),
		rwnx_hw->pwr.wait_wake);

	/* Execute lp action */
	bes_pwr_actions_process(rwnx_hw, &action_param);
}

void bes_pwr_set_busy_event_async(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 event)
{
	bool schdle = false;
	bool first = false;
	unsigned long sched_time;
	struct pwr_action_param action_param = {0};

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	/* Update event */
	if (vif_idx == RWNX_INVALID_VIF) {
		if (event < PHY_CONST_EVENT_MIN)
			first = bes_pwr_set_phy_delay_event(rwnx_hw, event);
		else
			first = bes_pwr_set_phy_const_event(rwnx_hw, event);
	} else {
		if (event < VIF_CONST_EVENT_MIN)
			first = bes_pwr_set_vif_delay_event(rwnx_hw, vif_idx, event);
		else
			first = bes_pwr_set_vif_const_event(rwnx_hw, vif_idx, event);
	}

	/* Get schedule timeout and lp action */
	schdle = bes_pwr_update_schedule_state(rwnx_hw, &sched_time);
	if (first) {
		/* if interrupt sleep_cfg, do nothing, and ensure wake-up when an event exists*/
		if (!bes_pwr_in_wakeup_process(rwnx_hw)) {
			action_param.cancel_sync = false;
			action_param.sched_time = 0;
			action_param.actions |= BIT(PWR_ACTION_SCHED);
		}
	} else if (schdle) {
		action_param.cancel_sync = false;
		action_param.sched_time = sched_time;
		action_param.actions |= BIT(PWR_ACTION_SCHED);
	}
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);
	bes_dbg(BES_DBG_PWR, "%s vif_idx:%d,event:%d(%s),first:%d,schdle:%d,actions:%x,"
		"SCt:%d,lp_state:%s,evt_ext:%d\n", __func__, vif_idx, event,
		(vif_idx == RWNX_INVALID_VIF) ? bes_pwr_get_phy_event_name(event) : bes_pwr_get_vif_event_name(event),
		first, schdle, action_param.actions, jiffies_to_msecs(action_param.sched_time),
		bes_pwr_get_lp_state_name(rwnx_hw->pwr.state),
		!bes_pwr_no_event_exist(rwnx_hw));

	/* Execute lp action */
	bes_pwr_actions_process(rwnx_hw, &action_param);
}


static void bes_pwr_clear_delay_evt_before_suspend(struct rwnx_hw *rwnx_hw)
{
	struct rwnx_vif *rwnx_vif = NULL;
	u8 vif_idx;
	bool schdle = false;
	struct pwr_action_param action_param = {0};

	bes_dbg(BES_DBG_PWR, "%s Clearing delay event before host suspend. phy_delay_event_mask:0x%08x vif_delay_event_mask:0x%08x",
		__func__, phy_delay_event_mask, vif_delay_event_mask);

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	/* No longer set delay_event after clearing const_event */
	rwnx_hw->pwr.flag_suspend = true;

	/* Clearing phy delay event before host suspend */
	if (bes_pwr_get_phy_delay_event(&rwnx_hw->pwr)) {
		rwnx_hw->pwr.phy_event &= ~phy_delay_event_mask;
		schdle = true;
	}

	/* Clearing vif delay event before host suspend */
	for (vif_idx = 0; vif_idx < NX_ITF_MAX; vif_idx++) {
		/* vif needs to be open */
		rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);
		if (!rwnx_vif)
			continue;

		/* Determine if exsiting delay_event on this vif */
		if (!bes_pwr_get_vif_delay_event(&rwnx_hw->pwr, vif_idx))
			continue;

		/* Clear specified vif events */
		rwnx_hw->pwr.vif_event[vif_idx] &= ~vif_delay_event_mask;
		schdle = true;
	}

	/* Queue work directly */
	if (schdle) {
		action_param.cancel_sync = true;
		action_param.sched_time = 0;
		action_param.actions |= BIT(PWR_ACTION_SCHED);
	}
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

	bes_dbg(BES_DBG_PWR, "%s schdle:%d,actions:%x,SCt:%d,"
		"lp_state:%s,evt_ext:%d\n", __func__,
		schdle, action_param.actions, jiffies_to_msecs(action_param.sched_time),
		bes_pwr_get_lp_state_name(rwnx_hw->pwr.state),
		!bes_pwr_no_event_exist(rwnx_hw));

	/* Execute lp action */
	bes_pwr_actions_process(rwnx_hw, &action_param);
}

bool bes_pwr_handle_lp_evt_before_suspend(struct rwnx_hw *rwnx_hw)
{
	/* Exiting directly if there is an exit event for host suspend */
	if (bes_pwr_handle_const_evt_before_suspend(rwnx_hw))
		return true;

	/* Cancel scan before host suspend */
	if (test_bit(RWNX_DEV_SCANNING, &rwnx_hw->flags))
		rwnx_send_scan_cancel_req(rwnx_hw, NULL);

	/* Clearing delay event before host suspend */
	bes_pwr_clear_delay_evt_before_suspend(rwnx_hw);

	return false;
}

void bes_pwr_clear_busy_event(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 event)
{
	bool schdle = false;
	unsigned long sched_time;
	struct pwr_action_param action_param = {0};

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	/* Update event */
	if (vif_idx == RWNX_INVALID_VIF) {
		if (event < PHY_CONST_EVENT_MIN)
			bes_pwr_clear_phy_delay_event(rwnx_hw, event);
		else
			bes_pwr_clear_phy_const_event(rwnx_hw, event);
	} else {
		if (event < VIF_CONST_EVENT_MIN)
			bes_pwr_clear_vif_delay_event(rwnx_hw, vif_idx, event);
		else
			bes_pwr_clear_vif_const_event(rwnx_hw, vif_idx, event);
	}

	/* Get schedule timeout and lp action */
	schdle = bes_pwr_update_schedule_state(rwnx_hw, &sched_time);
	if (schdle) {
		action_param.cancel_sync = false;
		action_param.sched_time = sched_time;
		action_param.actions |= BIT(PWR_ACTION_SCHED);
	}
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);
	bes_dbg(BES_DBG_PWR, "%s vif_idx:%d,event:%d(%s),schdle:%d,actions:%x,SCt:%d,"
		"lp_state:%s,evt_ext:%d\n", __func__, vif_idx, event,
		(vif_idx == RWNX_INVALID_VIF) ? bes_pwr_get_phy_event_name(event) : bes_pwr_get_vif_event_name(event),
		schdle, action_param.actions, jiffies_to_msecs(action_param.sched_time),
		bes_pwr_get_lp_state_name(rwnx_hw->pwr.state),
		!bes_pwr_no_event_exist(rwnx_hw));

	/* Execute lp action */
	bes_pwr_actions_process(rwnx_hw, &action_param);
}

static void bes_pwr_lp_work_func(struct work_struct *work)
{
	struct bes_pwr *pwr = container_of(work, struct bes_pwr, lp_work.work);
	struct rwnx_hw *rwnx_hw = container_of(pwr, struct rwnx_hw, pwr);
	unsigned long sched_time;
	struct pwr_action_param action_param = {0};

	bes_dbg(BES_DBG_PWR, "%s in.lp_state:%s,evt_ext:%d\n", __func__,
		bes_pwr_get_lp_state_name(rwnx_hw->pwr.state),
		!bes_pwr_no_event_exist(rwnx_hw));

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	while(!!(action_param.actions = bes_pwr_update_schedule_and_sleep_state(rwnx_hw, &sched_time))) {
		if (action_param.actions & BIT(PWR_ACTION_SCHED)) {
			action_param.cancel_sync = false;
			action_param.sched_time = sched_time;
		}

		/* If it will sleep, update lp state */
		if (action_param.actions & BIT(PWR_ACTION_SLEEP))
			rwnx_hw->pwr.state = WIFI_LP_SLEEP_CONFIG;

		/* Execute corresponding actions and
		   set a range that does not require lp handle */
		rwnx_hw->pwr.pwr_task = current;
		spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

		bes_dbg(BES_DBG_PWR, "%s actions:%x,SCt:%d,lp_state:%s,evt_ext:%d\n",
			__func__, action_param.actions, jiffies_to_msecs(action_param.sched_time),
			bes_pwr_get_lp_state_name(rwnx_hw->pwr.state),
			!bes_pwr_no_event_exist(rwnx_hw));

		bes_pwr_actions_process(rwnx_hw, &action_param);

		spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
		rwnx_hw->pwr.pwr_task = NULL;
	}
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

	bes_dbg(BES_DBG_PWR, "%s out.lp_state:%s,evt_ext:%d\n", __func__,
		bes_pwr_get_lp_state_name(rwnx_hw->pwr.state),
		!bes_pwr_no_event_exist(rwnx_hw));
}

static void pwr_msg_rx_scan_start_cfm(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	u8 vif_idx = 0;

	vif_idx = ((struct scanu_start_cfm *)msg->param)->vif_idx;
	if (bes_pwr_is_vif_up(rwnx_hw, vif_idx))
		bes_pwr_clear_busy_event(rwnx_hw, RWNX_INVALID_VIF, PHY_CONST_EVENT_SCAN);
}

static void pwr_msg_rx_scan_cancel_cfm(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	bes_pwr_clear_busy_event(rwnx_hw, RWNX_INVALID_VIF, PHY_CONST_EVENT_SCAN);
}

static void pwr_msg_rx_remain_on_channel_exp_ind(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	u8 vif_idx = 0;

	vif_idx = ((struct mm_remain_on_channel_exp_ind *)msg->param)->vif_index;
	if (bes_pwr_is_vif_up(rwnx_hw, vif_idx))
		bes_pwr_clear_busy_event(rwnx_hw, RWNX_INVALID_VIF, PHY_CONST_EVENT_ROC);
}

static void pwr_msg_rx_remain_on_channel_cfm(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	struct mm_remain_on_channel_cfm *cfm = (struct mm_remain_on_channel_cfm *)msg->param;
	if (cfm->status != CO_OK)
		bes_pwr_clear_busy_event(rwnx_hw, RWNX_INVALID_VIF, PHY_CONST_EVENT_ROC);
}

static void pwr_msg_rx_connect_ind(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	u8 vif_idx = 0;

	vif_idx = ((struct sm_connect_ind *)msg->param)->vif_idx;
	if (bes_pwr_is_vif_up(rwnx_hw, vif_idx)) {
		/* Processing of successful connection */
		if(((struct sm_connect_ind *)msg->param)->status_code == 0) {
			/* Set flag in vif_link_map */
			bes_pwr_set_vif_link_map(rwnx_hw, vif_idx, true);

			/* Set flag to protect 4-way handshake */
			bes_pwr_set_busy_event_async(rwnx_hw, vif_idx, VIF_DELAY_EVENT_TX_KEY_FRM);

			/* If the connection is successful, clear the roaming flag */
			bes_pwr_clear_busy_event(rwnx_hw, vif_idx, VIF_CONST_EVENT_ROAMED);
		}

		/* Clear connect flag*/
		bes_pwr_clear_busy_event(rwnx_hw, vif_idx, VIF_CONST_EVENT_CONNECT);
	}
}

static void pwr_msg_rx_disconnect_ind(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	u8 vif_idx = ((struct sm_disconnect_ind *)msg->param)->vif_idx;

	if (bes_pwr_is_vif_up(rwnx_hw, vif_idx)) {
		/* Clear flag in vif_link_map */
		bes_pwr_clear_vif_link_map(rwnx_hw, vif_idx);

		/* If sm_disconnect_ind marks reassoc, it is determined as roaming */
		if (((struct sm_disconnect_ind *)msg->param)->reassoc)
			bes_pwr_set_busy_event_async(rwnx_hw, vif_idx, VIF_CONST_EVENT_ROAMED);
		else
			bes_pwr_clear_busy_event(rwnx_hw, vif_idx, VIF_CONST_EVENT_ROAMED);

		/* Clear connect flag */
		bes_pwr_clear_busy_event(rwnx_hw, vif_idx, VIF_CONST_EVENT_CONNECT);

		/* Clear tx_key_frame flag */
		bes_pwr_clear_busy_event(rwnx_hw, vif_idx, VIF_DELAY_EVENT_TX_KEY_FRM);
	}
}

static void bes_pwr_csa_process(struct rwnx_vif *rwnx_vif, const struct ieee80211_mgmt *mgmt, u16 mgmt_len)
{
	struct rwnx_hw *rwnx_hw= rwnx_vif->rwnx_hw;
	const u8 *csa_field = NULL;
	const u8 *ecsa_field = NULL;
	u16 ie_len = 0;
	struct rwnx_sta *rwnx_sta = rwnx_vif->sta.ap;
	u8 vif_index = rwnx_vif->vif_index;

	/* Check the validity of vif_index */
	if (vif_index >= NX_ITF_MAX)
		return;

	ie_len = mgmt_len - offsetof(struct ieee80211_mgmt, u.beacon.variable);

	/* Handling the frame sent by the peer AP. */
	if ((RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION ||
	     RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_CLIENT) &&
	    rwnx_sta &&
	    ether_addr_equal(mgmt->bssid, rwnx_sta->mac_addr)) {
		/* Search csa/ecsa from beacon */
		csa_field = cfg80211_find_ie(WLAN_EID_CHANNEL_SWITCH, mgmt->u.beacon.variable, ie_len);
		ecsa_field = cfg80211_find_ie(WLAN_EID_EXT_CHANSWITCH_ANN, mgmt->u.beacon.variable, ie_len);

		if ((csa_field || ecsa_field) &&
		    !(bes_pwr_get_vif_const_event_lock(rwnx_hw, vif_index) & BIT(VIF_CONST_EVENT_CSA))) {
			/* When processing CSA, it is necessary to maintain wake-up */
			bes_pwr_set_busy_event_async(rwnx_hw, vif_index, VIF_CONST_EVENT_CSA);
		} else if ((!csa_field && !ecsa_field) &&
			   (bes_pwr_get_vif_const_event_lock(rwnx_hw, vif_index) & BIT(VIF_CONST_EVENT_CSA))) {
			/* Lmac can enter low power. */
			bes_pwr_clear_busy_event(rwnx_hw, vif_index, VIF_CONST_EVENT_CSA);
		}
	}
}

static void pwr_msg_rx_scanu_result_ind(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	struct scanu_result_ind *ind = (struct scanu_result_ind *)(msg->param);
	const struct ieee80211_mgmt *mgmt = (const struct ieee80211_mgmt *)ind->payload;
	u16 mgmt_len = ind->length;
	struct rwnx_vif *rwnx_vif;

	/* Find a vif */
	rwnx_vif = rwnx_rx_get_vif(rwnx_hw, ind->inst_nbr);

	/* Intercept CSA fields from scan results. */
	if (rwnx_vif && ieee80211_is_beacon(ind->framectrl))
		bes_pwr_csa_process(rwnx_vif, mgmt, mgmt_len);
}

static void pwr_msg_rx_mcu_wifi_on_off_cfm(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	bes_pwr_clear_busy_event(rwnx_hw, RWNX_INVALID_VIF, PHY_CONST_EVENT_INIT);
}

static void pwr_msg_rx_channel_switch_ind(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	bool schdle = false;
	unsigned long sched_time;
	struct pwr_action_param action_param = {0};
	int chan_idx = ((struct mm_channel_switch_ind *)msg->param)->chan_index;
	bool roc_req = ((struct mm_channel_switch_ind *)msg->param)->roc;
	bool roc_tdls = ((struct mm_channel_switch_ind *)msg->param)->roc_tdls;

	/* Not allow to enter the low-power state
	   when the ROC of the firmware level is currently in progress */
	if ((chan_idx == CHAN_ROC_CTXT_IDX) && !roc_req && !roc_tdls &&
	    !test_bit(RWNX_DEV_FW_LEVEL_ROC, &rwnx_hw->flags)) {
		bes_pwr_set_busy_event_async(rwnx_hw, RWNX_INVALID_VIF, PHY_CONST_EVENT_ROC);
	} else if (test_bit(RWNX_DEV_FW_LEVEL_ROC, &rwnx_hw->flags)) {
		bes_pwr_clear_busy_event(rwnx_hw, RWNX_INVALID_VIF, PHY_CONST_EVENT_ROC);
	}

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	rwnx_hw->pwr.cur_chanctx = ((struct mm_channel_switch_ind *)msg->param)->chan_index;

	schdle = bes_pwr_update_schedule_state(rwnx_hw, &sched_time);

	if (!bes_pwr_no_event_exist(rwnx_hw)) {
		if (!bes_pwr_in_wakeup_process(rwnx_hw)) {
			/* Events exist. If not wake, wakeup now. */
			action_param.cancel_sync = false;
			action_param.sched_time = 0;
			action_param.actions |= BIT(PWR_ACTION_SCHED);
		} else if (schdle) {
			/* Delay_event exist, queue work */
			action_param.cancel_sync = false;
			action_param.sched_time = sched_time;
			action_param.actions |= BIT(PWR_ACTION_SCHED);
		}
	} else {
		if (bes_pwr_in_wakeup_process(rwnx_hw)) {
			action_param.cancel_sync = false;
			action_param.sched_time = 0;
			action_param.actions |= BIT(PWR_ACTION_SCHED);
		}
	}
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);
	bes_dbg(BES_DBG_PWR, "%s chanctx:%d,schdle:%d,actions:%x,SCt:%d,"
		"lp_state:%s,evt_ext:%d\n", __func__, rwnx_hw->pwr.cur_chanctx,
		schdle, action_param.actions, jiffies_to_msecs(action_param.sched_time),
		bes_pwr_get_lp_state_name(rwnx_hw->pwr.state), !bes_pwr_no_event_exist(rwnx_hw));

	/* Execute lp action */
	bes_pwr_actions_process(rwnx_hw, &action_param);
}

static int bes_pwr_get_add_if_type(struct rwnx_hw *rwnx_hw)
{
	struct rwnx_cmd *cmd = NULL;
	int vif_type = -EINVAL;

	spin_lock_bh(&rwnx_hw->cmd_mgr.lock);
	list_for_each_entry(cmd, &rwnx_hw->cmd_mgr.cmds, list) {
		if (cmd->reqid == MM_ADD_IF_CFM) {
			vif_type = ((struct mm_add_if_req *)(cmd->a2e_msg->param))->type;
			break;
		}
	}
	spin_unlock_bh(&rwnx_hw->cmd_mgr.lock);

	return vif_type;
}

static void pwr_msg_rx_add_if_cfm(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	u8 vif_idx = ((struct mm_add_if_cfm *)(msg->param))->inst_nbr;
	int vif_type;

	/* Determine whether device is started */
	if (!rwnx_wlan_enabled(rwnx_hw)) {
		bes_warn(BES_DBG_PWR, "%s WARNING: device not started\n", __func__);
		return;
	}

	/* Update variable */
	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	rwnx_hw->pwr.vif_up_map |= BIT(vif_idx);
	rwnx_hw->pwr.vif_event[vif_idx] = 0;
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

	/* Check if it is the first opened VIF, init sleep manager */
	if (bes_pwr_get_vif_up_cnt(rwnx_hw) == 1)
		bes_sleep_manager_init(rwnx_hw);

	/* Set monitor event whrn monitor interface is up */
	vif_type = bes_pwr_get_add_if_type(rwnx_hw);
	bes_err_with_cond(vif_type < 0, BES_DBG_PWR, "%s fail to get vif type.type:%d\n", __func__, vif_type);
	if (vif_type == MM_MONITOR)
		bes_pwr_set_busy_event(rwnx_hw, vif_idx, VIF_CONST_EVENT_MONITOR);

	bes_pwr_set_busy_event_async(rwnx_hw, vif_idx, VIF_DELAY_EVENT_ADD_VIF);
}

static void pwr_msg_rx_p2p_vif_ps_change_ind(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	int vif_idx = ((struct mm_p2p_vif_ps_change_ind *)msg->param)->vif_index;
	int ps_state = ((struct mm_p2p_vif_ps_change_ind *)msg->param)->ps_state;
	struct rwnx_vif *rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);

	if (!rwnx_vif) {
		bes_err(BES_DBG_MSG_RX, "%s ERROR: vif:%d is not find\n", __func__, vif_idx);
		return;
	}

	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_P2P_GO)
		return;

	if (ps_state == MM_PS_MODE_OFF)
		bes_pwr_set_busy_event_async(rwnx_hw, vif_idx, VIF_CONST_EVENT_GO_PRESENT);
	else
		bes_pwr_clear_busy_event(rwnx_hw, vif_idx, VIF_CONST_EVENT_GO_PRESENT);
}

const struct bes_pwr_msg_process_t pwr_msg_rx_process_tbl[] = {
	{.id = SCANU_START_CFM,			.rx_pfun = pwr_msg_rx_scan_start_cfm},
	{.id = SCAN_CANCEL_CFM,			.rx_pfun = pwr_msg_rx_scan_cancel_cfm},
	{.id = MM_REMAIN_ON_CHANNEL_EXP_IND,	.rx_pfun = pwr_msg_rx_remain_on_channel_exp_ind},
	{.id = MM_REMAIN_ON_CHANNEL_CFM,	.rx_pfun = pwr_msg_rx_remain_on_channel_cfm},
	{.id = SM_CONNECT_IND,			.rx_pfun = pwr_msg_rx_connect_ind},
	{.id = SM_DISCONNECT_IND,		.rx_pfun = pwr_msg_rx_disconnect_ind},
	{.id = SCANU_RESULT_IND,		.rx_pfun = pwr_msg_rx_scanu_result_ind},
	{.id = MCU_WIFI_ON_OFF_CFM,		.rx_pfun = pwr_msg_rx_mcu_wifi_on_off_cfm},
	{.id = MM_CHANNEL_SWITCH_IND,		.rx_pfun = pwr_msg_rx_channel_switch_ind},
	{.id = MM_ADD_IF_CFM,			.rx_pfun = pwr_msg_rx_add_if_cfm},
	{.id = MM_P2P_VIF_PS_CHANGE_IND,	.rx_pfun = pwr_msg_rx_p2p_vif_ps_change_ind},
};

static void bes_pwr_msg_rx_process_fun(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	int i = 0;
	int tbl_len = sizeof(pwr_msg_rx_process_tbl) / sizeof(pwr_msg_rx_process_tbl[0]);

	for (i = 0; i < tbl_len; i++) {
		if (pwr_msg_rx_process_tbl[i].id != msg->id)
			continue;

		(pwr_msg_rx_process_tbl[i].rx_pfun)(rwnx_hw, msg);
	}
}

static bool bes_pwr_skip_msg_rx_process(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	struct rwnx_cmd *cmd = NULL;

	spin_lock_bh(&rwnx_hw->cmd_mgr.lock);
	list_for_each_entry(cmd, &rwnx_hw->cmd_mgr.cmds, list) {
		if (cmd->reqid == msg->id &&
		    (cmd->flags & RWNX_CMD_FLAG_LP_NOT_CARE_CFM) &&
		    (cmd->flags & RWNX_CMD_FLAG_WAIT_CFM)) {
			cmd->flags &= (~RWNX_CMD_FLAG_LP_NOT_CARE_CFM);
			spin_unlock_bh(&rwnx_hw->cmd_mgr.lock);

			return true;
		}
	}
	spin_unlock_bh(&rwnx_hw->cmd_mgr.lock);

	return false;
}

static bool bes_pwr_clear_msg_tx_event_by_msg(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	bool ret = false;
	struct rwnx_cmd *cmd = NULL;

	spin_lock_bh(&rwnx_hw->cmd_mgr.lock);
	/*
	 * If there are no items or more than one item,
	 * there is no need to perform a clear operation
	 */
	if (rwnx_cmd_mgr_queue_size(&rwnx_hw->cmd_mgr) != 1) {
		spin_unlock_bh(&rwnx_hw->cmd_mgr.lock);

		return false;
	}

	list_for_each_entry(cmd, &rwnx_hw->cmd_mgr.cmds, list) {
		/* Need cfm */
		if (cmd->reqid == msg->id &&
		    (cmd->flags & RWNX_CMD_FLAG_WAIT_CFM))
			ret = true;

		/* Only take the first element */
		break;
	}
	spin_unlock_bh(&rwnx_hw->cmd_mgr.lock);

	return ret;
}

void bes_pwr_msg_rx_process(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	/* Determine whether to not perform low-power processing */
	if (bes_pwr_no_event_exist(rwnx_hw) &&
	    bes_pwr_skip_msg_rx_process(rwnx_hw, msg)) {
		spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);
		bes_dbg(BES_DBG_PWR, "%s id:0x%x(%s) not care\n",
				__func__, msg->id, RWNX_ID2STR(msg->id));
		return;
	}
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);
	bes_dbg(BES_DBG_PWR, "%s id:0x%x(%s)\n",
				__func__, msg->id, RWNX_ID2STR(msg->id));

	/* Messages to be processed */
	bes_pwr_msg_rx_process_fun(rwnx_hw, msg);

	/* Determine if clearing MSG_TX event is necessary */
	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	if ((bes_pwr_get_phy_const_event(&rwnx_hw->pwr) & BIT(PHY_CONST_EVENT_MSG_TX)) &&
	    bes_pwr_clear_msg_tx_event_by_msg(rwnx_hw, msg)) {
		spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

		bes_pwr_clear_busy_event(rwnx_hw, RWNX_INVALID_VIF, PHY_CONST_EVENT_MSG_TX);

		return;
	}
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);
}

static bool bes_pwr_skip_msg_ack_rx_process(struct rwnx_hw *rwnx_hw)
{
	struct rwnx_cmd *cmd = NULL;

	spin_lock_bh(&rwnx_hw->cmd_mgr.lock);
	list_for_each_entry(cmd, &rwnx_hw->cmd_mgr.cmds, list) {
		if ((cmd->flags & RWNX_CMD_FLAG_LP_NOT_CARE_ACK) &&
		    (cmd->flags & RWNX_CMD_FLAG_WAIT_ACK)) {
			cmd->flags &= (~RWNX_CMD_FLAG_LP_NOT_CARE_ACK);
			spin_unlock_bh(&rwnx_hw->cmd_mgr.lock);

			return true;
		}
	}
	spin_unlock_bh(&rwnx_hw->cmd_mgr.lock);

	return false;
}

static bool bes_pwr_clear_msg_tx_event_by_msg_ack(struct rwnx_hw *rwnx_hw)
{
	bool ret = false;
	struct rwnx_cmd *cmd = NULL;

	spin_lock_bh(&rwnx_hw->cmd_mgr.lock);
	/*
	 * If there are no items or more than one item,
	 * there is no need to perform a clear operation
	 */
	if (rwnx_cmd_mgr_queue_size(&rwnx_hw->cmd_mgr) != 1) {
		spin_unlock_bh(&rwnx_hw->cmd_mgr.lock);

		return false;
	}

	list_for_each_entry(cmd, &rwnx_hw->cmd_mgr.cmds, list) {
		/* Wait ack, and don't need cfm */
		if ((cmd->flags & RWNX_CMD_FLAG_WAIT_ACK) &&
		    !(cmd->flags & RWNX_CMD_FLAG_WAIT_CFM))
			ret = true;

		/* Only take the first element */
		break;
	}
	spin_unlock_bh(&rwnx_hw->cmd_mgr.lock);

	return ret;
}

void bes_pwr_msg_ack_rx_process(struct rwnx_hw *rwnx_hw)
{
	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	/* Determine whether to not perform low-power processing */
	if (bes_pwr_no_event_exist(rwnx_hw) &&
	    bes_pwr_skip_msg_ack_rx_process(rwnx_hw)) {
		spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

		return;
	}

	/* Determine if clearing MSG_TX event is necessary */
	if ((bes_pwr_get_phy_const_event(&rwnx_hw->pwr) & BIT(PHY_CONST_EVENT_MSG_TX)) &&
	    bes_pwr_clear_msg_tx_event_by_msg_ack(rwnx_hw)) {
		spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

		bes_pwr_clear_busy_event(rwnx_hw, RWNX_INVALID_VIF, PHY_CONST_EVENT_MSG_TX);

		return;
	}
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);
}

void bes_pwr_data_rx_process(struct rwnx_hw *rwnx_hw, const struct sk_buff *skb, const struct hw_rxhdr *hw_rxhdr)
{
	struct rwnx_vif *rwnx_vif = NULL, *vif = NULL;
	u8 *frame = (u8 *)skb->data;
	u16 frame_len = skb->len;
	const struct ieee80211_mgmt *mgmt = NULL;

	mgmt = (const struct ieee80211_mgmt *)frame;

	if (ieee80211_is_beacon(mgmt->frame_control)) {
		/* Get rwnx_vif */
		if (hw_rxhdr->flags_vif_idx != RWNX_INVALID_VIF) {
			rwnx_vif = rwnx_rx_get_vif(rwnx_hw, hw_rxhdr->flags_vif_idx);
		} else {
			list_for_each_entry(vif, &rwnx_hw->vifs, list) {
				if (!vif->up)
					continue;
				rwnx_vif = vif;
				break;
			}
		}

		/* Intercept CSA fields from RX data */
		if (rwnx_vif)
			bes_pwr_csa_process(rwnx_vif, mgmt, frame_len);
	}
}

static void pwr_msg_tx_scan_start_req(struct rwnx_hw *rwnx_hw, struct rwnx_cmd_a2emsg *msg)
{
	u8 vif_idx = 0;

	vif_idx = ((struct scanu_start_req *)(msg->param))->vif_idx;
	if (bes_pwr_is_vif_up(rwnx_hw, vif_idx))
		bes_pwr_set_busy_event(rwnx_hw, RWNX_INVALID_VIF, PHY_CONST_EVENT_SCAN);
}

static void pwr_msg_tx_remian_on_channel_req(struct rwnx_hw *rwnx_hw, struct rwnx_cmd_a2emsg *msg)
{
	u8 vif_idx = 0;

	vif_idx = ((struct mm_remain_on_channel_req *)(msg->param))->vif_index;
	if (bes_pwr_is_vif_up(rwnx_hw, vif_idx)) {
		if (((struct mm_remain_on_channel_req *)(msg->param))->op_code == MM_ROC_OP_START)
			bes_pwr_set_busy_event(rwnx_hw, RWNX_INVALID_VIF, PHY_CONST_EVENT_ROC);
		else /* MM_ROC_OP_CANCEL*/
			bes_pwr_clear_busy_event(rwnx_hw, RWNX_INVALID_VIF, PHY_CONST_EVENT_ROC);
	}
}

static void pwr_msg_tx_connect_req(struct rwnx_hw *rwnx_hw, struct rwnx_cmd_a2emsg *msg)
{
	u8 vif_idx = 0;

	vif_idx = ((struct sm_connect_req *)(msg->param))->vif_idx;
	if (bes_pwr_is_vif_up(rwnx_hw, vif_idx))
		bes_pwr_set_busy_event(rwnx_hw, vif_idx, VIF_CONST_EVENT_CONNECT);
}

static void pwr_msg_tx_ap_start_req(struct rwnx_hw *rwnx_hw, struct rwnx_cmd_a2emsg *msg)
{
	u8 vif_idx = 0;
	struct rwnx_vif *rwnx_vif = NULL;

	vif_idx = ((struct apm_start_req *)(msg->param))->vif_idx;
	if (bes_pwr_is_vif_up(rwnx_hw, vif_idx)) {
		/* P2P GO dose not set AP event flag */
		rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);

		/* Check the validity of rwnx_vif */
		if (!rwnx_vif)
			return;

		if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_P2P_GO) {
			bes_pwr_set_busy_event(rwnx_hw, vif_idx, VIF_CONST_EVENT_AP);
		} else if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_GO) {
			/* Set flag to protect 4-way handshake */
			bes_pwr_set_busy_event(rwnx_hw, vif_idx, VIF_DELAY_EVENT_TX_KEY_FRM);
			/* Avoiding that P2P GO sends a beacon frame with PWR set to 1 before starting NOA or OPPPS */
			bes_pwr_set_busy_event(rwnx_hw, vif_idx, VIF_CONST_EVENT_GO_PRESENT);
		}

		bes_pwr_set_vif_link_map(rwnx_hw, vif_idx, false);
	}
}

static void pwr_msg_tx_ap_stop_req(struct rwnx_hw *rwnx_hw, struct rwnx_cmd_a2emsg *msg)
{
	u8 vif_idx = 0;
	struct rwnx_vif *rwnx_vif = NULL;

	vif_idx = ((struct apm_stop_req *)(msg->param))->vif_idx;
	if (val_get_bit(rwnx_hw->pwr.vif_up_map, vif_idx)) {
		rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);

		/* Check the validity of rwnx_vif */
		if (!rwnx_vif)
			return;

		if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_P2P_GO)
			bes_pwr_clear_busy_event(rwnx_hw, vif_idx, VIF_CONST_EVENT_AP);
		else
			bes_pwr_clear_busy_event(rwnx_hw, vif_idx, VIF_CONST_EVENT_GO_PRESENT);

		bes_pwr_clear_vif_link_map(rwnx_hw, vif_idx);
	}
}

static void pwr_msg_tx_remove_if_req(struct rwnx_hw *rwnx_hw, struct rwnx_cmd_a2emsg *msg)
{
	u8 vif_idx = ((struct mm_remove_if_req *)(msg->param))->inst_nbr;
	struct rwnx_vif *rwnx_vif = NULL;

	/* Check the validity of vif_index */
	if (vif_idx >= NX_ITF_MAX)
		return;

	/* Check the validity of rwnx_vif */
	rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);
	if (!rwnx_vif)
		return;

	if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_MONITOR)
		bes_pwr_clear_busy_event(rwnx_hw, vif_idx, VIF_CONST_EVENT_MONITOR);

	/* Determine whether wifi lp is start */
	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	if (!rwnx_hw->pwr.vif_up_map) {
		spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);
		bes_warn(BES_DBG_PWR, "%s WARNING: wifi lp is not started\n", __func__);
		return;
	}

	/* Update variable */
	rwnx_hw->pwr.vif_up_map &= ~BIT(vif_idx);
	rwnx_hw->pwr.vif_event[vif_idx] = 0;

	/* Check if it is the last closed VIF */
	if (!rwnx_hw->pwr.vif_up_map) {
		spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

		/* Deinit sleep manager lp_cb*/
		bes_sleep_manager_deinit(rwnx_hw);

		return;
	}
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

	bes_pwr_set_busy_event(rwnx_hw, vif_idx, VIF_DELAY_EVENT_DEL_VIF);
}

static void pwr_tx_custom_vif_info_req(struct rwnx_hw *rwnx_hw, struct mm_custom_req_hdr *hdr)
{
	u8 vif_idx;

	switch (hdr->ops) {
		case VIF_INFO_OPS_GET_CH_LOAD:
			vif_idx = ((struct vif_info_get_chan_load_req *)hdr)->vif_index;
			bes_pwr_set_busy_event_async(rwnx_hw, vif_idx, VIF_DELAY_EVENT_CH_LOAD_MEASURE);
			break;
		default:
			break;
	}
}

static void pwr_msg_tx_custom_req(struct rwnx_hw *rwnx_hw, struct rwnx_cmd_a2emsg *msg)
{
	struct mm_custom_req_hdr *hdr = (struct mm_custom_req_hdr *)(msg->param);
	switch (hdr->id) {
		case CUSTOM_ID_VIF_INFO:
			pwr_tx_custom_vif_info_req(rwnx_hw, hdr);
			break;
		default:
			break;
	}
}

const struct bes_pwr_msg_process_t pwr_msg_tx_process_tbl[] = {
	{.id = SCANU_START_REQ,			.tx_pfun = pwr_msg_tx_scan_start_req},
	{.id = MM_REMAIN_ON_CHANNEL_REQ,	.tx_pfun = pwr_msg_tx_remian_on_channel_req},
	{.id = SM_CONNECT_REQ,			.tx_pfun = pwr_msg_tx_connect_req},
	{.id = APM_START_REQ,			.tx_pfun = pwr_msg_tx_ap_start_req},
	{.id = APM_STOP_REQ,			.tx_pfun = pwr_msg_tx_ap_stop_req},
	{.id = MM_REMOVE_IF_REQ,		.tx_pfun = pwr_msg_tx_remove_if_req},
	{.id = MM_CUSTOM_REQ, 			.tx_pfun = pwr_msg_tx_custom_req},
};

static void bes_pwr_msg_tx_process_fun(struct rwnx_hw *rwnx_hw, struct rwnx_cmd_a2emsg *msg)
{
	int i = 0;
	int tbl_len = sizeof(pwr_msg_tx_process_tbl) / sizeof(pwr_msg_tx_process_tbl[0]);

	/* Set PHY_CONST_EVENT_MSG_TX when sending msg */
	bes_pwr_set_busy_event(rwnx_hw, RWNX_INVALID_VIF, PHY_CONST_EVENT_MSG_TX_PENDING);
	bes_pwr_set_busy_event(rwnx_hw, RWNX_INVALID_VIF, PHY_CONST_EVENT_MSG_TX);
	bes_pwr_clear_busy_event(rwnx_hw, RWNX_INVALID_VIF, PHY_CONST_EVENT_MSG_TX_PENDING);

	/* Handling special msgs */
	for (i = 0; i < tbl_len; i++) {
		if (pwr_msg_tx_process_tbl[i].id == msg->id) {
			(pwr_msg_tx_process_tbl[i].tx_pfun)(rwnx_hw, msg);
			break;
		}
	}
}

static bool bes_pwr_wakeup_by_non_blocking_msg(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd)
{
	struct pwr_action_param action_param = {0};

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	if (rwnx_hw->pwr.state == WIFI_LP_WAKE_CONFIG ||
	    rwnx_hw->pwr.state == WIFI_LP_WAKE) {
		spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);
		return false;
	}

	/* add defer cmd to list */
	list_add_tail(&cmd->list, &rwnx_hw->pwr.defer_cmds);

	/* Set msg_tx event and record wake-up event*/
	rwnx_hw->pwr.phy_event |= BIT(PHY_CONST_EVENT_MSG_TX);
	rwnx_hw->pwr.wakeup_event = PHY_CONST_EVENT_MSG_TX;
	rwnx_hw->pwr.wakeup_vif = RWNX_INVALID_VIF;

	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);
	/* Queue work directly to wakup mcu*/
	action_param.cancel_sync = false;
	action_param.sched_time = 0;
	action_param.actions |= BIT(PWR_ACTION_SCHED);

	/* Execute lp action */
	bes_pwr_actions_process(rwnx_hw, &action_param);

	return true;
}

bool bes_pwr_msg_tx_process(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd)
{
	struct rwnx_cmd_a2emsg *msg = cmd->a2e_msg;

	/* Filter messages sent before sleep to avoid waking up again */
	if (bes_pwr_get_lp_not_care_flag(cmd)) {
		bes_dbg(BES_DBG_PWR, "%s no need to care about msg(id:0x%x - %s)\n", __func__,
			msg->id, RWNX_ID2STR(msg->id));

		return false;
	}  else if (is_non_blocking_msg(msg->id)) {
		if (!bes_pwr_wakeup_by_non_blocking_msg(rwnx_hw, cmd)) {
			bes_dbg(BES_DBG_PWR, "%s id:0x%x(%s)\n", __func__, msg->id, RWNX_ID2STR(msg->id));
			return false;
		}

		bes_dbg(BES_DBG_PWR, "%s wakeup by non blocking msg(id:0x%x - %s)\n", __func__,
			msg->id, RWNX_ID2STR(msg->id));

		return true;
	}

	bes_dbg(BES_DBG_PWR, "%s id:0x%x(%s)\n", __func__, msg->id, RWNX_ID2STR(msg->id));

	bes_pwr_msg_tx_process_fun(rwnx_hw, msg);

	return false;
}

void bes_pwr_data_tx_process(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta, bool tx, bool buffered)
{
	int tid;
	struct rwnx_sta *cur_sta = NULL;
	struct rwnx_vif *rwnx_vif = NULL;
	u32_l count = 0;
	u8 vif_idx;

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	if (!rwnx_hw->pwr.vif_up_map) {
		spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);
		bes_warn(BES_DBG_PWR, "%s vif_up_map is empty", __func__);
		return;
	}
	if (buffered) {
		BUG_ON(!sta);

		/* Check the validity of rwnx_vif */
		rwnx_vif = rwnx_rx_get_vif(rwnx_hw, sta->vif_idx);
		if (!rwnx_vif) {
			spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);
			bes_warn(BES_DBG_PWR, "rwnx_vif null, vif_idx %d sta_idx %d",
				sta->vif_idx, sta->sta_idx);
			return;
		}

		/* Check the validity of vif_index */
		vif_idx = rwnx_vif->vif_index;
		if (vif_idx >= NX_ITF_MAX) {
			spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);
			return;
		}

		/* Avoid repeating to set or clear */
		if ((tx && (bes_pwr_get_vif_const_event(&rwnx_hw->pwr, vif_idx) & BIT(VIF_CONST_EVENT_DATA_TX))) ||
		    (!tx && !(bes_pwr_get_vif_const_event(&rwnx_hw->pwr, vif_idx) & BIT(VIF_CONST_EVENT_DATA_TX)))) {
			spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

			return;
		}
		spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

		if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION || RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_CLIENT) {
			for (tid = 0; tid < TID_MAX; tid++)
				count += rwnx_hw->ipc_env->shared->buffered[sta->sta_idx][tid];
		} else {
			list_for_each_entry(cur_sta, &rwnx_vif->ap.sta_list, list) {
				for (tid = 0; tid < TID_MAX; tid++)
					count += rwnx_hw->ipc_env->shared->buffered[cur_sta->sta_idx][tid];
			}
		}

		if (tx && count)
			bes_pwr_set_busy_event_async(rwnx_hw, rwnx_vif->vif_index, VIF_CONST_EVENT_DATA_TX);
		else if (!tx && !count)
			bes_pwr_clear_busy_event(rwnx_hw, rwnx_vif->vif_index, VIF_CONST_EVENT_DATA_TX);
	} else {
		/* Avoid repeating to set or clear */
		if ((tx && (bes_pwr_get_phy_const_event(&rwnx_hw->pwr) & BIT(PHY_CONST_EVENT_DATA_TX))) ||
		    (!tx && !(bes_pwr_get_phy_const_event(&rwnx_hw->pwr) & BIT(PHY_CONST_EVENT_DATA_TX)))) {
			spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

			return;
		}
		spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

		if (tx && rwnx_hw->ipc_env->unbuffered)
			bes_pwr_set_busy_event_async(rwnx_hw, RWNX_INVALID_VIF, PHY_CONST_EVENT_DATA_TX);
		else if (!tx && !rwnx_hw->ipc_env->unbuffered)
			bes_pwr_clear_busy_event(rwnx_hw, RWNX_INVALID_VIF, PHY_CONST_EVENT_DATA_TX);
	}
}

void bes_pwr_get_gateway_ip(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, struct sk_buff *skb)
{
	u32 gateway_ip = 0;

	/* Determine if parsing is necessary */
	if (!rwnx_hw->pwr.vif_up_map ||
	    rwnx_vif->vif_index == RWNX_INVALID_VIF ||
	    (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_STATION &&
	     RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_P2P_CLIENT) ||
	    rwnx_vif->sta.gateway_ip)
		return;

	/* Get gateway ip */
	if (!bes_common_parse_dhcp_ack_frame(skb, &gateway_ip))
		return;

	/* Set gateway ip */
	rwnx_vif->sta.gateway_ip = gateway_ip;
}

int bes_pwr_event_dump(struct rwnx_hw *rwnx_hw, char *buf, size_t size, size_t count)
{
	int len = 0;
	u8 vif_idx = 0;
	u32 j = 0;
	unsigned long cur_jiffies = jiffies, mtjiff_tmp = cur_jiffies;

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	len += scnprintf(&buf[len], min_t(size_t, size - 1, count), "Power_save status: %s ps_mode:%s\n",
			bes_pwr_get_lp_state_name(rwnx_hw->pwr.state),
			(rwnx_hw->user_ps_mode == MM_PS_MODE_OFF) ?
				("OFF") : ((rwnx_hw->user_ps_mode == MM_PS_MODE_ON) ? "ON" : "ON_DYN"));

	/* During the process of lowering the GPIO sleep WiFi, no printing occurs */
	if (rwnx_hw->pwr.state == WIFI_LP_SLEEPING ||
	    rwnx_hw->pwr.state == WIFI_LP_SLEEPED) {
		spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);
		return len;
	}

	/* Event title */
	len += scnprintf(&buf[len], min_t(size_t, size - len - 1, count),
			"%-15s\t\t%s\t\t%s\t\t%s\n",
			"EVENT", "Flag", "VIF", "Timeout(ms)");
	bes_pwr_update_delay_event_and_get_max_timeout(rwnx_hw, &mtjiff_tmp);

	/* Phy deley event */
	for (j = PHY_DELAY_EVENT_MIN; j < PHY_CONST_EVENT_MIN; j++) {
		if (val_get_bit(rwnx_hw->pwr.phy_event, j))
			len += scnprintf(&buf[len], min_t(size_t, size - len - 1, count),
					"%-15s\t\t%s\t\t%d\t\t%d\n",
					bes_pwr_get_phy_event_name(j), "D", -1,
					time_after_eq(cur_jiffies, rwnx_hw->pwr.phy_timeout[j]) ?
						      0 : jiffies_to_msecs(rwnx_hw->pwr.phy_timeout[j] - cur_jiffies));
	}

	/* Phy constant event */
	for (j = PHY_CONST_EVENT_MIN; j < PHY_EVENT_MAX; j++) {
		if (val_get_bit(rwnx_hw->pwr.phy_event, j))
			len += scnprintf(&buf[len], min_t(size_t, size - len - 1, count),
					"%-15s\t\t%s\t\t%d\t\t%d\n",
					bes_pwr_get_phy_event_name(j), "C", -1, -1);
	}

	/* Vif deley event */
	for (j = VIF_DELAY_EVENT_MIN; j < VIF_CONST_EVENT_MIN; j++) {
		for (vif_idx = 0; vif_idx < NX_ITF_MAX; vif_idx++) {
			if (val_get_bit(rwnx_hw->pwr.vif_up_map, vif_idx) &&
			    val_get_bit(rwnx_hw->pwr.vif_event[vif_idx], j))
				len += scnprintf(&buf[len], min_t(size_t, size - len - 1, count),
						"%-15s\t\t%s\t\t%d\t\t%d\n",
						bes_pwr_get_vif_event_name(j), "D", vif_idx,
						time_after_eq(cur_jiffies, rwnx_hw->pwr.vif_timeout[vif_idx][j]) ?
							      0 : jiffies_to_msecs(rwnx_hw->pwr.vif_timeout[vif_idx][j] - cur_jiffies));
		}
	}

	/* Vif constant event */
	for (j = VIF_CONST_EVENT_MIN; j < VIF_EVENT_MAX; j++) {
		for (vif_idx = 0; vif_idx < NX_ITF_MAX; vif_idx++) {
			if (val_get_bit(rwnx_hw->pwr.vif_up_map, vif_idx) &&
			    val_get_bit(rwnx_hw->pwr.vif_event[vif_idx], j))
				len += scnprintf(&buf[len], min_t(size_t, size - len - 1, count),
						"%-15s\t\t%s\t\t%d\t\t%d\n",
						bes_pwr_get_vif_event_name(j), "C", vif_idx, -1);
		}
	}
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

	return len;
}

bool bes_pwr_exist_pwr_events(struct rwnx_hw *rwnx_hw)
{
	u32 events = 0;
	u8 vif_idx = 0;
	struct rwnx_vif *rwnx_vif = NULL;

	/* Get pwr lock to avoid conflict */
	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);

	/* Get Phy Power events */
	events = rwnx_hw->pwr.phy_event;

	/* Get Vif Power events */
	for (vif_idx = 0; vif_idx < NX_ITF_MAX; vif_idx++) {
		u8 idx;
		/* vif needs to be open */
		rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);
		if (!rwnx_vif)
			continue;

		/* Check the validity of idx */
		idx = rwnx_vif->vif_index;
		if (idx >= NX_ITF_MAX)
			continue;

		/* Determine if there is an event on this vif */
		events |= rwnx_hw->pwr.vif_event[idx];
	}

	/* Release pwr lock*/
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

	return (events != 0);
}

static void bes_pwr_data_rx_moniter_traffic(void *arg)
{
	struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)arg;
	u8 vif_idx = 0;
	struct rwnx_sta *cur_sta = NULL;
	struct rwnx_vif *rwnx_vif = NULL;
	u32 rx_pkts_rate = 0;

	for (vif_idx = 0; vif_idx < NX_ITF_MAX; vif_idx++) {
		/* vif needs to be connected */
		rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);
		if (!rwnx_vif || !netif_carrier_ok(rwnx_vif->ndev))
			continue;

		/* Get data rx traffic rate for current vif */
		if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION || RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_CLIENT) {
			cur_sta = rwnx_vif->sta.ap;
			if (!cur_sta)
				continue;

			rx_pkts_rate = cur_sta->stats.rx_traffic_info.pkts_rate;
		} else {
			list_for_each_entry(cur_sta, &rwnx_vif->ap.sta_list, list)
				rx_pkts_rate += cur_sta->stats.rx_traffic_info.pkts_rate;
		}

		/* Not reach the threshold of data rx traffic rate, and continue to the next one */
		if (rx_pkts_rate < DATA_RX_TRAFFIC_RATE_THRESHOLD)
			continue;

		/* Set data_rx event to prevent sleeping  */
		bes_pwr_set_busy_event_async(rwnx_hw, rwnx_vif->vif_index, VIF_DELAY_EVENT_DATA_RX);
	}
}

static void bes_pwr_flush_defer_cmds_list(struct rwnx_hw *rwnx_hw)
{
	struct rwnx_cmd *defer_cmd = NULL, *tmp = NULL;

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	list_for_each_entry_safe(defer_cmd, tmp, &rwnx_hw->pwr.defer_cmds, list) {
		list_del(&defer_cmd->list);
		kfree(defer_cmd->a2e_msg);
		kfree(defer_cmd);
	}
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);
}

int bes_pwr_init(struct rwnx_hw *rwnx_hw, bes_pwr_wake_ctrl_ptr wake_ctrl)
{
	u8 i = 0;

	/* Malloc cb_tbl memory */
	rwnx_hw->pwr.cb_cnt = 0;
	rwnx_hw->pwr.cb_tbl = kzalloc(sizeof(struct bes_pwr_lp_cb_item) * PWR_CB_CNT_MAX, GFP_KERNEL);
	if (!rwnx_hw->pwr.cb_tbl) {
		bes_err(BES_DBG_TRAFFIC, "%s: pwr.cb_tbl allocation failed\n", __func__);
		return -ENOMEM;
	}

	/* Init the interface of control wifi */
	rwnx_hw->pwr.wake_ctrl = wake_ctrl;

	/* Init current schedule time for delay event */
	rwnx_hw->pwr.phy_timeout = phy_timeout;
	for (i = 0; i < NX_ITF_MAX; i++)
		rwnx_hw->pwr.vif_timeout[i] = vif_timeout[i];

	/* Init deleyed event schedule timeout */
	rwnx_hw->pwr.phy_d_evt_timeout = phy_d_evt_timeout;
	rwnx_hw->pwr.vif_d_evt_timeout = vif_d_evt_timeout;
	if (rwnx_mod_params.uapsd_timeout <= 0) {
		for (i = 0; i < PHY_CONST_EVENT_MIN; i++)
			((struct pwr_phy_d_evt_timeout *)rwnx_hw->pwr.phy_d_evt_timeout)[i].timeout =
				msecs_to_jiffies(phy_d_evt_t_default[i].timeout);
		for (i = 0; i < VIF_CONST_EVENT_MIN; i++)
			((struct pwr_vif_d_evt_timeout *)rwnx_hw->pwr.vif_d_evt_timeout)[i].timeout =
				msecs_to_jiffies(vif_d_evt_t_default[i].timeout);
	} else {
		for (i = 0; i < PHY_CONST_EVENT_MIN; i++)
			((struct pwr_phy_d_evt_timeout *)rwnx_hw->pwr.phy_d_evt_timeout)[i].timeout =
				msecs_to_jiffies(phy_d_evt_t_uapsd[i].timeout);
		for (i = 0; i < VIF_CONST_EVENT_MIN; i++)
			((struct pwr_vif_d_evt_timeout *)rwnx_hw->pwr.vif_d_evt_timeout)[i].timeout =
				msecs_to_jiffies(vif_d_evt_t_uapsd[i].timeout);
	}

	/* Init all event */
	rwnx_hw->pwr.phy_event = 0;
	for (i = 0; i < NX_ITF_MAX; i++)
		rwnx_hw->pwr.vif_event[i] = 0;

	/* Basic init */
	rwnx_hw->pwr.vif_up_map = 0;
	rwnx_hw->pwr.vif_link_map = 0;
	rwnx_hw->pwr.pwr_task = NULL;
	rwnx_hw->pwr.lp_wakeup_wait = false;
	rwnx_hw->pwr.wait_wake = false;
	rwnx_hw->pwr.cur_chanctx = RWNX_CH_NOT_SET;
	rwnx_hw->pwr.flag_suspend = false;
	spin_lock_init(&rwnx_hw->pwr.pwr_lock);
	init_waitqueue_head(&rwnx_hw->pwr.switch_wq);
	INIT_DELAYED_WORK(&rwnx_hw->pwr.lp_work, bes_pwr_lp_work_func);
	spin_lock_init(&rwnx_hw->pwr.pwr_cb_lock);

	/* Init defer cmd list */
	INIT_LIST_HEAD(&rwnx_hw->pwr.defer_cmds);

	/* Set init_event */
	bes_pwr_set_phy_const_event(rwnx_hw, PHY_CONST_EVENT_INIT);

	/* Pull up gpio and set pwr_state to wakeup */
	bes_pwr_wake_ctrl(rwnx_hw, true);
	bes_pwr_wifi_lp_state_set(rwnx_hw, WIFI_LP_WAKE);

	/* Register data traffic function */
	if (rwnx_mod_params.uapsd_timeout <= 0)
		bes_data_monitor_register_lp_cb(rwnx_hw, false, bes_pwr_data_rx_moniter_traffic, rwnx_hw);

	return 0;
}

void bes_pwr_deinit(struct rwnx_hw *rwnx_hw)
{
	u8 i = 0;

	/* Ensure that lp_work is complete */
	cancel_delayed_work_sync(&rwnx_hw->pwr.lp_work);

	/* Pull down gpio and set pwr_state to sleep */
	bes_pwr_wake_ctrl(rwnx_hw, false);
	bes_pwr_wifi_lp_state_set(rwnx_hw, WIFI_LP_SLEEPED);

	/* Unregister data traffic function */
	if (rwnx_mod_params.uapsd_timeout <= 0)
		bes_data_monitor_unregister_lp_cb(rwnx_hw, false, bes_pwr_data_rx_moniter_traffic);

	/* Deinit all event */
	rwnx_hw->pwr.phy_event = 0;
	for (i = 0; i < NX_ITF_MAX; i++)
		rwnx_hw->pwr.vif_event[i] = 0;

	/* Flush defer cmds list */
	bes_pwr_flush_defer_cmds_list(rwnx_hw);

	/* Basic deinit */
	rwnx_hw->pwr.vif_up_map = 0;
	rwnx_hw->pwr.vif_link_map = 0;
	rwnx_hw->pwr.pwr_task = NULL;
	rwnx_hw->pwr.lp_wakeup_wait = false;
	rwnx_hw->pwr.wait_wake = false;
	rwnx_hw->pwr.cur_chanctx = RWNX_CH_NOT_SET;
	rwnx_hw->pwr.flag_suspend = false;
	rwnx_hw->pwr.cb_cnt = 0;
	if (rwnx_hw->pwr.cb_tbl) {
		kfree(rwnx_hw->pwr.cb_tbl);
		rwnx_hw->pwr.cb_tbl = NULL;
	}

	/* Deinit the interface of control wifi */
	rwnx_hw->pwr.wake_ctrl = NULL;
}