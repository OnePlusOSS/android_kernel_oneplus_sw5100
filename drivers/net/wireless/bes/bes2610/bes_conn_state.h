/*
 * @file bes_conn_state.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BES_CONN_STATE
#define _BES_CONN_STATE

#include "rwnx_defs.h"

#define BES_CONN_STATE_GET_IP_TIMEOUT	1500

enum bes_conn_action_type {
	STA_CONN_ACTION_INIT = 0,
	STA_CONN_ACTION_CONN_REQ,
	STA_CONN_ACTION_CONN_IND,
	STA_CONN_ACTION_PORT_AUTH,
	STA_CONN_ACTION_GET_IP,
	STA_CONN_ACTION_DISCONN_REQ,
	STA_CONN_ACTION_DEINIT,
	STA_CONN_ACTION_DISCONN_IND,

	STA_CONN_ACTION_MAX
};

enum bes_conn_flag {
	STA_CONN_FLAG_CONN = 0,
	STA_CONN_FLAG_PORT_AUTH,
	STA_CONN_FLAG_CONN_ABORT,

	STA_CONN_FLAG_MAX
};

void bes_conn_state_init(struct rwnx_vif *rwnx_vif);
void bes_conn_state_process(struct rwnx_vif *rwnx_vif, struct rwnx_sta *rwnx_sta, u8 action);

#endif /* _BES_CONN_STATE */
