#include <net/rtnetlink.h>
#include "bes_psa.h"
#include "bes_log.h"
#include "rwnx_strs.h"
#include "rwnx_main.h"
#include "rwnx_msg_tx.h"
#include "bes_vif_info.h"
#include "bes_sched_scan.h"

static bool bes_sched_scan_skip_connectd_ap(struct rwnx_hw *rwnx_hw, u8 vif_idx, struct cfg80211_match_set *match_set)
{
	const uint8_t zero_mac[MAC_ADDR_LEN] = {0};
	struct rwnx_vif *rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);
	struct rwnx_sta *sta = NULL;

	if (!rwnx_vif) {
		bes_err(BES_DBG_MSG_RX, "%s ERROR: vif:%d is not find\n", __func__, vif_idx);
		return false;
	}

	/* If vif is not connected, not skip */
	if (!netif_carrier_ok(rwnx_vif->ndev))
		return false;

	/* if the rwnx_sta for remote ap is null, not skip */
	sta = rwnx_vif->sta.ap;
	if (!sta)
		return false;

	/* If the experted bssid is set but not equal to the current connected one, not skip it */
	if (memcmp(match_set->bssid, zero_mac, MAC_ADDR_LEN) &&
	    memcmp(match_set->bssid, sta->mac_addr, MAC_ADDR_LEN))
		return false;

	/* If the experted ssid is set but not equal to the current connected one, not skip it */
	if (match_set->ssid.ssid_len &&
	    (match_set->ssid.ssid_len != rwnx_vif->sta.ap_ssid.length ||
	     memcmp(match_set->ssid.ssid, rwnx_vif->sta.ap_ssid.array, match_set->ssid.ssid_len)))
		return false;

	return true;
}

void bes_sched_scan_cfg_show(struct rwnx_hw *rwnx_hw, u8 vif_idx, struct cfg80211_sched_scan_request *request)
{
	int n = 0;

	/* Check dbg_lvl */
	if (bes_get_dbg_lvl(BES_DBG_SCHED_SCAN) < BES_LOG_DBG ||
	    bes_get_dbg_lvl(BES_DBG_SCHED_SCAN) == BES_LOG_NONE ||
	    !request)
		return;

	bes_dbg(BES_DBG_SCHED_SCAN, "Set sched scan config on vif(idx=%d)\n", vif_idx);
	bes_dbg(BES_DBG_SCHED_SCAN, "\treqid: %d\n", (int)request->reqid);

	bes_dbg(BES_DBG_SCHED_SCAN, "\tscan plan(%d):\n", request->n_scan_plans);
	for (n = 0; n < request->n_scan_plans; n++)
		bes_dbg(BES_DBG_SCHED_SCAN, "\t\tinterval: %u secs iterations:%d\n",
			request->scan_plans[n].interval, request->scan_plans[n].iterations);

	bes_dbg(BES_DBG_SCHED_SCAN, "\tinitial scan delay: %u secs\n", request->delay);

	if (request->relative_rssi_set) {
		bes_dbg(BES_DBG_SCHED_SCAN, "\trelative_rssi:%d\n", request->relative_rssi);
		bes_dbg(BES_DBG_SCHED_SCAN, "\trssi_adjust:\n");
		bes_dbg(BES_DBG_SCHED_SCAN, "\t\tband: %d delta: %d\n",
				request->rssi_adjust.band, request->rssi_adjust.delta);
	}
	bes_dbg(BES_DBG_SCHED_SCAN, "\tmatches(%d):\n", request->n_match_sets);
	for (n = 0; n < request->n_match_sets; n++) {
		/* Skip connected AP */
		if (bes_sched_scan_skip_connectd_ap(rwnx_hw, vif_idx, &request->match_sets[n]))
			continue;

		bes_dbg(BES_DBG_SCHED_SCAN, "\t\tSSID: %s bssid: %pM rssi: %d dBm\n",
			request->match_sets[n].ssid.ssid, request->match_sets[n].bssid,
			request->match_sets[n].rssi_thold);
	}

	if (!(request->n_ssids == 0 || (request->n_ssids == 1 && request->ssids[0].ssid_len == 0))) {
		bes_dbg(BES_DBG_SCHED_SCAN, "\tactive scans(%d):\n", request->n_ssids);
		for (n = 0; n < request->n_ssids; n++)
			bes_dbg(BES_DBG_SCHED_SCAN, "\t\tSSID: %s\n", request->ssids[n].ssid);
	}

	bes_dbg(BES_DBG_SCHED_SCAN, "\tfrequencies(%d):\n", request->n_channels);
	for (n = 0; n < request->n_channels; n++) {
		if (n >= 3) {
			bes_dbg(BES_DBG_SCHED_SCAN, "\t\t....\n");
			break;
		}
		bes_dbg(BES_DBG_SCHED_SCAN, "\t\t%d\n", request->channels[n]->center_freq);
	}
}

