/*
 * @file bes_phy.c
 *
 * Copyright (c) 2024, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "rwnx_defs.h"
#include "rwnx_msg_tx.h"
#include "bes_phy.h"
#include "bes_log.h"

int bes_get_phy_state(struct rwnx_hw *rwnx_hw, struct phy_states_get_cfm *cfm)
{
	struct phy_states_get_req req;

	/* Set command header */
	req.hdr.id = CUSTOM_ID_PHY_STATES_GET;
	req.hdr.length = sizeof(struct phy_states_get_req);
	memset(cfm, 0, sizeof(*cfm));

	/* send custom request to device */
	return rwnx_send_custom_request(rwnx_hw, &req.hdr, cfm, sizeof(*cfm));
}

int bes_phy_calc_tx_duty_ratio(struct phy_states_get_cfm *phy_state)
{
	int duty_ratio = 0;

	bes_dbg(BES_DBG_PHY, "%s, airtime:%u, sample time:%u\n", __func__,
		phy_state->last_period_busy_time, phy_state->last_period_sample_time);

	if (phy_state->last_period_busy_time == 0)
		return 0;

	if (phy_state->last_period_sample_time)
		duty_ratio = div_u64(((u64)phy_state->last_period_busy_time) * 1000,
				      phy_state->last_period_sample_time);

	if (duty_ratio > 1000)
		duty_ratio = 1000;

	bes_dbg(BES_DBG_PHY, "tx duty ratio:%d.%d%%\n", duty_ratio / 10, duty_ratio % 10);

	return duty_ratio;
}

int bes_phy_get_tx_duty_ratio(struct rwnx_hw *rwnx_hw)
{
	int ret;
	struct phy_states_get_cfm phy_state = {0};

	ret = bes_get_phy_state(rwnx_hw, &phy_state);
	if (ret) {
		bes_err(BES_DBG_PHY, "%s get tx airtime fail, ret:%d\n", __func__, ret);
		return 0;
	}

	return bes_phy_calc_tx_duty_ratio(&phy_state);
}
