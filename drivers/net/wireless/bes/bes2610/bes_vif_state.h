/*
 * @file bes_vif_state.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BES_VIF_STATE_
#define _BES_VIF_STATE_

#include "rwnx_defs.h"

enum bes_vif_state_type {
	VIF_STATE_OPEN = 0,
	VIF_STATE_CONNECTED,
	VIF_STATE_AUTHORIZED,
	VIF_STATE_GETTED_IP,
	VIF_STATE_DISCONNECTED,
	VIF_STATE_CLOSE,

	VIF_STATE_MAX
};

void bes_vif_state_init(struct rwnx_vif *rwnx_vif);
void bes_vif_state_process(struct rwnx_vif *rwnx_vif, u8 state);
void bes_cancel_get_ip_timeout_work(struct rwnx_vif *rwnx_vif);
bool bes_vif_state_check_conn_finished(struct rwnx_hw *rwnx_hw);

#endif /* _BES_VIF_STATE_ */
