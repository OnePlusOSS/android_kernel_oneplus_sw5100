/*
 * @file bes_power_ctrl.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "rwnx_defs.h"
#include "bes_sysfs.h"
#include "bes_power_ctrl.h"
#include "bes_log.h"
#include "lmac_msg.h"
#include "rwnx_msg_tx.h"
#include "bes_hwio.h"
#include "bes_psa.h"
#include "bes_tx_loop.h"

#define BES_POWER_CTRL_SCAN_TIMEOUT (HZ / 10 * 8)

static void __bes_set_boot_wait_state(struct bes_power_ctrl *bes_power_ctrl,
				      enum wait_state state);

/*********************************************************************
 * wlan netdev switch on/off API
 ********************************************************************/

static int bes_wlan_open(struct rwnx_hw *rwnx_hw)
{
	int ret = 0;

	ret = bes_send_wifi_on_off_req(rwnx_hw, true);
	if (ret)
		return ret;

	rwnx_hw->plat->dev_status |= BES_SLAVE_STATUS_WIFI_ON_READY;

	return ret;
}

void bes_power_ctrl_cancel_wlan_close_work(struct rwnx_hw *rwnx_hw)
{
	if (rwnx_hw)
		cancel_delayed_work_sync(&rwnx_hw->wlan_power_ctrl.wlan_close_work);
}

void bes_power_ctrl_schedule_wlan_close(struct rwnx_hw *rwnx_hw)
{
#if defined(CONFIG_BES_SDIO) || defined(CONFIG_BES_USB) || defined(CONFIG_BES_MSPI)
	if (bes_sysfs_is_signal_mode())
		schedule_delayed_work(&rwnx_hw->wlan_power_ctrl.wlan_close_work, HZ);
#endif
}

int bes_power_ctrl_wlan_open(struct rwnx_hw *rwnx_hw)
{
	int ret = 0;

	/* cancel switch close operation */
	cancel_delayed_work_sync(&rwnx_hw->wlan_power_ctrl.wlan_close_work);

	mutex_lock(&rwnx_hw->wlan_power_ctrl.power_mutex);

	/* reopen wlan and wait for startup completion message */
	ret = bes_wlan_open(rwnx_hw);
	if (ret)
		goto err;

	bes_set_wlan_enabled(rwnx_hw, true);

err:
	mutex_unlock(&rwnx_hw->wlan_power_ctrl.power_mutex);

	return ret;
}

int bes_power_ctrl_wlan_close(struct rwnx_hw *rwnx_hw)
{
	int ret = 0;

	mutex_lock(&rwnx_hw->wlan_power_ctrl.power_mutex);

	ret = bes_send_wifi_on_off_req(rwnx_hw, false);

	bes_set_wlan_enabled(rwnx_hw, false);

	rwnx_hw->plat->dev_status &= (~BES_SLAVE_STATUS_WIFI_ON_READY);

	mutex_unlock(&rwnx_hw->wlan_power_ctrl.power_mutex);

	return ret;
}

static void bes_wlan_close_work(struct work_struct *work)
{
	struct rwnx_hw *rwnx_hw = container_of(work, struct rwnx_hw,
					       wlan_power_ctrl.wlan_close_work.work);

	bes_power_ctrl_wlan_close(rwnx_hw);
}

void bes_unregister_net_dev_after_get_dpd(struct rwnx_hw *rwnx_hw)
{
	BUG_ON(!rwnx_hw);

	cancel_delayed_work_sync(&rwnx_hw->wlan_power_ctrl.wlan_close_work);
	/* ensure wlan in firmware is closed */
	if (bes_get_wlan_enabled(rwnx_hw))
		bes_power_ctrl_wlan_close(rwnx_hw);

	/* unregister wlan */
	rwnx_unregister_net_dev(rwnx_hw->plat);
}

bool bes_get_wlan_enabled(struct rwnx_hw *rwnx_hw)
{
	return rwnx_hw->wlan_power_ctrl.power_status;
}

void bes_set_wlan_enabled(struct rwnx_hw *rwnx_hw, bool on)
{
	rwnx_hw->wlan_power_ctrl.power_status = on;
}

void bes_wlan_power_ctrl_init(struct rwnx_hw *rwnx_hw)
{
	bes_set_wlan_enabled(rwnx_hw, false);
	INIT_DELAYED_WORK(&rwnx_hw->wlan_power_ctrl.wlan_close_work, bes_wlan_close_work);
	mutex_init(&rwnx_hw->wlan_power_ctrl.power_mutex);

}

/*********************************************************************
 * wifi/bt power on/off API, by gpio or message
 ********************************************************************/

static int bes_power_ctrl_read_op_status(struct bes_power_ctrl *bes_power_ctrl,
					 u32 rd_status, bool target_val, u32 timeout)
{
	return bes_wait_dev_status(bes_power_ctrl->plat, rd_status, target_val, timeout);
}

static void bes_power_ctrl_set_op_status(struct bes_power_ctrl *bes_power_ctrl,
					 u32 rd_status, bool target_val)
{
	struct rwnx_plat *rwnx_plat = bes_power_ctrl->plat;

	if (target_val)
		rwnx_plat->dev_status |= rd_status;
	else
		rwnx_plat->dev_status &= ~rd_status;
}

int bes_chip_power(struct rwnx_plat *rwnx_plat, bool on)
{
	int ret;
	struct bes_power_ctrl *bes_power_ctrl;

	if (!rwnx_plat)
		return -EFAULT;

	if (!rwnx_plat->power_priv)
		return -EFAULT;

	bes_power_ctrl = priv_to_power_ctrl(rwnx_plat->power_priv);
	if (!bes_power_ctrl->ops->chip_power)
		return -EOPNOTSUPP;

	mutex_lock(&bes_power_ctrl->io_mutex);
	ret = bes_power_ctrl->ops->chip_power(bes_power_ctrl, on);
	mutex_unlock(&bes_power_ctrl->io_mutex);

	return ret;
}

