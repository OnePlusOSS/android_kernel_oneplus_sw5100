/**
 ******************************************************************************
 *
 * @file rwnx_txq.c
 *
 * Copyright (C) RivieraWaves 2016-2021
 *
 ******************************************************************************
 */

#include "rwnx_defs.h"
#include "rwnx_events.h"
#include "bes_log.h"
#include "bes_tx_loop.h"
#include "bes_pwr.h"

/******************************************************************************
 * Utils functions
 *****************************************************************************/
#ifdef CONFIG_RWNX_SOFTMAC
const int nx_tid_prio[NX_NB_TID_PER_STA] = {15, 7, 14, 6, 13, 5, 12, 4, 11, 3, 8, 0, 10, 2, 9, 1};

static inline int rwnx_txq_sta_idx(struct rwnx_sta *sta, u8 tid)
{
	return sta->sta_idx * NX_NB_TXQ_PER_STA;
}

static inline int rwnx_txq_vif_idx(struct rwnx_vif *vif, u8 ac)
{
	return vif->vif_index * NX_NB_TXQ_PER_VIF + ac + NX_FIRST_VIF_TXQ_IDX;
}

#ifdef CONFIG_MAC80211_TXQ
struct rwnx_txq *rwnx_txq_sta_get(struct rwnx_sta *rwnx_sta, u8 tid)
{
	struct ieee80211_sta *sta = rwnx_to_ieee80211_sta(rwnx_sta);
	struct ieee80211_txq *mac_txq = sta->txq[tid];

	return (struct rwnx_txq *)mac_txq->drv_priv;
}

struct rwnx_txq *rwnx_txq_vif_get(struct rwnx_vif *rwnx_vif, u8 ac)
{
	struct ieee80211_vif *vif = rwnx_to_ieee80211_vif(rwnx_vif);

	/* mac80211 only allocate one txq per vif for Best Effort */
	if (ac == RWNX_HWQ_BE) {
		struct ieee80211_txq *mac_txq = vif->txq;
		if (!mac_txq)
			return NULL;
		return (struct rwnx_txq *)mac_txq->drv_priv;
	}

	if (ac > NX_TXQ_CNT)
		ac = RWNX_HWQ_BK;

	return &rwnx_vif->txqs[ac];
}

#else /* ! CONFIG_MAC80211_TXQ */
struct rwnx_txq *rwnx_txq_sta_get(struct rwnx_sta *sta, u8 tid)
{
	if (tid >= NX_NB_TXQ_PER_STA)
		tid = 0;

	return &sta->txqs[tid];
}

struct rwnx_txq *rwnx_txq_vif_get(struct rwnx_vif *vif, u8 ac)
{
	if (ac > NX_TXQ_CNT)
		ac = RWNX_HWQ_BK;

	return &vif->txqs[ac];
}

#endif /* CONFIG_MAC80211_TXQ */

static inline struct rwnx_sta *rwnx_txq_2_sta(struct rwnx_txq *txq)
{
	if (txq->sta)
		return (struct rwnx_sta *)txq->sta->drv_priv;
	return NULL;
}

#else /* CONFIG_RWNX_FULLMAC */
const int nx_tid_prio[NX_NB_TID_PER_STA] = {7, 6, 5, 4, 3, 0, 2, 1};

static inline int rwnx_txq_sta_idx(struct rwnx_sta *sta, u8 tid)
{
	if (is_multicast_sta(sta->sta_idx))
		return NX_FIRST_VIF_TXQ_IDX + sta->vif_idx;
	else
		return (sta->sta_idx * NX_NB_TXQ_PER_STA) + tid;
}

static inline int rwnx_txq_vif_idx(struct rwnx_vif *vif, u8 type)
{
	return NX_FIRST_VIF_TXQ_IDX + master_vif_idx(vif) + (type * NX_VIRT_DEV_MAX);
}

struct rwnx_txq *rwnx_txq_sta_get(struct rwnx_sta *sta, u8 tid, struct rwnx_hw *rwnx_hw)
{
	if (tid >= NX_NB_TXQ_PER_STA)
		tid = 0;

	return &rwnx_hw->txq[rwnx_txq_sta_idx(sta, tid)];
}

struct rwnx_txq *rwnx_txq_vif_get(struct rwnx_vif *vif, u8 type)
{
	if (type > NX_UNK_TXQ_TYPE)
		type = NX_BCMC_TXQ_TYPE;

	return &vif->rwnx_hw->txq[rwnx_txq_vif_idx(vif, type)];
}

static inline struct rwnx_sta *rwnx_txq_2_sta(struct rwnx_txq *txq)
{
	return txq->sta;
}

#endif /* CONFIG_RWNX_SOFTMAC */

void skb_gki_append(struct sk_buff *old, struct sk_buff *newsk, struct sk_buff_head *list)
{
	unsigned long flags;

	spin_lock_irqsave(&list->lock, flags);
	__skb_queue_after(list, old, newsk);
	spin_unlock_irqrestore(&list->lock, flags);
}

void skb_append(struct sk_buff *old, struct sk_buff *newsk, struct sk_buff_head *list) __attribute__((weak, alias("skb_gki_append")));
/******************************************************************************
 * Init/Deinit functions
 *****************************************************************************/
/**
 * rwnx_txq_init - Initialize a TX queue
 *
 * @txq: TX queue to be initialized
 * @idx: TX queue index
 * @status: TX queue initial status
 * @hwq: Associated HW queue
 * @ndev: Net device this queue belongs to
 *        (may be null for non netdev txq)
 *
 * Each queue is initialized with the credit of @NX_TXQ_INITIAL_CREDITS.
 */
static void rwnx_txq_init(struct rwnx_txq *txq, int idx, u8 status, struct rwnx_hwq *hwq, int tid,
#ifdef CONFIG_RWNX_SOFTMAC
			  struct ieee80211_sta *sta
#else
			  struct rwnx_sta *sta, struct net_device *ndev
#endif
)
{
	int i;

	txq->idx = idx;
	txq->status = status;
	txq->credits = NX_TXQ_INITIAL_CREDITS;
	txq->pkt_sent = 0;
	skb_queue_head_init(&txq->sk_list);
	txq->last_retry_skb = NULL;
	txq->nb_retry = 0;
	txq->hwq = hwq;
	txq->sta = sta;
	for (i = 0; i < CONFIG_USER_MAX; i++)
		txq->pkt_pushed[i] = 0;
	txq->push_limit = 0;
	txq->tid = tid;
#ifdef CONFIG_MAC80211_TXQ
	txq->nb_ready_mac80211 = 0;
#endif
#ifdef CONFIG_RWNX_SOFTMAC
	txq->baw.agg_on = false;
#ifdef CONFIG_RWNX_AMSDUS_TX
	txq->amsdu_anchor = NULL;
	txq->amsdu_ht_len_cap = 0;
	txq->amsdu_vht_len_cap = 0;
#endif /* CONFIG_RWNX_AMSDUS_TX */

#else /* ! CONFIG_RWNX_SOFTMAC */
	txq->ps_id = LEGACY_PS_ID;
	if (idx < NX_FIRST_VIF_TXQ_IDX) {
		int sta_idx = sta->sta_idx;
		int tid = idx - (sta_idx * NX_NB_TXQ_PER_STA);
		if (tid < NX_NB_TID_PER_STA)
			txq->ndev_idx = NX_STA_NDEV_IDX(tid, sta_idx);
		else
			txq->ndev_idx = NDEV_NO_TXQ;
	} else if (idx < NX_FIRST_UNK_TXQ_IDX) {
		txq->ndev_idx = NX_BCMC_TXQ_NDEV_IDX;
	} else {
		txq->ndev_idx = NDEV_NO_TXQ;
	}
	txq->ndev = ndev;
#ifdef CONFIG_RWNX_AMSDUS_TX
	txq->amsdu = NULL;
	txq->amsdu_len = 0;
#endif /* CONFIG_RWNX_AMSDUS_TX */
#endif /* CONFIG_RWNX_SOFTMAC */
}

/**
 * rwnx_drop_skb_stat_sync - synchronize variable status when drop skb
 *
 * @rwnx_hw: main driver data
 * @sw_txhdr: Header for pkt to be pushed
 */
void rwnx_drop_skb_stat_sync(struct rwnx_hw *rwnx_hw, struct rwnx_sw_txhdr *sw_txhdr)
{
	int user = 0;
	struct rwnx_txq *txq = sw_txhdr->txq;

#ifdef CONFIG_RWNX_MUMIMO_TX
	user = RWNX_TXQ_POS_ID(txq);
#endif /* CONFIG_RWNX_MUMIMO_TX */

#if !defined(CONFIG_RWNX_PCIE) && !defined(CONFIG_BES_IBUS)
	atomic_add(sw_txhdr->buf_cnt, &rwnx_hw->plat->buf_tx);
	bes_tx_loop_dec_pending_data_count(rwnx_hw->plat, 1);
#endif

	txq->hwq->credits[user]++;
	rwnx_hw->stats.cfm_balance[txq->hwq->id]--;
}

/**
 * rwnx_txq_drop_skb - Drop the buffer skb from the TX queue
 *
 * @txq:          TX queue
 * @skb:          skb packet that should be dropped.
 * @rwnx_hw:      Driver main data
 * @retry_packet: Is it a retry packet
 *
 */
