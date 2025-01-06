/**
 ******************************************************************************
 *
 * @file rwnx_pci.c
 *
 * Copyright (C) RivieraWaves 2012-2021
 *
 ******************************************************************************
 */
#include <linux/module.h>
#include <linux/pci.h>

#include "rwnx_defs.h"
#include "rwnx_dini.h"
#include "bes_sysfs.h"
#include "rwnx_v7.h"
#include "bes_log.h"
#include "rwnx_msg_tx.h"
#include "bes_wowlan.h"
#include "rwnx_pci.h"
#include "bes_filter.h"
#include "reg_access.h"
#include "bes_dbac_cali.h"

#define PCI_VENDOR_ID_BESGROUP 0xBE57
#define BES_ADDRESS_0_DEF 0x60000000

#define PCI_VENDOR_ID_DINIGROUP 0x17DF
#define PCI_DEVICE_ID_DINIGROUP_DNV6_F2PCIE 0x1907

#define PCI_DEVICE_ID_XILINX_CEVA_VIRTEX7 0x7011

static const struct pci_device_id rwnx_pci_ids[] = {
	{PCI_DEVICE(PCI_VENDOR_ID_DINIGROUP, PCI_DEVICE_ID_DINIGROUP_DNV6_F2PCIE)},
	{PCI_DEVICE(PCI_VENDOR_ID_XILINX, PCI_DEVICE_ID_XILINX_CEVA_VIRTEX7)},
	{PCI_DEVICE(PCI_VENDOR_ID_XILINX, 0X8011)},
	{PCI_DEVICE(PCI_VENDOR_ID_BESGROUP, 0x2005)},
	{PCI_DEVICE(PCI_VENDOR_ID_BESGROUP, 0x2007)},
	{0,}
};

static int bes_pci_card_attend(struct rwnx_plat *rwnx_plat);
static int bes_pci_card_leave(struct rwnx_plat *rwnx_plat);
static void rwnx_pci_remove(struct pci_dev *pci_dev);
static int rwnx_pci_probe(struct pci_dev *pci_dev, const struct pci_device_id *pci_id);

void bes_io_bus_status_dump(void)
{
}

#if defined (PLAT_LAPTOP)
/* get dpd flag */
bool rwnx_dpd_flag;
module_param_named(rwnx_dpd_flag, rwnx_dpd_flag, bool, 0644);

void bes_pci_set_dpd_flag(bool dpd_flag)
{
	rwnx_dpd_flag = dpd_flag;
}

bool bes_pci_get_dpd_flag(void)
{
	return rwnx_dpd_flag;
}
#endif

#ifndef PLAT_LAPTOP
static int bes_pci_card_power_up(struct rwnx_plat *rwnx_plat)
{
	int ret = 0;

	if ((ret = bes_chip_power(rwnx_plat, false)) < 0)
		return ret;
	if ((ret = bes_chip_power(rwnx_plat, true)) < 0)
		return ret;
	msleep(1000); /* ensure scan success */
	bes_power_ctrl_start_bus_probe();
	pci_host_rescan();

	return ret;
}

static int bes_pci_card_power_down(struct rwnx_plat *rwnx_plat)
{
	if (rwnx_plat->pci_dev) {
		pci_lock_rescan_remove();
		pci_stop_and_remove_bus_device(rwnx_plat->pci_dev);
		pci_unlock_rescan_remove();
		rwnx_plat->pci_dev = NULL; /* only set NULL in here */
	}

	return bes_chip_power(rwnx_plat, false);
}
#endif

#ifdef CONFIG_BES_POWER_CTRL_GPIO

void pci_host_rescan(void)
{
	struct pci_bus *b = NULL;

	pci_lock_rescan_remove();
	while ((b = pci_find_next_bus(b)) != NULL)
		pci_rescan_bus(b);
	pci_unlock_rescan_remove();
}

static void bes_pci_bus_remove(void)
{
	int i;
	struct pci_dev *pci_dev = NULL;

	pci_lock_rescan_remove();
	for (i = 0; i < ARRAY_SIZE(rwnx_pci_ids) - 1; i++) {
		if (rwnx_pci_ids[i].vendor != 0 &&
		   (pci_dev = pci_get_device(rwnx_pci_ids[i].vendor, rwnx_pci_ids[i].device, NULL))) {
			pci_stop_and_remove_bus_device(pci_dev);
		}
	}
	pci_unlock_rescan_remove();
}

static int bes_pci_plat_power(struct rwnx_plat *rwnx_plat, bool on)
{
	int ret = 0;

	if (on) {
		ret = bes_pci_card_attend(rwnx_plat);
	} else {
		ret = bes_pci_card_leave(rwnx_plat);
	}

	return ret;
}