int bes_wakeup_wifi(struct bes_power_ctrl *bes_power_ctrl, bool wakeup, bool wait)
{
	int ret = 0;

	if (!bes_power_ctrl)
		return -EFAULT;

	if (!bes_power_ctrl->ops->wakeup_wifi)
		return -EOPNOTSUPP;

	mutex_lock(&bes_power_ctrl->io_mutex);

	ret = bes_power_ctrl->ops->wakeup_wifi(bes_power_ctrl, wakeup);

	/* sleep wifi not notify */
	if (!ret && wait && (ret = bes_power_ctrl_read_op_status(bes_power_ctrl,
	    BES_SLAVE_STATUS_MCU_WAKEUP_READY, wakeup, HZ / 50)))
		bes_err(BES_DBG_POWER_CTRL, "wifi enter %s status, confirm failed\n", wakeup ? "wakeup" : "sleep");
	else if (!ret && !wait)
		bes_power_ctrl_set_op_status(bes_power_ctrl, BES_SLAVE_STATUS_MCU_WAKEUP_READY, wakeup);

	mutex_unlock(&bes_power_ctrl->io_mutex);

	return ret;
}

int bes_bt_power(struct bes_power_ctrl *bes_power_ctrl, bool on)
{
	int ret = 0;

	if (!bes_power_ctrl)
		return -EFAULT;

	if (!bes_power_ctrl->ops->bt_power)
		return -EOPNOTSUPP;

	mutex_lock(&bes_power_ctrl->io_mutex);
	ret = bes_power_ctrl->ops->bt_power(bes_power_ctrl, on);

	if (!ret && (ret = bes_power_ctrl_read_op_status(bes_power_ctrl,
	    BES_SLAVE_STATUS_BT_ON_READY, on, HZ / 10 * 8)))
		bes_err(BES_DBG_POWER_CTRL, "bt enter %s status, confirm failed\n", on ? "on" : "off");

	mutex_unlock(&bes_power_ctrl->io_mutex);

	return ret;
}

int bes_bt_lp(struct bes_power_ctrl *bes_power_ctrl, bool wakeup)
{
	int ret = 0;

	if (!bes_power_ctrl)
		return -EFAULT;

	if (!bes_power_ctrl->ops->wakeup_bt)
		return -EOPNOTSUPP;

	mutex_lock(&bes_power_ctrl->io_mutex);

	ret = bes_power_ctrl->ops->wakeup_bt(bes_power_ctrl, wakeup);

	if (!ret && (ret = bes_power_ctrl_read_op_status(bes_power_ctrl,
	    BES_SLAVE_STATUS_BT_WAKEUP_READY, wakeup, HZ / 50)))
		bes_err(BES_DBG_POWER_CTRL, "bt enter %s status, confirm failed\n", wakeup ? "wakeup" : "sleep");

	mutex_unlock(&bes_power_ctrl->io_mutex);

	return ret;
}

static int bes_power_ctrl_system_open(struct bes_power_ctrl *bes_power_ctrl)
{
	int ret = 0;
	long status = 0;

	if (!bes_power_ctrl->ops->plat_power)
		return -EFAULT;

	/* reset bus error status when restart bes2610 */
	spin_lock(&bes_power_ctrl->status_lock);
	bes_power_ctrl->bus_error = false;
	bes_power_ctrl->bus_probe = BES_BUS_PROBE_START;
	spin_unlock(&bes_power_ctrl->status_lock);

	/* power up bes2610, trigger system to execute probe function */
	bes_power_ctrl->ops->plat_power(bes_power_ctrl->plat, true);

	/* wait bootup process end */
	status = wait_event_timeout(bes_power_ctrl->probe_wq_done,
				    bes_bootup_end(bes_power_ctrl), HZ * 8);
	WARN_ON(status <= 0);

	/* check if there is a error when bootup */
	ret = (status <= 0 || bes_power_ctrl_is_bus_error(bes_power_ctrl) ||
	       bes_power_ctrl->rmmod_stat > BES_POWER_CTRL_RMMOD_STATE_NONE) ? -1 : 0;

	/* add mdev */
	if (!ret)
		WARN_ON(bes_mdev_register());

	return ret;
}

int bes_power_ctrl_do_system_close(void)
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_power_ctrl *bes_power_ctrl = priv_to_power_ctrl(rwnx_plat->power_priv);
	int ret = 0;
	long status = 0;

	/* case for message power control */
	if (!bes_power_ctrl->ops->plat_power) {
		bes_power_ctrl_system_enabled(true, false);
		return 0;
	}

	if (!bes_power_ctrl->plat)
		return -EFAULT;

	bes_info(BES_DBG_POWER_CTRL, "power down bes2610.\n");

	/* trigger system to execute disconnect function */
	ret = bes_power_ctrl->ops->plat_power(bes_power_ctrl->plat, false);

	/* wait disconnect event */
	status = wait_event_timeout(bes_power_ctrl->probe_wq_done,
				(bes_power_ctrl->system_enabled == false), HZ * 2);
	WARN_ON(status <= 0);
	if (status <= 0)
		ret = -ETIMEDOUT;

	return ret;
}

bool bes_bootup_end(struct bes_power_ctrl *bes_power_ctrl)
{
	bool end = false;
	u8 allow_shutdown;

	bes_power_ctrl->ops->get_attr(bes_power_ctrl, BES_POWER_CTRL_ATTR_DEV_SHUTDOWN, &allow_shutdown);

	spin_lock(&bes_power_ctrl->status_lock);

	if (allow_shutdown && (!bes_power_ctrl->wifi_power && !bes_power_ctrl->bt_power))
		end = (bes_power_ctrl->bus_probe == BES_BUS_PROBE_TIMEOUT ||
		       bes_power_ctrl->bus_error ||
		       bes_power_ctrl->wait_state == BES_BOOT_WAIT_NONE);
	else
		end = (bes_power_ctrl->bus_probe == BES_BUS_PROBE_TIMEOUT ||
		       bes_power_ctrl->system_enabled ||
		       bes_power_ctrl->bus_error);

	spin_unlock(&bes_power_ctrl->status_lock);

	return end;
}

