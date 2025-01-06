/*
 * @file lmac_log_ctrl.c
 *
 * Copyright (c) 2022, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "rwnx_defs.h"
#include "rwnx_msg_tx.h"
#include "bes_log.h"
#include "bes_sysfs.h"

const char *lmac_log_info_template = "\
===========================================================================================\n\
| module  |  Bit7   |  Bit6   |  Bit5   |  Bit4   |  Bit3   |  Bit2   |  Bit1   |  Bit0   |\n\
-------------------------------------------------------------------------------------------\n\
| KERN[0] |               reserve                 |  STATE  | MSG_LOC | MSG_API |   ERR   |\n\
| (0x00)  |               invalid                 |    0    |    0    |    0    |    0    |\n\
-------------------------------------------------------------------------------------------\n\
| LMAC[0] |   TD    |   TPC   |   BA    |   DOZE  |  RX_ALL |   RX    |   TX    |   ERR   |\n\
| (0x00)  |    0    |    0    |    0    |    0    |    0    |    0    |    0    |    0    |\n\
-------------------------------------------------------------------------------------------\n\
| LMAC[1] |            reserve          |  RF_DBG |   CCA   |   RC    |  TX_TB  |  TD_DBG |\n\
| (0x00)  |            invalid          |    0    |    0    |    0    |    0    |    0    |\n\
-------------------------------------------------------------------------------------------\n\
| CHAN[0] |   TBTT  |   NOA   | CON_LES |  STATUS |  SWITCH |  CREATE |   INFO  |   ERR   |\n\
| (0x00)  |    0    |    0    |    0    |    0    |    0    |    0    |    0    |    0    |\n\
-------------------------------------------------------------------------------------------\n\
| CHAN[1] |                               reserve                               |   COEX  |\n\
| (0x00)  |                               invalid                               |    0    |\n\
-------------------------------------------------------------------------------------------\n\
| STA[0]  |            reserve          |SCHD_SCAN|   CONN  |   PS    |   BCN   |   ERR   |\n\
| (0x00)  |            invalid          |    0    |    0    |    0    |    0    |    0    |\n\
-------------------------------------------------------------------------------------------\n\
| AP[0]   |                   reserve                       |   PS    |   BCN   |   ERR   |\n\
| (0x00)  |                   invalid                       |    0    |    0    |    0    |\n\
-------------------------------------------------------------------------------------------\n\
| P2P[0]  |      reserve      |   TBTT  | NOA_DET |   NOA   |   PS    |   ITF   |   ERR   |\n\
| (0x00)  |      invalid      |    0    |    0    |    0    |    0    |    0    |    0    |\n\
-------------------------------------------------------------------------------------------\n\
| MESH[0] |  BCN_RX |   RX    |  SLEEP  |   AWIN  |   SP    |   PS    |   INFO  |   ERR   |\n\
| (0x00)  |    0    |    0    |    0    |    0    |    0    |    0    |    0    |    0    |\n\
-------------------------------------------------------------------------------------------\n\
| MESH[1] |      reserve      |   TBTT  |   MPM   |  MPATH  |   HWMP  | PST_TFC |  BCN_TX |\n\
| (0x00)  |      invalid      |    0    |    0    |    0    |    0    |    0    |    0    |\n\
-------------------------------------------------------------------------------------------\n\
| TDLS[0] |                        reserve                            |   CWH   |   ERR   |\n\
| (0x00)  |                        invalid                            |    0    |    0    |\n\
-------------------------------------------------------------------------------------------\n\
| RM[0]   |          reserve            | REQUEST |  REPORT | MEASURE |   WARN  |   ERR   |\n\
| (0x00)  |          invalid            |    0    |    0    |    0    |    0    |    0    |\n\
-------------------------------------------------------------------------------------------\n\
| RTOS[0] | reserve | SWH_OUT |  SWH_IN | SUSPEND |  CREATE |   FREE  |  ALLOC  |   ERR   |\n\
| (0x00)  | invalid |    0    |    0    |    0    |    0    |    0    |    0    |    0    |\n\
-------------------------------------------------------------------------------------------\n\
| FHOST[0]| reserve | SMT_CFG | TSK_INI |   DHCP  | WPA_INF | WPA_CMD | WPA_FSM |   ERR   |\n\
| (0x00)  | invalid |    0    |    0    |    0    |    0    |    0    |    0    |    0    |\n\
-------------------------------------------------------------------------------------------\n\
| APP[0]  |               reserve                 |   DBG   |   INFO  |   WARN  |   ERR   |\n\
| (0x00)  |               invalid                 |    0    |    0    |    0    |    0    |\n\
-------------------------------------------------------------------------------------------\n\
| TWT[0]  |           reserve           |   DBG   |   TX    |   RX    |   INFO  |   ERR   |\n\
| (0x00)  |           invalid           |    0    |    0    |    0    |    0    |    0    |\n\
-------------------------------------------------------------------------------------------\n\
| FTM[0]  |                        reserve                            |   INFO  |   ERR   |\n\
| (0x00)  |                        invalid                            |    0    |    0    |\n\
-------------------------------------------------------------------------------------------\n\
| COEX[0] |   SCO   |   BTC   |   MAIN  |   NOA   |  TRANSQ |   TXRX  |   CHAN  |   PTA   |\n\
| (0x00)  |    0    |    0    |    0    |    0    |    0    |    0    |    0    |    0    |\n\
-------------------------------------------------------------------------------------------\n\
| COEX[1] |                            reserve                                  |   PROT  |\n\
| (0x00)  |                            invalid                                  |    0    |\n\
===========================================================================================\n";

#define TEMPLATE_ONE_COMPO_SIZE 10
#define TEMPLATE_COMPO_NUM_MAX_IN_LINE 8
#define TEMPLATE_COMPO_BIT0_START_OFFSET 85
#define TEMPLATE_ONE_LINE_SIZE ((TEMPLATE_COMPO_NUM_MAX_IN_LINE + 1) * TEMPLATE_ONE_COMPO_SIZE + 2)

static u8 lmac_compo_level_max(enum trace_compo compo)
{
	u8 max = 0;
	switch(compo) {
		case TRACE_COMPO_KERNEL: 	max = TRACE_LVL_KERNEL_MAX; 	break;
		case TRACE_COMPO_LMAC:		max = TRACE_LVL_LMAC_MAX; 	break;
		case TRACE_COMPO_CHAN:		max = TRACE_LVL_CHAN_MAX; 	break;
		case TRACE_COMPO_STA:		max = TRACE_LVL_STA_MAX; 	break;
		case TRACE_COMPO_AP:		max = TRACE_LVL_AP_MAX; 	break;
		case TRACE_COMPO_P2P:		max = TRACE_LVL_P2P_MAX; 	break;
		case TRACE_COMPO_MESH:		max = TRACE_LVL_MESH_MAX; 	break;
		case TRACE_COMPO_TDLS:		max = TRACE_LVL_TDLS_MAX; 	break;
		case TRACE_COMPO_RM:		max = TRACE_LVL_RM_MAX; 	break;
		case TRACE_COMPO_RTOS:		max = TRACE_LVL_RTOS_MAX; 	break;
		case TRACE_COMPO_FHOST:		max = TRACE_LVL_FHOST_MAX; 	break;
		case TRACE_COMPO_APP:		max = TRACE_LVL_APP_MAX; 	break;
		case TRACE_COMPO_TWT:		max = TRACE_LVL_TWT_MAX; 	break;
		case TRACE_COMPO_FTM:		max = TRACE_LVL_FTM_MAX; 	break;
		case TRACE_COMPO_COEX:		max = TRACE_LVL_COEX_MAX; 	break;
		default:
			bes_err(BES_DBG_LMAC_LOG_FILTER, "%s compo:%d inlvalid\n", __func__, compo);
			break;
	}
	return max;
}

static char *lmac_log_compo_name(enum trace_compo compo)
{
	char *name = NULL;
	switch(compo) {
		case TRACE_COMPO_KERNEL:	name = "KERN";		break;
		case TRACE_COMPO_LMAC:		name = "LMAC";		break;
		case TRACE_COMPO_CHAN:		name = "CHAN";		break;
		case TRACE_COMPO_STA:		name = "STA";		break;
		case TRACE_COMPO_AP:		name = "AP";		break;
		case TRACE_COMPO_P2P:		name = "P2P";		break;
		case TRACE_COMPO_MESH:		name = "MESH";		break;
		case TRACE_COMPO_TDLS:		name = "TDLS";		break;
		case TRACE_COMPO_RM:		name = "RM";		break;
		case TRACE_COMPO_RTOS:		name = "RTOS";		break;
		case TRACE_COMPO_FHOST:		name = "FHOST";		break;
		case TRACE_COMPO_APP:		name = "APP";		break;
		case TRACE_COMPO_TWT:		name = "TWT";		break;
		case TRACE_COMPO_FTM:		name = "FTM";		break;
		case TRACE_COMPO_COEX:		name = "COEX";		break;
		default:
			bes_err(BES_DBG_LMAC_LOG_FILTER, "%s compo:%d inlvalid\n", __func__, compo);
			break;
	}
	return name;
}

/* Output LMAC log filtering through a table */
int lmac_log_filter_show(struct rwnx_hw *rwnx_hw, char *show_buf, u32 buf_len)
{
	u32 trace_compo_level[TRACE_COMPO_MAX] = {0};
	char (*buf_pos)[TEMPLATE_ONE_LINE_SIZE] = NULL;
	u16 row = 0, col = 0;
	u16 compo_idx, compo_num, compo_len, compo_len_max, bit_idx, bit_start, bit_end;
	u8 temp_level = 0;
	int error = 0;

	BUG_ON(!show_buf || !buf_len);

	/* Update the log filtering level of LMAC. */
	error = bes_get_lmac_log_level(rwnx_hw, trace_compo_level);
	if (error) {
		bes_err(BES_DBG_LMAC_LOG_FILTER, "%s read lmac log filter fail\n", __func__);
		return -EPERM;
	}

	/* Copy template to destination buffer. */
	strncpy(show_buf, lmac_log_info_template, buf_len);
	buf_pos = (char (*)[TEMPLATE_ONE_LINE_SIZE])show_buf;
	row += 2;

	/* Modify the switch values of each component in the template table. */
	for (compo_idx = TRACE_COMPO_KERNEL; compo_idx < TRACE_COMPO_MAX; compo_idx++) {
		compo_num = lmac_compo_level_max(compo_idx);
		compo_len_max = compo_num / TEMPLATE_COMPO_NUM_MAX_IN_LINE;
		if (compo_num % TEMPLATE_COMPO_NUM_MAX_IN_LINE != 0)
			compo_len_max++;

		for (compo_len = 0; compo_len < compo_len_max; compo_len++) {
			row += 2;
			temp_level = (u8)(-1);
			temp_level &= trace_compo_level[compo_idx] >> (compo_len * 8);
			sprintf((char *)(&buf_pos[row][5]), "%02X", temp_level);
			buf_pos[row][7] = ')';

			col = TEMPLATE_COMPO_BIT0_START_OFFSET;
			bit_start = compo_len * TEMPLATE_COMPO_NUM_MAX_IN_LINE;
			bit_end = ((compo_len + 1) * TEMPLATE_COMPO_NUM_MAX_IN_LINE);
			if (bit_end > compo_num)
				bit_end = compo_num;

			for (bit_idx = bit_start; bit_idx < bit_end; bit_idx++) {
				buf_pos[row][col] = !!(trace_compo_level[compo_idx] & (1U << bit_idx)) + '0';
				col -= TEMPLATE_ONE_COMPO_SIZE;
			}
			row++;
		}
	}

	return strlen(show_buf);
}