static void bes_pci_off_work(struct work_struct *ws)
{
	bes_power_ctrl_do_system_close();
}

/**
 * When both wifi and bt are closed,
 * the probe function calls this interface to execute the disconnect method.
 */
void bes_power_ctrl_pci_power_off(struct rwnx_plat *rwnx_plat)
{
	struct bes_power_state power_state;
	struct bes_pci_power_ctrl *bes_pci_power_ctrl;

	bes_pci_power_ctrl = rwnx_plat->power_priv;

	bes_power_ctrl_get_power_state(&power_state);

	if (power_state.wifi_power || power_state.bt_power)
		return;

	schedule_work(&bes_pci_power_ctrl->pci_off_work);
}

int bes_power_ctrl_pci_init(struct rwnx_plat *rwnx_plat)
{
	struct bes_pci_power_ctrl *bes_pci_power_ctrl;

	rwnx_plat->power_priv = bes_power_ctrl_gpio_register(rwnx_plat,
							     sizeof(struct bes_pci_power_ctrl),
							     bes_pci_plat_power);
	if (!rwnx_plat->power_priv) {
		bes_err(BES_DBG_PCI, "%s %d, failed\n", __func__, __LINE__);
		return -ENOMEM;
	}

	bes_pci_power_ctrl = rwnx_plat->power_priv;
	INIT_WORK(&bes_pci_power_ctrl->pci_off_work, bes_pci_off_work);

	bes_pci_bus_remove();

	return 0;
}

void bes_power_ctrl_pci_deinit(struct rwnx_plat *rwnx_plat)
{
	struct bes_pci_power_ctrl *bes_pci_power_ctrl = rwnx_plat->power_priv;

	cancel_work_sync(&bes_pci_power_ctrl->pci_off_work);

	bes_power_ctrl_gpio_unregister(rwnx_plat->power_priv);
}
#endif /* CONFIG_BES_POWER_CTRL_GPIO */

#if defined (PLAT_ROCKCHIP)
extern int rockchip_wifi_get_oob_irq_flag(void);
extern int rockchip_wifi_get_oob_irq(void);
#endif

#if !defined (PLAT_LAPTOP)
bool wlan_hostwake_registered = false; /* wifi request_irq success or not */

irqreturn_t wlan_hostwake_thread(int irq, void *data)
{
	struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)(data);

	bes_err(BES_DBG_PCI, RWNX_FN_ENTRY_STR);
	bes_wowlan_set_wakeup_source(rwnx_hw, true);
	disable_irq_nosync(irq);

	return IRQ_HANDLED;
}

static int wlan_hostwake_register(struct rwnx_hw *rwnx_hw)
{
	int ret = 0;
#if defined (PLAT_ROCKCHIP)
	int irq_flags = rockchip_wifi_get_oob_irq_flag();
	int irq = rockchip_wifi_get_oob_irq();
#endif
	bes_dbg(BES_DBG_PCI, RWNX_FN_ENTRY_STR);

	if (wlan_hostwake_registered == true) {
		bes_err(BES_DBG_PCI, "wlan hostwake register repeatedly.");
		return -EBUSY;
	}

#if defined (PLAT_ROCKCHIP)
	irq_flags |= IRQF_ONESHOT;
	irq_flags &= (~IRQF_NO_SUSPEND);
	if (((ret = request_threaded_irq(irq, NULL, wlan_hostwake_thread,
	      irq_flags, "rwnx_wlan_hostwake", rwnx_hw)) != 0) ||
	    ((ret = enable_irq_wake(irq)) != 0)) {
		bes_err(BES_DBG_PCI, "request_irq or irq_wake failed with %d\n", ret);
		return ret;
	}
#endif

	wlan_hostwake_registered = true;

	return ret;
}

static void wlan_hostwake_unregister(struct rwnx_hw *self)
{
#if defined (PLAT_ROCKCHIP)
	int ret = 0;
	int irq = rockchip_wifi_get_oob_irq();
#endif
	bes_dbg(BES_DBG_PCI, RWNX_FN_ENTRY_STR);

	if (wlan_hostwake_registered == false) {
		return;
	}

	wlan_hostwake_registered = false;
#if defined (PLAT_ROCKCHIP)
	ret = disable_irq_wake(irq);
	if (ret) {
		bes_err(BES_DBG_PCI, "disable_irq_wake failed with %d\n", ret);
	}
	free_irq(irq, self);
#endif
}
#endif

