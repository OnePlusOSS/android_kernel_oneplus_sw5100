/*
 * @file bes_dspi.c
 *
 * Copyright (c) 2024, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/crc32.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

#include "bes_sysfs.h"
#include "rwnx_platform.h"
#include "bes_pwr.h"
#include "bes_log.h"
#include "rwnx_msg_tx.h"
#include "bes_psa.h"
#include "bes_dspi.h"

#include "bes_tx_loop.h"
#include "bes_wowlan.h"
#include "bes_linkinfo.h"

static int bes_dspi_clock = 0;
module_param_named(clock, bes_dspi_clock, int, 0644);

static const struct of_device_id bes_spi_dev_ids[] = {
	{.compatible = "bes2610,slave_spi"},
	{/* end: all zeroes */},
};

extern struct rwnx_plat g_rwnx_plat;

void bes_io_bus_status_dump(void)
{
}

#if defined(CONFIG_BES_POWER_CTRL_MSG)

static int bes_dspi_iowrite(void *priv, u32 *io_dmabuf, u32 val)
{
	bes_err(BES_DBG_SPI, "%s to be continued\n", __func__);
	return 0;
}

static void bes_dspi_unregister_netdev(void *priv)
{
	bes_err(BES_DBG_SPI, "%s to be continued\n", __func__);
}

static void bes_dspi_power_ctrl_exit_handler(void *priv)
{
	bes_err(BES_DBG_SPI, "%s to be continued\n", __func__);
}

static const struct bes_power_ctrl_msg_ops ibus_power_ctrl_msg_ops = {
	.iowrite = bes_dspi_iowrite,
	.unregister_netdev = bes_dspi_unregister_netdev,
	.exit = bes_dspi_power_ctrl_exit_handler,
};

static int bes_dspi_plat_power(struct rwnx_plat *rwnx_plat, bool on)
{
	bes_info(BES_DBG_IBUS, "%s on=%d\n", __func__, on);
	bes_power_ctrl_system_enabled(on, false);
	return 0;
}

int bes_power_ctrl_dspi_init(struct rwnx_plat *rwnx_plat)
{
	rwnx_plat->power_priv = bes_power_ctrl_msg_register(&ibus_power_ctrl_msg_ops, rwnx_plat,
										sizeof(struct bes_dspi_power_ctrl), bes_dspi_plat_power);
	if (!rwnx_plat->power_priv)
		return -ENOMEM;

	return 0;
}

void bes_power_ctrl_dspi_deinit(struct rwnx_plat *rwnx_plat)
{
	bes_power_ctrl_msg_unregister(rwnx_plat->power_priv);
}

#endif

static void bes_dspi_lock(struct bes_spi *bes_spi)
{
	unsigned long flags;

	DECLARE_WAITQUEUE(wait, current);

	might_sleep();

	add_wait_queue(&bes_spi->wq, &wait);
	spin_lock_irqsave(&bes_spi->lock, flags);
	while (1) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		if (!bes_spi->claimed)
			break;
		spin_unlock_irqrestore(&bes_spi->lock, flags);
		schedule();
		spin_lock_irqsave(&bes_spi->lock, flags);
	}
	set_current_state(TASK_RUNNING);
	bes_spi->claimed = 1;
	spin_unlock_irqrestore(&bes_spi->lock, flags);
	remove_wait_queue(&bes_spi->wq, &wait);

	return;
}

static void bes_dspi_unlock(struct bes_spi *bes_spi)
{
	unsigned long flags;

	spin_lock_irqsave(&bes_spi->lock, flags);
	bes_spi->claimed = 0;
	spin_unlock_irqrestore(&bes_spi->lock, flags);
	wake_up(&bes_spi->wq);

	return;
}

static int bes_dspi_reg_read(struct spi_device *spidev, u32 reg,
					void *dst, int count)
{
	int ret = 0;
	u32 reg_addr, val;
	struct spi_message m;
	struct spi_transfer t_cmd;
	struct bes_spi *bes_spi = spi_get_drvdata(spidev);
	BUG_ON(!bes_spi);
	BUG_ON(spidev->bits_per_word != 8);
	WARN_ON(!bes_spi->claimed);

	if (!dst || count != 4) {
		bes_err(BES_DBG_SPI, "%s,%d %p:%d\n", __func__, __LINE__, dst, count);
		return -EINVAL;
	}

	reg_addr = BES_DSPI_RD_ADDR(reg);
#if defined(__LITTLE_ENDIAN)
	reg_addr = swab32(reg_addr);
#endif
	bes_dbg(BES_DBG_SPI, "REG_READ addr 0x%08x->0x%08x\n", reg, reg_addr);

	memcpy(bes_spi->xfer_reg.tx_buf, &reg_addr, 4);
	memset(&t_cmd, 0, sizeof(t_cmd));
	t_cmd.tx_buf = bes_spi->xfer_reg.tx_buf;
	t_cmd.rx_buf = bes_spi->xfer_reg.rx_buf;
	t_cmd.len = BES_HOST_SPI_REG_RD_BUFSIZE;
	t_cmd.speed_hz = spidev->max_speed_hz;

	spi_message_init(&m);
	spi_message_add_tail(&t_cmd, &m);
	ret = spi_sync(spidev, &m);
	if (!ret){
		val = *(u32 *)(t_cmd.rx_buf + 8);
		val = swab32(val);
		memcpy(dst, &val, 4);
		bes_dbg(BES_DBG_SPI, "REG_READ val:%x\n", val);
	} else {
		bes_err(BES_DBG_SPI, "%s,%d err=%d", __func__, __LINE__, ret);
	}

	return ret;
}

static int bes_dspi_reg_write(struct spi_device *spidev, u32 reg,
					const void *src, int count)
{
	int ret = 0;
	u32 reg_addr, val;
	struct spi_message m;
	struct spi_transfer t_cmd;
	struct bes_spi *bes_spi = spi_get_drvdata(spidev);
	BUG_ON(!bes_spi);
	BUG_ON(spidev->bits_per_word != 8);
	WARN_ON(!bes_spi->claimed);

	if (!src || count != 4) {
		bes_err(BES_DBG_SPI, "%s,%d %p:%d\n", __func__, __LINE__, src, count);
		return -EINVAL;
	}

	reg_addr = BES_DSPI_WR_ADDR(reg);
	memcpy(&val, src, 4);

#if defined(__LITTLE_ENDIAN)
	reg_addr = swab32(reg_addr);
	val = swab32(val);
#endif
	bes_dbg(BES_DBG_SPI, "REG_WR %08x->%08x 0x%08x->%08x\n", reg, reg_addr, *(u32 *)src, val);

	memcpy(bes_spi->xfer_reg.tx_buf, &reg_addr, 4);
	memcpy(bes_spi->xfer_reg.tx_buf + 4, &val, 4);
	memset(&t_cmd, 0, sizeof(t_cmd));
	t_cmd.tx_buf = bes_spi->xfer_reg.tx_buf;
	t_cmd.rx_buf = bes_spi->xfer_reg.rx_buf;
	t_cmd.len = BES_HOST_SPI_REG_WR_BUFSIZE;
	t_cmd.speed_hz = spidev->max_speed_hz;

	spi_message_init(&m);
	spi_message_add_tail(&t_cmd, &m);
	ret = spi_sync(spidev, &m);
	if (unlikely(ret)) {
		bes_err(BES_DBG_SPI, "%s,%d err=%d", __func__, __LINE__, ret);
	}

	return ret;
}

static u32 bes_crc32(u32 crc32, const u8 *data, u32 size) {
	u32 crc_cal = crc32;
	crc_cal ^= 0xffffffff;
	crc_cal = crc32_le(crc_cal, data, size);
	crc_cal ^= 0xffffffff;
	return crc_cal;
}