static int lmac_log_flush(struct rwnx_hw *rwnx_hw)
{
	struct mm_custom_req_hdr hdr;

	/* Set command header */
	hdr.id = CUSTOM_ID_LMAC_LOG;
	hdr.length = sizeof(struct mm_custom_req_hdr);
	hdr.ops = LMAC_LOG_OPS_FLUSH;

	/* send custom request to device */
	return rwnx_send_custom_request(rwnx_hw, &hdr, NULL, 0);
}

/* Set the switch for the LMAC log component */
int lmac_log_filter_set(struct rwnx_hw *rwnx_hw, const char __user *user_buf, size_t count, loff_t *ppos)
{
	char buf[64] = {0};
	size_t len = min_t(size_t, count, sizeof(buf) - 1);
	u32 target_compon_level[TRACE_COMPO_MAX] = {0};
	u8 compo_idx = 0;
	int error = 0, argc = 0;
	char *argv[4] = {NULL};

	/* Get the content of the file */
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';

	/* Parsing command line */
	argc = bes_common_parse_line(buf, argv, 4);
	if (argc <= 0) {
		bes_err(BES_DBG_LMAC_LOG_FILTER, "%s ERROR: fail to parse line. argc:%d\n", __func__, argc);
		return -EINVAL;
	}

	/* Parse Command. */
	if (argc == 3 && !strncasecmp(argv[0], "SET", 3)) {
		/* Obtain component name */
		for (compo_idx = 0; compo_idx < TRACE_COMPO_MAX; compo_idx++) {
			if (!strcasecmp(lmac_log_compo_name(compo_idx), argv[1]))
				break;
		}

		/* Exit when there are no matching components */
		if (compo_idx == TRACE_COMPO_MAX) {
			bes_err(BES_DBG_LMAC_LOG_FILTER, "%s not find compo:%s\n", __func__, argv[1]);
			return -EINVAL;
		}

		/* Obtain target level */
		if ((error = kstrtoint(argv[2], 0, &target_compon_level[compo_idx]))) {
			bes_err(BES_DBG_LMAC_LOG_FILTER, "%s invalid level:%s\n", __func__, argv[2]);
			return -EINVAL;
		}

		/* Send target component level to LMAC */
		if ((error = bes_set_lmac_log_level(rwnx_hw, target_compon_level, 1U << compo_idx))) {
			bes_err(BES_DBG_LMAC_LOG_FILTER, "%s send target compon level fail:%d\n", __func__, error);
			return -EFAULT;
		}
	} else if (argc == 1 && !strncasecmp(argv[0], "flush", 5)) {
		if ((error = lmac_log_flush(rwnx_hw))) {
			bes_err(BES_DBG_LMAC_LOG_FILTER, "%s send flush lmac log fail:%d\n", __func__, error);
			return -EFAULT;
		}
	} else
		bes_err(BES_DBG_LMAC_LOG_FILTER, "%s Command format error, please refer to the following command:\n"
			 "echo set CHAN 0 > lmac_log_ctrl\n", __func__);

	return len;
}

