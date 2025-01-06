/*
 * @file bes_custom_filter.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/inet.h>
#include "rwnx_defs.h"
#include "bes_log.h"
#include "bes_custom_filter.h"
#include "bes_wowlan.h"
#include "bes_filter_map.h"
#include "bes_filter.h"

/* Parameter for Parsing custom filter command */
#define CUSTOM_FILTER_MAXARGS			(15)

/* Custom filter list map and its parameters */
static struct bes_fl_map cfl_map;

static int bes_custom_filter_switch_rxf(struct rwnx_hw *rwnx_hw, int f_idx, bool on)
{
	u8 vif_idx = 0, j = 0;
	struct rwnx_vif *rwnx_vif = NULL;
	struct filter_item *item = NULL;
	struct bes_fl *fl = NULL;

	for (vif_idx = 0; vif_idx < NX_ITF_MAX; vif_idx++) {
		/* Check if vif is up */
		rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);
		if (!rwnx_vif)
			continue;

		/* Check filters for vif in cfl_map */
		for (j = 0; j < FL_MAX; j++) {
			fl = &cfl_map.fl_tbl[vif_idx][j];
			if (fl && fl->count != 0) {
				list_for_each_entry(item, &fl->list, list) {
					if (item->f_idx == f_idx)
						return bes_filter_manager_switch_rxf(rwnx_hw, item, on);
				}
			}
		}
	}

	return -EINVAL;
}

static int bes_custom_filter_bind_offload(struct rwnx_hw *rwnx_hw, u8 wf_type, int of_idx)
{
	u8 vif_idx = 0;
	struct rwnx_vif *rwnx_vif = NULL;
	struct filter_item *item = NULL;
	struct bes_fl *fl = NULL;
	struct list_head list_set_head;

	/* Init list_set_head */
	INIT_LIST_HEAD(&list_set_head);

	/* Add the items to bind arp offload to device to the list_set_head */
	for (vif_idx = 0; vif_idx < NX_ITF_MAX; vif_idx++) {
		if ((wf_type == FILTER_IPV4_ARP && rwnx_hw->filter.arp_offload[vif_idx]) ||
		    (wf_type == FILTER_IPV6_NS && rwnx_hw->filter.ns_offload[vif_idx])) {
			/* Check if vif is up */
			rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);
			if (!rwnx_vif)
				continue;

			/* Check filters for vif in cfl_map */
			fl = &cfl_map.fl_tbl[vif_idx][FL_ETH_TYPE];
			if (fl->count > 0) {
				list_for_each_entry(item, &fl->list, list) {
					if (item->type == wf_type) {
						list_add_tail(&item->list_set, &list_set_head);
						item->ref_count++;
					}
				}
			}
		}
	}

	/* Bind ARP offload for all elements in the list_set_head*/
	return bes_filter_manager_bind_fl_arp_offload(rwnx_hw, &list_set_head, of_idx);
}

static void bes_custom_filter_mode_change(struct rwnx_hw *rwnx_hw, u8 mode)
{
	/* Check if it repeat to set */
	if (mode == rwnx_hw->wowlan.mode) {
		bes_warn(BES_DBG_CUSTOM_FILTER, "%s WARN: Repeat to set wowlan mode to %s\n", __func__,
			 mode ==  WOWLAN_MODE_CUSTOM ? "CUSTOM" : "STANDARD");
		return;
	}

	if (mode == WOWLAN_MODE_STANDARD) {
		/* Clear common filter list map */
		bes_filter_map_delete_all(&cfl_map);

		/* diable common filter list map */
		bes_filter_map_set_valid(&cfl_map, false);
	} else {
		/* enable common filter list map */
		bes_filter_map_set_valid(&cfl_map, true);
	}

	/* Update wowlan mode value */
	rwnx_hw->wowlan.mode = mode;
}

static int bes_custom_filter_sysfs_enable(struct rwnx_hw *rwnx_hw, const char **argv, int argc)
{
	const int min_lastargc = 5;
	int ret = 0;
	int vif_idx = 0;

	/* Format Example: enable rxf on vif wlan0 */

	/* Check the validity of parameters */
	if (argc < min_lastargc) {
		return -ERANGE;
	}

	/* Parsing parameters */
	if (strcmp(argv[3], "vif") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[3]:%s\n",
			__func__, argv[3]);
		return -EINVAL;
	}
	if (((vif_idx = bes_common_vif_idx(rwnx_hw,
					   (char *)argv[4])) < 0) || (vif_idx >= NX_ITF_MAX)) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[4]:%s\n",
			__func__, argv[4]);
		return -EINVAL;
	}

	/* Enable rxf on vif */
	ret = bes_filter_manager_enable(rwnx_hw, vif_idx);
	if (ret) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR(%d): Fail to enable rxf on vif %s\n",
			__func__, ret, argv[4]);
		return ret;
	}
	bes_dbg(BES_DBG_CUSTOM_FILTER, "%s Sucess: enable rxf on vif %s\n", __func__, argv[4]);

	return 0;
}

static int bes_custom_filter_sysfs_sync(struct rwnx_hw *rwnx_hw, const char **argv, int argc)
{
	const int min_lastargc = 4;
	int ret = 0;

	/* Format Example: sync rxf to device */

	/* Check the validity of parameters */
	if (argc < min_lastargc) {
		return -ERANGE;
	}

	/* Parsing parameters */
	if (strcmp(argv[2], "to") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[2]:%s\n",
			__func__, argv[2]);
		return -EINVAL;
	}
	if (strcmp(argv[3], "device") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[3]:%s\n",
			__func__, argv[3]);
		return -EINVAL;
	}

	/* Sync rxf to device */
	ret = bes_filter_map_sync_cfl_map_to_device(rwnx_hw, &cfl_map);
	if (ret) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR(%d): Fail to sync rxf to device\n",
			__func__, ret);
		return ret;
	}
	bes_dbg(BES_DBG_CUSTOM_FILTER, "%s Sucess: sync rxf to device\n", __func__);

	return 0;
}

static int bes_custom_filter_sysfs_disable(struct rwnx_hw *rwnx_hw,
					   const char **argv, int argc)
{
	const int min_lastargc = 5;
	int ret = 0;
	int vif_idx = 0;

	/* Format Example: disable rxf on vif wlan0 */

	/* Check the validity of parameters */
	if (argc < min_lastargc) {
		return -ERANGE;
	}

	/* Parsing parameters */
	if (strcmp(argv[3], "vif") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[3]:%s\n",
			__func__, argv[3]);
		return -EINVAL;
	}
	if (((vif_idx = bes_common_vif_idx(rwnx_hw,
					   (char *)argv[4])) < 0) || (vif_idx >= NX_ITF_MAX)) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[4]:%s\n",
			__func__, argv[4]);
		return -EINVAL;
	}

	/* Disable rxf on vif */
	ret = bes_filter_manager_disable(rwnx_hw, vif_idx);
	if (ret) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR(%d): Fail to disable rxf on vif %s\n",
			__func__, ret, argv[4]);
		return ret;
	}
	bes_dbg(BES_DBG_CUSTOM_FILTER, "%s Sucess: disable rxf on vif %s\n", __func__, argv[4]);

	return 0;
}

static int bes_custom_filter_sysfs_reset(struct rwnx_hw *rwnx_hw, const char **argv, int argc)
{
	const int min_lastargc = 2;
	int ret = 0;

	/* Format Example: reset rxf */

	/* Check the validity of parameters */
	if (argc < min_lastargc) {
		return -ERANGE;
	}

	/* Parsing parameters */
	if ((strcmp(argv[1], "rxf") != 0)) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[1]:%s\n",
			__func__, argv[1]);
		return -EINVAL;
	}

	/* Reset rxf */
	ret = bes_filter_manager_reset(rwnx_hw);
	if (ret) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR(%d): Fail to reset rxf\n",
			__func__, ret);
		return ret;
	}
	bes_dbg(BES_DBG_CUSTOM_FILTER, "%s Sucess: reset rxf\n", __func__);

	return 0;
}

