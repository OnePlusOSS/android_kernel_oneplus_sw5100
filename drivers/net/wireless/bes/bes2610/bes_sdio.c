/*
 * @file bes_sdio.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/delay.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/version.h>
#include "bes_sysfs.h"
#include "rwnx_platform.h"
#include "bes_pwr.h"
#include "bes_wowlan.h"
#include "bes_sdio.h"

#ifdef PLAT_ROCKCHIP
#include <linux/rfkill-wlan.h>
#endif

#ifdef FW_DOWNLOAD_BY_SDIO
#include "bes_fw_common.h"
#include <linux/gpio.h>
#endif

#include "bes_log.h"
#include "rwnx_msg_tx.h"
#include "bes_psa.h"
#include "bes_tx_loop.h"
#include "bes_linkinfo.h"

static HI_CHAN sdio_chan = {0, 0};

static const struct sdio_device_id bes_sdio_ids[] = {
	{SDIO_DEVICE(0xbe57, 0x2005)},
	{SDIO_DEVICE(0xbe57, 0x2007)},
	{SDIO_DEVICE(0xbe57, 0x207a)},
	{/* end: all zeroes */},
};

void bes_io_bus_status_dump(void)
{
}

int bes_load_firmware_sdio(struct sbus_priv *priv, struct rwnx_plat *rwnx_plat);

extern struct rwnx_plat g_rwnx_plat;
static int bes_sdio_power_down(struct rwnx_plat *rwnx_plat);
static int bes_sdio_power_up(struct rwnx_plat *rwnx_plat);
extern u32 bes_bus_align_size(struct sdio_func *func, u32 size);
extern int bes_bus_clr_int(struct sdio_func *func, u32 *lenPtr);
#ifdef SDIO_AGGR
static int bes_wifi_rx_split(struct rwnx_plat *self, const u8 *buf, u32 buf_len);
#endif

#define AW_SDIOID 1
#ifdef PLAT_ROCKCHIP
extern int rockchip_wifi_set_carddetect(int val);
#elif defined PLAT_QIHUA
extern void sunxi_mmc_rescan_card(unsigned id);
#endif

#ifdef SDIO_AGGR
#ifdef CONFIG_TRX_COPY_LESS
static struct sk_buff *bes_build_rx_skb(const void *data, int linear_len, int data_len,
					    int buf_len)
{
	int len = min_t(int, data_len, linear_len);
	struct sk_buff *skb;

	skb = bes_alloc_skb(len, GFP_KERNEL);
	if (!skb)
		return NULL;

	skb_put_data(skb, data, len);
	if (data_len > len) {
		struct page *page;

		data += len;
		page = virt_to_head_page(data);
		skb_add_rx_frag(skb, skb_shinfo(skb)->nr_frags,
				page, data - page_address(page),
				data_len - len, buf_len);
		get_page(page);
	}

	return skb;
}
#endif /* CONFIG_TRX_COPY_LESS */

static int bes_sdio_e2a_data_pre_handler(struct rwnx_plat *self, const u8 *buf)
{
	struct hw_rxhdr *hw_rxhdr = NULL;
	struct sk_buff *temp = NULL;
	u16 real_len = 0;

	hw_rxhdr = (struct hw_rxhdr *)(buf + BES_PSA_E2A_HDR_LEN);
	real_len = BES_PSA_DATA_RX_HEADER_LEN + le32_to_cpu(hw_rxhdr->hwvect.len);
#ifdef CONFIG_TRX_COPY_LESS
	if (!hw_rxhdr->flags_is_monitor && hw_rxhdr->flags_is_80211_mpdu) {
		// mgmt frame use linear skb
		temp = bes_build_rx_skb(buf, real_len, real_len, 0);
	} else {
		// use nonlinear skb
		temp = bes_build_rx_skb(buf, BES_PSA_LINEAR_SKB_THRESHOLD, real_len, round_up(real_len, 4));
	}
	if (WARN_ON(!temp))
		return -ENOMEM;

	skb_queue_tail(&self->rx_normal_q, temp);
#else
	temp = bes_dev_alloc_skb(real_len);
	if (WARN_ON(!temp))
		return -ENOMEM;

	skb_put(temp, real_len);
	memcpy(temp->data, buf, real_len);
	bes_psa_rx_handler(self, temp);
#endif

	return 0;
}

static int bes_sdio_get_aggr_data(struct rwnx_plat *self, u32 real_len, u32 align_len)
{
	int ret;
#ifdef CONFIG_TRX_COPY_LESS
	struct page *page;
	u8 *buf;
	page = bes_dev_alloc_pages(align_len, GFP_KERNEL);
	if (WARN_ON(!page))
		return -ENOMEM;

	buf = page_address(page);
	ret = self->data_get(self->func, buf, real_len);
	if (!ret) {
		bes_wifi_rx_split(self, buf, align_len);
		atomic_inc(&self->wifi_rx);
		wake_up(&self->wifi_rx_wait_q);
	}
	put_page(page);
	return ret;
#else
	struct sk_buff *skb = bes_dev_alloc_skb(align_len);
	if (WARN_ON(!skb))
		return -ENOMEM;

	skb_put(skb, align_len);
	ret = self->data_get(self->func, (u8 *)skb->data, real_len);
	if (!ret) {
		skb_queue_tail(&self->rx_normal_q, skb);
		atomic_inc(&self->wifi_rx);
		wake_up(&self->wifi_rx_wait_q);
	} else {
		dev_kfree_skb(skb);
	}
	return ret;
#endif
}
#endif /* SDIO_AGGR */