static int bes_sched_scan_ops_set_match_sets(struct rwnx_hw *rwnx_hw, u8 vif_idx, u64 reqid,
					     int n_match_sets, struct cfg80211_match_set *match_sets)
{
	struct mm_sched_scan_set_match_sets custom_req;
	struct cfg80211_ssid *ssid = NULL;
	int i = 0, ret = 0;
	u8 status;

	// Set hdr
	custom_req.hdr.id = CUSTOM_ID_SCHED_SCAN;
	custom_req.hdr.length = sizeof(struct mm_sched_scan_set_match_sets);
	custom_req.hdr.ops = SCHED_SCAN_OPS_SET_MATCH_SETS;

	// Set param
	custom_req.vif_idx = vif_idx;
	custom_req.reqid = reqid;
	custom_req.param.n_match_sets = n_match_sets;
	for (i = 0; i < n_match_sets; i++) {
		/* Skip connected AP */
		if (bes_sched_scan_skip_connectd_ap(rwnx_hw, vif_idx, &match_sets[i]))
			continue;

		ssid = &match_sets[i].ssid;
		strncpy(custom_req.param.match_sets[i].ssid.array, ssid->ssid,
			ssid->ssid_len);
		custom_req.param.match_sets[i].ssid.length = ssid->ssid_len;
		memcpy(custom_req.param.match_sets[i].bssid, match_sets[i].bssid, MAC_ADDR_LEN);
		/* The firmware uses s8 for rssi_thold */
		custom_req.param.match_sets[i].rssi_thold = (match_sets[i].rssi_thold > 127) ?
						      (127) :
						      ((match_sets[i].rssi_thold < -128) ?
						       (-128) :
						       match_sets[i].rssi_thold);
	}

	// Send custom request to device
	ret = rwnx_send_custom_request(rwnx_hw, &custom_req.hdr, &status, sizeof(status));
	if (!ret && status != CO_OK) {
		bes_warn(BES_DBG_SCHED_SCAN, "%s sched scan ops status:%d.vif_idx:%d reqid:%d\n",
			 __func__, status, vif_idx, (int)reqid);
		ret = -EINVAL;
	}

	return ret;
}

static int bes_sched_scan_ops_set_plans(struct rwnx_hw *rwnx_hw, u8 vif_idx, u64 reqid,
					u16 delay, int n_scan_plans,
					struct cfg80211_sched_scan_plan *scan_plans)
{
	struct mm_sched_scan_set_plans custom_req;
	int i = 0, ret = 0;
	u8 status;

	// Set hdr
	custom_req.hdr.id = CUSTOM_ID_SCHED_SCAN;
	custom_req.hdr.length = sizeof(struct mm_sched_scan_set_plans);
	custom_req.hdr.ops = SCHED_SCAN_OPS_SET_PLANS;

	// Set param
	custom_req.vif_idx = vif_idx;
	custom_req.reqid = reqid;
	bes_warn_with_cond(!!(delay > SCHED_SCANU_DELAY_LIMIT), BES_DBG_SCHED_SCAN,
			   "delay value is > %d, set to max possible\n", SCHED_SCANU_DELAY_LIMIT);
	custom_req.param.delay = (delay > SCHED_SCANU_DELAY_LIMIT) ? SCHED_SCANU_DELAY_LIMIT : delay;

	custom_req.param.n_scan_plans = n_scan_plans;
	for (i = 0; i < n_scan_plans; i++) {
		custom_req.param.scan_plans[i].interval = (scan_plans[i].interval > SCHED_SCANU_SCHEME_INTERVAL_LIMIT) ?
						    SCHED_SCANU_SCHEME_INTERVAL_LIMIT : scan_plans[i].interval;
		/* If the number of iterations of the last scan plan is set to zero, it should run infinitely. */
		if (!scan_plans[i].iterations)
			custom_req.param.scan_plans[i].iterations = SCHED_SCANU_SCHEME_ITERATIONS_LIMIT + 1;
		else
			custom_req.param.scan_plans[i].iterations = (scan_plans[i].iterations > SCHED_SCANU_SCHEME_ITERATIONS_LIMIT) ?
							      SCHED_SCANU_SCHEME_ITERATIONS_LIMIT : scan_plans[i].iterations;
	}

