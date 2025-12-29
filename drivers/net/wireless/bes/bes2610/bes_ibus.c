/*
 * @file bes_ibus.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/iopoll.h>

#include "rwnx_defs.h"
#include "bes_soc.h"
#include "bes_sysfs.h"
#include "bes_log.h"
#include "bes_ibus.h"
#include "bes_hwio.h"
#ifdef CONFIG_PLATFORM_DEVICE_BUILTIN
#include <linux/platform_device.h>
#include <linux/ioport.h>
#endif
#ifdef CONFIG_BES_POWER_CTRL
#include "bes_ibus_rpmsg.h"
#endif

void bes_io_bus_status_dump(void)
{
}

#if defined(CONFIG_BES_POWER_CTRL_MSG)

int bes_ibus_wifi_on(void)
{
	return bes_ibus_rpmsg_wifi_on_off(true);
}

static int bes_ibus_wifi_off(void)
{
	return bes_ibus_rpmsg_wifi_on_off(false);
}

static int bes_ibus_plat_power(struct rwnx_plat *rwnx_plat, bool on)
{
	int ret;
	bes_info(BES_DBG_IBUS, "%s on=%d\n", __func__, on);
	if (on) {
		ret = bes_ibus_wifi_on();
		if (!ret)
			ret = bes_ibus_platform_init(rwnx_plat, NULL);
	} else {
		ret = bes_ibus_wifi_off();
	}
	bes_power_ctrl_system_enabled(on, false);
	return ret;
}

static int bes_ibus_iowrite(void *priv, u32 *io_dmabuf, u32 val)
{
	int ret = 0;
	struct bes_ibus_power_ctrl *bes_ibus_power_ctrl = priv;
	struct bes_soc *bes_soc;
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	u32 tmp;

	BUG_ON(!priv);

	bes_soc = (struct bes_soc *)bes_ibus_power_ctrl->priv;

	bes_info(BES_DBG_IBUS, "%s val=0x%x\n", __func__, val);

	if (val == HOST_INT_TYPE_WIFI_SLEEP || val == HOST_INT_TYPE_WIFI_WAKEUP) {
		if (hwspin_lock_timeout(bes_soc->lp_lock, 1000)) {
			ret = -ETIMEDOUT;
			goto err_lp_lock;
		}
		tmp = readl(bes_soc->lp_status);
		if (val == HOST_INT_TYPE_WIFI_SLEEP)
			tmp &= ~WIFI_WAKE_LOCK;
		else
			tmp |= WIFI_WAKE_LOCK;
		writel(tmp, bes_soc->lp_status);
		writel(SYS2WF_DATA_IND_SET, bes_soc->sys2wf_reg_base + SYS2WF_IRQ_SET_OFFSET);
		hwspin_unlock(bes_soc->lp_lock);
		if (readl_poll_timeout(bes_soc->sys2wf_reg_base + SYS2WF_IRQ_SET_OFFSET, tmp, tmp & SYS2WF_DATA_DONE_SET, 1000, 100000)) {
			ret = -EIO;
			bes_err(BES_DBG_IBUS, "%s hwspinlock timeout val=0x%x\n", __func__, val);
		} else {
			writel(SYS2WF_DATA_DONE_CLR, bes_soc->sys2wf_reg_base + SYS2WF_IRQ_CLR_OFFSET);
			tmp = readl(bes_soc->lp_status);
			if ((val == HOST_INT_TYPE_WIFI_WAKEUP) && (tmp & WIFI_WAKE_STATUS)) {
				rwnx_plat->dev_status |= BES_SLAVE_STATUS_MCU_WAKEUP_READY;
			}  else if (val == HOST_INT_TYPE_WIFI_SLEEP) {
				rwnx_plat->dev_status &= ~BES_SLAVE_STATUS_MCU_WAKEUP_READY;
			}
			wake_up(&rwnx_plat->dev_stat_wait_q);
		}
	} else {
		WARN_ON(1);
	}

	return ret;

err_lp_lock:
	return ret;
}

static void bes_ibus_unregister_netdev(void *priv)
{
	WARN_ON(1);
}

#define bes_ibus_power_ctrl_exit_handler NULL

static const struct bes_power_ctrl_msg_ops ibus_power_ctrl_msg_ops = {
	.iowrite = bes_ibus_iowrite,
	.unregister_netdev = bes_ibus_unregister_netdev,
	.exit = bes_ibus_power_ctrl_exit_handler,
};

int bes_power_ctrl_ibus_init(struct rwnx_plat *rwnx_plat)
{
	int ret = 0;
	rwnx_plat->power_priv = bes_power_ctrl_msg_register(&ibus_power_ctrl_msg_ops, rwnx_plat,
										sizeof(struct bes_ibus_power_ctrl), bes_ibus_plat_power);
	if (!rwnx_plat->power_priv)
		return -ENOMEM;

	if ((ret = bes_ibus_rpmsg_init()))
		bes_power_ctrl_msg_unregister(rwnx_plat->power_priv);

	return ret;
}

void bes_power_ctrl_ibus_deinit(struct rwnx_plat *rwnx_plat)
{
	bes_ibus_rpmsg_deinit();
	bes_power_ctrl_msg_unregister(rwnx_plat->power_priv);
}

#endif

#ifdef CONFIG_BES_POWER_CTRL
static int bes_ibus_power_up(struct rwnx_plat *rwnx_plat)
{
	int ret = 0;

	if ((ret = bes_ibus_wifi_on())) {
		bes_err(BES_DBG_IBUS, "%s ret=%d\n", __func__, ret);
		return ret;
	}
	bes_power_ctrl_start_bus_probe();

	return 0;
}
#else
static int bes_ibus_power_up(struct rwnx_plat *rwnx_plat)
{
	return 0;
}
#endif

int bes_ibus_platform_init(struct rwnx_plat *rwnx_plat, void **p)
{
	int ret = 0;
	void **drvdata;
	void *tmp;
	if (p)
		drvdata = p;
	else
		drvdata = &tmp;

	/* set download state, and notity enter probe */
	bes_set_boot_wait_state(BES_BOOT_WAIT_DOWNLOAD_FW);
	bes_power_ctrl_bus_probe_notify();

	ret = rwnx_platform_init(rwnx_plat, drvdata);
	if (ret)
		return ret;
	platform_set_drvdata(rwnx_plat->pdev, *drvdata);
	return 0;
}

