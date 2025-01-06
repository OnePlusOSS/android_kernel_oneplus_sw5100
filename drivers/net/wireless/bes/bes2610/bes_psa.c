/*
 * @file bes_psa.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <uapi/linux/sched/types.h>
#include <linux/kfifo.h>
#include "bes_psa.h"
#include "rwnx_msg_rx.h"
#include "lmac_msg.h"
#include "bes_sysfs.h"
#include "bes_pwr.h"
#include "bes_wowlan.h"
#include "rwnx_main.h"
#include "rwnx_prof.h"
#include "bes_hwio.h"
#include "bes_tx_loop.h"
#include "bes_eapol.h"
#include "bes_dbg.h"
#include "bes_wpa_priv_cmd.h"
#include "bes_hwio.h"
#include "bes_linkinfo.h"
#include "rwnx_events.h"

#define BES_RX_SCHED_PRIORITY  20
#define TX_MSG_SIZE	(sizeof(struct rwnx_tx_msg))

static void bes_psa_tx_subframe_peek(struct sk_buff *skb, struct tx_packet *pkt);

void *msga2e_hostid;
extern struct rwnx_plat g_rwnx_plat;

void bes_psa_msga2e_hostid_clear(struct rwnx_cmd *rwnx_cmd)
{
	if (msga2e_hostid == rwnx_cmd)
		msga2e_hostid = NULL;
}

void bes_psa_msg_push(struct rwnx_hw *rwnx_hw, void *msg_buf, uint16_t len)
{
	struct rwnx_cmd_a2emsg *src = ((struct rwnx_cmd *)msg_buf)->a2e_msg;
	void *dst = rwnx_hw->plat->a2e_msg;
	bool bus_suspended = false;
	unsigned long flags;

	/* Record current sending rwnx_cmd */
	msga2e_hostid = msg_buf;

	/* Fill message type */
	src->psa_pattern = EVENT_A2E_MSG_WITH_ACK;
	if (src->id == MM_RESET_REQ || src->id == MCU_WIFI_SEND_DPD_DATA_REQ) {
		src->psa_pattern = EVENT_A2E_MSG;
		((struct rwnx_cmd *)msg_buf)->flags &= (~RWNX_CMD_FLAG_WAIT_ACK);
	}

	/* limit maximum copy length */
	if (len > RWNX_MSG_MAX_LEN) {
		bes_err(BES_DBG_PSA, "msg: %s too large len(%d/%ld)\n", RWNX_ID2STR(src->id), len, RWNX_MSG_MAX_LEN);
		len = RWNX_MSG_MAX_LEN;
	}

	/* copy message context */
	memcpy(dst, src, len);

	bes_dbg(BES_DBG_PSA, "msg push id=0x%x pattern=0x%x\n", src->id, src->psa_pattern);

	/* Notify the PM core of a wakeup event(tx msg) if bus is suspend.
	   And mark there is a tx msg event */
	spin_lock_irqsave(&rwnx_hw->plat->run_flags_lock, flags);
	rwnx_hw->plat->run_flags |= BIT(PLAT_RUN_FLAGS_TX_MSG);
	bus_suspended = !!(rwnx_hw->plat->run_flags & BIT(PLAT_RUN_FLAGS_BUS_SUSPEND));
	rwnx_platform_pm_stay_awake(rwnx_hw->plat, TX_WAKELOCK);
	spin_unlock_irqrestore(&rwnx_hw->plat->run_flags_lock, flags);

	/* Defer msg tx when resuming */
	if (!bus_suspended)
		wake_up(&g_rwnx_plat.tx_wait_q);
	else
		bes_wowlan_msg_tx_info_print(src);
}

