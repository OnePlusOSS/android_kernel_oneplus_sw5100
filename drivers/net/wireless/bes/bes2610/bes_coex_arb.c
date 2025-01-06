/*
 * @file bes_coex_arb.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "bes_coex.h"
#include "rwnx_msg_tx.h"
#include "bes_power_ctrl.h"
#include "bes_dbac_cali.h"
#include "bes_log.h"
#include "bes_sysfs.h"
#include "bes_pwr.h"

#define BES_COEX_INIT_MAGIC		0xA55AAA55

typedef void (*arb_action_cb)(struct rwnx_hw *rwnx_hw);

struct bes_coex_csa_param {
	char if_name[IFNAMSIZ];
	u16 csa_cnt;
	u16 csa_freq;
	u32 csa_freq1;
	u16 csa_bw;
	int sec_chan_offset;
};

struct bes_coex_csa_env {
	char if_name[IFNAMSIZ + 12];
	char cs_cnt[20];
	char freq[25];
	char freq1[25];
	char bw[20];
	char sec_chan_offset[30];
	char ht[20];
	char vht[20];
	char he[20];
	char block_tx[30];
};

static u16 bes_coex_calc_csa_cnt(struct rwnx_vif *vif)
{
	struct rwnx_sta *sta;
	bool find_ps = false;

	/* sta not connect */
	if (list_empty(&vif->ap.sta_list))
		return 1;

	list_for_each_entry(sta, &vif->ap.sta_list, list) {
		if (sta->ps.active) {
			find_ps = true;
			break;
		}
	}

	/* stations have connected, and ps mode station is existing */
	if (find_ps)
		return 10;

	/* stations have connected, and ps mode station is not existed */
	return 5;
}

/**
 * bes_coex_csa_para_parse - parse channel definition parameter
 *
 * @vif: ap interface
 * @chandef: channel definition
 * @csa_para: csa parameters
 */
static void bes_coex_csa_para_parse(struct rwnx_vif *vif, struct cfg80211_chan_def *chandef,
				    struct bes_coex_csa_param *csa_para)
{
	u32 high_freq, low_freq;

	if (chandef != NULL) {
		csa_para->csa_freq = chandef->chan->center_freq;
		csa_para->csa_freq1 = chandef->center_freq1;
	} else {
		/* default freq */
		csa_para->csa_freq = 5745;
		csa_para->csa_freq1 = 5775;
	}

	if (csa_para->csa_freq == csa_para->csa_freq1)
		csa_para->sec_chan_offset = 0;
	else if (csa_para->csa_freq1 > csa_para->csa_freq)
		csa_para->sec_chan_offset = 1;
	else
		csa_para->sec_chan_offset = -1;

	high_freq = max_t(u32, csa_para->csa_freq, csa_para->csa_freq1);
	low_freq = min_t(u32, csa_para->csa_freq, csa_para->csa_freq1);

	if (high_freq - low_freq == 10)
		csa_para->csa_bw = 40;
	else if (high_freq - low_freq == 30)
		csa_para->csa_bw = 80;
	else
		csa_para->csa_bw = 20;

	csa_para->csa_cnt = bes_coex_calc_csa_cnt(vif);
	memcpy(csa_para->if_name, vif->ndev->name, strlen(vif->ndev->name) + 1);
}

/**
 * fill csa parameter, and notiy userspace
 */
static void __bes_coex_csa_notify(struct rwnx_vif *vif, struct cfg80211_chan_def *chandef,
				  struct bes_coex_csa_env *csa_env)
{
	struct bes_coex_csa_param csa_para = {0};
	struct bes_sysfs *bes_sysfs = bes_get_sysfs();
	char sub_event[] = BES_UEVET_ACTION_PREFIX BES_UEVET_CSA;
	char *env[] = { sub_event, csa_env->if_name, csa_env->cs_cnt, csa_env->freq,
			csa_env->freq1, csa_env->bw, csa_env->sec_chan_offset, csa_env->ht,
			csa_env->vht, csa_env->he, csa_env->block_tx, NULL};

