/*
 * @file bes_usb.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "bes_psa.h"
#include "bes_usb.h"
#include "bes_sysfs.h"
#include "bes_log.h"
#include "bes_power_ctrl.h"
#include "rwnx_msg_tx.h"
#include "bes_tx_loop.h"
#include <linux/of_gpio.h>
#include "bes_hwio.h"
#include "bes_linkinfo.h"
#include "bes_pwr.h"
#include "bes_wowlan.h"
#include "rwnx_main.h"
#ifdef FW_DOWNLOAD_BY_USB
#include "bes_fw_usb.h"
static bool fw_download_finish_flag = false;
#endif

struct sbus_priv;
extern struct rwnx_plat g_rwnx_plat;

static void bes_usb_recv_complete(struct urb *urb);

void bes_io_bus_status_dump(void)
{
}

#ifdef FW_DOWNLOAD_BY_USB
void bes_usb_set_fw_download_state(bool fw_started)
{
	fw_download_finish_flag = fw_started;
}
#endif

#ifdef CONFIG_BES_POWER_CTRL_MSG

#ifdef PLAT_SIGMASTAR
#define SIGMASTAR_POWER_GPIO 59
#endif

static int bes_usb_get_power_pdata(void)
{
	int gpio_num = -1;
	int ret;

#ifdef PLAT_SIGMASTAR
	gpio_num = SIGMASTAR_POWER_GPIO;
#endif

	if (gpio_is_valid(gpio_num)) {
		ret = gpio_request(gpio_num, "bes2610_powerup");
		if (ret) {
			bes_err(BES_DBG_POWER_CTRL, "can't request bes2610_powerup:%d\n", ret);
			return -1;
		} else {
			gpio_direction_output(gpio_num, 0);
		}
	} else {
		return -1;
	}

	return gpio_num;
}

static void bes_usb_release_power_pdata(struct bes_usb_power_ctrl *bes_usb_power_ctrl)
{
	if (gpio_is_valid(bes_usb_power_ctrl->power_pdata))
		gpio_free(bes_usb_power_ctrl->power_pdata);
}

static int bes_usb_chip_power(struct bes_power_ctrl *bes_power_ctrl, bool on)
{
	struct rwnx_plat *rwnx_plat = bes_power_ctrl->plat;
	struct bes_usb_power_ctrl *bes_usb_power_ctrl = rwnx_plat->power_priv;

	if (bes_usb_power_ctrl->power_pdata == -1)
		return 0;

	gpio_set_value(bes_usb_power_ctrl->power_pdata, on ? 1 : 0);
	msleep(10);

	return 0;
}

static void bes_enum_usb_dev(struct bes_usb_power_ctrl *bes_usb_power_ctrl)
{
	int ret;
	u32 *val = &bes_usb_power_ctrl->io_buf;

	if (!bes_usb_power_ctrl->need_enum)
		return;

	*val = cpu_to_le32(HOST_INT_TYPE_REBOOT);
	ret = usb_control_msg(bes_usb_power_ctrl->udev, usb_sndctrlpipe(bes_usb_power_ctrl->udev, 0),
			      BES_USB_VENDOR_REQ_REG_WRITE, BES_USB_VENDOR_REQ_OUT,
			      0, 0, val, sizeof(*val), HZ / 2);

	bes_usb_power_ctrl->removed = false;

	if (ret <= 0)
		bes_err(BES_DBG_USB, "%s fail, ret: %d\n", __func__, ret);
}

static void bes_usb_power_ctrl_exit_handler(void *priv)
{
	struct bes_usb_power_ctrl *bes_usb_power_ctrl = priv;
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();

	if (bes_usb_power_ctrl->power_pdata != -1)
		bes_chip_power(rwnx_plat, false);
	else
		bes_enum_usb_dev(bes_usb_power_ctrl);
}

static int bes_usb_iowrite(void *priv, u32 *io_dmabuf, u32 val)
{
	int ret = 0;
	struct bes_usb_power_ctrl *bes_usb_power_ctrl = priv;
	struct sbus_priv *sbus_priv;

	BUG_ON(!priv);

	sbus_priv = bes_usb_power_ctrl->sbus_priv;

	*io_dmabuf = cpu_to_le32(val);
	ret = usb_control_msg(sbus_priv->udev, usb_sndctrlpipe(sbus_priv->udev, 0),
			      BES_USB_VENDOR_REQ_REG_WRITE, BES_USB_VENDOR_REQ_OUT,
			      0, 0, io_dmabuf, sizeof(*io_dmabuf), HZ / 2);

	if (ret > 0)
		ret = 0;

	return ret;
}

static void bes_usb_unregister_netdev(void *priv)
{
	struct bes_usb_power_ctrl *bes_usb_power_ctrl = priv;
	struct sbus_priv *sbus_priv;

	BUG_ON(!priv);
	sbus_priv = bes_usb_power_ctrl->sbus_priv;

	bes_usb_and_platform_destroy(sbus_priv);
}

static const struct bes_power_ctrl_msg_ops usb_power_ctrl_msg_ops = {
	.iowrite = bes_usb_iowrite,
	.unregister_netdev = bes_usb_unregister_netdev,
	.exit = bes_usb_power_ctrl_exit_handler,
};

void bes_usb_power_ctrl_get_sbus_priv(struct sbus_priv *sbus_priv)
{
	struct bes_usb_power_ctrl *bes_usb_power_ctrl = g_rwnx_plat.power_priv;

	bes_usb_power_ctrl->sbus_priv = sbus_priv;
}

#endif /* CONFIG_BES_POWER_CTRL_MSG */

static int bes_usb_plat_power(struct rwnx_plat *rwnx_plat, bool on)
{
	int ret = 0;

	if (on) {
		bes_power_ctrl_start_bus_probe();
		if ((ret = bes_chip_power(rwnx_plat, false)) < 0)
			return ret;
		if ((ret = bes_chip_power(rwnx_plat, true)) < 0)
			return ret;
		msleep(100);
	} else {
		bes_usb_and_platform_destroy((struct sbus_priv *)(rwnx_plat->sbus_priv));
		ret = bes_chip_power(rwnx_plat, false);
	}

	return ret;
}

static void bes_usb_power_ctrl_get_dev(struct usb_device *dev)
{
#ifdef CONFIG_BES_POWER_CTRL_MSG
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_usb_power_ctrl *bes_usb_power_ctrl = rwnx_plat->power_priv;

	bes_usb_power_ctrl->udev = dev;
	bes_usb_power_ctrl->removed = false;
#endif
}

