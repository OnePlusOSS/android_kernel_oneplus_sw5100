/*
 * @file bes_power_ctrl_msg.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "bes_sysfs.h"
#include "bes_power_ctrl.h"
#include "bes_log.h"
#include "bes_hwio.h"

static struct bes_power_ctrl_ops g_msg_ops;

static int bes_power_ctrl_iowrite(struct bes_power_ctrl_msg *bes_power_ctrl_msg, u32 val)
{
	int ret = 0;

	if (bes_power_ctrl_msg->ops->iowrite)
		ret = bes_power_ctrl_msg->ops->iowrite(bes_power_ctrl_msg->priv,
						       &bes_power_ctrl_msg->io_dmabuf, val);

	return ret;
}

static int bes_wakeup_wifi_msg(struct bes_power_ctrl *bes_power_ctrl, bool wakeup)
{
	int ret = 0;
	u32 val = wakeup ? HOST_INT_TYPE_WIFI_WAKEUP : HOST_INT_TYPE_WIFI_SLEEP;

	ret = bes_power_ctrl_iowrite(power_ctrl_priv(bes_power_ctrl), val);

	return ret;
}

static int bes_bt_power_msg(struct bes_power_ctrl *bes_power_ctrl, bool on)
{
	int ret = 0;
	u32 val = on ? HOST_INT_TYPE_BT_ON : HOST_INT_TYPE_BT_OFF;

	ret = bes_power_ctrl_iowrite(power_ctrl_priv(bes_power_ctrl), val);

	return ret;
}

static int bes_power_ctrl_msg_wakeup_bt(struct bes_power_ctrl *bes_power_ctrl, bool wakeup)
{
	int ret = 0;
	u32 val = wakeup ? HOST_INT_TYPE_BT_WAKEUP : HOST_INT_TYPE_BT_SLEEP;

	ret = bes_power_ctrl_iowrite(power_ctrl_priv(bes_power_ctrl), val);

	return ret;
}

static int bes_power_ctrl_msg_change_fw_type(struct bes_power_ctrl *bes_power_ctrl, int type)
{
	int ret = 0;
	u32 val = HOST_INT_TYPE_CHG_FW_TYPE;
	struct bes_sysfs *bes_sysfs = bes_get_sysfs();
	struct bes_power_ctrl_msg *bes_power_ctrl_msg = power_ctrl_priv(bes_power_ctrl);

	if (type < 0 || type > BES_FW_TYPE_MAX_NUM)
		return -EPERM;

	bes_power_ctrl_set_chg_fw_type(bes_power_ctrl);

	/* unregister net dev */
	if (bes_power_ctrl_msg->ops->unregister_netdev)
		bes_power_ctrl_msg->ops->unregister_netdev(bes_power_ctrl_msg->priv);

	/* update power status */
	bes_sysfs->fw_type = type;
	bes_power_ctrl_set_system_closed(bes_power_ctrl);
	bes_power_ctrl_start_bus_probe();

	ret = bes_power_ctrl_iowrite(bes_power_ctrl_msg, val);
	if (ret)
		bes_err(BES_DBG_POWER_CTRL, "%s failed, ret: %d\n", __func__, ret);

	/* wait probe done event */
	ret = bes_power_ctrl_wait_probe_done(bes_power_ctrl);

	/* open bt */
	if (!ret && bes_sysfs->fw_type == BES_FW_TYPE_BT)
		ret = bes_bt_power(bes_power_ctrl, true);

	return ret;
}

static void bes_power_ctrl_msg_get_attr(struct bes_power_ctrl *bes_power_ctrl,
					enum bes_power_ctrl_attr attr, void *val)
{
	bool ret = false;

	switch (attr) {
	case BES_POWER_CTRL_ATTR_DEV_SHUTDOWN:
		if (g_msg_ops.plat_power)
			ret = !!CONFIG_BES_POWER_CTRL_ALLOW_SHUTDOWN;
		else
			ret = false;
		break;
	case BES_POWER_CTRL_ATTR_BT_SELF_MANAGED:
		ret = !!CONFIG_BES_POWER_CTRL_BT_SELF_MANAGED;
		break;

	default:
		break;
	}

	*((u8 *)val) = ret;
}

static struct bes_power_ctrl_ops g_msg_ops = {
	.wakeup_wifi = bes_wakeup_wifi_msg,
	.bt_power    = bes_bt_power_msg,
	.wakeup_bt   = bes_power_ctrl_msg_wakeup_bt,
	.chg_fw_type = bes_power_ctrl_msg_change_fw_type,
	.get_attr    = bes_power_ctrl_msg_get_attr,
};

void bes_power_ctrl_set_chip_power(int (*f)(struct bes_power_ctrl *, bool))
{
	g_msg_ops.chip_power = f;
}

void *bes_power_ctrl_msg_register(const struct bes_power_ctrl_msg_ops *ops,
				  struct rwnx_plat *rwnx_plat, size_t priv_data_len, plat_power_cb_t cb)
{
	struct bes_power_ctrl_msg *bes_power_ctrl_msg;
	int priv_size;

	priv_data_len = ALIGN(priv_data_len, sizeof(void *));
	priv_size = ALIGN(sizeof(struct bes_power_ctrl_msg), sizeof(void *)) + priv_data_len;

	if (cb)
		g_msg_ops.plat_power = cb;

	bes_power_ctrl_msg = bes_power_ctrl_register(&g_msg_ops, rwnx_plat, priv_size);
	if (!bes_power_ctrl_msg)
		return NULL;

	bes_power_ctrl_msg->ops = ops;
	bes_power_ctrl_msg->priv = (char *)bes_power_ctrl_msg +
				   ALIGN(sizeof(*bes_power_ctrl_msg), sizeof(void *));

	((struct bes_bus_power_ctrl *)(bes_power_ctrl_msg->priv))->power_ctrl = container_of(
		(void *)bes_power_ctrl_msg, struct bes_power_ctrl, priv);

	return bes_power_ctrl_msg->priv;
}

void bes_power_ctrl_msg_unregister(void *priv)
{
	struct bes_power_ctrl_msg *bes_power_ctrl_msg;
	struct bes_power_ctrl *bes_power_ctrl;

	if (!priv)
		return;

	bes_power_ctrl_msg = (struct bes_power_ctrl_msg *)((char *)priv -
					ALIGN(sizeof(*bes_power_ctrl_msg), sizeof(void *)));
	bes_power_ctrl = container_of((void *)bes_power_ctrl_msg, struct bes_power_ctrl, priv);

	if (bes_power_ctrl_msg->ops->exit)
		bes_power_ctrl_msg->ops->exit(bes_power_ctrl_msg->priv);

	bes_power_ctrl_unregister(bes_power_ctrl);
}
