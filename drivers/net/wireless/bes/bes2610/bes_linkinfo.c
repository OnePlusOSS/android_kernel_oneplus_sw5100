/*
 * @file bes_linkinfo.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "rwnx_msg_tx.h"
#include "bes_linkinfo.h"
#include "bes_pwr.h"
#include "bes_log.h"

extern int rwnx_fill_txrx_rate(struct txrx_rate_info *tx_rate_info, struct rate_info *txrate);

int bes_get_beacon_rssi(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta)
{
	int ret, beacon_rssi = -128;
	struct linkinfo_get_rssi_req req;
	struct linkinfo_get_rssi_cfm cfm;

	if (!sta->valid)
		return beacon_rssi;

	req.hdr.id = CUSTOM_ID_LINK_INFO_REQ;
	req.hdr.length = sizeof(req);
	req.hdr.ops = LINKINFO_OPS_BEACON_RSSI_GET;

	req.sta_index = sta->sta_idx;

	ret = rwnx_send_custom_request(rwnx_hw, &req.hdr, &cfm, sizeof(cfm));

	if (!ret)
		beacon_rssi = cfm.beacon_rssi;
	else
		bes_err(BES_DBG_MSG_TX, "%s get beacon rssi failed(%d)\n", __func__, ret);

	return beacon_rssi;
}

static int bes_link_flush_data_req(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta)
{
	int ret, beacon_rssi = -128;
	struct linkinfo_flush_data_req req;

	req.hdr.id = CUSTOM_ID_LINK_INFO_REQ;
	req.hdr.length = sizeof(req);
	req.hdr.ops = LINKINFO_OPS_FLUSH_DATA;

	req.sta_index = sta->sta_idx;

	ret = rwnx_send_custom_request(rwnx_hw, &req.hdr, NULL, 0);

	if (ret)
		bes_err(BES_DBG_MSG_TX, "%s failed(%d)\n", __func__, ret);

	return beacon_rssi;
}

int bes_link_flush_data(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta)
{
	int ret = 0;

	/* Do nothing if station is invalid */
	if (!sta->valid) {
		return 0;
	}

	/* Stop sta's txq to device before flushing  */
	spin_lock_bh(&rwnx_hw->tx_lock);
	rwnx_txq_sta_stop(sta, RWNX_TXQ_STOP_FLUSH, rwnx_hw);
	spin_unlock_bh(&rwnx_hw->tx_lock);

	/* init completion to wait flush opeation end */
	init_completion(&sta->flush_cmp);

	/* Send flush command to device */
	bes_link_flush_data_req(rwnx_hw, sta);

	/* Wait flush operation end */
	ret = wait_for_completion_killable(&sta->flush_cmp);

	/* Restart sta's txq after flushing */
	spin_lock_bh(&rwnx_hw->tx_lock);
	rwnx_txq_sta_start(sta, RWNX_TXQ_STOP_FLUSH, rwnx_hw);
	spin_unlock_bh(&rwnx_hw->tx_lock);

	return ret;
}

int bes_get_tx_rate(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta,
		    struct tx_rate_vector *tx_rate_vector)
{
	int ret;
	struct linkinfo_get_tx_rate_req req;
	struct linkinfo_get_tx_rate_cfm cfm;

	if (!sta->valid)
		return -ENOENT;

	req.hdr.id = CUSTOM_ID_LINK_INFO_REQ;
	req.hdr.length = sizeof(req);
	req.hdr.ops = LINKINFO_OPS_TX_RATE_GET;

	req.sta_index = sta->sta_idx;

	ret = rwnx_send_custom_request(rwnx_hw, &req.hdr, &cfm, sizeof(cfm));
	if (ret) {
		bes_err(BES_DBG_MSG_TX, "%s get tx rate failed(%d)\n", __func__, ret);
		return ret;
	} else if (cfm.tx_rate.status != CO_OK)
		return -EINVAL;

	*tx_rate_vector = cfm.tx_rate;

	return 0;
}

int bes_get_tx_delay_stat(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta, u32 *delay)
{
	int ret;
	struct linkinfo_get_tx_delay_req req;
	struct linkinfo_get_tx_delay_cfm cfm;

	if (!sta->valid)
		return -EINVAL;

	req.hdr.id = CUSTOM_ID_LINK_INFO_REQ;
	req.hdr.length = sizeof(req);
	req.hdr.ops = LINKINFO_OPS_TX_DELAY_GET;

	req.sta_index = sta->sta_idx;

	ret = rwnx_send_custom_request(rwnx_hw, &req.hdr, &cfm, sizeof(cfm));
	if (ret) {
		bes_err(BES_DBG_MSG_TX, "%s get tx delay statistics info failed(%d)\n", __func__, ret);
		return ret;
	}

	*delay = cfm.delay;
	return 0;
}

void bes_sta_txrx_rate_stat_deinit(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta)
{
	struct rwnx_tx_rate_table *tx_table = NULL;
	int *rx_table = NULL;

	/* Check the validity of sta */
	if (!sta)
		return;

	spin_lock_bh(&rwnx_hw->cb_lock);
	if (sta->stats.rx_rate.table) {
		rx_table = sta->stats.rx_rate.table;
		sta->stats.rx_rate.table = NULL;
	}

	if (sta->stats.tx_rate.table) {
		tx_table = sta->stats.tx_rate.table;
		sta->stats.tx_rate.table = NULL;
	}

	sta->stats.rx_rate.size = 0;
	sta->stats.rx_rate.cpt = 0;
	sta->stats.rx_rate.rate_cnt = 0;

	sta->stats.tx_rate.size = 0;
	sta->stats.tx_rate.rate_cnt = 0;
	sta->stats.tx_rate.tx_mpdu = 0;
	sta->stats.tx_rate.attempts = 0;
	spin_unlock_bh(&rwnx_hw->cb_lock);

	if (rx_table)
		vfree(rx_table);

	if (tx_table)
		vfree(tx_table);
}

