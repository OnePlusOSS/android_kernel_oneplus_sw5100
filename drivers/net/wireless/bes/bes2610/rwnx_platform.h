/**
 ******************************************************************************
 *
 * @file rwnx_platorm.h
 *
 * Copyright (C) RivieraWaves 2012-2021
 *
 ******************************************************************************
 */

#ifndef _RWNX_PLAT_H_
#define _RWNX_PLAT_H_

#include <linux/mmc/sdio_func.h>
#include <linux/kfifo.h>
#include <linux/pci.h>
#include <linux/spi/spi.h>
#include <linux/skbuff.h>
#include <linux/platform_device.h>
#include <linux/hwspinlock.h>
#include "ipc_shared.h"
#if defined(CONFIG_BES_SDIO)
#include "bes_sdio.h"
#elif defined(CONFIG_BES_USB)
#include "bes_usb.h"
#elif defined(CONFIG_BES_IBUS)
#include "bes_ibus.h"
#elif defined(CONFIG_BES_DSPI)
#include "bes_dspi.h"
#elif defined(CONFIG_BES_MSPI)
#include "bes_mspi.h"
#elif defined(CONFIG_RWNX_PCIE)
#include "rwnx_pci.h"
#endif

#ifndef CONFIG_BES_IBUS
#define RWNX_CONFIG_FW_NAME "rwnx_settings.ini"
#else
#define RWNX_CONFIG_FW_NAME "bes2009_rwnx_settings.ini"
#endif
#define RWNX_PHY_CONFIG_TRD_NAME "rwnx_trident.ini"
#define RWNX_PHY_CONFIG_KARST_NAME "rwnx_karst.ini"
#define RWNX_AGC_FW_NAME "agcram.bin"
#define RWNX_LDPC_RAM_NAME "ldpcram.bin"
#define RWNX_CATAXIA_FW_NAME "cataxia.fw"
#ifdef CONFIG_RWNX_SOFTMAC
#define RWNX_MAC_FW_BASE_NAME "lmacfw"
#elif defined CONFIG_RWNX_FULLMAC
#define RWNX_MAC_FW_BASE_NAME "fmacfw"
#elif defined CONFIG_RWNX_FHOST
#define RWNX_MAC_FW_BASE_NAME "fhostfw"
#endif /* CONFIG_RWNX_SOFTMAC */

#ifdef CONFIG_RWNX_TL4
#define RWNX_MAC_FW_NAME RWNX_MAC_FW_BASE_NAME ".hex"
#else
#define RWNX_MAC_FW_NAME RWNX_MAC_FW_BASE_NAME ".ihex"
#define RWNX_MAC_FW_NAME2 RWNX_MAC_FW_BASE_NAME ".bin"
#endif

#define RWNX_FCU_FW_NAME "fcuram.bin"

/**
 * Type of memory to access (cf rwnx_plat.get_address)
 *
 * @RWNX_ADDR_CPU To access memory of the embedded CPU
 * @RWNX_ADDR_SYSTEM To access memory/registers of one subsystem of the
 * embedded system
 *
 */
enum rwnx_platform_addr {
	RWNX_ADDR_CPU,
	RWNX_ADDR_SYSTEM,
#if defined(CONFIG_BES_IBUS)
	RWNX_ADDR_WIFI_SRAM,
#endif
	RWNX_ADDR_MAX,
};

struct rwnx_hw;

#ifdef CONFIG_BES_POWER_CTRL
/* extern */ struct bes_power_ctrl;
#endif

#ifdef CONFIG_PM
#define BES_AP_WAKEUP_TYPE_MASK		0xC
#define BES_AP_WAKEUP_TYPE_SHIFT	2
#define BES_AP_WAKEUP_TYPE_GPIO		0
#define BES_AP_WAKEUP_TYPE_IF		1

#define BES_AP_WAKEUP_CFG_VALID		(0x80)

#define BES_AP_WAKEUP_GPIO_MASK		(0x3)
#define BES_AP_WAKEUP_GPIO_HIGH		(0x0)
#define BES_AP_WAKEUP_GPIO_LOW		(0x1)
#define BES_AP_WAKEUP_GPIO_RISE		(0x2)
#define BES_AP_WAKEUP_GPIO_FALL		(0x3)
#endif

/*
 * Gpio control source
 *
 * Description:
 *     IO_LOCK_SRC_WIFI_LP: wifi lp
 *     IO_LOCK_SRC_HOST_LP: host lp
 */
enum pwr_io_lock_source {
	IO_LOCK_SRC_MIN = 0,
	IO_LOCK_SRC_WIFI_LP = IO_LOCK_SRC_MIN,
	IO_LOCK_SRC_HOST_LP,
	IO_LOCK_SRC_MAX
};

