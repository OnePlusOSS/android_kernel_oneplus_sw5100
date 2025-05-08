/*
 * @file bes_mspi.c
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
#include <linux/crc16.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <uapi/linux/sched/types.h>

#include "bes_sysfs.h"
#include "rwnx_platform.h"
#include "bes_pwr.h"
#include "bes_log.h"
#include "rwnx_msg_tx.h"
#include "bes_psa.h"
#include "bes_mspi.h"
#include "rwnx_main.h"

#include "bes_tx_loop.h"
#include "bes_wowlan.h"
#include "bes_linkinfo.h"
#ifdef CONFIG_OPLUS_PLATFORM
#include "oplus_custom_wlan.h"
#endif

#define BES_RX_SCHED_PRIORITY 50

static struct completion mspi_tx_ack_completion;

#define MSPI_XFER_ERR_MAX_CNT      (3)
static uint8_t mspi_rx_error_cnt = 0;
static uint8_t mspi_rx_crc_err_cnt = 0;

#if defined(CONFIG_OPLUS_PLATFORM) && defined(ADPT_GKI_KERNEL)
extern void oplus_spi_slave_ready_cb_register(struct spi_device *spi, void (*slave_ready_cb)(struct spi_device*, const void* tx, const void* rx));
#endif

static int bes_mspi_default_blksize = 256;
module_param_named(blksize, bes_mspi_default_blksize, int, 0644);

static const struct of_device_id bes_spi_dev_ids[] = {
	{.compatible = "bes2610,master_spi"},
	{/* end: all zeroes */},
};

extern struct rwnx_plat g_rwnx_plat;

static int bes_mspi_power_up(struct rwnx_plat *rwnx_plat);
static int bes_mspi_power_down(struct rwnx_plat *rwnx_plat);

const static char* mspi_state_strs[] = {
	"MST_TXIND",
	"SLV_RXRDY",
	"SLV_TXRDY",
	"SLV_CRC",
	"MST_CRC",
	"SPI_XFER",
	"TX_SEQ",
	"RX_SEQ",
	"RX_STAGE",
	"RX_FIRST_HDR",
	"RX_SECOND_HDR",
	"RX_THIRD_HDR",
};

struct mspi_state_record_ctxt state_ctxt;
static void mspi_state_record_info(enum mspi_state_record_type type, uint16_t value)
{
	uint32_t idx = state_ctxt.idx;
	state_ctxt.state_info[idx].type = type;
	state_ctxt.state_info[idx].trig_time = jiffies;
	state_ctxt.state_info[idx].value = value;
	state_ctxt.idx++;
	state_ctxt.idx %= MSPI_STATE_RECORD_NUM;
}

static void mspi_state_record_dump(void)
{
	int i;

	bes_info(BES_DBG_SPI, "DUMP state_info curr_idx:%d", state_ctxt.idx);
	for (i = 0; i < MSPI_STATE_RECORD_NUM; i += 2) {
		bes_info(BES_DBG_SPI, "[%d]%s-%d-%lu [%d]%s-%d-%lu",
			     i,
			     mspi_state_strs[state_ctxt.state_info[i].type],
			     state_ctxt.state_info[i].value,
			     state_ctxt.state_info[i].trig_time,
			     i + 1,
			     mspi_state_strs[state_ctxt.state_info[i + 1].type],
			     state_ctxt.state_info[i + 1].value,
			     state_ctxt.state_info[i + 1].trig_time);
	}
}

void bes_io_bus_status_dump(void)
{
	mspi_state_record_dump();
}

#if defined(CONFIG_BES_POWER_CTRL_GPIO)

static int bes_mspi_plat_power(struct rwnx_plat *rwnx_plat, bool on)
{
	if (on) {
		return bes_mspi_power_up(rwnx_plat);
	} else
		return bes_mspi_power_down(rwnx_plat);
}

int bes_power_ctrl_mspi_init(struct rwnx_plat *rwnx_plat)
{
	struct bes_mspi_power_ctrl *bes_mspi_power_ctrl;

	rwnx_plat->power_priv = bes_power_ctrl_gpio_register(rwnx_plat,
							     sizeof(struct bes_mspi_power_ctrl),
							     bes_mspi_plat_power);
	if (!rwnx_plat->power_priv) {
		bes_err(BES_DBG_SPI, "%s %d, failed\n", __func__, __LINE__);
		return -ENOMEM;
	}

	bes_mspi_power_ctrl = rwnx_plat->power_priv;

	return 0;
}

void bes_power_ctrl_mspi_deinit(struct rwnx_plat *rwnx_plat)
{
	bes_power_ctrl_gpio_unregister(rwnx_plat->power_priv);
}

#endif

static void bes_mspi_lock(struct bes_spi *bes_spi)
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

static void bes_mspi_unlock(struct bes_spi *bes_spi)
{
	unsigned long flags;

	spin_lock_irqsave(&bes_spi->lock, flags);
	bes_spi->claimed = 0;
	spin_unlock_irqrestore(&bes_spi->lock, flags);
	wake_up(&bes_spi->wq);

	return;
}

static u16 bes_crc16(u16 crc_16, const u8 *data, u16 size) {
	return crc16(crc_16, data, size);
}

static struct bes_spi_transfer_t *bes_mspi_cmd_build(struct bes_spi *bes_spi)
{
	struct bes_spi_transfer_t *xfer;

	u16 bufsize = bes_spi->mspi_param.blksize ? bes_spi->mspi_param.blksize : bes_mspi_default_blksize;

	if (atomic_inc_return(&bes_spi->cmd_lock) > 1) {
		WARN_ON(1);
		return NULL;
	}

	xfer = &bes_spi->xfer_cmd;
	memset(&xfer->xfer, 0, sizeof(struct spi_transfer));
	INIT_LIST_HEAD(&xfer->node);
	xfer->xfer.rx_buf = bes_spi->cmd_rx_buf;
	xfer->xfer.len = bufsize;
	memset(bes_spi->cmd_rx_buf, 0x55, bufsize);

	bes_dbg(BES_DBG_SPI, "%s xfer=%p txbuf=%p rxbuf=%p\n", __func__, xfer, xfer->xfer.tx_buf, xfer->xfer.rx_buf);
	return xfer;
}

void bes_mspi_add_header(struct bes_spi *bes_spi, struct bes_spi_transfer_t *xfer, void *buf,
		enum MSPI_XFER_TYPE type, u16 seq, u8 num, u16 len)
{
	u16 crc16;
	struct mspi_aggr_header_t *hdr = (struct mspi_aggr_header_t *)buf;
	hdr->type = type;
	hdr->seq = seq;
	hdr->aggr_num = num;
	hdr->aggr_len = len;
	crc16 = bes_crc16(0, buf + sizeof(u16), len + sizeof(struct mspi_aggr_header_t) - sizeof(u16));
	hdr->crc16 = crc16;
	bes_dbg(BES_DBG_SPI, "%s xfer=%p header=%p type=%u seq=%u num=%u len=%u crc16=0x%x\n", __func__,
			xfer, hdr, type, seq, num, len, crc16);
}

static void bes_mspi_free_one_transfer(struct bes_spi *bes_spi, struct bes_spi_transfer_t *xfer)
{
	bes_dbg(BES_DBG_SPI, "%s xfer=%p\n", __func__, xfer);
	kfree(xfer);
}

static struct bes_spi_transfer_t *bes_mspi_alloc_one_transfer(struct bes_spi *bes_spi, void *tx_buf, void *rx_buf, u16 len)
{
	struct bes_spi_transfer_t *xfer;

	xfer = (struct bes_spi_transfer_t *)kzalloc(sizeof(struct bes_spi_transfer_t), GFP_KERNEL);
	if (!xfer)
		return NULL;

	bes_dbg(BES_DBG_SPI, "%s xfer=%p tx_seq=%u rx_seq=%u rx_pos=%u len=%u %p %p\n", __func__, xfer, bes_spi->tx_seq, bes_spi->rx_seq, bes_spi->rx_pos, len, tx_buf, rx_buf);

	INIT_LIST_HEAD(&xfer->node);
	xfer->xfer.len = len;

	if (tx_buf) {
		xfer->xfer.tx_buf = tx_buf;
#ifdef PLAT_ROCKCHIP
		xfer->xfer.rx_buf = bes_spi->rx_dummy_buf;
#endif
	}

	if (rx_buf) {
		xfer->xfer.rx_buf = rx_buf;
	}

	return xfer;
}

static void bes_mspi_exchange_done(void *context)
{
	complete(context);
}

#ifdef BES_SPI_GPIO_RESPONSE
static irqreturn_t bes_mspi_tx_done_irq(int irq, void *dev)
{

	bes_dbg(BES_DBG_SPI, "%s\n", __func__);
	mspi_state_record_info(MSPI_STATE_MASTER_CRC, 1);
	complete(&mspi_tx_ack_completion);
	return IRQ_HANDLED;
}
#endif

static struct mspi_aggr_header_t *bes_mspi_first_data(struct bes_spi *bes_spi, struct bes_spi_transfer_t *xfer)
{
	if (xfer->tx)
		return (struct mspi_aggr_header_t *)xfer->xfer.tx_buf;
	else
		return (struct mspi_aggr_header_t *)xfer->xfer.rx_buf;
}

static struct mspi_aggr_header_t *bes_mspi_second_data(struct bes_spi *bes_spi, struct bes_spi_transfer_t *xfer)
{
	u16 bufsize = bes_spi->mspi_param.blksize ? bes_spi->mspi_param.blksize : bes_mspi_default_blksize;
	if (xfer->tx)
		return (struct mspi_aggr_header_t *)(xfer->xfer.tx_buf + bufsize);
	else
		return (struct mspi_aggr_header_t *)(xfer->xfer.rx_buf + bufsize);
}

static void bes_mspi_common_header(struct mspi_aggr_header_t *hdr, enum MSPI_XFER_TYPE type, u8 num, u16 len)
{
	hdr->type = (u8)type;
	hdr->aggr_num = num;
	hdr->aggr_len = len;
}

static void bes_mspi_add_seq(struct bes_spi *bes_spi, struct mspi_aggr_header_t *hdr, bool inc)
{
	hdr->seq = bes_spi->tx_seq;
	if (inc)
		bes_spi->tx_seq ++;
	mspi_state_record_info(MSPI_STATE_TX_SEQ, hdr->seq);
}

static void bes_mspi_add_crc16(struct bes_spi *bes_spi, struct mspi_aggr_header_t *hdr, u16 len)
{
	hdr->crc16 = bes_crc16(0, ((void *)hdr) + 2, len - 2);
}

