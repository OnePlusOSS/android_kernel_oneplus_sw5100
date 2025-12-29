/*
 * @file bes_sdio.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BES_SDIO_H_
#define _BES_SDIO_H_

#include "bes_hwio.h"
#include "rwnx_plat_comm.h"

struct rwnx_hw;

/* sdio regs */
#define BES_TX_LEN_REG_ID (0)
#define BES_BUSY_REG_ID (0x104)
#define BES_TX_LEN_BACKUP_REG_ID (0x10C)
#define BES_ASYNC_IRQ_REG_ID (0x16)
#define BES_ASYNC_IRQ_ENABLE 2

#define BES_TX_LEN_MASK (0x7fff)
#define QUEUE_PRI_MASK (0x8000)
#define HIF_REG_MASK (0x7FFF)
#define HIF_REG_TXLEN_MASK (0x3FFF)

/*
SDIO_QUEUE_WTG_ADDR17BIT
|  1 bit   |   2bit   |    14bit          |
|  toggle  |   type   |    real len       |
*/
#define SDIO_QUEUE_WTG_ADDR17BIT(queue_id, type, len) \
	((((queue_id)&0x1) << 16) | (((type) & 3) << 14) | (((len)&HIF_REG_TXLEN_MASK) << 0))

/*
SDIO_QUEUE_WTG_AGGR_ADDR17BIT
|  1 bit   |   2bit   |    4bit        |     5bit       |     5bit       |
|  toggle  |   type   |    reserved    |  aggr_per_len  |    aggr_cnt    |
*/
#define SDIO_QUEUE_WTG_AGGR_ADDR17BIT(queue_id, per_blocks, cnt) \
	((((queue_id)&0x1) << 16) | (1 << 14) | (((per_blocks)&0x1F) << 5) | (((cnt)&0x1F) << 0))

/*
SDIO_QUEUE_RTG_ADDR17BIT
|  1 bit   |          16bit               |
|  toggle  |          real len            |
*/
#define SDIO_QUEUE_RTG_ADDR17BIT(queue_id, reg_addr) \
	((((queue_id)&0x1) << 16) | (((reg_addr)&HIF_REG_MASK) << 0))

#define BES_SDIO_AGGR_PER_LEN_LIMIT BES_TRANSFER_LEN_LIMIT
#define BES_SDIO_AGGR_BLOCK_SIZE (64)
#ifndef CONFIG_SDIO_HOST_ADMA
#ifndef PLAT_FPGA
#define BES_SDIO_AGGR_CNT_LIMIT (15)
#else
#define BES_SDIO_AGGR_CNT_LIMIT (2)
#endif
#define BES_SDIO_AGGR_BUFSIZE (BES_SDIO_AGGR_PER_LEN_LIMIT * BES_SDIO_AGGR_CNT_LIMIT)
#endif

#define BES_SDIO_TRANS_MAX_RETRY 1
#define BES_SDIO_BUSY_MAX_RETRY 2

struct sdio_aggr_hdr
{
	u16_l type;
	union {
		struct {
			u8_l pid : 4;
			u8_l cnt : 4;
			u8_l blocks;
		};
		u16_l size;
	};
};

typedef struct {
	u32 Prog; // host read
	u32 Cmpl; // host write
} HI_CHAN;

#ifdef FW_DOWNLOAD_BY_SDIO
struct sbus_priv {
	struct sdio_func *func;
	spinlock_t lock;
	void *irq_priv;
	struct device *dev;
	bool fw_started;
};
#endif

#ifdef CONFIG_SDIO_HOST_ADMA
/**
 * scatter/gatter dma request
 */
#define SG_READ     1
#define SG_WRITE    2

#ifndef PLAT_FPGA
#define SG_MAX_CNT  15
#else
#define SG_MAX_CNT  2
#endif
#define SG_MAX_SEGS (SG_MAX_CNT * NX_TX_PAYLOAD_MAX)

struct sg_item {
	void *buf;
	u32 len;
};

/**
 * struct sg_request - used by sdio host tx
 *
 * @len: total tx length
 * @sg_len: sg count, one subframe use one sg_buf
 * @cnt: actual buffer cnt that fw used for the frame
 * @rw: SG_WRITE/SG_READ
 * @peer_len: used in sdio aggr
 * @list: list contains tx_buffer
 */
struct sg_request {
	u32 len;
	u16 sg_len;
	u8 cnt;
	u8 rw;
	u16 peer_len;
	struct list_head list;
};
#endif /* CONFIG_SDIO_HOST_ADMA */