static int bes_custom_filter_sysfs_switch(struct rwnx_hw *rwnx_hw,
					  const char **argv, int argc)
{
	const int min_lastargc = 4;
	int f_idx = 0;
	bool on = false;
	int ret = 0;

	/* Format Example: switch rxf 0 on/off */

	/* Check the validity of parameters */
	if (argc < min_lastargc) {
		return -EINVAL;
	}

	/* Parsing parameters */
	if (strcmp(argv[1], "rxf") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[1]:%s\n",
			__func__, argv[1]);
		return -EINVAL;
	}
	if ((sscanf(argv[2], "%d", &f_idx) < 1) || (f_idx < 0)) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[2]:%s\n",
			__func__, argv[2]);
		return -EINVAL;
	}
	if (strcmp(argv[3], "on") == 0) {
		on = true;
	} else if (strcmp(argv[3], "off") == 0) {
		on = false;
	} else {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[3]:%s\n",
			__func__, argv[3]);
		return -EINVAL;
	}

	/* Switch rxf to device */
	ret = bes_custom_filter_switch_rxf(rwnx_hw, f_idx, on);
	if (ret) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR(%d): Fail to switch rxf %d %s\n",
			__func__, ret, f_idx, argv[3]);
		return ret;
	}
	bes_info(BES_DBG_CUSTOM_FILTER, "%s Sucess: switch rxf %d %s\n",
		 __func__, f_idx, argv[3]);

	return 0;
}

static int bes_custom_filter_sysfs_bind(struct rwnx_hw *rwnx_hw, const char **argv, int argc)
{
	const int min_lastargc = 6;
	int ret = 0;
	int of_idx = 0;
	u8 wf_type = 0;

	/* Format Example: bind arp/ns offload on rxf 1 */

	/* Check the validity of parameters */
	if (argc < min_lastargc) {
		return -ERANGE;
	}

	/* Parsing parameters */
	if (strcmp(argv[1], "arp") == 0) {
		wf_type = FILTER_IPV4_ARP;
	} else if (strcmp(argv[1], "ns") == 0) {
		wf_type = FILTER_IPV6_NS;
	} else {
		bes_err(BES_DBG_WOWLAN, "%s ERROR: fail to parse. argv[1]:%s\n",
			__func__, argv[1]);
		return -EINVAL;
	}
	if (strcmp(argv[2], "offload") != 0) {
		bes_err(BES_DBG_WOWLAN, "%s ERROR: fail to parse. argv[2]:%s\n",
			__func__, argv[2]);
		return -EINVAL;
	}
	if (strcmp(argv[4], "rxf") != 0) {
		bes_err(BES_DBG_WOWLAN, "%s ERROR: fail to parse. argv[4]:%s\n",
			__func__, argv[4]);
		return -EINVAL;
	}
	if ((sscanf(argv[5], "%d", &of_idx) < 1) || (of_idx < 0)) {
		bes_err(BES_DBG_WOWLAN, "%s ERROR: fail to parse. argv[5]:%s\n",
			__func__, argv[5]);
		return -EINVAL;
	}

	/* Bind ns offload on rxf */
	ret = bes_custom_filter_bind_offload(rwnx_hw, wf_type, of_idx);
	if (ret) {
		bes_err(BES_DBG_WOWLAN, "%s ERROR(%d): Fail to bind offload on rxf %d\n",
			__func__, ret, of_idx);
		return ret;
	}
	bes_dbg(BES_DBG_WOWLAN, "%s Sucess: bind offload on rxf %d\n", __func__, of_idx);

	return 0;
}

static int bes_custom_filter_sysfs_set_mismatch_action(struct rwnx_hw *rwnx_hw,
						       const char **argv, int argc)
{
	const int min_lastargc = 5;
	int ret = 0;
	u8 action = 0;

	/* Format Example: set mismatch action to accept/reject */

	/* Check the validity of parameters */
	if (argc < min_lastargc) {
		return -ERANGE;
	}

	/* Parsing parameters */
	if (strcmp(argv[2], "action") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[2]:%s\n",
			__func__, argv[2]);
		return -EINVAL;
	}
	if (strcmp(argv[3], "to") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[3]:%s\n",
			__func__, argv[3]);
		return -EINVAL;
	}
	if (strcmp(argv[4], "accept") == 0) {
		action = FILTER_ACTION_ACCEPT;
	} else if (strcmp(argv[4], "reject") == 0) {
		action = FILTER_ACTION_DISCARD;
	} else {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[4]:%s\n",
			__func__, argv[4]);
		return -EINVAL;
	}

	/* Set mismatch action */
	ret = bes_filter_manager_set_mismatch_action(rwnx_hw, action);
	if (ret) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR(%d): Fail to set mismatch action to %s\n",
			__func__, ret, argv[4]);
		return ret;
	}
	bes_dbg(BES_DBG_CUSTOM_FILTER, "%s Sucess: set mismatch action to %s\n", __func__, argv[4]);

	return 0;
}

static int bes_custom_filter_sysfs_set_wowlan_mode(struct rwnx_hw *rwnx_hw,
						   const char **argv, int argc)
{
	const int min_lastargc = 5;
	int mode = 0;

	/* Format Example: set wowlan mode to standard/custom */

	/* Check the validity of parameters */
	if (argc < min_lastargc) {
		return -ERANGE;
	}

	/* Parsing parameters */
	if (strcmp(argv[2], "mode") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[2]:%s\n",
			__func__, argv[2]);
		return -EINVAL;
	}
	if (strcmp(argv[3], "to") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[3]:%s\n",
			__func__, argv[3]);
		return -EINVAL;
	}
	if (strcmp(argv[4], "standard") == 0) {
		mode = WOWLAN_MODE_STANDARD;
	} else if (strcmp(argv[4], "custom") == 0) {
		mode = WOWLAN_MODE_CUSTOM;
	} else {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[4]:%s\n",
			__func__, argv[4]);
		return -EINVAL;
	}

	/* wowlan operation mode change */
	bes_custom_filter_mode_change(rwnx_hw, mode);
	bes_dbg(BES_DBG_CUSTOM_FILTER, "%s set wowlan mode to %s\n", __func__, argv[4]);

	return 0;
}

static int bes_custom_filter_sysfs_set(struct rwnx_hw *rwnx_hw, const char **argv, int argc)
{
	int ret = 0;

	/* Check the validity of parameters */
	if (argc < 2) {
		return -ERANGE;
	}

	/* Parsing parameters */
	if (strcmp(argv[1], "mismatch") == 0) {
		ret = bes_custom_filter_sysfs_set_mismatch_action(rwnx_hw, argv, argc);
	} else if (strcmp(argv[1], "wowlan") == 0) {
		ret = bes_custom_filter_sysfs_set_wowlan_mode(rwnx_hw, argv, argc);
	} else {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to get set type. argv[1]:%s\n",
			__func__, argv[1]);
		return -EINVAL;
	}

	return ret;
}

static int bes_custom_filter_sysfs_get_add_pos(struct bes_fl *fl, const char **argv,
					       int argc, int lastargc)
{
	int pos = 0;

	/* Check the validity of parameters */
	if (argc < lastargc) {
		return -ERANGE;
	}

	/* Parsing parameters */
	if (strcmp(argv[lastargc - 2], "at") == 0) { // at head/tail
		if (argc < lastargc) {
			bes_err(BES_DBG_CUSTOM_FILTER,
				"%s ERROR: fail to parse. argc < lastargc:%d < %d\n",
				__func__, argc, lastargc);
			return -EINVAL;
		}
		if (strcmp(argv[lastargc - 1], "head") == 0) {
			pos = 0;
		} else if (strcmp(argv[lastargc - 1], "tail") == 0) {
			pos = fl->count;
		} else {
			bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[%d]:%s\n",
				__func__, lastargc - 1, argv[lastargc - 1]);
			return -EINVAL;
		}
	} else if (strcmp(argv[lastargc - 2], "after") == 0) { // after 1st/2nd/3rd/Nth item
		if (argc < (lastargc + 1)) {
			bes_err(BES_DBG_CUSTOM_FILTER,
				"%s ERROR: fail to parse. argc < (lastargc + 1):%d < (%d + 1)\n",
				__func__, argc, lastargc);
			return -EINVAL;
		}
		if (strcmp(argv[lastargc - 1], "1st") == 0) {
			pos = 1;
		} else if (strcmp(argv[lastargc - 1], "2nd") == 0) {
			pos = 2;
		} else if (strcmp(argv[lastargc - 1], "3rd") == 0) {
			pos = 3;
		} else if ((strstr(argv[lastargc - 1], "th") != NULL) &&
			   (sscanf(argv[lastargc - 1], "%d", &pos) == 1) && (pos > 3)) {
			; // do nothing
		} else {
			bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[%d]:%s\n",
				__func__, lastargc - 1, argv[lastargc - 1]);
			return -EINVAL;
		}
		if (strcmp(argv[lastargc], "item") != 0) {
			bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[%d]:%s\n",
				__func__, lastargc - 1, argv[lastargc - 1]);
			return -EINVAL;
		}
	} else {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[%d]:%s\n", __func__,
			lastargc, argv[lastargc]);
		return -EINVAL;
	}

	return pos;
}