static bool bes_mspi_check_first_data(struct bes_spi *bes_spi, struct bes_spi_transfer_t *xfer)
{
	u16 crc16_cal, crc16_res, bufsize = bes_spi->mspi_param.blksize ? bes_spi->mspi_param.blksize : bes_mspi_default_blksize;
	struct mspi_aggr_header_t *hdr = bes_mspi_first_data(bes_spi, xfer);
	//print_hex_dump(KERN_INFO, "check first data:", DUMP_PREFIX_NONE, 16, 1, hdr, bufsize, false);
	crc16_res = hdr->crc16;
	crc16_cal = bes_crc16(0, ((void *)hdr) + sizeof(u16), bufsize - sizeof(u16));
	if (unlikely(crc16_cal != crc16_res)) {
		bes_err(BES_DBG_SPI, "%s cal_crc16=0x%x != expected_crc16=0x%x seq=%u hdr->seq=%u\n", __func__, crc16_cal, crc16_res, bes_spi->rx_seq, hdr->seq);
		return false;
	}
	bes_spi->rx_seq ++;
	mspi_state_record_info(MSPI_STATE_RX_SEQ, hdr->seq);
	return true;
}

static bool bes_mspi_check_second_data(struct bes_spi *bes_spi, struct bes_spi_transfer_t *xfer)
{
	u16 crc16_cal, crc16_res, bufsize = bes_spi->mspi_param.blksize ? bes_spi->mspi_param.blksize : bes_mspi_default_blksize;
	struct mspi_aggr_header_t *hdr = bes_mspi_second_data(bes_spi, xfer);
	//print_hex_dump(KERN_INFO, "check second data:", DUMP_PREFIX_NONE, 16, 1, hdr, bufsize, false);
	bufsize = bes_spi->rx_aggr_len - bufsize + sizeof(struct mspi_aggr_header_t) * 2;
	crc16_res = hdr->crc16;
	crc16_cal = bes_crc16(0, ((void *)hdr) + sizeof(u16), bufsize - sizeof(u16));
	if (unlikely(crc16_cal != crc16_res)) {
		bes_err(BES_DBG_SPI, "%s cal_crc16=0x%x != expected_crc16=0x%x seq=%u hdr->seq=%u\n", __func__, crc16_cal, crc16_res, bes_spi->rx_seq, hdr->seq);
		msleep(10);
		return false;
	}
	bes_spi->rx_seq ++;
	mspi_state_record_info(MSPI_STATE_RX_SEQ, hdr->seq);
	return true;
}

static bool bes_mspi_data_crc_check(struct bes_spi *bes_spi, struct bes_spi_transfer_t *xfer)
{
	u16 blksize;
	if (bes_spi->rx_stage == FIRST_DATA) {
		return bes_mspi_check_first_data(bes_spi, xfer);
	} else if (bes_spi->rx_stage == SECOND_DATA) {
		BUG_ON(xfer->xfer.rx_buf != bes_spi->rx_buf + bes_spi->mspi_param.blksize);
		xfer->xfer.rx_buf = bes_spi->rx_buf;
		return bes_mspi_check_second_data(bes_spi, xfer);
	} else if (bes_spi->rx_stage == FULL_DATA) {
		blksize = bes_spi->mspi_param.blksize;
		if (bes_spi->rx_aggr_len > blksize - sizeof(struct mspi_aggr_header_t))
			return bes_mspi_check_first_data(bes_spi, xfer) && bes_mspi_check_second_data(bes_spi, xfer);
		else
			return bes_mspi_check_first_data(bes_spi, xfer);
	} else {
		BUG_ON(1);
		return false;
	}
}

static int bes_mspi_data_integrity(struct bes_spi *bes_spi, struct bes_spi_transfer_t *xfer)
{
	u16 blksize = bes_spi->mspi_param.blksize ? bes_spi->mspi_param.blksize : bes_mspi_default_blksize;
	int ret;
	struct mspi_aggr_header_t *hdr;

	mspi_state_record_info(MSPI_STATE_RX_STAGE, bes_spi->rx_stage);
	if (bes_spi->rx_stage == FIRST_DATA) {
		hdr = bes_mspi_first_data(bes_spi, xfer);
		bes_spi->rx_aggr_num = hdr->aggr_num;
		bes_spi->rx_aggr_len = hdr->aggr_len;
		mspi_state_record_info(MSPI_STATE_RX_FIRST_HDR, hdr->aggr_len);
		if (hdr->aggr_len <= blksize - sizeof(struct mspi_aggr_header_t)) {
			ret = 1;
		} else {
			bes_spi->rx_pos = blksize;
			memcpy(bes_spi->rx_buf, hdr, blksize);
			ret = 0;
		}
	} else if (bes_spi->rx_stage <= FULL_DATA) {
		hdr = bes_mspi_first_data(bes_spi, xfer);
		bes_spi->rx_aggr_num = hdr->aggr_num;
		bes_spi->rx_aggr_len = hdr->aggr_len;
		if (bes_spi->rx_stage == SECOND_DATA)
			mspi_state_record_info(MSPI_STATE_RX_SECOND_HDR, hdr->aggr_len);
		else
			mspi_state_record_info(MSPI_STATE_RX_FIRST_HDR, hdr->aggr_len);
		ret = 1;
	} else {
		BUG_ON(1);
		ret = -1;
	}
	bes_dbg(BES_DBG_SPI, "%s stage=%d first_hdr=%u:%u:%u:%u bes_spi=%u:%u:%u ret=%d\n", __func__,
			bes_spi->rx_stage, hdr->type, hdr->seq, hdr->aggr_num, hdr->aggr_len,
			bes_spi->rx_seq, bes_spi->rx_aggr_num, bes_spi->rx_aggr_len, ret);
	return ret;
}

static int bes_mspi_parse_data(struct bes_spi *bes_spi, struct bes_spi_transfer_t *xfer)
{
	u16 tmp, len, sublen, available, copy, blksize = bes_spi->mspi_param.blksize, total = bes_spi->rx_aggr_len, aggr_num = bes_spi->rx_aggr_num;
	int i, ret = -1;
	struct sk_buff_head rx;
	struct sk_buff *skb;
	struct ipc_header *ipc_hdr;
	struct mspi_aggr_header_t *hdr;
	void *src, *buf = xfer->xfer.rx_buf;

	if (!bes_spi->rx_aggr_num || !bes_spi->rx_aggr_len || (aggr_num > bes_spi->mspi_param.aggr_num) ||
			(bes_spi->rx_aggr_len > bes_spi->mspi_param.bufsize + sizeof(struct mspi_aggr_header_t))) {
		bes_err(BES_DBG_SPI, "%s aggr num=%u len=%u\n", __func__, bes_spi->rx_aggr_num, bes_spi->rx_aggr_len);
		msleep(10);
		goto err;
	}

	skb_queue_head_init(&rx);

	src = NULL;
	len = 0;
	for ( i = 0; i < aggr_num; i++) {
		if (len > total)
			goto err;
		sublen = 0;
		skb = NULL;
		ipc_hdr = NULL;
		copy = 0;
		while(len < total) {
			if (len < blksize - sizeof(struct mspi_aggr_header_t)) {
				src = buf + sizeof(struct mspi_aggr_header_t) + len;
				available = blksize - sizeof(struct mspi_aggr_header_t) - len;
			} else {
				src = buf + sizeof(struct mspi_aggr_header_t) * 2 + len;
				available = total - len;
			}
			if (!skb) {
				ipc_hdr = (struct ipc_header *)src;
				copy = BES_HOST_SPI_ALIGN(ipc_hdr->size);
				bes_dbg(BES_DBG_SPI, "%s,%d i=%d type=%u size=%u copy=%u src=%p\n", __func__, __LINE__,
						i, ipc_hdr->type, ipc_hdr->size, copy, src);
				skb = bes_dev_alloc_skb(copy);
				if (WARN_ON(!skb))
					goto err;
			}
			tmp = copy - sublen;
			tmp = min_t(u16, tmp, available);
			if (!tmp) {
				bes_err(BES_DBG_SPI, "%s data size error, aggr num=%u len=%u sublen=%u available=%u i=%d type=%u size=%u copy=%u src=%p\n",
						__func__, bes_spi->rx_aggr_num, bes_spi->rx_aggr_len, sublen, available, i, ipc_hdr->type, ipc_hdr->size, copy, src);
				goto err;
			}
			memcpy(skb->data + sublen, src, tmp);
			bes_dbg(BES_DBG_SPI, "%s,%d len=%u sublen=%u available=%u tmp=%u", __func__, __LINE__,
					len, sublen, available, tmp);
			sublen += tmp;
			len += tmp;
			if (sublen >= copy) {
				skb_put(skb, copy);
				__skb_queue_tail(&rx, skb);
				break;
			}
		}
	}

	if (total > blksize - sizeof(struct mspi_aggr_header_t)) {
		if (unlikely(total != len + sizeof(struct mspi_aggr_header_t))) {
			bes_err(BES_DBG_SPI, "%s,%d total=%u len=%u aggr=%u\n", __func__, __LINE__, total, len, aggr_num);
			msleep(10);
			ret = -EIO;
			goto err;
		}
		hdr = (struct mspi_aggr_header_t *)(buf + sizeof(struct mspi_aggr_header_t) * 2 + len);
		if (hdr->type == MSPI_XFER_LEN_INFO) {
			mspi_state_record_info(MSPI_STATE_RX_THIRD_HDR, hdr->aggr_len);
			bes_dbg(BES_DBG_SPI, "%s third header aggr num=%u len=%u\n", __func__, hdr->aggr_num, hdr->aggr_len);
			if ((hdr->type == MSPI_XFER_LEN_INFO) && hdr->aggr_len) {
				bes_spi->rx_aggr_num = hdr->aggr_num;
				bes_spi->rx_aggr_len = hdr->aggr_len;
				bes_spi->rx_pos = 0;
				ret = 1;
			}
		} else {
			ret = 0;
		}
	} else if (total != len) {
		bes_err(BES_DBG_SPI, "%s,%d total=%u len=%u aggrnum=%u\n", __func__, __LINE__,
				total, len, aggr_num);
		msleep(10);
		ret = -EIO;
		goto err;
	}

	spin_lock(&bes_spi->rx_packets.lock);
	skb_queue_splice_tail_init(&rx, &bes_spi->rx_packets);
	spin_unlock(&bes_spi->rx_packets.lock);
	return ret;

err:
	WARN_ON(1);
	skb_queue_purge(&rx);
	return ret;
}

