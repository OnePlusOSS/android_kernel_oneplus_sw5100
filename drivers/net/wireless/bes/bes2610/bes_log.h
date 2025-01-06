/*
 * @file bes_log.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __BES_LOG_H__
#define __BES_LOG_H__

#include "lmac_msg.h"

#ifdef CONFIG_OPLUS_PLATFORM
#define SENSITIVE_LOG_HIDE
#endif

/* Log Level Control */
#define BES_LOG_NONE		0
#define BES_LOG_ERROR		1
#define BES_LOG_WARN		2
#define BES_LOG_INFO		3
#define BES_LOG_DBG		4

/* Module Log Level Control */
enum BES_DBG {
	BES_DBG_DOWNLOAD = 0,
	BES_DBG_DEBUGFS,
	BES_DBG_BFMER,
	BES_DBG_BTUSB,
	BES_DBG_CFGFILE,
	BES_DBG_CMDS,
	BES_DBG_DINI,
	BES_DBG_FW_DUMP,
	BES_DBG_FW_TRACE,
	BES_DBG_IRQS,
	BES_DBG_INIT,
	BES_DBG_CHAN,
	BES_DBG_CSA,
	BES_DBG_IFACE,
	BES_DBG_SCAN,
	BES_DBG_KEY,
	BES_DBG_LINK,
	BES_DBG_AP,
	BES_DBG_ROC,
	BES_DBG_MOD_PARAMS,
	BES_DBG_MSG_RX,
	BES_DBG_MSG_TX,
	BES_DBG_MU_GROUP,
	BES_DBG_PCI,
	BES_DBG_PLATFORM,
	BES_DBG_PSA,
	BES_DBG_LOG,
	BES_DBG_RADAR,
	BES_DBG_RX,
	BES_DBG_SDIO,
	BES_DBG_SYSFS,
	BES_DBG_TDLS,
	BES_DBG_TESTMODE,
	BES_DBG_TXQ,
	BES_DBG_TX,
	BES_DBG_USB,
	BES_DBG_UTILS,
	BES_DBG_POWER_CTRL,
	BES_DBG_PWR,
	BES_DBG_VENDOR_CMD,
	BES_DBG_WPA_PRI_CMD,
	BES_DBG_IW_PRI_CMD,
	BES_DBG_CALI,
	BES_DBG_FILTER_MANAGER,
	BES_DBG_CUSTOM_FILTER,
	BES_DBG_FILTER_MAP,
	BES_DBG_WOWLAN,
	BES_DBG_COMMON,
	BES_DBG_TXLOOP,
	BES_DBG_GTK_OFL,
	BES_DBG_LMAC_LOG_FILTER,
	BES_DBG_FW_LOG_FIFO_DUMP,
	BES_DBG_VIF_INFO,
	BES_DBG_IBUS,
	BES_DBG_EAPOL,
	BES_DBG_COEX,
	BES_DBG_KEY_FRAME,
	BES_DBG_CSI_CAP,
	BES_DBG_SCHED_SCAN,
	BES_DBG_SPI,
	BES_DBG_TRAFFIC,
	BES_DBG_PHY,
	BES_DBG_ERROR_DBG,
	BES_DBG_CONN_STATE,
	BES_DBG_TP_DBG,

	BES_DBG_MAX,
};

int bes_get_dbg_lvl(int module);
int bes_set_dbg_lvl(int module,unsigned long log_level);

ssize_t log_ctrl_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos);

ssize_t log_ctrl_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos);

#define GET_LOG_LVL(module)		bes_get_dbg_lvl(module)

#define bes_dbg(module, ...)					\
	do {							\
		if (GET_LOG_LVL(module) >= BES_LOG_DBG)		\
			printk(KERN_INFO __VA_ARGS__);		\
	} while (0)

#define bes_dbg_with_cond(cond, module, ...)			\
	do {							\
		if ((0 != (cond)) && 				\
		    GET_LOG_LVL(module) >= BES_LOG_DBG)		\
			printk(KERN_DEBUG __VA_ARGS__);		\
	} while (0)

#define bes_dbg_dump(module, desc, array, len)			\
	do {							\
		if (GET_LOG_LVL(module) >= BES_LOG_DBG)		\
			print_hex_dump(KERN_DEBUG, 		\
				desc, DUMP_PREFIX_NONE,		\
				16, 1, array, 			\
				len, false);			\
	} while(0)