int bes_set_lmac_log_level(struct rwnx_hw *rwnx_hw, u32 *trace_compo_level, u32 compon_mask)
{
	int ret;
	struct lmac_log_set_req req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);
	BUG_ON(!trace_compo_level || !compon_mask);

	/* set header */
	req.hdr.id = CUSTOM_ID_LMAC_LOG;
	req.hdr.length = sizeof(struct lmac_log_set_req);
	req.hdr.ops = LMAC_LOG_OPS_LEVEL_SET;

	/* set param */
	req.config.trace_compo_mask = compon_mask;
	memcpy(req.config.trace_compo_level, trace_compo_level,
	       sizeof(req.config.trace_compo_level));

	// send custom request to device
	ret = rwnx_send_custom_request(rwnx_hw, &req.hdr, NULL, 0);

	return ret;
}

int bes_get_lmac_log_level(struct rwnx_hw *rwnx_hw, u32 *trace_compo_level)
{
	int ret;
	struct lmac_log_get_cfm cfm;
	struct lmac_log_get_req req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);
	BUG_ON(!trace_compo_level);

	/* set header */
	req.hdr.id = CUSTOM_ID_LMAC_LOG;
	req.hdr.length = sizeof(struct lmac_log_get_req);
	req.hdr.ops = LMAC_LOG_OPS_LEVEL_GET;

	// send custom request to device
	ret = rwnx_send_custom_request(rwnx_hw, &req.hdr, &cfm, sizeof(cfm));

	if (!ret) {
		/* Obtain component level */
		memcpy(trace_compo_level, cfm.config.trace_compo_level,
		       sizeof(cfm.config.trace_compo_level));
	}

	return ret;
}

