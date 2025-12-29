/*
 * @file bes_signal_test.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "rwnx_defs.h"
#include "bes_log.h"
#include "bes_filter_manager.h"
#include "bes_filter_map.h"
#include "bes_filter.h"
#include "bes_wowlan.h"
#include "bes_signal_test.h"

static u8 nsignal_pattern_mask = 0x7;
static u8 nsignal_pattern_content00[3] = {0x00, 0x00, 0x00};
static struct cfg80211_pkt_pattern nsignal_pattern00 = {
	.pattern_len = 3,
	.pkt_offset = sizeof(struct ethhdr),
	.mask = &nsignal_pattern_mask,
	.pattern = nsignal_pattern_content00,
};
static u8 nsignal_pattern_content55[3] = {0x55, 0x55, 0x55};
struct cfg80211_pkt_pattern nsignal_pattern55 = {
	.pattern_len = 3,
	.pkt_offset = sizeof(struct ethhdr),
	.mask = &nsignal_pattern_mask,
	.pattern = nsignal_pattern_content55,
};
static u8 nsignal_pattern_contentAA[3] = {0xAA, 0xAA, 0xAA};
static struct cfg80211_pkt_pattern nsignal_patternAA = {
	.pattern_len = 3,
	.pkt_offset = sizeof(struct ethhdr),
	.mask = &nsignal_pattern_mask,
	.pattern = nsignal_pattern_contentAA,
};

static int bes_signal_test_add_filter(struct rwnx_hw *rwnx_hw, u8 vif_idx)
{
	int ret = 0;

	/* Check if repeating to add non signal test filter */
	if (rwnx_hw->filter.signal_test_filter[vif_idx]) {
		bes_warn(BES_DBG_FILTER_MANAGER,
			 "%s WARN: repeating add non signal test filter\n", __func__);
		return 0;
	}

	do {
		ret = bes_filter_map_insert_pattern(rwnx_hw, &rwnx_hw->filter.ifl_map,
				0, vif_idx, &nsignal_pattern00, RXF_MATCH_EQUAL, RXF_REJECT);
		if (ret)
			break;

		ret = bes_filter_map_insert_pattern(rwnx_hw, &rwnx_hw->filter.ifl_map,
				0, vif_idx, &nsignal_pattern55, RXF_MATCH_EQUAL, RXF_REJECT);
		if (ret)
			break;

		ret = bes_filter_map_insert_pattern(rwnx_hw, &rwnx_hw->filter.ifl_map,
				0, vif_idx, &nsignal_patternAA, RXF_MATCH_EQUAL, RXF_REJECT);
		if (ret)
			break;

		rwnx_hw->filter.signal_test_filter[vif_idx] = true;
	} while(0);

	if (ret)
		bes_filter_map_del_filter_by_type(rwnx_hw, &rwnx_hw->filter.ifl_map,
						  vif_idx, FILTER_PATTERN);

	return ret;
}

int bes_signal_test_set_filter(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif)
{
	int ret = 0;
	u8 vif_idx = 0;

	/* Determine if it is effective to set arp offload */
	if (!rwnx_hw || !rwnx_vif)
		return -EINVAL;

	/* Get the index of vif */
	vif_idx = rwnx_vif->vif_index;
	if (vif_idx >= NX_ITF_MAX)
		return -EINVAL;

	if (!rwnx_hw->mod_params->signal_test_mode)
		return 0;

	do {
		/* Add arp offload filter to ifl_map */
		ret = bes_signal_test_add_filter(rwnx_hw, vif_idx);
		if (ret)
			break;

		/* Queue work to sync filter */
		ret = bes_filter_manager_sync_filter(rwnx_hw, false);
	} while(0);

	return ret;
}

int bes_signal_test_clear_filter(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif)
{
	int ret = 0;
	u8 vif_idx = 0;

	/* Determine if it is effective to set arp offload */
	if (!rwnx_hw || !rwnx_vif)
		return -EINVAL;

	/* Get the index of vif */
	vif_idx = rwnx_vif->vif_index;
	if (vif_idx >= NX_ITF_MAX)
		return -EINVAL;

	if (!rwnx_hw->mod_params->signal_test_mode)
		return 0;

	do {
		/* Avoid duplicate deletion */
		if (!rwnx_hw->filter.signal_test_filter[vif_idx])
			break;

		/* Delete non-signal test filter in ifl_map */
		bes_filter_map_del_filter_by_type(rwnx_hw, &rwnx_hw->filter.ifl_map,
						  vif_idx, FILTER_PATTERN);

		rwnx_hw->filter.signal_test_filter[vif_idx] = false;

		/* Queue work to sync filter */
		ret = bes_filter_manager_sync_filter(rwnx_hw, false);
	} while(0);

	return ret;
}

