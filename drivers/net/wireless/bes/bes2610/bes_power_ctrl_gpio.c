/*
 * @file bes_power_ctrl_gpio.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/of_gpio.h>
#include "rwnx_defs.h"
#include "rwnx_platform.h"
#include "bes_log.h"
#include "bes_sysfs.h"

static void bes_power_ctrl_gpio_data_deinit(struct bes_power_ctrl_gpio *bes_power_ctrl_gpio);
static struct bes_power_ctrl_ops bes_power_ctrl_gpio_ops;

enum bes_power_ctrl_type {
	BES_POWER_CTRL_CHIP_POWER = 0,
	BES_POWER_CTRL_BT_POWER,
	BES_POWER_CTRL_WAKEUP_WIFI,
	BES_POWER_CTRL_WAKEUP_BT,
};

static struct bes_gpio_data g_bes_pdata = {
	.powerup = -1,
	.wakeup_wifi = -1,
	.bt_powerup = -1,
	.wakeup_bt = -1,
};

static int bes_set_gpio(const struct bes_gpio_data *pdata,
			enum bes_power_ctrl_type type, bool gpio_op)
{
	int gpio_num;
	bes_dbg(BES_DBG_POWER_CTRL, RWNX_FN_ENTRY_STR);

	switch (type) {
	case BES_POWER_CTRL_CHIP_POWER:
		gpio_num = pdata->powerup;
		break;
	case BES_POWER_CTRL_WAKEUP_WIFI:
		gpio_num = pdata->wakeup_wifi;
		break;
	case BES_POWER_CTRL_BT_POWER:
		gpio_num = pdata->bt_powerup;
		break;
	case BES_POWER_CTRL_WAKEUP_BT:
		gpio_num = pdata->wakeup_bt;
		break;
	default:
		return -EINVAL;
		break;
	}

	if (!gpio_is_valid(gpio_num)) {
		bes_err(BES_DBG_POWER_CTRL, "%s, gpio is invalid\n", __func__);
		return -EINVAL;
	}

	gpio_set_value(gpio_num, gpio_op ? 1 : 0);

	return 0;
}

static int bes_wakeup_wifi_gpio(struct bes_power_ctrl *bes_power_ctrl, bool wakeup)
{
	int ret;
	struct bes_power_ctrl_gpio *bes_power_ctrl_gpio = power_ctrl_priv(bes_power_ctrl);

	ret = bes_set_gpio(bes_power_ctrl_gpio->pdata, BES_POWER_CTRL_WAKEUP_WIFI, wakeup);
	if (ret)
		bes_err(BES_DBG_POWER_CTRL, "%s: %d fail\n", __func__, wakeup);
	else
		bes_info(BES_DBG_POWER_CTRL, "%s: %d\n", __func__, wakeup);

	return ret;
}

static int bes_chip_power_gpio(struct bes_power_ctrl *bes_power_ctrl, bool on)
{
	int ret;
	struct bes_power_ctrl_gpio *bes_power_ctrl_gpio = power_ctrl_priv(bes_power_ctrl);

	ret = bes_set_gpio(bes_power_ctrl_gpio->pdata, BES_POWER_CTRL_CHIP_POWER, on);
	if (ret)
		bes_err(BES_DBG_POWER_CTRL, "%s set value: %d fail\n", __func__, on);
	else
		bes_info(BES_DBG_POWER_CTRL, "%s set value: %d\n", __func__, on);

	return ret;
}

static int bes_bt_power_gpio(struct bes_power_ctrl *bes_power_ctrl, bool on)
{
	int ret;
	struct bes_power_ctrl_gpio *bes_power_ctrl_gpio = power_ctrl_priv(bes_power_ctrl);

	ret = bes_set_gpio(bes_power_ctrl_gpio->pdata, BES_POWER_CTRL_BT_POWER, on);
	if (ret)
		bes_err(BES_DBG_POWER_CTRL, "%s set value: %d fail\n", __func__, on);
	else
		bes_info(BES_DBG_POWER_CTRL, "%s set value: %d\n", __func__, on);

	return ret;
}

static int bes_get_gpio_from_dts(int *gpio_num, const char *gpio_name)
{
	int gpio_data, ret = 0;
#if (LINUX_VERSION_CODE < KERNEL_VERSION(6, 3, 0))
	enum of_gpio_flags flags;
#endif
	struct device_node *gpio_node;

	gpio_node = of_find_node_with_property(NULL, gpio_name);
	if (gpio_node != NULL) {
		gpio_data = of_get_named_gpio_flags(gpio_node, gpio_name, 0, &flags);
		if (gpio_is_valid(gpio_data))
			*gpio_num = gpio_data;
	} else {
		bes_err(BES_DBG_POWER_CTRL, "find node for %s failed\n", gpio_name);
		ret = -EIO;
	}

	return ret;
}

/**
 * bes_power_ctrl_gpio_free - free gpio when host has used
 */
