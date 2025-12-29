/*
 * @file bes_ibus_rpmsg.c
 *
 * Copyright (c) 2024, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/types.h>
#include <linux/delay.h>
#include <linux/rpmsg.h>
#include <linux/iopoll.h>

#include "rwnx_platform.h"
#include "bes_ibus.h"
#include "bes_log.h"

#define ACTION_POWERUP_WIFI    (1 << 0)
#define ACTION_POWERDOWN_WIFI  (1 << 1)
#define ACTION_PATCHED_WIFI    (1 << 2)
#define ACTION_RUN_WIFI        (1 << 3)

#ifdef CONFIG_BES_IBUS_RPMSG

#define IBUS_RPMSG_NEED_CFM (ACTION_POWERUP_WIFI | ACTION_PATCHED_WIFI | ACTION_RUN_WIFI)

#define IBUS_RPMSG_ACTION_ID (((TASK_MCU + 1) << 10) - 2)
#define IBUS_RPMSG_ACTION_CFM_ID (((TASK_MCU + 1) << 10) - 1)

/* rpmsg receive is only for cofirm now */
static int bes_ibus_rpmsg_receive(struct rpmsg_device *rpdev, void *data, int len,
			   void *priv, u32 src)
{
	int ret = 0;
	struct lmac_msg *msg;
	struct bes_ibus_power_ctrl *bes_ibus_power_ctrl = dev_get_drvdata(&rpdev->dev);
	BUG_ON(!bes_ibus_power_ctrl);

	msg = (struct lmac_msg *)data;
	if (msg->id == IBUS_RPMSG_ACTION_CFM_ID) {
		bes_ibus_power_ctrl->confirm = kmalloc(len, GFP_ATOMIC);
		if (bes_ibus_power_ctrl->confirm) {
			memcpy(bes_ibus_power_ctrl->confirm, data, len);
			complete(&bes_ibus_power_ctrl->complete);
		} else
			ret = -ENOMEM;
	} else {
		// rpmsg indication
	}
	return ret;
}

/* rpmsg format compatible with wifi cmd msg now */
static int bes_ibus_rpmsg_send(struct rpmsg_device *rpdev, struct lmac_msg *msg, bool need_cfm, u32 tmo_ms)
{
	int ret, already = 0;
	struct bes_ibus_power_ctrl *bes_ibus_power_ctrl = dev_get_drvdata(&rpdev->dev);
	struct lmac_msg *cfm;

	BUG_ON(!bes_ibus_power_ctrl);

	mutex_lock(&bes_ibus_power_ctrl->msg_lock);
	reinit_completion(&bes_ibus_power_ctrl->complete);

	do {
		ret = rpmsg_trysend(rpdev->ept, msg, sizeof(struct lmac_msg) + msg->param_len);
		if (!ret) {
			break;
		} else if ((ret == -ENOMEM) && (already < 10)) {
			already ++;
			msleep(1);
		} else {
			goto out;
		}
	} while(1);

	if (need_cfm) {
		ret = wait_for_completion_timeout(&bes_ibus_power_ctrl->complete, msecs_to_jiffies(tmo_ms));
		if (unlikely(ret <= 0)) {
			ret = ret ? ret : -ETIMEDOUT;
			goto out;
		}
		cfm = bes_ibus_power_ctrl->confirm;
		if ((cfm->id == msg->id + 1) && (0 == *(u8 *)cfm->param)) {
			ret = 0;
		} else {
			bes_dbg(BES_DBG_POWER_CTRL, "unexcepted rpmsg confirm 0x%x:%d for 0x%x\n",
					cfm->id, *(u8 *)cfm->param, msg->id);
			ret = -EIO;
		}
		kfree(bes_ibus_power_ctrl->confirm);
		bes_ibus_power_ctrl->confirm = NULL;
	}

out:
	mutex_unlock(&bes_ibus_power_ctrl->msg_lock);
	return ret;
}