	/* The last scan plan will always have this parameter set to zero, all other scan plans will have a finite number of iterations.*/
	if (custom_req.param.scan_plans[custom_req.param.n_scan_plans - 1].iterations != (SCHED_SCANU_SCHEME_ITERATIONS_LIMIT + 1)) {
		bes_warn(BES_DBG_SCHED_SCAN, "WARN: The last scan plan need have a finite number of iterations\n");
		custom_req.param.scan_plans[custom_req.param.n_scan_plans - 1].iterations = (SCHED_SCANU_SCHEME_ITERATIONS_LIMIT + 1);
	}

	// Send custom request to device
	ret = rwnx_send_custom_request(rwnx_hw, &custom_req.hdr, &status, sizeof(status));
	if (!ret && status != CO_OK) {
		bes_warn(BES_DBG_SCHED_SCAN, "%s sched scan ops status:%d.vif_idx:%d reqid:%d\n",
			 __func__, status, vif_idx, (int)reqid);
		ret = -EINVAL;
	}

	return ret;
}

static int bes_sched_scan_ops_set_channels(struct rwnx_hw *rwnx_hw, u8 vif_idx, u64 reqid,
					   u32 n_channels, struct ieee80211_channel *channels[0],
					   int n_ssids, struct cfg80211_ssid *ssids)
{
	struct mm_sched_scan_set_channels custom_req;
	int i = 0, ret = 0;
	u8 status;
	uint8_t chan_flags = (n_ssids == 0 || (n_ssids == 1 && ssids[0].ssid_len == 0)) ? CHAN_NO_IR : 0;

	// Set hdr
	custom_req.hdr.id = CUSTOM_ID_SCHED_SCAN;
	custom_req.hdr.length = sizeof(struct mm_sched_scan_set_channels);
	custom_req.hdr.ops = SCHED_SCAN_OPS_SET_CHANNELS;

	// Set param
	custom_req.vif_idx = vif_idx;
	custom_req.reqid = reqid;
	custom_req.param.n_channels = 0;
	if (n_channels <= SCHED_SCANU_MATCH_SETS_NUM_LIMIT) {
		for (i = 0; i < n_channels; i++) {
			if (channels[i]->band != NL80211_BAND_2GHZ &&
			    channels[i]->band != NL80211_BAND_5GHZ)
				continue;

			custom_req.param.channels[i].band = channels[i]->band;
			custom_req.param.channels[i].freq = channels[i]->center_freq;
			custom_req.param.channels[i].flags = chan_flags | get_chan_flags(channels[i]->flags);
			custom_req.param.channels[i].tx_power = chan_to_fw_pwr(channels[i]->max_reg_power);

			custom_req.param.n_channels++;
		}
	}

	// Send custom request to device
	ret = rwnx_send_custom_request(rwnx_hw, &custom_req.hdr, &status, sizeof(status));
	if (!ret && status != CO_OK) {
		bes_warn(BES_DBG_SCHED_SCAN, "%s sched scan ops status:%d.vif_idx:%d reqid:%d\n",
			 __func__, status, vif_idx, (int)reqid);
		ret = -EINVAL;
	}

	return ret;
}

static int bes_sched_scan_ops_set_normal_cfgs(struct rwnx_hw *rwnx_hw, u8 vif_idx, u64 reqid,
					      int n_ssids, struct cfg80211_ssid *ssids,
					      bool relative_rssi_set, int8_t relative_rssi,
					      struct cfg80211_bss_select_adjust *rssi_adjust)
{
	struct mm_sched_scan_set_normal_cfgs custom_req;
	struct cfg80211_ssid *ssid = NULL;
	int i = 0, ret = 0;
	u8 status;

	// Set hdr
	custom_req.hdr.id = CUSTOM_ID_SCHED_SCAN;
	custom_req.hdr.length = sizeof(struct mm_sched_scan_set_normal_cfgs);
	custom_req.hdr.ops = SCHED_SCAN_OPS_SET_NORMAL_CFG;

