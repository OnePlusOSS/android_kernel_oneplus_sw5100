/**
 ******************************************************************************
 *
 * @file rwnx_debugfs.c
 *
 * @brief Definition of debugfs entries
 *
 * Copyright (C) RivieraWaves 2012-2021
 *
 ******************************************************************************
 */

#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/sort.h>
#include <linux/string.h>
#include <linux/rtnetlink.h>

#include "bes_pwr.h"
#include "rwnx_debugfs.h"
#include "rwnx_msg_tx.h"
#include "bes_log.h"
#include "reg_access.h"
#include "lmac_log_ctrl.h"
#include "bes_filter.h"
#include "bes_linkinfo.h"
#include "bes_vif_info.h"
#include "bes_csi.h"
#include "bes_phy.h"
#include "bes_dbac_cali.h"

DEBUGFS_READ_WRITE_FILE_OPS(log_ctrl);

#define TXRX_RATE_STATS_NUM 10

static struct rwnx_vif *rwnx_dbgfs_get_vif(char *dev_name)
{
	struct net_device *ndev;
	struct rwnx_vif *vif;

	/* __dev_get_by_name() must be called under RTNL semaphore or @dev_base_lock.
	 * When called dev_get_by_name(), The returned handle has the usage count incremented
	 * and the caller must use dev_put() to release it when it is no longer needed.*/
	ndev = dev_get_by_name(&init_net, dev_name);
	if (!ndev)
		return NULL;

	vif = (struct rwnx_vif *)netdev_priv(ndev);
	dev_put(ndev);

	return vif;
}

static ssize_t rwnx_dbgfs_log_ctrl_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	return log_ctrl_write(file, user_buf, count, ppos);
}

static ssize_t rwnx_dbgfs_log_ctrl_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	return log_ctrl_read(file, user_buf, count, ppos);
}

#ifdef CONFIG_RWNX_SOFTMAC
static ssize_t rwnx_dbgfs_stats_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char *buf;
	int per;
	int ret;
	int i, skipped;
	ssize_t read;
	int bufsz = (10 + NX_TX_PAYLOAD_MAX + NX_TXQ_CNT + IEEE80211_MAX_AMPDU_BUF_HE) * 50;

	buf = kmalloc(bufsz, GFP_ATOMIC);
	if (buf == NULL)
		return 0;

	if (priv->stats.agg_done)
		per = DIV_ROUND_UP((priv->stats.agg_retries + priv->stats.agg_died) * 100, priv->stats.agg_done);
	else
		per = 0;

	ret = scnprintf(buf, min_t(size_t, bufsz - 1, count),
			"agg_done         %10d\n"
			"agg_retries      %10d\n"
			"agg_retries_last %10d\n"
			"agg_died         %10d\n"
			"ampdu_all_ko     %10d\n"
			"agg_PER (%%)      %10d\n"
			"queues_stops     %10d\n\n",
			priv->stats.agg_done, priv->stats.agg_retries, priv->stats.agg_retries_last,
			priv->stats.agg_died, priv->stats.ampdu_all_ko, per, priv->stats.queues_stops);

	ret += scnprintf(&buf[ret], min_t(size_t, bufsz - 1, count - ret), "TXQs CFM balances ");
	for (i = 0; i < NX_TXQ_CNT; i++)
		ret += scnprintf(&buf[ret], min_t(size_t, bufsz - 1, count - ret), "  [%1d]:%3d", i,
				 priv->stats.cfm_balance[i]);

#ifdef CONFIG_RWNX_SPLIT_TX_BUF
	ret += scnprintf(&buf[ret], min_t(size_t, bufsz - 1, count - ret),
			 "\n\nAMSDU[len]             done   failed(%%)\n");
	for (i = skipped = 0; i < NX_TX_PAYLOAD_MAX; i++) {
		if (priv->stats.amsdus[i].done) {
			per = DIV_ROUND_UP((priv->stats.amsdus[i].failed) * 100, priv->stats.amsdus[i].done);
		} else {
			per = 0;
			skipped = 1;
			continue;
		}
		if (skipped) {
			ret += scnprintf(&buf[ret], min_t(size_t, bufsz - 1, count - ret),
					 "   * * *         %10d  %10d\n", 0, 0);
			skipped = 0;
		}

		ret += scnprintf(&buf[ret], min_t(size_t, bufsz - 1, count - ret), "   [%1d]           %10d  %10d\n",
				 i + 1, priv->stats.amsdus[i].done, per);
	}
	if (skipped)
		ret += scnprintf(&buf[ret], min_t(size_t, bufsz - 1, count - ret), "   * * *         %10d  %10d\n", 0,
				 0);
#endif

	ret += scnprintf(&buf[ret], min_t(size_t, bufsz - ret - 1, count - ret),
			 "\nIn-AMPDU     TX failures(%%)   RX counts\n");
	for (i = skipped = 0; i < IEEE80211_MAX_AMPDU_BUF_HE; i++) {
		int failed;

		if (priv->stats.in_ampdu[i].done) {
			failed = DIV_ROUND_UP(priv->stats.in_ampdu[i].failed * 100, priv->stats.in_ampdu[i].done);
		} else {
			if (!priv->stats.rx_in_ampdu[i].cnt) {
				skipped = 1;
				continue;
			}
			failed = 0;
		}
		if (skipped) {
			ret += scnprintf(&buf[ret], min_t(size_t, bufsz - ret - 1, count - ret),
					 "   * * *         %10d  %10d\n", 0, 0);
			skipped = 0;
		}
		ret += scnprintf(&buf[ret], min_t(size_t, bufsz - ret - 1, count - ret),
				 "   mpdu#%2d       %10d  %10d\n", i, failed, priv->stats.rx_in_ampdu[i].cnt);
	}
	if (skipped)
		ret += scnprintf(&buf[ret], min_t(size_t, bufsz - ret - 1, count - ret),
				 "   * * *         %10d  %10d\n", 0, 0);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	kfree(buf);

	return read;
}

#else

static ssize_t rwnx_dbgfs_stats_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char *buf;
	int ret;
	int i, skipped;
#ifdef CONFIG_RWNX_SPLIT_TX_BUF
	int per;
#endif
	ssize_t read;
	int bufsz = (NX_TXQ_CNT)*20 + (ARRAY_SIZE(priv->stats.amsdus_rx) + 1) * 40 +
		    (ARRAY_SIZE(priv->stats.ampdus_tx) * 30 + 100);
	struct lstats_time ps_tm = {0};
	struct lstats_time total_tm = {0};
	int percent_ps_tm = 0;
	struct phy_states_get_cfm cfm = {0};
	int tx_duty_ratio;

	if (priv->stats.phy_time.phy_on) {
		ktime_t diff = ktime_sub(ktime_get(), priv->stats.phy_time.start_time);
		total_tm = bes_lstats_ktime_to_tm(diff);
		ps_tm = bes_lstats_ktime_to_tm(priv->stats.ps_time.total_time);
		percent_ps_tm = div_s64(priv->stats.ps_time.total_time * 1000, diff);
	}
	bes_get_phy_state(priv, &cfm);
	tx_duty_ratio = bes_phy_calc_tx_duty_ratio(&cfm);

	if (*ppos)
		return 0;

	buf = kmalloc(bufsz, GFP_ATOMIC);
	if (buf == NULL)
		return 0;

	ret = scnprintf(&buf[0], bufsz, "\nTotal Time:      %uh:%um:%us\n", total_tm.hour,
			 total_tm.min, total_tm.sec);
	ret += scnprintf(&buf[ret], bufsz - ret, "PS Time   :      %uh:%um:%us(%2d.%1d%%)\n",
			 ps_tm.hour, ps_tm.min, ps_tm.sec, percent_ps_tm / 10, percent_ps_tm % 10);
	ret += scnprintf(&buf[ret], bufsz - ret, "CRC Fail :%u packets\n", cfm.fcs_error_count);
	ret += scnprintf(&buf[ret], bufsz - ret, "Sync Fail :%u packets\n", cfm.phy_error_count);
	ret += scnprintf(&buf[ret], bufsz - ret, "TX duty ratio :%d.%d%%\n", tx_duty_ratio / 10, tx_duty_ratio % 10);

	ret += scnprintf(&buf[ret], bufsz - ret, "TXQs Cfm Balances ");
	for (i = 0; i < NX_TXQ_CNT; i++)
		ret += scnprintf(&buf[ret], bufsz - ret, "  [%1d]:%3d", i, priv->stats.cfm_balance[i]);

	ret += scnprintf(&buf[ret], bufsz - ret, "\n");

#ifdef CONFIG_RWNX_SPLIT_TX_BUF
	ret += scnprintf(&buf[ret], bufsz - ret, "\nAMSDU[len]       Done         Failed   Received\n");
	for (i = skipped = 0; i < NX_TX_PAYLOAD_MAX; i++) {
		if (priv->stats.amsdus[i].done) {
			per = DIV_ROUND_UP((priv->stats.amsdus[i].failed) * 100, priv->stats.amsdus[i].done);
		} else if (priv->stats.amsdus_rx[i]) {
			per = 0;
		} else {
			per = 0;
			skipped = 1;
			continue;
		}
		if (skipped) {
			ret += scnprintf(&buf[ret], bufsz - ret, "   ...\n");
			skipped = 0;
		}

		ret += scnprintf(&buf[ret], bufsz - ret, "   [%2d]    %10d %8d(%3d%%) %10d\n", i ? i + 1 : i,
				 priv->stats.amsdus[i].done, priv->stats.amsdus[i].failed, per,
				 priv->stats.amsdus_rx[i]);
	}

	for (; i < ARRAY_SIZE(priv->stats.amsdus_rx); i++) {
		if (!priv->stats.amsdus_rx[i]) {
			skipped = 1;
			continue;
		}
		if (skipped) {
			ret += scnprintf(&buf[ret], bufsz - ret, "   ...\n");
			skipped = 0;
		}

		ret += scnprintf(&buf[ret], bufsz - ret, "   [%2d]                              %10d\n", i + 1,
				 priv->stats.amsdus_rx[i]);
	}
#else
	ret += scnprintf(&buf[ret], bufsz - ret, "\nAMSDU[len]   Received\n");
	for (i = skipped = 0; i < ARRAY_SIZE(priv->stats.amsdus_rx); i++) {
		if (!priv->stats.amsdus_rx[i]) {
			skipped = 1;
			continue;
		}
		if (skipped) {
			ret += scnprintf(&buf[ret], bufsz - ret, "   ...\n");
			skipped = 0;
		}

		ret += scnprintf(&buf[ret], bufsz - ret, "   [%2d]    %10d\n", i + 1, priv->stats.amsdus_rx[i]);
	}

#endif /* CONFIG_RWNX_SPLIT_TX_BUF */

	ret += scnprintf(&buf[ret], bufsz - ret, "\nAMPDU[len]     Done  Received\n");
	for (i = skipped = 0; i < ARRAY_SIZE(priv->stats.ampdus_tx); i++) {
		if (!priv->stats.ampdus_tx[i] && !priv->stats.ampdus_rx[i]) {
			skipped = 1;
			continue;
		}
		if (skipped) {
			ret += scnprintf(&buf[ret], bufsz - ret, "    ...\n");
			skipped = 0;
		}

		ret += scnprintf(&buf[ret], bufsz - ret, "   [%2d]   %9d %9d\n", i ? i + 1 : i,
				 priv->stats.ampdus_tx[i], priv->stats.ampdus_rx[i]);
	}

	ret += scnprintf(&buf[ret], bufsz - ret, "#MPDU Missed        %9d\n", priv->stats.ampdus_rx_miss);
	read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	kfree(buf);

	return read;
}
#endif /* CONFIG_RWNX_SOFTMAC */

static ssize_t rwnx_dbgfs_stats_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;

	/* Prevent from interrupt preemption as these statistics are updated under
	 * interrupt */
	spin_lock_bh(&priv->tx_lock);

	memset(&priv->stats, 0, sizeof(priv->stats));

	spin_unlock_bh(&priv->tx_lock);

	return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(stats);

#define TXQ_STA_PREF "tid|"
#define TXQ_STA_PREF_FMT "%3d|"

#ifdef CONFIG_RWNX_FULLMAC
#define TXQ_VIF_PREF "type|"
#define TXQ_VIF_PREF_FMT "%4s|"
#else
#define TXQ_VIF_PREF "AC|"
#define TXQ_VIF_PREF_FMT "%2s|"
#endif /* CONFIG_RWNX_FULLMAC */

#define TXQ_HDR "idx|  status|credit|ready|retry|pushed"
#define TXQ_HDR_FMT "%3d|%s%s%s%s%s%s%s%s%s|%6d|%5d|%5d|%6d"

#ifdef CONFIG_RWNX_AMSDUS_TX
#ifdef CONFIG_RWNX_FULLMAC
#define TXQ_HDR_SUFF "|amsdu"
#define TXQ_HDR_SUFF_FMT "|%5d"
#else
#define TXQ_HDR_SUFF "|amsdu-ht|amdsu-vht"
#define TXQ_HDR_SUFF_FMT "|%8d|%9d"
#endif /* CONFIG_RWNX_FULLMAC */
#else
#define TXQ_HDR_SUFF ""
#define TXQ_HDR_SUF_FMT ""
#endif /* CONFIG_RWNX_AMSDUS_TX */

#define TXQ_HDR_MAX_LEN (sizeof(TXQ_STA_PREF) + sizeof(TXQ_HDR) + sizeof(TXQ_HDR_SUFF) + 1)

#ifdef CONFIG_RWNX_FULLMAC
#define PS_HDR "Legacy PS: ready=%d, sp=%d / UAPSD: ready=%d, sp=%d"
#define PS_HDR_LEGACY "Legacy PS: ready=%d, sp=%d"
#define PS_HDR_UAPSD "UAPSD: ready=%d, sp=%d"
#define PS_HDR_MAX_LEN sizeof("Legacy PS: ready=xxx, sp=xxx / UAPSD: ready=xxx, sp=xxx\n")
#else
#define PS_HDR ""
#define PS_HDR_MAX_LEN 0
#endif /* CONFIG_RWNX_FULLMAC */

#define STA_HDR "** STA %d (%pM)\n"
#define STA_HDR_MAX_LEN sizeof("- STA xx (xx:xx:xx:xx:xx:xx)\n") + PS_HDR_MAX_LEN

#ifdef CONFIG_RWNX_FULLMAC
#define VIF_HDR "* VIF [%d] %s\n"
#define VIF_HDR_MAX_LEN sizeof(VIF_HDR) + IFNAMSIZ
#else
#define VIF_HDR "* VIF [%d]\n"
#define VIF_HDR_MAX_LEN sizeof(VIF_HDR)
#endif

#ifdef CONFIG_RWNX_AMSDUS_TX

#ifdef CONFIG_RWNX_FULLMAC
#define VIF_SEP "---------------------------------------\n"
#else
#define VIF_SEP "----------------------------------------------------\n"
#endif /* CONFIG_RWNX_FULLMAC */

#else /* ! CONFIG_RWNX_AMSDUS_TX */
#define VIF_SEP "---------------------------------\n"
#endif /* CONFIG_RWNX_AMSDUS_TX*/

#define VIF_SEP_LEN sizeof(VIF_SEP)

#define CAPTION                                                                                                        \
	"status: L=in hwq list, F=stop full, P=stop sta PS, V=stop vif PS,\
 C=stop channel, S=stop CSA, M=stop MU, P=Flushing N=Ndev queue stopped"
#define CAPTION_LEN sizeof(CAPTION)

#define STA_TXQ 0
#define VIF_TXQ 1

