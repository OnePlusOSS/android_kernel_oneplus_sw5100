/*
 * @file bes_fw_common.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "bes_fw_common.h"
#include "rwnx_utils.h"
#include "bes_log.h"
#include "bes_sysfs.h"

#define SEND_CMD_DEFAULT_TIMEOUT (5 * HZ)
//#define BES_CRC32_DOUBLE_CHECK
#ifdef BES_CRC32_DOUBLE_CHECK
extern u32 bes_crc32_c(u32 crc, const u8 *data, u32 data_len);
#endif

#if defined(FW_DOWNLOAD_BY_SDIO) || defined(FW_DOWNLOAD_BY_USB) || defined(FW_DOWNLOAD_BY_PCIE) || defined(FW_DOWNLOAD_BY_IBUS)

char *bes_get_fw_name(char *fw_name, const char *fw_name_tail)
{
	u16 device_id;
	const char *base_name;

	device_id = bes_sysfs_get_device_id();
	memset(fw_name, 0, BES_FW_NAME_MAX_LEN);
	base_name = (device_id == 0x207a) ? "best2007p" : "best%04x";
	sprintf(fw_name, base_name, device_id);
	if ((BES_FW_NAME_MAX_LEN - 1) >= (strlen(fw_name) + strlen(fw_name_tail)))
		strncat(fw_name, fw_name_tail, BES_FW_NAME_MAX_LEN - strlen(fw_name) - 1);
	else
		bes_err(BES_DBG_DOWNLOAD, "buffer is not enough\n");

	return fw_name;
}

void bes_parse_fw_info(const u8 *data, u32 data_len, u32 *load_addr, u32 *crc32)
{
	u8 buffer[16];
	struct exec_struct_t exec_struct;
	u32 exec_addr_last4byte;
	u32 crc_le = 0;
#ifdef BES_CRC32_DOUBLE_CHECK
	u32 crc_bes = 0;
	u32 crc_be = 0;
#endif
	crc_le = crc32_le(0xffffffffL, (u8 *)data, data_len - CODE_DATA_USELESS_SIZE);
	crc_le ^= 0xffffffffL;

#ifdef BES_CRC32_DOUBLE_CHECK
	crc_be = crc32_be(0xffffffffL, (u8 *)data, data_len - CODE_DATA_USELESS_SIZE);
	crc_be ^= 0xffffffffL;
	crc_bes = bes_crc32_c(crc_bes, (u8 *)data, data_len - CODE_DATA_USELESS_SIZE);
#endif

	// read entry,param,sp,exec_addr
	memcpy((u8 *)buffer, (u8 *)data, sizeof(exec_struct));
	exec_struct.entry = ((struct exec_struct_t *)buffer)->entry; // PC
	exec_struct.param = ((struct exec_struct_t *)buffer)->param;
	exec_struct.sp = ((struct exec_struct_t *)buffer)->sp;
	exec_struct.exec_addr = ((struct exec_struct_t *)buffer)->exec_addr; // load addr

#ifdef BES_CRC32_DOUBLE_CHECK
	bes_dbg(BES_DBG_DOWNLOAD, "crc32 %x(le) %x(be) %x(bes)\n", crc_le, crc_be, crc_bes);
#else
	bes_dbg(BES_DBG_DOWNLOAD, "crc32                :0x%08X\n", crc_le);
#endif
	bes_dbg(BES_DBG_DOWNLOAD, "exec_struct.entry    :0x%08X\n", exec_struct.entry);
	bes_dbg(BES_DBG_DOWNLOAD, "exec_struct.param    :0x%08X\n", exec_struct.param);
	bes_dbg(BES_DBG_DOWNLOAD, "exec_struct.sp       :0x%08X\n", exec_struct.sp);
	bes_dbg(BES_DBG_DOWNLOAD, "exec_struct.exec_addr:0x%08X\n", exec_struct.exec_addr);

	exec_addr_last4byte = (*((u32 *)(data + data_len - 4)));
	bes_dbg(BES_DBG_DOWNLOAD, "exec_addr_last4byte  :0x%08X\n", exec_addr_last4byte);
	if ((!exec_struct.exec_addr) || (exec_struct.exec_addr != exec_addr_last4byte && exec_addr_last4byte)) {
		exec_struct.exec_addr = exec_addr_last4byte;
		bes_dbg(BES_DBG_DOWNLOAD, "exec_addr_last4byte covered exec_struct.exec_addr\n");
	}
	bes_dbg(BES_DBG_DOWNLOAD, "final exec_struct.exec_addr:0x%08X\n", exec_struct.exec_addr);

	*load_addr = exec_struct.exec_addr;

#ifndef BES_CRC32_DOUBLE_CHECK
	*crc32 = crc_le;
#else
	*crc32 = crc_bes;
#endif
}

int bes_frame_rsp_check(void *rsp, u8 sequence, u8 cmd_type)
{
	int ret = 0;
	struct fw_frame_t *pframe = (struct fw_frame_t *)rsp;
	if (pframe->header.type == FRAME_HEADER_REPLY) {
		if (pframe->header.seq != sequence) {
			bes_err(BES_DBG_DOWNLOAD, "frame sequence err. sequence: 0x%x != 0x%x. len = %u\n", pframe->header.seq, sequence, pframe->header.len);
			ret = -200;
			goto error;
		}

		/*
		* the following cmd's payload may be data, so the common check not expected
		*/
		if (cmd_type == FRAME_HEADER_DOWNLOAD_START ||
			cmd_type == FRAME_HEADER_GET_FLASH_FW_VERSION ||
			cmd_type == FRAME_HEADER_CALCULATE_CRC ||
			cmd_type == FRAME_HEADER_ANALOG_REG_READ ||
			cmd_type == FRAME_HEADER_USBPHY_REG_READ) {
			return 0;
		}

		if (pframe->header.len != 4) {
			bes_err(BES_DBG_DOWNLOAD, "payload len error, len = %u\n", pframe->header.len);
			ret = -201;
			goto error;
		}

		if (pframe->payload == ERR_NONE) {
			bes_dbg(BES_DBG_DOWNLOAD, "receive frame successfully, type = 0x%x\n", pframe->header.type);
			return 0;
		} else {
			bes_err(BES_DBG_DOWNLOAD, "frame payload error, payload = 0x%x\n", pframe->payload);
			ret = -202;
			goto error;
		}
	} else if (pframe->header.type == FRAME_HEADER_REPLY_ERR) {
		bes_err(BES_DBG_DOWNLOAD, "device reply an error code:0x%X, sequence:0x%x(0x%x), len = %u\n", pframe->payload, pframe->header.seq,
			 sequence, pframe->header.len);
		ret = pframe->payload;
	} else {
		bes_err(BES_DBG_DOWNLOAD, "receive frame type error, type = 0x%x\n", pframe->header.type);
		ret = -204;
	}