void bes_psa_data_push(struct rwnx_hw *rwnx_hw, struct txdesc_host *txdesc_host, struct sk_buff *skb, int hw_queue)
{
	int len;
	int offset;
	struct ipc_header *pHdr = NULL;
	struct rwnx_sw_txhdr *sw_txhdr = container_of(txdesc_host, struct rwnx_sw_txhdr, desc);
	unsigned long flags;
	bool bus_suspended = false;

	txdesc_host->ctrl.hwq = hw_queue;
	spin_lock_bh(&g_rwnx_plat.tx_lock);
	txdesc_host->api.host.hostid = ipc_host_tx_host_ptr_to_id(rwnx_hw->ipc_env, skb);
	spin_unlock_bh(&g_rwnx_plat.tx_lock);
	if (!txdesc_host->api.host.hostid) {
		dev_err(rwnx_hw->dev, "No more tx_hostid available\n");
		return;
	}
	atomic_sub(sw_txhdr->buf_cnt, &rwnx_hw->plat->buf_tx);
	bes_tx_loop_inc_pending_data_count(rwnx_hw->plat);
	txdesc_host->ready = 0xFFFFFFFF;
	if (txdesc_host->api.host.flags & TXU_CNTRL_MGMT)
		offset = sizeof(struct rwnx_txhdr);
	else
		offset = sizeof(struct rwnx_txhdr) + DATA_OFFSET;

	pHdr = (struct ipc_header *)(skb->data + offset);
	pHdr->type = EVENT_A2E_DATA_REQ;
	pHdr->size = skb->len - offset;
	memcpy(skb->data + offset + sizeof(u32), txdesc_host, sizeof(struct txdesc_host));

	len = skb->len - offset;
	bes_dbg(BES_DBG_PSA, "%s len:%d,hostid=0x%x\n", __func__, len, txdesc_host->api.host.hostid);

	skb_queue_tail(&rwnx_hw->plat->tx_q, skb);

	/* Notify the PM core of a wakeup event(tx data) if bus is suspend.
	   And mark there is a tx data event */
	spin_lock_irqsave(&rwnx_hw->plat->run_flags_lock, flags);
	rwnx_hw->plat->run_flags |= BIT(PLAT_RUN_FLAGS_TX_DATA);
	bus_suspended = !!(rwnx_hw->plat->run_flags & BIT(PLAT_RUN_FLAGS_BUS_SUSPEND));
	rwnx_platform_pm_stay_awake(rwnx_hw->plat, TX_WAKELOCK);
	spin_unlock_irqrestore(&rwnx_hw->plat->run_flags_lock, flags);

	/* Defer data tx when resuming */
	if (!bus_suspended)
		wake_up(&rwnx_hw->plat->tx_wait_q);
	else
		bes_wowlan_data_tx_info_print(skb);
}

void bes_psa_e2a_msgack_rx(struct rwnx_hw *rwnx_hw, struct sk_buff *skb)
{
	/* Lp processing for msg ack rx */
	bes_pwr_msg_ack_rx_process(rwnx_hw);

	rwnx_hw->cmd_mgr.llind(&rwnx_hw->cmd_mgr, (struct rwnx_cmd *)msga2e_hostid);
}

void bes_psa_e2a_msg_rx(struct rwnx_hw *rwnx_hw, struct sk_buff *skb)
{
	struct ipc_e2a_msg *msg = (struct ipc_e2a_msg *)(skb->data + 4);

	bes_dbg(BES_DBG_PSA, "%s %d %d.\n", __func__, MSG_T(msg->id), MSG_I(msg->id));

	if (!rwnx_rx_check_msg_id(msg->id))
		return;

	REG_SW_SET_PROFILING(rwnx_hw, SW_PROF_MSGIND);

	/* Parse msg for wowlan */
	bes_wowlan_parse_msg(rwnx_hw, msg);

	/* Lp processing for msg rx */
	bes_pwr_msg_rx_process(rwnx_hw, msg);

	/* Relay further actions to the msg parser */
	rwnx_rx_handle_msg(rwnx_hw, msg);

	REG_SW_CLEAR_PROFILING(rwnx_hw, SW_PROF_MSGIND);
	return;
}

void bes_psa_e2a_monitor_rx(struct rwnx_hw *rwnx_hw, struct sk_buff *skb, struct hw_rxhdr *hw_rxhdr)
{
	struct rwnx_vif *rwnx_vif;
	struct hw_rxhdr hw_rxhdr_copy;
	struct sk_buff *skb_monitor;
	u8 rtap_len;

	// Check if monitor interface exists and is open
	rwnx_vif = rwnx_rx_get_vif(rwnx_hw, rwnx_hw->monitor_vif);
	if (!rwnx_vif) {
		bes_err(BES_DBG_PSA, "Received monitor frame but there is no monitor interface open\n");
		dev_kfree_skb(skb);
		return;
	}

	rtap_len = rwnx_rx_rtap_hdrlen(&hw_rxhdr->hwvect.rx_vect1, false);

	// Check if there is enough space to add the radiotap header
	if (skb_headroom(skb) > rtap_len) {
		skb_monitor = skb;

		// Duplicate the HW Rx Header to override with the radiotap header
		memcpy(&hw_rxhdr_copy, hw_rxhdr, sizeof(hw_rxhdr_copy));
		hw_rxhdr = &hw_rxhdr_copy;
	} else {
		// Duplicate the skb and extend the headroom
		skb_monitor = skb_copy_expand(skb, rtap_len, 0, GFP_ATOMIC);
		dev_kfree_skb(skb);
		if (unlikely(skb_monitor == NULL)) {
			bes_err(BES_DBG_PSA, "skb_copy_expand skb_monitor failed\n");
			return;
		}
	}

	if (rwnx_rx_monitor(rwnx_hw, rwnx_vif, skb_monitor, hw_rxhdr, rtap_len))
		dev_kfree_skb(skb_monitor);
}

