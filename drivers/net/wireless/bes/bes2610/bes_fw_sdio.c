/*
 * @file bes_fw_sdio.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "bes_fw_common.h"
#include "bes_sdio.h"
#include "bes_sysfs.h"
#include <linux/bug.h>
#include <linux/delay.h>
#include <linux/string.h>
#include "bes_log.h"
#include "bes_hwio.h"
#include "bes_psa.h"

extern int bes_sdio_reg_read(struct sdio_func *func, u32 reg, volatile void *dst, int count);
extern int bes_sdio_reg_write(struct sdio_func *func, u32 reg, const void *src, int count);

static struct sbus_priv *bes_sbus_priv = NULL;

int bes_reg_read(u32 addr, void *buf, size_t buf_len)
{
	int ret;
	sdio_claim_host(bes_sbus_priv->func);
	ret = bes_sdio_reg_read(bes_sbus_priv->func, addr, buf, buf_len);
	sdio_release_host(bes_sbus_priv->func);
	return ret;
}

int bes_reg_write(u32 addr, const void *buf, size_t buf_len)
{
	int ret;
	sdio_claim_host(bes_sbus_priv->func);
	ret = bes_sdio_reg_write(bes_sbus_priv->func, addr, buf, buf_len);
	sdio_release_host(bes_sbus_priv->func);
	return ret;
}

static int bes_sdio_memcpy_io_helper(struct sdio_func *func, int write, u8 *data_buf, u32 size)
{
	int ret = 0;

	if (!func || (func->num > 7) || (!size)) {
		ret = -EINVAL;
		goto out;
	}

	bes_dbg(BES_DBG_SDIO, "%s size=%d dir=%d\n", __func__, size, write);
	sdio_claim_host(func);
	if (write)
		ret = sdio_memcpy_toio(func, size, data_buf, sdio_align_size(func, size));
	else
		ret = sdio_memcpy_fromio(func, data_buf, size, sdio_align_size(func, size));
	sdio_release_host(func);
	if (ret)
		bes_err(BES_DBG_SDIO, "%s ret:0x%x", __func__, ret);
out:
	bes_dbg(BES_DBG_SDIO, "%s ret:0x%x", __func__, ret);
	return ret;
}

static void bes_fw_irq_handler(void *priv)
{
	struct platform_fw_t *fw_data = (struct platform_fw_t *)priv;
	int ret;
	u8 len_reg0 = 0;

	/*
	* we should read reg0/reg1 to clear sdio interrupt in there.
	**/
	ret = bes_reg_read(0, &len_reg0, sizeof(len_reg0));
	if (ret != 0) {
		bes_err(BES_DBG_DOWNLOAD, "%s, %d read reg0 fail ret=%d\n", __func__, __LINE__, ret);
		return;
	}

	if (len_reg0 != 0) {
		fw_data->rx_len = len_reg0;
		ret = bes_reg_read(1, &len_reg0, sizeof(len_reg0));

		if (ret == 0) {
			bes_dbg(BES_DBG_DOWNLOAD, "%s, rx_len=%x\n", __func__, fw_data->rx_len);
			complete(&fw_data->completion_tx);
		} else {
			bes_err(BES_DBG_DOWNLOAD, "%s, %d read reg1 fail ret=%d\n", __func__, __LINE__, ret);
		}
	} else {
		bes_warn(BES_DBG_DOWNLOAD, "%s, %d read len_reg0 = 0\n", __func__, __LINE__);
	}

}

static void rwnx_sdio_irq_handler_unified(struct sdio_func *func)
{
	struct sbus_priv *self = sdio_get_drvdata(func);
	bes_dbg(BES_DBG_SDIO, "\n %s called, fw_started:%d \n", __func__, self->fw_started);

	if (WARN_ON(!self)) {
		return;
	}

	if (self->fw_started) {
		bes_sdio_irq_handler(func);
	} else {
		bes_fw_irq_handler(self->irq_priv);
	}
}

