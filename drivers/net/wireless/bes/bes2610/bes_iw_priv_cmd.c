/*
 * @file bes_iw_priv_cmd.c
 *
 * Copyright (c) 2024, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/ctype.h>
#include "bes_iw_priv_cmd.h"
#include "bes_log.h"
#include "bes_pwr.h"
#include "bes_sysfs.h"
#include "rwnx_msg_tx.h"
#include "bes_regd.h"
#include "bes_phy.h"

static int bes_iw_priv_cmd_set_country(struct net_device *dev, char *extra)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	struct wiphy *wiphy = rwnx_hw->wiphy;
	char country_code[2] = {0};
	// convert to uppercase letters
	extra[0] = toupper(extra[0]);
	extra[1] = toupper(extra[1]);

	if((isalpha(extra[0]) && isalpha(extra[1])) ||
	   memcmp(extra, "00", sizeof("00")) == 0) {
		memcpy(country_code, extra, 2);

		if (!bes_regd_update_custregd(rwnx_hw, wiphy, country_code)) {
			bes_info(BES_DBG_IW_PRI_CMD, "%s: set country code :%c%c successful\n",
				 __FUNCTION__, country_code[0], country_code[1]);
		} else {
			bes_err(BES_DBG_IW_PRI_CMD, "%s: set country code :%c%c failed\n",
				__FUNCTION__, country_code[0], country_code[1]);
		}

		return 0;
	} else {
		bes_err(BES_DBG_IW_PRI_CMD, "%s: the country code :%c%c is incorrect\n",
			__FUNCTION__, extra[0], extra[1]);
		return -EINVAL;
	}
}

static int bes_iw_priv_cmd_get_country(struct net_device *dev, union iwreq_data *wrqu, char *extra)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	char *working_country = rwnx_hw->custregd.working_country;

	snprintf(wrqu->name, BES_IW_PRIV_GET_CMD_IFNAMSIZ_MAX, "%c%c", working_country[0], working_country[1]);

	return 0;
}

#ifdef CONFIG_BES_WIFI_LP
static int bes_iw_priv_cmd_set_power_save(struct net_device *dev, char *extra)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	u8 vif_index = rwnx_vif->vif_index;

	if (!rwnx_vif->up || vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_IW_PRI_CMD, "rwnx_vif is not up\n");
		return -EINVAL;
	}

	/* Ignore power save commands from user if user_ps_mgmt_disable is set */
	if (rwnx_hw->mod_params->user_ps_mgmt_disable) {
		bes_info(BES_DBG_PWR, "%s, vif:%u discard ps mgmt cmd\n", __func__, rwnx_vif->vif_index);
		return 0;
	}

	if (strncmp(extra, "on", 2) == 0)
		bes_pwr_clear_busy_event(rwnx_hw, vif_index, VIF_CONST_EVENT_PSM);
	else if (strncmp(extra, "off", 3) == 0)
		bes_pwr_set_busy_event(rwnx_hw, vif_index, VIF_CONST_EVENT_PSM);

	return 0;
}

static int bes_iw_priv_cmd_get_power_save(struct net_device *dev, union iwreq_data *wrqu, char *extra)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	u8 vif_index = rwnx_vif->vif_index;

	if (!rwnx_vif->up || vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_IW_PRI_CMD, "rwnx_vif is not up\n");
		return -EINVAL;
	}

	if (bes_pwr_get_vif_const_event_lock(rwnx_hw, vif_index) & BIT(VIF_CONST_EVENT_PSM))
		snprintf(wrqu->name, BES_IW_PRIV_GET_CMD_IFNAMSIZ_MAX, "off");
	else
		snprintf(wrqu->name, BES_IW_PRIV_GET_CMD_IFNAMSIZ_MAX, "on");

	return 0;
}
#endif

static int bes_iw_priv_cmd_set_online_scan(struct net_device *dev, char *extra)
{
	if (strncmp(extra, "on", 2) == 0)
		bes_sysfs_ctrl_online_scan(true);
	else if (strncmp(extra, "off", 3) == 0)
		bes_sysfs_ctrl_online_scan(false);

	return 0;
}

static int bes_iw_priv_cmd_get_online_scan(struct net_device *dev, union iwreq_data *wrqu, char *extra)
{
	if (bes_sysfs_support_online_scan())
		snprintf(wrqu->name, BES_IW_PRIV_GET_CMD_IFNAMSIZ_MAX, "on");
	else
		snprintf(wrqu->name, BES_IW_PRIV_GET_CMD_IFNAMSIZ_MAX, "off");

	return 0;
}