static void bes_usb_power_ctrl_set_remove(void)
{
#ifdef CONFIG_BES_POWER_CTRL_MSG
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_usb_power_ctrl *bes_usb_power_ctrl = rwnx_plat->power_priv;

	bes_usb_power_ctrl->removed = true;
#endif
}

static void bes_check_usb_dev_state(void)
{
#ifdef CONFIG_BES_POWER_CTRL_MSG
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_usb_power_ctrl *bes_usb_power_ctrl = rwnx_plat->power_priv;

	if (bes_usb_power_ctrl->udev && !bes_usb_power_ctrl->removed)
		bes_usb_power_ctrl->need_enum = true;
	else
		bes_usb_power_ctrl->need_enum = false;
#endif
}

int bes_power_ctrl_usb_init(struct rwnx_plat *rwnx_plat)
{
#ifdef CONFIG_BES_POWER_CTRL_GPIO
	rwnx_plat->power_priv = bes_power_ctrl_gpio_register(rwnx_plat,
							     sizeof(struct bes_usb_power_ctrl),
							     bes_usb_plat_power);
	if (!rwnx_plat->power_priv)
		return -ENOMEM;

#elif defined(CONFIG_BES_POWER_CTRL_MSG)
	struct bes_usb_power_ctrl *bes_usb_power_ctrl = NULL;
	plat_power_cb_t cb = NULL;
	int gpio_num = bes_usb_get_power_pdata();

	if (gpio_num != -1) {
		cb = bes_usb_plat_power;
		bes_power_ctrl_set_chip_power(bes_usb_chip_power);
	}

	rwnx_plat->power_priv = bes_power_ctrl_msg_register(&usb_power_ctrl_msg_ops,
							    rwnx_plat, sizeof(struct bes_usb_power_ctrl), cb);
	if (!rwnx_plat->power_priv)
		return -ENOMEM;

	bes_usb_power_ctrl = rwnx_plat->power_priv;
	bes_usb_power_ctrl->udev = NULL;
	bes_usb_power_ctrl->removed = false;
	bes_usb_power_ctrl->power_pdata = gpio_num;
#endif

	return 0;
}

void bes_power_ctrl_usb_deinit(struct rwnx_plat *rwnx_plat)
{
#ifdef CONFIG_BES_POWER_CTRL_GPIO
	bes_chip_power(rwnx_plat, false);
	bes_power_ctrl_gpio_unregister(rwnx_plat->power_priv);
#elif defined(CONFIG_BES_POWER_CTRL_MSG)
	struct bes_usb_power_ctrl *bes_usb_power_ctrl = rwnx_plat->power_priv;
	bes_usb_release_power_pdata(bes_usb_power_ctrl);
	bes_power_ctrl_msg_unregister(rwnx_plat->power_priv);
#endif
}

static int bes_usb_wait_data_urb(struct bes_usb_pipe *pipe)
{
	long time;
	int ret = 0;

	time = wait_event_timeout(pipe->urb_wait_q, !list_empty(&pipe->urb_list_head),
				  msecs_to_jiffies(100));

	if (WARN_ON(time <= 0)) {
		bes_err(BES_DBG_USB,"pipe %u, urb fulled!!!\n", pipe->logical_pipe_num);
		ret = -ETIMEDOUT;
	}

	return ret;
}

/* pipe/urb operations */
static struct bes_urb_context *bes_usb_alloc_urb_from_pipe(struct bes_usb_pipe *pipe, bool wait)
{
	struct bes_urb_context *urb_context = NULL;
	unsigned long flags;

	if (wait)
		bes_usb_wait_data_urb(pipe);
	spin_lock_irqsave(&pipe->ar_usb->cs_lock, flags);
	if (!list_empty(&pipe->urb_list_head)) {
		urb_context = list_first_entry(&pipe->urb_list_head, struct bes_urb_context, link);
		list_del(&urb_context->link);
		pipe->urb_cnt--;
	}
	spin_unlock_irqrestore(&pipe->ar_usb->cs_lock, flags);
	bes_dbg(BES_DBG_USB, "allc,pipe=%d,urb_cnt=%d\n", pipe->logical_pipe_num, pipe->urb_cnt);
	return urb_context;
}

static void bes_usb_free_urb_to_pipe(struct bes_usb_pipe *pipe, struct bes_urb_context *urb_context)
{
	unsigned long flags;

	spin_lock_irqsave(&pipe->ar_usb->cs_lock, flags);
	pipe->urb_cnt++;

	list_add(&urb_context->link, &pipe->urb_list_head);
	spin_unlock_irqrestore(&pipe->ar_usb->cs_lock, flags);

	if (wq_has_sleeper(&pipe->urb_wait_q)) {
		wake_up(&pipe->urb_wait_q);
	}

	bes_dbg(BES_DBG_USB, "free,pipe=%d,urb_cnt=%d\n", pipe->logical_pipe_num, pipe->urb_cnt);
}

static void bes_usb_cleanup_recv_urb(struct bes_urb_context *urb_context)
{
	if (urb_context->skb) {
		dev_kfree_skb(urb_context->skb);
		urb_context->skb = NULL;
	}
	bes_usb_free_urb_to_pipe(urb_context->pipe, urb_context);
}

static inline struct sbus_priv *bes_usb_priv(struct rwnx_plat *ar)
{
	return (struct sbus_priv *)ar->sbus_priv;
}

static inline void bes_usb_state_change(struct sbus_priv *device, enum bes_wf_usb_state state)
{
	device->state = state;
}

static void bes_usb_urb_free(struct bes_urb_context *urb_context)
{
	if (!urb_context)
		return;

#ifdef CONFIG_RWNX_AMSDUS_TX
	if (urb_context->amsdu_skb)
		kfree(urb_context->amsdu_skb);
#endif
	kfree(urb_context);
}

/* pipe resource allocation/cleanup */
static int bes_usb_alloc_pipe_resources(struct bes_usb_pipe *pipe, int urb_cnt)
{
	struct bes_urb_context *urb_context;
	int status = 0, i;

	INIT_LIST_HEAD(&pipe->urb_list_head);
	init_usb_anchor(&pipe->urb_submitted);
	init_waitqueue_head(&pipe->urb_wait_q);

	for (i = 0; i < urb_cnt; i++) {
		urb_context = kzalloc(sizeof(struct bes_urb_context), GFP_KERNEL);
		if (urb_context == NULL) {
			status = -ENOMEM;
			goto fail_alloc_pipe_resources;
		}

		urb_context->urb = usb_alloc_urb(0, GFP_ATOMIC);
		if (urb_context->urb == NULL) {
			bes_usb_urb_free(urb_context);
			status = -ENOMEM;
			goto fail_alloc_pipe_resources;
		}
		urb_context->pipe = pipe;

		/*
		 * we are only allocate the urb contexts here, the actual URB
		 * is allocated from the kernel as needed to do a transaction
		 */
		pipe->urb_alloc++;
		bes_usb_free_urb_to_pipe(pipe, urb_context);
	}

	bes_dbg(BES_DBG_USB, "BES usb: alloc resources lpipe:%d hpipe:0x%X urbs:%d\n", pipe->logical_pipe_num,
		pipe->usb_pipe_handle, pipe->urb_alloc);

fail_alloc_pipe_resources:
	return status;
}

