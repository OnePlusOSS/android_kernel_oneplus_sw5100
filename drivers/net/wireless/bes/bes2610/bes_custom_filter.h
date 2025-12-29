/*
 * @file bes_custom_filter.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __BES_CUSTOM_FILTER_H__
#define __BES_CUSTOM_FILTER_H__

#ifdef CONFIG_CUSTOM_FILTER
int bes_custom_filter_sysfs_ctrl(struct rwnx_hw *rwnx_hw, char *buf);
int bes_custom_filter_sysfs_show(struct rwnx_hw *rwnx_hw, char *buf);
void bes_custom_filter_init(struct rwnx_hw *rwnx_hw);
void bes_custom_filter_deinit(struct rwnx_hw *rwnx_hw);
#else
static inline int bes_custom_filter_sysfs_ctrl(struct rwnx_hw *rwnx_hw, char *buf) { return 0; }
static inline int bes_custom_filter_sysfs_show(struct rwnx_hw *rwnx_hw, char *buf) { return 0; }
static inline void bes_custom_filter_init(struct rwnx_hw *rwnx_hw) { }
static inline void bes_custom_filter_deinit(struct rwnx_hw *rwnx_hw) { }
#endif // CONFIG_CUSTOM_FILTER

#endif /* __BES_CUSTOM_FILTER_H__ */