#define bes_info(module, ...)					\
	do {							\
		if (GET_LOG_LVL(module) >= BES_LOG_INFO)	\
			printk(KERN_INFO __VA_ARGS__);		\
	} while (0)

#define bes_info_with_cond(cond, module, ...)			\
	do {							\
		if ((0 != (cond)) && 				\
		    GET_LOG_LVL(module) >= BES_LOG_INFO)	\
			printk(KERN_INFO __VA_ARGS__);		\
	} while (0)

#define bes_info_dump(module, desc, array, len)			\
	do {							\
		if (GET_LOG_LVL(module) >= BES_LOG_INFO)	\
			print_hex_dump(KERN_INFO, 		\
				desc, DUMP_PREFIX_NONE,		\
				16, 1, array, 			\
				len, false);			\
	} while(0)


#define bes_warn(module, ...)					\
	do {							\
		if (GET_LOG_LVL(module) >= BES_LOG_WARN)	\
			printk(KERN_WARNING __VA_ARGS__);	\
	} while (0)

#define bes_warn_with_cond(cond, module, ...)			\
	do {							\
		if ((0 != (cond)) && 				\
		    GET_LOG_LVL(module) >= BES_LOG_WARN)	\
			printk(KERN_WARNING __VA_ARGS__);	\
	} while (0)

#define bes_warn_dump(module, desc, array, len)			\
	do {							\
		if (GET_LOG_LVL(module) >= BES_LOG_WARN)	\
			print_hex_dump(KERN_WARNING, 		\
				desc, DUMP_PREFIX_NONE,		\
				16, 1, array, 			\
				len, false);			\
	} while(0)

#define bes_err(module, ...)					\
	do {							\
		if (GET_LOG_LVL(module) >= BES_LOG_ERROR)	\
			printk(KERN_ERR __VA_ARGS__);		\
	} while (0)

#define bes_err_with_cond(cond, module, ...)			\
	do {							\
		if ((0 != (cond)) && 				\
		    GET_LOG_LVL(module) >= BES_LOG_ERROR)	\
			printk(KERN_ERR __VA_ARGS__);		\
	} while (0)

#define bes_err_dump(module, desc, array, len)			\
	do {							\
		if (GET_LOG_LVL(module) >= BES_LOG_ERROR)	\
			print_hex_dump(KERN_ERR, 		\
				desc, DUMP_PREFIX_NONE,		\
				16, 1, array, 			\
				len, false);			\
	} while(0)


#ifdef SENSITIVE_LOG_HIDE
#define TR_MAC_STR "%02x:**:**:%02x:**:%02x"
#define TR_MAC(a) ((uint8_t *)(a))[0], ((uint8_t *)(a))[3], ((uint8_t *)(a))[5]
static inline const char *TR_SSID(const struct mac_ssid *ssid)
{
	static char sec_ssid_buf[6] = {0};
	const char *k;
	uint8_t len;
	if (ssid == NULL) {
		sec_ssid_buf[0] = '\0';
		return sec_ssid_buf;
	}
	k = (const char *)(ssid->array);
	len = (uint8_t)ssid->length;
	if (len > 3) {
		sec_ssid_buf[0] = *k;
		sec_ssid_buf[1] = *(k + 1);
		sec_ssid_buf[2] = '*';
		sec_ssid_buf[3] = '*';
		sec_ssid_buf[4] = *(k + len - 1);
		sec_ssid_buf[5] = '\0';
		return sec_ssid_buf;
	}
	return k;
}
#else
#define TR_MAC_STR "%02x:%02x:%02x:%02x:%02x:%02x"
#define TR_MAC(a) ((uint8_t *)(a))[0], ((uint8_t *)(a))[1], ((uint8_t *)(a))[2], ((uint8_t *)(a))[3], ((uint8_t *)(a))[4], ((uint8_t *)(a))[5]
#define TR_SSID(a) (const char *)((a)->array)
#endif

#define TR_VIF_STR "vif%d(type %d)"
#define TR_VIF(vif) (vif)->vif_index, (vif)->wdev.iftype

#ifdef CONFIG_OPLUS_PLATFORM
extern int oplus_wlan_log_init(void);
extern void oplus_wlan_log_deinit(void);
extern ssize_t oplus_log_write(const char *buf, size_t len);
#endif

#endif