void rwnx_txq_drop_skb(struct rwnx_txq *txq, struct sk_buff *skb, struct rwnx_hw *rwnx_hw, bool retry_packet)
{
	struct rwnx_sw_txhdr *sw_txhdr = ((struct rwnx_txhdr *)skb->data)->sw_hdr;
	unsigned long queued_time = 0;

	if (txq->sta == NULL) {
		bes_err(BES_DBG_TXQ, "%s txq->sta is not avaliable\n", __func__);
		return;
	}

	// Count dropped skb number for sta
	txq->sta->stats.drop_tx++;

	skb_unlink(skb, &txq->sk_list);

#ifdef CONFIG_RWNX_FULLMAC
	queued_time = jiffies - sw_txhdr->jiffies;
#endif
	trace_txq_drop_skb(skb, txq, queued_time);

#ifdef CONFIG_RWNX_AMSDUS_TX
	if (sw_txhdr->desc.api.host.packet_cnt > 1) {
		struct rwnx_amsdu_txhdr *amsdu_txhdr;
		list_for_each_entry(amsdu_txhdr, &sw_txhdr->amsdu.hdrs, list) {
			rwnx_ipc_buf_a2e_release(rwnx_hw, &amsdu_txhdr->ipc_data);
			rwnx_ipc_sta_buffer(rwnx_hw, txq->sta, txq->tid, -amsdu_txhdr->msdu_len);
			dev_kfree_skb_any(amsdu_txhdr->skb);
		}
#ifdef CONFIG_RWNX_FULLMAC
		if (txq->amsdu == sw_txhdr)
			txq->amsdu = NULL;
#endif
	}
#endif
	rwnx_ipc_buf_a2e_release(rwnx_hw, &sw_txhdr->ipc_data);
	rwnx_ipc_sta_buffer(rwnx_hw, txq->sta, txq->tid, -sw_txhdr->frame_len);
	kmem_cache_free(rwnx_hw->sw_txhdr_cache, sw_txhdr);
	if (retry_packet) {
		txq->nb_retry--;
		if (txq->nb_retry == 0) {
			WARN(skb != txq->last_retry_skb, "last dropped retry buffer is not the expected one");
			txq->last_retry_skb = NULL;
		}
	}
#ifdef CONFIG_RWNX_SOFTMAC
	else
		txq->hwq->len--; // hwq->len doesn't count skb to retry
	ieee80211_free_txskb(rwnx_hw->hw, skb);
#else
	dev_kfree_skb_any(skb);
#endif /* CONFIG_RWNX_SOFTMAC */
}

/**
 * rwnx_txq_flush - Flush all buffers queued for a TXQ
 *
 * @rwnx_hw: main driver data
 * @txq: txq to flush
 */
void rwnx_txq_flush(struct rwnx_hw *rwnx_hw, struct rwnx_txq *txq)
{
	int i, pushed = 0;

	while (!skb_queue_empty(&txq->sk_list)) {
		rwnx_txq_drop_skb(txq, skb_peek(&txq->sk_list), rwnx_hw, txq->nb_retry);
	}

	for (i = 0; i < CONFIG_USER_MAX; i++) {
		pushed += txq->pkt_pushed[i];
	}

	if (pushed)
		dev_warn(rwnx_hw->dev, "TXQ[%d]: %d skb still pushed to the FW", txq->idx, pushed);
}

bool rwnx_txq_sk_list_empty(struct rwnx_hw *rwnx_hw)
{
	int i = 0;

	for(i = 0; i < NX_NB_TXQ; i++) {
		if((rwnx_hw->txq[i].idx != TXQ_INACTIVE) && (!skb_queue_empty(&rwnx_hw->txq[i].sk_list))) /* idx acive and txq[idx].list not empty */
		return false;
	}

	return true;
}

/**
 * rwnx_txq_deinit - De-initialize a TX queue
 *
 * @rwnx_hw: Driver main data
 * @txq: TX queue to be de-initialized
 * Any buffer stuck in a queue will be freed.
 */
static void rwnx_txq_deinit(struct rwnx_hw *rwnx_hw, struct rwnx_txq *txq)
{
	spin_lock_bh(&rwnx_hw->tx_lock);
	if (txq->idx == TXQ_INACTIVE) {
		spin_unlock_bh(&rwnx_hw->tx_lock);
		return;
	}

	rwnx_txq_del_from_hw_list(txq);
	txq->idx = TXQ_INACTIVE;

	rwnx_txq_flush(rwnx_hw, txq);
	spin_unlock_bh(&rwnx_hw->tx_lock);
}

/**
 * rwnx_txq_vif_init - Initialize all TXQ linked to a vif
 *
 * @rwnx_hw: main driver data
 * @rwnx_vif: Pointer on VIF
 * @status: Intial txq status
 *
 * Softmac : 1 VIF TXQ per HWQ
 *
 * Fullmac : 1 VIF TXQ for BC/MC
 *           1 VIF TXQ for MGMT to unknown STA
 */
void rwnx_txq_vif_init(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, u8 status)
{
	struct rwnx_txq *txq;
	int idx;

#ifdef CONFIG_RWNX_SOFTMAC
	int ac;

	idx = rwnx_txq_vif_idx(rwnx_vif, 0);
	foreach_vif_txq(rwnx_vif, txq, ac)
	{
		if (txq) {
			rwnx_txq_init(txq, idx, status, &rwnx_hw->hwq[ac], 0, NULL);
#ifdef CONFIG_MAC80211_TXQ
			if (ac != RWNX_HWQ_BE)
				txq->nb_ready_mac80211 = NOT_MAC80211_TXQ;
#endif
		}
		idx++;
	}

#else
	struct rwnx_sta *rwnx_sta;

	txq = rwnx_txq_vif_get(rwnx_vif, NX_BCMC_TXQ_TYPE);
	idx = rwnx_txq_vif_idx(rwnx_vif, NX_BCMC_TXQ_TYPE);
	rwnx_sta = rwnx_rx_get_sta(rwnx_hw, rwnx_vif->ap.bcmc_index);

	WARN_ON(rwnx_sta == NULL);

	rwnx_txq_init(txq, idx, status, &rwnx_hw->hwq[RWNX_HWQ_BE], 0, rwnx_sta, rwnx_vif->ndev);

	txq = rwnx_txq_vif_get(rwnx_vif, NX_UNK_TXQ_TYPE);
	idx = rwnx_txq_vif_idx(rwnx_vif, NX_UNK_TXQ_TYPE);
	rwnx_txq_init(txq, idx, status, &rwnx_hw->hwq[RWNX_HWQ_VO], TID_MGT, NULL, rwnx_vif->ndev);
#endif /* CONFIG_RWNX_SOFTMAC */
}

/**
 * rwnx_txq_vif_deinit - Deinitialize all TXQ linked to a vif
 *
 * @rwnx_hw: main driver data
 * @rwnx_vif: Pointer on VIF
 */
void rwnx_txq_vif_deinit(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif)
{
	struct rwnx_txq *txq;

#ifdef CONFIG_RWNX_SOFTMAC
	int ac;

	foreach_vif_txq(rwnx_vif, txq, ac)
	{
		if (txq)
			rwnx_txq_deinit(rwnx_hw, txq);
	}

#else
	txq = rwnx_txq_vif_get(rwnx_vif, NX_BCMC_TXQ_TYPE);
	rwnx_txq_deinit(rwnx_hw, txq);

	txq = rwnx_txq_vif_get(rwnx_vif, NX_UNK_TXQ_TYPE);
	rwnx_txq_deinit(rwnx_hw, txq);

#endif /* CONFIG_RWNX_SOFTMAC */
}

/**
 * rwnx_txq_sta_init - Initialize TX queues for a STA
 *
 * @rwnx_hw: Main driver data
 * @rwnx_sta: STA for which tx queues need to be initialized
 * @status: Intial txq status
 *
 * This function initialize all the TXQ associated to a STA.
 * Softmac : 1 TXQ per TID
 *
 * Fullmac : 1 TXQ per TID (limited to 8)
 *           1 TXQ for MGMT
 */
void rwnx_txq_sta_init(struct rwnx_hw *rwnx_hw, struct rwnx_sta *rwnx_sta, u8 status)
{
	struct rwnx_txq *txq;
	int tid, idx;
	struct rwnx_vif *rwnx_vif;

#ifdef CONFIG_RWNX_SOFTMAC
	struct ieee80211_sta *sta = rwnx_to_ieee80211_sta(rwnx_sta);
	idx = rwnx_txq_sta_idx(rwnx_sta, 0);

	foreach_sta_txq(rwnx_sta, txq, tid, rwnx_hw) {
		rwnx_txq_init(txq, idx, status, &rwnx_hw->hwq[rwnx_tid2hwq[tid]], tid, sta);
		idx++;
	}

#else
	if (!rwnx_sta) {
		bes_err(BES_DBG_MSG_RX, "%s ERROR: sta is not find\n", __func__);
		return;
	}

	rwnx_vif = rwnx_rx_get_vif(rwnx_hw, rwnx_sta->vif_idx);

	if (!rwnx_vif) {
		bes_err(BES_DBG_MSG_RX, "%s ERROR: vif is not find\n", __func__);
		return;
	}

	idx = rwnx_txq_sta_idx(rwnx_sta, 0);
	foreach_sta_txq(rwnx_sta, txq, tid, rwnx_hw) {
		rwnx_txq_init(txq, idx, status, &rwnx_hw->hwq[rwnx_tid2hwq[tid]], tid, rwnx_sta, rwnx_vif->ndev);
		txq->ps_id = rwnx_sta->uapsd_tids & (1 << tid) ? UAPSD_ID : LEGACY_PS_ID;
		idx++;
	}

#endif /* CONFIG_RWNX_SOFTMAC*/
	rwnx_ipc_sta_buffer_init(rwnx_hw, rwnx_sta->sta_idx);
}

