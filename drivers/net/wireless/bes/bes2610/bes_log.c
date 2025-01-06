/*
 * @file bes_log.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/sort.h>
#include <linux/string.h>
#include <linux/seq_file.h>
#include "rwnx_defs.h"
#include "bes_log.h"

#define  LOG_CTRL_READ_BUF_SIZE  4096
#define  LOG_CTRL_MODELE_NAME_SIZE 25
struct bes_dbg_info {
	int dbg_lvl;
	char module_name[LOG_CTRL_MODELE_NAME_SIZE];
};

/* The maximum length of module_name is LOG_CTRL_MODELE_NAME_SIZE - 1 bytes */
struct bes_dbg_info bes_dbg_tbl[BES_DBG_MAX] = {
	{.dbg_lvl = BES_LOG_INFO, .module_name = "download"},         /* BES_DBG_DOWNLOAD */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "debugfs"},          /* BES_DBG_DEBUGFS */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "bfmer"},            /* BES_DBG_BFMER */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "btusb"},            /* BES_DBG_BTUSB */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "cfgfile"},          /* BES_DBG_CFGFILE */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "cmds"},             /* BES_DBG_CMDS */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "dini"},             /* BES_DBG_DINI */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "fw_dump"},          /* BES_DBG_FW_DUMP */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "fw_trace"},         /* BES_DBG_FW_TRACE */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "irqs"},             /* BES_DBG_IRQS */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "init"},             /* BES_DBG_INIT */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "chan"},             /* BES_DBG_CHAN */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "csa"},              /* BES_DBG_CSA */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "iface"},            /* BES_DBG_IFACE */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "scan"},             /* BES_DBG_SCAN */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "key_mgmt"},         /* BES_DBG_KEY */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "link"},             /* BES_DBG_LINK */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "ap"},               /* BES_DBG_AP */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "roc"},              /* BES_DBG_ROC */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "mod_params"},       /* BES_DBG_MOD_PARAMS */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "msg_rx"},           /* BES_DBG_MSG_RX */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "msg_tx"},           /* BES_DBG_MSG_TX */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "mu_group"},         /* BES_DBG_MU_GROUP */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "pci"},              /* BES_DBG_PCI */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "platform"},         /* BES_DBG_PLATFORM */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "psa"},              /* BES_DBG_PSA */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "log"},              /* BES_DBG_LOG */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "radar"},            /* BES_DBG_RADAR */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "rx"},               /* BES_DBG_RX */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "sdio"},             /* BES_DBG_SDIO */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "sysfs"},            /* BES_DBG_SYSFS */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "tdls"},             /* BES_DBG_TDLS */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "testmode"},         /* BES_DBG_TESTMODE */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "txq"},              /* BES_DBG_TXQ */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "tx"},               /* BES_DBG_TX */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "usb"},              /* BES_DBG_USB */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "utils"},            /* BES_DBG_UTILS */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "power_ctrl"},       /* BES_DBG_POWER_CTRL */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "pwr"},              /* BES_DBG_PWR */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "vendor_cmd"},       /* BES_DBG_VENDOR_CMD */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "wpa_pri_cmd"},      /* BES_DBG_WPA_PRI_CMD */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "iw_pri_cmd"},       /* BES_DBG_IW_PRI_CMD */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "cali"},             /* BES_DBG_CALI */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "filter_manager"},   /* BES_DBG_FILTER_MANAGER */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "custom_filter"},    /* BES_DBG_CUSTOM_FILTER */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "filter_map"},       /* BES_DBG_FILTER_MAP */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "wowlan"},           /* BES_DBG_WOWLAN */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "common"},           /* BES_DBG_COMMON */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "txloop"},           /* BES_DBG_TXLOOP */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "gtk_ofl"},          /* BES_DBG_GTK_OFL */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "lmac_log_filter"},  /* BES_DBG_LMAC_LOG_FILTER */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "fw_log_fifo_dump"}, /* BES_DBG_FW_LOG_FIFO_DUMP */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "vif_info"},         /* BES_DBG_VIF_INFO */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "ibus"},             /* BES_DBG_IBUS */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "eapol"},            /* BES_DBG_EAPOL */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "coex"},             /* BES_DBG_COEX */
	{.dbg_lvl = BES_LOG_DBG, .module_name = "key_frame"},        /* BES_DBG_KEY_FRAME */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "csi_cap"},          /* BES_DBG_CSI_CAP */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "sched_scan"},       /* BES_DBG_SCHED_SCAN */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "spi"},              /* BES_DBG_SPI */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "traffic"},          /* BES_DBG_TRAFFIC */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "phy"},              /* BES_DBG_PHY */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "error_dbg"},        /* BES_DBG_ERROR_DBG */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "conn_state"},       /* BES_DBG_CONN_STATE */
	{.dbg_lvl = BES_LOG_INFO, .module_name = "tp_dbg"},           /* BES_DBG_TP_DBG */
};

