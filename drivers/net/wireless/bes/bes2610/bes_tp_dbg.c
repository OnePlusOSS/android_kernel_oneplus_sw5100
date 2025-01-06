/*
 * @file bes_tp_dbg.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "rwnx_main.h"
#include "bes_log.h"
#include "bes_data_monitor.h"
#include "rwnx_msg_tx.h"
#include "rwnx_debugfs.h"
#include "bes_linkinfo.h"
#include "bes_vif_info.h"
#include "bes_tp_dbg.h"
#include <linux/string.h>

#define BES_TP_DBG_PRINT_BUF_SIZE		1024
#define BES_TP_DBG_BYTES_RATE_BUF_SIZE		16

#define BES_TP_DBG_TXRX_RATE_STATS_NUM 		5

static void bes_tp_dbg_dump_buf(char *buf)
{
	char *token = strsep(&buf, "\n");

	while (token != NULL) {
		bes_dbg(BES_DBG_TP_DBG, "%s\n", token);
		token = strsep(&buf, "\n");
	}
}

static int bes_tp_dbg_get_fw_info(struct rwnx_hw *rwnx_hw, struct tp_fw_info_get_cfm *cfm)
{
	struct tp_fw_info_get_req req;

	req.hdr.id = CUSTOM_ID_FW_TP_DBG;
	req.hdr.length = sizeof(struct tp_fw_info_get_req);

	return rwnx_send_custom_request(rwnx_hw, &req.hdr, cfm, sizeof(struct tp_fw_info_get_cfm));
}

static void bes_tp_dbg_dump(void *arg)
{
	struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)arg;

	queue_work(rwnx_hw->rwnx_wq, &rwnx_hw->tp_dbg.work);
}

static void bes_tp_dbg_dump_coex_state(struct rwnx_hw *rwnx_hw, char *buf, u32 max_size)
{
	u8 i = 0;
	int ret = 0, len = 0;
	u32 wifi_dur = 0, bt_dur = 0;
	struct tp_fw_info_get_cfm fw_info;
	u32 total_time = 0;
	u8 coex_mode_prop[COEX_MODE_CONFIG_UNVILD] = {0};

	/* Reset the print buf contents */
	memset(buf, 0, max_size);

	/* Get the information of coex bt state */
	len = bes_coex_bt_state_info(buf, max_size, rwnx_hw->coex.bt_coex_state);
	if (len)
		bes_tp_dbg_dump_buf(buf);

	/* Calculate duration info */
	bt_dur = bes_coex_bt_calc_time();
	wifi_dur = BES_COEX_PERIOD - bt_dur;
	bes_dbg(BES_DBG_TP_DBG, "Coex duration, wifi:%d bt:%d\n", wifi_dur, bt_dur);

	/* Get the throughput information from LMAC */
	ret = bes_tp_dbg_get_fw_info(rwnx_hw, &fw_info);
	if (ret) {
		bes_err(BES_DBG_TP_DBG, "%s Fail to get fw information\n", __func__);
		return;
	}

	/* Calculate proportion of coex mode */
	for (i = 0; i < COEX_MODE_CONFIG_UNVILD; i++)
		total_time += fw_info.coex_tp_info.prop_calc[i].total_time;
	if (total_time != 0) {
		for (i = 0; i < COEX_MODE_CONFIG_UNVILD; i++)
			coex_mode_prop[i] = (u8)div_u64(fw_info.coex_tp_info.prop_calc[i].total_time * 100, total_time);
	}
	bes_dbg(BES_DBG_TP_DBG, "Coex proportion, block_bt=%d%% unblock_bt:%d%% fdd:%d%%. Toatl time:%u\n",
		coex_mode_prop[COEX_MODE_CONFIG_BLOCKBT], coex_mode_prop[COEX_MODE_CONFIG_UNBLOCKBT],
		coex_mode_prop[COEX_MODE_CONFIG_FDD], total_time);

	/* Print the situation of tansq and host rx buf*/
	bes_dbg(BES_DBG_TP_DBG, "Tansq count, sucess=%u fail=%u. hostrxbuf unavail cnt:%u\n",
		fw_info.coex_tp_info.transq_stats.sucess, fw_info.coex_tp_info.transq_stats.fail,
		fw_info.hostrxbuf_unavail);
}