/**
 * rwnx_txq_sta_deinit - Deinitialize TX queues for a STA
 *
 * @rwnx_hw: Main driver data
 * @rwnx_sta: STA for which tx queues need to be deinitialized
 */
void rwnx_txq_sta_deinit(struct rwnx_hw *rwnx_hw, struct rwnx_sta *rwnx_sta)
{
	struct rwnx_txq *txq;
	int tid;

	foreach_sta_txq(rwnx_sta, txq, tid, rwnx_hw) {
		rwnx_txq_deinit(rwnx_hw, txq);
	}
}

#ifdef CONFIG_RWNX_FULLMAC
/**
 * rwnx_txq_unk_vif_init - Initialize TXQ for unknown STA linked to a vif
 *
 * @rwnx_vif: Pointer on VIF
 */
void rwnx_txq_unk_vif_init(struct rwnx_vif *rwnx_vif)
{
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	struct rwnx_txq *txq;
	int idx;

	txq = rwnx_txq_vif_get(rwnx_vif, NX_UNK_TXQ_TYPE);
	idx = rwnx_txq_vif_idx(rwnx_vif, NX_UNK_TXQ_TYPE);
	rwnx_txq_init(txq, idx, 0, &rwnx_hw->hwq[RWNX_HWQ_VO], TID_MGT, NULL, rwnx_vif->ndev);
}

/**
 * rwnx_txq_unk_vif_deinit - Deinitialize TXQ for unknown STA linked to a vif
 *
 * @rwnx_vif: Pointer on VIF
 */
void rwnx_txq_unk_vif_deinit(struct rwnx_vif *rwnx_vif)
{
	struct rwnx_txq *txq;

	txq = rwnx_txq_vif_get(rwnx_vif, NX_UNK_TXQ_TYPE);
	rwnx_txq_deinit(rwnx_vif->rwnx_hw, txq);
}

/**
 * rwnx_txq_offchan_init - Initialize TX queue for the transmission on a offchannel
 *
 * @vif: Interface for which the queue has to be initialized
 *
 * NOTE: Offchannel txq is only active for the duration of the ROC
 */
void rwnx_txq_offchan_init(struct rwnx_vif *rwnx_vif)
{
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	struct rwnx_txq *txq;

	txq = &rwnx_hw->txq[NX_OFF_CHAN_TXQ_IDX];
	rwnx_txq_init(txq, NX_OFF_CHAN_TXQ_IDX, (1 << RWNX_TXQ_STOP_CHAN), &rwnx_hw->hwq[RWNX_HWQ_VO], TID_MGT, NULL,
		      rwnx_vif->ndev);
}

/**
 * rwnx_deinit_offchan_txq - Deinitialize TX queue for offchannel
 *
 * @vif: Interface that manages the STA
 *
 * This function deintialize txq for one STA.
 * Any buffer stuck in a queue will be freed.
 */
void rwnx_txq_offchan_deinit(struct rwnx_vif *rwnx_vif)
{
	struct rwnx_txq *txq;

	txq = &rwnx_vif->rwnx_hw->txq[NX_OFF_CHAN_TXQ_IDX];
	rwnx_txq_deinit(rwnx_vif->rwnx_hw, txq);
}

/**
 * rwnx_txq_tdls_vif_init - Initialize TXQ vif for TDLS
 *
 * @rwnx_vif: Pointer on VIF
 */
void rwnx_txq_tdls_vif_init(struct rwnx_vif *rwnx_vif)
{
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;

	if (!(rwnx_hw->wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS))
		return;

	rwnx_txq_unk_vif_init(rwnx_vif);
}

/**
 * rwnx_txq_tdls_vif_deinit - Deinitialize TXQ vif for TDLS
 *
 * @rwnx_vif: Pointer on VIF
 */
void rwnx_txq_tdls_vif_deinit(struct rwnx_vif *rwnx_vif)
{
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;

	if (!(rwnx_hw->wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS))
		return;

	rwnx_txq_unk_vif_deinit(rwnx_vif);
}

/**
 * rwnx_txq_drop_old_traffic - Drop pkt queued for too long in a TXQ
 *
 * @txq: TXQ to process
 * @rwnx_hw: Driver main data
 * @skb_timeout: Max queue duration, in jiffies, for this queue
 * @dropped: Updated to inidicate if at least one skb was dropped
 *
 * @return Whether there is still pkt queued in this queue.
 */
static bool rwnx_txq_drop_old_traffic(struct rwnx_txq *txq, struct rwnx_hw *rwnx_hw, unsigned long skb_timeout,
				      bool *dropped)
{
	struct sk_buff *skb, *skb_next;
	bool pkt_queued = false;
	int retry_packet = txq->nb_retry;

	spin_lock_bh(&rwnx_hw->tx_lock);

	if (txq->idx == TXQ_INACTIVE) {
		spin_unlock_bh(&rwnx_hw->tx_lock);
		return false;
	}

	skb_queue_walk_safe(&txq->sk_list, skb, skb_next)
	{
		struct rwnx_sw_txhdr *sw_txhdr;

		if (retry_packet) {
			// Don't drop retry packets
			retry_packet--;
			continue;
		}

		sw_txhdr = ((struct rwnx_txhdr *)skb->data)->sw_hdr;

		if (!time_after(jiffies, sw_txhdr->jiffies + skb_timeout)) {
			pkt_queued = true;
			break;
		}

		*dropped = true;
		rwnx_txq_drop_skb(txq, skb, rwnx_hw, false);
		if (txq->sta && txq->sta->ps.active) {
			txq->sta->ps.pkt_ready[txq->ps_id]--;
			if (txq->sta->ps.pkt_ready[txq->ps_id] == 0)
				rwnx_set_traffic_status(rwnx_hw, txq->sta, false, txq->ps_id);

			// drop packet during PS service period ...
			if (txq->sta->ps.sp_cnt[txq->ps_id]) {
				txq->sta->ps.sp_cnt[txq->ps_id]--;
				if (txq->push_limit)
					txq->push_limit--;
				if (WARN(((txq->ps_id == UAPSD_ID) && (txq->sta->ps.sp_cnt[txq->ps_id] == 0)),
					 "Drop last packet of UAPSD service period")) {
					// TODO: inform FW to end SP
				}
			}
			trace_ps_drop(txq->sta);
		}
	}

	if (skb_queue_empty(&txq->sk_list)) {
		rwnx_txq_del_from_hw_list(txq);
		txq->pkt_sent = 0;
	}

	spin_unlock_bh(&rwnx_hw->tx_lock);

	/* restart netdev queue if number no more queued buffer */
	if (unlikely(test_bit(RWNX_TXQ_NDEV_FLOW_CTRL, &txq->status) && skb_queue_empty(&txq->sk_list))) {
		clear_bit(RWNX_TXQ_NDEV_FLOW_CTRL, &txq->status);
		netif_wake_subqueue(txq->ndev, txq->ndev_idx);
		trace_txq_flowctrl_restart(txq);
	}

	bes_dbg(BES_DBG_TXQ, "%s,exit, pkt_queued=%d, dropped=%d\n", __func__, pkt_queued, *dropped);
	return pkt_queued;
}

/**
 * rwnx_txq_drop_ap_vif_old_traffic - Drop pkt queued for too long in TXQs
 * linked to an "AP" vif (AP, MESH, P2P_GO)
 *
 * @vif: Vif to process
 * @return Whether there is still pkt queued in any TXQ.
 */
static bool rwnx_txq_drop_ap_vif_old_traffic(struct rwnx_vif *vif)
{
	struct rwnx_sta *sta;
	unsigned long timeout = (vif->ap.bcn_interval * HZ * 3) >> 10;
	bool pkt_queued = false;
	bool pkt_dropped = false;

	// Should never be needed but still check VIF queues
	rwnx_txq_drop_old_traffic(rwnx_txq_vif_get(vif, NX_BCMC_TXQ_TYPE), vif->rwnx_hw, RWNX_TXQ_MAX_QUEUE_JIFFIES,
				&pkt_dropped);
	rwnx_txq_drop_old_traffic(rwnx_txq_vif_get(vif, NX_UNK_TXQ_TYPE), vif->rwnx_hw, RWNX_TXQ_MAX_QUEUE_JIFFIES,
				&pkt_dropped);
	WARN(pkt_dropped, "Dropped packet in BCMC/UNK queue");

	list_for_each_entry(sta, &vif->ap.sta_list, list) {
		struct rwnx_txq *txq;
		int tid;
		foreach_sta_txq(sta, txq, tid, vif->rwnx_hw) {
			pkt_queued |= rwnx_txq_drop_old_traffic(txq, vif->rwnx_hw, timeout * sta->listen_interval,
								&pkt_dropped);
		}
	}

	return pkt_queued;
}

/**
 * rwnx_txq_drop_sta_vif_old_traffic - Drop pkt queued for too long in TXQs
 * linked to a "STA" vif. In theory this should not be required as there is no
 * case where traffic can accumulate in a STA interface.
 *
 * @vif: Vif to process
 * @return Whether there is still pkt queued in any TXQ.
 */