void bes_psa_e2a_data_rx(struct rwnx_hw *rwnx_hw, struct sk_buff *skb)
{
	struct rwnx_sta *sta = NULL;
	struct hw_rxhdr *hw_rxhdr;
	int msdu_offset = BES_PSA_DATA_RX_HEADER_LEN;
	struct rwnx_vif *rwnx_vif;

	hw_rxhdr = (struct hw_rxhdr *)(skb->data + BES_PSA_E2A_HDR_LEN);
	rwnx_rx_vector_convert(rwnx_hw->machw_type, &hw_rxhdr->hwvect.rx_vect1, &hw_rxhdr->hwvect.rx_vect2);
	pskb_pull(skb, msdu_offset);
#if !defined(CONFIG_BES_SDIO) || !defined(CONFIG_TRX_COPY_LESS)
	skb_trim(skb, le32_to_cpu(hw_rxhdr->hwvect.len));
#endif

#ifdef CONFIG_BES_AMSDU_DEAGG
	if(hw_rxhdr->flags_is_amsdu){
		memcpy(&skb->data[14], &skb->data[6], ETH_ALEN);
		memcpy(&skb->data[8], skb->data, ETH_ALEN);
		skb_pull(skb, 8);
		hw_rxhdr->flags_is_amsdu = 0;
	}
#endif

	bes_dbg(BES_DBG_PSA, "%s data:0x%x, data2:0x%x.idx=%d,mpdu=%d\n", __func__, *((int *)skb->data), *((int *)(skb->data + 4)),
		 hw_rxhdr->flags_sta_idx, hw_rxhdr->flags_is_80211_mpdu);

	if (hw_rxhdr->flags_sta_idx != RWNX_INVALID_STA) {
		sta = rwnx_rx_get_sta(rwnx_hw, hw_rxhdr->flags_sta_idx);
		if (sta && sta->valid)
			rwnx_rx_statistic(rwnx_hw, hw_rxhdr, sta);
	}

	if (hw_rxhdr->flags_is_monitor) {
		bes_psa_e2a_monitor_rx(rwnx_hw, skb, hw_rxhdr);
	} else if (hw_rxhdr->flags_is_80211_mpdu) {
		/* lp processing for data rx */
		bes_pwr_data_rx_process(rwnx_hw, skb, hw_rxhdr);

		rwnx_rx_mgmt_any(rwnx_hw, skb, hw_rxhdr);
	} else {
		rwnx_vif = rwnx_rx_get_vif(rwnx_hw, hw_rxhdr->flags_vif_idx);

		if (!rwnx_vif) {
			bes_err(BES_DBG_PSA, "Frame received but no active vif (%d)", hw_rxhdr->flags_vif_idx);
			dev_kfree_skb(skb);
			return;
		}

		if (sta) {
			if (sta->vlan_idx != rwnx_vif->vif_index) {
				u8 vif_index = rwnx_vif->vif_index;
				rwnx_vif = rwnx_rx_get_vif(rwnx_hw, sta->vlan_idx);
				if (!rwnx_vif) {
					dev_kfree_skb(skb);
					bes_err(BES_DBG_PSA, "Frame received sta vif (%d) != vif(%d)", sta->vlan_idx,
						 vif_index);
					return;
				}
			}

			if (hw_rxhdr->flags_is_4addr && !rwnx_vif->use_4addr) {
				cfg80211_rx_unexpected_4addr_frame(rwnx_vif->ndev, sta->mac_addr, GFP_ATOMIC);
			}
		}

		skb->priority = 256 + hw_rxhdr->flags_user_prio;
		rwnx_rx_data_skb(rwnx_hw, rwnx_vif, skb, hw_rxhdr);
	}
}