void bes_sdio_irq_handler(struct sdio_func *func)
{
	struct rwnx_plat *self = &g_rwnx_plat;
	u32 len = 0, align_len;
	int ret;
	bool pri;

	// get rx len and clear interrupt
	ret = self->clr_int(self->func, &len);
	if (ret) {
		bes_err(BES_DBG_PSA, "%s clr int err=%d\n", __func__, ret);
		return;
	}

	if (!(len & BES_TX_LEN_MASK)) {
		return;
	}
	pri = (len & QUEUE_PRI_MASK) ? 1 : 0;
	len &= BES_TX_LEN_MASK;
	align_len = self->align_size(self->func, len);
	bes_dbg(BES_DBG_PSA, "%s len=%d, align_len=%d\n", __func__, len, align_len);

	// get rx data
#ifdef SDIO_AGGR
	if (pri) {
#else
	if (1) {
#endif
		// get non_aggr data
		struct sk_buff *skb = bes_dev_alloc_skb(align_len);
		if (WARN_ON(!skb))
			return;

		skb_put(skb, align_len);
		ret = self->data_get(self->func, (u8 *)skb->data, len);
		if (!ret) {
			skb_queue_tail(pri ? &self->rx_pri_q : &self->rx_normal_q, skb);
			atomic_inc(&self->wifi_rx);
			wake_up(&self->wifi_rx_wait_q);
		} else {
			dev_kfree_skb(skb);
		}
#ifdef SDIO_AGGR
	} else {
		// get aggr data
		ret = bes_sdio_get_aggr_data(self, len, align_len);
#endif
	}

	if (ret) {
		bes_err(BES_DBG_MSG_TX, "%s read error=%d\n", __func__, ret);
		bes_tx_loop_force_close(self, true);
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

static void bes_wifi_normal_rx_handler(struct rwnx_plat *self)
{
	struct sk_buff *skb = NULL;

	while (!skb_queue_empty(&self->rx_pri_q) || !skb_queue_empty(&self->rx_normal_q)) {
		// Handle rx msg
		while (!skb_queue_empty(&self->rx_pri_q)) {
			skb = skb_dequeue(&self->rx_pri_q);
			if (skb)
				bes_psa_rx_handler(self, skb);
			else
				break;
		}

		// Handle rx data
		if (!skb_queue_empty(&self->rx_normal_q)) {
			skb = skb_dequeue(&self->rx_normal_q);
			if (skb) {
#if defined(SDIO_AGGR) && !defined(CONFIG_TRX_COPY_LESS)
				bes_wifi_rx_split(self, skb->data, skb->len);
				dev_kfree_skb(skb);
#else
				bes_psa_rx_handler(self, skb);
#endif
			}
		}
	}
}

static int bes_sdio_rx_thread(void *data)
{
	struct rwnx_hw *rwnx_hw = NULL;
	struct rwnx_plat *self = (struct rwnx_plat *)data;
	int wifi_rx = 0;
	bool stop = false;
#ifdef BES_RX_SCHED_PRIORITY
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 9, 0)
	struct sched_param param = {.sched_priority = BES_RX_SCHED_PRIORITY,};
	WARN_ON(sched_setscheduler(current, SCHED_FIFO, &param));
#else
	sched_set_fifo(current);
#endif
#endif
	bes_dbg(BES_DBG_PSA, "%s\n", __func__);

	while (1) {
		wait_event(self->wifi_rx_wait_q, ({
			   wifi_rx = atomic_xchg(&self->wifi_rx, 0);
			   stop = kthread_should_stop();
			   (wifi_rx || stop);
			}));

		if (stop)
			break;

		bes_dbg(BES_DBG_PSA, "wifi_rx=%d\n", wifi_rx);
		if (wifi_rx) {
			if (bes_tx_loop_is_enabled(self)) {
				bes_wifi_tx_loop_rx_handler(self);
			} else {
				bes_wifi_normal_rx_handler(self);
			}
		}

		rwnx_hw = self->drvdata;
		if (rwnx_hw && spin_trylock_bh(&rwnx_hw->tx_lock)) {
			if (bes_pwr_wifi_is_wake(rwnx_hw))
				rwnx_hwq_process_all(rwnx_hw);
			spin_unlock_bh(&rwnx_hw->tx_lock);
		}
	}
	return 0;
}

static int bes_sdio_rx_init(struct rwnx_plat *rwnx_plat)
{
	int ret = 0;
	struct task_struct *tmp_thread;

	tmp_thread = kthread_run(bes_sdio_rx_thread, rwnx_plat, "bes_rx_thread");
	if (IS_ERR(tmp_thread)) {
		ret = PTR_ERR(tmp_thread);
		bes_err(BES_DBG_SDIO, "%s: Failed to start kthread (%d)\n", __func__, ret);
		return ret;
	}

	rwnx_plat->bus_rx_thread = tmp_thread;

	return ret;
}

static void bes_sdio_rx_stop(struct rwnx_plat *rwnx_plat)
{
	if (rwnx_plat->bus_rx_thread) {
		kthread_stop(rwnx_plat->bus_rx_thread);
		rwnx_plat->bus_rx_thread = NULL;
	}
}

static int android_mmc_rescan_card(struct rwnx_plat *rwnx_plat, unsigned id)
{
	int ret = 0;
#if defined(PLAT_ROCKCHIP)
#ifdef CONFIG_BES_POWER_CTRL
	if ((ret = bes_chip_power(rwnx_plat, false)) < 0)
		return ret;
	if ((ret = bes_chip_power(rwnx_plat, true)) < 0)
		return ret;
	msleep(1000);
	bes_power_ctrl_start_bus_probe();
#endif
	rockchip_wifi_set_carddetect(id);
#elif defined PLAT_QIHUA
	sunxi_mmc_rescan_card(id);
#endif
	return ret;
}

static void bes_sdio_power_off_scancard(void)
{
#ifdef PLAT_ROCKCHIP
	rockchip_wifi_set_carddetect(0);
#endif
	bes_info(BES_DBG_SDIO, "%s: power down, rescan card\n", __FUNCTION__);
}

static int rwmx_sdio_chip_power_down(struct rwnx_plat *rwnx_plat)
{
	int ret = 0;

#if defined(PLAT_ROCKCHIP)
	ret = bes_chip_power(rwnx_plat, false);
#endif

	bes_sdio_power_off_scancard();
	bes_power_ctrl_set_scancard_time();

	return ret;
}

#ifdef CONFIG_BES_POWER_CTRL_GPIO
static int bes_sdio_plat_power(struct rwnx_plat *rwnx_plat, bool on)
{
	int ret = 0;

	if (on) {
		bes_power_ctrl_wait_scan_finish();
		ret = bes_sdio_power_up(rwnx_plat);
	} else {
		ret = bes_sdio_power_down(rwnx_plat);
	}

	return ret;
}

static void bes_sdio_off_work(struct work_struct *ws)
{
	bes_power_ctrl_do_system_close();
}

/**
 * When both wifi and bt are closed,
 * the probe function calls this interface to execute the disconnect method.
 */
void bes_power_ctrl_sdio_power_off(struct rwnx_plat *rwnx_plat)
{
	struct bes_power_state power_state;
	struct bes_sdio_power_ctrl *bes_sdio_power_ctrl;

	bes_sdio_power_ctrl = rwnx_plat->power_priv;

	bes_power_ctrl_get_power_state(&power_state);

	if (power_state.wifi_power || power_state.bt_power)
		return;

	schedule_work(&bes_sdio_power_ctrl->sdio_off_work);
}

int bes_power_ctrl_sdio_init(struct rwnx_plat *rwnx_plat)
{
	struct bes_sdio_power_ctrl *bes_sdio_power_ctrl;

	rwnx_plat->power_priv = bes_power_ctrl_gpio_register(rwnx_plat,
							     sizeof(struct bes_sdio_power_ctrl),
							     bes_sdio_plat_power);
	if (!rwnx_plat->power_priv) {
		bes_err(BES_DBG_SDIO, "%s %d, failed\n", __func__, __LINE__);
		return -ENOMEM;
	}

	bes_sdio_power_ctrl = rwnx_plat->power_priv;
	INIT_WORK(&bes_sdio_power_ctrl->sdio_off_work, bes_sdio_off_work);

	return 0;
}

void bes_power_ctrl_sdio_deinit(struct rwnx_plat *rwnx_plat)
{
	struct bes_sdio_power_ctrl *bes_sdio_power_ctrl = rwnx_plat->power_priv;

	cancel_work_sync(&bes_sdio_power_ctrl->sdio_off_work);

	bes_power_ctrl_gpio_unregister(rwnx_plat->power_priv);
}
#endif /* CONFIG_BES_POWER_CTRL_GPIO */

int bes_sdio_reg_read(struct sdio_func *func, u32 reg, volatile void *dst, int count)
{
	int ret = 0;
	if (count <= 0 || !dst)
		return -EINVAL;

	sdio_claim_host(func);
	while (count && !ret) {
		*(u8 *)dst = sdio_readb(func, reg, &ret);
		dst++;
		reg++;
		count--;
	}
	sdio_release_host(func);
	return ret;
}


int bes_sdio_reg_write(struct sdio_func *func, u32 reg, const void *src, int count)
{
	int ret = 0;
	if (count <= 0 || !src)
		return -EINVAL;

	sdio_claim_host(func);
	while (count && !ret) {
		sdio_writeb(func, *(u8 *)src, reg, &ret);
		src++;
		reg++;
		count--;
	}
	sdio_release_host(func);
	return ret;
}

#if (WFROM_VER == 1)
static int bes_sdio_trans_busy_check(struct sdio_func *func)
{
	int ret;
	u8 tx_busy;
	u8 busy_retry = 0;
	while (busy_retry++ < BES_SDIO_BUSY_MAX_RETRY) {
		ret = bes_sdio_reg_read(func, BES_BUSY_REG_ID, &tx_busy, 1);
		if (ret) {
			bes_err(BES_DBG_SDIO, "%s:reg read error:%d\n", __func__, ret);
			return ret;
		}
		if (tx_busy == 0xAA) {
			return 0;
		}
		bes_info(BES_DBG_SDIO, "%s:tx_busy=0x%x,\n", __func__, tx_busy);
		msleep(1);
	}
	return -EBUSY;
}
static bool _sdio_host_rx;
#endif

int bes_sdio_queue_mode_read(struct sdio_func *func, u8 *data_buf, u32 size)
{
	int ret, retry;
	u32 final_size, addr, reg;
	HI_CHAN *pQid = &sdio_chan;

	addr = size;
	reg = SDIO_QUEUE_RTG_ADDR17BIT(pQid->Prog, addr);
	final_size = sdio_align_size(func, size);
	bes_dbg(BES_DBG_SDIO, "%s:reg=0x%x, sz=0x%x,fsz=0x%x,\n", __func__, reg, size, final_size);

	retry = 0;
	sdio_claim_host(func);
#if (WFROM_VER == 1)
	// host rx don't need check busy in practical test
	_sdio_host_rx = true;
#endif
	while (retry < BES_SDIO_TRANS_MAX_RETRY) {
		ret = sdio_memcpy_fromio(func, data_buf, reg, final_size);
		if (ret) {
			retry++;
			bes_err(BES_DBG_SDIO, "%s error :[%d]\n", __func__, ret);
		} else {
			break;
		}
	}
	sdio_release_host(func);

	if (ret) {
		bes_err(BES_DBG_SDIO, "receive data err:%d, exit\n", ret);
	}
	(pQid->Prog)++;
	return ret;
}

static int bes_sdio_queue_mode_write(struct sdio_func *func, enum conn_type type, u8 *data_buf, u32 size, u32 aggr_cnt)
{
	int ret;
	u32 reg, len, final_size;
	HI_CHAN *pQid = &sdio_chan;

	bes_dbg(BES_DBG_SDIO, "%s, type:%u, size:%u, aggr_cnt:%u", __FUNCTION__, type, size, aggr_cnt);

	if (aggr_cnt) {
		BUG_ON(type != CONN_DATA);
		len = size * aggr_cnt;
		reg = SDIO_QUEUE_WTG_AGGR_ADDR17BIT(pQid->Cmpl, size / BES_SDIO_AGGR_BLOCK_SIZE, aggr_cnt);
	} else {
		len = (size & BES_TX_LEN_MASK);
		reg = SDIO_QUEUE_WTG_ADDR17BIT(pQid->Cmpl, type, len);
	}
	final_size = sdio_align_size(func, len);
	bes_dbg(BES_DBG_SDIO, "%s:reg=0x%x, size=0x%x, fsz=0x%x\n", __func__, reg, size, final_size);
	sdio_claim_host(func);
#if (WFROM_VER == 1)
	if (_sdio_host_rx) {
		ret = bes_sdio_trans_busy_check(func);
		if (ret) {
			bes_err(BES_DBG_SDIO, "%s:busy check fail:%d\n", __func__, ret);
			sdio_release_host(func);
			return ret;
		}
	}
	_sdio_host_rx = false;
#endif
	ret = sdio_memcpy_toio(func, reg, data_buf, final_size);
	sdio_release_host(func);
	if (ret) {
		bes_err(BES_DBG_SDIO, "%s:send data err:%d, exit\n", __func__, ret);
	}
	(pQid->Cmpl)++;
	return ret;
}

#ifdef SDIO_AGGR
#ifdef CONFIG_SDIO_HOST_ADMA
static int bes_sdio_rw_extended(struct sdio_func *func, struct sg_request *req)
{
	struct mmc_request mmc_req = {0};
	struct mmc_command cmd = {0};
	struct mmc_data data = {0};
	struct scatterlist *sg = g_rwnx_plat.sg;
	u32 addr, blksz, blocks, pads;
	u32 sg_len = 0;
	int err, i = 0;
	HI_CHAN *pQid = &sdio_chan;
	struct tx_buffer *tx_buf = NULL;

	if (req->cnt == 0 || req->cnt > SG_MAX_CNT || req->len == 0)
		return -EINVAL;

	if (req->rw == SG_READ)
		addr = SDIO_QUEUE_RTG_ADDR17BIT(pQid->Prog, req->cnt);
	else if (req->rw == SG_WRITE)
		addr = SDIO_QUEUE_WTG_AGGR_ADDR17BIT(pQid->Cmpl, req->peer_len / BES_SDIO_AGGR_BLOCK_SIZE, req->cnt);
	else
		return -EINVAL;

	if ((req->cnt == 1) && (req->len < func->cur_blksize) && (req->sg_len == 1)) {
		// only one msdu
		blksz = req->len;
		blocks = 0;
	} else {
		blksz = func->cur_blksize;
		blocks = (req->len + func->cur_blksize - 1) / func->cur_blksize;
		pads = blocks * func->cur_blksize - req->len;
		if (pads) {
			// tx add padding after computing addr
			if (req->rw == SG_WRITE) {
				req->len += pads;

				tx_buf = list_last_entry(&req->list, struct tx_buffer, hdr);
				BUG_ON(!tx_buf);
				tx_buf->size += pads;
			} else {
				return -ENOTSUPP;
			}
		}
	}

	cmd.opcode = SD_IO_RW_EXTENDED;
	cmd.arg = (req->rw == SG_WRITE) ? 0x80000000 : 0x00000000;
	cmd.arg |= func->num << 28;
	cmd.arg |= 0x04000000; // default incr_addr
	cmd.arg |= addr << 9;
	if (blocks == 0)
		cmd.arg |= (blksz == 512) ? 0 : blksz;	/* byte mode */
	else
		cmd.arg |= 0x08000000 | blocks;		/* block mode */
	cmd.flags = MMC_RSP_SPI_R5 | MMC_RSP_R5 | MMC_CMD_ADTC;

	data.blksz = blksz;
	/* Code in host drivers/fwk assumes that "blocks" always is >=1 */
	data.blocks = blocks ? blocks : 1;
	data.flags = (req->rw == SG_WRITE) ? MMC_DATA_WRITE : MMC_DATA_READ;

	sg_init_table(sg, req->sg_len);
	list_for_each_entry(tx_buf, &req->list, hdr) {
		WARN_ON(!tx_buf || (tx_buf->buf == NULL) || (tx_buf->size == 0));

		bes_dbg(BES_DBG_SDIO, "sdio %s sg%d addr 0x%lx size %u\n",
				(req->rw == SG_WRITE) ? "tx" : "rx", i++, (size_t)tx_buf->buf, tx_buf->size);

		WARN_ON(sg_len >= SG_MAX_SEGS);
		sg_set_buf(&sg[sg_len++], tx_buf->buf, tx_buf->size);
	}

	data.sg = sg;
	data.sg_len = sg_len;
	WARN_ON(sg_len != req->sg_len);

	mmc_req.cmd = &cmd;
	mmc_req.data = &data;

	sdio_claim_host(func);
	mmc_set_data_timeout(&data, func->card);
	mmc_wait_for_req(func->card->host, &mmc_req);
	sdio_release_host(func);

	if (cmd.error)
		err = cmd.error;
	else if (data.error)
		err = data.error;
	else if (cmd.resp[0] & R5_ERROR)
		err = -EIO;
	else if (cmd.resp[0] & R5_FUNCTION_NUMBER)
		err = -EINVAL;
	else if (cmd.resp[0] & R5_OUT_OF_RANGE)
		err = -ERANGE;
	else
		err = 0;

	if (err) {
		bes_err(BES_DBG_SDIO, "%s cmd.arg 0x%x request failed:%d\n", __func__, cmd.arg, err);
		return err;
	}

	if (req->rw == SG_READ)
		(pQid->Prog)++;
	else
		(pQid->Cmpl)++;

	return 0;
}

static int sdio_tx_aggr_try_add(const struct tx_packet *pkt)
{
	struct sg_request *req = &g_rwnx_plat.tx_sg_req;
	struct tx_buffer *tx_buf;
	int i = 0;
	u32 orig_len = 0, aligned_len = 0;

	/* aggr reach the limit, finish it */
	if (req->cnt + pkt->cnt >= SG_MAX_CNT || req->sg_len >= SG_MAX_SEGS)
		return -1;

	if (pkt->cnt == 1) {
		list_for_each_entry(tx_buf, &pkt->list[0], hdr) {
			orig_len += tx_buf->size;
		}
		aligned_len = ALIGN(orig_len, BES_SDIO_AGGR_BLOCK_SIZE);
	} else {
		aligned_len = BES_SDIO_AGGR_PER_LEN_LIMIT;
	}

	BUG_ON(aligned_len > BES_SDIO_AGGR_PER_LEN_LIMIT);
	BUG_ON((req->len > 0) ^ (req->cnt > 0));

	if (req->len && req->cnt &&
	   (aligned_len != req->peer_len))
		return -1;

	if (req->cnt == 0)
		req->peer_len = aligned_len;

	for (i = 0; i < pkt->cnt; i++) {
		struct tx_buffer *temp, *first_tx_buf = NULL;
		orig_len = 0;

		list_for_each_entry_safe(tx_buf, temp, &pkt->list[i], hdr) {
			if (orig_len == 0)
				first_tx_buf = tx_buf;

			list_move_tail(&tx_buf->hdr, &req->list);
			orig_len += tx_buf->size;
			req->sg_len++;
		}

		/* set the actual size */
		BUG_ON(!first_tx_buf);
		((struct ipc_header *)first_tx_buf->buf)->size = orig_len;

		/* every packet must align to aligned_len, so add pads to the last tx_buf */
		if (orig_len < aligned_len) {
			tx_buf = list_last_entry(&req->list, struct tx_buffer, hdr);
			tx_buf->size += aligned_len - orig_len;
		}

		req->cnt++;
		req->len += aligned_len;
	}

	req->rw = SG_WRITE;

	return 0;
}

int sdio_tx_aggr_finish(void)
{
	int ret;
	struct sg_request *req = &g_rwnx_plat.tx_sg_req;

	if (req->cnt == 0)
		return 0;

	bes_dbg(BES_DBG_SDIO, "%s, aggr cnt:%u sg_len %d len %d/%d",
		__func__, req->cnt, req->sg_len, req->len, req->peer_len);

	ret = bes_sdio_rw_extended(g_rwnx_plat.func, req);
	if (ret)
		bes_err(BES_DBG_SDIO, "sdio tx pkt fail:%d. count:%u, len:%u\n",
			ret, req->cnt, req->len);

	bes_psa_free_tx_buffer(&req->list);
	req->cnt = req->len = 0;
	req->sg_len = 0;
	req->peer_len = 0;

	return ret;
}
#else
static int sdio_tx_aggr_try_add(const struct tx_packet *pkt)
{
	u8 *tx_aggr_buf = NULL;
	struct tx_buffer *tx_buf;
	u32 orig_len = 0, aligned_len = 0;
	u8 i;

	if (pkt->cnt == 1) {
		list_for_each_entry(tx_buf, &pkt->list[0], hdr) {
			orig_len += tx_buf->size;
		}
		aligned_len = ALIGN(orig_len, BES_SDIO_AGGR_BLOCK_SIZE);
	} else {
		aligned_len = BES_SDIO_AGGR_PER_LEN_LIMIT;
	}

	BUG_ON(aligned_len > BES_SDIO_AGGR_PER_LEN_LIMIT);
	BUG_ON((g_rwnx_plat.tx_aggr_per_len > 0) ^ (g_rwnx_plat.tx_aggr_count > 0));

	if (g_rwnx_plat.tx_aggr_per_len && g_rwnx_plat.tx_aggr_count) {
		if (aligned_len != g_rwnx_plat.tx_aggr_per_len) {
			return -1;
		}
		if ((g_rwnx_plat.tx_aggr_count + pkt->cnt) * g_rwnx_plat.tx_aggr_per_len > BES_SDIO_AGGR_BUFSIZE) {
			return -2;
		}
	}

	tx_aggr_buf = &g_rwnx_plat.tx_aggr_buf[g_rwnx_plat.tx_aggr_count * g_rwnx_plat.tx_aggr_per_len];
	// tx data assemble
	for (i = 0; i < pkt->cnt; i++) {
		u16 amsdu_len = 0;

		list_for_each_entry(tx_buf, &pkt->list[i], hdr) {
			WARN_ON(amsdu_len + tx_buf->size > BES_SDIO_AGGR_PER_LEN_LIMIT);
			memcpy(tx_aggr_buf + amsdu_len, tx_buf->buf, tx_buf->size);
			amsdu_len += tx_buf->size;
		}
		bes_psa_free_tx_buffer((struct list_head *)&pkt->list[i]);

		/* set the actual size */
		((struct ipc_header *)tx_aggr_buf)->size = amsdu_len;
		tx_aggr_buf += BES_SDIO_AGGR_PER_LEN_LIMIT;
	}

	g_rwnx_plat.tx_aggr_per_len = aligned_len;
	g_rwnx_plat.tx_aggr_count += pkt->cnt;

	return 0;
}

int sdio_tx_aggr_finish(void)
{
	int ret;

	if (g_rwnx_plat.tx_aggr_count == 0)
		return 0;

	bes_dbg(BES_DBG_SDIO, "%s, aggr cnt:%u", __func__, g_rwnx_plat.tx_aggr_count);
	ret = bes_sdio_queue_mode_write(g_rwnx_plat.func, CONN_DATA, g_rwnx_plat.tx_aggr_buf,
		g_rwnx_plat.tx_aggr_per_len, g_rwnx_plat.tx_aggr_count);
	if (ret)
		bes_err(BES_DBG_SDIO, "%s:%u, sdio tx pkt fail:%d. drop them. count:%u, per_len:%u\n",
			__func__, __LINE__, ret, g_rwnx_plat.tx_aggr_count, g_rwnx_plat.tx_aggr_per_len);

	g_rwnx_plat.tx_aggr_count = 0;
	g_rwnx_plat.tx_aggr_per_len = 0;

	return ret;
}
#endif

static int bes_sdio_tx_aggr(struct sdio_func *func, const struct tx_packet *pkt)
{
	int ret = 0;

	ret = sdio_tx_aggr_try_add(pkt);
	if (ret) {
		// tx_aggr add failed, finish aggr and try to add again
		ret = sdio_tx_aggr_finish();
		if (ret)
			goto out;
		ret = sdio_tx_aggr_try_add(pkt);
		WARN_ON(ret);
	}

#ifdef CONFIG_SDIO_HOST_ADMA
	if (pkt->tx_now || g_rwnx_plat.tx_sg_req.cnt >= SG_MAX_CNT)
#else
	if (pkt->tx_now || g_rwnx_plat.tx_aggr_count >= BES_SDIO_AGGR_CNT_LIMIT)
#endif
		ret = sdio_tx_aggr_finish();

out:
	return ret;
}

static int bes_wifi_rx_split(struct rwnx_plat *self, const u8 *buf, u32 buf_len)
{
	static u8 pid = 0;
	const u8 *frame = (const u8 *)buf;
	struct sdio_aggr_hdr *hdr = (struct sdio_aggr_hdr *)frame;
	int frame_cnt = hdr->cnt;
	int ret = frame_cnt;

	while (frame_cnt-- > 0) {
		u16 frame_len;

		if (hdr->pid != ((pid + 1) & 0xf)) {
			bes_err(BES_DBG_PSA, "pid %d/%d error\n", frame[0], pid);
		}
		if (WARN_ON(hdr->type != EVENT_E2A_DATA_IND))
			break;

		pid = hdr->pid;
		frame_len = hdr->blocks * BES_SDIO_AGGR_BLOCK_SIZE;
		if (frame_len == 0) {
			bes_err(BES_DBG_PSA, "frame_len %u error, frame_cnt %d\n", frame_len, ret);
			break;
		}
		if (frame + frame_len - buf > buf_len) {
			bes_err(BES_DBG_PSA, "frame_len error, buf_len:%u, buf:%p, frame:%p, frame_len:%u, frame_cnt %d\n",
				buf_len, buf, frame, frame_len, ret);
			break;
		}

		hdr->size = frame_len;
		bes_sdio_e2a_data_pre_handler(self, frame);
		frame += frame_len;
		hdr = (struct sdio_aggr_hdr *)frame;
	}
	return ret;
}
#endif

static int bes_sdio_data_get(struct sdio_func *func, void *data_buf, u32 size)
{
	return bes_sdio_queue_mode_read(func, data_buf, size);
}

#ifndef SDIO_AGGR
static int __bes_sdio_send_one_packet(struct list_head *list, void *pre_alloc_buf)
{
	struct tx_buffer *tx_buf;
	u8 *buf;
	u16 len = 0;
	bool do_copy = true;
	int ret;

	WARN_ON(list_empty(list));
	if (list->next == list->prev) {
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

	/* set the actual size */
	((struct ipc_header *)buf)->size = len;

	len = ALIGN(len, BES_SDIO_AGGR_BLOCK_SIZE);
	ret = bes_sdio_queue_mode_write(g_rwnx_plat.func, CONN_DATA, buf, len, 1);
	if (buf && do_copy && !pre_alloc_buf)
		kfree(buf);

	return ret;
}
#endif

static int bes_sdio_data_push(const struct tx_packet *pkt)
{
#ifdef SDIO_AGGR
	return bes_sdio_tx_aggr(g_rwnx_plat.func, pkt);
#else

	if (pkt->is_amsdu) {
#ifdef CONFIG_RWNX_AMSDUS_TX
		u8 i;

		for (i = 0; i < pkt->cnt; i++) {
			int ret = __bes_sdio_send_one_packet((struct list_head *)&pkt->list[i], g_rwnx_plat.tx_amsdu);
			if (ret)
				return ret;
		}
		return 0;
#else
		BUG_ON(1);
		return -EINVAL;
#endif // CONFIG_RWNX_AMSDUS_TX
	} else {
		BUG_ON(pkt->cnt != 1);
		return __bes_sdio_send_one_packet((struct list_head *)&pkt->list[0], NULL);
	}
#endif // SDIO_AGGR
}

static int bes_sdio_msg_push(void *data_buf, u32 size)
{
	int ret = 0;
	struct rwnx_hw * rwnx_hw = (struct rwnx_hw *)(g_rwnx_plat.drvdata);

	ret = bes_sdio_queue_mode_write(rwnx_hw->plat->func, CONN_MSG, data_buf, size, 0);

	return ret;
}

u32 bes_bus_align_size(struct sdio_func *func, u32 size)
{
	return sdio_align_size(func, size);
}

int bes_bus_clr_int(struct sdio_func *func, u32 *lenPtr)
{
	u32 len;
	u8 reg = 0;
	int ret, retry = BES_SDIO_BUSY_MAX_RETRY;

	*lenPtr = 0;
	// try to read slave tx_len_reg
	while (retry-- > 0) {
		ret = bes_sdio_reg_read(func, BES_TX_LEN_REG_ID + 1, &reg, 1);
		if (ret) {
			return ret;
		}
		if (reg == 0) {
			ret = bes_sdio_reg_read(func, BES_TX_LEN_BACKUP_REG_ID + 1, &reg, 1);
			if (ret) {
				bes_err(BES_DBG_SDIO, "%s, reg read error%d\n", __FUNCTION__, ret);
				return ret;
			}

			// read BES_TX_LEN_REG_ID again for confirming clear interrupt
			if (reg != 0) {
				volatile u8 tmp_val;
				bes_sdio_reg_read(func, BES_TX_LEN_REG_ID + 1, &tmp_val, 1);
			}
		}

		len = reg & 0x7f;
		if (len != 0) {
			break;
		}
	}

	// get the real tx_len
	if (retry >= 0) {
		if (len > 512 / BES_SDIO_AGGR_BLOCK_SIZE) {
			len = 512 * (len - 512 / BES_SDIO_AGGR_BLOCK_SIZE);
		} else {
			len = BES_SDIO_AGGR_BLOCK_SIZE * len;
		}

		if (reg & 0x80) {
			*lenPtr = len | QUEUE_PRI_MASK;
		} else {
			*lenPtr = len;
		}
	}

	return 0;
}

static inline void bes_sdio_wake_ctrl(struct rwnx_hw *rwnx_hw, bool wakeup)
{
	if (wakeup)
		rwnx_platform_wakeup_mcu(rwnx_hw->plat, IO_LOCK_SRC_WIFI_LP, true);
	else
		rwnx_platform_sleep_mcu(rwnx_hw->plat, IO_LOCK_SRC_WIFI_LP, true);
}

int bes_sdio_platform_on(struct rwnx_hw *rwnx_hw)
{
	int ret = 0;
	struct rwnx_plat *rwnx_plat = rwnx_hw->plat;
	unsigned long flags;

	ret = bes_pwr_init(rwnx_hw, bes_sdio_wake_ctrl);
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
	skb_queue_head_init(&g_rwnx_plat.tx_q);
	rwnx_plat->a2e_msg = kzalloc(RWNX_MSG_MAX_LEN, GFP_KERNEL);
	if (rwnx_plat->a2e_msg == NULL)
		goto err_pwr_init;
#ifdef SDIO_AGGR
#ifdef CONFIG_SDIO_HOST_ADMA
	memset(&g_rwnx_plat.tx_sg_req, 0, sizeof(struct sg_request));
	INIT_LIST_HEAD(&g_rwnx_plat.tx_sg_req.list);
	g_rwnx_plat.sg = kmalloc(SG_MAX_SEGS * sizeof(struct scatterlist), GFP_KERNEL);
	if (!g_rwnx_plat.sg) {
		bes_err(BES_DBG_PSA, "%s:%d kmalloc failed\n", __func__, __LINE__);
		goto err_a2e_msg_alloc;
	}
#else
	g_rwnx_plat.tx_aggr_buf = (u8 *)kmalloc(BES_SDIO_AGGR_BUFSIZE, GFP_KERNEL);
	if (!g_rwnx_plat.tx_aggr_buf) {
		bes_err(BES_DBG_PSA, "%s:%d kmalloc failed\n", __func__, __LINE__);
		goto err_a2e_msg_alloc;
	}
	g_rwnx_plat.tx_aggr_per_len = 0;
	g_rwnx_plat.tx_aggr_count = 0;
#endif
#elif defined(CONFIG_RWNX_AMSDUS_TX)
	g_rwnx_plat.tx_amsdu = (u8 *)kmalloc(BES_TRANSFER_LEN_LIMIT, GFP_KERNEL);
	if (!g_rwnx_plat.tx_amsdu) {
		bes_err(BES_DBG_PSA, "%s:%d kmalloc failed\n", __func__, __LINE__);
		goto err_a2e_msg_alloc;
	}
#endif

	if ((ret = bes_bus_tx_init(rwnx_plat)) < 0) {
		bes_err(BES_DBG_PSA, "%s bus tx thread create fail\n", __func__);
#if defined (SDIO_AGGR) || defined (CONFIG_RWNX_AMSDUS_TX)
		goto err_sdio_alloc;
#else
		goto err_a2e_msg_alloc;
#endif
	}

	return 0;

err_sdio_alloc:
#if defined(SDIO_AGGR)
#ifdef CONFIG_SDIO_HOST_ADMA
	kfree(g_rwnx_plat.sg);
	g_rwnx_plat.sg = NULL;
#else
	// free tx_aggr_buf after bes_bus_tx_stop
	kfree(g_rwnx_plat.tx_aggr_buf);
	g_rwnx_plat.tx_aggr_buf = NULL;
#endif
#elif defined(CONFIG_RWNX_AMSDUS_TX)
	kfree(g_rwnx_plat.tx_amsdu);
	g_rwnx_plat.tx_amsdu = NULL;
#endif
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

void bes_sdio_platform_off(struct rwnx_hw *rwnx_hw)
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
#if defined(SDIO_AGGR)
#ifdef CONFIG_SDIO_HOST_ADMA
	kfree(g_rwnx_plat.sg);
	g_rwnx_plat.sg = NULL;
#else
	// free tx_aggr_buf after bes_bus_tx_stop
	kfree(g_rwnx_plat.tx_aggr_buf);
	g_rwnx_plat.tx_aggr_buf = NULL;
#endif
#elif defined(CONFIG_RWNX_AMSDUS_TX)
	kfree(g_rwnx_plat.tx_amsdu);
	g_rwnx_plat.tx_amsdu = NULL;
#endif
	if (g_rwnx_plat.a2e_msg) {
		kfree(g_rwnx_plat.a2e_msg);
		g_rwnx_plat.a2e_msg = NULL;
	}
}

static int bes_sdio_init_plat_cfg(struct sdio_func *func)
{
	int ret = 0;

#ifndef FW_DOWNLOAD_BY_SDIO
	sdio_claim_host(func);
	ret = sdio_claim_irq(func, bes_sdio_irq_handler);
	sdio_release_host(func);
	if (ret) {
		bes_err(BES_DBG_PSA, "%s sdio_claim_irq err:%d", __func__, ret);
		return ret;
	}
#endif

#if defined(FW_DOWNLOAD_BY_SDIO) || defined(CONFIG_BES_POWER_CTRL)
	init_waitqueue_head(&g_rwnx_plat.dev_stat_wait_q);
#endif

	g_rwnx_plat.func = func;
	g_rwnx_plat.clr_int = bes_bus_clr_int;
	g_rwnx_plat.data_get = bes_sdio_data_get;
	g_rwnx_plat.align_size = bes_bus_align_size;
	g_rwnx_plat.wakelock_flag = 0;
	g_rwnx_plat.dev = &func->dev;
	g_rwnx_plat.dev_status = 0;
	g_rwnx_plat.run_flags = 0;
	spin_lock_init(&g_rwnx_plat.run_flags_lock);

	spin_lock_init(&g_rwnx_plat.pwr_io_lock);
	atomic_set(&g_rwnx_plat.wifi_rx, 0);
	skb_queue_head_init(&g_rwnx_plat.rx_pri_q);
	skb_queue_head_init(&g_rwnx_plat.rx_normal_q);
	init_waitqueue_head(&g_rwnx_plat.wifi_rx_wait_q);

	if ((ret = bes_sdio_rx_init(&g_rwnx_plat)) < 0) {
		bes_err(BES_DBG_PSA, "%s rx thread create fail\n", __func__);
		return ret;
	}

	return 0;
}

static void bes_sdio_deinit_plat_cfg(void)
{
	bes_sdio_rx_stop(&g_rwnx_plat);
	skb_queue_purge(&g_rwnx_plat.rx_pri_q);
	skb_queue_purge(&g_rwnx_plat.rx_normal_q);
	g_rwnx_plat.func = NULL;
	bes_tx_loop_deinit(&g_rwnx_plat);
}

int bes_sdio_platform_init(void)
{
	spin_lock_init(&g_rwnx_plat.tx_lock);

	g_rwnx_plat.data_push = bes_sdio_data_push;
	g_rwnx_plat.msg_push = bes_sdio_msg_push;

	return rwnx_platform_init(&g_rwnx_plat, NULL);
}

void bes_firmware_work_func(struct work_struct *work)
{
	int ret = 0;
	struct bes_power_state power_state;

	/* tx_loop mechanism related initialization. */
	bes_tx_loop_init(&g_rwnx_plat);

	/* set boot wait state BES_BOOT_WAIT_DOWNLOAD_FW. */
	bes_set_boot_wait_state(BES_BOOT_WAIT_DOWNLOAD_FW);

	/* in bus detection process ,when device is detected, notification is sent. */
	bes_power_ctrl_bus_probe_notify();
#ifdef FW_DOWNLOAD_BY_SDIO
	/* load firmware onto devices through SDIO interface. */
	ret = bes_load_firmware_sdio(g_rwnx_plat.self, &g_rwnx_plat);
	if (!ret){
		g_rwnx_plat.self->fw_started = true;
	}

	if (ret < 0)
		goto download_err;
#endif

	/* bt testmode not register wlan */
	if (bes_sysfs_get_fw_type() == BES_FW_TYPE_BT)
		goto out;

	/* When dpd data has been obtained and only bt is enabled, wlan0 will not be registered */
	if (bes_power_ctrl_can_skip_platform_init(&g_rwnx_plat))
		goto out;

	/* platform initialization related to SDIO interface. */
	ret = bes_sdio_platform_init();
	if (ret) {
		bes_err(BES_DBG_SDIO, "register netdev fail\n");
		goto download_err;
	}

	/* query the current status of power control. */
	bes_power_ctrl_get_power_state(&power_state);

	/* When in signal mode and WiFi power is turned off, unregister WiFi related network devices. */
	if (bes_sysfs_is_signal_mode() && !power_state.wifi_power)
		bes_unregister_net_dev_after_get_dpd((struct rwnx_hw *)(g_rwnx_plat.drvdata));

	/* If system can close, set BES_BOOT_WAIT_PROBE_CLOSE and turn off the power related to the SDIO interface. */
	if (bes_power_ctrl_check_system_close(bes_get_power_ctrl(&g_rwnx_plat))) {
		bes_set_boot_wait_state(BES_BOOT_WAIT_PROBE_CLOSE);
		bes_power_ctrl_sdio_power_off(&g_rwnx_plat);
		return;
	}

out:
 	/* enable the system to control the power status of the system.*/
	if ((ret = bes_power_ctrl_system_enabled(true, false)) < 0)
		goto download_err;

	bes_info(BES_DBG_SDIO, "%s over\n", __func__);
	return;

download_err:
	/* set boot wait state BES_BOOT_WAIT_PROBE_CLOSE. */
	bes_set_boot_wait_state(BES_BOOT_WAIT_PROBE_CLOSE);

	/* system is disabled to control the power status of the system.*/
	bes_power_ctrl_system_enabled(false, true);

	/* turn off power related to SDIO interface.*/
	bes_power_ctrl_sdio_power_off(&g_rwnx_plat);

}

static int bes_sdio_probe(struct sdio_func *func, const struct sdio_device_id *id)
{
	int ret = 0;

	bes_err(BES_DBG_SDIO, "%s probe func=%p num=%d\n", __func__, func, func->num);

	if (func->num != 0x01)
		return -ENODEV;

#ifdef CONFIG_SDIO_HOST_ADMA
	if (func->card->host->max_segs < SG_MAX_SEGS ||
		func->card->host->max_seg_size < BES_SDIO_AGGR_PER_LEN_LIMIT) {
		bes_err(BES_DBG_SDIO, "sdio host max_segs %d, max_seg_size %d too small\n",
			func->card->host->max_segs, func->card->host->max_seg_size);
		return -ENOTSUPP;
	}
#endif

	bes_sysfs_set_device_id(id->device);
	func->card->quirks |= MMC_QUIRK_LENIENT_FN0;
	func->card->host->caps |= MMC_CAP_NONREMOVABLE;

	sdio_claim_host(func);
	sdio_f0_writeb(func, 0x2, 0x16, &ret);
	sdio_release_host(func);
	if(ret)
		bes_err(BES_DBG_SDIO, "%s sdio_f0_writeb ret:%d", __func__, ret);

	sdio_claim_host(func);
	ret = sdio_enable_func(func);
	sdio_release_host(func);
	if (ret) {
		bes_err(BES_DBG_SDIO, "%s sdio_enable_func err:%d", __func__, ret);
		return ret;
	}

	ret = bes_sdio_init_plat_cfg(func);
	if (ret)
		goto disable_func;

#ifdef FW_DOWNLOAD_BY_SDIO
	g_rwnx_plat.self = kzalloc(sizeof(struct sbus_priv), GFP_KERNEL);
	if (!g_rwnx_plat.self) {
		bes_info(BES_DBG_SDIO, "Can't allocate SDIO sbus_priv.");
		goto disable_func;
	}

	spin_lock_init(&g_rwnx_plat.self->lock);
	g_rwnx_plat.self->func = func;
	g_rwnx_plat.self->fw_started = false;
	sdio_set_drvdata(func, g_rwnx_plat.self);

#endif

	INIT_WORK((&g_rwnx_plat.download_fw_work), bes_firmware_work_func);

	schedule_work(&g_rwnx_plat.download_fw_work);

	return 0;

	bes_info(BES_DBG_SDIO, "%s over", __func__);

disable_func:
	/* SDIO related cleaning and deinitialization operations.*/
	bes_sdio_deinit_plat_cfg();
	bes_err(BES_DBG_SDIO, "%s failed, func:%d\n", __func__, func->num);
	func->card->host->caps &= ~MMC_CAP_NONREMOVABLE;
	sdio_claim_host(func);
	sdio_release_irq(func);
	sdio_disable_func(func);
	sdio_release_host(func);

	return 0;
}

static void bes_sdio_disconnect(struct sdio_func *func)
{
#ifdef FW_DOWNLOAD_BY_SDIO
	struct sbus_priv *self = sdio_get_drvdata(func);
#endif

	if (func->num != 0x01)
		return;

	bes_info(BES_DBG_SDIO, "%s enter", __func__);

	if (g_rwnx_plat.drvdata) {
		bes_power_ctrl_cancel_wlan_close_work((struct rwnx_hw *)(g_rwnx_plat.drvdata));
		rwnx_platform_deinit((struct rwnx_hw *)(g_rwnx_plat.drvdata));
		if (!g_rwnx_plat.pre_alloc)
			g_rwnx_plat.drvdata = NULL;
	}

#ifdef FW_DOWNLOAD_BY_SDIO
	if (self) {
		sdio_set_drvdata(func, NULL);
		kfree(self);
	}
#endif

	func->card->host->caps &= ~MMC_CAP_NONREMOVABLE;
	sdio_claim_host(func);
	sdio_release_irq(func);
	sdio_disable_func(func);
	sdio_release_host(func);
	bes_sdio_deinit_plat_cfg();
}

#ifdef CONFIG_PM
#if defined (PLAT_ROCKCHIP)
extern int rockchip_wifi_get_oob_irq_flag(void);
extern int rockchip_wifi_get_oob_irq(void);
#endif

bool wlan_hostwake_registered = false; /* Wifi request_irq success or not */

#if defined (PLAT_ROCKCHIP)
static irqreturn_t wlan_hostwake_thread(int irq, void *dev_id)
{
	unsigned long flags;

	bes_info(BES_DBG_SDIO, RWNX_FN_ENTRY_STR);

	/* Notify the PM core of a wakeup event(rx pending) if bus is suspend.
	   And mark there is a rx pending irq */
	spin_lock_irqsave(&g_rwnx_plat.run_flags_lock, flags);
	g_rwnx_plat.run_flags |= BIT(PLAT_RUN_FLAGS_BUS_RX_PENDING);
	rwnx_platform_pm_stay_awake(&g_rwnx_plat, RX_WAKELOCK);
	spin_unlock_irqrestore(&g_rwnx_plat.run_flags_lock, flags);

	/* Set the flag to get host wakeup source */
	bes_wowlan_set_wakeup_source((struct rwnx_hw *)g_rwnx_plat.drvdata, true);

	/* disavle wakeup source irq */
	disable_irq_nosync(irq);

	return IRQ_HANDLED;
}
#endif

static int wlan_hostwake_register(struct sbus_priv *self)
{
	int ret = 0;
#if defined (PLAT_ROCKCHIP)
	int irq_flags = rockchip_wifi_get_oob_irq_flag();
	int irq = rockchip_wifi_get_oob_irq();
#endif
	bes_dbg(BES_DBG_SDIO, RWNX_FN_ENTRY_STR);

	if (wlan_hostwake_registered) {
		bes_err(BES_DBG_SDIO, "wlan hostwake register repeatedly.");
		return -EBUSY;
	}

#if defined (PLAT_ROCKCHIP)
	irq_flags |= IRQF_ONESHOT;
	irq_flags &= (~IRQF_NO_SUSPEND);
	if (((ret = request_threaded_irq(irq, NULL, wlan_hostwake_thread,
	      irq_flags, "bes_wlan_hostwake", self)) != 0) ||
	    ((ret = enable_irq_wake(irq)) != 0)) {
		bes_err(BES_DBG_SDIO, "request_irq or irq_wake failed with %d\n", ret);
		return ret;
	}
#endif

	wlan_hostwake_registered = true;

	return ret;
}

static void wlan_hostwake_unregister(struct sbus_priv *self)
{
#if defined (PLAT_ROCKCHIP)
	int ret = 0;
	int irq = rockchip_wifi_get_oob_irq();
#endif
	bes_dbg(BES_DBG_SDIO, RWNX_FN_ENTRY_STR);

	if (!wlan_hostwake_registered) {
		return;
	}

	wlan_hostwake_registered = false;
#if defined (PLAT_ROCKCHIP)
	ret = disable_irq_wake(irq);
	if (ret) {
		bes_err(BES_DBG_SDIO, "disable_irq_wake failed with %d\n", ret);
	}
	free_irq(irq, self);
#endif
}

static int bes_sdio_prepare(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);

	bes_info(BES_DBG_SDIO, "%s (%p,%d)enter. run_flags:0x%lx\n",
		 __func__, func, func->num, g_rwnx_plat.run_flags);

	if (func->num > 1)
		goto out;

	/* hold the lock of keeping wifi awake before host suspending */
	rwnx_platform_wakeup_mcu(&g_rwnx_plat, IO_LOCK_SRC_HOST_LP, true);

out:
	bes_info(BES_DBG_SDIO, "%s (%p,%d)exit. run_flags:0x%lx\n",
		 __func__, func, func->num, g_rwnx_plat.run_flags);
	return 0;
}

static int bes_sdio_suspend(struct device *dev)
{
	int ret = 0;
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct sbus_priv *self = sdio_get_drvdata(func);
	unsigned long flags;
	bool tx_pending = false;
	bool tx_running = false;

	bes_info(BES_DBG_SDIO, "%s (%p,%d)enter. run_flags:0x%lx\n",
		 __func__, func, func->num, g_rwnx_plat.run_flags);

	if (func->num > 1)
		goto out;

	/* Mark bus enter suspend state */
	spin_lock_irqsave(&g_rwnx_plat.run_flags_lock, flags);
	tx_pending = !!(g_rwnx_plat.run_flags & (BIT(PLAT_RUN_FLAGS_TX_MSG) | BIT(PLAT_RUN_FLAGS_TX_DATA)));
	tx_running = !!(g_rwnx_plat.run_flags & BIT(PLAT_RUN_FLAGS_BUS_TX_PENDING));
	if (!tx_pending && !tx_running) {
		g_rwnx_plat.run_flags |= BIT(PLAT_RUN_FLAGS_BUS_SUSPEND);
	}
	spin_unlock_irqrestore(&g_rwnx_plat.run_flags_lock, flags);

	/* Reprocess tx data/msg */
	if (tx_pending) {
		wake_up(&g_rwnx_plat.tx_wait_q);
		ret = -EBUSY;
	}

	/* Bus tx thread is running */
	if (tx_running)
		ret = -EBUSY;

	/* System suspend process is been interrupted */
	if (ret)
		goto out;

	/* Notify SDIO that BES2600 will remain powered during suspend */
	ret = sdio_set_host_pm_flags(func, MMC_PM_KEEP_POWER);
	if (ret) {
		bes_err(BES_DBG_SDIO, "Error: setting SDIO pm flags: %d\n", ret);
		goto out;
	}

	/* register hostwake irq */
	ret = wlan_hostwake_register(self);
	if (ret) {
		bes_err(BES_DBG_SDIO, "Error: wlan hostwake register: %d\n", ret);
		goto out;
	}

	/* reset the wake source flag  */
	bes_wowlan_set_wakeup_source((struct rwnx_hw *)g_rwnx_plat.drvdata, false);

out:
	bes_info(BES_DBG_SDIO, "%s (%p,%d)exit. run_flags:0x%lx\n",
		 __func__, func, func->num, g_rwnx_plat.run_flags);
	return 0;
}

static int bes_sdio_suspend_noirq(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);

	bes_info(BES_DBG_SDIO, "%s (%p,%d)enter. run_flags:0x%lx\n",
		 __func__, func, func->num, g_rwnx_plat.run_flags);

	if (func->num > 1)
		goto out;

	/* release the lock of keeping wifi awake when host is suspended */
	rwnx_platform_sleep_mcu(&g_rwnx_plat, IO_LOCK_SRC_HOST_LP, false);

out:
	bes_info(BES_DBG_SDIO, "%s (%p,%d)exit. run_flags:0x%lx\n",
		 __func__, func, func->num, g_rwnx_plat.run_flags);
	return 0;
}

static int bes_sdio_resume_noirq(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);

	bes_info(BES_DBG_SDIO, "%s (%p,%d)enter. run_flags:0x%lx\n",
		 __func__, func, func->num, g_rwnx_plat.run_flags);

	if (func->num > 1)
		goto out;

	/* hold the lock of keeping wifi awake before host resuming */
	rwnx_platform_wakeup_mcu(&g_rwnx_plat, IO_LOCK_SRC_HOST_LP, false);

out:
	bes_info(BES_DBG_SDIO, "%s (%p,%d)exit. run_flags:0x%lx\n",
		 __func__, func, func->num, g_rwnx_plat.run_flags);
	return 0;
}