static int rwnx_dbgfs_txq(char *buf, size_t size, struct rwnx_txq *txq, int type, int tid, char *name)
{
	int res, idx = 0;
	int i, pushed = 0;

	if (type == STA_TXQ) {
		res = scnprintf(&buf[idx], size, TXQ_STA_PREF_FMT, tid);
		idx += res;
		size -= res;
	} else {
		res = scnprintf(&buf[idx], size, TXQ_VIF_PREF_FMT, name);
		idx += res;
		size -= res;
	}

	for (i = 0; i < CONFIG_USER_MAX; i++) {
		pushed += txq->pkt_pushed[i];
	}

	res = scnprintf(
		&buf[idx], size, TXQ_HDR_FMT, txq->idx,
		(txq->status & (1 << RWNX_TXQ_IN_HWQ_LIST)) ? "L" : " ",
		(txq->status & (1 << RWNX_TXQ_STOP_FULL)) ? "F" : " ",
		(txq->status & (1 << RWNX_TXQ_STOP_STA_PS)) ? "P" : " ",
		(txq->status & (1 << RWNX_TXQ_STOP_VIF_PS)) ? "V" : " ",
		(txq->status & (1 << RWNX_TXQ_STOP_CHAN)) ? "C" : " ",
		(txq->status & (1 << RWNX_TXQ_STOP_CSA)) ? "S" : " ",
		(txq->status & (1 << RWNX_TXQ_STOP_MU_POS)) ? "M" : " ",
		(txq->status & (1 << RWNX_TXQ_STOP_FLUSH)) ? "P" : " ",
		(txq->status & (1 << RWNX_TXQ_NDEV_FLOW_CTRL)) ? "N" : " ",
		txq->credits, skb_queue_len(&txq->sk_list),
		txq->nb_retry, pushed);
	idx += res;
	size -= res;

#ifdef CONFIG_RWNX_AMSDUS_TX
	if (type == STA_TXQ) {
		res = scnprintf(&buf[idx], size, TXQ_HDR_SUFF_FMT,
#ifdef CONFIG_RWNX_FULLMAC
				txq->amsdu_len
#else
				txq->amsdu_ht_len_cap, txq->amsdu_vht_len_cap
#endif /* CONFIG_RWNX_FULLMAC */
		);
		idx += res;
		size -= res;
	}
#endif

	res = scnprintf(&buf[idx], size, "\n");
	idx += res;
	size -= res;

	return idx;
}

static int rwnx_dbgfs_txq_sta(char *buf, size_t size, struct rwnx_sta *rwnx_sta, struct rwnx_hw *rwnx_hw)
{
	int tid, res, idx = 0;
	struct rwnx_txq *txq;
#ifdef CONFIG_RWNX_SOFTMAC
	struct ieee80211_sta *sta = rwnx_to_ieee80211_sta(rwnx_sta);
#endif /* CONFIG_RWNX_SOFTMAC */

	res = scnprintf(&buf[idx], size, "\n" STA_HDR, rwnx_sta->sta_idx,
#ifdef CONFIG_RWNX_SOFTMAC
			sta->addr
#else
			rwnx_sta->mac_addr
#endif /* CONFIG_RWNX_SOFTMAC */
	);
	idx += res;
	size -= res;

#ifdef CONFIG_RWNX_FULLMAC
	if (rwnx_sta->ps.active) {
		if (rwnx_sta->uapsd_tids && (rwnx_sta->uapsd_tids == ((1 << NX_NB_TXQ_PER_STA) - 1)))
			res = scnprintf(&buf[idx], size, PS_HDR_UAPSD "\n", rwnx_sta->ps.pkt_ready[UAPSD_ID],
					rwnx_sta->ps.sp_cnt[UAPSD_ID]);
		else if (rwnx_sta->uapsd_tids)
			res = scnprintf(&buf[idx], size, PS_HDR "\n", rwnx_sta->ps.pkt_ready[LEGACY_PS_ID],
					rwnx_sta->ps.sp_cnt[LEGACY_PS_ID], rwnx_sta->ps.pkt_ready[UAPSD_ID],
					rwnx_sta->ps.sp_cnt[UAPSD_ID]);
		else
			res = scnprintf(&buf[idx], size, PS_HDR_LEGACY "\n", rwnx_sta->ps.pkt_ready[LEGACY_PS_ID],
					rwnx_sta->ps.sp_cnt[LEGACY_PS_ID]);
		idx += res;
		size -= res;
	} else {
		res = scnprintf(&buf[idx], size, "\n");
		idx += res;
		size -= res;
	}
#endif /* CONFIG_RWNX_FULLMAC */

	res = scnprintf(&buf[idx], size, TXQ_STA_PREF TXQ_HDR TXQ_HDR_SUFF "\n");
	idx += res;
	size -= res;

	foreach_sta_txq(rwnx_sta, txq, tid, rwnx_hw)
	{
		res = rwnx_dbgfs_txq(&buf[idx], size, txq, STA_TXQ, tid, NULL);
		idx += res;
		size -= res;
	}

	return idx;
}

static int rwnx_dbgfs_txq_vif(char *buf, size_t size, struct rwnx_vif *rwnx_vif, struct rwnx_hw *rwnx_hw)
{
	int res, idx = 0;
	struct rwnx_txq *txq;
	struct rwnx_sta *rwnx_sta;

#ifdef CONFIG_RWNX_FULLMAC
	res = scnprintf(&buf[idx], size, VIF_HDR, rwnx_vif->vif_index, rwnx_vif->ndev->name);
	idx += res;
	size -= res;
	if (!rwnx_vif->up || rwnx_vif->ndev == NULL)
		return idx;

#else
	int ac;
	char ac_name[2] = {'0', '\0'};

	res = scnprintf(&buf[idx], size, VIF_HDR, rwnx_vif->vif_index);
	idx += res;
	size -= res;
#endif /* CONFIG_RWNX_FULLMAC */

#ifdef CONFIG_RWNX_FULLMAC
	if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_AP || RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_GO ||
	    RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_MESH_POINT) {
		res = scnprintf(&buf[idx], size, TXQ_VIF_PREF TXQ_HDR "\n");
		idx += res;
		size -= res;
		txq = rwnx_txq_vif_get(rwnx_vif, NX_UNK_TXQ_TYPE);
		res = rwnx_dbgfs_txq(&buf[idx], size, txq, VIF_TXQ, 0, "UNK");
		idx += res;
		size -= res;
		txq = rwnx_txq_vif_get(rwnx_vif, NX_BCMC_TXQ_TYPE);
		res = rwnx_dbgfs_txq(&buf[idx], size, txq, VIF_TXQ, 0, "BCMC");
		idx += res;
		size -= res;
		rwnx_sta = rwnx_rx_get_sta(rwnx_hw, rwnx_vif->ap.bcmc_index);
		if (WARN_ON(rwnx_sta == NULL))
			return idx;

		if (rwnx_sta->ps.active) {
			res = scnprintf(&buf[idx], size, PS_HDR_LEGACY "\n", rwnx_sta->ps.sp_cnt[LEGACY_PS_ID],
					rwnx_sta->ps.sp_cnt[LEGACY_PS_ID]);
			idx += res;
			size -= res;
		} else {
			res = scnprintf(&buf[idx], size, "\n");
			idx += res;
			size -= res;
		}

		list_for_each_entry(rwnx_sta, &rwnx_vif->ap.sta_list, list) {
			res = rwnx_dbgfs_txq_sta(&buf[idx], size, rwnx_sta, rwnx_hw);
			idx += res;
			size -= res;
		}
	} else if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION ||
		   RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_CLIENT) {
		rwnx_sta = rwnx_vif->sta.ap;

		if (rwnx_sta) {
			res = rwnx_dbgfs_txq_sta(&buf[idx], size, rwnx_sta, rwnx_hw);
			idx += res;
			size -= res;
		}
	}

#else
	res = scnprintf(&buf[idx], size, TXQ_VIF_PREF TXQ_HDR "\n");
	idx += res;
	size -= res;

	foreach_vif_txq(rwnx_vif, txq, ac)
	{
		ac_name[0]++;
		res = rwnx_dbgfs_txq(&buf[idx], size, txq, VIF_TXQ, 0, ac_name);
		idx += res;
		size -= res;
	}

	list_for_each_entry(rwnx_sta, &rwnx_vif->stations, list) {
		res = rwnx_dbgfs_txq_sta(&buf[idx], size, rwnx_sta, rwnx_hw);
		idx += res;
		size -= res;
	}
#endif /* CONFIG_RWNX_FULLMAC */
	return idx;
}

static ssize_t rwnx_dbgfs_txq_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *rwnx_hw = file->private_data;
	struct rwnx_vif *vif;
	char *buf;
	int idx, res;
	ssize_t read;
	size_t bufsz =
		((NX_VIRT_DEV_MAX * (VIF_HDR_MAX_LEN + 2 * VIF_SEP_LEN)) + (NX_REMOTE_STA_MAX * STA_HDR_MAX_LEN) +
		 ((NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX + NX_NB_TXQ) * TXQ_HDR_MAX_LEN) + CAPTION_LEN);

	/* everything is read in one go */
	if (*ppos)
		return 0;

	bufsz = min_t(size_t, bufsz, count);
	buf = kmalloc(bufsz, GFP_ATOMIC);
	if (buf == NULL)
		return 0;

	bufsz--;
	idx = 0;

	res = scnprintf(&buf[idx], bufsz, CAPTION);
	idx += res;
	bufsz -= res;

	spin_lock_bh(&rwnx_hw->tx_lock);
	list_for_each_entry(vif, &rwnx_hw->vifs, list) {
		res = scnprintf(&buf[idx], bufsz, "\n" VIF_SEP);
		idx += res;
		bufsz -= res;
		res = rwnx_dbgfs_txq_vif(&buf[idx], bufsz, vif, rwnx_hw);
		idx += res;
		bufsz -= res;
		res = scnprintf(&buf[idx], bufsz, VIF_SEP);
		idx += res;
		bufsz -= res;
	}
	spin_unlock_bh(&rwnx_hw->tx_lock);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, idx);
	kfree(buf);

	return read;
}
DEBUGFS_READ_FILE_OPS(txq);

#define BES_DBGFS_ACSINFO_BUFSIZE ((SCAN_CHANNEL_MAX + 1) * 43)
static ssize_t rwnx_dbgfs_acsinfo_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
#ifdef CONFIG_RWNX_SOFTMAC
	struct wiphy *wiphy = priv->hw->wiphy;
#else  // CONFIG_RWNX_SOFTMAC
	struct wiphy *wiphy = priv->wiphy;
#endif // CONFIG_RWNX_SOFTMAC
	int survey_cnt = 0;
	int len = 0;
	int band, chan_cnt;
	char *buf = (char *)kmalloc(BES_DBGFS_ACSINFO_BUFSIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&priv->dbgdump.mutex);

	len += scnprintf(buf, min_t(size_t, BES_DBGFS_ACSINFO_BUFSIZE - 1, count), "FREQ    TIME(ms)    BUSY(ms)    NOISE(dBm)\n");

	for (band = NL80211_BAND_2GHZ; band <= NL80211_BAND_5GHZ; band++) {
		for (chan_cnt = 0; chan_cnt < wiphy->bands[band]->n_channels; chan_cnt++) {
			struct rwnx_survey_info *p_survey_info = &priv->survey[survey_cnt];
			struct ieee80211_channel *p_chan = &wiphy->bands[band]->channels[chan_cnt];

			if (p_survey_info->filled) {
				len += scnprintf(&buf[len], min_t(size_t, BES_DBGFS_ACSINFO_BUFSIZE - len - 1, count),
						 "%d    %03d         %03d         %d\n", p_chan->center_freq,
						 p_survey_info->chan_time_ms, p_survey_info->chan_time_busy_ms,
						 p_survey_info->noise_dbm);
			} else {
				len += scnprintf(&buf[len], min_t(size_t, BES_DBGFS_ACSINFO_BUFSIZE - len - 1, count),
						 "%d    NOT AVAILABLE\n", p_chan->center_freq);
			}

			survey_cnt++;
		}
	}

	mutex_unlock(&priv->dbgdump.mutex);

	len = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);
	return len;
}

DEBUGFS_READ_FILE_OPS(acsinfo);

static ssize_t rwnx_dbgfs_pwr_status_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	int len = 0;
	char *buf = (char *)kmalloc(BES_DBGFS_PWR_STATUS_BUFSIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&priv->dbgdump.mutex);
	len = bes_pwr_event_dump(priv, buf, BES_DBGFS_PWR_STATUS_BUFSIZE, count);
	mutex_unlock(&priv->dbgdump.mutex);

	len = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	kfree(buf);
	return len;
}

DEBUGFS_READ_FILE_OPS(pwr_status);

static ssize_t rwnx_dbgfs_fw_dbg_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	char help[] = "usage: [MOD:<ALL|KE|DBG|IPC|DMA|MM|TX|RX|PHY>]* "
		      "[DBG:<NONE|CRT|ERR|WRN|INF|VRB>]\n";

	return simple_read_from_buffer(user_buf, count, ppos, help, sizeof(help));
}

static ssize_t rwnx_dbgfs_fw_dbg_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	int idx = 0;
	u32 mod = 0;
	size_t len = min_t(size_t, count, sizeof(buf) - 1);

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';

#define RWNX_MOD_TOKEN(str, val)                                                                                       \
	if (strncmp(&buf[idx], str, sizeof(str) - 1) == 0) {                                                           \
		idx += sizeof(str) - 1;                                                                                \
		mod |= val;                                                                                            \
		continue;                                                                                              \
	}

#define BES_DBG_TOKEN(str, val)                                                                                       \
	if (strncmp(&buf[idx], str, sizeof(str) - 1) == 0) {                                                           \
		idx += sizeof(str) - 1;                                                                                \
		dbg = val;                                                                                             \
		goto dbg_done;                                                                                         \
	}

	while ((idx + 4) < len) {
		if (strncmp(&buf[idx], "MOD:", 4) == 0) {
			idx += 4;
			RWNX_MOD_TOKEN("ALL", 0xffffffff);
			RWNX_MOD_TOKEN("KE", BIT(0));
			RWNX_MOD_TOKEN("DBG", BIT(1));
			RWNX_MOD_TOKEN("IPC", BIT(2));
			RWNX_MOD_TOKEN("DMA", BIT(3));
			RWNX_MOD_TOKEN("MM", BIT(4));
			RWNX_MOD_TOKEN("TX", BIT(5));
			RWNX_MOD_TOKEN("RX", BIT(6));
			RWNX_MOD_TOKEN("PHY", BIT(7));
			idx++;
		} else if (strncmp(&buf[idx], "DBG:", 4) == 0) {
			u32 dbg = 0;
			idx += 4;
			BES_DBG_TOKEN("NONE", 0);
			BES_DBG_TOKEN("CRT", 1);
			BES_DBG_TOKEN("ERR", 2);
			BES_DBG_TOKEN("WRN", 3);
			BES_DBG_TOKEN("INF", 4);
			BES_DBG_TOKEN("VRB", 5);
			idx++;
			continue;
		dbg_done:
			rwnx_send_dbg_set_sev_filter_req(priv, dbg);
		} else {
			idx++;
		}
	}

	if (mod) {
		rwnx_send_dbg_set_mod_filter_req(priv, mod);
	}

	return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(fw_dbg);

static ssize_t rwnx_dbgfs_sys_stats_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[3 * 64];
	int len = 0;
	ssize_t read;
	int error = 0;
	struct dbg_get_sys_stat_cfm cfm;
	u32 sleep_int, sleep_frac, doze_int, doze_frac;

	bes_dbg(BES_DBG_DEBUGFS, RWNX_FN_ENTRY_STR);

	/* Get the information from the FW */
	if ((error = rwnx_send_dbg_get_sys_stat_req(priv, &cfm)))
		return error;

	if (cfm.stats_time == 0)
		return 0;

	sleep_int = ((cfm.cpu_sleep_time * 100) / cfm.stats_time);
	sleep_frac = (((cfm.cpu_sleep_time * 100) % cfm.stats_time) * 10) / cfm.stats_time;
	doze_int = ((cfm.doze_time * 100) / cfm.stats_time);
	doze_frac = (((cfm.doze_time * 100) % cfm.stats_time) * 10) / cfm.stats_time;

	len += scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count), "\nSystem statistics:\n");
	len += scnprintf(&buf[len], min_t(size_t, sizeof(buf) - 1, count), "  CPU sleep [%%]: %d.%d\n", sleep_int,
			 sleep_frac);
	len += scnprintf(&buf[len], min_t(size_t, sizeof(buf) - 1, count), "  Doze      [%%]: %d.%d\n", doze_int,
			 doze_frac);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	return read;
}

DEBUGFS_READ_FILE_OPS(sys_stats);

#ifdef CONFIG_RWNX_MUMIMO_TX
static ssize_t rwnx_dbgfs_mu_group_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *rwnx_hw = file->private_data;
	struct rwnx_mu_info *mu = &rwnx_hw->mu;
	struct rwnx_mu_group *group;
	size_t bufsz = NX_MU_GROUP_MAX * sizeof("xx = (xx - xx - xx - xx)\n") + 50;
	char *buf;
	int j, res, idx = 0;

	if (*ppos)
		return 0;

	buf = kmalloc(bufsz, GFP_ATOMIC);
	if (buf == NULL)
		return 0;

	res = scnprintf(&buf[idx], bufsz, "MU Group list (%d groups, %d users max)\n", NX_MU_GROUP_MAX,
			CONFIG_USER_MAX);
	idx += res;
	bufsz -= res;

	list_for_each_entry(group, &mu->active_groups, list) {
		if (group->user_cnt) {
			res = scnprintf(&buf[idx], bufsz, "%2d = (", group->group_id);
			idx += res;
			bufsz -= res;
			for (j = 0; j < (CONFIG_USER_MAX - 1); j++) {
				if (group->users[j])
					res = scnprintf(&buf[idx], bufsz, "%2d - ", group->users[j]->sta_idx);
				else
					res = scnprintf(&buf[idx], bufsz, ".. - ");

				idx += res;
				bufsz -= res;
			}

			if (group->users[j])
				res = scnprintf(&buf[idx], bufsz, "%2d)\n", group->users[j]->sta_idx);
			else
				res = scnprintf(&buf[idx], bufsz, "..)\n");

			idx += res;
			bufsz -= res;
		}
	}

	res = simple_read_from_buffer(user_buf, count, ppos, buf, idx);
	kfree(buf);

	return res;
}

