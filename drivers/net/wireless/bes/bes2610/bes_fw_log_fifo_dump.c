/*
 * @file bes_fw_log_fifo_dump.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "rwnx_defs.h"
#include "bes_log.h"
#include "rwnx_msg_tx.h"
#if (DUMP_FW_LOG_METHOD ==3)
/* The actual working function for reading logs. */
static void bes_fw_log_dump_work(struct work_struct *work)
{
	struct bes_fw_log_fifo_dump *fw_fifo_dump =
		container_of(work, struct bes_fw_log_fifo_dump, dump_work);
	struct rwnx_hw *rwnx_hw = container_of(fw_fifo_dump, struct rwnx_hw, fw_fifo_dump);
	u8 *print_buf = NULL;
	u16 copy_len = 0;

	/* When the cache is empty, exit directly. */
	if (!kfifo_initialized(&rwnx_hw->fw_fifo_dump.kfifo) ||
	    kfifo_is_empty(&fw_fifo_dump->kfifo)) {
		bes_err(BES_DBG_FW_LOG_FIFO_DUMP, "%s kfifo status invalid\n", __func__);
		return;
	}

	/* Indicates that the dump has started. */
	if (down_timeout(&fw_fifo_dump->fifo_sema, msecs_to_jiffies(LOG_DUMP_WAIT_TIME_MAX))) {
		/* If the timeout occurs, it may be due to platform deinit. */
		bes_info(BES_DBG_FW_LOG_FIFO_DUMP, "%s try dump fw_log timeout\n", __func__);
		return;
	}

	/* Copy the firmware logs to the print_buffer,
	 * and then copy them to the kernel log cache by printk.
	 * Apply for an additional byte of space to be used by the terminator '\0'. */
	if (!(print_buf = kmalloc(LOG_DUMP_PRINTK_SIZE + 1, GFP_KERNEL))) {
		bes_err(BES_DBG_FW_LOG_FIFO_DUMP, "%s kmalloc printk buffr fail\n", __func__);
		goto out;
	}

	/* Print log. */
	bes_info(BES_DBG_FW_LOG_FIFO_DUMP, "%s fw log dump start, log_len:%u\n",
		  __func__, kfifo_len(&fw_fifo_dump->kfifo));
	while (!kfifo_is_empty(&fw_fifo_dump->kfifo)) {
		copy_len = kfifo_out(&fw_fifo_dump->kfifo, print_buf, LOG_DUMP_PRINTK_SIZE);
		if (!copy_len)
			continue;

		/* Add string terminator to prevent duplicate printing. */
		print_buf[copy_len] = '\0';

		pr_info("%s", print_buf);
	}
	bes_info(BES_DBG_FW_LOG_FIFO_DUMP, "%s fw log dump finish\n", __func__);

out:
	up(&fw_fifo_dump->fifo_sema);
	if (print_buf)
		kfree(print_buf);
	return;
}

/* When an error occurs, read the firmware log from the buffer and print it. */
int bes_fw_log_fifo_dump_start(struct rwnx_hw *rwnx_hw, bool err_occur)
{
	if (!err_occur)
		return 0;

	/* queue_work to dump */
	queue_work(rwnx_hw->rwnx_wq, &rwnx_hw->fw_fifo_dump.dump_work);

	return 0;
}

/* Write firmware logs to buffer. */
int bes_fw_log_store(struct rwnx_hw *rwnx_hw, u8 *log_data, u16 log_len)
{
	struct bes_fw_log_fifo_dump *fw_fifo_dump = &rwnx_hw->fw_fifo_dump;
	u16 written_len = 0, write_len = 0;

	BUG_ON(!log_data || !log_len);

	/* When the dump has already started or the platform has been deinit,
	 * discard subsequent firmware logs. */
	if (down_trylock(&fw_fifo_dump->fifo_sema))
		return 0;

	bes_dbg(BES_DBG_FW_LOG_FIFO_DUMP, "[FW]:\n%s>>", (char *)log_data);

	/* Calculate the string length of the log. */
	write_len = strnlen(log_data, log_len);
	if (!write_len) {
		bes_err(BES_DBG_FW_LOG_FIFO_DUMP, "%s fw upload log length is 0\n", __func__);
		goto out;
	}

	/* When the buffer space is not enough, we need to delete the oldest records (one or more)
	 * until the free cache size is greater than the size of the new record. */
	while (kfifo_avail(&fw_fifo_dump->kfifo) < write_len) {
		bes_dbg(BES_DBG_FW_LOG_FIFO_DUMP, "%s current_space(%d) not enough.\n",
			 __func__, kfifo_avail(&fw_fifo_dump->kfifo));

		/* Delete oldest record. */
		kfifo_skip(&fw_fifo_dump->kfifo);
	}

	bes_dbg(BES_DBG_FW_LOG_FIFO_DUMP, "%s write_len:%d free_len:%d\n\n",
		 __func__, write_len, kfifo_avail(&fw_fifo_dump->kfifo));

	/* Copy firmware logs into the kfifo. */
	written_len = kfifo_in(&fw_fifo_dump->kfifo, log_data, write_len);
	if (written_len != write_len)
		bes_err(BES_DBG_FW_LOG_FIFO_DUMP, "%s kfifo_in err, written_len:%d expected_len:%d\n",
			 __func__, written_len, write_len);

out:
	up(&fw_fifo_dump->fifo_sema);
	return 0;
}