static void bes_tp_dbg_dump_ipc_delay_stats(struct rwnx_hw *rwnx_hw)
{
	int ret = 0;
	struct mcu_get_delay_stats_info_cfm cfm;

	/* Get the information of bus/ipc delay */
	ret = rwnx_send_get_delay_stats_info_req(rwnx_hw,  &cfm);
	if (ret) {
		bes_err(BES_DBG_TP_DBG, "%s Fail to get mcu information\n", __func__);
		return;
	}
	bes_dbg(BES_DBG_TP_DBG, "Bus delay(ticks), max:%u min:%u avg:%u. Ipc delay(ticks), max:%u min:%u avg:%u\n",
		cfm.bus_delay_stats.max, cfm.bus_delay_stats.min, cfm.bus_delay_stats.avg,
		cfm.ipc_delay_stats.max, cfm.ipc_delay_stats.min, cfm.ipc_delay_stats.avg);
}

static void bes_tp_dbg_dump_tx_rx_rate(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta, char *buf, u32 max_size)
{
	int len = 0;
	int rate_sort[BES_TP_DBG_TXRX_RATE_STATS_NUM] = {-1};

	/* Get the statistics of tx rate */
	len = bes_vif_info_tx_rate_dump(rwnx_hw, sta, buf, max_size, rate_sort, BES_TP_DBG_TXRX_RATE_STATS_NUM);
	if (len)
		bes_tp_dbg_dump_buf(buf);

	/* Reset the statistics of tx rate */
	spin_lock_bh(&rwnx_hw->cb_lock);
	bes_sta_tx_rate_stat_reset(sta);
	spin_unlock_bh(&rwnx_hw->cb_lock);

	/* Reset rate_sort buffer */
	memset(rate_sort, -1, BES_TP_DBG_TXRX_RATE_STATS_NUM);

	/* Get the statistics of rx rate */
	len = bes_vif_info_rx_rate_dump(rwnx_hw, sta, buf, max_size, rate_sort, BES_TP_DBG_TXRX_RATE_STATS_NUM);
	if (len)
		bes_tp_dbg_dump_buf(buf);

	/* Reset the statistics of rx rate */
	spin_lock_bh(&rwnx_hw->cb_lock);
	bes_sta_rx_rate_stat_reset(sta);
	spin_unlock_bh(&rwnx_hw->cb_lock);
}

static void bes_tp_dbg_print_rate(char* buf, int max_size, uint64_t rate)
{
	const char *units[] = {"B/s", "kB/s", "MB/s", "GB/s"};
	const u32 level[] = {0, 0x3FF, 0xFFFFF, 0X3FFFFFFF};
	const u8 units_size = ARRAY_SIZE(units);
	int i = 0;
	uint64_t integer = 0, fraction = 0;

	for (i = units_size - 1; i > 0; i--) {
		if (rate & (~level[i]))
			break;
	}

	integer = (rate & (~level[i])) >> (10 * i);
	if (level[i])
		fraction = div_u64((rate & level[i]) * 100, level[i]);

	scnprintf(buf, max_size, "%Lu.%02Lu%s", integer, fraction, units[i]);
}