	bes_coex_csa_para_parse(vif, chandef, &csa_para);

	snprintf(csa_env->if_name, sizeof(csa_env->if_name), "BES_CSA_IF=%s", csa_para.if_name);
	snprintf(csa_env->cs_cnt, sizeof(csa_env->cs_cnt), "BES_CSA_CNT=%u", csa_para.csa_cnt);
	snprintf(csa_env->freq, sizeof(csa_env->freq), "BES_CSA_FREQ=%u", csa_para.csa_freq);
	snprintf(csa_env->freq1, sizeof(csa_env->freq1), "BES_CSA_FREQ1=%u", csa_para.csa_freq1);
	snprintf(csa_env->bw, sizeof(csa_env->bw), "BES_CSA_BW=%u", csa_para.csa_bw);
	snprintf(csa_env->sec_chan_offset, sizeof(csa_env->sec_chan_offset),
		"BES_CSA_SEC_CHAN_OFT=%d", csa_para.sec_chan_offset);

	snprintf(csa_env->ht, sizeof(csa_env->ht), "BES_CSA_HT=%s", "ht");
	snprintf(csa_env->vht, sizeof(csa_env->vht), "BES_CSA_VHT=%s", "vht");
	snprintf(csa_env->he, sizeof(csa_env->he), "BES_CSA_HE=%s", "he");
	snprintf(csa_env->block_tx, sizeof(csa_env->block_tx), "BES_CSA_BLOCKTX=%s", "blocktx");

	kobject_uevent_env(&bes_sysfs->dev->kobj, KOBJ_CHANGE, env);
}

static void bes_coex_csa_notify(struct rwnx_vif *vif, struct cfg80211_chan_def *chandef)
{
	struct bes_coex_csa_env *csa_env = kmalloc(sizeof(struct bes_coex_csa_env), GFP_KERNEL);

	if (WARN_ON(!csa_env)) {
		bes_err(BES_DBG_COEX, "%s, out of memory\n", __func__);
		return;
	}

	__bes_coex_csa_notify(vif, chandef, csa_env);

	kfree(csa_env);
}

static void bes_coex_arb_set_coex_start(struct rwnx_hw *rwnx_hw)
{
	struct bes_coex_cmd coex_cmd = {0};

	coex_cmd.coex_cmd_id = BES_COEX_CMD_START;
	bes_info(BES_DBG_COEX, "arb coex start\n");

	rwnx_send_pta_cfg(rwnx_hw, &coex_cmd);
}

static void bes_coex_arb_set_coex_stop(struct rwnx_hw *rwnx_hw)
{
    struct bes_coex_cmd coex_cmd = {0};

	coex_cmd.coex_cmd_id = BES_COEX_CMD_STOP;
	bes_info(BES_DBG_COEX, "arb coex stop\n");

	rwnx_send_pta_cfg(rwnx_hw, &coex_cmd);
}

static int bes_coex_arb_sync_duration(struct rwnx_hw *rwnx_hw, u32 wifi_dur, u32 bt_dur)
{
	struct bes_coex_cmd coex_cmd = {0};

	coex_cmd.coex_cmd_id = BES_COEX_CMD_DURATION;
	coex_cmd.u.time_ratio.wifi_duration = wifi_dur;
	coex_cmd.u.time_ratio.bt_duration = bt_dur;
	bes_info(BES_DBG_COEX, "set coex duration, wifi:%d bt:%d\n", wifi_dur, bt_dur);

	return rwnx_send_pta_cfg(rwnx_hw, &coex_cmd);
}

static void bes_coex_arb_sync_pta_flags(struct rwnx_hw *rwnx_hw)
{
    struct bes_coex_cmd coex_cmd = {0};
	u32 flags = 0;

	spin_lock(&rwnx_hw->coex.lock);
	flags = rwnx_hw->coex.pta_switch_flags;
	spin_unlock(&rwnx_hw->coex.lock);

	coex_cmd.coex_cmd_id = BES_COEX_CMD_SET_PTA_FLAG;
	coex_cmd.u.pta_switch_flags = flags;
	bes_info(BES_DBG_COEX, "set pta flags:0x%08x\n", flags);

	rwnx_send_pta_cfg(rwnx_hw, &coex_cmd);
}

