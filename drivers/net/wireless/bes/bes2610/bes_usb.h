/*
 * @file bes_usb.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BES_USB_H_
#define _BES_USB_H_

#include <linux/module.h>
#include <linux/usb.h>
#ifdef FW_DOWNLOAD_BY_USB
#include "bes_fw_common.h"
#endif
#include "rwnx_plat_comm.h"

struct rwnx_hw;

/* tx/rx pipes for usb */
enum BES_USB_PIPE_ID {
	BES_USB_PIPE_TX_CTRL = 0,
	BES_USB_PIPE_TX_DATA_LP,
	BES_USB_PIPE_TX_DATA_MP,
	BES_USB_PIPE_TX_DATA_HP,
	BES_USB_PIPE_BT_TX_DATA,
	BES_USB_PIPE_RX_CTRL,
	BES_USB_PIPE_RX_DATA,
	BES_USB_PIPE_BT_RX_DATA,
	BES_USB_PIPE_RX_INT,
	BES_USB_PIPE_MAX
};

enum bes_wf_usb_state {
	BES_WF_USB_DOWN,
	BES_WF_USB_UP,
	BES_WF_USB_SLEEP,
	BES_WF_USB_MAX
};

/*
 * USB vendor request types.
 */
#define BES_USB_VENDOR_REQUEST  ( USB_TYPE_VENDOR | USB_RECIP_DEVICE )
#define BES_USB_VENDOR_REQ_IN   ( USB_DIR_IN | BES_USB_VENDOR_REQUEST )  /* 0xC0 */
#define BES_USB_VENDOR_REQ_OUT  ( USB_DIR_OUT | BES_USB_VENDOR_REQUEST ) /* 0x40 */

/**
 * bes_vendor_request: USB vendor commands.
 */
#define BES_USB_VENDOR_REQ_REG_READ        (1)
#define BES_USB_VENDOR_REQ_REG_WRITE       (2)

#define BES_USB_PIPE_INVALID BES_USB_PIPE_MAX
/* usb device object */
struct bes_usb_pipe {
	struct list_head urb_list_head;
	struct usb_anchor urb_submitted;
	u32 urb_alloc;
	u32 urb_cnt;
	u32 urb_cnt_thresh;
	atomic_t urb_pending_cnt;
	unsigned int usb_pipe_handle;
	u32 flags;
	u8 ep_address;
	u8 logical_pipe_num;
	struct sbus_priv *ar_usb;
	u16 max_packet_size;
	struct work_struct io_complete_work;
	struct sk_buff_head io_comp_queue;
	struct usb_endpoint_descriptor *ep_desc;
	wait_queue_head_t urb_wait_q;
};

struct sbus_priv {
	/* protects pipe->urb_list_head and  pipe->urb_cnt */
	spinlock_t cs_lock;

	struct usb_device *udev;
	struct usb_interface *interface;
	struct bes_usb_pipe pipes[BES_USB_PIPE_MAX];
	struct rwnx_plat *ar;
	spinlock_t rx_queue_lock;
	struct sk_buff_head rx_queue;
	void *btdev;
	enum bes_wf_usb_state state;
	struct workqueue_struct *bes_io_wq;
#ifdef FW_DOWNLOAD_BY_USB
	void *irq_priv;
	spinlock_t status_lock;
	u32 int_control_reg;
	u32 int_status_reg;
#endif
};

#ifdef FW_DOWNLOAD_BY_USB
// virtual register definition
#define BES_USB_CONTROL_REG 0
#define BES_USB_STATUS_REG 1

// virtual register bits definition
#define BES_USB_FW_TX_DONE BIT(0)
#define BES_USB_FW_RX_INDICATION BIT(1)
#endif

#define BES_USB_PIPE_FLAG_TX (1 << 0)

/* usb urb object */
struct bes_urb_context {
	struct list_head link;
	struct bes_usb_pipe *pipe;
	struct sk_buff *skb;
	struct urb *urb;
#ifdef CONFIG_RWNX_AMSDUS_TX
	u8 *amsdu_skb;
#endif
};