static int bes_sdio_resume(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct sbus_priv *self = sdio_get_drvdata(func);
	struct mmc_host *host = func->card->host;
	unsigned long flags;
	bool tx_pending = false;

	bes_info(BES_DBG_SDIO, "%s (%p,%d)enter. run_flags:0x%lx\n",
		 __func__, func, func->num, g_rwnx_plat.run_flags);

	if (func->num > 1)
		goto out;

	/* Mark bus exit suspend state */
	spin_lock_irqsave(&g_rwnx_plat.run_flags_lock, flags);
	g_rwnx_plat.run_flags &= ~BIT(PLAT_RUN_FLAGS_BUS_SUSPEND);
	g_rwnx_plat.run_flags &= ~BIT(PLAT_RUN_FLAGS_BUS_RX_PENDING);
	tx_pending = !!(g_rwnx_plat.run_flags & (BIT(PLAT_RUN_FLAGS_TX_MSG) | BIT(PLAT_RUN_FLAGS_TX_DATA)));
	rwnx_platform_pm_relax(&g_rwnx_plat, RX_WAKELOCK);

	spin_unlock_irqrestore(&g_rwnx_plat.run_flags_lock, flags);

	/* unregister hostwake irq */
	wlan_hostwake_unregister(self);

	/* Reprocess tx data/msg */
	if (tx_pending)
		wake_up(&g_rwnx_plat.tx_wait_q);

	/* Reprocess SDIO IRQs if the sdio_irq is pending. */
	if (host->sdio_irq_pending) {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(5,3,0))
		sdio_run_irqs(host);
#else
		sdio_signal_irq(host);
#endif
	}

