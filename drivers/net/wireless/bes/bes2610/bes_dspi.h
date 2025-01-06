/*
 * @file bes_dspi.h
 *
 * Copyright (c) 2024, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BES_DSPI_H_
#define _BES_DSPI_H_

#include "rwnx_plat_comm.h"

struct rwnx_hw;

/* bes dspi config */

#define BES_DSPI_CFG_WIRE_REG        (0xfffc0000)
#define BES_DSPI_CFG_4_WIRE          (1 << 0)

#define BES_DSPI_CFG_READ_REG        (0xfffc0004)
#define BES_DSPI_CFG_CNTS            (1 << 0)


/* bes dspi transfer */

/*
BES_DSPI_RD_ADDR
|  1 bit   |   1bit   |    30bit          |
|  0       |   1      |    addr >> 2      |
*/
#define BES_DSPI_RD_ADDR(X)	      ((X >> 2) | (1 << 30))

/*
BES_DSPI_WR_ADDR
|  1 bit   |   1bit   |    30bit          |
|  0       |   0      |    addr >> 2      |
*/
#define BES_DSPI_WR_ADDR(X)	      ((X >> 2) | (0 << 30))

/* bes dspi can only be accessed as DWORD(32-bit)
 * if BYTE or WORD(16-bit) reg wants to be accessed,
 * please extract form a DWORD read or write operation
 */
#define BES_DSPI_TRANSFER_ALIGN      (4)

/*
 * BES DSPI READ TRANSFER:
 * contents                  direction
 * 4bytes rd reg             master -> slave
 * 4bytes dummy data         master <- slave
 * read data                 master <- slave
 *
 * BES DSPI WRITE TRANSFER:
 * contents                  direction
 * 4bytes wr reg             master -> slave
 * write data                master -> slave
*/

#define BES_HOST_SPI_CMD_BUFSIZE          (12)
#define BES_HOST_SPI_REG_RD_BUFSIZE       (12)
#define BES_HOST_SPI_DATA_RD_BUFSIZE      (8)
#define BES_HOST_SPI_REG_WR_BUFSIZE       (8)
#define BES_HOST_SPI_DATA_WR_BUFSIZE      (4)

#define BES_DSPI_RX_MSG_BUFSIZE           (1024)

#define BES_HOST_SPI_TRANSFER_BUFSIZE     (8 * 1680 + BES_HOST_SPI_CMD_BUFSIZE)
#define BES_HOST_SPI_TRANSFER_WR_TAIL     (8)
#define BES_HOST_SPI_TRANSFER_RD_TAIL     (4)

/* bes dspi share */
#define BES_DSPI_SHARE_LOC_REG            (0x400800F4)
#define BES_DSPI_SHARE(BASE,OFT)          (BASE + OFT)

#define BES_DSPI_DEVICE_ID_REG            (0x0)
#define BES_DSPI_DEVICE_ID_MASK           (0xFFFF0000)
#define BES_DSPI_DEVICE_ID_BES            (0xBE570000)
#define BES_DSPI_SUB_DEVICE_ID_MASK       (0x0000FFFF)

#define BES_DSPI_TX_INFO_REG              (0x8)

#define BES_DSPI_TX_CTRL_REG              (0xC)
#define BES_DSPI_TX_CTRL_TG_MASK          (1 << 31)
#define BES_DSPI_TX_CTRL_TG_OFT           (31)
#define BES_DSPI_TX_CTRL_MSG_MASK         (1 << 30)
#define BES_DSPI_TX_CTRL_MSG_OFT          (30)
#define BES_DSPI_TX_CTRL_LEN_MASK         (0xffff)
#define BES_DSPI_TX_CTRL_LEN_OFT          (0)

#define BES_DSPI_TX_EXTRA_TAIL
#define BES_DSPI_TX_EXTRA_TAIL_LEN        (8)

#define BES_DSPI_TX_BUF_REG               (0x10)
#define BES_DSPI_TX_ITEM_NUM              (64)

#define BES_DSPI_RX_MSG_BUF_REG           (0x18)

#define BES_DSPI_RX_DATA_CTRL             (0x1C)

#define BES_DSPI_RX_DATA_BUF_RENEW        (0x20)

#define BES_DSPI_RX_DATA_BUF_POOL         (0x24)

#if 0
#define BES_DSPI_INT_REG                  (0x40081000)
#define BES_DSPI_RD_INT                   (1 << 1)
#define BES_DSPI_WR_MSG_INT               (1 << 11)
#define BES_DSPI_WR_DATA_INT              (1 << 13)
#else
#if 0 // sys0 2 bth0
#define BES_DSPI_RD_INT_REG               (0x4000009C)
#define BES_DSPI_RD_INT                   (1 << 16)
#define BES_DSPI_WR_INT_REG               (0x50000134)
#define BES_DSPI_WR_MSG_INT               (1 << 16)
#define BES_DSPI_WR_DATA_INT              (1 << 17)
#else // sys1 2 bth0
#define BES_DSPI_RD_INT_REG               (0x4000009C)
#define BES_DSPI_RD_INT                   (1 << 18)
#define BES_DSPI_WR_INT_REG               (0x50000134)
#define BES_DSPI_WR_MSG_INT               (1 << 18)
#define BES_DSPI_WR_DATA_INT              (1 << 19)
#endif
#endif

