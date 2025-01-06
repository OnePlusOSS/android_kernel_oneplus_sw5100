/*
 * @file bes_mspi.h
 *
 * Copyright (c) 2024, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BES_PHY_H_
#define _BES_PHY_H_

int bes_get_phy_state(struct rwnx_hw *rwnx_hw, struct phy_states_get_cfm *cfm);

int bes_phy_calc_tx_duty_ratio(struct phy_states_get_cfm *phy_state);

/* get tx duty ratio, unit: 0.1, such as 981 is 98.1 */
int bes_phy_get_tx_duty_ratio(struct rwnx_hw *rwnx_hw);

#endif /* _BES_PHY_H_ */
