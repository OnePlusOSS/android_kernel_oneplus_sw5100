/*
 * @file bes_ibus_rpmsg.h
 *
 * Copyright (c) 2024, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BES_IBUS_RPMSG_H_
#define _BES_IBUS_RPMSG_H_

int bes_ibus_rpmsg_init(void);
void bes_ibus_rpmsg_deinit(void);
int bes_ibus_rpmsg_wifi_on_off(bool on);
int bes_ibus_rpmsg_wifi_patched(void);
int bes_ibus_rpmsg_wifi_run(void);

#endif /* _BES_IBUS_RPMSG_H_ */