static int bes_ibus_probe(struct platform_device *pdev)
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	void *drvdata; /* will store rwnx_hw */
	int ret = -ENODEV;
	struct bes_power_state power_state;

	bes_dbg(BES_DBG_IBUS, "%s\n", __func__);

	bes_sysfs_set_device_id(0x2009);

	ret = bes_soc_platform_init(pdev, &rwnx_plat);
	if (ret)
		return ret;

	rwnx_plat->pdev = pdev;
	ret = bes_ibus_platform_init(rwnx_plat, &drvdata);
	if (ret)
		goto plat_err;

	/* handle the power down and unregister wlan case */
	bes_power_ctrl_get_power_state(&power_state);

	if (bes_sysfs_is_signal_mode() && !power_state.wifi_power) {
		bes_unregister_net_dev_after_get_dpd((struct rwnx_hw *)(rwnx_plat->drvdata));
		platform_set_drvdata(pdev, NULL);
	}

	if (!power_state.wifi_power && !power_state.bt_power) {
		bes_set_boot_wait_state(BES_BOOT_WAIT_PROBE_CLOSE);
		bes_power_ctrl_do_system_close();
		return 0;
	}

	if ((ret = bes_power_ctrl_system_enabled(true, false)) < 0)
		goto plat_deinit;

	return ret;

plat_deinit:
	rwnx_platform_deinit(drvdata);
	platform_set_drvdata(pdev, NULL);
plat_err:
	rwnx_plat->deinit(rwnx_plat);

	return ret;
}

static int bes_ibus_remove(struct platform_device *pdev)
{
	struct rwnx_hw *rwnx_hw;
	struct rwnx_plat *rwnx_plat;

	bes_dbg(BES_DBG_IBUS, "%s\n", __func__);

	bes_sysfs_unregister();

	rwnx_hw = (struct rwnx_hw *)platform_get_drvdata(pdev);
	if (rwnx_hw) {
		rwnx_plat = rwnx_hw->plat;
		if (rwnx_plat->drvdata)
			rwnx_platform_deinit(rwnx_hw);
		rwnx_plat->deinit(rwnx_plat);
		platform_set_drvdata(pdev, NULL);
	}

	return 0;
}

