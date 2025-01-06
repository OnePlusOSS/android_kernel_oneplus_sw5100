/*
 * @file bes_data_monitor.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "rwnx_main.h"
#include "bes_data_monitor.h"
#include "bes_log.h"

int bes_data_monitor_register_lp_cb(struct rwnx_hw *rwnx_hw, bool tx, bes_data_monitor_cb cb, void *arg)
{
	int i = 0, ret = 0;
	struct bes_data_monitor_cb_item *item = NULL, *assign = NULL;
	struct bes_data_monitor *data_monitor = NULL;

	/* Check if rwnx_hw is invalid */
	if (!rwnx_hw) {
		ret = -EINVAL;
		goto out;
	}

	/* Get data_monitor pointer */
	data_monitor = (tx) ? (&rwnx_hw->data_tx_monitor) : (&rwnx_hw->data_rx_monitor);
	if (!data_monitor->cb_tbl) {
		ret = -EINVAL;
		goto out;
	}

	spin_lock(&data_monitor->cb_lock);
	/* The maximum value has been reached */
	if (data_monitor->cb_cnt >= DATA_MONITOR_CB_CNT_MAX) {
		spin_unlock(&data_monitor->cb_lock);
		ret = -ERANGE;
		goto out;
	}

	/* Check if cb is already exist */
	for (i = 0; i < DATA_MONITOR_CB_CNT_MAX; i++) {
		item = &data_monitor->cb_tbl[i];

		if (assign == NULL && item->cb == NULL)
			assign = item;

		if (item->cb == cb) {
			spin_unlock(&data_monitor->cb_lock);
			ret = -EEXIST;
			goto out;

		}
	}

	/* Not find the postion to assign */
	if (!assign) {
		spin_unlock(&data_monitor->cb_lock);
		ret = -EFAULT;
		goto out;
	}

	/* Add cb to tbl */
	assign->cb = cb;
	assign->arg = arg;
	data_monitor->cb_cnt++;
	spin_unlock(&data_monitor->cb_lock);

out:
	bes_err_with_cond(ret, BES_DBG_TRAFFIC, "%s ret=%d\n", __func__, ret);
	return ret;
}

void bes_data_monitor_unregister_lp_cb(struct rwnx_hw *rwnx_hw, bool tx, bes_data_monitor_cb cb)
{
	int i = 0;
	struct bes_data_monitor_cb_item *item = NULL;
	struct bes_data_monitor *data_monitor = NULL;

	/* Check if rwnx_hw is invalid */
	if (!rwnx_hw)
		return;

	/* Get data_monitor pointer */
	data_monitor = (tx) ? (&rwnx_hw->data_tx_monitor) : (&rwnx_hw->data_rx_monitor);
	if (!data_monitor->cb_tbl)
		return;

	spin_lock(&data_monitor->cb_lock);
	/* No callback function registed, return directly */
	if (data_monitor->cb_cnt == 0) {
		spin_unlock(&data_monitor->cb_lock);
		return;
	}

	/* Delete cb from tbl */
	for (i = 0; i < DATA_MONITOR_CB_CNT_MAX; i++) {
		item = &data_monitor->cb_tbl[i];
		if (cb != item->cb)
			continue;

		item->cb = NULL;
		item->arg = NULL;
		data_monitor->cb_cnt--;
		break;
	}
	spin_unlock(&data_monitor->cb_lock);
}

static void bes_data_monitor_call_lp_cb(struct rwnx_hw *rwnx_hw, bool tx)
{
	int i = 0;
	struct bes_data_monitor_cb_item *item = NULL;
	struct bes_data_monitor *data_monitor = NULL;

	/* Check if rwnx_hw is invalid */
	if (!rwnx_hw)
		return;

	/* Get data_monitor pointer */
	data_monitor = (tx) ? (&rwnx_hw->data_tx_monitor) : (&rwnx_hw->data_rx_monitor);
	if (!data_monitor->cb_tbl) {
		return;
	}

	spin_lock(&data_monitor->cb_lock);
	/* No callback function registed, return directly */
	if (data_monitor->cb_cnt == 0) {
		spin_unlock(&data_monitor->cb_lock);
		return;
	}

	/* Traverse and execute callback functions */
	for (i = 0; i < DATA_MONITOR_CB_CNT_MAX; i++) {
		item = &data_monitor->cb_tbl[i];
		if (item->cb != NULL)
			item->cb(item->arg);
	}
	spin_unlock(&data_monitor->cb_lock);
}