static int bes_dspi_direct_read(struct spi_device *spi, u32 addr, u8 *dst, u32 size, u32 irq)
{
	int ret, i;
	u32 reg_addr, done_reg, done_val;
	u32 *dst_u32, *src_u32;
	u32 crc32_cal, crc32_res;
	struct spi_transfer t_cmd;
	struct spi_message m;
	struct bes_spi *bes_spi = spi_get_drvdata(spi);
	BUG_ON(!bes_spi);
	BUG_ON(spi->bits_per_word != 8);
	WARN_ON(!bes_spi->claimed);

	bes_dbg(BES_DBG_SPI, "DIRECT_READ count=%d,addr=%x\n", size, addr);

	if (!addr || !dst || (size & 0x3) ||
			(size > bes_spi->rd_xfer_bufsize)) {
		bes_err(BES_DBG_SPI, "%s,%d %x:%p:%d", __func__, __LINE__, addr, dst, size);
		WARN_ON(1);
		return -EINVAL;
	}

	reg_addr = BES_DSPI_RD_ADDR(addr);
	if (irq) {
		done_reg = BES_DSPI_WR_ADDR(BES_DSPI_RD_INT_REG);
		done_val = irq;
	}

#if defined(__LITTLE_ENDIAN)
	reg_addr = swab32(reg_addr);
	if (irq) {
		done_reg = swab32(done_reg);
		done_val = swab32(done_val);
	}
#endif

	memcpy(bes_spi->xfer_data.tx_buf, &reg_addr, 4);
	memset(&t_cmd, 0, sizeof(t_cmd));
	t_cmd.tx_buf = bes_spi->xfer_data.tx_buf;
	t_cmd.rx_buf = bes_spi->xfer_data.rx_buf;
	t_cmd.len = BES_HOST_SPI_DATA_RD_BUFSIZE + size;
#ifdef BES_DSPI_TX_EXTRA_TAIL
	t_cmd.len += BES_DSPI_TX_EXTRA_TAIL_LEN;
#endif
	t_cmd.speed_hz = spi->max_speed_hz;

	spi_message_init(&m);
	spi_message_add_tail(&t_cmd, &m);
	ret = spi_sync(spi, &m);
	if (unlikely(ret)) {
		bes_err(BES_DBG_SPI, "%s,%d err=%d\n", __func__, __LINE__, ret);
		goto exit;
	}

#if defined(__LITTLE_ENDIAN)
	dst_u32 = (u32 *)dst;
	src_u32 = (u32 *)(bes_spi->xfer_data.rx_buf + BES_HOST_SPI_DATA_RD_BUFSIZE);
	for(i = 0; i < (size >> 2); i++) {
		dst_u32[i] = swab32(src_u32[i]);
	}
#else
	memcpy(dst, bes_spi->rx_data_buf, size);
#endif

	if (irq) {
		crc32_res = *(u32 *)(&dst[size - 4]);
		crc32_cal = bes_crc32(0, dst, size - 4);
		if (crc32_res != crc32_cal) {
			bes_err(BES_DBG_SPI, "%s crc32 error 0x%x:0x%x\n", __func__, crc32_res, crc32_cal);
			ret = -84;
			goto exit;
		}
		memset(dst + size - 4, 0, 4);
		memcpy(bes_spi->xfer_reg.tx_buf, &done_reg, 4);
		memcpy(bes_spi->xfer_reg.tx_buf + 4, &done_val, 4);
		memset(&t_cmd, 0, sizeof(t_cmd));
		t_cmd.tx_buf = bes_spi->xfer_reg.tx_buf;
		t_cmd.rx_buf = bes_spi->xfer_reg.rx_buf;
		t_cmd.len = BES_HOST_SPI_REG_WR_BUFSIZE;
		t_cmd.speed_hz = spi->max_speed_hz;
		spi_message_init(&m);
		spi_message_add_tail(&t_cmd, &m);
		ret = spi_sync(spi, &m);
		if (unlikely(ret))
			bes_err(BES_DBG_SPI, "%s,%d ret=%d\n", __func__, __LINE__, ret);
#ifdef BES_DSPI_TX_EXTRA_TAIL
		bes_spi->extra_tail = swab32(*(u32 *)&bes_spi->xfer_data.tx_buf[BES_HOST_SPI_DATA_RD_BUFSIZE + size]);
		crc32_res = swab32(*(u32 *)&bes_spi->xfer_data.tx_buf[BES_HOST_SPI_DATA_RD_BUFSIZE + size + 4]);
		crc32_cal = bes_crc32(0, (const u8 *)&bes_spi->extra_tail, 4);
		if (unlikely(crc32_res != crc32_cal)) {
			bes_spi->extra_tail = 0;
		}
#endif
	}
	//print_hex_dump(KERN_INFO, "raw data:", DUMP_PREFIX_NONE, 16, 1, bes_spi->rx_data_buf, 16, false);
exit:
	return ret;
}

static int bes_dspi_direct_write(struct spi_device *spidev, u32 addr, const u8 *src, u32 size, u32 irq)
{
	int ret, i;
	u32 reg_addr;
	u32 done_reg, done_val;
	u32 pos;
	u32 crc32, tail;
	u8 *src_u8;
	u32 *src_u32, *buf_u32;
	struct spi_transfer t_cmd, t_cmd_irq;
	struct spi_message m;
	struct bes_spi *bes_spi = spi_get_drvdata(spidev);
	BUG_ON(!bes_spi);
	BUG_ON(spidev->bits_per_word != 8);
	WARN_ON(!bes_spi->claimed);

	if (!addr || !src || (ALIGN(size, BES_DSPI_TRANSFER_ALIGN) > bes_spi->wr_xfer_bufsize)) {
		bes_err(BES_DBG_SPI, "%s,%d err=0x%x:%p:%d\n", __func__, __LINE__, addr, src, size);
		WARN_ON(1);
		return -EINVAL;
	}

	if (irq) {
		done_reg = BES_DSPI_WR_ADDR(BES_DSPI_WR_INT_REG);
		done_val = irq;
		crc32 = bes_crc32(0, src, size);
		tail = 0xCAFEFADE;
	}
	reg_addr = BES_DSPI_WR_ADDR(addr);

#if defined(__LITTLE_ENDIAN)
	reg_addr = swab32(reg_addr);
	if (irq) {
		crc32 = swab32(crc32);
		done_reg = swab32(done_reg);
		done_val = swab32(done_val);
		tail = swab32(tail);
	}
	pos = BES_HOST_SPI_DATA_WR_BUFSIZE >> 2;
	src_u32 = (u32 *)src;
	buf_u32 = (u32 *)(bes_spi->xfer_data.tx_buf + BES_HOST_SPI_DATA_WR_BUFSIZE);
	for (i = 0; i < (size >> 2); i++) {
		buf_u32[i] = swab32(src_u32[i]);
	}
	i <<= 2;
	pos <<= 2;
	pos += i;
	if (size & 0x3) {
		src_u8 = (u8 *)src;
		if ((size % 4) == 1) {
			bes_spi->xfer_data.tx_buf[pos] = 0;
			bes_spi->xfer_data.tx_buf[pos + 1] = 0;
			bes_spi->xfer_data.tx_buf[pos + 2] = 0;
			bes_spi->xfer_data.tx_buf[pos + 3] = src_u8[i];
		} else if ((size % 4) == 2) {
			bes_spi->xfer_data.tx_buf[pos] = 0;
			bes_spi->xfer_data.tx_buf[pos + 1] = 0;
			bes_spi->xfer_data.tx_buf[pos + 2] = src_u8[i + 1];
			bes_spi->xfer_data.tx_buf[pos + 3] = src_u8[i];
		} else {
			bes_spi->xfer_data.tx_buf[pos] = 0;
			bes_spi->xfer_data.tx_buf[pos + 1] = src_u8[i + 2];
			bes_spi->xfer_data.tx_buf[pos + 2] = src_u8[i + 1];
			bes_spi->xfer_data.tx_buf[pos + 3] = src_u8[i];
		}
		pos += 4;
	}
#else
	if (data_buf != (bes_spi->xfer_data.tx_buf + BES_HOST_SPI_DATA_WR_BUFSIZE))
		memcpy(bes_spi->xfer_data.tx_buf + BES_HOST_SPI_DATA_WR_BUFSIZE, data_buf, size);
	pos = BES_HOST_SPI_DATA_WR_BUFSIZE + ALIGN(size, BES_DSPI_TRANSFER_ALIGN);
#endif
	if (irq) {
		*(u32 *)&bes_spi->xfer_data.tx_buf[pos] = crc32;
		pos += 4;
		*(u32 *)&bes_spi->xfer_data.tx_buf[pos] = tail;
		pos += 4;
	}

	bes_dbg(BES_DBG_SPI, "DIRECT_WR %d:%x(%x) len=%d\n", size, addr, reg_addr, pos);

	memcpy(bes_spi->xfer_data.tx_buf, &reg_addr, 4);
	memset(&t_cmd, 0, sizeof(t_cmd));
	t_cmd.tx_buf = bes_spi->xfer_data.tx_buf;
	t_cmd.rx_buf = bes_spi->xfer_data.rx_buf;
	t_cmd.len = pos;
	t_cmd.speed_hz = spidev->max_speed_hz;

	//print_hex_dump(KERN_INFO, "DIRECT_WR cmd:", DUMP_PREFIX_NONE, 16, 1, t_cmd.tx_buf, t_cmd.len, false);
	//if (pos >= 1000) {
	//	print_hex_dump(KERN_INFO, "DIRECT_WR data:", DUMP_PREFIX_NONE, 32, 1, t_data.tx_buf, 32, false);
	//	print_hex_dump(KERN_INFO, "DIRECT_WR data:", DUMP_PREFIX_NONE, 32, 1, t_data.tx_buf + 72, 32, false);
	//	print_hex_dump(KERN_INFO, "DIRECT_WR data:", DUMP_PREFIX_NONE, 32, 1, t_data.tx_buf + pos - 32, 32, false);
	//}
	spi_message_init(&m);
	spi_message_add_tail(&t_cmd, &m);
	if (irq) {
		t_cmd.cs_change = 1;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 3, 0)
		t_cmd.cs_change_delay = 1;
		t_cmd.cs_change_delay_unit = SPI_DELAY_UNIT_USECS;
#endif

		*(u32 *)&bes_spi->xfer_reg.tx_buf[0] = done_reg;
		*(u32 *)&bes_spi->xfer_reg.tx_buf[4] = done_val;
		memset(&t_cmd_irq, 0, sizeof(t_cmd_irq));
		t_cmd_irq.tx_buf = bes_spi->xfer_reg.tx_buf;
		t_cmd_irq.rx_buf = bes_spi->xfer_reg.rx_buf;
		t_cmd_irq.len = BES_HOST_SPI_REG_WR_BUFSIZE;
		t_cmd_irq.speed_hz = spidev->max_speed_hz;
		spi_message_add_tail(&t_cmd_irq, &m);
	}
	ret = spi_sync(spidev, &m);
	if (unlikely(ret)) {
		bes_err(BES_DBG_SPI, "%s,%d err=%d\n", __func__, __LINE__, ret);
	}

	return ret;
}

