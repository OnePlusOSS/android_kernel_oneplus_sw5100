/*
 * @file bes_sleep_manager.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __BES_SLEEP_MANAGER_H__
#define __BES_SLEEP_MANAGER_H__

struct lp_bcn_lost_thres_set_req {
	struct mm_custom_req_hdr hdr;
	u8 thres;
};

struct lp_null_keepalive_set_req {
	struct mm_custom_req_hdr hdr;
	u8 vif_idx;
	u16 period;
};

void bes_sleep_manager_ctrl_beacon_filter(struct rwnx_hw *rwnx_hw, bool enable_filter);
void bes_sleep_manager_init(struct rwnx_hw *rwnx_hw);
void bes_sleep_manager_deinit(struct rwnx_hw *rwnx_hw);

#endif /* __BES_SLEEP_MANAGER_H__ */