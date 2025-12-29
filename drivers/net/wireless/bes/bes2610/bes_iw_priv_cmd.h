/*
 * @file bes_iw_priv_cmd.h
 *
 * Copyright (c) 2024, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BES_IW_PRIV_CMD_H_
#define _BES_IW_PRIV_CMD_H_

#include <linux/wireless.h>
#include <net/iw_handler.h>

#define BES_IW_PRIV_SET_CMD_IFNAMSIZ_MAX 16
#define BES_IW_PRIV_GET_CMD_IFNAMSIZ_MAX 16

enum {
	IW_PRIV_SET = SIOCIWFIRSTPRIV,
	IW_PRIV_GET,
};

enum {
	SET_COUNTRY = 0,
#ifdef CONFIG_BES_WIFI_LP
	SET_POWER_SAVE,
#endif
	SET_ONLINE_SCAN,
	SET_CRASH_LMAC,
	SET_TX_MAX_POWER,

	SET_CMD_MAX
};

enum {
	GET_COUNTRY = 0,
#ifdef CONFIG_BES_WIFI_LP
	GET_POWER_SAVE,
#endif
	GET_ONLINE_SCAN,
	GET_TEMPRTR,
	GET_TX_DUTY_RATIO,
	GET_PLL_STATE,

	GET_CMD_MAX
};

extern const struct iw_handler_def bes_iw_handler_def;

typedef int (*iw_set_handler)(struct net_device *dev, char *cmd_param);
typedef int (*iw_get_handler)(struct net_device *dev, union iwreq_data *wrqu, char *extra);

int bes_iw_priv_cmd(struct net_device *dev, struct ifreq *rq, int cmd);
#endif /* _BES_IW_PRIV_CMD_H_ */