static bool rwnx_txq_drop_sta_vif_old_traffic(struct rwnx_vif *vif)
{
	struct rwnx_txq *txq;
	bool pkt_queued = false, pkt_dropped = false;
	int tid;
	struct rwnx_sta *rwnx_sta = vif->sta.ap;

	bes_dbg(BES_DBG_TXQ, "%s, vif->tdls_status=0x%x, vif->sta.ap=%d\n", __func__, vif->tdls_status,
		 (rwnx_sta == NULL) ? 0 : 1);
	if (vif->tdls_status == TDLS_LINK_ACTIVE) {
		txq = rwnx_txq_vif_get(vif, NX_UNK_TXQ_TYPE);
		pkt_queued |= rwnx_txq_drop_old_traffic(txq, vif->rwnx_hw, RWNX_TXQ_MAX_QUEUE_JIFFIES, &pkt_dropped);
		foreach_sta_txq(vif->sta.tdls_sta, txq, tid, vif->rwnx_hw) {
			pkt_queued |=
				rwnx_txq_drop_old_traffic(txq, vif->rwnx_hw, RWNX_TXQ_MAX_QUEUE_JIFFIES, &pkt_dropped);
		}
	}

	if (rwnx_sta) {
		foreach_sta_txq(rwnx_sta, txq, tid, vif->rwnx_hw) {
			pkt_queued |=
				rwnx_txq_drop_old_traffic(txq, vif->rwnx_hw, RWNX_TXQ_MAX_QUEUE_JIFFIES, &pkt_dropped);
		}
	}

	if (pkt_dropped)
		netdev_warn(vif->ndev, "Dropped packet in STA interface TXQs");
	return pkt_queued;
}

/**
 * rwnx_txq_cleanup_timer_cb - callack for TXQ cleaup timer
 * Used to prevent pkt to accumulate in TXQ. The main use case is for AP
 * interface with client in Power Save mode but just in case all TXQs are
 * checked.
 *
 * @t: timer structure
 */
static void rwnx_txq_cleanup_timer_cb(struct timer_list *t)
{
	struct rwnx_hw *rwnx_hw = from_timer(rwnx_hw, t, txq_cleanup);
	struct rwnx_vif *vif;
	bool pkt_queue = false;

	if (atomic_read(&rwnx_hw->txq_cleanup_available) == 0) {
		bes_err(BES_DBG_TXQ, "%s timer is not avaliable\n", __func__);
		return;
	}

	spin_lock_bh(&rwnx_hw->cb_lock);
	list_for_each_entry(vif, &rwnx_hw->vifs, list) {
		/* when vif down, txqs had been cleaned up */
		if (vif->up == false)
			continue;

		switch (RWNX_VIF_TYPE(vif)) {
		case NL80211_IFTYPE_AP:
		case NL80211_IFTYPE_P2P_GO:
		case NL80211_IFTYPE_MESH_POINT:
			pkt_queue |= rwnx_txq_drop_ap_vif_old_traffic(vif);
			break;
		case NL80211_IFTYPE_STATION:
		case NL80211_IFTYPE_P2P_CLIENT:
			pkt_queue |= rwnx_txq_drop_sta_vif_old_traffic(vif);
			break;
		case NL80211_IFTYPE_AP_VLAN:
		case NL80211_IFTYPE_MONITOR:
		default:
			continue;
		}
	}
	spin_unlock_bh(&rwnx_hw->cb_lock);

	if (pkt_queue)
		mod_timer(t, jiffies + RWNX_TXQ_CLEANUP_INTERVAL);
}

/**
 * rwnx_txq_start_cleanup_timer - Start 'cleanup' timer if not started
 *
 * @rwnx_hw: Driver main data
 */
void rwnx_txq_start_cleanup_timer(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta)
{
	if (atomic_read(&rwnx_hw->txq_cleanup_available) == 0) {
		bes_err(BES_DBG_TXQ, "%s timer is not avaliable\n", __func__);
		return;
	}
	if (sta && !is_multicast_sta(sta->sta_idx) && !timer_pending(&rwnx_hw->txq_cleanup))
		mod_timer(&rwnx_hw->txq_cleanup, jiffies + RWNX_TXQ_CLEANUP_INTERVAL);
}

/**
 * rwnx_txq_prepare - Global initialization of txq
 *
 * @rwnx_hw: Driver main data
 */
void rwnx_txq_prepare(struct rwnx_hw *rwnx_hw)
{
	int i;

	for (i = 0; i < NX_NB_TXQ; i++) {
		rwnx_hw->txq[i].idx = TXQ_INACTIVE;
	}

	timer_setup(&rwnx_hw->txq_cleanup, rwnx_txq_cleanup_timer_cb, 0);
	atomic_set(&rwnx_hw->txq_cleanup_available, 1);
}

#endif // CONFIG_RWNX_FULLMAC

/******************************************************************************
 * Start/Stop functions
 *****************************************************************************/
/**
 * rwnx_txq_add_to_hw_list - Add TX queue to a HW queue schedule list.
 *
 * @txq: TX queue to add
 *
 * Add the TX queue if not already present in the HW queue list.
 * To be called with tx_lock hold
 */
void rwnx_txq_add_to_hw_list(struct rwnx_txq *txq)
{
	if (!test_bit(RWNX_TXQ_IN_HWQ_LIST, &txq->status)) {
		trace_txq_add_to_hw(txq);
		set_bit(RWNX_TXQ_IN_HWQ_LIST, &txq->status);
		list_add_tail(&txq->sched_list, &txq->hwq->list);
		txq->hwq->need_processing = true;
	}
}

/**
 * rwnx_txq_del_from_hw_list - Delete TX queue from a HW queue schedule list.
 *
 * @txq: TX queue to delete
 *
 * Remove the TX queue from the HW queue list if present.
 * To be called with tx_lock hold
 */
void rwnx_txq_del_from_hw_list(struct rwnx_txq *txq)
{
	if (test_bit(RWNX_TXQ_IN_HWQ_LIST, &txq->status)) {
		trace_txq_del_from_hw(txq);
		clear_bit(RWNX_TXQ_IN_HWQ_LIST, &txq->status);
		list_del(&txq->sched_list);
	}
}

/**
 * rwnx_txq_skb_ready - Check if skb are available for the txq
 *
 * @txq: Pointer on txq
 * @return True if there are buffer ready to be pushed on this txq,
 * false otherwise
 */
static inline bool rwnx_txq_skb_ready(struct rwnx_txq *txq)
{
#ifdef CONFIG_MAC80211_TXQ
	if (txq->nb_ready_mac80211 != NOT_MAC80211_TXQ)
		return ((txq->nb_ready_mac80211 > 0) || !skb_queue_empty(&txq->sk_list));
	else
#endif
		return !skb_queue_empty(&txq->sk_list);
}

/**
 * rwnx_txq_start - Try to Start one TX queue
 *
 * @txq: TX queue to start
 * @reason: reason why the TX queue is started (among RWNX_TXQ_STOP_xxx)
 *
 * Re-start the TX queue for one reason.
 * If after this the txq is no longer stopped and some buffers are ready,
 * the TX queue is also added to HW queue list.
 * To be called with tx_lock hold
 */
void rwnx_txq_start(struct rwnx_txq *txq, u16 reason)
{
	BUG_ON(txq == NULL);
	if (txq->idx != TXQ_INACTIVE
#if defined(CONFIG_RWNX_PCIE) || defined(CONFIG_BES_IBUS)
	    && test_bit(reason, &txq->status)
#endif
	) {
		trace_txq_start(txq, reason);
		clear_bit(reason, &txq->status);
		if (!rwnx_txq_is_stopped(txq) && rwnx_txq_skb_ready(txq))
			rwnx_txq_add_to_hw_list(txq);
	}
}

/**
 * rwnx_txq_stop - Stop one TX queue
 *
 * @txq: TX queue to stop
 * @reason: reason why the TX queue is stopped (among RWNX_TXQ_STOP_xxx)
 *
 * Stop the TX queue. It will remove the TX queue from HW queue list
 * To be called with tx_lock hold
 */
void rwnx_txq_stop(struct rwnx_txq *txq, u16 reason)
{
	BUG_ON(txq == NULL);
	if (txq->idx != TXQ_INACTIVE) {
		trace_txq_stop(txq, reason);
		set_bit(reason, &txq->status);
		rwnx_txq_del_from_hw_list(txq);
	}
}

/**
 * rwnx_txq_sta_start - Start all the TX queue linked to a STA
 *
 * @sta: STA whose TX queues must be re-started
 * @reason: Reason why the TX queue are restarted (among RWNX_TXQ_STOP_xxx)
 * @rwnx_hw: Driver main data
 *
 * This function will re-start all the TX queues of the STA for the reason
 * specified. It can be :
 * - RWNX_TXQ_STOP_STA_PS: the STA is no longer in power save mode
 * - RWNX_TXQ_STOP_VIF_PS: the VIF is in power save mode (p2p absence)
 * - RWNX_TXQ_STOP_CHAN: the STA's VIF is now on the current active channel
 *
 * Any TX queue with buffer ready and not Stopped for other reasons, will be
 * added to the HW queue list
 * To be called with tx_lock hold
 */
void rwnx_txq_sta_start(struct rwnx_sta *rwnx_sta, u16 reason
#ifdef CONFIG_RWNX_FULLMAC
			,
			struct rwnx_hw *rwnx_hw
#endif
)
{
	struct rwnx_txq *txq;
	int tid;

	trace_txq_sta_start(rwnx_sta->sta_idx);

	foreach_sta_txq(rwnx_sta, txq, tid, rwnx_hw) {
		rwnx_txq_start(txq, reason);
	}
}

