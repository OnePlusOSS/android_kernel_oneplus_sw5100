/*
 * @file bes_dbac_cali.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/crc32.h>
#include "rwnx_msg_tx.h"
#include "bes_sysfs.h"
#include "bes_dbac_cali.h"
#include "rwnx_main.h"
#include "bes_coex.h"

#define BES_DBAC_UNUSED_CHANNEL (0)

enum bes_cali_type {
	// Ordinary calibration, Its lifecycle is automatically managed.
	BES_ORD_CALI = 0,
	// Pre-calibration, The user management of its lifecycle.
	BES_PRE_CALI,
	// keep last
	BES_CALI_TYPE_MAX,
};

#ifdef PLAT_LAPTOP
static int save_flag_to_file(const char* save_file_name, u8 * flag, int size, loff_t pos);
static int read_flag_from_file(const char* save_file_name, u8 *flag, int size, loff_t pos);
#endif

/* get cfg80211_chan_def from mac_chan_op and ieee80211_channel */
static int bes_dbac_chan_to_chandef(struct mac_chan_op *mac_chan, struct ieee80211_channel *channel,
				    struct cfg80211_chan_def *chan_def)
{
	if (!mac_chan || !channel || !chan_def) {
		bes_err(BES_DBG_CALI, "%s mac_chan:%p channel:%p chan_def:%p\n",
			__func__, mac_chan, channel, chan_def);
		return -EINVAL;
	}

	memset(chan_def, 0, sizeof(struct cfg80211_chan_def));

	chan_def->chan = channel;
	chan_def->width = chnl2bw[mac_chan->type];
	chan_def->center_freq1 = mac_chan->center1_freq;
	chan_def->center_freq2 = mac_chan->center2_freq;

	if (!cfg80211_chandef_valid(chan_def))
		return -EINVAL;

	return 0;
}

static enum mac_chan_bandwidth bes_dbac_get_support_bw(struct rwnx_hw *rwnx_hw)
{
	struct rwnx_mod_params *mod_params = rwnx_hw->mod_params;
	enum mac_chan_bandwidth bw = PHY_CHNL_BW_20;

	if (mod_params) {
		if (mod_params->use_2040)
			bw = PHY_CHNL_BW_40;
		if (mod_params->use_80)
			bw = PHY_CHNL_BW_80;
	}
	return bw;
}

static void bes_dbac_vht_bw_parse(uint8_t vht_width, uint8_t center1_ch, uint8_t center2_ch,
				  enum mac_chan_bandwidth support_bw, struct mac_chan_op *chan)
{
	u16 delta = 0;

	if ((vht_width >= 4) || !chan) {
		bes_err(BES_DBG_CALI, "%s param invalid vht_width:%d chan:%p\n",
			__func__, vht_width, chan);
		return;
	}

	chan->center1_freq = ieee80211_channel_to_frequency(center1_ch, chan->band);
	chan->center2_freq = ieee80211_channel_to_frequency(center2_ch, chan->band);

	switch (vht_width)
	{
		case 1:
			// 80MHz, 160MHz or 80+80MHz channel
			if (chan->center2_freq)
				delta = (u16)abs(chan->center1_freq - chan->center2_freq);

			if ((delta == 40) && (support_bw >= PHY_CHNL_BW_160)) {
				chan->type = PHY_CHNL_BW_160;
				chan->center1_freq = chan->center2_freq;
				chan->center2_freq = 0;
			} else if ((delta > 40) && (support_bw == PHY_CHNL_BW_80P80)) {
				chan->type = PHY_CHNL_BW_80P80;
			} else {
				chan->type = PHY_CHNL_BW_80;
				chan->center2_freq = 0;
			}
			break;
		case 2:
			// 160MHZ channel
			// center0: center of the 160MHz channel
			// center1: 0
			if (support_bw >= PHY_CHNL_BW_160) {
				chan->type = PHY_CHNL_BW_160;
			} else {
				// 160MHz not supported, only use the 80MHz that contains the primary channel
				chan->type = PHY_CHNL_BW_80;
				if (chan->center1_freq < chan->prim20_freq)
					chan->center1_freq += 40;
				else
					chan->center1_freq -= 40;
			}
			break;
		case 3:
			// 80+80Mhz channel
			// center0: center of the primary 80MHz channel
			// center1: center of the secondary 80MHz channel
			if (support_bw == PHY_CHNL_BW_80P80) {
				chan->type = PHY_CHNL_BW_80P80;
			} else {
				// 80+80MHZ not supported, only used priumary 80MHz channel
				chan->type = PHY_CHNL_BW_80;
				chan->center2_freq = 0;
			}
			break;
		default:
			break;
	}
}

static void bes_dbac_ie2_chan(struct rwnx_hw *rwnx_hw, const u8 *ht_oper,
			      const u8 *vht_oper, struct mac_chan_op *mac_chan)
{
	struct ieee80211_ht_operation *ieee_ht_oper = NULL;
	struct ieee80211_vht_operation *ieee_vht_oper = NULL;
	enum mac_chan_bandwidth support_bw = PHY_CHNL_BW_20;
	u8 ht_second_chan_offet = 0;

	if (!mac_chan) {
		bes_err(BES_DBG_CALI, "%s param invalid mac_chan:%p\n",
			__func__, mac_chan);
		return;
	}

	support_bw = bes_dbac_get_support_bw(rwnx_hw);
	ieee_ht_oper = ht_oper ? (struct ieee80211_ht_operation *)(ht_oper + 2) : NULL;
	ieee_vht_oper = vht_oper ? (struct ieee80211_vht_operation *)(vht_oper + 2) : NULL;

	if (ieee_ht_oper && support_bw >= PHY_CHNL_BW_40) {
		ht_second_chan_offet = ieee_ht_oper->ht_param & IEEE80211_HT_PARAM_CHA_SEC_OFFSET;
		bes_dbg(BES_DBG_CALI, "%s ht secondary channel offset :%d\n",
			__func__, ht_second_chan_offet);

		if (ht_second_chan_offet) {
			mac_chan->center1_freq += (ht_second_chan_offet == 1 ? 10 : -10);
			mac_chan->type = PHY_CHNL_BW_40;
		}
	}

	if (ieee_vht_oper && support_bw>= PHY_CHNL_BW_80) {
		if (ieee_vht_oper->chan_width == 0 || ieee_vht_oper->center_freq_seg0_idx == 0)
			return;

		bes_dbg(BES_DBG_CALI, "%s vht width:%d, center1_ch:%d center2_ch:%d\n",
			__func__, ieee_vht_oper->chan_width, ieee_vht_oper->center_freq_seg0_idx,
			ieee_vht_oper->center_freq_seg1_idx);

		bes_dbac_vht_bw_parse(ieee_vht_oper->chan_width, ieee_vht_oper->center_freq_seg0_idx,
				      ieee_vht_oper->center_freq_seg1_idx, support_bw, mac_chan);
	}
}

/* Send a request to cancel ROC and wait for its end. */
static int bes_dbac_cali_wait_roc_end(struct rwnx_hw *rwnx_hw, struct rwnx_vif *vif)
{
	if (rwnx_hw->roc == NULL) {
		WARN(1, "ROC time for previous calibration is too short\n");
	} else {
		rwnx_send_cancel_roc(rwnx_hw, vif);

		if (wait_event_interruptible_timeout(rwnx_hw->roc_wait_wq,
						     (rwnx_hw->roc == NULL),
						     msecs_to_jiffies(400)) <= 0) {
			WARN(1, "%s timeout\n", __func__);
			return -EFAULT;
		}
	}

	return 0;
}

/* Send ROC request and wait for it to be ready. */
static int bes_dbac_trigger_roc(struct rwnx_vif *rwnx_vif,
				struct ieee80211_channel *chan, u32_l duration)
{
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	ktime_t start_time, end_time;
	bool roc_ready = false;
	int error = 0;

	/* Check if the chan is valid */
	if (!chan) {
		bes_err(BES_DBG_CALI, "%s chan is NULL\n", __func__);
		return -EINVAL;
	}

	/* record start roc start time */
	start_time = ktime_get();

	/* trigger roc operation */
	error = rwnx_cfg80211_remain_on_channel(rwnx_hw->wiphy, &rwnx_vif->wdev,
						chan, duration, NULL);
	if (error)
		return error;

	spin_lock_bh(&rwnx_hw->roc_lock);
	if (!rwnx_hw->roc) {
		spin_unlock_bh(&rwnx_hw->roc_lock);
		WARN(1, "%s roc fail\n", __func__);
		return -EFAULT;
	}
	rwnx_hw->roc->internal = true;
	spin_unlock_bh(&rwnx_hw->roc_lock);

	/* Waiting for ROC operation to be ready */
	if (wait_event_interruptible_timeout(rwnx_hw->roc_wait_wq,
		   ({
			spin_lock_bh(&rwnx_hw->roc_lock);
			roc_ready = (!!rwnx_hw->roc && rwnx_hw->roc->on_chan);
			spin_unlock_bh(&rwnx_hw->roc_lock);
			roc_ready;
		   }),
		   msecs_to_jiffies(400)) <= 0) {
		WARN(1, "%s wait roc ready(on_channel) timeout\n", __func__);
		return -EFAULT;
	}

