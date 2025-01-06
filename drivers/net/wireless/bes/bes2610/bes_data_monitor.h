/*
 * @file bes_data_monitor.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BES_DATA_MONITOR_
#define _BES_DATA_MONITOR_

/* Data monitor sampling cycle(ms) */
#define DATA_MONITOR_SAMPLE_CYCLE	1000

/* Max count for data monitor callback function */
#define DATA_MONITOR_CB_CNT_MAX		5

/*
 * Data type for data monitor callback
 * Note: This callback functions cannot have blocking operations
 */
typedef void (*bes_data_monitor_cb)(void *arg);

/* Data type for data monitor callback item */
struct bes_data_monitor_cb_item {
	void *arg;
	bes_data_monitor_cb cb;
};

int bes_data_monitor_init(struct rwnx_hw *rwnx_hw);
void bes_data_monitor_deinit(struct rwnx_hw *rwnx_hw);
void bes_data_monitor_rate_calc(struct bes_traffic_info *data_rate, u32 cur_pkts, u64 cur_bytes,
				unsigned long last_time, unsigned long cur_time);
void bes_data_monitor_sample(struct rwnx_hw *rwnx_hw, bool tx);
int bes_data_monitor_register_lp_cb(struct rwnx_hw *rwnx_hw, bool tx, bes_data_monitor_cb cb, void *arg);
void bes_data_monitor_unregister_lp_cb(struct rwnx_hw *rwnx_hw, bool tx, bes_data_monitor_cb cb);

#endif /* _BES_DATA_MONITOR_ */