static void bes_usb_free_pipe_resources(struct bes_usb_pipe *pipe)
{
	struct bes_urb_context *urb_context;

	if (pipe->ar_usb == NULL) {
		/* nothing allocated for this pipe */
		bes_dbg(BES_DBG_USB, "%s, %d, ar_usb is NULL!\n", __func__, __LINE__);
		return;
	}

	bes_dbg(BES_DBG_USB, "BES usb: free resources lpipe:%d"
		"hpipe:0x%X urbs:%d avail:%d\n",
		pipe->logical_pipe_num, pipe->usb_pipe_handle, pipe->urb_alloc, pipe->urb_cnt);

	if (pipe->urb_alloc != pipe->urb_cnt) {
		bes_dbg(BES_DBG_USB, "BES usb: urb leak! lpipe:%d"
			"hpipe:0x%X urbs:%d avail:%d\n",
			pipe->logical_pipe_num, pipe->usb_pipe_handle, pipe->urb_alloc, pipe->urb_cnt);
	}

	while (true) {
		urb_context = bes_usb_alloc_urb_from_pipe(pipe, false);
		if (urb_context == NULL)
			break;
		usb_free_urb(urb_context->urb);
		urb_context->urb = NULL;
		bes_usb_urb_free(urb_context);
	}
}

static void bes_usb_cleanup_pipe_resources(struct sbus_priv *ar_usb)
{
	int i;

	for (i = 0; i < BES_USB_PIPE_MAX; i++) {
		if ((i != BES_USB_PIPE_BT_RX_DATA) && (i != BES_USB_PIPE_BT_TX_DATA))
			bes_usb_free_pipe_resources(&ar_usb->pipes[i]);
	}
}

static u8 bes_usb_get_logical_pipe_num(struct sbus_priv *ar_usb, u8 ep_address, int *urb_count)
{
	u8 pipe_num = BES_USB_PIPE_INVALID;

	switch (ep_address) {
	case BES_USB_EP_ADDR_APP_CTRL_IN:
		pipe_num = BES_USB_PIPE_RX_CTRL;
		*urb_count = RX_URB_COUNT;
		break;
	case BES_USB_EP_ADDR_APP_DATA_IN:
		pipe_num = BES_USB_PIPE_RX_DATA;
		*urb_count = RX_URB_COUNT;
		break;
	case BES_USB_EP_ADDR_APP_DATA_BT_IN:
		pipe_num = BES_USB_PIPE_BT_RX_DATA;
		*urb_count = RX_URB_COUNT;
		break;
	case BES_USB_EP_ADDR_APP_CTRL_OUT:
		pipe_num = BES_USB_PIPE_TX_CTRL;
		*urb_count = TX_URB_COUNT;
		break;
	case BES_USB_EP_ADDR_APP_DATA_LP_OUT:
		pipe_num = BES_USB_PIPE_TX_DATA_LP;
		*urb_count = TX_DATA_URB_COUNT;
		break;
	case BES_USB_EP_ADDR_APP_DATA_BT_OUT:
		pipe_num = BES_USB_PIPE_BT_TX_DATA;
		*urb_count = TX_URB_COUNT;
		break;
	default:
		/* note: there may be endpoints not currently used */
		break;
	}

	return pipe_num;
}

static int bes_usb_setup_pipe_resources(struct sbus_priv *ar_usb)
{
	struct usb_interface *interface = ar_usb->interface;
	struct usb_host_interface *iface_desc = interface->cur_altsetting;
	struct usb_endpoint_descriptor *endpoint;
	struct bes_usb_pipe *pipe;
	int i, urbcount, status = 0;
	u8 pipe_num;

	bes_dbg(BES_DBG_USB, "setting up USB Pipes using interface\n");

	/* walk decriptors and setup pipes */
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (BES_USB_IS_BULK_EP(endpoint->bmAttributes)) {
			bes_dbg(BES_DBG_USB, "%s Bulk Ep:0x%2.2X maxpktsz:%d\n",
				BES_USB_IS_DIR_IN(endpoint->bEndpointAddress) ? "RX" : "TX",
				endpoint->bEndpointAddress, le16_to_cpu(endpoint->wMaxPacketSize));
		} else if (BES_USB_IS_INT_EP(endpoint->bmAttributes)) {
			bes_dbg(BES_DBG_USB, "%s Int Ep:0x%2.2X maxpktsz:%d interval:%d\n",
				BES_USB_IS_DIR_IN(endpoint->bEndpointAddress) ? "RX" : "TX",
				endpoint->bEndpointAddress, le16_to_cpu(endpoint->wMaxPacketSize), endpoint->bInterval);
		} else if (BES_USB_IS_ISOC_EP(endpoint->bmAttributes)) {
			/* TODO for ISO */
			bes_dbg(BES_DBG_USB, "%s ISOC Ep:0x%2.2X maxpktsz:%d interval:%d\n",
				BES_USB_IS_DIR_IN(endpoint->bEndpointAddress) ? "RX" : "TX",
				endpoint->bEndpointAddress, le16_to_cpu(endpoint->wMaxPacketSize), endpoint->bInterval);
		}
		urbcount = 0;

		pipe_num = bes_usb_get_logical_pipe_num(ar_usb, endpoint->bEndpointAddress, &urbcount);
		if (pipe_num == BES_USB_PIPE_INVALID)
			continue;

		pipe = &ar_usb->pipes[pipe_num];
		if (pipe->ar_usb != NULL) {
			/* hmmm..pipe was already setup */
			continue;
		}
		atomic_set(&pipe->urb_pending_cnt, 0);
		pipe->ar_usb = ar_usb;
		pipe->logical_pipe_num = pipe_num;
		pipe->ep_address = endpoint->bEndpointAddress;
		pipe->max_packet_size = le16_to_cpu(endpoint->wMaxPacketSize);

		if (BES_USB_IS_BULK_EP(endpoint->bmAttributes)) {
			if (BES_USB_IS_DIR_IN(pipe->ep_address)) {
				pipe->usb_pipe_handle = usb_rcvbulkpipe(ar_usb->udev, pipe->ep_address);
			} else {
				pipe->usb_pipe_handle = usb_sndbulkpipe(ar_usb->udev, pipe->ep_address);
			}
		} else if (BES_USB_IS_INT_EP(endpoint->bmAttributes)) {
			if (BES_USB_IS_DIR_IN(pipe->ep_address)) {
				pipe->usb_pipe_handle = usb_rcvintpipe(ar_usb->udev, pipe->ep_address);
			} else {
				pipe->usb_pipe_handle = usb_sndintpipe(ar_usb->udev, pipe->ep_address);
			}
		} else if (BES_USB_IS_ISOC_EP(endpoint->bmAttributes)) {
			/* TODO for ISO */
			if (BES_USB_IS_DIR_IN(pipe->ep_address)) {
				pipe->usb_pipe_handle = usb_rcvisocpipe(ar_usb->udev, pipe->ep_address);
			} else {
				pipe->usb_pipe_handle = usb_sndisocpipe(ar_usb->udev, pipe->ep_address);
			}
		}

		pipe->ep_desc = endpoint;

		if (!BES_USB_IS_DIR_IN(pipe->ep_address))
			pipe->flags |= BES_USB_PIPE_FLAG_TX;

		if ((pipe_num != BES_USB_PIPE_BT_RX_DATA) && (pipe_num != BES_USB_PIPE_BT_TX_DATA)) {
			status = bes_usb_alloc_pipe_resources(pipe, urbcount);
			if (status != 0)
				break;
		}
	}