	/* record roc end time and calculate consumed time */
	end_time = ktime_get();
	bes_dbg(BES_DBG_CALI, "wait roc ready(on_channel:%d) takes:%lld ms\n",
		ieee80211_frequency_to_channel(rwnx_hw->roc->chan->center_freq),
		ktime_to_ms(ktime_sub(end_time, start_time)));

	return 0;
}

static struct bes_cali_result *channel_to_cali_res(struct bes_rf_cali *rf_cali, u8 channel)
{
	struct bes_cali_result *cali_res;
	u8 i;

	for (i = 0; i < MAX_COEXIST_CHAN_CTXT; i++) {
		cali_res = &rf_cali->cali_res[i];

		if (cali_res->count && cali_res->channel == channel)
			return cali_res;
	}

	return NULL;
}

/* Delete a calibration result. */
static void bes_dbac_del_cali_res(struct rwnx_hw *rwnx_hw, struct bes_cali_result *cali_res)
{
	/* Subtract the reference count. */
	if (cali_res->count > 1) {
		cali_res->count--;
		return;
	}

	/* Set the calibration result that is no longer needed to be invalid. */
	bes_info(BES_DBG_CALI, "calibration(channel-%d) will be set to invalid\n",
		 cali_res->channel);
	bes_send_dbac_invalid_cali_data(rwnx_hw, cali_res->channel);
	cali_res->channel = BES_DBAC_UNUSED_CHANNEL;
	cali_res->type = BES_CALI_TYPE_MAX;
	cali_res->count = 0;
	rwnx_hw->rf_cali.cali_res_cnt--;
}

/**
 * bes_dbac_cali_required() - Check whether the new calibration can be skipped
 * and invalidate the calibration results that are no longer in use.
 * @rwnx_vif: The interface to be calibrated
 * @new_chandef: Channels to be compared
 * @pre_cali: Is it the inspection before pre-calibration?
 * Returns true when calibration is required.
*/
static bool bes_dbac_cali_required(struct rwnx_vif *rwnx_vif,
				   const struct cfg80211_chan_def *new_chandef,
				   bool pre_cali)
{
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	struct bes_rf_cali *rf_cali = &rwnx_hw->rf_cali;
	struct bes_cali_result *cali_res;
	const struct cfg80211_chan_def *temp_chandef = NULL;
	u8 new_ch = ieee80211_frequency_to_channel(new_chandef->center_freq1);
	struct rwnx_chanctx *ctx;
	u8 i, channel;

	/* Invalidate the old calibration results. */
	if (rwnx_vif->ch_index < NX_CHAN_CTXT_CNT) {
		ctx = &rwnx_hw->chanctx_table[rwnx_vif->ch_index];
		channel = (u8)ieee80211_frequency_to_channel(ctx->chan_def.center_freq1);
		cali_res = channel_to_cali_res(rf_cali, channel);

		if (cali_res && cali_res->count == 1 &&
		    cali_res->type == BES_ORD_CALI &&
		    cali_res->channel != new_ch)
			bes_dbac_del_cali_res(rwnx_hw, cali_res);
	}

	/* If there are the same calibration results, skip this calibration. */
	for (i = 0; i < MAX_COEXIST_CHAN_CTXT; i++) {
		cali_res = &rf_cali->cali_res[i];
		if (!cali_res->count ||
		    cali_res->channel != new_ch)
			continue;

		if (pre_cali)
			cali_res->type = BES_PRE_CALI;

		/* Update the reference count. */
		cali_res->count++;
		return false;
	}

	if (pre_cali)
		return true;

	/* Check if the new channel requires calibration. */
	for (i = 0; i < NX_CHAN_CTXT_CNT; i++) {
		ctx = &rwnx_hw->chanctx_table[i];
		if (!ctx->count || !cfg80211_chandef_valid(&ctx->chan_def))
			continue;

		/* When roaming, a new channel context will be created. */
		if (netif_running(rwnx_vif->ndev) &&
		    netif_carrier_ok(rwnx_vif->ndev) &&
		    rwnx_vif->ch_index == i)
			continue;

		/* Check compatibility. */
		temp_chandef = cfg80211_chandef_compatible(&ctx->chan_def, new_chandef);
		if (!temp_chandef)
			continue;

		/* Try to find the corresponding calibration result. */
		channel = (u8)ieee80211_frequency_to_channel(ctx->chan_def.center_freq1);
		cali_res = channel_to_cali_res(rf_cali, channel);
		if (!cali_res || cali_res->type != BES_ORD_CALI)
			continue;

		/* Update the reference count and interface reference. */
		if (temp_chandef == &ctx->chan_def) {
			cali_res->count++;
			return false;
		} else if (temp_chandef == new_chandef) {
			/* When the new connection bandwidth is larger,
			 * LMAC will also expand the bandwidth.
			 * Even if the (status_code != 0) in connected_ind. */
			bes_info(BES_DBG_CALI, "calibration(channel-%d) will be set to invalid\n",
				 cali_res->channel);
			bes_send_dbac_invalid_cali_data(rwnx_hw, cali_res->channel);
			cali_res->channel = new_ch;
			cali_res->count++;
			ctx->chan_def = *new_chandef;
			return true;
		}
	}

	return true;
}

static bool
bes_dbac_cali_result_record(struct rwnx_vif *rwnx_vif, u8 channel, bool pre_cali)
{
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	struct bes_rf_cali *rf_cali = &rwnx_hw->rf_cali;
	struct bes_cali_result *cali_res;
	u8 idx = 0;
	bool filled = false;

	/* When there are the same channel, bandwidth expansion has occurred.
	 * The calibration result information has been updated. */
	if (channel_to_cali_res(rf_cali, channel)) {
		filled = true;
	} else {
		/* Find a space to record the calibration channel. */
		for (idx = 0; idx < MAX_COEXIST_CHAN_CTXT; idx++) {
			cali_res = &rf_cali->cali_res[idx];
			if (cali_res->count)
				continue;

			/* Record channel */
			cali_res->count = 1;
			cali_res->channel = channel;
			cali_res->type = pre_cali ? BES_PRE_CALI : BES_ORD_CALI;
			rf_cali->cali_res_cnt++;
			filled = true;
			break;
		}
	}

	/* Validity Check */
	WARN(!filled, "%s Calibration results are overwritten.\n", __func__);

	return filled;
}

static void bes_cali_working_ch_get(struct rwnx_hw *rwnx_hw, u8 *ctxt_chan)
{
	struct cfg80211_chan_def *chan_def;
	u8 i, j;

	for (i = 0; i < MAX_COEXIST_CHAN_CTXT; i++)
		ctxt_chan[i] = BES_DBAC_UNUSED_CHANNEL;

	for ((j = 0, i = 0);
	     (j < NX_CHAN_CTXT_CNT && i < MAX_COEXIST_CHAN_CTXT); j++) {
		if (!rwnx_hw->chanctx_table[j].count)
			continue;

		chan_def = &rwnx_hw->chanctx_table[j].chan_def;
		if (!cfg80211_chandef_valid(chan_def))
			continue;

		ctxt_chan[i++] = (u8)ieee80211_frequency_to_channel(chan_def->center_freq1);
	}
}

static void bes_cali_fallback(struct rwnx_hw *rwnx_hw)
{
	struct bes_rf_cali *rf_cali = &rwnx_hw->rf_cali;
	struct bes_cali_result *cali_res;
	u8 ctxt_chan[MAX_COEXIST_CHAN_CTXT];
	u8 cali_idx, ctxt_idx;
	bool found;

	bes_warn(BES_DBG_CALI, ">>%s Two calibration results already exist"
		 "and we have to delete one of them.\n", __func__);

	/* According to the channel context in the work,
	 * find the calibration result we currently need. */
	bes_cali_working_ch_get(rwnx_hw, ctxt_chan);

	/* Compare calibration records with channels(in use). */
	for (cali_idx = 0; cali_idx < MAX_COEXIST_CHAN_CTXT; cali_idx++) {
		cali_res = &rf_cali->cali_res[cali_idx];
		if ((cali_res->count != 1) ||
		    (cali_res->channel == BES_DBAC_UNUSED_CHANNEL))
			continue;

		/* Check if there is a corresponding channel context for this calibration result. */
		found = false;
		for (ctxt_idx = 0; ctxt_idx < MAX_COEXIST_CHAN_CTXT; ctxt_idx++) {
			if (ctxt_chan[ctxt_idx] == BES_DBAC_UNUSED_CHANNEL)
				continue;

			if (cali_res->channel == ctxt_chan[ctxt_idx]) {
				found = true;
				break;
			}
		}

		if (found)
			continue;

		/* Set a calibration result that is not in use as invalid. */
		bes_dbac_del_cali_res(rwnx_hw, cali_res);
	}
}

