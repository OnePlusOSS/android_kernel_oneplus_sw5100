/*
 * @file bes_sysfs.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include "bes_fw_common.h"
#include "bes_sysfs.h"
#include "rwnx_testmode.h"
#include "rwnx_utils.h"
#include "rwnx_msg_tx.h"
#include "bes_custom_filter.h"
#include "rwnx_main.h"

static struct bes_sysfs bes_sysfs = {
	.online_scan = true,
	.fw_log_type = 0,
	.fw_cca_onoff = 0,
#if (GET_MAC_ADDR_METHOD == 3)
	.custom_mac_addr = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0},
#endif
};

typedef int (*bes_sysfs_change_fw_type_cb)(struct bes_sysfs *bes_sysfs, int type);

/* get driver operation mode */
module_param_named(fw_type, bes_sysfs.fw_type, int, 0644);

static ssize_t bes_besphy_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	u16_l rp_size = bes_sysfs.tsm_priv.tm_rp.hdr.len;

	rp_size = (rp_size <= RWNX_TM_REP_SIZE) ? rp_size : RWNX_TM_REP_SIZE;
	memcpy(buf, &bes_sysfs.tsm_priv.tm_rp, rp_size);

	return rp_size;
}

static ssize_t bes_besphy_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	ssize_t ret;

	mutex_lock(&bes_sysfs.tsm_priv.tm_lock);

	ret = rwnx_testmode_cmd(bes_sysfs.rwnx_hw, buf, count, &bes_sysfs.tsm_priv.tm_rp);
	if (ret >= 0)
		ret = count;

	mutex_unlock(&bes_sysfs.tsm_priv.tm_lock);

	return ret;
}

#ifdef CONFIG_BES_BT
static ssize_t bes_btrf_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	memcpy(buf, bes_sysfs.btrf_priv.rx_buff, bes_sysfs.btrf_priv.len);
	return bes_sysfs.btrf_priv.len;
}

static ssize_t bes_btrf_store(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	ssize_t ret;

	mutex_lock(&bes_sysfs.btrf_priv.btrf_lock);

	memset(bes_sysfs.btrf_priv.rx_buff, 0, sizeof(bes_sysfs.btrf_priv.rx_buff));

	ret = bes_btusb_testmode_cmd(bes_sysfs.hdev, buf, count);
	if (ret >= 0)
		ret = count;

	mutex_unlock(&bes_sysfs.btrf_priv.btrf_lock);

	return ret;
}
#endif

static ssize_t bes_dump_fw_log_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	char *mcu_type[2] = {"uart", "ipc"};
	char *fw_type[4] = {"none","ipc_upload", "ipc_local", "uart"};
	int idx_m = 0, idx_f = 0;
	int ret;

	if (bes_sysfs.rwnx_hw) {
		ret = bes_send_get_fw_log_type_req(bes_sysfs.rwnx_hw, &bes_sysfs.fw_log_type);
		if (ret) {
			bes_err(BES_DBG_SYSFS, "get log_mode fail\n");
			return ret;
		}

		if (bes_get_wlan_enabled(bes_sysfs.rwnx_hw) &&
			(ret = bes_get_lmac_log_mode(bes_sysfs.rwnx_hw, &bes_sysfs.lmac_log_mode))) {
			bes_err(BES_DBG_SYSFS, "get log_mode fail\n");
			return ret;
		}
	}

	if (bes_sysfs.fw_log_type & FW_LOG_MCU_UPLOAD)
		idx_m = 1;

	if (bes_sysfs.lmac_log_mode == NX_PRINT_PRINTF)
		idx_f = 3;
	else if (bes_sysfs.lmac_log_mode == NX_PRINT_IPC) {
		if (bes_sysfs.fw_log_type & FW_LOG_LMAC_LOCAL)
			idx_f = 2;
		else
			idx_f = 1;
	}

	return snprintf(buf, 64, "mcu: %s, lmac: %s\n", mcu_type[idx_m], fw_type[idx_f]);
}

