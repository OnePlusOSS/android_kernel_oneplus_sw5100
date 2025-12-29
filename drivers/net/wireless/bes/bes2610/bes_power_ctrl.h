/*
 * @file bes_power_ctrl.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __BES_POWER_CTRL_H
#define __BES_POWER_CTRL_H

typedef int (*plat_power_cb_t)(struct rwnx_plat *rwnx_plat, bool on);

#include "bes_power_ctrl_gpio.h"
#include "bes_power_ctrl_msg.h"
#include "ipc_shared.h"

/* wifi/bt power switch API, by gpio or message method */
enum wait_state {
	BES_BOOT_WAIT_NONE = 0,
	BES_BOOT_WAIT_DOWNLOAD_FW,
	BES_BOOT_WAIT_PROBE_DONE,
	BES_BOOT_WAIT_PROBE_CLOSE,
	BES_BOOT_WAIT_POWER_OPERATE_DONE,
};

enum bus_probe_state {
	BES_BUS_PROBE_NONE = 0,
	BES_BUS_PROBE_START,
	BES_BUS_PROBE_OK,
	BES_BUS_PROBE_TIMEOUT,
	BES_BUS_PROBE_ERROR,
};

enum bes_power_ctrl_operate_type {
	BES_POWER_CTRL_OPERATE_WIFI,
	BES_POWER_CTRL_OPERATE_BT,
	BES_POWER_CTRL_OPERATE_BT_LP,
	BES_POWER_CTRL_OPERATE_FW_TYPE,
	BES_POWER_CTRL_OPERATE_RMMOD,
};

enum bes_power_ctrl_rmmod_state {
	BES_POWER_CTRL_RMMOD_STATE_NONE = 0,
	BES_POWER_CTRL_RMMOD_STATE_PREP,
	BES_POWER_CTRL_RMMOD_STATE_IN,
};

/**
 * enum bes_power_ctrl_attr - power control attribute
 *
 * attribute of allow device shutdown, or more
 */
enum bes_power_ctrl_attr {
	BES_POWER_CTRL_ATTR_DEV_SHUTDOWN = 0,
	BES_POWER_CTRL_ATTR_BT_SELF_MANAGED = 1,
};

/**
 * struct bes_wlan_power_ctrl - BES wlan switch data
 *
 * @power_status: wlan swtich status
 * @wlan_close_work: the work for wlan close
 * @power_mutex: the mutex of switch wlan
 */
struct bes_wlan_power_ctrl {
	bool power_status;
	struct delayed_work wlan_close_work;
	struct mutex power_mutex;
};

struct bes_power_state {
	bool wifi_power;
	bool bt_power;
};

#if defined(CONFIG_RWNX_PCIE) || defined(CONFIG_BES_IBUS)
#define MCU_REQ_DPD_DATA_MAX_SIZE     ((IPC_A2E_MSG_BUF_SIZE * 4) - sizeof(struct lmac_msg))
#define MCU_RECOVER_DPD_DATA_MAX_SIZE ((IPC_A2E_MSG_BUF_SIZE * 4) - sizeof(struct mcu_recover_dpd) - sizeof(struct lmac_msg))
#else
#define MCU_REQ_DPD_DATA_MAX_SIZE     (IPC_A2E_MSG_BUF_SIZE * 4)
#define MCU_RECOVER_DPD_DATA_MAX_SIZE ((IPC_A2E_MSG_BUF_SIZE * 4) - sizeof(struct mcu_recover_dpd) - sizeof(struct lmac_msg))
#endif

#if defined(CONFIG_BES_MSPI)
#define BES_BUS_PROBE_TIMEOUT_JIFFIES (20 * HZ)
#else
#define BES_BUS_PROBE_TIMEOUT_JIFFIES (2 * HZ)
#endif

struct bes_power_ctrl_ops {
	int (*chip_power)(struct bes_power_ctrl *bes_power_ctrl, bool on);
	int (*wakeup_wifi)(struct bes_power_ctrl *bes_power_ctrl, bool wakeup);
	int (*bt_power)(struct bes_power_ctrl *bes_power_ctrl, bool on);
	int (*wakeup_bt)(struct bes_power_ctrl *bes_power_ctrl, bool wakeup);
	int (*plat_power)(struct rwnx_plat *rwnx_plat, bool on);
	int (*chg_fw_type)(struct bes_power_ctrl *bes_power_ctrl, int type);
	void (*get_attr)(struct bes_power_ctrl *bes_power_ctrl, enum bes_power_ctrl_attr attr, void *val);
};

