/*
 * @file bes_wpa_priv_cmd.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BES_WPA_PRIV_CMD_H_
#define _BES_WPA_PRIV_CMD_H_

#include <linux/wireless.h>
#include "rwnx_main.h"

#define CMD_MAXARGS 10
#define CMD_MAXALEN 20

#define RX_FILTER_TYPE_V4_MULTICAST 2
#define RX_FILTER_TYPE_V6_MULTICAST 3

#define BTCOEXMODE_ENABLE 0
#define BTCOEXMODE_DISABLE 1
#define BTCOEXMODE_SENSE 2

enum bes_wpa_priv_cmd_filter_type {
	PRIV_CMD_FILTER_TYPE_V4_MULTICAST = 0,
	PRIV_CMD_FILTER_TYPE_V6_MULTICAST
};

struct bes_wpa_cmd_map
{
	char cmd[CMD_MAXALEN];	// cmd
	int cmd_len;	// cmd length, used for effiency
	int (*cmd_handler) (struct rwnx_vif *vif, char *command, u32 max_len); // cmd handler
};

typedef struct _android_wifi_priv_cmd {
	char *buf;
	int used_len;
	int total_len;
} android_wifi_priv_cmd;

#ifdef CONFIG_COMPAT
typedef struct _compat_android_wifi_priv_cmd {
	compat_caddr_t buf;
	int used_len;
	int total_len;
} compat_android_wifi_priv_cmd;
#endif /* CONFIG_COMPAT */

#ifdef CONFIG_WPA_PRIV_CMD
void bes_wpa_priv_cmd_init(struct rwnx_hw *rwnx_hw);
int android_priv_cmd(struct net_device *net, struct ifreq *ifr, int cmd);
int bes_wpa_priv_cmd_add_mc_and_mdns_filter(struct rwnx_hw *rwnx_hw, struct list_head *list_set_head,
					   struct bes_capacity *stats);
void bes_wpa_priv_cmd_increase_event_cnt(struct rwnx_hw *rwnx_hw);
int bes_wpa_priv_cmd_update_screen_suspend_filters(struct rwnx_hw *rwnx_hw, bool suspend);
static inline void bes_wpa_priv_cmd_set_rxfilte(struct rwnx_vif *rwnx_vif, u8 rxfilter)
{
	rwnx_vif->priv_cmd.rxfilter = rxfilter;
}

static inline u8 bes_wpa_priv_cmd_get_rxfilte(struct rwnx_vif *rwnx_vif)
{
	return rwnx_vif->priv_cmd.rxfilter;
}
#else
static inline void bes_wpa_priv_cmd_init(struct rwnx_hw *rwnx_hw) {}
static inline int android_priv_cmd(struct net_device *net, struct ifreq *ifr, int cmd) { return 0; }
static inline int bes_wpa_priv_cmd_add_mc_and_mdns_filter(struct rwnx_hw *rwnx_hw, struct list_head *list_set_head,
							 struct bes_capacity *stats) { return 0; }
static inline void bes_wpa_priv_cmd_increase_event_cnt(struct rwnx_hw *rwnx_hw) {}
static inline int bes_wpa_priv_cmd_update_screen_suspend_filters(struct rwnx_hw *rwnx_hw, bool suspend) { return 0; }
static inline void bes_wpa_priv_cmd_set_rxfilte(struct rwnx_vif *rwnx_vif, u8 rxfilter) {}
static inline u8 bes_wpa_priv_cmd_get_rxfilte(struct rwnx_vif *rwnx_vif) { return 0; }
#endif
#endif /* _BES_WPA_PRIV_CMD_H_ */