int bes_mspi_recv_check(struct bes_spi *bes_spi, struct bes_spi_transfer_t *xfer)
{
	int ret = 0;
	struct mspi_aggr_header_t *hdr;

	if (xfer->tx && (bes_spi->mspi_param.duplex == 0))
		return 0;

	if (bes_mspi_data_crc_check(bes_spi, xfer) == false) {
		ret = -84;
		goto crc_err;
	}
#ifdef BES_SPI_GPIO_RESPONSE
	bes_spi->slv_crc_ts = jiffies;
	if (jiffies_to_msecs(bes_spi->slv_crc_ts - bes_spi->rx_ready_ts) > 80) {
		bes_warn(BES_DBG_SPI, "%s trig too late, crc_ts:%lu ready_ts:%lu delta:%lu",
			 __func__, bes_spi->slv_crc_ts, bes_spi->rx_ready_ts,
			 (unsigned long int)jiffies_to_msecs(bes_spi->slv_crc_ts - bes_spi->rx_ready_ts));
		ret = -84;
		goto crc_err;
	}
	gpio_set_value(bes_spi->rx_done_gpio, 1);
	mspi_state_record_info(MSPI_STATE_SLAVE_CRC, 1);
	bes_dbg(BES_DBG_SPI, "%s", __func__);
#endif
#if 0
		if (hdr->type == HOST_FIRST_DATA) {
			BUG_ON(bes_spi->rx_skb);
			bes_spi->rx_skb = bes_dev_alloc_skb(BES_HOST_SPI_PACKET_SIZE);
			if (WARN_ON(!bes_spi->rx_skb)) {
				ret = -ENOMEM;
				break;
			}
			memcpy(bes_spi->rx_skb->data, hdr + 1, bufsize);
			bes_spi->rx_pos = bufsize;
			bes_spi->rx_next = hdr->next;
			bes_dbg(BES_DBG_SPI, "%s first_data next=%u %u\n", __func__, bes_spi->rx_next, bes_spi->rx_seq);
		} else if (hdr->type == HOST_LAST_DATA) {
			BUG_ON(bes_spi->rx_pos > BES_HOST_SPI_PACKET_SIZE);
			BUG_ON(BES_HOST_SPI_PACKET_SIZE - bes_spi->rx_pos > bufsize);
			memcpy(bes_spi->rx_skb->data + bes_spi->rx_pos, hdr + 1,
					BES_HOST_SPI_PACKET_SIZE - bes_spi->rx_pos);
			bes_spi->rx_pos = BES_HOST_SPI_PACKET_SIZE;
			skb_put(bes_spi->rx_skb, bes_spi->rx_pos);
			__skb_queue_tail(&rx, bes_spi->rx_skb);
			bes_spi->rx_skb = NULL;
			bes_dbg(BES_DBG_SPI, "%s last data rxpos=%u %u\n", __func__, bes_spi->rx_pos, bes_spi->rx_seq);
		} else if (hdr->type == HOST_CONTINUE_DATA) {
			BUG_ON(bes_spi->rx_pos + bufsize > BES_HOST_SPI_PACKET_SIZE);
			memcpy(bes_spi->rx_skb->data + bes_spi->rx_pos, hdr + 1, bufsize);
			bes_spi->rx_pos += bufsize;
			bes_dbg(BES_DBG_SPI, "%s cont data rxpos=%u %u\n", __func__, bes_spi->rx_pos, bes_spi->rx_seq);
		} else if (hdr->type == HOST_INIT_TRX_PARAMETER) {
			memcpy(&bes_spi->param, hdr + 1, sizeof(struct trx_param_t));
		}
#else

	ret = bes_mspi_data_integrity(bes_spi, xfer);
	if (unlikely(ret < 0)) {
		goto reset;
	} else if (ret == 1) {
		ret = 0;
		hdr = bes_mspi_first_data(bes_spi, xfer);
		if (unlikely(hdr->type == MSPI_XFER_INIT_PARAM)) {
			bes_spi->inited = true;
			memcpy(&bes_spi->mspi_param, hdr + 1, sizeof(struct mspi_init_param_t));
			goto reset;
		}
		if (hdr->type == MSPI_XFER_DATA) {
			if (bes_mspi_parse_data(bes_spi, xfer) > 0) {
				bes_spi->rx_stage = FULL_DATA;
				goto out;
			} else {
				goto reset;
			}
		}
	} else {
		bes_spi->rx_stage = SECOND_DATA;
		goto out;
	}
#endif

crc_err:
	if (bes_spi->rx_stage == FIRST_DATA) {
		bes_spi->rx_stage = FIRST_DATA;
		bes_spi->rx_aggr_len = 0;
		bes_spi->rx_aggr_num = 0;
		bes_spi->rx_pos = 0;
	}
	return ret;
reset:
	bes_spi->rx_stage = FIRST_DATA;
	bes_spi->rx_aggr_len = 0;
	bes_spi->rx_aggr_num = 0;
	bes_spi->rx_pos = 0;
out:
	return ret;
}

static void bes_mspi_ready(struct spi_device *spidev, const void *tx_buf, const void *rx_buf)
{
	const void *txbuf, *rxbuf;
	struct bes_spi_transfer_t *xfer;
	struct bes_spi *bes_spi = spi_get_drvdata(spidev);
	spin_lock(&bes_spi->xfer_lock);
	xfer = list_first_entry(&bes_spi->xfering, struct bes_spi_transfer_t, node);
	if(xfer->node.next->prev != &xfer->node) {
		bes_dbg(BES_DBG_SPI, "%s xfer=%p next=%p next_prev=%p xfering=%p\n", __func__, xfer, xfer->node.next,
				xfer->node.next->prev, &bes_spi->xfering);
		spin_unlock(&bes_spi->xfer_lock);
		WARN_ON(1);
		msleep(20000);
		return;
	}
	list_del_init(&xfer->node);
	spin_unlock(&bes_spi->xfer_lock);
	txbuf = xfer->xfer.tx_buf;
	rxbuf = xfer->xfer.rx_buf;
	bes_dbg(BES_DBG_SPI, "%s %p,%d %p:%p %p:%p\n", __func__, xfer, xfer->tx, txbuf, tx_buf, rxbuf, rx_buf);
	BUG_ON((txbuf != tx_buf) || (rxbuf != rx_buf));
	if (xfer->tx) {
		gpio_set_value(bes_spi->tx_ready_gpio, 1);
		mspi_state_record_info(MSPI_STATE_SLAVE_TX_READY, 1);
	} else {
		gpio_set_value(bes_spi->rx_ready_gpio, 1);
		bes_spi->rx_ready_ts = jiffies;
		mspi_state_record_info(MSPI_STATE_SLAVE_RX_READY, 1);
#ifdef BES_SPI_GPIO_RESPONSE
		gpio_set_value(bes_spi->rx_done_gpio, 0);
		mspi_state_record_info(MSPI_STATE_SLAVE_CRC, 0);
#endif
	}
}

static int bes_mspi_exchange(struct spi_device *spidev, struct bes_spi_transfer_t *xfer)
{
	int ret = 0;
	int retry_times = 0;
	DECLARE_COMPLETION_ONSTACK(done);
	struct spi_message m;
	struct bes_spi *bes_spi = spi_get_drvdata(spidev);

	BUG_ON(!bes_spi);
	BUG_ON(spidev->bits_per_word != 32);

	bes_mspi_lock(bes_spi);
again:

	spi_message_init(&m);
	m.complete = bes_mspi_exchange_done;
	m.context = &done;

	spi_message_add_tail(&xfer->xfer, &m);
	spin_lock(&bes_spi->xfer_lock);
	list_add_tail(&xfer->node, &bes_spi->xfering);
	spin_unlock(&bes_spi->xfer_lock);
	bes_dbg(BES_DBG_SPI, "%s xfer=%p len=%d tx=%d\n", __func__,
			xfer, xfer->xfer.len, xfer->tx);

	mspi_state_record_info(MSPI_STATE_SPI_XFER, 0);
	ret = spi_async(spidev, &m);
	if (unlikely(ret)) {
		bes_err(BES_DBG_SPI, "%s,%d err=%d", __func__, __LINE__, ret);
		spin_lock(&bes_spi->xfer_lock);
		list_del_init(&xfer->node);
		spin_unlock(&bes_spi->xfer_lock);
		if (xfer->tx) {
			retry_times++;
			if (retry_times <= MSPI_XFER_ERR_MAX_CNT) {
				bes_err(BES_DBG_SPI, "%s spi_async tx fail, retrytimes:%d", __func__, retry_times);
				goto again;
			}
			else {
				bes_err(BES_DBG_SPI, "%s spi_async tx fail, retrytimes exceeds", __func__);
				goto out;
			}
		} else {
				mspi_rx_error_cnt++;
				bes_err(BES_DBG_SPI, "%s spi_async rx fail, mspi_rx_error_cnt:%d", __func__, mspi_rx_error_cnt);
				goto out;
		}
	}

	do {
		ret = wait_for_completion_interruptible_timeout(&done, msecs_to_jiffies(1000));
		if (ret > 0) {
			ret = 0;
			break;
		}
		bes_err(BES_DBG_SPI, "%s spi_async wait complete timeout ret=%d ", __func__, ret);
	} while (1);
	mspi_state_record_info(MSPI_STATE_SPI_XFER, 1);

	if (xfer->tx) {
		gpio_set_value(bes_spi->tx_ready_gpio, 0);
		mspi_state_record_info(MSPI_STATE_SLAVE_TX_READY, 0);
		// print_hex_dump(KERN_INFO, "mspi tx data:", DUMP_PREFIX_NONE, 16, 1, xfer->xfer.tx_buf, 32, false);
		ret = wait_for_completion_interruptible_timeout(&mspi_tx_ack_completion, msecs_to_jiffies(100));
		if (ret > 0) {
			reinit_completion(&mspi_tx_ack_completion);
			ret = 0;
		} else if (ret == 0) {
			reinit_completion(&mspi_tx_ack_completion);
			retry_times++;
			ret = -ETIMEDOUT;
			if (retry_times <= MSPI_XFER_ERR_MAX_CNT) {
				bes_err(BES_DBG_SPI, "mspi_tx_ack_completion timeout,retrytimes:%d", retry_times);
				/* Let the mspi rx thread to have a chance to run */
				bes_mspi_unlock(bes_spi);
				msleep(1);
				bes_mspi_lock(bes_spi);
				goto again;
			} else {
				bes_err(BES_DBG_SPI, "mspi_tx_ack_completion timeout,retrytimes exceeds");
				goto out;
			}
		} else {
			reinit_completion(&mspi_tx_ack_completion);
			bes_err(BES_DBG_SPI, "mspi_tx_ack_completion interrupt");
			retry_times++;
			ret = -ETIMEDOUT;
			if (retry_times <= MSPI_XFER_ERR_MAX_CNT)
				goto again;
			else {
				goto out;
			}
		}
	} else {
		gpio_set_value(bes_spi->rx_ready_gpio, 0);
		mspi_state_record_info(MSPI_STATE_SLAVE_RX_READY, 0);
		// print_hex_dump(KERN_INFO, "mspi rx data:", DUMP_PREFIX_NONE, 16, 1, xfer->xfer.rx_buf, 32, false);
	}

	bes_dbg(BES_DBG_SPI, "%s xfer=%p m.acutal=%d status=%d tx=%d\n", __func__,
			xfer, m.actual_length, m.status, xfer->tx);
	ret = m.status;
	if (ret || !m.actual_length || (xfer->xfer.len != m.actual_length)) {
		bes_err(BES_DBG_SPI, "%s err xfer=%p m.acutal=%d status=%d tx=%d\n", __func__,
				xfer, m.actual_length, m.status, xfer->tx);
		ret = ret ? ret : -EIO;
		if (xfer->tx) {
			retry_times++;
			if (retry_times <= MSPI_XFER_ERR_MAX_CNT) {
				bes_err(BES_DBG_SPI, "%s status fail, retrytimes:%d", __func__, retry_times);
				goto again;
			} else {
				bes_err(BES_DBG_SPI, "%s status fail, retrytimes exceeds", __func__);
				goto out;
			}
		} else {
			mspi_rx_error_cnt++;
			bes_err(BES_DBG_SPI, "%s status fail, mspi_rx_error_cnt:%d", __func__, mspi_rx_error_cnt);
		}
		goto out;
	} else {
		if (!(xfer->tx)) {
			mspi_rx_error_cnt = 0;
		}
	}

	ret = bes_mspi_recv_check(bes_spi, xfer);
	if (!(xfer->tx)) {
		if (ret) {
			mspi_rx_crc_err_cnt++;
			bes_err(BES_DBG_SPI, "%s recv check fail, mspi_rx_crc_err_cnt:%d", __func__, mspi_rx_crc_err_cnt);
		} else {
			mspi_rx_crc_err_cnt = 0;
		}
	}
out:
	bes_mspi_unlock(bes_spi);
	if (xfer != &bes_spi->xfer_cmd) {
		bes_mspi_free_one_transfer(bes_spi, xfer);
	} else {
		atomic_set(&bes_spi->cmd_lock, 0);
	}
	return ret;
}