struct bes_power_ctrl {
	bool wifi_power;
	bool wifi_pwr_change;
	bool bt_power;
	bool bt_pwr_change;
	/* ensure that the firmware download is successful before executing bt_on */
	bool bton_pending;
	bool system_enabled;
	bool bus_error;
	wait_queue_head_t probe_wq_done;
	struct delayed_work probe_timeout_work;
	enum bus_probe_state bus_probe;
	struct bes_sysfs *bes_sysfs;
	spinlock_t status_lock;
	enum wait_state wait_state; /* wait boot status */
	struct rwnx_plat *plat;
	struct mutex probe_mutex; /* The power switch of wifi and bt are mutually exclusive */
	const struct bes_power_ctrl_ops *ops;
	struct mutex io_mutex;
	enum bes_power_ctrl_rmmod_state rmmod_stat;
	unsigned long start_scan_time;
	bool wait_scan;
	struct timer_list scan_clean;
	bool is_chg_fw_type;
	bool no_reboot_fw_chg;
	struct wakeup_source *ws;
	bool stay_pm;

	/* must be placed last */
	char priv[0] __aligned(sizeof(void *));
};

#ifdef CONFIG_BES_POWER_CTRL

struct bes_bus_power_ctrl {
	struct bes_power_ctrl *power_ctrl;
};

static inline struct bes_power_ctrl *priv_to_power_ctrl(void *priv)
{
	BUG_ON(!priv);
	return ((struct bes_bus_power_ctrl *)priv)->power_ctrl;
}

static inline void *power_ctrl_priv(struct bes_power_ctrl *bes_power_ctrl)
{
	BUG_ON(!bes_power_ctrl);
	return &bes_power_ctrl->priv;
}
#endif

static inline struct bes_power_ctrl *bes_get_power_ctrl(struct rwnx_plat *rwnx_plat)
{
#ifdef CONFIG_BES_POWER_CTRL
	return priv_to_power_ctrl(rwnx_plat->power_priv);
#else
	return NULL;
#endif
}

#ifdef CONFIG_BES_POWER_CTRL

/* wlan netdev up/down API */
int bes_power_ctrl_wlan_open(struct rwnx_hw *rwnx_hw);
int bes_power_ctrl_wlan_close(struct rwnx_hw *rwnx_hw);
void bes_power_ctrl_set_system_closed(struct bes_power_ctrl *bes_power_ctrl);
bool bes_get_wlan_enabled(struct rwnx_hw *rwnx_hw);
void bes_set_wlan_enabled(struct rwnx_hw *rwnx_hw, bool on);
void bes_wlan_power_ctrl_init(struct rwnx_hw *rwnx_hw);

/**
 * power control common API by gpio or msg.
 * used to control device power
 */
int bes_power_ctrl_wifi_power(struct bes_power_ctrl *bes_power_ctrl, bool on);
int bes_power_ctrl_bt_power(struct bes_power_ctrl *bes_power_ctrl, bool on);
int bes_power_ctrl_bt_lp(struct bes_power_ctrl *bes_power_ctrl, bool bt_lp);
int bes_power_ctrl_change_fw_type(struct bes_sysfs *bes_sysfs, int type);

/* get wifi/bt enable state */
bool bes_power_ctrl_wifi_enabled(void);
bool bes_power_ctrl_bt_enabled(void);
bool bes_power_ctrl_recovery_trigger(void);

bool bes_power_ctrl_check_system_close(struct bes_power_ctrl *bes_power_ctrl);

/* set system enable state */
int bes_power_ctrl_system_enabled(bool system_enabled, bool bus_error);

/* bus probe check */
void bes_power_ctrl_start_bus_probe(void);
void bes_power_ctrl_bus_probe_notify(void);
void bes_power_ctrl_cancel_probe_work(void);

void *bes_power_ctrl_register(const struct bes_power_ctrl_ops *ops, struct rwnx_plat *rwnx_plat, int priv_size);
void bes_power_ctrl_unregister(struct bes_power_ctrl *bes_power_ctrl);
void bes_power_ctrl_state_reset(void);
void bes_power_ctrl_set_chg_fw_type(struct bes_power_ctrl *bes_power_ctrl);
bool bes_power_ctrl_is_chg_fw_type(void);
bool bes_bootup_end(struct bes_power_ctrl *bes_power_ctrl);

bool bes_power_ctrl_is_bus_error(struct bes_power_ctrl *bes_power_ctrl);
void bes_set_boot_wait_state(enum wait_state state);

int bes_chip_power(struct rwnx_plat *rwnx_plat, bool on);
int bes_wakeup_wifi(struct bes_power_ctrl *bes_power_ctrl, bool wakeup, bool wait);
int bes_bt_power(struct bes_power_ctrl *bes_power_ctrl, bool on);

