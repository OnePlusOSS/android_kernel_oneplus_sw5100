/**
 ****************************************************************************************
 *
 * @file rwnx_msg_rx.c
 *
 * @brief RX function definitions
 *
 * Copyright (C) RivieraWaves 2012-2021
 *
 ****************************************************************************************
 */
#include "rwnx_defs.h"
#include "rwnx_prof.h"
#ifdef CONFIG_RWNX_BFMER
#include "rwnx_bfmer.h"
#endif //(CONFIG_RWNX_BFMER)
#ifdef CONFIG_RWNX_FULLMAC
#include "rwnx_msg_tx.h"
#include "rwnx_msg_rx.h"
#include "rwnx_tdls.h"
#endif /* CONFIG_RWNX_FULLMAC */
#include "rwnx_events.h"
#include "bes_power_ctrl.h"
#include "rwnx_main.h"
#include "bes_dbac_cali.h"
#include "bes_gtk_offload.h"
#include "bes_fw_log_fifo_dump.h"
#include "bes_linkinfo.h"
#include "bes_sched_scan.h"
#include "bes_vif_info.h"
#include "bes_regd.h"
#include "bes_dbg.h"
#include "bes_csi.h"
#include "bes_tx_loop.h"
#include "bes_error_dbg.h"
#include "bes_conn_state.h"
#include "bes_eapol.h"
#include "bes_signal_test.h"
#include "bes_tp_dbg.h"
#include "bes_wpa_priv_cmd.h"
#include <linux/rtnetlink.h>

static int rwnx_freq_to_idx(struct rwnx_hw *rwnx_hw, int freq)
{
	struct ieee80211_supported_band *sband;
	int band, ch, idx = 0;

	for (band = NL80211_BAND_2GHZ; band < NUM_NL80211_BANDS; band++) {
#ifdef CONFIG_RWNX_SOFTMAC
		sband = rwnx_hw->hw->wiphy->bands[band];
#else
		sband = rwnx_hw->wiphy->bands[band];
#endif /* CONFIG_RWNX_SOFTMAC */
		if (!sband) {
			continue;
		}

		for (ch = 0; ch < sband->n_channels; ch++, idx++) {
			if (sband->channels[ch].center_freq == freq) {
				goto exit;
			}
		}
	}

	BUG_ON(1);

exit:
	// Channel has been found, return the index
	return idx;
}

/***************************************************************************
 * Messages from MM task
 **************************************************************************/
static inline int rwnx_rx_chan_pre_switch_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
#ifdef CONFIG_RWNX_SOFTMAC
	struct rwnx_chanctx *chan_ctxt;
#endif
	struct rwnx_vif *rwnx_vif;
	int chan_idx = ((struct mm_channel_pre_switch_ind *)msg->param)->chan_index;

	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	REG_SW_SET_PROFILING_CHAN(rwnx_hw, SW_PROF_CHAN_CTXT_PSWTCH_BIT);

#ifdef CONFIG_RWNX_SOFTMAC
	list_for_each_entry(chan_ctxt, &rwnx_hw->chan_ctxts, list) {
		if (chan_ctxt->index == chan_idx) {
			chan_ctxt->active = false;
			break;
		}
	}

	list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
		if (rwnx_vif->chanctx && (rwnx_vif->chanctx->index == chan_idx)) {
			rwnx_txq_vif_stop(rwnx_vif, RWNX_TXQ_STOP_CHAN, rwnx_hw);
		}
	}
#else
	list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
		if (rwnx_vif->up && rwnx_vif->ch_index == chan_idx) {
			rwnx_txq_vif_stop(rwnx_vif, RWNX_TXQ_STOP_CHAN, rwnx_hw);
		}
	}
#endif /* CONFIG_RWNX_SOFTMAC */

	REG_SW_CLEAR_PROFILING_CHAN(rwnx_hw, SW_PROF_CHAN_CTXT_PSWTCH_BIT);

	return 0;
}

static inline int rwnx_rx_chan_switch_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
#ifdef CONFIG_RWNX_SOFTMAC
	struct rwnx_chanctx *chan_ctxt;
	struct rwnx_sta *rwnx_sta;
#endif
	struct rwnx_vif *rwnx_vif;
	int chan_idx = ((struct mm_channel_switch_ind *)msg->param)->chan_index;
	bool roc_req = ((struct mm_channel_switch_ind *)msg->param)->roc;
	bool roc_tdls = ((struct mm_channel_switch_ind *)msg->param)->roc_tdls;

	bes_dbg(BES_DBG_MSG_RX, ">>>%s chan_idx:%d roc_req:%d roc_tdls:%d\n",
		   __func__, chan_idx, roc_req, roc_tdls);

	REG_SW_SET_PROFILING_CHAN(rwnx_hw, SW_PROF_CHAN_CTXT_SWTCH_BIT);

	if ((chan_idx == CHAN_ROC_CTXT_IDX) && !roc_req && !roc_tdls) {
		if (!test_bit(RWNX_DEV_FW_LEVEL_ROC, &rwnx_hw->flags))
			rwnx_wf_status_set(rwnx_hw, RWNX_DEV_FW_LEVEL_ROC);
		else
			WARN(1, "The ROC of the firmware level repeats\n");
	} else if (test_bit(RWNX_DEV_FW_LEVEL_ROC, &rwnx_hw->flags)) {
		rwnx_wf_status_clear(rwnx_hw, RWNX_DEV_FW_LEVEL_ROC);
		wake_up(&rwnx_hw->roc_wait_wq);
	}

#ifdef CONFIG_RWNX_SOFTMAC
	if (roc_tdls) {
		u8 vif_index = ((struct mm_channel_switch_ind *)msg->param)->vif_index;
		// Enable traffic only for TDLS station
		list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
			if (rwnx_vif->vif_index == vif_index) {
				list_for_each_entry(rwnx_sta, &rwnx_vif->stations, list) {
					if (rwnx_sta->tdls.active) {
						rwnx_vif->roc_tdls = true;
						rwnx_txq_tdls_sta_start(rwnx_sta, RWNX_TXQ_STOP_CHAN, rwnx_hw);
						break;
					}
				}
				break;
			}
		}
	} else if (!roc_req) {
		list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
			if (rwnx_vif->chanctx && (rwnx_vif->chanctx->index == chan_idx)) {
				rwnx_txq_vif_start(rwnx_vif, RWNX_TXQ_STOP_CHAN, rwnx_hw);
			}
		}
	} else {
		u8 vif_index = ((struct mm_channel_switch_ind *)msg->param)->vif_index;

		// Inform the host that the offchannel period has been started
		ieee80211_ready_on_channel(rwnx_hw->hw);

		// Enable traffic for associated VIF (roc may happen without chanctx)
		list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
			if (rwnx_vif->vif_index == vif_index) {
				rwnx_txq_vif_start(rwnx_vif, RWNX_TXQ_STOP_CHAN, rwnx_hw);
			}
		}
	}

	/* keep cur_chan up to date */
	list_for_each_entry(chan_ctxt, &rwnx_hw->chan_ctxts, list) {
		if (chan_ctxt->index == chan_idx) {
			chan_ctxt->active = true;
			rwnx_hw->cur_freq = chan_ctxt->ctx->def.center_freq1;
			rwnx_hw->cur_band = chan_ctxt->ctx->def.chan->band;
			if (chan_ctxt->ctx->def.chan->flags & IEEE80211_CHAN_RADAR) {
				rwnx_radar_detection_enable(&rwnx_hw->radar, RWNX_RADAR_DETECT_REPORT, RWNX_RADAR_RIU);
			} else {
				rwnx_radar_detection_enable(&rwnx_hw->radar, RWNX_RADAR_DETECT_DISABLE, RWNX_RADAR_RIU);
			}
			break;
		}
	}

#else
	if (roc_tdls) {
		u8 vif_index = ((struct mm_channel_switch_ind *)msg->param)->vif_index;
		list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
			if (rwnx_vif->vif_index == vif_index) {
				rwnx_vif->roc_tdls = true;
				rwnx_txq_tdls_sta_start(rwnx_vif, RWNX_TXQ_STOP_CHAN, rwnx_hw);
			}
		}
	} else if (!roc_req) {
		list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
			if (rwnx_vif->up && rwnx_vif->ch_index == chan_idx) {
				rwnx_txq_vif_start(rwnx_vif, RWNX_TXQ_STOP_CHAN, rwnx_hw);
			}
		}
	} else {
		spin_lock_bh(&rwnx_hw->roc_lock);
		if (!rwnx_hw->roc) {
			spin_unlock_bh(&rwnx_hw->roc_lock);
			WARN(1, "%s driver did not initiate an ROC request\n", __func__);
			return 0;
		}

		rwnx_vif = rwnx_hw->roc->vif;

		trace_switch_roc(rwnx_vif->vif_index);

		if (!rwnx_hw->roc->internal) {
			// If RoC has been started by the user space, inform it that we have
			// switched on the requested off-channel
			u64 cookie = (unsigned long)(rwnx_hw->roc);
			cfg80211_ready_on_channel(&rwnx_vif->wdev, cookie,
				   rwnx_hw->roc->chan, rwnx_hw->roc->duration, GFP_ATOMIC);
		}

		// Keep in mind that we have switched on the channel
		rwnx_hw->roc->on_chan = true;
		spin_unlock_bh(&rwnx_hw->roc_lock);

		// Enable traffic on OFF channel queue
		rwnx_txq_offchan_start(rwnx_hw);

		wake_up(&rwnx_hw->roc_wait_wq);
	}

	rwnx_hw->cur_chanctx = chan_idx;
	rwnx_radar_detection_enable_on_cur_channel(rwnx_hw);

#endif /* CONFIG_RWNX_SOFTMAC */

	REG_SW_CLEAR_PROFILING_CHAN(rwnx_hw, SW_PROF_CHAN_CTXT_SWTCH_BIT);

	return 0;
}

static inline int rwnx_rx_tdls_chan_switch_cfm(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	return 0;
}

static inline int rwnx_rx_tdls_chan_switch_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
#ifdef CONFIG_RWNX_SOFTMAC
	struct rwnx_chanctx *chan_ctxt;
	u8 chan_idx = ((struct tdls_chan_switch_ind *)msg->param)->chan_ctxt_index;

	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	// Enable channel context
	list_for_each_entry(chan_ctxt, &rwnx_hw->chan_ctxts, list) {
		if (chan_ctxt->index == chan_idx) {
			chan_ctxt->active = true;
			rwnx_hw->cur_freq = chan_ctxt->ctx->def.center_freq1;
			rwnx_hw->cur_band = chan_ctxt->ctx->def.chan->band;
		}
	}

	return 0;
#else
	// Enable traffic on OFF channel queue
	rwnx_txq_offchan_start(rwnx_hw);

	return 0;
#endif
}

static inline int rwnx_rx_tdls_chan_switch_base_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd,
						    struct ipc_e2a_msg *msg)
{
	struct rwnx_vif *rwnx_vif;
	u8 vif_index = ((struct tdls_chan_switch_base_ind *)msg->param)->vif_index;
#ifdef CONFIG_RWNX_SOFTMAC
	struct rwnx_sta *rwnx_sta;
#endif

	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

#ifdef CONFIG_RWNX_SOFTMAC
	// Disable traffic for associated VIF
	list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
		if (rwnx_vif->vif_index == vif_index) {
			if (rwnx_vif->chanctx)
				rwnx_vif->chanctx->active = false;
			list_for_each_entry(rwnx_sta, &rwnx_vif->stations, list) {
				if (rwnx_sta->tdls.active) {
					rwnx_vif->roc_tdls = false;
					rwnx_txq_tdls_sta_stop(rwnx_sta, RWNX_TXQ_STOP_CHAN, rwnx_hw);
					break;
				}
			}
			break;
		}
	}
	return 0;
#else
	list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
		if (rwnx_vif->vif_index == vif_index) {
			rwnx_vif->roc_tdls = false;
			rwnx_txq_tdls_sta_stop(rwnx_vif, RWNX_TXQ_STOP_CHAN, rwnx_hw);
		}
	}
	return 0;
#endif
}

static inline int rwnx_rx_tdls_peer_ps_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct rwnx_vif *rwnx_vif;
	u8 vif_index = ((struct tdls_peer_ps_ind *)msg->param)->vif_index;
	bool ps_on = ((struct tdls_peer_ps_ind *)msg->param)->ps_on;

#ifdef CONFIG_RWNX_SOFTMAC
	u8 sta_idx = ((struct tdls_peer_ps_ind *)msg->param)->sta_idx;
	struct rwnx_sta *rwnx_sta;
	list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
		if (rwnx_vif->vif_index == vif_index) {
			list_for_each_entry(rwnx_sta, &rwnx_vif->stations, list) {
				if (rwnx_sta->sta_idx == sta_idx) {
					rwnx_sta->tdls.ps_on = ps_on;
					if (ps_on) {
						// disable TXQ for TDLS peer
						rwnx_txq_tdls_sta_stop(rwnx_sta, RWNX_TXQ_STOP_STA_PS, rwnx_hw);
					} else {
						// Enable TXQ for TDLS peer
						rwnx_txq_tdls_sta_start(rwnx_sta, RWNX_TXQ_STOP_STA_PS, rwnx_hw);
					}
					break;
				}
			}
			break;
		}
	}
	return 0;
#else
	list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
		if (rwnx_vif->vif_index == vif_index) {
			rwnx_vif->sta.tdls_sta->tdls.ps_on = ps_on;
			// Update PS status for the TDLS station
			rwnx_ps_bh_enable(rwnx_hw, rwnx_vif->sta.tdls_sta, ps_on);
		}
	}

	return 0;
#endif
}

static inline int rwnx_rx_remain_on_channel_exp_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd,
						    struct ipc_e2a_msg *msg)
{
#ifdef CONFIG_RWNX_SOFTMAC
	struct rwnx_vif *rwnx_vif;
	u8 vif_index = ((struct mm_remain_on_channel_exp_ind *)msg->param)->vif_index;

	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	ieee80211_remain_on_channel_expired(rwnx_hw->hw);

	// Disable traffic for associated VIF
	list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
		if (rwnx_vif->vif_index == vif_index) {
			if (rwnx_vif->chanctx)
				rwnx_vif->chanctx->active = false;

			rwnx_txq_vif_stop(rwnx_vif, RWNX_TXQ_STOP_CHAN, rwnx_hw);
			break;
		}
	}

	return 0;