/**
 * rwnx_stop_sta_txq - Stop all the TX queue linked to a STA
 *
 * @sta: STA whose TX queues must be stopped
 * @reason: Reason why the TX queue are stopped (among RWNX_TX_STOP_xxx)
 * @rwnx_hw: Driver main data
 *
 * This function will stop all the TX queues of the STA for the reason
 * specified. It can be :
 * - RWNX_TXQ_STOP_STA_PS: the STA is in power save mode
 * - RWNX_TXQ_STOP_VIF_PS: the VIF is in power save mode (p2p absence)
 * - RWNX_TXQ_STOP_CHAN: the STA's VIF is not on the current active channel
 *
 * Any TX queue present in a HW queue list will be removed from this list.
 * To be called with tx_lock hold
 */
void rwnx_txq_sta_stop(struct rwnx_sta *rwnx_sta, u16 reason
#ifdef CONFIG_RWNX_FULLMAC
		       ,
		       struct rwnx_hw *rwnx_hw
#endif
)
{
	struct rwnx_txq *txq;
	int tid;

	if (!rwnx_sta)
		return;

	trace_txq_sta_stop(rwnx_sta->sta_idx);
	foreach_sta_txq(rwnx_sta, txq, tid, rwnx_hw) {
		rwnx_txq_stop(txq, reason);
	}
}

#ifdef CONFIG_RWNX_SOFTMAC
void rwnx_txq_tdls_sta_start(struct rwnx_sta *rwnx_sta, u16 reason, struct rwnx_hw *rwnx_hw)
#else
void rwnx_txq_tdls_sta_start(struct rwnx_vif *rwnx_vif, u16 reason, struct rwnx_hw *rwnx_hw)
#endif
{
#ifdef CONFIG_RWNX_SOFTMAC
	trace_txq_vif_start(rwnx_sta->vif_idx);
	spin_lock_bh(&rwnx_hw->tx_lock);

	if (rwnx_sta->tdls.active) {
		rwnx_txq_sta_start(rwnx_sta, reason);
	}

	spin_unlock_bh(&rwnx_hw->tx_lock);
#else
	trace_txq_vif_start(rwnx_vif->vif_index);
	spin_lock_bh(&rwnx_hw->tx_lock);

	if (rwnx_vif->sta.tdls_sta)
		rwnx_txq_sta_start(rwnx_vif->sta.tdls_sta, reason, rwnx_hw);

	spin_unlock_bh(&rwnx_hw->tx_lock);
#endif
}

#ifdef CONFIG_RWNX_SOFTMAC
void rwnx_txq_tdls_sta_stop(struct rwnx_sta *rwnx_sta, u16 reason, struct rwnx_hw *rwnx_hw)
#else
void rwnx_txq_tdls_sta_stop(struct rwnx_vif *rwnx_vif, u16 reason, struct rwnx_hw *rwnx_hw)
#endif
{
#ifdef CONFIG_RWNX_SOFTMAC
	trace_txq_vif_stop(rwnx_sta->vif_idx);

	spin_lock_bh(&rwnx_hw->tx_lock);

	if (rwnx_sta->tdls.active) {
		rwnx_txq_sta_stop(rwnx_sta, reason);
	}

	spin_unlock_bh(&rwnx_hw->tx_lock);
#else
	trace_txq_vif_stop(rwnx_vif->vif_index);

	spin_lock_bh(&rwnx_hw->tx_lock);

	if (rwnx_vif->sta.tdls_sta)
		rwnx_txq_sta_stop(rwnx_vif->sta.tdls_sta, reason, rwnx_hw);

	spin_unlock_bh(&rwnx_hw->tx_lock);
#endif
}

#ifdef CONFIG_RWNX_FULLMAC
static inline void rwnx_txq_vif_for_each_sta(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
					     void (*f)(struct rwnx_sta *, u16, struct rwnx_hw *), u16 reason)
{
	struct rwnx_sta *rwnx_sta = rwnx_vif->sta.ap;

	switch (RWNX_VIF_TYPE(rwnx_vif)) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT: {
		if (rwnx_vif->tdls_status == TDLS_LINK_ACTIVE)
			f(rwnx_vif->sta.tdls_sta, reason, rwnx_hw);
		if (!WARN_ON(rwnx_sta == NULL))
			f(rwnx_sta, reason, rwnx_hw);
		break;
	}
	case NL80211_IFTYPE_AP_VLAN:
		rwnx_vif = rwnx_vif->ap_vlan.master;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_P2P_GO: {
		struct rwnx_sta *sta;
		list_for_each_entry(sta, &rwnx_vif->ap.sta_list, list) {
			f(sta, reason, rwnx_hw);
		}
		break;
	}
	default:
		BUG();
		break;
	}
}

#endif

/**
 * rwnx_txq_vif_start - START TX queues of all STA associated to the vif
 *                      and vif's TXQ
 *
 * @vif: Interface to start
 * @reason: Start reason (RWNX_TXQ_STOP_CHAN or RWNX_TXQ_STOP_VIF_PS)
 * @rwnx_hw: Driver main data
 *
 * Iterate over all the STA associated to the vif and re-start them for the
 * reason @reason
 * Take tx_lock
 */
void rwnx_txq_vif_start(struct rwnx_vif *rwnx_vif, u16 reason, struct rwnx_hw *rwnx_hw)
{
	struct rwnx_txq *txq;
#ifdef CONFIG_RWNX_SOFTMAC
	struct rwnx_sta *rwnx_sta;
	int ac;
#endif

	trace_txq_vif_start(rwnx_vif->vif_index);

	spin_lock_bh(&rwnx_hw->tx_lock);

#ifdef CONFIG_RWNX_SOFTMAC
	list_for_each_entry(rwnx_sta, &rwnx_vif->stations, list) {
		if ((!rwnx_vif->roc_tdls) || (rwnx_sta->tdls.active && rwnx_vif->roc_tdls && rwnx_sta->tdls.chsw_en))
			rwnx_txq_sta_start(rwnx_sta, reason);
	}

	foreach_vif_txq(rwnx_vif, txq, ac)
	{
		if (txq)
			rwnx_txq_start(txq, reason);
	}
#else
	// Reject if monitor interface
	if (rwnx_vif->wdev.iftype == NL80211_IFTYPE_MONITOR)
		goto end;

	if (rwnx_vif->roc_tdls && rwnx_vif->sta.tdls_sta && rwnx_vif->sta.tdls_sta->tdls.chsw_en) {
		rwnx_txq_sta_start(rwnx_vif->sta.tdls_sta, reason, rwnx_hw);
	}
	if (!rwnx_vif->roc_tdls) {
		rwnx_txq_vif_for_each_sta(rwnx_hw, rwnx_vif, rwnx_txq_sta_start, reason);
	}

	txq = rwnx_txq_vif_get(rwnx_vif, NX_BCMC_TXQ_TYPE);
	rwnx_txq_start(txq, reason);
	txq = rwnx_txq_vif_get(rwnx_vif, NX_UNK_TXQ_TYPE);
	rwnx_txq_start(txq, reason);

end:
#endif /* CONFIG_RWNX_SOFTMAC */

	spin_unlock_bh(&rwnx_hw->tx_lock);
}

/**
 * rwnx_txq_vif_stop - STOP TX queues of all STA associated to the vif
 *
 * @vif: Interface to stop
 * @arg: Stop reason (RWNX_TXQ_STOP_CHAN or RWNX_TXQ_STOP_VIF_PS)
 * @rwnx_hw: Driver main data
 *
 * Iterate over all the STA associated to the vif and stop them for the
 * reason RWNX_TXQ_STOP_CHAN or RWNX_TXQ_STOP_VIF_PS
 * Take tx_lock
 */
void rwnx_txq_vif_stop(struct rwnx_vif *rwnx_vif, u16 reason, struct rwnx_hw *rwnx_hw)
{
	struct rwnx_txq *txq;
#ifdef CONFIG_RWNX_SOFTMAC
	struct rwnx_sta *sta;
	int ac;
#endif

	trace_txq_vif_stop(rwnx_vif->vif_index);
	spin_lock_bh(&rwnx_hw->tx_lock);

#ifdef CONFIG_RWNX_SOFTMAC
	list_for_each_entry(sta, &rwnx_vif->stations, list) {
		rwnx_txq_sta_stop(sta, reason);
	}

	foreach_vif_txq(rwnx_vif, txq, ac)
	{
		if (txq)
			rwnx_txq_stop(txq, reason);
	}

#else
	// Reject if monitor interface
	if (rwnx_vif->wdev.iftype == NL80211_IFTYPE_MONITOR)
		goto end;

	rwnx_txq_vif_for_each_sta(rwnx_hw, rwnx_vif, rwnx_txq_sta_stop, reason);

	txq = rwnx_txq_vif_get(rwnx_vif, NX_BCMC_TXQ_TYPE);
	rwnx_txq_stop(txq, reason);
	txq = rwnx_txq_vif_get(rwnx_vif, NX_UNK_TXQ_TYPE);
	rwnx_txq_stop(txq, reason);

end:
#endif /* CONFIG_RWNX_SOFTMAC*/

	spin_unlock_bh(&rwnx_hw->tx_lock);
}

#ifdef CONFIG_RWNX_FULLMAC

/**
 * rwnx_start_offchan_txq - START TX queue for offchannel frame
 *
 * @rwnx_hw: Driver main data
 */
