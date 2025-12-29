/**
 ******************************************************************************
 *
 * @file rwnx_pci.h
 *
 * Copyright (C) RivieraWaves 2012-2021
 *
 ******************************************************************************
 */

#ifndef _RWNX_PCI_H_
#define _RWNX_PCI_H_

#include "rwnx_plat_comm.h"

struct rwnx_hw;

struct rwnx_v7 {
	u8 *pci_bar0_vaddr;
	u8 *pci_bar1_vaddr;
};

struct rwnx_dini {
	u8 *pci_bar0_vaddr;
	u8 *pci_bar4_vaddr;
};

#define RWNX_PCI_PLAT_PRIV_SIZE (sizeof(struct rwnx_v7) > sizeof(struct rwnx_dini) ?\
				 sizeof(struct rwnx_v7) : sizeof(struct rwnx_dini))

#define PCIE_CONFIG_SPACE_LEN 64

/**
 * struct rwnx_plat - Operation pointers for RWNX PCI platform
 *
 * @drvdata: driver data
 * @pre_alloc: indicate if pre-alloc wifi resource
 * @register_flag: bus driver register flag
 * @deferred_e2amsg_work: sta connect/disconect work
 * @pci_dev: pointer to pci dev
 * @enabled: Set if embedded platform has been enabled (i.e. fw loaded and
 *          ipc started)
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
	bool register_flag;
	struct work_struct deferred_e2amsg_work;
	struct pci_dev *pci_dev;
	u32 pcie_cfgregs[PCIE_CONFIG_SPACE_LEN];

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
	u8 priv[RWNX_PCI_PLAT_PRIV_SIZE];
};

#define RWNX_ADDR(plat, base, offset) plat->get_address(plat, base, offset)

#define RWNX_REG_READ(plat, base, offset) readl(plat->get_address(plat, base, offset))

#define RWNX_REG_WRITE(val, plat, base, offset) writel(val, plat->get_address(plat, base, offset))

int rwnx_pci_register_drv(void);
void rwnx_pci_unregister_drv(void);

#if defined(CONFIG_RWNX_PCIE) && defined(PLAT_ROCKCHIP)
void rwnx_pci_vif_start(struct rwnx_hw *rwnx_hw, u8 idx);
void rwnx_pci_vif_stop(struct rwnx_hw *rwnx_hw, u8 idx);
#else
static inline void rwnx_pci_vif_start(struct rwnx_hw *rwnx_hw, u8 idx) {}
static inline void rwnx_pci_vif_stop(struct rwnx_hw *rwnx_hw, u8 idx) {}
#endif

#ifdef CONFIG_BES_POWER_CTRL_GPIO
struct bes_pci_power_ctrl {
	struct bes_power_ctrl *power_ctrl; /* keep first */
	struct work_struct pci_off_work;
};

int bes_power_ctrl_pci_init(struct rwnx_plat *rwnx_plat);
void bes_power_ctrl_pci_deinit(struct rwnx_plat *rwnx_plat);
void bes_power_ctrl_pci_power_off(struct rwnx_plat *rwnx_plat);
void pci_host_rescan(void);
#else
static inline int bes_power_ctrl_pci_init(struct rwnx_plat *rwnx_plat) { return 0; }
static inline void bes_power_ctrl_pci_deinit(struct rwnx_plat *rwnx_plat) {}
static inline void bes_power_ctrl_pci_power_off(struct rwnx_plat *rwnx_plat) {}
static inline void pci_host_rescan(void) {}
#endif /* CONFIG_BES_POWER_CTRL_GPIO */

#if defined (PLAT_LAPTOP)
void bes_pci_set_dpd_flag(bool dpd_flag);
bool bes_pci_get_dpd_flag(void);
#endif /* PLAT_LAPTOP */

#endif /* _RWNX_PCI_H_ */