out:
	bes_info(BES_DBG_SDIO, "%s (%p,%d)exit. run_flags:0x%lx\n",
		 __func__, func, func->num, g_rwnx_plat.run_flags);
	return 0;
}

static void bes_sdio_complete(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	bool suspend_notified;
	unsigned long flags;

	bes_info(BES_DBG_SDIO, "%s (%p,%d)enter. run_flags:0x%lx\n",
		 __func__, func, func->num, g_rwnx_plat.run_flags);

	if (func->num > 1)
		goto out;

	/* suspend_notified status is error */
	spin_lock_irqsave(&g_rwnx_plat.run_flags_lock, flags);
	suspend_notified = !!(g_rwnx_plat.run_flags & BIT(PLAT_RUN_FLAGS_SUSPEND_NOTIFIED));
	spin_unlock_irqrestore(&g_rwnx_plat.run_flags_lock, flags);
	WARN_ON(suspend_notified);

	/* release the lock of keeping wifi awake after host resuming */
	rwnx_platform_sleep_mcu(&g_rwnx_plat, IO_LOCK_SRC_HOST_LP, true);

out:
	bes_info(BES_DBG_SDIO, "%s (%p,%d)exit. run_flags:0x%lx\n",
		 __func__, func, func->num, g_rwnx_plat.run_flags);
}

