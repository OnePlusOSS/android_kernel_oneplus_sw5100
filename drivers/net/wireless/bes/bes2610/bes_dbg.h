/*
 * @file bes_dbg.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BES_DBG_
#define _BES_DBG_

#include "rwnx_defs.h"

/* The ratelimit interval and burst parameters */
#define BES_DBG_RATELIMIT_INTERVAL		(HZ / 10)
#define BES_DBG_RATELIMIT_BURST			20

/* The minimum interval between tx status of mgmt frame prints(ms) */
#define BES_DBG_MGMT_TX_ST_PRINT_MIN_INTERVAL	20

/* The minimum interval between diag reg prints(ms) */
#define BES_DBG_DIAG_REG_PRINT_MIN_INTERVAL	300

void bes_dbg_key_frame_tx(struct sk_buff *skb);
void bes_dbg_key_frame_tx_cfm(struct sk_buff *skb, struct tx_cfm_tag *cfm);
void bes_dbg_key_frame_rx(struct rwnx_vif *rwnx_vif, bool is_80211_mpdu,
			  struct sk_buff *skb, struct rx_vector_1 *rxvect1);
void bes_dbg_mgt_trace(struct rwnx_hw *rwnx_hw, struct sm_mgt_frame_ind *ind);

#endif /* _BES_DBG_ */
