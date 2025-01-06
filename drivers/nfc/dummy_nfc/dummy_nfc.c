// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * dummy_nfc.c - Nfc Test Driver
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/jiffies.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/signal_types.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/workqueue.h>

#define MAX_NCI_BUFFER_SIZE (512)
#define MAX_DL_BUFFER_SIZE (512)

#define NO_RETRY           (0)

#define BLOCK_READ          (-1)

#define end_packed_struct __attribute__((packed))

extern void *ccpch_open (char *name);

extern int ccpch_close (void *pdev);

extern int ccpch_read (void *pdev , void *buf, const int count, bool nonblock);

extern int ccpch_write (void *pdev , const void *buf, const int count, bool nonblock);

struct dummy_nfc_dev {
	wait_queue_head_t read_wq;
	struct mutex read_mutex;
	struct mutex write_mutex;
	uint8_t *read_kbuf;
	uint8_t *write_kbuf;
	struct mutex dev_ref_mutex;
	unsigned int dev_ref_count;
	struct miscdevice   dummy_nfc_device;
	void *cdev;
	int (*nfc_read)(struct dummy_nfc_dev *dev, char *buf, size_t count,
			int timeout);
	int (*nfc_write)(struct dummy_nfc_dev *dev, const char *buf,
			 const size_t count, int max_retry_cnt);
};

struct crc_header {
	uint16_t crc_head;
	uint16_t crc_body_size;
	uint16_t crc_body;
}end_packed_struct;

struct simple_data_header {
	struct crc_header crc_impl;
	uint16_t cmd;
	uint16_t async;
	uint16_t seq;
	uint16_t len;
	uint16_t reserved[4];
	uint8_t buffer[0];
} end_packed_struct;

struct simple_data_response {
	struct crc_header crc_impl;
	uint16_t cmd;
	uint16_t async;
	uint16_t seq;
	uint16_t len;
	int16_t status;
	uint16_t reserved[3];
	uint8_t buffer[0];
} end_packed_struct;

struct simple_read {
	struct simple_data_header header;
	uint16_t read_size;
} end_packed_struct;

struct simple_ioctl {
	struct simple_data_header header;
	uint32_t cmd;
	uint64_t arg;
} end_packed_struct;

enum {
	DUMMY_NFC_OPEN,
	DUMMY_NFC_CLOSE,
	DUMMY_NFC_READ,
	DUMMY_NFC_WRITE,
	DUMMY_NFC_IOCTL,
};

static struct dummy_nfc_dev *g_dummy_nfc_dev = NULL;

uint16_t dummy_nfc_compute_crc16(const uint8_t *data, uint32_t length)
{
	uint32_t crc = 0xEF4A;
	uint32_t j;
	uint32_t i;
	uint32_t byte;
	uint32_t temp;
	const uint32_t and_value = 0x8000U;
	const uint32_t xor_value = 0x1021U;

	for (j = 0; j < length; ++j)
	{
		byte = data[j];
		crc ^= byte << 8;
		for (i = 0; i < 8U; ++i)
		{
			temp = crc << 1;
			if (0UL != (crc & and_value))
			{
				temp ^= xor_value;
			}
			crc = temp;
		}
	}

	return (uint16_t)crc;
}

static inline int get_next_seq(void)
{
	return 0;
}

static int nfc_dcc_read(struct dummy_nfc_dev *dev, char *buf, size_t count, int timeout)
{
	bool non_block = false;

	if (timeout >= 0) non_block = true;

	return ccpch_read(dev->cdev, buf, count, non_block);
}

static int nfc_dcc_write(struct dummy_nfc_dev *dev, const char *buf,
			 const size_t count, int max_retry_cnt)
{

	return ccpch_write(dev->cdev, buf, count, false);
}