static int bes_custom_filter_sysfs_add_mac(struct rwnx_hw *rwnx_hw,
					   const char **argv, int argc)
{
	const int min_lastargc = 11;
	const u8 wfl_idx = FL_MAC_ADDR;
	int ret = 0;
	int vif_idx = 0;
	u8 wf_type = 0, action = 0;
	int pos = 0;
	struct bes_fl *fl = NULL;
	u8 match_mode = 0;
	u8 mac[MAC_ADDR_LEN] = {0};

	/* Format Example:
	 * add mac da/sa Value/NValue(00:02:04:94:f6:b1) with accept/reject on vif wlan0
	 * (at head/tail)/(after 1st/2nd/3rd/Nth item)
	 */

	/* Check the validity of parameters */
	if (argc < min_lastargc) {
		return -ERANGE;
	}

	/* Parsing parameters */
	if (strcmp(argv[2], "da") == 0) {
		wf_type = FILTER_MAC_DA;
	} else if (strcmp(argv[2], "sa") == 0) {
		wf_type = FILTER_MAC_SA;
	} else {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[2]:%s\n",
			__func__, argv[2]);
		return -EINVAL;
	}
	if ((strncmp(argv[3], "Value", 5) == 0) &&
	    (sscanf(argv[3], "Value(%hhx:%hhx:%hhx:%hhx:%hhx:%hhx)", &mac[0],
	    	&mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) == 6)) {
		match_mode = RXF_MATCH_EQUAL;
	} else if ((strncmp(argv[3], "NValue", 6) == 0) &&
		    (sscanf(argv[3], "NValue(%hhx:%hhx:%hhx:%hhx:%hhx:%hhx)",
			    &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) == 6)) {
		match_mode = RXF_MATCH_NOT_EQUAL;
	} else {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[3]:%s\n",
			__func__, argv[3]);
		return -EINVAL;
	}
	if (strcmp(argv[4], "with") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[4]:%s\n",
			__func__, argv[4]);
		return -EINVAL;
	}
	if (strcmp(argv[5], "accept") == 0) {
		action = RXF_ACCEPT;
	} else if (strcmp(argv[5], "reject") == 0) {
		action = RXF_REJECT;
	} else {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[5]:%s\n",
			__func__, argv[5]);
		return -EINVAL;
	}
	if (strcmp(argv[7], "vif") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[7]:%s\n",
			__func__, argv[7]);
		return -EINVAL;
	}
	if ((vif_idx = bes_common_vif_idx(rwnx_hw, (char *)argv[8])) < 0 || (vif_idx >= NX_ITF_MAX)) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[8]:%s\n",
			__func__, argv[8]);
		return -EINVAL;
	}
	if ((!(fl = &cfl_map.fl_tbl[vif_idx][wfl_idx])) ||
	    ((pos = bes_custom_filter_sysfs_get_add_pos(fl, argv, argc, min_lastargc)) < 0)) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to get add pos\n", __func__);
		return -EINVAL;
	}

	/* Insert mac_addr filter to cfl_map */
	ret = bes_filter_map_insert_mac_addr(rwnx_hw, &cfl_map, pos, vif_idx,
			wf_type, (struct mac_addr *)mac, match_mode, action);
	if (ret) {
		bes_err(BES_DBG_CUSTOM_FILTER,
			"%s ERROR(%d): Fail to add mac %s %pM with %s on vif %s after %dst item\n",
			__func__, ret, argv[2], mac, argv[5], argv[8], pos);
		return ret;
	}
	bes_dbg(BES_DBG_CUSTOM_FILTER, "%s Sucess: add mac %s %pM with %s on vif %s after %dst item\n",
		__func__, argv[2], mac, argv[5], argv[8], pos);

	return 0;
}

static int bes_custom_filter_sysfs_add_eth(struct rwnx_hw *rwnx_hw,
					   const char **argv, int argc)
{
	const int min_lastargc = 11;
	const u8 wfl_idx = FL_ETH_TYPE;
	int ret = 0;
	int vif_idx = 0;
	u8 action = 0;
	int pos = 0;
	struct bes_fl *fl = NULL;
	u8 match_mode = 0;
	u32 eth_type = 0;

	/* Format Example:
	 * add eth type Value/NValue(0x8000) with accept/reject on vif wlan0
	 * (at head/tail)/(after 1st/2nd/3rd/Nth item)
	 */

	/* Check the validity of parameters */
	if (argc < min_lastargc) {
		return -ERANGE;
	}

	/* Parsing parameters */
	if (strcmp(argv[2], "type") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[2]:%s\n",
			__func__, argv[2]);
		return -EINVAL;
	}
	if ((strncmp(argv[3], "Value", 5) == 0) &&
	    (sscanf(argv[3], "Value(0x%04x)", &eth_type) == 1)) {
		match_mode = RXF_MATCH_EQUAL;
	} else if ((strncmp(argv[3], "NValue", 6) == 0) &&
		   (sscanf(argv[3], "NValue(0x%04x)", &eth_type) == 1)) {
		match_mode = RXF_MATCH_NOT_EQUAL;
	} else {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[3]:%s\n",
			__func__, argv[3]);
		return -EINVAL;
	}
	if (strcmp(argv[4], "with") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[4]:%s\n",
			__func__, argv[4]);
		return -EINVAL;
	}
	if (strcmp(argv[5], "accept") == 0) {
		action = RXF_ACCEPT;
	} else if (strcmp(argv[5], "reject") == 0) {
		action = RXF_REJECT;
	} else {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[5]:%s\n",
			__func__, argv[5]);
		return -EINVAL;
	}
	if (strcmp(argv[7], "vif") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[7]:%s\n",
			__func__, argv[7]);
		return -EINVAL;
	}
	if ((vif_idx = bes_common_vif_idx(rwnx_hw, (char *)argv[8])) < 0 || (vif_idx >= NX_ITF_MAX)) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[8]:%s\n",
			__func__, argv[8]);
		return -EINVAL;
	}
	if ((!(fl = &cfl_map.fl_tbl[vif_idx][wfl_idx])) ||
	    ((pos = bes_custom_filter_sysfs_get_add_pos(fl, argv, argc, min_lastargc)) < 0)) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to get add pos\n", __func__);
		return -EINVAL;
	}

	/* Insert eth_type filter to cfl_map */
	ret = bes_filter_map_insert_eth_type(rwnx_hw, &cfl_map, pos, vif_idx,
					     (u16)eth_type, match_mode, action);
	if (ret) {
		bes_err(BES_DBG_CUSTOM_FILTER,
			"%s ERROR(%d): Fail to add eth type %s(0x%04x) with %s on vif %s after %dst item\n",
			__func__, ret, match_mode == RXF_MATCH_EQUAL ? "Value" : "NValue",
			(u16)eth_type, argv[5], argv[8], pos);
		return ret;
	}
	bes_dbg(BES_DBG_CUSTOM_FILTER,
		"%s Sucess: add eth type %s(0x%04x) with %s on vif %s after %dst item\n",
		__func__, match_mode == RXF_MATCH_EQUAL ? "Value" : "NValue",
		(u16)eth_type, argv[5], argv[8], pos);

	return 0;
}