static ssize_t bes_dump_fw_log_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct rwnx_hw *rwnx_hw = bes_sysfs.rwnx_hw;

	if (!strncmp(buf, "none", 4)) {
		bes_sysfs.fw_log_type = 0;
		bes_sysfs.lmac_log_mode = NX_PRINT_NONE;
	} else if (!strncmp(buf, "mcu ipc", 7)) {
		bes_sysfs.fw_log_type |= FW_LOG_MCU_UPLOAD;
	} else if (!strncmp(buf, "mcu uart", 8)) {
		bes_sysfs.fw_log_type &= ~FW_LOG_MCU_UPLOAD;
	} else if (!strncmp(buf, "fw_ipc_upload open", 18)) {
		bes_sysfs.fw_log_type &= ~FW_LOG_LMAC_LOCAL;
		bes_sysfs.fw_log_type |= FW_LOG_LMAC_UPLOAD;
		bes_sysfs.lmac_log_mode = NX_PRINT_IPC;
	} else if (!strncmp(buf, "fw_ipc_upload close", 19)) {
		bes_sysfs.fw_log_type &= ~FW_LOG_LMAC_UPLOAD;
		bes_sysfs.lmac_log_mode = NX_PRINT_NONE;
	} else if (!strncmp(buf, "fw_ipc_local open", 17)) {
		bes_sysfs.fw_log_type &= ~FW_LOG_LMAC_UPLOAD;
		bes_sysfs.fw_log_type |= FW_LOG_LMAC_LOCAL;
		bes_sysfs.lmac_log_mode = NX_PRINT_IPC;
	} else if (!strncmp(buf, "fw_ipc_local close", 18)) {
		bes_sysfs.fw_log_type &= ~FW_LOG_LMAC_LOCAL;
		bes_sysfs.lmac_log_mode = NX_PRINT_NONE;
	} else if (!strncmp(buf, "fw_uart open", 12)) {
		bes_sysfs.fw_log_type &= ~FW_LOG_LMAC_UPLOAD;
		bes_sysfs.lmac_log_mode = NX_PRINT_PRINTF;
	} else if (!strncmp(buf, "fw_uart close", 13)) {
		bes_sysfs.lmac_log_mode = NX_PRINT_NONE;
	} else if (!strncmp(buf, "tp_dbg", 6)) {
		bes_set_dbg_lvl(BES_DBG_TP_DBG,BES_LOG_DBG);
	} else if (!strncmp(buf, "tp_info", 7)) {
		bes_set_dbg_lvl(BES_DBG_TP_DBG,BES_LOG_INFO);
	} else {
		bes_err(BES_DBG_SYSFS, "invalid param\n");
		return -EINVAL;
	}

	/* Mark current log configuration setted by user */
	bes_sysfs.user_cfg_valid = true;

	if (rwnx_hw) {
		if (rwnx_hw->mod_params && rwnx_hw->mod_params->auto_lmac_log_ctrl) {
			bes_info(BES_DBG_SYSFS, "%s, auto_lmac_log_ctrl is enable\n", __func__);
		} else {
			bes_set_lmac_log_mode(bes_sysfs.rwnx_hw, bes_sysfs.lmac_log_mode);
			bes_send_dump_fw_log(bes_sysfs.rwnx_hw, bes_sysfs.fw_log_type);
		}
	}

	bes_info(BES_DBG_SYSFS, "set bes_sysfs.fw_log_type:%d, lmac_log_mode:%d\n",
	         bes_sysfs.fw_log_type, bes_sysfs.lmac_log_mode);

	return count;
}

int bes_sysfs_fw_log_type_get(void)
{
	return bes_sysfs.fw_log_type;
}

void bes_sysfs_restore_log_config(void)
{
	struct rwnx_hw* rwnx_hw = bes_sysfs.rwnx_hw;

	/* If auto_lmac_log_ctrl mode is enabled, set it to resume mode first */
	if (rwnx_hw->mod_params && rwnx_hw->mod_params->auto_lmac_log_ctrl) {
		bes_auto_set_fw_log(rwnx_hw, AUTO_LMAC_LOG_RESUME_STATUS);
		return;
	}

	/* Check if current config is configured by user */
	if (!bes_sysfs.user_cfg_valid)
		return;

	/* Sync lmac log mode */
	bes_set_lmac_log_mode(rwnx_hw, bes_sysfs.lmac_log_mode);

	/* Sync fw log type mode */
	bes_send_dump_fw_log(rwnx_hw, bes_sysfs.fw_log_type);
}

#if !defined(CONFIG_BES_POWER_CTRL) && defined(CONFIG_BES_USB)
static int bes_reboot_mcu_change_fw_type(struct bes_sysfs *bes_sysfs, int type)
{
	int ret = 0;

#ifndef CONFIG_BES_BT
	if (type >= BES_FW_TYPE_BT)
		return -EPERM;
#endif

	if (bes_sysfs_get_fw_type() == BES_FW_TYPE_BT) {
#ifdef CONFIG_BES_BT
		char buff_send[6] = {0xFF, BTRF_REBOOT_MCU, 0xDE, 0xAD, 0xBE, 0xEF};
		ret = bes_btusb_testmode_cmd(bes_sysfs->hdev, buff_send, sizeof(buff_send));
		if (ret > 0)
			ret = 0;
#endif
	} else {
		int status = -1;
		if (!bes_sysfs->rwnx_hw)
			return -EFAULT;
		ret = bes_send_drv2mcu_msg(bes_sysfs->rwnx_hw, NULL, 0, MCU_FW_REBOOT_REQ,
					   MCU_FW_REBOOT_CFM, &status, sizeof(status));
	}

	if (!ret)
		bes_sysfs->fw_type = type;

#ifndef CONFIG_BES_TXLOOP
	/* wifi & bt stop receiving data. */
	if (bes_sysfs->rwnx_hw)
		bes_sysfs->rwnx_hw->cmd_mgr.state = RWNX_CMD_MGR_STATE_CRASHED; // avoid cmd timeout
#endif
#ifdef CONFIG_BES_BT
	if (bes_sysfs->hdev)
		bes_btusb_clear_hci_running(bes_sysfs->hdev);
	bes_sysfs_set_hdev(NULL);
#endif

	bes_sysfs_set_hw(NULL);

	return ret;
}
#endif