int bes_psa_txdatacfm(void *pthis, void *arg)
{
	struct rwnx_hw *rwnx_hw = pthis;
	struct tx_cfm_tag *cfm;
	struct sk_buff *skb = NULL;
	struct rwnx_sw_txhdr *sw_txhdr;
	struct rwnx_hwq *hwq;
	struct rwnx_txq *txq;
	int offset;

	bes_dbg(BES_DBG_PSA, "cfmcnt=%d\n", (uint32_t)(((struct ipc_header *)arg)->size / sizeof(struct tx_cfm_tag)));
	for (offset = BES_PSA_E2A_HDR_LEN; offset < ((struct ipc_header *)arg)->size;
		offset += sizeof(struct tx_cfm_tag)) {
		cfm = (struct tx_cfm_tag *)((u8 *)arg + offset);
		spin_lock_bh(&g_rwnx_plat.tx_lock);
		skb = ipc_host_tx_host_id_to_ptr(rwnx_hw->ipc_env, cfm->hostid);
		spin_unlock_bh(&g_rwnx_plat.tx_lock);
		bes_dbg(BES_DBG_PSA, "%s skb 0x%p\n", __func__, skb);
		if (!skb || !(skb->data)) {
			bes_err(BES_DBG_PSA, "%s,skb is null!\n", __func__);
			continue; // process next
		}

		sw_txhdr = ((struct rwnx_txhdr *)skb->data)->sw_hdr;
		if (!sw_txhdr) {
			bes_err(BES_DBG_PSA, "%s,sw_txhdr is null!\n", __func__);
			continue; // process next
		}

		bes_tx_uldelay_update(sw_txhdr);

		txq = sw_txhdr->txq;
		/* don't use txq->hwq as it may have changed between push and confirm */
		hwq = &rwnx_hw->hwq[sw_txhdr->hw_queue];

		/* Notify EAPOL of completion of sending. */
		bes_tx_eapol_completion_notify(sw_txhdr, cfm);

		/* Print key frame tx cfm */
		bes_dbg_key_frame_tx_cfm(skb, cfm);

		/* Update Credits Information */
		spin_lock_bh(&rwnx_hw->tx_lock);
		atomic_add(sw_txhdr->buf_cnt, &rwnx_hw->plat->buf_tx);
		rwnx_txq_confirm_any(rwnx_hw, txq, hwq, sw_txhdr);
		spin_unlock_bh(&rwnx_hw->tx_lock);

		/* Update tx_loop count */
		bes_tx_loop_dec_pending_data_count(rwnx_hw->plat, 1);

		/* Retransmit the key frames. */
#ifdef KEY_FRAME_RETRY
		if (txq->idx != TXQ_INACTIVE &&
		    sw_txhdr->rwnx_vif->up &&
		    cfm->status.tx_done &&
		    !cfm->status.acknowledged &&
		    !cfm->status.sw_retry_required &&
		    sw_txhdr->is_key_frame &&
		    sw_txhdr->sw_retry_cnt++ < SW_RETRY_CNT_MAX) {
			cfm->status.sw_retry_required = 1;
		}
#endif

		/* Update txq and HW queue credits */
		if (!cfm->status.sw_retry_required && sw_txhdr->desc.api.host.flags & TXU_CNTRL_MGMT) {
			trace_mgmt_cfm(sw_txhdr->rwnx_vif->vif_index,
				       (sw_txhdr->rwnx_sta) ? sw_txhdr->rwnx_sta->sta_idx : 0xFF,
				       cfm->status.acknowledged);

			/* Confirm transmission to CFG80211 */
			cfg80211_mgmt_tx_status(&sw_txhdr->rwnx_vif->wdev, (unsigned long)skb, skb_mac_header(skb),
						sw_txhdr->frame_len, cfm->status.acknowledged, GFP_ATOMIC);
		}
#if 1
		else if ((txq->idx != TXQ_INACTIVE) && cfm->status.sw_retry_required) {
			int ret = 0;
			/* firmware postponed this buffer */
			spin_lock_bh(&rwnx_hw->tx_lock);
#ifdef KEY_FRAME_RETRY
			if (cfm->credits)
				txq->credits += cfm->credits;
			txq->credits--;
#endif
			ret = rwnx_tx_retry(rwnx_hw, skb, sw_txhdr, cfm->status);
#ifdef KEY_FRAME_RETRY
			if (sw_txhdr->is_key_frame && !cfm->status.retry_required)
				rwnx_ipc_sta_buffer(rwnx_hw, txq->sta, txq->tid, -sw_txhdr->frame_len);
#endif
			spin_unlock_bh(&rwnx_hw->tx_lock);
			bes_dbg(BES_DBG_PSA, "%s, sw_retry_required!\n", __func__);
			if (ret >= 0)
				continue; // process next
		}
		trace_skb_confirm(skb, txq, hwq, cfm);
#endif

		/* STA may have disconnect (and txq stopped) when buffers were stored
		   in fw. In this case do nothing when they're returned */
		bes_dbg(BES_DBG_PSA, "cfm,idx=%d,crd_cfm=%d,crd_txq=%d,txq->stat=0x%lx\n", txq->idx, cfm->credits, txq->credits,
			 txq->status);
		spin_lock_bh(&rwnx_hw->tx_lock);
		if (txq->idx != TXQ_INACTIVE) {
			if (cfm->credits) {
				txq->credits += cfm->credits;
				if (txq->credits <= 0)
					rwnx_txq_stop(txq, RWNX_TXQ_STOP_FULL);
				else if (txq->credits > 0) {
					rwnx_txq_start(txq, RWNX_TXQ_STOP_FULL);
				}
			}

			/* continue service period */
			if (unlikely(txq->push_limit && !rwnx_txq_is_full(txq))) {
				rwnx_txq_add_to_hw_list(txq);
			}
		}

		if (cfm->ampdu_size && (cfm->ampdu_size < IEEE80211_MAX_AMPDU_BUF_HE))
			rwnx_hw->stats.ampdus_tx[cfm->ampdu_size - 1]++;

#ifdef CONFIG_RWNX_AMSDUS_TX
		bes_dbg(BES_DBG_PSA, "%s,cfm->stat.ack=%d\n", __func__, cfm->status.acknowledged);
		if (!cfm->status.acknowledged) {
			if (sw_txhdr->desc.api.host.flags & TXU_CNTRL_AMSDU)
				rwnx_hw->stats.amsdus[sw_txhdr->amsdu.nb - 1].failed++;
			else if (!sw_txhdr->rwnx_sta || !is_multicast_sta(sw_txhdr->rwnx_sta->sta_idx))
				rwnx_hw->stats.amsdus[0].failed++;
		}

		rwnx_amsdu_update_len(rwnx_hw, txq, cfm->amsdu_size);
#endif

		/* Release SKBs */
#ifdef CONFIG_RWNX_AMSDUS_TX
		if (sw_txhdr->desc.api.host.flags & TXU_CNTRL_AMSDU) {
			struct rwnx_amsdu_txhdr *amsdu_txhdr, *tmp;
			list_for_each_entry_safe(amsdu_txhdr, tmp, &sw_txhdr->amsdu.hdrs, list) {
				rwnx_amsdu_del_subframe_header(amsdu_txhdr);
				rwnx_ipc_buf_a2e_release(rwnx_hw, &amsdu_txhdr->ipc_data);
				rwnx_ipc_sta_buffer(rwnx_hw, txq->sta, txq->tid, -amsdu_txhdr->msdu_len);
				rwnx_tx_statistic(sw_txhdr->rwnx_vif, txq, cfm, amsdu_txhdr->msdu_len);
				consume_skb(amsdu_txhdr->skb);
			}
		}
#endif /* CONFIG_RWNX_AMSDUS_TX */

		rwnx_ipc_buf_a2e_release(rwnx_hw, &sw_txhdr->ipc_data);
		rwnx_ipc_sta_buffer(rwnx_hw, txq->sta, txq->tid, -sw_txhdr->frame_len);
		rwnx_tx_statistic(sw_txhdr->rwnx_vif, txq, cfm, sw_txhdr->frame_len);

		kmem_cache_free(rwnx_hw->sw_txhdr_cache, sw_txhdr);
		skb_pull(skb, RWNX_TX_HEADROOM);
		consume_skb(skb);

		spin_unlock_bh(&rwnx_hw->tx_lock);
	} // end for

	return 0;
}