void bes_data_monitor_rate_calc(struct bes_traffic_info *data_rate, u32 cur_pkts, u64 cur_bytes,
				unsigned long last_time, unsigned long cur_time)
{
	u32 last_pkts = 0;
	u64 last_bytes = 0;

	/* Get last pkts and bytes, and update it for next process */
	last_pkts = data_rate->last_pkts;
	last_bytes = data_rate->last_bytes;
	data_rate->last_pkts = cur_pkts;
	data_rate->last_bytes = cur_bytes;

	/*  Calcalate delta value */
	data_rate->delta_pkts = cur_pkts - last_pkts;
	data_rate->delta_bytes = cur_bytes - last_bytes;

	/* If it has not been set before, won't calculate it this time */
	if (!data_rate->set) {
		data_rate->set = true;
		return;
	}

	/* calculate rate */
	data_rate->pkts_rate = div_u64(((u64)(data_rate->delta_pkts) * 1000) ,
				       (jiffies_to_msecs(cur_time - last_time) + 1));
	data_rate->bytes_rate = div_u64(data_rate->delta_bytes * 1000,
					(jiffies_to_msecs(cur_time - last_time) + 1));
}

static void bes_data_monitor_update_rate(struct rwnx_hw *rwnx_hw, bool tx)
{
	u8 i = 0;
	struct rwnx_vif *rwnx_vif = NULL;
	struct rwnx_sta *cur_sta = NULL;
	struct bes_data_monitor *data_monitor = (tx) ? (&rwnx_hw->data_tx_monitor) :
						       (&rwnx_hw->data_rx_monitor);
	struct bes_traffic_info *data_rate = NULL;
	unsigned long last_time = data_monitor->last_time, cur_time = jiffies;
	u32 cur_pkts = 0;
	u64 cur_bytes = 0;

	bes_dbg(BES_DBG_TRAFFIC, ">> %s(): Data rate calculate for [%s]\n", __func__, tx ? "TX" : "RX");

	/* Data rate for sta */
	for (i = 0; i < NX_REMOTE_STA_MAX; i++) {
		/* sta should be valid */
		cur_sta = &rwnx_hw->sta_table[i];
		if (!cur_sta->valid)
			continue;

		/* vif is connected  */
		rwnx_vif = rwnx_rx_get_vif(rwnx_hw, cur_sta->vif_idx);
		if (!rwnx_vif ||
		    !netif_running(rwnx_vif->ndev) ||
		    !netif_carrier_ok(rwnx_vif->ndev))
			continue;

		/* Get current pkts and bytes, and update it for vif */
		cur_pkts = (tx) ? (cur_sta->stats.tx_pkts) : (cur_sta->stats.rx_pkts);
		cur_bytes = (tx) ? (cur_sta->stats.tx_bytes) : (cur_sta->stats.rx_bytes);

		/* Get current structure for data rate calculation, and  */
		data_rate = (tx) ? (&cur_sta->stats.tx_traffic_info) : (&cur_sta->stats.rx_traffic_info);

		/* Calculate rate */
		bes_data_monitor_rate_calc(data_rate, cur_pkts, cur_bytes, last_time, cur_time);

		bes_dbg(BES_DBG_TRAFFIC, "%s sta_data_rate(%d): pkts_rate=%d bytes_rate=%Lu\n",
			__func__, i, data_rate->pkts_rate, data_rate->bytes_rate);
	}
}