#ifdef CONFIG_BES_BT
	status = bes_btusb_setup_pipes(ar_usb);
#endif

	return status;
}

/* pipe operations */
static void bes_usb_post_recv_transfers(struct bes_usb_pipe *recv_pipe, int buffer_length)
{
	struct bes_urb_context *urb_context;
	struct urb *urb;
	int usb_status;
	struct sbus_priv *device = NULL;

	if (recv_pipe)
		device = recv_pipe->ar_usb;
	if (device->state != BES_WF_USB_UP) {
		bes_err(BES_DBG_USB, "%s:state:%d, usb is not up\n", __func__, device->state);
		return;
	}
	while (true) {
		urb_context = bes_usb_alloc_urb_from_pipe(recv_pipe, false);
		if (urb_context == NULL) {
			bes_dbg(BES_DBG_USB, "post recv trans error! urb_ctxt failed!\n");
			break;
		}
		urb_context->skb = dev_alloc_skb(buffer_length);
		if (urb_context->skb == NULL)
			goto err_cleanup_urb;

		urb = urb_context->urb;
		BUG_ON(!urb);
		usb_fill_bulk_urb(urb, recv_pipe->ar_usb->udev, recv_pipe->usb_pipe_handle, urb_context->skb->data,
				  buffer_length, bes_usb_recv_complete, urb_context);

		bes_dbg(BES_DBG_USB, "BES usb: bulk recv submit:%d, 0x%X (ep:0x%2.2X), %d bytes buf:0x%p\n",
			recv_pipe->logical_pipe_num, recv_pipe->usb_pipe_handle, recv_pipe->ep_address, buffer_length,
			urb_context->skb);

		usb_anchor_urb(urb, &recv_pipe->urb_submitted);
		usb_status = usb_submit_urb(urb, GFP_ATOMIC);
		if (usb_status) {
			bes_err(BES_DBG_USB, "BES usb : usb bulk recv failed %d\n", usb_status);
			usb_unanchor_urb(urb);
			goto err_cleanup_urb;
		}
	}
	return;

err_cleanup_urb:
	bes_usb_cleanup_recv_urb(urb_context);
	return;
}

static void bes_usb_flush_all(struct sbus_priv *ar_usb)
{
	int i;

	for (i = 0; i < BES_USB_PIPE_MAX; i++) {
		if ((i != BES_USB_PIPE_BT_RX_DATA) && (i != BES_USB_PIPE_BT_TX_DATA)) {
			if (ar_usb->pipes[i].ar_usb != NULL)
				usb_kill_anchored_urbs(&ar_usb->pipes[i].urb_submitted);
		}
	}

	/*
	 * Flushing any pending I/O may schedule work this call will block
	 * until all scheduled work runs to completion.
	 */
	if (ar_usb != NULL && ar_usb->bes_io_wq != NULL) {
		flush_workqueue(ar_usb->bes_io_wq);
		destroy_workqueue(ar_usb->bes_io_wq);
	}
}

static void bes_usb_start_recv_pipes(struct sbus_priv *ar_usb)
{
	/*
	 * note: control pipe is no longer used
	 * ar_usb->pipes[BES_USB_PIPE_RX_CTRL].urb_cnt_thresh =
	 *      ar_usb->pipes[BES_USB_PIPE_RX_CTRL].urb_alloc/2;
	 * bes_usb_post_recv_transfers(&ar_usb->
	 *		pipes[BES_USB_PIPE_RX_CTRL],
	 *		BES_USB_RX_BUFFER_SIZE);
	 */
	bes_usb_state_change(ar_usb, BES_WF_USB_UP);
	ar_usb->pipes[BES_USB_PIPE_RX_DATA].urb_cnt_thresh = 1;

	bes_usb_post_recv_transfers(&ar_usb->pipes[BES_USB_PIPE_RX_DATA], BES_USB_RX_BUFFER_SIZE);
}

void bes_recv_buf_put(struct sbus_priv *ar_usb, struct sk_buff *skb)
{
	unsigned long flags;
	if (!ar_usb || !skb)
		return;

	spin_lock_irqsave(&ar_usb->rx_queue_lock, flags);
	skb_queue_tail(&ar_usb->rx_queue, skb);
	spin_unlock_irqrestore(&ar_usb->rx_queue_lock, flags);
}

struct sk_buff *bes_recv_buf_get(struct sbus_priv *ar_usb)
{
	unsigned long flags;
	struct sk_buff *skb = NULL;

	spin_lock_irqsave(&ar_usb->rx_queue_lock, flags);
	skb = skb_dequeue(&ar_usb->rx_queue);
	spin_unlock_irqrestore(&ar_usb->rx_queue_lock, flags);