DEBUGFS_READ_FILE_OPS(mu_group);
#endif

#ifdef CONFIG_RWNX_P2P_DEBUGFS
static ssize_t rwnx_dbgfs_oppps_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *rw_hw = file->private_data;
	struct rwnx_vif *rw_vif;
	char buf[32];
	size_t len = min_t(size_t, count, sizeof(buf) - 1);
	int ctw;

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';

	/* Read the written CT Window (provided in ms) value */
	if (sscanf(buf, "ctw=%d", &ctw) > 0) {
		/* Check if at least one VIF is configured as P2P GO */
		list_for_each_entry(rw_vif, &rw_hw->vifs, list) {
#ifdef CONFIG_RWNX_SOFTMAC
			if ((RWNX_VIF_TYPE(rw_vif) == NL80211_IFTYPE_AP) && rw_vif->vif->p2p) {
#else  /* CONFIG_RWNX_FULLMAC */
			if (RWNX_VIF_TYPE(rw_vif) == NL80211_IFTYPE_P2P_GO) {
#endif /* CONFIG_RWNX_SOFTMAC */
				struct mm_set_p2p_oppps_cfm cfm;

				/* Forward request to the embedded and wait for confirmation */
				rwnx_send_p2p_oppps_req(rw_hw, rw_vif, (u8)ctw, &cfm);

				break;
			}
		}
	}

	return count;
}

DEBUGFS_WRITE_FILE_OPS(oppps);

static ssize_t rwnx_dbgfs_noa_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *rw_hw = file->private_data;
	struct rwnx_vif *rw_vif;
	char buf[64];
	size_t len = min_t(size_t, count, sizeof(buf) - 1);
	int noa_count, interval, duration, dyn_noa;

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';

	/* Read the written NOA information */
	if (sscanf(buf, "count=%d interval=%d duration=%d dyn=%d", &noa_count, &interval, &duration, &dyn_noa) > 0) {
		/* Check if at least one VIF is configured as P2P GO */
		list_for_each_entry(rw_vif, &rw_hw->vifs, list) {
#ifdef CONFIG_RWNX_SOFTMAC
			if ((RWNX_VIF_TYPE(rw_vif) == NL80211_IFTYPE_AP) && rw_vif->vif->p2p) {
#else  /* CONFIG_RWNX_FULLMAC */
			if (RWNX_VIF_TYPE(rw_vif) == NL80211_IFTYPE_P2P_GO) {
#endif /* CONFIG_RWNX_SOFTMAC */
				struct mm_set_p2p_noa_cfm cfm;

				/* Forward request to the embedded and wait for confirmation */
				rwnx_send_p2p_noa_req(rw_hw, rw_vif, noa_count, interval, duration, (dyn_noa > 0),
						      &cfm);

				break;
			}
		}
	}

	return count;
}

DEBUGFS_WRITE_FILE_OPS(noa);
#endif /* CONFIG_RWNX_P2P_DEBUGFS */

struct rwnx_dbgfs_fw_trace {
	struct rwnx_fw_trace_local_buf lbuf;
	struct rwnx_fw_trace *trace;
	struct rwnx_hw *rwnx_hw;
};

static int rwnx_dbgfs_fw_trace_open(struct inode *inode, struct file *file)
{
	struct rwnx_dbgfs_fw_trace *ltrace = kmalloc(sizeof(*ltrace), GFP_KERNEL);
	struct rwnx_hw *priv = inode->i_private;

	if (!ltrace)
		return -ENOMEM;

	if (rwnx_fw_trace_alloc_local(&ltrace->lbuf, 5120)) {
		kfree(ltrace);
		return -ENOMEM;
	}

	ltrace->trace = &priv->debugfs.fw_trace;
	ltrace->rwnx_hw = priv;
	file->private_data = ltrace;
	return 0;
}

static int rwnx_dbgfs_fw_trace_release(struct inode *inode, struct file *file)
{
	struct rwnx_dbgfs_fw_trace *ltrace = file->private_data;

	if (ltrace) {
		rwnx_fw_trace_free_local(&ltrace->lbuf);
		kfree(ltrace);
	}

	return 0;
}

static ssize_t rwnx_dbgfs_fw_trace_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_dbgfs_fw_trace *ltrace = file->private_data;
	bool dont_wait = ((file->f_flags & O_NONBLOCK) || ltrace->rwnx_hw->debugfs.unregistering);

	return rwnx_fw_trace_read(ltrace->trace, &ltrace->lbuf, dont_wait, user_buf, count);
}

static ssize_t rwnx_dbgfs_fw_trace_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_dbgfs_fw_trace *ltrace = file->private_data;
	int ret;

	ret = _rwnx_fw_trace_reset(ltrace->trace, true);
	if (ret)
		return ret;

	return count;
}

DEBUGFS_READ_WRITE_OPEN_RELEASE_FILE_OPS(fw_trace);

static ssize_t rwnx_dbgfs_fw_trace_level_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	return rwnx_fw_trace_level_read(&priv->debugfs.fw_trace, user_buf, count, ppos);
}

static ssize_t rwnx_dbgfs_fw_trace_level_write(struct file *file, const char __user *user_buf, size_t count,
					       loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	return rwnx_fw_trace_level_write(&priv->debugfs.fw_trace, user_buf, count);
}
DEBUGFS_READ_WRITE_FILE_OPS(fw_trace_level);

#ifdef CONFIG_RWNX_RADAR
static ssize_t rwnx_dbgfs_pulses_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos, int rd_idx)
{
	struct rwnx_hw *priv = file->private_data;
	char *buf;
	int len = 0;
	int bufsz;
	int i;
	int index;
	struct rwnx_radar_pulses *p = &priv->radar.pulses[rd_idx];
	ssize_t read;

	if (*ppos != 0)
		return 0;

	/* Prevent from interrupt preemption */
	spin_lock_bh(&priv->radar.lock);
	bufsz = p->count * 34 + 51;
	bufsz += rwnx_radar_dump_pattern_detector(NULL, 0, &priv->radar, rd_idx);
	buf = kmalloc(bufsz, GFP_ATOMIC);
	if (buf == NULL) {
		spin_unlock_bh(&priv->radar.lock);
		return 0;
	}

	if (p->count) {
		len += scnprintf(&buf[len], bufsz - len, " PRI     WIDTH     FOM     FREQ\n");
		index = p->index;
		for (i = 0; i < p->count; i++) {
			struct radar_pulse *pulse;

			if (index > 0)
				index--;
			else
				index = RWNX_RADAR_PULSE_MAX - 1;

			pulse = (struct radar_pulse *)&p->buffer[index];

			len += scnprintf(&buf[len], bufsz - len, "%05dus  %03dus     %2d%%    %+3dMHz\n", pulse->rep,
					 2 * pulse->len, 6 * pulse->fom, 2 * pulse->freq);
		}
	}

	len += rwnx_radar_dump_pattern_detector(&buf[len], bufsz - len, &priv->radar, rd_idx);

	spin_unlock_bh(&priv->radar.lock);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	kfree(buf);

	return read;
}

static ssize_t rwnx_dbgfs_pulses_prim_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	return rwnx_dbgfs_pulses_read(file, user_buf, count, ppos, 0);
}

DEBUGFS_READ_FILE_OPS(pulses_prim);

static ssize_t rwnx_dbgfs_pulses_sec_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	return rwnx_dbgfs_pulses_read(file, user_buf, count, ppos, 1);
}

DEBUGFS_READ_FILE_OPS(pulses_sec);

static ssize_t rwnx_dbgfs_detected_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char *buf;
	int bufsz, len = 0;
	ssize_t read;

	if (*ppos != 0)
		return 0;

	bufsz = 5; // RIU:\n
	bufsz += rwnx_radar_dump_radar_detected(NULL, 0, &priv->radar, RWNX_RADAR_RIU);

	if (priv->phy.cnt > 1) {
		bufsz += 5; // FCU:\n
		bufsz += rwnx_radar_dump_radar_detected(NULL, 0, &priv->radar, RWNX_RADAR_FCU);
	}

	buf = kmalloc(bufsz, GFP_KERNEL);
	if (buf == NULL) {
		return 0;
	}

	len = scnprintf(&buf[len], bufsz, "RIU:\n");
	len += rwnx_radar_dump_radar_detected(&buf[len], bufsz - len, &priv->radar, RWNX_RADAR_RIU);

	if (priv->phy.cnt > 1) {
		len += scnprintf(&buf[len], bufsz - len, "FCU:\n");
		len += rwnx_radar_dump_radar_detected(&buf[len], bufsz - len, &priv->radar, RWNX_RADAR_FCU);
	}

	read = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	kfree(buf);

	return read;
}

DEBUGFS_READ_FILE_OPS(detected);

static ssize_t rwnx_dbgfs_enable_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	int ret;
	ssize_t read;

	spin_lock_bh(&priv->radar.lock);
	ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count), "RIU=%d FCU=%d\n",
			priv->radar.dpd[RWNX_RADAR_RIU]->enabled, priv->radar.dpd[RWNX_RADAR_FCU]->enabled);
	spin_unlock_bh(&priv->radar.lock);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	return read;
}

static ssize_t rwnx_dbgfs_enable_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	int val;
	size_t len = min_t(size_t, count, sizeof(buf) - 1);

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';

	if (sscanf(buf, "RIU=%d", &val) > 0)
		rwnx_radar_detection_enable(&priv->radar, val, RWNX_RADAR_RIU);

	if (sscanf(buf, "FCU=%d", &val) > 0)
		rwnx_radar_detection_enable(&priv->radar, val, RWNX_RADAR_FCU);

	return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(enable);

static ssize_t rwnx_dbgfs_band_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	int ret;
	ssize_t read;

	ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count), "BAND=%d\n", priv->phy.sec_chan.band);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	return read;
}

static ssize_t rwnx_dbgfs_band_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	int val;
	size_t len = min_t(size_t, count, sizeof(buf) - 1);

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';

	if ((sscanf(buf, "%d", &val) > 0) && (val >= 0) && (val <= NL80211_BAND_5GHZ))
		priv->phy.sec_chan.band = val;

	return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(band);

static ssize_t rwnx_dbgfs_type_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	int ret;
	ssize_t read;

	ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count), "TYPE=%d\n", priv->phy.sec_chan.type);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	return read;
}

static ssize_t rwnx_dbgfs_type_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	int val;
	size_t len = min_t(size_t, count, sizeof(buf) - 1);

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';

	if ((sscanf(buf, "%d", &val) > 0) && (val >= PHY_CHNL_BW_20) && (val <= PHY_CHNL_BW_80P80))
		priv->phy.sec_chan.type = val;

	return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(type);

static ssize_t rwnx_dbgfs_prim20_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	int ret;
	ssize_t read;

	ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count), "PRIM20=%dMHz\n", priv->phy.sec_chan.prim20_freq);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	return read;
}

static ssize_t rwnx_dbgfs_prim20_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	int val;
	size_t len = min_t(size_t, count, sizeof(buf) - 1);

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';

	if (sscanf(buf, "%d", &val) > 0)
		priv->phy.sec_chan.prim20_freq = val;

	return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(prim20);

static ssize_t rwnx_dbgfs_center1_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	int ret;
	ssize_t read;

	ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count), "CENTER1=%dMHz\n", priv->phy.sec_chan.center1_freq);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	return read;
}

static ssize_t rwnx_dbgfs_center1_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	int val;
	size_t len = min_t(size_t, count, sizeof(buf) - 1);

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';

	if (sscanf(buf, "%d", &val) > 0)
		priv->phy.sec_chan.center1_freq = val;

	return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(center1);

static ssize_t rwnx_dbgfs_center2_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	int ret;
	ssize_t read;

	ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count), "CENTER2=%dMHz\n", priv->phy.sec_chan.center2_freq);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	return read;
}

static ssize_t rwnx_dbgfs_center2_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[32];
	int val;
	size_t len = min_t(size_t, count, sizeof(buf) - 1);

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	buf[len] = '\0';

	if (sscanf(buf, "%d", &val) > 0)
		priv->phy.sec_chan.center2_freq = val;

	return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(center2);

static ssize_t rwnx_dbgfs_set_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	return 0;
}

static ssize_t rwnx_dbgfs_set_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;

	rwnx_send_set_channel(priv, 1, NULL);
	rwnx_radar_detection_enable(&priv->radar, RWNX_RADAR_DETECT_ENABLE, RWNX_RADAR_FCU);

	return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(set);
#endif /* CONFIG_RWNX_RADAR */

#ifdef CONFIG_RWNX_FULLMAC

#define LINE_MAX_SZ 170

struct st {
	char line[LINE_MAX_SZ + 1];
	unsigned int r_idx;
};

static int compare_idx(const void *st1, const void *st2)
{
	int index1 = ((struct st *)st1)->r_idx;
	int index2 = ((struct st *)st2)->r_idx;

	if (index1 > index2)
		return 1;
	if (index1 < index2)
		return -1;

	return 0;
}

static struct rwnx_sta *rwnx_dbgfs_get_sta(struct rwnx_hw *rwnx_hw, char *mac_addr)
{
	u8 mac[6];

	if (sscanf(mac_addr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) !=
	    6)
		return NULL;
	return rwnx_get_sta(rwnx_hw, mac);
}

static ssize_t rwnx_dbgfs_twt_request_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[750];
	ssize_t read;
	struct rwnx_hw *priv = file->private_data;
	struct rwnx_sta *sta = NULL;
	int len;

	/* Get the station index from MAC address */
	sta = rwnx_dbgfs_get_sta(priv, file->f_path.dentry->d_parent->d_parent->d_iname);
	if (sta == NULL)
		return -EINVAL;
	if (sta->twt_ind.sta_idx != RWNX_INVALID_STA) {
		struct twt_conf_tag *conf = &sta->twt_ind.conf;
		if (sta->twt_ind.resp_type == MAC_TWT_SETUP_ACCEPT)
			len = scnprintf(buf, sizeof(buf) - 1, "Accepted configuration");
		else if (sta->twt_ind.resp_type == MAC_TWT_SETUP_ALTERNATE)
			len = scnprintf(buf, sizeof(buf) - 1, "Alternate configuration proposed by AP");
		else if (sta->twt_ind.resp_type == MAC_TWT_SETUP_DICTATE)
			len = scnprintf(buf, sizeof(buf) - 1, "AP dictates the following configuration");
		else if (sta->twt_ind.resp_type == MAC_TWT_SETUP_REJECT)
			len = scnprintf(buf, sizeof(buf) - 1, "AP rejects the following configuration");
		else {
			len = scnprintf(buf, sizeof(buf) - 1, "Invalid response from the peer");
			goto end;
		}
		len += scnprintf(&buf[len], sizeof(buf) - 1 - len,
				 ":\n"
				 "flow_type = %d\n"
				 "wake interval mantissa = %d\n"
				 "wake interval exponent = %d\n"
				 "wake interval = %d us\n"
				 "nominal minimum wake duration = %d us\n",
				 conf->flow_type, conf->wake_int_mantissa, conf->wake_int_exp,
				 conf->wake_int_mantissa << conf->wake_int_exp,
				 conf->wake_dur_unit ? conf->min_twt_wake_dur * 1024 : conf->min_twt_wake_dur * 256);
	} else {
		len = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
				"setup_command = <0: request, 1: suggest, 2: demand>,"
				"flow_type = <0: announced, 1: unannounced>,"
				"wake_interval_mantissa = <0 if setup request and no constraints>,"
				"wake_interval_exp = <0 if setup request and no constraints>,"
				"nominal_min_wake_dur = <0 if setup request and no constraints>,"
				"wake_dur_unit = <0: 256us, 1: tu>");
	}
end:
	read = simple_read_from_buffer(user_buf, count, ppos, buf, len);
	return read;
}

