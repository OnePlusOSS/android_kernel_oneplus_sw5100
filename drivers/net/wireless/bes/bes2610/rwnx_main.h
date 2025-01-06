/**
 ******************************************************************************
 *
 * @file rwnx_main.h
 *
 * Copyright (C) RivieraWaves 2012-2021
 *
 ******************************************************************************
 */

#ifndef _RWNX_MAIN_H_
#define _RWNX_MAIN_H_

#include "rwnx_defs.h"
#include "bes_log.h"

#define PHY_OP_WAIT_TIMEOUT_IN_MS 1000

int rwnx_cfg80211_preinit(struct rwnx_plat *rwnx_plat);
int rwnx_cfg80211_init(struct rwnx_plat *rwnx_plat, void **platform_data);
void rwnx_cfg80211_deinit(struct rwnx_hw *rwnx_hw);
void rwnx_cfg80211_post_deinit(struct rwnx_plat *rwnx_plat);
int rwnx_cfg80211_remain_on_channel(struct wiphy *wiphy, struct wireless_dev *wdev,
					   struct ieee80211_channel *chan, unsigned int duration, u64 *cookie);
void rwnx_reset_sta_stats(struct rwnx_sta *sta);

bool rwnx_wlan_enabled(struct rwnx_hw *rwnx_hw);

static inline void rwnx_wf_status_set(struct rwnx_hw *rwnx_hw, enum rwnx_dev_flag flag)
{
	set_bit(flag, &rwnx_hw->flags);
	if (wq_has_sleeper(&rwnx_hw->wf_conf_wait_q)) {
		wake_up(&rwnx_hw->wf_conf_wait_q);
	}
}

static inline void rwnx_wf_status_clear(struct rwnx_hw *rwnx_hw, enum rwnx_dev_flag flag)
{
	clear_bit(flag, &rwnx_hw->flags);
	if (wq_has_sleeper(&rwnx_hw->wf_conf_wait_q)) {
		wake_up(&rwnx_hw->wf_conf_wait_q);
	}
}

static inline bool rwnx_wf_status_test_and_clear(struct rwnx_hw *rwnx_hw, enum rwnx_dev_flag flag)
{
	bool ret = 0;

	ret = test_and_clear_bit(flag, &rwnx_hw->flags);
	if (wq_has_sleeper(&rwnx_hw->wf_conf_wait_q)) {
		wake_up(&rwnx_hw->wf_conf_wait_q);
	}

	return ret;
}

static inline bool rwnx_wf_status_test_and_wait(struct rwnx_hw *rwnx_hw, enum rwnx_dev_flag flag,
		bool in_state, u32 timeout)
{
	int ret = wait_event_interruptible_timeout(rwnx_hw->wf_conf_wait_q,
			in_state ? test_bit(flag, &rwnx_hw->flags) : !test_bit(flag, &rwnx_hw->flags),
			timeout);
	if (ret > 0) {
		return true;
	} else if (ret == 0) {
		bes_err(BES_DBG_LINK, "Timed out waiting for wf status:%d %s\n",
				flag, in_state ? "in_state" : "not_in_state");
	} else {
		bes_err(BES_DBG_LINK, "Interrupted while waiting forwf status:%d %s\n",
				flag, in_state ? "in_state" : "not_in_state");
	}

	return false;
}

#endif /* _RWNX_MAIN_H_ */
