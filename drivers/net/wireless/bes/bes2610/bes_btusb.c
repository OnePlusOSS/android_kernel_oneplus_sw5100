/*
 * @file bes_btusb.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>

#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <linux/device.h>
#include <linux/firmware.h>

#include <linux/usb.h>

#include "bes_usb.h"
#include "bes_log.h"
#include "bes_sysfs.h"

#define BES_BTUSB_MAX_BULK_TX 2
#define BES_BTUSB_MAX_BULK_RX 2

enum {
	CQ_OK = 0,
	CQ_ERR,
};

typedef unsigned char CQItemType;

typedef struct __CQueue {
	int read;
	int write;
	int size;
	int len;
	CQItemType *base;
} CQueue;

struct bes_btusb_data {
	struct usb_device *udev;
	struct bes_usb_pipe *rx_pipes;
	struct bes_usb_pipe *tx_pipes;
	struct hci_dev *hdev;

	unsigned long state;

	rwlock_t lock;

	struct sk_buff_head transmit_q;
	struct work_struct tx_work;

	atomic_t pending_tx;
	struct sk_buff_head pending_q;
	CQueue bt_rx_queue;
	unsigned char *rx_buffer;
};

struct bes_btusb_data_scb {
	struct urb *urb;
};

static int bes_btusb_rx_submit(struct bes_btusb_data *data, struct urb *urb);
static void bes_btusb_btrf_get_resp(CQueue *bt_rx_queue, int count);

int InitCQueue(CQueue *Q, unsigned int size, CQItemType *buf)
{
	Q->size = size;
	Q->base = buf;
	Q->len = 0;
	if (!Q->base)
		return CQ_ERR;

	Q->read = Q->write = 0;
	return CQ_OK;
}

int IsEmptyCQueue(CQueue *Q)
{
	if (Q->len == 0)
		return CQ_OK;
	else
		return CQ_ERR;
}

int LengthOfCQueue(CQueue *Q)
{
	return Q->len;
}

int AvailableOfCQueue(CQueue *Q)
{
	return (Q->size - Q->len);
}

int EnCQueue(CQueue *Q, CQItemType *e, unsigned int len)
{
	uint32_t bytesToTheEnd = 0;

	if (AvailableOfCQueue(Q) < len) {
		return CQ_ERR;
	}

	Q->len += len;

	bytesToTheEnd = Q->size - Q->write;
	if (bytesToTheEnd > len) {
		memcpy((uint8_t *)&Q->base[Q->write], (uint8_t *)e, len);
		Q->write += len;
	} else {
		memcpy((uint8_t *)&Q->base[Q->write], (uint8_t *)e, bytesToTheEnd);
		memcpy((uint8_t *)&Q->base[0], (((uint8_t *)e) + bytesToTheEnd), len - bytesToTheEnd);
		Q->write = len - bytesToTheEnd;
	}

	return CQ_OK;
}

int PeekCQueue(CQueue *Q, unsigned int len_want, CQItemType **e1, unsigned int *len1, CQItemType **e2,
	       unsigned int *len2)
{
	if (LengthOfCQueue(Q) < len_want) {
		return CQ_ERR;
	}

	*e1 = &(Q->base[Q->read]);
	if ((Q->write > Q->read) || (Q->size - Q->read >= len_want)) {
		*len1 = len_want;
		*e2 = NULL;
		*len2 = 0;
		return CQ_OK;
	} else {
		*len1 = Q->size - Q->read;
		*e2 = &(Q->base[0]);
		*len2 = len_want - *len1;
		return CQ_OK;
	}

	return CQ_ERR;
}

int PeekCQueueToBuf(CQueue *Q, CQItemType *e, unsigned int len)
{
	int status = CQ_OK;
	unsigned char *e1 = NULL, *e2 = NULL;
	unsigned int len1 = 0, len2 = 0;

	status = PeekCQueue(Q, len, &e1, &len1, &e2, &len2);

	if (status == CQ_OK) {
		if (len == (len1 + len2)) {
			memcpy(e, e1, len1);
			memcpy(e + len1, e2, len2);
		} else {
			status = CQ_ERR;
		}
	}

	return status;
}

int DeCQueue(CQueue *Q, CQItemType *e, unsigned int len)
{
	if (LengthOfCQueue(Q) < len)
		return CQ_ERR;

	Q->len -= len;

	if (e != NULL) {
		uint32_t bytesToTheEnd = Q->size - Q->read;
		if (bytesToTheEnd > len) {
			memcpy((uint8_t *)e, (uint8_t *)&Q->base[Q->read], len);
			Q->read += len;
		} else {
			memcpy((uint8_t *)e, (uint8_t *)&Q->base[Q->read], bytesToTheEnd);
			memcpy((((uint8_t *)e) + bytesToTheEnd), (uint8_t *)&Q->base[0], len - bytesToTheEnd);
			Q->read = len - bytesToTheEnd;
		}
	} else {
		if (0 < Q->size) {
			Q->read = (Q->read + len) % Q->size;
		} else {
			Q->read = 0;
		}
	}

	return CQ_OK;
}

static int bes_btusb_recv_bulk(struct bes_btusb_data *data, void *buffer, int count)
{
	struct sk_buff *skb = NULL;
	int err = 0;
	unsigned char bt_rx_hdr[5];
	unsigned short frame_len = 0;
	unsigned char head_length = 0;
	unsigned char *raw_data = NULL;
	struct bes_sysfs *bes_sysfs = bes_get_sysfs();
	{
		EnCQueue(&data->bt_rx_queue, (CQItemType *)buffer, count);

		while (1) {
			frame_len = 0;
			head_length = 0;
			if (LengthOfCQueue(&data->bt_rx_queue) >= 4) {
				PeekCQueueToBuf(&data->bt_rx_queue, (CQItemType *)bt_rx_hdr, 4);
				if (bt_rx_hdr[0] == HCI_EVENT_PKT) {
					head_length = 3;
					frame_len = bt_rx_hdr[2] + head_length;
					if ((bt_rx_hdr[1] == BTRF_VENDOR_EVENT_PKT &&
					    bes_sysfs_get_fw_type() == BES_FW_TYPE_BT) ||
					    (bt_rx_hdr[1] == HCI_VENDOR_PKT && bt_rx_hdr[3] == HCI_FACTORY_SUBCODE)) {
						if (bes_sysfs->btrf_priv.testmode)
							bes_btusb_btrf_get_resp(&data->bt_rx_queue, frame_len);
						DeCQueue(&data->bt_rx_queue, NULL, frame_len);
						break;
					}
				} else if (bt_rx_hdr[0] == HCI_ACLDATA_PKT) {
					head_length = 5;
					if (LengthOfCQueue(&data->bt_rx_queue) >= head_length) {
						PeekCQueueToBuf(&data->bt_rx_queue, (CQItemType *)(bt_rx_hdr),
								head_length);
					} else {
						break;
					}
					frame_len = (bt_rx_hdr[3] | (bt_rx_hdr[4] << 8)) + head_length;
				} else if (bt_rx_hdr[0] == HCI_SCODATA_PKT) {
					head_length = 4;
					frame_len = bt_rx_hdr[3] + head_length;
				} else {
					bes_err(BES_DBG_BTUSB, "Invalid packet type:%02x\n", bt_rx_hdr[0]);
				}
			} else {
				break;
			}

			/* test mode does not accept all frames */
			if (!bes_sysfs_is_signal_mode())
				break;

			bes_dbg(BES_DBG_BTUSB, "btrx flen:%d\n", frame_len);
			if (LengthOfCQueue(&data->bt_rx_queue) >= frame_len) {
				skb = bt_skb_alloc(HCI_MAX_FRAME_SIZE, GFP_ATOMIC);
				if (!skb) {
					err = -ENOMEM;
					break;
				}
				hci_skb_pkt_type(skb) = bt_rx_hdr[0];
				raw_data = skb_put(skb, frame_len);
				PeekCQueueToBuf(&data->bt_rx_queue, (CQItemType *)raw_data, frame_len);
				// jump packet type
				skb_pull(skb, 1);
				DeCQueue(&data->bt_rx_queue, NULL, frame_len);
				bes_dbg(BES_DBG_BTUSB, "btrx flen:%d %02x %02x %02x %02x %02x %02x %02x %02x\n",
					frame_len, raw_data[0], raw_data[1], raw_data[2], raw_data[3], raw_data[4],
					raw_data[5], raw_data[6], raw_data[7]);
				/* Complete frame */
				hci_recv_frame(data->hdev, skb);
			} else {
				break;
			}
		}
	}

	return err;
}

