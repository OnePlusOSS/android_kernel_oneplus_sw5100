/*
 * @file bes_fw_log_fifo_dump.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __BES_FW_FIFO_DUMP_H__
#define __BES_FW_FIFO_DUMP_H__

#define LOG_DUMP_WAIT_TIME_MAX (50)
#if (DUMP_FW_LOG_METHOD == 3)
/* The buffer area used by printk after reading the logs from KFIFO.
 * Cannot be smaller than the size of one firmware log record. */
#define LOG_DUMP_PRINTK_SIZE (IPC_E2A_MSG_PARAM_SIZE * sizeof(u32))
/* Must be a power of 2 and must be less than or equal to (1U << 32)
 * This is due to the limitations of KFIFO. */
#define DEFAULT_FW_LOG_BUFFER_SIZE (1U << 20)
#else
#define FW_STR "fmac"
#define DEFAULT_FW_LOG_BUFFER_SIZE (1U << 16)
#define LOG_DUMP_PRINTK_SIZE 256
#endif
#ifdef CONFIG_FW_LOG_FIFO_DUMP
int bes_fw_fifo_dump_init(struct rwnx_hw *rwnx_hw, u32 buf_size, u8 log_level);
int bes_fw_fifo_dump_deinit(struct rwnx_hw *rwnx_hw);
#if (DUMP_FW_LOG_METHOD == 2)
int bes_fw_fifo_dump_in(struct rwnx_hw *rwnx_hw, char *buf, u8 log_type);
int bes_fw_fifo_dump_out(struct rwnx_hw *rwnx_hw, char *buf, u16 max_len);
static inline void bes_fw_log_fifo_dump_start(struct rwnx_hw *rwnx_hw, bool err_occur) { };
static inline void bes_fw_log_store(struct rwnx_hw *rwnx_hw, u8 *log_data, u16 log_len) { };
#else
int bes_fw_log_fifo_dump_start(struct rwnx_hw *rwnx_hw, bool err_occur);
int bes_fw_log_store(struct rwnx_hw *rwnx_hw, u8 *log_data, u16 log_len);
#endif
#else
static inline void bes_fw_log_fifo_dump_start(struct rwnx_hw *rwnx_hw, bool err_occur) { };
static inline void bes_fw_log_store(struct rwnx_hw *rwnx_hw, u8 *log_data, u16 log_len) { };
static inline int bes_fw_fifo_dump_init(struct rwnx_hw *rwnx_hw, u32 buf_size, u8 log_level) { return 0; }
static inline void bes_fw_fifo_dump_deinit(struct rwnx_hw *rwnx_hw) { };
#endif

#endif /* __BES_FW_FIFO_DUMP_H__ */