static void bes_probe_timeout_work(struct work_struct *ws)
{
	struct delayed_work *dw = container_of(ws, struct delayed_work, work);
	struct bes_power_ctrl *bes_power_ctrl = container_of(dw, struct bes_power_ctrl, probe_timeout_work);

	spin_lock(&bes_power_ctrl->status_lock);
	bes_power_ctrl->bus_probe = BES_BUS_PROBE_TIMEOUT;
	bes_power_ctrl->wifi_power = false;
	bes_power_ctrl->bt_power = false;
	bes_power_ctrl->bton_pending = false;
	spin_unlock(&bes_power_ctrl->status_lock);

	bes_err(BES_DBG_POWER_CTRL, "bus probe timeout\n");
	wake_up(&bes_power_ctrl->probe_wq_done);
}

bool bes_power_ctrl_is_bus_error(struct bes_power_ctrl *bes_power_ctrl)
{
	bool error = false;
	u8 allow_shutdown;

	bes_power_ctrl->ops->get_attr(bes_power_ctrl, BES_POWER_CTRL_ATTR_DEV_SHUTDOWN, &allow_shutdown);

	spin_lock(&bes_power_ctrl->status_lock);

	if (allow_shutdown && (!bes_power_ctrl->wifi_power && !bes_power_ctrl->bt_power))
		error = (bes_power_ctrl->bus_error || bes_power_ctrl->bus_probe >= BES_BUS_PROBE_START);
	else
		error = (bes_power_ctrl->bus_error || bes_power_ctrl->bus_probe != BES_BUS_PROBE_OK);
	spin_unlock(&bes_power_ctrl->status_lock);

	return error;
}

bool bes_power_ctrl_check_system_close(struct bes_power_ctrl *bes_power_ctrl)
{
	bool sys_closed = false;
	u8 allow_shutdown, bt_self_managed;

	if (!bes_power_ctrl)
		return false;

	bes_power_ctrl->ops->get_attr(bes_power_ctrl, BES_POWER_CTRL_ATTR_DEV_SHUTDOWN, &allow_shutdown);
	bes_power_ctrl->ops->get_attr(bes_power_ctrl, BES_POWER_CTRL_ATTR_BT_SELF_MANAGED, &bt_self_managed);

	if (!allow_shutdown)
		return false;

	spin_lock(&bes_power_ctrl->status_lock);
	sys_closed = (bes_power_ctrl->wifi_power == false) &&
		     ((bes_power_ctrl->bt_power == false) || bt_self_managed);
	spin_unlock(&bes_power_ctrl->status_lock);

	return sys_closed;
}

static int bes_power_ctrl_can_op_switch(struct bes_power_ctrl *bes_power_ctrl,
					enum bes_power_ctrl_operate_type op_type)
{
	long status = 0xFF; /* Initialize a positive number */
	int fw_type;
	enum wait_state wait_state;
	enum bus_probe_state probe_state;
	enum bes_power_ctrl_rmmod_state rmmod_stat;
	bool bus_error;
	bool condition_met = false;

	spin_lock(&bes_power_ctrl->status_lock);
	rmmod_stat = bes_power_ctrl->rmmod_stat;
	bus_error = bes_power_ctrl->bus_error;
	spin_unlock(&bes_power_ctrl->status_lock);

	if (bus_error)
		return -EFAULT;

	if (rmmod_stat == BES_POWER_CTRL_RMMOD_STATE_IN)
		return -EFAULT;

	mutex_lock(&bes_power_ctrl->probe_mutex);

	fw_type = bes_sysfs_get_fw_type();

	/* only work for wifi signal mode except fw_type */
	if (op_type == BES_POWER_CTRL_OPERATE_RMMOD) {
		/* go on */
	} else if (op_type != BES_POWER_CTRL_OPERATE_FW_TYPE && fw_type != BES_FW_TYPE_WIFI_SIGNAL) {
		mutex_unlock(&bes_power_ctrl->probe_mutex);
		return -EPERM;
	}

	spin_lock(&bes_power_ctrl->status_lock);
	probe_state = bes_power_ctrl->bus_probe;
	spin_unlock(&bes_power_ctrl->status_lock);
	/* wait bus probe operation end */
	if (probe_state == BES_BUS_PROBE_START) {
		bes_dbg(BES_DBG_POWER_CTRL, "wait bus probe operation end\n");
		status = wait_event_timeout(bes_power_ctrl->probe_wq_done,
					    ({
						spin_lock(&bes_power_ctrl->status_lock);
						condition_met = (bes_power_ctrl->bus_probe > BES_BUS_PROBE_START);
						spin_unlock(&bes_power_ctrl->status_lock);
						condition_met;
					    }),
					    HZ * 3);
		WARN_ON(status <= 0);
		if (status <= 0)
			goto err;
	} else if (probe_state > BES_BUS_PROBE_OK) {
		goto err;
	}

	spin_lock(&bes_power_ctrl->status_lock);
	wait_state = bes_power_ctrl->wait_state;
	spin_unlock(&bes_power_ctrl->status_lock);
	/* must wait previous operation end in critical section */
	if (wait_state != BES_BOOT_WAIT_NONE) {
		bes_dbg(BES_DBG_POWER_CTRL, "wait previous operation end\n");
		status = wait_event_timeout(bes_power_ctrl->probe_wq_done,
					    ({
						spin_lock(&bes_power_ctrl->status_lock);
						condition_met = (bes_power_ctrl->wait_state == BES_BOOT_WAIT_NONE);
						spin_unlock(&bes_power_ctrl->status_lock);
						condition_met;
					    }),
					    HZ * 8);
		WARN_ON(status <= 0);
	}

	if (status <= 0)
		goto err;

	/* switch operations are mutually exclusive, setting the waiting state */
	spin_lock(&bes_power_ctrl->status_lock);
	if (op_type == BES_POWER_CTRL_OPERATE_RMMOD)
		bes_power_ctrl->rmmod_stat = BES_POWER_CTRL_RMMOD_STATE_IN;

