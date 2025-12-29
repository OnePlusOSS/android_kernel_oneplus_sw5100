/*
 * @file bes_fw_usb.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __BES_FW_USB_H__
#define __BES_FW_USB_H__

#include "bes_usb.h"
#include <linux/types.h>

int bes_load_firmware_usb(struct sbus_priv *priv, struct rwnx_plat *rwnx_plat);

void bes_usb_irq_handler(struct sbus_priv *ar_usb);
int bes_usb_test_control_reg(struct sbus_priv *ar_usb, int bitval);
void bes_usb_set_status_reg(struct sbus_priv *ar_usb, int bitval);

#endif /*__BES_FW_USB_H__*/
