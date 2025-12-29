/*
 * @file bes_tp_dbg.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BES_TP_DBG_
#define _BES_TP_DBG_

#include "rwnx_defs.h"

enum bes_tp_dbg_feature_type {
	BES_TP_DBG_FEATURE_TYPR_TP_TX_RX_RATE = 0,
	BES_TP_DBG_FEATURE_TYPR_COEX_STATE,
	BES_TP_DBG_FEATURE_TYPR_TX_RX_RATE,
	BES_TP_DBG_FEATURE_TYPR_IPC_DELAY_STATS,
	BES_TP_DBG_FEATURE_TYPR_TX_DELAY_STATS
};

#ifdef CONFIG_BES_TP_DBG
void bes_tp_dbg_reset_tx_rx_info(struct rwnx_hw *rwnx_hw, struct rwnx_sta *rwnx_sta);
void bes_tp_dbg_init(struct rwnx_hw *rwnx_hw);
void bes_tp_dbg_deinit(struct rwnx_hw *rwnx_hw);
#else
static inline void bes_tp_dbg_reset_tx_rx_info(struct rwnx_hw *rwnx_hw, struct rwnx_sta *rwnx_sta) {}
static inline void bes_tp_dbg_init(struct rwnx_hw *rwnx_hw) {}
static inline void bes_tp_dbg_deinit(struct rwnx_hw *rwnx_hw) {}
#endif

#endif /* _BES_TP_DBG_ */