/*for PCI configuration register, bar address is not same between by xxx and vvv*/
static void rwnx_pci_set_config_addr(struct pci_dev *pci_dev)
{
	u32 i, addr0;

	pci_read_config_dword(pci_dev, PCI_BASE_ADDRESS_0, &addr0);
	if(addr0 != BES_ADDRESS_0_DEF) {
		bes_err(BES_DBG_PCI, "%s:%d, config bar0 addr=0x%x\n", __func__, __LINE__, addr0);
		return;
	}
	for (i = 0; i < PCI_ROM_RESOURCE; i++) {
		pci_write_config_dword(pci_dev, PCI_BASE_ADDRESS_0 + i*4, pci_resource_start(pci_dev, i));
	}
	pci_write_config_dword(pci_dev, PCI_ROM_ADDRESS, pci_resource_start(pci_dev, PCI_ROM_RESOURCE));
}

#if defined (PLAT_ROCKCHIP)
u32 vif_up = 0;

void rwnx_pci_vif_start(struct rwnx_hw *rwnx_hw, u8 idx) /* reference to bes_pwr_start */
{
	/* Determine whether device is started */
	if (!test_bit(RWNX_DEV_STARTED, &rwnx_hw->flags)) {
		bes_err(BES_DBG_PWR, "%s wifi lp start error, device not started\n", __func__);
		return;
	}

	vif_up |= BIT(idx);
}

void rwnx_pci_vif_stop(struct rwnx_hw *rwnx_hw, u8 idx) /* reference to bes_pwr_stop */
{
	/* Determine whether wifi lp is start */
	if (!vif_up) {
		return;
	}

	vif_up &= ~BIT(idx);
}

static int rwnx_pci_save_cfgregs(struct pci_dev *pci_dev, u32 *buffer, u32 len)
{
	int i;

	for (i = 0; i < len; i++)
		pci_read_config_dword(pci_dev, i * 4, &buffer[i]);
	return 0;
}

static int rwnx_pci_restore_cfgregs(struct pci_dev *pci_dev, u32 *buffer, u32 len)
{
	int i;

	for (i = 0; i < len; i++)
		pci_write_config_dword(pci_dev, i * 4, buffer[i]);
	return 0;
}
#endif

#if defined (PLAT_LAPTOP)
static void rwnx_pci_suspend_laptop(struct pci_dev *pci_dev)
{
	rwnx_pci_remove(pci_dev);
}

static int rwnx_pci_resume_laptop(struct pci_dev *pci_dev)
{
	struct pci_device_id pci_id = {0};
	pci_id.vendor = pci_dev->vendor;
	pci_id.device = pci_dev->device;
	return  rwnx_pci_probe(pci_dev, &pci_id);
}
#endif

static int rwnx_pci_suspend(struct pci_dev *pci_dev, pm_message_t state)
{
	int ret = 0;
#if defined (PLAT_ROCKCHIP)
	struct rwnx_hw *rwnx_hw = pci_get_drvdata(pci_dev);
	struct rwnx_plat *rwnx_plat = rwnx_hw->plat;
	int i = 0;
#endif
	bes_info(BES_DBG_PCI, RWNX_FN_ENTRY_STR);
#if defined (PLAT_ROCKCHIP)
	rwnx_pci_save_cfgregs(pci_dev, rwnx_plat->pcie_cfgregs, PCIE_CONFIG_SPACE_LEN);
	bes_dbg(BES_DBG_PCI,"%s dump pci config space\n",__func__);
	for (i = 0; i < PCIE_CONFIG_SPACE_LEN; i++)
		bes_dbg(BES_DBG_PCI,"0x%x: 0x%x\n", i * 4, rwnx_plat->pcie_cfgregs[i]);

	ret = wlan_hostwake_register(rwnx_hw);
	if (ret) {
		bes_err(BES_DBG_PCI, "Error: wlan hostwake register: %d\n", ret);
		return ret;
	}
	bes_wowlan_set_wakeup_source(rwnx_hw, false);

	/* Mark bus enter suspend state */
	rwnx_run_flags_set_bit(rwnx_plat, PLAT_RUN_FLAGS_BUS_SUSPEND);

	pci_save_state(pci_dev);

	//pci_enable_wake(pci_dev, PCI_D3hot, true);
	rwnx_ipc_stop(rwnx_hw);
	synchronize_irq(pci_dev->irq);
	disable_irq_nosync(pci_dev->irq);
#elif defined (PLAT_LAPTOP)
	rwnx_pci_suspend_laptop(pci_dev);
#endif

	if (pci_is_enabled(pci_dev))
		pci_disable_device(pci_dev);
#if 0
	ret = pci_set_power_state(pci_dev, PCI_D3hot);
	if (ret) {
		bes_err(BES_DBG_PCI,"%s: pci_set_power_state error %d\n",__FUNCTION__, ret);
	}
#endif
	bes_info(BES_DBG_PCI,"%s suspend done\n",__func__);
	return ret;
}