int bes_set_lmac_log_mode(struct rwnx_hw *rwnx_hw, u8 log_mode)
{
	int ret;
	struct lmac_log_set_req req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* set header */
	req.hdr.id = CUSTOM_ID_LMAC_LOG;
	req.hdr.length = sizeof(struct lmac_log_set_req);
	req.hdr.ops = LMAC_LOG_OPS_MODE_SET;

	/* set param */
	req.config.log_mode = log_mode;

	// send custom request to device
	ret = rwnx_send_custom_request(rwnx_hw, &req.hdr, NULL, 0);

	return ret;
}

int bes_get_lmac_log_mode(struct rwnx_hw *rwnx_hw, u8 *log_mode)
{
	int ret;
	struct lmac_log_get_cfm cfm;
	struct lmac_log_get_req req;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);
	BUG_ON(!log_mode);

	/* set header */
	req.hdr.id = CUSTOM_ID_LMAC_LOG;
	req.hdr.length = sizeof(struct lmac_log_get_req);
	req.hdr.ops = LMAC_LOG_OPS_MODE_GET;

	// send custom request to device
	ret = rwnx_send_custom_request(rwnx_hw, &req.hdr, &cfm, sizeof(cfm));

	if (!ret) {
		/* Obtain component level */
		*log_mode = cfm.config.log_mode;
	}

	return ret;
}

