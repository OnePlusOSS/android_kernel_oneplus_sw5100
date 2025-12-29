/*
 * @file bes_soc.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "bes_soc.h"
#include "hal_desc.h"
#include "reg_access.h"
#include "rwnx_defs.h"
#include "rwnx_irqs.h"
#include "bes_hwio.h"
#include "bes_ibus.h"

static int bes_soc_platform_enable(struct rwnx_hw *rwnx_hw)
{
	int ret;
	struct bes_soc *bes_soc = (struct bes_soc *)(rwnx_hw->plat->priv);

	/* sched_setscheduler on ONESHOT threaded irq handler for BCNs ? */
	ret = devm_request_irq(&rwnx_hw->plat->pdev->dev, bes_soc->irq, rwnx_irq_hdlr, 0, "rwnx", rwnx_hw);
	return ret;
}

static int bes_soc_platform_disable(struct rwnx_hw *rwnx_hw)
{
	struct bes_soc *bes_soc = (struct bes_soc *)(rwnx_hw->plat->priv);
	devm_free_irq(&rwnx_hw->plat->pdev->dev, bes_soc->irq, rwnx_hw);
	return 0;
}

static void bes_soc_platform_deinit(struct rwnx_plat *rwnx_plat) {}

static u8 *bes_soc_get_address(struct rwnx_plat *rwnx_plat, int addr_name, unsigned int offset)
{
	struct bes_soc *bes_soc = (struct bes_soc *)(rwnx_plat->priv);

	if (WARN(addr_name >= RWNX_ADDR_MAX, "Invalid address %d", addr_name))
		return NULL;

	if (addr_name == RWNX_ADDR_SYSTEM || addr_name == RWNX_ADDR_CPU)
		return bes_soc->mem_vaddr + offset;
	else if (addr_name == RWNX_ADDR_WIFI_SRAM)
		return bes_soc->sram_vaddr + offset;
	else
		return NULL;
}

static void bes_soc_ack_irq(struct rwnx_plat *rwnx_plat) {}

static const u32 bes_soc_config_reg[] = {
	NXMAC_DEBUG_PORT_SEL_ADDR,
	SYSCTRL_DIAG_CONF_ADDR,
	SYSCTRL_PHYDIAG_CONF_ADDR,
	SYSCTRL_RIUDIAG_CONF_ADDR,
	RF_V7_DIAGPORT_CONF1_ADDR,
};

static const u32 bes_soc_he_config_reg[] = {
	SYSCTRL_DIAG_CONF0,
	SYSCTRL_DIAG_CONF1,
	SYSCTRL_DIAG_CONF2,
	SYSCTRL_DIAG_CONF3,
};

static int bes_soc_get_config_reg(struct rwnx_plat *rwnx_plat, const u32 **list)
{
	u32 fpga_sign;

	if (!list)
		return 0;

	fpga_sign = RWNX_REG_READ(rwnx_plat, RWNX_ADDR_SYSTEM, SYSCTRL_SIGNATURE_ADDR);
	if (__FPGA_TYPE(fpga_sign) == 0xc0ca) {
		*list = bes_soc_he_config_reg;
		return ARRAY_SIZE(bes_soc_he_config_reg);
	} else {
		*list = bes_soc_config_reg;
		return ARRAY_SIZE(bes_soc_config_reg);
	}
}

/**
 * bes_soc_platform_init - Initialize the DINI platform
 *
 * @platform_device platform device
 * @rwnx_plat Pointer on struct rwnx_stat * to be populated
 *
 * @return 0 on success, < 0 otherwise
 *
 * Allocate and initialize a rwnx_plat structure for the dini platform.
 */
int bes_soc_platform_init(struct platform_device *pdev, struct rwnx_plat **rwnx_plat)
{
	struct bes_soc *bes_soc;
	int ret = 0;

	bes_soc = (struct bes_soc *)(*rwnx_plat)->priv;
	bes_soc->irq = platform_get_irq(pdev, 0);
	if (bes_soc->irq < 0) {
		ret = -EFAULT;
		goto err;
	}

	bes_soc->mem_vaddr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(bes_soc->mem_vaddr)) {
		ret = -ENOMEM;
		goto err;
	}

	bes_soc->sram_vaddr = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(bes_soc->sram_vaddr)) {
		ret = PTR_ERR(bes_soc->sram_vaddr);
		goto err1;
	}

	bes_soc->lp_lock = devm_hwspin_lock_request_specific(&pdev->dev, 1);
	if (!bes_soc->lp_lock) {
		ret = -EBUSY;
		goto err2;
	}

	bes_soc->sys2wf_reg_base = devm_ioremap(&pdev->dev, SYS2WF_REG_BASE, SYS2WF_REG_SIZE);
	if (!bes_soc->sys2wf_reg_base) {
		ret = -ENOMEM;
		goto err3;
	}

	bes_soc->lp_status = devm_ioremap(&pdev->dev, WIFI_LP_STATUS_REG, 4);
	if (!bes_soc->lp_status) {
		ret = -ENOMEM;
		goto err4;
	}

#ifdef CONFIG_BES_POWER_CTRL
	((struct bes_ibus_power_ctrl *)((*rwnx_plat)->power_priv))->priv = bes_soc;
#endif

	(*rwnx_plat)->enable = bes_soc_platform_enable;
	(*rwnx_plat)->disable = bes_soc_platform_disable;
	(*rwnx_plat)->deinit = bes_soc_platform_deinit;
	(*rwnx_plat)->get_address = bes_soc_get_address;
	(*rwnx_plat)->ack_irq = bes_soc_ack_irq;
	(*rwnx_plat)->get_config_reg = bes_soc_get_config_reg;

	return 0;

err4:
	devm_iounmap(&pdev->dev, bes_soc->sys2wf_reg_base);
err3:
	devm_hwspin_lock_free(&pdev->dev, bes_soc->lp_lock);
err2:
	devm_iounmap(&pdev->dev, bes_soc->sram_vaddr);
err1:
	devm_iounmap(&pdev->dev, bes_soc->mem_vaddr);
err:
	return ret;
}
