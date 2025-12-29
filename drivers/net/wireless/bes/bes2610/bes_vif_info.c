/*
 * @file bes_vif_info.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "bes_vif_info.h"
#include "bes_log.h"
#include "rwnx_msg_tx.h"
#include "bes_linkinfo.h"

static const int ru_size_he_er[] = {242, 106};
static const int ru_size_he_mu[] = {26, 52, 106, 242, 484, 996};

int bes_vif_info_ext_get(struct rwnx_vif *vif, int offset, size_t value_length, void *value)
{
	int ret;
	BUG_ON(!value);

	ret = rwnx_send_vif_info_ext_get_req(vif, offset, value_length, value);

	bes_err_with_cond(ret, BES_DBG_VIF_INFO, "%s get vif info ext failed(%d)\n", __func__, ret);

	return ret;
}

int bes_vif_info_ext_set(struct rwnx_vif *vif, int offset, size_t value_length, void *value)
{
	int ret;
	BUG_ON(!value);

	ret = rwnx_send_vif_info_ext_set_req(vif, offset, value_length, value);

	bes_err_with_cond(ret, BES_DBG_VIF_INFO, "%s set vif info ext failed(%d)\n", __func__, ret);

	return ret;
}

static void bes_set_ap_ageout_param(struct rwnx_vif *vif)
{
	u8 age_out_value = VIF_INFO_MM_AP_AGE_OUT_DEFAULT;
	u8 age_null_data_check_value = VIF_INFO_MM_AP_AGE_OUT_NULL_DATA_DEFAULT;
	u8 age_active_null_data_check_value = VIF_INFO_MM_AP_AGE_OUT_NULL_DATA_CHECK_DEFAULT;

	bes_vif_info_ext_set(vif, offsetof(struct vif_info_ext_tag, age_out_thd),
			     sizeof(u8), &age_out_value);
	bes_vif_info_ext_set(vif, offsetof(struct vif_info_ext_tag, age_null_data_check_thd),
			     sizeof(u8), &age_null_data_check_value);
	bes_vif_info_ext_set(vif, offsetof(struct vif_info_ext_tag, age_active_null_data_check_thd),
			    sizeof(u8), &age_active_null_data_check_value);
}

static void bes_set_sta_beacon_lost_param(struct rwnx_vif *vif)
{
	u8 bss_lst_value = vif->rwnx_hw->conf_file.bcn_loss_thd;
	u8 bss_null_data_thd = vif->rwnx_hw->conf_file.bcn_loss_null_data_thd;

	bes_vif_info_ext_set(vif, offsetof(struct vif_info_ext_tag, bss_lst),
			     sizeof(u8), &bss_lst_value);
	bes_vif_info_ext_set(vif, offsetof(struct vif_info_ext_tag, bss_null_data_check_thd),
			     sizeof(u8), &bss_null_data_thd);
}

void bes_set_rc_opt(struct rwnx_vif *vif)
{
#ifdef BES_RC_OPT
	u8 thd = (1<<RC_OPT_RTSCTS) | (1<<RC_OPT_SHORT_GI) | (1<<RC_OPT_AGC);

	/* Set Rate Control Optimize parameter */
	bes_vif_info_ext_set(vif, offsetof(struct vif_info_ext_tag, rc_opt), sizeof(u8), &thd);
#endif
}

