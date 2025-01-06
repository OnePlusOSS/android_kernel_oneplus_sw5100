/**
 ****************************************************************************************
 *
 * @file rwnx_testmode.h
 *
 * @brief Test mode function declarations
 *
 * Copyright (C) RivieraWaves 2012-2021
 *
 ****************************************************************************************
 */

#ifndef RWNX_TESTMODE_H_
#define RWNX_TESTMODE_H_
#include "ipc_shared.h"

/* Commands from user space to kernel space(RWNX_TM_CMD_APP2DEV_XX) and
 * from and kernel space to user space(RWNX_TM_CMD_DEV2APP_XX).
 */
enum rwnx_tm_cmd_t {
	/* commands from user application to access register */
	RWNX_TM_CMD_APP2DEV_REG_READ = 1,
	RWNX_TM_CMD_APP2DEV_REG_WRITE,

	/* commands from user application to select the Debug levels */
	RWNX_TM_CMD_APP2DEV_SET_DBGMODFILTER,
	RWNX_TM_CMD_APP2DEV_SET_DBGSEVFILTER,

	/* commands to access registers without sending messages to LMAC layer,
	 * this must be used when LMAC FW is stuck. */
	RWNX_TM_CMD_APP2DEV_REG_READ_DBG,
	RWNX_TM_CMD_APP2DEV_REG_WRITE_DBG,

	/* vendor rf cmd id */
	RWNX_TM_CMD_RF_NOSIGNALING_CMD,
	RWNX_TM_CMD_RF_GET_SAVE_FREQOFFSET_CMD,
	RWNX_TM_CMD_RF_GET_SAVE_POWERLEVEL_CMD,
	RWNX_TM_CMD_RF_SAVE_FREQOFFSET_CMD,
	RWNX_TM_CMD_RF_SAVE_POWERLEVEL_CMD,
	RWNX_TM_CMD_RF_POWER_CALIB_FINISH,
	RWNX_TM_CMD_MCU_REG_READ,
	RWNX_TM_CMD_MCU_REG_WRITE,
	/* add new here */

	RWNX_TM_CMD_MAX,
};

enum rwnx_tm_attr_t {
	RWNX_TM_ATTR_NOT_APPLICABLE = 0,

	RWNX_TM_ATTR_COMMAND,

	/* When RWNX_TM_ATTR_COMMAND is RWNX_TM_CMD_APP2DEV_REG_XXX,
	 * The mandatory fields are:
	 * RWNX_TM_ATTR_REG_OFFSET for the offset of the target register;
	 * RWNX_TM_ATTR_REG_VALUE32 for value */
	RWNX_TM_ATTR_REG_OFFSET,
	RWNX_TM_ATTR_REG_VALUE32,

	/* When RWNX_TM_ATTR_COMMAND is RWNX_TM_CMD_APP2DEV_SET_DBGXXXFILTER,
	 * The mandatory field is RWNX_TM_ATTR_REG_FILTER. */
	RWNX_TM_ATTR_REG_FILTER,

	RWNX_TM_ATTR_MAX,
};

struct app2dev_reg_read_value {
	unsigned int reg_value;
};

struct rwnx_sysfs_tm_headr {
	u32 id;
	u8 data[0];
};

/*
 * include signaling and nosignaling mode
 */
struct vendor_rf_cmd {
	u32 cmd_type;
	u32 cmd_argc;
	u32 cmd_len;
	u8 cmd[0];
};

/*
 * @len: msg length, including hdr
 * @reserve: reserve field
 * @cmd_type: cmd type
 * @status: msg return status
 */
struct bes_tm_rp_hdr {
	uint16_t len;
	uint16_t reserve;
	uint32_t cmd_type;
	int32_t status;
};

#define RWNX_TM_REP_SIZE (IPC_E2A_MSG_PARAM_SIZE * 4)
#define RWNX_TM_SHOW_SIZE (RWNX_TM_REP_SIZE - sizeof(struct bes_tm_rp_hdr))
struct bes_tm_rp {
	struct bes_tm_rp_hdr hdr;
	u8 sysfs_show[RWNX_TM_SHOW_SIZE];
};

struct bes_tm_common {
	struct mutex tm_lock;
	struct bes_tm_rp tm_rp;
};

/***********************************************************************/
int rwnx_testmode_reg(struct rwnx_hw *rwnx_hw, struct rwnx_sysfs_tm_headr *tm_msg, size_t count,
		      struct bes_tm_rp *tm_rp);
int rwnx_testmode_dbg_filter(struct rwnx_hw *rwnx_hw, struct rwnx_sysfs_tm_headr *tm_msg, size_t count,
			     struct bes_tm_rp *tm_rp);
int rwnx_testmode_reg_dbg(struct rwnx_hw *rwnx_hw, struct rwnx_sysfs_tm_headr *tm_msg, size_t count,
			  struct bes_tm_rp *tm_rp);

ssize_t rwnx_testmode_cmd(struct rwnx_hw *rwnx_hw, const char *buf, size_t count, struct bes_tm_rp *tm_rp);
void bes_tsm_resp_assembly(struct bes_tm_rp *tm_rp, s32 status, u32 cmd_type, u8 *data, u16 msg_len);

#endif /* RWNX_TESTMODE_H_ */