#if defined(CONFIG_RWNX_PCIE) || defined(CONFIG_BES_IBUS)
static int bes_mcu_change_fw_type(struct bes_sysfs *bes_sysfs, int type)
{
	int ret = 0;
	int status = -1;

	if (type > BES_FW_TYPE_BT)
		return -EPERM;

	if (!bes_sysfs->rwnx_hw)
		return -EFAULT;
	ret = bes_send_drv2mcu_msg(bes_sysfs->rwnx_hw, (u8 *)&type, sizeof(int), MCU_FW_CHANGE_MODE_REQ,
	                           MCU_FW_CHANGE_MODE_CFM, &status, sizeof(status));

	if (!ret)
		bes_sysfs->fw_type = type;
	bes_sysfs_status_update(false);
	rwnx_wf_status_set(bes_sysfs->rwnx_hw, RWNX_DEV_STARTED);
	rwnx_send_reset(bes_sysfs->rwnx_hw);
	return ret;
}
#endif

#if defined(BES_NO_REBOOT_CHG_FW)
static int bes_no_reboot_chg_fw_type(struct bes_sysfs *bes_sysfs, int type)
{
	int ret = 0;
	int status = -1;
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_power_state power_state;
	struct bes_power_ctrl *bes_power_ctrl = priv_to_power_ctrl(rwnx_plat->power_priv);

	if (type > BES_FW_TYPE_BT)
		return -EPERM;

	if (type == bes_sysfs->fw_type)
		return 0;

	if (!bes_sysfs->rwnx_hw)
		return -EFAULT;

	ret = bes_power_ctrl_no_reboot_fw_chg_set(bes_power_ctrl, true);
	if (ret)
		return ret;

	ret = bes_send_drv2mcu_msg(bes_sysfs->rwnx_hw, (u8 *)&type, sizeof(int), MCU_FW_CHANGE_MODE_REQ,
                                   MCU_FW_CHANGE_MODE_CFM, &status, sizeof(status));

	if (!ret)
		bes_sysfs->fw_type = type;

	if (bes_sysfs_get_fw_type() < BES_FW_TYPE_BT &&
	    rwnx_plat->drvdata) {
		bes_power_ctrl_cancel_wlan_close_work(bes_sysfs->rwnx_hw);
		rwnx_unregister_net_dev(rwnx_plat);
	}

	bes_sysfs_status_update(false);

	bes_power_ctrl_get_power_state(&power_state);

	if (power_state.wifi_power) {
		if ((bes_sysfs_get_fw_type() < BES_FW_TYPE_BT) &&
		   (ret = rwnx_register_net_dev(rwnx_plat)))
			goto err;

		if ((ret = bes_power_ctrl_system_enabled(true, false)) < 0)
			goto err;
	} else {
		if (bes_power_ctrl_check_system_close(bes_power_ctrl))
			if ((ret = bes_power_ctrl_do_system_close()) < 0)
				goto err;;
	}

	bes_power_ctrl_no_reboot_fw_chg_set(bes_power_ctrl, false);
	return 0;

err:
	bes_power_ctrl_system_enabled(false, true);
	bes_power_ctrl_no_reboot_fw_chg_set(bes_power_ctrl, false);
	return ret;
}
#endif

static ssize_t bes_fw_type_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 15, "fw_type: %d\n", bes_sysfs.fw_type);
}

static ssize_t bes_fw_type_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	ssize_t ret;
	bes_sysfs_change_fw_type_cb change_fw_type_cb = NULL;

#if defined(BES_NO_REBOOT_CHG_FW)
	change_fw_type_cb = bes_no_reboot_chg_fw_type;
#elif defined(CONFIG_RWNX_PCIE) || defined(CONFIG_BES_IBUS)
	change_fw_type_cb = bes_mcu_change_fw_type;
#elif defined CONFIG_BES_POWER_CTRL
	change_fw_type_cb = bes_power_ctrl_change_fw_type;