error:
	return ret;
}

const u8 *bes_get_firmware_version_info(const u8 *data, u32 count)
{
	int i = 0;
	const u8 *tmp_ptr = NULL;
	const char month[12][4] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

	if (!data || count < 4)
		return NULL;

	for (tmp_ptr = data + count - 3; tmp_ptr > data; tmp_ptr -= 1) {
		for (i = 0; i < 12; i++) {
			if (memcmp(tmp_ptr, month[i], 3) == 0) {
				return tmp_ptr;
			}
		}
	}

	return NULL;
}

bool bes_firmware_is_pgm(const char *fw_name)
{
	return (NULL != strstr(fw_name, "pgm"));
}

#if defined(FW_DOWNLOAD_BY_SDIO) || defined(FW_DOWNLOAD_BY_USB)
int bes_get_firmware(const struct firmware **fw_bin, const char *fw_name, struct fw_info_t *fw_info, struct fw_crc_t *crc32_t)
{
	int ret = 0;
	const u8 *fw_ver_ptr;

	//  get firmware data
	ret = request_firmware(fw_bin, fw_name, NULL);
	if (ret) {
		bes_err(BES_DBG_DOWNLOAD, "request firmware err:%d\n", ret);
		return ret;
	}
	// parse firmware information
	bes_parse_fw_info((*fw_bin)->data, (*fw_bin)->size, &fw_info->addr, &crc32_t->crc32);
	fw_ver_ptr = bes_get_firmware_version_info((*fw_bin)->data, (*fw_bin)->size);

	if (fw_ver_ptr == NULL) {
		bes_err(BES_DBG_DOWNLOAD, "------Firmware version get failed\n");
		release_firmware(*fw_bin);
		return -EFAULT;
	}

	bes_info(BES_DBG_DOWNLOAD, "------Firmware: %s version :%s\n", fw_name, fw_ver_ptr);
	bes_sysfs_set_fw_ver(fw_ver_ptr);

	bes_info(BES_DBG_DOWNLOAD, "------fw size    :%zu\n", (*fw_bin)->size);
	bes_info(BES_DBG_DOWNLOAD, "------load addr  :0x%08X\n", fw_info->addr);
	bes_info(BES_DBG_DOWNLOAD, "------data crc   :0x%08X\n", crc32_t->crc32);
	bes_info(BES_DBG_DOWNLOAD, "------code size  :%ld\n", (*fw_bin)->size - CODE_DATA_USELESS_SIZE);

	return ret;
}