static const struct dev_pm_ops bes_pm_ops = {
	.prepare = bes_sdio_prepare,
	.suspend = bes_sdio_suspend,
	.suspend_noirq = bes_sdio_suspend_noirq,
	.resume_noirq = bes_sdio_resume_noirq,
	.resume = bes_sdio_resume,
	.complete = bes_sdio_complete,
};
#endif

static struct sdio_driver bes_sdio_driver = {
	.name = "bes_wlan_sdio",
	.id_table = bes_sdio_ids,
	.probe = bes_sdio_probe,
	.remove = bes_sdio_disconnect,
#ifdef CONFIG_PM
	.drv = {
		.pm = &bes_pm_ops,
	}
#endif
};

static int bes_sdio_power_up(struct rwnx_plat *rwnx_plat)
{
	int ret = 0;

	/**
	 * If remove() is not called by sdio_unregister_driver(),
	 * sdio_register_driver() cannot be called when entering again.
	 */
	if (!rwnx_plat->register_flag) {
		ret = sdio_register_driver(&bes_sdio_driver);
		bes_dbg(BES_DBG_SDIO, "%s ret %d\n", __func__, ret);
		if (ret)
			return ret;
	}

	rwnx_plat->register_flag = true;

	android_mmc_rescan_card(rwnx_plat, AW_SDIOID);

	return ret;
}