static void bes_coex_arb_set_mode(struct rwnx_hw *rwnx_hw)
{
	struct bes_coex_cmd coex_cmd = {0};

	coex_cmd.coex_cmd_id = BES_COEX_CMD_MODE;
	coex_cmd.u.coex_mode = rwnx_hw->coex.coex_mode;
	bes_info(BES_DBG_COEX, "change coex mode to %s\n",
		 (rwnx_hw->coex.coex_mode == BES_COEX_MODE_TDD) ? "tdd" : "fdd");

	rwnx_send_pta_cfg(rwnx_hw, &coex_cmd);
}

static void bes_coex_arb_sync_bt_state(struct rwnx_hw *rwnx_hw)
{
	struct bes_coex_cmd coex_cmd = {0};
	coex_cmd.coex_cmd_id = COEX_CMD_ID_BT_STAT;
	coex_cmd.u.bt_state = rwnx_hw->coex.bt_coex_state;
	bes_info(BES_DBG_COEX, "sync bt state\n");

	rwnx_send_pta_cfg(rwnx_hw, &coex_cmd);
}

static void bes_coex_arb_lp_sche(struct rwnx_hw *rwnx_hw, bool lp_sche)
{
	struct bes_coex_cmd coex_cmd = {0};
	static bool cur_lp_sche = false;

	/* Do nothing if state is not change */
	if (lp_sche == cur_lp_sche)
		return;

	/* Update current schedule state */
	cur_lp_sche = lp_sche;

	/* Send command to device */
	coex_cmd.coex_cmd_id = BES_COEX_CMD_LP_SCHE;
	coex_cmd.u.lp_sche = lp_sche;
	bes_info(BES_DBG_COEX, "%s lp schedule\n", lp_sche ? "enter" : "exit");

	rwnx_send_pta_cfg(rwnx_hw, &coex_cmd);
}

static void bes_coex_arb_set_duration(struct rwnx_hw *rwnx_hw)
{
	u32 wifi_dur = 0, bt_dur = 0;

	/* calculate duration info */
	bt_dur = bes_coex_bt_calc_time();
	wifi_dur = BES_COEX_PERIOD - bt_dur;

	/* set duration info to device */
	bes_coex_arb_sync_duration(rwnx_hw, wifi_dur, bt_dur);
}

static void bes_coex_arb_notify_ch_switch(struct rwnx_hw *rwnx_hw)
{
	u32 ch_idx = 0;
	struct rwnx_vif *vif = NULL;
	struct rwnx_chanctx *ctxt = NULL;
	struct cfg80211_chan_def *chandef = NULL;

	/* check if 2.4g ap has connected */
	if (!(bes_coex_wifi_get_status() & BIT(WIFI_2G4_AP_SETTED)))
		return;

	/* get ap net devicee */
	list_for_each_entry(vif, &rwnx_hw->vifs, list) {
		if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_AP)
			break;
	}

	/* get channeldef that belongs to 5g band */
	for (ch_idx = 0; ch_idx < NX_CHAN_CTXT_CNT; ch_idx++) {
		ctxt = &rwnx_hw->chanctx_table[ch_idx];
		if (ctxt->count > 0 && ch_idx != vif->ch_index &&
			ctxt->chan_def.chan->band != NL80211_BAND_2GHZ) {
			chandef = &ctxt->chan_def;
			break;
		}
	}

	/* notify userspace to switch new channel */
	bes_coex_csa_notify(vif, chandef);
}

void bes_coex_arb_rf_cali_control(struct rwnx_hw *rwnx_hw)
{
	/* Check if we can do defered calibration. */
	bes_dbac_defer_cali_check(rwnx_hw);

	/* Turn off dynamic calibration when needed. */
	bes_dbac_dyna_cali_check(rwnx_hw);
}