int bes_get_flash_rom_type(struct platform_fw_t *fw_data)
{
	int ret = 0;
	u16 tx_buf_len = 512, rx_buf_len = 64;

	struct fw_info_t fw_info;
	struct fw_frame_t *tx_frame = NULL;
	struct fw_frame_t *rx_frame = NULL;
	u32 *tx_payload = NULL;

	tx_frame = (struct fw_frame_t *)kzalloc(tx_buf_len, GFP_KERNEL);
	rx_frame = (struct fw_frame_t *)kzalloc(rx_buf_len, GFP_KERNEL);
	if (!tx_frame || !rx_frame) {
		bes_err(BES_DBG_DOWNLOAD, "%s:%d fw failed to allocate memory\n", __func__, __LINE__);
		ret = -ENOMEM;
		goto err;
	}

	tx_payload = tx_frame->variable;
	fw_info.len = 0;
	fw_info.addr = 0;

	tx_frame->header.type = FRAME_HEADER_DOWNLOAD_START;
	tx_frame->header.seq = 0;
	tx_frame->header.len = sizeof(struct fw_info_t);

	memcpy(tx_payload, (u8 *)&fw_info, sizeof(struct fw_info_t));
	ret = fw_data->send_cmd(fw_data, tx_frame, rx_frame, rx_buf_len, SEND_CMD_DEFAULT_TIMEOUT);
	if (ret) {
		bes_err(BES_DBG_DOWNLOAD, "%s-line(%d),Send FRAME_HEADER_DOWNLOAD_START Failed.\n", __func__, __LINE__);
		goto err;
	}

	// check rom_version between V1.0 and V2.x
	if (rx_frame->payload == 0xff) {
		// check rom_version between V2.0 and V2.1
		fw_info.addr = 0x23;
		memcpy(tx_payload, (u8 *)&fw_info, sizeof(struct fw_info_t));
		ret = fw_data->send_cmd(fw_data, tx_frame, rx_frame, rx_buf_len, SEND_CMD_DEFAULT_TIMEOUT);
		if (rx_frame->header.type == FRAME_HEADER_REPLY && rx_frame->payload == 0x23) {
			fw_data->rom_version = FLASH_ROM_VER_2_0;
		} else if (rx_frame->header.type == FRAME_HEADER_REPLY_ERR && rx_frame->payload == 0x23) {
			fw_data->rom_version = FLASH_ROM_VER_2_1;
			bes_info(BES_DBG_DOWNLOAD, "the 'device reply an error code:0x23....' is normal.\n");
		} else {
			bes_err(BES_DBG_DOWNLOAD, "%s-line(%d), Send FRAME_HEADER_DOWNLOAD_START CMD Failed, ret = %d.\n", __func__, __LINE__, ret);
			goto err;
		}
	} else {
		fw_data->rom_version = FLASH_ROM_VER_1_0;
	}
	bes_info(BES_DBG_DOWNLOAD, "bes slave rom version:0x%02X\n", fw_data->rom_version);
	ret = 0;
err:
	if (tx_frame != NULL) {
		kfree(tx_frame);
	}

	if (rx_frame != NULL) {
		kfree(rx_frame);
	}

	return ret;
}