int bes_dbac_do_cali(struct rwnx_vif *rwnx_vif, const struct cfg80211_chan_def *chan_def,
		     bool pre_cali)
{
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	struct bes_rf_cali *rf_cali = &rwnx_hw->rf_cali;
	struct mcu_wf_cali_req cali_req;
	struct mcu_wf_cali_cfm cfm;
	ktime_t start_time, end_time;
	u8 cali_ch = 0;
	int error = 0;

	/* When there are already two calibration results,
 	 * one pre-calibrated result needs to be set invalid before the new calibration. */
	if (rf_cali->cali_res_cnt >= MAX_COEXIST_CHAN_CTXT)
		bes_cali_fallback(rwnx_hw);

	/* The time range for calibration in [0, CONFIG_CALI_TIME] milliseconds */
	error = bes_dbac_trigger_roc(rwnx_vif, chan_def->chan, CONFIG_CALI_TIME);
	if (error) {
		bes_err(BES_DBG_CALI, "start ROC err:%d\n", error);
		return error;
	}

	/* Set wifi calibration parameters */
	memset(&cali_req, 0, sizeof(cali_req));
	cali_req.step = BES_MCU_WIFI_CALI_TX;
	cali_req.freq = chan_def->center_freq1;
	cali_req.band = bw2chnl[chan_def->width];
	cali_ch = (u8)ieee80211_frequency_to_channel(chan_def->center_freq1);

	/* Record start time */
	start_time = ktime_get();

	/* Send wifi calibration command to device */
	memset(&cfm, 0, sizeof(struct mcu_wf_cali_cfm));
	error = bes_send_wf_cali_req(rwnx_hw, &cali_req, &cfm);
	if (error || cfm.status) {
		bes_err(BES_DBG_CALI, "%s fail:%d status:%d\n", __func__, error, cfm.status);
		goto end;
	}

	/* Store calibrated channel information. */
	bes_dbac_cali_result_record(rwnx_vif, cali_ch, pre_cali);

	/* Record end time and calculate consumed time */
	end_time = ktime_get();
	bes_info(BES_DBG_CALI, "%s (channel-%d)use:%lld ms\n",
		 __func__, cali_ch, ktime_to_ms(ktime_sub(end_time, start_time)));

end:
	/* Cancel ROC */
	bes_dbac_cali_wait_roc_end(rwnx_hw, rwnx_vif);
	return error;
}

static int bes_dbac_cali(struct rwnx_vif *rwnx_vif, const struct cfg80211_chan_def *chan_def,
			 bool pre_cali)
{
	struct rwnx_hw *rwnx_hw;
	struct bes_rf_cali *rf_cali;
	int error = 0;
	u8 vif_index;

	if (rwnx_vif == NULL) {
		bes_err(BES_DBG_CALI, "%s err rwnx_vif is NULL\n", __func__);
		return -EINVAL;
	}

	rwnx_hw = rwnx_vif->rwnx_hw;
	if (rwnx_hw == NULL) {
		bes_err(BES_DBG_CALI, "%s err rwnx_hw is NULL\n", __func__);
		return -EINVAL;
	}

	rf_cali = &rwnx_hw->rf_cali;
	vif_index = rwnx_vif->vif_index;

	if (vif_index >= NX_ITF_MAX) {
		return -EINVAL;
	}

	/* Check whether chan_def is NULL */
	if (!chan_def) {
		bes_err(BES_DBG_CALI, "%s err param: chandef is NULL\n", __func__);
		return -EINVAL;
	}

	/* Check whether chan_def->chan is NULL */
	if (!chan_def->chan) {
		bes_err(BES_DBG_CALI, "%s err param: chandef->chan is NULL\n", __func__);
		return -EINVAL;
	}

	bes_dbg(BES_DBG_CALI, ">>>%s freq:%d center1_freq:%d cneter2_freq:%d bw:%d\n",
		__func__, chan_def->chan->center_freq, chan_def->center_freq1,
		chan_def->center_freq2, bw2chnl[chan_def->width]);

	/* Determine if this calibration can be skipped. */
	if(!bes_dbac_cali_required(rwnx_vif, chan_def, pre_cali)) {
		bes_info(BES_DBG_CALI, "skipping this calibration\n");
		return 0;
	}

	/* Mark the vif which calibration is defered. */
	if ((bes_coex_bt_get_audio_state(rwnx_hw) != BES_BT_AUDIO_NONE)) {
		rf_cali->defer_cali_vif_map |= BIT(vif_index);
		bes_info(BES_DBG_CALI, "cali of freq(%d) on vif(%s) defered\n",
				 chan_def->center_freq1, rwnx_vif->ndev->name);
		return 0;
	}

	/* Do calibration operaton */
	error = bes_dbac_do_cali(rwnx_vif, chan_def, pre_cali);
	return error;
}

int bes_dbac_cali_connect(struct rwnx_vif *rwnx_vif, struct cfg80211_connect_params *sme)
{
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	struct cfg80211_chan_def chan_def;
	struct mac_chan_op mac_chan;
	struct cfg80211_bss *bss = NULL;
	const u8 *ht_oper = NULL;
	const u8 *vht_oper = NULL;
	int error = 0;

	if (!sme->channel) {
		bes_err(BES_DBG_CALI, "%s param invliad\n", __func__);
		return -EINVAL;
	}

	memset(&mac_chan, 0, sizeof(struct mac_chan_op));
	mac_chan.band = sme->channel->band;
	mac_chan.prim20_freq = sme->channel->center_freq;
	mac_chan.type = PHY_CHNL_BW_20;
	mac_chan.center1_freq = sme->channel->center_freq;
	mac_chan.center2_freq = 0;

	bss = cfg80211_get_bss(rwnx_hw->wiphy, NULL, sme->bssid, sme->ssid, sme->ssid_len,
			       IEEE80211_BSS_TYPE_ANY, IEEE80211_PRIVACY_ANY);
	if (!bss) {
		bes_err(BES_DBG_CALI, "%s can't get bss\n", __func__);
		return -EINVAL;
	}

	/* Obtain channel information through BSS. */
	rcu_read_lock();
	ht_oper = ieee80211_bss_get_ie(bss, WLAN_EID_HT_OPERATION);
	vht_oper = ieee80211_bss_get_ie(bss, WLAN_EID_VHT_OPERATION);
	bes_dbac_ie2_chan(rwnx_hw, ht_oper, vht_oper, &mac_chan);
	rcu_read_unlock();

	cfg80211_put_bss(rwnx_hw->wiphy, bss);

	error = bes_dbac_chan_to_chandef(&mac_chan, sme->channel, &chan_def);
	if (error) {
		bes_err(BES_DBG_CALI, "%s chandef invalid or create err:%d\n",
			__func__, error);
		return error;
	}

	error = bes_dbac_cali(rwnx_vif, &chan_def, false);
	if (error) {
		bes_err(BES_DBG_CALI, "bes_dbac_cali fail err:%d\n", error);
		return error;
	}
	return 0;
}

/**
 * bes_dbac_cali_start_ap() - Calibrate before turning on the AP
 * The actual bandwidth is currently obtained from the information in the beacon frame
 *
 * There is a @chandef in the @cfg80211_ap_settings,
 * but the actual calibration uses the @ie_chandef obtained from the IE information.
 * This is because RK3566's Android source code does not support P2P GO's HE support perfectly,
 * @cfg80211_ap_settings can only be 40MHz when HE is turned on, but it is actually 80MHz.
*/
int bes_dbac_cali_start_ap(struct rwnx_vif *rwnx_vif, struct cfg80211_ap_settings *settings)
{
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	const u8 *ht_oper = NULL;
	const u8 *vht_oper = NULL;
	struct mac_chan_op mac_chan;
	struct cfg80211_chan_def ie_chandef;
	int error = 0;

	if (!settings) {
		bes_err(BES_DBG_CALI, "%s err param: settings is NULL\n", __func__);
		return -EINVAL;
	}

	/* Check whether settings->chandef.chan is NULL */
	if (!settings->chandef.chan) {
		bes_err(BES_DBG_CALI, "%s err param: settings->chandef.chan is NULL\n", __func__);
		return -EINVAL;
	}

	ht_oper = cfg80211_find_ie(WLAN_EID_HT_OPERATION, settings->beacon.tail,
				   settings->beacon.tail_len);
	vht_oper = cfg80211_find_ie(WLAN_EID_VHT_OPERATION, settings->beacon.tail,
				    settings->beacon.tail_len);

	memset(&mac_chan, 0, sizeof(struct mac_chan_op));
	mac_chan.type = PHY_CHNL_BW_20;
	mac_chan.band = settings->chandef.chan->band;
	mac_chan.prim20_freq = settings->chandef.chan->center_freq;
	mac_chan.center1_freq = settings->chandef.chan->center_freq;
	mac_chan.center2_freq = 0;

	bes_dbac_ie2_chan(rwnx_hw, ht_oper, vht_oper, &mac_chan);
	bes_dbg(BES_DBG_CALI, "%s freq:%d band:%d center1:%d cneter2:%d\n",
		__func__, mac_chan.prim20_freq, mac_chan.type, mac_chan.center1_freq,
		mac_chan.center2_freq);

	error = bes_dbac_chan_to_chandef(&mac_chan, settings->chandef.chan, &ie_chandef);
	if (error) {
		bes_err(BES_DBG_CALI, "%s chandef invalid or create err:%d\n",
			__func__, error);
		return error;
	}

	error = bes_dbac_cali(rwnx_vif, &ie_chandef, false);
	if (error) {
		bes_err(BES_DBG_CALI, "%s fail err:%d\n", __func__, error);
		return error;
	}

	settings->chandef = ie_chandef;
	rwnx_vif->vif_chandef = ie_chandef;

	return 0;
}

