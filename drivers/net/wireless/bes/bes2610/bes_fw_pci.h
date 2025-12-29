/*
 * @file bes_fw_pci.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __BES_FW_PCIE_H__
#define __BES_FW_PCIE_H__

#include "rwnx_pci.h"

#define RAM_BASE                        0x20000000
#if 0
#define ROM_PARAM_ADDR                  0xE01B0
#else
#define CONN_FW_PARAM_ADDR              0x801A8
#endif

#define CHECK_LENGTH                    (1024 * 32)

struct conn_firmware_param_t {
    u32 addr;
    u32 len;
    u32 boot_addr;
    u32 crc32;
};

int bes_load_firmware_pcie(struct rwnx_plat *rwnx_plat);
#endif /*__BES_FW_PCIE_H__*/