int bes_get_dbg_lvl(int module)
{
	if(module >= ARRAY_SIZE(bes_dbg_tbl))
		return BES_LOG_NONE;

	return bes_dbg_tbl[module].dbg_lvl;
}

int bes_set_dbg_lvl(int module,unsigned long log_level)
{
	if(module >= ARRAY_SIZE(bes_dbg_tbl))
		return BES_LOG_NONE;

	bes_dbg_tbl[module].dbg_lvl = log_level;
	return bes_dbg_tbl[module].dbg_lvl;
}

ssize_t log_ctrl_write(struct file *file, const char __user *user_buf, size_t count, loff_t *ppos)
{
	u32 i;
	char *buf = NULL;
	char *module, *level;
	unsigned long log_level;
	char *str_none = "NONE";
	char *str_err = "ERR";
	char *str_warn = "WARN";
	char *str_info = "INFO";
	char *str_dbg = "DBG";

	buf = kmalloc(count + 1, GFP_KERNEL);
	if (!buf) {
		bes_err(BES_DBG_LOG, "%s kmalloc failed\n", __func__);
		return -ENOMEM;
	}

	if (copy_from_user(buf, user_buf, count)) {
		kfree(buf);
		return -EFAULT;
	}
	buf[count] = '\0';

	module = strstr(buf, "module:");
	level = strstr(buf, "level:");
	if (module == NULL || level == NULL) {
		bes_err(BES_DBG_LOG, "module or level error. %s\n", buf);
		goto out;
	}
	module += 7;
	level += 6;
	if (strncasecmp(str_none, level, strlen(str_none)) == 0)
		log_level = BES_LOG_NONE;
	else if(strncasecmp(str_err, level, strlen(str_err)) == 0)
		log_level = BES_LOG_ERROR;
	else if(strncasecmp(str_warn, level, strlen(str_warn)) == 0)
		log_level = BES_LOG_WARN;
	else if(strncasecmp(str_info, level, strlen(str_info)) == 0)
		log_level = BES_LOG_INFO;
	else if(strncasecmp(str_dbg, level, strlen(str_dbg)) == 0)
		log_level = BES_LOG_DBG;
	else {
		bes_err(BES_DBG_LOG, "module level error. %s\n", __func__);
		goto out;
	}

	for (i = 0; i < ARRAY_SIZE(bes_dbg_tbl); ++i) {
		if (strncasecmp(bes_dbg_tbl[i].module_name, module, strlen(bes_dbg_tbl[i].module_name)) == 0) {
			bes_dbg_tbl[i].dbg_lvl = log_level;
			break;
		}
	}

out:
	kfree(buf);
	return count;
}

ssize_t log_ctrl_read(struct file *file, char __user *user_buf, size_t count, loff_t *ppos)
{
	char *buf = NULL;
	int i, ret;
	int used_len = 0, tmp_len = 0;
	char str[10] = {0};
	char str_tmp[LOG_CTRL_MODELE_NAME_SIZE + sizeof(str)] = {0};

	if (!count)
		return -EINVAL;
	buf = kmalloc(LOG_CTRL_READ_BUF_SIZE, GFP_KERNEL);
	if(!buf) {
		bes_err(BES_DBG_LOG, "kmalloc error. %s\n", __func__);
		return -ENOMEM;
	}
	for (i = 0; i < ARRAY_SIZE(bes_dbg_tbl); ++i) {
		if (bes_dbg_tbl[i].dbg_lvl == BES_LOG_NONE) {
			strcpy(str, "NONE");
		} else if (bes_dbg_tbl[i].dbg_lvl == BES_LOG_ERROR) {
			strcpy(str, "ERR");
		} else if (bes_dbg_tbl[i].dbg_lvl == BES_LOG_WARN) {
			strcpy(str, "WARN");
		} else if (bes_dbg_tbl[i].dbg_lvl == BES_LOG_INFO) {
			strcpy(str, "INFO");
		} else if (bes_dbg_tbl[i].dbg_lvl == BES_LOG_DBG) {
			strcpy(str, "DBG");
		} else {
			ret = -EINVAL;
			goto exit;
		}

		/* 25 is LOG_CTRL_MODELE_NAME_SIZE */
		tmp_len = snprintf(str_tmp, sizeof(str_tmp), "%-25s%s\n", bes_dbg_tbl[i].module_name, str);
		if (LOG_CTRL_READ_BUF_SIZE - used_len - 1 >= tmp_len)
			used_len += snprintf(buf + used_len, (LOG_CTRL_READ_BUF_SIZE - used_len), "%s", str_tmp);
	}
	ret = simple_read_from_buffer(user_buf, count, ppos, buf, used_len);

exit:
	kfree(buf);
	return ret;
}