int bes_data_read(void *buf, size_t buf_len)
{
	int ret = 0; // retry = 1;
	int retry = 1;

	sdio_claim_host(bes_sbus_priv->func);
	while (retry <= BES_SDIO_TRANS_MAX_RETRY) {
		ret = bes_sdio_memcpy_io_helper(bes_sbus_priv->func, 0, buf, buf_len);
		if (ret) {
			retry++;
			mdelay(1);
			bes_info(BES_DBG_DOWNLOAD, "%s error :[%d]\n", __func__, ret);
		} else {
			break;
		}
	}

	sdio_release_host(bes_sbus_priv->func);
	return ret;
}

int bes_data_write(void *buf, size_t buf_len)
{
	int ret = 0; // retry = 1;
	int retry = 1;

	sdio_claim_host(bes_sbus_priv->func);

	while (retry <= BES_SDIO_TRANS_MAX_RETRY) {
		bes_sdio_memcpy_io_helper(bes_sbus_priv->func, 1, buf, buf_len);
		if (ret) {
			retry++;
			mdelay(1);
			bes_info(BES_DBG_DOWNLOAD, "%s,error :[%d]\n", __func__, ret);
		} else {
			break;
		}
	}

	sdio_release_host(bes_sbus_priv->func);
	return ret;
}

static int bes_slave_rx_ready(struct platform_fw_t *fw_data, u8 *buf_cnt, u16 *buf_len, int timeout)
{
	int ret;
	unsigned long start = jiffies;
	*buf_cnt = 0;

	do {
		ret = bes_reg_read(0x108, buf_cnt, 1);
		if (!(ret || *buf_cnt)) {
			msleep(5);
			continue;
		} else if (ret) {
			bes_err(BES_DBG_DOWNLOAD, "%s,%d err=%d\n", __func__, __LINE__, ret);
		} else {
			ret = bes_reg_read(0x109, buf_len, sizeof(*buf_len));
		}
		break;
	} while (time_before(jiffies, start + timeout));

	return ret;
}

static int bes_slave_tx_ready(struct platform_fw_t *fw_data, u16 *tx_len, int timeout)
{
	int ret = 0;

wait_again:
	ret = wait_for_completion_interruptible_timeout(&fw_data->completion_tx, timeout);
	if (ret > 0) {
		*tx_len = fw_data->rx_len;
		ret = 0;
		reinit_completion(&fw_data->completion_tx);
	} else if (ret == 0) {
		reinit_completion(&fw_data->completion_tx);
		ret = -110;
	} else {
		// ret = -ERESTARTSYS, to be continued;
		reinit_completion(&fw_data->completion_tx);
		goto wait_again;
	}

	return ret;
}

int bes_download_cmd_send_sdio(struct platform_fw_t *fw_data, struct fw_frame_t* tx_frame, void* rx_frame, u16 rec_buf_len, int timeout)
{
	int ret = 0, length = 0;
	u8 buf_cnt = 0;
	u16 tx_size = 0, rx_size = 0;

	ret = bes_slave_rx_ready(fw_data, &buf_cnt, &tx_size, timeout);
	if (!ret) {
		bes_dbg(BES_DBG_DOWNLOAD, "bes sdio slave rx buf cnt:%d,buf len max:%d\n", buf_cnt, tx_size);
	} else {
		bes_err(BES_DBG_DOWNLOAD, "wait bes sdio slave rx ready tiemout:%d\n", ret);
		return ret;
	}

	length = BES_FW_MSG_TOTAL_LEN(tx_frame->header);

	if (tx_size < length) {
		bes_err(BES_DBG_DOWNLOAD, "%s:%d bes slave has no enough buffer%d/%d\n", __func__, __LINE__, tx_size, length);
		ret = -ENOMEM;
		return ret;
	}

	length = length > 512 ? length : 512;
	ret = bes_data_write(tx_frame, length);
	if (ret) {
		bes_err(BES_DBG_DOWNLOAD, "sdio tx cmd err:%d\n", ret);
		return ret;
	}

	ret = bes_slave_tx_ready(fw_data, &rx_size, timeout);
	if (!ret) {
		bes_dbg(BES_DBG_DOWNLOAD, "sdio slave tx ready %d bytes\n", rx_size);
	} else {
		bes_err(BES_DBG_DOWNLOAD, "wait bes sdio slave tx ready tiemout:%d\n", ret);
		return ret;
	}

	if (rx_size > rec_buf_len) {
		bes_err(BES_DBG_DOWNLOAD, "host has no enough buffer to receive data:%d/%d\n", rx_size, rec_buf_len);
		ret = -ENOMEM;
		return ret;
	}

	ret = bes_data_read(rx_frame, rx_size);
	if (ret) {
		bes_err(BES_DBG_DOWNLOAD, "read cmd response err:%d\n", ret);
		return ret;
	}

	// check cmd response
	ret = bes_frame_rsp_check(rx_frame, tx_frame->header.seq, tx_frame->header.type);

	return ret;
}