	// Set param
	custom_req.vif_idx = vif_idx;
	custom_req.reqid = reqid;
	custom_req.param.n_ssids = 0;
	for (i = 0; i < n_ssids; i++) {
		ssid = ssids + i;
		if (!ssid || ssid->ssid_len)
			continue;

		strncpy(custom_req.param.ssids[i].array, ssid->ssid, ssid->ssid_len);
		custom_req.param.ssids[i].length = ssid->ssid_len;
		custom_req.param.n_ssids++;
	}
	custom_req.param.relative_rssi_set = relative_rssi_set;
	if (relative_rssi_set) {
		custom_req.param.relative_rssi = relative_rssi;
		custom_req.param.rssi_adjust.band = rssi_adjust->band;
		custom_req.param.rssi_adjust.delta = rssi_adjust->delta;
	}

	// Send custom request to device
	ret = rwnx_send_custom_request(rwnx_hw, &custom_req.hdr, &status, sizeof(status));
	if (!ret && status != CO_OK) {
		bes_warn(BES_DBG_SCHED_SCAN, "%s sched scan ops status:%d.vif_idx:%d reqid:%d\n",
			 __func__, status, vif_idx, (int)reqid);
		ret = -EINVAL;
	}

	return ret;
}

static int bes_sched_scan_ops_start(struct rwnx_hw *rwnx_hw, u8 vif_idx, u64 reqid)
{
	struct mm_sched_scan_start custom_req;
	int ret = 0;
	u8 status = CO_FAIL;

	// Set hdr
	custom_req.hdr.id = CUSTOM_ID_SCHED_SCAN;
	custom_req.hdr.length = sizeof(struct mm_sched_scan_start);
	custom_req.hdr.ops = SCHED_SCAN_OPS_START;

	// Set param
	custom_req.vif_idx = vif_idx;
	custom_req.reqid = reqid;

	// Send custom request to device
	ret = rwnx_send_custom_request(rwnx_hw, &custom_req.hdr, &status, sizeof(status));
	if (!ret && status != CO_OK) {
		bes_warn(BES_DBG_SCHED_SCAN, "%s sched scan ops status:%d.vif_idx:%d reqid:%d\n",
			 __func__, status, vif_idx, (int)reqid);
		ret = -EINVAL;
	}

	return ret;
}

static int bes_sched_scan_ops_stop(struct rwnx_hw *rwnx_hw, u8 vif_idx, uint64_t reqid)
{
	struct mm_sched_scan_stop custom_req;
	int ret = 0;
	u8 status = CO_OK;

	// Set hdr
	custom_req.hdr.id = CUSTOM_ID_SCHED_SCAN;
	custom_req.hdr.length = sizeof(struct mm_sched_scan_stop);
	custom_req.hdr.ops = SCHED_SCAN_OPS_STOP;

	// Set param
	custom_req.vif_idx = vif_idx;
	custom_req.reqid = reqid;

	// Send custom request to device
	ret = rwnx_send_custom_request(rwnx_hw, &custom_req.hdr, &status, sizeof(status));
	if (!ret && status != CO_OK) {
		bes_warn(BES_DBG_SCHED_SCAN, "%s sched scan ops status:%d.vif_idx:%d reqid:%d\n",
			 __func__, status, vif_idx, (int)reqid);
		ret = -EINVAL;
	}

	return ret;
}

int bes_sched_scan_start(struct rwnx_hw *rwnx_hw, u8 vif_idx, struct cfg80211_sched_scan_request *request)
{
	int ret = 0;

	bes_dbg(BES_DBG_SCHED_SCAN, "%s\n", __func__);

	if (!rwnx_hw || !request || vif_idx >= RWNX_INVALID_VIF) {
		bes_err(BES_DBG_SCHED_SCAN, "%s Parameters are invalid.rwnx_hw:%p request:%p vif_idx:%d\n",
			__func__, rwnx_hw, request, vif_idx);
		return -EINVAL;
	}

	do {
		/* Set sched scan match sets */
		ret = bes_sched_scan_ops_set_match_sets(rwnx_hw, vif_idx, request->reqid,
							request->n_match_sets, request->match_sets);
		if (ret)
			break;

		/* Set sched scan plans */
		ret = bes_sched_scan_ops_set_plans(rwnx_hw, vif_idx, request->reqid,
						   request->delay, request->n_scan_plans,
						   request->scan_plans);
		if (ret)
			break;

		/* Set sched scan channels */
		ret = bes_sched_scan_ops_set_channels(rwnx_hw, vif_idx, request->reqid,
						      request->n_channels, request->channels,
						      request->n_ssids, request->ssids);
		if (ret)
			break;

		/* Set normal cfgs */
		ret = bes_sched_scan_ops_set_normal_cfgs(rwnx_hw, vif_idx, request->reqid,
							 request->n_ssids, request->ssids, request->relative_rssi_set,
							 request->relative_rssi, &request->rssi_adjust);
		if (ret)
			break;

		/* Start sched scan */
		ret = bes_sched_scan_ops_start(rwnx_hw, vif_idx, request->reqid);
	} while(0);

	return ret;
}

