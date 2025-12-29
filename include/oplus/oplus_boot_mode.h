/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2018-2021 Oplus. All rights reserved.
 */
#ifndef __OPLUS_BOOT_MODE_H
#define __OPLUS_BOOT_MODE_H
enum{
	MSM_BOOT_MODE__NORMAL,
	MSM_BOOT_MODE__FASTBOOT,
	MSM_BOOT_MODE__RECOVERY,
	MSM_BOOT_MODE__FACTORY,
	MSM_BOOT_MODE__RF,
	MSM_BOOT_MODE__WLAN,
	MSM_BOOT_MODE__MOS,
	MSM_BOOT_MODE__CHARGE,
	MSM_BOOT_MODE__SILENCE,
	MSM_BOOT_MODE__SAU,
	MSM_BOOT_MODE__AGING = 998,
	MSM_BOOT_MODE__SAFE = 999,
};

extern int get_boot_mode(void);

/*add for charge*/
extern bool qpnp_is_power_off_charging(void);

/*add for detect charger when reboot */
extern bool qpnp_is_charger_reboot(void);

#endif  /*_BOOT_MODE_H*/

/*Add for kernel monitor whole bootup*/
#ifdef PHOENIX_PROJECT
extern bool op_is_monitorable_boot(void);

#endif /*__OPLUS_BOOT_MODE_H*/

