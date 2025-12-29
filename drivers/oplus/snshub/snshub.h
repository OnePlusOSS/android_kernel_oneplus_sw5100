/*
 * oplus_sensorhub.h - Linux kernel modules for Oplus SensorHub
 *
 * Copyright (C), 2008-2023, Oplus Mobile Comm Corp., Ltd.
 * Author: Zeng Zhaoxiu
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.
 */

#ifndef __SNSHUB_H
#define __SNSHUB_H

#define MCU_STATES_UEVENT

#include <linux/notifier.h>

enum {
    SNSHUB_PRE_RESET,
    SNSHUB_POST_RESET,
};

int oplus_snshub_reset_notifier_register(struct device *dev, struct notifier_block *nb);
int oplus_snshub_reset_notifier_unregister(struct device *dev, struct notifier_block *nb);

void oplus_snshub_assert_reset(struct device *dev);
void oplus_snshub_deassert_reset(struct device *dev);
void oplus_snshub_reset(struct device *dev);
void oplus_snshub_power_off_once(struct device *dev);
int oplus_snshub_hw_gpio_in(struct device *dev);
bool is_mcureseting(void);
void wait_mcureseting(void);

#ifdef MCU_STATES_UEVENT
typedef enum {
    MCU_STATES_FIRST = 0,
    MCU_STATES_NORMAL,
    MCU_STATES_CRASH,
    MCU_STATES_DUMP,
    MCU_STATES_MAX,
} snshub_mcu_states_t;

void oplus_snshub_states_uevent(int index, char* const dev_name);
void oplus_snshub_wakeup_data_uevent(char* const dev_name);

uint oplus_snshub_get_disable_uevent(void);
void oplus_snshub_set_disable_uevent(uint val);
#endif

#endif /*__SNSHUB_H*/