#elif defined(CONFIG_BES_USB)
	change_fw_type_cb = bes_reboot_mcu_change_fw_type;
#else
	return -EPERM;
#endif

	if (!strncmp(buf, "0", 1))
		ret = change_fw_type_cb(&bes_sysfs, BES_FW_TYPE_WIFI_SIGNAL);
	else if (!strncmp(buf, "1", 1))
		ret = change_fw_type_cb(&bes_sysfs, BES_FW_TYPE_WIFI_NONSIGNAL);
	else if (!strncmp(buf, "2", 1))
		ret = change_fw_type_cb(&bes_sysfs, BES_FW_TYPE_BT);
	else
		ret = -EPERM;

	if (!ret)
		ret = count;

	return ret;
}

static ssize_t bes_online_scan_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 64, "online_scan:%d\n", bes_sysfs.online_scan);
}

static ssize_t bes_online_scan_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	bes_sysfs.online_scan = (buf[0] != '0') ? true : false;
	bes_info(BES_DBG_SYSFS, "set bes_sysfs.online_scan %d\n", bes_sysfs.online_scan);
	return count;
}

#if (GET_MAC_ADDR_METHOD == 3)
static ssize_t bes_custom_mac_addr_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 64, "custom_mac_addr: %pM\n", bes_sysfs.custom_mac_addr);
}

static ssize_t bes_custom_mac_addr_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	u8 custom_mac[ETH_ALEN] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0};

	if (sscanf(buf, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &custom_mac[0],
		   &custom_mac[1], &custom_mac[2], &custom_mac[3], &custom_mac[4],
		   &custom_mac[5]) != ETH_ALEN || !is_valid_ether_addr(custom_mac)) {
		bes_err(BES_DBG_SYSFS, "The custom mac addr is incorrect for wlan0\n");
		ret = -EPERM;
	} else {
		bes_info(BES_DBG_SYSFS, "set custom_mac_addr: %pM\n", custom_mac);
		memcpy(bes_sysfs.custom_mac_addr, custom_mac, ETH_ALEN);
		ret = count;
	}

	return ret;
}
#endif

#ifdef CONFIG_BES_POWER_CTRL

#ifndef CONFIG_RWNX_PCIE
static ssize_t bes_sysfs_wifi_power_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	ssize_t ret;

	if (!bes_sysfs.power_ctrl)
		return -EFAULT;

	if (!strncmp(buf, "on", 2))
		ret = bes_power_ctrl_wifi_power(bes_sysfs.power_ctrl, true);
	else if (!strncmp(buf, "off", 3))
		ret = bes_power_ctrl_wifi_power(bes_sysfs.power_ctrl, false);
	else
		ret = -EPERM;

	if (!ret)
		ret = count;

	return ret;
}

static ssize_t bes_sysfs_wifi_power_show(struct device *dev,
					 struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 64, "wifi_power: %s\n", bes_sysfs.power_ctrl->wifi_power ? "on" : "off");
}
#endif /* CONFIG_RWNX_PCIE */

#if (CONFIG_BES_POWER_CTRL_BT_SELF_MANAGED == 0)
static ssize_t bes_sysfs_bt_power_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	ssize_t ret;

	/* only work for wifi signal mode */
	if (bes_sysfs.fw_type != BES_FW_TYPE_WIFI_SIGNAL)
		return -EFAULT;

	if (!bes_sysfs.power_ctrl)
		return -EFAULT;

	if (!strncmp(buf, "on", 2))
		ret = bes_power_ctrl_bt_power(bes_sysfs.power_ctrl, true);
	else if (!strncmp(buf, "off", 3))
		ret = bes_power_ctrl_bt_power(bes_sysfs.power_ctrl, false);
	else
		ret = -EPERM;

	if (!ret)
		ret = count;

	return ret;
}

static ssize_t bes_sysfs_bt_power_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 64, "bt_power: %s\n", bes_sysfs.power_ctrl->bt_power ? "on" : "off");
}

static ssize_t bes_sysfs_bt_lp_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	ssize_t ret;

	if (!bes_sysfs.power_ctrl)
		return -EFAULT;

	if (!strncmp(buf, "wakeup", 6))
		ret = bes_power_ctrl_bt_lp(bes_sysfs.power_ctrl, false);
	else if (!strncmp(buf, "sleep", 5))
		ret = bes_power_ctrl_bt_lp(bes_sysfs.power_ctrl, true);
	else
		ret = -EPERM;

	if (!ret)
		ret = count;

	return ret;
}
#endif /* BES_POWER_CTRL_BT_SELF_MANAGED */
#endif /* CONFIG_BES_POWER_CTRL */