#else
	struct rwnx_vif *rwnx_vif = NULL;
	struct rwnx_roc *roc;

	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	spin_lock_bh(&rwnx_hw->roc_lock);
	if (!rwnx_hw->roc) {
		spin_unlock_bh(&rwnx_hw->roc_lock);
		WARN(1, "%s driver did not initiate an ROC request\n", __func__);
		return 0;
	}

	rwnx_vif = rwnx_hw->roc->vif;

	if (!rwnx_vif) {
		bes_err(BES_DBG_MSG_RX, "%s ERROR: vif is not find\n", __func__);
		return -EINVAL;
	}

	trace_roc_exp(rwnx_vif->vif_index);

	if (!rwnx_hw->roc->internal && rwnx_hw->roc->on_chan) {
		// If RoC has been started by the user space and hasn't been cancelled,
		// inform it that off-channel period has expired
		u64 cookie = (unsigned long)(rwnx_hw->roc);
		cfg80211_remain_on_channel_expired(&rwnx_vif->wdev, cookie,
			   rwnx_hw->roc->chan, GFP_ATOMIC);
	}

	roc = rwnx_hw->roc;
	rwnx_hw->roc = NULL;
	spin_unlock_bh(&rwnx_hw->roc_lock);

	rwnx_txq_offchan_deinit(rwnx_vif);

	kfree(roc);
	wake_up(&rwnx_hw->roc_wait_wq);

#endif /* CONFIG_RWNX_SOFTMAC */
	return 0;
}

static inline int rwnx_rx_p2p_vif_ps_change_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	int vif_idx = ((struct mm_p2p_vif_ps_change_ind *)msg->param)->vif_index;
	int ps_state = ((struct mm_p2p_vif_ps_change_ind *)msg->param)->ps_state;
	struct rwnx_vif *vif_entry;

	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

#ifdef CONFIG_RWNX_SOFTMAC
	// Look for VIF entry
	list_for_each_entry(vif_entry, &rwnx_hw->vifs, list) {
		if (vif_entry->vif_index == vif_idx) {
			goto found_vif;
		}
	}
#else
	vif_entry = rwnx_rx_get_vif(rwnx_hw, vif_idx);

	if (vif_entry) {
		goto found_vif;
	}
#endif /* CONFIG_RWNX_SOFTMAC */

	goto exit;

found_vif:

#ifdef CONFIG_RWNX_SOFTMAC
	if (ps_state == MM_PS_MODE_OFF) {
		rwnx_txq_vif_start(vif_entry, RWNX_TXQ_STOP_VIF_PS, rwnx_hw);
	} else {
		rwnx_txq_vif_stop(vif_entry, RWNX_TXQ_STOP_VIF_PS, rwnx_hw);
	}
#else
	if (ps_state == MM_PS_MODE_OFF) {
		// Start TX queues for provided VIF
		rwnx_txq_vif_start(vif_entry, RWNX_TXQ_STOP_VIF_PS, rwnx_hw);
	} else {
		// Stop TX queues for provided VIF
		rwnx_txq_vif_stop(vif_entry, RWNX_TXQ_STOP_VIF_PS, rwnx_hw);
	}
#endif /* CONFIG_RWNX_SOFTMAC */

exit:
	return 0;
}

static inline int rwnx_rx_channel_survey_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct mm_channel_survey_ind *ind = (struct mm_channel_survey_ind *)msg->param;
	// Get the channel index
	int idx = rwnx_freq_to_idx(rwnx_hw, ind->freq);
	// Get the survey
	struct rwnx_survey_info *rwnx_survey;

	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	if (idx >= ARRAY_SIZE(rwnx_hw->survey))
		return 0;

	rwnx_survey = &rwnx_hw->survey[idx];

	// Store the received parameters
	rwnx_survey->chan_time_ms = ind->chan_time_ms;
	rwnx_survey->chan_time_busy_ms = ind->chan_time_busy_ms;
	rwnx_survey->noise_dbm = ind->noise_dbm;
	rwnx_survey->filled = (SURVEY_INFO_TIME | SURVEY_INFO_TIME_BUSY);

	if (ind->noise_dbm != 0) {
		rwnx_survey->filled |= SURVEY_INFO_NOISE_DBM;
	}

	return 0;
}

static inline int rwnx_rx_p2p_noa_upd_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	return 0;
}

static inline int rwnx_rx_rssi_status_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct mm_rssi_status_ind *ind = (struct mm_rssi_status_ind *)msg->param;
	int vif_idx = ind->vif_index;
	bool rssi_status = ind->rssi_status;

	struct rwnx_vif *vif_entry;

	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

#ifdef CONFIG_RWNX_SOFTMAC
	list_for_each_entry(vif_entry, &rwnx_hw->vifs, list) {
		if (vif_entry->vif_index == vif_idx) {
			ieee80211_cqm_rssi_notify(vif_entry->vif,
						  rssi_status ? NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW
							      : NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH,
						  ind->rssi, GFP_ATOMIC);
			bes_cfgvendor_rssi_monitor_event(vif_entry, ind->rssi);
		}
	}
#else
	vif_entry = rwnx_rx_get_vif(rwnx_hw, vif_idx);
	if (vif_entry) {
		cfg80211_cqm_rssi_notify(vif_entry->ndev,
					 rssi_status ? NL80211_CQM_RSSI_THRESHOLD_EVENT_LOW
						     : NL80211_CQM_RSSI_THRESHOLD_EVENT_HIGH,
					 ind->rssi, GFP_ATOMIC);
		bes_cfgvendor_rssi_monitor_event(vif_entry, ind->rssi);
	}
#endif /* CONFIG_RWNX_SOFTMAC */

	return 0;
}

static inline int rwnx_rx_pktloss_notify_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
#ifdef CONFIG_RWNX_FULLMAC
	struct mm_pktloss_ind *ind = (struct mm_pktloss_ind *)msg->param;
	struct rwnx_vif *vif_entry;
	int vif_idx = ind->vif_index;

	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	vif_entry = rwnx_rx_get_vif(rwnx_hw, vif_idx);
	if (vif_entry) {
		cfg80211_cqm_pktloss_notify(vif_entry->ndev, (const u8 *)ind->mac_addr.array, ind->num_packets,
					    GFP_ATOMIC);
	}
#endif /* CONFIG_RWNX_FULLMAC */

	return 0;
}

static void bes_assert_rec_printk_ratelimited(struct anomaly_detect_ind *ind)
{
#define LMAC_ASSERT_REC_RATELIMIT_INTERVAL (2 * HZ)
#define LMAC_ASSERT_REC_RATELIMIT_BURST    5

	static DEFINE_RATELIMIT_STATE(_lmac_assert_rec_rs,
				      LMAC_ASSERT_REC_RATELIMIT_INTERVAL,
				      LMAC_ASSERT_REC_RATELIMIT_BURST);

	if (!__ratelimit(&_lmac_assert_rec_rs))
		bes_tx_loop_force_close(rwnx_get_plat_data(), true);
}

static inline int rwnx_rx_anomaly_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	int info_len = 0;
	u8 anomaly_event_msg[200 + ANOMALY_INFO_MAX_LENGTH] = {0};
	struct timer_err_param *timer_err;
	struct anomaly_detect_ind *ind = (struct anomaly_detect_ind *)msg->param;
	const char *anomaly_detect_event[] = {
		"ANOMALY_DETECT_TBTT_DRIFT",
		"ANOMALY_DETECT_TIM_ERROR",
		"ANOMALY_DETECT_ASSERT_RECOVERY",
		"ANOMALY_DETECT_CS_TIMEOUT"
		"ANOMALY_DETECT_TIMER_SET_ERROR"
	};

	if (ind->id >= ANOMALY_DETECT_MAX) {
		bes_err(BES_DBG_MSG_RX, "Unknown anomaly event(%d) occurs\n", ind->id);
		return 0;
	}

	if (ind->id == ANOMALY_DETECT_TIMER_SET_ERROR) {
		timer_err = (struct timer_err_param *)ind->info;

		info_len = snprintf(anomaly_event_msg, sizeof(anomaly_event_msg), "%s event occurs!\n"
				    "timer_adss:0x%x, time_value:%u, location:0x%x, machw_time[0]:%u, machw_time[1]:%u, machw_time[2]:%u\n",
				    anomaly_detect_event[ind->id], timer_err->timer_adss, timer_err->time_value,
				    timer_err->location, timer_err->machw_time[0], timer_err->machw_time[1], timer_err->machw_time[2]);
	} else {
		info_len = snprintf(anomaly_event_msg, sizeof(anomaly_event_msg), "%s event occurs!\n""%s\n",
				    anomaly_detect_event[ind->id], ind->info);
	}

	bes_warn(BES_DBG_MSG_RX, "%s", anomaly_event_msg);

	if (ind->id == ANOMALY_DETECT_ASSERT_RECOVERY)
		bes_assert_rec_printk_ratelimited(ind);

	return 0;
}

static inline int rwnx_rx_sched_scan_result_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct sched_scan_result_ind *ind = (struct sched_scan_result_ind *)msg->param;

	return bes_sched_scan_result_ind(rwnx_hw, ind);
}

static inline int rwnx_rx_wifi_bt_coex_evt_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct coex_evt_hdr *ind = (struct coex_evt_hdr *)msg->param;

	bes_info(BES_DBG_MSG_RX, "%s id=%d\n", __func__, ind->id);

	return 0;
}

static inline int rwnx_rx_csa_counter_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct mm_csa_counter_ind *ind = (struct mm_csa_counter_ind *)msg->param;
	struct rwnx_vif *vif;
	bool found = false;

	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	// Look for VIF entry
	list_for_each_entry(vif, &rwnx_hw->vifs, list) {
		if (vif->vif_index == ind->vif_index) {
			found = true;
			break;
		}
	}

	if (found) {
#ifdef CONFIG_RWNX_SOFTMAC
		if (ind->csa_count == 1)
			ieee80211_csa_finish(vif->vif);
		else
			ieee80211_beacon_update_cntdwn(vif->vif);
#else
		if (vif->ap.csa)
			vif->ap.csa->count = ind->csa_count;
		else
			netdev_err(vif->ndev, "CSA counter update but no active CSA");

#endif
	}

	return 0;
}

#ifdef CONFIG_RWNX_SOFTMAC
static inline int rwnx_rx_connection_loss_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct rwnx_vif *rwnx_vif;
	u8 inst_nbr;

	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	inst_nbr = ((struct mm_connection_loss_ind *)msg->param)->inst_nbr;

	/* Search the VIF entry corresponding to the instance number */
	list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
		if (rwnx_vif->vif_index == inst_nbr) {
			ieee80211_connection_loss(rwnx_vif->vif);
			break;
		}
	}

	return 0;
}

#ifdef CONFIG_RWNX_BCN
static inline int rwnx_rx_prm_tbtt_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	rwnx_tx_bcns(rwnx_hw);

	return 0;
}
#endif

#else  /* !CONFIG_RWNX_SOFTMAC */
static inline int rwnx_queue_deferred_e2amsg_work(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg,
						  struct rwnx_vif *vif, struct rwnx_sta *sta)
{
	struct rwnx_deferred_msg *deferred_msg;

	deferred_msg = kmalloc(sizeof(struct rwnx_deferred_msg), GFP_ATOMIC);
	if (!deferred_msg) {
		WARN(1, "%s Memory allocation failed\n", __func__);
		return -ENOMEM;
	}
	memcpy(&(deferred_msg->msg), msg, offsetof(struct ipc_e2a_msg, param) + msg->param_len);
	deferred_msg->vif = vif;
	deferred_msg->sta = sta;
	list_add_tail(&(deferred_msg->list), &(rwnx_hw->deferred_e2amsg_list.list));
	queue_work(rwnx_hw->rwnx_wq, &(rwnx_hw->plat->deferred_e2amsg_work));

	return 0;
}

void rwnx_flush_deferred_e2amsg_work(struct rwnx_hw *rwnx_hw)
{
	flush_work(&(rwnx_hw->plat->deferred_e2amsg_work));
}

static inline int rwnx_rx_csa_finish_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct mm_csa_finish_ind *ind = (struct mm_csa_finish_ind *)msg->param;
	struct rwnx_vif *vif = rwnx_rx_get_vif(rwnx_hw, ind->vif_index);

	bes_info(BES_DBG_CSA, "%s vif_index:%d status:%d chan_idx:%d cur_chctx:%d\n",
		 __func__, ind->vif_index, ind->status, ind->chan_idx, rwnx_hw->cur_chanctx);

	if (!vif) {
		bes_err(BES_DBG_MSG_RX, "%s ind->vif_index:%d rwnx_vif is invalid\n", __func__, ind->vif_index);
		return -EINVAL;
	}

	if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_AP || RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_GO) {
		if (vif->ap.csa) {
			vif->ap.csa->status = ind->status;
			vif->ap.csa->ch_idx = ind->chan_idx;
			schedule_work(&vif->ap.csa->work);
		} else
			netdev_err(vif->ndev, "CSA finish indication but no active CSA");
	} else if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_STATION ||
		   RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_CLIENT) {
		if (ind->status)
			return 0;

		/* Marking that vif is in the CSA process */
		vif->in_csa = true;

		/* Update the channel context used by vif.
		 * The information such as channel and bandwidth cannot be obtained
		 * at here. The corresponding channel context will be updated when
		 * the first Beacon frame received on the new channel is received later. */
		rwnx_chanctx_unlink(vif);
		rwnx_chanctx_link(vif, ind->chan_idx, NULL);

		/* If the channel context used by this vif after CSA is the
		 * current channel context, then start txq of this vif. Otherwise, stop it. */
		if (rwnx_hw->cur_chanctx == ind->chan_idx) {
			rwnx_radar_detection_enable_on_cur_channel(rwnx_hw);
			rwnx_txq_vif_start(vif, RWNX_TXQ_STOP_CHAN, rwnx_hw);
		} else
			rwnx_txq_vif_stop(vif, RWNX_TXQ_STOP_CHAN, rwnx_hw);
	} else {
		netdev_err(vif->ndev, "this vif not support CSA\n");
	}

	return rwnx_queue_deferred_e2amsg_work(rwnx_hw, msg, vif, NULL);
}

static inline int rwnx_defer_csa_finish_ind(struct rwnx_hw *rwnx_hw, struct rwnx_deferred_msg *deferred_msg)
{
	struct ipc_e2a_msg *msg = &(deferred_msg->msg);
	struct mm_csa_finish_ind *ind = (struct mm_csa_finish_ind *)msg->param;
	struct rwnx_vif *vif = deferred_msg->vif;

	if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_STATION ||
	    RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_CLIENT) {
		/* Indicates that channel context and calibration can begin. */
		bes_dbac_sta_csa_finish_ind(vif, ind);
	}

	return 0;
}

static inline int rwnx_rx_csa_traffic_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct mm_csa_traffic_ind *ind = (struct mm_csa_traffic_ind *)msg->param;
	struct rwnx_vif *vif;
	bool found = false;

	bes_dbg(BES_DBG_CSA, "%s vif-%u %s txq\n",
		__func__, ind->vif_index, ind->enable ? "start" : "stop");

	// Look for VIF entry
	list_for_each_entry(vif, &rwnx_hw->vifs, list) {
		if (vif->vif_index == ind->vif_index) {
			found = true;
			break;
		}
	}

	if (found) {
		if (ind->enable)
			rwnx_txq_vif_start(vif, RWNX_TXQ_STOP_CSA, rwnx_hw);
		else
			rwnx_txq_vif_stop(vif, RWNX_TXQ_STOP_CSA, rwnx_hw);
	}

	return 0;
}

