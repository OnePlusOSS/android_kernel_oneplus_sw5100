/*
 * @file bes_signal_test.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BES_SIGNAL_TEST_
#define _BES_SIGNAL_TEST_

#include "rwnx_defs.h"

void bes_signal_test_init(struct rwnx_hw *rwnx_hw);
void bes_signal_test_deinit(struct rwnx_hw *rwnx_hw);

int bes_signal_test_set_filter(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif);
int bes_signal_test_clear_filter(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif);
int bes_signal_test_add_filter_when_screen_off(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif,
					       struct list_head *list_set_head, struct bes_capacity *stats);

#endif /* _BES_SIGNAL_TEST_ */