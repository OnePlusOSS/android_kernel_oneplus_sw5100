// SPDX-License-Identifier: GPL-2.0-only
/*
* Copyright (C) 2019-2023 Oplus. All rights reserved.
*/


#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/syscalls.h>
#include <linux/fs.h>
#include <linux/version.h>
#include <linux/of_fdt.h>
#include <linux/of.h>

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/setup.h>
//#include "../include/oplus_cmdline_parser.h"

#define MAX_CMDLINE_PARAM_LEN 1024

char cdt[MAX_CMDLINE_PARAM_LEN];

EXPORT_SYMBOL(cdt);

module_param_string(cdt_intergrity, cdt, MAX_CMDLINE_PARAM_LEN, 0600);
MODULE_PARM_DESC(cdt_intergrity, "cdt_intergrity=<cdt_intergrity>");


//#define MAX_CMDLINE_PARAM_LEN 1024
//char charger_present[MAX_CMDLINE_PARAM_LEN];
//
//EXPORT_SYMBOL(charger_present);

//#define MAX_CMDLINE_PARAM_LEN 1024
char oplus_ftm_mode[MAX_CMDLINE_PARAM_LEN];

EXPORT_SYMBOL(oplus_ftm_mode);

module_param_string(ftm_mode, oplus_ftm_mode, MAX_CMDLINE_PARAM_LEN, 0600);
MODULE_PARM_DESC(oplus_ftm_mode, "oplus.ftm_mode=<oplus_ftm_mode>");


//#define MAX_CMDLINE_PARAM_LEN 1024
char saupwk_enable[MAX_CMDLINE_PARAM_LEN];

EXPORT_SYMBOL(saupwk_enable);

module_param_string(en, saupwk_enable, MAX_CMDLINE_PARAM_LEN, 0600);
MODULE_PARM_DESC(en, "en=<en>");

//#define MAX_CMDLINE_PARAM_LEN 1024
char shutdown_force_panic[MAX_CMDLINE_PARAM_LEN];

EXPORT_SYMBOL(shutdown_force_panic);

module_param_string(force_panic, shutdown_force_panic, MAX_CMDLINE_PARAM_LEN, 0600);
MODULE_PARM_DESC(force_panic, "shutdown_speed.force_panic=<force_panic>");

//#define MAX_CMDLINE_PARAM_LEN 1024
char sim_card_num[MAX_CMDLINE_PARAM_LEN];

EXPORT_SYMBOL(sim_card_num);

module_param_string(simcardnum, sim_card_num, MAX_CMDLINE_PARAM_LEN, 0600);
MODULE_PARM_DESC(simcardnum, "simcardnum.simcardnum=<doublesim>");

#if IS_ENABLED(CONFIG_BCMDHD)
char oplus_wlan_mac_str[MAX_CMDLINE_PARAM_LEN];

EXPORT_SYMBOL_NS(oplus_wlan_mac_str, OPLUS_WLAN_EXPORT_ONLY);

module_param_string(bcmmac, oplus_wlan_mac_str, MAX_CMDLINE_PARAM_LEN, 0600);
MODULE_PARM_DESC(oplus_wlan_mac_str, "oplus_wlan_mac_str");
#endif

char *bootargs_ptr = NULL;
EXPORT_SYMBOL(bootargs_ptr);

/*****************************************************************************/
//oplus.xxx
static uint sec_status = 0;
static uint hw_id = 0;
static uint board_id = 0;
static uint board_type = 0;
static uint panel_id = 0x108004;
static uint panel_present = 1;
static uint dsi_relock_en = 1;  /*dsi pll relock failed flag*/
static uint display_vflip = 0;
static uint tp_wakeup_en = 0;
static uint uart_enable   = 0;
static uint boot_silence = 0;

static uint user_printk_disable_uart = 1;
static uint is_user_build_variant = 0;
static uint is_recovery_mode = 0;
/*static uint deep_dischrg_count = 0;*/
static uint clikshutdown = 0;

/******************************************************************************/
module_param(sec_status, uint, 0444);
MODULE_PARM_DESC(hw_id, "Oplus Secboot Status");

module_param(hw_id, uint, 0444);
MODULE_PARM_DESC(hw_id, "Oplus Hw Id");

module_param(board_id, uint, 0444);
MODULE_PARM_DESC(board_id, "Oplus Board Id");

module_param(board_type, uint, 0444);
MODULE_PARM_DESC(board_type, "Oplus Board Type");

module_param(panel_id, uint, 0444);
MODULE_PARM_DESC(panel_id, "Oplus Panel Id");

module_param(panel_present, uint, 0444);
MODULE_PARM_DESC(panel_present, "Oplus Panel Present");

module_param(dsi_relock_en, uint, 0444);
MODULE_PARM_DESC(dsi_relock_en, "Oplus Dsi Relock En");

