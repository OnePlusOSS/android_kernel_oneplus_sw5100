/**
 ****************************************************************************************
 *
 * @file rwnx_msg_rx.h
 *
 * @brief RX function declarations
 *
 * Copyright (C) RivieraWaves 2012-2021
 *
 ****************************************************************************************
 */

#ifndef _RWNX_MSG_RX_H_
#define _RWNX_MSG_RX_H_

void rwnx_rx_handle_msg(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg);
void rwnx_defer_msg_work(struct work_struct *work);
void rwnx_flush_deferred_e2amsg_work(struct rwnx_hw *rwnx_hw);
bool rwnx_rx_check_msg_id(u16 id);

#endif /* _RWNX_MSG_RX_H_ */