int bes_sta_txrx_rate_stat_init(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta)
{
	int nb_rx_rate = 0;
	int nb_tx_rate = 0;
	struct rwnx_rx_rate_stats *rx_rate_stats;
	struct rwnx_tx_rate_stats *tx_rate_stats;
	struct rwnx_tx_rate_table *tx_table = NULL;
	int *rx_table = NULL;

	/* Check the validity of sta */
	if (!sta)
		return -EINVAL;

	rx_rate_stats = &sta->stats.rx_rate;
	tx_rate_stats = &sta->stats.tx_rate;

	/* rx */
	/* As MTK phone's wifi hotspot send frame at some rates that exceeds the negotiated rates,
	 * expanding the rx_rate_stats table to the maximum we can support.
	 * */
	nb_rx_rate = N_CCK + N_OFDM;
	nb_rx_rate += N_HT;
	nb_rx_rate += N_VHT;
	nb_rx_rate += N_HE_SU + N_HE_MU + N_HE_ER;

	rx_table = vzalloc(nb_rx_rate * sizeof(rx_rate_stats->table[0]));
	if (!rx_table) {
		bes_err(BES_DBG_RX, "%s rx rate, memory alloc failed\n", __func__);
		goto err;
	}

	/* tx */
	if (sta->he)
		nb_tx_rate = NUM_TX_RATE_HE_SU_ER;
	else if (sta->vht)
		nb_tx_rate = NUM_TX_RATE_VHT;
	else if (sta->ht)
		nb_tx_rate = NUM_TX_RATE_HT;
	else
		nb_tx_rate = NUM_TX_RATE_NON_HT;

	tx_table = vzalloc(nb_tx_rate * sizeof(tx_rate_stats->table[0]));
	if (!tx_table) {
		bes_err(BES_DBG_TX, "%s tx rate, memory alloc failed\n", __func__);
		goto err;
	}

	spin_lock_bh(&rwnx_hw->cb_lock);
	rx_rate_stats->table = rx_table;
	rx_rate_stats->size = nb_rx_rate;
	rx_rate_stats->cpt = 0;
	rx_rate_stats->rate_cnt = 0;

	tx_rate_stats->table = tx_table;
	tx_rate_stats->size = nb_tx_rate;
	tx_rate_stats->rate_cnt = 0;
	tx_rate_stats->tx_mpdu = 0;
	tx_rate_stats->attempts = 0;
	spin_unlock_bh(&rwnx_hw->cb_lock);

	return 0;

err:
	bes_sta_txrx_rate_stat_deinit(rwnx_hw, sta);
	return -ENOMEM;
}

void bes_sta_rx_rate_stat_reset(struct rwnx_sta *sta)
{
	memset(sta->stats.rx_rate.table, 0,
	       sta->stats.rx_rate.size * sizeof(sta->stats.rx_rate.table[0]));
	sta->stats.rx_rate.cpt = 0;
	sta->stats.rx_rate.rate_cnt = 0;
}

void bes_sta_tx_rate_stat_reset(struct rwnx_sta *sta)
{
	memset((void *)sta->stats.tx_rate.table, 0,
	       sta->stats.tx_rate.size * sizeof(sta->stats.tx_rate.table[0]));
	sta->stats.tx_rate.rate_cnt = 0;
	sta->stats.tx_rate.tx_mpdu = 0;
	sta->stats.tx_rate.attempts = 0;
}

void idx_to_rate_cfg(int idx, union rwnx_rate_ctrl_info *r_cfg, int *ru_size)
{
	r_cfg->value = 0;
	if (idx < N_CCK) {
		r_cfg->formatModTx = FORMATMOD_NON_HT;
		r_cfg->giAndPreTypeTx = (idx & 1) << 1;
		r_cfg->mcsIndexTx = idx / 2;
	} else if (idx < (N_CCK + N_OFDM)) {
		r_cfg->formatModTx = FORMATMOD_NON_HT;
		r_cfg->mcsIndexTx = idx - N_CCK + 4;
	} else if (idx < (N_CCK + N_OFDM + N_HT)) {
		union rwnx_mcs_index *r = (union rwnx_mcs_index *)r_cfg;

		idx -= (N_CCK + N_OFDM);
		r_cfg->formatModTx = FORMATMOD_HT_MF;
		r->ht.nss = idx / (8 * 2 * 2);
		r->ht.mcs = (idx % (8 * 2 * 2)) / (2 * 2);
		r_cfg->bwTx = ((idx % (8 * 2 * 2)) % (2 * 2)) / 2;
		r_cfg->giAndPreTypeTx = idx & 1;
	} else if (idx < (N_CCK + N_OFDM + N_HT + N_VHT)) {
		union rwnx_mcs_index *r = (union rwnx_mcs_index *)r_cfg;

		idx -= (N_CCK + N_OFDM + N_HT);
		r_cfg->formatModTx = FORMATMOD_VHT;
		r->vht.nss = idx / (10 * 4 * 2);
		r->vht.mcs = (idx % (10 * 4 * 2)) / (4 * 2);
		r_cfg->bwTx = ((idx % (10 * 4 * 2)) % (4 * 2)) / 2;
		r_cfg->giAndPreTypeTx = idx & 1;
	} else if (idx < (N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU)) {
		union rwnx_mcs_index *r = (union rwnx_mcs_index *)r_cfg;

		idx -= (N_CCK + N_OFDM + N_HT + N_VHT);
		r_cfg->formatModTx = FORMATMOD_HE_SU;
		r->vht.nss = idx / (12 * 4 * 3);
		r->vht.mcs = (idx % (12 * 4 * 3)) / (4 * 3);
		r_cfg->bwTx = ((idx % (12 * 4 * 3)) % (4 * 3)) / 3;
		r_cfg->giAndPreTypeTx = idx % 3;
	} else if (idx < (N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU + N_HE_MU)) {
		union rwnx_mcs_index *r = (union rwnx_mcs_index *)r_cfg;

		BUG_ON(ru_size == NULL);

		idx -= (N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU);
		r_cfg->formatModTx = FORMATMOD_HE_MU;
		r->vht.nss = idx / (12 * 6 * 3);
		r->vht.mcs = (idx % (12 * 6 * 3)) / (6 * 3);
		*ru_size = ((idx % (12 * 6 * 3)) % (6 * 3)) / 3;
		r_cfg->giAndPreTypeTx = idx % 3;
		r_cfg->bwTx = 0;
	} else {
		union rwnx_mcs_index *r = (union rwnx_mcs_index *)r_cfg;

		idx -= (N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU + N_HE_MU);
		r_cfg->formatModTx = FORMATMOD_HE_ER;
		r_cfg->bwTx = idx / 9;
		if (ru_size)
			*ru_size = idx / 9;
		r_cfg->giAndPreTypeTx = idx % 3;
		r->vht.mcs = (idx % 9) / 3;
		r->vht.nss = 0;
	}
}

