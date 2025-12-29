/*
 * @file bes_fw_common.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __BES_FW_COMMON_H__
#define __BES_FW_COMMON_H__

#include <linux/types.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/crc32.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/wait.h>
#include <linux/completion.h>
#include <linux/firmware.h>
#include <linux/fs.h>
#include <linux/version.h>
#include "rwnx_platform.h"

/***** firmware macro *****/
#define BUF_SIZE 49152
#define RETRY_CNT_MAX 3
#define TIMEOUT_TIME 20
#define FRAME_HEADER_SIZE 0x04
#define CODE_DATA_USELESS_SIZE 0x04
/***** flash_rom version *****/
#define FLASH_ROM_VER_1_0 0x10
#define FLASH_ROM_VER_2_0 0x20
#define FLASH_ROM_VER_2_1 0x21
/****frame header code****/
#define FRAME_HEADER_REPLY 0xB0
#define FRAME_HEADER_DOWNLOAD_START 0xB1
#define FRAME_HEADER_DOWNLOAD_DATA 0xB2
#define FRAME_HEADER_DOWNLOAD_END 0xB3
#define FRAME_HEADER_RUN_PGM 0xB4
#define FRAME_HEADER_GET_FLASH_FW_VERSION 0xBD
#define FRAME_HEADER_RUN_FLASH 0xBE

#define FRAME_HEADER_CALCULATE_CRC 0xBF
#define FRAME_HEADER_REPLY_ERR 0xC0
#define FRAME_HEADER_ANALOG_REG_READ 0xC1
#define FRAME_HEADER_ANALOG_REG_WRITE 0xC2
#define FRAME_HEADER_USBPHY_REG_READ 0xC3
#define FRAME_HEADER_USBPHY_REG_WRITE 0xC4

/****frame length get****/
#define BES_FW_MSG_TOTAL_LEN(msg) (sizeof(struct fw_msg_hdr_t) + ((struct fw_msg_hdr_t)(msg)).len)

#define BOOT_MAGIC_NUMBER 0xBE57EC1C
#define BES_FW_NAME_MAX_LEN 64

#if defined(FW_DOWNLOAD_BY_SDIO)
#define BES_LOAD_FW_NAME "_fw_sdio.bin"
#define BES_LOAD_NOSIG_FW_NAME "_fw_sdio_nosignal.bin"
#define BES_LOAD_BTRF_FW_NAME "_fw_sdio_btrf.bin"

#define BES_LOAD_F_PGM_NAME "_f_pgm_sdio.bin"
#define BES_LOAD_F_FW_NAME "_f_fw_sdio.bin"
#define BES_LOAD_F_NOSIGNAL_FW_NAME "_f_fw_sdio_nosignal.bin"
#define BES_LOAD_F_BTRF_FW_NAME "_f_fw_sdio_btrf.bin"

#elif defined(FW_DOWNLOAD_BY_USB)
#define BES_LOAD_FW_NAME "_fw_usb.bin"
#define BES_LOAD_NOSIG_FW_NAME "_fw_usb_nosignal.bin"
#define BES_LOAD_BTRF_FW_NAME "_fw_usb_btrf.bin"

#define BES_LOAD_F_PGM_NAME "_f_pgm_usb.bin"
#define BES_LOAD_F_FW_NAME "_f_fw_usb.bin"
#define BES_LOAD_F_NOSIGNAL_FW_NAME "_f_fw_usb_nosignal.bin"
#define BES_LOAD_F_BTRF_FW_NAME "_f_fw_usb_btrf.bin"

#define BES_LOAD_FW_NAME_USB3 "_fw_usb3.bin"
#define BES_LOAD_NOSIG_FW_NAME_USB3 "_fw_usb3_nosignal.bin"
#define BES_LOAD_BTRF_FW_NAME_USB3 "_fw_usb3_btrf.bin"
#define BES_LOAD_F_PGM_NAME_USB3 "_f_pgm_usb3.bin"
#define BES_LOAD_F_FW_NAME_USB3 "_f_fw_usb3.bin"

#elif defined(FW_DOWNLOAD_BY_PCIE)
#define BES_LOAD_FW_NAME "_fw_pcie.bin"
#define BES_LOAD_NOSIG_FW_NAME "_fw_pcie_nosignal.bin"
#define BES_LOAD_BTRF_FW_NAME "_fw_pcie_btrf.bin"

#elif defined(FW_DOWNLOAD_BY_IBUS)
#define SYS_OP_WIFI_PATCH1      0x1
#define SYS_OP_WIFI_PATCH2      0x2
#define SYS_OP_WIFI_INIT        0x4
#define BES_FW_PATCH1_OFFSET    0x17000
#define BES_LOAD_FW_PATCH1_NAME "_fw_ibus_patch1.bin"
#define BES_FW_PATCH2_OFFSET    0x28000
#define BES_LOAD_FW_PATCH2_NAME "_fw_ibus_patch2.bin"
#endif
/***** bes fw error code *****/
enum ERR_CODE {
	ERR_NONE = 0x00,
	ERR_LEN = 0x01,
};

/***** data struct *****/
struct fw_msg_hdr_t {
	u8 type;
	u8 seq;
	u16 len;
};

struct fw_frame_t {
	struct fw_msg_hdr_t header;
	union {
		u32 payload;
		u32 variable[0];
	};
};

struct fw_msg_replay_t {
	u32 replay;
};

struct fw_info_t {
	u32 len;
	u32 addr;
};

struct fw_download_t {
	u32 addr;
	u8 data[0];
};

struct fw_crc_t {
	u32 crc32;
};

struct fw_header_t {
	u32 migc_num;
	u16 param0;
	u16 param1;
	u32 crc32;
	u32 param2;
};

struct fw_run_addr_t {
	u32 addr;
};

struct fw_flash_data_t {
	u32 addr;
	u32 len;
};

struct exec_struct_t {
	u32 entry;
	u32 param;
	u32 sp;
	u32 exec_addr;
};

struct platform_fw_t {
#ifdef FW_DOWNLOAD_BY_SDIO
	struct sdio_func *func;
	u8 rx_len;
#endif
#ifdef FW_DOWNLOAD_BY_USB
	struct completion completion_rx;
#endif
	struct completion completion_tx;
	struct rwnx_plat *plat;
	int (* send_cmd)(struct platform_fw_t *fw_data,
					struct fw_frame_t *tx_frame,
					void *rx_frame,
					u16 rec_buf_len,
					int timeout);
	void (* set_fw_download_state)(bool finish);
	char fw_name[BES_FW_NAME_MAX_LEN];
	char temp_name[BES_FW_NAME_MAX_LEN];
	u8 rom_version;
};

void bes_parse_fw_info(const u8 *data, u32 data_len, u32 *load_addr, u32 *crc32);
int bes_frame_rsp_check(void *rsp, u8 sequence, u8 cmd_type);
const u8 *bes_get_firmware_version_info(const u8 *data, u32 count);
char *bes_get_fw_name(char *fw_name, const char *fw_name_tail);
bool bes_firmware_is_pgm(const char *fw_name);
#if defined(FW_DOWNLOAD_BY_SDIO) || defined(FW_DOWNLOAD_BY_USB)
int bes_get_flash_rom_type(struct platform_fw_t *fw_data);
int bes_is_firmware_update(struct platform_fw_t *fw_data, const char *fw_name);
int bes_firmware_download(struct platform_fw_t *fw_data, const char *fw_name, bool auto_run, bool ram_fw);
#endif
#endif /* __BES_FW_COMMON_H__ */