#ifdef CONFIG_CUSTOM_FILTER
static ssize_t bes_sysfs_custom_filter_show(struct device *dev,
					    struct device_attribute *attr, char *buf)
{
	struct rwnx_hw *rwnx_hw = bes_sysfs.rwnx_hw;

	return bes_custom_filter_sysfs_show(rwnx_hw, buf);
}

static ssize_t bes_sysfs_custom_filter_store(struct device *dev,
					     struct device_attribute *attr,
					     const char *buf, size_t count)
{
	ssize_t ret = 0;
	int len = 0;
	struct rwnx_hw *rwnx_hw = bes_sysfs.rwnx_hw;
	char buf_copy[100 + 1];

	/* Check and strcpy buf */
	if (!buf || (len = strlen(buf)) <= 0 || len > 100 || buf[len] != '\0' ||
		!strcpy(buf_copy, buf)) {
		bes_err(BES_DBG_SYSFS, "fail to strcpy buf\n");
		return count;
	}

	/* Check buf_copy that wheteher the ending element is '\n' */
	if (buf_copy[len - 1] == '\n') {
		buf_copy[len - 1] = '\0';
	}

	/* Command ctrl */
	ret = bes_custom_filter_sysfs_ctrl(rwnx_hw, buf_copy);
	if (!ret)
		ret = count;

	return ret;
}
#endif /* CONFIG_CUSTOM_FILTER */

static ssize_t bes_bss_rssi_stable_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct rwnx_hw *rwnx_hw = bes_sysfs.rwnx_hw;
	int bss_rssi_stable = rwnx_bss_rssi_stable_get(rwnx_hw);

	return snprintf(buf, 64, "bss_rssi_stable: %d\n", bss_rssi_stable);
}

static ssize_t bes_bss_rssi_stable_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct rwnx_hw *rwnx_hw = bes_sysfs.rwnx_hw;

	if (buf[0] == '0') {
		rwnx_bss_rssi_stable_set(rwnx_hw, 0);
	} else if (buf[0] == '1') {
		rwnx_bss_rssi_stable_set(rwnx_hw, 1);
	} else {
		bes_err(BES_DBG_SYSFS, "unexpected parameter for bss_rssi_stable\n");
	}

	bes_info(BES_DBG_SYSFS, "set bss_rssi_stable %d\n", rwnx_bss_rssi_stable_get(rwnx_hw));
	return count;
}

static ssize_t bes_cca_onoff_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return snprintf(buf, 64, "fw_cca_onoff: %d\n", bes_sysfs.fw_cca_onoff);
}

static ssize_t bes_cca_onoff_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct rwnx_hw *rwnx_hw = bes_sysfs.rwnx_hw;

	if (!strncmp(buf, "0", 1)) {
		//disable cca
		rwnx_send_cca_enable(rwnx_hw, 0);
		bes_sysfs.fw_cca_onoff = false;
	} else if (!strncmp(buf, "1", 1)) {
		//enable cca
		rwnx_send_cca_enable(rwnx_hw, 1);
		bes_sysfs.fw_cca_onoff = true;
	} else {
		bes_err(BES_DBG_SYSFS, "unexpected parameter for cca onoff:eg: echo 0/1 > cca_onoff disable/enable cca\n");
	}

	bes_info(BES_DBG_SYSFS, "set cca onoff %d\n",bes_sysfs.fw_cca_onoff);
	return count;
}

static ssize_t bes_cca_config_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct adaptive_cca_config* cca_config = &bes_sysfs.cca_cfg;

	return snprintf(buf, 256, "test_mode_cca_eng:  %d\n noise_thd_low: %d\n noise_thd_high: %d\n enter_test_mode_thd: %d\n exit_test_mode_thd: %d\n rx_abort_cnt_limit: %d\n",
					 cca_config->test_mode_cca_eng, cca_config->noise_thd_low, cca_config->noise_thd_high, cca_config->enter_test_mode_thd,
					 cca_config->exit_test_mode_thd,cca_config->rx_abort_cnt_limit);
}

