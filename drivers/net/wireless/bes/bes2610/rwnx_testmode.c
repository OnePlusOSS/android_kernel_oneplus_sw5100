/**
 ****************************************************************************************
 *
 * @file rwnx_testmode.c
 *
 * @brief Test mode function definitions
 *
 * Copyright (C) RivieraWaves 2012-2021
 *
 ****************************************************************************************
 */
#include "rwnx_defs.h"
#include "rwnx_msg_tx.h"
#include "rwnx_dini.h"
#include "reg_access.h"
#include "rwnx_testmode.h"
#include "lmac_msg.h"
#include "bes_log.h"
#include "bes_sysfs.h"

/*
 * mcu msg error code
 * The error code convert to the Linux error code in the driver.
 */
enum mcu_msg_err {
	MCU_MSG_EINVAL = 1,
	MCU_MSG_EBADRQC = 2,
	/* add new here */

	MCU_MSG_ERR_NUM,
};

/* convert mcu status code */
static int const g_drvmcu_err[MCU_MSG_ERR_NUM] = {
	[MCU_MSG_EINVAL] = -EINVAL,
	[MCU_MSG_EBADRQC] = -EBADRQC,
};

/**********************************************************************/

void bes_tsm_resp_assembly(struct bes_tm_rp *tm_rp, s32 status, u32 cmd_type, u8 *data, u16 msg_len)
{
	tm_rp->hdr.len = msg_len + sizeof(struct bes_tm_rp_hdr);
	tm_rp->hdr.cmd_type = cmd_type;
	tm_rp->hdr.status = status;

	if (msg_len)
		memcpy(tm_rp->sysfs_show, data, msg_len);
	else
		tm_rp->sysfs_show[0] = '\0';

	bes_dbg(BES_DBG_TESTMODE, "%s, print replay msg: %s\n", __func__, tm_rp->sysfs_show);
}

/*
 * This function handles the user application commands for register access.
 *
 * It retrieves command ID carried with RWNX_TM_ATTR_COMMAND and calls to the
 * handlers respectively.
 *
 * If it's an unknown commdn ID, -ENOSYS is returned; or -ENOMSG if the
 * mandatory fields(RWNX_TM_ATTR_REG_OFFSET,RWNX_TM_ATTR_REG_VALUE32)
 * are missing; Otherwise 0 is replied indicating the success of the command execution.
 *
 * If RWNX_TM_ATTR_COMMAND is RWNX_TM_CMD_APP2DEV_REG_READ, the register read
 * value is returned with RWNX_TM_ATTR_REG_VALUE32.
 */
int rwnx_testmode_reg(struct rwnx_hw *rwnx_hw, struct rwnx_sysfs_tm_headr *tm_msg, size_t count,
		      struct bes_tm_rp *tm_rp)
{
	u32 mem_addr;
	int ret = 0;

	mem_addr = *((u32 *)tm_msg->data);

	switch (tm_msg->id) {
	case RWNX_TM_CMD_APP2DEV_REG_READ: {
		struct dbg_mem_read_cfm mem_read_cfm;
		struct app2dev_reg_read_value read_value;

		/*** Send the command to the LMAC ***/
		if ((ret = rwnx_send_dbg_mem_read_req(rwnx_hw, mem_addr, &mem_read_cfm)))
			goto err;

		read_value.reg_value = mem_read_cfm.memdata;
		bes_tsm_resp_assembly(tm_rp, 0, tm_msg->id, (u8 *)(&read_value),
				      sizeof(struct app2dev_reg_read_value));
		goto exit;
	} break;
	case RWNX_TM_CMD_APP2DEV_REG_WRITE:
		/* Send the command to the LMAC */
		ret = rwnx_send_dbg_mem_write_req(rwnx_hw, mem_addr, mem_addr);
		if (ret)
			goto err;

		break;
	default:
		bes_err(BES_DBG_TESTMODE, "Unknown testmode register command ID\n");
		ret = -EINVAL;
		goto err;
		break;
	}

err:
	bes_tsm_resp_assembly(tm_rp, ret, tm_msg->id, NULL, 0);

exit:
	return ret;
}