static int bes_dspi_data_buf_pool_renew(struct bes_spi *bes_spi)
{
	int ret;
	u32 pos;
	u32 bufnum = 1 << bes_spi->data_buf_ctrl.bufnum_power;
	u32 diff = (u32)(bes_spi->data_pool_top - bes_spi->data_pool_bottom);
	struct spi_device *spi = bes_spi->spi;

	if (diff > 0 && diff < bufnum)
		return 0;

	if ((ret = bes_dspi_reg_read(spi, BES_DSPI_SHARE(bes_spi->share_base, BES_DSPI_RX_DATA_BUF_RENEW),
			&bes_spi->data_pool_top, 4))) {
		return ret;
	}

	diff = (u32)(bes_spi->data_pool_top - bes_spi->data_pool_bottom);
	BUG_ON(diff > bufnum);
	if (unlikely(!diff)) {
		WARN_ON(1);
		return -ENOMEM;
	}

	pos = bes_spi->data_pool_bottom & (bufnum - 1);
	bes_dbg(BES_DBG_SPI, "%s, %u %u %u:%u\n", __func__, diff, pos,
			bes_spi->data_pool_bottom, bes_spi->data_pool_top);
	if (bufnum - pos >= diff)
		ret = bes_dspi_direct_read(spi, BES_DSPI_SHARE(bes_spi->share_base, BES_DSPI_RX_DATA_BUF_POOL) + pos * 4,
				(u8 *)&bes_spi->data_buf_pool[pos], diff << 2, 0);
	else {
		ret = bes_dspi_direct_read(spi, BES_DSPI_SHARE(bes_spi->share_base, BES_DSPI_RX_DATA_BUF_POOL),
				(u8 *)bes_spi->data_buf_pool, bufnum << 2, 0);
	}

#if 0
	if (unlikely(!bes_spi->data_addr_min)) {
		bes_spi->data_addr_min = bes_spi->data_buf_pool[0];
		bes_spi->data_addr_max = bes_spi->data_buf_pool[bes_spi->data_pool_top - 1];
		bes_info(BES_DBG_SPI, "%s min=0x%x max=0x%x\n", __func__, bes_spi->data_addr_min,
				bes_spi->data_addr_max);
	}
#endif

	return ret;
}

static u32 bes_dspi_single_buf(struct bes_spi *bes_spi, enum conn_type type, void *data, u32 size)
{
	int ret, index;
	u32 addr = 0;
	u32 bufnum = 1 << bes_spi->data_buf_ctrl.bufnum_power;
	struct txdesc_host *txdesc_host;
	struct retry_entry_t *entry;

	if (type == CONN_DATA) {
		ret = bes_dspi_data_buf_pool_renew(bes_spi);
		if (WARN_ON(ret)) {
			goto out;
		}
		addr = bes_spi->data_buf_pool[bes_spi->data_pool_bottom & (bufnum - 1)];
#if 0 // add sanity check
		if (WARN_ON(addr > bes_spi->data_addr_max || addr < bes_spi->data_addr_min)) {
			bes_err(BES_DBG_SPI, "%s, addr err:%x,%x,%x\n", __func__, addr, bes_spi->data_addr_max, bes_spi->data_addr_min);
			addr = 0;
			goto out;
		}
#endif
		bes_dbg(BES_DBG_SPI, "%s %u:%u 0x%x\n", __func__,
				bes_spi->data_pool_bottom, bes_spi->data_pool_top, addr);
		txdesc_host = (struct txdesc_host *)(data + sizeof(struct ipc_header));
		index = bes_spi->data_pool_bottom & (bufnum - 1);
		entry = (struct retry_entry_t *)(((u8 *)bes_spi->last_data) + index * bes_spi->retry_entry_size);
		entry->hostid = addr;
		entry->origin_hostid_num = 1;
		entry->origin_hostids[0] = txdesc_host->api.host.hostid;
		bes_spi->data_pool_bottom ++;
	} else if (type == CONN_MSG) {
		ret = bes_dspi_reg_read(bes_spi->spi,
				BES_DSPI_SHARE(bes_spi->share_base, BES_DSPI_RX_MSG_BUF_REG), &addr, 4);
		if (unlikely(ret)) {
			bes_err(BES_DBG_SPI, "%s get slave spi rx msg addr failed(%d)\n", __func__, ret);
			addr = 0;
		}
		bes_spi->last_msg = data;
		bes_spi->last_msg_size = size;
	}

out:
	return addr;
}

//#define DATA_CRC_ERR_SELF_CFM
static u32 bes_dspi_aggr_buf(struct bes_spi *bes_spi)
{
	int ret;
	u32 test_val, test_addr;
	int index = bes_spi->data_pool_bottom & ((1 << bes_spi->data_buf_ctrl.bufnum_power) - 1);
	struct retry_entry_t *entry = (struct retry_entry_t *)(((u8 *)bes_spi->last_data) + index * bes_spi->retry_entry_size);
#ifdef DATA_CRC_ERR_SELF_CFM
	int i;
	uint8_t tmp[sizeof(struct ipc_header) + sizeof(struct tx_cfm_tag)];
	struct ipc_header *ipc = (struct ipc_header *)tmp;
	struct tx_cfm_tag *cfm = (struct tx_cfm_tag *)(ipc + 1);
	struct sk_buff *skb;
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
#endif

	test_addr = bes_spi->data_buf_pool[index];
	ret = bes_dspi_reg_read(bes_spi->spi, test_addr, &test_val, 4);
	if (!ret) {
		if (!test_val) {
			bes_dbg(BES_DBG_SPI, "%s %u 0x%x\n", __func__, bes_spi->data_pool_bottom, test_addr);
			entry->hostid = test_addr;
			entry->origin_hostid_num = bes_spi->aggr_copy_num;
			memcpy(entry->origin_hostids, bes_spi->aggr_copy_hostids, bes_spi->aggr_copy_num * sizeof(u32));
			bes_spi->data_pool_bottom ++;
			return test_addr;
		} else {
			bes_info(BES_DBG_SPI, "%s rx addr 0x%x busy(%u:0x%x)\n", __func__,
					test_addr, bes_spi->data_pool_bottom, test_val);
#ifdef DATA_CRC_ERR_SELE_CFM
			if ((test_val & 0xFF) == EVENT_E2A_DATA_CRC_ERR_IND) {
				WARN_ON(((test_val & 0xFF00) >> 8) != entry->origin_hostid_num);
				ipc->type = EVENT_E2A_TX_CFM;
				ipc->size = sizeof(tmp);
				cfm->status.value = 0;
				cfm->ampdu_size = 0;
				cfm->credits = 1;
				cfm->status.tx_done = 1;
				cfm->status.sw_required = 1;
				for (i = 0; i < entry->origin_hostid_num; i++) {
					skb = bes_dev_alloc_skb(sizeof(tmp));
					if (skb) {
						cfm->hostid = entry->origin_hostids[i];
						memcpy(skb->data, tmp, sizeof(tmp));
						skb_put(skb, sizeof(tmp));
						skb_queue_tail(&rwnx_plat->rx_pri_q, skb);
						bes_info(BES_DBG_SPI, "%s confirm hostid=0x%x(%d,%d)\n", __func__, cfm->hostid, i, index);
					}
				}
				return test_addr;
			}
#endif
			return 0;
		}
	} else {
		WARN_ON(1);
		return 0;
	}
}