static int bes_sdio_power_down(struct rwnx_plat *rwnx_plat)
{
	int ret = 0;

	/* sdio has been powered down, and return 0. */
	if (!rwnx_plat->register_flag)
		return 0;

	sdio_unregister_driver(&bes_sdio_driver);

	ret = rwmx_sdio_chip_power_down(rwnx_plat);

	bes_power_ctrl_system_enabled(false, false);

	rwnx_plat->register_flag = false;

	return ret;
}

/* Init Module function -> Called by insmod */
int bes_sdio_init(void)
{
	int ret;

	bes_dbg(BES_DBG_SDIO, "%s enter\n", __func__);

	/* keep first */
	ret = bes_power_ctrl_sdio_init(&g_rwnx_plat);
	if (ret)
		return ret;

	ret = bes_sysfs_register(&g_rwnx_plat);
	if (ret)
		goto register_sysfs_err;

	bes_sysfs_status_update(true);

	ret = bes_sdio_power_up(&g_rwnx_plat);
	bes_info(BES_DBG_SDIO, "%s ret %d\n", __func__, ret);
	if (ret)
		goto sdio_register_err;

	return ret;

sdio_register_err:
	bes_power_ctrl_cancel_probe_work();
	bes_sysfs_unregister();
register_sysfs_err:
	bes_power_ctrl_sdio_deinit(&g_rwnx_plat);

	return ret;
}

/* Called at Driver Unloading */
void bes_sdio_exit(void)
{
	/* FIXME -- this won't support multiple devices */
	bes_dbg(BES_DBG_SDIO, "%s enter\n", __func__);

	bes_power_ctrl_prep_rmmod(&g_rwnx_plat);
	bes_sdio_power_down(&g_rwnx_plat);

	bes_sysfs_unregister();
	bes_power_ctrl_sdio_deinit(&g_rwnx_plat);

	bes_info(BES_DBG_SDIO, "%s exit\n", __func__);
}
