/*
 * @file bes_wowlan.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BES_WOWLAN_
#define _BES_WOWLAN_


/* Max timout to wait for the delayed work to end */
#define BES_WOWLAN_DELAY_WORK_END_MAX_TIMEOUT 200

#define BES_WOWLAN_DATA_TX_DBG_BUF_MAX 100

#define BES_MAX_PATTERN_FILTERS (5)
#define BES_MAX_PATTERN_LEN (100)
#define BES_MAX_OFFSET_LEN (100)

#define BES_MAX_CHECK_ITEM_LEN (30)

enum wowlan_mode {
	WOWLAN_MODE_STANDARD = 0,
	WOWLAN_MODE_CUSTOM
};

enum parse_msg_step {
	PARSE_MSG_STEP_NONE = 0,
	PARSE_MSG_STEP_SET,
};

#ifdef CONFIG_BES_WOWLAN
void bes_wowlan_parse_data_pkt(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, struct sk_buff *skb);
void bes_wowlan_parse_mgmt_pkt(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, const struct sk_buff *skb);
void bes_wowlan_parse_msg(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg);
int bes_wowlan_suspend_cfg(struct rwnx_hw *rwnx_hw, struct cfg80211_wowlan *wow);
int bes_wowlan_resume_cfg(struct rwnx_hw *rwnx_hw);
void bes_wowlan_set_wakeup_source(struct rwnx_hw *rwnx_hw, bool on);
void bes_wowlan_init(struct rwnx_hw *rwnx_hw);
void bes_wowlan_deinit(struct rwnx_hw *rwnx_hw);
int bes_wowlan_set_filter(struct rwnx_hw *rwnx_hw, struct cfg80211_wowlan *wow);
int bes_wowlan_resume_filter(struct rwnx_hw *rwnx_hw);
int bes_wowlan_insert_at_head(struct rwnx_hw *rwnx_hw, struct list_head *list_set_head,
			      struct bes_capacity *stats, struct filter_item *item);
void bes_wowlan_msg_tx_info_print(struct rwnx_cmd_a2emsg *cmd_a2e_msg);
void bes_wowlan_data_tx_info_print(struct sk_buff *skb);
#else
static inline void bes_wowlan_parse_data_pkt(struct rwnx_hw *rwnx_hw,
	struct rwnx_vif *rwnx_vif, struct sk_buff *skb) { }
static inline void bes_wowlan_parse_mgmt_pkt(struct rwnx_hw *rwnx_hw,
	struct rwnx_vif *rwnx_vif, const struct sk_buff *skb) { }
static inline void bes_wowlan_parse_msg(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg) { }
static inline int bes_wowlan_suspend_cfg(struct rwnx_hw *rwnx_hw,
	struct cfg80211_wowlan *wow) { return 0; }
static inline int bes_wowlan_resume_cfg(struct rwnx_hw *rwnx_hw) { return 0; }
static inline void bes_wowlan_set_wakeup_source(struct rwnx_hw *rwnx_hw, bool on) { }
static inline void bes_wowlan_init(struct rwnx_hw *rwnx_hw) { }
static inline void bes_wowlan_deinit(struct rwnx_hw *rwnx_hw) { }
static inline int bes_wowlan_set_filter(struct rwnx_hw *rwnx_hw, struct cfg80211_wowlan *wow) { return 0; }
static inline int bes_wowlan_resume_filter(struct rwnx_hw *rwnx_hw) { return 0; }
static inline int bes_wowlan_insert_at_head(struct rwnx_hw *rwnx_hw, struct list_head *list_set_head,
			      struct bes_capacity *stats, struct filter_item *item) { return 0; }
static inline void bes_wowlan_msg_tx_info_print(struct rwnx_cmd_a2emsg *cmd_a2e_msg) { }
static inline void bes_wowlan_data_tx_info_print(struct sk_buff *skb) { }
#endif /* CONFIG_BES_WOWLAN */

#endif