#define BES_DBGFS_TWT_REQ_BUFSIZE 1024
static ssize_t rwnx_dbgfs_twt_request_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	char *accepted_params[] = {"setup_command",
				   "flow_type",
				   "flow_id",
				   "wake_interval_mantissa",
				   "wake_interval_exp",
				   "nominal_min_wake_dur",
				   "wake_dur_unit",
				   0};
	struct twt_conf_tag twt_conf;
	struct twt_setup_cfm twt_setup_cfm;
	struct rwnx_sta *sta = NULL;
	struct rwnx_hw *priv = file->private_data;
	char param[30];
	char *buf = NULL, *line = NULL;
	int error = 1, i, val, setup_command = -1;
	bool_l found;
	size_t len = BES_DBGFS_TWT_REQ_BUFSIZE - 1;
	uint8_t flow_id = 0;

	bes_dbg(BES_DBG_DEBUGFS, RWNX_FN_ENTRY_STR);
	/* Get the station index from MAC address */
	sta = rwnx_dbgfs_get_sta(priv, file->f_path.dentry->d_parent->d_parent->d_iname);
	if (sta == NULL)
		return -EINVAL;

	buf = (char *)kmalloc(BES_DBGFS_TWT_REQ_BUFSIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* Get the content of the file */
	if (copy_from_user(buf, user_buf, len)) {
		kfree(buf);
		return -EFAULT;
	}

	buf[len] = '\0';
	memset(&twt_conf, 0, sizeof(twt_conf));

	line = buf;
	/* Get the content of the file */
	while (line != NULL) {
		if (sscanf(line, "%s = %d", param, &val) == 2) {
			i = 0;
			found = false;
			// Check if parameter is valid
			while (accepted_params[i]) {
				if (strcmp(accepted_params[i], param) == 0) {
					found = true;
					break;
				}
				i++;
			}

			if (!found) {
				dev_err(priv->dev, "%s: parameter %s is not valid\n", __func__, param);
				kfree(buf);
				return -EINVAL;
			}

			if (!strcmp(param, "setup_command")) {
				setup_command = val;
			} else if (!strcmp(param, "flow_type")) {
				twt_conf.flow_type = val;
			} else if (!strcmp(param, "flow_id")) {
				flow_id = val;
			} else if (!strcmp(param, "wake_interval_mantissa")) {
				twt_conf.wake_int_mantissa = val;
			} else if (!strcmp(param, "wake_interval_exp")) {
				twt_conf.wake_int_exp = val;
			} else if (!strcmp(param, "nominal_min_wake_dur")) {
				twt_conf.min_twt_wake_dur = val;
			} else if (!strcmp(param, "wake_dur_unit")) {
				twt_conf.wake_dur_unit = val;
			}
		} else {
			dev_err(priv->dev, "%s: Impossible to read TWT configuration option\n", __func__);
			kfree(buf);
			return -EFAULT;
		}
		line = strchr(line, ',');
		if (line == NULL)
			break;
		line++;
	}

	kfree(buf);
	buf = NULL;
	if (setup_command == -1) {
		dev_err(priv->dev, "%s: TWT missing setup command\n", __func__);
		return -EFAULT;
	}

	// Forward the request to the LMAC
	if ((error = rwnx_send_twt_request(priv, setup_command, sta->vif_idx, flow_id, &twt_conf, &twt_setup_cfm)) != 0)
		return error;

	// Check the status
	if (twt_setup_cfm.status != CO_OK)
		return -EIO;

	return count;
}
DEBUGFS_READ_WRITE_FILE_OPS(twt_request);

static ssize_t rwnx_dbgfs_twt_teardown_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[512];
	int ret;
	ssize_t read;

	ret = scnprintf(buf, min_t(size_t, sizeof(buf) - 1, count),
			"TWT teardown format:\n\n"
			"flow_id = <ID>\n");
	read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	return read;
}

static ssize_t rwnx_dbgfs_twt_teardown_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct twt_teardown_req twt_teardown;
	struct twt_teardown_cfm twt_teardown_cfm;
	struct rwnx_sta *sta = NULL;
	struct rwnx_hw *priv = file->private_data;
	char buf[256];
	char *line;
	int error = 1;
	size_t len = min_t(size_t, count, sizeof(buf) - 1);

	bes_dbg(BES_DBG_DEBUGFS, RWNX_FN_ENTRY_STR);
	/* Get the station index from MAC address */
	sta = rwnx_dbgfs_get_sta(priv, file->f_path.dentry->d_parent->d_parent->d_iname);
	if (sta == NULL)
		return -EINVAL;

	/* Get the content of the file */
	if (copy_from_user(buf, user_buf, len))
		return -EINVAL;

	buf[len] = '\0';
	memset(&twt_teardown, 0, sizeof(twt_teardown));

	/* Get the content of the file */
	line = buf;

	if (sscanf(line, "flow_id = %d", (int *)&twt_teardown.id) != 1) {
		dev_err(priv->dev, "%s: Invalid TWT configuration\n", __func__);
		return -EINVAL;
	}

	twt_teardown.neg_type = 0;
	twt_teardown.all_twt = 0;
	twt_teardown.vif_idx = sta->vif_idx;

	// Forward the request to the LMAC
	if ((error = rwnx_send_twt_teardown(priv, &twt_teardown, &twt_teardown_cfm)) != 0)
		return error;

	// Check the status
	if (twt_teardown_cfm.status != CO_OK)
		return -EIO;

	return count;
}
DEBUGFS_READ_WRITE_FILE_OPS(twt_teardown);

static ssize_t rwnx_dbgfs_rc_stats_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_sta *sta = NULL;
	struct rwnx_hw *priv = file->private_data;
	char *buf;
	int bufsz, len = 0;
	ssize_t read;
	int i = 0;
	int error = 0;
	struct me_rc_stats_cfm me_rc_stats_cfm;
	unsigned int no_samples;
	struct st *st;

	bes_dbg(BES_DBG_DEBUGFS, RWNX_FN_ENTRY_STR);

	/* everything should fit in one call */
	if (*ppos)
		return 0;

	/* Get the station index from MAC address */
	sta = rwnx_dbgfs_get_sta(priv, file->f_path.dentry->d_parent->d_parent->d_iname);
	if (sta == NULL)
		return -EINVAL;

	/* Forward the information to the LMAC */
	if ((error = rwnx_send_me_rc_stats(priv, sta->sta_idx, &me_rc_stats_cfm)))
		return error;

	no_samples = me_rc_stats_cfm.no_samples;
	if (no_samples == 0)
		return 0;

	bufsz = no_samples * LINE_MAX_SZ + 500;

	buf = kmalloc(bufsz + 1, GFP_ATOMIC);
	if (buf == NULL)
		return 0;

	st = kmalloc(sizeof(struct st) * no_samples, GFP_ATOMIC);
	if (st == NULL) {
		kfree(buf);
		return 0;
	}

	for (i = 0; i < no_samples; i++) {
		unsigned int tp, eprob;
		len = bes_vif_info_print_rate_from_cfg(st[i].line, LINE_MAX_SZ, me_rc_stats_cfm.rate_stats[i].rate_config,
					  &st[i].r_idx, 0);

		if (me_rc_stats_cfm.sw_retry_step != 0) {
			len += scnprintf(&st[i].line[len], LINE_MAX_SZ - len, "%c",
					 me_rc_stats_cfm.retry_step_idx[me_rc_stats_cfm.sw_retry_step] == i ? '*'
													    : ' ');
		} else {
			len += scnprintf(&st[i].line[len], LINE_MAX_SZ - len, " ");
		}
		len += scnprintf(&st[i].line[len], LINE_MAX_SZ - len, "%c",
				 me_rc_stats_cfm.retry_step_idx[0] == i ? 'T' : ' ');
		len += scnprintf(&st[i].line[len], LINE_MAX_SZ - len, "%c",
				 me_rc_stats_cfm.retry_step_idx[1] == i ? 't' : ' ');
		len += scnprintf(&st[i].line[len], LINE_MAX_SZ - len, "%c ",
				 me_rc_stats_cfm.retry_step_idx[2] == i ? 'P' : ' ');

		tp = me_rc_stats_cfm.tp[i] / 10;
		len += scnprintf(&st[i].line[len], LINE_MAX_SZ - len, " %4u.%1u", tp / 10, tp % 10);

		eprob = ((me_rc_stats_cfm.rate_stats[i].probability * 1000) >> 16) + 1;
		len += scnprintf(&st[i].line[len], LINE_MAX_SZ - len, "  %4u.%1u %5u(%6u)  %6u", eprob / 10, eprob % 10,
				 me_rc_stats_cfm.rate_stats[i].success, me_rc_stats_cfm.rate_stats[i].attempts,
				 me_rc_stats_cfm.rate_stats[i].sample_skipped);
	}
	len = scnprintf(buf, bufsz, "\nTX rate info for %02X:xx:xx:xx:%02X:%02X:\n", sta->mac_addr[0],
						 sta->mac_addr[4], sta->mac_addr[5]);

	len += scnprintf(&buf[len], bufsz - len,
			 "   # type               rate             tpt   eprob    ok(   tot)   skipped\n");

	// add sorted statistics to the buffer
	sort(st, no_samples, sizeof(st[0]), compare_idx, NULL);
	for (i = 0; i < no_samples; i++) {
		len += scnprintf(&buf[len], bufsz - len, "%s\n", st[i].line);
	}

	// display HE TB statistics if any
	if (me_rc_stats_cfm.rate_stats[RC_HE_STATS_IDX].rate_config != 0) {
		unsigned int tp, eprob;
		struct rc_rate_stats *rate_stats = &me_rc_stats_cfm.rate_stats[RC_HE_STATS_IDX];
		int ru_index = rate_stats->ru_and_length & 0x07;
		int ul_length = rate_stats->ru_and_length >> 3;

		len += scnprintf(&buf[len], bufsz - len, "\nHE TB rate info:\n");

		len += scnprintf(
			&buf[len], bufsz - len,
			"     type               rate             tpt   eprob    ok(   tot)   ul_length\n     ");
		len += bes_vif_info_print_rate_from_cfg(&buf[len], bufsz - len, rate_stats->rate_config, NULL, ru_index);

		tp = me_rc_stats_cfm.tp[RC_HE_STATS_IDX] / 10;
		len += scnprintf(&buf[len], bufsz - len, "      %4u.%1u", tp / 10, tp % 10);

		eprob = ((rate_stats->probability * 1000) >> 16) + 1;
		len += scnprintf(&buf[len], bufsz - len, "  %4u.%1u %5u(%6u)  %6u\n", eprob / 10, eprob % 10,
				 rate_stats->success, rate_stats->attempts, ul_length);
	}

	len += scnprintf(&buf[len], bufsz - len, "\n MPDUs AMPDUs AvLen trialP");
	len += scnprintf(&buf[len], bufsz - len, "\n%6u %6u %3d.%1d %6u\n", me_rc_stats_cfm.ampdu_len,
			 me_rc_stats_cfm.ampdu_packets, me_rc_stats_cfm.avg_ampdu_len >> 16,
			 ((me_rc_stats_cfm.avg_ampdu_len * 10) >> 16) % 10, me_rc_stats_cfm.sample_wait);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	kfree(buf);
	kfree(st);

	return read;
}

DEBUGFS_READ_FILE_OPS(rc_stats);

static ssize_t rwnx_dbgfs_rc_fixed_rate_idx_write(struct file *file, const char __user *user_buf, size_t count,
						  loff_t *ppos)
{
	struct rwnx_sta *sta = NULL;
	struct rwnx_hw *priv = file->private_data;
	char buf[10];
	int fixed_rate_idx = -1;
	union rwnx_rate_ctrl_info rate_config;
	int error = 0;
	size_t len = min_t(size_t, count, sizeof(buf) - 1);

	bes_dbg(BES_DBG_DEBUGFS, RWNX_FN_ENTRY_STR);

	/* Get the station index from MAC address */
	sta = rwnx_dbgfs_get_sta(priv, file->f_path.dentry->d_parent->d_parent->d_iname);
	if (sta == NULL)
		return -EINVAL;

	/* Get the content of the file */
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';
	if (sscanf(buf, "%i\n", &fixed_rate_idx) != 1) {
		bes_err(BES_DBG_DEBUGFS, "fixed_rate_idx parse failed\n");
		return -EINVAL;
	}

	/* Convert rate index into rate configuration */
	if ((fixed_rate_idx < 0) || (fixed_rate_idx >= (N_CCK + N_OFDM + N_HT + N_VHT + N_HE_SU + N_HE_MU + N_HE_ER))) {
		// disable fixed rate
		rate_config.value = (u32)-1;
	} else {
		idx_to_rate_cfg(fixed_rate_idx, &rate_config, NULL);
	}

	// Forward the request to the LMAC
	if ((error = rwnx_send_me_rc_set_rate(priv, sta->sta_idx, (u16)rate_config.value)) != 0) {
		return error;
	}

	priv->debugfs.rc_config[sta->sta_idx] = (int)rate_config.value;
	return len;
}

DEBUGFS_WRITE_FILE_OPS(rc_fixed_rate_idx);


static ssize_t rwnx_dbgfs_obss_bw_ru_filter_write(struct file *file, const char __user *user_buf, size_t count,
						  loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char buf[10];
	u32 obss_bw_ru_filter;
	int error = 0;
	size_t len = min_t(size_t, count, sizeof(buf) - 1);
	struct mm_hw_filter hw_filter;

	bes_dbg(BES_DBG_DEBUGFS, RWNX_FN_ENTRY_STR);

	/* Get the content of the file */
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';
	if (sscanf(buf, "%i\n", &obss_bw_ru_filter) != 1) {
		bes_err(BES_DBG_COEX, "BES_DBG_DEBUGFS parse failed\n");
		return -EINVAL;
	}

	// Forward the request to the LMAC
	if ((error = bes_filter_get_hw_filter(priv, &hw_filter)) != 0) {
		return error;
	}
	if (!!obss_bw_ru_filter) {
		hw_filter.umac_filter |= NXMAC_ACCEPT_ALL_BEACON_BIT;
	} else {
		hw_filter.umac_filter &= (~NXMAC_ACCEPT_ALL_BEACON_BIT);
	}

	/* Sync umac filter to device */
	if ((error = bes_filter_set_hw_filter(priv, &hw_filter)) != 0) {
		return error;
	}

	spin_lock(&priv->obss_narrow.lock);
	priv->obss_narrow.acpt_all_beacon = !!obss_bw_ru_filter;
	spin_unlock(&priv->obss_narrow.lock);

	return len;
}

DEBUGFS_WRITE_FILE_OPS(obss_bw_ru_filter);

static ssize_t rwnx_dbgfs_last_rx_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	ssize_t read;
	struct rwnx_sta *sta = NULL;
	struct rwnx_hw *priv = file->private_data;
	int bufsz = 0, len = 0;
	char *buf = NULL;
	int rate_sort[TXRX_RATE_STATS_NUM] = {-1};
	const int hist_len = bes_vif_info_get_hist_len(NULL);

	bes_dbg(BES_DBG_DEBUGFS, RWNX_FN_ENTRY_STR);

	/* everything should fit in one call */
	if (*ppos)
		return 0;

	/* Get the station index from MAC address */
	sta = rwnx_dbgfs_get_sta(priv, file->f_path.dentry->d_parent->d_parent->d_iname);
	if (sta == NULL)
		return -EINVAL;

	bufsz = ((TXRX_RATE_STATS_NUM + 1) * 50 + hist_len + 250);
	buf = kmalloc(bufsz + 1, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	len = bes_vif_info_rx_rate_dump(priv, sta, buf, bufsz + 1, rate_sort, TXRX_RATE_STATS_NUM);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	kfree(buf);

	return read;
}

static ssize_t rwnx_dbgfs_last_rx_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_sta *sta = NULL;
	struct rwnx_hw *priv = file->private_data;

	/* Get the station index from MAC address */
	sta = rwnx_dbgfs_get_sta(priv, file->f_path.dentry->d_parent->d_parent->d_iname);
	if (sta == NULL)
		return -EINVAL;

	/* Prevent from interrupt preemption as these statistics are updated under
	 * interrupt */
	spin_lock_bh(&priv->cb_lock);
	bes_sta_rx_rate_stat_reset(sta);
	spin_unlock_bh(&priv->cb_lock);

	return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(last_rx);