static void bes_auto_set_fw_log_type(struct rwnx_hw *rwnx_hw, enum auto_lmac_log_status lmac_log_status)
{
	int ret;
	u8 fw_log_type = 0;
	u8 lmac_log_mode = 0;

	if (lmac_log_status == AUTO_LMAC_LOG_SUSPEND_STATUS) {
		/* When the system suspend, LMAC logs are uploaded to the MCU and printed in MCU */
		fw_log_type |= FW_LOG_LMAC_LOCAL;
		lmac_log_mode = NX_PRINT_IPC;
	} else if (lmac_log_status == AUTO_LMAC_LOG_RESUME_STATUS) {
		/* When the system resume, LMAC logs are uploaded to the host and printed in host */
		fw_log_type |= FW_LOG_LMAC_UPLOAD;
		lmac_log_mode = NX_PRINT_IPC;
	}

	ret = bes_set_lmac_log_mode(rwnx_hw, lmac_log_mode);
	bes_err_with_cond(ret, BES_DBG_SYSFS, "%s set lmac log mode fail:%d\n", __func__, ret);

	ret = bes_send_dump_fw_log(rwnx_hw, fw_log_type);
	bes_err_with_cond(ret, BES_DBG_SYSFS, "%s set dump fw log fail:%d\n", __func__, ret);
}