	return skb;
}
/* hif usb rx/tx completion functions */
static void bes_usb_recv_complete(struct urb *urb)
{
	struct bes_urb_context *urb_context = urb->context;
	struct bes_usb_pipe *pipe = urb_context->pipe;
	struct sk_buff *skb = NULL;
	struct sbus_priv *device = NULL;
	int status = 0;

	bes_dbg(BES_DBG_USB, "%s: recv pipe: %d, stat:%d, len:%d urb:0x%p\n", __func__, pipe->logical_pipe_num, urb->status,
		urb->actual_length, urb);

	if (urb->status != 0) {
		status = -EIO;
		switch (urb->status) {
		case -ECONNRESET:
		case -ENOENT:
		case -ESHUTDOWN:
			/*
			 * no need to spew these errors when device
			 * removed or urb killed due to driver shutdown
			 */
			status = -ECANCELED;
			break;
		default:
			bes_err(BES_DBG_USB, "%s recv pipe: %d (ep:0x%2.2X), failed:%d\n", __func__, pipe->logical_pipe_num,
				pipe->ep_address, urb->status);
			break;
		}
		goto cleanup_recv_urb;
	}
	if (pipe)
		device = pipe->ar_usb;
	if (device->state != BES_WF_USB_UP) {
		bes_err(BES_DBG_USB, "%s:state:%d, usb is not up\n", __func__, device->state);
		bes_tx_loop_force_close(&g_rwnx_plat, true);
		goto cleanup_recv_urb;
	}
	if (urb->actual_length == 0)
		goto cleanup_recv_urb;

	skb = urb_context->skb;
	skb_put(skb, urb->actual_length);

	/* we are going to pass it up */
	urb_context->skb = NULL;

#ifdef FW_DOWNLOAD_BY_USB
	if (!fw_download_finish_flag) {
		if (bes_usb_test_control_reg(pipe->ar_usb, BES_USB_FW_RX_INDICATION))
			bes_usb_set_status_reg(pipe->ar_usb, BES_USB_FW_RX_INDICATION);
		bes_recv_buf_put(pipe->ar_usb, skb);
		bes_usb_irq_handler(pipe->ar_usb);
	} else
#endif
	{
		skb_queue_tail(&pipe->io_comp_queue, skb);
		queue_work(device->bes_io_wq, &pipe->io_complete_work);
	}

cleanup_recv_urb:
	bes_usb_cleanup_recv_urb(urb_context);
	usb_unanchor_urb(urb);

	if (status == 0 && pipe->urb_cnt >= pipe->urb_cnt_thresh) {
		/* our free urbs are piling up, post more transfers */
		bes_usb_post_recv_transfers(pipe, BES_USB_RX_BUFFER_SIZE);
	}
}

static void bes_usb_transmit_complete(struct urb *urb)
{
	struct bes_urb_context *urb_context = urb->context;
	struct bes_usb_pipe *pipe = urb_context->pipe;
	struct sbus_priv *device = NULL;
	int send_cnt = atomic_read(&pipe->urb_pending_cnt);

	bes_dbg(BES_DBG_USB, "%s: pipe: %d, stat:%d, len:%d\n", __func__, pipe->logical_pipe_num, urb->status, urb->actual_length);
	if (pipe)
		device = pipe->ar_usb;
	if (device->state != BES_WF_USB_UP)
		bes_err(BES_DBG_USB, "%s:state:%d, usb is not up\n", __func__, device->state);
	if (urb->status != 0) {
		bes_err(BES_DBG_USB, "%s:  pipe: %d, failed:%d\n", __func__, pipe->logical_pipe_num, urb->status);
	}

	urb_context->skb = NULL;
#ifdef CONFIG_RWNX_AMSDUS_TX
	if (urb_context->amsdu_skb) {
		kfree(urb_context->amsdu_skb);
		urb_context->amsdu_skb = NULL;
	}
#endif
	bes_usb_free_urb_to_pipe(urb_context->pipe, urb_context);
	usb_unanchor_urb(urb);
	atomic_dec(&pipe->urb_pending_cnt);
#ifdef FW_DOWNLOAD_BY_USB
	if (!fw_download_finish_flag) {
		if (!urb->status && bes_usb_test_control_reg(pipe->ar_usb, BES_USB_FW_TX_DONE))
			bes_usb_set_status_reg(pipe->ar_usb, BES_USB_FW_TX_DONE);
		// notify tx done event
		bes_usb_irq_handler(pipe->ar_usb);
	}
#endif
	bes_dbg(BES_DBG_USB, "trans cmpl,send_cnt=%d, urb->status=%d\n", send_cnt, urb->status);
}

void bes_usb_rx_complete(struct rwnx_plat *priv, struct sk_buff *skb)
{
	/* may recv data before platform on */
	if (priv)
		bes_psa_rx_handler(priv, skb);
	else
		dev_kfree_skb(skb);

	return;
}

void bes_core_rx_complete(struct rwnx_plat *ar, struct sk_buff *skb, u8 pipe)
{
	if(!bes_tx_loop_is_enabled(&g_rwnx_plat)) {
		// bes_htc_rx_complete(ar, skb, pipe);
		bes_usb_rx_complete(ar, skb);
	} else {
		dev_kfree_skb(skb);
	}
}
EXPORT_SYMBOL(bes_core_rx_complete);

static void bes_usb_io_comp_work(struct work_struct *work)
{
	struct bes_usb_pipe *pipe = container_of(work, struct bes_usb_pipe, io_complete_work);
	struct sbus_priv *ar_usb;
	struct sk_buff *skb;

	ar_usb = pipe->ar_usb;

	while ((skb = skb_dequeue(&pipe->io_comp_queue))) {
		bes_core_rx_complete(ar_usb->ar, skb, pipe->logical_pipe_num);
	}

	if (ar_usb->ar) {
		struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)ar_usb->ar->drvdata;
		if (rwnx_hw && spin_trylock_bh(&rwnx_hw->tx_lock)) {
			rwnx_hwq_process_all(rwnx_hw);
			spin_unlock_bh(&rwnx_hw->tx_lock);
		}
	}
}