	__bes_set_boot_wait_state(bes_power_ctrl, BES_BOOT_WAIT_POWER_OPERATE_DONE);
	spin_unlock(&bes_power_ctrl->status_lock);

	mutex_unlock(&bes_power_ctrl->probe_mutex);

	return 0;

err:
	mutex_unlock(&bes_power_ctrl->probe_mutex);
	return -ETIMEDOUT;
}

void bes_power_ctrl_prep_rmmod(struct rwnx_plat *rwnx_plat)
{
	struct bes_power_ctrl *bes_power_ctrl = priv_to_power_ctrl(rwnx_plat->power_priv);

	spin_lock(&bes_power_ctrl->status_lock);
	bes_power_ctrl->rmmod_stat = BES_POWER_CTRL_RMMOD_STATE_PREP;
	spin_unlock(&bes_power_ctrl->status_lock);

	bes_power_ctrl_can_op_switch(bes_power_ctrl, BES_POWER_CTRL_OPERATE_RMMOD);
}

static int bes_power_ctrl_operate_wifi_power(struct bes_power_ctrl *bes_power_ctrl, bool on)
{
	int ret = 0;

	if (on) {
		if (bes_power_ctrl_check_system_close(bes_power_ctrl)) {
			/* When sdio card scanning fails, power_ctrl is NULL. */
			if (!bes_power_ctrl->ops->plat_power || !bes_power_ctrl->plat)
				return -EFAULT;

			spin_lock(&bes_power_ctrl->status_lock);
			bes_power_ctrl->wifi_power = true;
			spin_unlock(&bes_power_ctrl->status_lock);

			bes_info(BES_DBG_POWER_CTRL, "power up bes2610 when active wifi.\n");
			ret = bes_power_ctrl_system_open(bes_power_ctrl);
		} else {
			/* chip is already powered up, we just need to create net device */
			if (bes_power_ctrl->plat) {
				if (!rwnx_net_dev_created(bes_power_ctrl->plat)) {
					bes_info(BES_DBG_POWER_CTRL, "bes2610 activate wifi\n");
					bes_power_ctrl->wifi_power = true;
					ret = rwnx_register_net_dev(bes_power_ctrl->plat);
				}
			} else {
				ret = -EFAULT;
			}
		}
	} else {
		if (bes_power_ctrl->plat && rwnx_net_dev_created(bes_power_ctrl->plat)) {
			ret = rwnx_unregister_net_dev(bes_power_ctrl->plat);
		}
	}

	if (!ret) {
		spin_lock(&bes_power_ctrl->status_lock);
		bes_power_ctrl->wifi_power = on;
		spin_unlock(&bes_power_ctrl->status_lock);
	} else {
		spin_lock(&bes_power_ctrl->status_lock);
		bes_power_ctrl->wifi_power = false;
		spin_unlock(&bes_power_ctrl->status_lock);
		if (on)
			bes_err(BES_DBG_POWER_CTRL, "open wifi failed\n");
	}

	if (bes_power_ctrl_check_system_close(bes_power_ctrl))
		ret = bes_power_ctrl_do_system_close();

	return ret;
}

int bes_power_ctrl_wifi_power(struct bes_power_ctrl *bes_power_ctrl, bool on)
{
	int ret = 0;

	/* Check whether need to change. If need, Set wifi_pwr_change flag to true */
	spin_lock(&bes_power_ctrl->status_lock);
	if (bes_power_ctrl->wifi_power == on) {
		spin_unlock(&bes_power_ctrl->status_lock);
		return 0;
	}
	bes_power_ctrl->wifi_pwr_change = true;
	spin_unlock(&bes_power_ctrl->status_lock);

	ret = bes_power_ctrl_can_op_switch(bes_power_ctrl, BES_POWER_CTRL_OPERATE_WIFI);
	if (ret) {
		spin_lock(&bes_power_ctrl->status_lock);
		if (bes_power_ctrl->wifi_pwr_change) { /* restore state */
			bes_power_ctrl->wifi_power = !on;
			bes_power_ctrl->wifi_pwr_change = false;
		}
		spin_unlock(&bes_power_ctrl->status_lock);
		return ret;
	}

	/* Wait for the end of the close wlan operation */
	if (!on && rwnx_net_dev_created(bes_power_ctrl->plat)) {
		struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)bes_power_ctrl->plat->drvdata;
		cancel_delayed_work_sync(&rwnx_hw->wlan_power_ctrl.wlan_close_work);
	}

	ret = bes_power_ctrl_operate_wifi_power(bes_power_ctrl, on);

	/* Clear wifi powering up flag */
	spin_lock(&bes_power_ctrl->status_lock);
	bes_power_ctrl->wifi_pwr_change = false;
	spin_unlock(&bes_power_ctrl->status_lock);

	bes_set_boot_wait_state(BES_BOOT_WAIT_NONE);
	wake_up(&bes_power_ctrl->probe_wq_done);

	return ret;
}

static int bes_power_ctrl_operate_bt_power(struct bes_power_ctrl *bes_power_ctrl, bool on)
{
	int ret = 0;

	if (on) {
		if (bes_power_ctrl_check_system_close(bes_power_ctrl)) {
			if (!bes_power_ctrl->ops->plat_power)
				return -EFAULT;
			bes_info(BES_DBG_POWER_CTRL, "power up bes2610 when active bt\n");

			/* set opend state in advance */
			spin_lock(&bes_power_ctrl->status_lock);
			bes_power_ctrl->bt_power = true;
			bes_power_ctrl->bton_pending = true;
			spin_unlock(&bes_power_ctrl->status_lock);

			/* power up bes2610, trigger system to execute probe function */
			ret = bes_power_ctrl_system_open(bes_power_ctrl);
		} else {
			bes_info(BES_DBG_POWER_CTRL, "bes2610 activate bt\n");
			ret = bes_bt_power(bes_power_ctrl, true);
		}
	} else {
		bes_info(BES_DBG_POWER_CTRL, "bes2610 deactivate bt\n");
		ret = bes_bt_power(bes_power_ctrl, false);
	}

	if (!ret) {
		spin_lock(&bes_power_ctrl->status_lock);
		bes_power_ctrl->bt_power = on;
		spin_unlock(&bes_power_ctrl->status_lock);
	} else {
		spin_lock(&bes_power_ctrl->status_lock);
		bes_power_ctrl->bt_power = false;
		bes_power_ctrl->bton_pending = false;
		spin_unlock(&bes_power_ctrl->status_lock);
		if (on)
			bes_err(BES_DBG_POWER_CTRL, "open bt failed\n");
	}

	if (!ret && bes_power_ctrl_check_system_close(bes_power_ctrl))
		ret = bes_power_ctrl_do_system_close();

	return ret;
}