/**
 * txrx_rate_sort - tx and rx rate sort by attempts or mpdu
 * tx use attempts and rx use mpdu
 */
void txrx_rate_sort(int *rate, int num, int cmp_idx, void *rate_stats, bool tx)
{
#define GET_MPDU(is_tx, p, idx) \
		(is_tx ? (((struct rwnx_tx_rate_stats *)p)->table[idx].tx_mpdu + \
		((struct rwnx_tx_rate_stats *)p)->table[idx].mpdu_lost) : \
		((struct rwnx_rx_rate_stats *)p)->table[idx])

	int i = 0, aim_idx = num;
	u32 mpdu;

	if (tx && cmp_idx >= ((struct rwnx_tx_rate_stats *)rate_stats)->size) {
		bes_err(BES_DBG_TX, "%s cmp_idx(%d) >= tx_rate->size(%d)\n",
			__func__, cmp_idx, ((struct rwnx_tx_rate_stats *)rate_stats)->size);
		return;
	}

	if (!tx && cmp_idx >= ((struct rwnx_rx_rate_stats *)rate_stats)->size) {
		bes_err(BES_DBG_RX, "%s cmp_idx(%d) >= rx_rate->size(%d)\n",
			__func__, cmp_idx, ((struct rwnx_rx_rate_stats *)rate_stats)->size);
		return;
	}

	mpdu = GET_MPDU(tx, rate_stats, cmp_idx);

	/* find the index of the first element less than mpdu */
	for (i = 0; i < num; i++) {
		if (rate[i] >= 0) {
			u32 tmp = GET_MPDU(tx, rate_stats, rate[i]);
			if (mpdu > tmp) {
				aim_idx = i;
				break;
			}
		} else {
			aim_idx = i;
			break;
		}
	}

	if (aim_idx == num)
		return;

	/* remove smaller value */
	for (i = num - 1; i > aim_idx; i--)
		rate[i] = rate[i - 1];

	rate[i] = cmp_idx;

#undef GET_MPDU
}

bool txrx_rate_idx_is_most_used(int *rate, int num, int cmp_idx)
{
	int i;

	for (i = 0; i < num; i++) {
		if (cmp_idx == rate[i])
			return true;
	}

	return false;
}

static void _bes_tx_update_peer_info(struct rwnx_sta_stats *sta_stats,
				     struct rc_rate_stats *rc_rate_stats)
{
	union rwnx_rate_ctrl_info *r_cfg = (union rwnx_rate_ctrl_info *)&rc_rate_stats->rate_config;
	union rwnx_mcs_index *mcs_index = (union rwnx_mcs_index *)&rc_rate_stats->rate_config;
	u8 ru_size = rc_rate_stats->ru_and_length & 0x7;
	unsigned int ft, pre, sgi, bw, nss, mcs, dcm;
	int rate_idx;
	struct rwnx_tx_rate_table *rate_table;

	if (!rc_rate_stats->attempts)
		return;

	ft = r_cfg->formatModTx;
	pre = r_cfg->giAndPreTypeTx >> 1;
	sgi = r_cfg->giAndPreTypeTx;
	bw = r_cfg->bwTx;
	dcm = 0;

	if (ft >= FORMATMOD_HE_SU) {
		mcs = mcs_index->he.mcs;
		nss = mcs_index->he.nss;
		dcm = r_cfg->dcmTx;
		if (ft == FORMATMOD_HE_MU)
			bw = ru_size;
	} else if (ft == FORMATMOD_VHT) {
		mcs = mcs_index->vht.mcs;
		nss = mcs_index->vht.nss;
	} else if (ft >= FORMATMOD_HT_MF) {
		mcs = mcs_index->ht.mcs;
		nss = mcs_index->ht.nss;
	} else {
		mcs = mcs_index->legacy;
		nss = 0;
	}