int bes_usb_send(struct sbus_priv *self, u8 pipe_id, u8 *data_buf, u32 len, bool need_urb_free)
{
	struct sbus_priv *device = self;
	struct bes_usb_pipe *pipe = &device->pipes[pipe_id];
	struct bes_urb_context *urb_context;
	int usb_status, status = 0;
	struct urb *urb;
	int send_cnt = atomic_read(&pipe->urb_pending_cnt);

	if (device->state != BES_WF_USB_UP) {
		bes_err(BES_DBG_USB, "%s:state:%d, usb is not up\n", __func__, device->state);
		goto fail_hif_send;
	}

	urb_context = bes_usb_alloc_urb_from_pipe(pipe, true);
	BUG_ON(urb_context == NULL);

#ifdef CONFIG_RWNX_AMSDUS_TX
	if (need_urb_free)
		urb_context->amsdu_skb = data_buf;
#endif

	urb_context->skb = (struct sk_buff *)data_buf;
	urb = urb_context->urb;
	BUG_ON(!urb);
	usb_fill_bulk_urb(urb, device->udev, pipe->usb_pipe_handle, (void *)data_buf, len, bes_usb_transmit_complete, urb_context);
	bes_dbg(BES_DBG_USB, "%s usb_dev:%p.\n", __func__, device->udev);

	if ((len % pipe->max_packet_size) == 0) {
		/* hit a max packet boundary on this pipe */
		urb->transfer_flags |= URB_ZERO_PACKET;
	}

	bes_dbg(BES_DBG_USB, "athusb bulk send submit:%d, 0x%X (ep:0x%2.2X), %d bytes\n", pipe->logical_pipe_num,
		pipe->usb_pipe_handle, pipe->ep_address, len);

	usb_anchor_urb(urb, &pipe->urb_submitted);
	usb_status = usb_submit_urb(urb, GFP_ATOMIC);

	atomic_inc(&pipe->urb_pending_cnt);
	if (usb_status) {
		bes_err(BES_DBG_USB, "BES usb : usb bulk transmit failed %d\n", usb_status);
		usb_unanchor_urb(urb);
		bes_usb_free_urb_to_pipe(urb_context->pipe, urb_context);
		atomic_dec(&pipe->urb_pending_cnt);
		status = -EINVAL;
	}

fail_hif_send:
	bes_dbg(BES_DBG_USB, "usb_send, send_cnt=%d, usb_stat=%d, stat=%d\n", send_cnt, usb_status, status);
	return status;
}

static int __bes_usb_data_push(u8 *data_buf, u32 len, bool need_urb_free)
{
	int ret;

	ret = bes_usb_send(g_rwnx_plat.sbus_priv, BES_USB_PIPE_TX_DATA_LP, data_buf, len, need_urb_free);
	if (ret)
		bes_err(BES_DBG_USB,"usb send data err:%d, exit\n", ret);

	return ret;
}

static int __bes_usb_send_one_packet(struct list_head *list)
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
		buf = (u8 *)kmalloc(BES_TRANSFER_LEN_LIMIT, GFP_KERNEL);
		if (!buf) {
			bes_psa_free_tx_buffer(list);
			return -ENOMEM;
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

	/* If do_copy, free buf in bes_usb_transmit_complete */
	ret = __bes_usb_data_push(buf, len, do_copy);
	if (ret && buf && do_copy)
		kfree(buf);

	return ret;
}