/**
 * @brief Check if the device needs to update firmware
 * @return 1 if the device needs to update firmware;
 *         0 if the device does not need to update firmware and runs successfully;
 *         otherwise operation fail.
*/
int bes_is_firmware_update(struct platform_fw_t *fw_data, const char *fw_name)
{
	int ret;

	u16 tx_buf_len = 512, rx_buf_len = 64;
	const struct firmware *fw_bin;

	struct fw_info_t fw_info;
	struct fw_crc_t crc32_t;
	struct fw_header_t version_t;
	struct fw_run_addr_t run_addr;
	struct fw_flash_data_t flash_data;
	struct fw_frame_t *tx_frame = NULL;
	struct fw_frame_t *rx_frame = NULL;
	u32 *tx_payload = NULL;

	// get firmware data
	ret = bes_get_firmware(&fw_bin, fw_name, &fw_info, &crc32_t);
	if (ret) {
		bes_err(BES_DBG_DOWNLOAD, "get firmware err:%d\n", ret);
		return ret;
	}

	tx_frame = (struct fw_frame_t *)kzalloc(tx_buf_len, GFP_KERNEL);
	rx_frame = (struct fw_frame_t *)kzalloc(rx_buf_len, GFP_KERNEL);
	if (!tx_frame || !rx_frame) {
		bes_err(BES_DBG_DOWNLOAD, "%s-line(%d), fw failed to allocate memory\n", __func__, __LINE__);
		ret = -ENOMEM;
		goto err;
	}

	// get flash firmware version
	flash_data.addr = fw_info.addr;
	flash_data.len = 16;
	tx_payload = tx_frame->variable;

	tx_frame->header.type = FRAME_HEADER_GET_FLASH_FW_VERSION;
	tx_frame->header.seq = 0;
	tx_frame->header.len = sizeof(struct fw_flash_data_t);

	memcpy(tx_payload, (u8 *)&flash_data.addr, sizeof(struct fw_flash_data_t));
	ret = fw_data->send_cmd(fw_data, tx_frame, rx_frame, rx_buf_len, SEND_CMD_DEFAULT_TIMEOUT);

	if (ret) {
		bes_err(BES_DBG_DOWNLOAD, "%s-line(%d), Send FRAME_HEADER_GET_FLASH_FW_VERSION CMD Failed, ret = %d.\n", __func__, __LINE__, ret);
		goto err;
	}

	memcpy((u8 *)&version_t, (u8 *)&(rx_frame->payload), sizeof(struct fw_header_t));
	bes_info(BES_DBG_DOWNLOAD, "firmware version on BES2610:0x%08X maigc_num:0x%08X\n", version_t.crc32, version_t.migc_num);
	bes_info(BES_DBG_DOWNLOAD, "firmware version on platform:0x%08X maigc_num:0x%08X\n", crc32_t.crc32, BOOT_MAGIC_NUMBER);
	if ((version_t.crc32 == crc32_t.crc32) && (version_t.migc_num == BOOT_MAGIC_NUMBER)) {
		// Notify Device:Run firmware
		run_addr.addr = fw_info.addr;

		tx_frame->header.type = FRAME_HEADER_RUN_FLASH;
		tx_frame->header.seq++;
		tx_frame->header.len = sizeof(struct fw_run_addr_t);

		memcpy(tx_payload, (u8 *)&run_addr.addr, sizeof(struct fw_run_addr_t));
		ret = fw_data->send_cmd(fw_data, tx_frame, rx_frame, rx_buf_len, SEND_CMD_DEFAULT_TIMEOUT);

		if (ret) {
			bes_err(BES_DBG_DOWNLOAD, "%s-line(%d), Send FRAME_HEADER_RUN_FLASH CMD Failed, ret = %d.\n", __func__, __LINE__, ret);
			goto err;
		}

		/* wait firmware ready */
		fw_data->set_fw_download_state(true);
		ret = bes_wait_dev_status(fw_data->plat, BES_SLAVE_STATUS_MCU_READY, 1, 2 * HZ);
		fw_data->set_fw_download_state(false);

		if (!ret) {
			bes_info(BES_DBG_DOWNLOAD, "firmware is downloaded successfully and is already running.\n");
		} else {
			bes_err(BES_DBG_DOWNLOAD, "firmware is downloaded successfully but wait running msg failed.\n");
		}
	} else {
		bes_info(BES_DBG_DOWNLOAD, "firmware need to be updated.\n");
		ret = 1;
	}

err:
	if (tx_frame != NULL) {
		kfree(tx_frame);
	}

	if (rx_frame != NULL) {
		kfree(rx_frame);
	}

	release_firmware(fw_bin);

	return ret;
}