static ssize_t rwnx_dbgfs_link_info_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)rwnx_get_plat_data()->drvdata;
	struct rwnx_sta *rwnx_sta = file->private_data;
	struct rx_vector_1 *rx_vect1 = &rwnx_sta->stats.last_rx.rx_vect1;
	struct rx_vector_2 *rx_vect2 = &rwnx_sta->stats.last_rx.rx_vect2;
	struct lstats_time connected_ts = {0};
	u32 len = 0;
	u32 bufsz = 1024;
	u32 ul_delay = 0;
	u32 cca_busy_ratio = U32_MAX;
	u32 tx_delay = U32_MAX;
	ssize_t read = 0;
	char *buf = NULL;
	char band[][32] = {
		"2.4GHZ",
		"5GHZ",
		"INVALID VALUE",
	};

	char chan_width[][32] = {
		"20 MHz",
		"40 MHz",
		"80 MHz",
		"160 MHz",
		"80+80 MHz",
		"INVALID VALUE"
	};
	bes_dbg(BES_DBG_DEBUGFS, RWNX_FN_ENTRY_STR);

	/* everything should fit in one call */
	if (*ppos)
		return 0;
	buf = kmalloc(bufsz, GFP_ATOMIC);
	if (buf == NULL)
		return -ENOMEM;

	connected_ts = bes_lstats_ktime_to_tm(ktime_sub(ktime_get(), rwnx_sta->stats.connected_start_time));
	ul_delay = bes_tx_uldelay_get(rwnx_sta);
	bes_vif_info_get_chan_load(rwnx_hw, rwnx_sta->vif_idx, &cca_busy_ratio);
	bes_get_tx_delay_stat(rwnx_hw, rwnx_sta, &tx_delay);

	len = scnprintf(&buf[len], bufsz - len, "Station  %pM\n", rwnx_sta->mac_addr);
	len += scnprintf(&buf[len], bufsz - len, "    Association ID:  %u\n", rwnx_sta->aid);
	len += scnprintf(&buf[len], bufsz - len, "    RSSI: %d dBm, SNR: %u dB, Noise: %d dBm\n",
			 rx_vect1->rssi1, rx_vect2->rcpi1, rx_vect1->rssi1 - rx_vect2->rcpi1);
	len += scnprintf(&buf[len], bufsz - len, "    Inactive Time:  %d ms\n", jiffies_to_msecs(jiffies - rwnx_sta->stats.last_act));
	len += scnprintf(&buf[len], bufsz - len, "    Connected Time:  %uh:%um:%us\n", connected_ts.hour, connected_ts.min, connected_ts.sec);
	len += scnprintf(&buf[len], bufsz - len, "    Uplink Delay:  %u us\n", ul_delay);
	if (cca_busy_ratio != U32_MAX)
		len += scnprintf(&buf[len], bufsz - len, "    CCA Busy Ratio:  %d.%d%%\n", cca_busy_ratio / 10 , cca_busy_ratio % 10);
	if (tx_delay != U32_MAX)
		len += scnprintf(&buf[len], bufsz - len, "    Tx Delay:  %d us\n", tx_delay);
	len += scnprintf(&buf[len], bufsz - len, "    RX:  %llu bytes (%u packets)\n", rwnx_sta->stats.rx_bytes, rwnx_sta->stats.rx_pkts);
	len += scnprintf(&buf[len], bufsz - len, "    TX:  %llu bytes (%u packets, %u fail_packets %u continuous_fail_pkts) \n",
			 rwnx_sta->stats.tx_bytes, rwnx_sta->stats.tx_pkts, rwnx_sta->stats.tx_fail_stats.tx_fail_pkts,
			 rwnx_sta->stats.tx_cont_fail_pkts);
	len += scnprintf(&buf[len], bufsz - len, "    Tx Fail(Over Retry:%u packets  Life Timeout:%u packets)\n",
			 rwnx_sta->stats.tx_fail_stats.over_retry_pkts, rwnx_sta->stats.tx_fail_stats.life_timeout_pkts);
	len += scnprintf(&buf[len], bufsz - len, "    TX Dropped: %u packets\n", rwnx_sta->stats.drop_tx);
	len += scnprintf(&buf[len], bufsz - len, "    Freq:  %u MHz\n", rwnx_sta->center_freq);
	len += scnprintf(&buf[len], bufsz - len, "    Channel Index:  %u\n", rwnx_sta->ch_idx);
	len += scnprintf(&buf[len], bufsz - len, "    Band:  %s\n", band[rwnx_sta->band]);
	len += scnprintf(&buf[len], bufsz - len, "    Channel Width:  %s\n", chan_width[rwnx_sta->width]);
	len += scnprintf(&buf[len], bufsz - len, "    HT:  %d\n", rwnx_sta->ht);
	len += scnprintf(&buf[len], bufsz - len, "    VHT:  %d\n", rwnx_sta->vht);
	len += scnprintf(&buf[len], bufsz - len, "    HE:  %d\n", rwnx_sta->he);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, (size_t)len);
	kfree(buf);
	return read;
}
// rwnx_dbgfs_link_info_ops
DEBUGFS_READ_FILE_OPS(link_info);

static ssize_t rwnx_dbgfs_tx_rate_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	ssize_t read;
	struct rwnx_sta *sta = NULL;
	struct rwnx_hw *priv = file->private_data;
	int bufsz = 0, len = 0;
	char *buf = NULL;
	int rate_sort[TXRX_RATE_STATS_NUM] = {-1};
	const int hist_len = bes_vif_info_get_hist_len(NULL);

	bes_dbg(BES_DBG_DEBUGFS, RWNX_FN_ENTRY_STR);

	/* everything should fit in one call */
	if (*ppos)
		return 0;

	/* Get the station index from MAC address */
	sta = rwnx_dbgfs_get_sta(priv, file->f_path.dentry->d_parent->d_parent->d_iname);
	if (sta == NULL)
		return -EINVAL;

	bufsz = ((TXRX_RATE_STATS_NUM + 1) * (63 + 2) + hist_len + 150);
	buf = kmalloc(bufsz + 1, GFP_KERNEL);
	if (buf == NULL)
		return -ENOMEM;

	len = bes_vif_info_tx_rate_dump(priv, sta, buf, bufsz + 1, rate_sort, TXRX_RATE_STATS_NUM);

	read = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	kfree(buf);

	return read;
}

static ssize_t rwnx_dbgfs_tx_rate_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_sta *sta = NULL;
	struct rwnx_hw *priv = file->private_data;

	/* Get the station index from MAC address */
	sta = rwnx_dbgfs_get_sta(priv, file->f_path.dentry->d_parent->d_parent->d_iname);
	if (sta == NULL)
		return -EINVAL;

	/* Prevent from interrupt preemption as these statistics are updated under
	 * interrupt */
	spin_lock_bh(&priv->cb_lock);
	bes_sta_tx_rate_stat_reset(sta);
	spin_unlock_bh(&priv->cb_lock);

	return count;
}

DEBUGFS_READ_WRITE_FILE_OPS(tx_rate);

static ssize_t rwnx_dbgfs_flush_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_sta *sta = NULL;
	struct rwnx_hw *priv = file->private_data;

	/* Get the station index from MAC address */
	sta = rwnx_dbgfs_get_sta(priv, file->f_path.dentry->d_parent->d_iname);
	if (sta == NULL)
		return -EINVAL;

	/*
	 * Flush data packet in device.
	 * Using rtnl_lock avoids delete station when flush data
	 */
	rtnl_lock();
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0))
	wiphy_lock(priv->wiphy);
#endif
	bes_link_flush_data(priv, sta);
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0))
	wiphy_unlock(priv->wiphy);
#endif
	rtnl_unlock();

	return count;
}

DEBUGFS_WRITE_FILE_OPS(flush);

#endif /* CONFIG_RWNX_FULLMAC */

/*
 * trace helper
 */
#ifdef CONFIG_RWNX_FW_TRACE
void rwnx_fw_trace_dump(struct rwnx_hw *rwnx_hw)
{
	/* may be called before rwnx_dbgfs_register */
	if (rwnx_hw->plat->enabled && !rwnx_hw->debugfs.fw_trace.buf.data) {
		rwnx_fw_trace_buf_init(&rwnx_hw->debugfs.fw_trace.buf, rwnx_ipc_fw_trace_desc_get(rwnx_hw));
	}

	if (!rwnx_hw->debugfs.fw_trace.buf.data)
		return;

	_rwnx_fw_trace_dump(&rwnx_hw->debugfs.fw_trace.buf);
}

void rwnx_fw_trace_reset(struct rwnx_hw *rwnx_hw)
{
	_rwnx_fw_trace_reset(&rwnx_hw->debugfs.fw_trace, true);
}
#endif

#ifdef CONFIG_RWNX_FW_DUMP
void rwnx_dbgfs_trigger_fw_dump(struct rwnx_hw *rwnx_hw, char *reason)
{
	rwnx_send_dbg_trigger_req(rwnx_hw, reason);
}
#endif

static ssize_t rwnx_dbgfs_lmac_log_ctrl_read(struct file *file, char __user *user_buf,
					     size_t count, loff_t *ppos)
{
	struct rwnx_hw *rwnx_hw = file->private_data;
	static char *read_buf = NULL;
	static int str_len = 0;
	u32 bufsz = 10 * (1U << 10);
	ssize_t read = 0;
	int error = 0;

	/* Initialize on first call. */
	if (!*ppos) {
		/* When entering for the first time but not releasing the previous round of calls. */
		if (read_buf) {
			bes_err(BES_DBG_LMAC_LOG_FILTER, "%s read_buf in use\n", __func__);
			return -EBUSY;
		}

		/* Request Read Buffer Memory. */
		if (!(read_buf = kmalloc(bufsz, GFP_KERNEL))) {
			bes_err(BES_DBG_LMAC_LOG_FILTER, "%s kmalloc read_buf fail\n", __func__);
			return -ENOMEM;
		}

		/* Fill buffer */
		str_len = lmac_log_filter_show(rwnx_hw, read_buf, bufsz);
		if (str_len <= 0) {
			error = -EFAULT;
			goto free;
		}
	}

	/* On the last call, release the buffer. */
	if (*ppos >= str_len) {
		error = 0;
		goto free;
	}

	/* Size per read. */
	if (count > str_len - *ppos)
		read = str_len - *ppos;
	else
		read = count;

	bes_dbg(BES_DBG_LMAC_LOG_FILTER, "%s str_len:%d count:%zu *ppos:%lld read:%zd\n",
		 __func__, str_len, count, *ppos, read);

	if (copy_to_user(user_buf, read_buf + *ppos, read))
		return -EFAULT;

	/* Record the position of each read. */
	*ppos += read;

	return read;

free:
	kfree(read_buf);
	read_buf = NULL;
	str_len = 0;
	return error;
}

static ssize_t rwnx_dbgfs_lmac_log_ctrl_write(struct file *file, const char __user *user_buf,
					      size_t count, loff_t *ppos)
{
	struct rwnx_hw *rwnx_hw = file->private_data;

	return lmac_log_filter_set(rwnx_hw, user_buf, count, ppos);
}

/* rwnx_dbgfs_lmac_log_ctrl_ops */
DEBUGFS_READ_WRITE_FILE_OPS(lmac_log_ctrl);

#ifdef CONFIG_BES_COEX_SUPPORT
static u32 rwnx_gpio_num_to_io_idx(u32 gpio)
{
	if (gpio <= 7)
		return gpio;
	else if (gpio <= 17)
		return gpio - 2;
	else if (gpio <= 27)
		return gpio - 4;
	else if (gpio <= 37)
		return gpio - 6;

	return gpio;
}

static ssize_t rwnx_dbgfs_tports_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *rw_hw = file->private_data;
	char buf[100];
	size_t len = min_t(size_t, count, sizeof(buf) - 1);
	char *argv[12];
	int argc = 0;
	u8 ver = 0, i = 0;
	struct bes_coex_cmd coex_cmd = {0};

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';

	/* Parsing command line */
	argc = bes_common_parse_line(buf, argv, 12);
	if (argc == 0) {
		bes_err(BES_DBG_DEBUGFS,
			"%s ERROR: fail to parse line. argc:0\n", __func__);
		return -EINVAL;
	}

	/* Get tports version */
	if (strcmp(argv[0], "tports_v0") == 0) {
		ver = 0;
	} else if (strcmp(argv[0], "tports_v1") == 0) {
		ver = 1;
	} else {
		bes_err(BES_DBG_DEBUGFS, "coex tport input is invalid\n");
		return -EINVAL;
	}

	/* Set command id */
	coex_cmd.coex_cmd_id = BES_COEX_CMD_GOIO_TEST;

	/* Parse IO information */
	for (i = 0 ; i < argc; i++) {
		char *option = argv[i];
		int val = 0;

		if (!strncasecmp(option, "en:", 3) && !kstrtoint(&option[3], 0, &val)) {
			if (ver == 0)
				coex_cmd.u.tports_v0.enable = !!val;
			else if (ver == 1)
				coex_cmd.u.tports_v1.enable = !!val;
		} else if (!strncasecmp(option, "fdd:", 4) && !kstrtoint(&option[4], 0, &val)) {
			if (ver == 0) {
				coex_cmd.u.tports_v0.io_fdd = rwnx_gpio_num_to_io_idx(val);
			} else if (ver == 1) {
				coex_cmd.u.tports_v1.gpio[BES_TPORT_IO_FDD] = rwnx_gpio_num_to_io_idx(val);
				coex_cmd.u.tports_v1.gpio[BES_TPORT_IO_FDD] |= BIT(7);	// valid flag
			}
		} else if (!strncasecmp(option, "doze:", 5) && !kstrtoint(&option[5], 0, &val)) {
			if (ver == 0) {
				coex_cmd.u.tports_v0.io_doze = rwnx_gpio_num_to_io_idx(val);
			} else if (ver == 1) {
				coex_cmd.u.tports_v1.gpio[BES_TPORT_IO_WAKEUP] = rwnx_gpio_num_to_io_idx(val);
				coex_cmd.u.tports_v1.gpio[BES_TPORT_IO_WAKEUP] |= BIT(7);	// valid flag
			}
		} else if (!strncasecmp(option, "wf_win:", 7) && !kstrtoint(&option[7], 0, &val)) {
			if (ver == 0) {
				coex_cmd.u.tports_v0.io_wifi_win = rwnx_gpio_num_to_io_idx(val);
			} else if (ver == 1) {
				coex_cmd.u.tports_v1.gpio[BES_TPORT_IO_WIFI_WIN] = rwnx_gpio_num_to_io_idx(val);
				coex_cmd.u.tports_v1.gpio[BES_TPORT_IO_WIFI_WIN] |= BIT(7);	// valid flag
			}
		} else if (!strncasecmp(option, "bcn:", 4) && !kstrtoint(&option[4], 0, &val)) {
			if (ver == 0) {
				coex_cmd.u.tports_v0.io_bcn = rwnx_gpio_num_to_io_idx(val);
			} else if (ver == 1) {
				coex_cmd.u.tports_v1.gpio[BES_TPORT_IO_BEACON] = rwnx_gpio_num_to_io_idx(val);
				coex_cmd.u.tports_v1.gpio[BES_TPORT_IO_BEACON] |= BIT(7);	// valid flag
			}
		} else if (!strncasecmp(option, "null_data:", 9) && !kstrtoint(&option[9], 0, &val)) {
			if (ver == 1) {
				coex_cmd.u.tports_v1.gpio[BES_TPORT_IO_PM_NULL_DATA] = rwnx_gpio_num_to_io_idx(val);
				coex_cmd.u.tports_v1.gpio[BES_TPORT_IO_PM_NULL_DATA] |= BIT(7);	// valid flag
			}
		} else if (!strncasecmp(option, "coex_state:", 11) && !kstrtoint(&option[11], 0, &val)) {
			if (ver == 1) {
				coex_cmd.u.tports_v1.gpio[BES_TPORT_IO_CHAN_START] = rwnx_gpio_num_to_io_idx(val);
				coex_cmd.u.tports_v1.gpio[BES_TPORT_IO_CHAN_START] |= BIT(7);	// valid flag
			}
		} else if (!strncasecmp(option, "transq_win:", 11) && !kstrtoint(&option[11], 0, &val)) {
			if (ver == 1) {
				coex_cmd.u.tports_v1.gpio[BES_TPORT_IO_TRANSQ_WIN] = rwnx_gpio_num_to_io_idx(val);
				coex_cmd.u.tports_v1.gpio[BES_TPORT_IO_TRANSQ_WIN] |= BIT(7);	// valid flag
			}
		}
	}

	/* Output debug information */
	if (ver == 0) {
		bes_info(BES_DBG_COEX, "tports_v0 en:%u fdd:%u doze:%u wf_win:%u bcn:%u\n",
				coex_cmd.u.tports_v0.enable, coex_cmd.u.tports_v0.io_fdd,
				coex_cmd.u.tports_v0.io_doze, coex_cmd.u.tports_v0.io_wifi_win,
				coex_cmd.u.tports_v0.io_bcn);
	} else if (ver == 1) {
		bes_info(BES_DBG_COEX,
				"tports_v1 en:%x fdd:0x%x doze:0x%x wf_win:0x%x bcn:0x%x null_data:0x%x coex_state:0x%x transq_win:0x%x\n",
				 coex_cmd.u.tports_v1.enable,
				 coex_cmd.u.tports_v1.gpio[BES_TPORT_IO_FDD],
				 coex_cmd.u.tports_v1.gpio[BES_TPORT_IO_WAKEUP],
				 coex_cmd.u.tports_v1.gpio[BES_TPORT_IO_WIFI_WIN],
				 coex_cmd.u.tports_v1.gpio[BES_TPORT_IO_BEACON],
				 coex_cmd.u.tports_v1.gpio[BES_TPORT_IO_PM_NULL_DATA],
				 coex_cmd.u.tports_v1.gpio[BES_TPORT_IO_CHAN_START],
				 coex_cmd.u.tports_v1.gpio[BES_TPORT_IO_TRANSQ_WIN]);
	}

	/* Sync configuration to device */
	rwnx_send_pta_cfg(rw_hw, &coex_cmd);

	return count;
}