void bes_sdio_set_fw_download_state(bool fw_started)
{
	bes_sbus_priv->fw_started = fw_started;
}

static int bes_load_wifi_firmware(struct platform_fw_t *fw_data)
{
	int ret = 0;
	int fw_type = bes_sysfs_get_fw_type();
	char *fw_name = fw_data->fw_name;

	if (fw_type == BES_FW_TYPE_WIFI_SIGNAL) {
#if defined(FW_DOWNLOAD_RAM_RUN_MOD)
		ret = bes_firmware_download(fw_data, bes_get_fw_name(fw_name, BES_LOAD_FW_NAME), true, true);
#else
		ret = bes_get_flash_rom_type(fw_data);

		if (!ret) {
			if (fw_data->rom_version == FLASH_ROM_VER_2_0 || fw_data->rom_version == FLASH_ROM_VER_2_1) {
				ret = bes_is_firmware_update(fw_data, bes_get_fw_name(fw_name, BES_LOAD_F_FW_NAME));
				if (ret != 1) {
					return ret;
				}
			}
			ret = bes_firmware_download(fw_data, bes_get_fw_name(fw_name, BES_LOAD_F_PGM_NAME), true, true);
			if (ret) {
				bes_err(BES_DBG_DOWNLOAD, "download normal firmware failed %d.\n", ret);
				return ret;
			}
			ret = bes_firmware_download(fw_data, bes_get_fw_name(fw_name, BES_LOAD_F_FW_NAME), true, false);
		}
#endif
		/* Non-signal mode loading ram firmware */
	} else if (fw_type == BES_FW_TYPE_WIFI_NONSIGNAL) {
		ret = bes_firmware_download(fw_data, bes_get_fw_name(fw_name, BES_LOAD_NOSIG_FW_NAME), true, true);
	} else {
		ret = bes_firmware_download(fw_data, bes_get_fw_name(fw_name, BES_LOAD_BTRF_FW_NAME), true, true);
	}

	if (ret)
		bes_err(BES_DBG_DOWNLOAD, "download normal flash firmware failed %d.\n", ret);

	return ret;
}

int bes_load_firmware_sdio(struct sbus_priv *priv, struct rwnx_plat *rwnx_plat)
{
	int ret = 0;
	struct platform_fw_t *temp_fw_data;

	pr_info("%s %d\n", __func__, __LINE__);

	temp_fw_data = kzalloc(sizeof(struct platform_fw_t), GFP_KERNEL);
	if (!temp_fw_data)
		return -ENOMEM;

	init_completion(&temp_fw_data->completion_tx);

	bes_sbus_priv = priv;
	priv->irq_priv = temp_fw_data;
	rwnx_plat->dev_status &= (~BES_SLAVE_STATUS_MCU_READY);
	temp_fw_data->plat = rwnx_plat;
	temp_fw_data->send_cmd = bes_download_cmd_send_sdio;
	temp_fw_data->set_fw_download_state = bes_sdio_set_fw_download_state;

	sdio_claim_host(priv->func);
	ret = sdio_claim_irq(priv->func, rwnx_sdio_irq_handler_unified);
	sdio_release_host(priv->func);

	bes_info(BES_DBG_DOWNLOAD, "%s start load wifi firmware.\n", __func__);

	ret = bes_load_wifi_firmware(temp_fw_data);

	kfree(temp_fw_data);

	return ret;
}