static void bes_btusb_rx_complete(struct urb *urb)
{
	struct sk_buff *skb = (struct sk_buff *)urb->context;
	struct bes_btusb_data *data = (struct bes_btusb_data *)skb->dev;
	int count = urb->actual_length;
	int err;

	bes_dbg(BES_DBG_BTUSB, "btusb_rx_complete %p urb %p skb %p len %d %d\n",
		data, urb, skb, skb->len, urb->actual_length);

	read_lock(&data->lock);

	if (!test_bit(HCI_RUNNING, &data->hdev->flags)) {
		bes_dbg(BES_DBG_BTUSB, "rx_complete data->hdev->flags=%lx\n",
			data->hdev->flags);
		goto unlock;
	}

	if (urb->status || !count) {
		bes_err(BES_DBG_BTUSB, "rx_complete urb->status:%d count:%d\n",
			urb->status, count);
		goto resubmit;
	}

	data->hdev->stat.byte_rx += count;

	bes_btusb_recv_bulk(data, urb->transfer_buffer, urb->actual_length);

	skb_unlink(skb, &data->pending_q);
	kfree_skb(skb);

	bes_btusb_rx_submit(data, urb);

	read_unlock(&data->lock);

	return;

resubmit:
	urb->dev = data->udev;

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err) {
		bes_err(BES_DBG_BTUSB, "%s bulk resubmit failed urb %p err %d\n",
			data->hdev->name, urb, err);
	}