	if (ft > FORMATMOD_NON_HT_DUP_OFDM) {
		switch (ft) {
		case FORMATMOD_HT_MF:
		case FORMATMOD_HT_GF:
			rate_idx = nss * 32 + mcs * 4 + bw * 2 + sgi + N_CCK; /* add CCK */
			break;
		case FORMATMOD_VHT:
			rate_idx = nss * 80 + mcs * 8 + bw * 2 + sgi;
			break;
		case FORMATMOD_HE_SU:
			rate_idx = nss * 144 + mcs * 12 + bw * 3 + sgi + 1; /* add CCK 1M */
			break;
		case FORMATMOD_HE_ER:
			rate_idx = NUM_TX_RATE_HE_SU + bw * 9 + mcs * 3 + sgi;
			break;
		default:
			rate_idx = N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU + N_HE_MU + bw * 9 +
				   mcs * 3 + sgi;
			break;
		}
	} else {
		if (mcs < 4) {
			rate_idx = mcs * 2 + pre;
		} else {
			rate_idx = N_CCK + mcs - 4;
		}
	}

	if (rate_idx >= sta_stats->tx_rate.size) {
		bes_err(BES_DBG_TX, "tx: Invalid index conversion => %d/%d\n",
			rate_idx, sta_stats->tx_rate.size);
		return;
	}

	rate_table = &sta_stats->tx_rate.table[rate_idx];
	if (!rate_table->enable) {
		rate_table->enable = true;
		sta_stats->tx_rate.rate_cnt++;
	}

	rate_table->tx_mpdu += rc_rate_stats->success;
	rate_table->mpdu_lost += rc_rate_stats->attempts - rc_rate_stats->success;

	sta_stats->tx_rate.tx_mpdu += rc_rate_stats->success;
	sta_stats->tx_rate.attempts += rc_rate_stats->attempts;
}

void bes_tx_update_peer_info(struct rwnx_hw *rwnx_hw,
			     struct rwnx_sta *rwnx_sta,
			     struct me_rc_stats_cfm *rc_stats)
{
	int i;
	struct rwnx_sta_stats *sta_stats = &rwnx_sta->stats;

	if (!sta_stats->tx_rate.size)
		return;

	for (i = 0; i < rc_stats->no_samples; i++)
		_bes_tx_update_peer_info(sta_stats, &rc_stats->rate_stats[i]);

	if (rc_stats->rate_stats[RC_HE_STATS_IDX].rate_config != 0)
		_bes_tx_update_peer_info(sta_stats, &rc_stats->rate_stats[RC_HE_STATS_IDX]);
}

void bes_tx_uldelay_update(struct rwnx_sw_txhdr *sw_txhdr)
{
	struct rwnx_sta *sta = sw_txhdr ? sw_txhdr->rwnx_sta : NULL;

	if (!sw_txhdr || !sta) {
		return;
	}

	sta->stats.ul_delay[sta->stats.ul_count] = jiffies_to_usecs(jiffies - sw_txhdr->jiffies);

	sta->stats.ul_count++;
	sta->stats.ul_count %= BES_ULDELAY_LENGTH;
}

u32 bes_tx_uldelay_get(struct rwnx_sta *rwnx_sta)
{
	u32 ul_delay = 0, i;

	if (!rwnx_sta) {
		bes_err(BES_DBG_TX, "%s rwnx_sta is NULL\n", __func__);
		return -1;
	}

	for (i = 0; i < BES_ULDELAY_LENGTH; i++) {
		ul_delay += rwnx_sta->stats.ul_delay[i] / BES_ULDELAY_LENGTH;
	}

	return ul_delay;
}

int tx_rate_table_idx_to_cfg_idx(struct rwnx_tx_rate_stats *rate_stats, int idx)
{
	int rate_idx = idx;

	switch (rate_stats->size) {
	case NUM_TX_RATE_NON_HT:
		break; /* do nothing */
	case NUM_TX_RATE_HT:
		if (idx >= 0 && idx < N_CCK)
			rate_idx = idx;
		else
			rate_idx = rate_idx + N_OFDM;
		break;
	case NUM_TX_RATE_VHT:
		rate_idx = rate_idx + N_CCK + N_OFDM + N_HT;
		break;
	case NUM_TX_RATE_HE_SU_ER:
		if (idx >= NUM_TX_RATE_HE_SU)
			rate_idx = rate_idx - NUM_TX_RATE_HE_SU + N_CCK + N_OFDM + N_HT +
				   N_VHT + N_HE_SU + N_HE_MU; /* HE_ER */
		else if (idx > 0)
			rate_idx = rate_idx - 1 + N_CCK + N_OFDM + N_HT + N_VHT;
		break;
	default:
		rate_idx = -1;
		break;
	}

	return rate_idx;
}

int cfg_idx_to_tx_rate_table_idx(struct rwnx_tx_rate_stats *rate_stats, int idx)
{
	int rate_idx = idx;

	switch (rate_stats->size) {
	case NUM_TX_RATE_NON_HT:
		break; /* do nothing */
	case NUM_TX_RATE_HT:
		if (idx >= 0 && idx < N_CCK)
			rate_idx = idx;
		else if (idx >= (N_CCK + N_OFDM))
			rate_idx = rate_idx - N_OFDM;
		else
			rate_idx = -1;
		break;
	case NUM_TX_RATE_VHT:
		rate_idx = rate_idx - N_CCK - N_OFDM - N_HT;
		break;
	case NUM_TX_RATE_HE_SU_ER:
		if (idx == 0)
			return 0;
		rate_idx = rate_idx - N_CCK - N_OFDM - N_HT - N_VHT;
		if (rate_idx >= 0 && rate_idx < NUM_TX_RATE_HE_SU - 1)
			rate_idx = rate_idx + 1; /* HE SU */
		else if (rate_idx >= (N_HE_SU + N_HE_MU))
			rate_idx = rate_idx + NUM_TX_RATE_HE_SU - N_HE_SU - N_HE_MU; /* HE ER */
		else
			rate_idx = -1;
		break;
	default:
		rate_idx = -1;
		break;
	}

	if (rate_idx >= rate_stats->size)
		rate_idx = -1;

	return rate_idx;
}