/*
	echo "cca_eng noise_low_thd noise_hig_thd enter_thd exit_thd rx_abort_cnt" > cca_config
	eg: echo  "188 184 192 3 6 20 "  > cca_config
*/
static ssize_t bes_cca_config_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	int argc = 0;
	char *argv[6] = {NULL};
	struct rwnx_hw *rwnx_hw = bes_sysfs.rwnx_hw;

	argc = bes_common_parse_line((char *)buf, argv, 6);
	if (argc <= 0) {
		bes_err(BES_DBG_SYSFS, "%s ERROR: fail to parse line. argc:%d\n", __func__, argc);
		return -EINVAL;
	}
	bes_sysfs.cca_cfg.test_mode_cca_eng = (uint8_t)simple_strtol(argv[0], NULL, 10);
	bes_sysfs.cca_cfg.noise_thd_low = (uint8_t)simple_strtol(argv[1], NULL, 10);
	bes_sysfs.cca_cfg.noise_thd_high = (uint8_t)simple_strtol(argv[2], NULL, 10);
	bes_sysfs.cca_cfg.enter_test_mode_thd = (uint32_t)simple_strtol(argv[3], NULL, 10);
	bes_sysfs.cca_cfg.exit_test_mode_thd = (uint32_t)simple_strtol(argv[4], NULL, 10);
	bes_sysfs.cca_cfg.rx_abort_cnt_limit = (uint32_t)simple_strtol(argv[5], NULL, 10);

	rwnx_send_cca_config(rwnx_hw, &bes_sysfs.cca_cfg);

	return count;
}


/* define group file attr, one or more */
/* besphy */
static DEVICE_ATTR(BES_SYSFS_BESPHY, 0644, bes_besphy_show, bes_besphy_store);
/* online scan */
static DEVICE_ATTR(BES_SYSFS_ONLINE_SCAN, 0644, bes_online_scan_show, bes_online_scan_store);
/* FW type */
static DEVICE_ATTR(BES_SYSFS_FW_TYPE, 0644, bes_fw_type_show, bes_fw_type_store);
#ifdef CONFIG_BES_BT
/* btrf */
static DEVICE_ATTR(BES_SYSFS_BTRF, 0644, bes_btrf_show, bes_btrf_store);
#endif
#ifdef CONFIG_BES_POWER_CTRL
/* WIFI_SWITCH */
#ifndef CONFIG_RWNX_PCIE
static DEVICE_ATTR(BES_SYSFS_WIFI_POWER, 0664, bes_sysfs_wifi_power_show,
					bes_sysfs_wifi_power_store);
#endif
#if (CONFIG_BES_POWER_CTRL_BT_SELF_MANAGED == 0)
/* BT_SWITCH */
static DEVICE_ATTR(BES_SYSFS_BT_POWER, 0664, bes_sysfs_bt_power_show, bes_sysfs_bt_power_store);
/* BT_LP */
static DEVICE_ATTR(BES_SYSFS_BT_LP, 0664, NULL, bes_sysfs_bt_lp_store);
#endif
#endif /* CONFIG_BES_POWER_CTRL */
/* fw_log */
static DEVICE_ATTR(BES_SYSFS_DUMP_FW_LOG, 0644, bes_dump_fw_log_show, bes_dump_fw_log_store);
#ifdef CONFIG_CUSTOM_FILTER
/* WOWLAN_FILTER */
static DEVICE_ATTR(BES_SYSFS_CUSTOM_FILTER, 0664, bes_sysfs_custom_filter_show,
		   bes_sysfs_custom_filter_store);
#endif
#if (GET_MAC_ADDR_METHOD == 3)
/* custom mac addr */
static DEVICE_ATTR(BES_SYSFS_CUSTOM_MAC_ADDR, 0644, bes_custom_mac_addr_show, bes_custom_mac_addr_store);
#endif
/* bss rssi stable */
static DEVICE_ATTR(BES_SYSFS_BSS_RSSI_STABLE, 0644, bes_bss_rssi_stable_show, bes_bss_rssi_stable_store);

/*cca onoff */
static DEVICE_ATTR(BES_SYSFS_CCA_ONOFF, 0644, bes_cca_onoff_show, bes_cca_onoff_store);

/*cca config */
static DEVICE_ATTR(BES_SYSFS_CCA_CONFIG, 0644, bes_cca_config_show, bes_cca_config_store);

/* member synchronizes with DEVICE_ATTR */
static struct attribute *bes_sysfs_attr[] = {
	&BES_SYSFS_FILE(BES_SYSFS_BESPHY).attr,
	&BES_SYSFS_FILE(BES_SYSFS_FW_TYPE).attr,
	&BES_SYSFS_FILE(BES_SYSFS_ONLINE_SCAN).attr,
#ifdef CONFIG_BES_POWER_CTRL
#ifndef CONFIG_RWNX_PCIE
	&BES_SYSFS_FILE(BES_SYSFS_WIFI_POWER).attr,
#endif
#if (CONFIG_BES_POWER_CTRL_BT_SELF_MANAGED == 0)
	&BES_SYSFS_FILE(BES_SYSFS_BT_POWER).attr,
	&BES_SYSFS_FILE(BES_SYSFS_BT_LP).attr,
#endif
#endif
#ifdef CONFIG_BES_BT
	&BES_SYSFS_FILE(BES_SYSFS_BTRF).attr,
#endif
	&BES_SYSFS_FILE(BES_SYSFS_DUMP_FW_LOG).attr,
#ifdef CONFIG_CUSTOM_FILTER
	&BES_SYSFS_FILE(BES_SYSFS_CUSTOM_FILTER).attr,
#endif
#if (GET_MAC_ADDR_METHOD == 3)
	&BES_SYSFS_FILE(BES_SYSFS_CUSTOM_MAC_ADDR).attr,
#endif
	&BES_SYSFS_FILE(BES_SYSFS_BSS_RSSI_STABLE).attr,
	&BES_SYSFS_FILE(BES_SYSFS_CCA_ONOFF).attr,
	&BES_SYSFS_FILE(BES_SYSFS_CCA_CONFIG).attr,
	NULL, /* NULL keep last */
};