void bes_psa_e2a_cfm_rx(struct rwnx_hw *rwnx_hw, struct sk_buff *skb)
{
	bes_psa_txdatacfm(rwnx_hw, skb->data);
}

/**
 * bes_psa_radarind() - radarind handler
 *
 * @pthis: Pointer to main driver data
 * @arg: Pointer to IPC buffer from radar_pool
 */
static void bes_psa_radarind(void *pthis, void *arg)
{
#ifdef CONFIG_RWNX_RADAR
	int i;
	struct rwnx_hw *rwnx_hw = pthis;
	struct radar_pulse_array_desc *pulses = arg;

	if (rwnx_radar_detection_is_enable(&rwnx_hw->radar, pulses->idx)) {
		/* Save the received pulses only if radar detection is enabled */
		for (i = 0; i < pulses->cnt; i++) {
			struct rwnx_radar_pulses *p = &rwnx_hw->radar.pulses[pulses->idx];

			p->buffer[p->index] = pulses->pulse[i];
			p->index = (p->index + 1) % RWNX_RADAR_PULSE_MAX;
			if (p->count < RWNX_RADAR_PULSE_MAX)
				p->count++;
		}

		/* Defer pulse processing in separate work */
		if (!work_pending(&rwnx_hw->radar.detection_work))
			schedule_work(&rwnx_hw->radar.detection_work);
	}
#endif /* CONFIG_RWNX_RADAR */
}

void bes_psa_e2a_radar_ind_rx(struct rwnx_hw *rwnx_hw, struct sk_buff *skb)
{
	bes_psa_radarind(rwnx_hw, skb->data + 4);
}

void bes_psa_dev_status_ind(struct sk_buff *skb)
{
	struct dev_status_t *dev_status = (struct dev_status_t *)skb->data;

	if (dev_status->header.size != sizeof(struct dev_status_t))
		return ;

	g_rwnx_plat.dev_status = dev_status->stat;

	wake_up(&g_rwnx_plat.dev_stat_wait_q);
}