static inline int rwnx_rx_ps_change_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct mm_ps_change_ind *ind = (struct mm_ps_change_ind *)msg->param;
	struct rwnx_sta *sta = rwnx_rx_get_sta(rwnx_hw, ind->sta_idx);
	struct rwnx_vif *vif;
	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	if (!sta)
		return -EFAULT;

	if (ind->sta_idx >= (NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX)) {
		wiphy_err(rwnx_hw->wiphy, "Invalid sta index reported by fw %d\n", ind->sta_idx);
		return -EFAULT;
	}

	vif = rwnx_rx_get_vif(rwnx_hw, sta->vif_idx);
	if(vif == NULL) {
		wiphy_err(rwnx_hw->wiphy, "vif is NULL\n");
		return -EFAULT;
	} else {
		netdev_dbg(vif->ndev, "Sta %d, change PS mode to %s", sta->sta_idx,
			   ind->ps_state ? "ON" : "OFF");
	}

	if (sta->valid) {
		rwnx_ps_bh_enable(rwnx_hw, sta, ind->ps_state);
	} else if (test_bit(RWNX_DEV_ADDING_STA, &rwnx_hw->flags)) {
		sta->ps.active = ind->ps_state ? true : false;
	} else {
		netdev_err(vif->ndev, "Ignore PS mode change on invalid sta\n");
	}

	return 0;
}

static inline int rwnx_rx_traffic_req_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct mm_traffic_req_ind *ind = (struct mm_traffic_req_ind *)msg->param;
	struct rwnx_sta *sta = rwnx_rx_get_sta(rwnx_hw, ind->sta_idx);
	struct rwnx_vif *vif;

	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	if (!sta)
		return -EFAULT;

	vif = rwnx_rx_get_vif(rwnx_hw, sta->vif_idx);

	if (vif == NULL) {
		bes_err(BES_DBG_MSG_RX, "%s, vif is NULL\n", __func__);
		return -EFAULT;
	} else {
		netdev_dbg(vif->ndev, "Sta %d, asked for %d pkt", sta->sta_idx, ind->pkt_cnt);
	}

	rwnx_ps_bh_traffic_req(rwnx_hw, sta, ind->pkt_cnt, ind->uapsd ? UAPSD_ID : LEGACY_PS_ID);

	return 0;
}
#endif /* CONFIG_RWNX_SOFTMAC */

/***************************************************************************
 * Messages from SCAN task
 **************************************************************************/
#ifdef CONFIG_RWNX_SOFTMAC
static inline int rwnx_rx_scan_done_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
	struct cfg80211_scan_info info = {
		.aborted = false,
	};
#endif
	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	rwnx_ipc_buf_dealloc(rwnx_hw, &rwnx_hw->scan_ie);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
	ieee80211_scan_completed(rwnx_hw->hw, &info);
#else
	ieee80211_scan_completed(rwnx_hw->hw, false);
#endif

	return 0;
}
#endif /* CONFIG_RWNX_SOFTMAC */


#ifdef CONFIG_RWNX_FULLMAC
static void rwnx_scan_done(struct rwnx_hw *rwnx_hw, bool scan_aborted)
{
	if (rwnx_wf_status_test_and_clear(rwnx_hw, RWNX_DEV_SCANNING)) {
		/* When the kernel detects that netdev_down occurs, the cfg80211_scan_done
		 * will be called by kernel and the wiphy will be cleared */
		if (rwnx_hw->scan_request && rwnx_hw->scan_request->wiphy) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 8, 0)
			struct cfg80211_scan_info info = {
				.aborted = scan_aborted,
			};

			cfg80211_scan_done(rwnx_hw->scan_request, &info);
#else
			cfg80211_scan_done(rwnx_hw->scan_request, scan_aborted);
#endif
			rwnx_hw->scan_request = NULL;
			up(&rwnx_hw->phy_op_sema);
		}
	}
}

/***************************************************************************
 * Messages from SCAN task
 **************************************************************************/
static inline int rwnx_rx_scan_cancel_cfm(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct scan_cancel_cfm *cfm;

	cfm = (struct scan_cancel_cfm *)msg->param;
	bes_info(BES_DBG_SCAN, "scan_cancel_cfm status %d\n", cfm->status);
	if (cfm->status != CO_OK) {
		return 0;
	}

	rwnx_ipc_buf_dealloc(rwnx_hw, &rwnx_hw->scan_ie);

	rwnx_scan_done(rwnx_hw, true);

	return 0;
}

/***************************************************************************
 * Messages from SCANU task
 **************************************************************************/
static inline int rwnx_rx_scanu_start_cfm(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct scanu_start_cfm *cfm;

	cfm = (struct scanu_start_cfm *)msg->param;
	WARN_ON(cfm->status != CO_OK);
	bes_info(BES_DBG_SCAN, "scanu_start_cfm vif %d status %d result_cnt %d\n",
		cfm->vif_idx, cfm->status, cfm->result_cnt);

	rwnx_ipc_buf_dealloc(rwnx_hw, &rwnx_hw->scan_ie);

	rwnx_scan_done(rwnx_hw, false);

	bes_regd_no_ir_update(rwnx_hw);

	return 0;
}

static inline int rwnx_rx_scanu_result_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	u64 tsf;
	u8 ssid_len;
	const u8 *ssid_ie;
	u8 ssid[IEEE80211_MAX_SSID_LEN] = {0};
	struct cfg80211_bss *bss = NULL;
	struct ieee80211_channel *chan;
	struct scanu_result_ind *ind = (struct scanu_result_ind *)msg->param;
	struct ieee80211_mgmt *mgmt = NULL;

	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);
	chan = ieee80211_get_channel(rwnx_hw->wiphy, ind->center_freq);
	if (!chan)
		return -EINVAL;

	mgmt = (struct ieee80211_mgmt *)ind->payload;

	if (GET_LOG_LVL(BES_DBG_SCAN) >= BES_LOG_DBG) {
		int len = ind->length - offsetof(struct ieee80211_mgmt, u.beacon.variable);
		ssid_ie = cfg80211_find_ie(WLAN_EID_SSID, mgmt->u.beacon.variable, len);

		if (ssid_ie) {
			ssid_len = *(ssid_ie + 1);
			if (ssid_len) {
				if (ssid_len > (sizeof(ssid) - 1))
					memcpy(ssid, ssid_ie + 2, sizeof(ssid) - 1);
				else
					memcpy(ssid, ssid_ie + 2, ssid_len);
			}
		}
	}

	/* When scanning for aps on a specific channel, aps on adjacent channels may be detected.
	   As a result, the rssi of reported aps is unstable. Therefore, if the scanned channel
	   is different from the channel where the AP resides, the scan result is discarded. */
	if (rwnx_bss_rssi_stable_get(rwnx_hw) && (ind->inst_nbr < NX_ITF_MAX)) {
		u8 *work_chan = (u8 *)ind->payload + ind->length;
		u8 ap_chan = ieee80211_frequency_to_channel(ind->center_freq);

		if (ap_chan != *work_chan) {
			if (ssid_ie)
				bes_dbg(BES_DBG_SCAN, "discard scanu result:bssid:%pM. ssid:%s. rssi:%d (ap_chan: %d != work_chan: %d)\n",
					mgmt->bssid, ssid, ind->rssi, ap_chan, *work_chan);
			return -EINVAL;
		}
	}

	bes_regd_chan_no_ir_update(rwnx_hw, chan);

	if (mgmt) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
		struct timespec ts;
		get_monotonic_boottime(&ts);
		tsf = (u64)ts.tv_sec * 1000000 + div_u64(ts.tv_nsec, 1000);
		mgmt->u.probe_resp.timestamp = ((u64)ts.tv_sec*1000000) + ts.tv_nsec/1000;
#elif LINUX_VERSION_CODE < KERNEL_VERSION(5, 6, 0)
		struct timespec ts;
		ts = ktime_to_timespec(ktime_get_boottime());
		tsf = (u64)ts.tv_sec * 1000000 + div_u64(ts.tv_nsec, 1000);
		mgmt->u.probe_resp.timestamp = tsf;
#else
		struct timespec64 ts;
		ts = ktime_to_timespec64(ktime_get_boottime());
		tsf = (u64)ts.tv_sec * 1000000 + div_u64(ts.tv_nsec, 1000);
		mgmt->u.probe_resp.timestamp = tsf;
#endif
	}

	if (chan != NULL)
		bss = cfg80211_inform_bss_frame(rwnx_hw->wiphy, chan, mgmt,
						ind->length, ind->rssi * 100, GFP_ATOMIC);

	if (bss != NULL) {
		if (GET_LOG_LVL(BES_DBG_SCAN) >= BES_LOG_DBG) {
			u8 security[ENCRYP_MODE_CHARACTER_SIZE] = {0};

			rcu_read_lock();

			bes_encryp_analyze(bss, mgmt, security);

			rcu_read_unlock();
			if (ssid_ie)
				bes_dbg(BES_DBG_SCAN,"scanu result:bssid:%pM stype:0x%02x ssid:%s, security:%s, rssi:%d\n",
					mgmt->bssid, mgmt->frame_control & IEEE80211_FCTL_STYPE, ssid, security, ind->rssi);
		}
		cfg80211_put_bss(rwnx_hw->wiphy, bss);
	}

	/* When in the scanning state, all received management frames pass by here.
	 * When scanning starts before CSA, obtain frame information here. */
	bes_dbac_scan_sta_csa(rwnx_hw, ind);

	return 0;
}
#endif /* CONFIG_RWNX_FULLMAC */

/***************************************************************************
 * Messages from ME task
 **************************************************************************/
#ifdef CONFIG_RWNX_FULLMAC
static inline int rwnx_rx_me_tkip_mic_failure_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd,
						  struct ipc_e2a_msg *msg)
{
	struct me_tkip_mic_failure_ind *ind = (struct me_tkip_mic_failure_ind *)msg->param;
	struct rwnx_vif *rwnx_vif = rwnx_rx_get_vif(rwnx_hw, ind->vif_idx);
	struct net_device *dev;

	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	if (WARN_ON(rwnx_vif == NULL))
		return -EFAULT;
	else
		dev = rwnx_vif->ndev;

	cfg80211_michael_mic_failure(dev, (u8 *)&ind->addr,
				     (ind->ga ? NL80211_KEYTYPE_GROUP : NL80211_KEYTYPE_PAIRWISE), ind->keyid,
				     (u8 *)&ind->tsc, GFP_ATOMIC);

	return 0;
}

static inline int rwnx_rx_me_tx_credits_update_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd,
						   struct ipc_e2a_msg *msg)
{
	struct me_tx_credits_update_ind *ind = (struct me_tx_credits_update_ind *)msg->param;

	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	rwnx_txq_credit_update(rwnx_hw, ind->sta_idx, ind->tid, ind->credits);

	return 0;
}

static inline int rwnx_rx_me_data_path_flushed_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd,
						   struct ipc_e2a_msg *msg)
{
	struct me_data_path_flushed_ind *ind = (struct me_data_path_flushed_ind *)msg->param;
	struct rwnx_sta *sta = rwnx_rx_get_sta(rwnx_hw, ind->sta_idx);

	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	if (WARN_ON(sta == NULL))
		return -EFAULT;

	complete(&sta->flush_cmp);

	return 0;
}
#endif /* CONFIG_RWNX_FULLMAC */

/***************************************************************************
 * Messages from SM task
 **************************************************************************/

#ifdef CONFIG_RWNX_FULLMAC
static void rwnx_rx_sm_connect_notify_cfg80211(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, struct rwnx_sta *rwnx_sta,
					       struct sm_connect_ind *ind, const u8 *req_ie, const u8 *rsp_ie,
					       uint16_t status_code)
{
	const u8 *ssid_ie;
	struct cfg80211_bss *bss = NULL;
	struct ieee80211_channel *chan = rwnx_vif->vif_chandef.chan;
	struct net_device *dev = rwnx_vif->ndev;
	u8 ch_index = rwnx_vif->ch_index;

	ssid_ie = cfg80211_find_ie(WLAN_EID_SSID, req_ie, ind->assoc_req_ie_len);
	if (ssid_ie) {
		if (ssid_ie[1]) {
			if(ssid_ie[1] > MAC_SSID_LEN - 1) {
				bes_info(BES_DBG_LINK, "ap ssid length (%d) is unexpected, change it to %d\n", ssid_ie[1], MAC_SSID_LEN - 1);
				rwnx_vif->sta.ap_ssid.length = MAC_SSID_LEN - 1;
				memcpy(rwnx_vif->sta.ap_ssid.array, ssid_ie + 2, MAC_SSID_LEN - 1);
			} else {
				rwnx_vif->sta.ap_ssid.length = ssid_ie[1];
				memcpy(rwnx_vif->sta.ap_ssid.array, ssid_ie + 2, ssid_ie[1]);
			}

			bss = cfg80211_get_bss(rwnx_hw->wiphy, chan, (const u8 *)ind->bssid.array,
						rwnx_vif->sta.ap_ssid.array, rwnx_vif->sta.ap_ssid.length,
						IEEE80211_BSS_TYPE_ANY, IEEE80211_PRIVACY_ANY);
		}
	}

	if (ind->roamed) {
		struct cfg80211_roam_info info;
		memset(&info, 0, sizeof(info));

		if (ch_index < NX_CHAN_CTXT_CNT) {
#ifdef ADPT_GKI_KERNEL
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 81))
			info.links[0].channel = rwnx_hw->chanctx_table[ch_index].chan_def.chan;
#else
			info.channel = rwnx_hw->chanctx_table[ch_index].chan_def.chan;
#endif
#else /* ADPT_GKI_KERNEL */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0))
			info.links[0].channel = rwnx_hw->chanctx_table[ch_index].chan_def.chan;
#else
			info.channel = rwnx_hw->chanctx_table[ch_index].chan_def.chan;
#endif
#endif /* ADPT_GKI_KERNEL */
		}

#ifdef ADPT_GKI_KERNEL
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 81))
		info.links[0].bssid = (const u8 *)ind->bssid.array;
#else
		info.bssid = (const u8 *)ind->bssid.array;
#endif
#else /* ADPT_GKI_KERNEL */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0))
		info.links[0].bssid = (const u8 *)ind->bssid.array;
#else
		info.bssid = (const u8 *)ind->bssid.array;
