/**
 ******************************************************************************
 *
 * @file rwnx_irqs.c
 *
 * Copyright (C) RivieraWaves 2012-2021
 *
 ******************************************************************************
 */
#include <linux/interrupt.h>

#include "rwnx_defs.h"
#include "rwnx_prof.h"
#if defined(CONFIG_BES_IBUS)
#include "bes_soc.h"
#endif

/**
 * rwnx_irq_hdlr - IRQ handler
 *
 * Handler registerd by the platform driver
 */
irqreturn_t rwnx_irq_hdlr(int irq, void *dev_id)
{
	struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)dev_id;
	disable_irq_nosync(irq);
	tasklet_schedule(&rwnx_hw->task);
	return IRQ_HANDLED;
}

/**
 * rwnx_task - Bottom half for IRQ handler
 *
 * Read irq status and process accordingly
 */
void rwnx_task(unsigned long data)
{
	struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)data;
	struct rwnx_plat *rwnx_plat = rwnx_hw->plat;
	u32 status;

	REG_SW_SET_PROFILING(rwnx_hw, SW_PROF_RWNX_IPC_IRQ_HDLR);

	/* Ack unconditionnally in case ipc_host_get_status does not see the irq */
	rwnx_plat->ack_irq(rwnx_plat);
process:
	while ((status = ipc_host_get_status(rwnx_hw->ipc_env))) {
		/* All kinds of IRQs will be handled in one shot (RX, MSG, DBG, ...)
		 * this will ack IPC irqs not the cfpga irqs */
		ipc_host_irq(rwnx_hw->ipc_env, status);

		rwnx_plat->ack_irq(rwnx_plat);
	}

	spin_lock_bh(&rwnx_hw->tx_lock);
	rwnx_hwq_process_all(rwnx_hw);
	spin_unlock_bh(&rwnx_hw->tx_lock);

	if (ipc_host_get_status(rwnx_hw->ipc_env)) {
		goto process;
	}

	rwnx_ipc_stop(rwnx_hw);
	enable_irq(rwnx_platform_get_irq(rwnx_plat));
	rwnx_ipc_start(rwnx_hw);

	REG_SW_CLEAR_PROFILING(rwnx_hw, SW_PROF_RWNX_IPC_IRQ_HDLR);
}