static int bes_custom_filter_sysfs_add_arp(struct rwnx_hw *rwnx_hw, const char **argv, int argc)
{
	const int min_lastargc = 7;
	const u8 wfl_idx = FL_ETH_TYPE;
	int ret = 0;
	int vif_idx = 0;
	int pos = 0;
	struct bes_fl *fl = NULL;

	/* Format Example: add arp on vif wlan0 (at head/tail)/(after 1st/2nd/3rd/Nth item) */

	/* Check the validity of parameters */
	if (argc < min_lastargc) {
		return -ERANGE;
	}

	/* Parsing parameters */
	if (strcmp(argv[3], "vif") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[3]:%s\n",
			__func__, argv[3]);
		return -EINVAL;
	}
	if ((vif_idx = bes_common_vif_idx(rwnx_hw, (char *)argv[4])) < 0 || (vif_idx >= NX_ITF_MAX)) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[4]:%s\n",
			__func__, argv[4]);
		return -EINVAL;
	}
	if ((!(fl = &cfl_map.fl_tbl[vif_idx][wfl_idx])) ||
	    ((pos = bes_custom_filter_sysfs_get_add_pos(fl, argv, argc, min_lastargc)) < 0)) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to get add pos\n", __func__);
		return -EINVAL;
	}

	/* Insert arp offload filter to cfl_map */
	ret = bes_filter_map_insert_ipv4_arp(rwnx_hw, &cfl_map, pos, vif_idx, ARP_OFFLOAD_FUNC_IDX);
	if (ret) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR(%d): Fail to add arp on vif %s after %dst item\n",
			__func__, ret, argv[4], pos);
		return ret;
	}
	bes_dbg(BES_DBG_CUSTOM_FILTER, "%s Sucess: add arp on vif %s after %dst item\n",
		__func__, argv[4], pos);

	return 0;
}

static int bes_custom_filter_sysfs_add_ns(struct rwnx_hw *rwnx_hw,
					  const char **argv, int argc)
{
	const int min_lastargc = 7;
	const u8 wfl_idx = FL_ETH_TYPE;
	int ret = 0;
	int vif_idx = 0;
	int pos = 0;
	struct bes_fl *fl = NULL;

	/* Format Example: add ns on vif wlan0 (at head/tail)/(after 1st/2nd/3rd/Nth item) */

	/* Check the validity of parameters */
	if (argc < min_lastargc) {
		return -ERANGE;
	}

	/* Parsing parameters */
	if (strcmp(argv[3], "vif") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[3]:%s\n",
			__func__, argv[3]);
		return -EINVAL;
	}
	if ((vif_idx = bes_common_vif_idx(rwnx_hw, (char *)argv[4])) < 0 || (vif_idx >= NX_ITF_MAX)) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[4]:%s\n",
			__func__, argv[4]);
		return -EINVAL;
	}
	if ((!(fl = &cfl_map.fl_tbl[vif_idx][wfl_idx])) ||
	    ((pos = bes_custom_filter_sysfs_get_add_pos(fl, argv, argc, min_lastargc)) < 0)) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to get add pos\n", __func__);
		return -EINVAL;
	}

	/* Insert ns offload filter to cfl_map */
	ret = bes_filter_map_insert_ipv6_ns(rwnx_hw, &cfl_map, pos, vif_idx, NS_OFFLOAD_FUNC_IDX);
	if (ret) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR(%d): Fail to add ns on vif %s after %dst item\n",
			__func__, ret, argv[4], pos);
		return ret;
	}
	bes_dbg(BES_DBG_CUSTOM_FILTER, "%s Sucess: add ns on vif %s after %dst item\n",
		__func__, argv[4], pos);

	return 0;
}

static int bes_custom_filter_sysfs_add_ipv4_ip(struct rwnx_hw *rwnx_hw,
					       const char **argv, int argc)
{
	const int min_lastargc = 11;
	const u8 wfl_idx = FL_IP_ADDR;
	int ret = 0;
	int vif_idx = 0;
	u8 wf_type = 0, action = 0;
	int pos = 0;
	struct bes_fl *fl = NULL;
	u8 match_mode = 0;
	u8 ip_tmp[4];
	u32 *ip = (u32 *)ip_tmp;

	/* Format Example:
	 * add ipv4 src/dst Value/NValue(192.168.31.199) with accept/reject on vif wlan0
	 * (at head/tail)/(after 1st/2nd/3rd/Nth item)
	 */

	/* Check the validity of parameters */
	if (argc < min_lastargc) {
		return -ERANGE;
	}

	/* Parsing parameters */
	if (strcmp(argv[2], "src") == 0) {
		wf_type = FILTER_IPV4_SRC;
	} else if (strcmp(argv[2], "dst") == 0) {
		wf_type = FILTER_IPV4_DST;
	} else {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[2]:%s\n",
			__func__, argv[2]);
		return -EINVAL;
	}
	if ((strncmp(argv[3], "Value", 5) == 0) &&
	    (sscanf(argv[3], "Value(%hhu.%hhu.%hhu.%hhu)",
		    &ip_tmp[0], &ip_tmp[1], &ip_tmp[2], &ip_tmp[3]) == 4)) {
		match_mode = RXF_MATCH_EQUAL;
	} else if ((strncmp(argv[3], "NValue", 6) == 0) &&
		   (sscanf(argv[3], "NValue(%hhu.%hhu.%hhu.%hhu)",
		    &ip_tmp[0], &ip_tmp[1], &ip_tmp[2], &ip_tmp[3]) == 4)) {
		match_mode = RXF_MATCH_NOT_EQUAL;
	} else {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[3]:%s\n",
			__func__, argv[3]);
		return -EINVAL;
	}
	if (strcmp(argv[4], "with") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[4]:%s\n",
			__func__, argv[4]);
		return -EINVAL;
	}
	if (strcmp(argv[5], "accept") == 0) {
		action = RXF_ACCEPT;
	} else if (strcmp(argv[5], "reject") == 0) {
		action = RXF_REJECT;
	} else {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[5]:%s\n",
			__func__, argv[5]);
		return -EINVAL;
	}
	if (strcmp(argv[7], "vif") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[7]:%s\n",
			__func__, argv[7]);
		return -EINVAL;
	}
	if ((vif_idx = bes_common_vif_idx(rwnx_hw, (char *)argv[8])) < 0 || (vif_idx >= NX_ITF_MAX)) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[8]:%s\n",
			__func__, argv[8]);
		return -EINVAL;
	}
	if ((!(fl = &cfl_map.fl_tbl[vif_idx][wfl_idx])) ||
	    ((pos = bes_custom_filter_sysfs_get_add_pos(fl, argv, argc, min_lastargc)) < 0)) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to get add pos\n", __func__);
		return -EINVAL;
	}

	/* Insert ipv4_addr filter to cfl_map */
	ret = bes_filter_map_insert_ipv4_addr(rwnx_hw, &cfl_map, pos, vif_idx,
					      wf_type, ip, match_mode, action);
	if (ret) {
		bes_err(BES_DBG_CUSTOM_FILTER,
			"%s ERROR(%d): Fail to add ipv4 %s %pI4 with %s on vif %s after %dst item\n",
			__func__, ret, argv[2], ip, argv[5], argv[8], pos);
		return ret;
	}
	bes_dbg(BES_DBG_CUSTOM_FILTER, "%s Sucess: add ipv4 %s %pI4 with %s on vif %s after %dst item\n",
		__func__, argv[2], ip, argv[5], argv[8], pos);

	return 0;
}