static void bes_power_ctrl_gpio_free(int gpio)
{
#ifdef GPIO_HOST_USED
	gpio_free(gpio);
#endif
}

static int bes_power_ctrl_gpio_data_init(struct bes_power_ctrl_gpio *bes_power_ctrl_gpio)
{
	int ret = 0;
	struct bes_gpio_data *pdata = &g_bes_pdata;

	if (!bes_power_ctrl_gpio)
		return -EFAULT;

	bes_power_ctrl_gpio->pdata = pdata;

#ifdef GPIO_CHIP_POWER_DTS_NAME
	if ((ret = bes_get_gpio_from_dts(&pdata->powerup, GPIO_CHIP_POWER_DTS_NAME)) < 0)
		goto exit;
	if (gpio_is_valid(pdata->powerup)) {
		bes_power_ctrl_gpio_free(pdata->powerup);
		ret = gpio_request(pdata->powerup, "bes2610_powerup");
		if (ret) {
			bes_err(BES_DBG_POWER_CTRL, "can't request bes2610_powerup:%d\n", ret);
			goto exit;
		} else {
			gpio_direction_output(pdata->powerup, 0);
		}
	} else {
		bes_err(BES_DBG_POWER_CTRL, "bes2610_powerup is invalid\n");
		ret = -EIO;
		goto exit;
	}
#endif

#ifdef GPIO_WAKEUP_WIFI_DTS_NAME
	/* wifi switch and wakeup */
	if ((ret = bes_get_gpio_from_dts(&pdata->wakeup_wifi, GPIO_WAKEUP_WIFI_DTS_NAME)) < 0)
		goto exit;
	if (gpio_is_valid(pdata->wakeup_wifi)) {
		bes_power_ctrl_gpio_free(pdata->wakeup_wifi);
		ret = gpio_request(pdata->wakeup_wifi, "bes2610_host_wakeup_wifi");
		if (ret) {
			bes_err(BES_DBG_POWER_CTRL, "can't reqest host_wakeup_wifi gpio:%d\n", ret);
			goto exit;
		} else {
			gpio_direction_output(pdata->wakeup_wifi, 0);
		}
	} else {
		bes_err(BES_DBG_POWER_CTRL, "host wakeup wifi is invalid\n");
		ret = -EIO;
		goto exit;
	}
#endif

#ifdef GPIO_BT_POWER_DTS_NAME
	if ((ret = bes_get_gpio_from_dts(&pdata->bt_powerup, GPIO_BT_POWER_DTS_NAME)) < 0)
		goto exit;
	if (gpio_is_valid(pdata->bt_powerup)) {
		bes_power_ctrl_gpio_free(pdata->bt_powerup);
		ret = gpio_request(pdata->bt_powerup, "bes2610_bt_powerup");
		if (ret) {
			bes_err(BES_DBG_POWER_CTRL, "can't reqest bes2610_bt_powerup gpio:%d\n", ret);
			goto exit;
		} else {
			gpio_direction_output(pdata->bt_powerup, 0);
		}
	} else {
		bes_err(BES_DBG_POWER_CTRL, "bt power_up is invalid\n");
		ret = -EIO;
		goto exit;
	}
#endif

#ifdef GPIO_WAKEUP_BT_DTS_NAME
	if ((ret = bes_get_gpio_from_dts(&pdata->wakeup_bt, GPIO_WAKEUP_BT_DTS_NAME)) < 0)
		goto exit;
	if (gpio_is_valid(pdata->wakeup_bt)) {
		bes_power_ctrl_gpio_free(pdata->wakeup_bt);
		ret = gpio_request(pdata->wakeup_bt, "bes2610_host_wakeup_bt");
		if (ret) {
			bes_err(BES_DBG_POWER_CTRL, "can't reqest host_wakeup_bt gpio:%d\n", ret);
			goto exit;
		} else {
			gpio_direction_output(pdata->wakeup_bt, 0);
		}
	} else {
		bes_err(BES_DBG_POWER_CTRL, "host wakeup bt is invalid\n");
		ret = -EIO;
		goto exit;
	}
#endif

#ifdef GPIO_WAKEUP_HOST_DTS_NAME
	if ((ret = bes_get_gpio_from_dts(&pdata->wakeup_host, GPIO_WAKEUP_HOST_DTS_NAME)) < 0)
		goto exit;
	if (gpio_is_valid(pdata->wakeup_host)) {
		bes_power_ctrl_gpio_free(pdata->wakeup_host);
		ret = gpio_request(pdata->wakeup_host, "bes2600iwb_wakeup_host");
		if (ret) {
			bes_err(BES_DBG_POWER_CTRL, "can't reqest wakeup_host gpio:%d\n", ret);
			goto exit;
		} else {
			gpio_direction_input(pdata->wakeup_host);
		}
	} else {
		bes_err(BES_DBG_POWER_CTRL, "wakeup host is invalid\n");
		ret = -EIO;
		goto exit;
	}
#endif

	return 0;

exit:
	bes_power_ctrl_gpio_data_deinit(bes_power_ctrl_gpio);
	return ret;
}