void bes_data_monitor_sample(struct rwnx_hw *rwnx_hw, bool tx)
{
	struct bes_data_monitor *data_monitor = NULL;

	/* Check if rwnx_hw is invalid */
	if (!rwnx_hw)
		return;

	/* Get data_monitor pointer */
	data_monitor = (tx) ? (&rwnx_hw->data_tx_monitor) : (&rwnx_hw->data_rx_monitor);

	/* Starting data monitor sampling if it is not start */
	if (!data_monitor->start) {
		data_monitor->start = true;
		data_monitor->last_time = jiffies;

		return;
	}

	/* If the data monitor sampling cycle is arrived, execute callback function */
	if (time_after(jiffies, data_monitor->last_time +
				msecs_to_jiffies(DATA_MONITOR_SAMPLE_CYCLE))) {
		// update rate value
		bes_data_monitor_update_rate(rwnx_hw, tx);
		data_monitor->last_time = jiffies;

		// execute callback functions
		bes_data_monitor_call_lp_cb(rwnx_hw, tx);
	}
}

int bes_data_monitor_init(struct rwnx_hw *rwnx_hw)
{
	// data tx monitor init
	rwnx_hw->data_tx_monitor.start = false;
	rwnx_hw->data_tx_monitor.last_time = 0;
	spin_lock_init(&rwnx_hw->data_tx_monitor.cb_lock);
	rwnx_hw->data_tx_monitor.cb_cnt = 0;
	rwnx_hw->data_tx_monitor.cb_tbl = kzalloc(sizeof(struct bes_data_monitor_cb_item) * DATA_MONITOR_CB_CNT_MAX, GFP_KERNEL);
	if (!rwnx_hw->data_tx_monitor.cb_tbl) {
		bes_err(BES_DBG_TRAFFIC, "%s: data_tx_monitor.cb_tbl allocation failed\n", __func__);
		goto tx_alloc_err;
	}

	// data rx monitor init
	rwnx_hw->data_rx_monitor.start = false;
	rwnx_hw->data_rx_monitor.last_time = 0;
	spin_lock_init(&rwnx_hw->data_rx_monitor.cb_lock);
	rwnx_hw->data_rx_monitor.cb_cnt = 0;
	rwnx_hw->data_rx_monitor.cb_tbl = kzalloc(sizeof(struct bes_data_monitor_cb_item) * DATA_MONITOR_CB_CNT_MAX, GFP_KERNEL);
	if (!rwnx_hw->data_rx_monitor.cb_tbl) {
		bes_err(BES_DBG_TRAFFIC, "%s: data_rx_monitor.cb_tbl allocation failed\n", __func__);
		goto rx_alloc_err;
	}

	return 0;

rx_alloc_err:
	kfree(rwnx_hw->data_tx_monitor.cb_tbl);
	rwnx_hw->data_tx_monitor.cb_tbl = NULL;
tx_alloc_err:
	return -ENOMEM;
}

void bes_data_monitor_deinit(struct rwnx_hw *rwnx_hw)
{
	// data tx monitor init
	rwnx_hw->data_tx_monitor.start = false;
	rwnx_hw->data_tx_monitor.last_time = 0;
	rwnx_hw->data_tx_monitor.cb_cnt = 0;
	if (rwnx_hw->data_tx_monitor.cb_tbl) {
		kfree(rwnx_hw->data_tx_monitor.cb_tbl);
		rwnx_hw->data_tx_monitor.cb_tbl = NULL;
	}

	// data rx monitor init
	rwnx_hw->data_rx_monitor.start = false;
	rwnx_hw->data_rx_monitor.last_time = 0;
	rwnx_hw->data_rx_monitor.cb_cnt = 0;
	if (rwnx_hw->data_rx_monitor.cb_tbl) {
		kfree(rwnx_hw->data_rx_monitor.cb_tbl);
		rwnx_hw->data_rx_monitor.cb_tbl = NULL;
	}
}