static int bes_mspi_clr_int(struct spi_device *spi, u16 *lenPtr)
{
	u16 blksize;
	int ret = 0;

	struct bes_spi *bes_spi = spi_get_drvdata(spi);
	struct bes_spi_transfer_t *xfer;
	BUG_ON(!bes_spi);

	blksize = bes_spi->mspi_param.blksize ? bes_spi->mspi_param.blksize : bes_mspi_default_blksize;

	if (bes_spi->rx_stage == FIRST_DATA) {
		xfer = bes_mspi_cmd_build(bes_spi);
		if (xfer)
			ret = bes_mspi_exchange(bes_spi->spi, xfer);
		else
			return -EBUSY;
	}

	bes_dbg(BES_DBG_SPI, "%s stage=%d aggr num=%u len=%u rxpos=%u\n", __func__, bes_spi->rx_stage, bes_spi->rx_aggr_num,
		bes_spi->rx_aggr_len, bes_spi->rx_pos);

	if (!ret && lenPtr) {
		if (bes_spi->rx_stage == FIRST_DATA) {
			*lenPtr = 0;
		} else if (bes_spi->rx_stage == SECOND_DATA) {
			*lenPtr = bes_spi->rx_aggr_len - blksize + sizeof(struct mspi_aggr_header_t) * 2;
		} else if (bes_spi->rx_stage == FULL_DATA) {
			if (bes_spi->rx_aggr_len > blksize - sizeof(struct mspi_aggr_header_t))
				*lenPtr = bes_spi->rx_aggr_len + sizeof(struct mspi_aggr_header_t) * 2;
			else
				*lenPtr = blksize;
		} else {
			BUG_ON(1);
			ret = -EIO;
		}
	}

	return ret;
}

static int bes_mspi_data_get(struct spi_device *spi, u16 len)
{
	struct bes_spi *bes_spi = spi_get_drvdata(spi);
	struct bes_spi_transfer_t *xfer;

	xfer = bes_mspi_alloc_one_transfer(bes_spi, NULL, bes_spi->rx_buf + bes_spi->rx_pos, len);
	return bes_mspi_exchange(bes_spi->spi, xfer);
}

static void bes_mspi_recv_packets(struct bes_spi *bes_spi)
{
	bool notify = false;
	struct ipc_header *hdr;
	struct sk_buff *skb;
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();

	do {
		skb = skb_dequeue(&bes_spi->rx_packets);
		if (!skb)
			break;
		hdr = (struct ipc_header *)skb->data;
		bes_dbg(BES_DBG_SPI, "%s 0x%x %u\n", __func__, hdr->type, hdr->size);
		if (hdr->type == EVENT_E2A_DATA_IND)
			skb_queue_tail(&rwnx_plat->rx_normal_q, skb);
		else
			skb_queue_tail(&rwnx_plat->rx_pri_q, skb);
		notify = true;
	} while(1);

	if (notify) {
		atomic_inc(&rwnx_plat->wifi_rx);
		queue_work(rwnx_plat->bus_rx_wq, &rwnx_plat->bus_rx_work);
	}
}

static irqreturn_t bes_mspi_irq_handler(int irq, void *dev_id)
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct spi_device *spi = (struct spi_device *)dev_id;
	struct bes_spi *bes_spi = spi_get_drvdata(spi);
	unsigned long flags;
	bool bus_suspended = false;

	mspi_state_record_info(MSPI_STATE_MASTER_TX_IND, 1);
	/* Ignore if device is not match */
	if (rwnx_plat->spi_dev != spi)
		return IRQ_NONE;

	/* Debug information */
	bes_dbg(BES_DBG_SPI, "%s\n", __func__);

	/* Notify the PM core of a wakeup event(rx pending) if bus is suspend.
	   And mark there is a rx pending irq */
	spin_lock_irqsave(&rwnx_plat->run_flags_lock, flags);
	disable_irq_nosync(bes_spi->irq);
	rwnx_platform_pm_stay_awake(rwnx_plat, RX_WAKELOCK);
	rwnx_plat->run_flags |= BIT(PLAT_RUN_FLAGS_BUS_RX_PENDING);
	bus_suspended = !!(rwnx_plat->run_flags & BIT(PLAT_RUN_FLAGS_BUS_SUSPEND));
	spin_unlock_irqrestore(&rwnx_plat->run_flags_lock, flags);

	/* Defer mspi rx when resuming */
	if (bus_suspended) {
		return IRQ_HANDLED;
	}

	/* Wakeup mspi rx thread */
	atomic_inc(&rwnx_plat->mspi_rx);
	wake_up(&rwnx_plat->mspi_rx_wait_q);

	return IRQ_HANDLED;
}

static int bes_mspi_irq_poll(struct bes_spi *bes_spi)
{
	unsigned long start;
	int ret = gpio_request(bes_spi->irq_gpio, "irq-gpio");
	if (ret) {
		bes_err(BES_DBG_SPI, "can't reqest riq-gpio:%d\n", ret);
		return ret;
	} else {
		gpio_direction_input(bes_spi->irq_gpio);
	}
	start = jiffies;
	ret = -EBUSY;
	do {
		if (gpio_get_value(bes_spi->irq_gpio) == 1) {
			ret = 0;
			break;
		}
		msleep(1000 / HZ);
	} while(time_after(start + HZ, jiffies));
	gpio_free(bes_spi->irq_gpio);
	return ret;
}

static struct bes_spi_transfer_t *bes_mspi_aggr_build(struct bes_spi *bes_spi)
{
	bool one;
	u16 bufsize, align, blksize = bes_spi->mspi_param.blksize;
	struct bes_spi_transfer_t *xfer;
	struct mspi_aggr_header_t *hdr, *second;

	bufsize = bes_spi->mspi_param.blksize;

	if (bes_spi->aggr_copy_len <= blksize - sizeof(struct mspi_aggr_header_t)) {
		align = blksize;
		one = true;
	} else {
		align = bes_spi->aggr_copy_len + sizeof(struct mspi_aggr_header_t) * 2;
		one = false;
	}
	bes_dbg(BES_DBG_SPI, "%s size=%u align=%u one=%d\n", __func__, bes_spi->aggr_copy_len, align, one);
	xfer = bes_mspi_alloc_one_transfer(bes_spi, bes_spi->tx_buf, NULL, align);
	if (unlikely(!xfer))
		return NULL;

	xfer->tx = true;
	hdr = bes_mspi_first_data(bes_spi, xfer);
	bes_mspi_common_header(hdr, MSPI_XFER_DATA, bes_spi->aggr_copy_num, bes_spi->aggr_copy_len);
	bes_mspi_add_seq(bes_spi, hdr, true);
	bes_mspi_add_crc16(bes_spi, hdr, blksize);
	//print_hex_dump(KERN_INFO, "first data:", DUMP_PREFIX_NONE, 32, 1, hdr, blksize, false);
	if (!one) {
		second = bes_mspi_second_data(bes_spi, xfer);
		memcpy(second, hdr, sizeof(struct mspi_aggr_header_t));
		bes_mspi_add_seq(bes_spi, second, true);
		bes_mspi_add_crc16(bes_spi, second, align - blksize);
		//print_hex_dump(KERN_INFO, "second data:", DUMP_PREFIX_NONE, 32, 1, second, align - blksize, false);
	}
	return xfer;
}

static void bes_mspi_aggr_copy(struct bes_spi *bes_spi, void *buf, u16 size)
{
	u16 available, tmp, blksize = bes_spi->mspi_param.blksize, bufsize = bes_spi->mspi_param.bufsize, len, pos;
	void *dst;

	//print_hex_dump(KERN_INFO, "copy data:", DUMP_PREFIX_NONE, 32, 1, buf, size < 32 ? size : 32, false);

	len = 0;
	pos = bes_spi->aggr_copy_len;
	while(len < size) {
		if (pos >= blksize - sizeof(struct mspi_aggr_header_t)) {
			dst = bes_spi->tx_buf + pos + sizeof(struct mspi_aggr_header_t) * 2;
			available = bufsize - pos;
		} else {
			dst = bes_spi->tx_buf + pos + sizeof(struct mspi_aggr_header_t);
			available = blksize - sizeof(struct mspi_aggr_header_t) - pos;
		}
		tmp = size - len;
		tmp = min_t(u16, tmp, available);
		bes_dbg(BES_DBG_SPI, "%s size=%u len=%u pos=%u ava=%u tmp=%u\n", __func__,
				size, len, pos, available, tmp);
		memcpy(dst, buf + len, tmp);
		len += tmp;
		pos += tmp;
	}
}