static bool bes_match_oui_blacklist(struct rwnx_vif *rwnx_vif)
{
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	struct ieee80211_channel *chan = NULL;
	struct cfg80211_bss *bss = NULL;
	struct rwnx_sta *cur_sta = NULL;
	const struct cfg80211_bss_ies *beacon_ies, *proberesp_ies;
	int i = 0;
	bool match = false;

	/* Not set OUI blacklist, return directly */
	if (rwnx_hw->conf_file.oui_blacklist_cnt == 0)
		return false;

	/* Check the validity of cur_sta */
	cur_sta = rwnx_vif->sta.ap;
	if (cur_sta == NULL) {
		bes_err(BES_DBG_MSG_RX, "%s ERROR: rwnx_vif->sta.ap is NULL\n", __func__);
		return false;
	}

	/* Get BSS */
	chan = ieee80211_get_channel(rwnx_hw->wiphy, cur_sta->center_freq);
	if (!chan)
		return false;
	bss = cfg80211_get_bss(rwnx_hw->wiphy, chan, cur_sta->mac_addr,
			       rwnx_vif->sta.ap_ssid.array, rwnx_vif->sta.ap_ssid.length,
			       IEEE80211_BSS_TYPE_ANY, IEEE80211_PRIVACY_ANY);
	if (!bss)
		return false;

	/* Get extra information element(s) in Beacon frames and Probe Response frames */
	rcu_read_lock();
	beacon_ies = rcu_dereference(bss->beacon_ies);
	proberesp_ies = rcu_dereference(bss->proberesp_ies);
	if (!beacon_ies && !proberesp_ies)
		goto out;

	/* Comparison with OUI blacklist config */
	for (i = 0; i < rwnx_hw->conf_file.oui_blacklist_cnt; i++)
	{
		if ((beacon_ies && cfg80211_find_vendor_ie(rwnx_hw->conf_file.oui_blacklist[i], -1, beacon_ies->data, beacon_ies->len)) ||
		    (proberesp_ies && cfg80211_find_vendor_ie(rwnx_hw->conf_file.oui_blacklist[i], -1, proberesp_ies->data, proberesp_ies->len))) {
			match = true;
			goto out;
		}
	}

out:
	rcu_read_unlock();
	cfg80211_put_bss(rwnx_hw->wiphy, bss);
	return match;
}

static void bes_set_bcn_edca_param(struct rwnx_vif *vif, bool is_sta)
{
	int i, ret;
	u32 ac_param;
	struct rwnx_edca_config *edca;
	u8 vif_index = vif->vif_index;

	if (!vif->rwnx_hw->conf_file.edca_use_config || bes_match_oui_blacklist(vif) ||
	    vif_index >= NX_ITF_MAX)
		return;

	if (is_sta)
		edca = vif->rwnx_hw->conf_file.sta_edca;
	else
		edca = vif->rwnx_hw->conf_file.ap_edca;

	if ((ret = bes_send_ignore_bcn_edca_req(vif->rwnx_hw, true))) {
		bes_err(BES_DBG_VIF_INFO, "%s set vif disable beacon edca failed, ret:%d\n", __func__, ret);
		return;
	}

	for (i = 0; i < AC_MAX; i++) {
		ac_param = (u32)(edca[i].aifs);
		ac_param |= (u32)(edca[i].cwmin << 4);
		ac_param |= (u32)(edca[i].cwmax << 8);
		ac_param |= (u32)(edca[i].txop << 12);

		bes_dbg(BES_DBG_VIF_INFO, "set ac:%d edca param %x\n", i, ac_param);
		if ((ret = rwnx_send_set_edca(vif->rwnx_hw, i, ac_param, 0, vif_index))) {
			bes_err(BES_DBG_VIF_INFO, "%s set ac:%d edca param %x failed, ret:%d\n",
				__func__, i, ac_param, ret);
			return;
		}
	}
}

void bes_set_vif_ext_param(struct rwnx_vif *vif, bool start)
{
	if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_AP ||
	    RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_GO) {
		if (start) {
			bes_set_rc_opt(vif);
			bes_set_ap_ageout_param(vif);
			bes_set_bcn_edca_param(vif, false);
		}
	} else if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_STATION ||
		   RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_CLIENT) {
		if (start) {
			bes_set_rc_opt(vif);
			/* Set Beacon Lost parameters */
			bes_set_sta_beacon_lost_param(vif);
			bes_set_bcn_edca_param(vif, true);
		}
	}
}