int bes_power_ctrl_bt_power(struct bes_power_ctrl *bes_power_ctrl, bool on)
{
	int ret = 0;

	/* Check whether need to change. If need, Set bt_pwr_change flag to true */
	spin_lock(&bes_power_ctrl->status_lock);
	if (bes_power_ctrl->bt_power == on) {
		spin_unlock(&bes_power_ctrl->status_lock);
		return 0;
	}
	bes_power_ctrl->bt_pwr_change = true;
	spin_unlock(&bes_power_ctrl->status_lock);

	ret = bes_power_ctrl_can_op_switch(bes_power_ctrl, BES_POWER_CTRL_OPERATE_BT);
	if (ret) {
		spin_lock(&bes_power_ctrl->status_lock);
		if (bes_power_ctrl->bt_pwr_change) { /* restore state */
			bes_power_ctrl->bt_power = !on;
			bes_power_ctrl->bt_pwr_change = false;
		}
		spin_unlock(&bes_power_ctrl->status_lock);
		return ret;
	}

	ret = bes_power_ctrl_operate_bt_power(bes_power_ctrl, on);

	/* Clear bt powering up flag */
	spin_lock(&bes_power_ctrl->status_lock);
	bes_power_ctrl->bt_pwr_change = false;
	spin_unlock(&bes_power_ctrl->status_lock);

	bes_set_boot_wait_state(BES_BOOT_WAIT_NONE);
	wake_up(&bes_power_ctrl->probe_wq_done);

	return ret;
}

int bes_power_ctrl_bt_lp(struct bes_power_ctrl *bes_power_ctrl, bool bt_lp)
{
	int ret = 0;
	bool bt_power;

	long status;
	ret = bes_power_ctrl_can_op_switch(bes_power_ctrl, BES_POWER_CTRL_OPERATE_BT_LP);
	if (ret)
		return ret;

	spin_lock(&bes_power_ctrl->status_lock);
	bt_power = bes_power_ctrl->bt_power;
	spin_unlock(&bes_power_ctrl->status_lock);

	if (!bt_power) {
		ret = -EAGAIN;
		goto exit;
	}

	/* wait probe done event */
	status = wait_event_timeout(bes_power_ctrl->probe_wq_done,
				    bes_bootup_end(bes_power_ctrl), HZ * 8);

	if (status <= 0 || bes_power_ctrl_is_bus_error(bes_power_ctrl) ||
	    bes_power_ctrl->rmmod_stat > BES_POWER_CTRL_RMMOD_STATE_NONE)
		return -ETIMEDOUT;

	ret = bes_bt_lp(bes_power_ctrl, bt_lp ? false : true);
	if (ret)
		bes_err(BES_DBG_POWER_CTRL, "bt %s failed\n", bt_lp ? "sleep" : "wakeup");

exit:
	bes_set_boot_wait_state(BES_BOOT_WAIT_NONE);
	wake_up(&bes_power_ctrl->probe_wq_done);

	return ret;
}

int bes_power_ctrl_change_fw_type(struct bes_sysfs *bes_sysfs, int type)
{
	int ret = 0;
	struct bes_power_ctrl *bes_power_ctrl;

	/* no need to realod firmware if new fw_type is equal to the old */
	if (type == bes_sysfs_get_fw_type()) {
		bes_dbg(BES_DBG_POWER_CTRL, "fw type is equal\n");
		return 0;
	}

	bes_power_ctrl = bes_sysfs->power_ctrl;

	if (!bes_power_ctrl)
		return -EFAULT;

	if (!bes_power_ctrl->ops->chg_fw_type)
		return -EOPNOTSUPP;

	ret = bes_power_ctrl_can_op_switch(bes_sysfs->power_ctrl, BES_POWER_CTRL_OPERATE_FW_TYPE);
	if (ret)
		return ret;

	/* Wait for the end of the close wlan operation */
	if (rwnx_net_dev_created(bes_sysfs->power_ctrl->plat)) {
		struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)(bes_sysfs->power_ctrl->plat->drvdata);
		cancel_delayed_work_sync(&rwnx_hw->wlan_power_ctrl.wlan_close_work);
	}

	ret = bes_power_ctrl->ops->chg_fw_type(bes_sysfs->power_ctrl, type);

	return ret;
}

bool bes_power_ctrl_wifi_enabled(void)
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_power_ctrl *bes_power_ctrl = priv_to_power_ctrl(rwnx_plat->power_priv);
	bool wifi_enabled = false;

	spin_lock(&bes_power_ctrl->status_lock);
	wifi_enabled = bes_power_ctrl->wifi_power;
	spin_unlock(&bes_power_ctrl->status_lock);

	return wifi_enabled;
}

bool bes_power_ctrl_bt_enabled(void)
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_power_ctrl *bes_power_ctrl = priv_to_power_ctrl(rwnx_plat->power_priv);
	bool bt_enabled = false;
	bool bt_self_mgmd = !!CONFIG_BES_POWER_CTRL_BT_SELF_MANAGED;

	if (bt_self_mgmd) {
		bt_enabled = !bes_coex_bt_closed((struct rwnx_hw *)rwnx_plat->drvdata);
	} else {
		spin_lock(&bes_power_ctrl->status_lock);
		bt_enabled = bes_power_ctrl->bt_power;
		spin_unlock(&bes_power_ctrl->status_lock);
	}

	return bt_enabled;
}