void bes_psa_e2a_handler(struct rwnx_hw *rwnx_hw, struct sk_buff *skb)
{
	struct ipc_header *pHdr = (struct ipc_header *)skb->data;
	struct ipc_e2a_msg *msg = (struct ipc_e2a_msg *)(skb->data + 4);
	bes_dbg(BES_DBG_PSA, "%s  psa_id:%u, len=%u!!!\n", __func__, pHdr->type, pHdr->size);

	/* skip rx data except reset cfm when fw reseting */
	if (unlikely(!rwnx_hw)) {
		dev_kfree_skb(skb);
		return;
	} else if (unlikely(test_bit(RWNX_DEV_STACK_RESTARTING, &rwnx_hw->flags))) {
		if (pHdr->type == EVENT_E2A_MSG && msg->id == MM_RESET_CFM) {
			rwnx_wf_status_clear(rwnx_hw, RWNX_DEV_STACK_RESTARTING);
		} else if (pHdr->type == EVENT_E2A_MSG && msg->id == MCU_WIFI_SEND_DPD_DATA_CFM) {
			; // do nothing
		} else {
			dev_kfree_skb(skb);
			return;
		}
	}

	bes_tx_loop_rx_handler(rwnx_hw, skb);

	switch (pHdr->type) {
	case EVENT_E2A_MSG_ACK:
		bes_psa_e2a_msgack_rx(rwnx_hw, skb);
		dev_kfree_skb(skb);
		break;
	case EVENT_E2A_MSG:
		bes_psa_e2a_msg_rx(rwnx_hw, skb);
		dev_kfree_skb(skb);
		break;
	case EVENT_E2A_DATA_IND:
		bes_psa_e2a_data_rx(rwnx_hw, skb);
		break;
	case EVENT_E2A_TX_CFM:
		bes_psa_e2a_cfm_rx(rwnx_hw, skb);
		dev_kfree_skb(skb);
		break;
	case EVENT_E2A_RADAR_IND:
		bes_psa_e2a_radar_ind_rx(rwnx_hw, skb);
		dev_kfree_skb(skb);
		break;
	default:
		bes_err(BES_DBG_PSA, "%s psa_id:%u, len=%u!!!\n", __func__, pHdr->type, pHdr->size);
		dev_kfree_skb(skb);
		break;
	}
}

void bes_psa_rx_handler(struct rwnx_plat *self, struct sk_buff *skb)
{
	struct ipc_header *pHdr = (struct ipc_header *)skb->data;
	struct rwnx_hw *rwnx_hw = self->drvdata;

	bes_dbg(BES_DBG_PSA, "%s  psa_id:%u, len=%u\n", __func__, pHdr->type, pHdr->size);

	/* Capture rx message for coexistence */
	bes_coex_wifi_rx_process(rwnx_hw, skb);

	if (pHdr->type == EVENT_E2A_DEV_READY_IND) {
		bes_psa_dev_status_ind(skb);
		dev_kfree_skb(skb);
	} else if (rwnx_hw) {
		bes_psa_e2a_handler(rwnx_hw, skb);
	} else {
		dev_kfree_skb(skb);
	}
}

void bes_process_tx_msg(struct rwnx_hw *rwnx_hw)
{
	int ret = 0;
	struct rwnx_cmd_a2emsg *cmd_a2e_msg = (struct rwnx_cmd_a2emsg *)rwnx_hw->plat->a2e_msg;

	/* Capture tx messages for coexistence */
	bes_coex_wifi_msg_tx_proc(rwnx_hw, cmd_a2e_msg);

	/* Send message to device */
	if (!bes_tx_loop_direct_send(rwnx_hw->plat, cmd_a2e_msg)) {
		rwnx_hw->msg_push_start++;
		ret = rwnx_hw->plat->msg_push(cmd_a2e_msg, RWNX_CMD_A2EMSG_LEN(cmd_a2e_msg));
		rwnx_hw->msg_push_stop++;
	}

	/* force close when bus error */
	if (ret) {
		bes_err(BES_DBG_SDIO, "%s, %u, SDIO/USB/SPI tx fail:%d.\n", __FUNCTION__, __LINE__, ret);
		bes_tx_loop_force_close(&g_rwnx_plat, true);
	}
}

static void bes_process_tx_data(struct rwnx_hw *rwnx_hw)
{
	int ret = 0;
	struct sk_buff *skb = NULL;
	/* When tx_loop is enabled, skb is not sent */
	if (bes_tx_loop_is_enabled(rwnx_hw->plat)) {
		skb_queue_head_init(&rwnx_hw->plat->tx_q);
		return;
	}

	while (!skb_queue_empty(&rwnx_hw->plat->tx_q)) {
		skb = skb_dequeue(&rwnx_hw->plat->tx_q);
		if (skb) {
			struct tx_packet pkt = {0};

			/* sending data */
			if (bes_tx_loop_is_enabled(rwnx_hw->plat)) {
				/* When tx_loop is enabled, skb is not sent */
				skb_queue_head_init(&rwnx_hw->plat->tx_q);
				break;
			}

			/* print key frame tx */
			bes_dbg_key_frame_tx(skb);

			bes_psa_tx_subframe_peek(skb, &pkt);
			pkt.type = CONN_DATA;
			if (skb_queue_empty(&rwnx_hw->plat->tx_q))
				pkt.tx_now = true;
			rwnx_hw->data_push_start++;
			ret = rwnx_hw->plat->data_push(&pkt);
			rwnx_hw->data_push_stop++;
			if (ret) {
				bes_err(BES_DBG_PSA, "%s, %u, tx fail:%d.\n", __func__, __LINE__, ret);
				bes_tx_loop_force_close(&g_rwnx_plat, true);
			}
		}
	}
}