static int bes_mspi_tx_aggr_try_add(struct bes_spi *bes_spi, const struct tx_packet *pkt, bool data)
{
	u32 size = 0;
	int i;
	struct tx_buffer *tx_buf;

	if (bes_spi->aggr_copy_num >= bes_spi->mspi_param.aggr_num)
		return -1;

	for (i = 0; i < pkt->cnt; i++) {
		list_for_each_entry(tx_buf, &pkt->list[i], hdr) {
			size += tx_buf->size;
		}
		size = BES_HOST_SPI_ALIGN(size);
	}

	BUG_ON(size > bes_spi->mspi_param.bufsize);
	if (bes_spi->aggr_copy_len + size > bes_spi->mspi_param.bufsize)
		return -2;

	size = 0;
	for (i = 0; i < pkt->cnt; i++) {
		list_for_each_entry(tx_buf, &pkt->list[i], hdr) {
			bes_mspi_aggr_copy(bes_spi, tx_buf->buf, tx_buf->size);
			size += tx_buf->size;
		}
		bes_spi->aggr_copy_num ++;
		bes_spi->aggr_copy_len += BES_HOST_SPI_ALIGN(size);
		bes_dbg(BES_DBG_SPI, "%s i=%d txnow=%d size=%u aggr num=%u len=%u\n", __func__, i, pkt->tx_now,
			size, bes_spi->aggr_copy_num, bes_spi->aggr_copy_len);
		if (data)
			bes_psa_free_tx_buffer((struct list_head *)&pkt->list[i]);
	}
	return 0;
}

static int bes_mspi_tx_aggr_finish(struct bes_spi *bes_spi)
{
	int ret;
	struct bes_spi_transfer_t *xfer;

	bes_dbg(BES_DBG_SPI, "%s, aggr %u %u", __func__, bes_spi->aggr_copy_num, bes_spi->aggr_copy_len);
	xfer = bes_mspi_aggr_build(bes_spi);
	if (WARN_ON(!xfer)) {
		ret = -ENOMEM;
		goto out;
	}
	ret = bes_mspi_exchange(bes_spi->spi, xfer);
out:
	bes_spi->aggr_copy_len = 0;
	bes_spi->aggr_copy_num = 0;
	return ret;
}

static int bes_mspi_tx_aggr(struct bes_spi *bes_spi, const struct tx_packet *pkt, bool data)
{
	int ret = 0;

	ret = bes_mspi_tx_aggr_try_add(bes_spi, pkt, data);
	if (ret) {
		// tx_aggr add failed, finish aggr and try to add again
		ret = bes_mspi_tx_aggr_finish(bes_spi);
		if (ret)
			goto out;
		ret = bes_mspi_tx_aggr_try_add(bes_spi, pkt, data);
		WARN_ON(ret);
	}

	if (pkt->tx_now || (bes_spi->aggr_copy_num >= bes_spi->mspi_param.aggr_num || bes_spi->aggr_copy_len >= bes_spi->mspi_param.bufsize))
		ret = bes_mspi_tx_aggr_finish(bes_spi);
out:
	return ret;
}

static int bes_mspi_msg_push(void *data_buf, u32 size)
{
	struct ipc_header *ipc_hdr;
	struct tx_packet pkt;
	struct tx_buffer txbuf;
	struct bes_spi *bes_spi = (struct bes_spi *)g_rwnx_plat.priv;

	bes_dbg(BES_DBG_SPI, "%s buf=%p size=%u\n", __func__, data_buf, size);
	ipc_hdr = (struct ipc_header *)data_buf;
	ipc_hdr->size = size;

	memset(&pkt, 0, sizeof(struct tx_packet));
	pkt.type = CONN_MSG;
	pkt.cnt = 1;
	pkt.tx_now = 1;
	INIT_LIST_HEAD(&pkt.list[0]);
	memset(&txbuf, 0, sizeof(struct tx_buffer));
	txbuf.buf = data_buf;
	txbuf.size = size;
	INIT_LIST_HEAD(&txbuf.hdr);
	list_add_tail(&txbuf.hdr, &pkt.list[0]);

	return bes_mspi_tx_aggr(bes_spi, &pkt, false);

	return 0;
}

static int bes_mspi_data_push(const struct tx_packet *pkt)
{
	int ret;
	struct bes_spi *bes_spi = (struct bes_spi *)g_rwnx_plat.priv;

	BUG_ON(pkt->cnt != 1);
	BUG_ON(pkt->list[0].next != pkt->list[0].prev);

	bes_dbg(BES_DBG_SPI, "%s cnt=%d tx=%d\n", __func__, pkt->cnt, pkt->tx_now);

	if (pkt->is_amsdu) {
		BUG_ON(1);
#ifdef CONFIG_RWNX_AMSDUS_TX
		u8 i;

		for (i = 0; i < pkt->cnt; i++) {
			int ret = bes_mspi_send_one_packet(bes_spi, (struct list_head *)&pkt->list[i]);
			if (WARN_ON(ret))
				return ret;
		}
		return 0;
#else
		return -EINVAL;
#endif // CONFIG_RWNX_AMSDUS_TX
	} else {
		ret = bes_mspi_tx_aggr(bes_spi, pkt, true);
		if (WARN_ON(ret))
			return ret;
	}
	return 0;
}

static void bes_wifi_normal_rx_handler(struct rwnx_plat *rwnx_plat)
{
	struct sk_buff *skb = NULL;

	while (!skb_queue_empty(&rwnx_plat->rx_pri_q) || !skb_queue_empty(&rwnx_plat->rx_normal_q)) {
		// Handle rx msg
		while (!skb_queue_empty(&rwnx_plat->rx_pri_q)) {
			skb = skb_dequeue(&rwnx_plat->rx_pri_q);
			if (skb)
				bes_psa_rx_handler(rwnx_plat, skb);
			else
				break;
		}

		// Handle rx data
		if (!skb_queue_empty(&rwnx_plat->rx_normal_q)) {
			skb = skb_dequeue(&rwnx_plat->rx_normal_q);
			if (skb) {
				bes_psa_rx_handler(rwnx_plat, skb);
			}
		}
	}
}

static void bes_wifi_tx_loop_rx_handler(struct rwnx_plat *self)
{
	struct sk_buff *skb = NULL;

	while (!skb_queue_empty(&self->rx_pri_q)) {
		skb = skb_dequeue(&self->rx_pri_q);
		if (skb)
			dev_kfree_skb(skb);
		else
			break;
	}
	while (!skb_queue_empty(&self->rx_normal_q)) {
		skb = skb_dequeue(&self->rx_normal_q);
		if(skb)
			dev_kfree_skb(skb);
		else
			break;
	}

	while (!skb_queue_empty(&self->tx_loop.rx_queue)) {
		skb = bes_tx_loop_get_skb(self);
		if (skb)
			bes_psa_rx_handler(self, skb);
		else
			break;
	}
}

static void bes_bus_rx_work(struct work_struct *work)
{
	struct rwnx_hw *rwnx_hw = NULL;
	struct rwnx_plat *rwnx_plat = container_of(work, struct rwnx_plat, bus_rx_work);
	int wifi_rx = 0;

	bes_dbg(BES_DBG_PSA, "%s\n", __func__);

	while (1) {
		wifi_rx = atomic_xchg(&rwnx_plat->wifi_rx, 0);
		bes_dbg(BES_DBG_PSA, "wifi_rx=%d\n", wifi_rx);
		if (wifi_rx) {
			if (bes_tx_loop_is_enabled(rwnx_plat)) {
				bes_wifi_tx_loop_rx_handler(rwnx_plat);
			} else {
				bes_wifi_normal_rx_handler(rwnx_plat);
			}
		} else {
			break;
		}

		rwnx_hw = rwnx_plat->drvdata;
		if (rwnx_plat->enabled && rwnx_hw &&
		    bes_pwr_wifi_is_wake(rwnx_hw) &&
		    spin_trylock_bh(&rwnx_hw->tx_lock)) {
			rwnx_hwq_process_all(rwnx_hw);
			spin_unlock_bh(&rwnx_hw->tx_lock);
		}
	}
}

static int bes_mspi_rx_handler(void)
{
	u16 len, blksize;
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct spi_device *spi = (struct spi_device *)rwnx_plat->spi_dev;
	struct bes_spi *bes_spi = spi_get_drvdata(spi);
	unsigned long start_recv;
	int ret;

	blksize = bes_spi->mspi_param.blksize ?  bes_spi->mspi_param.blksize : bes_mspi_default_blksize;

	start_recv = jiffies;
	ret = rwnx_plat->clr_int(spi, &len);
	// if (WARN_ON(ret)) {
	if (ret) {
		bes_err(BES_DBG_SPI, "%s clr int err=%d\n", __func__, ret);
		goto err;
	}

	do {
		if (!len)
			break;
		ret = rwnx_plat->data_get(rwnx_plat->spi_dev, len);
		// if (WARN_ON(ret)) {
		if (ret) {
			bes_err(BES_DBG_SPI, "%s read error=%d\n", __func__, ret);
			goto err;
		}

		bes_dbg(BES_DBG_SPI, "%s stage=%d aggr num=%u len=%u rxpos=%u blksize=%u\n", __func__, bes_spi->rx_stage, bes_spi->rx_aggr_num,
				bes_spi->rx_aggr_len, bes_spi->rx_pos, blksize);

		if (bes_spi->rx_stage == FIRST_DATA) {
			len = 0;
		} else if (bes_spi->rx_stage == SECOND_DATA) {
			len = bes_spi->rx_aggr_len - blksize + sizeof(struct mspi_aggr_header_t) * 2;
		} else if (bes_spi->rx_stage == FULL_DATA) {
			if (bes_spi->rx_aggr_len > blksize - sizeof(struct mspi_aggr_header_t))
				len = bes_spi->rx_aggr_len + sizeof(struct mspi_aggr_header_t) * 2;
			else
				len = blksize;
		} else {
			BUG_ON(1);
			len = 0;
		}
	} while(time_after(start_recv + HZ / 2, jiffies));

	bes_mspi_recv_packets(bes_spi);

	return 0;
err:
	if (ret) {
		if ((mspi_rx_error_cnt > MSPI_XFER_ERR_MAX_CNT) ||
			(mspi_rx_crc_err_cnt > MSPI_XFER_ERR_MAX_CNT)) {
			mspi_rx_error_cnt = 0;
			mspi_rx_crc_err_cnt = 0;
			bes_tx_loop_force_close(rwnx_plat, true);
		}
	}

	return -EFAULT;
}