DEBUGFS_WRITE_FILE_OPS(tports);

static ssize_t rwnx_dbgfs_transq_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *rw_hw = file->private_data;
	char buf[35];
	size_t len = min_t(size_t, count, sizeof(buf) - 1);
	u32 start, dur, period;

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';

	/* Read transq event parameters */
	if (sscanf(buf, "transq start:%d dur:%d period:%d", &start, &dur, &period) > 0) {
		struct bes_coex_cmd coex_cmd = {0};

		coex_cmd.coex_cmd_id = BES_COEX_CMD_TRANSQ_TEST;
		coex_cmd.u.coex_transq.start = start;
		coex_cmd.u.coex_transq.duration = dur;
		coex_cmd.u.coex_transq.period = period;
		bes_info(BES_DBG_COEX, "transq start:%d dur:%d period:%d\n", start, dur, period);

		rwnx_send_pta_cfg(rw_hw, &coex_cmd);
	} else {
		bes_err(BES_DBG_COEX, "coex transq parse failed\n");
		return count;
	}

	return count;
}

DEBUGFS_WRITE_FILE_OPS(transq);

static ssize_t rwnx_dbgfs_coex_state_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *priv = file->private_data;
	char *buf= NULL;
	int len = 0;
	u32 wifi_dur = 0, bt_dur = 0;

	/* malloc dump buffer */
	buf = (char *)kmalloc(BES_COEX_BT_STATE_DUMP_BUFSIZE, GFP_KERNEL);
	if (!buf) {
		bes_err(BES_DBG_COEX, "%s Fail to alloc memory\n", __func__);
		return len;
	}

	/* get the information of coex bt state */
	len = bes_coex_bt_state_info(buf, BES_COEX_BT_STATE_DUMP_BUFSIZE,
				     priv->coex.bt_coex_state);
	if (len >= BES_COEX_BT_STATE_DUMP_BUFSIZE - 1)
		goto out;

	/* calculate duration info */
	bt_dur = bes_coex_bt_calc_time();
	wifi_dur = BES_COEX_PERIOD - bt_dur;
	len += scnprintf(&buf[len], BES_COEX_BT_STATE_DUMP_BUFSIZE - 1, "coex duration, wifi:%d bt:%d\n",
			 wifi_dur, bt_dur);

out:
	/* print */
	len = simple_read_from_buffer(user_buf, count, ppos, buf, len);

	/* free dump buffer */
	kfree(buf);
	return len;
}

DEBUGFS_READ_FILE_OPS(coex_state);

static ssize_t rwnx_dbgfs_mode_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *rw_hw = file->private_data;
	char buf[35];
	size_t len = min_t(size_t, count, sizeof(buf) - 1);
	struct bes_coex_cmd coex_cmd = {0};

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';

	coex_cmd.coex_cmd_id = BES_COEX_CMD_MODE;
	if (strncasecmp(buf, "tdd", 3) == 0)
		coex_cmd.u.coex_mode = BES_COEX_MODE_TDD;
	else if (strncasecmp(buf, "fdd", 3) == 0)
		coex_cmd.u.coex_mode = BES_COEX_MODE_FDD;
	else {
		bes_err(BES_DBG_COEX, "unexpect mode for coex mode\n");
		return count;
	}

	bes_info(BES_DBG_COEX, "change coex_mode to %d\n", coex_cmd.u.coex_mode);

	rwnx_send_pta_cfg(rw_hw, &coex_cmd);

	return count;
}

DEBUGFS_WRITE_FILE_OPS(mode);

static ssize_t rwnx_dbgfs_duration_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *rw_hw = file->private_data;
	char buf[35];
	size_t len = min_t(size_t, count, sizeof(buf) - 1);
	u32 wifi_dur, bt_dur;

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';

	/* Read the duration (provided in ms) value */
	if (sscanf(buf, "wifi:%d bt:%d", &wifi_dur, &bt_dur) > 0) {
		struct bes_coex_cmd coex_cmd = {0};

		coex_cmd.coex_cmd_id = BES_COEX_CMD_DURATION;
		coex_cmd.u.time_ratio.wifi_duration = wifi_dur;
		coex_cmd.u.time_ratio.bt_duration = bt_dur;
		bes_info(BES_DBG_COEX, "coex wifi:%d bt:%d\n", wifi_dur, bt_dur);

		rwnx_send_pta_cfg(rw_hw, &coex_cmd);
	} else {
		bes_err(BES_DBG_COEX, "coex duration parse failed\n");
		return count;
	}

	return count;
}

DEBUGFS_WRITE_FILE_OPS(duration);

static ssize_t rwnx_dbgfs_sche_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *rw_hw = file->private_data;
	char buf[35];
	size_t len = min_t(size_t, count, sizeof(buf) - 1);
	struct bes_coex_cmd coex_cmd = {0};

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';

	if (strncasecmp(buf, "start", 5) == 0) {
		bes_info(BES_DBG_COEX, "start coex schedule\n");
		coex_cmd.coex_cmd_id = BES_COEX_CMD_START;
	} else if (strncasecmp(buf, "stop", 4) == 0) {
		bes_info(BES_DBG_COEX, "stop coex schedule\n");
		coex_cmd.coex_cmd_id = BES_COEX_CMD_STOP;
	} else {
		bes_err(BES_DBG_COEX, "unexpect input for sche mode\n");
		return count;
	}

	rwnx_send_pta_cfg(rw_hw, &coex_cmd);

	return count;
}

DEBUGFS_WRITE_FILE_OPS(sche);

static ssize_t rwnx_dbgfs_pta_switch_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *rw_hw = file->private_data;
	char buf[35];
	size_t len = min_t(size_t, count, sizeof(buf) - 1);
	struct bes_coex_cmd coex_cmd = {0};
	u32 pta_switch_flags;

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';

	if (sscanf(buf, "val:0x%x", &pta_switch_flags) > 0) {
		coex_cmd.coex_cmd_id = BES_COEX_CMD_SET_PTA_FLAG;
		coex_cmd.u.pta_switch_flags = pta_switch_flags;
		bes_info(BES_DBG_COEX, "pta val:0x%x\n", pta_switch_flags);

		rwnx_send_pta_cfg(rw_hw, &coex_cmd);
	} else {
		bes_err(BES_DBG_COEX, "parse pta switch failed\n");
		return count;
	}

	rwnx_send_pta_cfg(rw_hw, &coex_cmd);

	return count;
}

DEBUGFS_WRITE_FILE_OPS(pta_switch);

static ssize_t rwnx_dbgfs_pta_test_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_hw *rw_hw = file->private_data;
	char buf[35];
	size_t len = min_t(size_t, count, sizeof(buf) - 1);
	struct bes_coex_cmd coex_cmd = {0};
	u32 pta_mode = 0;

	/* Copy data from user */
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';

	/* Parse command paramter */
	if (strncasecmp(buf, "cali_mode", 9) == 0) {
		pta_mode = COEX_TEST_PTA_SET_CAL_MODE;
	} else if (strncasecmp(buf, "cali_exit", 9) == 0) {
		pta_mode = COEX_TEST_PTA_EXIT_CAL_MODE;
	} else if (strncasecmp(buf, "fdd", 3) == 0) {
		pta_mode = COEX_TEST_PTA_SET_FDD_MODE;
	} else if (strncasecmp(buf, "unblockbt", 9) == 0) {
		pta_mode = COEX_TEST_PTA_SET_UNBLOCKBT_MODE;
	} else if (strncasecmp(buf, "blockbt", 7) == 0) {
		pta_mode = COEX_TEST_PTA_SET_BLOCKBT_MODE;
	} else {
		bes_err(BES_DBG_COEX, "parse pta switch failed\n");
		return -EINVAL;
	}

	/* Set command information based on user config */
	coex_cmd.coex_cmd_id = BES_COEX_CMD_PTA_TEST;
	coex_cmd.u.pta_mode = pta_mode;

	/* Send command to device */
	rwnx_send_pta_cfg(rw_hw, &coex_cmd);

	return count;
}

DEBUGFS_WRITE_FILE_OPS(pta_test);
#endif // CONFIG_BES_COEX_SUPPORT

static ssize_t rwnx_dbgfs_tdd_slice_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[35];
	char ifname1[10] = {0};
	char ifname2[10] = {0};
	u32 time_slice1, time_slice2, period;
	struct rwnx_vif *vif1, *vif2;
	size_t len = min_t(size_t, count, sizeof(buf) - 1);

	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';

	if (sscanf(buf, "%s (%d):%s (%d)", ifname1, &time_slice1, ifname2, &time_slice2) > 0) {
		/* get vif instance by vif name */
		vif1 = rwnx_dbgfs_get_vif(ifname1);
		vif2 = rwnx_dbgfs_get_vif(ifname2);

		/* check if vif is valid */
		if ((vif1 == NULL) || (vif2 == NULL)) {
			bes_err(BES_DBG_DEBUGFS, "%s/%s is invalid\n", ifname1, ifname2);
			return count;
		}

		/* check if time slice is valid  */
		if (time_slice1 == 0 || time_slice2 == 0) {
			bes_err(BES_DBG_DEBUGFS, "time slice info is invalid\n");
			return count;
		}

		/* convert user input to percent */
		period = time_slice1 + time_slice2;
		time_slice1 = time_slice1 * 100 / period;
		time_slice2 = time_slice2 * 100 / period;

		/* convert user input to us */
		time_slice1 *= 1000;
		time_slice2 *= 1000;

		/* set user setting to device */
		bes_info(BES_DBG_DEBUGFS, "%s:%s = %d:%d\n", ifname1, ifname2, time_slice1, time_slice2);
		bes_set_vif_partition(vif1, vif2, time_slice1, time_slice2);

	} else {
		bes_err(BES_DBG_COEX, "parse tdd slice failed\n");
		return count;
	}

	return count;
}

DEBUGFS_WRITE_FILE_OPS(tdd_slice);

static ssize_t rwnx_dbgfs_unmatch_dst_mgmt_filter_write(struct file *file, const char __user *user_buf,
						   size_t count, loff_t *ppos)
{
	struct rwnx_hw *rwnx_hw = file->private_data;
	struct rwnx_debugfs *debugfs = &rwnx_hw->debugfs;
	int rc;
	bool en;

	/* You can enable the filter by using "Y", "y", "1", "On", "on",
	 * and disable the filter by using "N", "n", "0", "Off", "off".  */
	rc = kstrtobool_from_user(user_buf, count, &en);
	if (rc)
		return rc;

	if (en != debugfs->unmatch_dst_mgmt_filter_en) {
		bes_filter_unmatch_dst_mgmt_set(rwnx_hw, en);
		debugfs->unmatch_dst_mgmt_filter_en = en;
	}

	return count;
}

static ssize_t rwnx_dbgfs_unmatch_dst_mgmt_filter_read(struct file *file, char __user *user_buf,
						  size_t count, loff_t *ppos)
{
	struct rwnx_debugfs *debugfs =
			&((struct rwnx_hw *)(file->private_data))->debugfs;
	char buf[64] = {0};
	ssize_t read;
	int len = 0;

	len = scnprintf(&buf[len], sizeof(buf) - len,
			"Unmatch destination mgmt filter status:%s\n",
			debugfs->unmatch_dst_mgmt_filter_en ? "enable" : "disable");
	read = simple_read_from_buffer(user_buf, count, ppos, buf, (size_t)len);
	return read;
}

/* rwnx_dbgfs_unmatch_dst_mgmt_filter_ops */
DEBUGFS_READ_WRITE_FILE_OPS(unmatch_dst_mgmt_filter);

static ssize_t rwnx_dbgfs_pre_cali_read(struct file *file, char __user *user_buf,
						  size_t count, loff_t *ppos)
{
	struct rwnx_hw *rwnx_hw = file->private_data;
	struct bes_rf_cali *rf_cali = &rwnx_hw->rf_cali;
	struct bes_cali_result *cali_res;
	u32 len = 0;
	char buf[256] = {0};
	u8 i;

	len += scnprintf(&buf[len], sizeof(buf) - len,
			 "cali_result num:%d\n", rf_cali->cali_res_cnt);
	for (i = 0; i < MAX_COEXIST_CHAN_CTXT; i++ ) {
		cali_res = &(rf_cali->cali_res[i]);
		if (!cali_res->count)
			continue;

		len += scnprintf(&buf[len], sizeof(buf) - len,
				 "CALI[%d] channel:%d type:%d ref_cnt:%d\n",
				 i, cali_res->channel, cali_res->type, cali_res->count);
	}

	return simple_read_from_buffer(user_buf, count, ppos, buf, (size_t)len);
}

static ssize_t rwnx_dbgfs_pre_cali_write(struct file *file, const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct rwnx_hw *rwnx_hw = file->private_data;
	struct rwnx_vif *rwnx_vif;
	char buf[IFNAMSIZ + 16] = {0};
	size_t len = min_t(size_t, count, sizeof(buf) - 1);
	int ret, argc;
	char *argv[6] = {NULL};

	/* Get the content of the file */
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;

	/* Parsing command line */
	argc = bes_common_parse_line(buf, argv, 6);
	if (argc == 2 && !strcasecmp(argv[0], "INVALID_CALI")) {
		u8 idx = 0xFF;

		/* Get the index of the Pre-calibration result that
		 * is desired to be invalidated. */
		if ((ret = kstrtou8(argv[1], 0, &idx))) {
			dev_err(rwnx_hw->dev, "%s invalid idx:%s ret:%d\n",
				__func__, argv[1], ret);
			return -EINVAL;
		}

		/* Invalidate the specified pre-calibration result. */
		bes_dbac_pre_cali_invalid(rwnx_hw, idx);

	} else if (argc == 5) {
		u16 primary_freq = 0;
		struct cfg80211_chan_def chandef;

		/* Find the corresponding interface pointer based on the provided name. */
		rwnx_vif = rwnx_dbgfs_get_vif(argv[0]);
		if (!rwnx_vif) {
			dev_err(rwnx_hw->dev, "%s Interface %s was not found\n",
				__func__, argv[0]);
			return -EINVAL;
		}

		/* Get the pointer to the main channel structure. */
		memset(&chandef, 0, sizeof(struct cfg80211_chan_def));
		if ((ret = kstrtou16(argv[1], 0, &primary_freq)) ||
		!(chandef.chan = ieee80211_get_channel(rwnx_hw->wiphy, primary_freq))) {
			dev_err(rwnx_hw->dev, "%s invalid pri_ch:%s ret:%d freq:%d\n",
				__func__, argv[1], ret, primary_freq);
			return -EINVAL;
		}

		/* Channel bandwidth in MHz (20M, 40M, 80M). */
		if (!strcasecmp(argv[2], "20M")) {
			chandef.width = NL80211_CHAN_WIDTH_20;
		} else if (!strcasecmp(argv[2], "40M")) {
			chandef.width = NL80211_CHAN_WIDTH_40;
		} else if (!strcasecmp(argv[2], "80M")) {
			chandef.width = NL80211_CHAN_WIDTH_80;
		} else {
			dev_err(rwnx_hw->dev, "%s invalid bw:%s\n", __func__, argv[2]);
			return -EINVAL;
		}

		/* Segment 0 center frequency in MHz */
		if ((ret = kstrtouint(argv[3], 0, &chandef.center_freq1))) {
			dev_err(rwnx_hw->dev, "%s invalid center1:%s ret:%d\n",
				__func__, argv[3], ret);
			return -EINVAL;
		}

		/* Segment 1 center frequency in MHz */
		if ((ret = kstrtouint(argv[4], 0, &chandef.center_freq2))) {
			dev_err(rwnx_hw->dev, "%s invalid center2:%s ret:%d\n",
				__func__, argv[4], ret);
			return -EINVAL;
		}

		/* Pre-calibrate the designated channel. */
		bes_dbac_pre_cali(rwnx_vif, &chandef);
	} else {
		dev_err(rwnx_hw->dev, "Invalid cmd\n");
	}

	return count;
}

/* rwnx_dbgfs_pre_cali_ops */
DEBUGFS_READ_WRITE_FILE_OPS(pre_cali);