/*
 * This function handles the user application commands for Debug filter settings.
 */
int rwnx_testmode_dbg_filter(struct rwnx_hw *rwnx_hw, struct rwnx_sysfs_tm_headr *tm_msg, size_t count,
			     struct bes_tm_rp *tm_rp)
{
	u32 filter;
	int ret = 0;

	filter = *((u32 *)tm_msg->data);
	bes_dbg(BES_DBG_TESTMODE, "testmode debug filter, setting: 0x%x\n", filter);

	switch (tm_msg->id) {
	case RWNX_TM_CMD_APP2DEV_SET_DBGMODFILTER:
		/* Send the command to the LMAC */
		ret = rwnx_send_dbg_set_mod_filter_req(rwnx_hw, filter);
		break;
	case RWNX_TM_CMD_APP2DEV_SET_DBGSEVFILTER:
		/* Send the command to the LMAC */
		ret = rwnx_send_dbg_set_sev_filter_req(rwnx_hw, filter);
		break;
	default:
		bes_err(BES_DBG_TESTMODE, "Unknown testmode register command ID\n");
		ret = -EINVAL;
		break;
	}

	bes_tsm_resp_assembly(tm_rp, ret, tm_msg->id, NULL, 0);

	return ret;
}

/*
 * This function handles the user application commands for register access without using
 * the normal LMAC messaging way.
 * This time register access will be done through direct PCI BAR windows. This can be used
 * to access registers even when the :AMC FW is stuck.
 *
 */
int rwnx_testmode_reg_dbg(struct rwnx_hw *rwnx_hw, struct rwnx_sysfs_tm_headr *tm_msg, size_t count,
			  struct bes_tm_rp *tm_rp)
{
#if defined(CONFIG_RWNX_PCIE) || defined(CONFIG_BES_IBUS)
	struct rwnx_plat *rwnx_plat = rwnx_hw->plat;
	u32 mem_addr = ((u32 *)tm_msg->data)[0];
	volatile unsigned int reg_value = 0;
	unsigned int offset = mem_addr & 0x00FFFFFF;
#endif
	int ret = 0;

	switch (tm_msg->id) {
#if defined(CONFIG_RWNX_PCIE) || defined(CONFIG_BES_IBUS)
	case RWNX_TM_CMD_APP2DEV_REG_READ_DBG: {
		struct app2dev_reg_read_value read_value;
		/*** Send the command to the LMAC ***/
		reg_value = RWNX_REG_READ(rwnx_plat, RWNX_ADDR_SYSTEM, offset);
		read_value.reg_value = reg_value;
		bes_tsm_resp_assembly(tm_rp, 0, tm_msg->id, (u8 *)(&read_value),
				       sizeof(struct app2dev_reg_read_value));
	} break;
	case RWNX_TM_CMD_APP2DEV_REG_WRITE_DBG:
		reg_value = ((u32 *)tm_msg->data)[1];
		/* Send the command to the LMAC */
		RWNX_REG_WRITE(reg_value, rwnx_plat, RWNX_ADDR_SYSTEM, offset);
		bes_tsm_resp_assembly(tm_rp, 0, tm_msg->id, NULL, 0);
		break;
#endif
	default:
		bes_err(BES_DBG_TESTMODE, "Unknown testmode register command ID\n");
		ret = -EINVAL;
		bes_tsm_resp_assembly(tm_rp, ret, tm_msg->id, NULL, 0);
		break;
	}

	return ret;
}

static int bes_testmode_mcu_reg_dbg(struct rwnx_hw *rwnx_hw, struct rwnx_sysfs_tm_headr *tm_msg, size_t count,
				    struct bes_tm_rp *tm_rp)
{
	u32 mem_addr;
	int ret = 0;
	u32_l id = tm_msg->id;
	u32_l reg_dbg[3] = {0};

	mem_addr = ((u32 *)tm_msg->data)[0];