#define BES_DSPI_TRANS_MAX_RETRY          (3)
#define BES_DSPI_BUSY_MAX_RETRY           (2)


struct bes_spi {
	struct spi_device *spi;
	spinlock_t lock;
	wait_queue_head_t wq;
	int claimed;
	int irq_gpio;
	int irq;
	u32 share_base;

	/* spi transfer */
	u16 rd_xfer_bufsize;
	u16 wr_xfer_bufsize;
	struct bes_spi_transfer_t {
		u8 *tx_buf;
		u8 *rx_buf;
	} xfer_reg, xfer_data;

	/* spi master tx */
	struct {
		u32 bufsize:16;
		u32 bufnum_power:4;
		u32 aggr_num:8;
		u32 item_power:4;
	} data_buf_ctrl;
	u32 *data_buf_pool;
	u32 data_pool_top;
	u32 data_pool_bottom;

	/* aggr */
	u8 aggr_copy_num;
	u32 aggr_copy_len;
	u8 *aggr_copy_buf;
	u32 *aggr_copy_hostids;
	atomic_t aggr_preempt;

	/* retry */
	void *last_msg;
	u32 last_msg_size;
	u32 retry_entry_size;
	struct retry_entry_t {
		u32 hostid;
		u32 origin_hostid_num;
		u32 origin_hostids[];
	} *last_data;

	/* spi master rx */
	struct {
		u16 bufsize;
		u8 bufnum;
		u8 items;
	} rx_info;
	u32 rx_cnt;
	u32 rx_data_addr[2];
#ifdef BES_DSPI_TX_EXTRA_TAIL
	u32 extra_tail;
#endif
};

#define RWNX_PLAT_PRIV_SIZE (sizeof(struct bes_spi))

struct bes_spi_aggr_hdr {
    u8 type;
    u8 aggr_num;
    u16 size;
};

struct rwnx_plat {
	void *drvdata;
	bool pre_alloc;
	struct bes_tx_loop tx_loop;
	spinlock_t tx_lock;
	int (*data_push)(const struct tx_packet *pkt);
	int (*msg_push)(void *data_buf, u32 size);
	int (*msg_fifo_push)(void *data_buf, u32 size);
	atomic_t buf_tx;
	void *a2e_msg;
	wait_queue_head_t tx_wait_q;
	struct task_struct *bus_tx_thread;
	struct rwnx_tx_msg tx_msg;
	struct sk_buff_head tx_q;
	struct list_head tx_buf_list;
	struct tx_buffer tx_buf_pool[TX_BUFFER_POOL_CNT];
	bool register_flag;
	struct work_struct deferred_e2amsg_work;
	struct spi_device *spi_dev;
	int (*clr_int)(struct spi_device *spi, u32 *lenPtr);
	int (*data_get)(struct spi_device *spi, void **data, u32 size);
	u32 (*align_size)(struct spi_device *spi, u32 size);
	atomic_t wifi_rx;
	struct sk_buff_head rx_pri_q;
	struct sk_buff_head rx_normal_q;
	struct workqueue_struct *bus_rx_wq;
	struct work_struct bus_rx_work;
	bool enabled;

	int (*enable)(struct rwnx_hw *rwnx_hw);
	int (*disable)(struct rwnx_hw *rwnx_hw);
	void (*deinit)(struct rwnx_plat *rwnx_plat);
	u8 *(*get_address)(struct rwnx_plat *rwnx_plat, int addr_name, unsigned int offset);
	void (*ack_irq)(struct rwnx_plat *rwnx_plat);
	int (*get_config_reg)(struct rwnx_plat *rwnx_plat, const u32 **list);
#ifdef CONFIG_BES_POWER_CTRL
	void *power_priv;
#endif
	wait_queue_head_t dev_stat_wait_q;
	u32 dev_status;

	spinlock_t pwr_io_lock;
	u32 pwr_io_ctl;

	unsigned long wakelock_flag;
	struct device *dev;
	spinlock_t run_flags_lock;
	unsigned long run_flags;

	struct rwnx_dpd_cali dpd_cali;

	/* keep last */
	u8 priv[RWNX_PLAT_PRIV_SIZE];
};

#ifdef CONFIG_BES_POWER_CTRL_MSG
struct bes_dspi_power_ctrl {
	struct bes_power_ctrl *power_ctrl; /* keep first */
	struct work_struct ibus_off_work;
	void *priv;
};

int bes_power_ctrl_dspi_init(struct rwnx_plat *rwnx_plat);
void bes_power_ctrl_dspi_deinit(struct rwnx_plat *rwnx_plat);
#else
static inline int bes_power_ctrl_dspi_init(struct rwnx_plat *rwnx_plat) { return 0; }
static inline void bes_power_ctrl_dspi_deinit(struct rwnx_plat *rwnx_plat) {}
static inline void bes_power_ctrl_dspi_power_off(struct rwnx_plat *rwnx_plat) {}
#endif

int bes_dspi_init(void);
void bes_dspi_exit(void);
int bes_dspi_platform_init(void);
int bes_dspi_platform_on(struct rwnx_hw *rwnx_hw);
void bes_dspi_platform_off(struct rwnx_hw *rwnx_hw);
#endif /* _BES_DSPI_H_ */
