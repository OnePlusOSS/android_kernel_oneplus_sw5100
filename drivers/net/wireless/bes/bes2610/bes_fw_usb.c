/*
 * @file bes_fw_usb.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "bes_fw_common.h"
#include "rwnx_platform.h"
#include <linux/string.h>
#include "rwnx_utils.h"
#include "bes_usb.h"
#include "bes_fw_usb.h"
#include "bes_sysfs.h"
#include "bes_log.h"
#include "bes_hwio.h"
#include "bes_psa.h"

extern int bes_usb_send(struct sbus_priv *self, u8 pipe_id, u8 *data_buf, u32 len, bool need_urb_free);

static struct sbus_priv *bes_sbus_priv = NULL;

static int bes_usb_reg_read(struct sbus_priv *self, u32 reg, void *dst, int count)
{
	int ret = 0;
	unsigned long flags;

	if (reg == BES_USB_CONTROL_REG) {
		spin_lock_irqsave(&self->status_lock, flags);
		*((u32 *)dst) = self->int_control_reg;
		spin_unlock_irqrestore(&self->status_lock, flags);
	} else if (reg == BES_USB_STATUS_REG) {
		spin_lock_irqsave(&self->status_lock, flags);
		*((u32 *)dst) = self->int_status_reg;
		spin_unlock_irqrestore(&self->status_lock, flags);
	} else
		ret = -EINVAL;
	return ret;
}

static int bes_usb_reg_write(struct sbus_priv *self, u32 reg, const void *src, int count)
{
	int ret = 0;
	unsigned long flags;

	if (reg == BES_USB_CONTROL_REG) {
		spin_lock_irqsave(&self->status_lock, flags);
		self->int_control_reg = *((u32 *)src);
		spin_unlock_irqrestore(&self->status_lock, flags);
	} else if (reg == BES_USB_STATUS_REG) {
		spin_lock_irqsave(&self->status_lock, flags);
		self->int_status_reg = *((u32 *)src);
		spin_unlock_irqrestore(&self->status_lock, flags);
	} else
		ret = -EINVAL;

	return ret;
}

static void bes_fw_irq_handler(struct platform_fw_t *fw_data)
{
	u32 ctrl_reg, status_reg;

	// read status and control register
	bes_usb_reg_read(bes_sbus_priv, BES_USB_CONTROL_REG, &ctrl_reg, 1);
	bes_usb_reg_read(bes_sbus_priv, BES_USB_STATUS_REG, &status_reg, 1);
	bes_dbg(BES_DBG_DOWNLOAD, "%s ctrl_reg:0x%08x status_reg:0x%08x\n", __func__, ctrl_reg, status_reg);

	// notify tx done event
	if ((ctrl_reg & BES_USB_FW_TX_DONE) != 0 && (status_reg & BES_USB_FW_TX_DONE) != 0) {
		status_reg &= ~BES_USB_FW_TX_DONE;
		bes_usb_reg_write(bes_sbus_priv, BES_USB_STATUS_REG, &status_reg, 1);
		complete(&fw_data->completion_tx);
	}

	// notify rx indication event
	if ((ctrl_reg & BES_USB_FW_RX_INDICATION) != 0 && (status_reg & BES_USB_FW_RX_INDICATION) != 0) {
		status_reg &= ~BES_USB_FW_RX_INDICATION;
		bes_usb_reg_write(bes_sbus_priv, BES_USB_STATUS_REG, &status_reg, 1);
		complete(&fw_data->completion_rx);
	}
}

static int bes_usb_fw_write(struct platform_fw_t *fw_data, u8 *data, size_t len)
{
	int ret = 0;
	long time_left;
	u32 control_reg = 0;

	// enable tx done notification
	bes_usb_reg_read(bes_sbus_priv, BES_USB_CONTROL_REG, &control_reg, 1);
	control_reg |= BES_USB_FW_TX_DONE;
	bes_usb_reg_write(bes_sbus_priv, BES_USB_CONTROL_REG, &control_reg, 1);

	// send firmware data to device
	ret = bes_usb_send(bes_sbus_priv, BES_USB_PIPE_TX_DATA_LP, data, len, false);
	if (ret < 0) {
		bes_usb_reg_read(bes_sbus_priv, BES_USB_CONTROL_REG, &control_reg, 1);
		control_reg &= ~BES_USB_FW_TX_DONE;
		bes_usb_reg_write(bes_sbus_priv, BES_USB_CONTROL_REG, &control_reg, 1);
		return ret;
	}

	// wait for sending done of firmware data
	time_left = wait_for_completion_interruptible_timeout(&fw_data->completion_tx, HZ * 5);
	if (time_left == 0)
		ret = -ETIMEDOUT;
	else if (time_left < 0)
		ret = time_left;

	// turn off tx done notification
	bes_usb_reg_read(bes_sbus_priv, BES_USB_CONTROL_REG, &control_reg, 1);
	control_reg &= ~BES_USB_FW_TX_DONE;
	bes_usb_reg_write(bes_sbus_priv, BES_USB_CONTROL_REG, &control_reg, 1);

	return ret;
}

int bes_download_cmd_send_usb(struct platform_fw_t *fw_data, struct fw_frame_t* tx_frame, void* rx_frame, u16 rec_buf_len, int timeout)
{
	int length = 0, ret = 0;
	struct sk_buff *skb = NULL;

	length = BES_FW_MSG_TOTAL_LEN(tx_frame->header);

	// send firmware information frame to device and wait tx done
	ret = bes_usb_fw_write(fw_data, (u8 *)tx_frame, length);
	if (ret) {
		bes_err(BES_DBG_DOWNLOAD, "usb tx cmd err:%d\n", ret);
		return ret;
	}

	// wait rx indication
	ret = wait_for_completion_interruptible_timeout(&fw_data->completion_rx, timeout);
	if (ret <= 0) {
		bes_err(BES_DBG_DOWNLOAD, "usb receive download firmware info response timeout or interrupt\n");
		ret = -ETIMEDOUT;
		return ret;
	}

	// read device response
	skb = bes_usb_read(bes_sbus_priv);
	WARN_ON(!skb);

	if (skb->len > rec_buf_len) {
		bes_err(BES_DBG_DOWNLOAD, "host has no enough buffer to receive data:%d/%d\n", skb->data_len, rec_buf_len);
		ret = -ENOMEM;
		return ret;
	}

	memcpy(rx_frame, (u8*)skb->data, skb->len);
	// check cmd response
	ret = bes_frame_rsp_check(rx_frame, tx_frame->header.seq, tx_frame->header.type);

	dev_kfree_skb(skb);

	return ret;
}

int bes_load_firmware_usb(struct sbus_priv *priv, struct rwnx_plat *rwnx_plat)
{
	int ret;
	struct platform_fw_t *temp_fw_data;
	u32 control_reg = 0;
	int fw_type = bes_sysfs_get_fw_type();
	char *fw_name = NULL;
	char *fw_name_tail = NULL;
	bool is_super_speed = (priv->udev->speed == USB_SPEED_SUPER);
	temp_fw_data = kzalloc(sizeof(struct platform_fw_t), GFP_KERNEL);
	if (!temp_fw_data)
		return -ENOMEM;

	fw_name = temp_fw_data->fw_name;
	init_completion(&temp_fw_data->completion_rx);
	init_completion(&temp_fw_data->completion_tx);

	bes_sbus_priv = priv;
	priv->irq_priv = temp_fw_data;
	rwnx_plat->dev_status &= (~BES_SLAVE_STATUS_MCU_READY);
	temp_fw_data->plat = rwnx_plat;
	temp_fw_data->send_cmd = bes_download_cmd_send_usb;
	temp_fw_data->set_fw_download_state = bes_usb_set_fw_download_state;

	// enable rx indication
	bes_usb_reg_read(bes_sbus_priv, BES_USB_CONTROL_REG, &control_reg, 1);
	control_reg |= BES_USB_FW_RX_INDICATION;
	bes_usb_reg_write(bes_sbus_priv, BES_USB_CONTROL_REG, &control_reg, 1);

	if (fw_type == BES_FW_TYPE_WIFI_SIGNAL) {
#if defined(FW_DOWNLOAD_RAM_RUN_MOD)
		fw_name_tail = is_super_speed ? BES_LOAD_FW_NAME_USB3 : BES_LOAD_FW_NAME;
		ret = bes_firmware_download(temp_fw_data, bes_get_fw_name(fw_name, fw_name_tail), true, true);
#else
		ret = bes_get_flash_rom_type(temp_fw_data);

		if (!ret) {
			if (temp_fw_data->rom_version == FLASH_ROM_VER_2_0 || temp_fw_data->rom_version == FLASH_ROM_VER_2_1) {
				fw_name_tail = is_super_speed ? BES_LOAD_F_FW_NAME_USB3 : BES_LOAD_F_FW_NAME;
				ret = bes_is_firmware_update(temp_fw_data, bes_get_fw_name(fw_name, fw_name_tail));
				if (ret != 1) {
					goto err;
				}
			}

			fw_name_tail = is_super_speed ? BES_LOAD_F_PGM_NAME_USB3 : BES_LOAD_F_PGM_NAME;
			ret = bes_firmware_download(temp_fw_data, bes_get_fw_name(fw_name, fw_name_tail), true, true);
			if (ret) {
				bes_err(BES_DBG_DOWNLOAD, "download normal firmware failed %d.\n", ret);
				goto err;
			}

			fw_name_tail = is_super_speed ? BES_LOAD_F_FW_NAME_USB3 : BES_LOAD_F_FW_NAME;
			ret = bes_firmware_download(temp_fw_data, bes_get_fw_name(fw_name, fw_name_tail), true, false);
		}
#endif
	/* Non-signal mode loading ram firmware */
	} else if (fw_type == BES_FW_TYPE_WIFI_NONSIGNAL) {
		fw_name_tail = is_super_speed ? BES_LOAD_NOSIG_FW_NAME_USB3 : BES_LOAD_NOSIG_FW_NAME;
		ret = bes_firmware_download(temp_fw_data, bes_get_fw_name(fw_name, fw_name_tail), true, true);
	} else {
		fw_name_tail = is_super_speed ? BES_LOAD_BTRF_FW_NAME_USB3 : BES_LOAD_BTRF_FW_NAME;
		ret = bes_firmware_download(temp_fw_data, bes_get_fw_name(fw_name, fw_name_tail), true, true);
	}
	if (ret)
		bes_err(BES_DBG_DOWNLOAD, "download normal flash firmware failed %d.\n", ret);

err:

	// disable rx indication
	bes_usb_reg_read(bes_sbus_priv, BES_USB_CONTROL_REG, &control_reg, 1);
	control_reg &= ~BES_USB_FW_RX_INDICATION;
	bes_usb_reg_write(bes_sbus_priv, BES_USB_CONTROL_REG, &control_reg, 1);

	kfree(temp_fw_data);

	return ret;
}

void bes_usb_irq_handler(struct sbus_priv *ar_usb)
{
	bes_fw_irq_handler(ar_usb->irq_priv);
}

int bes_usb_test_control_reg(struct sbus_priv *ar_usb, int bitval)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&ar_usb->status_lock, flags);
	ret = ar_usb->int_control_reg & bitval;
	spin_unlock_irqrestore(&ar_usb->status_lock, flags);
	return ret;
}

void bes_usb_set_status_reg(struct sbus_priv *ar_usb, int bitval)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&ar_usb->status_lock, flags);
	ret = ar_usb->int_status_reg |= bitval;
	spin_unlock_irqrestore(&ar_usb->status_lock, flags);
}