#endif
#endif /* ADPT_GKI_KERNEL */
		info.req_ie = req_ie;
		info.req_ie_len = ind->assoc_req_ie_len;
		info.resp_ie = rsp_ie;
		info.resp_ie_len = ind->assoc_rsp_ie_len;
		cfg80211_roamed(dev, &info, GFP_ATOMIC);
	} else {
		/**
		 * If REASSOCIATION failed, we use cfg80211_disconnected rather than cfg80211_connect_result,
		 * thus avoid the error that android Association request to the driver failed.
		 */
		if (ind->status_code) {
			struct sm_connect_req *last_req = (struct sm_connect_req *)rwnx_vif->sta.connect_param;
			if (last_req->flags & REASSOCIATION) {
				bes_warn(BES_DBG_LINK, "REASSOCIATION failed %d, report disconnected\n", ind->status_code);
				cfg80211_disconnected(dev, WLAN_REASON_UNSPECIFIED, NULL, 0, true, GFP_ATOMIC);
				goto out;
			}
		}

		/**
		 * In some cases(e.g. the AP changed from 2.4G to 5G), two bss with the same bssid exist,
		 * thus the upper layer may hold an old bss after cfg80211_connect_result.
		 * So we set bss via cfg80211_connect_bss and fix this problem.
		 */
		cfg80211_connect_bss(dev, (const u8 *)ind->bssid.array, bss,
				     req_ie, ind->assoc_req_ie_len,
				     rsp_ie, ind->assoc_rsp_ie_len,
				     status_code, GFP_ATOMIC,
				     NL80211_TIMEOUT_UNSPECIFIED);
	}

out:
	/* Update the conn state of STA, and blocking is not allowed during processing */
	bes_conn_state_process(rwnx_vif, rwnx_sta, STA_CONN_ACTION_CONN_IND);

	if (status_code == 0) {
		/**
		 * The Kernel may send packet after cfg80211_connect_result,
		 * so we should start tx queues as soon as possible.
		 */
		netif_tx_start_all_queues(dev);
		netif_carrier_on(dev);
	}

	bes_fw_log_fifo_dump_start(rwnx_hw, status_code);
}

static inline int rwnx_rx_sm_connect_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct sm_connect_ind *ind = (struct sm_connect_ind *)msg->param;
	struct rwnx_vif *rwnx_vif;
	const u8 *req_ie, *rsp_ie;
	uint16_t status_code;
	struct rwnx_sta *sta = NULL;

	bes_info(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	if (ind->vif_idx >= NX_ITF_MAX) {
		bes_err(BES_DBG_MSG_RX, "%s ind->vif_idx:%d error\n", __func__, ind->vif_idx);
		return -EINVAL;
	}
	rwnx_vif = rwnx_rx_get_vif(rwnx_hw, ind->vif_idx);
	if (!rwnx_vif) {
		bes_err(BES_DBG_MSG_RX, "%s ind->vif_idx:%d invalid\n", __func__, ind->vif_idx);
		return -EINVAL;
	}

	/* Retrieve IE addresses and lengths */
	req_ie = (const u8 *)ind->assoc_ie_buf;
	rsp_ie = req_ie + ind->assoc_req_ie_len;

	bes_info(BES_DBG_MSG_RX, "%s," TR_VIF_STR ",status_code=%d, task_sm_status=%d, roamed=%d\n",
		 __func__, TR_VIF(rwnx_vif), ind->status_code & 0xFFF, ((ind->status_code & 0xF000) >> 12), ind->roamed);

	/* Increases the count of event_cnt for screen-off filters */
	bes_wpa_priv_cmd_increase_event_cnt(rwnx_hw);

	/* Get the actual status_code */
	status_code = ind->status_code & 0xFFF;

	// Fill-in the AP information
	if (status_code == 0) {
		u8 txq_status;
		struct ieee80211_channel *chan;
		struct cfg80211_chan_def chandef;
		const u8 *extcap_ie;
		const struct ieee_types_extcap *extcap;

		bes_info(BES_DBG_LINK, "CONNECT_IND: bssid " TR_MAC_STR ", freq %d bw %d\n",
			 TR_MAC(&ind->bssid), ind->chan.prim20_freq, (1 << ind->chan.type) * 20);

		sta = rwnx_rx_get_sta(rwnx_hw, ind->ap_idx);
		if (WARN_ON(sta == NULL))
			return -EFAULT;

		sta->valid = true;
		sta->sta_idx = ind->ap_idx;
		sta->ch_idx = ind->ch_idx;
		sta->vif_idx = ind->vif_idx;
		sta->vlan_idx = sta->vif_idx;
		sta->qos = ind->qos;
		sta->acm = ind->acm;
		sta->ps.active = false;
		sta->aid = ind->aid;
		sta->band = ind->chan.band;
		sta->width = ind->chan.type;
		sta->center_freq = ind->chan.prim20_freq;
		sta->center_freq1 = ind->chan.center1_freq;
		sta->center_freq2 = ind->chan.center2_freq;
		sta->stats.connected_start_time = ktime_get();
		sta->ht = (rwnx_mod_params.ht_on && cfg80211_find_ie(WLAN_EID_HT_OPERATION, rsp_ie, ind->assoc_rsp_ie_len)) ? 1 : 0;
		sta->vht = (rwnx_mod_params.vht_on && cfg80211_find_ie(WLAN_EID_VHT_OPERATION, rsp_ie, ind->assoc_rsp_ie_len)) ? 1 : 0;
		sta->he = (rwnx_mod_params.he_on && cfg80211_find_ext_ie(WLAN_EID_EXT_HE_OPERATION, rsp_ie, ind->assoc_rsp_ie_len)) ? 1 : 0;
		sta->twt_ind.sta_idx = RWNX_INVALID_STA;
		rwnx_vif->sta.ap = sta;
		rwnx_vif->generation++;

		/* Update the conn state of STA */
		bes_conn_state_process(rwnx_vif, sta, STA_CONN_ACTION_INIT);

		/* Get Security Information */
		bes_get_security_info(rwnx_vif, req_ie, ind->assoc_req_ie_len);

		chan = ieee80211_get_channel(rwnx_hw->wiphy, ind->chan.prim20_freq);
		cfg80211_chandef_create(&chandef, chan, NL80211_CHAN_NO_HT);
		if (!rwnx_hw->mod_params->ht_on)
			chandef.width = NL80211_CHAN_WIDTH_20_NOHT;
		else
			chandef.width = chnl2bw[ind->chan.type];
		chandef.center_freq1 = ind->chan.center1_freq;
		chandef.center_freq2 = ind->chan.center2_freq;
		rwnx_chanctx_link(rwnx_vif, ind->ch_idx, &chandef);

		rwnx_vif->vif_chandef = chandef;
		memcpy(sta->mac_addr, ind->bssid.array, ETH_ALEN);

		if (ind->ch_idx == rwnx_hw->cur_chanctx) {
			txq_status = 0;
		} else {
			txq_status = (1 << RWNX_TXQ_STOP_CHAN);
		}
		memcpy(sta->ac_param, ind->ac_param, sizeof(sta->ac_param));
		rwnx_txq_sta_init(rwnx_hw, sta, txq_status);
		rwnx_txq_tdls_vif_init(rwnx_vif);
		rwnx_mu_group_sta_init(sta, NULL);
		/* Look for TDLS Channel Switch Prohibited flag in the Extended Capability
		 * Information Element*/
		extcap_ie = cfg80211_find_ie(WLAN_EID_EXT_CAPABILITY, rsp_ie, ind->assoc_rsp_ie_len);
		if (extcap_ie && extcap_ie[1] >= 5) {
			extcap = (void *)(extcap_ie);
			rwnx_vif->tdls_chsw_prohibited = extcap->ext_capab[4] & WLAN_EXT_CAPA5_TDLS_CH_SW_PROHIBITED;
		}

#ifdef CONFIG_RWNX_BFMER
		/* If Beamformer feature is activated, check if features can be used
		 * with the new peer device
		 */
		if (rwnx_hw->mod_params->bfmer) {
			const u8 *vht_capa_ie;
			const struct ieee80211_vht_cap *vht_cap;

			do {
				/* Look for VHT Capability Information Element */
				vht_capa_ie = cfg80211_find_ie(WLAN_EID_VHT_CAPABILITY, rsp_ie, ind->assoc_rsp_ie_len);

				/* Stop here if peer device does not support VHT */
				if (!vht_capa_ie) {
					break;
				}

				vht_cap = (const struct ieee80211_vht_cap *)(vht_capa_ie + 2);

				/* Send MM_BFMER_ENABLE_REQ message if needed */
				rwnx_send_bfmer_enable(rwnx_hw, sta, vht_cap);
			} while (0);
		}
#endif //(CONFIG_RWNX_BFMER)

#ifdef CONFIG_RWNX_MON_DATA
		// If there are 1 sta and 1 monitor interface active at the same time then
		// monitor interface channel context is always the same as the STA interface.
		// This doesn't work with 2 STA interfaces but we don't want to support it.
		if (rwnx_hw->monitor_vif != RWNX_INVALID_VIF) {
			struct rwnx_vif *rwnx_mon_vif = rwnx_hw->vif_table[rwnx_hw->monitor_vif];
			if (rwnx_mon_vif) {
				rwnx_chanctx_unlink(rwnx_mon_vif);
				rwnx_chanctx_link(rwnx_mon_vif, ind->ch_idx, NULL);
			}
		}
#endif

		rwnx_rx_sm_connect_notify_cfg80211(rwnx_hw, rwnx_vif, sta, ind, req_ie, rsp_ie, status_code);
	}

	return rwnx_queue_deferred_e2amsg_work(rwnx_hw, msg, rwnx_vif, sta);
}