enum rwnx_plat_run_flags_type {
	PLAT_RUN_FLAGS_BUS_RX_PENDING = 0,
	PLAT_RUN_FLAGS_SUSPEND_NOTIFIED,
	PLAT_RUN_FLAGS_BUS_SUSPEND,
	PLAT_RUN_FLAGS_BUS_TX_PENDING,
	PLAT_RUN_FLAGS_TX_DATA,
	PLAT_RUN_FLAGS_TX_MSG,
};

int rwnx_platform_init(struct rwnx_plat *rwnx_plat, void **platform_data);
void rwnx_platform_deinit(struct rwnx_hw *rwnx_hw);

int rwnx_platform_on(struct rwnx_hw *rwnx_hw, void *config);
void rwnx_platform_off(struct rwnx_hw *rwnx_hw, void **config);

int rwnx_platform_register_drv(void);
void bes_platform_unregister_drv(void);

int rwnx_register_net_dev(struct rwnx_plat *rwnx_plat);
int rwnx_unregister_net_dev(struct rwnx_plat *rwnx_plat);
bool rwnx_net_dev_created(struct rwnx_plat *rwnx_plat);

int bes_wait_dev_status(struct rwnx_plat *rwnx_plat, u32 rd_status, bool target_val, u32 timeout);

static inline void rwnx_run_flags_set_bit(struct rwnx_plat *rwnx_plat, unsigned long nr)
{
	unsigned long flags;

	spin_lock_irqsave(&rwnx_plat->run_flags_lock, flags);
	rwnx_plat->run_flags |= BIT(nr);
	spin_unlock_irqrestore(&rwnx_plat->run_flags_lock, flags);
}

static inline void rwnx_run_flags_clear_bit(struct rwnx_plat *rwnx_plat, unsigned long nr)
{
	unsigned long flags;

	spin_lock_irqsave(&rwnx_plat->run_flags_lock, flags);
	rwnx_plat->run_flags &= ~BIT(nr);
	spin_unlock_irqrestore(&rwnx_plat->run_flags_lock, flags);
}

static inline bool rwnx_run_flags_test_bit(struct rwnx_plat *rwnx_plat, unsigned long nr)
{
	unsigned long flags;
	bool ret;

	spin_lock_irqsave(&rwnx_plat->run_flags_lock, flags);
	ret = !!(rwnx_plat->run_flags & BIT(nr));
	spin_unlock_irqrestore(&rwnx_plat->run_flags_lock, flags);

	return ret;
}

#ifdef CONFIG_PM
enum RWNX_PLAT_PM_LOCK_USER {
	TX_WAKELOCK = 0,
	RX_WAKELOCK,
	WAKLOCK_MAX,
};

int rwnx_plat_notify_host_suspend(struct rwnx_plat *rwnx_plat);
int rwnx_plat_notify_host_resume(struct rwnx_plat *rwnx_plat);

void rwnx_platform_pm_stay_awake(struct rwnx_plat *rwnx_plat, enum RWNX_PLAT_PM_LOCK_USER user);
void rwnx_platform_pm_relax(struct rwnx_plat *rwnx_plat, enum RWNX_PLAT_PM_LOCK_USER user);
#endif
void rwnx_platform_wakeup_mcu(struct rwnx_plat *rwnx_plat, enum pwr_io_lock_source src, bool wait);
void rwnx_platform_sleep_mcu(struct rwnx_plat *rwnx_plat, enum pwr_io_lock_source src, bool wait);

static inline struct device *rwnx_platform_get_dev(struct rwnx_plat *rwnx_plat)
{
#ifdef CONFIG_BES_SDIO
	return &(rwnx_plat->func->dev);
#elif defined(CONFIG_BES_USB)
	return rwnx_plat->usb_dev;
#elif defined(CONFIG_BES_IBUS)
	return &(rwnx_plat->pdev->dev);
#elif defined(CONFIG_BES_DSPI)
	return &(rwnx_plat->spi_dev->dev);
#elif defined(CONFIG_BES_MSPI)
	return &(rwnx_plat->spi_dev->dev);
#else
	return &(rwnx_plat->pci_dev->dev);
#endif
}

#if defined(CONFIG_RWNX_PCIE)
#define rwnx_platform_get_irq(rwnx_plat) rwnx_plat->pci_dev->irq
#elif defined(CONFIG_BES_IBUS)
#define rwnx_platform_get_irq(rwnx_plat) ((struct bes_soc *)(rwnx_plat->priv))->irq
#endif

struct rwnx_plat *rwnx_get_plat_data(void);

#endif /* _RWNX_PLAT_H_ */
