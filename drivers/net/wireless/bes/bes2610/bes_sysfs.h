/*
 * @file bes_sysfs.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __BES_SYSFS_H
#define __BES_SYSFS_H

#include <linux/cdev.h>
#include "rwnx_defs.h"
#include "rwnx_testmode.h"
#ifdef CONFIG_BES_BT
#include "bes_btusb.h"
#endif

#define BES_SYSFS_FILE(name)       dev_attr_##name
#define BES_SYSFS_BESPHY           rwnx_besphy
#define BES_SYSFS_FW_TYPE          fw_type
#define BES_SYSFS_ONLINE_SCAN      online_scan
#define BES_SYSFS_WIFI_POWER       wifi_power
#define BES_SYSFS_BT_POWER         bt_power
#define BES_SYSFS_BT_LP            bt_lp
#ifdef CONFIG_BES_BT
#define BES_SYSFS_BTRF             rwnx_btrf
#endif
#define BES_SYSFS_DUMP_FW_LOG      fw_log_type
#ifdef CONFIG_CUSTOM_FILTER
#define BES_SYSFS_CUSTOM_FILTER    custom_filter
#endif
#if (GET_MAC_ADDR_METHOD == 3)
#define BES_SYSFS_CUSTOM_MAC_ADDR  custom_mac_addr
#endif
#define BES_SYSFS_BSS_RSSI_STABLE  bss_rssi_stable
#define BES_SYSFS_CCA_ONOFF        cca_onoff
#define BES_SYSFS_CCA_CONFIG       cca_config

#define BES_FW_TYPE_WIFI_SIGNAL    0
#define BES_FW_TYPE_WIFI_NONSIGNAL 1
#define BES_FW_TYPE_BT             2
#define BES_FW_TYPE_MAX_NUM        3

#define BES_UEVET_ACTION_PREFIX    "BES_UDEV_ACTION="
#define BES_UEVET_RECOVER          "recover"
#define BES_UEVET_CSA              "csa"

struct bes_sysfs {
	struct class *class;
	struct device *dev;
	struct rwnx_hw *rwnx_hw;
	int fw_type;
	char fw_ver[70];
	bool sig_mode;
	bool online_scan;
	struct bes_tm_common tsm_priv;
#ifdef CONFIG_BES_POWER_CTRL
	struct bes_power_ctrl *power_ctrl;
#endif /* CONFIG_BES_POWER_CTRL */
#ifdef CONFIG_BES_BT
	struct bes_btrf btrf_priv;
	void *hdev;
#endif
	u8 fw_log_type;
	u8 lmac_log_mode;
	bool user_cfg_valid;
	bool fw_cca_onoff;
	struct adaptive_cca_config cca_cfg;
#ifdef CONFIG_BES_MDEV_SUPPORT
	/* adapt to mdev */
	struct device *mdev;
	struct cdev m_cdev;
	dev_t mdev_num;
#endif
	u16 device_id;
#if (GET_MAC_ADDR_METHOD == 3)
	u8 custom_mac_addr[ETH_ALEN];
#endif
};

int bes_sysfs_register(struct rwnx_plat *rwnx_plat);
void bes_sysfs_unregister(void);
void bes_sysfs_status_update(bool reset);
bool bes_sysfs_is_signal_mode(void);
int bes_sysfs_get_fw_type(void);
void bes_sysfs_set_fw_ver(const char *fw_ver);
char *bes_sysfs_get_fw_ver(void);
void bes_sysfs_set_hw(struct rwnx_hw *rwnx_hw);
bool bes_sysfs_support_online_scan(void);
void bes_sysfs_ctrl_online_scan(bool enable);
int bes_sysfs_fw_log_type_get(void);
void bes_sysfs_restore_log_config(void);
#if (GET_MAC_ADDR_METHOD == 3)
u8 *bes_sysfs_custom_mac_addr_get(void);
#endif
struct bes_sysfs *bes_get_sysfs(void);
#ifdef CONFIG_BES_BT
void bes_sysfs_set_hdev(void *hdev);
#endif
#ifdef CONFIG_BES_MDEV_SUPPORT
int bes_mdev_register(void);
void bes_mdev_unregister(void);
#else
static inline int bes_mdev_register(void) { return 0; }
static inline void bes_mdev_unregister(void) {}
#endif

void bes_sysfs_set_device_id(u16 id);
u16 bes_sysfs_get_device_id(void);
void bes_sysfs_go_back_signal_mode(bool reset);

#endif /* __BES_SYSFS_H */