static int bes_bus_tx_thread(void *data)
{
	struct rwnx_plat *self = (struct rwnx_plat *)data;
	struct rwnx_hw *rwnx_hw = self->drvdata;
	bool tx = false, tx_msg = false, stop = false;
	unsigned long flags;
#ifdef BES_TX_SCHED_PRIORITY
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0)
	struct sched_param param = {.sched_priority = BES_TX_SCHED_PRIORITY,};
	WARN_ON(sched_setscheduler(current, SCHED_FIFO, &param));
#else
	sched_set_fifo(current);
#endif
#endif

	while (1) {
		rwnx_run_flags_clear_bit(rwnx_hw->plat, PLAT_RUN_FLAGS_BUS_TX_PENDING);

		wait_event_interruptible_timeout(self->tx_wait_q, ({
					spin_lock_irqsave(&rwnx_hw->plat->run_flags_lock, flags);
					tx = !!(rwnx_hw->plat->run_flags & BIT(PLAT_RUN_FLAGS_TX_DATA));
					tx_msg = !!(rwnx_hw->plat->run_flags & BIT(PLAT_RUN_FLAGS_TX_MSG));
					rwnx_hw->plat->run_flags &= ~(BIT(PLAT_RUN_FLAGS_TX_DATA) | BIT(PLAT_RUN_FLAGS_TX_MSG));
					if (tx || tx_msg)
						rwnx_hw->plat->run_flags |= BIT(PLAT_RUN_FLAGS_BUS_TX_PENDING);
					spin_unlock_irqrestore(&rwnx_hw->plat->run_flags_lock, flags);
					stop = kthread_should_stop();
					(tx || tx_msg || stop);
				}), MAX_SCHEDULE_TIMEOUT);
		bes_dbg(BES_DBG_PSA, "tx=%d tx_msg:%d stop:%d\n", tx, tx_msg, stop);

		rwnx_hw->tx_thread_in++;

		if (tx_msg)
			bes_process_tx_msg(rwnx_hw);

		if (tx)
			bes_process_tx_data(rwnx_hw);

		if (tx || tx_msg) {
			spin_lock_irqsave(&rwnx_hw->plat->run_flags_lock, flags);
			rwnx_platform_pm_relax(rwnx_hw->plat, TX_WAKELOCK);
			spin_unlock_irqrestore(&rwnx_hw->plat->run_flags_lock, flags);
		}

		if (stop) {
			rwnx_run_flags_clear_bit(rwnx_hw->plat, PLAT_RUN_FLAGS_BUS_TX_PENDING);
			break;
		}
		rwnx_hw->tx_thread_out++;
	}

	return 0;
}

int bes_bus_tx_init(struct rwnx_plat *rwnx_plat)
{
	int i, ret = 0;
	struct task_struct *tmp_thread;

	INIT_LIST_HEAD(&rwnx_plat->tx_buf_list);
	for (i = 0; i < TX_BUFFER_POOL_CNT; i++) {
		list_add_tail(&rwnx_plat->tx_buf_pool[i].hdr, &rwnx_plat->tx_buf_list);
	}

	tmp_thread = kthread_run(bes_bus_tx_thread, rwnx_plat, "bes_bus_tx_thread");
	if (IS_ERR(tmp_thread)) {
		ret = PTR_ERR(tmp_thread);
		bes_err(BES_DBG_PSA, "%s: Failed to start kthread (%d)\n", __func__, ret);
		return ret;
	}

	rwnx_plat->bus_tx_thread = tmp_thread;

	return ret;
}

void bes_bus_tx_stop(struct rwnx_plat *rwnx_plat)
{
	if (rwnx_plat->bus_tx_thread) {
		kthread_stop(rwnx_plat->bus_tx_thread);
		rwnx_plat->bus_tx_thread = NULL;
	}
}

struct tx_buffer *bes_psa_alloc_tx_buffer(void)
{
	struct tx_buffer *tx_buf = NULL;

	if (!list_empty(&g_rwnx_plat.tx_buf_list)) {
		tx_buf = list_first_entry(&g_rwnx_plat.tx_buf_list, struct tx_buffer, hdr);
		list_del(&tx_buf->hdr);
	}