static void bes_auto_set_lmac_log_ctrl(struct rwnx_hw *rwnx_hw, enum auto_lmac_log_status lmac_log_status)
{
	int ret;
	u32 target_compon_level[TRACE_COMPO_MAX] = {0};

	if (lmac_log_status == AUTO_LMAC_LOG_SUSPEND_STATUS) {
		/* Reduce the LMAC log when the system is suspend */
		target_compon_level[TRACE_COMPO_KERNEL] = 0x0001;
		target_compon_level[TRACE_COMPO_LMAC] = 0x0021;
		target_compon_level[TRACE_COMPO_CHAN] = 0x0027;
		target_compon_level[TRACE_COMPO_STA] = 0x0009;
	} else if (lmac_log_status == AUTO_LMAC_LOG_RESUME_STATUS) {
		/* Increases the LMAC log when the system is resume */
		target_compon_level[TRACE_COMPO_KERNEL] = 0x0007;
		target_compon_level[TRACE_COMPO_LMAC] = 0x14b9;
		target_compon_level[TRACE_COMPO_CHAN] = 0x00ff;
		target_compon_level[TRACE_COMPO_STA] = 0x000f;
		target_compon_level[TRACE_COMPO_AP] = 0x0007;
		target_compon_level[TRACE_COMPO_P2P] = 0x000f;
		target_compon_level[TRACE_COMPO_MESH] = 0x0003;
		target_compon_level[TRACE_COMPO_TDLS] = 0x0003;
		target_compon_level[TRACE_COMPO_RM] = 0x0001;
		target_compon_level[TRACE_COMPO_RTOS] = 0x0009;
		target_compon_level[TRACE_COMPO_FHOST] = 0x0001;
		target_compon_level[TRACE_COMPO_APP] = 0x0001;
		target_compon_level[TRACE_COMPO_TWT] = 0x0003;
		target_compon_level[TRACE_COMPO_FTM] = 0x0003;
		target_compon_level[TRACE_COMPO_COEX] = 0x01ff;
	}

	ret = bes_set_lmac_log_level(rwnx_hw, target_compon_level, 0xffff);
	if (ret)
		bes_err(BES_DBG_LMAC_LOG_FILTER, "%s set lmac log level fail:%d\n", __func__, ret);
}

static void bes_auto_set_fw_dbg_log(struct rwnx_hw *rwnx_hw, enum auto_lmac_log_status lmac_log_status)
{
	int ret;
	u32 dbg_filter = 0;

	if (lmac_log_status == AUTO_LMAC_LOG_SUSPEND_STATUS) {
		/* Reduce the LMAC dbg log when the system is suspend */
		dbg_filter = 0;
	} else if (lmac_log_status == AUTO_LMAC_LOG_RESUME_STATUS) {
		/* Increases the LMAC dbg log when the system is resume */
		dbg_filter = DBG_MOD_ALL;
	}

	ret = rwnx_send_dbg_set_mod_filter_req(rwnx_hw, dbg_filter);
	bes_err_with_cond(ret, BES_DBG_SYSFS, "%s set dump fw dbg log fail:%d\n", __func__, ret);
}

void bes_auto_set_fw_log(struct rwnx_hw *rwnx_hw, enum auto_lmac_log_status lmac_log_status)
{
	if (rwnx_hw->mod_params && rwnx_hw->mod_params->auto_lmac_log_ctrl) {
                bes_info(BES_DBG_LMAC_LOG_FILTER, "set auto_lmac_log_status: %d\n", lmac_log_status);

		if (lmac_log_status == AUTO_LMAC_LOG_SUSPEND_STATUS) {
			bes_auto_set_fw_dbg_log(rwnx_hw, lmac_log_status);
			bes_auto_set_lmac_log_ctrl(rwnx_hw, lmac_log_status);
			bes_auto_set_fw_log_type(rwnx_hw, lmac_log_status);
		} else if (lmac_log_status == AUTO_LMAC_LOG_RESUME_STATUS) {
			bes_auto_set_fw_log_type(rwnx_hw, lmac_log_status);
			bes_auto_set_fw_dbg_log(rwnx_hw, lmac_log_status);
			bes_auto_set_lmac_log_ctrl(rwnx_hw, lmac_log_status);
		}
	}
}