static inline int bes_iw_priv_cmd_crash_lmac(struct rwnx_hw *rwnx_hw, u8 crash_type)
{
	struct mm_custom_req_hdr req;

	req.id = CUSTOM_ID_CRASH_LMAC;
	req.length = sizeof(struct mm_custom_req_hdr);
	req.ops = crash_type;

	return rwnx_send_custom_request(rwnx_hw, &req, NULL, 0);
}

static int bes_iw_priv_cmd_set_crash_lmac(struct net_device *dev, char *extra)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	u8 crash_type;

	if (strncmp(extra, "exception", 9) && strncmp(extra, "assert", 6))
		return 0;

	crash_type = strncmp(extra, "exception", 9) == 0 ?
		     CRASH_LMAC_OPS_ILLEGAL_ACCESS : CRASH_LMAC_OPS_ASSERT;
	return bes_iw_priv_cmd_crash_lmac(rwnx_hw, crash_type);
}

static int bes_iw_priv_cmd_set_tx_max_pwr(struct net_device *dev, char *extra)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	unsigned long pwr_idx = simple_strtoul(extra, NULL, 0);
	int ret;
	struct wifi_pwr_range pwr_range = {S16_MIN, S16_MIN};

	if (pwr_idx >= rwnx_hw->conf_file.max_pwr_cfg_num) {
		bes_err(BES_DBG_IW_PRI_CMD, "%s: invalid power inedx %lu\n", __func__, pwr_idx);
		return -EINVAL;
	}

	ret = bes_send_set_tx_max_pwr_req(rwnx_hw, &rwnx_hw->conf_file.max_pwr[pwr_idx], &pwr_range);
	if (ret)
		return ret;

	bes_info(BES_DBG_IW_PRI_CMD, "%s, current max power:%d.%d(dBm), min power:%d.%d(dBm)\n", __func__,
		 pwr_range.max_pwr / 10, abs(pwr_range.max_pwr) % 10, pwr_range.min_pwr / 10, abs(pwr_range.min_pwr) % 10);

	return 0;
}

static int bes_iw_priv_cmd_get_temprtr(struct net_device *dev, union iwreq_data *wrqu, char *extra)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	int ret = 0;
	int32_t temprtr = 0;

	ret = bes_send_temprtr_get(rwnx_hw, &temprtr);
	if (ret)
		return ret;

	snprintf(wrqu->name, BES_IW_PRIV_GET_CMD_IFNAMSIZ_MAX, "%d°C", temprtr);

	return ret;
}

static int bes_iw_priv_cmd_get_tx_duty_ratio(struct net_device *dev,
					     union iwreq_data *wrqu, char *extra)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	int duty_ratio;

	duty_ratio = bes_phy_get_tx_duty_ratio(rwnx_vif->rwnx_hw);

	snprintf(wrqu->name, BES_IW_PRIV_GET_CMD_IFNAMSIZ_MAX,
		 "%d.%d%%\n", duty_ratio / 10, duty_ratio % 10);

	return 0;
}

static int bes_iw_priv_cmd_get_pll_state(struct net_device *dev, union iwreq_data *wrqu, char *extra)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	int ret = 0;
	bool pll_break = false;

	ret = bes_send_drv2mcu_msg(rwnx_vif->rwnx_hw, NULL, 0, MCU_GET_PLL_STATE_REQ,
				   MCU_GET_PLL_STATE_CFM, &pll_break, sizeof(pll_break));
	if (ret)
		return ret;

	snprintf(wrqu->name, BES_IW_PRIV_GET_CMD_IFNAMSIZ_MAX, pll_break ? "true" : "false");

	return ret;
}

static iw_set_handler iw_set_handler_tab[] = {
	bes_iw_priv_cmd_set_country,
#ifdef CONFIG_BES_WIFI_LP
	bes_iw_priv_cmd_set_power_save,
#endif
	bes_iw_priv_cmd_set_online_scan,
	bes_iw_priv_cmd_set_crash_lmac,
	bes_iw_priv_cmd_set_tx_max_pwr,
};

static iw_get_handler iw_get_handler_tab[] = {
	bes_iw_priv_cmd_get_country,
#ifdef CONFIG_BES_WIFI_LP
	bes_iw_priv_cmd_get_power_save,
#endif
	bes_iw_priv_cmd_get_online_scan,
	bes_iw_priv_cmd_get_temprtr,
	bes_iw_priv_cmd_get_tx_duty_ratio,
	bes_iw_priv_cmd_get_pll_state,
};