static int bes_mspi_rx_thread(void *data)
{
	struct rwnx_plat *self = (struct rwnx_plat *)data;
	struct spi_device *spi = (struct spi_device *)self->spi_dev;
	struct bes_spi *bes_spi = spi_get_drvdata(spi);
	int mspi_rx = 0;
	bool stop = false;
	unsigned long flags;

	/* Set thread priority */
#ifdef BES_RX_SCHED_PRIORITY
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0)
	struct sched_param param = {.sched_priority = BES_RX_SCHED_PRIORITY,};
	WARN_ON(sched_setscheduler(current, SCHED_FIFO, &param));
#else
	sched_set_fifo(current);
#endif
#endif

	while (1) {
		/* Wait rx/stop signal */
		wait_event(self->mspi_rx_wait_q, ({
					mspi_rx = atomic_xchg(&self->mspi_rx, 0);
					stop = kthread_should_stop();
					(mspi_rx || stop);
					}));

		/* Thread exit */
		if (stop)
			break;

		/* Process mspi rx */
		bes_mspi_rx_handler();

		/* Notify the PM core that processing of a wakeup event(rx pending) has ended.
		   And mark rx irq processed */
		spin_lock_irqsave(&self->run_flags_lock, flags);
		if (!!(self->run_flags & BIT(PLAT_RUN_FLAGS_BUS_RX_PENDING))) {
			enable_irq(bes_spi->irq);
			self->run_flags &= ~BIT(PLAT_RUN_FLAGS_BUS_RX_PENDING);
		}
		rwnx_platform_pm_relax(self, RX_WAKELOCK);
		spin_unlock_irqrestore(&self->run_flags_lock, flags);
	}

	return 0;
}

static int bes_bus_rx_init(struct rwnx_plat *rwnx_plat)
{
	int ret;
	struct bes_spi *bes_spi = spi_get_drvdata(rwnx_plat->spi_dev);
	struct task_struct *tmp_thread;

	tmp_thread = kthread_run(bes_mspi_rx_thread, rwnx_plat, "bes_rx_thread");
	if (IS_ERR(tmp_thread)) {
		ret = PTR_ERR(tmp_thread);
		bes_err(BES_DBG_SPI, "%s: Failed to start kthread (%d)\n", __func__, ret);
		return ret;
	}

	rwnx_plat->bus_rx_thread = tmp_thread;

	rwnx_plat->bus_rx_wq = alloc_workqueue("bes_spi", WQ_MEM_RECLAIM | WQ_HIGHPRI | WQ_CPU_INTENSIVE, 1);
	if (WARN_ON(!rwnx_plat->bus_rx_wq))
		goto  alloc_workqueue_fail;

	INIT_WORK(&rwnx_plat->bus_rx_work, bes_bus_rx_work);

	ret = request_threaded_irq(bes_spi->irq, NULL, bes_mspi_irq_handler,
				   IRQF_TRIGGER_HIGH | IRQF_ONESHOT, "bes_mspi_irq", rwnx_plat->spi_dev);
	if (unlikely(ret)) {
		bes_err(BES_DBG_PSA, "%s request irq for slave spi failed(%d)\n", __func__, ret);
		goto  request_threaded_irq_fail;
	}

	enable_irq_wake(bes_spi->irq);

	return 0;

request_threaded_irq_fail:
	if (rwnx_plat->bus_rx_wq) {
		flush_workqueue(rwnx_plat->bus_rx_wq);
		destroy_workqueue(rwnx_plat->bus_rx_wq);
		rwnx_plat->bus_rx_wq = NULL;
	}

alloc_workqueue_fail:
	if (rwnx_plat->bus_rx_thread) {
		kthread_stop(rwnx_plat->bus_rx_thread);
		rwnx_plat->bus_rx_thread = NULL;
	}

	return -1;
}

static void bes_bus_rx_stop(struct rwnx_plat *rwnx_plat)
{
	struct bes_spi *bes_spi = spi_get_drvdata(rwnx_plat->spi_dev);

	free_irq(bes_spi->irq, rwnx_plat->spi_dev);

	if (rwnx_plat->bus_rx_thread) {
		kthread_stop(rwnx_plat->bus_rx_thread);
		rwnx_plat->bus_rx_thread = NULL;
	}

	if (rwnx_plat->bus_rx_wq) {
		flush_workqueue(rwnx_plat->bus_rx_wq);
		destroy_workqueue(rwnx_plat->bus_rx_wq);
		rwnx_plat->bus_rx_wq = NULL;
	}

	skb_queue_purge(&rwnx_plat->rx_pri_q);
	skb_queue_purge(&rwnx_plat->rx_normal_q);
}

int bes_mspi_init_plat_cfg(struct spi_device *spi)
{
	g_rwnx_plat.spi_dev = spi;
	g_rwnx_plat.clr_int = bes_mspi_clr_int;
	g_rwnx_plat.data_get = bes_mspi_data_get;
	//g_rwnx_plat.align_size = bes_bus_align_size;
	g_rwnx_plat.dev_status = 0;
	g_rwnx_plat.wakelock_flag = 0;
	g_rwnx_plat.dev = &spi->dev;
	g_rwnx_plat.run_flags = 0;
	spin_lock_init(&g_rwnx_plat.run_flags_lock);

	/* Init tx loop */
	bes_tx_loop_init(&g_rwnx_plat);
	spin_lock_init(&g_rwnx_plat.pwr_io_lock);
	atomic_set(&g_rwnx_plat.wifi_rx, 0);
	atomic_set(&g_rwnx_plat.mspi_rx, 0);
	skb_queue_head_init(&g_rwnx_plat.rx_pri_q);
	skb_queue_head_init(&g_rwnx_plat.rx_normal_q);
	skb_queue_head_init(&g_rwnx_plat.tx_q);
	init_waitqueue_head(&g_rwnx_plat.dev_stat_wait_q);
	init_waitqueue_head(&g_rwnx_plat.mspi_rx_wait_q);

	return 0;
}

static void bes_mspi_deinit_plat_cfg(void)
{
	struct rwnx_plat *rwnx_plat = (struct rwnx_plat *)&g_rwnx_plat;

	bes_tx_loop_deinit(rwnx_plat);
	rwnx_plat->spi_dev = NULL;
}

static inline void bes_mspi_wake_ctrl(struct rwnx_hw *rwnx_hw, bool wakeup)
{
	if (wakeup)
		rwnx_platform_wakeup_mcu(rwnx_hw->plat, IO_LOCK_SRC_WIFI_LP, false);
	else
		rwnx_platform_sleep_mcu(rwnx_hw->plat, IO_LOCK_SRC_WIFI_LP, false);
}

int bes_mspi_platform_init(void)
{
	spin_lock_init(&g_rwnx_plat.tx_lock);

	g_rwnx_plat.data_push = bes_mspi_data_push;
	g_rwnx_plat.msg_push = bes_mspi_msg_push;

	return rwnx_platform_init(&g_rwnx_plat, NULL);
}

int bes_mspi_platform_on(struct rwnx_hw *rwnx_hw)
{
	int ret = 0;
	struct rwnx_plat *rwnx_plat = rwnx_hw->plat;
	unsigned long flags;

	ret = bes_pwr_init(rwnx_hw, bes_mspi_wake_ctrl);
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

	if ((ret = bes_bus_rx_init(rwnx_plat)) < 0) {
		bes_err(BES_DBG_PSA, "%s bus rx thread create fail\n", __func__);
		goto err_a2e_msg_alloc;
	}

	if ((ret = bes_bus_tx_init(rwnx_plat)) < 0) {
		bes_err(BES_DBG_PSA, "%s bus tx thread create fail\n", __func__);
		goto err_rx_init;
	}

	rwnx_plat->enabled = true;

	return 0;

err_rx_init:
	bes_bus_rx_stop(rwnx_plat);
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

void bes_mspi_platform_off(struct rwnx_hw *rwnx_hw)
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
	bes_bus_rx_stop(rwnx_plat);

	if (rwnx_plat->a2e_msg) {
		kfree(rwnx_plat->a2e_msg);
		rwnx_plat->a2e_msg = NULL;
	}
}

static int bes_mspi_cmd_mem_init(struct bes_spi *bes_spi)
{
	u16 bufsize = bes_mspi_default_blksize;
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	u8 *buf = rwnx_plat->trx_buf;

	if (buf == NULL)
		return -ENOMEM;

	bes_spi->cmd_tx_buf = buf;
	bes_spi->cmd_rx_buf = buf + bufsize;

	return 0;
}

static void bes_mspi_cmd_mem_deinit(struct bes_spi *bes_spi)
{
	bes_spi->cmd_tx_buf = NULL;
	bes_spi->cmd_rx_buf = NULL;
}

static int bes_mspi_data_mem_init(struct bes_spi *bes_spi)
{
	u16 bufsize = bes_spi->mspi_param.bufsize, blksize = bes_spi->mspi_param.blksize;
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	u8 *buf = rwnx_plat->trx_buf;
	u32 buf_needs = 0;

	/* Pre-allocate resource fail */
	if (!buf)
		return -ENOMEM;

	/* Add aggr header size */
	bufsize += 3 * sizeof(struct mspi_aggr_header_t);

	/* Pre-allocate resource isn't enough */
	buf_needs = ((bufsize + blksize) << 1);
#ifdef PLAT_ROCKCHIP
	buf_needs += bufsize;
#endif
	if (buf_needs > BES_SPI_TRX_BUF_SIZE) {
		bes_err(BES_DBG_SPI, "%s total:%d need:%d\n",
				__func__, BES_SPI_TRX_BUF_SIZE, buf_needs);
		return -ENOMEM;
	}

	/* Assign data buffers */
	bes_spi->tx_buf = buf;
	buf += bufsize;
	bes_spi->rx_buf = buf;
	buf += bufsize;
#ifdef PLAT_ROCKCHIP
	bes_spi->rx_dummy_buf = buf;
	buf += bufsize;
#endif

	/* Assign command buffers */
	bes_spi->cmd_tx_buf = buf;
	bes_spi->cmd_rx_buf = buf + blksize;

	return 0;
}

