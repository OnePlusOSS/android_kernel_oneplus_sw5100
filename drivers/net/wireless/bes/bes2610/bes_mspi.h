/*
 * @file bes_mspi.h
 *
 * Copyright (c) 2024, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BES_MSPI_H_
#define _BES_MSPI_H_

#include "rwnx_plat_comm.h"

struct rwnx_hw;

#define BES_SPI_TRX_BUF_SIZE              (0x00028000)
#define BES_SPI_DEVICE_ID_MASK            (0xFFFF0000)
#define BES_SPI_DEVICE_ID_BES             (0xBE570000)
#define BES_SPI_SUB_DEVICE_ID_MASK        (0x0000FFFF)

#define BES_HOST_SPI_DEFAULT_BLOCK_SIZE   (256)
#define BES_HOST_SPI_PACKET_SIZE          (1680)
#define BES_HOST_SPI_ALIGN(x)             ((x + 3) & (~0x3UL))

#define BES_HOST_SPI_SEQ_MASK             (0x3F)

#define BES_SPI_GPIO_RESPONSE

enum bes_spi_transfer_type {
	HOST_INIT_TRX_PARAMETER = 0,
	HOST_DATA_ERR,
	HOST_DATA,
	HOST_DUMMY_DATA,
	HOST_INVALID_TYPE,
};

enum MSPI_XFER_TYPE {
	MSPI_XFER_INIT_PARAM = 0,
	MSPI_XFER_DATA,
	MSPI_XFER_DATA_CRC_ERR,
	MSPI_XFER_DUMMY_DATA,
	MSPI_XFER_LEN_INFO,
	MSPI_XFER_TYPE_MAX,
};

enum MSPI_XFER_RX_STAGE {
	FIRST_DATA = 0,
	SECOND_DATA,
	FULL_DATA,
};

struct mspi_aggr_header_t {
	u16 crc16;
	u16 seq;
	u8  type;
	u8  aggr_num;
	u16 aggr_len;
}__attribute__((packed));

struct bes_spi_transfer_t {
	struct list_head node;
	bool tx;
	u8 pkt_start;
	u8 pkt_end;
	u32 seq;
	struct spi_transfer xfer;
	enum bes_spi_transfer_type type;
	u32 payload[];
};

struct mspi_init_param_t {
	u32 device_id; //0xbe571700
	u8 duplex; // 0: half 1:full
	u8 tx_bufcnt;
	u8 rx_bufcnt;
	u8 aggr_num;
	u16 bufsize; // 840 for example
	u16 blksize; // max bufcnt for one transfer for master to slave
} __attribute__((packed));

struct bes_spi {
	struct spi_device *spi;
	spinlock_t lock;
	wait_queue_head_t wq;
	int irq_gpio;
	int irq;
	int claimed;

	bool inited;
	struct mspi_init_param_t mspi_param;

	/* spi transfer */
	atomic_t tx, rx, stop, poll;
	atomic_t cmd_lock;
	//u32 seq;
	spinlock_t xfer_lock;
	struct list_head xfering;
	//wait_queue_head_t xfer_wait;
	struct bes_spi_transfer_t xfer_cmd;
	void *cmd_tx_buf, *cmd_rx_buf;
	struct task_struct *spi_thread;

	/* spi slave tx */
	u8 aggr_copy_num;
	u16 aggr_copy_len;
	u32 tx_seq;
	int tx_done_gpio, tx_done_irq, tx_ready_gpio;
	//struct list_head tx_free;
	//struct list_head tx_ready;
	//struct list_head tx_completed;
	//spinlock_t tx_lock;
	void *tx_buf;

	/* spi slave rx */
	int rx_ready_gpio, rx_done_gpio;
	unsigned long rx_ready_ts;
	unsigned long slv_crc_ts;
	//struct list_head rx_ready;
	//struct list_head rx_completed;
	struct sk_buff_head rx_packets;
	u16 rx_seq;
	enum MSPI_XFER_RX_STAGE rx_stage;
	u8  rx_aggr_num;
	u16 rx_aggr_len;
	u16 rx_bufsize;
	u16 rx_pos;
	void *rx_buf;
#ifdef PLAT_ROCKCHIP
	void *rx_dummy_buf;
#endif
};

#define RWNX_PLAT_PRIV_SIZE (sizeof(struct bes_spi))

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
	int (*clr_int)(struct spi_device *spi, u16 *lenPtr);
	int (*data_get)(struct spi_device *spi, u16 size);
	u32 (*align_size)(struct spi_device *spi, u32 size);
	atomic_t wifi_rx;
	atomic_t mspi_rx;
	struct sk_buff_head rx_pri_q;
	struct sk_buff_head rx_normal_q;
	struct workqueue_struct *bus_rx_wq;
	struct work_struct bus_rx_work;
	wait_queue_head_t mspi_rx_wait_q;
	struct task_struct *bus_rx_thread;
	bool enabled;
	u8 *trx_buf;

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
	unsigned long run_flags;
	spinlock_t run_flags_lock;

	struct rwnx_dpd_cali dpd_cali;

	/* keep last */
	u8 priv[RWNX_PLAT_PRIV_SIZE];
};

#define MSPI_STATE_RECORD_NUM (64)

enum mspi_state_record_type {
	MSPI_STATE_MASTER_TX_IND = 0,
	MSPI_STATE_SLAVE_RX_READY,
	MSPI_STATE_SLAVE_TX_READY,
	MSPI_STATE_SLAVE_CRC,
	MSPI_STATE_MASTER_CRC,
	MSPI_STATE_SPI_XFER,
	MSPI_STATE_TX_SEQ,
	MSPI_STATE_RX_SEQ,
	MSPI_STATE_RX_STAGE,
	MSPI_STATE_RX_FIRST_HDR,
	MSPI_STATE_RX_SECOND_HDR,
	MSPI_STATE_RX_THIRD_HDR,

	MSPI_STATE_MAX_NUM,
};

struct mspi_state_info {
	enum mspi_state_record_type type;
	uint16_t value;
	unsigned long trig_time;
};

struct mspi_state_record_ctxt {
	uint32_t idx;
	struct mspi_state_info state_info[MSPI_STATE_RECORD_NUM];
};

#ifdef CONFIG_BES_POWER_CTRL_GPIO
struct bes_mspi_power_ctrl {
	struct bes_power_ctrl *power_ctrl; /* keep first */
	void *priv;
};

int bes_power_ctrl_mspi_init(struct rwnx_plat *rwnx_plat);
void bes_power_ctrl_mspi_deinit(struct rwnx_plat *rwnx_plat);
void bes_power_ctrl_mspi_power_off(struct rwnx_plat *rwnx_plat);
#else
static inline int bes_power_ctrl_mspi_init(struct rwnx_plat *rwnx_plat) { return 0; }
static inline void bes_power_ctrl_mspi_deinit(struct rwnx_plat *rwnx_plat) {}
static inline void bes_power_ctrl_mspi_power_off(struct rwnx_plat *rwnx_plat) {}
#endif

int bes_mspi_init(void);
void bes_mspi_exit(void);
int bes_mspi_platform_init(void);
int bes_mspi_platform_on(struct rwnx_hw *rwnx_hw);
void bes_mspi_platform_off(struct rwnx_hw *rwnx_hw);
#endif /* _BES_MSPI_H_ */