#ifdef CONFIG_RWNX_FULLMAC
static void _rwnx_dbgfs_register_sta(struct rwnx_debugfs *rwnx_debugfs, struct rwnx_sta *sta)
{
	struct rwnx_hw *rwnx_hw = container_of(rwnx_debugfs, struct rwnx_hw, debugfs);
	struct dentry *dir_sta;
	char sta_name[18];
	struct dentry *dir_rc;
	struct dentry *file;
	struct rwnx_rc_config_save *rc_cfg, *next;
	bool reuse_rc_cfg = BES_RC_CFG_SAVE;
	struct rwnx_vif *rwnx_vif;

	if (sta->sta_idx >= NX_REMOTE_STA_MAX) {
		WARN(1, "Invalid sta index %d", sta->sta_idx);
		goto error;
	}

	scnprintf(sta_name, sizeof(sta_name), "%pM", sta->mac_addr);

	if (!sta->stats.rx_rate.size || !sta->stats.tx_rate.size)
		goto error;

	if (!(dir_sta = debugfs_create_dir(sta_name, rwnx_debugfs->dir_stas)))
		goto error;
	rwnx_debugfs->dir_sta[sta->sta_idx] = dir_sta;

	if (!(dir_rc = debugfs_create_dir("rc", rwnx_debugfs->dir_sta[sta->sta_idx])))
		goto error_after_dir;

	rwnx_debugfs->dir_rc_sta[sta->sta_idx] = dir_rc;

	file = debugfs_create_file("stats", S_IRUSR, dir_rc, rwnx_hw, &rwnx_dbgfs_rc_stats_ops);
	if (IS_ERR_OR_NULL(file))
		goto error_after_dir;

	file = debugfs_create_file("fixed_rate_idx", S_IWUSR, dir_rc, rwnx_hw, &rwnx_dbgfs_rc_fixed_rate_idx_ops);
	if (IS_ERR_OR_NULL(file))
		goto error_after_dir;

	file = debugfs_create_file("rx_rate", S_IRUSR | S_IWUSR, dir_rc, rwnx_hw, &rwnx_dbgfs_last_rx_ops);
	if (IS_ERR_OR_NULL(file))
		goto error_after_dir;

	file = debugfs_create_file("tx_rate", S_IRUSR | S_IWUSR, dir_rc, rwnx_hw, &rwnx_dbgfs_tx_rate_ops);
	if (IS_ERR_OR_NULL(file))
		goto error_after_dir;

	file = debugfs_create_file("link", S_IRUSR, dir_sta, sta, &rwnx_dbgfs_link_info_ops);
	if (IS_ERR_OR_NULL(file))
		goto error_after_dir;

	file = debugfs_create_file("flush", S_IWUSR, dir_sta, rwnx_hw, &rwnx_dbgfs_flush_ops);
	if (IS_ERR_OR_NULL(file))
		goto error_after_dir;

	/* By default enable rate contoller */
	rwnx_debugfs->rc_config[sta->sta_idx] = -1;

	/* Unless we already fix the rate for this station */
	list_for_each_entry_safe(rc_cfg, next, &rwnx_debugfs->rc_config_save, list) {
		if (jiffies_to_msecs(jiffies - rc_cfg->timestamp) > RC_CONFIG_DUR) {
			list_del(&rc_cfg->list);
			kfree(rc_cfg);
		} else if (!memcmp(rc_cfg->mac_addr, sta->mac_addr, ETH_ALEN)) {
			rwnx_debugfs->rc_config[sta->sta_idx] = rc_cfg->rate;
			list_del(&rc_cfg->list);
			kfree(rc_cfg);
			break;
		}
	}

	if (reuse_rc_cfg && (rwnx_debugfs->rc_config[sta->sta_idx] >= 0) &&
	    rwnx_send_me_rc_set_rate(rwnx_hw, sta->sta_idx, (u16)rwnx_debugfs->rc_config[sta->sta_idx]))
		rwnx_debugfs->rc_config[sta->sta_idx] = -1;

	rwnx_vif = rwnx_rx_get_vif(rwnx_hw, sta->vif_idx);

	if (rwnx_vif && RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION) {
		/* register the sta */
		struct dentry *dir_twt;
		struct dentry *file;

		if (!(dir_twt = debugfs_create_dir("twt", rwnx_debugfs->dir_sta[sta->sta_idx])))
			goto error_after_dir;

		rwnx_debugfs->dir_twt_sta[sta->sta_idx] = dir_twt;

		file = debugfs_create_file("request", S_IRUSR | S_IWUSR, dir_twt, rwnx_hw, &rwnx_dbgfs_twt_request_ops);
		if (IS_ERR_OR_NULL(file))
			goto error_after_dir;

		file = debugfs_create_file("teardown", S_IRUSR | S_IWUSR, dir_twt, rwnx_hw,
					   &rwnx_dbgfs_twt_teardown_ops);
		if (IS_ERR_OR_NULL(file))
			goto error_after_dir;
	}
	return;

error_after_dir:
	debugfs_remove_recursive(rwnx_debugfs->dir_sta[sta->sta_idx]);
	rwnx_debugfs->dir_sta[sta->sta_idx] = NULL;
	rwnx_debugfs->dir_rc_sta[sta->sta_idx] = NULL;
	rwnx_debugfs->dir_twt_sta[sta->sta_idx] = NULL;
error:
	dev_err(rwnx_hw->dev, "Error while registering debug entry for sta %d\n", sta->sta_idx);
}

static void _rwnx_dbgfs_unregister_sta(struct rwnx_debugfs *rwnx_debugfs, struct rwnx_sta *sta)
{
	debugfs_remove_recursive(rwnx_debugfs->dir_sta[sta->sta_idx]);

	/* If fix rate was set for this station, save the configuration in case
	   we reconnect to this station within RC_CONFIG_DUR msec */
	if (rwnx_debugfs->rc_config[sta->sta_idx] >= 0) {
		struct rwnx_rc_config_save *rc_cfg;
		rc_cfg = kmalloc(sizeof(*rc_cfg), GFP_KERNEL);
		if (rc_cfg) {
			rc_cfg->rate = rwnx_debugfs->rc_config[sta->sta_idx];
			rc_cfg->timestamp = jiffies;
			memcpy(rc_cfg->mac_addr, sta->mac_addr, ETH_ALEN);
			list_add_tail(&rc_cfg->list, &rwnx_debugfs->rc_config_save);
		}
	}

	rwnx_debugfs->dir_sta[sta->sta_idx] = NULL;
	rwnx_debugfs->dir_rc_sta[sta->sta_idx] = NULL;
	rwnx_debugfs->dir_twt_sta[sta->sta_idx] = NULL;
}

static void rwnx_sta_work(struct work_struct *ws)
{
	struct rwnx_debugfs *rwnx_debugfs = container_of(ws, struct rwnx_debugfs, sta_work);
	struct rwnx_hw *rwnx_hw = container_of(rwnx_debugfs, struct rwnx_hw, debugfs);
	struct rwnx_sta *sta;
	uint8_t sta_idx;

	sta_idx = rwnx_debugfs->sta_idx;
	if (sta_idx >= (NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX)) {
		WARN(1, "Invalid sta index %d", sta_idx);
		return;
	}

	rwnx_debugfs->sta_idx = RWNX_INVALID_STA;
	sta = rwnx_rx_get_sta(rwnx_hw, sta_idx);
	if (!sta) {
		WARN(1, "Invalid sta %d", sta_idx);
		return;
	}

	if (sta_idx < NX_REMOTE_STA_MAX) {
		if (rwnx_debugfs->dir_sta[sta_idx] == NULL)
			_rwnx_dbgfs_register_sta(rwnx_debugfs, sta);
		else
			_rwnx_dbgfs_unregister_sta(rwnx_debugfs, sta);
	}

	return;
}

static void _rwnx_dbgfs_sta_write(struct rwnx_debugfs *rwnx_debugfs, uint8_t sta_idx)
{
	if (rwnx_debugfs->unregistering)
		return;

	rwnx_debugfs->sta_idx = sta_idx;
	schedule_work(&rwnx_debugfs->sta_work);
}

void rwnx_dbgfs_unregister_sta(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta)
{
	_rwnx_dbgfs_sta_write(&rwnx_hw->debugfs, sta->sta_idx);
}

void rwnx_dbgfs_register_sta(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta)
{
	_rwnx_dbgfs_sta_write(&rwnx_hw->debugfs, sta->sta_idx);
}
#endif /* CONFIG_RWNX_FULLMAC */

static ssize_t rwnx_dbgfs_vif_stats_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	struct rwnx_vif *rwnx_vif = NULL;
	struct rwnx_hw *rwnx_hw = file->private_data;
	ssize_t read;
	char *buf;
	int bufsz, ret = 0, i;
	struct bes_vif_stat *vif_stat;
	struct lstats_time tm = {0};
	char *ac[] = {"VO", "VI", "BE", "BK"};

	bes_dbg(BES_DBG_DEBUGFS, RWNX_FN_ENTRY_STR);

	/* everything should fit in one call */
	if (*ppos)
		return 0;

	/* Get the station index from dev name */
	rwnx_vif = rwnx_dbgfs_get_vif(file->f_path.dentry->d_parent->d_iname);
	if (rwnx_vif == NULL)
		return -EINVAL;

	if (!rwnx_vif->up)
		return 0;

	vif_stat = &rwnx_vif->lstats_vif.v_stat;

	bufsz = 2048;
	buf = kmalloc(bufsz, GFP_ATOMIC);
	if (buf == NULL)
		return 0;

	bes_get_vif_time_stats(rwnx_hw, rwnx_vif);
	tm = bes_lstats_ktime_to_tm(vif_stat->on_time);
	ret = scnprintf(buf, bufsz, "On Time: %uh:%um:%us\n", tm.hour, tm.min, tm.sec);

	tm = bes_lstats_ktime_to_tm(vif_stat->tx_time);
	ret += scnprintf(&buf[ret], bufsz - ret, "Tx Time: %uh:%um:%us\n", tm.hour, tm.min, tm.sec);

	tm = bes_lstats_ktime_to_tm(vif_stat->rx_time);
	ret += scnprintf(&buf[ret], bufsz - ret, "Rx Time: %uh:%um:%us\n", tm.hour, tm.min, tm.sec);

	ret += scnprintf(&buf[ret], bufsz - ret, "\nRx Beacon :%16u packets\n", vif_stat->beacon_rx);
	ret += scnprintf(&buf[ret], bufsz - ret, "Mgmt Rssi :%16d dBm\n", vif_stat->rssi_mgmt);

	ret += scnprintf(&buf[ret], bufsz - ret, "\n*** Traffic AC ***\n");

	/* (7 + 18 + 18 + 18 + 18) * 5 = 395 */
	ret += scnprintf(&buf[ret], bufsz - ret, "AC%14cTX_MPDU%11cRX_MPDU%9cMPDU_LOST%11cRETRIES\n", ' ', ' ', ' ', ' ');

	for (i = 0; i < WIFI_AC_MAX; i++) {
		ret += scnprintf(&buf[ret], bufsz - ret, "%s%2c%c%18u%18u%18u%18u\n",
				ac[i], ' ', ':', vif_stat->ac_stat[BES_CFGVENDOR_AC_IDX(i)].tx_mpdu,
				vif_stat->ac_stat[BES_CFGVENDOR_AC_IDX(i)].rx_mpdu,
				vif_stat->ac_stat[BES_CFGVENDOR_AC_IDX(i)].mpdu_lost,
				vif_stat->ac_stat[BES_CFGVENDOR_AC_IDX(i)].retries);

	}

	read = simple_read_from_buffer(user_buf, count, ppos, buf, ret);

	kfree(buf);

	return read;
}

DEBUGFS_READ_FILE_OPS(vif_stats);

static int rwnx_set_edca_info(struct rwnx_edca_param *edca_config, char *cmd)
{
	u32 info = 0;

	if (!cmd) {
		bes_err(BES_DBG_DEBUGFS, "%s cmd == NULL\n", __func__);
		return -EFAULT;
	}

	if (!strncasecmp(cmd, "uapsd:", 6) && !kstrtoint(&cmd[6], 0, &info)) {
		edca_config->uapsd = !!info;
	} else if (!strncasecmp(cmd, "cwmax:", 6) && !kstrtoint(&cmd[6], 0, &info)) {
		edca_config->edca.cwmax = (u8)info;
	} else if (!strncasecmp(cmd, "cwmin:", 6) && !kstrtoint(&cmd[6], 0, &info)) {
		edca_config->edca.cwmin = (u8)info;
	} else if (!strncasecmp(cmd, "aifsn:", 6) && !kstrtoint(&cmd[6], 0, &info)) {
		edca_config->edca.aifs = (u8)info;
	} else if (!strncasecmp(cmd, "txop:", 5) && !kstrtoint(&cmd[5], 0, &info)) {
		edca_config->edca.txop = (u16)info;
	} else if (!strncasecmp(cmd, "hw_q:", 5)) {
		if (!strncasecmp(&cmd[5], "BK:", 2))
			edca_config->hw_queue = RWNX_HWQ_BK;
		else if (!strncasecmp(&cmd[5], "BE:", 2))
			edca_config->hw_queue = RWNX_HWQ_BE;
		else if (!strncasecmp(&cmd[5], "VI:", 2))
			edca_config->hw_queue = RWNX_HWQ_VI;
		else if (!strncasecmp(&cmd[5], "VO:", 2))
			edca_config->hw_queue = RWNX_HWQ_VO;
		else {
			bes_err(BES_DBG_DEBUGFS, "%s cmd:\"%s\" invalid\n", __func__, cmd);
			return -EINVAL;
		}
	} else {
		bes_err(BES_DBG_DEBUGFS, "%s [ERR] %s\n", __func__, cmd);
		return -EINVAL;
	}

	return 0;
}

/* Set edca parameters for the specific interface. */
static ssize_t rwnx_dbgfs_vif_edca_write(struct file *file, const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	struct rwnx_hw *rwnx_hw = file->private_data;
	struct rwnx_vif *rwnx_vif = NULL;
	char cmd[128] = {0};
	size_t len = min_t(size_t, count, sizeof(cmd) - 1);
	int error = 0, argc = 0;
	char *argv[8] = {NULL};
	struct rwnx_edca_param edca_config;
	u32 ac_param = 0;
	u8 i;
	u8 vif_index = 0;

	/* Get the content of the file */
	if (copy_from_user(cmd, user_buf, len))
		return -EFAULT;
	cmd[len] = '\0';

	/* Parsing command line */
	argc = bes_common_parse_line(cmd, argv, 8);
	if (argc <= 0) {
		bes_err(BES_DBG_DEBUGFS, "%s ERROR: fail to parse line. argc:%d\n", __func__, argc);
		return -EINVAL;
	}

	/* Fill in information */
	memset(&edca_config, 0, sizeof(struct rwnx_edca_param));
	for (i = 0; i < argc; i++) {
		if (i >= ARRAY_SIZE(argv))
			break;

		if (rwnx_set_edca_info(&edca_config, argv[i]))
			return -EINVAL;
	}

	/* Get the vif from dev name */
	rwnx_vif = rwnx_dbgfs_get_vif(file->f_path.dentry->d_parent->d_iname);

	if (!rwnx_vif || !rwnx_vif->up)
		return -EINVAL;

	/* Get the index of vif */
	vif_index = rwnx_vif->vif_index;
	if (vif_index >= NX_ITF_MAX)
		return -EINVAL;

	/* Effective value check */
	if (edca_config.hw_queue >= AC_MAX || edca_config.edca.aifs > 0xF ||
	    edca_config.edca.cwmin > 0xF || edca_config.edca.cwmax > 0xF || edca_config.edca.txop > 0xFFFF)
		return -EINVAL;

	/* Store queue information in general structure */
	ac_param = (u32)(edca_config.edca.aifs);
	ac_param |= (u32)(edca_config.edca.cwmin << 4);
	ac_param |= (u32)(edca_config.edca.cwmax << 8);
	ac_param |= (u32)(edca_config.edca.txop << 12);

	bes_info(BES_DBG_DEBUGFS,
		  "%s txop:%x vif_idx:%d hw_queue:%d aifs:%x cwmin:%x cwmax:%x uapsd:%d ac_param:0x%x\n",
		  __func__, edca_config.edca.txop, vif_index, edca_config.hw_queue, edca_config.edca.aifs,
		  edca_config.edca.cwmin, edca_config.edca.cwmax, edca_config.uapsd, ac_param);

	/* ignore beacon edca param */
	error = bes_send_ignore_bcn_edca_req(rwnx_hw, true);
	if (error) {
		bes_err(BES_DBG_DEBUGFS, "%s send disable beacon edca fail:%d\n", __func__, error);
		return -EFAULT;
	}

	/* Send the MM_SET_EDCA_REQ message to the FW */
	error = rwnx_send_set_edca(rwnx_hw, edca_config.hw_queue, ac_param,
				   edca_config.uapsd, vif_index);
	if (error) {
		bes_err(BES_DBG_DEBUGFS, "%s send edca param fail:%d\n", __func__, error);
		return -EFAULT;
	}

	return len;
}

