/*
 * @file bes_error_dbg.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include "bes_error_dbg.h"
#include "bes_log.h"

#define BES_CRASH_BUFF_SIZE 128

static void bes_error_dbg_dump_regs(struct dbg_debug_info_tag *dump)
{
	uint32_t *regs = dump->error.exception_info.regs;
	int i, pos;
	uint32_t val;

	if (dump->error.exception_info.IPSR == 0) {
		bes_err(BES_DBG_ERROR_DBG, "PC =%08X, ThreadMode, EXC_RETURN=%08X\n",
			regs[15], dump->error.exception_info.EXC_RETURN);
	} else {
		bes_err(BES_DBG_ERROR_DBG, "PC =%08X, ExceptionNumber=D'%d, EXC_RETURN=%08X\n",
			regs[15], (int)dump->error.exception_info.IPSR - 16,
			dump->error.exception_info.EXC_RETURN);
	}
	for (i = 0; i < 3; i++) {
		pos = i * 4;
		bes_err(BES_DBG_ERROR_DBG, "R%-2d=%08X, R%-2d=%08X, R%-2d=%08X, R%-2d=%08X",
			pos, regs[pos], pos + 1, regs[pos + 1],
			pos + 2, regs[pos + 2], pos + 3, regs[pos + 3]);
	}
	bes_err(BES_DBG_ERROR_DBG, "R12=%08X, SP =%08X, LR =%08X\n", regs[12], regs[13], regs[14]);
	bes_err(BES_DBG_ERROR_DBG, "MSP   =%08X, PSP   =%08X",
		dump->error.exception_info.MSP, dump->error.exception_info.PSP);
	bes_err(BES_DBG_ERROR_DBG, "MSPLIM=%08X, PSPLIM=%08X\n",
		dump->error.exception_info.MSP_LIM, dump->error.exception_info.PSP_LIM);
	bes_err(BES_DBG_ERROR_DBG, "PRIMASK   =%02X, FAULTMASK   =%02X, BASEPRI   =%02X, CONTROL   =%02X\n",
		dump->error.exception_info.PRIMASK, dump->error.exception_info.FAULTMASK,
		dump->error.exception_info.BASEPRI, dump->error.exception_info.CONTROL);
	val = dump->error.exception_info.XPSR & 0x1FF;
	bes_err(BES_DBG_ERROR_DBG, "XPSR=%08X, APSR=%c%c%c%c%c, EPSR=%08X, IPSR=%03X%s\n",
		dump->error.exception_info.XPSR,
		(dump->error.exception_info.XPSR & (1 << 31)) ? 'N' : 'n',
		(dump->error.exception_info.XPSR & (1 << 30)) ? 'Z' : 'z',
		(dump->error.exception_info.XPSR & (1 << 29)) ? 'C' : 'c',
		(dump->error.exception_info.XPSR & (1 << 28)) ? 'V' : 'v',
		(dump->error.exception_info.XPSR & (1 << 27)) ? 'Q' : 'q',
		(unsigned)(dump->error.exception_info.XPSR & 0x0700FC00), (unsigned)val,
		(val == 0) ? " (NoException)":"");
	bes_err(BES_DBG_ERROR_DBG, "ICSR =%08X, AIRCR=%08X, SCR  =%08X, CCR  =%08X",
		dump->error.exception_info.ICSR, dump->error.exception_info.AIRCR,
		dump->error.exception_info.SCR, dump->error.exception_info.CCR);
	bes_err(BES_DBG_ERROR_DBG, "SHCSR=%08X, CFSR =%08X, HFSR =%08X, AFSR =%08X",
		dump->error.exception_info.SHCSR, dump->error.exception_info.CFSR,
		dump->error.exception_info.HFSR, dump->error.exception_info.AFSR);
	bes_err(BES_DBG_ERROR_DBG, "DFSR =%08X", dump->error.exception_info.DFSR);
	bes_err(BES_DBG_ERROR_DBG, "MMFAR=%08X, BFAR =%08X\n",
		dump->error.exception_info.MMFAR, dump->error.exception_info.BFAR);

	if (dump->error.exception_info.HFSR & (1 << 30)) {
		bes_err(BES_DBG_ERROR_DBG, "(Escalation HardFault)");
	}
	bes_err(BES_DBG_ERROR_DBG, "FaultInfo    :");
	if ((dump->error.exception_info.SHCSR & 0x13F) == 0) {
		bes_err(BES_DBG_ERROR_DBG, "               (None)");
	} else {
		if (dump->error.exception_info.SHCSR & (1 << 0)) {
			bes_err(BES_DBG_ERROR_DBG, "               (MemFault)");
		}
		if (dump->error.exception_info.SHCSR & (1 << 1)) {
			bes_err(BES_DBG_ERROR_DBG, "               (BusFault)");
		}
		if (dump->error.exception_info.SHCSR & (1 << 2)) {
			bes_err(BES_DBG_ERROR_DBG, "               (HardFault)");
		}
		if (dump->error.exception_info.SHCSR & (1 << 3)) {
			bes_err(BES_DBG_ERROR_DBG, "               (UsageFault)");
		}
		if (dump->error.exception_info.SHCSR & (1 << 4)) {
			bes_err(BES_DBG_ERROR_DBG, "               (SecureFault)");
		}
		if (dump->error.exception_info.SHCSR & (1 << 5)) {
			bes_err(BES_DBG_ERROR_DBG, "               (NMI)");
		}
			if (dump->error.exception_info.SHCSR & (1 << 8)) {
		bes_err(BES_DBG_ERROR_DBG, "               (Monitor)");
		}
	}

	bes_err(BES_DBG_ERROR_DBG, "FaultCause   :");
	if (dump->error.exception_info.CFSR == 0) {
		bes_err(BES_DBG_ERROR_DBG, "               (None)");
	} else {
		if (dump->error.exception_info.CFSR & (1 << 0)) {
			bes_err(BES_DBG_ERROR_DBG, "               (Instruction access violation)");
		}
		if (dump->error.exception_info.CFSR & (1 << 1)) {
			bes_err(BES_DBG_ERROR_DBG, "               (Data access violation)");
		}
		if (dump->error.exception_info.CFSR & (1 << 3)) {
			bes_err(BES_DBG_ERROR_DBG, "               (MemFault on unstacking for a return from exception)");
		}
		if (dump->error.exception_info.CFSR & (1 << 4)) {
			bes_err(BES_DBG_ERROR_DBG, "               (MemFault on stacking for exception entry)");
		}
		if (dump->error.exception_info.CFSR & (1 << 5)) {
			bes_err(BES_DBG_ERROR_DBG, "               (MemFault during floating-point lazy state preservation)");
		}
		if (dump->error.exception_info.CFSR & (1 << 7)) {
			bes_err(BES_DBG_ERROR_DBG, "               (MMFAR valid)");
		}
		if (dump->error.exception_info.CFSR & (1 << 8)) {
			bes_err(BES_DBG_ERROR_DBG, "               (Instruction bus error)");
		}
		if (dump->error.exception_info.CFSR & (1 << 9)) {
			bes_err(BES_DBG_ERROR_DBG, "               (Precise data bus error)");
		}
		if (dump->error.exception_info.CFSR & (1 << 10)) {
			bes_err(BES_DBG_ERROR_DBG, "               (Imprecise data bus error)");
		}
		if (dump->error.exception_info.CFSR & (1 << 11)) {
			bes_err(BES_DBG_ERROR_DBG, "               (BusFault on unstacking for a return from exception)");
		}
		if (dump->error.exception_info.CFSR & (1 << 12)) {
			bes_err(BES_DBG_ERROR_DBG, "               (BusFault on stacking for exception entry)");
		}
		if (dump->error.exception_info.CFSR & (1 << 13)) {
			bes_err(BES_DBG_ERROR_DBG, "               (BusFault during floating-point lazy state preservation)");
		}
		if (dump->error.exception_info.CFSR & (1 << 15)) {
			bes_err(BES_DBG_ERROR_DBG, "               (BFAR valid)");
		}
		if (dump->error.exception_info.CFSR & (1 << 16)) {
			bes_err(BES_DBG_ERROR_DBG, "               (Undefined instruction UsageFault)");
		}
		if (dump->error.exception_info.CFSR & (1 << 17)) {
			bes_err(BES_DBG_ERROR_DBG, "               (Invalid state UsageFault)");
		}
		if (dump->error.exception_info.CFSR & (1 << 18)) {
			bes_err(BES_DBG_ERROR_DBG, "               (Invalid PC load by EXC_RETURN UsageFault)");
		}
		if (dump->error.exception_info.CFSR & (1 << 19)) {
			bes_err(BES_DBG_ERROR_DBG, "               (No coprocessor UsageFault)");
		}
		if (dump->error.exception_info.CFSR & (1 << 20)) {
			bes_err(BES_DBG_ERROR_DBG, "               (Stack overflow UsageFault)");
		}
		if (dump->error.exception_info.CFSR & (1 << 24)) {
			bes_err(BES_DBG_ERROR_DBG, "               (Unaligned access UsageFault)");
		}
		if (dump->error.exception_info.CFSR & (1 << 25)) {
			bes_err(BES_DBG_ERROR_DBG, "               (Divide by zero UsageFault)");
		}
	}

	bes_err(BES_DBG_ERROR_DBG, "DebugEvent   :");
	if (dump->error.exception_info.DFSR == 0) {
		bes_err(BES_DBG_ERROR_DBG, "               (None)");
	} else {
		if (dump->error.exception_info.DFSR & (1 << 0)) {
			bes_err(BES_DBG_ERROR_DBG, "               (Halted)");
		}
		if (dump->error.exception_info.DFSR & (1 << 2)) {
			bes_err(BES_DBG_ERROR_DBG, "               (Data Watchpoint Match)");
		}
		if (dump->error.exception_info.dwt_fun_match) {
			for (i = 0; i < 0xf; i++) {
				if (dump->error.exception_info.dwt_fun_match & (1 << i))
					bes_err(BES_DBG_ERROR_DBG, "               (Function %d matched)", i);
			}
		}
	}
}

static void bes_error_dbg_dump_stack(struct dbg_debug_info_tag *dump)
{
	int i, pos, len = 0;
	uint32_t *regs = dump->error.exception_info.regs;
	uint32_t *stack;
	uint8_t stack_cnt;
	char crash_buf[BES_CRASH_BUFF_SIZE];

	bes_err(BES_DBG_ERROR_DBG, "Stack:\n");
	stack = dump->error.exception_info.stack;
	stack_cnt = dump->error.exception_info.stack_cnt;
	for (i = 0; i < (STACK_DUMP_CNT_PREV + STACK_DUMP_CNT) && i < stack_cnt; i++) {
		pos = (i % STACK_DUMP_CNT_PER_LEN);
		if (pos == 0) {
			len = snprintf(crash_buf, BES_CRASH_BUFF_SIZE, "%c %08X: %08X",
				(i == STACK_DUMP_CNT_PREV) ? '*' : ' ',
				(uint32_t)(regs[13] + (i / STACK_DUMP_CNT_PER_LEN) * (4 * sizeof(32))),
				(unsigned)stack[i]);
		} else {
			len += snprintf(crash_buf + len, BES_CRASH_BUFF_SIZE - len, " %08X", (unsigned)stack[i]);
			if (pos == (STACK_DUMP_CNT_PER_LEN - 1)) {
				bes_err(BES_DBG_ERROR_DBG, "%s\n", crash_buf);
			}
		}
	}
	if (pos != (STACK_DUMP_CNT_PER_LEN - 1))
		bes_err(BES_DBG_ERROR_DBG, "%s\n", crash_buf);
}

static void bes_error_dbg_dump_backtrace(uint32_t *backtrace, uint8_t backtrace_cnt)
{
	int i;

	bes_err(BES_DBG_ERROR_DBG, "Possible Backtrace:\n");
	for (i = 0; i < backtrace_cnt; i++)
		bes_err(BES_DBG_ERROR_DBG, "%08X\n", backtrace[i]);
}

static void bes_error_dbg_dump_mem_leak(struct dbg_debug_info_tag *dump)
{
	int i = 0;
	union dbg_error_info_tag *msg = &dump->error;

	bes_err(BES_DBG_ERROR_DBG, "\nLMAC ERROR: MEM LEAK OCCURS OR MEM RECORD RUN OUT\n");
	bes_err(BES_DBG_ERROR_DBG, "Total Malloc Size=%u, Alloc Mem Num =%u, Left record num =%u\n",
		msg->mem_leak_info.total_malloc_size, msg->mem_leak_info.used_record_num,
		msg->mem_leak_info.free_record_num);
	bes_err(BES_DBG_ERROR_DBG, "LAST MALLOC REQ INFO: Req Mem size=%u Time =%u, Caller=%08x, Msg_id=%x\n",
		msg->mem_leak_info.last_alloc_req.size, msg->mem_leak_info.last_alloc_req.time,
		msg->mem_leak_info.last_alloc_req.caller, msg->mem_leak_info.last_alloc_req.msg_id);

	if (msg->mem_leak_info.used_record_num > BES_LMAC_MAX_MEM_RECORD_NUM) {
		bes_err(BES_DBG_ERROR_DBG, "WARN: used_record_num is invalid\n");
		msg->mem_leak_info.used_record_num = BES_LMAC_MAX_MEM_RECORD_NUM;
	}
	for (i = 0; i < msg->mem_leak_info.used_record_num; ++i) {
		bes_err(BES_DBG_ERROR_DBG, "Record %d: size=%u Time =%u, Caller=%08x, Msg_id =%x\n", i,
			msg->mem_leak_info.alloc_record[i].size, msg->mem_leak_info.alloc_record[i].time,
			msg->mem_leak_info.alloc_record[i].caller, msg->mem_leak_info.alloc_record[i].msg_id);
	}
}

void bes_error_dbg_dump(struct dbg_debug_info_tag *dump)
{
	if (!dump)
		return;

	if (dump->fw_err_type == FW_ERR_DUMP_TYPE_NORMAL) {
		bes_info(BES_DBG_ERROR_DBG, "%s %d No exception/assert in lmac\n", __func__, __LINE__);
	} else if (dump->fw_err_type == FW_ERR_DUMP_TYPE_EXCEPTION) {
		// dump fault info
		bes_error_dbg_dump_regs(dump);

		// dump stack
		bes_error_dbg_dump_stack(dump);

		// dump backtrace
		bes_error_dbg_dump_backtrace(dump->error.exception_info.backtrace,
					      dump->error.exception_info.backtrace_cnt);
	} else if (dump->fw_err_type == FW_ERR_DUMP_TYPE_ASSERT) {
		// dump message
		bes_err(BES_DBG_ERROR_DBG, "%s\n", (char *)dump->error.assert_info.message);

		// dump backtrace
		bes_error_dbg_dump_backtrace(dump->error.assert_info.backtrace,
					     dump->error.assert_info.backtrace_cnt);
	} else if (dump->fw_err_type == FW_ERR_DUMP_TYPE_MEM_LEAK) {
		// dump memory allocation record
		bes_error_dbg_dump_mem_leak(dump);
	} else {
		bes_err(BES_DBG_ERROR_DBG, "%s %d Error: fw_err_type is %d and is invalid",
			__func__, __LINE__, dump->fw_err_type);
	}
}