static int bes_custom_filter_sysfs_add_ipv4_proto(struct rwnx_hw *rwnx_hw,
						  const char **argv, int argc)
{
	const int min_lastargc = 11;
	const u8 wfl_idx = FL_PROTO;
	int ret = 0;
	int vif_idx = 0;
	u8 action = 0;
	int pos = 0;
	struct bes_fl *fl = NULL;
	u8 match_mode = 0;
	u32 proto = 0;

	/* Format Example:
	 * add ipv4 proto Value/NValue(5) with accept/reject on vif wlan0
	 * (at head/tail)/(after 1st/2nd/3rd/Nth item)
	 */

	/* Check the validity of parameters */
	if (argc < min_lastargc) {
		return -ERANGE;
	}

	/* Parsing parameters */
	if ((strncmp(argv[3], "Value", 5) == 0) && (sscanf(argv[3], "Value(%d)", &proto) == 1)) {
		match_mode = RXF_MATCH_EQUAL;
	} else if ((strncmp(argv[3], "NValue", 6) == 0) && (sscanf(argv[3], "NValue(%d)", &proto) == 1)) {
		match_mode = RXF_MATCH_NOT_EQUAL;
	} else {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[3]:%s\n", __func__, argv[3]);
		return -EINVAL;
	}
	if (strcmp(argv[4], "with") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[4]:%s\n", __func__, argv[4]);
		return -EINVAL;
	}
	if (strcmp(argv[5], "accept") == 0) {
		action = RXF_ACCEPT;
	} else if (strcmp(argv[5], "reject") == 0) {
		action = RXF_REJECT;
	} else {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[5]:%s\n", __func__, argv[5]);
		return -EINVAL;
	}
	if (strcmp(argv[7], "vif") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[7]:%s\n", __func__, argv[7]);
		return -EINVAL;
	}
	if ((vif_idx = bes_common_vif_idx(rwnx_hw, (char *)argv[8])) < 0 || (vif_idx >= NX_ITF_MAX)) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[8]:%s\n", __func__, argv[8]);
		return -EINVAL;
	}
	if ((!(fl = &cfl_map.fl_tbl[vif_idx][wfl_idx])) ||
	    ((pos = bes_custom_filter_sysfs_get_add_pos(fl, argv, argc, min_lastargc)) < 0)) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to get add pos\n", __func__);
		return -EINVAL;
	}

	/* Insert ipv4_proto filter to cfl_map */
	ret = bes_filter_map_insert_ipv4_proto(rwnx_hw, &cfl_map, pos, vif_idx, (u8)proto, match_mode, action);
	if (ret) {
		bes_err(BES_DBG_CUSTOM_FILTER,
			"%s ERROR(%d): Fail to add ipv4 proto %s(%d) with %s on vif %s after %dst item\n",
			__func__, ret, match_mode == RXF_MATCH_EQUAL ? "Value" : "NValue",
			proto, argv[5], argv[8], pos);
		return ret;
	}
	bes_dbg(BES_DBG_CUSTOM_FILTER,
		"%s Sucess: add ipv4 proto %s(%d) with %s on vif %s after %dst item\n", __func__,
		match_mode == RXF_MATCH_EQUAL ? "Value" : "NValue", proto, argv[5], argv[8], pos);

	return 0;
}

static int bes_custom_filter_get_match_type_param(const char *str, u8 wf_type_single,
	u8 wf_type_range, u8 *wf_type, u8 *match_mode, u32 *s_port, u32 *d_port)
{
	if ((strncmp(str, "Value", 5) == 0) && (sscanf(str, "Value(%d)", s_port) == 1)) {
		*wf_type = wf_type_single;
		*match_mode = RXF_MATCH_EQUAL;
	} else if ((strncmp(str, "NValue", 6) == 0) && (sscanf(str, "NValue(%d)", s_port) == 1)) {
		*wf_type = wf_type_single;
		*match_mode = RXF_MATCH_NOT_EQUAL;
	} else if ((strncmp(str, "Range", 5) == 0) && (sscanf(str, "Range(%d,%d)", s_port, d_port) == 2)) {
		*wf_type = wf_type_range;
		*match_mode = RXF_MATCH_RANGE;
	} else if ((strncmp(str, "ORange", 6) == 0) && (sscanf(str, "ORange(%d,%d)", s_port, d_port) == 2)) {
		*wf_type = wf_type_range;
		*match_mode = RXF_MATCH_OUT_OF_RANGE;
	} else {
		return -EINVAL;
	}

	return 0;
}

static int bes_custom_filter_sysfs_add_ipv4_port(struct rwnx_hw *rwnx_hw, const char **argv, int argc)
{
	const int min_lastargc = 12;
	u8 wfl_idx = -1;
	int ret = 0;
	int vif_idx = 0;
	u8 wf_type = 0, action = 0;
	int pos = 0;
	struct bes_fl *fl = NULL;
	u8 match_mode = 0;
	u32 s_port = 0, d_port = 0;

	/* Format Example:
	 * add ipv4 udp/tcp src/dst Value(500) with accept/reject on vif wlan0
	 * (at head/tail)/(after 1st/2nd/3rd/Nth item)
	 * add ipv4 udp/tcp src/dst Range(500,600) with accept/reject on vif wlan0
	 * (at head/tail)/(after 1st/2nd/3rd/Nth item)
	 */

	/* Check the validity of parameters */
	if (argc < min_lastargc) {
		return -ERANGE;
	}

	/* Parsing parameters */
	if ((strcmp(argv[2], "udp") == 0) && (strcmp(argv[3], "src") == 0)) {
		wfl_idx = FL_UDP_PORT;
		ret = bes_custom_filter_get_match_type_param(argv[4], FILTER_IPV4_UDP_SRC_SINGLE,
							     FILTER_IPV4_UDP_SRC_RANGE,
							     &wf_type, &match_mode, &s_port, &d_port);
	} else if ((strcmp(argv[2], "udp") == 0) && (strcmp(argv[3], "dst") == 0)) {
		wfl_idx = FL_UDP_PORT;
		ret = bes_custom_filter_get_match_type_param(argv[4], FILTER_IPV4_UDP_DST_SINGLE,
							     FILTER_IPV4_UDP_DST_RANGE,
							     &wf_type, &match_mode, &s_port, &d_port);
	} else if ((strcmp(argv[2], "tcp") == 0) && (strcmp(argv[3], "src") == 0)) {
		wfl_idx = FL_TCP_PORT;
		ret = bes_custom_filter_get_match_type_param(argv[4], FILTER_IPV4_TCP_SRC_SINGLE,
							     FILTER_IPV4_TCP_SRC_RANGE,
							     &wf_type, &match_mode, &s_port, &d_port);
	} else if ((strcmp(argv[2], "tcp") == 0) && (strcmp(argv[3], "dst") == 0)) {
		wfl_idx = FL_TCP_PORT;
		ret = bes_custom_filter_get_match_type_param(argv[4], FILTER_IPV4_TCP_DST_SINGLE,
							     FILTER_IPV4_TCP_DST_RANGE,
							     &wf_type, &match_mode, &s_port, &d_port);
	} else {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[3]:%s\n",
			__func__, argv[3]);
		return -EINVAL;
	}
	if (ret) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[4]:%s\n",
			__func__, argv[4]);
		return -EINVAL;
	}
	if (((wf_type == FILTER_IPV4_UDP_SRC_RANGE) || (wf_type == FILTER_IPV4_UDP_DST_RANGE) ||
	     (wf_type == FILTER_IPV4_TCP_SRC_RANGE) || (wf_type == FILTER_IPV4_TCP_DST_RANGE)) &&
	    (s_port >= d_port)) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s fail to parse. not support s_port >= d_port : %d >= %d\n",
			__func__, s_port, d_port);
		return -EINVAL;
	}
	if (strcmp(argv[5], "with") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[5]:%s\n",
			__func__, argv[5]);
		return -EINVAL;
	}
	if (strcmp(argv[6], "accept") == 0) {
		action = RXF_ACCEPT;
	} else if (strcmp(argv[6], "reject") == 0) {
		action = RXF_REJECT;
	} else {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[6]:%s\n",
			__func__, argv[6]);
		return -EINVAL;
	}
	if (strcmp(argv[8], "vif") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[8]:%s\n",
			__func__, argv[8]);
		return -EINVAL;
	}
	if ((vif_idx = bes_common_vif_idx(rwnx_hw, (char *)argv[9])) < 0 || (vif_idx >= NX_ITF_MAX)) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[9]:%s\n",
			__func__, argv[9]);
		return -EINVAL;
	}
	if ((!(fl = &cfl_map.fl_tbl[vif_idx][wfl_idx])) ||
	    ((pos = bes_custom_filter_sysfs_get_add_pos(fl, argv, argc, min_lastargc)) < 0)) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to get add pos\n", __func__);
		return -EINVAL;
	}

	/* Insert port filter to cfl_map */
	ret = bes_filter_map_insert_to_port_list(rwnx_hw, &cfl_map, pos, vif_idx, wf_type,
						 s_port, d_port, match_mode, action);
	if (ret) {
		if (match_mode == RXF_MATCH_EQUAL || match_mode == RXF_MATCH_NOT_EQUAL) {
			bes_err(BES_DBG_CUSTOM_FILTER,
				"%s ERROR(%d): Fail to ipv4 %s %s %s(%d) with %s on vif %s after %dst item\n",
				__func__, ret, argv[2], argv[3], match_mode == RXF_MATCH_EQUAL ? "Value" : "NValue",
				s_port, argv[6], argv[9], pos);
		} else {
			bes_err(BES_DBG_CUSTOM_FILTER,
				"%s ERROR(%d): Fail to ipv4 %s %s %s(%d,%d) with %s on vif %s after %dst item\n",
				__func__, ret, argv[2], argv[3], match_mode == RXF_MATCH_RANGE ? "Range" : "ORange",
				s_port, d_port, argv[6], argv[9], pos);
		}
		return ret;
	}
	if (match_mode == RXF_MATCH_EQUAL || match_mode == RXF_MATCH_NOT_EQUAL) {
		bes_dbg(BES_DBG_CUSTOM_FILTER,
			"%s Sucess: Fail to ipv4 %s %s %s(%d) with %s on vif %s after %dst item\n",
			__func__, argv[2], argv[3], match_mode == RXF_MATCH_EQUAL ? "Value" : "NValue",
			s_port, argv[6], argv[9], pos);
	} else {
		bes_dbg(BES_DBG_CUSTOM_FILTER,
			"%s Sucess: Fail to ipv4 %s %s %s(%d,%d) with %s on vif %s after %dst item\n",
			__func__, argv[2], argv[3], match_mode == RXF_MATCH_RANGE ? "Range" : "ORange",
			s_port, d_port, argv[6], argv[9], pos);
	}

	return 0;
}

