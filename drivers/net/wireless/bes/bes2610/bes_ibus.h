/*
 * @file bes_ibus.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BES_IBUS_H_
#define _BES_IBUS_H_

#include "rwnx_plat_comm.h"

struct rwnx_hw;

#define DRIVER_NAME     "bes_ibus"
#define WIFI_IBUS_MEM_BASE 0x72000000
#define WIFI_IBUS_MEM_SIZE 0x1000000

struct bes_soc {
	int irq;
	u8 *mem_vaddr;
	u8 *sram_vaddr;
	u8 *sys2wf_reg_base;
	u8 *lp_status;
	struct hwspinlock *lp_lock;
	bool rx_pending;
};

#define RWNX_IBUS_PLAT_PRIV_SIZE (sizeof(struct bes_soc))

/**
 * struct rwnx_plat - Operation pointers for BES IBUS platform
 *
 * @drvdata: driver data
 * @pre_alloc: indicate if pre-alloc wifi resource
 * @deferred_e2amsg_work: sta connect/disconect work
 * @pdev: ibus platform device
 * @enable: Configure communication with the fw (i.e. configure the transfers
 *         enable and register interrupt)
 * @disable: Stop communication with the fw
 * @deinit: Free all ressources allocated for the embedded platform
 * @get_address: Return the virtual address to access the requested address on
 *              the platform.
 * @ack_irq: Acknowledge the irq at link level.
 * @get_config_reg: Return the list (size + pointer) of registers to restore in
 * order to reload the platform while keeping the current configuration.
 * @power_priv: private data for platform power control
 * @dev_stat_wait_q: dev state wait queue
 * @dev_status: dev state
 *
 * @pwr_io_lock: host_wakeup_mcu gpio spinlock
 * @pwr_io_ctl: host_wakeup_mcu gpio setting value
 *
 * @run_flags: indicate the run flags for plat
 * @run_flags_lock: the spinlock to protect the run flags for plat
 * @dpd_cali: dpd calib value
 *
 * @priv: Private data for the link driver
 */

struct rwnx_plat {
	void *drvdata;
	bool pre_alloc;
	struct work_struct deferred_e2amsg_work;
	struct platform_device *pdev;
	bool enabled;

	int (*enable)(struct rwnx_hw *rwnx_hw);
	int (*disable)(struct rwnx_hw *rwnx_hw);
	void (*deinit)(struct rwnx_plat *rwnx_plat);
	u8 *(*get_address)(struct rwnx_plat *rwnx_plat, int addr_name, unsigned int offset);
	void (*ack_irq)(struct rwnx_plat *rwnx_plat);
	int (*get_config_reg)(struct rwnx_plat *rwnx_plat, const u32 **list);
#ifdef CONFIG_BES_POWER_CTRL
	void *power_priv;
#endif
	wait_queue_head_t dev_stat_wait_q;
	u32 dev_status;
	spinlock_t pwr_io_lock;
	u32 pwr_io_ctl;
	unsigned long wakelock_flag;
	struct device *dev;
	unsigned long run_flags;
	spinlock_t run_flags_lock;
	struct rwnx_dpd_cali dpd_cali;

	/* keep last */
	u8 priv[RWNX_IBUS_PLAT_PRIV_SIZE];
};

int bes_ibus_register_drv(void);
void bes_ibus_unregister_drv(void);

#define RWNX_ADDR(plat, base, offset) plat->get_address(plat, base, offset)

#define RWNX_REG_READ(plat, base, offset) readl(plat->get_address(plat, base, offset))

#define RWNX_REG_WRITE(val, plat, base, offset) writel(val, plat->get_address(plat, base, offset))

#ifdef CONFIG_BES_POWER_CTRL_MSG
struct bes_ibus_power_ctrl {
	struct bes_power_ctrl *power_ctrl; /* keep first */
	struct work_struct ibus_off_work;
#ifdef CONFIG_BES_IBUS_RPMSG
	struct rpmsg_device *rpdev;
	struct mutex msg_lock;
	struct completion complete;
	void *confirm;
#endif
	void *priv;
};

int bes_power_ctrl_ibus_init(struct rwnx_plat *rwnx_plat);
void bes_power_ctrl_ibus_deinit(struct rwnx_plat *rwnx_plat);
#else
static inline int bes_power_ctrl_ibus_init(struct rwnx_plat *rwnx_plat) { return 0; }
static inline void bes_power_ctrl_ibus_deinit(struct rwnx_plat *rwnx_plat) {}
#endif /* CONFIG_BES_POWER_CTRL_MSG */

int bes_ibus_platform_init(struct rwnx_plat *rwnx_plat, void **drvdata);

#endif /* _BES_IBUS_H_ */