void bes_power_ctrl_get_power_state(struct bes_power_state *power_state)
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_power_ctrl *bes_power_ctrl = priv_to_power_ctrl(rwnx_plat->power_priv);

	spin_lock(&bes_power_ctrl->status_lock);
	power_state->wifi_power = bes_power_ctrl->wifi_power;
	power_state->bt_power = bes_power_ctrl->bt_power;
	spin_unlock(&bes_power_ctrl->status_lock);

}

bool bes_power_ctrl_recovery_trigger(void)
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_power_ctrl *bes_power_ctrl = priv_to_power_ctrl(rwnx_plat->power_priv);
	bool recovery = false;

	spin_lock(&bes_power_ctrl->status_lock);
	if (bes_power_ctrl->no_reboot_fw_chg) {
		spin_unlock(&bes_power_ctrl->status_lock);
		return false;
	}
	recovery = bes_power_ctrl->wifi_power && !bes_power_ctrl->wifi_pwr_change;
	spin_unlock(&bes_power_ctrl->status_lock);

	return recovery;
}

void bes_power_ctrl_set_system_closed(struct bes_power_ctrl *bes_power_ctrl)
{
	spin_lock(&bes_power_ctrl->status_lock);
	bes_power_ctrl->system_enabled = false;
	spin_unlock(&bes_power_ctrl->status_lock);
}

int bes_power_ctrl_system_enabled(bool system_enabled, bool bus_error)
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_power_ctrl *bes_power_ctrl = priv_to_power_ctrl(rwnx_plat->power_priv);
	int ret = 0;

	bes_power_ctrl->system_enabled = system_enabled;

	if (system_enabled) {
		if (bes_power_ctrl->bton_pending) {
			ret = bes_bt_power(bes_power_ctrl, true);
			if (ret)
				return ret;

			bes_info(BES_DBG_POWER_CTRL, "execute pending bt on operation.\n");
			bes_power_ctrl->bton_pending = false;
		}

		spin_lock(&bes_power_ctrl->status_lock);
		__bes_set_boot_wait_state(bes_power_ctrl, BES_BOOT_WAIT_NONE);
		spin_unlock(&bes_power_ctrl->status_lock);
		bes_info(BES_DBG_POWER_CTRL, "wakup proc on wq of probe_done.\n");
	} else {
		spin_lock(&bes_power_ctrl->status_lock);
		bes_power_ctrl->bus_error = bus_error;
		if (!bus_error && bes_power_ctrl->wait_state > BES_BOOT_WAIT_DOWNLOAD_FW)
			__bes_set_boot_wait_state(bes_power_ctrl, BES_BOOT_WAIT_NONE);
		bes_power_ctrl->wifi_power = false;
		bes_power_ctrl->bt_power = false;
		bes_power_ctrl->bton_pending = false;
		if (bes_power_ctrl->bus_probe > BES_BUS_PROBE_START)
			bes_power_ctrl->bus_probe = BES_BUS_PROBE_NONE;
		spin_unlock(&bes_power_ctrl->status_lock);
		bes_info(BES_DBG_POWER_CTRL, "wakup proc on wq of disconnect_done.\n");
	}

	wake_up(&bes_power_ctrl->probe_wq_done);

	return 0;
}

int bes_power_ctrl_wait_probe_done(struct bes_power_ctrl *bes_power_ctrl)
{
	long status = 0;
	int ret;

	/* wait probe done event */
	status = wait_event_timeout(bes_power_ctrl->probe_wq_done,
				    bes_bootup_end(bes_power_ctrl), HZ * 8);
	WARN_ON(status <= 0);

	ret = (status <= 0 || bes_power_ctrl_is_bus_error(bes_power_ctrl) ||
	       bes_power_ctrl->rmmod_stat > BES_POWER_CTRL_RMMOD_STATE_NONE) ? -1 : 0;

	return ret;
}

int bes_power_ctrl_wait_disconnect_done(struct bes_power_ctrl *bes_power_ctrl)
{
	long status = 0;
	int ret = 0;

	status = wait_event_timeout(bes_power_ctrl->probe_wq_done,
				   (bes_power_ctrl->system_enabled == false), HZ * 5);
	WARN_ON(status <= 0);

	if (status <= 0)
		ret = -ETIMEDOUT;

	return ret;
}

void bes_power_ctrl_start_bus_probe(void)
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_power_ctrl *bes_power_ctrl = priv_to_power_ctrl(rwnx_plat->power_priv);

	spin_lock(&bes_power_ctrl->status_lock);
	bes_power_ctrl->bus_probe = BES_BUS_PROBE_START;
	__bes_set_boot_wait_state(bes_power_ctrl, BES_BOOT_WAIT_DOWNLOAD_FW);
	spin_unlock(&bes_power_ctrl->status_lock);

	cancel_delayed_work_sync(&bes_power_ctrl->probe_timeout_work);

	/**
	 * When the bootup is success,
	 * the call is canceled by bes_power_ctrl_bus_probe_notify().
	 */
	schedule_delayed_work(&bes_power_ctrl->probe_timeout_work, BES_BUS_PROBE_TIMEOUT_JIFFIES);
}

void bes_power_ctrl_cancel_probe_work(void)
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_power_ctrl *bes_power_ctrl = priv_to_power_ctrl(rwnx_plat->power_priv);
	cancel_delayed_work_sync(&bes_power_ctrl->probe_timeout_work);
}

void bes_power_ctrl_bus_probe_notify(void)
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_power_ctrl *bes_power_ctrl = priv_to_power_ctrl(rwnx_plat->power_priv);

	spin_lock(&bes_power_ctrl->status_lock);
	bes_power_ctrl->bus_probe = BES_BUS_PROBE_OK;
	spin_unlock(&bes_power_ctrl->status_lock);
	cancel_delayed_work_sync(&bes_power_ctrl->probe_timeout_work);

	wake_up(&bes_power_ctrl->probe_wq_done);
}

static void bes_power_ctrl_clean_scan_timer_cb(struct timer_list *t_list)
{
	struct bes_power_ctrl *bes_power_ctrl = container_of(t_list, struct bes_power_ctrl, scan_clean);

	spin_lock(&bes_power_ctrl->status_lock);
	bes_power_ctrl->start_scan_time = 0;
	bes_power_ctrl->wait_scan = false;
	spin_unlock(&bes_power_ctrl->status_lock);
}

