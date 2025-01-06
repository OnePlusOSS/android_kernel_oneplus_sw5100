/*
 * @file bes_tx_loop.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __TX_LOOP_H__
#define __TX_LOOP_H__

#include "bes_psa.h"
#include "bes_log.h"
#include "rwnx_strs.h"

struct tx_loop_ind_timeout {
	u16 msg_req;
	unsigned long max_timeout;
	unsigned long end_time;
	struct rwnx_cmd_a2emsg *record_a2emsg;
};

#ifdef CONFIG_BES_TXLOOP
/* tx_loop mechanism related initialization/deinitialization function */
void bes_tx_loop_init(struct rwnx_plat *rwnx_plat);
void bes_tx_loop_deinit(struct rwnx_plat *rwnx_plat);

/* tx loop mechanism enable function and related state operation function */
bool bes_tx_loop_is_enabled(struct rwnx_plat *rwnx_plat);
void bes_tx_loop_force_close(struct rwnx_plat *rwnx_plat, bool need_warn);
void bes_tx_loop_set_dev_disconnect(struct rwnx_plat *rwnx_plat);
void bes_tx_loop_status_clear(struct rwnx_plat *rwnx_plat);
void bes_tx_loop_all_clear(struct rwnx_plat *rwnx_plat);

/* tx loop mechanism skb fetching function */
struct sk_buff *bes_tx_loop_get_skb(struct rwnx_plat *rwnx_plat);

/* tx loop mechanism related timer operation functions */
int bes_tx_loop_direct_send(struct rwnx_plat *rwnx_plat, struct rwnx_cmd_a2emsg *a2e_msg);
int bes_tx_loop_inc_pending_data_count(struct rwnx_plat *rwnx_plat);
void bes_tx_loop_dec_pending_data_count(struct rwnx_plat *rwnx_plat, int dec_num);
void bes_tx_loop_rx_handler(struct rwnx_hw *rwnx_hw, struct sk_buff *skb);

/* tx loop mechanism wait_dev_status operation function */
void bes_tx_loop_record_dev_status(struct rwnx_plat *rwnx_plat, u32 rd_status, bool target_val);
void bes_tx_loop_clear_dev_status(struct rwnx_plat *rwnx_plat);

/* tx_loop mechanism related error avoidance function */
void bes_tx_loop_exception_check(struct rwnx_plat *rwnx_plat);

/* tx_loop mechanism related ind timer function */
void bes_tx_loop_del_specific_ind_timer(struct rwnx_plat *rwnx_plat, lmac_msg_id_t msg_id);
#else
/* tx_loop mechanism related initialization/deinitialization function */
static inline void bes_tx_loop_init(struct rwnx_plat *rwnx_plat) { }
static inline void bes_tx_loop_deinit(struct rwnx_plat *rwnx_plat) { }

/* tx loop mechanism enable function and related state operation function */
static inline bool bes_tx_loop_is_enabled(struct rwnx_plat *rwnx_plat) { return 0; }
static inline void bes_tx_loop_force_close(struct rwnx_plat *rwnx_plat, bool need_warn) { }
static inline void bes_tx_loop_set_dev_disconnect(struct rwnx_plat *rwnx_plat) { }
static inline void bes_tx_loop_status_clear(struct rwnx_plat *rwnx_plat) { }
static inline void bes_tx_loop_all_clear(struct rwnx_plat *rwnx_plat)  { }

/* tx loop mechanism skb fetching function */
static inline struct sk_buff *bes_tx_loop_get_skb(struct rwnx_plat *rwnx_plat) { return NULL; }

/* tx loop mechanism related timer operation functions */
static inline int bes_tx_loop_direct_send(struct rwnx_plat *rwnx_plat, struct rwnx_cmd_a2emsg *a2e_msg) { return 0; }
static inline int bes_tx_loop_inc_pending_data_count(struct rwnx_plat *rwnx_plat) { return 0; }
static inline void bes_tx_loop_dec_pending_data_count(struct rwnx_plat *rwnx_plat, int dec_num) {}
static inline void bes_tx_loop_rx_handler(struct rwnx_hw *rwnx_hw, struct sk_buff *skb) { }

/* tx loop mechanism wait_dev_status operation function */
static inline void bes_tx_loop_record_dev_status(struct rwnx_plat *rwnx_plat, u32 rd_status, bool target_val) { }
static inline void bes_tx_loop_clear_dev_status(struct rwnx_plat *rwnx_plat) { }

/* tx_loop mechanism related error avoidance function */
static inline void bes_tx_loop_exception_check(struct rwnx_plat *rwnx_plat) { }

/* tx_loop mechanism related ind timer function */
static inline void bes_tx_loop_del_specific_ind_timer(struct rwnx_plat *rwnx_plat, lmac_msg_id_t msg_id) { }
#endif /* CONFIG_BES_TXLOOP */

#endif /* __TX_LOOP_H__ */