	BUG_ON(!tx_buf);
	return tx_buf;
}

void bes_psa_free_tx_buffer(struct list_head *list)
{
	list_splice_tail_init(list, &g_rwnx_plat.tx_buf_list);
}

static u16 skb_add_to_list(struct sk_buff *skb, u16 offset, struct list_head *list)
{
	u16 len = 0;
	struct tx_buffer *tx_buf;

	tx_buf = bes_psa_alloc_tx_buffer();
	tx_buf->buf = (u8 *)skb->data + offset;
	tx_buf->size = skb_headlen(skb) - offset;
	list_add_tail(&tx_buf->hdr, list);
	len += tx_buf->size;

	if (skb_is_nonlinear(skb)) {
		u8 i;
		struct skb_shared_info *shinfo = skb_shinfo(skb);
		for (i = 0; i < shinfo->nr_frags; i++) {
			tx_buf = bes_psa_alloc_tx_buffer();
			tx_buf->buf = skb_frag_address(&shinfo->frags[i]);
			tx_buf->size = skb_frag_size(&shinfo->frags[i]);
			list_add_tail(&tx_buf->hdr, list);
			len += tx_buf->size;
		}
	}

	WARN_ON(len != skb->len - offset);
	return len;
}

static void bes_psa_tx_subframe_peek(struct sk_buff *skb, struct tx_packet *pkt)
{
	struct rwnx_sw_txhdr *sw_txhdr = ((struct rwnx_txhdr *)skb->data)->sw_hdr;
	struct txdesc_host *txdesc_host = &sw_txhdr->desc;
	u16 offset;
	u8 i;
#ifdef CONFIG_RWNX_AMSDUS_TX
	struct rwnx_amsdu_txhdr *amsdu_txhdr;
#endif

	/* Init pkt */
	memset(pkt, 0, sizeof(struct tx_packet));
	for (i = 0; i < TX_BUFFER_MAX; i++) {
		INIT_LIST_HEAD(&pkt->list[i]);
	}

#ifdef CONFIG_RWNX_FULLMAC
	if (txdesc_host->api.host.flags & TXU_CNTRL_KEY_FRAME)
		pkt->tx_now = true;
#endif

	/* pkt->cnt check */
	BUG_ON(txdesc_host->api.host.packet_cnt > NX_TX_PAYLOAD_MAX);

	if (txdesc_host->api.host.flags & TXU_CNTRL_MGMT)
		offset = sizeof(struct rwnx_txhdr);
	else
		offset = sizeof(struct rwnx_txhdr) + DATA_OFFSET;

	skb_add_to_list(skb, offset, &pkt->list[0]);
	pkt->cnt = 1;

#ifdef CONFIG_RWNX_AMSDUS_TX
	if (!(sw_txhdr->desc.api.host.flags & TXU_CNTRL_AMSDU))
		return;

	/**
	 * amsdu packets:
	 * AMSDU subframe = AMSDU_HDR + LLC/SNAP + PAYLOAD(ip...)
	 * BUF(0)= PSA + txdesc_host + AMSDU(0) + PAD + AMSDU(1) + ...
	 * BUF(n)= PSA + PAD + AMSDU(n) + PAD + AMSDU(n+1) + ...
	 * A new buffer always starts with PSA(struct ipc_header).
	 */
	pkt->is_amsdu = true;
	i = 1;
	list_for_each_entry(amsdu_txhdr, &sw_txhdr->amsdu.hdrs, list) {
		offset = sizeof(struct rwnx_amsdu_txhdr) + sizeof(struct ethhdr);

		if ((txdesc_host->api.host.packet_addr[i] % BES_TRANSFER_LEN_LIMIT) == 0) {
			u8 *buf;
			u16 size;
			struct ipc_header *hdr;

			buf = (u8 *)amsdu_txhdr->skb->data + offset;
			size = txdesc_host->api.host.packet_len[i] + RWNX_TX_PSA_HEAD_LEN;
			WARN_ON(amsdu_txhdr->skb->len != size + offset);

			// fill RWNX_TX_PSA_HEAD
			hdr = (struct ipc_header *)buf;
			hdr->type = EVENT_A2E_DATA_REQ;
			hdr->size = size; // temp size, changed later
			pkt->cnt++;
		} else {
			// skip RWNX_TX_PSA_HEAD
			offset += RWNX_TX_PSA_HEAD_LEN;
		}

		BUG_ON(pkt->cnt > TX_BUFFER_MAX);
		skb_add_to_list(amsdu_txhdr->skb, offset, &pkt->list[pkt->cnt - 1]);
		i++;
	}
	WARN_ON(pkt->cnt != sw_txhdr->buf_cnt);
#endif
}
