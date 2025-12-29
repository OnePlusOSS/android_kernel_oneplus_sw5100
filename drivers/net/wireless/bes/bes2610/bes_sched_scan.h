/*
 * @file bes_sched_scan.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BES_SCHED_SCAN_
#define _BES_SCHED_SCAN_

#include "rwnx_defs.h"

#define BES_SCHED_SCAN_RESULT_WAKELOCK_TIME 3000

enum sched_scan_ops {
	SCHED_SCAN_OPS_SET_MATCH_SETS = 0,
	SCHED_SCAN_OPS_SET_PLANS,
	SCHED_SCAN_OPS_SET_CHANNELS,
	SCHED_SCAN_OPS_SET_NORMAL_CFG,
	SCHED_SCAN_OPS_START,
	SCHED_SCAN_OPS_STOP,
};

struct sched_scan_match_set {
	/// SSID to be matched; may be zero-length in case of BSSID match
	/// or no match (RSSI only)
	struct mac_ssid ssid;
	/// BSSID to be matched; may be all-zero BSSID in case of SSID match
	/// or no match (RSSI only)
	uint8_t bssid[MAC_ADDR_LEN];
	/// Don't report scan results below this threshold (in s32 dBm)
	int8_t rssi_thold;
};

struct sched_scan_plan {
	/// Number of scan iterations in this scan plan. Zero means
	/// infinite loop.
	/// The last scan plan will always have this parameter set to zero,
	/// all other scan plans will have a finite number of iterations.
	uint32_t iterations:8;
	/// Interval between scheduled scan iterations. In seconds.
	uint32_t interval:24;
};

struct sched_scan_match_sets_param {
	/// Number of match sets
	int n_match_sets;
	/// Sets of parameters to be matched for a scan result
	/// entry to be considered valid and to be passed to the host
	/// (others are filtered out).
	/// If ommited, all results are passed.
	struct sched_scan_match_set match_sets[SCHED_SCANU_MATCH_SETS_NUM_LIMIT];
};

struct sched_scan_plans_param {
	/// Delay in seconds to use before starting the first scan
	/// cycle.  The driver may ignore this parameter and start
	/// immediately (or at any other time), if this feature is not
	/// supported.
	uint32_t delay;
	/// Number of scan plans, at least 1.
	int n_scan_plans;
	/// Scan plans to be executed in this scheduled scan. Lowest
	/// index must be executed first.
	struct sched_scan_plan scan_plans[SCHED_SCANU_SCHEME_NUM_LIMIT];
};

struct sched_scan_channels_param {
	/// Total number of channels to scan. 0 means full channel scanning.
	uint8_t n_channels;
	/// Channels to scan on.
	struct mac_chan_def channels[SCHED_SCANU_MATCH_SETS_NUM_LIMIT];
};

struct bss_select_adjust
{
    /// band of BSS which should match for RSSI level adjustment.
    uint8_t band;
    /// value of RSSI level adjustment.
    int8_t delta;
};

struct sched_scan_normal_cfgs_param {
	/// Number of SSIDs
	int n_ssids;
	/// SSIDs to scan for (passed in the probe_reqs in active scans)
	struct mac_ssid ssids[SCHED_SCANU_SSIDS_NUM_LIMIT];
	/// Indicates whether @relative_rssi is set or not.
	bool relative_rssi_set;
	/// Relative RSSI threshold in dB to restrict scan result
	/// reporting in connected state to cases where a matching BSS is determined
	/// to have better or slightly worse RSSI than the current connected BSS.
	/// The relative RSSI threshold values are ignored in disconnected state.
	int8_t relative_rssi;
	/// Delta dB of RSSI preference to be given to the BSSs that belong
	/// to the specified band while deciding whether a better BSS is reported
	/// using @relative_rssi. If delta is a negative number, the BSSs that
	/// belong to the specified band will be penalized by delta dB in relative
	/// comparisions.
	struct bss_select_adjust rssi_adjust;
};

struct mm_sched_scan_set_match_sets {
	/// Header of custom request
	struct mm_custom_req_hdr hdr;
	/// Index of vif
	uint8_t vif_idx;
	/// Identifies this request.
	uint64_t reqid;
	/// Parameter of custom request
	struct sched_scan_match_sets_param param;
};

struct mm_sched_scan_set_plans {
	/// Header of custom request
	struct mm_custom_req_hdr hdr;
	/// Index of vif
	uint8_t vif_idx;
	/// Identifies this request.
	uint64_t reqid;
	/// Parameter of custom request
	struct sched_scan_plans_param param;
};

struct mm_sched_scan_set_channels {
	/// Header of custom request
	struct mm_custom_req_hdr hdr;
	/// Index of vif
	uint8_t vif_idx;
	/// Identifies this request.
	uint64_t reqid;
	/// Parameter of custom request
	struct sched_scan_channels_param param;
};

struct mm_sched_scan_set_normal_cfgs {
	/// Header of custom request
	struct mm_custom_req_hdr hdr;
	/// Index of vif
	uint8_t vif_idx;
	/// Identifies this request.
	uint64_t reqid;
	/// Parameter of custom request
	struct sched_scan_normal_cfgs_param param;
};

struct mm_sched_scan_start {
	/// Header of custom request
	struct mm_custom_req_hdr hdr;
	/// Index of vif
	uint8_t vif_idx;
	/// Identifies this request.
	uint64_t reqid;
};

struct mm_sched_scan_stop {
	/// Header of custom request
	struct mm_custom_req_hdr hdr;
	/// Index of vif
	uint8_t vif_idx;
	/// Identifies this request.
	uint64_t reqid;
};

#ifdef CONFIG_SCHED_SCAN
void bes_sched_scan_init(struct rwnx_hw *rwnx_hw);
void bes_sched_scan_deinit(struct rwnx_hw *rwnx_hw);
void bes_sched_scan_cfg_show(struct rwnx_hw *rwnx_hw, u8 vif_idx, struct cfg80211_sched_scan_request *request);
int bes_sched_scan_start(struct rwnx_hw *rwnx_hw, u8 vif_idx, struct cfg80211_sched_scan_request *request);
int bes_sched_scan_stop(struct rwnx_hw *rwnx_hw, u8 vif_idx, u64 reqid);
int bes_sched_scan_result_ind(struct rwnx_hw *rwnx_hw, struct sched_scan_result_ind *ind);
#else
static inline void bes_sched_scan_init(struct rwnx_hw *rwnx_hw) {}
static inline void bes_sched_scan_deinit(struct rwnx_hw *rwnx_hw) {}
static inline void bes_sched_scan_cfg_show(struct rwnx_hw *rwnx_hw, u8 vif_idx, struct cfg80211_sched_scan_request *request) {}
static inline int bes_sched_scan_start(struct rwnx_hw *rwnx_hw, u8 vif_idx, struct cfg80211_sched_scan_request *request) { return 0; }
static inline int bes_sched_scan_stop(struct rwnx_hw *rwnx_hw, u8 vif_idx, u64 reqid) { return 0; }
static inline int bes_sched_scan_result_ind(struct rwnx_hw *rwnx_hw, struct sched_scan_result_ind *ind) { return 0; }
#endif

#endif /* _BES_SCHED_SCAN_ */