static const struct attribute_group bes_sysfs_attr_group = {
	.attrs = bes_sysfs_attr,
};

void bes_sysfs_status_update(bool reset)
{
	if (bes_sysfs.fw_type >= BES_FW_TYPE_MAX_NUM) {
		bes_sysfs.fw_type = BES_FW_TYPE_WIFI_SIGNAL;
		bes_err(BES_DBG_SYSFS, "unexpected fw type, switch to wifi signal mode\n");
	}

	if (bes_sysfs.fw_type == BES_FW_TYPE_WIFI_SIGNAL) {
		bes_sysfs.sig_mode = true;
	} else if ((bes_sysfs.fw_type == BES_FW_TYPE_WIFI_NONSIGNAL) ||
		   (bes_sysfs.fw_type == BES_FW_TYPE_BT)) {
		bes_sysfs.sig_mode = false;
	}

	if (reset)
		bes_power_ctrl_state_reset();
}

bool bes_sysfs_support_online_scan(void)
{
	return bes_sysfs.online_scan;
}

void bes_sysfs_ctrl_online_scan(bool enable)
{
	bes_sysfs.online_scan = enable;
}

#if (GET_MAC_ADDR_METHOD == 3)
u8 *bes_sysfs_custom_mac_addr_get(void)
{
	return bes_sysfs.custom_mac_addr;
}
#endif

bool bes_sysfs_is_signal_mode(void)
{
	return bes_sysfs.sig_mode;
}

int bes_sysfs_get_fw_type(void)
{
	return bes_sysfs.fw_type;
}

void bes_sysfs_set_fw_ver(const char *fw_ver_ptr)
{
	int fw_type = bes_sysfs_get_fw_type();
	int fw_ver_len = strlen(fw_ver_ptr);
	int max_fw_ver_len = sizeof(bes_sysfs.fw_ver);

	if (fw_type == BES_FW_TYPE_WIFI_SIGNAL) {
		if(fw_ver_len < max_fw_ver_len) {
			strncpy(bes_sysfs.fw_ver, fw_ver_ptr, fw_ver_len);
			bes_sysfs.fw_ver[fw_ver_len] = '\0';
		} else {
			bes_err(BES_DBG_SYSFS, "unexpected fw_ver length : %d\n", fw_ver_len);
			strncpy(bes_sysfs.fw_ver, fw_ver_ptr, max_fw_ver_len - 1);
			bes_sysfs.fw_ver[max_fw_ver_len - 1] = '\0';
		}
	}
}

char *bes_sysfs_get_fw_ver(void)
{
	return (char *)bes_sysfs.fw_ver;
}

void bes_sysfs_set_hw(struct rwnx_hw *rwnx_hw)
{
	bes_sysfs.rwnx_hw = rwnx_hw;
}

struct bes_sysfs *bes_get_sysfs(void)
{
	return &bes_sysfs;
}

void bes_sysfs_go_back_signal_mode(bool reset)
{
	bes_sysfs.fw_type = BES_FW_TYPE_WIFI_SIGNAL;
	bes_sysfs_status_update(false);
}

#ifdef CONFIG_BES_BT
void bes_sysfs_set_hdev(void *hdev)
{
	bes_sysfs.hdev = hdev;
}
#endif

#ifdef CONFIG_BES_MDEV_SUPPORT
static int bes_mdev_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int bes_mdev_release(struct inode *inode, struct file *file)
{
	return 0;
}

static struct file_operations bes_mdev_fops = {
	.owner = THIS_MODULE,
	.open = bes_mdev_open,
	.release = bes_mdev_release,
};