/**
 * bes_dbac_temp_compensation_start() - Start temperature compensation
*/
void bes_dbac_temp_compensation_start(struct rwnx_hw *rwnx_hw)
{
	queue_work(rwnx_hw->rwnx_wq, &(rwnx_hw->rf_cali.temp_comp_work));
	bes_info(BES_DBG_CALI, ">>> %s\n", __func__);
}

/**
 * bes_get_next_temp_cali_chan_ctxt() - Obtain the channel context for
 * temperature calibration
 *
 * Non-reentrant functions
*/
static struct rwnx_chanctx *bes_get_next_temp_cali_chan_ctxt(struct rwnx_hw *rwnx_hw)
{
	struct bes_rf_cali *rf_cali = &rwnx_hw->rf_cali;
	struct rwnx_chanctx *ctxt = NULL, *tmp_ctxt = NULL;
	u8 ch_ctxt_idx = rf_cali->cur_cali_ch_ctxt_idx;
	u8 i = 0, j = 0;

	/* Return NULL when not available */
	if (atomic_read(&rwnx_hw->chanctx_count) == 0)
		return NULL;

	/* get using channel context at first temperature calibration request */
	if (ch_ctxt_idx == 0) {
		for (i = 0; i < MAX_COEXIST_CHAN_CTXT; i++)
			rf_cali->using_chan_ctxt[i] = NULL;

		for (i = 0, j = 0; i < NX_CHAN_CTXT_CNT; i++) {
			tmp_ctxt = &rwnx_hw->chanctx_table[i];
			if (rwnx_hw->chanctx_table[i].count > 0) {
				rf_cali->using_chan_ctxt[j++] = tmp_ctxt;
				if (j >= MAX_COEXIST_CHAN_CTXT)
					break;
			}
		}
	}

	/* Check if the current channel need to calibrate */
	ctxt = rf_cali->using_chan_ctxt[ch_ctxt_idx];
	if (ch_ctxt_idx > 0 && ctxt) {
		if ((ctxt->count == 0) ||
		    (ctxt->chan_def.center_freq1 ==
		     rf_cali->using_chan_ctxt[0]->chan_def.center_freq1)) {
			ctxt = NULL;
		}
	}

	/* increase cur_cali_ch_ctxt_idx */
	ch_ctxt_idx++;
	ch_ctxt_idx = ch_ctxt_idx % MAX_COEXIST_CHAN_CTXT;
	rf_cali->cur_cali_ch_ctxt_idx = ch_ctxt_idx;

	return ctxt;
}

static void bes_temp_compensation_work(struct work_struct *work)
{
	struct rwnx_hw *rwnx_hw = container_of(work, struct rwnx_hw, rf_cali.temp_comp_work);
	struct rwnx_chanctx *ctxt = NULL;
	struct rwnx_vif *vif = NULL, *roc_vif = NULL;
	u8 cali_chan = 0;
	int error = 0;

	/* Temperature compensation calibration requires stopping scanning */
	if (test_bit(RWNX_DEV_SCANNING, &rwnx_hw->flags)) {
		error = rwnx_send_scan_cancel_req(rwnx_hw, NULL);
		if (error) {
			bes_err(BES_DBG_CALI, "%s cancel_scan fail\n", __func__);
			return;
		}
	}

	/* wait other mutex operation done */
	if (down_timeout(&rwnx_hw->phy_op_sema, msecs_to_jiffies(PHY_OP_WAIT_TIMEOUT_IN_MS))) {
		bes_err(BES_DBG_CALI, "wait mutex operation timeout when temp_comp\n");
		return;
	}

	/* get next calibration channel */
	ctxt = bes_get_next_temp_cali_chan_ctxt(rwnx_hw);
	if (!ctxt) {
		cali_chan = 0;
	} else {
		cali_chan = ieee80211_frequency_to_channel(ctxt->chan_def.center_freq1);

		list_for_each_entry(vif, &rwnx_hw->vifs, list) {
			if (vif->ch_index < NX_CHAN_CTXT_CNT &&
			    ctxt == &rwnx_hw->chanctx_table[vif->ch_index]) {
				roc_vif = vif;
				break;
			}
		}
		BUG_ON(!roc_vif);

		/* The time range for calibration in [0, CONFIG_CALI_TIME] milliseconds */
		if ((error = bes_dbac_trigger_roc(roc_vif, ctxt->chan_def.chan, CONFIG_CALI_TIME))) {
			/* Skip calibration after roc failure. */
			bes_err(BES_DBG_CALI, "%s roc fail:%d skip calibration\n", __func__, error);
			cali_chan = 0;
			ctxt = NULL;
		}
	}

	bes_info(BES_DBG_CALI, "%s ch:%d\n", __func__, cali_chan);

	/* send temperature calibration request to device */
	bes_send_dbac_temp_compensation_req(rwnx_hw, cali_chan);

	/* Cancel ROC */
	if (ctxt)
		bes_dbac_cali_wait_roc_end(rwnx_hw, roc_vif);

	/* notify other modules that temperature calibration done */
	up(&rwnx_hw->phy_op_sema);
}

static int bes_dbac_ctrl_dyna_cali(struct rwnx_hw *rwnx_hw, bool enable)
{
	struct mcu_wf_cali_req cali_req;
	struct mcu_wf_cali_cfm cfm;
	int err = 0;

	memset(&cali_req, 0, sizeof(cali_req));
	if (enable) {
		cali_req.step = BES_MCU_WIFI_CALI_DYNA;
	} else {
		cali_req.step = BES_MCU_WIFI_CALI_NONE;
	}

	memset(&cfm, 0, sizeof(struct mcu_wf_cali_cfm));
	err = bes_send_wf_cali_req(rwnx_hw, &cali_req, &cfm);
	if (err || cfm.status)
		bes_err(BES_DBG_CALI, "%s fail:%d status:%d\n", __func__, err, cfm.status);

	return err;
}

/**
 * bes_dbac_dyna_cali_check() - Check when the channel context changes
 *
 * When no channel context is in use, dynamic calibration is turned off
*/
void bes_dbac_dyna_cali_check(struct rwnx_hw *rwnx_hw)
{
	struct bes_rf_cali *rf_cali = &rwnx_hw->rf_cali;
	u8 ctx_num = atomic_read(&rwnx_hw->chanctx_count);

	/* When all connections are lost, turn off dynamic calibration */
	if ((bes_coex_bt_get_audio_state(rwnx_hw) != BES_BT_AUDIO_NONE) ||
		(ctx_num == 0 && rf_cali->dyna_cali_enabled)) {
		bes_dbac_ctrl_dyna_cali(rwnx_hw, false);
		rf_cali->dyna_cali_enabled = false;
	} else if (!rf_cali->dyna_cali_enabled) {
		bes_dbac_ctrl_dyna_cali(rwnx_hw, true);
		rf_cali->dyna_cali_enabled = true;
	}

	bes_dbg(BES_DBG_CALI, "%s ch_cnt:%d dyna_enable:%s\n",
		__func__, ctx_num, rf_cali->dyna_cali_enabled ? "true" : "false");
}

/**
 * If there is no corresponding channel context for a certain calibration result,
 * then invalidate it. This function may cause blocking.
 */
static void bes_dbac_inval_cali_check(struct rwnx_hw *rwnx_hw)
{
	struct bes_rf_cali *rf_cali = &rwnx_hw->rf_cali;
	struct bes_cali_result *cali_res;
	u8 ctxt_chan[MAX_COEXIST_CHAN_CTXT];
	u8 cali_idx, ctxt_idx;
	bool found;

	/* According to the channel context in the work,
	 * find the calibration result we currently need. */
	bes_cali_working_ch_get(rwnx_hw, ctxt_chan);

	/* Compare calibration records with channels(in use). */
	for (cali_idx = 0; cali_idx < MAX_COEXIST_CHAN_CTXT; cali_idx++) {
		cali_res = &rf_cali->cali_res[cali_idx];
		if (!cali_res->count ||
		    cali_res->channel == BES_DBAC_UNUSED_CHANNEL)
			continue;

		/* Check if there is a corresponding channel context for this calibration result. */
		found = false;
		for (ctxt_idx = 0; ctxt_idx < MAX_COEXIST_CHAN_CTXT; ctxt_idx++) {
			if (ctxt_chan[ctxt_idx] == BES_DBAC_UNUSED_CHANNEL)
				continue;

			if (cali_res->channel == ctxt_chan[ctxt_idx]) {
				found = true;
				break;
			}
		}

		if (found)
			continue;

		/* Pre-calibration is managed by the user and
		 * at least one reference count is reserved for the user.  */
		if (cali_res->type == BES_PRE_CALI) {
			if (cali_res->count != 1)
				cali_res->count--;

			continue;
		}

		/* The calibration result has not been used and is set as invalid. */
		cali_res->count = 0;
		bes_dbac_del_cali_res(rwnx_hw, cali_res);
	}
}

