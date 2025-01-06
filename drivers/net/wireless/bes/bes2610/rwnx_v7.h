/**
 ******************************************************************************
 *
 * @file rwnx_v7.h
 *
 * Copyright (C) RivieraWaves 2012-2021
 *
 ******************************************************************************
 */

#ifndef _RWNX_V7_H_
#define _RWNX_V7_H_

#include "rwnx_platform.h"
#include <linux/pci.h>
int rwnx_v7_platform_init(struct pci_dev *pci_dev, struct rwnx_plat **rwnx_plat);
#endif /* _RWNX_V7_H_ */
