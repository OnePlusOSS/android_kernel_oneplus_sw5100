/*
 * @file bes_fw_pci.c
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
#include "bes_sysfs.h"
#include "bes_log.h"

#if defined(FW_DOWNLOAD_BY_PCIE)
#include "bes_fw_pci.h"

void my_memcpy_fromio(void *to, const volatile void __iomem *from, size_t count)
{
	while (count && !IS_ALIGNED((unsigned long)from, 8)) {
		*(u8 *)to = __raw_readb(from);
		from++;
		to++;
		count--;
	}

	while (count >= 4) {
		*(u32 *)to = __raw_readl(from);
		from += 4;
		to += 4;
		count -= 4;
	}

	while (count) {
		*(u8 *)to = __raw_readb(from);
		from++;
		to++;
		count--;
	}
}

static int bes_firmware_download(struct rwnx_plat *rwnx_plat, const char *fw_name)
{
	u32 code_length = 0;
	int ret = 0;
	int redownload_cnt = 0;
	u8 *addr;
	const u8 *fw_ver_ptr;
	const u8 *data_p;
	u8 *data_back;
	const struct firmware *fw_bin;

	struct fw_info_t fw_info;
	struct fw_download_t download_addr;
	struct fw_crc_t crc32_t;
	struct conn_firmware_param_t fw_param;

	//  get firmware data
	ret = request_firmware(&fw_bin, fw_name, NULL);
	if (ret) {
		bes_err(BES_DBG_DOWNLOAD, "request firmware err:%d\n", ret);
		return ret;
	}

	// parse firmware information
	bes_parse_fw_info(fw_bin->data, fw_bin->size, &fw_info.addr, &crc32_t.crc32);

	fw_ver_ptr = bes_get_firmware_version_info(fw_bin->data, fw_bin->size);

	if (fw_ver_ptr == NULL) {
		bes_err(BES_DBG_DOWNLOAD, "------Firmware version get failed\n");
		ret = -EFAULT;
		goto err_release_fw;
	} else {
		bes_info(BES_DBG_DOWNLOAD, "------Firmware: %s version :%s\n", fw_name, fw_ver_ptr);
	}

	bes_info(BES_DBG_DOWNLOAD, "------load addr  :0x%08X\n", fw_info.addr);
	bes_info(BES_DBG_DOWNLOAD, "------data crc   :0x%08X\n", crc32_t.crc32);

	code_length = fw_bin->size - CODE_DATA_USELESS_SIZE;
	bes_info(BES_DBG_DOWNLOAD, "------code size  :%d\n", code_length);

	fw_info.len = code_length;
	data_p = fw_bin->data;
	download_addr.addr = fw_info.addr - RAM_BASE;
	bes_info(BES_DBG_DOWNLOAD, "------download_addr.addr  :0x%x\n", download_addr.addr);
	addr = rwnx_plat->get_address(rwnx_plat, RWNX_ADDR_CPU, download_addr.addr);
	data_back = kmalloc(CHECK_LENGTH, GFP_KERNEL);
	if (!data_back) {
		bes_err(BES_DBG_DOWNLOAD, "%s:%d fw failed to allocate memory\n", __func__, __LINE__);
		ret = -ENOMEM;
		goto err_release_fw;
	}
retry:
	code_length = 0;
	memcpy_toio(addr, data_p, fw_info.len);//download bin
	while (code_length <= fw_info.len) {
		my_memcpy_fromio(data_back, addr + code_length, (fw_info.len - code_length)  > CHECK_LENGTH ? CHECK_LENGTH : fw_info.len % CHECK_LENGTH);
		if (memcmp(data_back, data_p + code_length, (fw_info.len - code_length) > CHECK_LENGTH ? CHECK_LENGTH : fw_info.len % CHECK_LENGTH)) {
			redownload_cnt ++;
			bes_err(BES_DBG_DOWNLOAD, "fw download by pcie err redownload_cnt is %d\n", redownload_cnt);
			if (redownload_cnt >= 3) {
				bes_err(BES_DBG_DOWNLOAD, "fw download by pcie for %d times\n", redownload_cnt);
				ret = -ETIMEDOUT;
				goto err_download;
			}
			goto retry;
		}
		code_length += CHECK_LENGTH;
	}

	addr = rwnx_plat->get_address(rwnx_plat, RWNX_ADDR_CPU, CONN_FW_PARAM_ADDR);
	fw_param.addr = fw_info.addr;
	fw_param.len = fw_info.len;
	fw_param.boot_addr = fw_info.addr;
	fw_param.crc32 = crc32_t.crc32;
	memcpy_toio(addr, &fw_param, sizeof(fw_param));//ready to run fw

	ret = 0; /* no error, set 0 */
err_download:
	kfree(data_back);
err_release_fw:
	release_firmware(fw_bin);
	return ret;
}

int bes_load_firmware_pcie(struct rwnx_plat *rwnx_plat)
{
	int ret;
	// struct platform_fw_t *temp_fw_data;
	int fw_type = bes_sysfs_get_fw_type();
	char fw_name[BES_FW_NAME_MAX_LEN];

	if (fw_type == BES_FW_TYPE_WIFI_SIGNAL) {
		ret = bes_firmware_download(rwnx_plat, bes_get_fw_name(fw_name, BES_LOAD_FW_NAME));
		/* Non-signal mode loading ram firmware */
	} else if (fw_type == BES_FW_TYPE_WIFI_NONSIGNAL) {
		ret = bes_firmware_download(rwnx_plat, bes_get_fw_name(fw_name, BES_LOAD_NOSIG_FW_NAME));
	} else {
		ret = bes_firmware_download(rwnx_plat, bes_get_fw_name(fw_name, BES_LOAD_BTRF_FW_NAME));
	}
	if (ret)
		bes_err(BES_DBG_DOWNLOAD, "download normal firmware failed %d.\n", ret);

	return ret;
}
#endif