void bes_dbac_defer_cali_check(struct rwnx_hw *rwnx_hw)
{
	u8 i = 0;
	unsigned long chan_ctxt_bit_map = 0;
	unsigned long defer_cali_vif_map = 0;
	bool found;
	u8 ctxt_chan, cali_idx;
	struct bes_cali_result *cali_res = &rwnx_hw->rf_cali.cali_res[0];

	/* When bt is playing music, don't calibration */
	if ((bes_coex_bt_get_audio_state(rwnx_hw) != BES_BT_AUDIO_NONE))
		return;

	/* Clear defer vif bitmap */
	defer_cali_vif_map = rwnx_hw->rf_cali.defer_cali_vif_map;
	rwnx_hw->rf_cali.defer_cali_vif_map = 0;

	/* Get channel context bitmap which need to calibrate */
	for_each_set_bit(i, &defer_cali_vif_map, NX_ITF_MAX) {
		struct rwnx_vif *vif = rwnx_rx_get_vif(rwnx_hw, i);

		/* Check if the vif is aleady stopped */
		if (vif == NULL)
			continue;

		/* Vif needs to be connected */
		if (!netif_running(vif->ndev) ||
		    !netif_carrier_ok(vif->ndev))
			continue;

		/* Channel index must be valid when carrier ok */
		BUG_ON(vif->ch_index >= NX_CHAN_CTXT_CNT);

		/* mark which channel context need to calibrate */
		chan_ctxt_bit_map |= BIT(vif->ch_index);
	}

	/* Do defer calibration */
	for_each_set_bit(i, &chan_ctxt_bit_map, NX_CHAN_CTXT_CNT) {
		struct rwnx_chanctx *chan_ctxt = &rwnx_hw->chanctx_table[i];
		struct rwnx_vif *rwnx_vif = NULL;

		/* Skip calibration if there isn't vifs that refrence to it */
		if (!chan_ctxt->count || !cfg80211_chandef_valid(&chan_ctxt->chan_def))
			continue;

		/* Find a vif that bind to the chan_ctxt */
		spin_lock_bh(&rwnx_hw->cb_lock);
		list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
			/* vif needs to be connected */
			if (!netif_running(rwnx_vif->ndev) ||
			    !netif_carrier_ok(rwnx_vif->ndev))
				continue;

			if (rwnx_vif->ch_index == i)
				break;
		}
		spin_unlock_bh(&rwnx_hw->cb_lock);

		/* Found uncalibrated channel context. */
		found = false;
		ctxt_chan = (u8)ieee80211_frequency_to_channel(chan_ctxt->chan_def.center_freq1);
		for (cali_idx = 0; cali_idx < MAX_COEXIST_CHAN_CTXT; cali_idx++) {
			if (ctxt_chan == cali_res[cali_idx].channel) {
				found = true;
				break;
			}
		}

		if (found) {
			bes_info(BES_DBG_CALI, "Already do cali of freq(%d) on vif(%s)\n",
				 chan_ctxt->chan_def.center_freq1, rwnx_vif->ndev->name);
			continue;
		}

		/* Output debug information */
		bes_info(BES_DBG_CALI, "do defered cali of freq(%d) on vif(%s)\n",
				 chan_ctxt->chan_def.center_freq1, rwnx_vif->ndev->name);

		/* Do cali operation */
		bes_dbac_do_cali(rwnx_vif, &chan_ctxt->chan_def, false);
	}
}

/* Check after connecting, start_ap, disconnecting, and stop_ap.
 * This function may cause blocking. */
void bes_dbac_cali_check(struct rwnx_hw *rwnx_hw)
{
	/* Setting calibration results invalid when needed. */
	bes_dbac_inval_cali_check(rwnx_hw);

	/* Check if we can do defered calibration. */
	bes_dbac_defer_cali_check(rwnx_hw);

	/* Turn off dynamic calibration when needed. */
	bes_dbac_dyna_cali_check(rwnx_hw);
}

static bool check_channel_support(struct rwnx_hw *rwnx_hw,
				  const struct cfg80211_chan_def *chandef)
{
	if (chandef->width == NL80211_CHAN_WIDTH_40 &&
	    !rwnx_hw->mod_params->use_2040)
		return false;

	if (chandef->width == NL80211_CHAN_WIDTH_80 &&
	    !rwnx_hw->mod_params->use_80)
		return false;

	return true;
}

int bes_dbac_pre_cali(struct rwnx_vif *rwnx_vif, const struct cfg80211_chan_def *chandef)
{
	struct rwnx_hw *rwnx_hw;
	u8 vif_index;

	if (rwnx_vif == NULL) {
		bes_err(BES_DBG_CALI, "%s err rwnx_vif is NULL\n", __func__);
		return -EINVAL;
	}

	vif_index = rwnx_vif->vif_index;
	rwnx_hw = rwnx_vif->rwnx_hw;
	if (rwnx_hw == NULL) {
		bes_err(BES_DBG_CALI, "%s err rwnx_hw is NULL\n", __func__);
		return -EINVAL;
	}

	/* Check if the interface is active. */
	if (!rwnx_vif->up || vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_CALI, "%s Inactive interface idx:%d\n",
			__func__, vif_index);
		return -EINVAL;
	}

	/* Check the validity of channel parameters. */
	if (!cfg80211_chandef_valid(chandef)) {
		bes_err(BES_DBG_CALI, "%s invalid chandef\n", __func__);
		return -EINVAL;
	}

	/* Check whether we support this channel. */
	if (!check_channel_support(rwnx_hw, chandef)) {
		bes_err(BES_DBG_CALI, "%s channel not support.\n", __func__);
		return -EINVAL;
	}

	/* Do calibration operaton */
	return bes_dbac_cali(rwnx_vif, chandef, true);
}

int bes_dbac_pre_cali_invalid(struct rwnx_hw *rwnx_hw, u8 idx)
{
	struct bes_rf_cali *rf_cali = &rwnx_hw->rf_cali;
	struct bes_cali_result *cali_res;

	if (idx >= MAX_COEXIST_CHAN_CTXT) {
		bes_err(BES_DBG_CALI, "%s invalid idx:%d\n",
			__func__, idx);
		return -EINVAL;
	}

	cali_res = &(rf_cali->cali_res[idx]);
	if (!cali_res->count ||
	    cali_res->type != BES_PRE_CALI) {
		bes_err(BES_DBG_CALI,
			"%s The specified Pre-calibration result does not exist."
			"count:%d type:%d\n",
			__func__, cali_res->count, cali_res->type);
		return -EINVAL;
	}

	cali_res->count--;
	if (cali_res->count) {
		cali_res->type = BES_ORD_CALI;
	} else {
		bes_dbac_del_cali_res(rwnx_hw, cali_res);
	}

	return 0;
}

/**
 * Predict the channel to be used based on one's own capabilities and Beacon frame information.
 * @target_chandef: Predicted cfg80211_chan_def.
 * Return 0 on success.
*/
static int bes_dbac_csa_beacon_to_chandef(struct rwnx_hw *rwnx_hw, struct bes_dbac_sta_csa *sta_csa,
					  const void *frame, u32 frame_len, struct cfg80211_chan_def *target_chandef)
{
	const struct ieee80211_mgmt *mgmt = (const struct ieee80211_mgmt *)frame;
	struct ieee80211_channel *chan = NULL;
	u16 ie_len = frame_len;
	enum nl80211_band band = NL80211_BAND_2GHZ;
	struct cfg80211_chan_def ie_chandef;
	struct mac_chan_op mac_chan;
	const u8 *ds_param = NULL;
	const u8 *ht_oper = NULL;
	const u8 *vht_oper = NULL;
	u8 chan_id = 0;
	int freq = 0;

	/* search ie from beacon */
	ds_param = cfg80211_find_ie(WLAN_EID_DS_PARAMS, mgmt->u.beacon.variable, ie_len);
	ht_oper = cfg80211_find_ie(WLAN_EID_HT_OPERATION, mgmt->u.beacon.variable, ie_len);
	vht_oper = cfg80211_find_ie(WLAN_EID_VHT_OPERATION, mgmt->u.beacon.variable, ie_len);
	if (!ds_param && !ht_oper)
		return -EINVAL;

	/* get work channel */
	chan_id = ds_param ? ds_param[2] : ht_oper[2];

	/* set default mac_chan_op */
	memset(&mac_chan, 0, sizeof(struct mac_chan_op));
	band = (chan_id > 14) ? NL80211_BAND_5GHZ : NL80211_BAND_2GHZ;
	freq = ieee80211_channel_to_frequency(chan_id, band);
	mac_chan.band = band;
	mac_chan.type = PHY_CHNL_BW_20;
	mac_chan.prim20_freq = freq;
	mac_chan.center1_freq = freq;
	mac_chan.center2_freq = 0;

	/* get mac_chan_op from ht op ie and vht op ie */
	bes_dbac_ie2_chan(rwnx_hw, ht_oper, vht_oper, &mac_chan);
	bes_dbg(BES_DBG_CALI, "%s freq:%d bw:%d center1:%d cneter2:%d\n",
		__func__, mac_chan.prim20_freq, mac_chan.type, mac_chan.center1_freq,
		mac_chan.center2_freq);

	/* convert mac_chan_op to chandef */
	chan = ieee80211_get_channel(rwnx_hw->wiphy, freq);
	if (!chan || bes_dbac_chan_to_chandef(&mac_chan, chan, &ie_chandef))
		return -EINVAL;

	/* Pass the generated chandef. */
	*target_chandef = ie_chandef;

	return 0;
}

/*
 * Intercept CSA information in frames and update status.
 * After the CSA is completed, start the calibration work.
 */