void bes_unregister_net_dev_after_get_dpd(struct rwnx_hw *rwnx_hw);

void bes_power_ctrl_cancel_wlan_close_work(struct rwnx_hw *rwnx_hw);
void bes_power_ctrl_schedule_wlan_close(struct rwnx_hw *rwnx_hw);
bool bes_power_ctrl_can_skip_platform_init(struct rwnx_plat *rwnx_plat);
void bes_power_ctrl_get_power_state(struct bes_power_state *power_state);

int bes_power_ctrl_do_system_close(void);
int bes_power_ctrl_wait_probe_done(struct bes_power_ctrl *bes_power_ctrl);
int bes_power_ctrl_wait_disconnect_done(struct bes_power_ctrl *bes_power_ctrl);
void bes_power_ctrl_prep_rmmod(struct rwnx_plat *rwnx_plat);
void bes_power_ctrl_set_scancard_time(void);
void bes_power_ctrl_wait_scan_finish(void);

int bes_power_ctrl_no_reboot_fw_chg_set(struct bes_power_ctrl *bes_power_ctrl, bool fw_chging);
bool bes_power_ctrl_no_reboot_fw_chg_get(struct bes_power_ctrl *bes_power_ctrl);
#else /* CONFIG_BES_POWER_CTRL */
/* wlan netdev up/down API */
static inline int bes_power_ctrl_wlan_open(struct rwnx_hw *rwnx_hw) { return 0; }
static inline int bes_power_ctrl_wlan_close(struct rwnx_hw *rwnx_hw) { return 0; }
static inline bool bes_get_wlan_enabled(struct rwnx_hw *rwnx_hw) { return true; }
static inline void bes_set_wlan_enabled(struct rwnx_hw *rwnx_hw, bool on) {}
static inline void bes_wlan_power_ctrl_init(struct rwnx_hw *rwnx_hw) {}

/* get wifi/bt enable state */
static inline bool bes_power_ctrl_wifi_enabled(void) { return true; }
static inline bool bes_power_ctrl_bt_enabled(void) { return true; }
static inline bool bes_power_ctrl_recovery_trigger(void) { return false; }

/* set system enable state */
static inline int bes_power_ctrl_system_enabled(bool system_enabled, bool bus_error) { return 0; }
static inline bool bes_power_ctrl_check_system_close(struct bes_power_ctrl *bes_power_ctrl) { return false; }

/* bus probe check */
static inline void bes_power_ctrl_start_bus_probe(void) {}
static inline void bes_power_ctrl_bus_probe_notify(void) {}
static inline void bes_power_ctrl_cancel_probe_work(void) {}

static inline void bes_power_ctrl_state_reset(void) {}
static inline bool bes_power_ctrl_is_chg_fw_type(void) { return false; }
static inline void bes_set_boot_wait_state(enum wait_state state) {}

static inline int bes_chip_power(struct rwnx_plat *rwnx_plat, bool on) {return 0;}
static inline int bes_wakeup_wifi(struct bes_power_ctrl *bes_power_ctrl, bool wakeup, bool wait) {return 0;}
static inline int bes_bt_power(struct bes_power_ctrl *bes_power_ctrl, bool on) {return 0;}

static inline void bes_unregister_net_dev_after_get_dpd(struct rwnx_hw *rwnx_hw) {}

static inline void bes_power_ctrl_cancel_wlan_close_work(struct rwnx_hw *rwnx_hw) {}
static inline void bes_power_ctrl_schedule_wlan_close(struct rwnx_hw *rwnx_hw) {}
static inline bool bes_power_ctrl_can_skip_platform_init(struct rwnx_plat *rwnx_plat) { return false; }

static inline void bes_power_ctrl_get_power_state(struct bes_power_state *power_state)
{
	power_state->wifi_power = true;
	power_state->bt_power = true;
}

static inline int bes_power_ctrl_do_system_close(void) { return 0; }
static inline void bes_power_ctrl_prep_rmmod(struct rwnx_plat *rwnx_plat) {}
static inline void bes_power_ctrl_set_scancard_time(void) {}
static inline void bes_power_ctrl_wait_scan_finish(void) {}

static inline int bes_power_ctrl_no_reboot_fw_chg_set(struct bes_power_ctrl *bes_power_ctrl, bool fw_chging) {}
static inline bool bes_power_ctrl_no_reboot_fw_chg_get(struct bes_power_ctrl *bes_power_ctrl) { return 0; }
#endif /* CONFIG_BES_POWER_CTRL */

#endif /* __BES_POWER_CTRL_H */
