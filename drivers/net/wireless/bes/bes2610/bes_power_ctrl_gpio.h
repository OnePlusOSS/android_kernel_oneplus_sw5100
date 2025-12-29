/*
 * @file bes_power_ctrl_gpio.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __BES_POWER_CTRL_GPIO_H
#define __BES_POWER_CTRL_GPIO_H

struct bes_gpio_data {
	int powerup;          /* GPIO to main switch */                         // use WL_REG_ON
	int wakeup_wifi;      /* GPIO to host wakeup wifi and wifi switch */    // use BT_WAKEUP_HOST
	int bt_powerup;       /* GPIO to bt switch */                           // BT_EN
	int wakeup_bt;        /* GPIO to host wakeup bt */                      // HOST_WAKE_BT
	int wakeup_host;      /* GPIO to wifi/bt mcu wakeup host */             // WIFI_HOST_WAKE
};

/* extern */ struct bes_power_ctrl;

struct bes_power_ctrl_gpio {
	struct bes_gpio_data *pdata;
	struct rwnx_plat *plat;
	void *priv;
};

#ifdef CONFIG_BES_POWER_CTRL_GPIO
void *bes_power_ctrl_gpio_register(struct rwnx_plat *rwnx_plat, size_t priv_data_len, plat_power_cb_t cb);
void bes_power_ctrl_gpio_unregister(void *priv);
#else /* CONFIG_BES_POWER_CTRL_GPIO */
static inline void *bes_power_ctrl_gpio_register(struct rwnx_plat *rwnx_plat,
						 size_t priv_data_len, plat_power_cb_t cb) { return NULL; }
static inline void bes_power_ctrl_gpio_unregister(void *priv) {}
#endif /* CONFIG_BES_POWER_CTRL_GPIO */

#endif /* __BES_POWER_CTRL_GPIO_H */