static void bes_mspi_data_mem_deinit(struct bes_spi *bes_spi)
{
	bes_spi->tx_buf = NULL;
	bes_spi->rx_buf = NULL;
}

static int bes_mspi_probe(struct spi_device *spi)
{
	int ret = 0;
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_spi *bes_spi = (struct bes_spi *)rwnx_plat->priv;
	struct bes_spi_transfer_t *xfer;

	bes_power_ctrl_bus_probe_notify();

	device_init_wakeup(&spi->dev, true);
	memset(bes_spi, 0, sizeof(*bes_spi));

	spi->bits_per_word = 32;
	spi->mode = SPI_MODE_1;
	init_completion(&mspi_tx_ack_completion);
#if defined(CONFIG_OPLUS_PLATFORM) && defined(ADPT_GKI_KERNEL)
	oplus_spi_slave_ready_cb_register(spi, bes_mspi_ready);
#else
	spi->slave_ready_cb = bes_mspi_ready;
#endif

	bes_info(BES_DBG_SPI, "bes2610_slave_spi: Probe called (FUNC %p CS %d M %d BPW %d CLK %d)\n",
			spi, spi->chip_select, spi->mode, spi->bits_per_word,
			spi->max_speed_hz);

	bes_spi->tx_ready_gpio = of_get_named_gpio(spi->dev.of_node, "slave-tx-ready-gpio", 0);
	if (!gpio_is_valid(bes_spi->tx_ready_gpio)) {
		bes_err(BES_DBG_PSA, "%s find slave-tx-ready-gpio for spi failed(%d)\n", __func__, ret);
		ret = bes_spi->tx_ready_gpio;
		goto err_tx_ready_gpio;
	}
	ret = gpio_request(bes_spi->tx_ready_gpio, "slave-tx-ready-gpio");
	if (ret) {
		bes_err(BES_DBG_POWER_CTRL, "can't reqest slave-tx-ready-gpio:%d\n", ret);
		goto err_tx_ready_gpio;
	} else {
		gpio_direction_output(bes_spi->tx_ready_gpio, 0);
	}

	bes_spi->rx_ready_gpio = of_get_named_gpio(spi->dev.of_node, "slave-rx-ready-gpio", 0);
	if (!gpio_is_valid(bes_spi->rx_ready_gpio)) {
		bes_err(BES_DBG_PSA, "%s find slave-rx-ready-gpio for spi failed(%d)\n", __func__, ret);
		ret = bes_spi->rx_ready_gpio;
		goto err_rx_ready_gpio;
	}
	ret = gpio_request(bes_spi->rx_ready_gpio, "slave-rx-ready-gpio");
	if (ret) {
		bes_err(BES_DBG_POWER_CTRL, "can't reqest slave-rx-ready-gpio:%d\n", ret);
		goto err_rx_ready_gpio;
	} else {
		gpio_direction_output(bes_spi->rx_ready_gpio, 0);
	}

	bes_spi->irq_gpio = of_get_named_gpio(spi->dev.of_node, "master-tx-ind-gpio", 0);
	if (!gpio_is_valid(bes_spi->irq_gpio)) {
		bes_err(BES_DBG_SPI, "%s find master-tx-ind-gpio for spi failed(%d)\n", __func__, ret);
		ret = bes_spi->irq_gpio;
		goto err_irq_gpio;
	}
	bes_spi->irq = gpio_to_irq(bes_spi->irq_gpio);

#ifdef BES_SPI_GPIO_RESPONSE
	bes_spi->tx_done_gpio = of_get_named_gpio(spi->dev.of_node, "master-crc-ok-gpio", 0);
	if (!gpio_is_valid(bes_spi->tx_done_gpio)) {
		bes_err(BES_DBG_PSA, "%s find master-crc-ok-gpio for slave spi failed(%d)\n", __func__, ret);
		ret = bes_spi->tx_done_gpio;
		goto err_tx_done_gpio;
	}
	bes_spi->tx_done_irq = gpio_to_irq(bes_spi->tx_done_gpio);
	if ((ret = request_irq(bes_spi->tx_done_irq, bes_mspi_tx_done_irq,
			IRQF_TRIGGER_RISING | IRQF_ONESHOT, "master-crc-ok-gpio", bes_spi))) {
		bes_err(BES_DBG_SPI, "%s request master-crc-ok-gpio irq failed(%d)\n", __func__, ret);
		goto err_tx_done_irq;
	}

	bes_spi->rx_done_gpio = of_get_named_gpio(spi->dev.of_node, "slave-crc-ok-gpio", 0);
	if (!gpio_is_valid(bes_spi->rx_done_gpio)) {
		bes_err(BES_DBG_PSA, "%s find slave-crc-ok-gpio for slave spi failed(%d)\n", __func__, ret);
		ret = bes_spi->rx_done_gpio;
		goto err_rx_done_gpio;
	}
	ret = gpio_request(bes_spi->rx_done_gpio, "slave-crc-ok-gpio");
	if (ret) {
		bes_err(BES_DBG_POWER_CTRL, "can't reqest slave-crc-ok-gpio:%d\n", ret);
		goto err_rx_done_gpio;
	} else {
			gpio_direction_output(bes_spi->rx_done_gpio, 0);
	}
#endif

	msleep(1);

	if (bes_mspi_irq_poll(bes_spi)) {
		bes_err(BES_DBG_SPI, "bes_mspi_irq_poll() failed\n");
		goto err_wait_irq;
	}

#if 0
	while(1) {
		gpio_direction_output(bes_spi->tx_ready_gpio, 1);
		gpio_direction_output(bes_spi->tx_ind_gpio, 1);
		gpio_direction_output(bes_spi->rx_ready_gpio, 1);
		msleep(10);
		gpio_direction_output(bes_spi->tx_ready_gpio, 0);
		gpio_direction_output(bes_spi->tx_ind_gpio, 0);
		gpio_direction_output(bes_spi->rx_ready_gpio, 0);
		msleep(10);
	}
#endif

#ifdef PLAT_ROCKCHIP
	if ((ret = spi_setup(spi))) {
		bes_err(BES_DBG_SPI, "spi_setup() failed(%d)\n", ret);
		goto err_setup;
	}
#endif

	if ((ret = bes_mspi_cmd_mem_init(bes_spi))) {
		bes_err(BES_DBG_SPI, "spi cmd mem init failed(%d)\n", ret);
		goto err_cmd_mem_init;
	}

	spin_lock_init(&bes_spi->lock);
	init_waitqueue_head(&bes_spi->wq);
	bes_spi->claimed = 0;
	spi_set_drvdata(spi, bes_spi);
	bes_spi->spi = spi;

	spin_lock_init(&bes_spi->xfer_lock);
	INIT_LIST_HEAD(&bes_spi->xfering);
	skb_queue_head_init(&bes_spi->rx_packets);
	mspi_rx_error_cnt = 0;
	mspi_rx_crc_err_cnt = 0;

	xfer = bes_mspi_cmd_build(bes_spi);
	ret = bes_mspi_exchange(bes_spi->spi, xfer);
	bes_info(BES_DBG_SPI, "%s dummy read ret=%d\n", __func__, ret);
	if (ret)
		bes_spi->rx_seq ++;
	xfer = bes_mspi_cmd_build(bes_spi);
	ret = bes_mspi_exchange(bes_spi->spi, xfer);
	if (ret) {
		bes_err(BES_DBG_SPI, "spi read mspi init paramter failed(%d)\n", ret);
		goto err_cfg;
	}
	bes_dbg(BES_DBG_SPI, "get mspi init param:  deviceid=0x%x duplex=%d bufcnt=%u:%d bufsize=%u blksize=%u\n",
			bes_spi->mspi_param.device_id, bes_spi->mspi_param.duplex,
			bes_spi->mspi_param.tx_bufcnt, bes_spi->mspi_param.rx_bufcnt,
			bes_spi->mspi_param.bufsize, bes_spi->mspi_param.blksize);
	if (!bes_spi->inited) {
		WARN_ON(1);
		goto err_cfg;
	}

	if (BES_SPI_DEVICE_ID_BES == (bes_spi->mspi_param.device_id & BES_SPI_DEVICE_ID_MASK))
		bes_info(BES_DBG_SPI, "bes device id(0x%x)\n", bes_spi->mspi_param.device_id);
	else {
		bes_err(BES_DBG_SPI, "bes device id mismatch(0x%x)\n", bes_spi->mspi_param.device_id);
		goto err_cfg;
	}
	bes_sysfs_set_device_id(bes_spi->mspi_param.device_id & BES_SPI_SUB_DEVICE_ID_MASK);

	if ((ret = bes_mspi_data_mem_init(bes_spi))) {
		bes_err(BES_DBG_SPI, "%s spi data mem init failed", __func__);
		goto err_data_mem_init;
	}

	if ((ret = bes_mspi_init_plat_cfg(spi))) {
		bes_err(BES_DBG_SPI, "%s spi init_plat_cfg failed(%d)\n", __func__, ret);
		goto err_plat_cfg;
	}

	/* When dpd data has been obtained and only bt is enabled, wlan0 will not be registered */
	if (bes_power_ctrl_can_skip_platform_init(&g_rwnx_plat))
		goto out;

	/* Reset tx loop status */
	bes_tx_loop_status_clear(&g_rwnx_plat);

	/* master spi platform init */
	ret = bes_mspi_platform_init();
	if (ret)
		goto err_init_trx;

out:
	/* Notify power control that system is enabled */
	if ((ret = bes_power_ctrl_system_enabled(true, false)) < 0)
		goto err_init_trx;

	return 0;

err_init_trx:
	bes_mspi_deinit_plat_cfg();
err_plat_cfg:
	bes_mspi_data_mem_deinit(bes_spi);
err_data_mem_init:
err_cfg:
	bes_spi->spi = NULL;
	spi_set_drvdata(spi, NULL);
	bes_mspi_cmd_mem_deinit(bes_spi);
err_cmd_mem_init:
#ifdef PLAT_ROCKCHIP
err_setup:
#endif
err_wait_irq:
#ifdef BES_SPI_GPIO_RESPONSE
	gpio_free(bes_spi->rx_done_gpio);
err_rx_done_gpio:
	gpio_free(bes_spi->tx_done_gpio);
err_tx_done_irq:
	free_irq(bes_spi->tx_done_irq, bes_spi);
err_tx_done_gpio:
#endif
	gpio_free(bes_spi->irq_gpio);
err_irq_gpio:
	gpio_free(bes_spi->rx_ready_gpio);
err_rx_ready_gpio:
	gpio_free(bes_spi->tx_ready_gpio);
err_tx_ready_gpio:
	bes_set_boot_wait_state(BES_BOOT_WAIT_NONE);
	bes_power_ctrl_system_enabled(false, true);

	device_init_wakeup(&spi->dev, false);

	return ret;
}