void bes_power_ctrl_set_scancard_time(void)
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_power_ctrl *bes_power_ctrl = priv_to_power_ctrl(rwnx_plat->power_priv);
	unsigned long current_time;

	spin_lock(&bes_power_ctrl->status_lock);
	current_time = jiffies;
	bes_power_ctrl->start_scan_time = current_time;
	bes_power_ctrl->wait_scan = true;
	spin_unlock(&bes_power_ctrl->status_lock);
	mod_timer(&bes_power_ctrl->scan_clean, current_time + BES_POWER_CTRL_SCAN_TIMEOUT);
}

void bes_power_ctrl_wait_scan_finish(void)
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_power_ctrl *bes_power_ctrl = priv_to_power_ctrl(rwnx_plat->power_priv);
	unsigned long start;
	unsigned long current_time;
	unsigned long wait_time;
	unsigned long tmp;
	bool wait_scan;

	spin_lock(&bes_power_ctrl->status_lock);
	start = bes_power_ctrl->start_scan_time;
	wait_scan = bes_power_ctrl->wait_scan;
	current_time = jiffies;

	/* do not wait */
	if (!wait_scan)
		goto exit;

	if (time_after(current_time, start + BES_POWER_CTRL_SCAN_TIMEOUT)) {
		bes_power_ctrl->start_scan_time = 0;
		bes_power_ctrl->wait_scan = false;
		goto exit;
	}

	tmp = start + BES_POWER_CTRL_SCAN_TIMEOUT;
	if (tmp < start) {
		if (current_time > start)
			wait_time = ULONG_MAX - current_time + tmp;
		else
			wait_time = tmp - current_time;
	} else {
		wait_time = tmp - current_time;
	}
	spin_unlock(&bes_power_ctrl->status_lock);

	msleep(wait_time);
	return;

exit:
	spin_unlock(&bes_power_ctrl->status_lock);
}

void bes_power_ctrl_set_chg_fw_type(struct bes_power_ctrl *bes_power_ctrl)
{
	spin_lock(&bes_power_ctrl->status_lock);
	bes_power_ctrl->is_chg_fw_type = true;
	spin_unlock(&bes_power_ctrl->status_lock);
}

bool bes_power_ctrl_is_chg_fw_type(void)
{
	bool ret = false;
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_power_ctrl *bes_power_ctrl = priv_to_power_ctrl(rwnx_plat->power_priv);

	spin_lock(&bes_power_ctrl->status_lock);
	ret = bes_power_ctrl->is_chg_fw_type;
	spin_unlock(&bes_power_ctrl->status_lock);

	return ret;
}

void bes_power_ctrl_state_reset(void)
{
	int fw_type = bes_sysfs_get_fw_type();
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_power_ctrl *bes_power_ctrl = priv_to_power_ctrl(rwnx_plat->power_priv);

	spin_lock(&bes_power_ctrl->status_lock);
	/* update wifi/bt switch status */
	if (fw_type == BES_FW_TYPE_WIFI_SIGNAL) {
#ifdef CONFIG_BES_WIFI_BOOT_ON
		bes_power_ctrl->wifi_power = true;
#else
		bes_power_ctrl->wifi_power = false;
#endif
#ifdef CONFIG_BES_BT_BOOT_ON
		bes_power_ctrl->bt_power = true;
		bes_power_ctrl->bton_pending = !CONFIG_BES_POWER_CTRL_BT_SELF_MANAGED;
#else
		bes_power_ctrl->bt_power = false;
		bes_power_ctrl->bton_pending = false;
#endif
		bes_power_ctrl->wifi_pwr_change = false;
		bes_power_ctrl->bt_pwr_change = false;
	} else if (fw_type == BES_FW_TYPE_WIFI_NONSIGNAL) {
		bes_power_ctrl->wifi_power = true;
		bes_power_ctrl->bt_power = false;
		bes_power_ctrl->bton_pending = false;
	} else if (fw_type == BES_FW_TYPE_BT) {
		bes_power_ctrl->wifi_power = false;
		bes_power_ctrl->bt_power = true;
		bes_power_ctrl->bton_pending = false;
	}
	bes_power_ctrl->bus_error = false;
	bes_power_ctrl->system_enabled = false;
	bes_power_ctrl->is_chg_fw_type = false;
	bes_power_ctrl->no_reboot_fw_chg = false;

	spin_unlock(&bes_power_ctrl->status_lock);
}

static void bes_power_ctrl_pm_awake(struct bes_power_ctrl *bes_power_ctrl)
{
	bool pm_awake = false;

	if (bes_power_ctrl->stay_pm == false) {
		__pm_stay_awake(bes_power_ctrl->ws);
		bes_power_ctrl->stay_pm = true;
		pm_awake = true;
	}

	if (!pm_awake)
		bes_dbg(BES_DBG_POWER_CTRL, "%s, pm already awake, do not awake\n", __func__);
	else
		bes_dbg(BES_DBG_POWER_CTRL, "%s, pm awake\n", __func__);
}

static void bes_power_ctrl_pm_relax(struct bes_power_ctrl *bes_power_ctrl)
{
	bool pm_relax = false;

	if (bes_power_ctrl->stay_pm == true) {
		__pm_relax(bes_power_ctrl->ws);
		bes_power_ctrl->stay_pm = false;
		pm_relax = true;
	}

	if (!pm_relax)
		bes_dbg(BES_DBG_POWER_CTRL, "%s, pm already relax, do not relax\n", __func__);
	else
		bes_dbg(BES_DBG_POWER_CTRL, "%s, pm relax\n", __func__);
}

void __bes_set_boot_wait_state(struct bes_power_ctrl *bes_power_ctrl, enum wait_state state)
{
	bes_power_ctrl->wait_state = state;
	if (state != BES_BOOT_WAIT_NONE)
		bes_power_ctrl_pm_awake(bes_power_ctrl);
	else
		bes_power_ctrl_pm_relax(bes_power_ctrl);
}