	if (id == RWNX_TM_CMD_MCU_REG_READ) {
		reg_dbg[0] = id;
		reg_dbg[1] = mem_addr;
		ret = bes_send_drv2mcu_msg(rwnx_hw, (u8 *)reg_dbg, sizeof(u32_l) * 2, MCU_REG_READ_REQ,
					    MCU_REG_READ_CFM, tm_rp, sizeof(*tm_rp));
	} else if (id == RWNX_TM_CMD_MCU_REG_WRITE) {
		reg_dbg[0] = id;
		reg_dbg[1] = mem_addr;
		/* reg_val */
		reg_dbg[2] = ((u32 *)tm_msg->data)[1];
		ret = bes_send_drv2mcu_msg(rwnx_hw, (u8 *)reg_dbg, sizeof(reg_dbg), MCU_REG_WRITE_REQ,
					   MCU_REG_WRITE_CFM, tm_rp, sizeof(*tm_rp));
	}

	if (ret) {
		bes_err(BES_DBG_TESTMODE, "%s fail,  error code: %d\n", __func__, ret);
		bes_tsm_resp_assembly(tm_rp, ret, tm_msg->id, NULL, 0);
	}

	return ret;
}

/**
 * vendor_rf_cmd, signaling cmd & rf nosignaling cmd
 * reaches rwnx
 *
 * @rwnx_hw: rwnx_hw object that represents the device
 * @tm_msg: userspace message
 * @count: msg len
 * @tm_rp: msg reply buffer
 *
 * Returns: 0 on success or non zero value on failure
 */
int bes_vendor_rf_cmd(struct rwnx_hw *rwnx_hw, struct rwnx_sysfs_tm_headr *tm_msg, size_t count,
		       struct bes_tm_rp *tm_rp)
{
	struct vendor_rf_cmd *vendor_rf_cmd = (struct vendor_rf_cmd *)tm_msg;
	int ret = 0;

	/* signaling mode does not support the vendor rf cmd. */
	if (bes_sysfs_is_signal_mode()) {
		ret = -EOPNOTSUPP;
		goto err;
	}

	if (vendor_rf_cmd->cmd_argc == 0 || vendor_rf_cmd->cmd_len == 0 ||
	    strlen(vendor_rf_cmd->cmd) + 1 != vendor_rf_cmd->cmd_len) {
		bes_err(BES_DBG_TESTMODE, "%s line: %d, vendor rf cmd parsing failed\n", __func__, __LINE__);
		ret = -EINVAL;
		goto err;
	}
	bes_dbg(BES_DBG_TESTMODE, "vendor rf cmd: %s, cmd_type:%d, cmd_len:%d\n", vendor_rf_cmd->cmd, vendor_rf_cmd->cmd_type,
		 vendor_rf_cmd->cmd_len);

	switch (vendor_rf_cmd->cmd_type) {
	case RWNX_TM_CMD_RF_POWER_CALIB_FINISH:
	case RWNX_TM_CMD_RF_GET_SAVE_FREQOFFSET_CMD:
	case RWNX_TM_CMD_RF_GET_SAVE_POWERLEVEL_CMD:
	case RWNX_TM_CMD_RF_NOSIGNALING_CMD:
	case RWNX_TM_CMD_RF_SAVE_FREQOFFSET_CMD:
	case RWNX_TM_CMD_RF_SAVE_POWERLEVEL_CMD:
		ret = bes_send_drv2mcu_msg(rwnx_hw, (u8 *)vendor_rf_cmd,
					   sizeof(struct vendor_rf_cmd) + vendor_rf_cmd->cmd_len,
					   MCU_VENDOR_RF_CMD_REQ, MCU_VENDOR_RF_CMD_CFM,
					   tm_rp, sizeof(*tm_rp));
		if (ret) {
			bes_err(BES_DBG_TESTMODE, "vendor rf cmd send error code:%d\n", ret);
			goto err;
		}

		/* status code convert */
		if (tm_rp->hdr.status < 0)
			tm_rp->hdr.status = -tm_rp->hdr.status;
		if (tm_rp->hdr.status >= MCU_MSG_ERR_NUM) {
			bes_err(BES_DBG_TESTMODE, "%s %d, invalid status code: %d\n", __func__, __LINE__, -tm_rp->hdr.status);
			ret = -EINVAL;
			goto err;
		}
		tm_rp->hdr.status = g_drvmcu_err[tm_rp->hdr.status];
		break;
	default:
		ret = -EOPNOTSUPP;
		goto err;
		break;
	}

err:
	if (ret)
		bes_tsm_resp_assembly(tm_rp, ret, tm_msg->id, NULL, 0);