int bes_vif_info_edca_get(struct rwnx_vif *vif, u32 *acm_param, u16 len)
{
	int ret;
	struct vif_info_edca_get_req req;

	BUG_ON(!acm_param);

	req.hdr.id = CUSTOM_ID_VIF_INFO;
	req.hdr.length = sizeof(struct vif_info_edca_get_req);
	req.hdr.ops = VIF_INFO_OPS_EDCA_GET;

	req.vif_index = vif->vif_index;
	if (req.vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_VIF_INFO, "%s invalid index\n", __func__);
		return -EINVAL;
	}

	ret = rwnx_send_custom_request(vif->rwnx_hw, &req.hdr, acm_param, len);

	bes_err_with_cond(ret, BES_DBG_VIF_INFO, "%s get vif info edca failed(%d)\n", __func__, ret);

	return ret;
}

void bes_set_vif_partition(struct rwnx_vif *vif1, struct rwnx_vif *vif2, u32 part1, u32 part2)
{
	struct rwnx_vif *rwnx_vif = NULL;
	bool go_exist = false;

	/* sync tdd partition info to device */
	bes_vif_info_ext_set(vif1,
		offsetof(struct vif_info_ext_tag, config_tdd_repartition_dur),
		sizeof(u32), &part1);
	bes_vif_info_ext_set(vif2,
		offsetof(struct vif_info_ext_tag, config_tdd_repartition_dur),
		sizeof(u32), &part2);

	/* loop vif list to check there is a p2p go interface */
	list_for_each_entry(rwnx_vif, &vif1->rwnx_hw->vifs, list) {
		/* vif needs to be open */
		if (!netif_running(rwnx_vif->ndev))
			continue;

		/* check if this vif is p2p go */
		if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_GO) {
			go_exist = true;
			break;
		}
	}

	/* don't need to update noa if there isn't a go interface */
	if (!go_exist)
		return;

	/* don't need to update noa if there is only one channel context */
	if (atomic_read(&vif1->rwnx_hw->chanctx_count) < 2)
		return;

	/* trigger noa update */
	bes_send_chan_noa_update(vif1);
}

int bes_vif_info_get_chan_load(struct rwnx_hw *rwnx_hw, u8 vif_idx, u32 *busy_ratio)
{
	struct vif_info_get_chan_load_req req;
	struct vif_info_get_ch_load_cfm cfm;
	u64 busy_time;
	int ret;

	if (vif_idx >= NX_ITF_MAX) {
		bes_err(BES_DBG_VIF_INFO, "%s invalid index\n", __func__);
		return -EINVAL;
	}

	req.hdr.id = CUSTOM_ID_VIF_INFO;
	req.hdr.length = sizeof(struct vif_info_get_chan_load_req);
	req.hdr.ops = VIF_INFO_OPS_GET_CH_LOAD;
	req.vif_index = vif_idx;

	ret = rwnx_send_custom_request(rwnx_hw, &req.hdr, &cfm, sizeof(cfm));
	if (ret) {
		bes_err(BES_DBG_MSG_TX, "%s failed(%d)\n", __func__, ret);
		return ret;
	}

	/* Convert to thousandth ratio. */
	busy_time = cfm.busy_time;
	if (cfm.measure_time)
		*busy_ratio = div_u64(busy_time * 1000, cfm.measure_time);
	else
		*busy_ratio = 0;

	return 0;
}