int rwnx_defer_sm_connect_ind(struct rwnx_hw *rwnx_hw, struct rwnx_deferred_msg *deferred_msg)
{
	struct ipc_e2a_msg *msg = &(deferred_msg->msg);
	struct rwnx_vif *rwnx_vif = deferred_msg->vif;
	struct rwnx_sta *sta = deferred_msg->sta;
	struct sm_connect_ind *ind = (struct sm_connect_ind *)msg->param;
	const u8 *req_ie, *rsp_ie;
	const u8 *supp_rate_ie;
	u8 i, is_11b_only = 1;
	uint16_t status_code;

	bes_info(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	if (ind->vif_idx >= NX_ITF_MAX) {
		bes_err(BES_DBG_MSG_RX, "%s ind->vif_idx:%d error\n", __func__, ind->vif_idx);
		return -EINVAL;
	}
	if (!rwnx_vif) {
		bes_err(BES_DBG_MSG_RX, "%s ind->vif_idx:%d rwnx_vif is invalid\n", __func__, ind->vif_idx);
		return -EINVAL;
	}

	/* Retrieve IE addresses and lengths */
	req_ie = (const u8 *)ind->assoc_ie_buf;
	rsp_ie = req_ie + ind->assoc_req_ie_len;

	/* Get the actual status_code */
	status_code = ind->status_code & 0xFFF;

	// Fill-in the AP information
	if (status_code == 0) {
		// AP information has been filled in rwnx_rx_sm_connect_ind
		WARN_ON(sta == NULL);

		/* Init the parameters of tx/rx statistics */
		bes_sta_txrx_rate_stat_init(rwnx_hw, sta);

		/* Register debugfs for sta */
		rwnx_dbgfs_register_sta(rwnx_hw, sta);

		/* Look for support_IEEE80211b flag in the Support Rate Information Element*/
		supp_rate_ie = cfg80211_find_ie(WLAN_EID_SUPP_RATES, rsp_ie, ind->assoc_rsp_ie_len);
		if (supp_rate_ie && supp_rate_ie[1] < 5) {
			for (i = 0; i < supp_rate_ie[1]; i++) {
				int rate = supp_rate_ie[i + 2] > 0x80 ? (supp_rate_ie[i + 2] - 0x80) : supp_rate_ie[i + 2];
				if ( rate != 2 && rate != 4 && rate != 11 && rate != 22) {
					is_11b_only = 0;
					break;
				}
			}
			rwnx_send_11b_only_mode(rwnx_hw, is_11b_only);
		} else {
			is_11b_only = 0;
			rwnx_send_11b_only_mode(rwnx_hw, is_11b_only);
		}

		/* set activity management parameters */
		bes_set_vif_ext_param(rwnx_vif, true);
	} else {
		/**
		 * In some corner cases, such as when a STA connects to a WPA3 AP,
		 * a bus issue may stop the auth frame from being sent to the LMAC.
		 * This can cause a connection timeout, making the driver call
		 * rwnx_external_auth_enable twice by mistake, leading to problems.
		 * To fix this, calling rwnx_external_auth_disable during the connection
		 * failure can stop the repeated calls.
		 */
		rwnx_external_auth_disable(rwnx_vif);

		if (!rwnx_vif->sta.conn_abort) {
			int ret = rwnx_reconnect_req(rwnx_vif, ind);
			if (ret == 0) // send reconnect_req ok, do not report connection failure
				return ret;
		}

		rwnx_rx_sm_connect_notify_cfg80211(rwnx_hw, rwnx_vif, sta, ind, req_ie, rsp_ie, status_code);
	}
	bes_dbg(BES_DBG_MSG_RX, "assoc req_ie_len=%d rsp_ie_len=%d.\n", ind->assoc_req_ie_len, ind->assoc_rsp_ie_len);

	/* Check whether it is necessary to deregister the calibration result and switch step 4. */
	bes_dbac_cali_check(rwnx_hw);

	return 0;
}

static inline int rwnx_rx_sm_disconnect_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct sm_disconnect_ind *ind = (struct sm_disconnect_ind *)msg->param;
	struct rwnx_vif *rwnx_vif = NULL;
	struct net_device *dev = NULL;
	struct rwnx_sta *ap_sta = NULL;

	bes_info(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	if (ind->vif_idx >= NX_ITF_MAX) {
		bes_err(BES_DBG_MSG_RX, "invalid vif_idx:%d\n", ind->vif_idx);
		return -EINVAL;
	}

	rwnx_vif = rwnx_rx_get_vif(rwnx_hw, ind->vif_idx);
	if (!rwnx_vif) {
		bes_err(BES_DBG_MSG_RX, "%s, vif(idx: %d) is null\n", __func__, ind->vif_idx);
		return -EINVAL;
	}

	dev = rwnx_vif->ndev;
	ap_sta = rwnx_vif->sta.ap;
	if (!ap_sta) {
		bes_warn(BES_DBG_MSG_RX, TR_VIF_STR " is already disconnected\n", TR_VIF(rwnx_vif));
		return -EFAULT;
	}

	/* Set ap station to invalid first.
	 * When rwnx_cfg80211_del_key comes after cfg80211_disconnected, we may del sta->key while the sta is already deleted,
	 * thus lmac may crash.
	 */
	ap_sta->valid = false;
	rwnx_vif->sta.ap = NULL;
	rwnx_vif->generation++;
	ap_sta->twt_ind.sta_idx = RWNX_INVALID_STA;

	/* Increases the count of event_cnt for screen-off filters */
	bes_wpa_priv_cmd_increase_event_cnt(rwnx_hw);

	/* if vif is not up, rwnx_close has already been called */
	if (rwnx_vif->up) {
		if (!ind->reassoc) {
			/* The driver should not notify wpa_supplicant the deauth/disconnect during the WNM process */
			if(RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION && rwnx_vif->sta.wnm_process)
				bes_info(BES_DBG_MSG_RX, "%s, deauth do not report during the WNM process\n", __func__);
			else
				cfg80211_disconnected(dev, ind->reason_code, NULL, 0, (ind->reason_code <= 1), GFP_ATOMIC);

			if (rwnx_vif->sta.ft_assoc_ies) {
				kfree(rwnx_vif->sta.ft_assoc_ies);
				rwnx_vif->sta.ft_assoc_ies = NULL;
				rwnx_vif->sta.ft_assoc_ies_len = 0;
			}
		}
		netif_tx_stop_all_queues(dev);
		netif_carrier_off(dev);
	}

#ifdef CONFIG_RWNX_BFMER
	/* Disable Beamformer if supported */
	rwnx_bfmer_report_del(rwnx_hw, ap_sta);
#endif //(CONFIG_RWNX_BFMER)

	rwnx_txq_sta_deinit(rwnx_hw, ap_sta);
	rwnx_txq_tdls_vif_deinit(rwnx_vif);
	rwnx_reset_sta_stats(ap_sta);
	bes_tp_dbg_reset_tx_rx_info(rwnx_hw, ap_sta);
	rwnx_external_auth_disable(rwnx_vif);

	rwnx_chanctx_unlink(rwnx_vif);

	rwnx_vif->sta.ap_ssid.length = 0;
	memset(rwnx_vif->sta.ap_ssid.array, 0, sizeof(rwnx_vif->sta.ap_ssid.array));
	memset(&rwnx_vif->vif_chandef, 0, sizeof(struct cfg80211_chan_def));

	return rwnx_queue_deferred_e2amsg_work(rwnx_hw, msg, rwnx_vif, ap_sta);
}

static inline int rwnx_defer_sm_disconnect_ind(struct rwnx_hw *rwnx_hw, struct rwnx_deferred_msg *deferred_msg)
{
	struct ipc_e2a_msg *msg = &(deferred_msg->msg);
	struct rwnx_vif *rwnx_vif = deferred_msg->vif;
	struct rwnx_sta *sta = deferred_msg->sta;
	struct sm_disconnect_ind *ind = (struct sm_disconnect_ind *)msg->param;

	bes_info(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	if (ind->vif_idx >= NX_ITF_MAX) {
		bes_err(BES_DBG_MSG_RX, "%s ind->vif_idx:%d error\n", __func__, ind->vif_idx);
		return -EINVAL;
	}
	if (!rwnx_vif) {
		bes_err(BES_DBG_MSG_RX, "%s vifind->vif_idx:%d rwnx_vif is invalid\n", __func__, ind->vif_idx);
		return -EINVAL;
	}

	bes_info(BES_DBG_MSG_RX, "%s," TR_VIF_STR ",rwnx_vif->up=%d,reassoc=%d,reason_code=0x%x\n", __func__,
		 TR_VIF(rwnx_vif), rwnx_vif->up, ind->reassoc, ind->reason_code);

	/* Unregister debugfs for sta */
	rwnx_dbgfs_unregister_sta(rwnx_hw, sta);

	/* Deinit the parameters of tx/rx statistics */
	bes_sta_txrx_rate_stat_deinit(rwnx_hw, sta);

	/* Update the conn state of STA */
	bes_conn_state_process(rwnx_vif, sta, STA_CONN_ACTION_DEINIT);

	if (rwnx_narrow_ru_filter_acpt_all_beacon(rwnx_hw))
		rwnx_narrow_ru_filter_reset(rwnx_hw);

	/* Clear stored key and skb */
	bes_eapol_sta_stored_key_deinit(rwnx_vif);

	/* Delete arp/gtk offload function */
	bes_filter_manager_ctrl_gtk_rekey_offload_filter(rwnx_hw, rwnx_vif, false);
	if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION ||
	    RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_CLIENT) {
		if (!ind->reassoc) {
			rwnx_vif->sta.gateway_ip = 0;
			if (rwnx_hw->filter.arp_offload[ind->vif_idx])
				bes_filter_manager_clear_arp_offload(rwnx_hw, rwnx_vif);
			if (rwnx_hw->filter.ns_offload[ind->vif_idx])
				bes_filter_manager_clear_ns_offload(rwnx_hw, rwnx_vif);
			if (rwnx_hw->filter.signal_test_filter[ind->vif_idx])
				bes_signal_test_clear_filter(rwnx_hw, rwnx_vif);
		}
	}

	/* During roaming, this check is performed during the connection indication process. */
	if (!ind->reassoc)
		bes_dbac_cali_check(rwnx_hw);

	/* set activity management parameters */
	bes_set_vif_ext_param(rwnx_vif, false);

	/* Update the conn state of STA when not roaming */
	if (!ind->reassoc)
		bes_conn_state_process(rwnx_vif, sta, STA_CONN_ACTION_DISCONN_IND);

	// Only up phy_op_sema when RWNX_DEV_DISCONNECTING
	// because the fw may report SM_DISCONNECT_IND voluntarily, e.g. bss lost/rx disassoc.
	if (rwnx_wf_status_test_and_clear(rwnx_hw, RWNX_DEV_DISCONNECTING)) {
		bes_tx_loop_del_specific_ind_timer(rwnx_hw->plat, SM_DISCONNECT_REQ);
		up(&rwnx_hw->phy_op_sema);
	}

	/* When CSA is in progress, wait for its end */
	bes_dbac_disconn_while_csa(rwnx_vif);

	return 0;
}

static inline int rwnx_rx_sm_external_auth_required_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd,
							struct ipc_e2a_msg *msg)
{
	struct sm_external_auth_required_ind *ind = (struct sm_external_auth_required_ind *)msg->param;
	struct rwnx_vif *rwnx_vif = rwnx_rx_get_vif(rwnx_hw, ind->vif_idx);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
	struct net_device *dev;
	struct cfg80211_external_auth_params params = {0};

	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	if (WARN_ON(rwnx_vif == NULL))
		return -EFAULT;

	dev = rwnx_vif->ndev;
	params.action = NL80211_EXTERNAL_AUTH_START;
	memcpy(params.bssid, ind->bssid.array, ETH_ALEN);
	params.ssid.ssid_len = ind->ssid.length;
	memcpy(params.ssid.ssid, ind->ssid.array, min_t(size_t, ind->ssid.length, sizeof(params.ssid.ssid)));
	params.key_mgmt_suite = ind->akm;

	if ((ind->vif_idx > NX_VIRT_DEV_MAX) || !rwnx_vif->up || (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_STATION) ||
	    cfg80211_external_auth_request(dev, &params, GFP_ATOMIC)) {
		wiphy_err(rwnx_hw->wiphy, "Failed to start external auth on vif %d", ind->vif_idx);
		rwnx_send_sm_external_auth_required_rsp(rwnx_hw, rwnx_vif, WLAN_STATUS_UNSPECIFIED_FAILURE);
		return 0;
	}

	rwnx_external_auth_enable(rwnx_vif);
#else
	if (WARN_ON(rwnx_vif == NULL))
		return -EFAULT;

	rwnx_send_sm_external_auth_required_rsp(rwnx_hw, rwnx_vif, WLAN_STATUS_UNSPECIFIED_FAILURE);
#endif
	return 0;
}

static inline int rwnx_rx_sm_ft_auth_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct sm_ft_auth_ind *ind = (struct sm_ft_auth_ind *)msg->param;
	struct rwnx_vif *rwnx_vif = rwnx_rx_get_vif(rwnx_hw, ind->vif_idx);
	struct sk_buff *skb;
	size_t data_len = (offsetof(struct ieee80211_mgmt, u.auth.variable) + ind->ft_ie_len);

	if (WARN_ON(rwnx_vif == NULL))
		return -EFAULT;

	skb = dev_alloc_skb(data_len);
	if (skb) {
		struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb_put(skb, data_len);
		mgmt->frame_control = cpu_to_le16(IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_AUTH);
		memcpy(mgmt->u.auth.variable, ind->ft_ie_buf, ind->ft_ie_len);
		rwnx_rx_defer_skb(rwnx_hw, rwnx_vif, skb);
		dev_kfree_skb(skb);
	} else {
		netdev_warn(rwnx_vif->ndev, "Allocation failed for FT auth ind\n");
	}

	return 0;
}

static inline int rwnx_rx_sm_mgt_frame_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct sm_mgt_frame_ind *ind = (struct sm_mgt_frame_ind *)msg->param;

	bes_dbg_mgt_trace(rwnx_hw, ind);

	return 0;
}

/***************************************************************************
 * Messages from TWT task
 **************************************************************************/
static inline int rwnx_rx_twt_setup_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct twt_setup_ind *ind = (struct twt_setup_ind *)msg->param;
	struct rwnx_sta *rwnx_sta = rwnx_rx_get_sta(rwnx_hw, ind->sta_idx);

	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	if (WARN_ON(rwnx_sta == NULL))
		return -EFAULT;

	memcpy(&rwnx_sta->twt_ind, ind, sizeof(struct twt_setup_ind));

	return 0;
}

static inline int rwnx_rx_mesh_path_create_cfm(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct mesh_path_create_cfm *cfm = (struct mesh_path_create_cfm *)msg->param;
	struct rwnx_vif *rwnx_vif = rwnx_rx_get_vif(rwnx_hw, cfm->vif_idx);

	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	/* Check we well have a Mesh Point Interface */
	if (rwnx_vif && (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_MESH_POINT))
		rwnx_vif->ap.flags &= ~RWNX_AP_CREATE_MESH_PATH;

	return 0;
}

static inline int rwnx_rx_mesh_peer_update_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct mesh_peer_update_ind *ind = (struct mesh_peer_update_ind *)msg->param;
	struct rwnx_vif *rwnx_vif = rwnx_rx_get_vif(rwnx_hw, ind->vif_idx);
	struct rwnx_sta *rwnx_sta = rwnx_rx_get_sta(rwnx_hw, ind->sta_idx);

	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	if (WARN_ON(rwnx_vif == NULL))
		return -EFAULT;

	if (WARN_ON(rwnx_sta == NULL))
		return -EFAULT;

	if ((ind->vif_idx >= NX_VIRT_DEV_MAX) || (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT) ||
	    (ind->sta_idx >= NX_REMOTE_STA_MAX))
		return -EFAULT;

	if (rwnx_vif->ap.flags & RWNX_AP_USER_MESH_PM) {
		if (!ind->estab && rwnx_sta->valid) {
			/* There is no way to inform upper layer for lost of peer, still
			   clean everything in the driver */
			rwnx_sta->ps.active = false;
			rwnx_sta->valid = false;
			rwnx_sta->twt_ind.sta_idx = RWNX_INVALID_STA;

			/* Remove the station from the list of VIF's station */
			list_del_init(&rwnx_sta->list);

			rwnx_txq_sta_deinit(rwnx_hw, rwnx_sta);
			rwnx_reset_sta_stats(rwnx_sta);
			bes_tp_dbg_reset_tx_rx_info(rwnx_hw, rwnx_sta);
		} else {
			WARN_ON(0);
		}
	} else {
		/* Check if peer link has been established or lost */
		if (ind->estab) {
			if (!rwnx_sta->valid) {
				u8 txq_status;

				rwnx_sta->valid = true;
				rwnx_sta->sta_idx = ind->sta_idx;
				rwnx_sta->ch_idx = rwnx_vif->ch_index;
				rwnx_sta->vif_idx = ind->vif_idx;
				rwnx_sta->vlan_idx = rwnx_sta->vif_idx;
				rwnx_sta->ps.active = false;
				rwnx_sta->qos = true;
				rwnx_sta->aid = ind->sta_idx + 1;
				rwnx_sta->twt_ind.sta_idx = RWNX_INVALID_STA;
				// rwnx_sta->acm = ind->acm;
				memcpy(rwnx_sta->mac_addr, ind->peer_addr.array, ETH_ALEN);

				rwnx_chanctx_link(rwnx_vif, rwnx_sta->ch_idx, NULL);

				/* Add the station in the list of VIF's stations */
				INIT_LIST_HEAD(&rwnx_sta->list);
				list_add_tail(&rwnx_sta->list, &rwnx_vif->ap.sta_list);

				/* Initialize the TX queues */
				if (rwnx_sta->ch_idx == rwnx_hw->cur_chanctx) {
					txq_status = 0;
				} else {
					txq_status = (1 << RWNX_TXQ_STOP_CHAN);
				}

				rwnx_txq_sta_init(rwnx_hw, rwnx_sta, txq_status);

#ifdef CONFIG_RWNX_BFMER
				// TODO: update indication to contains vht capabilties
				if (rwnx_hw->mod_params->bfmer)
					rwnx_send_bfmer_enable(rwnx_hw, rwnx_sta, NULL);

				rwnx_mu_group_sta_init(rwnx_sta, NULL);
#endif /* CONFIG_RWNX_BFMER */

			} else {
				WARN_ON(0);
			}
		} else {
			if (rwnx_sta->valid) {
				rwnx_sta->ps.active = false;
				rwnx_sta->valid = false;
				rwnx_sta->twt_ind.sta_idx = RWNX_INVALID_STA;

				/* Remove the station from the list of VIF's station */
				list_del_init(&rwnx_sta->list);

				rwnx_txq_sta_deinit(rwnx_hw, rwnx_sta);
				rwnx_reset_sta_stats(rwnx_sta);
				bes_tp_dbg_reset_tx_rx_info(rwnx_hw, rwnx_sta);
			} else {
				WARN_ON(0);
			}
		}
	}

	return rwnx_queue_deferred_e2amsg_work(rwnx_hw, msg, rwnx_vif, rwnx_sta);
}