	return ret;
}

/**
 * rwnx_testmode_cmd -called when tesmode command
 * reaches rwnx
 *
 * @rwnx_hw: ieee80211_hw object that represents the device
 * @buf: userspace message
 * @count: msg len
 * @tm_rp: msg reply buffer
 *
 * Returns: 0 on success or non zero value on failure
 */
ssize_t rwnx_testmode_cmd(struct rwnx_hw *rwnx_hw, const char *buf, size_t count, struct bes_tm_rp *tm_rp)
{
	int ret = 0;
	struct rwnx_sysfs_tm_headr *tm_msg;
	int fw_type = bes_sysfs_get_fw_type();

	if (!tm_rp)
		return -EFAULT;

	if (!buf) {
		bes_tsm_resp_assembly(tm_rp, -EFAULT, 0, NULL, 0);
		return -EFAULT;
	}

	tm_msg = (struct rwnx_sysfs_tm_headr *)buf;

	if (!rwnx_hw) {
		bes_tsm_resp_assembly(tm_rp, -EFAULT, tm_msg->id, NULL, 0);
		return -EFAULT;
	}

	if (fw_type == BES_FW_TYPE_BT) {
		bes_tsm_resp_assembly(tm_rp, -EPERM, tm_msg->id, NULL, 0);
		return -EPERM;
	}

	bes_dbg(BES_DBG_TESTMODE, "%s: msg id: %u\n", __func__, tm_msg->id);

	/* ret stause, using type enum rwnx_tm_rp_status */
	switch (tm_msg->id) {
	case RWNX_TM_CMD_RF_GET_SAVE_FREQOFFSET_CMD:
	case RWNX_TM_CMD_RF_GET_SAVE_POWERLEVEL_CMD:
	case RWNX_TM_CMD_RF_SAVE_FREQOFFSET_CMD:
	case RWNX_TM_CMD_RF_SAVE_POWERLEVEL_CMD:
	case RWNX_TM_CMD_RF_POWER_CALIB_FINISH:
	case RWNX_TM_CMD_RF_NOSIGNALING_CMD:
		ret = bes_vendor_rf_cmd(rwnx_hw, tm_msg, count - sizeof(u32), tm_rp);
		break;
	case RWNX_TM_CMD_APP2DEV_REG_READ:
	case RWNX_TM_CMD_APP2DEV_REG_WRITE:
		ret = rwnx_testmode_reg(rwnx_hw, tm_msg, count - sizeof(u32), tm_rp);
		break;
	case RWNX_TM_CMD_APP2DEV_SET_DBGMODFILTER:
	case RWNX_TM_CMD_APP2DEV_SET_DBGSEVFILTER:
		ret = rwnx_testmode_dbg_filter(rwnx_hw, tm_msg, count - sizeof(u32), tm_rp);
		break;
	case RWNX_TM_CMD_APP2DEV_REG_READ_DBG:
	case RWNX_TM_CMD_APP2DEV_REG_WRITE_DBG:
		ret = rwnx_testmode_reg_dbg(rwnx_hw, tm_msg, count - sizeof(u32), tm_rp);
		break;
	case RWNX_TM_CMD_MCU_REG_READ:
	case RWNX_TM_CMD_MCU_REG_WRITE:
		bes_testmode_mcu_reg_dbg(rwnx_hw, tm_msg, count - sizeof(u32), tm_rp);
		break;
	default:
		break;
	}

	return ret;
}