int bes_usb_data_push(const struct tx_packet *pkt)
{
	if (pkt->is_amsdu) {
#ifdef CONFIG_RWNX_AMSDUS_TX
		u8 i;

		for (i = 0; i < pkt->cnt; i++) {
			int ret = __bes_usb_send_one_packet((struct list_head *)&pkt->list[i]);
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
		return __bes_usb_send_one_packet((struct list_head *)&pkt->list[0]);
	}
}

int bes_usb_msg_push(void *addr, u32 len)
{
	return __bes_usb_data_push(addr, len, false);
}


void *bes_usb_read(struct sbus_priv *self)
{
	return (void *)bes_recv_buf_get(self);
}

static void bes_usb_destroy(struct sbus_priv *ar_usb)
{
	bes_usb_flush_all(ar_usb);

	bes_usb_cleanup_pipe_resources(ar_usb);

	usb_set_intfdata(ar_usb->interface, NULL);

	kfree(ar_usb);
}

static struct sbus_priv *bes_usb_create(struct usb_interface *interface)
{
	struct usb_device *dev = interface_to_usbdev(interface);
	struct sbus_priv *ar_usb;
	struct bes_usb_pipe *pipe;
	int status = 0;
	int i;

	ar_usb = kzalloc(sizeof(struct sbus_priv), GFP_KERNEL);
	if (ar_usb == NULL)
		goto fail_bes_usb_create;

	usb_set_intfdata(interface, ar_usb);
	spin_lock_init(&(ar_usb->cs_lock));
	ar_usb->udev = dev;
	ar_usb->interface = interface;
	ar_usb->ar = NULL;

	ar_usb->bes_io_wq = alloc_workqueue("bes_io_wq", 0, 0);
	if (!ar_usb->bes_io_wq) {
		status = -ENOMEM;
		goto fail_bes_usb_create;
	}

	for (i = 0; i < BES_USB_PIPE_MAX; i++) {
		if ((i != BES_USB_PIPE_BT_RX_DATA) && (i != BES_USB_PIPE_BT_TX_DATA)) {
			pipe = &ar_usb->pipes[i];
			INIT_WORK(&pipe->io_complete_work, bes_usb_io_comp_work);
			skb_queue_head_init(&pipe->io_comp_queue);
		}
	}

	spin_lock_init(&ar_usb->rx_queue_lock);
	skb_queue_head_init(&ar_usb->rx_queue);
#ifdef FW_DOWNLOAD_BY_USB
	spin_lock_init(&ar_usb->status_lock);
	ar_usb->int_control_reg = 0;
	ar_usb->int_status_reg = 0;
#endif

	status = bes_usb_setup_pipe_resources(ar_usb);

fail_bes_usb_create:
	if (status != 0) {
		bes_err(BES_DBG_USB, "%s %d.\n", __func__, __LINE__);
		bes_usb_destroy(ar_usb);
		ar_usb = NULL;
	}
	return ar_usb;
}

int bes_usb_init(void *self_p, struct rwnx_plat *rwnx_plat)
{
	struct sbus_priv *self = (struct sbus_priv *)self_p;

	self->ar = rwnx_plat;

	return 0;
}

int bes_usb_reset(struct sbus_priv *self)
{
	self->ar = NULL;
	return 0;
}

void bes_usb_and_platform_destroy(struct sbus_priv *self)
{
	struct rwnx_hw *rwnx_hw = NULL;
	struct rwnx_plat *rwnx_plat = NULL;

#ifdef CONFIG_BES_BT
	bes_btusb_uninit(self);
#endif

	if (self) {
		bes_usb_state_change(self, BES_WF_USB_DOWN);

		rwnx_plat = self->ar;
		if (rwnx_plat) {
			rwnx_hw = rwnx_plat->drvdata;
			self->ar = NULL;
		}
		if (rwnx_hw) {
			bes_power_ctrl_cancel_wlan_close_work(rwnx_hw);
#ifndef CONFIG_BES_TXLOOP
			rwnx_hw->cmd_mgr.state = RWNX_CMD_MGR_STATE_CRASHED; // avoid cmd timeout
#endif
			rwnx_platform_deinit(rwnx_hw);
			if (!rwnx_plat->pre_alloc)
				rwnx_plat->drvdata = NULL;
		}
		if (rwnx_plat)
			rwnx_plat->sbus_priv = NULL;
		bes_usb_destroy(self);
	}

#ifdef FW_DOWNLOAD_BY_USB
	fw_download_finish_flag = false;
#endif
}

static void bes_usb_init_plat_cfg(struct sbus_priv *self)
{
	g_rwnx_plat.sbus_priv = self;
	self->ar = &g_rwnx_plat;
	g_rwnx_plat.dev_status = 0;
	g_rwnx_plat.wakelock_flag = 0;
	g_rwnx_plat.dev = &self->udev->dev;
	g_rwnx_plat.run_flags = 0;
	spin_lock_init(&g_rwnx_plat.run_flags_lock);
	spin_lock_init(&g_rwnx_plat.pwr_io_lock);
	init_waitqueue_head(&g_rwnx_plat.dev_stat_wait_q);
}

static void bes_usb_deinit_plat_cfg(void)
{
	g_rwnx_plat.sbus_priv = NULL;
	g_rwnx_plat.dev_status = 0;
	bes_tx_loop_deinit(&g_rwnx_plat);
}

int bes_usb_platform_init(struct device *pdev, void *sbus_priv)
{
	int ret = 0;
	void *drvdata;

	bes_usb_init(sbus_priv, &g_rwnx_plat);
	bes_err(BES_DBG_PSA, "%s %d.\n", __func__, __LINE__);

	spin_lock_init(&g_rwnx_plat.tx_lock);

	g_rwnx_plat.usb_dev = pdev;
	g_rwnx_plat.data_push = bes_usb_data_push;
	g_rwnx_plat.msg_push = bes_usb_msg_push;

	ret = rwnx_platform_init(&g_rwnx_plat, &drvdata);

	return ret;
}

static inline void bes_usb_wake_ctrl(struct rwnx_hw *rwnx_hw, bool wakeup)
{
	if (wakeup)
		rwnx_platform_wakeup_mcu(rwnx_hw->plat, IO_LOCK_SRC_WIFI_LP, true);
	else
		rwnx_platform_sleep_mcu(rwnx_hw->plat, IO_LOCK_SRC_WIFI_LP, true);
}

int bes_usb_platform_on(struct rwnx_hw *rwnx_hw)
{
	int ret = 0;
	struct rwnx_plat *rwnx_plat = rwnx_hw->plat;
	unsigned long flags;

	ret = bes_pwr_init(rwnx_hw, bes_usb_wake_ctrl);
	if (ret)
		return ret;
	bes_filter_manager_init(rwnx_hw);
	bes_wowlan_init(rwnx_hw);
	bes_lstats_init(rwnx_hw);
	rwnx_wf_status_set(rwnx_hw, RWNX_DEV_STACK_RESTARTING);

	/* Clear all run flags */
	spin_lock_irqsave(&rwnx_plat->run_flags_lock, flags);
	rwnx_plat->run_flags = 0;
	spin_unlock_irqrestore(&rwnx_plat->run_flags_lock, flags);

	init_waitqueue_head(&rwnx_plat->tx_wait_q);
	rwnx_plat->a2e_msg = kzalloc(RWNX_MSG_MAX_LEN, GFP_KERNEL);
	if (rwnx_plat->a2e_msg == NULL)
		goto err_pwr_init;

	skb_queue_head_init(&g_rwnx_plat.tx_q);
	if ((ret = bes_bus_tx_init(rwnx_plat)) < 0) {
		bes_err(BES_DBG_PSA, "%s bus tx thread create fail\n", __func__);
		goto err_a2e_msg_alloc;
	}

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

void bes_usb_platform_off(struct rwnx_hw *rwnx_hw)
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

void bes_firmware_work_func(struct work_struct *work)
{
	int ret = 0;
	struct bes_power_state power_state;

	/* set boot wait state BES_BOOT_WAIT_DOWNLOAD_FW. */
	bes_set_boot_wait_state(BES_BOOT_WAIT_DOWNLOAD_FW);

	/* in bus detection process ,when device is detected, notification is sent. */
	bes_power_ctrl_bus_probe_notify();

	/* tx_loop mechanism related initialization. */
	bes_tx_loop_init(&g_rwnx_plat);
#ifdef FW_DOWNLOAD_BY_USB
	/* load firmware onto devices through USB interface. */
	ret = bes_load_firmware_usb(g_rwnx_plat.self, &g_rwnx_plat);
	if (!ret) {
		bes_info(BES_DBG_USB, "%s:%d, bes_load_firmware_usb over\n", __func__, __LINE__);
		fw_download_finish_flag = true;
	}

	if (ret < 0) {
		bes_err(BES_DBG_USB, "%s %d. fw download by usb error!\n", __func__, ret);
		goto err_core_free;
	}
#endif

	/* bt testmode not register wlan */
	if (bes_sysfs_get_fw_type() == BES_FW_TYPE_BT)
		goto out;

	/* When dpd data has been obtained and only bt is enabled, wlan0 will not be registered */
	if (bes_power_ctrl_can_skip_platform_init(&g_rwnx_plat))
		goto out;

	/* platform initialization related to USB interface. */
	ret = bes_usb_platform_init(&g_rwnx_plat.self->udev->dev, g_rwnx_plat.self);
	if (ret) {
		bes_err(BES_DBG_USB, "register netdev fail\n");
		goto err_core_free;
	}

	/* query the current status of power control. */
	bes_power_ctrl_get_power_state(&power_state);

	/* When in signal mode and WiFi power is turned off, unregister WiFi related network devices.*/
	if (bes_sysfs_is_signal_mode() && !power_state.wifi_power)
		bes_unregister_net_dev_after_get_dpd((struct rwnx_hw *)(g_rwnx_plat.drvdata));

	/* if WiFi and Bt are turned off, turn off the power. */
	if (!power_state.wifi_power && !power_state.bt_power) {
		bes_power_ctrl_do_system_close();
		return ;
	}
out:
	/* enable the system to control the power status of the system.*/
	if ((ret = bes_power_ctrl_system_enabled(true, false)) < 0)
		goto err_core_free;

	bes_info(BES_DBG_USB, "bes firmware work func over.\n");
	return ;

err_core_free:
#ifdef FW_DOWNLOAD_BY_USB
	fw_download_finish_flag = false;
#endif
	/* set boot wait state BES_BOOT_WAIT_PROBE_CLOSE. */
	bes_set_boot_wait_state(BES_BOOT_WAIT_PROBE_CLOSE);

	/* system is disabled to control the power status of the system.*/
	bes_power_ctrl_system_enabled(false, true);

	/* host pull down the PWR pin of the chip.*/
	bes_chip_power(&g_rwnx_plat, false);
}

/* BES usb driver registered functions */
static int bes_usb_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(interface);
	int vendor_id, product_id;
	int ret = 0;
	int i;

	usb_get_dev(dev);

	vendor_id = le16_to_cpu(dev->descriptor.idVendor);
	product_id = le16_to_cpu(dev->descriptor.idProduct);
	bes_err(BES_DBG_USB, "vendor_id = %04x\n", vendor_id);
	bes_err(BES_DBG_USB, "product_id = %04x\n", product_id);

	bes_sysfs_set_device_id(product_id);
	bes_usb_power_ctrl_get_dev(dev);

	if (interface->cur_altsetting)
		bes_dbg(BES_DBG_USB, "USB Interface %d\n", interface->cur_altsetting->desc.bInterfaceNumber);

	switch (dev->speed) {
		case USB_SPEED_LOW:
		case USB_SPEED_FULL:
			bes_dbg(BES_DBG_USB, "USB 1.1 Host\n");
			break;
		case USB_SPEED_HIGH:
			bes_dbg(BES_DBG_USB, "USB 2.0 Host\n");
			break;
		case USB_SPEED_SUPER:
			bes_dbg(BES_DBG_USB, "USB 3.0 Host\n");
			break;
		default:
			bes_dbg(BES_DBG_USB, "USB 1.1 Host\n");
			break;
	}

	g_rwnx_plat.self = bes_usb_create(interface);
	if (g_rwnx_plat.self == NULL) {
		ret = -ENOMEM;
		bes_err(BES_DBG_USB, "USB create failed!\n");
		goto err_usb_put;
	}

	bes_usb_init_plat_cfg(g_rwnx_plat.self);
	bes_usb_power_ctrl_get_sbus_priv(g_rwnx_plat.self);

	bes_usb_start_recv_pipes(g_rwnx_plat.self);
	/* set the TX resource avail threshold for each TX pipe */
	for (i = BES_USB_PIPE_TX_CTRL; i <= BES_USB_PIPE_BT_TX_DATA; i++) {
		g_rwnx_plat.self->pipes[i].urb_cnt_thresh = g_rwnx_plat.self->pipes[i].urb_alloc / 2;
	}
	bes_dbg(BES_DBG_USB, "%s usb_dev:%p.\n", __func__, dev);

	INIT_WORK((&g_rwnx_plat.download_fw_work), bes_firmware_work_func);

	schedule_work(&g_rwnx_plat.download_fw_work);

	bes_info(BES_DBG_USB, "bes usb probe over.\n");
	return 0;
err_usb_put:
	usb_put_dev(dev);

	bes_set_boot_wait_state(BES_BOOT_WAIT_PROBE_CLOSE);
	bes_power_ctrl_system_enabled(false, true);
	bes_chip_power(&g_rwnx_plat, false);
	usb_set_intfdata(interface, NULL);

	return 0;
}

static void bes_usb_remove(struct usb_interface *interface)
{
	struct sbus_priv *self = NULL;
	bool status_reset;

	bes_dbg(BES_DBG_USB, RWNX_FN_ENTRY_STR);

	bes_tx_loop_set_dev_disconnect(&g_rwnx_plat);
	bes_tx_loop_force_close(&g_rwnx_plat, false);

	self = usb_get_intfdata(interface);
	if (self) {
		usb_put_dev(interface_to_usbdev(interface));
		bes_usb_and_platform_destroy(self);
	}

	bes_usb_deinit_plat_cfg();
	bes_power_ctrl_system_enabled(false, false);
	bes_chip_power(&g_rwnx_plat, false);
	/* update fw_type after command reset. case for message reset of mcu */
	status_reset = bes_power_ctrl_is_chg_fw_type();
	bes_sysfs_status_update(status_reset);

	bes_usb_power_ctrl_set_remove();
}

#define bes_usb_pm_suspend NULL
#define bes_usb_pm_resume NULL

/* table of devices that work with this driver */
static const struct usb_device_id bes_usb_ids[] = {
	{USB_DEVICE(0xBE57, 0x2005)},
	{USB_DEVICE(0xBE57, 0x2007)},
	{USB_DEVICE(0xBE57, 0x207a)},
	{/* Terminating entry */},
};

MODULE_DEVICE_TABLE(usb, bes_usb_ids);

static struct usb_driver bes_usb_driver = {
	.name = "bes_usb",
	.probe = bes_usb_probe,
	.suspend = bes_usb_pm_suspend,
	.resume = bes_usb_pm_resume,
	.disconnect = bes_usb_remove,
	.id_table = bes_usb_ids,
	.supports_autosuspend = true,
	.disable_hub_initiated_lpm = 1,
};

// module_usb_driver(bes_usb_driver);
int bes_usb_register_drv(void)
{
	int ret;

	/* keep first */
	ret = bes_power_ctrl_usb_init(&g_rwnx_plat);
	if (ret)
		return ret;

	ret = bes_sysfs_register(&g_rwnx_plat);
	if (ret)
		goto register_sysfs_err;

	bes_sysfs_status_update(true);

	bes_usb_plat_power(&g_rwnx_plat, true);

	bes_dbg(BES_DBG_USB, "%s:%d, register_drv\n", __func__, __LINE__);
	if ((ret = usb_register(&bes_usb_driver)) < 0) {
		bes_err(BES_DBG_USB, "usb_register error");
		goto register_err;
	}
	return 0;

register_err:
	bes_power_ctrl_cancel_probe_work();
	bes_sysfs_unregister();
register_sysfs_err:
	bes_power_ctrl_usb_deinit(&g_rwnx_plat);

	return ret;
}

void bes_usb_unregister_drv(void)
{
	struct rwnx_hw *rwnx_hw;

	bes_power_ctrl_prep_rmmod(&g_rwnx_plat);
	bes_check_usb_dev_state();
	rwnx_hw = g_rwnx_plat.drvdata;
	usb_deregister(&bes_usb_driver);

	bes_sysfs_unregister();
	bes_power_ctrl_usb_deinit(&g_rwnx_plat);
}

MODULE_AUTHOR("Bestechnic, Inc.");
MODULE_DESCRIPTION("Driver support for BES2610 wireless USB devices");
MODULE_LICENSE("Dual BSD/GPL");
