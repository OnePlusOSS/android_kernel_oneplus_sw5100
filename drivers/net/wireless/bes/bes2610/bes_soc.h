/*
 * @file bes_soc.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BES_SOC_H_
#define _BES_SOC_H_

#include <linux/platform_device.h>
#include "rwnx_platform.h"

int bes_soc_platform_init(struct platform_device *pdev, struct rwnx_plat **rwnx_plat);

#endif /* _BES_SOC_H_ */