/**
 * bes_fw_fifo_dump_init - Initialize buffer
 * @buf_size: The buffer size must be a power of 2.
 * @log_level: The firmware type of the target log.(@ref:enum fw_log_type)
*/
int bes_fw_fifo_dump_init(struct rwnx_hw *rwnx_hw, u32 buf_size, u8 log_level)
{
	struct bes_fw_log_fifo_dump *fw_fifo_dump = &rwnx_hw->fw_fifo_dump;
	u8 *buf;
	int error = 0;

	/* The log module has been repeatedly initialized. */
	if (fw_fifo_dump->log_buffer || kfifo_initialized(&fw_fifo_dump->kfifo)) {
		bes_err(BES_DBG_FW_LOG_FIFO_DUMP, "%s kfifo initialized buf:%p init:%d\n",
			 __func__, fw_fifo_dump->log_buffer, kfifo_initialized(&fw_fifo_dump->kfifo));
		return -EPERM;
	}

	/* Initialize log buffer. */
	if (!(buf = vmalloc(buf_size))) {
		bes_err(BES_DBG_FW_LOG_FIFO_DUMP, "%s vmalloc fail\n", __func__);
		return -ENOMEM;
	}
	fw_fifo_dump->log_buffer = buf;

	/* Using KFIFO to manage log buffers. */
	if ((error = kfifo_init(&fw_fifo_dump->kfifo, buf, buf_size))) {
		bes_err(BES_DBG_FW_LOG_FIFO_DUMP, "%s kfifo_int fail:%d\n", __func__, error);
		goto err;
	}
	INIT_WORK(&fw_fifo_dump->dump_work, bes_fw_log_dump_work);

	/* Ensure mutually exclusive operation. */
	sema_init(&fw_fifo_dump->fifo_sema, 1);

	/* Notify firmware that logs can be uploaded. */
	if ((error = bes_send_dump_fw_log(rwnx_hw, log_level))) {
		bes_err(BES_DBG_FW_LOG_FIFO_DUMP, "%s send dump_fw_log fail:%d\n", __func__, __LINE__);
		goto err;
	}

	return 0;
err:
	vfree(buf);
	fw_fifo_dump->log_buffer = NULL;
	return error;
}

/**
 * bes_fw_fifo_dump_init - Deinitialization buffer
*/
int bes_fw_fifo_dump_deinit(struct rwnx_hw *rwnx_hw)
{
	struct bes_fw_log_fifo_dump *fw_fifo_dump = &rwnx_hw->fw_fifo_dump;
	int error = 0;

	/* If not initialized, exit directly */
	if (!fw_fifo_dump->log_buffer)
		return 0;

	/* Waiting for dump or entering FIFO to end. */
	if (down_timeout(&fw_fifo_dump->fifo_sema, msecs_to_jiffies(LOG_DUMP_WAIT_TIME_MAX))) {
		bes_err(BES_DBG_FW_LOG_FIFO_DUMP, "%s timeout\n", __func__);
	}

	/* Notify firmware not to upload logs. */
	if ((error = bes_send_dump_fw_log(rwnx_hw, 0))) {
		bes_err(BES_DBG_FW_LOG_FIFO_DUMP, "");
	}

	/* Free log buffer. */
	vfree(fw_fifo_dump->log_buffer);
	fw_fifo_dump->log_buffer = NULL;

	return 0;
}
#elif (DUMP_FW_LOG_METHOD == 2)
static void bes_fw_log_dump_work(struct work_struct *work)
{
	char print_buf[LOG_DUMP_PRINTK_SIZE];
	struct bes_fw_log_fifo_dump *fw_fifo_dump =
		container_of(work, struct bes_fw_log_fifo_dump, dump_work);
	struct rwnx_hw *rwnx_hw = container_of(fw_fifo_dump, struct rwnx_hw, fw_fifo_dump);
	if (down_timeout(&fw_fifo_dump->fifo_sema, msecs_to_jiffies(LOG_DUMP_WAIT_TIME_MAX))) {
		/* If the timeout occurs, it may be due to platform deinit. */
		bes_info(BES_DBG_FW_LOG_FIFO_DUMP, "%s try dump fw_log timeout\n", __func__);
		return;
	}
	while (!kfifo_is_empty(&fw_fifo_dump->kfifo))
		bes_fw_fifo_dump_out(rwnx_hw, print_buf, LOG_DUMP_PRINTK_SIZE);
	up(&fw_fifo_dump->fifo_sema);
	return;
}