static inline int rwnx_defer_mesh_peer_update_ind(struct rwnx_hw *rwnx_hw, struct rwnx_deferred_msg *deferred_msg)
{
	struct ipc_e2a_msg *msg = &(deferred_msg->msg);
	struct rwnx_vif *rwnx_vif = deferred_msg->vif;
	struct rwnx_sta *rwnx_sta =  deferred_msg->sta;
	struct mesh_peer_update_ind *ind = (struct mesh_peer_update_ind *)msg->param;

	if (WARN_ON(rwnx_vif == NULL))
		return -EFAULT;

	if (WARN_ON(rwnx_sta == NULL))
		return -EFAULT;

	if ((ind->vif_idx >= NX_VIRT_DEV_MAX) || (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT) ||
	    (ind->sta_idx >= NX_REMOTE_STA_MAX))
		return -EFAULT;

	if (rwnx_vif->ap.flags & RWNX_AP_USER_MESH_PM) {
		if (!ind->estab && rwnx_sta->valid) {
			rwnx_dbgfs_unregister_sta(rwnx_hw, rwnx_sta);
			bes_sta_txrx_rate_stat_deinit(rwnx_hw, rwnx_sta);
		}
	} else {
		/* Check if peer link has been established or lost */
		if (ind->estab) {
			if (!rwnx_sta->valid) {
				bes_sta_txrx_rate_stat_init(rwnx_hw, rwnx_sta);
				rwnx_dbgfs_register_sta(rwnx_hw, rwnx_sta);
			}
		} else {
			if (rwnx_sta->valid) {
				rwnx_dbgfs_unregister_sta(rwnx_hw, rwnx_sta);
				bes_sta_txrx_rate_stat_deinit(rwnx_hw, rwnx_sta);
			}
		}
	}

	return 0;
}

static void rwnx_dbg_phy_err_info_analyze(struct mm_diag_reg_ind *ind)
{
	int i = 0;
	struct phy_err_dbg *phy_err_dbg = (struct phy_err_dbg *)((u8 *)ind +
		sizeof(struct mm_diag_reg_ind) + ind->diag_reg.dbg_reg_num * sizeof(struct dbg_reg_stats));
	struct tx_policy_tbl *tx_pol = &phy_err_dbg->tx_pol;

	bes_warn(BES_DBG_MSG_RX, "AC: %u, is_ampdu: %d, phy_err_cnt=%u, available_len=%u\n",
		 phy_err_dbg->ac, phy_err_dbg->ampdu, phy_err_dbg->phy_err_cnt, phy_err_dbg->available_len);

	if (phy_err_dbg->ampdu) {
		bes_warn(BES_DBG_MSG_RX, "curr_cnt=%u, phy_flags=0x%x", phy_err_dbg->curr_cnt, phy_err_dbg->phy_flags);
	}

	bes_warn(BES_DBG_MSG_RX, "frmlen=%u, phyctrlinfo=0x%x, macctrlinfo1=0x%x, macctrlinfo2=0x%x\n",
		 phy_err_dbg->frmlen, phy_err_dbg->phyctrlinfo, phy_err_dbg->macctrlinfo1, phy_err_dbg->macctrlinfo2);

	bes_warn(BES_DBG_MSG_RX, "tx policy tbl dump start\nupatterntx=%u", tx_pol->upatterntx);

	bes_warn(BES_DBG_MSG_RX, "phyctrlinfo_1=0x%x, phyctrlinfo_2=0x%x\n",
		 tx_pol->phyctrlinfo_1.value, tx_pol->phyctrlinfo_2.value);

	bes_warn(BES_DBG_MSG_RX, "macctrlinfo_1=0x%x, macctrlinfo_2=0x%x\n",
		 tx_pol->macctrlinfo_1.value, tx_pol->macctrlinfo_2.value);

	for (i = 0; i < NX_TX_MAX_RATES; i++) {
		bes_warn(BES_DBG_MSG_RX, "ratecntrlinfo[%d]: mcs=%u, bw=%u, gi_and_pretype=%u, format=%u, dcm=%u\n",
			 i, tx_pol->ratectrlinfos[i].mcsIndexTx, tx_pol->ratectrlinfos[i].bwTx,
			 tx_pol->ratectrlinfos[i].giAndPreTypeTx, tx_pol->ratectrlinfos[i].formatModTx,
			 tx_pol->ratectrlinfos[i].dcmTx);
		bes_warn(BES_DBG_MSG_RX, "powercntrlinfo[%d]: tx_pwr_pt=%u, tx_pwr_prot_pt=%u\n",
			 i, tx_pol->powerctrlinfos[i].txPwrLevelPT, tx_pol->powerctrlinfos[i].txPwrLevelProtPT);
	}

	bes_warn(BES_DBG_MSG_RX, "tx policy tbl dump end\n");
}

void rwnx_dbg_vif_info_analyze(struct rwnx_hw *rwnx_hw, struct mm_diag_reg_ind *ind)
{
	const char *ind_type[] = {
		"DIAG_REG_IND_NORMAL",
		"DIAG_REG_IND_BEACON_LOST",
		"DIAG_REG_IND_WAIT_TIMEOUT",
		"DIAG_REG_IND_ASSERT_RECOVERY",
		"DIAG_REG_IND_PHY_ERR"
	};

	bes_warn(BES_DBG_MSG_RX, "ind_type: %s start dump\n", ind_type[ind->ind_type]);

	if (ind->ind_type == DIAG_REG_IND_NORMAL) {
		struct diag_normal_pram *normal = (struct diag_normal_pram *)&ind->diag_param[0];

		bes_warn(BES_DBG_MSG_RX, "vif_index: %u\n", normal->vif_index);
	} else if (ind->ind_type == DIAG_REG_IND_BEACON_LOST) {
		struct diag_beacon_lost_pram *beacon_lost = (struct diag_beacon_lost_pram *)&ind->diag_param[0];

		bes_warn(BES_DBG_MSG_RX, "vif_index: %u, beacon_lost_nm: %u, off_chan_num: %u\n",
			 beacon_lost->vif_index, beacon_lost->beacon_lost_nm,
			 beacon_lost->off_chan_num);
	} else if (ind->ind_type == DIAG_REG_IND_WAIT_TIMEOUT) {
		struct diag_wait_timeout_pram *wait_timeout = (struct diag_wait_timeout_pram *)&ind->diag_param[0];

		bes_warn(BES_DBG_MSG_RX, "lmac wait time out in location: 0x%08x\n", wait_timeout->location);
	} else if (ind->ind_type == DIAG_REG_IND_ASSERT_RECOVERY) {
		bes_warn(BES_DBG_MSG_RX, "assert recovery occurs\n");
	} else if (ind->ind_type == DIAG_REG_IND_PHY_ERR) {
		bes_warn(BES_DBG_MSG_RX, "assert recovery, phy err occurs\n");

		rwnx_dbg_phy_err_info_analyze(ind);
	}
}

void rwnx_dbg_diag_info_analyze(struct rwnx_hw *rwnx_hw, struct diag_reg_dump *diag_reg)
{
	int i;
	u32 total_time = 0;
	u8 coex_mode_prop[COEX_MODE_CONFIG_UNVILD] = {0};
	struct coex_tp_info *coex_tp;

	bes_warn(BES_DBG_MSG_RX, "diag mac: discard_wep=%d fcs_err=%d phy_err=%d fifo_over=%d tx_under=%d MPIF_over=%d\n",
		 diag_reg->dot11_wep_excluded_count, diag_reg->dot11_fcs_error_count, diag_reg->rw_rx_phy_error_count,
		 diag_reg->rw_rd_fifo_overflow_count, diag_reg->rw_tx_underrun_count, diag_reg->rw_rx_mpif_overflow_count);

	bes_warn(BES_DBG_MSG_RX, "diag mac: rx [uampdu=%d gampdu=%d o_ampdu=%d mpdu=%d ampdu_dlm=%d]\n",
		 diag_reg->rw_uampdu_received_count, diag_reg->rw_gampdu_received_count,
		 diag_reg->rw_other_ampdu_received_count, diag_reg->dot11_mpdu_in_received_ampdu_count,
		 diag_reg->dot11_ampdu_delimiter_crc_error_count);

	bes_warn(BES_DBG_MSG_RX, "diag mac: tx [20M[fail=%d suc=%d] 40M[fail=%d suc=%d] 80M[fail=%d suc=%d]]\n",
		 diag_reg->rw_failed_20mhz_txop, diag_reg->rw_successful_20mhz_txop,
		 diag_reg->rw_failed_40mhz_txop, diag_reg->rw_successful_40mhz_txop,
		 diag_reg->rw_failed_80mhz_txop, diag_reg->rw_successful_80mhz_txop);

	bes_warn(BES_DBG_MSG_RX, "diag rx: rx_ctl(filter)=0x%08x rx_buf_check=%d, txrx_int=0x%08x\n",
		 diag_reg->rx_ctl_filter, diag_reg->rx_buf_check, diag_reg->txrx_int);

	if (diag_reg->ipc_rxbuf_print) {
		bes_warn(BES_DBG_MSG_RX, "diag rx2: rx_idx:%d, idx buf: 0x%x 0x%x\n",
			 diag_reg->ipc_rxbuf_idx, diag_reg->dma_idx_buf0, diag_reg->dma_idx_buf31);
	}

	bes_warn(BES_DBG_MSG_RX, "rf_reg_202: %04x, rf_reg_3043: %04x\n",
		 diag_reg->rf_reg_202, diag_reg->rf_reg_3043);

	bes_warn(BES_DBG_MSG_RX, "tpc cur freq: %u\n", diag_reg->center_freq);

	bes_warn(BES_DBG_MSG_RX, "chan_mode: %u, del_time: %u, transq_stat: %d\n",
		 diag_reg->dbg_coex_stats.chan_mode, diag_reg->dbg_coex_stats.del_time, diag_reg->dbg_coex_stats.transq_stat);

	/* Calculate proportion of coex mode */
	coex_tp = &diag_reg->dbg_coex_stats.coex_tp;
	for (i = 0; i < COEX_MODE_CONFIG_UNVILD; i++)
		total_time += coex_tp->prop_calc[i].total_time;
	if (total_time != 0) {
		for (i = 0; i < COEX_MODE_CONFIG_UNVILD; i++)
			coex_mode_prop[i] = (u8)div_u64(coex_tp->prop_calc[i].total_time * 100, total_time);
	}

	bes_warn(BES_DBG_MSG_RX, "coex proportion, block_bt=%d%% unblock_bt:%d%% fdd:%d%%. Toatl time:%u\n",
		 coex_mode_prop[COEX_MODE_CONFIG_BLOCKBT], coex_mode_prop[COEX_MODE_CONFIG_UNBLOCKBT],
		 coex_mode_prop[COEX_MODE_CONFIG_FDD], total_time);

	bes_warn(BES_DBG_MSG_RX, "tansq count, sucess=%u fail=%u.\n",
		 coex_tp->transq_stats.sucess, coex_tp->transq_stats.fail);
}

void rwnx_dbg_reg_info_analyze(struct rwnx_hw *rwnx_hw, struct diag_reg_dump *diag_reg)
{
	int i;

	for (i = 0; i < diag_reg->dbg_reg_num; i++) {
		bes_warn(BES_DBG_MSG_RX, "reg[%d]:0x%08x=0x%08x\n",
			 i, diag_reg->dbg_reg[i].reg, diag_reg->dbg_reg[i].val);
	}

	bes_warn(BES_DBG_MSG_RX, "%s, end dump\n", __func__);
}

static inline int rwnx_defer_mm_diag_reg_ind(struct rwnx_hw *rwnx_hw, struct rwnx_deferred_msg *deferred_msg)
{
	struct ipc_e2a_msg *msg = &(deferred_msg->msg);
	struct mm_diag_reg_ind *ind = (struct mm_diag_reg_ind *)msg->param;
	static unsigned long caller_jiffies;

	if (ind->ind_type >= DIAG_REG_IND_MAX) {
		bes_warn(BES_DBG_MSG_RX, "%s, ind_type: %d is unexcepted\n", __func__, ind->ind_type);
		return -EFAULT;
	}

	/* Only print diag reg info when the interval is greater than the minimum value */
	if (!printk_timed_ratelimit(&caller_jiffies, BES_DBG_DIAG_REG_PRINT_MIN_INTERVAL))
		return 0;

	rwnx_dbg_vif_info_analyze(rwnx_hw, ind);
	rwnx_dbg_diag_info_analyze(rwnx_hw, &ind->diag_reg);
	rwnx_dbg_reg_info_analyze(rwnx_hw, &ind->diag_reg);

	return 0;
}

static inline int rwnx_rx_mesh_path_update_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct mesh_path_update_ind *ind = (struct mesh_path_update_ind *)msg->param;
	struct rwnx_vif *rwnx_vif = rwnx_rx_get_vif(rwnx_hw, ind->vif_idx);
	struct rwnx_mesh_path *mesh_path;
	bool found = false;

	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	if (ind->vif_idx >= NX_VIRT_DEV_MAX)
		return 1;

	if (!rwnx_vif || (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT))
		return 0;

	/* Look for path with provided target address */
	list_for_each_entry(mesh_path, &rwnx_vif->ap.mpath_list, list) {
		if (mesh_path->path_idx == ind->path_idx) {
			found = true;
			break;
		}
	}

	/* Check if element has been deleted */
	if (ind->delete) {
		if (found) {
			trace_mesh_delete_path(mesh_path);
			/* Remove element from list */
			list_del_init(&mesh_path->list);
			/* Free the element */
			kfree(mesh_path);
		}
	} else {
		if (found) {
			// Update the Next Hop STA
			struct rwnx_sta *rwnx_sta = rwnx_rx_get_sta(rwnx_hw, ind->nhop_sta_idx);
			if (!WARN_ON(rwnx_sta == NULL)) {
				mesh_path->nhop_sta = rwnx_sta;
				trace_mesh_update_path(mesh_path);
			}
		} else {
			// Allocate a Mesh Path structure
			mesh_path = kmalloc(sizeof(struct rwnx_mesh_path), GFP_ATOMIC);

			if (mesh_path) {
				struct rwnx_sta *rwnx_sta = rwnx_rx_get_sta(rwnx_hw, ind->nhop_sta_idx);
				INIT_LIST_HEAD(&mesh_path->list);

				if (!WARN_ON(rwnx_sta == NULL)) {
					mesh_path->path_idx = ind->path_idx;
					mesh_path->nhop_sta = rwnx_sta;
					memcpy(&mesh_path->tgt_mac_addr, &ind->tgt_mac_addr, MAC_ADDR_LEN);

					// Insert the path in the list of path
					list_add_tail(&mesh_path->list, &rwnx_vif->ap.mpath_list);
					trace_mesh_create_path(mesh_path);
				}
			}
		}
	}

	return 0;
}