unlock:
	read_unlock(&data->lock);
}

static int bes_btusb_rx_submit(struct bes_btusb_data *data, struct urb *urb)
{
	struct bes_btusb_data_scb *scb;
	struct sk_buff *skb;
	struct bes_usb_pipe *recv_pipe = NULL;
	int err, size = HCI_MAX_FRAME_SIZE + 32;
	bes_dbg(BES_DBG_BTUSB, "btusb_rx_submit %p urb %p\n", data, urb);
	if (!urb) {
		urb = usb_alloc_urb(0, GFP_ATOMIC);
		if (!urb)
			return -ENOMEM;
	}

	skb = bt_skb_alloc(size, GFP_ATOMIC);
	if (!skb) {
		usb_free_urb(urb);
		return -ENOMEM;
	}

	skb->dev = (void *)data;

	scb = (struct bes_btusb_data_scb *)skb->cb;
	scb->urb = urb;

	recv_pipe = data->rx_pipes;
	usb_fill_bulk_urb(urb, data->udev, data->rx_pipes->usb_pipe_handle, skb->data, size,
			  bes_btusb_rx_complete, skb);
	bes_dbg(BES_DBG_BTUSB, "btusb_rx: bulk recv submit:%d, 0x%X (ep:0x%2.2X), %d bytes buf:0x%p\n",
		recv_pipe->logical_pipe_num, recv_pipe->usb_pipe_handle, recv_pipe->ep_address, size, skb);
	skb_queue_tail(&data->pending_q, skb);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err) {
		bes_err(BES_DBG_BTUSB, "%s bulk rx submit failed urb %p err %d\n",
			data->hdev->name, urb, err);
		skb_unlink(skb, &data->pending_q);
		kfree_skb(skb);
		usb_free_urb(urb);
	}

	return err;
}

static int bes_btusb_open(struct hci_dev *hdev)
{
	struct bes_btusb_data *data = hci_get_drvdata(hdev);
	unsigned long flags;
	int i, err;

	write_lock_irqsave(&data->lock, flags);

	err = bes_btusb_rx_submit(data, NULL);
	if (!err) {
		for (i = 1; i < BES_BTUSB_MAX_BULK_RX; i++)
			bes_btusb_rx_submit(data, NULL);
	}

	write_unlock_irqrestore(&data->lock, flags);
	return err;
}