/* Display edca parameters for specific interfaces. */
static ssize_t rwnx_dbgfs_vif_edca_read(struct file *file, char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct rwnx_vif *rwnx_vif = NULL;
	char *buf = NULL;
	u32 bufsz = 1024;
	ssize_t read = 0;
	u32 len = 0;
	struct rwnx_mac_ac_param ac_param[AC_MAX];
#define EDCA_PRINT(AC_TYPE) len += scnprintf(&buf[len], bufsz - len, \
		"%s  %-4d   %-4d   %-4d  %-4d\n", #AC_TYPE, ac_param[AC_TYPE].cwmax, \
		 ac_param[AC_TYPE].cwmin, ac_param[AC_TYPE].aifs, ac_param[AC_TYPE].txop)

	bes_dbg(BES_DBG_DEBUGFS, RWNX_FN_ENTRY_STR);

	/* everything should fit in one call */
	if (*ppos)
		return 0;
	buf = kzalloc(bufsz, GFP_KERNEL);
	if (!buf) {
		bes_err(BES_DBG_DEBUGFS, "%s kzalloc fail\n", __func__);
		return -ENOMEM;
	}

	/* Get the vif from dev name */
	rwnx_vif = rwnx_dbgfs_get_vif(file->f_path.dentry->d_parent->d_iname);

	if (!rwnx_vif || !rwnx_vif->up) {
		kfree(buf);
		return -EINVAL;
	}

	memset(ac_param, 0, sizeof(ac_param));
	bes_vif_info_edca_get(rwnx_vif, (u32 *)ac_param, sizeof(ac_param));

	len += scnprintf(&buf[len], bufsz - len,
				"TYPE  CWmax  CWmin  AIFS  TXOP\n");
	EDCA_PRINT(AC_BK);
	EDCA_PRINT(AC_BE);
	EDCA_PRINT(AC_VI);
	EDCA_PRINT(AC_VO);
#undef EDCA_PRINT

	read = simple_read_from_buffer(user_buf, count, ppos, buf, (size_t)len);
	kfree(buf);
	return read;
}

DEBUGFS_READ_WRITE_FILE_OPS(vif_edca);

#ifdef CONFIG_RWNX_CSI_CAP
static ssize_t rwnx_dbgfs_csi_cap_write(struct file *file, const char __user *user_buf,
					 size_t count, loff_t *ppos)
{
	char cmd[128] = {0};
	size_t len = min_t(size_t, count, sizeof(cmd) - 1);

	/* Get the content of the file */
	if (copy_from_user(cmd, user_buf, len))
		return -EFAULT;
	cmd[len] = '\0';

	bes_dbgfs_csi_cap_write(cmd);

	return len;
}

static ssize_t rwnx_dbgfs_csi_cap_read(struct file *file, char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct rwnx_hw *rwnx_hw = file->private_data;
	ssize_t read = 0;
	u32 bufsz = 2048 * 3;
	static size_t csi_data_len = 0;
	static u8 *csi_buf = NULL;

	bes_dbg(BES_DBG_DEBUGFS, RWNX_FN_ENTRY_STR);

	/* everything should fit in one call */
	if (!*ppos) {
		csi_buf = kzalloc(bufsz, GFP_KERNEL);
		if (!csi_buf) {
			bes_err(BES_DBG_DEBUGFS, "%s kzalloc fail\n", __func__);
			return -ENOMEM;
		}
		csi_data_len = bes_dbgfs_csi_cap_read(rwnx_hw, csi_buf, bufsz);
	}

	if (csi_data_len >= count) {
		if (copy_to_user(user_buf, csi_buf + *ppos, count))
			return -EFAULT;
		csi_data_len -= count;
		*ppos += count;
		read = count;
	} else if (csi_data_len > 0) {
		if (copy_to_user(user_buf, csi_buf + *ppos, csi_data_len))
			return -EFAULT;
		*ppos += csi_data_len;
		read = csi_data_len;
		csi_data_len = 0;
	} else {
		kfree(csi_buf);
		csi_buf = NULL;
		read = 0;
	}

	return read;
}
DEBUGFS_READ_WRITE_FILE_OPS(csi_cap);
#endif

static void _rwnx_dbgfs_register_vif(struct rwnx_debugfs *rwnx_debugfs, struct rwnx_vif *rwnx_vif)
{
	struct rwnx_hw *rwnx_hw = container_of(rwnx_debugfs, struct rwnx_hw, debugfs);
	struct dentry *dir_vif;
	u8 vif_index = rwnx_vif->drv_vif_index;
	struct dentry *file;

	if (!(dir_vif = debugfs_create_dir(rwnx_vif->ndev->name, rwnx_debugfs->dir_vifs)))
		goto error;

	rwnx_debugfs->dir_vif[vif_index] = dir_vif;

	file = debugfs_create_file("stats", S_IRUSR, dir_vif, rwnx_hw, &rwnx_dbgfs_vif_stats_ops);
	if (IS_ERR_OR_NULL(file))
		goto error_after_dir;

	file = debugfs_create_file("edca", S_IRUSR, dir_vif, rwnx_hw, &rwnx_dbgfs_vif_edca_ops);
	if (IS_ERR_OR_NULL(file))
		goto error_after_dir;

	return;

error_after_dir:
	debugfs_remove_recursive(rwnx_debugfs->dir_vif[vif_index]);
	rwnx_debugfs->dir_vif[vif_index] = NULL;
error:
	dev_err(rwnx_hw->dev, "Error while registering debug entry for vif %s\n", rwnx_vif->ndev->name);
}

static void _rwnx_dbgfs_unregister_vif(struct rwnx_debugfs *rwnx_debugfs, struct rwnx_vif *rwnx_vif)
{
	debugfs_remove_recursive(rwnx_debugfs->dir_vif[rwnx_vif->drv_vif_index]);

	rwnx_debugfs->dir_vif[rwnx_vif->drv_vif_index] = NULL;

}

void _rwnx_dbgfs_vif_write(struct rwnx_debugfs *rwnx_debugfs, struct rwnx_vif *rwnx_vif)
{
	uint8_t vif_idx = rwnx_vif->drv_vif_index;

	if (rwnx_debugfs->unregistering)
		return;

	if (vif_idx >= (NX_ITF_MAX)) {
		WARN(1, "Invalid vif index %d", vif_idx);
		return;
	}

	if (rwnx_debugfs->dir_vif[vif_idx] == NULL)
		_rwnx_dbgfs_register_vif(rwnx_debugfs, rwnx_vif);
	else
		_rwnx_dbgfs_unregister_vif(rwnx_debugfs, rwnx_vif);
}

void rwnx_dbgfs_register_vif(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif)
{
	_rwnx_dbgfs_vif_write(&rwnx_hw->debugfs, rwnx_vif);
}

void rwnx_dbgfs_unregister_vif(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif)
{
	_rwnx_dbgfs_vif_write(&rwnx_hw->debugfs, rwnx_vif);
}

int rwnx_dbgfs_register(struct rwnx_hw *rwnx_hw, const char *name)
{
#ifdef CONFIG_RWNX_SOFTMAC
	struct dentry *phyd = rwnx_hw->hw->wiphy->debugfsdir;
#else
	struct dentry *phyd = rwnx_hw->wiphy->debugfsdir;
#endif /* CONFIG_RWNX_SOFTMAC */
	struct rwnx_debugfs *rwnx_debugfs = &rwnx_hw->debugfs;
	struct dentry *dir_drv, *dir_diags, *dir_stas, *dir_vifs;

	if (!(dir_drv = debugfs_create_dir(name, phyd)))
		return -ENOMEM;

	rwnx_debugfs->dir = dir_drv;

	if (!(dir_stas = debugfs_create_dir("stations", dir_drv)))
		goto err;

	spin_lock_init(&rwnx_debugfs->umh_lock);
	rwnx_debugfs->dir_stas = dir_stas;
	rwnx_debugfs->unregistering = false;

	if (!(dir_diags = debugfs_create_dir("diags", dir_drv)))
		goto err;

	if (!(dir_vifs = debugfs_create_dir("vifs", dir_drv)))
		goto err;

	rwnx_debugfs->dir_vifs = dir_vifs;

	DEBUGFS_ADD_FILE(obss_bw_ru_filter, dir_drv, S_IRUSR | S_IWUSR);

#ifdef CONFIG_RWNX_FULLMAC
	INIT_WORK(&rwnx_debugfs->sta_work, rwnx_sta_work);
	INIT_LIST_HEAD(&rwnx_debugfs->rc_config_save);
	rwnx_debugfs->sta_idx = RWNX_INVALID_STA;
#endif

	DEBUGFS_ADD_U32(tcp_pacing_shift, dir_drv, &rwnx_hw->tcp_pacing_shift, S_IWUSR | S_IRUSR);
	DEBUGFS_ADD_FILE(stats, dir_drv, S_IWUSR | S_IRUSR);
	DEBUGFS_ADD_FILE(sys_stats, dir_drv, S_IRUSR);
	DEBUGFS_ADD_FILE(log_ctrl, dir_drv, S_IRUSR | S_IWUSR);
	DEBUGFS_ADD_FILE(lmac_log_ctrl, dir_drv, S_IRUSR | S_IWUSR);
	DEBUGFS_ADD_FILE(tdd_slice, dir_drv, S_IWUSR);
	DEBUGFS_ADD_FILE(unmatch_dst_mgmt_filter, dir_drv, S_IRUSR | S_IWUSR);
	DEBUGFS_ADD_FILE(pre_cali, dir_drv, S_IWUSR | S_IRUSR);

#ifdef CONFIG_RWNX_SOFTMAC
	DEBUGFS_ADD_X64(rateidx, dir_drv, &rwnx_hw->debugfs.rateidx);
#endif
	DEBUGFS_ADD_FILE(txq, dir_drv, S_IRUSR);
	DEBUGFS_ADD_FILE(acsinfo, dir_drv, S_IRUSR);
#ifdef CONFIG_RWNX_MUMIMO_TX
	DEBUGFS_ADD_FILE(mu_group, dir_drv, S_IRUSR);
#endif
	DEBUGFS_ADD_FILE(pwr_status, dir_drv, S_IRUSR);

#ifdef CONFIG_RWNX_CSI_CAP
	DEBUGFS_ADD_FILE(csi_cap, dir_drv, S_IRUSR | S_IWUSR);
#endif

#ifdef CONFIG_RWNX_P2P_DEBUGFS
	{
		/* Create a p2p directory */
		struct dentry *dir_p2p;
		if (!(dir_p2p = debugfs_create_dir("p2p", dir_drv)))
			goto err;

		/* Add file allowing to control Opportunistic PS */
		DEBUGFS_ADD_FILE(oppps, dir_p2p, S_IWUSR);
		/* Add file allowing to control Notice of Absence */
		DEBUGFS_ADD_FILE(noa, dir_p2p, S_IWUSR);
	}
#endif /* CONFIG_RWNX_P2P_DEBUGFS */

#ifdef CONFIG_BES_COEX_SUPPORT
	{
		/* Create a coex directory */
		struct dentry *dir_coex;
		if (!(dir_coex = debugfs_create_dir("coex", dir_drv)))
			goto err;

		/* Add file allowing to control tports */
		DEBUGFS_ADD_FILE(tports, dir_coex, S_IWUSR);
		/* Add file allowing to simulate bt transq inerrupt */
		DEBUGFS_ADD_FILE(transq, dir_coex, S_IWUSR);
		/* Add file allowing to set coex mode */
		DEBUGFS_ADD_FILE(mode, dir_coex, S_IWUSR);
		/* Add file allowing to set the ratio between wifi and bt */
		DEBUGFS_ADD_FILE(duration, dir_coex, S_IWUSR);
		/* Add file allowing to start/stop coex schedule */
		DEBUGFS_ADD_FILE(sche, dir_coex, S_IWUSR);
		/* Add file allowing to modify pta_switch flags */
		DEBUGFS_ADD_FILE(pta_switch, dir_coex, S_IWUSR);
		/* Add file allowing to modify pta_switch flags */
		DEBUGFS_ADD_FILE(pta_test, dir_coex, S_IWUSR);
		/* Add file allowing to read the state of coex */
		DEBUGFS_ADD_FILE(coex_state, dir_coex, S_IRUSR);
	}
#endif /* CONFIG_BES_COEX_SUPPORT */

#ifdef CONFIG_RWNX_FW_DUMP
	if (rwnx_dbgfs_register_fw_dump(rwnx_hw, dir_drv, dir_diags))
		goto err;
#endif
	DEBUGFS_ADD_FILE(fw_dbg, dir_diags, S_IWUSR | S_IRUSR);
#if defined(CONFIG_RWNX_PCIE) || defined(CONFIG_BES_IBUS)
	if (!rwnx_fw_trace_init(&rwnx_hw->debugfs.fw_trace, rwnx_ipc_fw_trace_desc_get(rwnx_hw))) {
		DEBUGFS_ADD_FILE(fw_trace, dir_diags, S_IWUSR | S_IRUSR);
		if (rwnx_hw->debugfs.fw_trace.buf.nb_compo)
			DEBUGFS_ADD_FILE(fw_trace_level, dir_diags, S_IWUSR | S_IRUSR);
	} else {
		rwnx_debugfs->fw_trace.buf.data = NULL;
	}
#endif
#ifdef CONFIG_RWNX_RADAR
	{
		struct dentry *dir_radar, *dir_sec;
		if (!(dir_radar = debugfs_create_dir("radar", dir_drv)))
			goto err;

		DEBUGFS_ADD_FILE(pulses_prim, dir_radar, S_IRUSR);
		DEBUGFS_ADD_FILE(detected, dir_radar, S_IRUSR);
		DEBUGFS_ADD_FILE(enable, dir_radar, S_IRUSR);

		if (rwnx_hw->phy.cnt == 2) {
			DEBUGFS_ADD_FILE(pulses_sec, dir_radar, S_IRUSR);

			if (!(dir_sec = debugfs_create_dir("sec", dir_radar)))
				goto err;

			DEBUGFS_ADD_FILE(band, dir_sec, S_IWUSR | S_IRUSR);
			DEBUGFS_ADD_FILE(type, dir_sec, S_IWUSR | S_IRUSR);
			DEBUGFS_ADD_FILE(prim20, dir_sec, S_IWUSR | S_IRUSR);
			DEBUGFS_ADD_FILE(center1, dir_sec, S_IWUSR | S_IRUSR);
			DEBUGFS_ADD_FILE(center2, dir_sec, S_IWUSR | S_IRUSR);
			DEBUGFS_ADD_FILE(set, dir_sec, S_IWUSR | S_IRUSR);
		}
	}
#endif /* CONFIG_RWNX_RADAR */

	return 0;

err:
	rwnx_dbgfs_unregister(rwnx_hw);
	return -ENOMEM;
}

void rwnx_dbgfs_unregister(struct rwnx_hw *rwnx_hw)
{
	struct rwnx_debugfs *rwnx_debugfs = NULL;
#ifdef CONFIG_RWNX_FULLMAC
	struct rwnx_rc_config_save *cfg, *next;
#endif /* CONFIG_RWNX_FULLMAC */

	if (!rwnx_hw || !rwnx_hw->debugfs.dir)
		return;

	rwnx_debugfs = &rwnx_hw->debugfs;
#ifdef CONFIG_RWNX_FULLMAC
	list_for_each_entry_safe(cfg, next, &rwnx_debugfs->rc_config_save, list) {
		list_del(&cfg->list);
		kfree(cfg);
	}
#endif /* CONFIG_RWNX_FULLMAC */

	spin_lock_bh(&rwnx_debugfs->umh_lock);
	rwnx_debugfs->unregistering = true;
	spin_unlock_bh(&rwnx_debugfs->umh_lock);
#ifdef CONFIG_RWNX_FW_DUMP
	rwnx_wait_um_helper(rwnx_hw);
#endif
#ifdef CONFIG_RWNX_FW_TRACE
	rwnx_fw_trace_deinit(&rwnx_hw->debugfs.fw_trace);
#endif
#ifdef CONFIG_RWNX_FULLMAC
	flush_work(&rwnx_debugfs->sta_work);
#endif
	debugfs_remove_recursive(rwnx_hw->debugfs.dir);
	rwnx_hw->debugfs.dir = NULL;
}