static int bes_custom_filter_sysfs_add_ipv4(struct rwnx_hw *rwnx_hw, const char **argv, int argc)
{
	int ret = 0;

	/* Check the validity of parameters */
	if (argc < 3) {
		return -ERANGE;
	}

	/* Parsing parameters */
	if ((strcmp(argv[2], "src") == 0) || (strcmp(argv[2], "dst") == 0)) {
		ret = bes_custom_filter_sysfs_add_ipv4_ip(rwnx_hw, argv, argc);
	} else if (strcmp(argv[2], "proto") == 0) {
		ret = bes_custom_filter_sysfs_add_ipv4_proto(rwnx_hw, argv, argc);
	} else if ((strcmp(argv[2], "udp") == 0) || (strcmp(argv[2], "tcp") == 0)) {
		ret = bes_custom_filter_sysfs_add_ipv4_port(rwnx_hw, argv, argc);
	} else {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to get add ipv4 type. argv[2]:%s\n",
			__func__, argv[2]);
		return -EINVAL;
	}

	return ret;
}

static int bes_custom_filter_sysfs_add_ipv6_ip(struct rwnx_hw *rwnx_hw, const char **argv, int argc)
{
	const int min_lastargc = 11;
	const u8 wfl_idx = FL_IP_ADDR;
	int ret = 0;
	int vif_idx = 0;
	u8 wf_type = 0, action = 0;
	int pos = 0;
	struct bes_fl *fl = NULL;
	u8 match_mode = 0;
	struct in6_addr ipv6_addr;
	char *addr_start = NULL, *addr_end = NULL;
	char addr[40] = "\0"; /* The length of the ipv6 address string (including '\0') should not exceed 40 */

	/* Format Example: add ipv6 src/dst Value/NValue(fe80::2812:2a85:f2b:cd82) with
	 * accept/reject on vif wlan0 (at head/tail)/(after 1st/2nd/3rd/Nth item) */

	/* Check the validity of parameters */
	if (argc < min_lastargc) {
		return -ERANGE;
	}

	/* Parsing parameters */
	if (strcmp(argv[2], "src") == 0) {
		wf_type = FILTER_IPV6_SRC;
	} else if (strcmp(argv[2], "dst") == 0) {
		wf_type = FILTER_IPV6_DST;
	} else {
		bes_err(BES_DBG_WOWLAN, "%s ERROR: fail to parse. argv[2]:%s\n",
			__func__, argv[2]);
		return -EINVAL;
	}

	if ((strncmp(argv[3], "Value", 5) == 0) &&
	    (addr_start = strstr(argv[3], "(")) &&
	    (addr_end = strstr(argv[3], ")")) &&
	    (addr_end - addr_start <= 40) &&
	    (strncpy(addr, (char *)(addr_start + 1), 39)) &&
	    (in6_pton(addr, -1, (u8 *)&ipv6_addr, -1, NULL) == 1)) {
		match_mode = RXF_MATCH_EQUAL;
	} else if ((strncmp(argv[3], "NValue", 6) == 0) &&
		   (addr_start = strstr(argv[3], "(")) &&
		   (addr_end = strstr(argv[3], ")")) &&
		   (addr_end - addr_start <= 40) &&
		   (strncpy(addr, (char *)(addr_start + 1), 39)) &&
		   (in6_pton(addr, -1, (u8 *)&ipv6_addr, -1, NULL) == 1)) {
		match_mode = RXF_MATCH_NOT_EQUAL;
	} else {
		bes_err(BES_DBG_WOWLAN, "%s ERROR: fail to parse. argv[3]:%s\n",
			__func__, argv[3]);
		return -EINVAL;
	}

	if (strcmp(argv[4], "with") != 0) {
		bes_err(BES_DBG_WOWLAN, "%s ERROR: fail to parse. argv[4]:%s\n",
			__func__, argv[4]);
		return -EINVAL;
	}
	if (strcmp(argv[5], "accept") == 0) {
		action = RXF_ACCEPT;
	} else if (strcmp(argv[5], "reject") == 0) {
		action = RXF_REJECT;
	} else {
		bes_err(BES_DBG_WOWLAN, "%s ERROR: fail to parse. argv[5]:%s\n",
			__func__, argv[5]);
		return -EINVAL;
	}
	if (strcmp(argv[7], "vif") != 0) {
		bes_err(BES_DBG_WOWLAN, "%s ERROR: fail to parse. argv[7]:%s\n",
			__func__, argv[7]);
		return -EINVAL;
	}
	if ((vif_idx = bes_common_vif_idx(rwnx_hw, (char *)argv[8])) < 0 || (vif_idx >= NX_ITF_MAX)) {
		bes_err(BES_DBG_WOWLAN, "%s ERROR: fail to parse. argv[8]:%s\n", __func__, argv[8]);
		return -EINVAL;
	}
	if ((!(fl = &cfl_map.fl_tbl[vif_idx][wfl_idx])) ||
	    ((pos = bes_custom_filter_sysfs_get_add_pos(fl, argv, argc, min_lastargc)) < 0)) {
		bes_err(BES_DBG_WOWLAN, "%s ERROR: fail to get add pos\n", __func__);
		return -EINVAL;
	}

	/* Insert filter to ip_addr list */
	ret = bes_filter_map_insert_ipv6_addr(rwnx_hw, &cfl_map, pos, vif_idx, wf_type,
					      &ipv6_addr, match_mode, action);
	if (ret) {
		bes_err(BES_DBG_WOWLAN,
			"%s ERROR(%d): Fail to add ipv6 %s %pI6 with %s on vif %s after %dst item\n",
			__func__, ret, argv[2], &ipv6_addr, argv[5], argv[8], pos);
		return ret;
	}
	bes_dbg(BES_DBG_WOWLAN, "%s Sucess: add ipv6 %s %pI6 with %s on vif %s after %dst item\n",
		__func__, argv[2], &ipv6_addr, argv[5], argv[8], pos);

	return 0;
}

static int bes_custom_filter_sysfs_add_ipv6(struct rwnx_hw *rwnx_hw,
					    const char **argv, int argc)
{
	int ret = 0;

	/* Check the validity of parameters */
	if (argc < 3) {
		return -ERANGE;
	}

	/* Parsing parameters */
	if ((strcmp(argv[2], "src") == 0) || (strcmp(argv[2], "dst") == 0)) {
		ret = bes_custom_filter_sysfs_add_ipv6_ip(rwnx_hw, argv, argc);
	} else {
		bes_err(BES_DBG_WOWLAN, "%s ERROR: fail to get add ipv6 type. argv[2]:%s\n",
			__func__, argv[2]);
		return -EINVAL;
	}

	return ret;
}

