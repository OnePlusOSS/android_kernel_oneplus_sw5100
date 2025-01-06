/**
 ******************************************************************************
 *
 * @file rwnx_plat_comm.h
 *
 * Copyright (C) RivieraWaves 2012-2021
 *
 ******************************************************************************
 */

#ifndef __RWNX_PLAT_COMM_H__
#define __RWNX_PLAT_COMM_H__

#include "lmac_msg.h"

#define MSG_FIFO_NUM (3)

/**
 * struct rwnx_tx_msg - Tx msg data
 *
 * @size: Indicates the length of tx msg buf
 * @data_buf: pointer to tx msg buf
 */
struct rwnx_tx_msg {
	u32 size;
	void *data_buf;
};

/**
 * struct wait_dev_status - Record dev_status for which indication msg has not been returned for tx_loop mechanism
 *
 * @dev_status: dev_status which is waiting for indication msg
 * @target_val: Indicates whether to wait for dev_tatus to complete or clear
 */
struct wait_dev_status {
	u32 dev_status;
	bool target_val;
};

/**
 * struct bes_tx_loop - Operation pointers for tx_loop mechanism
 *
 * @enabled: Indicates whether the tx_loop mechanism is enabled
 * @dev_disconnect: Indicates whether to wait for dev_tatus to complete or clear
 * @lmac_tx_pending: Indicates the number of pending data skb
 * @lmac_mon_timer: This timer is used to ensure that data skbs are properly sent and received
 * @wait_ind_timer: This timer is used to ensure that specific indication skbs are properly received
 * @wait_ind_lock: The spin lock to protects the ind_timeout_tbl
 * @wait_dev_status: Indicates whether the device is disconnected from the host
 * @rx_queue: This queue is used to store the msg returned by the tx loop mechanism
 * @tx_loop_close_work: Used to power off the device and unregister wlan
 * @ind_timeout_msg: Used to record the msg corresponding to wait_ind_timer
 */
struct bes_tx_loop {
	unsigned long enabled;
	bool dev_disconnect;
	atomic_t lmac_tx_pending;
	struct timer_list lmac_mon_timer;
	struct timer_list wait_ind_timer;
	spinlock_t wait_ind_lock;
	struct wait_dev_status wait_dev_status;
	struct sk_buff_head rx_queue;
	struct work_struct tx_loop_close_work;
	lmac_msg_id_t ind_timeout_msg;
};

enum conn_type {
	CONN_DATA = 1,
	CONN_MSG,
	CONN_CUSTOM,
};


#define TX_BUFFER_MAX NX_TX_PAYLOAD_MAX
#ifdef CONFIG_SDIO_HOST_ADMA
#define TX_BUFFER_POOL_CNT SG_MAX_SEGS
#else
#define TX_BUFFER_POOL_CNT (15 * NX_TX_PAYLOAD_MAX)
#endif

/**
 * struct tx_buffer - tx buffer infomation
 *
 * @hdr: hdr to link multi tx buffers
 * @buf: buffer addr to send
 * @size: buffer size to send
 */
struct tx_buffer {
	struct list_head hdr;
	void *buf;
	u16 size;
};

/**
 * struct tx_packet - tx packet infomation
 *
 * @list: list contains tx_buffer
 * @cnt: actual buffer cnt that fw used for the frame
 * @tx_now: send immediately
 * @type: data type
 */
struct tx_packet {
	union {
		struct list_head list[TX_BUFFER_MAX]; // for data
		struct tx_buffer tx_buf; // for msg
	};
	u8 cnt;
	bool tx_now;
	bool is_amsdu;
	enum conn_type type;
};

/**
 * struct rwnx_dpd_cali - Information of dpd calibration data
 *
 * @size: dpd data size
 * @data: dpd data
 */
struct rwnx_dpd_cali {
	u16 size;
	u8 *data;
};

/* dpd data struct */
struct mcu_dpd_req {
	u16 recv_offset;
	u16 req_size;
};

struct mcu_recover_dpd {
	u16 offset;
	u16 size;
	u8 last_pkt;
	u8 data[0];
} __attribute__((packed));

#endif /* __RWNX_PLAT_COMM_H__ */