static void bes_power_ctrl_gpio_data_deinit(struct bes_power_ctrl_gpio *bes_power_ctrl_gpio)
{
	struct bes_gpio_data *pdata;

	if (!bes_power_ctrl_gpio)
		return ;

	pdata = bes_power_ctrl_gpio->pdata;

	if (pdata == NULL)
		return;

	if (gpio_is_valid(pdata->powerup))
		gpio_free(pdata->powerup);

	if (gpio_is_valid(pdata->wakeup_wifi))
		gpio_free(pdata->wakeup_wifi);

	if (gpio_is_valid(pdata->bt_powerup))
		gpio_free(pdata->bt_powerup);

	if (gpio_is_valid(pdata->wakeup_bt))
		gpio_free(pdata->wakeup_bt);

	if (gpio_is_valid(pdata->wakeup_host))
		gpio_free(pdata->wakeup_host);
}

static int bes_power_ctrl_gpio_wakeup_bt(struct bes_power_ctrl *bes_power_ctrl, bool wakeup)
{
	int ret = 0;
	struct bes_power_ctrl_gpio *bes_power_ctrl_gpio = power_ctrl_priv(bes_power_ctrl);

	ret = bes_set_gpio(bes_power_ctrl_gpio->pdata, BES_POWER_CTRL_WAKEUP_BT, wakeup);
	if (ret)
		bes_err(BES_DBG_POWER_CTRL, "%s set value: %d fail\n", __func__, wakeup);
	else
		bes_info(BES_DBG_POWER_CTRL, "%s set value: %d\n", __func__, wakeup);

	return ret;
}

static void bes_power_ctrl_gpio_pull_down(struct bes_power_ctrl *bes_power_ctrl)
{
	bes_chip_power_gpio(bes_power_ctrl, false);
	bes_wakeup_wifi_gpio(bes_power_ctrl, false);
	bes_bt_power_gpio(bes_power_ctrl, false);
	bes_power_ctrl_gpio_wakeup_bt(bes_power_ctrl, false);
}

static int bes_power_ctrl_gpio_change_fw_type(struct bes_power_ctrl *bes_power_ctrl, int type)
{
	int ret = 0;
	bool sys_closed;
	struct bes_sysfs *bes_sysfs = bes_get_sysfs();
	struct bes_power_ctrl_gpio *bes_power_ctrl_gpio = power_ctrl_priv(bes_power_ctrl);
	const struct bes_power_ctrl_ops *ops = &bes_power_ctrl_gpio_ops;

	if (!ops->plat_power)
		return -EOPNOTSUPP;

	BUG_ON(!bes_power_ctrl_gpio->plat);

	bes_dbg(BES_DBG_POWER_CTRL, "%s is called, change fw_type: %d\n", __func__, type);

	/* close wifi net device */
	if (bes_power_ctrl_gpio->plat && rwnx_net_dev_created(bes_power_ctrl_gpio->plat)) {
		rwnx_unregister_net_dev(bes_power_ctrl_gpio->plat);
	}

	sys_closed = bes_power_ctrl_check_system_close(bes_power_ctrl);

	/* update firmware type */
	bes_sysfs->fw_type = type;

	if (!sys_closed) {
		/* close device to call disconnect function */
		ops->plat_power(bes_power_ctrl_gpio->plat, false);
	}

	/* wait disconnect event */
	bes_power_ctrl_wait_disconnect_done(bes_power_ctrl);

	bes_sysfs_status_update(true);

	/* pull down gpio */
	bes_power_ctrl_gpio_pull_down(bes_power_ctrl);

	/* power on device to call probe function */
	bes_info(BES_DBG_POWER_CTRL, "reload firmware...\n");
	ops->plat_power(bes_power_ctrl->plat, true);

	/* wait probe done event */
	ret = bes_power_ctrl_wait_probe_done(bes_power_ctrl);

	return ret;
}