static void bes_btusb_unlink_urbs(struct bes_btusb_data *data)
{
	struct sk_buff *skb;
	struct urb *urb;

	while ((skb = skb_dequeue(&data->pending_q))) {
		urb = ((struct bes_btusb_data_scb *)skb->cb)->urb;
		usb_kill_urb(urb);
		kfree_skb(skb);
	}
}

static int bes_btusb_flush(struct hci_dev *hdev)
{
	struct bes_btusb_data *data = hci_get_drvdata(hdev);
	bes_dbg(BES_DBG_BTUSB, "btusb_flush hdev %p btusb %p\n", hdev, data);
	read_lock(&data->lock);
	skb_queue_purge(&data->transmit_q);
	read_unlock(&data->lock);

	return 0;
}

static int bes_btusb_close(struct hci_dev *hdev)
{
	struct bes_btusb_data *data = hci_get_drvdata(hdev);
	unsigned long flags;
	bes_dbg(BES_DBG_BTUSB, "btusb_close hdev %p btusb %p\n", hdev, data);

	if (!bes_sysfs_is_signal_mode())
		clear_bit(HCI_RUNNING, &data->hdev->flags);

	write_lock_irqsave(&data->lock, flags);
	write_unlock_irqrestore(&data->lock, flags);

	bes_btusb_unlink_urbs(data);
	bes_btusb_flush(hdev);

	return 0;
}

static int bes_btusb_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
	char *pkt_type = NULL;
	struct bes_btusb_data *data = hci_get_drvdata(hdev);
	bes_dbg(BES_DBG_BTUSB, "btusb_send_frame hdev %p skb %p type %d len %d\n",
		hdev, skb, hci_skb_pkt_type(skb), skb->len);

	bes_dbg(BES_DBG_BTUSB, "%02x %02x %02x %02x %02x %02x %02x %02x %02x\n",
		skb->data[0], skb->data[1], skb->data[2], skb->data[3], skb->data[4],
		skb->data[5], skb->data[6], skb->data[7], skb->data[8]);
	switch (hci_skb_pkt_type(skb)) {
	case HCI_COMMAND_PKT:
		hdev->stat.cmd_tx++;
		break;
	case HCI_ACLDATA_PKT:
		hdev->stat.acl_tx++;
		break;
	case HCI_SCODATA_PKT:
		hdev->stat.sco_tx++;
		break;
	}
	pkt_type = skb_push(skb, 1);
	pkt_type[0] = hci_skb_pkt_type(skb);
	read_lock(&data->lock);
	skb_queue_tail(&data->transmit_q, skb);
	read_unlock(&data->lock);

	schedule_work(&data->tx_work);
	return 0;
}

static void bes_btusb_notify(struct hci_dev *hdev, unsigned int evt)
{
	// struct bes_btusb_data *data = hci_get_drvdata(hdev);
	bes_dbg(BES_DBG_BTUSB, "btusb_notify %s evt %d\n", hdev->name, evt);
	if (evt == HCI_NOTIFY_CONN_ADD) {
	} else if (evt == HCI_NOTIFY_CONN_DEL) {
	} else if (evt == HCI_NOTIFY_VOICE_SETTING) {
	}
}

static inline int bes_btusb_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *bdaddr)
{
	bes_dbg(BES_DBG_BTUSB, "bes_btusb_set_bdaddr\n");
	return -EOPNOTSUPP;
}

static void bes_btusb_tx_complete(struct urb *urb)
{
	struct sk_buff *skb = (struct sk_buff *)urb->context;
	struct bes_btusb_data *data = (struct bes_btusb_data *)skb->dev;
	bes_dbg(BES_DBG_BTUSB, "btusb_tx_complete %p urb %p skb %p len %d\n",
		data, urb, skb, skb->len);
	atomic_dec(&data->pending_tx);

	if (!test_bit(HCI_RUNNING, &data->hdev->flags))
		return;

	if (!urb->status)
		data->hdev->stat.byte_tx += skb->len;
	else
		data->hdev->stat.err_tx++;

	read_lock(&data->lock);
	skb_unlink(skb, &data->pending_q);
	read_unlock(&data->lock);
	kfree_skb(skb);

	if (skb_queue_len(&data->transmit_q))
		schedule_work(&data->tx_work);
}