static void bes_dbac_sta_csa_process(struct rwnx_vif *rwnx_vif, const struct ieee80211_mgmt *mgmt,
				     u32 mgmt_len, struct cfg80211_chan_def *rx_chandef)
{
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	struct bes_dbac_sta_csa *sta_csa = &rwnx_vif->sta.sta_csa;
	const u8 *csa_field = NULL;
	const u8 *ecsa_field = NULL;
	struct cfg80211_chan_def target_chandef;
	bool current_in_csa = false;
	bool update_ctxt = false;
	int ret = 0;

	/* search csa/ecsa from beacon */
	csa_field = cfg80211_find_ie(WLAN_EID_CHANNEL_SWITCH,
				     mgmt->u.beacon.variable, mgmt_len);
	ecsa_field = cfg80211_find_ie(WLAN_EID_EXT_CHANSWITCH_ANN,
				      mgmt->u.beacon.variable, mgmt_len);
	current_in_csa = (csa_field || ecsa_field);

	bes_dbg(BES_DBG_CALI, "%s sn:%d mgmt_len:%d current_in_csa:%d csa_cnt:%d rx_chandef:%p\n",
		__func__, IEEE80211_SEQ_TO_SN(mgmt->seq_ctrl), mgmt_len, (current_in_csa ? 1 : 0),
		(csa_field ? ((struct ieee80211_channel_sw_ie*)(csa_field + 2))->count :
		 (ecsa_field ? ((struct ieee80211_ext_chansw_ie*)(ecsa_field + 2))->count : -1)),
		rx_chandef);

	/* Update CSA status */
	if (current_in_csa && !sta_csa->in_csa)
		sta_csa->in_csa = true;
	else if (!current_in_csa && sta_csa->in_csa)
		sta_csa->in_csa = false;

	/* Determine whether the current CSA is complete. */
	spin_lock(&sta_csa->csa_lock);
	update_ctxt = (!sta_csa->in_csa && sta_csa->csa && sta_csa->chan_update_pending);
	spin_unlock(&sta_csa->csa_lock);
	if (!update_ctxt)
		return;

	/* Get channel information */
	ret = bes_dbac_csa_beacon_to_chandef(rwnx_hw, sta_csa, mgmt, mgmt_len, &target_chandef);
	if (ret) {
		/* Can assign the previous parsing results if no channel information is obtained from IEs */
		if (rx_chandef) {
			target_chandef = *rx_chandef;
		} else {
			bes_err(BES_DBG_CALI, "%s Fail to parse channel information", __func__);
			return;
		}
	}

	/* After the completion of CSA, update the channel context and calibration
	 * based on the new frame information. */
	spin_lock(&sta_csa->csa_lock);
	if (!sta_csa->in_csa && sta_csa->csa && sta_csa->chan_update_pending) {
		sta_csa->chan_update_pending = false;
		sta_csa->csa->chandef = target_chandef;
		spin_unlock(&sta_csa->csa_lock);

		/* Start updating channel context and calibration */
		queue_work(rwnx_hw->rwnx_wq, &sta_csa->csa->work);
	} else {
		spin_unlock(&sta_csa->csa_lock);
	}
}

/* Calibrate the specified channel context. */
static int bes_dbac_csa_recali(struct rwnx_vif *rwnx_vif, u8 ctxt_idx)
{
	struct mcu_wf_cali_req cali_req;
	struct mcu_wf_cali_cfm cfm;
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	struct rwnx_chanctx *ctxt = NULL;
	ktime_t start_time, end_time;
	u8 cali_ch = 0;
	int error = 0;

	/* Channel context to be calibrated. */
	ctxt = &rwnx_hw->chanctx_table[ctxt_idx];

	/* Abort csa calibration if vif is disconnected */
	if(!rwnx_vif_is_connected(rwnx_vif))
		return 0;

	/* Calibration requires stopping the scanning. */
	if (test_bit(RWNX_DEV_SCANNING, &rwnx_hw->flags)) {
		error = rwnx_send_scan_cancel_req(rwnx_hw, NULL);
		if (error) {
			bes_err(BES_DBG_CALI, "%s cancel_scan fail\n", __func__);
			return error;
		}
	}

	/* Ensure that scanning is not triggered during calibration. */
	if (down_timeout(&rwnx_hw->phy_op_sema, msecs_to_jiffies(PHY_OP_WAIT_TIMEOUT_IN_MS))) {
		WARN(1, "%s wait mutex operation timeout\n", __func__);
		return -EFAULT;
	}

	/* Double check if the vif is disconnected */
	if(!rwnx_vif_is_connected(rwnx_vif))
		goto out;

	/* Set wifi calibration parameters */
	memset(&cali_req, 0, sizeof(cali_req));
	cali_req.step = BES_MCU_WIFI_CALI_TX;
	cali_req.freq = ctxt->chan_def.center_freq1;
	cali_req.band = bw2chnl[ctxt->chan_def.width];
	cali_ch = (u8)ieee80211_frequency_to_channel(ctxt->chan_def.center_freq1);

	/* The time range for calibration in [0, CONFIG_CALI_TIME] milliseconds */
	error = bes_dbac_trigger_roc(rwnx_vif, ctxt->chan_def.chan, CONFIG_CALI_TIME);
	if (error) {
		bes_err(BES_DBG_CALI, "%s start ROC err:%d\n", __func__, error);
		goto out;
	}

	/* Record start time */
	start_time = ktime_get();

	/* Send wifi calibration command to device */
	memset(&cfm, 0, sizeof(struct mcu_wf_cali_cfm));
	error = bes_send_wf_cali_req(rwnx_hw, &cali_req, &cfm);
	if (error || cfm.status) {
		bes_err(BES_DBG_CALI, "%s fail:%d status:%d\n", __func__, error, cfm.status);
	} else {
		/* Store calibrated channel information. */
		bes_dbac_cali_result_record(rwnx_vif, cali_ch, false);
	}

	/* Record end time and calculate consumed time */
	end_time = ktime_get();
	bes_info(BES_DBG_CALI, "%s (channel-%d)use:%lld ms\n",
		 __func__, cali_ch, ktime_to_ms(ktime_sub(end_time, start_time)));

	/* Cancel ROC */
	bes_dbac_cali_wait_roc_end(rwnx_hw, rwnx_vif);
out:
	up(&rwnx_hw->phy_op_sema);
	return error;
}

/**
 * After CSA updates the channel context,
 * traverse the channel context and calibrate the uncalibrated channel.
*/
void bes_dbac_cali_after_csa(struct rwnx_vif *rwnx_vif)
{
	struct rwnx_chanctx *ctxt = NULL;
	u8 ctxt_chan = 0;
	struct rwnx_hw *rwnx_hw;
	struct bes_cali_result *cali_res;
	u8 cali_idx, ctxt_idx;
	bool found;
	u8 vif_idx;

	/* Check if the rwnx_vif is valid */
	if (!rwnx_vif) {
		bes_err(BES_DBG_CALI, "%s rwnx_vif is NULL\n", __func__);
		return;
	}

	/* Check the validity of vif_index */
	vif_idx = rwnx_vif->vif_index;
	if (vif_idx >= NX_ITF_MAX){
		bes_err(BES_DBG_CALI, "%s vif_idx(%d) is invalid\n",
			__func__, vif_idx);
		return;
	}

	/* Get the value of rwnx_hw and cali_res */
	rwnx_hw = rwnx_vif->rwnx_hw;
	cali_res = &rwnx_hw->rf_cali.cali_res[0];

	/* Mark the vif which calibration is defered. */
	if (bes_coex_bt_get_audio_state(rwnx_hw) != BES_BT_AUDIO_NONE) {
		rwnx_hw->rf_cali.defer_cali_vif_map |= BIT(vif_idx);
		bes_info(BES_DBG_CALI, "cali on vif(%s) defered\n",
			 rwnx_vif->ndev->name);
		return;
	}

	/* Invalid calibration data that is no longer used after CSA. */
	bes_dbac_inval_cali_check(rwnx_hw);

	/* Find the uncalibrated channel in the channel context and calibrate. */
	for (ctxt_idx = 0; ctxt_idx < NX_CHAN_CTXT_CNT; ctxt_idx++) {
		ctxt = &rwnx_hw->chanctx_table[ctxt_idx];
		if (!ctxt->count || !cfg80211_chandef_valid(&ctxt->chan_def))
			continue;

		found = false;
		ctxt_chan = (u8)ieee80211_frequency_to_channel(ctxt->chan_def.center_freq1);

		/* Found uncalibrated channel context. */
		for (cali_idx = 0; cali_idx < MAX_COEXIST_CHAN_CTXT; cali_idx++) {
			if (ctxt_chan == cali_res[cali_idx].channel) {
				found = true;
				break;
			}
		}

		/* Calibrate if needed. */
		if (!found)
			bes_dbac_csa_recali(rwnx_vif, ctxt_idx);
	}
}