static void bes_tp_dbg_dump_tp_tx_rx_rate(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta)
{
	char tx_bytes_rate_buf[BES_TP_DBG_BYTES_RATE_BUF_SIZE] = { 0 };
	char rx_bytes_rate_buf[BES_TP_DBG_BYTES_RATE_BUF_SIZE] = { 0 };
	struct bes_tp_tx_rx_info *tx_rx_info = &rwnx_hw->tp_dbg.tx_rx_info[sta->sta_idx];
	unsigned long cur_time = jiffies;

	/* Just record start time at first */
	if (!tx_rx_info->start) {
		tx_rx_info->start = true;
		tx_rx_info->last_time = cur_time;
		memcpy(&tx_rx_info->last_tx_fail_stats, &sta->stats.tx_fail_stats, sizeof(struct rwnx_tx_fail_stats));
		tx_rx_info->last_tx_cont_fail_pkts = sta->stats.tx_cont_fail_pkts;
		tx_rx_info->last_drop_tx = sta->stats.drop_tx;
		return;
	}

	/* Calculate the throughput rx tx traffic results and print it */
	bes_data_monitor_rate_calc(&tx_rx_info->tx_traffic, sta->stats.tx_pkts, sta->stats.tx_bytes,
				   tx_rx_info->last_time, cur_time);
	bes_data_monitor_rate_calc(&tx_rx_info->rx_traffic, sta->stats.rx_pkts, sta->stats.rx_bytes,
				   tx_rx_info->last_time, cur_time);
	tx_rx_info->last_time = cur_time;
	bes_tp_dbg_print_rate(tx_bytes_rate_buf, BES_TP_DBG_BYTES_RATE_BUF_SIZE, tx_rx_info->tx_traffic.bytes_rate);
	bes_tp_dbg_print_rate(rx_bytes_rate_buf, BES_TP_DBG_BYTES_RATE_BUF_SIZE, tx_rx_info->rx_traffic.bytes_rate);
	bes_dbg(BES_DBG_TP_DBG, "Packets rate, tx:%u(%u pkt/s) rx:%u(%u pkt/s). Bytes rate, tx:%s rx:%s\n",
		tx_rx_info->tx_traffic.delta_pkts, tx_rx_info->tx_traffic.pkts_rate,
		tx_rx_info->rx_traffic.delta_pkts, tx_rx_info->rx_traffic.pkts_rate,
		tx_bytes_rate_buf, rx_bytes_rate_buf);

	/* Print tx fail information and update it */
	bes_dbg(BES_DBG_TP_DBG, " Tx Fail(Over Retry:%u packets Life Timeout:%u packets)\n",
		sta->stats.tx_fail_stats.over_retry_pkts - tx_rx_info->last_tx_fail_stats.over_retry_pkts,
		sta->stats.tx_fail_stats.life_timeout_pkts - tx_rx_info->last_tx_fail_stats.life_timeout_pkts);
	bes_dbg(BES_DBG_TP_DBG, " TX Dropped: %u packets", sta->stats.drop_tx - tx_rx_info->last_drop_tx);
	memcpy(&tx_rx_info->last_tx_fail_stats, &sta->stats.tx_fail_stats, sizeof(struct rwnx_tx_fail_stats));
	tx_rx_info->last_tx_cont_fail_pkts = sta->stats.tx_cont_fail_pkts;
	tx_rx_info->last_drop_tx = sta->stats.drop_tx;
}

static void bes_tp_dbg_dump_tx_delay_stats(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta)
{
	int ret = 0;
	u32 cca_busy_ratio = U32_MAX;
	u32 tx_delay = U32_MAX;
	u32 ul_delay = 0;

	/* Get the value of tx delay */
	ret = bes_get_tx_delay_stat(rwnx_hw, sta, &tx_delay);
	if (ret) {
		bes_err(BES_DBG_TP_DBG, "%s Fail to get tx delay value\n", __func__);
		return;
	}

	/* Get the value of CCA busy ratio */
	ret = bes_vif_info_get_chan_load(rwnx_hw, sta->vif_idx, &cca_busy_ratio);
	if (ret) {
		bes_err(BES_DBG_TP_DBG, "%s Fail to get CCA busy ratio value\n", __func__);
		goto out;
	}

	/* Get the value of uplink delay */
	ul_delay = bes_tx_uldelay_get(sta);

out:
	bes_dbg(BES_DBG_TP_DBG, "Tx delay, %u us. CCA busy ratio, %u.%u%%. Uplink Delay, %u us\n",
		tx_delay, (u32)div_u64(cca_busy_ratio, 10), cca_busy_ratio % 10, ul_delay);
}