static int bes_custom_filter_sysfs_add_multicast(struct rwnx_hw *rwnx_hw,
						 const char **argv, int argc)
{
	const int min_lastargc = 11;
	const u8 wfl_idx = FL_MAC_ADDR;
	int ret = 0;
	int vif_idx = 0;
	u8 wf_type = 0;
	u8 action = 0;
	int pos = 0;
	struct bes_fl *fl = NULL;

	/* Format Example: add multicast for ipv4/ipv6/all with accept/reject on vif wlan0
	 * (at head/tail)/(after 1st/2nd/3rd/Nth item) */

	/* Check the validity of parameters */
	if (argc < min_lastargc) {
		return -ERANGE;
	}

	/* Parsing parameters */
	if (strcmp(argv[2], "for") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[2]:%s\n",
			__func__, argv[2]);
		return -EINVAL;
	}
	if (strcmp(argv[3], "ipv4") == 0) {
		wf_type = FILTER_IPV4_MULTICAST;
	} else if (strcmp(argv[3], "ipv6") == 0) {
		wf_type = FILTER_IPV6_MULTICAST;
	} else if (strcmp(argv[3], "all") == 0) {
		wf_type = FILTER_ALL_MULTICAST;
	} else {
		bes_err(BES_DBG_WOWLAN, "%s ERROR: fail to parse. argv[3]:%s\n",
			__func__, argv[3]);
		return -EINVAL;
	}
	if (strcmp(argv[4], "with") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[4]:%s\n",
			__func__, argv[4]);
		return -EINVAL;
	}
	if (strcmp(argv[5], "accept") == 0) {
		action = RXF_ACCEPT;
	} else if (strcmp(argv[5], "reject") == 0) {
		action = RXF_REJECT;
	} else {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[5]:%s\n",
			__func__, argv[5]);
		return -EINVAL;
	}
	if (strcmp(argv[7], "vif") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[7]:%s\n",
			__func__, argv[7]);
		return -EINVAL;
	}
	if ((vif_idx = bes_common_vif_idx(rwnx_hw, (char *)argv[8])) < 0 || (vif_idx >= NX_ITF_MAX)) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[8]:%s\n",
			__func__, argv[8]);
		return -EINVAL;
	}
	if ((!(fl = &cfl_map.fl_tbl[vif_idx][wfl_idx])) ||
	    ((pos = bes_custom_filter_sysfs_get_add_pos(fl, argv, argc, min_lastargc)) < 0)) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to get add pos\n", __func__);
		return -EINVAL;
	}

	/* Insert multicast filter to cfl_map */
	ret = bes_filter_map_insert_multicast_filter(rwnx_hw, &cfl_map, pos, vif_idx, wf_type, action);
	if (ret) {
		bes_err(BES_DBG_CUSTOM_FILTER,
			"%s ERROR(%d): Fail to add multicast for %s with %s on vif %s after %dst item\n",
			__func__, ret, argv[2], argv[5], argv[8], pos);
		return ret;
	}
	bes_dbg(BES_DBG_CUSTOM_FILTER, "%s Sucess: add multicast for %s with %s on vif %s after %dst item\n",
		__func__, argv[2], argv[5], argv[8], pos);

	return 0;
}

static int bes_custom_filter_sysfs_add_magic_packet(struct rwnx_hw *rwnx_hw,
						    const char **argv, int argc)
{
	const int min_lastargc = 9;
	const u8 wfl_idx = FL_MAC_ADDR;
	int ret = 0;
	int vif_idx = 0;
	u8 action = 0;
	int pos = 0;
	struct bes_fl *fl = NULL;
	struct rwnx_vif *rwnx_vif = NULL;

	/* Format Example: add magic_packet with accept/reject on vif wlan0
	 * (at head/tail)/(after 1st/2nd/3rd/Nth item) */

	/* Check the validity of parameters */
	if (argc < min_lastargc) {
		return -ERANGE;
	}

	/* Parsing parameters */
	if (strcmp(argv[2], "with") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[2]:%s\n",
			__func__, argv[2]);
		return -EINVAL;
	}
	if (strcmp(argv[3], "accept") == 0) {
		action = RXF_ACCEPT;
	} else if (strcmp(argv[3], "reject") == 0) {
		action = RXF_REJECT;
	} else {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[3]:%s\n",
			__func__, argv[3]);
		return -EINVAL;
	}
	if (strcmp(argv[5], "vif") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[5]:%s\n",
			__func__, argv[5]);
		return -EINVAL;
	}
	if ((vif_idx = bes_common_vif_idx(rwnx_hw, (char *)argv[6])) < 0 || (vif_idx >= NX_ITF_MAX)) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[6]:%s\n",
			__func__, argv[6]);
		return -EINVAL;
	}
	if ((!(fl = &cfl_map.fl_tbl[vif_idx][wfl_idx])) ||
	    ((pos = bes_custom_filter_sysfs_get_add_pos(fl, argv, argc, min_lastargc)) < 0)) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to get add pos\n", __func__);
		return -EINVAL;
	}

	/* Get vif mac */
	rwnx_vif = rwnx_rx_get_vif(rwnx_hw, vif_idx);
	if (!rwnx_vif || !rwnx_vif->ndev) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to get vif mac. vif:%d\n",
			__func__, vif_idx);
		return -EINVAL;
	}

	/* Insert magic_packet filter to cfl_map */
	ret = bes_filter_map_insert_magic_packet(rwnx_hw, &cfl_map, pos, vif_idx,
			(struct mac_addr *)rwnx_vif->ndev->dev_addr, action);
	if (ret) {
		bes_err(BES_DBG_CUSTOM_FILTER,
			"%s ERROR(%d): Fail to add magic_packet with %s on vif %s after %dst item\n",
			__func__, ret, argv[3], argv[6], pos);
		return ret;
	}
	bes_dbg(BES_DBG_CUSTOM_FILTER, "%s Sucess: add magic_packet with %s on vif %s after %dst item\n",
		__func__, argv[3], argv[6], pos);

	return 0;
}

static int bes_custom_filter_sysfs_add(struct rwnx_hw *rwnx_hw, const char **argv, int argc)
{
	int ret = 0;

	/* Check the validity of parameters */
	if (argc < 2) {
		return -ERANGE;
	}

	/* Parsing parameters */
	if (strcmp(argv[1], "mac") == 0) {
		ret = bes_custom_filter_sysfs_add_mac(rwnx_hw, argv, argc);
	} else if (strcmp(argv[1], "eth") == 0) {
		ret = bes_custom_filter_sysfs_add_eth(rwnx_hw, argv, argc);
	} else if (strcmp(argv[1], "arp") == 0) {
		ret = bes_custom_filter_sysfs_add_arp(rwnx_hw, argv, argc);
	} else if (strcmp(argv[1], "ns") == 0) {
		ret = bes_custom_filter_sysfs_add_ns(rwnx_hw, argv, argc);
	} else if (strcmp(argv[1], "ipv4") == 0) {
		ret = bes_custom_filter_sysfs_add_ipv4(rwnx_hw, argv, argc);
	} else if (strcmp(argv[1], "ipv6") == 0) {
		ret = bes_custom_filter_sysfs_add_ipv6(rwnx_hw, argv, argc);
	} else if (strcmp(argv[1], "multicast") == 0) {
		ret = bes_custom_filter_sysfs_add_multicast(rwnx_hw, argv, argc);
	} else if (strcmp(argv[1], "magic_packet") == 0) {
		ret = bes_custom_filter_sysfs_add_magic_packet(rwnx_hw, argv, argc);
	} else {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to get add type. argv[1]:%s\n",
			__func__, argv[1]);
		return -EINVAL;
	}

	return ret;
}