int bes_signal_test_add_filter_when_screen_off(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
					       struct list_head *list_set_head, struct bes_capacity *stats)
{
	int ret = 0;
	u8 vif_idx = 0;
	struct filter_item *item = NULL;

	if (!rwnx_hw || !rwnx_vif || !list_set_head || !stats)
		return -EINVAL;

	/* Get the index of vif */
	vif_idx = rwnx_vif->vif_index;
	if (vif_idx >= NX_ITF_MAX)
		return -EINVAL;

	if (!rwnx_hw->mod_params->signal_test_mode)
		return 0;

	do {
		/* nsignal_pattern00 filter */
		item = bes_filter_manager_create_pattern(vif_idx, FILTER_PATTERN,
							&nsignal_pattern00, RXF_MATCH_EQUAL, RXF_REJECT);
		if (!item) {
			ret = -EINVAL;
			bes_err(BES_DBG_WPA_PRI_CMD,
				"%s ERROR(%d): Fail to create nsignal_pattern00 filter on vif %s at head\n",
				__func__, ret, rwnx_vif->ndev->name);
			break;
		}
		ret = bes_wowlan_insert_at_head(rwnx_hw, list_set_head, stats, item);
		if (ret) {
			bes_err(BES_DBG_WPA_PRI_CMD,
				"%s ERROR(%d): Fail to add nsignal_pattern00 filter on vif %s at head\n",
				__func__, ret, rwnx_vif->ndev->name);
			break;
		}

		/* nsignal_pattern55 filter */
		item = bes_filter_manager_create_pattern(vif_idx, FILTER_PATTERN,
							&nsignal_pattern55, RXF_MATCH_EQUAL, RXF_REJECT);
		if (!item) {
			ret = -EINVAL;
			bes_err(BES_DBG_WPA_PRI_CMD,
				"%s ERROR(%d): Fail to create nsignal_pattern55 filter on vif %s at head\n",
				__func__, ret, rwnx_vif->ndev->name);
			break;
		}
		ret = bes_wowlan_insert_at_head(rwnx_hw, list_set_head, stats, item);
		if (ret) {
			bes_err(BES_DBG_WPA_PRI_CMD,
				"%s ERROR(%d): Fail to add nsignal_pattern55 filter on vif %s at head\n",
				__func__, ret, rwnx_vif->ndev->name);
			break;
		}

		/* nsignal_patternAA filter */
		item = bes_filter_manager_create_pattern(vif_idx, FILTER_PATTERN,
							&nsignal_patternAA, RXF_MATCH_EQUAL, RXF_REJECT);
		if (!item) {
			ret = -EINVAL;
			bes_err(BES_DBG_WPA_PRI_CMD,
				"%s ERROR(%d): Fail to create nsignal_patternAA filter on vif %s at head\n",
				__func__, ret, rwnx_vif->ndev->name);
			break;
		}
		ret = bes_wowlan_insert_at_head(rwnx_hw, list_set_head, stats, item);
		if (ret) {
			bes_err(BES_DBG_WPA_PRI_CMD,
				"%s ERROR(%d): Fail to add nsignal_patternAA filter on vif %s at head\n",
				__func__, ret, rwnx_vif->ndev->name);
			break;
		}
	} while(0);

	return ret;
}

void bes_signal_test_init(struct rwnx_hw *rwnx_hw)
{
	u8 vif_idx = 0;

	for (vif_idx = 0; vif_idx < NX_ITF_MAX; vif_idx++)
		rwnx_hw->filter.signal_test_filter[vif_idx] = false;
}

void bes_signal_test_deinit(struct rwnx_hw *rwnx_hw)
{
	u8 vif_idx = 0;

	for (vif_idx = 0; vif_idx < NX_ITF_MAX; vif_idx++)
		rwnx_hw->filter.signal_test_filter[vif_idx] = false;
}