static int bes_dspi_tx_aggr_try_add(struct bes_spi *bes_spi, const struct tx_packet *pkt)
{
	u8 *txbuf = NULL, *first;
	u32 size = 0, aligned_len = 0;
	int i, index;
	struct txdesc_host *txdesc_host;
	struct tx_buffer *tx_buf;

	if (bes_spi->aggr_copy_num >= (1 << bes_spi->data_buf_ctrl.item_power))
		return -1;

	for (i = 0; i < pkt->cnt; i++) {
		list_for_each_entry(tx_buf, &pkt->list[i], hdr) {
			size += tx_buf->size;
		}
	}

	aligned_len = ALIGN(size, BES_DSPI_TRANSFER_ALIGN);
	BUG_ON(aligned_len > bes_spi->data_buf_ctrl.bufsize);
	if (bes_spi->aggr_copy_len + aligned_len > bes_spi->wr_xfer_bufsize)
		return -2;

	txbuf = &bes_spi->aggr_copy_buf[bes_spi->aggr_copy_len];
	size = 0;
	first = (u8 *)list_first_entry(&pkt->list[0], struct tx_buffer, hdr);
	for (i = 0; i < pkt->cnt; i++) {
		list_for_each_entry(tx_buf, &pkt->list[i], hdr) {
			memcpy(txbuf + size, tx_buf->buf, tx_buf->size);
			size += tx_buf->size;
		}
		bes_psa_free_tx_buffer((struct list_head *)&pkt->list[i]);
	}
	txdesc_host = (struct txdesc_host *)(first + sizeof(struct ipc_header));
	index = bes_spi->data_pool_bottom & ((1 << bes_spi->data_buf_ctrl.bufnum_power) - 1);
	bes_spi->aggr_copy_hostids[bes_spi->aggr_copy_num] = txdesc_host->api.host.hostid;
	bes_spi->aggr_copy_num ++;
	bes_spi->aggr_copy_len += aligned_len;
	bes_dbg(BES_DBG_SPI, "%s %d %d %u 0x%x %u\n", __func__, pkt->tx_now,
			bes_spi->aggr_copy_num, aligned_len, txdesc_host->api.host.hostid, bes_spi->data_pool_bottom);
	return 0;
}

static int bes_dspi_tx_aggr_finish(struct bes_spi *bes_spi)
{
	int ret;
	u32 addr;
	unsigned long start;
	struct bes_spi_aggr_hdr *hdr = (struct bes_spi_aggr_hdr *)bes_spi->aggr_copy_buf;

	bes_dbg(BES_DBG_SPI, "%s, aggr %u %u", __func__, bes_spi->aggr_copy_num, bes_spi->aggr_copy_len);
	hdr->size = bes_spi->aggr_copy_len;
	hdr->type = EVENT_A2E_DATA_REQ;
	hdr->aggr_num = bes_spi->aggr_copy_num;

	start = jiffies;
	do {
		atomic_set(&bes_spi->aggr_preempt, 1);
		bes_dspi_lock(bes_spi);
		if (!atomic_read(&bes_spi->aggr_preempt)) {
			ret = 0;
			goto done;
		}
		addr = bes_dspi_aggr_buf(bes_spi);
		if (!addr) {
			if (time_after(start + HZ, jiffies)) {
				bes_dspi_unlock(bes_spi);
				msleep(5);
				continue;
			} else {
				ret = -EBUSY;
				WARN_ON(1);
				bes_dspi_unlock(bes_spi);
				break;
			}
		}
		ret = bes_dspi_direct_write(bes_spi->spi, addr, bes_spi->aggr_copy_buf, bes_spi->aggr_copy_len, BES_DSPI_WR_DATA_INT);
done:
		bes_spi->aggr_copy_len = 4;
		bes_spi->aggr_copy_num = 0;
		bes_dspi_unlock(bes_spi);
		break;
	} while(1);
	return ret;
}

static int bes_dspi_tx_aggr(struct bes_spi *bes_spi, const struct tx_packet *pkt)
{
	int ret = 0;

	ret = bes_dspi_tx_aggr_try_add(bes_spi, pkt);
	if (ret) {
		// tx_aggr add failed, finish aggr and try to add again
		ret = bes_dspi_tx_aggr_finish(bes_spi);
		if (ret)
			goto out;
		ret = bes_dspi_tx_aggr_try_add(bes_spi, pkt);
		WARN_ON(ret);
	}

	if (pkt->tx_now || (bes_spi->aggr_copy_num >= (1 << bes_spi->data_buf_ctrl.item_power) || bes_spi->aggr_copy_len >= bes_spi->wr_xfer_bufsize))
		ret = bes_dspi_tx_aggr_finish(bes_spi);
out:
	return ret;
}

static int bes_dspi_data_get(struct spi_device *spi, void **data_buf, u32 size)
{
	int ret, retry = BES_DSPI_TRANS_MAX_RETRY;
	struct bes_spi *bes_spi = spi_get_drvdata(spi);
	int index = bes_spi->rx_cnt & ((u32)BES_DSPI_TX_CTRL_TG_MASK >> BES_DSPI_TX_CTRL_TG_OFT);
	while (retry --) {
		ret = bes_dspi_direct_read(spi, bes_spi->rx_data_addr[index],
				bes_spi->xfer_data.rx_buf + BES_HOST_SPI_DATA_WR_BUFSIZE,
				size, BES_DSPI_RD_INT);
		if ((!ret) || (ret != -84))
			break;
		else {
			bes_info(BES_DBG_SPI, "%s rx_cnt=%u size=0x%x retry=%d\n", __func__, bes_spi->rx_cnt, size, retry);
			msleep(1);
		}
	}
	if (!ret) {
		bes_spi->rx_cnt ++;
		*data_buf = bes_spi->xfer_data.rx_buf + BES_HOST_SPI_DATA_WR_BUFSIZE;
	} else
		*data_buf = NULL;
	return ret;
}

static int __bes_dspi_send_one_packet(struct bes_spi *bes_spi, struct list_head *list, void *pre_alloc_buf)
{
	struct tx_buffer *tx_buf;
	u8 *buf;
	u16 len = 0, align_len;
	bool do_copy = true;
	int ret;
	u32 addr;

	WARN_ON(list_empty(list));

	if ( 1 /*list->next == list->prev*/) {
		/* Don't copy if there is only one tx_buffer */
		tx_buf = list_first_entry(list, struct tx_buffer, hdr);
		buf = (u8 *)tx_buf->buf;
		len = tx_buf->size;
		do_copy = false;
	} else {
		if (pre_alloc_buf) {
			buf = (u8 *)pre_alloc_buf;
		} else {
			buf = (u8 *)kmalloc(BES_TRANSFER_LEN_LIMIT, GFP_KERNEL);
			if (!buf) {
				bes_psa_free_tx_buffer(list);
				return -ENOMEM;
			}
		}

		list_for_each_entry(tx_buf, list, hdr) {
			WARN_ON(len + tx_buf->size > BES_TRANSFER_LEN_LIMIT);
			memcpy(buf + len, tx_buf->buf, tx_buf->size);
			len += tx_buf->size;
		}
	}
	bes_psa_free_tx_buffer(list);

	align_len = ALIGN(len, BES_DSPI_TRANSFER_ALIGN);
	if (align_len > bes_spi->data_buf_ctrl.bufsize)
		return -EINVAL;

	/* set the actual size */
	((struct ipc_header *)buf)->size = len;

	bes_dspi_lock(bes_spi);

	addr = bes_dspi_single_buf(bes_spi, CONN_DATA, buf, len);

	if (unlikely(!addr)) {
		ret = -ENOSPC;
		goto out;
	}
	ret = bes_dspi_direct_write(bes_spi->spi, addr, buf, len, BES_DSPI_WR_DATA_INT);

out:
	bes_dspi_unlock(bes_spi);

	if (buf && do_copy && !pre_alloc_buf)
		kfree(buf);

	return ret;
}

static int bes_dspi_data_push(const struct tx_packet *pkt)
{
	struct bes_spi *bes_spi = (struct bes_spi *)g_rwnx_plat.priv;

	BUG_ON(pkt->cnt != 1);
	BUG_ON(pkt->list[0].next != pkt->list[0].prev);

	if (bes_spi->data_buf_ctrl.aggr_num)
		return bes_dspi_tx_aggr(bes_spi, pkt);

	if (pkt->is_amsdu) {
#ifdef CONFIG_RWNX_AMSDUS_TX
		u8 i;

		for (i = 0; i < pkt->cnt; i++) {
			int ret = __bes_dspi_send_one_packet(bes_spi, (struct list_head *)&pkt->list[i], g_rwnx_plat.tx_amsdu);
			if (ret)
				return ret;
		}
		return 0;
#else
		BUG_ON(1);
		return -EINVAL;
#endif // CONFIG_RWNX_AMSDUS_TX
	} else {
		return __bes_dspi_send_one_packet(bes_spi, (struct list_head *)&pkt->list[0], NULL);
	}
}