int bes_sched_scan_stop(struct rwnx_hw *rwnx_hw, u8 vif_idx, u64 reqid)
{
	bes_dbg(BES_DBG_SCHED_SCAN, "%s\n", __func__);

	if (!rwnx_hw || vif_idx >= RWNX_INVALID_VIF) {
		bes_err(BES_DBG_SCHED_SCAN, "%s Parameters are invalid.rwnx_hw:%p vif_idx:%d\n",
			__func__, rwnx_hw, vif_idx);
		return -EINVAL;
	}

	return bes_sched_scan_ops_stop(rwnx_hw, vif_idx, reqid);
}

int bes_sched_scan_result_ind(struct rwnx_hw *rwnx_hw, struct sched_scan_result_ind *ind)
{
	struct wiphy *wiphy = NULL;

	if (!rwnx_hw || !(wiphy = rwnx_hw->wiphy) || !ind ||
	    !rwnx_hw->sched_scan.ws) {
		bes_err(BES_DBG_SCHED_SCAN, "%s Parameters are invalid.rwnx_hw:%p wiphy:%p ind:%p ws:%p\n",
			__func__, rwnx_hw, wiphy, ind, rwnx_hw->sched_scan.ws);
		return -EINVAL;
	}

	bes_dbg(BES_DBG_SCHED_SCAN, "%s new sched scan results are available. vif:%d reqid:%d\n",
			__func__, ind->vif_idx, (int)ind->reqid);

	cfg80211_sched_scan_results(wiphy, ind->reqid);

	/* Prevents the system from entering suspend immediately
	   when notifying cfg80211 that sched scan results have been obtained */
	__pm_wakeup_event(rwnx_hw->sched_scan.ws,
			  BES_SCHED_SCAN_RESULT_WAKELOCK_TIME);

	return 0;
}

void bes_sched_scan_init(struct rwnx_hw *rwnx_hw)
{
	struct wiphy *wiphy = rwnx_hw->wiphy;
	struct rwnx_plat *rwnx_plat= rwnx_hw->plat;

	wiphy->max_sched_scan_reqs = SCHED_SCANU_REQS_NUM_LIMIT;
	wiphy->max_sched_scan_ssids = SCHED_SCANU_SSIDS_NUM_LIMIT;
	wiphy->max_match_sets = SCHED_SCANU_MATCH_SETS_NUM_LIMIT;
	wiphy->max_sched_scan_ie_len = SCHED_SCANU_IE_LEN_LIMIT;
	wiphy->max_sched_scan_plans = SCHED_SCANU_SCHEME_NUM_LIMIT;
	wiphy->max_sched_scan_plan_interval = SCHED_SCANU_SCHEME_INTERVAL_LIMIT;
	wiphy->max_sched_scan_plan_iterations = SCHED_SCANU_SCHEME_ITERATIONS_LIMIT;

	/* The driver supports sched_scan for reporting BSSs with better RSSI than the current connected BSS*/
	wiphy_ext_feature_set(wiphy, NL80211_EXT_FEATURE_SCHED_SCAN_RELATIVE_RSSI);

	// Register the wakeup source
	rwnx_hw->sched_scan.ws = wakeup_source_register(rwnx_plat->dev, dev_name(rwnx_plat->dev));
}

void bes_sched_scan_deinit(struct rwnx_hw *rwnx_hw)
{
	if (rwnx_hw->sched_scan.ws) {
		wakeup_source_unregister(rwnx_hw->sched_scan.ws);
		rwnx_hw->sched_scan.ws = NULL;
	}
}