static void bes_dbac_sta_csa_work(struct work_struct *work)
{
	struct rwnx_csa *rwnx_csa = container_of(work, struct rwnx_csa, work);
	struct rwnx_vif *rwnx_vif = rwnx_csa->vif;
	struct rwnx_hw *rwnx_hw;
	struct bes_dbac_sta_csa *sta_csa;
	struct rwnx_csa *csa = NULL;

	if (!rwnx_vif) {
		bes_err(BES_DBG_MSG_RX, "%s ERROR: vif is not find\n", __func__);
		return;
	}

	rwnx_hw = rwnx_vif->rwnx_hw;
	sta_csa = &rwnx_vif->sta.sta_csa;

	/* Update the channel context. */
	spin_lock_bh(&rwnx_hw->cb_lock);
	rwnx_chanctx_unlink(rwnx_vif);
	rwnx_chanctx_link(rwnx_vif, rwnx_csa->ch_idx, &rwnx_csa->chandef);
	rwnx_vif->in_csa = false;
	spin_unlock_bh(&rwnx_hw->cb_lock);

	/* Update Records */
	rwnx_vif->vif_chandef = rwnx_csa->chandef;

	/* Notify channel switch to cfg80211 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 7, 0))
	wiphy_lock(rwnx_hw->wiphy);
#else
	mutex_lock(&rwnx_vif->wdev.mtx);
	__acquire(&rwnx_vif->wdev.mtx);
#endif
#ifdef ADPT_GKI_KERNEL
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 144))
	cfg80211_ch_switch_notify(rwnx_vif->ndev, &rwnx_csa->chandef, 0, 0);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 81))
	cfg80211_ch_switch_notify(rwnx_vif->ndev, &rwnx_csa->chandef, 0);
#else
	cfg80211_ch_switch_notify(rwnx_vif->ndev, &rwnx_csa->chandef);
#endif
#else /* ADPT_GKI_KERNEL */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0))
	cfg80211_ch_switch_notify(rwnx_vif->ndev, &rwnx_csa->chandef, 0, 0);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 19, 2))
	cfg80211_ch_switch_notify(rwnx_vif->ndev, &rwnx_csa->chandef, 0);
#else
	cfg80211_ch_switch_notify(rwnx_vif->ndev, &rwnx_csa->chandef);
#endif /* ADPT_GKI_KERNEL */
#endif
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 7, 0))
	wiphy_unlock(rwnx_hw->wiphy);
#else
	mutex_unlock(&rwnx_vif->wdev.mtx);
	__release(&rwnx_vif->wdev.mtx);
#endif

	/* Recalibration process. */
	bes_dbac_cali_after_csa(rwnx_vif);

	/* CSA completion for STA */
	spin_lock(&sta_csa->csa_lock);
	csa = sta_csa->csa;
	sta_csa->csa = NULL;
	spin_unlock(&sta_csa->csa_lock);

	/* Free rwnx_csa resource */
	if (csa)
		kfree(csa);
}

void bes_dbac_sta_csa_finish_ind(struct rwnx_vif *rwnx_vif, const struct mm_csa_finish_ind *ind)
{
	struct bes_dbac_sta_csa *sta_csa = &rwnx_vif->sta.sta_csa;
	struct rwnx_csa *csa = NULL, *csa_free = NULL;

	csa = kzalloc(sizeof(struct rwnx_csa), GFP_ATOMIC);
	if (!csa) {
		bes_err(BES_DBG_CALI, "%s kzalloc fail", __func__);
		return;
	}

	/* When the previous CSA calibration has not been completed,
	 * Cancelling it before the next calibration. */
	spin_lock(&sta_csa->csa_lock);
	if (sta_csa->csa) {
		spin_unlock(&sta_csa->csa_lock);

		cancel_work_sync(&sta_csa->csa->work);

		spin_lock(&sta_csa->csa_lock);
	}

	if (sta_csa->csa)
		csa_free = sta_csa->csa;

	/* Initialize the work of updating channel context after CSA. */
	csa->vif = rwnx_vif;
	csa->ch_idx = ind->chan_idx;
	INIT_WORK(&csa->work, bes_dbac_sta_csa_work);
	sta_csa->chan_update_pending = true;
	sta_csa->csa = csa;
	spin_unlock(&sta_csa->csa_lock);

	if (csa_free)
		kfree(csa_free);
}

/* Intercept management frames containing CSA on the data receiving path. */
void bes_dbac_rx_sta_csa(struct rwnx_vif *rwnx_vif, const struct sk_buff *skb, struct hw_rxhdr *hw_rxhdr)
{
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;
	struct ieee80211_channel *chan = NULL;
	struct cfg80211_chan_def rx_chandef;
	struct mac_chan_op mac_chan;
	struct rwnx_sta *rwnx_sta = rwnx_vif->sta.ap;

	/* filter peer ap beacon to update chandef */
	if ((RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_STATION &&
	     RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_P2P_CLIENT) ||
	    !rwnx_sta ||
	    !ether_addr_equal(mgmt->bssid, rwnx_sta->mac_addr))
		return;

	/* set mac_chan_op value */
	memset(&mac_chan, 0, sizeof(struct mac_chan_op));
	mac_chan.band = hw_rxhdr->phy_info.phy_band;
	mac_chan.type = hw_rxhdr->hwvect.rx_vect1.ch_bw;
	mac_chan.prim20_freq = hw_rxhdr->phy_info.phy_prim20_freq;
	mac_chan.center1_freq = hw_rxhdr->phy_info.phy_center1_freq;
	mac_chan.center2_freq = hw_rxhdr->phy_info.phy_center2_freq;

	/* Parse channel information in advance by hw_rxhdr structure.
	   And 36 means 24(header size) + 12(fixed fields) */
	chan = ieee80211_get_channel(rwnx_vif->rwnx_hw->wiphy, hw_rxhdr->phy_info.phy_prim20_freq);
	if (chan && !bes_dbac_chan_to_chandef(&mac_chan, chan, &rx_chandef))
		bes_dbac_sta_csa_process(rwnx_vif, mgmt, skb->len - 36, &rx_chandef);
	else
		bes_dbac_sta_csa_process(rwnx_vif, mgmt, skb->len - 36, NULL);
}

/**
 * Intercept Beacon frames containing CSA in scan results.
 * As STA. CSA is performed after scanning starts,
 * and beacon frames can only be received from scan results.
*/
void bes_dbac_scan_sta_csa(struct rwnx_hw *rwnx_hw, const struct scanu_result_ind *ind)
{
	struct rwnx_vif *rwnx_vif;
	const struct ieee80211_mgmt *mgmt = (const struct ieee80211_mgmt *)ind->payload;
	struct ieee80211_channel *chan = NULL;
	struct cfg80211_chan_def rx_chandef;
	struct mac_chan_op mac_chan;
	struct rwnx_sta *rwnx_sta;

	rwnx_vif = rwnx_rx_get_vif(rwnx_hw, ind->inst_nbr);
	if (!rwnx_vif) {
		bes_err(BES_DBG_CALI, "%s No active virtual interfaces\n", __func__);
		return;
	}

	rwnx_sta = rwnx_vif->sta.ap;

	/* filter peer ap beacon to update chandef */
	if (!ieee80211_is_beacon(mgmt->frame_control) ||
	    (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_STATION &&
	     RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_P2P_CLIENT) ||
	    !rwnx_sta ||
	    !ether_addr_equal(mgmt->bssid, rwnx_sta->mac_addr))
		return;

	/* set mac_chan_op value */
	memset(&mac_chan, 0, sizeof(struct mac_chan_op));
	mac_chan.band = ind->band;
	mac_chan.type = PHY_CHNL_BW_20;
	mac_chan.prim20_freq = ind->center_freq;
	mac_chan.center1_freq = ind->center_freq;
	mac_chan.center2_freq = 0;

	/* Parse channel information in advance by scanu_result_ind structure. */
	chan = ieee80211_get_channel(rwnx_hw->wiphy, ind->center_freq);
	if (chan && !bes_dbac_chan_to_chandef(&mac_chan, chan, &rx_chandef))
		bes_dbac_sta_csa_process(rwnx_vif, mgmt, ind->length, &rx_chandef);
	else
		bes_dbac_sta_csa_process(rwnx_vif, mgmt, ind->length, NULL);
}

/* Disconnect while processing CSA. */
void bes_dbac_disconn_while_csa(struct rwnx_vif *rwnx_vif)
{
	struct bes_dbac_sta_csa *sta_csa = &rwnx_vif->sta.sta_csa;
	struct rwnx_csa *csa = NULL;

	spin_lock(&sta_csa->csa_lock);
	if (!sta_csa->csa) {
		/* CSA hasn't started. */
		spin_unlock(&sta_csa->csa_lock);
		return;
	}

	/* CSA has started. */
	csa = sta_csa->csa;
	if (sta_csa->chan_update_pending) {
		/* before queue_work */
		sta_csa->chan_update_pending = false;
		sta_csa->csa = NULL;
		spin_unlock(&sta_csa->csa_lock);

		kfree(csa);
	} else {
		/* after queue_work */
		spin_unlock(&sta_csa->csa_lock);

		/* Blocking waiting for work to be cancelled. */
		cancel_work_sync(&csa->work);

		/* Manually release the CSA structure when the work is not actually executed. */
		spin_lock(&sta_csa->csa_lock);
		if (sta_csa->csa) {
			csa = sta_csa->csa;
			sta_csa->csa = NULL;
			spin_unlock(&sta_csa->csa_lock);

			kfree(csa);
		} else {
			spin_unlock(&sta_csa->csa_lock);
		}
	}

	bes_info(BES_DBG_CALI, "Disconnecting in CSA\n");
}