void rwnx_txq_offchan_start(struct rwnx_hw *rwnx_hw)
{
	struct rwnx_txq *txq;

	txq = &rwnx_hw->txq[NX_OFF_CHAN_TXQ_IDX];
	spin_lock_bh(&rwnx_hw->tx_lock);
	rwnx_txq_start(txq, RWNX_TXQ_STOP_CHAN);
	spin_unlock_bh(&rwnx_hw->tx_lock);
}

/**
 * rwnx_switch_vif_sta_txq - Associate TXQ linked to a STA to a new vif
 *
 * @sta: STA whose txq must be switched
 * @old_vif: Vif currently associated to the STA (may no longer be active)
 * @new_vif: vif which should be associated to the STA for now on
 *
 * This function will switch the vif (i.e. the netdev) associated to all STA's
 * TXQ. This is used when AP_VLAN interface are created.
 * If one STA is associated to an AP_vlan vif, it will be moved from the master
 * AP vif to the AP_vlan vif.
 * If an AP_vlan vif is removed, then STA will be moved back to mastert AP vif.
 *
 */
void rwnx_txq_sta_switch_vif(struct rwnx_sta *sta, struct rwnx_vif *old_vif, struct rwnx_vif *new_vif)
{
	struct rwnx_hw *rwnx_hw = new_vif->rwnx_hw;
	struct rwnx_txq *txq;
	int i;

	/* start TXQ on the new interface, and update ndev field in txq */
	if (!netif_carrier_ok(new_vif->ndev))
		netif_carrier_on(new_vif->ndev);
	txq = rwnx_txq_sta_get(sta, 0, rwnx_hw);
	for (i = 0; i < NX_NB_TID_PER_STA; i++, txq++) {
		txq->ndev = new_vif->ndev;
		netif_wake_subqueue(txq->ndev, txq->ndev_idx);
	}
}
#endif /* CONFIG_RWNX_FULLMAC */

/******************************************************************************
 * TXQ queue/schedule functions
 *****************************************************************************/
/**
 * rwnx_txq_queue_skb - Queue a buffer in a TX queue
 *
 * @skb: Buffer to queue
 * @txq: TX Queue in which the buffer must be added
 * @rwnx_hw: Driver main data
 * @retry: Should it be queued in the retry list
 * @skb_prev: If not NULL insert buffer after this skb instead of the tail
 * of the list (ignored if retry is true)
 *
 * @return: Return 1 if txq has been added to hwq list, 0 if txq has not been added
 * to hwq list, and -1 means need drop the skb
 *
 * Add a buffer in the buffer list of the TX queue
 * and add this TX queue in the HW queue list if the txq is not stopped.
 * If this is a retry packet it is added after the last retry packet or at the
 * beginning if there is no retry packet queued.
 *
 * If the STA is in PS mode and this is the first packet queued for this txq
 * update TIM.
 *
 * To be called with tx_lock hold
 */
int rwnx_txq_queue_skb(struct sk_buff *skb, struct rwnx_txq *txq, struct rwnx_hw *rwnx_hw, bool retry,
		       struct sk_buff *skb_prev)
{
	int res = 0;

	if (unlikely(txq->idx == TXQ_INACTIVE))
		return -1;

#ifdef CONFIG_RWNX_FULLMAC
	if (unlikely(txq->sta && txq->sta->ps.active)) {
		txq->sta->ps.pkt_ready[txq->ps_id]++;
		trace_ps_queue(txq->sta);

		if (txq->sta->ps.pkt_ready[txq->ps_id] == 1) {
			rwnx_set_traffic_status(rwnx_hw, txq->sta, true, txq->ps_id);
		}
	}
#else
	// Update mac80211 when buffered traffic for station in PS change
	if (rwnx_txq_is_ps(txq) && skb_queue_empty(&txq->sk_list))
		ieee80211_sta_set_buffered(txq->sta, txq->tid, true);
#endif
	bes_dbg(BES_DBG_TXQ, "%s,retry=%d, txq->status=0x%lx\n", __func__, retry, txq->status);

	if (!retry) {
		/* add buffer in the sk_list */
		if (skb_prev)
			skb_append(skb_prev, skb, &txq->sk_list);
		else
			skb_queue_tail(&txq->sk_list, skb);

#ifdef CONFIG_RWNX_FULLMAC
		// to update for SOFTMAC
		rwnx_ipc_sta_buffer(rwnx_hw, txq->sta, txq->tid, ((struct rwnx_txhdr *)skb->data)->sw_hdr->frame_len);
		if (txq->sta && ((struct rwnx_txhdr *)skb->data)->sw_hdr->desc.api.host.flags & TXU_CNTRL_KEY_FRAME)
			bes_pwr_set_busy_event_async(rwnx_hw,  txq->sta->vif_idx, VIF_DELAY_EVENT_TX_KEY_FRM);

		rwnx_txq_start_cleanup_timer(rwnx_hw, txq->sta);
#endif
	} else {
		if (txq->last_retry_skb)
			skb_append(txq->last_retry_skb, skb, &txq->sk_list);
		else
			skb_queue_head(&txq->sk_list, skb);

		txq->last_retry_skb = skb;
		txq->nb_retry++;
	}

	trace_txq_queue_skb(skb, txq, retry);

	/* Flowctrl corresponding netdev queue if needed */
#ifdef CONFIG_RWNX_FULLMAC
	/* If too many buffer are queued for this TXQ stop netdev queue */
	if ((txq->ndev_idx != NDEV_NO_TXQ) && (skb_queue_len(&txq->sk_list) > RWNX_NDEV_FLOW_CTRL_STOP)) {
		bes_dbg(BES_DBG_TXQ, "%s,netif stop,\n", __func__);
		set_bit(RWNX_TXQ_NDEV_FLOW_CTRL, &txq->status);
		netif_stop_subqueue(txq->ndev, txq->ndev_idx);
		trace_txq_flowctrl_stop(txq);
	}
#else  /* ! CONFIG_RWNX_FULLMAC */

	if (!retry && ++txq->hwq->len == txq->hwq->len_stop) {
		trace_hwq_flowctrl_stop(txq->hwq->id);
		ieee80211_stop_queue(rwnx_hw->hw, txq->hwq->id);
		rwnx_hw->stats.queues_stops++;
	}
#endif /* CONFIG_RWNX_FULLMAC */

	/* add it in the hwq list if not stopped and not yet present */
	if (!rwnx_txq_is_stopped(txq)) {
		rwnx_txq_add_to_hw_list(txq);
		res = 1;
	}
	bes_dbg(BES_DBG_TXQ, "%s, res=%d, txq->status=0x%lx\n", __func__, res, txq->status);
	return res;
}

/**
 * rwnx_txq_confirm_any - Process buffer confirmed by fw
 *
 * @rwnx_hw: Driver main data
 * @txq: TX Queue
 * @hwq: HW Queue
 * @sw_txhdr: software descriptor of the confirmed packet
 *
 * Process a buffer returned by the fw. It doesn't check buffer status
 * and only does systematic counter update:
 * - hw credit
 * - buffer pushed to fw
 *
 * To be called with tx_lock hold
 */
void rwnx_txq_confirm_any(struct rwnx_hw *rwnx_hw, struct rwnx_txq *txq, struct rwnx_hwq *hwq,
			  struct rwnx_sw_txhdr *sw_txhdr)
{
	int user = 0;

#ifdef CONFIG_RWNX_MUMIMO_TX
	int group_id;

	user = RWNX_MUMIMO_INFO_POS_ID(sw_txhdr->desc.api.host.mumimo_info);
	group_id = RWNX_MUMIMO_INFO_GROUP_ID(sw_txhdr->desc.api.host.mumimo_info);

	if ((txq->idx != TXQ_INACTIVE) && (txq->pkt_pushed[user] == 1) && test_bit(RWNX_TXQ_STOP_MU_POS, &txq->status))
		rwnx_txq_start(txq, RWNX_TXQ_STOP_MU_POS);

#endif /* CONFIG_RWNX_MUMIMO_TX */

	if (txq->pkt_pushed[user])
		txq->pkt_pushed[user]--;

	hwq->credits[user]++;
	hwq->need_processing = true;
	rwnx_hw->stats.cfm_balance[hwq->id]--;
}

/******************************************************************************
 * HWQ processing
 *****************************************************************************/
static inline bool rwnx_txq_take_mu_lock(struct rwnx_hw *rwnx_hw)
{
	bool res = false;
#ifdef CONFIG_RWNX_MUMIMO_TX
	if (rwnx_hw->mod_params->mutx)
		res = (down_trylock(&rwnx_hw->mu.lock) == 0);
#endif /* CONFIG_RWNX_MUMIMO_TX */
	return res;
}

static inline void rwnx_txq_release_mu_lock(struct rwnx_hw *rwnx_hw)
{
#ifdef CONFIG_RWNX_MUMIMO_TX
	up(&rwnx_hw->mu.lock);
#endif /* CONFIG_RWNX_MUMIMO_TX */
}

static inline void rwnx_txq_set_mu_info(struct rwnx_hw *rwnx_hw, struct rwnx_txq *txq, int group_id, int pos)
{
#ifdef CONFIG_RWNX_MUMIMO_TX
	trace_txq_select_mu_group(txq, group_id, pos);
	if (group_id) {
		txq->mumimo_info = group_id | (pos << 6);
		rwnx_mu_set_active_group(rwnx_hw, group_id);
	} else
		txq->mumimo_info = 0;
#endif /* CONFIG_RWNX_MUMIMO_TX */
}