void bes_coex_arb_exit_lp_sche(struct rwnx_hw *rwnx_hw)
{
	bes_coex_arb_lp_sche(rwnx_hw, false);
}

static arb_action_cb act_cb_tbl[] = {
	bes_coex_bt_sync_status,	// BES_ARB_OPER_GET_BT_STATUS
	bes_coex_arb_set_duration,	// BES_ARB_OPER_DUR_SET
	bes_coex_arb_sync_bt_state,     // BES_ARB_OPER_SYNC_BT_STATE
	bes_coex_arb_set_mode,		// BES_ARB_OPER_COEX_MODE
	bes_coex_arb_set_coex_start,	// BES_ARB_OPER_COEX_START
	bes_coex_arb_set_coex_stop,	// BES_ARB_OPER_COEX_STOP
	bes_coex_arb_sync_pta_flags,	// BES_ARB_OPER_PTA_FLAGS
	bes_coex_arb_rf_cali_control,	// BES_ARB_OPER_RF_CALI
	bes_coex_arb_notify_ch_switch,  // BES_ARB_OPER_AP_CSA
};

static void bes_coex_arb_work(struct work_struct *work)
{
	struct bes_coex *coex = container_of(work, struct bes_coex, work);
	struct rwnx_hw *rwnx_hw = container_of(coex, struct rwnx_hw, coex);
	unsigned long int action = 0;
	unsigned long int bit = 0;

	/* get pending action */
	spin_lock(&coex->lock);
	action = coex->pending_action;
	coex->pending_action = 0;
	spin_unlock(&coex->lock);

	do {
		/* do action callback based on action */
		for_each_set_bit(bit, &action, BES_ARB_OPER_MAX) {
			act_cb_tbl[bit](rwnx_hw);
		}

		/* check if there is new pending action */
		spin_lock(&coex->lock);
		action = coex->pending_action;
		coex->pending_action = 0;
		spin_unlock(&coex->lock);
	} while(action);

}

static void bes_coex_ps_mode_handler(void *arg, u8 status)
{
	struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)arg;

	/* skip operation for fdd or bt is disabled */
	if (rwnx_hw->coex.hw_fdd ||
	    rwnx_hw->coex.coex_mode == BES_COEX_MODE_FDD ||
	    !bes_power_ctrl_bt_enabled())
		return;

	/* notify host lp state */
	if (status == PWR_LP_CB_STATUS_BEFORE_WIFI_SLEEP) {
		/* Enter low power schedule when wifi idle */
		if (!bes_pwr_exist_pwr_events(rwnx_hw))
			bes_coex_arb_lp_sche(rwnx_hw, true);
	} else if (status == PWR_LP_CB_STATUS_AFTER_WIFI_WAKEUP) {
		bes_coex_arb_lp_sche(rwnx_hw, false);
	}
}

static bool bes_coex_bt_check_wifi_idle(struct rwnx_hw *rwnx_hw)
{
	struct rwnx_vif *vif = NULL;

	/* check if there is an vif that is working */
	list_for_each_entry(vif, &rwnx_hw->vifs, list) {
		if (!netif_running(vif->ndev))
			continue;

		if (netif_carrier_ok(vif->ndev))
			return false;
	}

	return true;
}

void bes_coex_arb_up(struct rwnx_hw *rwnx_hw)
{
	u32 actions = 0;
	bool bt_self_mgmd = !!CONFIG_BES_POWER_CTRL_BT_SELF_MANAGED;

	if (rwnx_hw->coex.coex_mode == BES_COEX_MODE_TDD) {
		if (bt_self_mgmd || bes_power_ctrl_bt_enabled())
			actions |= BIT(BES_ARB_OPER_GET_BT_STATUS);

		bes_coex_arb_set_wifi_state(rwnx_hw, 0, actions);

		bes_pwr_register_lp_cb(rwnx_hw, bes_coex_ps_mode_handler, rwnx_hw);
	} else {
		bes_coex_arb_set_mode(rwnx_hw);
	}
}