static void bes_tp_dbg_dump_defer_work(struct work_struct *work)
{
	struct rwnx_hw *rwnx_hw = container_of(work, struct rwnx_hw, tp_dbg.work);
	char *buf = NULL;
	u8 sta_idx = 0;
	struct rwnx_sta *sta = NULL;
	struct rwnx_vif *vif = NULL;

	/* Check dbg_lvl */
	if (bes_get_dbg_lvl(BES_DBG_TP_DBG) < BES_LOG_DBG ||
	    bes_get_dbg_lvl(BES_DBG_TP_DBG) == BES_LOG_NONE)
		return;

	/* Retrurn if supporting no features */
	if (!rwnx_hw->tp_dbg.feature)
		return;

	/* Malloc print buffer */
	buf = vmalloc(BES_TP_DBG_PRINT_BUF_SIZE);
	if (!buf) {
		bes_err(BES_DBG_TP_DBG, "%s Fail to vmalloc memory\n", __func__);
		return;
	}

	/* Dump the coex information */
	if (rwnx_hw->tp_dbg.feature &
	    BIT(BES_TP_DBG_FEATURE_TYPR_COEX_STATE))
		bes_tp_dbg_dump_coex_state(rwnx_hw, buf, BES_TP_DBG_PRINT_BUF_SIZE);

	/* Dump the delay stats */
	if (rwnx_hw->tp_dbg.feature &
	    BIT(BES_TP_DBG_FEATURE_TYPR_IPC_DELAY_STATS))
		bes_tp_dbg_dump_ipc_delay_stats(rwnx_hw);

	for (sta_idx = 0; sta_idx < NX_REMOTE_STA_MAX; sta_idx++) {
		/* Sta should be valid */
		sta = rwnx_rx_get_sta(rwnx_hw, sta_idx);
		if (!sta || !sta->valid)
			continue;

		/* Related vif should be connected */
		vif = rwnx_rx_get_vif(rwnx_hw, sta->vif_idx);
		if (!vif || !netif_running(vif->ndev) ||
		    !netif_carrier_ok(vif->ndev))
			continue;

		bes_dbg(BES_DBG_TP_DBG, "Dump the throughput situation of STA-%d(corresponding to VIF-%d)\n",
			sta_idx, sta->vif_idx);

		/* Dump the throughput tx/rx rate information */
		if (rwnx_hw->tp_dbg.feature &
		    BIT(BES_TP_DBG_FEATURE_TYPR_TP_TX_RX_RATE))
			bes_tp_dbg_dump_tp_tx_rx_rate(rwnx_hw, sta);

		/* Dump the tx/rx rate information */
		if (rwnx_hw->tp_dbg.feature &
		    BIT(BES_TP_DBG_FEATURE_TYPR_TX_RX_RATE))
			bes_tp_dbg_dump_tx_rx_rate(rwnx_hw, sta, buf, BES_TP_DBG_PRINT_BUF_SIZE);

		/* Dump the tx/rx delay information */
		if (rwnx_hw->tp_dbg.feature &
		    BIT(BES_TP_DBG_FEATURE_TYPR_TX_DELAY_STATS))
			bes_tp_dbg_dump_tx_delay_stats(rwnx_hw, sta);
	}

	/* Free print buffer */
	vfree(buf);
}

void bes_tp_dbg_reset_tx_rx_info(struct rwnx_hw *rwnx_hw, struct rwnx_sta *rwnx_sta)
{
	if (!rwnx_hw || !rwnx_sta || rwnx_sta->sta_idx >= NX_REMOTE_STA_MAX)
		return;

	memset(&rwnx_hw->tp_dbg.tx_rx_info[rwnx_sta->sta_idx], 0, sizeof(struct bes_tp_tx_rx_info));
}

void bes_tp_dbg_init(struct rwnx_hw *rwnx_hw)
{
	const int size = sizeof(struct bes_tp_tx_rx_info) * NX_REMOTE_STA_MAX;

	rwnx_hw->tp_dbg.feature = CONFIG_BES_TP_DBG_FEATURE_SUPPORT;
	memset(&rwnx_hw->tp_dbg.tx_rx_info, 0, size);
	INIT_WORK(&rwnx_hw->tp_dbg.work, bes_tp_dbg_dump_defer_work);
	bes_data_monitor_register_lp_cb(rwnx_hw, false, bes_tp_dbg_dump, rwnx_hw);
}

void bes_tp_dbg_deinit(struct rwnx_hw *rwnx_hw)
{
	const int size = sizeof(struct bes_tp_tx_rx_info) * NX_REMOTE_STA_MAX;

	bes_data_monitor_unregister_lp_cb(rwnx_hw, false, bes_tp_dbg_dump);
	cancel_work_sync(&rwnx_hw->tp_dbg.work);
	rwnx_hw->tp_dbg.feature = 0;
	memset(&rwnx_hw->tp_dbg.tx_rx_info, 0, size);
}