static inline s8 rwnx_txq_get_credits(struct rwnx_txq *txq)
{
	s8 cred = txq->credits;
	/* if destination is in PS mode, push_limit indicates the maximum
	   number of packet that can be pushed on this txq. */
	if (txq->push_limit && (cred > txq->push_limit)) {
		cred = txq->push_limit;
	}
	return cred;
}

/**
 * skb_queue_extract - Extract buffer from skb list
 *
 * @list: List of skb to extract from
 * @head: List of skb to append to
 * @nb_elt: Number of skb to extract
 *
 * extract the first @nb_elt of @list and append them to @head
 * It is assume that:
 * - @list contains more that @nb_elt
 * - There is no need to take @list nor @head lock to modify them
 */
static inline void skb_queue_extract(struct sk_buff_head *list, struct sk_buff_head *head, int nb_elt)
{
	int i;
	struct sk_buff *first, *last, *ptr;

	first = ptr = list->next;
	for (i = 0; i < nb_elt; i++) {
		ptr = ptr->next;
	}
	last = ptr->prev;

	/* unlink nb_elt in list */
	list->qlen -= nb_elt;
	list->next = ptr;
	ptr->prev = (struct sk_buff *)list;

	/* append nb_elt at end of head */
	head->qlen += nb_elt;
	last->next = (struct sk_buff *)head;
	head->prev->next = first;
	first->prev = head->prev;
	head->prev = last;
}

#ifdef CONFIG_MAC80211_TXQ
/**
 * rwnx_txq_mac80211_dequeue - Dequeue buffer from mac80211 txq and
 *                             add them to push list
 *
 * @rwnx_hw: Main driver data
 * @sk_list: List of buffer to push (initialized without lock)
 * @txq: TXQ to dequeue buffers from
 * @max: Max number of buffer to dequeue
 *
 * Dequeue buffer from mac80211 txq, prepare them for transmission and chain them
 * to the list of buffer to push.
 *
 * @return true if no more buffer are queued in mac80211 txq and false otherwise.
 */
static bool rwnx_txq_mac80211_dequeue(struct rwnx_hw *rwnx_hw, struct sk_buff_head *sk_list, struct rwnx_txq *txq,
				      int max)
{
	struct ieee80211_txq *mac_txq;
	struct sk_buff *skb;
	unsigned long mac_txq_len;

	if (txq->nb_ready_mac80211 == NOT_MAC80211_TXQ)
		return true;

	mac_txq = container_of((void *)txq, struct ieee80211_txq, drv_priv);

	for (; max > 0; max--) {
		skb = rwnx_tx_dequeue_prep(rwnx_hw, mac_txq);
		if (skb == NULL)
			return true;

		__skb_queue_tail(sk_list, skb);
	}

	/* re-read mac80211 txq current length.
	   It is mainly for debug purpose to trace dropped packet. There is no
	   problems to have nb_ready_mac80211 != actual mac80211 txq length */
	ieee80211_txq_get_depth(mac_txq, &mac_txq_len, NULL);
	if (txq->nb_ready_mac80211 > mac_txq_len)
		trace_txq_drop(txq, txq->nb_ready_mac80211 - mac_txq_len);
	txq->nb_ready_mac80211 = mac_txq_len;

	return (txq->nb_ready_mac80211 == 0);
}
#endif

#if !defined(CONFIG_RWNX_PCIE) && !defined(CONFIG_BES_IBUS)
/**
 * rwnx_txq_adjust_credits - adjust credits for non dma bus
 *
 * @hwq: HWQ on wich buffers will be pushed
 * @txq: TXQ to get buffers from
 * @credits: available credit for the queue
 * @buf_free: number of buffer in non dma bus
 *
 * @return: adjust credits, calculated by actual number of buffers required
 */
static int rwnx_txq_adjust_credits(struct rwnx_hwq *hwq, struct rwnx_txq *txq,
				   int credits, int buf_free)
{
	int adjust_credits = credits;

#ifdef CONFIG_RWNX_AMSDUS_TX
	struct sk_buff *skb_tmp;
	bool update = false;
	u32 sum = 0;
	int adjust_nb_ready = 0;

	if (credits > 0) {
		skb_queue_walk(&txq->sk_list, skb_tmp) {
			struct rwnx_sw_txhdr *sw_hdr = ((struct rwnx_txhdr *)skb_tmp->data)->sw_hdr;

			sum += sw_hdr->buf_cnt;
			update = adjust_nb_ready < credits && sum <= buf_free;

			if (update) {
				adjust_nb_ready += 1;
			} else {
				break;
			}
		}
	}

	adjust_credits = adjust_nb_ready;
#else
	if (buf_free < adjust_credits) {
		adjust_credits = buf_free;
	}
#endif /* CONFIG_RWNX_AMSDUS_TX */

	return adjust_credits;
}
#endif

/**
 * rwnx_txq_get_skb_to_push() - Get list of buffer to push for one txq
 *
 * @rwnx_hw: main driver data
 * @hwq: HWQ on wich buffers will be pushed
 * @txq: TXQ to get buffers from
 * @user: user postion to use
 * @sk_list_push: list to update
 *
 *
 * This function will returned a list of buffer to push for one txq.
 * It will take into account the number of credit of the HWQ for this user
 * position and TXQ (and push_limit).
 * This allow to get a list that can be pushed without having to test for
 * hwq/txq status after each push
 *
 * If a MU group has been selected for this txq, it will also update the
 * counter for the group
 *
 * @return true if txq no longer have buffer ready after the ones returned.
 *         false otherwise
 */
static bool rwnx_txq_get_skb_to_push(struct rwnx_hw *rwnx_hw, struct rwnx_hwq *hwq, struct rwnx_txq *txq, int user,
				     struct sk_buff_head *sk_list_push)
{
	int nb_ready = skb_queue_len(&txq->sk_list);
	int credits = min_t(int, rwnx_txq_get_credits(txq), hwq->credits[user]);
	bool res = false;
#if defined(CONFIG_RWNX_PCIE) || defined(CONFIG_BES_IBUS)
	__skb_queue_head_init(sk_list_push);
	bes_dbg(BES_DBG_TXQ, "%s, crd=%d,nb_ready=%d, crd_txq=%d, crd_hwq=%d, user=%d\n",
		__func__, credits, nb_ready, txq->credits, hwq->credits[user], user);
#else
	int buf_free = atomic_read(&rwnx_hw->plat->buf_tx);

	__skb_queue_head_init(sk_list_push);
	credits = rwnx_txq_adjust_credits(hwq, txq, credits, buf_free);

	bes_dbg(BES_DBG_TXQ, "%s, crd=%d,nb_ready=%d, txq%d crd=%d, hwq%d crd=%d, buf_free=%d,user=%d\n",
		__func__, credits, nb_ready, txq->idx, txq->credits, hwq->id,
		hwq->credits[user], buf_free, user);

	if (credits <= 0) {
		return false;
	}
#endif /* CONFIG_RWNX_PCIE || CONFIG_BES_IBUS */

	if (credits >= nb_ready) {
		skb_queue_splice_init(&txq->sk_list, sk_list_push);
#ifdef CONFIG_MAC80211_TXQ
		res = rwnx_txq_mac80211_dequeue(rwnx_hw, sk_list_push, txq, credits - nb_ready);
		credits = skb_queue_len(sk_list_push);
#else
		res = true;
		credits = nb_ready;
#endif
	} else {
		skb_queue_extract(&txq->sk_list, sk_list_push, credits);

		/* When processing PS service period (i.e. push_limit != 0), no longer
		   process this txq if the buffers extracted will complete the SP for
		   this txq */
		if (txq->push_limit && (credits == txq->push_limit))
			res = true;
	}
	if (txq->nb_retry > 0 && credits > 0) {
		if (txq->nb_retry > credits) {
			txq->nb_retry -= credits;
		} else {
			txq->nb_retry = 0;
			txq->last_retry_skb = NULL;
		}
	}
	rwnx_mu_set_active_sta(rwnx_hw, rwnx_txq_2_sta(txq), credits);
	bes_dbg(BES_DBG_TXQ, "%s,res=%d\n", __func__, res);
	return res;
}

/**
 * rwnx_txq_select_user - Select User queue for a txq
 *
 * @rwnx_hw: main driver data
 * @mu_lock: true is MU lock is taken
 * @txq: TXQ to select MU group for
 * @hwq: HWQ for the TXQ
 * @user: Updated with user position selected
 *
 * @return false if it is no possible to process this txq.
 *         true otherwise
 *
 * This function selects the MU group to use for a TXQ.
 * The selection is done as follow:
 *
 * - return immediately for STA that don't belongs to any group and select
 *   group 0 / user 0
 *
 * - If MU tx is disabled (by user mutx_on, or because mu group are being
 *   updated !mu_lock), select group 0 / user 0
 *
 * - Use the best group selected by @rwnx_mu_group_sta_select.
 *
 *   Each time a group is selected (except for the first case where sta
 *   doesn't belongs to a MU group), the function checks that no buffer is
 *   pending for this txq on another user position. If this is the case stop
 *   the txq (RWNX_TXQ_STOP_MU_POS) and return false.
 *
 */