void bes_set_boot_wait_state(enum wait_state state)
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_power_ctrl *bes_power_ctrl = priv_to_power_ctrl(rwnx_plat->power_priv);

	spin_lock(&bes_power_ctrl->status_lock);
	__bes_set_boot_wait_state(bes_power_ctrl, state);
	spin_unlock(&bes_power_ctrl->status_lock);
}

bool bes_power_ctrl_can_skip_platform_init(struct rwnx_plat *rwnx_plat)
{
	struct bes_power_ctrl *bes_power_ctrl = priv_to_power_ctrl(rwnx_plat->power_priv);
	bool ret = false;

	spin_lock(&bes_power_ctrl->status_lock);
	if (rwnx_plat->dpd_cali.data && bes_power_ctrl->bt_power &&
	    !bes_power_ctrl->wifi_power) {
		__bes_set_boot_wait_state(bes_power_ctrl, BES_BOOT_WAIT_PROBE_DONE);
		ret = true;
	}
	spin_unlock(&bes_power_ctrl->status_lock);

	return ret;
}

void *bes_power_ctrl_register(const struct bes_power_ctrl_ops *ops, struct rwnx_plat *rwnx_plat, int priv_size)
{
	struct bes_power_ctrl *bes_power_ctrl;
	int alloc_size;

	BUG_ON(!ops->get_attr);

	alloc_size = sizeof(*bes_power_ctrl) + priv_size;

	bes_power_ctrl = kzalloc(alloc_size, GFP_KERNEL);
	if (!bes_power_ctrl)
		return NULL;

	bes_power_ctrl->ws = wakeup_source_register(NULL, "bes_power_ctrl");
	if (!bes_power_ctrl->ws)
		goto register_ws_err;

	init_waitqueue_head(&bes_power_ctrl->probe_wq_done);
	INIT_DELAYED_WORK(&bes_power_ctrl->probe_timeout_work, bes_probe_timeout_work);

	spin_lock_init(&bes_power_ctrl->status_lock);
	mutex_init(&bes_power_ctrl->probe_mutex);
	mutex_init(&bes_power_ctrl->io_mutex);

	bes_power_ctrl->system_enabled = false;
	bes_power_ctrl->bus_error = false;
	bes_power_ctrl->wait_state = BES_BOOT_WAIT_NONE;
	bes_power_ctrl->stay_pm = false;

	bes_power_ctrl->ops = ops;
	bes_power_ctrl->plat = rwnx_plat;
	timer_setup(&bes_power_ctrl->scan_clean, bes_power_ctrl_clean_scan_timer_cb, 0);

	return &bes_power_ctrl->priv;

register_ws_err:
	kfree(bes_power_ctrl);
	return NULL;
}

void bes_power_ctrl_unregister(struct bes_power_ctrl *bes_power_ctrl)
{
	if (!bes_power_ctrl)
		return;

	if (bes_power_ctrl->ws != NULL) {
		wakeup_source_unregister(bes_power_ctrl->ws);
		bes_power_ctrl->ws = NULL;
	}

	mutex_destroy(&bes_power_ctrl->probe_mutex);
	mutex_destroy(&bes_power_ctrl->io_mutex);
	del_timer_sync(&bes_power_ctrl->scan_clean);

	kfree(bes_power_ctrl);
}

int bes_power_ctrl_no_reboot_fw_chg_set(struct bes_power_ctrl *bes_power_ctrl, bool fw_chging)
{
	int ret = 0;
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();

	if (!rwnx_plat)
		return -EFAULT;

	spin_lock(&bes_power_ctrl->status_lock);
	/* no reboot change fw type operation not allowed when wifi_power is off */
	if (!rwnx_plat->drvdata || !bes_power_ctrl->wifi_power) {
		spin_unlock(&bes_power_ctrl->status_lock);
		bes_err(BES_DBG_SYSFS, "no reboot change fw type operation not allowed when wifi_power is off\n");
		return -EFAULT;
	}
	spin_unlock(&bes_power_ctrl->status_lock);

	if (fw_chging) {
		ret = bes_power_ctrl_can_op_switch(bes_power_ctrl, BES_POWER_CTRL_OPERATE_FW_TYPE);
		if (ret) {
			bes_err(BES_DBG_SYSFS, "%s, %d, bes_no_reboot_chg_fw_type failed\n", __func__, __LINE__);
			return ret;
		}

		spin_lock(&bes_power_ctrl->status_lock);
		/* no reboot change fw type operation not allowed after wifi_power is off */
		if (!rwnx_plat->drvdata || !bes_power_ctrl->wifi_power) {
			spin_unlock(&bes_power_ctrl->status_lock);
			bes_err(BES_DBG_SYSFS, "no reboot change fw type operation not allowed after wifi_power is off\n");

			/* restore the wait_state: BES_BOOT_WAIT_NONE */
			bes_set_boot_wait_state(BES_BOOT_WAIT_NONE);
			wake_up(&bes_power_ctrl->probe_wq_done);
			return -EFAULT;
		}

		bes_power_ctrl->no_reboot_fw_chg = fw_chging;
		spin_unlock(&bes_power_ctrl->status_lock);
	} else {
		spin_lock(&bes_power_ctrl->status_lock);
		bes_power_ctrl->no_reboot_fw_chg = fw_chging;
		spin_unlock(&bes_power_ctrl->status_lock);

		/* restore the wait_state: BES_BOOT_WAIT_NONE */
		bes_set_boot_wait_state(BES_BOOT_WAIT_NONE);
		wake_up(&bes_power_ctrl->probe_wq_done);
	}

	return 0;
}

bool bes_power_ctrl_no_reboot_fw_chg_get(struct bes_power_ctrl *bes_power_ctrl)
{
	bool fw_chging;

	spin_lock(&bes_power_ctrl->status_lock);
	fw_chging = bes_power_ctrl->no_reboot_fw_chg;
	spin_unlock(&bes_power_ctrl->status_lock);

	return fw_chging;
}