static ssize_t dummy_nfc_dev_read(struct file *filp, char __user *buf,
		size_t count, loff_t *offset)
{
	int ret;
	struct dummy_nfc_dev *nfc_dev = (struct dummy_nfc_dev *)filp->private_data;
	struct simple_data_header *data = (struct simple_data_header *)nfc_dev->read_kbuf;
	struct simple_read msg = {
		.header.cmd = DUMMY_NFC_READ,
		.header.seq = get_next_seq(),
		.header.async = 1,
		.header.len = sizeof(msg.read_size),
		.read_size = count,
	};

	if (!nfc_dev) {
		pr_err("%s: device doesn't exist anymore\n", __func__);
		return -ENODEV;
	}

	if (count > MAX_NCI_BUFFER_SIZE)
		count = MAX_NCI_BUFFER_SIZE;

	mutex_lock(&nfc_dev->read_mutex);

	if (filp->f_flags & O_NONBLOCK) {
		ret = nfc_dev->nfc_read(nfc_dev, (char *)data, count + sizeof(struct simple_data_header), BLOCK_READ);
		pr_debug("%s: NONBLOCK read ret = %d\n", __func__, ret);
	} else {
		ret = nfc_dev->nfc_write(nfc_dev, (char *)&msg, sizeof(struct simple_read), NO_RETRY);
		if (ret < 0) {
			pr_warn("%s: perform read data error\n", __func__);
			ret = -EFAULT;
			mutex_unlock(&nfc_dev->read_mutex);
			return ret;
		}
		ret = nfc_dev->nfc_read(nfc_dev, (char *)data, count + sizeof(struct simple_data_header), BLOCK_READ);
	}

	if (ret > 0) {
		if (ret != (data->len + sizeof(struct simple_data_header))) {
			pr_warn("%s: recv data error %d\n", __func__, data->len);
			ret = -EFAULT;
			mutex_unlock(&nfc_dev->read_mutex);
			return ret;
		}
		if (copy_to_user(buf, data->buffer, data->len)) {
			pr_warn("%s: failed to copy to user space\n", __func__);
			ret = -EFAULT;
		}
		ret = data->len;
	}
	mutex_unlock(&nfc_dev->read_mutex);
	return ret;
}

static ssize_t dummy_nfc_dev_write(struct file *filp, const char __user *buf,
		size_t count, loff_t *offset)
{
	int ret;
	struct dummy_nfc_dev *nfc_dev = (struct dummy_nfc_dev *)filp->private_data;
	struct simple_data_header *data = (struct simple_data_header *)nfc_dev->write_kbuf;

	if (count > MAX_DL_BUFFER_SIZE)
		count = MAX_DL_BUFFER_SIZE;

	if (!nfc_dev) {
		pr_err("%s: device doesn't exist anymore\n", __func__);
		return -ENODEV;
	}

	mutex_lock(&nfc_dev->write_mutex);
	data->cmd = DUMMY_NFC_WRITE;
	data->seq = get_next_seq();
	data->async = 1;
	data->len = count;
	if (copy_from_user(data->buffer, buf, count)) {
		pr_err("%s: failed to copy from user space\n", __func__);
		mutex_unlock(&nfc_dev->write_mutex);
		return -EFAULT;
	}

	ret = nfc_dev->nfc_write(nfc_dev, (char *)data, count + sizeof(struct simple_data_header), NO_RETRY);
	if (ret != count + sizeof(struct simple_data_header)) {
		ret = -EFAULT;
	} else {
		ret = count;
	}
	mutex_unlock(&nfc_dev->write_mutex);
	return ret;
}

static int dummy_nfc_dev_open(struct inode *inode, struct file *filp)
{
	void *ch;
	int ret;
	struct dummy_nfc_dev *nfc_dev = container_of(filp->private_data,
			struct dummy_nfc_dev,
			dummy_nfc_device);
	struct simple_data_header data = {
		.cmd = DUMMY_NFC_OPEN,
		.seq = get_next_seq(),
		.async = 1,
		.len = 0,
	};
	filp->private_data = nfc_dev;

	ch = ccpch_open("mcu_nfc");
	if (ch == NULL) {
		return -1;
	}
	nfc_dev->cdev = ch;
	ret = nfc_dev->nfc_write(nfc_dev, (char *)&data, sizeof(struct simple_data_header), NO_RETRY);
	if (ret < 0) {
		return -2;
	}
	return 0;
}

static int dummy_nfc_dev_release(struct inode *inode, struct file *filp)
{
	int ret;
	struct dummy_nfc_dev *nfc_dev = (struct dummy_nfc_dev *)filp->private_data;
	struct simple_data_header data = {
		.cmd = DUMMY_NFC_CLOSE,
		.seq = get_next_seq(),
		.async = 1,
		.len = 0,
	};
	ret = nfc_dev->nfc_write(nfc_dev, (char *)&data, sizeof(struct simple_data_header), NO_RETRY);
	ccpch_close(nfc_dev->cdev);
	pr_info("%s:%d\n", __func__, ret);
	return 0;
}