void bes_coex_arb_down(struct rwnx_hw *rwnx_hw)
{
	/* Unregister low power callback */
	if (rwnx_hw->coex.coex_mode == BES_COEX_MODE_TDD)
		bes_pwr_unregister_lp_cb(rwnx_hw, bes_coex_ps_mode_handler);

	/* Cancel coex work */
	cancel_work_sync(&rwnx_hw->coex.work);
}

void bes_coex_arb_init(struct rwnx_hw *rwnx_hw, bool fdd)
{
	u32 actions = 0;
	bool bt_self_mgmd = !!CONFIG_BES_POWER_CTRL_BT_SELF_MANAGED;

	/* init arb module */
	rwnx_hw->coex.hw_fdd = fdd;
	rwnx_hw->coex.bt_coex_state = 0;
	rwnx_hw->coex.wifi_coex_state = 0;
	spin_lock_init(&rwnx_hw->coex.lock);
	INIT_WORK(&rwnx_hw->coex.work, bes_coex_arb_work);

	/* init wifi and bt module */
	bes_coex_wifi_init();
	bes_coex_bt_init();

	/* set device coex status */
	if (!fdd) {
		rwnx_hw->coex.coex_mode = BES_COEX_MODE_TDD;

		if (bt_self_mgmd || bes_power_ctrl_bt_enabled())
			actions |= BIT(BES_ARB_OPER_GET_BT_STATUS);

		bes_coex_arb_set_wifi_state(rwnx_hw, 0, actions);
	} else {
		rwnx_hw->coex.coex_mode = BES_COEX_MODE_FDD;
		bes_coex_arb_set_mode(rwnx_hw);
	}

	/* set inited flag */
	rwnx_hw->coex.inited = BES_COEX_INIT_MAGIC;
}

void bes_coex_arb_deinit(struct rwnx_hw *rwnx_hw)
{
	/* Cancel coex work */
	cancel_work_sync(&rwnx_hw->coex.work);

	/* reset inited flag */
	rwnx_hw->coex.inited = 0;
}

bool bes_coex_arb_check_sta_assoc(struct rwnx_hw *rwnx_hw, struct ieee80211_channel *channel)
{
	u8 i = 0;

	/* do nothing for fdd mode */
	if (rwnx_hw->coex.hw_fdd)
		return true;

	if (channel == NULL) {
		bes_warn(BES_DBG_COEX, "%s channel is null\n", __func__);
		return true;
	}

	/* wait last coex setting end */
	flush_work(&rwnx_hw->coex.work);

	/* don't restrict sta link when bt is disabled */
	if (!bes_power_ctrl_bt_enabled())
		goto stop_coex;

	/* don't restrict sta link when the link works on 5G */
	if (channel->band != NL80211_BAND_2GHZ)
		goto stop_coex;

	/* don't restrict sta link when this is the first link */
	if (atomic_add_return(0, &rwnx_hw->chanctx_count) == 0)
		goto stop_coex;

	/* check if there is an another link that works on 2.4g but with different channel */
	for (i = 0; i < NX_CHAN_CTXT_CNT; i++) {
		struct rwnx_chanctx *ctxt = &rwnx_hw->chanctx_table[i];

		if ((ctxt->count > 0) &&
		    (ctxt->chan_def.chan->band == NL80211_BAND_2GHZ) &&
			(ctxt->chan_def.chan != channel)) {
			bes_warn(BES_DBG_COEX,
				 "don't support coex between bt and two sta with diffrent 2.4g chan\n");
			return false;
		}
	}

stop_coex:
	bes_coex_arb_set_coex_stop(rwnx_hw);

	return true;
}

