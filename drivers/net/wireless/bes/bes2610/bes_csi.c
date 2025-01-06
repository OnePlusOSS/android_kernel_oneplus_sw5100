/*
 * @file bes_csi.c
 *
 * Copyright (c) 2024, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "rwnx_msg_tx.h"
#include "bes_csi.h"
#include "bes_log.h"

static struct mm_csi_cap_req csi_req = {
	.bw = WIFI_CFG_CSI_CAP_ALL_BW_EN,
	.type = WIFI_CFG_CSI_CAP_ALL_TYPE_EN,
	.stbc = 1,
	.mac_match = 0,
	.lsig_len_min = 0,
	.lsig_len_max = 0xFFE,
	.csi_sniff = 0,
};

static char *csi_status_bw[4] = {
	"20MHZ",
	"40MHZ",
	"80MHZ",
	"160MHZ",
};

static char *csi_status_format[9] = {
	"NON_HT",
	"NON_HT_DUP",
	"HT_MM",
	"HT_GF",
	"VHT",
	"HE_SU",
	"HE_MU",
	"HE_ER_SU",
	"HE_TB",
};

int bes_csi_recv_cb(struct rwnx_hw *rwnx_hw, struct mm_csi_cap_ind *ind)
{
	struct mm_csi_cap_result *csi_result;

	if (!ind)
		return -EINVAL;

	/* First packet */
	if (!rwnx_hw->csi_cap.csi_len) {
		csi_result = (struct mm_csi_cap_result *)rwnx_hw->csi_cap.buf;
		csi_result->status = ind->csi_result.status;
		rwnx_hw->csi_cap.csi_len += sizeof(struct mm_csi_cap_result);
	}

	/* Copy csi result to driver buf */
	if (rwnx_hw->csi_cap.buf_len >= (rwnx_hw->csi_cap.csi_len + ind->data_len)) {
		if (ind->data_len) {
			memcpy(rwnx_hw->csi_cap.buf + rwnx_hw->csi_cap.csi_len, (void *)ind->csi_result.csi_data, ind->data_len);
			rwnx_hw->csi_cap.csi_len += ind->data_len;
		}

		if ((ind->data_offset + ind->data_len) == ind->tot_len)
			complete(&rwnx_hw->csi_cap.csi_cmp);
	} else if (rwnx_hw->csi_cap.buf_len > rwnx_hw->csi_cap.csi_len) {
		bes_info(BES_DBG_CSI_CAP, "%s buf_len(0x%x) is shorter than csi data len\n", __func__, rwnx_hw->csi_cap.buf_len);
		memcpy(rwnx_hw->csi_cap.buf + rwnx_hw->csi_cap.csi_len, (void *)ind->csi_result.csi_data, rwnx_hw->csi_cap.buf_len - rwnx_hw->csi_cap.csi_len);
		rwnx_hw->csi_cap.csi_len = rwnx_hw->csi_cap.buf_len;

		complete(&rwnx_hw->csi_cap.csi_cmp);
	}

	return 0;
}

int bes_send_csi_cap_req(struct rwnx_hw *rwnx_hw, struct mm_csi_cap_req *req, u8 *buf, u32 buf_len)
{
	int ret;

	/* Save buf and buf_len, reinit completion */
	rwnx_hw->csi_cap.buf = buf;
	rwnx_hw->csi_cap.buf_len = buf_len;
	rwnx_hw->csi_cap.csi_len = 0;
	reinit_completion(&rwnx_hw->csi_cap.csi_cmp);

	/* Send csi cap command and wait indication */
	ret = rwnx_send_csi_cap_req(rwnx_hw, req);
	ret = wait_for_completion_interruptible_timeout(&rwnx_hw->csi_cap.csi_cmp, msecs_to_jiffies(CSI_CAP_WAIT_TIME_MAX));
	if (ret <= 0) {
		bes_err(BES_DBG_CSI_CAP, "%s response timeout or interrupt\n", __func__);
		return 0;
	}

	/* Return csi total length */
	return rwnx_hw->csi_cap.csi_len;
}