static int bes_dspi_msg_push(void *data_buf, u32 size)
{
	int ret;
	u32 addr;
	struct spi_device *spi = g_rwnx_plat.spi_dev;
	struct bes_spi *bes_spi = (struct bes_spi *)g_rwnx_plat.priv;

	if (ALIGN(size, BES_DSPI_TRANSFER_ALIGN) > BES_DSPI_RX_MSG_BUFSIZE)
		return -EINVAL;

	*(u32 *)data_buf |= size << 16;

	bes_dspi_lock(bes_spi);
	addr = bes_dspi_single_buf(bes_spi, CONN_MSG, data_buf, size);
	ret = bes_dspi_direct_write(spi, addr, data_buf, size, BES_DSPI_WR_MSG_INT);
	bes_dspi_unlock(bes_spi);
	return ret;
}

#define TX_MSG_SIZE	(sizeof(struct rwnx_tx_msg))
static int bes_msg_fifo_repush(void *data_buf, u32 size)
{
	int bytes_copied = 0;
	struct rwnx_tx_msg msg;
	msg.size = size;
	msg.data_buf = data_buf;

	bytes_copied = kfifo_in_spinlocked(&g_rwnx_plat.fifo, &msg,
		TX_MSG_SIZE, &g_rwnx_plat.tx_lock);

	if (bytes_copied != TX_MSG_SIZE) {
		bes_err(BES_DBG_USB,"%s: failed to put msg to fifo, "
			"bytes_copied=%d\n", __FUNCTION__, bytes_copied);
		BUG_ON(1);
	}

	rwnx_run_flags_set_bit(&g_rwnx_plat, PLAT_RUN_FLAGS_TX_MSG);

	wake_up(&g_rwnx_plat.tx_wait_q);
	return 0;
}

static u32 bes_bus_align_size(struct spi_device *spidev, u32 size)
{
	return ALIGN(size, BES_DSPI_TRANSFER_ALIGN);
}

static int bes_bus_clr_int(struct spi_device *spi, u32 *lenPtr)
{
	u32 ctrl;// val;
	int ret, retry = BES_DSPI_BUSY_MAX_RETRY;
	struct bes_spi *bes_spi = spi_get_drvdata(spi);
	BUG_ON(!bes_spi);

	*lenPtr = 0;
	while (retry-- > 0) {
		ret = bes_dspi_reg_read(spi, BES_DSPI_SHARE(bes_spi->share_base, BES_DSPI_TX_CTRL_REG), &ctrl, 4);
		if (unlikely(ret))
			return ret;

		if (ctrl == 0)
			break;

		bes_dbg(BES_DBG_SPI, "%s %u:0x%x\n", __func__, bes_spi->rx_cnt, ctrl);

		if ((bes_spi->rx_cnt & ((u32)BES_DSPI_TX_CTRL_TG_MASK >> BES_DSPI_TX_CTRL_TG_OFT)) ==
				((ctrl & BES_DSPI_TX_CTRL_TG_MASK) >> BES_DSPI_TX_CTRL_TG_OFT)) {
		} else {
			//val = BES_DSPI_RD_INT;
			//ret = bes_dspi_reg_write(spi, BES_DSPI_RD_INT_REG, &val, 4);
			//if (unlikely(ret))
				//return ret;
			continue;
		}

		*lenPtr = ctrl & (BES_DSPI_TX_CTRL_MSG_MASK | BES_DSPI_TX_CTRL_LEN_MASK);
		break;
	}
	return 0;
}

static bool bes_dspi_msg_local(struct bes_spi *bes_spi,
		struct sk_buff *skb, struct sk_buff_head *rx)
{
	struct ipc_header *hdr = (struct ipc_header *)skb->data;
	int index, i;
	struct tx_cfm_tag *cfm;
	struct lmac_msg *msg;
	struct sk_buff *tmp;
	struct retry_entry_t *entry;

	if (hdr->type < EVENT_E2A_MSG_CRC_ERR_IND) {
		return false;
	}
	if (hdr->type == EVENT_E2A_DATA_CRC_ERR_IND) {
		cfm = (struct tx_cfm_tag *)(hdr + 1);
		index = cfm->amsdu_size;
		entry = (struct retry_entry_t *)(((u8 *)bes_spi->last_data) + index * bes_spi->retry_entry_size);
		bes_info(BES_DBG_SPI, "%s confirm data %d 0x%x:0x%x(%d)", __func__, index, cfm->hostid,
				entry->hostid, entry->origin_hostid_num);
		if (cfm->hostid == entry->hostid) {
			cfm->amsdu_size = 0;
			hdr->type = EVENT_E2A_TX_CFM;
			for (i = 0; i < entry->origin_hostid_num; i++) {
				tmp = skb_copy(skb, GFP_KERNEL);
				if (WARN_ON(!tmp))
					continue;
				cfm = (struct tx_cfm_tag *)(tmp->data + sizeof(struct ipc_header));
				cfm->hostid = entry->origin_hostids[i];
				bes_info(BES_DBG_SPI, "%s extract hostid %d=0x%x\n", __func__, i, cfm->hostid);
				skb_queue_tail(rx, tmp);
			}
		} else {
			BUG_ON(1);
		}
	} else if (hdr->type == EVENT_E2A_MSG_CRC_ERR_IND) {
		msg = (struct lmac_msg *)bes_spi->last_msg;
		bes_info(BES_DBG_SPI, "%s repush msg 0x%x 0x%x\n", __func__, msg->psa_pattern,
				msg->id);
		bes_msg_fifo_repush(bes_spi->last_msg, bes_spi->last_msg_size);
	} else {
		bes_dbg(BES_DBG_SPI, "%s type=%u size=%u\n", __func__, hdr->type, hdr->size);
		WARN_ON(1);
	}

	return true;
}

static irqreturn_t bes_dspi_irq_handler(int irq, void *dev_id)
{
	bool pri;
	int ret, i;
	u32 len = 0, total;
	void *data;
	struct rwnx_plat *self = &g_rwnx_plat;
	struct spi_device *spi = (struct spi_device *)dev_id;
	struct bes_spi *bes_spi = spi_get_drvdata(spi);
	struct sk_buff *skb;
	struct ipc_header *hdr;
	struct sk_buff_head rx;
	int loop = 0;
	u32 addr;
	unsigned long start_recv;

	if (WARN_ON(self->spi_dev != spi)) {
		return IRQ_NONE;
	}

	bes_dspi_lock(bes_spi);

	start_recv = jiffies;
	// get rx len and clear interrupt
	ret = self->clr_int(spi, &len);
	if (unlikely(ret)) {
		bes_err(BES_DBG_PSA, "%s clr int err=%d\n", __func__, ret);
		bes_tx_loop_force_close(self, true);
		goto out;
	}

	do {
		if (!len) {
			break;
		}
		pri = (len & BES_DSPI_TX_CTRL_MSG_MASK) ? 1 : 0;
		len = (len & BES_DSPI_TX_CTRL_LEN_MASK) >> BES_DSPI_TX_CTRL_LEN_OFT;
		bes_dbg(BES_DBG_PSA, "%s len=%d, pri=%d\n", __func__, len, pri);

		ret = self->data_get(self->spi_dev, &data, len);
		if (unlikely(ret)) {
			bes_err(BES_DBG_MSG_TX, "%s read error=%d\n", __func__, ret);
			bes_tx_loop_force_close(self, true);
			goto out;
		}

		total = 0;
		skb_queue_head_init(&rx);
		for (i = 0; ; i++) {
			hdr = (struct ipc_header *)(data + total);
			if (!hdr->size)
				break;
			total += self->align_size(bes_spi->spi, hdr->size);
			if (unlikely((hdr->size > bes_spi->rd_xfer_bufsize) || (total > len))) {
				bes_err(BES_DBG_PSA, "len=%u total=%u i=%d type=%d size=%u\n", len, total, i, hdr->type, hdr->size);
				WARN_ON(1);
				skb_queue_purge(&rx);
				goto out;
			}
			skb = bes_dev_alloc_skb(hdr->size);
			if (WARN_ON(!skb))
				break;
			memcpy(skb->data, hdr, hdr->size);
			skb_put(skb, hdr->size);
			if (pri) {
				if (bes_dspi_msg_local(bes_spi, skb, &rx))
					dev_kfree_skb(skb);
				else
					skb_queue_tail(&rx, skb);
			} else
				skb_queue_tail(&rx, skb);
		}
		WARN_ON(i > bes_spi->rx_info.items);
		if (pri) {
			spin_lock(&self->rx_pri_q.lock);
			skb_queue_splice(&rx, &self->rx_pri_q);
			spin_unlock(&self->rx_pri_q.lock);
		} else {
			spin_lock(&self->rx_normal_q.lock);
			skb_queue_splice(&rx, &self->rx_normal_q);
			spin_unlock(&self->rx_normal_q.lock);
		}
#ifdef BES_DSPI_TX_EXTRA_TAIL
		len = bes_spi->extra_tail;
		bes_spi->extra_tail = 0;
		if (len && atomic_read(&bes_spi->aggr_preempt)) {
			addr = bes_dspi_aggr_buf(bes_spi);
			if (addr) {
				ret = bes_dspi_direct_write(bes_spi->spi, addr, bes_spi->aggr_copy_buf, bes_spi->aggr_copy_len, BES_DSPI_WR_DATA_INT);
				if (!ret)
					atomic_set(&bes_spi->aggr_preempt, 0);
			}
		}
#else
		len = 0;
#endif
		loop ++;
	} while(time_after(start_recv + HZ / 2, jiffies));

out:
	bes_dspi_unlock(bes_spi);
	if (loop) {
		atomic_inc(&self->wifi_rx);
		queue_work(self->bus_rx_wq, &self->bus_rx_work);
		return IRQ_HANDLED;
	} else {
		return IRQ_NONE;
	}
}