/* Initialize the CSA structure of the station. */
void bes_dbac_sta_csa_init(struct rwnx_vif *rwnx_vif)
{
	struct bes_dbac_sta_csa *sta_csa;

	/* Initialize STA only. */
	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_STATION &&
	    RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_P2P_CLIENT)
		return;

	sta_csa = &rwnx_vif->sta.sta_csa;
	spin_lock_init(&sta_csa->csa_lock);
	sta_csa->csa = NULL;
	sta_csa->chan_update_pending = false;
	sta_csa->in_csa = false;
}

int bes_dbac_cali_init(struct rwnx_hw *rwnx_hw)
{
	struct bes_rf_cali *rf_cali = &rwnx_hw->rf_cali;
	struct bes_cali_result *cali_res = &rf_cali->cali_res[0];
	u8 idx = 0;

	for (idx = 0; idx < MAX_COEXIST_CHAN_CTXT; idx++) {
		cali_res[idx].type = BES_CALI_TYPE_MAX;
		cali_res[idx].channel = BES_DBAC_UNUSED_CHANNEL;
		cali_res[idx].count = 0;
	}

	rf_cali->cali_res_cnt = 0;
	rf_cali->dyna_cali_enabled = false;
	rf_cali->cur_cali_ch_ctxt_idx = 0;
	rf_cali->defer_cali_vif_map = 0;
	INIT_WORK(&rf_cali->temp_comp_work, bes_temp_compensation_work);

	return 0;
}

/*********************************************************************
 * dpd calibration save API
 ********************************************************************/

void bes_free_dpd_buffer(struct rwnx_plat *rwnx_plat)
{
	if (rwnx_plat->dpd_cali.data)
		kfree(rwnx_plat->dpd_cali.data);

	rwnx_plat->dpd_cali.data = NULL;
	rwnx_plat->dpd_cali.size = 0;
}

static bool bes_get_dpd_buffer(struct rwnx_plat *rwnx_plat, u16 size)
{
	if (rwnx_plat->dpd_cali.data)
		bes_free_dpd_buffer(rwnx_plat);

	if (size == 0)
		return false;

	rwnx_plat->dpd_cali.data = kzalloc(size, GFP_KERNEL);
	if (!rwnx_plat->dpd_cali.data) {
		rwnx_plat->dpd_cali.size = 0;
		return false;
	}

	rwnx_plat->dpd_cali.size = size;

	return true;
}

bool bes_check_dpd_cali_data(struct rwnx_hw *rwnx_hw)
{
	bool ret;
#ifndef PLAT_LAPTOP
	ret = (rwnx_hw->plat->dpd_cali.data != NULL) ? true : false;
#else
	ret = bes_pci_get_dpd_flag();
#endif
	return ret;
}

int bes_get_dpd_cail_data(struct rwnx_hw *rwnx_hw)
{
	u32 cal_crc = 0;
	u32 dpd_crc = 0;
	u16 dpd_size = 0;
	int ret;
	struct rwnx_plat *rwnx_plat = rwnx_hw->plat;

	struct mcu_dpd_req dpd_req = {
		.recv_offset = 0,
		.req_size = MCU_REQ_DPD_DATA_MAX_SIZE,
	};

	ret = bes_send_drv2mcu_msg(rwnx_hw, NULL, 0, MCU_WIFI_GET_DPD_SIZE_REQ,
				   MCU_WIFI_GET_DPD_SIZE_CFM, (u8 *)&dpd_size, sizeof(dpd_size));
	if (ret)
		return ret;

	if (!bes_get_dpd_buffer(rwnx_plat, dpd_size))
		return -ENOMEM;

	while (dpd_req.recv_offset < dpd_size) {
		if (dpd_size - dpd_req.recv_offset >= MCU_REQ_DPD_DATA_MAX_SIZE)
			dpd_req.req_size = MCU_REQ_DPD_DATA_MAX_SIZE;
		else
			dpd_req.req_size = dpd_size - dpd_req.recv_offset;
		ret = bes_send_drv2mcu_msg(rwnx_hw, (u8 *)&dpd_req, sizeof(struct mcu_dpd_req),
					   MCU_WIFI_GET_DPD_DATA_REQ, MCU_WIFI_GET_DPD_DATA_CFM,
					   rwnx_plat->dpd_cali.data + dpd_req.recv_offset, dpd_req.req_size);
		if (ret)
			goto err;

		dpd_req.recv_offset += dpd_req.req_size;
	};

	/* check if the dpd data is valid */
	dpd_crc = *((u32 *)rwnx_plat->dpd_cali.data);
	cal_crc ^= 0xffffffffL;
	cal_crc = crc32_le(cal_crc, rwnx_plat->dpd_cali.data + 4, rwnx_plat->dpd_cali.size - 4);
	cal_crc ^= 0xffffffffL;
	if (cal_crc != dpd_crc) {
		bes_err(BES_DBG_CALI, "dpd data check failed, calc_crc: %u dpd_crc: %u\n",
		         cal_crc, dpd_crc);
		ret = -1;
		goto err;
	}
#ifdef PLAT_LAPTOP
	save_flag_to_file(DPD_SAVE_DIR, (u8 *)&(rwnx_plat->dpd_cali.size),
			  sizeof(rwnx_plat->dpd_cali.size), 0);
	save_flag_to_file(DPD_SAVE_DIR, rwnx_plat->dpd_cali.data, rwnx_plat->dpd_cali.size,
			  sizeof(rwnx_plat->dpd_cali.size));
#endif
	bes_info(BES_DBG_CALI, "bes2610 get dpd cali success.\n");
	return 0;

err:
	bes_free_dpd_buffer(rwnx_plat);
	return ret;

}

int bes_send_dpd_cail_data(struct rwnx_hw *rwnx_hw)
{
	int ret;
	struct rwnx_plat *rwnx_plat = rwnx_hw->plat;
	u16 dpd_size = rwnx_plat->dpd_cali.size;
	struct mcu_recover_dpd recover_dpd = {
		.offset = 0,
		.size = MCU_RECOVER_DPD_DATA_MAX_SIZE,
		.last_pkt = 0,
	};
#ifndef PLAT_LAPTOP
	if (!rwnx_plat->dpd_cali.data)
		return -ENOENT;
#else
	read_flag_from_file(DPD_SAVE_DIR, (u8 *)&(rwnx_plat->dpd_cali.size),
			    sizeof(rwnx_plat->dpd_cali.size), 0);
	if (!bes_get_dpd_buffer(rwnx_plat, rwnx_plat->dpd_cali.size)) {
		bes_err(BES_DBG_CALI, "%s get dpd buffer fail\n", __func__);
		return -ENOENT;
	}
	dpd_size = rwnx_plat->dpd_cali.size;
	read_flag_from_file(DPD_SAVE_DIR, rwnx_plat->dpd_cali.data, rwnx_plat->dpd_cali.size,
			    sizeof(rwnx_plat->dpd_cali.size));
#endif

	while (recover_dpd.offset < dpd_size) {
		if (dpd_size - recover_dpd.offset >= MCU_RECOVER_DPD_DATA_MAX_SIZE)
			recover_dpd.size = MCU_RECOVER_DPD_DATA_MAX_SIZE;
		else {
			recover_dpd.size = dpd_size - recover_dpd.offset;
			recover_dpd.last_pkt = 1;
			bes_dbg(BES_DBG_CALI, "recover_dpd.last_pkt = %d\n", recover_dpd.last_pkt);
		}

		ret = bes_send_recover_wifi_dpd_data(rwnx_hw, (void *)&recover_dpd,
						     rwnx_plat->dpd_cali.data + recover_dpd.offset);
		if (ret) {
			bes_err(BES_DBG_CALI, "bes2610 send dpd cali fail.\n");
			return ret;
		}

		recover_dpd.offset += recover_dpd.size;
	};

	bes_info(BES_DBG_CALI, "bes2610 send dpd cali success.\n");
	return 0;
}

#ifdef PLAT_LAPTOP
static int save_flag_to_file(const char* save_file_name, u8 * flag, int size, loff_t pos)
{
	struct file *fp = NULL;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	mm_segment_t fs;
#endif
	fp = filp_open(save_file_name, O_RDWR | O_CREAT, 0644);
	if (IS_ERR(fp)) {
		bes_err(BES_DBG_CALI, "open dpd file fail\n");
		return -1;
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	fs = get_fs();
	set_fs(KERNEL_DS);
	vfs_write(fp, flag, size, &pos);
#else
	kernel_write(fp, flag, size, &pos);
#endif
	filp_close(fp, NULL);
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	set_fs(fs);
#endif
	return 0;
}

static int read_flag_from_file(const char* save_file_name, u8 *flag, int size, loff_t pos)
{
	struct file *fp = NULL;
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	mm_segment_t fs;
#endif
	fp = filp_open(save_file_name, O_RDWR | O_CREAT, 0644);
	if (IS_ERR(fp)) {
		bes_err(BES_DBG_CALI, "open dpd file fail\n");
		return -1;
	}
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	fs = get_fs();
	set_fs(KERNEL_DS);
	vfs_read(fp, flag, size, &pos);
#else
	kernel_read(fp, flag, size, &pos);
#endif
	filp_close(fp, NULL);
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
	set_fs(fs);
#endif
	return 0;
}
#endif