static int rwnx_pci_resume(struct pci_dev *pci_dev)
{
	int ret = 0;
#if defined (PLAT_ROCKCHIP)
	struct rwnx_hw *rwnx_hw;
	struct rwnx_plat *rwnx_plat;
	int i;

	bes_info(BES_DBG_PCI, RWNX_FN_ENTRY_STR);
	rwnx_hw = pci_get_drvdata(pci_dev);
	rwnx_plat = rwnx_hw->plat;
	bes_dbg(BES_DBG_PCI,"%s:%d, state:%d, state_saved:%d, msi_enabled:%d,msix_enabled:%d\n",
	__func__, __LINE__, pci_dev->current_state, pci_dev->state_saved,pci_dev->msi_enabled,pci_dev->msix_enabled);
	pci_dev->state_saved = true;
	pci_dev->msi_enabled = true;
	pci_dev->current_state = 0;

	/* Mark bus exit suspend state */
	rwnx_run_flags_clear_bit(rwnx_plat, PLAT_RUN_FLAGS_BUS_SUSPEND);

	pci_restore_state(pci_dev);
	rwnx_pci_restore_cfgregs(pci_dev, rwnx_plat->pcie_cfgregs, PCIE_CONFIG_SPACE_LEN);

	for (i = 0; i < PCIE_CONFIG_SPACE_LEN; i++)
		rwnx_plat->pcie_cfgregs[i] = 0x0;
	rwnx_pci_save_cfgregs(pci_dev, rwnx_plat->pcie_cfgregs, PCIE_CONFIG_SPACE_LEN);
	bes_dbg(BES_DBG_PCI,"%s dump pci config space\n",__func__);
	for (i = 0; i < PCIE_CONFIG_SPACE_LEN; i++)
		bes_dbg(BES_DBG_PCI,"0x%x: 0x%x\n", i * 4, rwnx_plat->pcie_cfgregs[i]);
	ret = pci_enable_device(pci_dev);
	if (ret) {
		bes_err(BES_DBG_PCI, "%s:pci_enable_device error %d \n", __FUNCTION__, ret);
	}

#if 0
	ret = pci_set_power_state(pci_dev, PCI_D0);
	if (ret) {
		bes_err(BES_DBG_PCI, "%s:pci_set_power_state error %d \n", __FUNCTION__, ret);
	}
#endif
	enable_irq(pci_dev->irq);
	rwnx_ipc_start(rwnx_hw);
	wlan_hostwake_unregister(rwnx_hw);
#elif defined (PLAT_LAPTOP)
	bes_pci_set_dpd_flag(true);
	ret = rwnx_pci_resume_laptop(pci_dev);
#endif
	bes_info(BES_DBG_PCI,"%s resume done\n",__func__);
	return ret;
}

/* Uncomment this for depmod to create module alias */
/* We don't want this on development platform */
// MODULE_DEVICE_TABLE(pci, rwnx_pci_ids);

static int rwnx_pci_probe(struct pci_dev *pci_dev, const struct pci_device_id *pci_id)
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();
	struct bes_power_state power_state;
	void *drvdata;
	int ret = -ENODEV;

	bes_dbg(BES_DBG_PCI, RWNX_FN_ENTRY_STR);

	bes_sysfs_set_device_id(pci_id->device);

	/* set download state, and notity enter probe */
	bes_set_boot_wait_state(BES_BOOT_WAIT_DOWNLOAD_FW);
	bes_power_ctrl_bus_probe_notify();

	rwnx_pci_set_config_addr(pci_dev);

	if (pci_id->vendor == PCI_VENDOR_ID_DINIGROUP) {
		ret = rwnx_dini_platform_init(pci_dev, &rwnx_plat);
	} else if (pci_id->vendor == PCI_VENDOR_ID_XILINX || pci_id->vendor == PCI_VENDOR_ID_BESGROUP) {
		ret = rwnx_v7_platform_init(pci_dev, &rwnx_plat);
	}

	if (ret)
		goto pci_init_err;

	rwnx_plat->pci_dev = pci_dev;
	ret = rwnx_platform_init(rwnx_plat, &drvdata);
	if (ret)
		goto plat_err;

	pci_set_drvdata(pci_dev, drvdata);

	rwnx_plat->wakelock_flag = 0;
	rwnx_plat->dev = &pci_dev->dev;
	rwnx_plat->run_flags = 0;
	spin_lock_init(&rwnx_plat->run_flags_lock);

	/* handle the power down and unregister wlan case */
	bes_power_ctrl_get_power_state(&power_state);

	if (bes_sysfs_is_signal_mode() && !power_state.wifi_power)
		bes_unregister_net_dev_after_get_dpd((struct rwnx_hw *)(rwnx_plat->drvdata));

	if (!power_state.wifi_power && !power_state.bt_power) {
		bes_set_boot_wait_state(BES_BOOT_WAIT_PROBE_CLOSE);
		bes_power_ctrl_pci_power_off(rwnx_plat);
		return 0;
	}

	if ((ret = bes_power_ctrl_system_enabled(true, false)) < 0)
		goto plat_err;

	return 0;