int bes_ibus_rpmsg_wifi_on_off(bool on)
{
	int ret;
	struct lmac_msg *msg;
	struct rwnx_plat *rwnx_plat = (struct rwnx_plat *)rwnx_get_plat_data();
	struct bes_ibus_power_ctrl *bes_ibus_power_ctrl = (struct bes_ibus_power_ctrl *)rwnx_plat->power_priv;
	if (WARN_ON(!bes_ibus_power_ctrl->rpdev)) {
		bes_err(BES_DBG_IBUS, "mismatch scp and kernel ????\n");
		return -ENODEV;
	}

	msg = kmalloc(sizeof(struct lmac_msg) + sizeof(u16), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->id = IBUS_RPMSG_ACTION_ID;
	msg->src_id = DRV_TASK_ID;
	msg->dest_id = TASK_MCU;
	msg->param_len = sizeof(u16);
	if (on)
		*(u16 *)msg->param = ACTION_POWERUP_WIFI;
	else
		*(u16 *)msg->param = ACTION_POWERDOWN_WIFI;
	ret = bes_ibus_rpmsg_send(bes_ibus_power_ctrl->rpdev, msg, on, 1000);
	kfree(msg);
	return ret;
}

int bes_ibus_rpmsg_wifi_run(void)
{
	int ret;
	struct lmac_msg *msg;
	struct rwnx_plat *rwnx_plat = (struct rwnx_plat *)rwnx_get_plat_data();
	struct bes_ibus_power_ctrl *bes_ibus_power_ctrl = (struct bes_ibus_power_ctrl *)rwnx_plat->power_priv;
	BUG_ON(!bes_ibus_power_ctrl->rpdev);

	msg = kmalloc(sizeof(struct lmac_msg) + sizeof(u16), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->id = IBUS_RPMSG_ACTION_ID;
	msg->src_id = DRV_TASK_ID;
	msg->dest_id = TASK_MCU;
	msg->param_len = sizeof(u16);
	*(u16 *)msg->param = ACTION_RUN_WIFI;
	ret = bes_ibus_rpmsg_send(bes_ibus_power_ctrl->rpdev, msg, true, 1000);
	kfree(msg);
	return ret;
}

int bes_ibus_rpmsg_wifi_patched(void)
{
	int ret;
	struct lmac_msg *msg;
	struct rwnx_plat *rwnx_plat = (struct rwnx_plat *)rwnx_get_plat_data();
	struct bes_ibus_power_ctrl *bes_ibus_power_ctrl = (struct bes_ibus_power_ctrl *)rwnx_plat->power_priv;
	BUG_ON(!bes_ibus_power_ctrl->rpdev);

	msg = kmalloc(sizeof(struct lmac_msg) + sizeof(u16), GFP_KERNEL);
	if (!msg)
		return -ENOMEM;

	msg->id = IBUS_RPMSG_ACTION_ID;
	msg->src_id = DRV_TASK_ID;
	msg->dest_id = TASK_MCU;
	msg->param_len = sizeof(u16);
	*(u16 *)msg->param = ACTION_PATCHED_WIFI;
	ret = bes_ibus_rpmsg_send(bes_ibus_power_ctrl->rpdev, msg, true, 1000);
	kfree(msg);
	return ret;
}

static int bes_ibus_rpmsg_probe(struct rpmsg_device *rpdev)
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_ibus_power_ctrl *bes_ibus_power_ctrl = (struct bes_ibus_power_ctrl *)rwnx_plat->power_priv;

	mutex_init(&bes_ibus_power_ctrl->msg_lock);
	init_completion(&bes_ibus_power_ctrl->complete);
	bes_ibus_power_ctrl->confirm = NULL;
	bes_ibus_power_ctrl->rpdev = rpdev;
	dev_set_drvdata(&rpdev->dev, bes_ibus_power_ctrl);
	bes_dbg(BES_DBG_POWER_CTRL, "bes ibus rpmsg channel: 0x%x -> 0x%x\n", rpdev->src, rpdev->dst);
	return 0;
}

static void bes_ibus_rpmsg_remove(struct rpmsg_device *rpdev)
{
	dev_set_drvdata(&rpdev->dev, NULL);
}

static struct rpmsg_device_id bes_ibus_rpmsg_device_id_table[] = {
	{.name = "rpmsg_channel_ibus_ctrl" },
	{ },
};

static struct rpmsg_driver bes_ibus_rpmsg_driver = {
	.drv.name = "bes_ibus_rpmsg",
	.drv.owner = THIS_MODULE,
	.id_table = bes_ibus_rpmsg_device_id_table,
	.probe = bes_ibus_rpmsg_probe,
	.callback = bes_ibus_rpmsg_receive,
	.remove = bes_ibus_rpmsg_remove,
};

int bes_ibus_rpmsg_init(void)
{
	return register_rpmsg_driver(&bes_ibus_rpmsg_driver);
}

void bes_ibus_rpmsg_deinit(void)
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_ibus_power_ctrl *bes_ibus_power_ctrl = (struct bes_ibus_power_ctrl *)rwnx_plat->power_priv;
	bes_ibus_power_ctrl->rpdev = NULL;
	unregister_rpmsg_driver(&bes_ibus_rpmsg_driver);
}

#else // pseudo rpmsg

static int bes_ibus_rpmsg_send(struct bes_soc *bes_soc, u32 op, bool need_cfm, u32 tmo_ms)
{
	int ret = 0;
	u32 tmp;
	void __iomem *aon_cmu = ioremap(0x58080000, 0x1000);
	if (!aon_cmu) {
		bes_err(BES_DBG_DOWNLOAD, "%s aon cmu ioremap failed\n", __func__);
		return -ENOMEM;
	}

	if ((ret = hwspin_lock_timeout(bes_soc->lp_lock, tmo_ms))) {
		bes_err(BES_DBG_DOWNLOAD, "%s op(0x%x) memsc_lock failed\n", __func__, op);
		goto exit;
	}

	writel(op, aon_cmu + 0xF8);
	hwspin_unlock(bes_soc->lp_lock);

	if (!need_cfm)
		goto exit;

	if ((ret = readl_poll_timeout(aon_cmu + 0xF8, tmp, tmp & (op << 16), 100, tmo_ms * 1000))) {
		bes_err(BES_DBG_DOWNLOAD, "%s op(0x%x) cfm failed(0x%x)\n", __func__, op, tmp);
		ret = - EIO;
	}

exit:
	iounmap(aon_cmu);
	return ret;
}

int bes_ibus_rpmsg_wifi_on_off(bool on)
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_soc *bes_soc = (struct bes_soc *)rwnx_plat->priv;
	if (on) // pseudo rpmsg don't support wifi on
		return 0;
	else
		return bes_ibus_rpmsg_send(bes_soc, ACTION_POWERDOWN_WIFI, true, 1000);
}

int bes_ibus_rpmsg_wifi_patched(void) {return 0;}

int bes_ibus_rpmsg_wifi_run(void)
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_soc *bes_soc = (struct bes_soc *)rwnx_plat->priv;
	return bes_ibus_rpmsg_send(bes_soc, ACTION_RUN_WIFI, true, 1000);
}

int bes_ibus_rpmsg_init(void) {return 0;}
void bes_ibus_rpmsg_deinit(void) {}

#endif
