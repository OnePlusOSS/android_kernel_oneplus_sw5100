/*
 * @file bes_btusb.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __BES_BTUSB_H
#define __BES_BTUSB_H

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#define BTRF_RX_MAX_SIZE       64
#define BTRF_VENDOR_EVENT_PKT  0x0e
#define HCI_FACTORY_SUBCODE    0x06

#define BTRF_READ_REG          0x15
#define BTRF_WRITE_REG         0x16
#define BTRF_REBOOT_MCU        0x22

struct bes_btrf {
	struct mutex btrf_lock;
	wait_queue_head_t btrf_rx_wq;
	bool testmode;
	int len;
	u8 rx_buff[BTRF_RX_MAX_SIZE];
};

int bes_btusb_testmode_cmd(struct hci_dev *hdev, const char *buff, unsigned int size);
void bes_btusb_clear_hci_running(struct hci_dev *hdev);

#endif /* __BES_BTUSB_H */