static int bes_custom_filter_sysfs_del(struct rwnx_hw *rwnx_hw, const char **argv, int argc)
{
	const int min_lastargc = 9;
	int ret = 0;
	int vif_idx = 0, pos_num = 0, tbl_idx = 0;
	struct bes_fl *fl = NULL;

	/* Format Example: del item 5 from ip_addr table of vif wlan0 */

	/* Check the validity of parameters */
	if (argc < min_lastargc) {
		return -ERANGE;
	}

	/* Parsing parameters */
	if (strcmp(argv[1], "item") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[1]:%s\n",
			__func__, argv[1]);
		return -EINVAL;
	}
	if (sscanf(argv[2], "%d", &pos_num) < 1) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[2]:%s\n",
			__func__, argv[2]);
		return -EINVAL;
	}
	if (strcmp(argv[3], "from") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[3]:%s\n",
			__func__, argv[3]);
		return -EINVAL;
	}
	if ((tbl_idx = bes_filter_map_fl_type_idx(argv[4])) < 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[4]:%s\n",
			__func__, argv[4]);
		return -EINVAL;
	}
	if (strcmp(argv[5], "table") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[5]:%s\n",
			__func__, argv[5]);
		return -EINVAL;
	}
	if (strcmp(argv[7], "vif") != 0) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[7]:%s\n",
			__func__, argv[7]);
		return -EINVAL;
	}
	if ((vif_idx = bes_common_vif_idx(rwnx_hw, (char *)argv[8])) < 0 || (vif_idx >= NX_ITF_MAX)) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to parse. argv[8]:%s\n",
			__func__, argv[8]);
		return -EINVAL;
	}
	if (!(fl = &cfl_map.fl_tbl[vif_idx][tbl_idx])) {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to get fl\n", __func__);
		return -EINVAL;
	}

	/* Del item from filter table */
	ret = bes_filter_map_delete_by_pos(rwnx_hw, &cfl_map, fl, pos_num);
	if (ret) {
		bes_err(BES_DBG_CUSTOM_FILTER,
			"%s ERROR(%d): Fail to del item %d from %s table of vif %s\n",
			__func__, ret, pos_num, argv[4], argv[8]);
		return ret;
	}
	bes_dbg(BES_DBG_CUSTOM_FILTER,
		"%s Sucess: del item %d from %s table of vif %s\n", __func__,
		pos_num, argv[4], argv[8]);

	return 0;
}

int bes_custom_filter_sysfs_ctrl(struct rwnx_hw *rwnx_hw, char *buf)
{
	int ret = 0;
	char *argv[20];
	int argc = 0;

	/* Check the validity of parameters */
	if (!rwnx_hw || !buf) {
		bes_err(BES_DBG_CUSTOM_FILTER,
			"%s ERROR: rwnx_hw or buf is NULL\n", __func__);
		return -EINVAL;
	}

	/* Check wlan_power_ctrl.power_status */
	if (!bes_get_wlan_enabled(rwnx_hw)) {
		bes_err(BES_DBG_CUSTOM_FILTER,
			"%s ERROR: wlan_power_ctrl.power_status is false\n", __func__);
		return -EINVAL;
	}

	/* Parsing command line */
	argc = bes_common_parse_line(buf, argv, CUSTOM_FILTER_MAXARGS);
	if (argc == 0) {
		bes_err(BES_DBG_CUSTOM_FILTER,
			"%s ERROR: fail to parse line. argc:0\n", __func__);
		return -EINVAL;
	}

	/* If mode is WOWLAN_STANDARD , only the set operation is supported */
	if ((rwnx_hw->wowlan.mode == WOWLAN_MODE_STANDARD) && (strcmp(argv[0], "set") != 0)) {
		bes_warn(BES_DBG_CUSTOM_FILTER,
			"%s WARN: WOWLAN_STANDARD mode is not support %s operation... \n",
			__func__, argv[0]);
		return -EFAULT;
	}

	/* Parsing parameters */
	mutex_lock(&rwnx_hw->filter.mutex);
	if (strcmp(argv[0], "enable") == 0) {
		ret = bes_custom_filter_sysfs_enable(rwnx_hw, (const char **)argv, argc);
	} else if (strcmp(argv[0], "sync") == 0) {
		ret = bes_custom_filter_sysfs_sync(rwnx_hw, (const char **)argv, argc);
	} else if (strcmp(argv[0], "disable") == 0) {
		ret = bes_custom_filter_sysfs_disable(rwnx_hw, (const char **)argv, argc);
	} else if (strcmp(argv[0], "reset") == 0) {
		ret = bes_custom_filter_sysfs_reset(rwnx_hw, (const char **)argv, argc);
	} else if (strcmp(argv[0], "switch") == 0) {
		ret = bes_custom_filter_sysfs_switch(rwnx_hw, (const char **)argv, argc);
	} else if (strcmp(argv[0], "bind") == 0) {
		ret = bes_custom_filter_sysfs_bind(rwnx_hw, (const char **)argv, argc);
	} else if (strcmp(argv[0], "set") == 0) {
		ret = bes_custom_filter_sysfs_set(rwnx_hw, (const char **)argv, argc);
	} else if (strcmp(argv[0], "add") == 0) {
		ret = bes_custom_filter_sysfs_add(rwnx_hw, (const char **)argv, argc);
	} else if (strcmp(argv[0], "del") == 0) {
		ret = bes_custom_filter_sysfs_del(rwnx_hw, (const char **)argv, argc);
	} else {
		bes_err(BES_DBG_CUSTOM_FILTER, "%s ERROR: fail to get ctrl type. argv[0]:%s\n",
			__func__, argv[0]);
		ret = -EINVAL;
	}
	mutex_unlock(&rwnx_hw->filter.mutex);

	return ret;
}

int bes_custom_filter_sysfs_show(struct rwnx_hw *rwnx_hw, char *buf)
{
	int len = 0;
	char *bref_info = NULL;

	/* Check the validity of parameters */
	if (!rwnx_hw || !buf) {
		bes_err(BES_DBG_CUSTOM_FILTER,
			"%s ERROR: rwnx_hw or buf is NULL\n", __func__);
		return -EINVAL;
	}

	/* Check wlan_power_ctrl.power_status */
	if (!bes_get_wlan_enabled(rwnx_hw)) {
		bes_err(BES_DBG_CUSTOM_FILTER,
			"%s ERROR: wlan_power_ctrl.power_status is false\n", __func__);
		return 0;
	}

	/* Malloc temporary memory */
	bref_info = kmalloc(80, GFP_ATOMIC);
	if (!bref_info) {
		bes_err(BES_DBG_WOWLAN, "%s Error: fail to malloc", __func__);
		return 0;
	}

	/* Show filter config */
	len += sprintf(&buf[len], "-------- Filter Config --------\n");
	len += sprintf(&buf[len], "Opertation mode: %s\nMismatch action: %s\n",
		       rwnx_hw->wowlan.mode ==  WOWLAN_MODE_CUSTOM ? "CUSTOM" : "STANDARD",
		       (rwnx_hw->filter.mismatch_action == FILTER_ACTION_DISCARD) ? "DISCARD" : "ACCEPT");
	len += sprintf(&buf[len], "Capacity max: rxf_idx=%d rxf_arry_idx=%d\n",
			rwnx_hw->filter.max.rxf_idx, rwnx_hw->filter.max.rxf_arry_idx);
	len += sprintf(&buf[len], "\n");

	/* Show custom filter */
	len += sprintf(&buf[len], "-------- Custom Filters --------\n");
	len += bes_filter_map_show(rwnx_hw,  &cfl_map, bref_info, buf, len);

	/* Show internal filter */
	len += sprintf(&buf[len], "-------- Internal Filters --------\n");
	len += bes_filter_map_show(rwnx_hw,  &rwnx_hw->filter.ifl_map, bref_info, buf, len);

	/* Free temporary memory */
	kfree(bref_info);

	return len;
}

void bes_custom_filter_init(struct rwnx_hw *rwnx_hw)
{
	/* init and disable custom filter map */
	bes_filter_map_init(&cfl_map);
	bes_filter_map_set_valid(&cfl_map, false);
}

void bes_custom_filter_deinit(struct rwnx_hw *rwnx_hw)
{
	/* deinit and disable custom filter map */
	bes_filter_map_deinit(&cfl_map);
	bes_filter_map_set_valid(&cfl_map, false);
}