static inline int rwnx_rx_mesh_proxy_update_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct mesh_proxy_update_ind *ind = (struct mesh_proxy_update_ind *)msg->param;
	struct rwnx_vif *rwnx_vif = rwnx_rx_get_vif(rwnx_hw, ind->vif_idx);
	struct rwnx_mesh_proxy *mesh_proxy;
	bool found = false;

	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	if (ind->vif_idx >= NX_VIRT_DEV_MAX)
		return 1;

	if (!rwnx_vif || (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT))
		return 0;

	/* Look for path with provided external STA address */
	list_for_each_entry(mesh_proxy, &rwnx_vif->ap.proxy_list, list) {
		if (!memcmp(&ind->ext_sta_addr, &mesh_proxy->ext_sta_addr, ETH_ALEN)) {
			found = true;
			break;
		}
	}

	if (ind->delete &&found) {
		/* Delete mesh path */
		list_del_init(&mesh_proxy->list);
		kfree(mesh_proxy);
	} else if (!ind->delete &&!found) {
		/* Allocate a Mesh Path structure */
		mesh_proxy = (struct rwnx_mesh_proxy *)kmalloc(sizeof(*mesh_proxy), GFP_ATOMIC);

		if (mesh_proxy) {
			INIT_LIST_HEAD(&mesh_proxy->list);

			memcpy(&mesh_proxy->ext_sta_addr, &ind->ext_sta_addr, MAC_ADDR_LEN);
			mesh_proxy->local = ind->local;

			if (!ind->local) {
				memcpy(&mesh_proxy->proxy_addr, &ind->proxy_mac_addr, MAC_ADDR_LEN);
			}

			/* Insert the path in the list of path */
			list_add_tail(&mesh_proxy->list, &rwnx_vif->ap.proxy_list);
		}
	}

	return 0;
}

/***************************************************************************
 * Messages from APM task
 **************************************************************************/
static inline int rwnx_rx_apm_probe_client_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct apm_probe_client_ind *ind = (struct apm_probe_client_ind *)msg->param;
	struct rwnx_vif *rwnx_vif = rwnx_rx_get_vif(rwnx_hw, ind->vif_idx);
	struct rwnx_sta *rwnx_sta = rwnx_rx_get_sta(rwnx_hw, ind->sta_idx);

	if (WARN_ON(rwnx_vif == NULL))
		return -EFAULT;

	if (WARN_ON(rwnx_sta == NULL))
		return -EFAULT;

	rwnx_sta->stats.last_act = jiffies;
	cfg80211_probe_status(rwnx_vif->ndev, rwnx_sta->mac_addr, (u64)ind->probe_id, ind->client_present, 0, false,
			      GFP_ATOMIC);

	return 0;
}

#endif /* CONFIG_RWNX_FULLMAC */

/***************************************************************************
 * Messages from DEBUG task
 **************************************************************************/
static inline int rwnx_rx_dbg_error_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);
#if defined(CONFIG_RWNX_PCIE) || defined(CONFIG_BES_IBUS)
	bes_error_ind(rwnx_hw);
#else
	bes_error_dbg_dump((struct dbg_debug_info_tag *)msg->param);
#endif

	// Trigger tx_loop when lmac crash
	bes_tx_loop_force_close(rwnx_hw->plat, true);

	return 0;
}

static inline int bes_vendor_rf_cmd_cfm(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	return 0;
}

static inline int rwnx_mcu_wf_cali_cfm(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct mcu_wf_cali_cfm *cfm = (struct mcu_wf_cali_cfm *)msg->param;
	bes_info(BES_DBG_MSG_RX, "%s:%d, step:%d, status:%d\n", __func__, __LINE__, cfm->step, cfm->status);
	return 0;
}

#ifdef DUMP_FW_LOG_ENABLE
int rwnx_mcu_dump_fw_log_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

#if (DUMP_FW_LOG_METHOD == 1)
	/* Print directly through messages */
#ifdef CONFIG_OPLUS_PLATFORM
	oplus_log_write((u8 *)msg->param, (u16)msg->param_len); //oplus added
#else
	bes_info(BES_LOG_INFO, "[FW]:\n%s>>", (char *)msg->param);
#endif
#elif (DUMP_FW_LOG_METHOD == 2)
	bes_fw_fifo_dump_in(rwnx_hw, (char *)msg->param, FW_LOG_MCU_UPLOAD);
	queue_work(rwnx_hw->rwnx_wq, &rwnx_hw->fw_fifo_dump.dump_work);
#elif (DUMP_FW_LOG_METHOD == 3)
	/* Store in FIFO through messages and read from FIFO when an error occurs */
	bes_fw_log_store(rwnx_hw, (u8 *)msg->param, (u16)msg->param_len);

#elif (DUMP_FW_LOG_METHOD == 4)
	/* Android loggr (TODO) */
#endif
	return 0;
}
#endif


int rwnx_mcu_wifi_temp_cali_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	bes_dbac_temp_compensation_start(rwnx_hw);
	bes_dbg(BES_DBG_CALI, ">>> %s\n", __func__);
	return 0;
}

int rwnx_mcu_dev_state_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
#ifdef CONFIG_RWNX_PCIE
	u32 *dev_status = msg->param;
	struct rwnx_plat *rwnx_plat = rwnx_hw->plat;

	rwnx_plat->dev_status = *dev_status;

	wake_up(&rwnx_plat->dev_stat_wait_q);
#endif
	return 0;
}

static int format_string(char *buf, size_t size, const char *fmt, ...)
{
	int len;
	va_list ap;

	va_start(ap, fmt);
	len = vsnprintf(&buf[0], size, fmt, ap);
	va_end(ap);

	if (len < 0) {
		len = 0;
	} else if (len >= size) {
		len = (size > 1) ? (size - 1) : 0;
	}
	return len;
}

static int print_fault_info(const struct ltrace_exception_info *info)
{
	const u32 *regs;
	int len, i, index;
	u32 val;
	u16 cbuf_len = 1024;
	char *crash_buf = NULL;

	if (!(crash_buf = (char *)kmalloc(cbuf_len, GFP_KERNEL))) {
		bes_err(BES_DBG_FW_TRACE, "%s kzalloc crash_buf fail\n", __func__);
		return -ENOMEM;
	}

	regs = (u32*)((u8*)info + sizeof(struct ltrace_exception_info));

	len = format_string(crash_buf, cbuf_len, "PC =%08X", (unsigned)regs[15]);

	len += format_string(&crash_buf[len], cbuf_len - len, ", EXC_RETURN=%08X \r\n", regs[19]);

	for (i = 0; i < 3; i++) {
		index = i * 4;
		len += format_string(&crash_buf[len], cbuf_len - len, "R%-2d=%08X, R%-2d=%08X, R%-2d=%08X, R%-2d=%08X\n",
			index, (unsigned)regs[index], index + 1, (unsigned)regs[index + 1],
			index + 2, (unsigned)regs[index + 2], index + 3, (unsigned)regs[index + 3]);
	}
	len += format_string(&crash_buf[len], cbuf_len - len, "R12=%08X, SP =%08X, LR =%08X\r\n",
		(unsigned)regs[12], (unsigned)regs[13], (unsigned)regs[14]);

	len += format_string(&crash_buf[len], cbuf_len - len, "MSP   =%08X, PSP   =%08X\r\n", info->MSP, info->PSP);

	len += format_string(&crash_buf[len], cbuf_len - len, "PRIMASK   =%02X, FAULTMASK   =%02X, BASEPRI   =%02X, CONTROL   =%02X\r\n",
		(unsigned)info->PRIMASK, (unsigned)info->FAULTMASK, (unsigned)info->BASEPRI, (unsigned)info->CONTROL);

	len += format_string(&crash_buf[len], cbuf_len - len, "XPSR=%08X", (unsigned)regs[16]);
	len += format_string(&crash_buf[len], cbuf_len - len, ", APSR=%c%c%c%c%c",
		(regs[16] & (1 << 31)) ? 'N' : 'n',
		(regs[16] & (1 << 30)) ? 'Z' : 'z',
		(regs[16] & (1 << 29)) ? 'C' : 'c',
		(regs[16] & (1 << 28)) ? 'V' : 'v',
		(regs[16] & (1 << 27)) ? 'Q' : 'q'
		);
	val = regs[16] & 0x1FF;
	len += format_string(&crash_buf[len], cbuf_len - len, ", EPSR=%08X, IPSR=%03X", (unsigned)(regs[16] & 0x0700FC00), (unsigned)val);
	if (val == 0) {
		len += format_string(&crash_buf[len], cbuf_len - len, " (NoException)");
	}

	len += format_string(&crash_buf[len], cbuf_len - len, "\r\nICSR =%08X, AIRCR=%08X, SCR  =%08X, CCR  =%08X\n",
		(unsigned)info->ICSR, (unsigned)info->AIRCR, (unsigned)info->SCR, (unsigned)info->CCR);

	len += format_string(&crash_buf[len], cbuf_len - len, "SHCSR=%08X, CFSR =%08X, HFSR =%08X, AFSR =%08X\n",
		(unsigned)info->SHCSR, (unsigned)info->CFSR, (unsigned)info->HFSR, (unsigned)info->AFSR);

	len += format_string(&crash_buf[len], cbuf_len - len, "DFSR =%08X\n", (unsigned)info->DFSR);

	len += format_string(&crash_buf[len], cbuf_len - len, "MMFAR=%08X, BFAR =%08X\r\n",
		(unsigned)info->MMFAR, (unsigned)info->BFAR);

	if (info->HFSR & (1 << 30)) {
		len += format_string(&crash_buf[len], cbuf_len - len, "(Escalation HardFault)\n");
	}

	len += format_string(&crash_buf[len], cbuf_len - len, "FaultInfo	:");
	if ((info->SHCSR & 0x13F) == 0) {
		len += format_string(&crash_buf[len], cbuf_len - len, " (None)");
	} else {
		if (info->SHCSR & (1 << 0)) {
			len += format_string(&crash_buf[len], cbuf_len - len, " (MemFault)");
		}
		if (info->SHCSR & (1 << 1)) {
			len += format_string(&crash_buf[len], cbuf_len - len, " (BusFault)");
		}
		if (info->SHCSR & (1 << 2)) {
			len += format_string(&crash_buf[len], cbuf_len - len, " (HardFault)");
		}
		if (info->SHCSR & (1 << 3)) {
			len += format_string(&crash_buf[len], cbuf_len - len, " (UsageFault)");
		}
		if (info->SHCSR & (1 << 4)) {
			len += format_string(&crash_buf[len], cbuf_len - len, " (SecureFault)");
		}
		if (info->SHCSR & (1 << 5)) {
			len += format_string(&crash_buf[len], cbuf_len - len, " (NMI)");
		}
		if (info->SHCSR & (1 << 8)) {
			len += format_string(&crash_buf[len], cbuf_len - len, " (Monitor)");
		}
	}

	len += format_string(&crash_buf[len], cbuf_len - len, "\nFaultCause   :");
	if (info->CFSR == 0) {
		len += format_string(&crash_buf[len], cbuf_len - len, " (None)");
	} else {
		if (info->CFSR & (1 << 0)) {
			len += format_string(&crash_buf[len], cbuf_len - len, " (Instruction access violation)");
		}
		if (info->CFSR & (1 << 1)) {
			len += format_string(&crash_buf[len], cbuf_len - len, " (Data access violation)");
		}
		if (info->CFSR & (1 << 3)) {
			len += format_string(&crash_buf[len], cbuf_len - len, " (MemFault on unstacking for a return from exception)");
		}
		if (info->CFSR & (1 << 4)) {
			len += format_string(&crash_buf[len], cbuf_len - len, " (MemFault on stacking for exception entry)");
		}
		if (info->CFSR & (1 << 5)) {
			len += format_string(&crash_buf[len], cbuf_len - len, " (MemFault during floating-point lazy state preservation)");
		}
		if (info->CFSR & (1 << 7)) {
			len += format_string(&crash_buf[len], cbuf_len - len, " (MMFAR valid)");
		}
		if (info->CFSR & (1 << 8)) {
			len += format_string(&crash_buf[len], cbuf_len - len, " (Instruction bus error)");
		}
		if (info->CFSR & (1 << 9)) {
			len += format_string(&crash_buf[len], cbuf_len - len, " (Precise data bus error)");
		}
		if (info->CFSR & (1 << 10)) {
			len += format_string(&crash_buf[len], cbuf_len - len, " (Imprecise data bus error)");
		}
		if (info->CFSR & (1 << 11)) {
			len += format_string(&crash_buf[len], cbuf_len - len, " (BusFault on unstacking for a return from exception)");
		}
		if (info->CFSR & (1 << 12)) {
			len += format_string(&crash_buf[len], cbuf_len - len, " (BusFault on stacking for exception entry)");
		}
		if (info->CFSR & (1 << 13)) {
			len += format_string(&crash_buf[len], cbuf_len - len, " (BusFault during floating-point lazy state preservation)");
		}
		if (info->CFSR & (1 << 15)) {
			len += format_string(&crash_buf[len], cbuf_len - len, " (BFAR valid)");
		}

		len += format_string(&crash_buf[len], cbuf_len - len, " \r\n");
	}

	if(crash_buf)
		bes_info(BES_DBG_FW_TRACE, "%s , %s\n", __func__ ,crash_buf);

	kfree(crash_buf);

	return 0;
}


int rwnx_mcu_dump_ltrace_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	switch (msg->id)
	{
	case MCU_DUMP_LTRACE_START_IND:
	case MCU_DUMP_LTRACE_CONT_IND:
	case MCU_DUMP_LTRACE_END_IND:
	case MCU_DUMP_LTRACE_SINGLE_IND:
		break;
	case MCU_DUMP_LTRACE_FAULT_IND:
		if (msg->param[0] == LTR_MSG_ASSERT) {
			struct ltrace_assert_msg *ptr = (struct ltrace_assert_msg *)(&msg->param[1]);
			bes_err(BES_DBG_FW_TRACE, ">>> lmac assert occured at %s\n", ptr->str);
		} else if (msg->param[0] == LTR_MSG_EXCEPTION) {
			bes_err(BES_DBG_FW_TRACE, ">>> lmac exception occured\n");
			print_fault_info((struct ltrace_exception_info *)(&msg->param[1]));
		}
		break;
	default:
		bes_warn(BES_DBG_FW_TRACE, ">>> %s unknow msg_id: %d\n", __func__, msg->id);
		break;
	}

	return 0;
}

static int rwnx_mcu_sys_state_cfm(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct mcu_sys_state_cfm *cfm = (struct mcu_sys_state_cfm *)msg->param;
	struct mm_channel_pre_switch_ind *pre_switch_ind = NULL;
	struct mm_channel_switch_ind *switch_ind = NULL;
	struct ipc_e2a_msg fake_msg;

	/* Sync bt state */
	fake_msg.param[0] = cfm->bt_state;
	bes_coex_bt_status_ind(rwnx_hw, NULL, &fake_msg);

	/* Sync channel context state */
	if (rwnx_hw->cur_chanctx != cfm->cur_switch_ind.chan_index) {
		pre_switch_ind = (struct mm_channel_pre_switch_ind *)&fake_msg.param[0];
		pre_switch_ind->chan_index = rwnx_hw->cur_chanctx;
		rwnx_rx_chan_pre_switch_ind(rwnx_hw, NULL, &fake_msg);

		switch_ind = (struct mm_channel_switch_ind *)&fake_msg.param[0];
		*switch_ind = cfm->cur_switch_ind;
		rwnx_rx_chan_switch_ind(rwnx_hw, NULL, &fake_msg);
	}

	return 0;
}