// allow wifi can sleep
static int bes_ibus_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_soc *bes_soc = (struct bes_soc *)rwnx_plat->priv;
	u32 tmp;
	bool dma_busy;
	int ret;

	tmp = readl(bes_soc->lp_status);
	dma_busy = tmp & MCU_WAKE_LOCK;
	bes_info(BES_DBG_IBUS, "%s rx_pending=%d dma_busy=%d\n", __func__,
			bes_soc->rx_pending, dma_busy);
	if (!bes_soc->rx_pending && !dma_busy) {
		if (hwspin_lock_timeout(bes_soc->lp_lock, 1000)) {
			return -ETIMEDOUT;
		}
		tmp &= ~ MCU_WAKE_STATUS;
		writel(tmp, bes_soc->lp_status);
		writel(SYS2WF_DATA_IND_SET, bes_soc->sys2wf_reg_base + SYS2WF_IRQ_SET_OFFSET);
		hwspin_unlock(bes_soc->lp_lock);
		if (readl_poll_timeout(bes_soc->sys2wf_reg_base + SYS2WF_IRQ_SET_OFFSET, tmp, tmp & SYS2WF_DATA_DONE_SET, 1000, 100000)) {
			ret = -EIO;
		} else {
			writel(SYS2WF_DATA_DONE_CLR, bes_soc->sys2wf_reg_base + SYS2WF_IRQ_CLR_OFFSET);
		}
	}

	return ret;
}

// wakeup wifi
static int bes_ibus_resume(struct platform_device *pdev)
{
	int ret;
	u32 tmp;
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_soc *bes_soc = (struct bes_soc *)rwnx_plat->priv;

	if (hwspin_lock_timeout(bes_soc->lp_lock, 1000)) {
		return -ETIMEDOUT;
	}
	tmp = readl(bes_soc->lp_status);
	tmp |= MCU_WAKE_STATUS;
	writel(tmp, bes_soc->lp_status);
	writel(SYS2WF_DATA_IND_SET, bes_soc->sys2wf_reg_base + SYS2WF_IRQ_SET_OFFSET);
	hwspin_unlock(bes_soc->lp_lock);
	if (readl_poll_timeout(bes_soc->sys2wf_reg_base + SYS2WF_IRQ_SET_OFFSET, tmp, tmp & SYS2WF_DATA_DONE_SET, 1000, 100000)) {
		ret = -EIO;
	} else {
		writel(SYS2WF_DATA_DONE_CLR, bes_soc->sys2wf_reg_base + SYS2WF_IRQ_CLR_OFFSET);
	}

	return ret;
}

static const struct of_device_id of_bes_match[] = {
	{ .compatible = "bestechnic, bes6000-wifi" },
	{},
};

struct platform_driver bes_ibus_driver = {
	.probe		= bes_ibus_probe,
	.remove		= (void *)bes_ibus_remove,
	.suspend 	= bes_ibus_suspend,
	.resume 	= bes_ibus_resume,
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = of_bes_match,
	},
};

#ifdef CONFIG_PLATFORM_DEVICE_BUILTIN
static struct resource wifi_resources[] = {
	[0] = {
		.start = WIFI_IBUS_MEM_BASE,
		.end = WIFI_IBUS_MEM_BASE + WIFI_IBUS_MEM_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
	[1] = {
		.start = 81, // 81 + ?
		.end = 81,
		.flags = IORESOURCE_IRQ,
	}
};
static struct platform_device bes_ibus_dev = {
	.name = DRIVER_NAME,
	.id = PLATFORM_DEVID_AUTO,
	.num_resources = ARRAY_SIZE(wifi_resources),
	.resource = wifi_resources,
};
#endif

MODULE_DEVICE_TABLE(of, of_bes_match);

int bes_ibus_register_drv(void)
{
	int ret = 0;
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();

	/* keep first */
	ret = bes_power_ctrl_ibus_init(rwnx_plat);
	if (ret)
		return ret;

	ret = bes_sysfs_register(rwnx_plat);
	if (ret)
		goto register_sysfs_err;

	bes_sysfs_status_update(true);

	if ((ret = bes_ibus_power_up(rwnx_plat)))
		goto power_up_err;

#ifdef CONFIG_PLATFORM_DEVICE_BUILTIN
	ret += platform_device_register(&bes_ibus_dev);
#endif
	ret += platform_driver_register(&bes_ibus_driver);

	return ret;

power_up_err:
	bes_power_ctrl_cancel_probe_work();
	bes_sysfs_unregister();
register_sysfs_err:
	bes_power_ctrl_ibus_deinit(rwnx_plat);
	return ret;
}

void bes_ibus_unregister_drv(void)
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
#ifdef CONFIG_PLATFORM_DEVICE_BUILTIN
	platform_device_unregister(&bes_ibus_dev);
#endif
	platform_driver_unregister(&bes_ibus_driver);
	bes_power_ctrl_ibus_deinit(rwnx_plat);
}