int bes_firmware_download(struct platform_fw_t *fw_data, const char *fw_name, bool auto_run, bool ram_fw)
{
	u32 length = 0, code_length = 0, retry_cnt = 0;

	int ret;
	const u8 *data_p;
	u16 tx_buf_len = 32 * 1024, rx_buf_len = 64;
	const struct firmware *fw_bin;

	struct fw_info_t fw_info;
	struct fw_download_t download_addr;
	struct fw_crc_t crc32_t;
	struct fw_header_t version_t;
	struct fw_run_addr_t run_addr;
	struct fw_flash_data_t flash_data;

	struct fw_frame_t *tx_frame = NULL;
	struct fw_frame_t *rx_frame = NULL;
	u32 *tx_payload = NULL;

retry:
	// get firmware data
	ret = bes_get_firmware(&fw_bin, fw_name, &fw_info, &crc32_t);
	if (ret) {
		bes_err(BES_DBG_DOWNLOAD, "get firmware err:%d\n", ret);
		return ret;
	}

	tx_frame = (struct fw_frame_t *)kzalloc(tx_buf_len, GFP_KERNEL);
	rx_frame = (struct fw_frame_t *)kzalloc(rx_buf_len, GFP_KERNEL);
	if (!tx_frame || !rx_frame) {
		bes_err(BES_DBG_DOWNLOAD, "%s-line(%d), fw failed to allocate memory\n", __func__, __LINE__);
		ret = -ENOMEM;
		goto err;
	}

	tx_payload = tx_frame->variable;
#if !defined(FW_DOWNLOAD_RAM_RUN_MOD)
	if (!ram_fw) {
		if (fw_data->rom_version == FLASH_ROM_VER_1_0) {
			// get flash firmware version
			flash_data.addr = fw_info.addr;
			flash_data.len = 16;

			tx_frame->header.type = FRAME_HEADER_GET_FLASH_FW_VERSION;
			tx_frame->header.seq = 0;
			tx_frame->header.len = sizeof(struct fw_flash_data_t);

			memcpy(tx_payload, (u8 *)&flash_data.addr, sizeof(struct fw_flash_data_t));
			ret = fw_data->send_cmd(fw_data, tx_frame, rx_frame, rx_buf_len, SEND_CMD_DEFAULT_TIMEOUT);

			if (ret) {
				bes_err(BES_DBG_DOWNLOAD, "%s-line(%d), Send FRAME_HEADER_GET_FLASH_FW_VERSION CMD Failed, ret = %d.\n", __func__, __LINE__, ret);
				goto err;
			} else {
				memcpy((u8 *)&version_t, (u8 *)&(rx_frame->payload), sizeof(struct fw_header_t));
				bes_info(BES_DBG_DOWNLOAD, "firmware version on BES2610:0x%08X maigc_num:0x%08X\n", version_t.crc32,
						version_t.migc_num);
				bes_info(BES_DBG_DOWNLOAD, "firmware version on platform:0x%08X maigc_num:0x%08X\n", crc32_t.crc32,
						BOOT_MAGIC_NUMBER);
				if ((version_t.crc32 == crc32_t.crc32) && (version_t.migc_num == BOOT_MAGIC_NUMBER)) {
					goto run;
				}
			}
		}
	}
#endif
	// download firmware info and start download
	code_length = fw_bin->size - CODE_DATA_USELESS_SIZE;
	fw_info.len = code_length;
	data_p = fw_bin->data;

	tx_frame->header.type = FRAME_HEADER_DOWNLOAD_START;
	tx_frame->header.seq++;
	tx_frame->header.len = sizeof(struct fw_info_t);

	memcpy(tx_payload, (u8 *)&fw_info, sizeof(struct fw_info_t));
	ret = fw_data->send_cmd(fw_data, tx_frame, rx_frame, rx_buf_len, SEND_CMD_DEFAULT_TIMEOUT);

	if (ret) {
		bes_err(BES_DBG_DOWNLOAD, "%s-line(%d), Send FRAME_HEADER_DOWNLOAD_START CMD Failed, ret = %d.\n", __func__, __LINE__, ret);
		goto err;
	}

	// download firmware data
	download_addr.addr = fw_info.addr;

	while (code_length) {
		if ((sizeof(struct fw_msg_hdr_t) + sizeof(struct fw_download_t) + code_length) < tx_buf_len) {
			length = code_length + sizeof(struct fw_download_t);
		} else {
			length = tx_buf_len - sizeof(struct fw_msg_hdr_t);
		}

		tx_frame->header.type = FRAME_HEADER_DOWNLOAD_DATA;
		tx_frame->header.seq++;
		tx_frame->header.len = length;	//payload length include fw_download_t and code data

		memcpy(tx_payload, &download_addr.addr, sizeof(struct fw_download_t));
		length -= sizeof(struct fw_download_t); // real data length
		memcpy(((u8 *)tx_payload) + sizeof(struct fw_download_t), data_p, length);
		ret = fw_data->send_cmd(fw_data, tx_frame, rx_frame, rx_buf_len, 10 * HZ);

		if (ret) {
			bes_err(BES_DBG_DOWNLOAD, "%s-line(%d), Send FRAME_HEADER_DOWNLOAD_DATA CMD Failed, ret = %d.\n", __func__, __LINE__, ret);
			bes_err(BES_DBG_DOWNLOAD, "already tx fw size:%x/%x\n", download_addr.addr - fw_info.addr, fw_info.len);
			goto err;
		}

		code_length -= length;
		data_p += length;
		download_addr.addr += length;
		bes_dbg(BES_DBG_DOWNLOAD, "already tx fw size:0x%x/0x%x\n", download_addr.addr - fw_info.addr, fw_info.len);
	}

	// Notify Device:The firmware download is complete
	tx_frame->header.type = FRAME_HEADER_DOWNLOAD_END;
	tx_frame->header.seq++;
	tx_frame->header.len = sizeof(struct fw_crc_t);

	memcpy(tx_payload, (u8 *)&crc32_t.crc32, sizeof(struct fw_crc_t));
	ret = fw_data->send_cmd(fw_data, tx_frame, rx_frame, rx_buf_len, 10 * HZ);

	if (ret) {
		bes_err(BES_DBG_DOWNLOAD, "%s-line(%d), Send FRAME_HEADER_DOWNLOAD_END CMD Failed, ret = %d.\n", __func__, __LINE__, ret);
		goto err;
	}

	if (auto_run == false) {
		bes_err(BES_DBG_DOWNLOAD, "partial firmware(%s) is downloaded successfully\n", fw_name);
		goto err;
	}

#if !defined(FW_DOWNLOAD_RAM_RUN_MOD)
run:
#endif
	// Notify Device:Run firmware
	run_addr.addr = fw_info.addr;

	tx_frame->header.type = ram_fw? FRAME_HEADER_RUN_PGM: FRAME_HEADER_RUN_FLASH;
	tx_frame->header.seq++;
	tx_frame->header.len = sizeof(struct fw_run_addr_t);

	memcpy(tx_payload, (u8 *)&run_addr.addr, sizeof(struct fw_run_addr_t));
	ret = fw_data->send_cmd(fw_data, tx_frame, rx_frame, rx_buf_len, SEND_CMD_DEFAULT_TIMEOUT);

	if (ret) {
		bes_err(BES_DBG_DOWNLOAD, "%s-line(%d), Send FRAME_HEADER_RUN CMD Failed, ret = %d.\n", __func__, __LINE__, ret);
		goto err;
	}
	/* wait firmware ready */
	if (!bes_firmware_is_pgm(fw_name)) {
		/* wait firmware ready */
		fw_data->set_fw_download_state(true);
		ret = bes_wait_dev_status(fw_data->plat, BES_SLAVE_STATUS_MCU_READY, 1, 2 * HZ);
		fw_data->set_fw_download_state(false);
	}

	if (!ret) {
		bes_info(BES_DBG_DOWNLOAD, "firmware is downloaded successfully and is already running.\n");
	} else {
		bes_err(BES_DBG_DOWNLOAD, "firmware is downloaded successfully but wait running msg failed.\n");
	}

err:
	if (tx_frame != NULL) {
		kfree(tx_frame);
	}

	if (rx_frame != NULL) {
		kfree(rx_frame);
	}

	release_firmware(fw_bin);
	if (ret && retry_cnt < 3) {
		retry_cnt++;
		bes_info(BES_DBG_DOWNLOAD, "firmware downloaded start retry...., retry %d.\n", retry_cnt);
		goto retry;
	}
	return ret;
}
#endif

#endif