void bes_lstats_vif_start(struct rwnx_vif *rwnx_vif)
{
	struct bes_lstats_vif *lstats_vif = &rwnx_vif->lstats_vif;
	struct bes_vif_stat *v_stat = &rwnx_vif->lstats_vif.v_stat;
	struct net_device_stats *net_stats = &rwnx_vif->net_stats;

	memset(lstats_vif, 0, sizeof(*lstats_vif));
	v_stat->radio_on_start_time = ktime_get();
	v_stat->radio_on_with_ps_time = rwnx_vif->rwnx_hw->stats.ps_time.total_time;
	memset(net_stats, 0, sizeof(*net_stats));
}

void bes_lstats_vif_stop(struct rwnx_vif *rwnx_vif)
{
	struct bes_lstats_vif *lstats_vif = &rwnx_vif->lstats_vif;
	struct net_device_stats *net_stats = &rwnx_vif->net_stats;

	memset(lstats_vif, 0, sizeof(*lstats_vif));
	memset(net_stats, 0, sizeof(*net_stats));
}

void bes_lstats_phy_start(struct rwnx_hw *rwnx_hw)
{
	rwnx_hw->stats.phy_time.phy_on = true;
	rwnx_hw->stats.phy_time.start_time = ktime_get();

	rwnx_hw->stats.ps_time.start_time = ktime_get();
	rwnx_hw->stats.ps_time.total_time = 0;
}

void bes_lstats_phy_stop(struct rwnx_hw *rwnx_hw)
{
	rwnx_hw->stats.phy_time.phy_on = false;
	rwnx_hw->stats.phy_time.start_time = 0;

	rwnx_hw->stats.ps_time.start_time = 0;
	rwnx_hw->stats.ps_time.total_time = 0;
}

static void bes_lstats_ps_time_statistic(struct rwnx_hw *rwnx_hw)
{
	ktime_t diff;

	if (!rwnx_hw->stats.phy_time.phy_on)
		return;

	if (unlikely(rwnx_hw->stats.ps_time.start_time == 0))
		rwnx_hw->stats.ps_time.start_time = ktime_get();

	/* reset power saving time */
	if (unlikely(ktime_get() < rwnx_hw->stats.ps_time.start_time)) {
		rwnx_hw->stats.ps_time.start_time = ktime_get();
		rwnx_hw->stats.ps_time.total_time = 0;
	}

	diff = ktime_sub(ktime_get(), rwnx_hw->stats.ps_time.start_time);
	rwnx_hw->stats.ps_time.total_time = ktime_add(rwnx_hw->stats.ps_time.total_time, diff);
}

static void bes_lstats_reset_ps_start_time(struct rwnx_hw *rwnx_hw)
{
	if (!rwnx_hw->stats.phy_time.phy_on)
		return;

	rwnx_hw->stats.ps_time.start_time = ktime_get();
}

static void bes_lstats_ps_time_handler(void *arg, u8 status)
{
	struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)arg;

	if (status == PWR_LP_CB_STATUS_IO_SLEEP)
		bes_lstats_reset_ps_start_time(rwnx_hw);
	else if (status == PWR_LP_CB_STATUS_IO_WAKEUP)
		bes_lstats_ps_time_statistic(rwnx_hw);
}

static bool bes_ether_addr_equal(struct rwnx_sta *sta, struct ieee80211_mgmt *mgmt)
{
	if (!sta || !mgmt)
		return false;

	return ether_addr_equal(mgmt->bssid, sta->mac_addr);
}


static bool bes_lstats_vif_find_sta(struct rwnx_vif *rwnx_vif, struct ieee80211_mgmt *mgmt)
{
	bool find_sta = false;
	struct rwnx_sta *rwnx_sta = rwnx_vif->sta.ap;

	switch (RWNX_VIF_TYPE(rwnx_vif)) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		find_sta = bes_ether_addr_equal(rwnx_sta, mgmt);
		break;
	case NL80211_IFTYPE_AP_VLAN:
		find_sta = bes_ether_addr_equal(rwnx_vif->ap_vlan.sta_4a, mgmt);
		break;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_MESH_POINT:
	case NL80211_IFTYPE_P2P_GO: {
		struct rwnx_sta *sta;
		list_for_each_entry(sta, &rwnx_vif->ap.sta_list, list) {
			find_sta = bes_ether_addr_equal(sta, mgmt);
			if (find_sta)
				break;
		}
		break;
	}
	default:
		break;
	}

	return find_sta;
}

void bes_lstats_rssi_mgmt_update(struct rwnx_vif *rwnx_vif,
				 struct ieee80211_mgmt *mgmt, int rssi)
{
	struct bes_vif_stat *v_stat = &rwnx_vif->lstats_vif.v_stat;
	bool find_sta;

	if (rssi > 0)
		return;

	find_sta = bes_lstats_vif_find_sta(rwnx_vif, mgmt);
	if (!find_sta)
		return;

	/* overflow case */
	if (unlikely((v_stat->rssi_mgmt_sum + rssi > 0) || (v_stat->mgmt_count == INT_MAX))) {
		v_stat->mgmt_count = 0;
		v_stat->rssi_mgmt_sum = 0;
	}

	v_stat->mgmt_count++;
	v_stat->rssi_mgmt_sum += rssi;
	v_stat->rssi_mgmt = v_stat->rssi_mgmt_sum / v_stat->mgmt_count;
}

void bes_lstats_ac_tx_statistic(struct rwnx_vif *rwnx_vif, struct rwnx_txq *txq,
				union rwnx_hw_txstatus status)
{
	struct bes_vif_stat *v_stat = &rwnx_vif->lstats_vif.v_stat;