plat_err:
	rwnx_plat->deinit(rwnx_plat);
	pci_set_drvdata(pci_dev, NULL);
pci_init_err:
	bes_set_boot_wait_state(BES_BOOT_WAIT_PROBE_CLOSE);

	bes_power_ctrl_system_enabled(false, true);
	bes_power_ctrl_pci_power_off(rwnx_plat);

	return 0;
}

static void rwnx_pci_remove(struct pci_dev *pci_dev)
{
	struct rwnx_hw *rwnx_hw;
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();

	bes_dbg(BES_DBG_PCI, RWNX_FN_ENTRY_STR);

	rwnx_hw = rwnx_plat->drvdata;
	if (rwnx_hw) {
		rwnx_platform_deinit(rwnx_hw);
		if (!rwnx_plat->pre_alloc)
			rwnx_plat->drvdata = NULL;
	}

	if (pci_get_drvdata(pci_dev)) {
		rwnx_plat->deinit(rwnx_plat);
		pci_set_drvdata(pci_dev, NULL);
	}
}

static struct pci_driver rwnx_pci_drv = {
	.name = KBUILD_MODNAME,
	.id_table = rwnx_pci_ids,
	.probe = rwnx_pci_probe,
	.remove = rwnx_pci_remove,
	.suspend = rwnx_pci_suspend,
	.resume = rwnx_pci_resume
};

static int bes_pci_card_attend(struct rwnx_plat *rwnx_plat)
{
	int ret = 0;

	/**
	 * If remove() is not called by pci_register_driver(),
	 * pci_register_driver() cannot be called when entering again.
	 */
	if (!rwnx_plat->register_flag) {
		ret = pci_register_driver(&rwnx_pci_drv);
		bes_dbg(BES_DBG_PCI, "%s ret %d\n", __func__, ret);
		if (ret)
			return ret;
	}

	rwnx_plat->register_flag = true;
#ifndef PLAT_LAPTOP
	bes_pci_card_power_up(rwnx_plat);
#endif
	return ret;
}

static int bes_pci_card_leave(struct rwnx_plat *rwnx_plat)
{
	int ret = 0;

	/* pci device has been powered down, and return 0. */
	if (!rwnx_plat->register_flag)
		return 0;

	pci_unregister_driver(&rwnx_pci_drv);
#ifndef PLAT_LAPTOP
	ret = bes_pci_card_power_down(rwnx_plat);
#endif
	bes_power_ctrl_system_enabled(false, false);

	rwnx_plat->register_flag = false;

	return ret;
}

int rwnx_pci_register_drv(void)
{
	int ret;
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();

	bes_dbg(BES_DBG_PCI, "%s enter\n", __func__);

	/* keep first */
	ret = bes_power_ctrl_pci_init(rwnx_plat);
	if (ret)
		return ret;

	ret = bes_sysfs_register(rwnx_plat);
	if (ret)
		goto register_sysfs_err;

	bes_sysfs_status_update(true);

	ret = bes_pci_card_attend(rwnx_plat);
	bes_info(BES_DBG_PCI, "%s ret %d\n", __func__, ret);
	if (ret)
		goto pci_register_err;

	return ret;

pci_register_err:
	bes_power_ctrl_cancel_probe_work();
	bes_sysfs_unregister();
register_sysfs_err:
	bes_power_ctrl_pci_deinit(rwnx_plat);

	return ret;
}

void rwnx_pci_unregister_drv(void)
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();

	bes_dbg(BES_DBG_PCI, "%s enter\n", __func__);

	bes_power_ctrl_prep_rmmod(rwnx_plat);
	bes_pci_card_leave(rwnx_plat);

	bes_sysfs_unregister();
	bes_power_ctrl_pci_deinit(rwnx_plat);

	bes_info(BES_DBG_PCI, "%s exit\n", __func__);
}