static int rwnx_rc_update_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct me_rc_stats_cfm *ind = (struct me_rc_stats_cfm *)msg->param;
	struct rwnx_sta *rwnx_sta = rwnx_rx_get_sta(rwnx_hw, ind->sta_idx);

	if (ind->no_samples == 0)
		return 0;

	if (WARN_ON(rwnx_sta == NULL))
		return -EFAULT;

	bes_tx_update_peer_info(rwnx_hw, rwnx_sta, ind);

	return 0;
}

static inline int rwnx_rx_csi_cap_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	struct mm_csi_cap_ind *drv_ind = (struct mm_csi_cap_ind *)msg->param;

	bes_dbg(BES_DBG_MSG_RX, "%s param_len:%d tot_len:%d data_offset:%d data_len:%d status:0x%x\n",
		__func__, msg->param_len, drv_ind->tot_len, drv_ind->data_offset, drv_ind->data_len, drv_ind->csi_result.status);

	bes_csi_recv_cb(rwnx_hw, drv_ind);

	return 0;
}

static inline int rwnx_rx_diag_reg_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	bes_info(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	// Print diag reg info in defered work
	return rwnx_queue_deferred_e2amsg_work(rwnx_hw, msg, NULL, NULL);
}

#ifdef CONFIG_RWNX_SOFTMAC

static msg_cb_fct mm_hdlrs[MSG_I(MM_MAX)] = {
	[MSG_I(MM_CONNECTION_LOSS_IND)] = rwnx_rx_connection_loss_ind,
	[MSG_I(MM_CHANNEL_SWITCH_IND)] = rwnx_rx_chan_switch_ind,
	[MSG_I(MM_CHANNEL_PRE_SWITCH_IND)] = rwnx_rx_chan_pre_switch_ind,
	[MSG_I(MM_REMAIN_ON_CHANNEL_EXP_IND)] = rwnx_rx_remain_on_channel_exp_ind,
#ifdef CONFIG_RWNX_BCN
	[MSG_I(MM_PRIMARY_TBTT_IND)] = rwnx_rx_prm_tbtt_ind,
#endif
	[MSG_I(MM_P2P_VIF_PS_CHANGE_IND)] = rwnx_rx_p2p_vif_ps_change_ind,
	[MSG_I(MM_CSA_COUNTER_IND)] = rwnx_rx_csa_counter_ind,
	[MSG_I(MM_CHANNEL_SURVEY_IND)] = rwnx_rx_channel_survey_ind,
	[MSG_I(MM_RSSI_STATUS_IND)] = rwnx_rx_rssi_status_ind,
};

static msg_cb_fct scan_hdlrs[MSG_I(SCAN_MAX)] = {
	[MSG_I(SCAN_DONE_IND)] = rwnx_rx_scan_done_ind,
};

#else /* CONFIG_RWNX_FULLMAC */

static msg_cb_fct mm_hdlrs[MSG_I(MM_MAX)] = {
	[MSG_I(MM_CHANNEL_SWITCH_IND)] = rwnx_rx_chan_switch_ind,
	[MSG_I(MM_CHANNEL_PRE_SWITCH_IND)] = rwnx_rx_chan_pre_switch_ind,
	[MSG_I(MM_REMAIN_ON_CHANNEL_EXP_IND)] = rwnx_rx_remain_on_channel_exp_ind,
	[MSG_I(MM_PS_CHANGE_IND)] = rwnx_rx_ps_change_ind,
	[MSG_I(MM_TRAFFIC_REQ_IND)] = rwnx_rx_traffic_req_ind,
	[MSG_I(MM_P2P_VIF_PS_CHANGE_IND)] = rwnx_rx_p2p_vif_ps_change_ind,
	[MSG_I(MM_CSA_COUNTER_IND)] = rwnx_rx_csa_counter_ind,
	[MSG_I(MM_CSA_FINISH_IND)] = rwnx_rx_csa_finish_ind,
	[MSG_I(MM_CSA_TRAFFIC_IND)] = rwnx_rx_csa_traffic_ind,
	[MSG_I(MM_CHANNEL_SURVEY_IND)] = rwnx_rx_channel_survey_ind,
	[MSG_I(MM_P2P_NOA_UPD_IND)] = rwnx_rx_p2p_noa_upd_ind,
	[MSG_I(MM_RSSI_STATUS_IND)] = rwnx_rx_rssi_status_ind,
	[MSG_I(MM_PKTLOSS_IND)] = rwnx_rx_pktloss_notify_ind,
	[MSG_I(MM_COEX_EVENT_IND)] = rwnx_rx_wifi_bt_coex_evt_ind,
	[MSG_I(MM_RC_UPDATE_IND)] = rwnx_rc_update_ind,
	[MSG_I(MM_ANOMALY_IND)] = rwnx_rx_anomaly_ind,
	[MSG_I(MM_SCHED_SCAN_RESULT_IND)] = rwnx_rx_sched_scan_result_ind,
	[MSG_I(MM_CSI_CAP_IND)] = rwnx_rx_csi_cap_ind,
	[MSG_I(MM_DIAG_REG_IND)] = rwnx_rx_diag_reg_ind,
};

static msg_cb_fct scan_hdlrs[MSG_I(SCAN_MAX)] = {
	[MSG_I(SCAN_CANCEL_CFM)] = rwnx_rx_scan_cancel_cfm,
};

static msg_cb_fct scanu_hdlrs[MSG_I(SCANU_MAX)] = {
	[MSG_I(SCANU_START_CFM)] = rwnx_rx_scanu_start_cfm,
	[MSG_I(SCANU_RESULT_IND)] = rwnx_rx_scanu_result_ind,
};

static msg_cb_fct me_hdlrs[MSG_I(ME_MAX)] = {
	[MSG_I(ME_TKIP_MIC_FAILURE_IND)] = rwnx_rx_me_tkip_mic_failure_ind,
	[MSG_I(ME_TX_CREDITS_UPDATE_IND)] = rwnx_rx_me_tx_credits_update_ind,
	[MSG_I(ME_DATA_PATH_FLUSHED_IND)] = rwnx_rx_me_data_path_flushed_ind,
};

static msg_cb_fct sm_hdlrs[MSG_I(SM_MAX)] = {
	[MSG_I(SM_CONNECT_IND)] = rwnx_rx_sm_connect_ind,
	[MSG_I(SM_DISCONNECT_IND)] = rwnx_rx_sm_disconnect_ind,
	[MSG_I(SM_EXTERNAL_AUTH_REQUIRED_IND)] = rwnx_rx_sm_external_auth_required_ind,
	[MSG_I(SM_FT_AUTH_IND)] = rwnx_rx_sm_ft_auth_ind,
	[MSG_I(SM_MGT_FRAME_IND)] = rwnx_rx_sm_mgt_frame_ind,
};

static msg_cb_fct apm_hdlrs[MSG_I(APM_MAX)] = {
	[MSG_I(APM_PROBE_CLIENT_IND)] = rwnx_rx_apm_probe_client_ind,
};

static msg_cb_fct twt_hdlrs[MSG_I(TWT_MAX)] = {
	[MSG_I(TWT_SETUP_IND)] = rwnx_rx_twt_setup_ind,
};

static msg_cb_fct mesh_hdlrs[MSG_I(MESH_MAX)] = {
	[MSG_I(MESH_PATH_CREATE_CFM)] = rwnx_rx_mesh_path_create_cfm,
	[MSG_I(MESH_PEER_UPDATE_IND)] = rwnx_rx_mesh_peer_update_ind,
	[MSG_I(MESH_PATH_UPDATE_IND)] = rwnx_rx_mesh_path_update_ind,
	[MSG_I(MESH_PROXY_UPDATE_IND)] = rwnx_rx_mesh_proxy_update_ind,
};

#endif /* CONFIG_RWNX_SOFTMAC */

static msg_cb_fct dbg_hdlrs[MSG_I(DBG_MAX)] = {
	[MSG_I(DBG_ERROR_IND)] = rwnx_rx_dbg_error_ind,
};

static msg_cb_fct tdls_hdlrs[MSG_I(TDLS_MAX)] = {
	[MSG_I(TDLS_CHAN_SWITCH_CFM)] = rwnx_rx_tdls_chan_switch_cfm,
	[MSG_I(TDLS_CHAN_SWITCH_IND)] = rwnx_rx_tdls_chan_switch_ind,
	[MSG_I(TDLS_CHAN_SWITCH_BASE_IND)] = rwnx_rx_tdls_chan_switch_base_ind,
	[MSG_I(TDLS_PEER_PS_IND)] = rwnx_rx_tdls_peer_ps_ind,
};

static msg_cb_fct mcu_hdlrs[MSG_I(MCU_MSG_MAX)] = {
	[MSG_I(MCU_VENDOR_RF_CMD_CFM)] = bes_vendor_rf_cmd_cfm,
	[MSG_I(MCU_WIFI_CALI_CFM)] = rwnx_mcu_wf_cali_cfm,
#ifdef DUMP_FW_LOG_ENABLE
	[MSG_I(MCU_DUMP_FW_LOG_IND)] = rwnx_mcu_dump_fw_log_ind,
#endif
	[MSG_I(MCU_DBAC_TEMP_CALI_IND)] = rwnx_mcu_wifi_temp_cali_ind,
	[MSG_I(MCU_BT_STATUS_IND)] = bes_coex_bt_status_ind,
	[MSG_I(MCU_DEV_STATE_IND)] = rwnx_mcu_dev_state_ind,
	[MSG_I(MCU_DUMP_LTRACE_START_IND)] = rwnx_mcu_dump_ltrace_ind,
	[MSG_I(MCU_DUMP_LTRACE_CONT_IND)] = rwnx_mcu_dump_ltrace_ind,
	[MSG_I(MCU_DUMP_LTRACE_END_IND)] = rwnx_mcu_dump_ltrace_ind,
	[MSG_I(MCU_DUMP_LTRACE_SINGLE_IND)] = rwnx_mcu_dump_ltrace_ind,
	[MSG_I(MCU_DUMP_LTRACE_FAULT_IND)] = rwnx_mcu_dump_ltrace_ind,
	[MSG_I(MCU_SYS_STATE_CFM)] = rwnx_mcu_sys_state_cfm,
};

static msg_cb_fct *msg_hdlrs[] = {
	[TASK_MM] = mm_hdlrs,
	[TASK_DBG] = dbg_hdlrs,
#ifdef CONFIG_RWNX_SOFTMAC
	[TASK_SCAN] = scan_hdlrs,
	[TASK_TDLS] = tdls_hdlrs,
#else
	[TASK_TDLS] = tdls_hdlrs,
	[TASK_SCAN] = scan_hdlrs,
	[TASK_SCANU] = scanu_hdlrs,
	[TASK_ME] = me_hdlrs,
	[TASK_SM] = sm_hdlrs,
	[TASK_APM] = apm_hdlrs,
	[TASK_MESH] = mesh_hdlrs,
	[TASK_TWT] = twt_hdlrs,
#endif /* CONFIG_RWNX_SOFTMAC */
	[TASK_MCU] = mcu_hdlrs,
};

static u16 sub_msg_id_max[TASK_MAX] = {
	[TASK_MM] = MSG_I(MM_MAX),
	[TASK_DBG] = MSG_I(DBG_MAX),
	[TASK_SCAN] = MSG_I(SCAN_MAX),
	[TASK_TDLS] = MSG_I(TDLS_MAX),
	[TASK_SCANU] = MSG_I(SCANU_MAX),
	[TASK_ME] = MSG_I(ME_MAX),
	[TASK_SM] = MSG_I(SM_MAX),
	[TASK_APM] = MSG_I(APM_MAX),
	[TASK_MESH] = MSG_I(MESH_MAX),
	[TASK_TWT] = MSG_I(TWT_MAX),
	[TASK_FTM] = MSG_I(FTM_MAX),
	[TASK_MCU] = MSG_I(MCU_MSG_MAX),
};

bool rwnx_rx_check_msg_id(u16 id)
{
	u16 msg_t = MSG_T(id);
	u16 msg_i = MSG_I(id);

	if (msg_t >= TASK_MAX || msg_t == TASK_API)
		goto err_id;

	if (msg_i >= sub_msg_id_max[msg_t])
		goto err_id;

	return true;

err_id:
	bes_err(BES_DBG_MSG_RX, "%s error id: %u %u.\n", __func__, msg_t, msg_i);
	return false;
}

/**
 *
 */
void rwnx_rx_handle_msg(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg)
{
	rwnx_hw->cmd_mgr.msgind(&rwnx_hw->cmd_mgr, msg, msg_hdlrs[MSG_T(msg->id)][MSG_I(msg->id)]);
}

void rwnx_defer_msg_work(struct work_struct *work)
{
	struct rwnx_plat *self = container_of(work, struct rwnx_plat, deferred_e2amsg_work);
	struct rwnx_hw *rwnx_hw = self->drvdata;
	struct rwnx_deferred_msg *deferred_msg, *next;
	struct list_head deferred_msg_work_head;
	u16_l msg_id;

	bes_dbg(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	if (rwnx_hw == NULL) {
		bes_info(BES_DBG_MSG_RX, "%s:%d rwnx_hw is null\n", __func__, __LINE__);
		goto param_err;
	}

	/* Init deferred_msg_work_head */
	INIT_LIST_HEAD(&deferred_msg_work_head);

	spin_lock_bh(&rwnx_hw->cb_lock);
	/* Traverse adding item to deferred_msg_work_head and reinitialise the emptied list */
	list_splice_init(&rwnx_hw->deferred_e2amsg_list.list, &deferred_msg_work_head);
	spin_unlock_bh(&rwnx_hw->cb_lock);

	if (!list_empty(&deferred_msg_work_head)) {
		list_for_each_entry_safe(deferred_msg, next, &deferred_msg_work_head, list) {
			msg_id = deferred_msg->msg.id;
			switch (msg_id) {
			case SM_CONNECT_IND:
				rwnx_defer_sm_connect_ind(rwnx_hw, deferred_msg);
				break;
			case SM_DISCONNECT_IND:
				rwnx_defer_sm_disconnect_ind(rwnx_hw, deferred_msg);
				break;
			case MESH_PEER_UPDATE_IND:
				rwnx_defer_mesh_peer_update_ind(rwnx_hw, deferred_msg);
				break;
			case MM_DIAG_REG_IND:
				rwnx_defer_mm_diag_reg_ind(rwnx_hw, deferred_msg);
				break;
			case MM_CSA_FINISH_IND:
				rwnx_defer_csa_finish_ind(rwnx_hw, deferred_msg);
				break;
			default:
				break;
			}
			list_del(&deferred_msg->list);
			kfree(deferred_msg);
		}
	}

param_err:
	bes_info(BES_DBG_MSG_RX, "%s:%d exit\n", __func__, __LINE__);
}