int bes_mdev_register(void)
{
	int ret = 0;

	if (!!bes_sysfs.mdev)
		return 0;

	/* System assigned main device number */
	bes_sysfs.mdev_num = MKDEV(0, 0);
	ret = alloc_chrdev_region(&bes_sysfs.mdev_num, 0, 1, "char_mdev");
	if (ret) {
		bes_err(BES_DBG_SYSFS, "rnwx mdev device number application failed\n");
		goto err;
	}

	cdev_init(&bes_sysfs.m_cdev, &bes_mdev_fops);
	ret = cdev_add(&bes_sysfs.m_cdev, bes_sysfs.mdev_num, 1);
	if (ret < 0){
		bes_err(BES_DBG_SYSFS, "bes mdev char device add fail\n");
		goto err1;
	}

	bes_sysfs.mdev = device_create(bes_sysfs.class, NULL, bes_sysfs.mdev_num, NULL, "bes_mdev");
	if (IS_ERR(bes_sysfs.mdev)) {
		bes_err(BES_DBG_SYSFS, "rnwx sysfs mdev create fail\n");
		ret = -EFAULT;
		goto err2;
	}

	return 0;

err2:
	cdev_del(&bes_sysfs.m_cdev);
err1:
	unregister_chrdev_region(bes_sysfs.mdev_num, 1);
	bes_sysfs.mdev_num = 0;
err:
	bes_sysfs.mdev = NULL;
	return ret;
}

void bes_mdev_unregister(void)
{
	if (bes_sysfs.mdev) {
		cdev_del(&bes_sysfs.m_cdev);
		unregister_chrdev_region(bes_sysfs.mdev_num, 1);
		device_destroy(bes_sysfs.class, bes_sysfs.mdev_num);
		bes_sysfs.mdev_num = 0;
		bes_sysfs.mdev = NULL;
	}
}
#endif

void bes_sysfs_set_device_id(u16 id)
{
	bes_sysfs.device_id = id;
}

u16 bes_sysfs_get_device_id(void)
{
	return bes_sysfs.device_id;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0)
static int bes_dev_uevent(const struct device *dev, struct kobj_uevent_env *env)
#else
static int bes_dev_uevent(struct device *dev, struct kobj_uevent_env *env)
#endif
{
	return add_uevent_var(env, "MODALIAS=%s", "platform:bes_udev");
}

static const struct device_type bes_udev_type = {
	.uevent = bes_dev_uevent,
};

int bes_sysfs_register(struct rwnx_plat *rwnx_plat)
{
	int ret;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
	bes_sysfs.class = class_create("rwnx_dev");
#else
	bes_sysfs.class = class_create(THIS_MODULE, "rwnx_dev");
#endif
	if (IS_ERR(bes_sysfs.class)) {
		bes_err(BES_DBG_SYSFS, "bes sysfs class create fail\n");
		ret = -EFAULT;
		goto err;
	}

	ret = bes_mdev_register();
	if (ret)
		goto err1;

	bes_sysfs.dev = device_create(bes_sysfs.class, NULL, 0, NULL, "rwnx");
	if (IS_ERR(bes_sysfs.dev)) {
		bes_err(BES_DBG_SYSFS, "bes sysfs device create fail\n");
		ret = -EFAULT;
		goto err2;
	}

	ret = sysfs_create_group(&(bes_sysfs.dev->kobj), &bes_sysfs_attr_group);
	if (ret) {
		bes_err(BES_DBG_SYSFS, "bes sysfs object create fail\n");
		ret = -EFAULT;
		goto err3;
	}

	/* Set default value */
	bes_sysfs.dev->type = &bes_udev_type;
	bes_sysfs.user_cfg_valid = false;

	/* besphy only support order write */
	mutex_init(&bes_sysfs.tsm_priv.tm_lock);
#ifdef CONFIG_BES_BT
	mutex_init(&bes_sysfs.btrf_priv.btrf_lock);
	init_waitqueue_head(&bes_sysfs.btrf_priv.btrf_rx_wq);
#endif

#ifdef CONFIG_BES_POWER_CTRL
	bes_sysfs.power_ctrl = priv_to_power_ctrl(rwnx_plat->power_priv);
#endif

	bes_dbg(BES_DBG_SYSFS, "%s done\n", __func__);

	return 0;

err3:
	device_destroy(bes_sysfs.class, 0);
err2:
	bes_mdev_unregister();
err1:
	class_destroy(bes_sysfs.class);
err:
	return ret;
}

void bes_sysfs_unregister(void)
{
	bes_mdev_unregister();
	sysfs_remove_group(&(bes_sysfs.dev->kobj), &bes_sysfs_attr_group);
	device_destroy(bes_sysfs.class, 0);
	class_destroy(bes_sysfs.class);

	mutex_destroy(&bes_sysfs.tsm_priv.tm_lock);
#ifdef CONFIG_BES_BT
	mutex_destroy(&bes_sysfs.btrf_priv.btrf_lock);
#endif

#ifdef CONFIG_BES_POWER_CTRL
	bes_sysfs.power_ctrl = NULL;
#endif
	bes_dbg(BES_DBG_SYSFS, "%s done\n", __func__);
}