static int bes_mspi_disconnect(struct spi_device *spidev)
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_spi *bes_spi = (struct bes_spi *)rwnx_plat->priv;

	bes_info(BES_DBG_SPI, "%s enter", __func__);

	device_init_wakeup(&spidev->dev, false);

	if (rwnx_plat->spi_dev)
		bes_mspi_deinit_plat_cfg();
	if (bes_spi->spi) {
#ifdef BES_SPI_GPIO_RESPONSE
		disable_irq(bes_spi->tx_done_irq);
		free_irq(bes_spi->tx_done_irq, bes_spi);
		gpio_free(bes_spi->rx_done_gpio);
		gpio_free(bes_spi->tx_done_gpio);
#endif
		bes_mspi_data_mem_deinit(bes_spi);
		bes_mspi_cmd_mem_deinit(bes_spi);
		gpio_free(bes_spi->irq_gpio);
		gpio_free(bes_spi->rx_ready_gpio);
		gpio_free(bes_spi->tx_ready_gpio);
		bes_spi->spi = NULL;
		spi_set_drvdata(spidev, NULL);
	}
	return 0;
}

static int bes_mspi_suspend(struct device *dev)
{
	int ret = 0;
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	unsigned long flags;
	bool rx_pending = false;
	bool tx_pending = false;
	bool tx_running = false;

	bes_info(BES_DBG_SPI, "%s enter. run_flags:0x%lx", __func__, rwnx_plat->run_flags);

	/* Mark bus enter suspend state */
	spin_lock_irqsave(&rwnx_plat->run_flags_lock, flags);
	rx_pending = !!(rwnx_plat->run_flags & BIT(PLAT_RUN_FLAGS_BUS_RX_PENDING));
	tx_pending = !!(rwnx_plat->run_flags & (BIT(PLAT_RUN_FLAGS_TX_MSG) | BIT(PLAT_RUN_FLAGS_TX_DATA)));
	tx_running = !!(rwnx_plat->run_flags & BIT(PLAT_RUN_FLAGS_BUS_TX_PENDING));
	if (!rx_pending && !tx_pending && !tx_running) {
		rwnx_plat->run_flags |= BIT(PLAT_RUN_FLAGS_BUS_SUSPEND);
	}
	spin_unlock_irqrestore(&rwnx_plat->run_flags_lock, flags);

	/* Reprocess rx pending irq */
	if (rx_pending) {
		atomic_inc(&rwnx_plat->mspi_rx);
		wake_up(&rwnx_plat->mspi_rx_wait_q);
		ret = -EBUSY;
	}

	/* Reprocess tx data/msg */
	if (tx_pending) {
		wake_up(&rwnx_plat->tx_wait_q);
		ret = -EBUSY;
	}

	/* Bus tx thread is running */
	if (tx_running)
		ret = -EBUSY;

	bes_info(BES_DBG_SPI, "%s exit. run_flags:0x%lx", __func__, rwnx_plat->run_flags);

	return ret;
}

static int bes_mspi_resume(struct device *dev)
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	unsigned long flags;
	bool rx_pending = false;
	bool tx_pending = false;

	bes_info(BES_DBG_SPI, "%s enter. run_flags:0x%lx", __func__, rwnx_plat->run_flags);

	/* Mark bus exit suspend state */
	spin_lock_irqsave(&rwnx_plat->run_flags_lock, flags);
	rwnx_plat->run_flags &= ~BIT(PLAT_RUN_FLAGS_BUS_SUSPEND);
	rx_pending = !!(rwnx_plat->run_flags & BIT(PLAT_RUN_FLAGS_BUS_RX_PENDING));
	tx_pending = !!(rwnx_plat->run_flags & (BIT(PLAT_RUN_FLAGS_TX_MSG) | BIT(PLAT_RUN_FLAGS_TX_DATA)));
	spin_unlock_irqrestore(&rwnx_plat->run_flags_lock, flags);

	/* Reprocess rx pending irq */
	if (rx_pending) {
		atomic_inc(&rwnx_plat->mspi_rx);
		wake_up(&rwnx_plat->mspi_rx_wait_q);
	}

	/* Reprocess tx data/msg */
	if (tx_pending)
		wake_up(&rwnx_plat->tx_wait_q);

	bes_info(BES_DBG_SPI, "%s exit. run_flags:0x%lx", __func__, rwnx_plat->run_flags);

	return 0;
}

static const struct dev_pm_ops bes_pm_ops = {
	.suspend = bes_mspi_suspend,
	.resume = bes_mspi_resume,
};

static struct spi_driver bes_mspi_driver = {
	.probe = bes_mspi_probe,
	.remove = bes_mspi_disconnect,
	.driver = {
		.name = "bes2610_wlan_spi",
		.of_match_table = of_match_ptr(bes_spi_dev_ids),
#ifdef CONFIG_PM
		.pm = &bes_pm_ops,
#endif
	}
};

static int bes_mspi_power_up(struct rwnx_plat *rwnx_plat)
{
	int ret = 0;

	bes_info(BES_DBG_SPI, "%s\n", __func__);

	WARN_ON((ret = bes_chip_power(rwnx_plat, false)));
	msleep(10);
	WARN_ON((ret |= bes_chip_power(rwnx_plat, true)));
	if (unlikely(ret))
		return ret;

	/**
	 * If remove() is not called by sdio_unregister_driver(),
	 * sdio_register_driver() cannot be called when entering again.
	 */
	if (!rwnx_plat->register_flag) {
		rwnx_plat->register_flag = true;
		bes_power_ctrl_start_bus_probe();
		ret = spi_register_driver(&bes_mspi_driver);
		if (ret) {
			rwnx_plat->register_flag = false;
			bes_power_ctrl_cancel_probe_work();
			bes_set_boot_wait_state(BES_BOOT_WAIT_NONE);
			return ret;
		}
	}

	return ret;
}

static int bes_mspi_power_down(struct rwnx_plat *rwnx_plat)
{
	int ret = 0;

	bes_info(BES_DBG_SPI, "%s %d\n", __func__, rwnx_plat->register_flag);

	/* sdio has been powered down, and return 0. */
	if (!rwnx_plat->register_flag)
		return 0;

	rwnx_plat->register_flag = false;

	spi_unregister_driver(&bes_mspi_driver);

	ret = bes_chip_power(rwnx_plat, false);

#ifdef BES_NO_REBOOT_CHG_FW
	bes_sysfs_go_back_signal_mode(false);
#endif

	bes_power_ctrl_system_enabled(false, false);

	return ret;
}

static bool bes_mspi_alloc_resource(struct rwnx_plat *rwnx_plat)
{
	int order = get_order(BES_SPI_TRX_BUF_SIZE);

#ifdef PLAT_ROCKCHIP
	rwnx_plat->trx_buf = (u8 *)__get_dma_pages(GFP_KERNEL|__GFP_RETRY_MAYFAIL, order);
#else
	rwnx_plat->trx_buf = (u8 *)__get_dma_pages(GFP_KERNEL|__GFP_RETRY_MAYFAIL, order);
#endif

	if (rwnx_plat->trx_buf == NULL) {
		bes_err(BES_DBG_SPI, "%s alloc res fail\n", __func__);
		return false;
	}

	return true;
}

static void bes_mspi_free_resource(struct rwnx_plat *rwnx_plat)
{
	int order = get_order(BES_SPI_TRX_BUF_SIZE);

	if (rwnx_plat->trx_buf == NULL)
		return;

#ifdef PLAT_ROCKCHIP
	free_pages((unsigned long)rwnx_plat->trx_buf, order);
#else
	free_pages((unsigned long)rwnx_plat->trx_buf, order);
#endif
}

/* Init Module function -> Called by insmod */
int bes_mspi_init(void)
{
	int ret;
	struct bes_power_state power_state;

	bes_dbg(BES_DBG_SPI, "%s enter\n", __func__);

#ifdef CONFIG_OPLUS_PLATFORM
	oplus_custom_init();
#endif

	/* Allocate resources */
	if (!bes_mspi_alloc_resource(&g_rwnx_plat))
		return -ENOMEM;

	/* Set pre_alloc to true to make wiphy only allocate once */
	g_rwnx_plat.pre_alloc = true;

	/* Allocate wiphy resource */
	ret = rwnx_cfg80211_preinit(&g_rwnx_plat);
	if (ret)
		goto mspi_alloc_res_err;

	/* Init power control */
	ret = bes_power_ctrl_mspi_init(&g_rwnx_plat);
	if (ret)
		goto cfg80211_pre_alloc_err;

	/* Register sysfs */
	ret = bes_sysfs_register(&g_rwnx_plat);
	if (ret)
		goto power_ctl_err;

	/* reset sysfs state */
	bes_sysfs_status_update(true);

	/* Get power state */
	bes_power_ctrl_get_power_state(&power_state);

	/* start probe when wifi is on */
	if (power_state.wifi_power) {
		ret = bes_mspi_power_up(&g_rwnx_plat);
		if (ret)
			goto mspi_register_err;
	}

	return ret;

mspi_register_err:
	bes_power_ctrl_cancel_probe_work();
	bes_sysfs_unregister();
power_ctl_err:
	bes_power_ctrl_mspi_deinit(&g_rwnx_plat);
cfg80211_pre_alloc_err:
	rwnx_cfg80211_post_deinit(&g_rwnx_plat);
mspi_alloc_res_err:
	bes_mspi_free_resource(&g_rwnx_plat);

	return ret;
}

/* Called at Driver Unloading */
void bes_mspi_exit(void)
{
	struct bes_power_ctrl *bes_power_ctrl = priv_to_power_ctrl(g_rwnx_plat.power_priv);
	/* FIXME -- this won't support multiple devices */
	bes_dbg(BES_DBG_SPI, "%s enter\n", __func__);

	bes_power_ctrl_wifi_power(bes_power_ctrl, false);
#if (CONFIG_BES_POWER_CTRL_BT_SELF_MANAGED == 0)
	bes_power_ctrl_bt_power(bes_power_ctrl, false);
#endif

	bes_sysfs_unregister();
	bes_power_ctrl_mspi_deinit(&g_rwnx_plat);
	rwnx_cfg80211_post_deinit(&g_rwnx_plat);
	g_rwnx_plat.pre_alloc = false;
	bes_mspi_free_resource(&g_rwnx_plat);

	bes_info(BES_DBG_SPI, "%s exit\n", __func__);
}