static bool rwnx_txq_select_user(struct rwnx_hw *rwnx_hw, bool mu_lock, struct rwnx_txq *txq, struct rwnx_hwq *hwq,
				 int *user)
{
	int pos = 0;
#ifdef CONFIG_RWNX_MUMIMO_TX
	int id, group_id = 0;
	struct rwnx_sta *sta = rwnx_txq_2_sta(txq);

	/* for sta that belong to no group return immediately */
	if (!sta || !sta->group_info.cnt)
		goto end;

	/* If MU is disabled, need to check user */
	if (!rwnx_hw->mod_params->mutx_on || !mu_lock)
		goto check_user;

	/* Use the "best" group selected */
	group_id = sta->group_info.group;

	if (group_id > 0)
		pos = rwnx_mu_group_sta_get_pos(rwnx_hw, sta, group_id);

check_user:
	/* check that we can push on this user position */
#if CONFIG_USER_MAX == 2
	id = (pos + 1) & 0x1;
	if (txq->pkt_pushed[id]) {
		rwnx_txq_stop(txq, RWNX_TXQ_STOP_MU_POS);
		return false;
	}

#else
	for (id = 0; id < CONFIG_USER_MAX; id++) {
		if (id != pos && txq->pkt_pushed[id]) {
			rwnx_txq_stop(txq, RWNX_TXQ_STOP_MU_POS);
			return false;
		}
	}
#endif

end:
	rwnx_txq_set_mu_info(rwnx_hw, txq, group_id, pos);
#endif /* CONFIG_RWNX_MUMIMO_TX */

	*user = pos;
	return true;
}

/**
 * rwnx_hwq_process - Process one HW queue list
 *
 * @rwnx_hw: Driver main data
 * @hw_queue: HW queue index to process
 *
 * The function will iterate over all the TX queues linked in this HW queue
 * list. For each TX queue, push as many buffers as possible in the HW queue.
 * (NB: TX queue have at least 1 buffer, otherwise it wouldn't be in the list)
 * - If TX queue no longer have buffer, remove it from the list and check next
 *   TX queue
 * - If TX queue no longer have credits or has a push_limit (PS mode) and it
 *   is reached , remove it from the list and check next TX queue
 * - If HW queue is full, update list head to start with the next TX queue on
 *   next call if current TX queue already pushed "too many" pkt in a row, and
 *   return
 *
 * To be called when HW queue list is modified:
 * - when a buffer is pushed on a TX queue
 * - when new credits are received
 * - when a STA returns from Power Save mode or receives traffic request.
 * - when Channel context change
 *
 * To be called with tx_lock hold
 */
#define ALL_HWQ_MASK ((1 << CONFIG_USER_MAX) - 1)

void rwnx_hwq_process(struct rwnx_hw *rwnx_hw, struct rwnx_hwq *hwq)
{
	struct rwnx_txq *txq, *next;
	int user, credit_map = 0;
	bool mu_enable;

	trace_process_hw_queue(hwq);

	mu_enable = rwnx_txq_take_mu_lock(rwnx_hw);
	if (!mu_enable)
		credit_map = ALL_HWQ_MASK - 1;

	list_for_each_entry_safe(txq, next, &hwq->list, sched_list) {
		struct rwnx_txhdr *txhdr = NULL;
		struct sk_buff_head sk_list_push;
		struct sk_buff *skb;
		bool txq_empty;

		trace_process_txq(txq);

		/* sanity check for debug */
		BUG_ON(!test_bit(RWNX_TXQ_IN_HWQ_LIST, &txq->status));
		BUG_ON(txq->idx == TXQ_INACTIVE);
		BUG_ON(txq->credits <= 0);
		BUG_ON(!rwnx_txq_skb_ready(txq));

		if (!rwnx_txq_select_user(rwnx_hw, mu_enable, txq, hwq, &user)) {
			bes_dbg(BES_DBG_TXQ, "%s,txq_select_user, continue\n", __func__);
			continue;
		}

		if (!hwq->credits[user]) {
			credit_map |= BIT(user);
			bes_dbg(BES_DBG_TXQ, "%s,credit_map=0x%x\n", __func__, credit_map);
			if (credit_map == ALL_HWQ_MASK)
				break;
			continue;
		}
		txq_empty = rwnx_txq_get_skb_to_push(rwnx_hw, hwq, txq, user, &sk_list_push);
		while ((skb = __skb_dequeue(&sk_list_push)) != NULL) {
			txhdr = (struct rwnx_txhdr *)skb->data;
			rwnx_tx_push(rwnx_hw, txhdr, 0);
		}

		if (txq_empty) {
			rwnx_txq_del_from_hw_list(txq);
			txq->pkt_sent = 0;
#if defined CONFIG_RWNX_SOFTMAC && defined CONFIG_RWNX_AMSDUS_TX
			if (txq->amsdu_ht_len_cap)
				ieee80211_amsdu_ctl(rwnx_hw->hw, txq->sta, txq->tid, NULL, 0, 0, false);
#endif
		} else if ((hwq->credits[user] == 0) && rwnx_txq_is_scheduled(txq)) {
			bes_dbg(BES_DBG_TXQ, "%s,crd_hwq=%d, txq scheduled\n", __func__, hwq->credits[user]);
			/* txq not empty,
			   - To avoid starving need to process other txq in the list
			   - For better aggregation, need to send "as many consecutive
			   pkt as possible" for he same txq
			   ==> Add counter to trigger txq switch
			*/
			if (txq->pkt_sent > hwq->size) {
				txq->pkt_sent = 0;
				list_rotate_left(&hwq->list);
			}
		}

#ifdef CONFIG_RWNX_FULLMAC
		bes_dbg(BES_DBG_TXQ, "%s,push_limit=%d,sta=%d,ps_id=%d,txq->status=0x%lx\n", __func__, txq->push_limit,
			 (txq->sta == NULL) ? 0 : 1, txq->ps_id, txq->status);

		/* Unable to complete PS traffic request because of hwq credit */
		if (txq->push_limit && txq->sta) {
			if (txq->ps_id == LEGACY_PS_ID) {
				/* for legacy PS abort SP and wait next ps-poll */
				txq->sta->ps.sp_cnt[txq->ps_id] -= txq->push_limit;
				txq->push_limit = 0;
			}
			/* for u-apsd need to complete the SP to send EOSP frame */
		}

		/* restart netdev queue if number of queued buffer is below threshold */
		if (unlikely(test_bit(RWNX_TXQ_NDEV_FLOW_CTRL, &txq->status)) &&
		    skb_queue_len(&txq->sk_list) < RWNX_NDEV_FLOW_CTRL_RESTART) {
			clear_bit(RWNX_TXQ_NDEV_FLOW_CTRL, &txq->status);
			netif_wake_subqueue(txq->ndev, txq->ndev_idx);
			trace_txq_flowctrl_restart(txq);
		}
#endif /* CONFIG_RWNX_FULLMAC */
	}
#ifdef CONFIG_RWNX_SOFTMAC
	if (hwq->len < hwq->len_start && ieee80211_queue_stopped(rwnx_hw->hw, hwq->id)) {
		trace_hwq_flowctrl_start(hwq->id);
		ieee80211_wake_queue(rwnx_hw->hw, hwq->id);
	}
#endif /* CONFIG_RWNX_SOFTMAC */

	if (list_empty(&hwq->list))
		hwq->need_processing = false;

	if (mu_enable)
		rwnx_txq_release_mu_lock(rwnx_hw);
}

/**
 * rwnx_hwq_process_all - Process all HW queue list
 *
 * @rwnx_hw: Driver main data
 *
 * Loop over all HWQ, and process them if needed
 * To be called with tx_lock hold
 */
void rwnx_hwq_process_all(struct rwnx_hw *rwnx_hw)
{
	int id;

	rwnx_mu_group_sta_select(rwnx_hw);

	for (id = ARRAY_SIZE(rwnx_hw->hwq) - 1; id >= 0; id--) {
		if (rwnx_hw->hwq[id].need_processing) {
			rwnx_hwq_process(rwnx_hw, &rwnx_hw->hwq[id]);
		}
	}
}

/**
 * rwnx_hwq_init - Initialize all hwq structures
 *
 * @rwnx_hw: Driver main data
 *
 */
void rwnx_hwq_init(struct rwnx_hw *rwnx_hw)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(rwnx_hw->hwq); i++) {
		struct rwnx_hwq *hwq = &rwnx_hw->hwq[i];

		for (j = 0; j < CONFIG_USER_MAX; j++)
			hwq->credits[j] = nx_txdesc_cnt[i];
		hwq->id = i;
		hwq->size = nx_txdesc_cnt[i];
		INIT_LIST_HEAD(&hwq->list);

#ifdef CONFIG_RWNX_SOFTMAC
		hwq->len = 0;
		hwq->len_stop = nx_txdesc_cnt[i] * 2;
		hwq->len_start = hwq->len_stop / 4;
#endif
	}
}

/**
 * rwnx_hwq_update_credits - Update HWQ's credits
 *
 * @rwnx_hw: Driver main data
 * @buf_tx_cnt: Number of transmission buffers
 *
 * When HWQ has a high credits, it lead to starvation during TXQ rotation.
 */
#if !defined(CONFIG_RWNX_PCIE) && !defined(CONFIG_BES_IBUS)
void rwnx_hwq_update_credits(struct rwnx_hw *rwnx_hw, u8 buf_tx_cnt)
{
	int i, j;

	for (i = 0; i < ARRAY_SIZE(rwnx_hw->hwq); i++) {
		struct rwnx_hwq *hwq = &rwnx_hw->hwq[i];

		for (j = 0; j < CONFIG_USER_MAX; j++)
			hwq->credits[j] = min(hwq->credits[j], buf_tx_cnt);
		hwq->size = min(hwq->size, buf_tx_cnt);
	}
}
#endif
