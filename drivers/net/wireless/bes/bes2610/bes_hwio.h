/*
 * @file bes_hwio.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __BES_HWIO_H
#define __BES_HWIO_H

/* mcu status confirm */
#define BES_SLAVE_STATUS_MCU_READY          (1 << 0)
#define BES_SLAVE_STATUS_WIFI_ON_READY      (1 << 1)
#define BES_SLAVE_STATUS_MCU_WAKEUP_READY   (1 << 2)
#define BES_SLAVE_STATUS_BT_ON_READY        (1 << 3)
#define BES_SLAVE_STATUS_BT_WAKEUP_READY    (1 << 4)

/* host active/deactive type */
#define HOST_INT_TYPE_WIFI_ON        (1 << 0)
#define HOST_INT_TYPE_WIFI_OFF       (1 << 1)
#define HOST_INT_TYPE_WIFI_WAKEUP    (1 << 2)
#define HOST_INT_TYPE_WIFI_SLEEP     (1 << 3)
#define HOST_INT_TYPE_BT_ON          (1 << 4)
#define HOST_INT_TYPE_BT_OFF         (1 << 5)
#define HOST_INT_TYPE_BT_WAKEUP      (1 << 6)
#define HOST_INT_TYPE_BT_SLEEP       (1 << 7)
#define HOST_INT_TYPE_CHG_FW_TYPE    (1 << 8)

#define HOST_INT_TYPE_REBOOT HOST_INT_TYPE_CHG_FW_TYPE

#ifdef CONFIG_BES_IBUS
/* WIFI_LP_CTRL_REG */
#define WIFI_LP_STATUS_REG               0x400800F4
#define WIFI_WAKE_LOCK                   BIT(0)
#define WIFI_WAKE_STATUS                 BIT(1)
#define MCU_WAKE_LOCK                    BIT(2)
#define MCU_WAKE_STATUS                  BIT(3)

/* SYS2WIFI_REG */
#define SYS2WF_REG_BASE                  0x58000128
#define SYS2WF_REG_SIZE                  8

#define SYS2WF_IRQ_SET_OFFSET            0
#define SYS2WF_DATA_IND_SET              BIT(4)
#define SYS2WF_DATA_DONE_SET             BIT(20)

#define SYS2WF_IRQ_CLR_OFFSET            4
#define SYS2WF_DATA_DONE_CLR             BIT(0)
#endif

#if defined(CONFIG_BES_SDIO) || defined(CONFIG_BES_DSPI) || defined(CONFIG_BES_MSPI)
#define BES_TRANSFER_LEN_LIMIT (1600)
#elif defined(CONFIG_BES_USB)
#define BES_TRANSFER_LEN_LIMIT (2048)
#endif

#endif /* __BES_HWIO_H */