/**
 * struct rwnx_plat - Operation pointers for BES SDIO platform
 *
 * @drvdata: driver data
 * @pre_alloc: indicate if pre-alloc wifi resource
 * @tx_loop: tx recover
 * @tx_lock: tx lock
 * @data_push: tx data push to bus
 * @msg_push: message push to bus
 * @buf_tx: tx buffer number
 * @fifo: message fifo
 * @fifo_buf: message fifo buffer
 * @tx_wait_q: tx thread wait queue
 * @bus_tx_thread: tx thread
 * @tx_msg: message buffer
 * @tx_q: tx data list
 * @func: sdio function
 * @clr_int: sdio irq clear
 * @data_get: get data
 * @align_size: size align
 * @wifi_rx: one rx buffer wait receive
 * @wifi_rx_wait_q: rx receive wait queue
 * @bus_rx_thread: rx thread
 * @rx_pri_q: rx message list
 * @rx_normal_q: rx data list
 * @tx_sg_req: tx scatterlist management
 * @sg: tx scatterlist to send by sdio bus
 * @tx_aggr_buf: tx aggregation buffer
 * @tx_aggr_per_len: single aggregate unit length
 * @tx_aggr_count: number of tx aggregation
 * @tx_amsdu: tx amsdu buffer
 *
 * @tx_buf_list: tx buffer list
 * @tx_buf_pool: tx buffer pool
 *
 * @register_flag: bus driver register flag
 * @deferred_e2amsg_work: sta connect/disconect work
 * @enabled: Set if embedded platform has been enabled (i.e. fw loaded and
 *          ipc started)
 * @power_priv: private data for platform power control
 * @dev_stat_wait_q: dev state wait queue
 * @dev_status: dev state
 *
 * @pwr_io_lock: host_wakeup_mcu gpio spinlock
 * @pwr_io_ctl: host_wakeup_mcu gpio setting value
 *
 * @wakelock_flag: indicate the status of pm wakelock
 * @dev: pointer to struct device
 * @run_flags: indicate the run flags for plat
 * @run_flags_lock: the spinlock to protect the run flags for plat
 * @dpd_cali: dpd calib value
 */
struct rwnx_plat {
	void *drvdata;
	bool pre_alloc;
	struct bes_tx_loop tx_loop;
	spinlock_t tx_lock;
	int (*data_push)(const struct tx_packet *pkt);
	int (*msg_push)(void *data_buf, u32 size);
	atomic_t buf_tx;
	void *a2e_msg;
	wait_queue_head_t tx_wait_q;
	struct task_struct *bus_tx_thread;
	struct rwnx_tx_msg tx_msg;
	struct sk_buff_head tx_q;

	struct sdio_func *func;
	int (*clr_int)(struct sdio_func *func, u32 *lenPtr);
	int (*data_get)(struct sdio_func *func, void *data, u32 size);
	u32 (*align_size)(struct sdio_func *func, u32 size);
	atomic_t wifi_rx;
	wait_queue_head_t wifi_rx_wait_q;
	struct task_struct *bus_rx_thread;
	struct sk_buff_head rx_pri_q;
	struct sk_buff_head rx_normal_q;

#ifdef SDIO_AGGR
#ifdef CONFIG_SDIO_HOST_ADMA
	struct sg_request tx_sg_req;
	struct scatterlist *sg; /* SG_MAX_SEGS */
#else
	u8 *tx_aggr_buf;
	u32 tx_aggr_per_len;
	u32 tx_aggr_count;
#endif
#elif defined(CONFIG_RWNX_AMSDUS_TX)
	u8 *tx_amsdu;
#endif

	struct list_head tx_buf_list;
	struct tx_buffer tx_buf_pool[TX_BUFFER_POOL_CNT];

	bool register_flag;
	struct work_struct deferred_e2amsg_work;
	bool enabled;

#ifdef CONFIG_BES_POWER_CTRL
	void *power_priv;
#endif
	wait_queue_head_t dev_stat_wait_q;
	u32 dev_status;
	spinlock_t pwr_io_lock;
	u32 pwr_io_ctl;
	unsigned long wakelock_flag;
	struct device *dev;
	unsigned long run_flags;
	spinlock_t run_flags_lock;
	struct rwnx_dpd_cali dpd_cali;
	struct work_struct download_fw_work;
	struct sbus_priv *self;
};


#ifdef CONFIG_BES_POWER_CTRL_GPIO
struct bes_sdio_power_ctrl {
	struct bes_power_ctrl *power_ctrl; /* keep first */
	struct work_struct sdio_off_work;
};

int bes_power_ctrl_sdio_init(struct rwnx_plat *rwnx_plat);
void bes_power_ctrl_sdio_deinit(struct rwnx_plat *rwnx_plat);
void bes_power_ctrl_sdio_power_off(struct rwnx_plat *rwnx_plat);
#else
static inline int bes_power_ctrl_sdio_init(struct rwnx_plat *rwnx_plat) { return 0; }
static inline void bes_power_ctrl_sdio_deinit(struct rwnx_plat *rwnx_plat) {}
static inline void bes_power_ctrl_sdio_power_off(struct rwnx_plat *rwnx_plat) {}
#endif /* CONFIG_BES_POWER_CTRL_GPIO */

int bes_sdio_init(void);
void bes_sdio_exit(void);

int bes_sdio_platform_init(void);
int bes_sdio_platform_on(struct rwnx_hw *rwnx_hw);
void bes_sdio_platform_off(struct rwnx_hw *rwnx_hw);
void bes_sdio_irq_handler(struct sdio_func *func);
#endif /* _BES_SDIO_H_ */