int bes_vif_info_print_rate(char *buf, int size, int format, int nss, int mcs, int bw, int sgi, int pre, int dcm, int *r_idx)
{
	int res = 0;
	int bitrates_cck[4] = {10, 20, 55, 110};
	int bitrates_ofdm[8] = {6, 9, 12, 18, 24, 36, 48, 54};
	char he_gi[3][4] = {"0.8", "1.6", "3.2"};

	if (format < FORMATMOD_HT_MF) {
		if (mcs < 4) {
			if (r_idx) {
				*r_idx = (mcs * 2) + pre;
				res = scnprintf(buf, size - res, "%4d ", *r_idx);
			}
			res += scnprintf(&buf[res], size - res, "L-CCK/%cP%11c%2u.%1uM   ", pre > 0 ? 'L' : 'S', ' ',
					 bitrates_cck[mcs] / 10, bitrates_cck[mcs] % 10);
		} else {
			mcs -= 4;
			if (r_idx) {
				*r_idx = N_CCK + mcs;
				res = scnprintf(buf, size - res, "%4d ", *r_idx);
			}
			res += scnprintf(&buf[res], size - res, "L-OFDM%13c%2u.0M   ", ' ', bitrates_ofdm[mcs]);
		}
	} else if (format < FORMATMOD_VHT) {
		if (r_idx) {
			*r_idx = N_CCK + N_OFDM + nss * 32 + mcs * 4 + bw * 2 + sgi;
			res = scnprintf(buf, size - res, "%4d ", *r_idx);
		}
		mcs += nss * 8;
		res += scnprintf(&buf[res], size - res, "HT%d/%cGI%11cMCS%-2d   ", 20 * (1 << bw), sgi ? 'S' : 'L', ' ',
				 mcs);
	} else if (format == FORMATMOD_VHT) {
		if (r_idx) {
			*r_idx = N_CCK + N_OFDM + N_HT + nss * 80 + mcs * 8 + bw * 2 + sgi;
			res = scnprintf(buf, size - res, "%4d ", *r_idx);
		}
		res += scnprintf(&buf[res], size - res, "VHT%d/%cGI%*cMCS%d/%1d  ", 20 * (1 << bw), sgi ? 'S' : 'L',
				 bw > 2 ? 9 : 10, ' ', mcs, nss + 1);
	} else if (format == FORMATMOD_HE_SU) {
		if (r_idx) {
			*r_idx = N_CCK + N_OFDM + N_HT + N_VHT + nss * 144 + mcs * 12 + bw * 3 + sgi;
			res = scnprintf(buf, size - res, "%4d ", *r_idx);
		}
		res += scnprintf(&buf[res], size - res, "HE%d/GI%s%4s%*cMCS%d/%1d%*c", 20 * (1 << bw), he_gi[sgi],
				 dcm ? "/DCM" : "", bw > 2 ? 4 : 5, ' ', mcs, nss + 1, mcs > 9 ? 1 : 2, ' ');
	} else if (format == FORMATMOD_HE_MU) {
		if (r_idx) {
			*r_idx = N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU + nss * 216 + mcs * 18 + bw * 3 + sgi;
			res = scnprintf(buf, size - res, "%4d ", *r_idx);
		}
		res += scnprintf(&buf[res], size - res, "HEMU-%d/GI%s%*cMCS%d/%1d%*c", ru_size_he_mu[bw], he_gi[sgi],
				 bw > 1 ? 5 : 6, ' ', mcs, nss + 1, mcs > 9 ? 1 : 2, ' ');

	} else // HE ER
	{
		if (r_idx) {
			*r_idx = N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU + N_HE_MU + bw * 9 + mcs * 3 + sgi;
			res = scnprintf(buf, size - res, "%4d ", *r_idx);
		}
		res += scnprintf(&buf[res], size - res, "HEER-%d/GI%s%4s%1cMCS%d/%1d%2c", ru_size_he_er[bw], he_gi[sgi],
				 dcm ? "/DCM" : "", ' ', mcs, nss + 1, ' ');
	}

	return res;
}

int bes_vif_info_print_rate_from_cfg(char *buf, int size, u32 rate_config, int *r_idx, int ru_size)
{
	union rwnx_rate_ctrl_info *r_cfg = (union rwnx_rate_ctrl_info *)&rate_config;
	union rwnx_mcs_index *mcs_index = (union rwnx_mcs_index *)&rate_config;
	unsigned int ft, pre, gi, bw, nss, mcs, dcm, len;

	ft = r_cfg->formatModTx;
	pre = r_cfg->giAndPreTypeTx >> 1;
	gi = r_cfg->giAndPreTypeTx;
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

	len = bes_vif_info_print_rate(buf, size, ft, nss, mcs, bw, gi, pre, dcm, r_idx);
	return len;
}