static int bes_btusb_send_bulk(struct bes_btusb_data *data, struct sk_buff *skb)
{
	struct bes_btusb_data_scb *scb = (void *)skb->cb;
	struct urb *urb = NULL;
	int err;
	struct bes_usb_pipe *tx_pipe = data->tx_pipes;

	urb = usb_alloc_urb(0, GFP_ATOMIC);
	if (!urb)
		return -ENOMEM;

	skb->dev = (void *)data;
	usb_fill_bulk_urb(urb, data->udev, tx_pipe->usb_pipe_handle, skb->data,
			  skb->len, bes_btusb_tx_complete, skb);
	bes_dbg(BES_DBG_BTUSB, "btusb_tx: bulk tx submit:%d, 0x%X (ep:0x%2.2X), %d bytes buf:0x%p\n",
		tx_pipe->logical_pipe_num, tx_pipe->usb_pipe_handle, tx_pipe->ep_address, skb->len, skb);
	scb->urb = urb;

	skb_queue_tail(&data->pending_q, skb);

	err = usb_submit_urb(urb, GFP_ATOMIC);
	if (err) {
		bes_err(BES_DBG_BTUSB, "%s bulk tx submit failed urb %p err %d\n",
			data->hdev->name, urb, err);
		skb_unlink(skb, &data->pending_q);
	} else
		atomic_inc(&data->pending_tx);

	usb_free_urb(urb);
	return err;
}

static void bes_btusb_work(struct work_struct *work)
{
	struct bes_btusb_data *data = container_of(work, struct bes_btusb_data, tx_work);
	struct sk_buff *skb;
	read_lock(&data->lock);
	while ((atomic_read(&data->pending_tx) < BES_BTUSB_MAX_BULK_TX) &&
	       (skb = skb_dequeue(&data->transmit_q))) {
		if (bes_btusb_send_bulk(data, skb) < 0) {
			skb_queue_head(&data->transmit_q, skb);
			break;
		}
	}
	read_unlock(&data->lock);
}

static void bes_btusb_btrf_get_resp(CQueue *bt_rx_queue, int count)
{
	struct bes_sysfs *bes_sysfs = bes_get_sysfs();

	bes_sysfs->btrf_priv.len = (count <= BTRF_RX_MAX_SIZE) ? count : BTRF_RX_MAX_SIZE;
	PeekCQueueToBuf(bt_rx_queue, (CQItemType *)(bes_sysfs->btrf_priv.rx_buff),
			bes_sysfs->btrf_priv.len);

	wake_up(&bes_sysfs->btrf_priv.btrf_rx_wq);
}