static long  dummy_nfc_dev_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	int ret;
	struct dummy_nfc_dev *nfc_dev = (struct dummy_nfc_dev *)filp->private_data;
	struct simple_ioctl data;
	data.header.cmd = DUMMY_NFC_IOCTL;
	data.header.seq = get_next_seq();
	data.header.async = 1;
	data.header.len = sizeof(struct simple_ioctl) - sizeof(struct simple_data_header);
	data.cmd = cmd;
	data.arg = arg;
	ret = nfc_dev->nfc_write(nfc_dev, (char *)&data, sizeof(struct simple_data_header), NO_RETRY);
	pr_info("%s:%d\n", __func__, ret);
	return 0;
}

static const struct file_operations dummy_nfc_dev_fops = {
		.owner  = THIS_MODULE,
		.llseek = no_llseek,
		.read   = dummy_nfc_dev_read,
		.write  = dummy_nfc_dev_write,
		.open   = dummy_nfc_dev_open,
		.release = dummy_nfc_dev_release,
		.unlocked_ioctl  = dummy_nfc_dev_ioctl,
};

static int __init dummy_nfc_init(void)
{
	int ret = 0;
	struct dummy_nfc_dev *nfc_dev = kzalloc(sizeof(*nfc_dev), GFP_KERNEL);
	pr_info("%s:enter\n", __func__);
	if (nfc_dev == NULL) {
		pr_err("failed to allocate memory for module data\n");
		ret = -ENOMEM;
		return ret;
	}
	g_dummy_nfc_dev = nfc_dev;
	nfc_dev->read_kbuf = kzalloc(MAX_NCI_BUFFER_SIZE + sizeof(struct simple_data_header), GFP_KERNEL);
	if (!nfc_dev->read_kbuf) {
		ret = -ENOMEM;
		goto err_free_nfc_dev;
	}
	nfc_dev->write_kbuf = kzalloc(MAX_DL_BUFFER_SIZE + sizeof(struct simple_data_header), GFP_KERNEL);
	if (!nfc_dev->write_kbuf) {
		ret = -ENOMEM;
		goto err_free_read_kbuf;
	}

	nfc_dev->nfc_read = nfc_dcc_read;
	nfc_dev->nfc_write = nfc_dcc_write;

	mutex_init(&nfc_dev->read_mutex);
	mutex_init(&nfc_dev->write_mutex);
	mutex_init(&nfc_dev->dev_ref_mutex);
	nfc_dev->dummy_nfc_device.minor = MISC_DYNAMIC_MINOR;
	nfc_dev->dummy_nfc_device.name = "pn553";
	nfc_dev->dummy_nfc_device.fops = &dummy_nfc_dev_fops;

	ret = misc_register(&nfc_dev->dummy_nfc_device);
	if (ret) {
		pr_err("%s: nfc_misc_register failed\n", __func__);
		goto err_mutex_destroy;
	}
	return 0;
err_mutex_destroy:
	mutex_destroy(&nfc_dev->dev_ref_mutex);
	mutex_destroy(&nfc_dev->read_mutex);
	mutex_destroy(&nfc_dev->write_mutex);
//err_free_write_kbuf:
	kfree(nfc_dev->write_kbuf);
err_free_read_kbuf:
	kfree(nfc_dev->read_kbuf);
err_free_nfc_dev:
	kfree(nfc_dev);
	return ret;
}
module_init(dummy_nfc_init);

static void __exit dummy_nfc_exit(void)
{
	struct dummy_nfc_dev *nfc_dev = g_dummy_nfc_dev;
	pr_info("%s:enter\n", __func__);
	misc_deregister(&nfc_dev->dummy_nfc_device);
	mutex_destroy(&nfc_dev->dev_ref_mutex);
	mutex_destroy(&nfc_dev->read_mutex);
	mutex_destroy(&nfc_dev->write_mutex);
	kfree(nfc_dev->read_kbuf);
	kfree(nfc_dev->write_kbuf);
	kfree(nfc_dev);
}
module_exit(dummy_nfc_exit);

MODULE_AUTHOR("Oplus");
MODULE_DESCRIPTION("Oplus NFC Test Driver");
MODULE_LICENSE("GPL");