static int bes_iw_priv_cmd_set_handler(struct net_device *dev, struct iw_request_info *info,
				       union iwreq_data *wrqu, char *extra)
{
	if (wrqu->encoding.flags >= SET_CMD_MAX) {
		bes_err(BES_DBG_IW_PRI_CMD, "%s: cmd_type(%d) >= SET_CMD_MAX\n",
			__FUNCTION__, wrqu->encoding.flags);
		return -EINVAL;
	}

	bes_dbg(BES_DBG_WPA_PRI_CMD, "%s iw private cmd type:%d param:\"%s\" vif:%s\n",
			__FUNCTION__, wrqu->encoding.flags, extra, dev->name);

	return (iw_set_handler_tab[wrqu->encoding.flags])(dev, extra);
}

static int bes_iw_priv_cmd_get_handler(struct net_device *dev, struct iw_request_info *info,
				       union iwreq_data *wrqu, char *extra)
{
	if (wrqu->mode >= GET_CMD_MAX) {
		bes_err(BES_DBG_IW_PRI_CMD, "%s: cmd_type(%d) >= GET_CMD_MAX\n",
			__FUNCTION__, wrqu->mode);
		return -EINVAL;
	}

	return (iw_get_handler_tab[wrqu->mode])(dev, wrqu, extra);
}

static iw_handler bes_iw_priv_handler[] = {
	bes_iw_priv_cmd_set_handler,
	bes_iw_priv_cmd_get_handler,
	NULL
};

static struct iw_priv_args bes_iw_priv_args[] = {
	// --- core-ioctls interface information ---
	{
		IW_PRIV_SET,
		IW_PRIV_TYPE_CHAR | BES_IW_PRIV_SET_CMD_IFNAMSIZ_MAX,
		0,
		""
	},
	{
		IW_PRIV_GET,
		0,
		IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | BES_IW_PRIV_GET_CMD_IFNAMSIZ_MAX,
		""
	},
	// --- sub-ioctls interface information ---
	{
		SET_COUNTRY,
		IW_PRIV_TYPE_CHAR | BES_IW_PRIV_SET_CMD_IFNAMSIZ_MAX,
		0,
		"set_country"
	},
	{
		GET_COUNTRY,
		0,
		IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | BES_IW_PRIV_GET_CMD_IFNAMSIZ_MAX,
		"get_country"
	},
#ifdef CONFIG_BES_WIFI_LP
	{
		SET_POWER_SAVE,
		IW_PRIV_TYPE_CHAR | BES_IW_PRIV_SET_CMD_IFNAMSIZ_MAX,
		0,
		"set_power_save"
	},
	{
		GET_POWER_SAVE,
		0,
		IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | BES_IW_PRIV_GET_CMD_IFNAMSIZ_MAX,
		"get_power_save"
	},
#endif
	{
		SET_ONLINE_SCAN,
		IW_PRIV_TYPE_CHAR | BES_IW_PRIV_SET_CMD_IFNAMSIZ_MAX,
		0,
		"set_online_scan"
	},
	{
		GET_ONLINE_SCAN,
		0,
		IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | BES_IW_PRIV_GET_CMD_IFNAMSIZ_MAX,
		"get_online_scan"
	},
	{
		SET_CRASH_LMAC,
		IW_PRIV_TYPE_CHAR | BES_IW_PRIV_SET_CMD_IFNAMSIZ_MAX,
		0,
		"set_crash_lmac"
	},
	{
		GET_TEMPRTR,
		0,
		IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | BES_IW_PRIV_GET_CMD_IFNAMSIZ_MAX,
		"get_temprtr"
	},
	{
		SET_TX_MAX_POWER,
		IW_PRIV_TYPE_CHAR | BES_IW_PRIV_SET_CMD_IFNAMSIZ_MAX,
		0,
		"set_tx_max_pwr"
	},
	{
		GET_TX_DUTY_RATIO,
		0,
		IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | BES_IW_PRIV_GET_CMD_IFNAMSIZ_MAX,
		"tx_duty_ratio"
	},
	{
		GET_PLL_STATE,
		0,
		IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | BES_IW_PRIV_GET_CMD_IFNAMSIZ_MAX,
		"pll_break"
	},
	{ 0, 0, 0, { 0 } }
};

const struct iw_handler_def bes_iw_handler_def =
{
	.num_private = ARRAY_SIZE(bes_iw_priv_handler),
	.num_private_args = ARRAY_SIZE(bes_iw_priv_args),
	.private = bes_iw_priv_handler,
	.private_args = bes_iw_priv_args,
};