bool bes_coex_arb_check_ap_setup(struct rwnx_hw *rwnx_hw, struct cfg80211_chan_def *chandef)
{
	/* do nothing for fdd mode */
	if (rwnx_hw->coex.hw_fdd)
		return true;

	/* wait last coex setting end */
	flush_work(&rwnx_hw->coex.work);

	/* don't restrict ap setup when bt is disabled */
	if (!bes_power_ctrl_bt_enabled())
		goto stop_coex;

	/* don't restrict ap setup when the ap works on 5G */
	if (chandef->chan->band != NL80211_BAND_2GHZ)
		goto stop_coex;

	/* output warning information */
	bes_warn(BES_DBG_COEX, "don't support coex between bt and 2.4g ap\n");

	return false;

stop_coex:
	bes_coex_arb_set_coex_stop(rwnx_hw);

	return true;
}

void bes_coex_arb_set_wifi_state(struct rwnx_hw *rwnx_hw, u32 wifi_state, u32 action)
{
	/* Don't need to do anything if hardware is dual antennas  */
	if (rwnx_hw->coex.hw_fdd)
		return;

	/* set pending action and wifi state */
	spin_lock(&rwnx_hw->coex.lock);
	rwnx_hw->coex.wifi_coex_state = wifi_state;
	rwnx_hw->coex.pending_action |= action;
	spin_unlock(&rwnx_hw->coex.lock);

	/* trigger coex work */
	queue_work(rwnx_hw->rwnx_wq, &rwnx_hw->coex.work);
}

void bes_coex_arb_set_bt_state(struct rwnx_hw *rwnx_hw, u32 bt_state, u32 action)
{
	u32 pending_action = 0;
	bool bt_self_mgmd = !!CONFIG_BES_POWER_CTRL_BT_SELF_MANAGED;
	u32 old_state;

	/*
	 * Don't need to do anything if hardware is dual antennas
	 * or coex work is not ready
	 */
	if ((rwnx_hw->coex.inited != BES_COEX_INIT_MAGIC) ||
	    rwnx_hw->coex.hw_fdd)
		return;

	/* set pending action and bt state */
	spin_lock(&rwnx_hw->coex.lock);

	/* update bt state */
	old_state = rwnx_hw->coex.bt_coex_state;
	rwnx_hw->coex.bt_coex_state = bt_state;

	/* switch rf calibration based on bt audio state */
	if (bes_coex_bt_audio_state_change(old_state, bt_state))
		action |= BIT(BES_ARB_OPER_RF_CALI);

	/* switch coex schedule based on wifi&bt state */
	if (bt_self_mgmd && bes_coex_bt_state_change(old_state, bt_state)) {
		if (!bes_coex_bt_closed(rwnx_hw)) {
			if (bes_coex_bt_check_wifi_idle(rwnx_hw))
				action |= BIT(BES_ARB_OPER_COEX_STOP);
			else
				action |= BIT(BES_ARB_OPER_COEX_START);
		} else {
			action |= BIT(BES_ARB_OPER_COEX_STOP);
		}
	}

	/* update pending action */
	rwnx_hw->coex.pending_action |= action;
	pending_action = rwnx_hw->coex.pending_action;
	spin_unlock(&rwnx_hw->coex.lock);

	/* trigger coex work */
	if (pending_action)
		queue_work(rwnx_hw->rwnx_wq, &rwnx_hw->coex.work);
}

void bes_coex_arb_set_pta_flags(struct rwnx_hw *rwnx_hw, u32 flag, bool set)
{
	/* Don't need to do anything if hardware is dual antennas  */
	if (rwnx_hw->coex.hw_fdd)
		return;

	/* set pending action and pta switch flags */
	spin_lock(&rwnx_hw->coex.lock);
	if (set)
		rwnx_hw->coex.pta_switch_flags = flag;
	else
		rwnx_hw->coex.pta_switch_flags &= ~flag;

	rwnx_hw->coex.pending_action |= BIT(BES_ARB_OPER_PTA_FLAGS);
	spin_unlock(&rwnx_hw->coex.lock);

	/* trigger coex work */
	queue_work(rwnx_hw->rwnx_wq, &rwnx_hw->coex.work);
}