static void bes_wifi_normal_rx_handler(struct rwnx_plat *self)
{
	struct sk_buff *skb = NULL;

	while (!skb_queue_empty(&self->rx_pri_q)) {
		skb = skb_dequeue(&self->rx_pri_q);
		if (skb)
			bes_psa_rx_handler(self, skb);
		else
			break;
	}
	while (!skb_queue_empty(&self->rx_normal_q)) {
		skb = skb_dequeue(&self->rx_normal_q);
		if(skb) {
			bes_psa_rx_handler(self, skb);
		} else {
			break;
		}
	}
}

static void bes_bus_rx_work(struct work_struct *work)
{
	struct rwnx_hw *rwnx_hw = NULL;
	struct rwnx_plat *self = container_of(work, struct rwnx_plat, bus_rx_work);
	int wifi_rx = 0;

	bes_dbg(BES_DBG_PSA, "%s\n", __func__);

	while (1) {
		wifi_rx = atomic_xchg(&self->wifi_rx, 0);
		bes_dbg(BES_DBG_PSA, "wifi_rx=%d\n", wifi_rx);
		if (wifi_rx) {
			if (bes_tx_loop_is_enabled(self)) {
				//bes_wifi_tx_loop_rx_handler(self);
				BUG_ON(1);
			} else {
				bes_wifi_normal_rx_handler(self);
			}
		} else {
			break;
		}

		rwnx_hw = self->drvdata;
		if (self->enabled && rwnx_hw && spin_trylock_bh(&rwnx_hw->tx_lock)) {
			rwnx_hwq_process_all(rwnx_hw);
			spin_unlock_bh(&rwnx_hw->tx_lock);
		}
	}
}

static int bes_bus_rx_init(struct rwnx_plat *rwnx_plat)
{
	rwnx_plat->bus_rx_wq = alloc_workqueue("bes_spi", WQ_MEM_RECLAIM | WQ_HIGHPRI | WQ_UNBOUND, 1);
	if (WARN_ON(!rwnx_plat->bus_rx_wq))
		return -ENOMEM;

	INIT_WORK(&rwnx_plat->bus_rx_work, bes_bus_rx_work);
	return 0;
}

static void bes_bus_rx_stop(struct rwnx_plat *rwnx_plat)
{
	if (rwnx_plat->bus_rx_wq) {
		flush_workqueue(rwnx_plat->bus_rx_wq);
		destroy_workqueue(rwnx_plat->bus_rx_wq);
		rwnx_plat->bus_rx_wq = NULL;
	}
}

int bes_dspi_init_plat_cfg(struct spi_device *spi)
{
	int ret = 0;
	struct bes_spi *bes_spi = spi_get_drvdata(spi);

	g_rwnx_plat.spi_dev = spi;
	g_rwnx_plat.clr_int = bes_bus_clr_int;
	g_rwnx_plat.data_get = bes_dspi_data_get;
	g_rwnx_plat.align_size = bes_bus_align_size;
	g_rwnx_plat.dev_status = 0;
	g_rwnx_plat.wakelock_flag = 0;
	g_rwnx_plat.dev = &spi->dev;
	g_rwnx_plat.run_flags = 0;
	spin_lock_init(&g_rwnx_plat.run_flags_lock);

	spin_lock_init(&g_rwnx_plat.pwr_io_lock);
	atomic_set(&g_rwnx_plat.wifi_rx, 0);
	skb_queue_head_init(&g_rwnx_plat.rx_pri_q);
	skb_queue_head_init(&g_rwnx_plat.rx_normal_q);
	skb_queue_head_init(&g_rwnx_plat.tx_q);
	init_waitqueue_head(&g_rwnx_plat.dev_stat_wait_q);

	if ((ret = bes_bus_rx_init(&g_rwnx_plat)) < 0) {
		bes_err(BES_DBG_PSA, "%s bus rx thread create fail\n", __func__);
		return ret;
	}

	ret = request_threaded_irq(bes_spi->irq, NULL, bes_dspi_irq_handler,
			IRQF_TRIGGER_HIGH | IRQF_ONESHOT, "bes_dspi_irq", spi);
	if (unlikely(ret)) {
		bes_err(BES_DBG_PSA, "%s request irq for dspi failed(%d)\n", __func__, ret);
		return ret;
	}

	return 0;
}

void bes_dspi_deinit_plat_cfg(void)
{
	struct rwnx_plat *rwnx_plat = (struct rwnx_plat *)&g_rwnx_plat;
	struct bes_spi *bes_spi = spi_get_drvdata(rwnx_plat->spi_dev);
	disable_irq(bes_spi->irq);
	free_irq(bes_spi->irq, rwnx_plat->spi_dev);
	bes_bus_rx_stop(rwnx_plat);
	skb_queue_purge(&rwnx_plat->rx_pri_q);
	skb_queue_purge(&rwnx_plat->rx_normal_q);
	rwnx_plat->spi_dev = NULL;
}

int bes_dspi_platform_init(void)
{
	spin_lock_init(&g_rwnx_plat.tx_lock);

	g_rwnx_plat.data_push = bes_dspi_data_push;
	g_rwnx_plat.msg_push = bes_dspi_msg_push;

	return rwnx_platform_init(&g_rwnx_plat, NULL);
}

int bes_dspi_platform_on(struct rwnx_hw *rwnx_hw)
{
	int ret = 0;
	struct rwnx_plat *rwnx_plat = rwnx_hw->plat;
	unsigned long flags;

	ret = bes_pwr_init(rwnx_hw, NULL);
	if (ret)
		return ret;
	bes_filter_manager_init(rwnx_hw);
	bes_wowlan_init(rwnx_hw);
	bes_lstats_init(rwnx_hw);

	/* Clear all run flags */
	spin_lock_irqsave(&rwnx_plat->run_flags_lock, flags);
	rwnx_plat->run_flags = 0;
	spin_unlock_irqrestore(&rwnx_plat->run_flags_lock, flags);

	init_waitqueue_head(&rwnx_plat->tx_wait_q);
	rwnx_plat->a2e_msg = kzalloc(RWNX_MSG_MAX_LEN, GFP_KERNEL);
	if (rwnx_plat->a2e_msg == NULL)
		goto err_pwr_init;
#if defined(CONFIG_RWNX_AMSDUS_TX)
#error "SLAVE SPI Cann't support amsdu tx"
#endif

	if ((ret = bes_bus_tx_init(rwnx_plat)) < 0) {
		bes_err(BES_DBG_PSA, "%s bus tx thread create fail\n", __func__);
		goto err_a2e_msg_alloc;
	}

	rwnx_plat->enabled = true;

	return 0;

err_a2e_msg_alloc:
	if (rwnx_plat->a2e_msg) {
		kfree(rwnx_plat->a2e_msg);
		rwnx_plat->a2e_msg = NULL;
	}
err_pwr_init:
	bes_lstats_deinit(rwnx_hw);
	bes_wowlan_deinit(rwnx_hw);
	bes_filter_manager_deinit(rwnx_hw);
	bes_pwr_deinit(rwnx_hw);

	return -1;
}