module_param(display_vflip, uint, 0444);
MODULE_PARM_DESC(display_vflip, "Oplus Display Vflip");

module_param(uart_enable, uint, 0444);
MODULE_PARM_DESC(uart_enable, "Oplus Uart Enable");

#if 0
module_param(deep_dischrg_count, uint, 0444);
MODULE_PARM_DESC(deep_dischrg_count, "Oplus Deep Discharge Count");
#endif

module_param(clikshutdown, uint, 0444);
MODULE_PARM_DESC(clikshutdown, "Oplus Clikshutdown");

module_param(boot_silence, uint, 0444);
MODULE_PARM_DESC(boot_silence, "Oplus Boot Silence");

uint __always_inline get_oplus_hw_id(void)
{
	return hw_id;
}
EXPORT_SYMBOL(get_oplus_hw_id);

uint  get_oplus_board_id(void)
{
	return board_id;
}
EXPORT_SYMBOL(get_oplus_board_id);

uint  get_oplus_board_type(void)
{
	return board_type;
}
EXPORT_SYMBOL(get_oplus_board_type);

uint  get_oplus_panel_id(void)
{
	return panel_id;
}
EXPORT_SYMBOL(get_oplus_panel_id);

uint  get_panel_present(void)
{
	return panel_present;
}
EXPORT_SYMBOL(get_panel_present);

uint  get_dsi_relock_en(void)
{
	return dsi_relock_en;
}
EXPORT_SYMBOL(get_dsi_relock_en);

uint  get_user_printk_disable_uart(void)
{
	return user_printk_disable_uart;
}
EXPORT_SYMBOL(get_user_printk_disable_uart);

bool  is_user_build(void)
{
	return is_user_build_variant;
}
EXPORT_SYMBOL(is_user_build);


bool  is_recovery(void)
{
	return is_recovery_mode;
}
EXPORT_SYMBOL(is_recovery);

uint  get_display_vflip(void)
{
	return display_vflip;
}
EXPORT_SYMBOL(get_display_vflip);

void  set_display_vflip(uint value)
{
	display_vflip = value;
	return;
}
EXPORT_SYMBOL(set_display_vflip);

uint  get_boot_silence(void)
{
	return boot_silence;
}
EXPORT_SYMBOL(get_boot_silence);

void set_boot_silence(uint value)
{
	boot_silence = value;
	return;
}
EXPORT_SYMBOL(set_boot_silence);

unsigned int get_tp_wakeup_en(void)
{
	return tp_wakeup_en;
}
EXPORT_SYMBOL(get_tp_wakeup_en);

void set_tp_wakeup_en(unsigned int  value)
{
	tp_wakeup_en = value;
	return;
}
EXPORT_SYMBOL(set_tp_wakeup_en);

#if 0
uint  get_deep_dischrg_count(void)
{
	return deep_dischrg_count;
}
EXPORT_SYMBOL(get_deep_dischrg_count);
#endif

uint get_clikshutdown(void)
{
	return clikshutdown;
}
EXPORT_SYMBOL(get_clikshutdown);

/******************************************************************************/
#if 0
static int __init set_boomode(char *line)
{
	const char typemode[]  = "recovery";
	char oplus_bootmode[20];

	strlcpy(oplus_bootmode, line, sizeof(oplus_bootmode));

	is_recovery_mode = !strncmp(oplus_bootmode, typemode, sizeof(typemode));
	return 1;
}
__setup("androidboot.mode=", set_boomode);
#endif

extern char build_variant[];
static void __init __build_variant_init(void)
{
	const char typeuser[]  = "user";

	is_user_build_variant = !strncmp(build_variant, typeuser, sizeof(typeuser));
}




#define BOOTARGS_LEN (4*1024) //4K
static int __init oplus_bootargs_init(void)
{
	int ret;
	struct device_node *of_chosen;

	__build_variant_init();



	bootargs_ptr = (char *)kmalloc(BOOTARGS_LEN, GFP_KERNEL);
	if (!bootargs_ptr) {
		printk(KERN_ERR "kmalloc for bootargs ptr failed !\n");
		return -1;
	}

	of_chosen = of_find_node_by_path("/chosen");
	if (!of_chosen) {
		of_chosen = of_find_node_by_path("/chosen@0");
	}

	if (of_chosen) {
		ret = of_property_read_string(of_chosen, "bootargs", (const char **)&bootargs_ptr);
		if (ret) {
			printk(KERN_ERR "try to get bootargs failed !");
			return -1;
		}
	}

	return 0;
}

module_init(oplus_bootargs_init);


MODULE_AUTHOR("Oplus Team");
MODULE_DESCRIPTION("Oplus cmdline parser driver");
MODULE_LICENSE("GPL v2");
