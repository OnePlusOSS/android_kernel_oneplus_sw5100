#ifndef _OPLUS_CROWN_H_
#define _OPLUS_CROWN_H_

#include <linux/input.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/regulator/consumer.h>
#include <linux/suspend.h>
#include <linux/proc_fs.h>
#include <linux/version.h>
#include <linux/types.h>

#if defined(CONFIG_PANEL_NOTIFIER)
#include <linux/soc/qcom/panel_event_notifier.h>
#include <oplus/msm_drm_notify.h>
#elif defined(CONFIG_DRM)
#include <linux/msm_drm_notify.h>
#endif

#ifdef CONFIG_OPLUS_SWITCH
#include "oplus/oplus_switch.h"
#endif

#ifdef OPLUS_CROWN_GET_CPI_X_BY_SETUP
#include <oplus/oplus.h>
#else
#include <oplus/oplus_cmdline_parser.h>
#endif /* OPLUS_CROWN_GET_CPI_X_BY_SETUP */

#define DYNAMIC_RESOLUTION
//#define REGULATOR_VDD
#define REGULATOR_VLD

#define OPLUS_CROWN_DEV_NAME "oplus_crown"

#define MAX_BUF_SIZE 20
#define RESET_DELAY_US 15000
#define RESET_RETRY_CNT_MAX 2
#define PINCTRL_STATE_ACTIVE "oplus_crown_pinctrl_active"
#define PINCTRL_STATE_SUSPEND "oplus_crown_pinctrl_suspend"
#define PINCTRL_STATE_RELEASE "oplus_crown_pinctrl_release"
#define VDD_VTG_MIN_UV 1800000
#define VDD_VTG_MAX_UV 1800000
#define VDD_ACTIVE_LOAD_UA 10000
#define VLD_VTG_MIN_UV 2800000
#define VLD_VTG_MAX_UV 3300000
#define VLD_ACTIVE_LOAD_UA 10000
#define DELAY_BETWEEN_REG_US 25000
#define DELAY_POWERDOWN 2500

#define OPLUS_CROWN_PRODUCT_ID_REG 0x00
#define OPLUS_CROWN_DEFAULT_CPI_RESOLUTION_X 0xB9
#define OPLUS_CROWN_CPI_RESOLUTION_X_STATIC 0x1E //Fix Press Down Debounce

#define OPLUS_CROWN_SAMPLING_PERIOD_US_MIN 14000
#define OPLUS_CROWN_SAMPLING_PERIOD_US_MAX 14500  //about 60Hz
#ifdef DYNAMIC_RESOLUTION
#define OPLUS_CROWN_SAMPLING_AUTO_REDUCTION_TIMEOUT_US  1000000
#define OPLUS_CROWN_SAMPLING_AUTO_REDUCTION_CNT \
	((OPLUS_CROWN_SAMPLING_AUTO_REDUCTION_TIMEOUT_US + OPLUS_CROWN_SAMPLING_PERIOD_US_MAX) \
	/ OPLUS_CROWN_SAMPLING_PERIOD_US_MIN)
#endif

#define OPLUS_PANEL_EVENT_MAGIC_NUM 7263543

enum em_drm_state {
	DRM_BLANK_ACTIVED = MSM_DRM_BLANK_UNBLANK, /*panel: actived*/
	DRM_BLANK_IDLE, /*panel: power off or AON*/
};

struct oplus_crown_platform_data {
	struct i2c_client *client;
	struct input_dev *input;
	struct wakeup_source *ws;
	int irq_gpio;
#ifdef REGULATOR_VDD
	struct regulator *vdd;
#endif
#ifdef REGULATOR_VLD
	struct regulator *vld;
#endif
	struct pinctrl *pinctrl;
	struct pinctrl_state *pinctrl_state_active;
	struct pinctrl_state *pinctrl_state_suspend;
	struct pinctrl_state *pinctrl_state_release;
	int irq_en;
	int calibrateing;
#ifdef CONFIG_OPLUS_SWITCH
	int switch_state;
	struct notifier_block switch_nb;
#endif
	uint force_init;
	uint default_init;
	uint notify_id;
	enum em_drm_state drm_state;
#if defined(CONFIG_PANEL_NOTIFIER)
	struct panel_event_notifier_entry *panel_event_notifier;
#elif defined(CONFIG_DRM)
	struct notifier_block drm_notifier;
#endif
	struct work_struct pwr_work;
	struct workqueue_struct *pwr_on_workqueue;
	struct proc_dir_entry *proc_entry;
	struct oplus_crown_sensor_ops *current_ops;
};

struct oplus_crown_sensor_ops {
	int (*init)(struct oplus_crown_platform_data *pdata);
	int (*deinit)(struct oplus_crown_platform_data *pdata);
	int (*sw_power)(struct oplus_crown_platform_data *pdata, bool on);
	int (*force_sleep)(struct oplus_crown_platform_data *pdata);
	int (*set_cpi_x_reg)(struct oplus_crown_platform_data *pdata, long val);
	int (*irq_handler)(struct oplus_crown_platform_data *pdata, int irq);
	void (*clear_data)(struct oplus_crown_platform_data *pdata);
	void (*calibrate)(struct oplus_crown_platform_data *pdata, bool start);
	int (*get_calibrate_value)(struct oplus_crown_platform_data *pdata);
	void (*dump_state)(struct oplus_crown_platform_data *pdata, char * buf, int len);
};

struct oplus_crown_id_ops_table {
	unsigned char id;
	struct oplus_crown_sensor_ops *ops;
};

extern uint static_xcpi;
extern uint auto_suspend;

struct oplus_crown_sensor_ops * get_crown_ops(struct oplus_crown_platform_data * pdata);

extern unsigned char read_data(struct i2c_client *, u8 addr);
extern void write_data(struct i2c_client *, u8 addr, u8 data);

bool oplus_crown_write_read(struct i2c_client *client, u8 address, u8 wdata);
bool oplus_crown_sensor_init(struct oplus_crown_platform_data *pdata);

unsigned short get_cpi_resolution_x(void);
void set_cpi_resolution_x(unsigned short new);
void set_cpi_res_x_reg(struct oplus_crown_platform_data *pdata, long val);

void set_oplus_crown_sensor_id(struct oplus_crown_platform_data * pdata, unsigned char id);

#endif /* _OPLUS_CROWN_H_ */