	if (status.acknowledged) {
		v_stat->ac_stat[rwnx_tid2hwq[txq->tid]].tx_mpdu++;
		v_stat->ac_stat[rwnx_tid2hwq[txq->tid]].retries += status.retry_cnt;
	}

	if (!status.acknowledged && !status.retry_cnt)
		v_stat->ac_stat[rwnx_tid2hwq[txq->tid]].mpdu_lost++;
}

void bes_lstats_ac_rx_statistic(struct rwnx_vif *rwnx_vif, struct hw_rxhdr *hw_rxhdr)
{
	struct bes_vif_stat *v_stat = &rwnx_vif->lstats_vif.v_stat;

	v_stat->ac_stat[rwnx_tid2hwq[hw_rxhdr->flags_user_prio]].rx_mpdu++;
}

void bes_lstats_beacon_statistic(struct rwnx_vif *rwnx_vif, struct sk_buff *skb)
{
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;
	bool find_sta = false;

	find_sta = bes_lstats_vif_find_sta(rwnx_vif, mgmt);

	if (!find_sta)
		return;

	rwnx_vif->lstats_vif.v_stat.beacon_rx++;
}

struct lstats_time bes_lstats_ktime_to_tm(ktime_t kt)
{
	struct timespec64 ts;
	struct lstats_time time;

	ts = ktime_to_timespec64(kt);
	/* Round up any ns */
	if (ts.tv_nsec)
		ts.tv_sec++;

	time.hour = (u32)div_s64(ts.tv_sec, 3600);
	time.min = (u8)div_s64(ts.tv_sec - time.hour * 3600, 60);
	time.sec = (u8)(ts.tv_sec - time.hour * 3600 - time.min * 60);

	return time;
}

void bes_get_vif_time_stats(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif)
{
	struct bes_vif_stat *v_stat = &rwnx_vif->lstats_vif.v_stat;
	ktime_t ps_time = 0;
	u64 trx_total_time = 0;
	u64 trx_total_bytes = 0, tmp = 0, tx_bytes, rx_bytes;

	if (!rwnx_vif->up) {
		memset(v_stat, 0, sizeof(*v_stat));
		return;
	}

	/* overflow check? */
	if (unlikely(ktime_get() < v_stat->radio_on_start_time)) {
		v_stat->radio_on_start_time = ktime_get();
		v_stat->radio_on_with_ps_time = rwnx_hw->stats.ps_time.total_time;
	}

	v_stat->on_time = ktime_sub(ktime_get(), v_stat->radio_on_start_time);

	if (unlikely(rwnx_hw->stats.ps_time.total_time < v_stat->radio_on_with_ps_time))
		v_stat->radio_on_with_ps_time = rwnx_hw->stats.ps_time.total_time;
	ps_time = ktime_sub(rwnx_hw->stats.ps_time.total_time, v_stat->radio_on_with_ps_time);

	/* Deviation caused by caculation start time */
	if (ps_time > v_stat->on_time)
		ps_time = v_stat->on_time;

	tx_bytes = rwnx_vif->net_stats.tx_bytes;
	rx_bytes = rwnx_vif->net_stats.rx_bytes;

	trx_total_bytes = tx_bytes + rx_bytes;
	trx_total_time = v_stat->on_time - ps_time;

	if (trx_total_bytes == 0) {
		v_stat->tx_time = 0;
		v_stat->rx_time = 0;

	} else {
		/* tx_time = (trx_total_time * tx_total_bytes) / trx_total_bytes; */
		/* rx_time = (trx_total_time * rx_total_bytes) / trx_total_bytes; */

		tmp = (tx_bytes * trx_total_time);
		do_div(tmp, trx_total_bytes);
		v_stat->tx_time = tmp;

		tmp = (rx_bytes * trx_total_time);
		do_div(tmp, trx_total_bytes);
		v_stat->rx_time = tmp;
	}
}

void bes_lstats_init(struct rwnx_hw *rwnx_hw)
{
	bes_pwr_register_lp_cb(rwnx_hw, bes_lstats_ps_time_handler, rwnx_hw);
}

void bes_lstats_deinit(struct rwnx_hw *rwnx_hw)
{
	bes_pwr_unregister_lp_cb(rwnx_hw, bes_lstats_ps_time_handler);
}

#ifdef CONFIG_VENDOR_CMD

static struct txrx_rate_info bes_get_txrx_rate_info(union rwnx_rate_ctrl_info *r_cfg,
						    int ru_size, u8 *preamble)
{
	union rwnx_mcs_index *mcs_index = (union rwnx_mcs_index *)r_cfg;
	struct txrx_rate_info txrx_rate = {0};

	txrx_rate.format_mod = r_cfg->formatModTx;
	txrx_rate.pre_type = r_cfg->giAndPreTypeTx >> 1;
	txrx_rate.he_gi_type = r_cfg->giAndPreTypeTx;
	txrx_rate.short_gi = r_cfg->giAndPreTypeTx & 0x1;
	txrx_rate.ch_bw = r_cfg->bwTx;
	txrx_rate.dcm = 0;

	if (txrx_rate.format_mod >= FORMATMOD_HE_SU) {
		txrx_rate.mcs = mcs_index->he.mcs;
		txrx_rate.nss = mcs_index->he.nss;
		txrx_rate.dcm = r_cfg->dcmTx;
		*preamble = 4;

		if (txrx_rate.format_mod > FORMATMOD_HE_SU)
			txrx_rate.ru_size = ru_size;

		if (txrx_rate.format_mod == FORMATMOD_HE_MU)
			txrx_rate.ch_bw = ru_size;
	} else if (txrx_rate.format_mod == FORMATMOD_VHT) {
		txrx_rate.mcs = mcs_index->vht.mcs;
		txrx_rate.nss = mcs_index->vht.nss;
		*preamble = 3;
	} else if (txrx_rate.format_mod >= FORMATMOD_HT_MF) {
		txrx_rate.mcs = mcs_index->ht.mcs;
		txrx_rate.nss = mcs_index->ht.nss;
		*preamble = 2;
	} else {
		txrx_rate.leg_rate = mcs_index->legacy;
		txrx_rate.nss = 0;

		if (txrx_rate.leg_rate < 4)
			*preamble = 1;
		else
			*preamble = 0;
	}

	return txrx_rate;
}