static void bes_power_ctrl_gpio_get_attr(struct bes_power_ctrl *bes_power_ctrl,
					 enum bes_power_ctrl_attr attr, void *val)
{
	bool ret = false;

	switch (attr) {
	case BES_POWER_CTRL_ATTR_DEV_SHUTDOWN:
		ret = !!CONFIG_BES_POWER_CTRL_ALLOW_SHUTDOWN;
		break;
	case BES_POWER_CTRL_ATTR_BT_SELF_MANAGED:
		ret = !!CONFIG_BES_POWER_CTRL_BT_SELF_MANAGED;
		break;

	default:
		break;
	}

	*((u8 *)val) = ret;
}

static struct bes_power_ctrl_ops bes_power_ctrl_gpio_ops = {
	.chip_power  = bes_chip_power_gpio,
#ifdef GPIO_WAKEUP_WIFI_DTS_NAME
	.wakeup_wifi = bes_wakeup_wifi_gpio,
#endif
#ifdef GPIO_BT_POWER_DTS_NAME
	.bt_power    = bes_bt_power_gpio,
#endif
#ifdef GPIO_WAKEUP_BT_DTS_NAME
	.wakeup_bt   = bes_power_ctrl_gpio_wakeup_bt,
#endif
	.chg_fw_type = bes_power_ctrl_gpio_change_fw_type,
	.get_attr    = bes_power_ctrl_gpio_get_attr,
};

void *bes_power_ctrl_gpio_register(struct rwnx_plat *rwnx_plat, size_t priv_data_len, plat_power_cb_t cb)
{
	struct bes_power_ctrl_gpio *bes_power_ctrl_gpio;
	int priv_size;

	priv_data_len = ALIGN(priv_data_len, sizeof(void *));
	priv_size = ALIGN(sizeof(struct bes_power_ctrl_gpio), sizeof(void *)) + priv_data_len;

	bes_power_ctrl_gpio_ops.plat_power = cb;

	bes_power_ctrl_gpio = bes_power_ctrl_register(&bes_power_ctrl_gpio_ops, rwnx_plat, priv_size);
	if (!bes_power_ctrl_gpio)
		return NULL;

	WARN_ON(bes_power_ctrl_gpio_data_init(bes_power_ctrl_gpio));

	bes_power_ctrl_gpio->plat = rwnx_plat;
	bes_power_ctrl_gpio->priv = (char *)bes_power_ctrl_gpio +
				    ALIGN(sizeof(*bes_power_ctrl_gpio), sizeof(void *));

	((struct bes_bus_power_ctrl *)(bes_power_ctrl_gpio->priv))->power_ctrl = container_of(
		(void *)bes_power_ctrl_gpio, struct bes_power_ctrl, priv);

	return bes_power_ctrl_gpio->priv;
}

void bes_power_ctrl_gpio_unregister(void *priv)
{
	struct bes_power_ctrl_gpio *bes_power_ctrl_gpio;
	struct bes_power_ctrl *bes_power_ctrl;

	if (!priv)
		return;

	bes_power_ctrl_gpio = (struct bes_power_ctrl_gpio *)((char *)priv -
					ALIGN(sizeof(*bes_power_ctrl_gpio), sizeof(void *)));
	bes_power_ctrl = container_of((void *)bes_power_ctrl_gpio, struct bes_power_ctrl, priv);

	bes_power_ctrl_gpio_data_deinit(bes_power_ctrl_gpio);

	bes_power_ctrl_unregister(bes_power_ctrl);
}