int bes_vif_info_get_hist_len(const char **hist)
{
	static const char rate_stats_hist[] = "##################################################";

	if (hist)
		*hist = rate_stats_hist;

	return sizeof(rate_stats_hist) - 1;
}

int bes_vif_info_tx_rate_dump(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta, char *buf, int buf_max_size,
			      int *rate_sort, int rate_print_num)
{
	struct rwnx_tx_rate_stats *rate_stats;
	struct rwnx_tx_rate_table *rate_table = NULL;
	int i, len = 0;
	bool have_others = false;
	u32 tx_mpdu_others = 0, attempts_others = 0;
	const char *hist;
	const int hist_len = bes_vif_info_get_hist_len(&hist);

	spin_lock_bh(&rwnx_hw->cb_lock);
	rate_stats = &sta->stats.tx_rate;
	rate_table = rate_stats->table;
	if (rate_table == NULL) {
		spin_unlock_bh(&rwnx_hw->cb_lock);
		return 0;
	}

	/* 36 bytes */
	len += scnprintf(buf, buf_max_size - 1, "\nTX rate info for %02X:xx:xx:xx:%02X:%02X:\n", sta->mac_addr[0],
						 sta->mac_addr[4], sta->mac_addr[5]);

	/* 19 + 8 + 18 + 18 = 63 */
	/* 63 + 2 - 1 bytes */
	len += scnprintf(&buf[len], buf_max_size - len, "FT/GI%14cMCS/NSS%11cTX_MPDU%9cATTEMPTS\n", ' ', ' ', ' ');

	for (i = 0; i < rate_stats->size; i++) {
		if (rate_table[i].enable)
			txrx_rate_sort(rate_sort, rate_print_num, i, rate_stats, true);
	}

	// Display Statistics of the most used
	for (i = 0; i < rate_print_num && rate_sort[i] >= 0; i++) {
		union rwnx_rate_ctrl_info rate_config;
		u32 peer_attempts;
		int percent_attempts;
		int percent_peer_mpdu;
		int p;
		int ru_size, idx;

		if (rate_sort[i] >= rate_stats->size) {
			bes_err(BES_DBG_VENDOR_CMD, "%s rate_sort[%d](%d) is unexpected, rate_stats->size:%d\n",
				__func__, i, rate_sort[i], rate_stats->size);
			continue;
		}

		peer_attempts = rate_table[rate_sort[i]].tx_mpdu + rate_table[rate_sort[i]].mpdu_lost;
		percent_attempts = div_u64(((u64)peer_attempts) * 1000, rate_stats->attempts);
		percent_peer_mpdu = div_u64(((u64)rate_table[rate_sort[i]].tx_mpdu) * 1000, peer_attempts);

		idx = tx_rate_table_idx_to_cfg_idx(rate_stats, rate_sort[i]);
		if (WARN_ON(idx < 0))
			continue;

		idx_to_rate_cfg(idx, &rate_config, &ru_size);
		len += bes_vif_info_print_rate_from_cfg(&buf[len], buf_max_size - len, rate_config.value, NULL, ru_size);
		len += scnprintf(&buf[len], buf_max_size - len, ": %9d(%2d.%1d%%)", rate_table[rate_sort[i]].tx_mpdu,
				 percent_peer_mpdu / 10, percent_peer_mpdu % 10);
		p = (percent_attempts * hist_len) / 1000;
		len += scnprintf(&buf[len], buf_max_size - len, " %9d(%2d.%1d%%)%.*s\n", peer_attempts,
				 percent_attempts / 10, percent_attempts % 10, p, hist);
	}

	if (rate_stats->rate_cnt > rate_print_num)
		have_others = true;

	// Display Statistics of ohter rates
	if (have_others) {
		for (i = 0; i < rate_stats->size; i++) {
			if (rate_table[i].enable &&
			    !txrx_rate_idx_is_most_used(rate_sort, rate_print_num, i)) {
				tx_mpdu_others += rate_table[i].tx_mpdu;
				attempts_others += rate_table[i].tx_mpdu + rate_table[i].mpdu_lost;
			}
		}

		if (tx_mpdu_others || attempts_others) {
			int percent_attemp_others = div_u64(((u64)attempts_others) * 1000, rate_stats->attempts);
			int percent_mpdu_others = div_u64(((u64)tx_mpdu_others) * 1000, attempts_others);
			int p;

			len += scnprintf(&buf[len], buf_max_size - len, "\nOther rate statistics:\n%27c", ' ');
			len += scnprintf(&buf[len], buf_max_size - len, ": %9d(%2d.%1d%%)", tx_mpdu_others,
					 percent_mpdu_others / 10, percent_mpdu_others % 10);
			p = (percent_attemp_others * hist_len) / 1000;
			len += scnprintf(&buf[len], buf_max_size - len, " %9d(%2d.%1d%%)%.*s\n", attempts_others,
					 percent_attemp_others / 10, percent_attemp_others % 10, p, hist);
		}
	}
	spin_unlock_bh(&rwnx_hw->cb_lock);

	return len;
}