static void _bes_get_sta_txrx_peer_info(struct rwnx_sta *sta,
					struct wifi_rate_stat* rate_stat, u16 num_rate)
{
	int i, idx, j = 0;
	struct rwnx_tx_rate_stats *tx_rate = &sta->stats.tx_rate;
	struct rwnx_rx_rate_stats *rx_rate = &sta->stats.rx_rate;
	struct rwnx_tx_rate_table *tx_rate_table;
	int *rx_rate_table;

	if (!rx_rate->size || !tx_rate->size)
		return;

	tx_rate_table = tx_rate->table;
	rx_rate_table = rx_rate->table;

	if (!tx_rate_table || !rx_rate_table)
		return;

	for (i = 0; i < sta->stats.rx_rate.size && j < num_rate; i++) {
		bool find = false;

		/* rx_rate */
		if (rx_rate_table[i]) {
			rate_stat[j].rx_mpdu = rx_rate_table[i];
			find = true;
		}

		/* tx_rate */
		idx = cfg_idx_to_tx_rate_table_idx(tx_rate, i);
		if (idx >= tx_rate->size) {
			bes_err(BES_DBG_VENDOR_CMD, "%s idx(%d) is unexpected, i=%d, tx_rate->size:%d, rx_rate->size:%d\n",
				__func__, idx, i, tx_rate->size, rx_rate->size);
			continue;
		}

		if (idx >= 0 && idx < tx_rate->size && tx_rate_table[idx].enable) {
			rate_stat[j].tx_mpdu = tx_rate_table[idx].tx_mpdu;
			rate_stat[j].mpdu_lost = tx_rate_table[idx].mpdu_lost;
			find = true;
		}

		/* fill rate info */
		if (find) {
			struct rate_info cfg80211_rate = {0};
			struct txrx_rate_info txrx_rate;
			union rwnx_rate_ctrl_info rate_config;
			int ru_size = 0;
			u8 preamble = 0;

			idx_to_rate_cfg(i, &rate_config, &ru_size);
			txrx_rate = bes_get_txrx_rate_info(&rate_config, ru_size, &preamble);

			rwnx_fill_txrx_rate(&txrx_rate, &cfg80211_rate);
			rate_stat[j].rate.preamble = preamble;
			rate_stat[j].rate.nss = txrx_rate.nss;
			rate_stat[j].rate.bw = txrx_rate.ch_bw;
			rate_stat[j].rate.bitrate = cfg80211_calculate_bitrate(&cfg80211_rate);
			if (txrx_rate.format_mod < FORMATMOD_HT_MF)
				rate_stat[j].rate.rateMcsIdx = rate_stat[j].rate.bitrate / 5;
			else
				rate_stat[j].rate.rateMcsIdx = txrx_rate.mcs;

			j++;
		}
	}
}

static void bes_get_sta_txrx_peer_info(struct rwnx_vif *rwnx_vif,
				       struct wifi_iface_stat *iface_stat,
				       struct bes_peer_rate_num *num_stats)
{
	u8 *pos = (u8 *)iface_stat->peer_info;
	struct wifi_peer_info *peer_info;
	struct rwnx_sta *sta;
	int i = 0;
	enum nl80211_iftype iftype = RWNX_VIF_TYPE(rwnx_vif);

	switch (iftype) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		sta = rwnx_vif->sta.ap;
		if (!sta || !sta->valid)
			break;
		peer_info = (struct wifi_peer_info *)pos;
		peer_info->type = (iftype == NL80211_IFTYPE_STATION) ? WIFI_PEER_STA : WIFI_PEER_P2P_CLIENT;
		memcpy(peer_info->peer_mac_address, sta->mac_addr, ETH_ALEN);
		peer_info->num_rate = num_stats->num_rate[0];
		_bes_get_sta_txrx_peer_info(sta, peer_info->rate_stats, peer_info->num_rate);
		break;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
		list_for_each_entry(sta, &rwnx_vif->ap.sta_list, list) {
			peer_info = (struct wifi_peer_info *)pos;
			peer_info->type = (iftype == NL80211_IFTYPE_AP) ? WIFI_PEER_AP : WIFI_PEER_P2P_GO;
			memcpy(peer_info->peer_mac_address, sta->mac_addr, ETH_ALEN);
			peer_info->num_rate = num_stats->num_rate[i];
			_bes_get_sta_txrx_peer_info(sta, peer_info->rate_stats, num_stats->num_rate[i]);
			pos += sizeof(struct wifi_peer_info) + num_stats->num_rate[i] * sizeof(struct wifi_rate_stat);
			i++;
		}
		break;
	default:
		break;
	}
}

