/*
 * @file bes_fw_ibus.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/string.h>
#include <linux/iopoll.h>
#include "bes_fw_common.h"
#include "rwnx_platform.h"
#include "bes_sysfs.h"
#include "bes_log.h"
#include "bes_ibus_rpmsg.h"

#if defined(FW_DOWNLOAD_BY_IBUS)

/*
 * TODO: how to process sys hook init when patch1 ???
 */
static int bes_firmware_download_patch(struct rwnx_plat *rwnx_plat,
		int index, const struct firmware *patch)
{
#ifdef CONFIG_BES_IBUS_RPMSG
	u32 offset = (index == 0) ? BES_FW_PATCH1_OFFSET : BES_FW_PATCH2_OFFSET;
	u8 *patch_vaddr = rwnx_plat->get_address(rwnx_plat, RWNX_ADDR_WIFI_SRAM, offset);
	memcpy_toio(patch_vaddr, patch->data, patch->size);
#else
	// pseudo rpmsg don't support wifi patch
#endif
	return 0;
}

static int bes_firmware_download(struct rwnx_plat *rwnx_plat)
{
	bool patched = false;
	int ret, i;
	char fw_name[BES_FW_NAME_MAX_LEN];
	char patch_name[2][BES_FW_NAME_MAX_LEN] = {BES_LOAD_FW_PATCH1_NAME, BES_LOAD_FW_PATCH2_NAME};
	const struct firmware *fw_bin;

	for (i = 0; i < 2; i++) {
		ret = request_firmware(&fw_bin, bes_get_fw_name(fw_name, &patch_name[i][0]), NULL);
		if (ret) {
			bes_dbg(BES_DBG_DOWNLOAD, "request firmware(%s) err(%d)\n", fw_name, ret);
			ret = 0;
		} else {
			ret = bes_firmware_download_patch(rwnx_plat, i, fw_bin);
			release_firmware(fw_bin);
			if (ret) {
				bes_err(BES_DBG_DOWNLOAD, "download firmware err=%d\n", ret);
				goto exit;
			}
			patched = true;
		}
	}

#if 1
	if (patched)
		if ((ret = bes_ibus_rpmsg_wifi_patched())) {
			bes_err(BES_DBG_DOWNLOAD, "patched firmware err=%d\n", ret);
			goto exit;
		}

	if ((ret = bes_ibus_rpmsg_wifi_run()))
		bes_err(BES_DBG_DOWNLOAD, "run firmware err=%d\n", ret);
#endif

exit:
	return ret;
}

int bes_load_firmware_ibus(struct rwnx_plat *rwnx_plat)
{
	int ret;

	ret = bes_firmware_download(rwnx_plat);
	if (ret)
		bes_err(BES_DBG_DOWNLOAD, "download normal firmware failed %d.\n", ret);

	return ret;
}
#endif