void bes_dspi_platform_off(struct rwnx_hw *rwnx_hw)
{
	struct rwnx_plat *rwnx_plat = rwnx_hw->plat;

	bes_lstats_deinit(rwnx_hw);
	bes_wowlan_deinit(rwnx_hw);
	bes_filter_manager_deinit(rwnx_hw);
	bes_pwr_deinit(rwnx_hw);
	bes_bus_tx_stop(rwnx_plat);

	if (rwnx_hw->rwnx_wq) {
		flush_workqueue(rwnx_hw->rwnx_wq);
		destroy_workqueue(rwnx_hw->rwnx_wq);
		rwnx_hw->rwnx_wq = NULL;
	}

	if (rwnx_plat->a2e_msg) {
		kfree(rwnx_plat->a2e_msg);
		rwnx_plat->a2e_msg = NULL;
	}
}

static int bes_dspi_cmd_mem_init(struct bes_spi *bes_spi)
{
	u8 *buf;

	memset(bes_spi, 0, sizeof(*bes_spi));

	buf = kmalloc(BES_HOST_SPI_CMD_BUFSIZE * 2, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	bes_spi->xfer_reg.tx_buf = buf;
	buf += BES_HOST_SPI_CMD_BUFSIZE;
	bes_spi->xfer_reg.rx_buf = buf;
	return 0;
}

static void bes_dspi_cmd_mem_deinit(struct bes_spi *bes_spi)
{
	kfree(bes_spi->xfer_reg.tx_buf);
	bes_spi->xfer_reg.tx_buf = NULL;
	bes_spi->xfer_reg.rx_buf = NULL;
}

static int bes_dspi_data_mem_init(struct bes_spi *bes_spi)
{
	int i;
	u16 buf_len;
	int ret = -ENOMEM;
	u8 *buf;
	u16 rd_xfer, wr_xfer;
	u8 bufnum = 1 << bes_spi->data_buf_ctrl.bufnum_power;

	atomic_set(&bes_spi->aggr_preempt, 0);

	bes_spi->data_buf_pool = kzalloc(bufnum * sizeof(u32), GFP_KERNEL);
	if (!bes_spi->data_buf_pool)
		return -ENOMEM;

	if (bes_spi->data_buf_ctrl.aggr_num) {
		for (i = 0; i < bufnum; i++) {
			if (bes_dspi_reg_read(bes_spi->spi,
					BES_DSPI_SHARE(bes_spi->share_base, BES_DSPI_RX_DATA_BUF_POOL + i * 4),
					bes_spi->data_buf_pool + i, 4)) {
				kfree(bes_spi->data_buf_pool);
				return -EIO;
			} else {
				bes_info(BES_DBG_SPI, "%s spi read tx data addr %d=0x%x", __func__,
						i, bes_spi->data_buf_pool[i]);
			}
		}
	}


	bes_spi->retry_entry_size = sizeof(struct retry_entry_t);
	if (bes_spi->data_buf_ctrl.aggr_num) {
		bes_spi->retry_entry_size += sizeof(u32) * bes_spi->data_buf_ctrl.aggr_num;
	} else {
		bes_spi->retry_entry_size += sizeof(u32);
	}
	bes_spi->last_data = kzalloc(bes_spi->retry_entry_size * bufnum, GFP_KERNEL);
	if (!bes_spi->last_data)
		goto err_entry;

	bes_spi->rd_xfer_bufsize = bes_spi->rx_info.bufsize * bes_spi->rx_info.bufnum + BES_HOST_SPI_TRANSFER_RD_TAIL;
	rd_xfer = bes_spi->rd_xfer_bufsize + BES_HOST_SPI_DATA_RD_BUFSIZE;
#ifdef BES_DSPI_TX_EXTRA_TAIL
	rd_xfer += BES_DSPI_TX_EXTRA_TAIL_LEN;
#endif
	if (bes_spi->data_buf_ctrl.aggr_num)
		bes_spi->wr_xfer_bufsize = bes_spi->data_buf_ctrl.aggr_num * bes_spi->data_buf_ctrl.bufsize;
	else
		bes_spi->wr_xfer_bufsize = bufnum * bes_spi->data_buf_ctrl.bufsize;
	wr_xfer = bes_spi->wr_xfer_bufsize + BES_HOST_SPI_DATA_WR_BUFSIZE + BES_HOST_SPI_TRANSFER_WR_TAIL;

	if (bes_spi->data_buf_ctrl.aggr_num) {
		bes_spi->aggr_copy_buf = (u8 *)__get_dma_pages(GFP_KERNEL, get_order(bes_spi->data_buf_ctrl.aggr_num * bes_spi->data_buf_ctrl.bufsize));
		if (!bes_spi->aggr_copy_buf)
			goto err_aggr_copy_buf;
		bes_spi->aggr_copy_hostids = kmalloc(sizeof(u32) * (1 << bes_spi->data_buf_ctrl.item_power), GFP_KERNEL);
		if (!bes_spi->aggr_copy_hostids) {
			goto err_aggr_copy_hostids;
		}
	}

	buf_len = max_t(u16, rd_xfer, wr_xfer);
	buf = (u8 *)__get_dma_pages(GFP_KERNEL, get_order(buf_len * 2));
	if (!buf) {
		kfree(bes_spi->data_buf_pool);
		goto err_data_buf;
	}
	bes_spi->xfer_data.tx_buf = buf;
	buf += buf_len;
	bes_spi->xfer_data.rx_buf = buf;

	bes_spi->aggr_copy_num = 0;
	bes_spi->aggr_copy_len = 4;

	return 0;

err_data_buf:
	if (bes_spi->aggr_copy_hostids) {
		kfree(bes_spi->aggr_copy_hostids);
		bes_spi->aggr_copy_hostids = NULL;
	}
err_aggr_copy_hostids:
	if (bes_spi->aggr_copy_buf) {
		free_pages((unsigned long)bes_spi->aggr_copy_buf, get_order(bes_spi->data_buf_ctrl.aggr_num * bes_spi->data_buf_ctrl.bufsize));
		bes_spi->aggr_copy_buf = NULL;
	}
err_aggr_copy_buf:
	kfree(bes_spi->last_data);
	bes_spi->last_data = NULL;
err_entry:
	kfree(bes_spi->data_buf_pool);
	bes_spi->data_buf_pool = NULL;
	return ret;
}

static void bes_dspi_data_mem_deinit(struct bes_spi *bes_spi)
{
	int buf_len;
	u16 rd_xfer;

	if (bes_spi->data_buf_pool)
		return;

	rd_xfer = bes_spi->rd_xfer_bufsize + BES_HOST_SPI_DATA_RD_BUFSIZE;
#ifdef BES_DSPI_TX_EXTRA_TAIL
	rd_xfer += BES_DSPI_TX_EXTRA_TAIL_LEN;
#endif
	buf_len = max_t(u16, rd_xfer, bes_spi->wr_xfer_bufsize + BES_HOST_SPI_DATA_WR_BUFSIZE + BES_HOST_SPI_TRANSFER_WR_TAIL);
	bes_spi->rd_xfer_bufsize = 0;
	bes_spi->wr_xfer_bufsize = 0;
	free_pages((unsigned long)bes_spi->xfer_reg.tx_buf, get_order(buf_len * 2));
	bes_spi->xfer_data.tx_buf = NULL;
	bes_spi->xfer_data.rx_buf = NULL;

	if (bes_spi->data_buf_ctrl.aggr_num) {
		kfree(bes_spi->aggr_copy_hostids);
		bes_spi->aggr_copy_hostids = NULL;
		free_pages((unsigned long)bes_spi->aggr_copy_buf, get_order(bes_spi->data_buf_ctrl.aggr_num * bes_spi->data_buf_ctrl.bufsize));
		bes_spi->aggr_copy_buf = NULL;
	}

	kfree(bes_spi->last_data);
	bes_spi->last_data = NULL;

	kfree(bes_spi->data_buf_pool);
	bes_spi->data_buf_pool = NULL;
}

static int bes_dspi_probe(struct spi_device *spi)
{
	u32 val;
	int ret = 0;
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_spi *bes_spi = (struct bes_spi *)rwnx_plat->priv;
	//u32 len;
	//u8 *tmp;

	if (!spi->master->max_speed_hz || spi->master->max_speed_hz > 48000000)
		spi->max_speed_hz = 48000000;
	else
		spi->max_speed_hz = spi->master->max_speed_hz;

	if (bes_dspi_clock)
		spi->max_speed_hz = bes_dspi_clock;

	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_3;

	bes_info(BES_DBG_SPI, "bes2610_dspi: Probe called (FUNC %p CS %d M %d BPW %d CLK %d)\n",
			spi, spi->chip_select, spi->mode, spi->bits_per_word,
			spi->max_speed_hz);

	if ((ret = spi_setup(spi))) {
		bes_err(BES_DBG_SPI, "spi_setup() failed(%d)\n", ret);
		goto err_setup;
	}

	if ((ret = bes_dspi_cmd_mem_init(bes_spi))) {
		bes_err(BES_DBG_SPI, "spi cmd mem init failed(%d)\n", ret);
		goto err_cmd_mem_init;
	}
	spin_lock_init(&bes_spi->lock);
	init_waitqueue_head(&bes_spi->wq);
	bes_spi->claimed = 0;
	spi_set_drvdata(spi, bes_spi);
	bes_spi->spi = spi;

	bes_spi->irq_gpio = of_get_named_gpio(spi->dev.of_node, "irq-gpio", 0);
	if (!gpio_is_valid(bes_spi->irq_gpio)) {
		bes_err(BES_DBG_PSA, "%s find irq-gpio for slave spi failed(%d)\n", __func__, ret);
		ret = bes_spi->irq_gpio;
		goto err_gpio;
	}
	bes_spi->irq = gpio_to_irq(bes_spi->irq_gpio);

	bes_dspi_lock(bes_spi);

	bes_dspi_reg_read(spi, BES_DSPI_SHARE_LOC_REG, &bes_spi->share_base, 4);
	bes_dbg(BES_DBG_SPI, "get share base 0x%x\n", bes_spi->share_base);

	if ((ret = bes_dspi_reg_read(spi,
			BES_DSPI_SHARE(bes_spi->share_base, BES_DSPI_DEVICE_ID_REG), &val, 4))) {
		bes_err(BES_DBG_SPI, "spi read device id failed(%d)\n", ret);
		goto err_cfg;
	}
	if (BES_DSPI_DEVICE_ID_BES == (val & BES_DSPI_DEVICE_ID_MASK))
		bes_info(BES_DBG_SPI, "bes device id(0x%x)\n", val);
	else {
		bes_err(BES_DBG_SPI, "bes device id mismatch(0x%x)\n", val);
		goto err_cfg;
	}
	bes_sysfs_set_device_id(val & BES_DSPI_SUB_DEVICE_ID_MASK);

	val = BES_DSPI_CFG_4_WIRE;
	if ((ret = bes_dspi_reg_write(spi, BES_DSPI_CFG_WIRE_REG, &val, 4))) {
		bes_err(BES_DBG_SPI, "%s spi config wire failed(%d)\n", __func__, ret);
		goto err_cfg;
	}

	val = BES_DSPI_CFG_CNTS;
	if ((ret = bes_dspi_reg_write(spi, BES_DSPI_CFG_READ_REG, &val, 4))) {
		bes_err(BES_DBG_SPI, "%s spi config read mode failed(%d)\n", __func__, ret);
		goto err_cfg;
	}

	if ((ret = bes_dspi_reg_read(spi, BES_DSPI_SHARE(bes_spi->share_base, BES_DSPI_TX_INFO_REG), (u8 *)&bes_spi->rx_info, 4))) {
		bes_err(BES_DBG_SPI, "%s spi read rx info failed(%d)\n", __func__, ret);
		goto err_cfg;
	}
	bes_info(BES_DBG_SPI, "spi read rx info %u:%d:%d\n", bes_spi->rx_info.bufsize, bes_spi->rx_info.bufnum, bes_spi->rx_info.items);

	if ((ret = bes_dspi_reg_read(spi, BES_DSPI_SHARE(bes_spi->share_base, BES_DSPI_TX_BUF_REG), (u8 *)bes_spi->rx_data_addr, 4))) {
		bes_err(BES_DBG_SPI, "%s spi read rx data addr0 failed(%d)\n", __func__, ret);
		goto err_cfg;
	}
	if ((ret = bes_dspi_reg_read(spi, BES_DSPI_SHARE(bes_spi->share_base, BES_DSPI_TX_BUF_REG + 4), (u8 *)&bes_spi->rx_data_addr[1], 4))) {
		bes_err(BES_DBG_SPI, "%s spi read rx data addr1 failed(%d)\n", __func__, ret);
		goto err_cfg;
	}
	bes_info(BES_DBG_SPI, "spi read addr 0x%x:0x%x\n", bes_spi->rx_data_addr[0], bes_spi->rx_data_addr[1]);

	if ((ret = bes_dspi_reg_read(spi, BES_DSPI_SHARE(bes_spi->share_base, BES_DSPI_RX_DATA_CTRL), (u8 *)&bes_spi->data_buf_ctrl, 4))) {
		bes_err(BES_DBG_SPI, "%s spi read rx data ctrl failed(%d)\n", __func__, ret);
		goto err_cfg;
	}
	bes_info(BES_DBG_SPI, "spi rx data ctrl %u:%u:%u:%u\n", bes_spi->data_buf_ctrl.bufsize,
			bes_spi->data_buf_ctrl.bufnum_power, bes_spi->data_buf_ctrl.aggr_num, bes_spi->data_buf_ctrl.item_power);

	if ((ret = bes_dspi_data_mem_init(bes_spi))) {
		bes_err(BES_DBG_SPI, "%s spi data mem init failed", __func__);
		goto err_data_mem_init;
	}

	bes_dspi_unlock(bes_spi);

	if ((ret = bes_dspi_init_plat_cfg(spi))) {
		bes_err(BES_DBG_SPI, "%s spi init_plat_cfg failed(%d)\n", __func__, ret);
		goto err_plat_cfg;
	}

	ret = bes_dspi_platform_init();
	if (ret)
		goto err_plat_init;

	if ((ret = bes_power_ctrl_system_enabled(true, false)) < 0)
		goto err_plat_init;

	return 0;

err_plat_init:
	bes_dspi_deinit_plat_cfg();
err_plat_cfg:
	bes_dspi_data_mem_deinit(bes_spi);
err_data_mem_init:
err_cfg:
	bes_dspi_unlock(bes_spi);
	gpio_free(bes_spi->irq_gpio);
err_gpio:
	bes_spi->spi = NULL;
	spi_set_drvdata(spi, NULL);
	bes_dspi_cmd_mem_deinit(bes_spi);
err_cmd_mem_init:
err_setup:
	return ret;
}

static int bes_dspi_disconnect(struct spi_device *spidev)
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_spi *bes_spi = (struct bes_spi *)rwnx_plat->priv;

	bes_info(BES_DBG_SPI, "%s enter", __func__);

	if (rwnx_plat->drvdata) {
		rwnx_platform_deinit((struct rwnx_hw *)(rwnx_plat->drvdata));
		if (!rwnx_plat->pre_alloc)
			rwnx_plat->drvdata = NULL;
	}
	if (rwnx_plat->spi_dev)
		bes_dspi_deinit_plat_cfg();
	gpio_free(bes_spi->irq_gpio);
	bes_spi->spi = NULL;
	spi_set_drvdata(spidev, NULL);
	bes_dspi_data_mem_deinit(bes_spi);
	bes_dspi_cmd_mem_deinit(bes_spi);
	return 0;
}