static int bes_set_csi_info(struct mm_csi_cap_req *csi_config, char *cmd)
{
	u32 info = 0;

	if (!cmd) {
		bes_err(BES_DBG_CSI_CAP, "%s cmd == NULL\n", __func__);
		return -EFAULT;
	}

	if (!strncasecmp(cmd, "bw:", 3) && !kstrtoint(&cmd[3], 0, &info)) {
		csi_config->bw = (u8)info;
	} else if (!strncasecmp(cmd, "type:", 5) && !kstrtoint(&cmd[5], 0, &info)) {
		csi_config->type = (u8)info;
	} else if (!strncasecmp(cmd, "stbc:", 5) && !kstrtoint(&cmd[5], 0, &info)) {
		csi_config->stbc = (u8)info;
	} else if (!strncasecmp(cmd, "match:", 6) && !kstrtoint(&cmd[6], 0, &info)) {
		csi_config->mac_match = (u16)info;
	} else if (!strncasecmp(cmd, "len_min:", 8) && !kstrtoint(&cmd[8], 0, &info)) {
		csi_config->lsig_len_min = (u16)info;
	} else if (!strncasecmp(cmd, "len_max:", 8) && !kstrtoint(&cmd[8], 0, &info)) {
		csi_config->lsig_len_max = (u16)info;
	} else if (!strncasecmp(cmd, "sniff:", 6) && !kstrtoint(&cmd[6], 0, &info)) {
		csi_config->csi_sniff = (u8)info;
	} else {
		bes_err(BES_DBG_CSI_CAP, "%s [ERR] %s\n", __func__, cmd);
		return -EINVAL;
	}

	return 0;
}

int bes_dbgfs_csi_cap_write(char *cmd)
{
	int num;
	int argc = 0;
	char *argv[8] = {NULL};

	/* Parsing command line */
	argc = bes_common_parse_line(cmd, argv, 8);
	if (argc <= 0) {
		bes_err(BES_DBG_CSI_CAP, "%s ERROR: fail to parse line. argc:%d\n", __func__, argc);
		return -EINVAL;
	}

	memset(&csi_req, 0, sizeof(struct mm_csi_cap_req));
	/* Set csi cap command paramters */
	for (num = 0; num < argc; num++) {
		if (bes_set_csi_info(&csi_req, argv[num]))
			return -EINVAL;
	}

	return 0;
}

int bes_dbgfs_csi_cap_read(struct rwnx_hw *rwnx_hw, char *buf, int buf_len)
{
	u32 csi_bufsz = 2052, len = 0, num;
	u8 *csi_buf = NULL;
	int ret;
	struct mm_csi_cap_result *csi_result;
	union wifi_csi_cap_status *csi_status;
	u16 data_len;

	csi_buf = kzalloc(csi_bufsz, GFP_KERNEL);
	if (!csi_buf) {
		bes_err(BES_DBG_CSI_CAP, "%s kzalloc fail\n", __func__);
		return -ENOMEM;
	}

	/* Send csi cap command and copy csi ind to csi_buf */
	ret = bes_send_csi_cap_req(rwnx_hw, &csi_req, csi_buf, csi_bufsz);

	csi_result = (struct mm_csi_cap_result *)csi_buf;
	csi_status = (union wifi_csi_cap_status *)csi_buf;
	data_len = rwnx_hw->csi_cap.csi_len - sizeof(struct mm_csi_cap_result);
	/* Save csi status in buf */
	len += scnprintf(buf + len, buf_len - len, "bw:%s format:%s stbc:0x%x lsig_len:0x%x data_len:0x%x\n",
		csi_status_bw[csi_status->bw], csi_status_format[csi_status->formats], csi_status->stbc, csi_status->lsig_len, data_len);

	/* Save csi ind in buf */
	for (num = 0; num < data_len / 4; num++) {
		if ((num + 1) % 8 == 0)
			len += scnprintf(buf + len, buf_len - len, "%08x\n", *(csi_result->csi_data + num));
		else
			len += scnprintf(buf + len, buf_len - len, "%08x ", *(csi_result->csi_data + num));
	}
	kfree(csi_buf);

	return len;
}

void bes_csi_cap_init(struct rwnx_hw *rwnx_hw)
{
	init_completion(&rwnx_hw->csi_cap.csi_cmp);
}