static u16 _bes_get_sta_txrx_rate_nums(struct rwnx_sta *sta)
{
	int i, idx;
	struct rwnx_tx_rate_stats *tx_rate = &sta->stats.tx_rate;
	struct rwnx_rx_rate_stats *rx_rate = &sta->stats.rx_rate;
	u16 comm_rate = 0;
	struct rwnx_tx_rate_table *tx_rate_table;
	int *rx_rate_table;

	if (!tx_rate->size || !rx_rate->size)
		return 0;

	tx_rate_table = tx_rate->table;
	rx_rate_table = rx_rate->table;

	if (!tx_rate_table || !rx_rate_table)
		return 0;

	/* common rate */
	for (i = 0; i < sta->stats.tx_rate.size; i++) {
		if (!tx_rate_table[i].enable)
			continue;

		idx = tx_rate_table_idx_to_cfg_idx(tx_rate, i);
		if (idx >= rx_rate->size) {
			bes_err(BES_DBG_VENDOR_CMD,
				"%s idx(%d) is unexpected, i=%d, tx_rate->size:%d, rx_rate->size:%d\n",
				__func__, idx, i, tx_rate->size, rx_rate->size);
			continue;
		}

		if (idx >= 0 && rx_rate_table[idx])
			comm_rate++;
	}

	return tx_rate->rate_cnt + rx_rate->rate_cnt - comm_rate;
}

void bes_get_sta_txrx_rate_nums(struct rwnx_vif *rwnx_vif,
				struct bes_peer_rate_num *num_stats)
{
	struct rwnx_sta *rwnx_sta = rwnx_vif->sta.ap;

	switch (RWNX_VIF_TYPE(rwnx_vif)) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		if (rwnx_sta) {
			num_stats->num_peers = 1;
			num_stats->num_rate[0] = _bes_get_sta_txrx_rate_nums(rwnx_sta);
		} else {
			num_stats->num_peers = 0;
		}
		break;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO: {
		struct rwnx_sta *sta;

		num_stats->num_peers = 0;
		list_for_each_entry(sta, &rwnx_vif->ap.sta_list, list) {
			num_stats->num_rate[num_stats->num_peers] = _bes_get_sta_txrx_rate_nums(sta);
			num_stats->num_peers++;
		}
		break;
	}
	default:
		num_stats->num_peers = 0;
		break;
	}
}

void bes_link_layer_get_peer_info(struct rwnx_vif *rwnx_vif,
				  struct wifi_iface_stat *iface_stat,
				  struct bes_peer_rate_num *num_stats)
{
	iface_stat->num_peers = num_stats->num_peers;
	if (iface_stat->num_peers == 0)
		return;

	bes_get_sta_txrx_peer_info(rwnx_vif, iface_stat, num_stats);
}

void bes_link_layer_get_radio_stats(struct rwnx_hw *rwnx_hw,
				    struct rwnx_vif *rwnx_vif,
				    struct wifi_radio_stat *radio_stat)
{
	struct bes_vif_stat *v_stat;

	bes_get_vif_time_stats(rwnx_hw, rwnx_vif);
	v_stat = &rwnx_vif->lstats_vif.v_stat;

	radio_stat->on_time = (u32)ktime_to_ms(v_stat->on_time);
	radio_stat->tx_time = (u32)ktime_to_ms(v_stat->tx_time);
	radio_stat->rx_time = (u32)ktime_to_ms(v_stat->rx_time);

	bes_dbg(BES_DBG_VENDOR_CMD, "%s %d, on_time = %u\n", __func__, __LINE__, radio_stat->on_time);
	bes_dbg(BES_DBG_VENDOR_CMD, "%s %d, tx_time = %u\n", __func__, __LINE__, radio_stat->tx_time);
	bes_dbg(BES_DBG_VENDOR_CMD, "%s %d, rx_time = %u\n", __func__, __LINE__, radio_stat->rx_time);
}

void bes_link_layer_get_iface_stats(struct rwnx_hw *rwnx_hw,
				    struct rwnx_vif *rwnx_vif,
				    struct wifi_iface_stat *iface_stat)
{
	struct bes_vif_stat *v_stat = &rwnx_vif->lstats_vif.v_stat;
	int i;

	if (!rwnx_vif->up)
		memset(v_stat, 0, sizeof(*v_stat));

	iface_stat->beacon_rx = v_stat->beacon_rx;
	iface_stat->rssi_mgmt = v_stat->rssi_mgmt;

	bes_dbg(BES_DBG_VENDOR_CMD, "%s, beacon_rx = %u\n", __func__, iface_stat->beacon_rx);
	bes_dbg(BES_DBG_VENDOR_CMD, "%s, rssi_mgmt = %d\n", __func__, iface_stat->rssi_mgmt);

	for (i = 0; i < WIFI_AC_MAX; i++) {
		iface_stat->ac[i].ac = i;
		iface_stat->ac[i].tx_mpdu = v_stat->ac_stat[BES_CFGVENDOR_AC_IDX(i)].tx_mpdu;
		iface_stat->ac[i].rx_mpdu = v_stat->ac_stat[BES_CFGVENDOR_AC_IDX(i)].rx_mpdu;
		iface_stat->ac[i].mpdu_lost = v_stat->ac_stat[BES_CFGVENDOR_AC_IDX(i)].mpdu_lost;
		iface_stat->ac[i].retries = v_stat->ac_stat[BES_CFGVENDOR_AC_IDX(i)].retries;

		bes_dbg(BES_DBG_VENDOR_CMD, "%s, ac[%d].tx_mpdu = %u\n", __func__, i, iface_stat->ac[i].tx_mpdu);
		bes_dbg(BES_DBG_VENDOR_CMD, "%s, ac[%d].rx_mpdu = %u\n", __func__, i, iface_stat->ac[i].rx_mpdu);
		bes_dbg(BES_DBG_VENDOR_CMD, "%s, ac[%d].mpdu_lost = %u\n", __func__, i, iface_stat->ac[i].mpdu_lost);
		bes_dbg(BES_DBG_VENDOR_CMD, "%s, ac[%d].retries = %u\n\n", __func__, i, iface_stat->ac[i].retries);
	}
}

#endif /* CONFIG_VENDOR_CMD */