int bes_vif_info_rx_rate_dump(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta, char *buf, int buf_max_size,
			      int *rate_sort, int rate_print_num)
{
	struct rwnx_rx_rate_stats *rate_stats;
	int *rate_table = NULL;
	int i, len = 0;
	unsigned int fmt, pre, bw, nss, mcs, gi, dcm = 0;
	struct rx_vector_1 *last_rx;
	u8 nrx;
	bool have_others = false;
	u32 rx_mpdu_others = 0;
	const char *hist;
	const int hist_len = bes_vif_info_get_hist_len(&hist);

	rate_stats = &sta->stats.rx_rate;
	spin_lock_bh(&rwnx_hw->cb_lock);
	rate_table = rate_stats->table;
	if (rate_table == NULL) {
		spin_unlock_bh(&rwnx_hw->cb_lock);
		return 0;
	}

	// Get number of RX paths
	nrx = (rwnx_hw->version_cfm.version_phy_1 & MDM_NRX_MASK) >> MDM_NRX_LSB;

	len += scnprintf(buf, buf_max_size, "\nRX rate info for %02X:xx:xx:xx:%02X:%02X:\n", sta->mac_addr[0],
						 sta->mac_addr[4], sta->mac_addr[5]);

	for (i = 0; i < rate_stats->size; i++) {
		if (rate_table[i])
			txrx_rate_sort(rate_sort, rate_print_num, i, rate_stats, false);
	}

	// Display Statistics of the most used
	for (i = 0; i < rate_print_num && rate_sort[i] >= 0; i++) {
		union rwnx_rate_ctrl_info rate_config;
		int percent = div_u64(((u64)rate_table[rate_sort[i]]) * 1000, rate_stats->cpt);
		int p;
		int ru_size;

		idx_to_rate_cfg(rate_sort[i], &rate_config, &ru_size);
		len += bes_vif_info_print_rate_from_cfg(&buf[len], buf_max_size - len, rate_config.value, NULL, ru_size);
		p = (percent * hist_len) / 1000;
		len += scnprintf(&buf[len], buf_max_size - len, ": %9d(%2d.%1d%%)%.*s\n", rate_table[rate_sort[i]],
				 percent / 10, percent % 10, p, hist);
	}

	if (rate_stats->rate_cnt > rate_print_num)
		have_others = true;

	// Display Statistics of ohter rates
	if (have_others) {
		for (i = 0; i < rate_stats->size; i++) {
			if (rate_table[i] &&
			    !txrx_rate_idx_is_most_used(rate_sort, rate_print_num, i))
				rx_mpdu_others += rate_table[i];
		}

		if (rx_mpdu_others) {
			int percent = div_u64(((u64)rx_mpdu_others) * 1000, rate_stats->cpt);
			int p;

			len += scnprintf(&buf[len], buf_max_size - len, "\nOther rate statistics:\n%27c", ' ');
			p = div_u64(percent * hist_len, 1000);
			len += scnprintf(&buf[len], buf_max_size - len, ": %9d(%2d.%1d%%)%.*s\n", rx_mpdu_others,
					 percent / 10, percent % 10, p, hist);
		}
	}

	// Display detailed info of the last received rate
	/* 20 + 67 * 2 = 154 */
	last_rx = &sta->stats.last_rx.rx_vect1;
	len += scnprintf(&buf[len], buf_max_size - len,
			 "\nLast received rate\n"
			 "type               rate     LDPC STBC BEAMFM DCM DOPPLER %s\n",
			 (nrx > 1) ? "rssi1(dBm) rssi2(dBm)" : "rssi(dBm)");

	fmt = last_rx->format_mod;
	bw = last_rx->ch_bw;
	pre = last_rx->pre_type;
	if (fmt >= FORMATMOD_HE_SU) {
		mcs = last_rx->he.mcs;
		nss = last_rx->he.nss;
		gi = last_rx->he.gi_type;
		if ((fmt == FORMATMOD_HE_MU) || (fmt == FORMATMOD_HE_ER))
			bw = last_rx->he.ru_size;
		dcm = last_rx->he.dcm;
	} else if (fmt == FORMATMOD_VHT) {
		mcs = last_rx->vht.mcs;
		nss = last_rx->vht.nss;
		gi = last_rx->vht.short_gi;
	} else if (fmt >= FORMATMOD_HT_MF) {
		mcs = last_rx->ht.mcs % 8;
		nss = last_rx->ht.mcs / 8;
		gi = last_rx->ht.short_gi;
	} else {
		BUG_ON((mcs = legrates_lut[last_rx->leg_rate].idx) == -1);
		nss = 0;
		gi = 0;
	}

	len += bes_vif_info_print_rate(&buf[len], buf_max_size - len, fmt, nss, mcs, bw, gi, pre, dcm, NULL);

	/* flags for HT/VHT/HE */
	if (fmt >= FORMATMOD_HE_SU) {
		len += scnprintf(&buf[len], buf_max_size - len, "  %c    %c     %c    %c     %c", last_rx->he.fec ? 'L' : ' ',
				 last_rx->he.stbc ? 'S' : ' ', last_rx->he.beamformed ? 'B' : ' ',
				 last_rx->he.dcm ? 'D' : ' ', last_rx->he.doppler ? 'D' : ' ');
	} else if (fmt == FORMATMOD_VHT) {
		len += scnprintf(&buf[len], buf_max_size - len, "  %c    %c     %c           ", last_rx->vht.fec ? 'L' : ' ',
				 last_rx->vht.stbc ? 'S' : ' ', last_rx->vht.beamformed ? 'B' : ' ');
	} else if (fmt >= FORMATMOD_HT_MF) {
		len += scnprintf(&buf[len], buf_max_size - len, "  %c    %c                  ", last_rx->ht.fec ? 'L' : ' ',
				 last_rx->ht.stbc ? 'S' : ' ');
	} else {
		len += scnprintf(&buf[len], buf_max_size - len, "                         ");
	}
	if (nrx > 1) {
		len += scnprintf(&buf[len], buf_max_size - len, "       %-4d       %d\n", last_rx->rssi1, last_rx->rssi1);
	} else {
		len += scnprintf(&buf[len], buf_max_size - len, "      %d\n", last_rx->rssi1);
	}

	spin_unlock_bh(&rwnx_hw->cb_lock);

	return len;
}