int bes_btusb_testmode_cmd(struct hci_dev *hdev, const char *buff, unsigned int size)
{
	int ret;
	struct bes_sysfs *bes_sysfs = bes_get_sysfs();
	struct bes_btusb_data *data;
	struct sk_buff *skb;
	u8 *frame;

	if (!hdev || !buff || !bes_sysfs)
		return -EFAULT;

	if (bes_sysfs_is_signal_mode() && (buff[4] != BTRF_READ_REG &&
	    buff[4] != BTRF_WRITE_REG))
		return -EPERM;

	data = hci_get_drvdata(hdev);
	if (!test_bit(HCI_RUNNING, &data->hdev->flags))
		return -EPERM;

	skb = bt_skb_alloc(size, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	bes_sysfs->btrf_priv.testmode = 1;

	frame = skb_put(skb, size - 1);
	memcpy(frame, buff + 1, size - 1);

	hci_skb_pkt_type(skb) = buff[0];

	hdev->send(hdev, skb);

	ret = wait_event_timeout(bes_sysfs->btrf_priv.btrf_rx_wq,
				(bes_sysfs->btrf_priv.rx_buff[1] == HCI_VENDOR_PKT ||
				 bes_sysfs->btrf_priv.rx_buff[1] == BTRF_VENDOR_EVENT_PKT),
				 HZ * 2);

	bes_sysfs->btrf_priv.testmode = 0;

	if (ret <= 0)
		ret = -ENOMSG;

	return ret;
}

void bes_btusb_clear_hci_running(struct hci_dev *hdev)
{
	struct bes_btusb_data *data;

	if (!hdev)
		return;

	data = hci_get_drvdata(hdev);
	if (test_bit(HCI_RUNNING, &data->hdev->flags))
		clear_bit(HCI_RUNNING, &data->hdev->flags);
}

int bes_btusb_setup_pipes(struct sbus_priv *ar_usb)
{
	struct hci_dev *hdev;
	struct bes_btusb_data *data;

	if (bes_sysfs_get_fw_type() == BES_FW_TYPE_WIFI_NONSIGNAL)
		return 0;

	/* Initialize control structure */
	data = kzalloc(sizeof(struct bes_btusb_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->rx_buffer = kzalloc(8 * 1024, GFP_KERNEL);
	InitCQueue(&data->bt_rx_queue, 8 * 1024, data->rx_buffer);

	rwlock_init(&data->lock);

	skb_queue_head_init(&data->transmit_q);
	skb_queue_head_init(&data->pending_q);

	/* Initialize and register HCI device */
	hdev = hci_alloc_dev();
	if (!hdev) {
		bes_err(BES_DBG_BTUSB, "bes Can't allocate HCI device\n");
		goto done;
	}

	data->udev = ar_usb->udev;
	data->rx_pipes = &ar_usb->pipes[BES_USB_PIPE_BT_RX_DATA];
	data->tx_pipes = &ar_usb->pipes[BES_USB_PIPE_BT_TX_DATA];
	data->hdev = hdev;
	INIT_WORK(&data->tx_work, bes_btusb_work);

	hdev->bus = HCI_USB;
	hdev->dev_type = HCI_PRIMARY;
	hci_set_drvdata(hdev, data);
	SET_HCIDEV_DEV(hdev, &ar_usb->interface->dev);
	hdev->open = bes_btusb_open;
	hdev->close = bes_btusb_close;
	hdev->flush = bes_btusb_flush;
	hdev->send = bes_btusb_send_frame;
	hdev->notify = bes_btusb_notify;
	hdev->set_bdaddr = bes_btusb_set_bdaddr;

	// set_bit(HCI_QUIRK_BROKEN_LOCAL_COMMANDS, &hdev->quirks);
	set_bit(HCI_QUIRK_BROKEN_STORED_LINK_KEY, &hdev->quirks);
	set_bit(HCI_QUIRK_RESET_ON_CLOSE, &hdev->quirks);

	/* only register hci in signaling mode */
	if (bes_sysfs_is_signal_mode()) {
		if (hci_register_dev(hdev) < 0) {
			bes_err(BES_DBG_BTUSB, "Can't register HCI device\n");
			hci_free_dev(hdev);
			goto done;
		}
		bes_dbg(BES_DBG_BTUSB, "register HCI device ok:%p %p\n", hdev, data);
	} else if (bes_sysfs_get_fw_type() == BES_FW_TYPE_BT) {
		if (bes_btusb_open(hdev)) {
			hci_free_dev(hdev);
			goto done;
		}
		set_bit(HCI_RUNNING, &data->hdev->flags);
	}

	ar_usb->btdev = data;
	bes_sysfs_set_hdev(hdev);

	return 0;

done:
	return -EIO;
}

void bes_btusb_uninit(struct sbus_priv *ar_usb)
{
	struct bes_btusb_data *data = NULL;
	struct hci_dev *hdev = NULL;

	if (bes_sysfs_get_fw_type() == BES_FW_TYPE_WIFI_NONSIGNAL)
		return ;

	if (ar_usb == NULL)
		return;

	data = (struct bes_btusb_data *)ar_usb->btdev;
	if (!data)
		return;

	hdev = data->hdev;
	cancel_work_sync(&data->tx_work);
	if (!hdev)
		return;

	bes_btusb_close(hdev);
	kfree(data->rx_buffer);
	kfree(data);
	ar_usb->btdev = NULL;

	if (bes_sysfs_is_signal_mode())
		hci_unregister_dev(hdev);

	bes_sysfs_set_hdev(NULL);

	hci_free_dev(hdev);
}