/* constants */
#define TX_URB_COUNT 64
#define RX_URB_COUNT 64
#define USB_TX_BUF_NUM 72
#define TX_DATA_URB_COUNT (USB_TX_BUF_NUM + MSG_FIFO_NUM)
#ifdef CONFIG_BES_AMSDU_DEAGG
#define BES_USB_RX_BUFFER_SIZE 2048
#else
#define BES_USB_RX_BUFFER_SIZE 4608
#endif

/* USB endpoint definitions */
#define BES_USB_EP_ADDR_APP_CTRL_IN 0x81
#define BES_USB_EP_ADDR_APP_DATA_IN 0x82
#define BES_USB_EP_ADDR_APP_DATA_BT_IN 0x83

#define BES_USB_EP_ADDR_APP_CTRL_OUT 0x01
#define BES_USB_EP_ADDR_APP_DATA_LP_OUT 0x02
#define BES_USB_EP_ADDR_APP_DATA_BT_OUT 0x03

#ifdef CONFIG_BES_POWER_CTRL
struct bes_usb_power_ctrl {
	struct bes_power_ctrl *power_ctrl; /* keep first */
#ifdef CONFIG_BES_POWER_CTRL_MSG
	struct sbus_priv *sbus_priv;
	struct usb_device *udev;
	bool removed;
	bool need_enum;
	int power_pdata;
	u32 io_buf;
#endif
};
#endif

/**
 * struct rwnx_plat - Operation pointers for BES USB platform
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
 * @usb_dev: usb device
 * @sbus_priv: usb bus private
 * @deferred_e2amsg_work: sta connect/disconect work
 * @enabled: Set if embedded platform has been enabled (i.e. fw loaded and
 *           ipc started)
 *
 * @tx_buf_list: tx buffer list
 * @tx_buf_pool: tx buffer pool
 *
 * @power_priv: private data for platform power control
 * @dev_stat_wait_q: dev state wait queue
 * @dev_status: dev state
 * @pwr_io_lock: host_wakeup_mcu gpio spinlock
 * @pwr_io_ctl: host_wakeup_mcu gpio setting value
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

	struct device *usb_dev;
	void *sbus_priv;
	struct work_struct deferred_e2amsg_work;
	bool enabled;

	struct list_head tx_buf_list;
	struct tx_buffer tx_buf_pool[TX_BUFFER_POOL_CNT];

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

#ifdef CONFIG_BES_POWER_CTRL_MSG
void bes_usb_power_ctrl_get_sbus_priv(struct sbus_priv *sbus_priv);
#else
static inline void bes_usb_power_ctrl_get_sbus_priv(struct sbus_priv *sbus_priv) {}
#endif

#define BES_USB_IS_BULK_EP(attr) (((attr)&3) == 0x02)
#define BES_USB_IS_INT_EP(attr) (((attr)&3) == 0x03)
#define BES_USB_IS_ISOC_EP(attr) (((attr)&3) == 0x01)
#define BES_USB_IS_DIR_IN(addr) ((addr)&0x80)

int bes_usb_init(void *self_p, struct rwnx_plat *rwnx_plat);
void *bes_usb_read(struct sbus_priv *self);
int bes_usb_data_push(const struct tx_packet *pkt);
int bes_usb_msg_push(void *addr, u32 len);
int bes_usb_register_drv(void);
void bes_usb_unregister_drv(void);
void bes_usb_rx_complete(struct rwnx_plat *priv, struct sk_buff *skb);
#ifdef CONFIG_BES_BT
int bes_btusb_setup_pipes(struct sbus_priv *ar_usb);
void bes_btusb_uninit(struct sbus_priv *ar_usb);
#endif
void bes_usb_and_platform_destroy(struct sbus_priv *self);
#ifdef FW_DOWNLOAD_BY_USB
void bes_usb_set_fw_download_state(bool finish);
#endif

int bes_usb_platform_init(struct device *pdev, void *sbus_priv);
int bes_usb_platform_on(struct rwnx_hw *rwnx_hw);
void bes_usb_platform_off(struct rwnx_hw *rwnx_hw);

#endif /* _BES_USB_H_ */