static struct spi_driver bes_dspi_driver = {
	.probe = bes_dspi_probe,
	.remove = bes_dspi_disconnect,
	.driver = {
		.name = "bes2610_wlan_spi",
		.of_match_table = of_match_ptr(bes_spi_dev_ids),
#ifdef CONFIG_PM
		.pm = NULL,
#endif
	}
};

/* Init Module function -> Called by insmod */
int bes_dspi_init(void)
{
	int ret;

	bes_dbg(BES_DBG_SPI, "%s enter\n", __func__);

	/* keep first */
	ret = bes_power_ctrl_dspi_init(&g_rwnx_plat);
	if (ret)
		return ret;

	ret = bes_sysfs_register(&g_rwnx_plat);
	if (ret)
		goto register_sysfs_err;

	bes_sysfs_status_update(true);

	ret = spi_register_driver(&bes_dspi_driver);
	if (ret)
		goto dspi_register_err;

	return ret;

dspi_register_err:
	bes_power_ctrl_cancel_probe_work();
	bes_sysfs_unregister();
register_sysfs_err:
	bes_power_ctrl_dspi_deinit(&g_rwnx_plat);

	return ret;
}

/* Called at Driver Unloading */
void bes_dspi_exit(void)
{
	/* FIXME -- this won't support multiple devices */
	bes_dbg(BES_DBG_SPI, "%s enter\n", __func__);

	spi_unregister_driver(&bes_dspi_driver);

	bes_sysfs_unregister();
	bes_power_ctrl_dspi_deinit(&g_rwnx_plat);

	bes_info(BES_DBG_SPI, "%s exit\n", __func__);
}