int bes_fw_fifo_dump_init(struct rwnx_hw *rwnx_hw, u32 buf_size, u8 log_level)
{
	struct bes_fw_log_fifo_dump *fw_fifo_dump = &rwnx_hw->fw_fifo_dump;
	int ret = kfifo_alloc(&fw_fifo_dump->kfifo, buf_size, GFP_KERNEL);
	if (ret) {
			bes_err(BES_DBG_FW_LOG_FIFO_DUMP, "kfifo_alloc fail ret %d\n", ret);
			return ret;
		}
	INIT_WORK(&fw_fifo_dump->dump_work, bes_fw_log_dump_work);
	spin_lock_init(&fw_fifo_dump->dbg_lock);
	sema_init(&fw_fifo_dump->fifo_sema, 1);
	return 0;
}

int bes_fw_fifo_dump_deinit(struct rwnx_hw *rwnx_hw)
{
	struct bes_fw_log_fifo_dump *fw_fifo_dump = &rwnx_hw->fw_fifo_dump;
	if (down_timeout(&fw_fifo_dump->fifo_sema, msecs_to_jiffies(LOG_DUMP_WAIT_TIME_MAX))) {
		bes_err(BES_DBG_FW_LOG_FIFO_DUMP, "%s timeout\n", __func__);
	}
	// turn off mcu and lmac log to driver
	bes_set_lmac_log_mode(rwnx_hw, NX_PRINT_NONE);
	bes_send_dump_fw_log(rwnx_hw, 0);

	kfifo_free(&fw_fifo_dump->kfifo);
	return 0;
}

int bes_fw_fifo_dump_in(struct rwnx_hw *rwnx_hw, char *buf, u8 log_type)
{
	u8 ret;
	u8 result[2];
	struct bes_fw_log_fifo_dump *fw_fifo_dump = &rwnx_hw->fw_fifo_dump;
	u8 len = strlen(buf);
	if (!len)
		return -1;
	spin_lock_bh(&fw_fifo_dump->dbg_lock);
	if (len + sizeof(result) > kfifo_avail(&fw_fifo_dump->kfifo)) {
		bes_err(BES_DBG_FW_LOG_FIFO_DUMP, "log lost\n");
		spin_unlock_bh(&fw_fifo_dump->dbg_lock);
		return 0;
	}
	result[0] = len;
	result[1] = log_type;
	ret = kfifo_in(&fw_fifo_dump->kfifo, result, sizeof(result));
	WARN_ON(ret != sizeof(result));
	ret = kfifo_in(&fw_fifo_dump->kfifo, buf, len);
	WARN_ON(ret != len);
	spin_unlock_bh(&fw_fifo_dump->dbg_lock);
	return 0;
}

int bes_fw_fifo_dump_out(struct rwnx_hw *rwnx_hw, char *buf, u16_l max_len)
{
	u8 len ,ret, log_type;
	u8 result[2];
	struct bes_fw_log_fifo_dump *fw_fifo_dump = &rwnx_hw->fw_fifo_dump;
	spin_lock_bh(&fw_fifo_dump->dbg_lock);
	ret = kfifo_out(&fw_fifo_dump->kfifo, &result, sizeof(result));
	WARN_ON(ret != sizeof(result));
	len = result[0];
	ret = kfifo_out(&fw_fifo_dump->kfifo, buf, len);
	WARN_ON(ret != len);
	buf[(len > max_len ? max_len : len)] = '\0';
	spin_unlock_bh(&fw_fifo_dump->dbg_lock);
	log_type = result[1];
	if (log_type == FW_LOG_MCU)
		bes_info(BES_DBG_UTILS, "[FW]:\n%s>>", buf);
	else if (log_type == FW_LOG_LMAC)
		bes_info(BES_DBG_UTILS, "%s %s", FW_STR, buf);
	return 0;
}
#endif
