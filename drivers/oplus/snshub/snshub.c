// SPDX-License-Identifier: GPL-2.0-only
/*
* Copyright (C) 2019-2023 Oplus. All rights reserved.
*/

#define pr_fmt(fmt)  "ossh:" fmt "\n"

#define MCU_DUMP_REPORT_UEVENT_ENABLE

#include <linux/device.h>
#include <linux/err.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include "snshub.h"

struct oplus_snshub_platform_data {
	int gpio_reset;
	bool gpio_reset_active_low;
	int gpio_power;
	bool gpio_power_active_low;
	int gpio_buck_enable;
	bool gpio_buck_enable_active_low;
	int gpio_ap_suspend;
	bool gpio_ap_suspend_active_low;
	int gpio_ap_state;
	bool gpio_ap_state_active_low;
	int gpio_dump;
	bool gpio_dump_active_low;
};


struct oplus_snshub {
	struct device *dev;
	struct oplus_snshub_platform_data *pdata;
	struct mutex reset_lock;
	struct blocking_notifier_head rst_nh;
	struct mutex power_lock;
	struct blocking_notifier_head power_nh;
	struct mutex dump_lock;
	int          disable_uevent;
	struct mutex disable_uevent_lock;
#ifdef MCU_DUMP_REPORT_UEVENT_ENABLE
	int                  dump_irq;
	struct work_struct   dump_work;
	struct wakeup_source dump_wakesrc;
#endif
	int          mcureseting;
};
struct oplus_snshub *g_snshub;

#define SNSHUB_RESET_ACTIVE_TIME    10
#define SNSHUB_RESET_DELAY_TIME     50
#define SNSHUB_POWER_ACTIVE_TIME    200
#define SNSHUB_POWER_DELAY_TIME     10

static int reset_time  = 1;

static inline void oplus_snshub_hw_reset(struct oplus_snshub *snshub, bool assert)
{
	//gpiod_direction_output(gpio_to_desc(snshub->pdata->gpio_reset),!!snshub->pdata->gpio_reset_active_low ^ !!assert);
	if (gpio_is_valid(snshub->pdata->gpio_reset)) {
		if (!!snshub->pdata->gpio_reset_active_low ^ !!assert) {
			gpio_direction_output(snshub->pdata->gpio_reset, 1);
		} else {
			gpio_direction_input(snshub->pdata->gpio_reset);
		}
	}
}

static inline void oplus_snshub_hw_power(struct oplus_snshub *snshub, bool assert)
{
	if (gpio_is_valid(snshub->pdata->gpio_power)) {
		gpio_direction_output(snshub->pdata->gpio_power,!!snshub->pdata->gpio_power_active_low ^ !!assert);
		//gpio_set_value(snshub->pdata->gpio_power,
		//				!!snshub->pdata->gpio_power_active_low ^ !!assert);
	}

	if (gpio_is_valid(snshub->pdata->gpio_buck_enable)) {
		gpio_direction_output(snshub->pdata->gpio_buck_enable,!!snshub->pdata->gpio_buck_enable_active_low ^ !!assert);
		//gpio_set_value(snshub->pdata->gpio_buck_enable,
		//			!!snshub->pdata->gpio_buck_enable_active_low ^ !!assert);
	}
}

static inline void oplus_snshub_hw_ap_suspend(struct oplus_snshub *snshub, bool assert)
{
	if (gpio_is_valid(snshub->pdata->gpio_ap_suspend)) {
		gpio_direction_output(snshub->pdata->gpio_ap_suspend, !!snshub->pdata->gpio_ap_suspend_active_low ^ !!assert);
		//gpio_set_value(snshub->pdata->gpio_ap_suspend,
		//				!!snshub->pdata->gpio_ap_suspend_active_low ^ !!assert);
	}
}

int oplus_snshub_hw_gpio_in(struct device *dev)
{
	unsigned int res;
	struct oplus_snshub *snshub = dev_get_drvdata(dev->parent);
	if (gpio_is_valid(snshub->pdata->gpio_reset)) {
		gpiod_direction_input(gpio_to_desc(snshub->pdata->gpio_reset));
		return res = gpio_get_value(snshub->pdata->gpio_reset);
	} else {
		return -1;
	}
}
EXPORT_SYMBOL_GPL(oplus_snshub_hw_gpio_in);

int oplus_snshub_reset_notifier_register(struct device *dev, struct notifier_block *nb)
{
	struct oplus_snshub *snshub = g_snshub;

	if (dev) {
		snshub = dev_get_drvdata(dev->parent);
	}

	if (NULL == snshub) {
		pr_err("oplus_snshub_reset_notifier_register error snshub is NULL");
		return 0;
	}

	return blocking_notifier_chain_register(&snshub->rst_nh, nb);
}
EXPORT_SYMBOL_GPL(oplus_snshub_reset_notifier_register);

int oplus_snshub_reset_notifier_unregister(struct device *dev, struct notifier_block *nb)
{
	struct oplus_snshub *snshub = g_snshub;

	if (dev) {
		snshub = dev_get_drvdata(dev->parent);
	}

	if (NULL == snshub) {
		pr_err("oplus_snshub_reset_notifier_register error snshub is NULL");
		return 0;
	}

	return blocking_notifier_chain_unregister(&snshub->rst_nh, nb);
}
EXPORT_SYMBOL_GPL(oplus_snshub_reset_notifier_unregister);

static void __oplus_snshub_assert_reset(struct oplus_snshub *snshub, void *v)
{
	mutex_lock(&snshub->reset_lock);

	blocking_notifier_call_chain(&snshub->rst_nh, SNSHUB_PRE_RESET, v);

	oplus_snshub_hw_reset(snshub, true);
	msleep(SNSHUB_RESET_ACTIVE_TIME);
}

static void __oplus_snshub_deassert_reset(struct oplus_snshub *snshub, void *v)
{
	oplus_snshub_hw_reset(snshub, false);
	msleep(SNSHUB_RESET_DELAY_TIME);

	blocking_notifier_call_chain(&snshub->rst_nh, SNSHUB_POST_RESET, v);

	mutex_unlock(&snshub->reset_lock);
}

static void __oplus_snshub_reset(struct oplus_snshub *snshub, void *v)
{
	if (mutex_trylock(&snshub->reset_lock)) {
		g_snshub->mcureseting = 1;
		blocking_notifier_call_chain(&snshub->rst_nh, SNSHUB_PRE_RESET, v);

		if (gpio_is_valid(snshub->pdata->gpio_ap_state)) {
			gpio_direction_output(snshub->pdata->gpio_ap_state, !!snshub->pdata->gpio_ap_state_active_low ^ false);
		}

		/* assert reset pin */
		oplus_snshub_hw_reset(snshub, true);
		pr_err("__oplus_snshub_reset sleep time:%d",reset_time);
		msleep(reset_time * SNSHUB_RESET_ACTIVE_TIME);

		/* deassert reset pin */
		oplus_snshub_hw_reset(snshub, false);

		if (gpio_is_valid(snshub->pdata->gpio_ap_state)) {
			gpio_direction_output(snshub->pdata->gpio_ap_state, !!snshub->pdata->gpio_ap_state_active_low ^ true);
		}

		msleep(reset_time* SNSHUB_RESET_DELAY_TIME);
		pr_err("__oplus_snshub_reset e sleep time:%d",reset_time);

		blocking_notifier_call_chain(&snshub->rst_nh, SNSHUB_POST_RESET, v);
		g_snshub->mcureseting = 0;
	} else {
		/* another resetting is in progress, wait it done */
		mutex_lock(&snshub->reset_lock);
	}
	mutex_unlock(&snshub->reset_lock);
}

static void __oplus_snshub_power_off_once(struct oplus_snshub *snshub, void *v)
{
	if (mutex_trylock(&snshub->power_lock)) {
		blocking_notifier_call_chain(&snshub->power_nh, SNSHUB_PRE_RESET, v);
		g_snshub->mcureseting = 1;
		if (gpio_is_valid(snshub->pdata->gpio_ap_state)) {
			gpio_direction_output(snshub->pdata->gpio_ap_state, !!snshub->pdata->gpio_ap_state_active_low ^ false);
		}

		//reset pin is active, power pin is disactive,
		//can power off the mcu supply.
		oplus_snshub_hw_reset(snshub, true);
		msleep(SNSHUB_RESET_ACTIVE_TIME);
		oplus_snshub_hw_power(snshub, true);
		msleep(SNSHUB_POWER_ACTIVE_TIME);

		//reset pin is disactive, power pin is active,
		//can power on the mcu supply.
		oplus_snshub_hw_power(snshub, false);
		msleep(SNSHUB_POWER_DELAY_TIME);
		oplus_snshub_hw_reset(snshub, false);

		if (gpio_is_valid(snshub->pdata->gpio_ap_state)) {
			gpio_direction_output(snshub->pdata->gpio_ap_state, !!snshub->pdata->gpio_ap_state_active_low ^ true);
		}

		msleep(SNSHUB_RESET_DELAY_TIME);

		blocking_notifier_call_chain(&snshub->power_nh, SNSHUB_POST_RESET, v);
		g_snshub->mcureseting = 0;
	} else {
		/* another resetting is in progress, wait it done */
		mutex_lock(&snshub->power_lock);
	}
	mutex_unlock(&snshub->power_lock);
}

void oplus_snshub_assert_reset(struct device *dev)
{
	struct oplus_snshub *snshub = dev_get_drvdata(dev->parent);
	__oplus_snshub_assert_reset(snshub, dev);
}
EXPORT_SYMBOL_GPL(oplus_snshub_assert_reset);

void oplus_snshub_deassert_reset(struct device *dev)
{
	struct oplus_snshub *snshub = dev_get_drvdata(dev->parent);
	__oplus_snshub_deassert_reset(snshub, dev);
}
EXPORT_SYMBOL_GPL(oplus_snshub_deassert_reset);

void oplus_snshub_reset(struct device *dev)
{
	struct oplus_snshub *snshub = dev_get_drvdata(dev->parent);
	__oplus_snshub_reset(snshub, dev);
}
EXPORT_SYMBOL_GPL(oplus_snshub_reset);

void oplus_snshub_power_off_once(struct device *dev)
{
	struct oplus_snshub *snshub = dev_get_drvdata(dev->parent);
	__oplus_snshub_power_off_once(snshub, dev);
}
EXPORT_SYMBOL_GPL(oplus_snshub_power_off_once);

bool is_mcureseting(void)
{
	if (unlikely(!g_snshub)) {
		pr_err("g_snshub is NULL");
		return false;
	}
	return !!g_snshub->mcureseting;
}
EXPORT_SYMBOL_GPL(is_mcureseting);

void wait_mcureseting(void)
{
	if (unlikely(!g_snshub)) {
		pr_err("g_snshub is NULL");
		return;
	}

	while (g_snshub->mcureseting) {
		msleep(20);
	}

	return;
}
EXPORT_SYMBOL_GPL(wait_mcureseting);


#ifdef MCU_STATES_UEVENT
char* const mcu_uevent = "MCU_UEVENT";

static inline void oplus_snshub_uevent(char* const change, char* const dev_name)
{
	char *changed[4] = {mcu_uevent, dev_name, change, NULL };

	if (unlikely(!dev_name)) {
		pr_err("dev_name is NULL");
		return;
	}

	if (unlikely(!change)) {
		pr_err("change is NULL");
		return;
	}

	if (unlikely(!g_snshub)) {
		pr_err("g_snshub is NULL");
		return;
	}

	pr_err("uevent report %s:(%s)", dev_name, change);
	/*change@  uevent*/
	kobject_uevent_env(&g_snshub->dev->kobj, KOBJ_CHANGE, changed);
	return;
}

void oplus_snshub_states_uevent(int index, char* const dev_name)
{
	static uint print_cnt = 0;
	char *mcu_states[MCU_STATES_MAX] = {"MCU_STATES=first", "MCU_STATES=normal", "MCU_STATES=crash", "MCU_STATES=dump"};

	if (unlikely(index >= MCU_STATES_MAX)) {
		pr_err("index(%d) is too big, max=%d", index, MCU_STATES_MAX);
		return;
	}

	if (unlikely(!dev_name)) {
		pr_err("dev_name is NULL");
		return;
	}

	if (unlikely(!g_snshub)) {
		pr_err("g_snshub is NULL");
		return;
	}

	if ((MCU_STATES_CRASH == index)&&(g_snshub->disable_uevent)) {
		if ((print_cnt < 10)
			|| ((print_cnt < 256)&&(0 == (print_cnt & 0x0f)))
			|| ((0 == (print_cnt & 0xff))))
			pr_err("uevent (%s), but not report(%d)", mcu_states[index], print_cnt);
		print_cnt++;
	} else {
		print_cnt = 0;
		oplus_snshub_uevent(mcu_states[index], dev_name);
	}
}
EXPORT_SYMBOL_GPL(oplus_snshub_states_uevent);

void oplus_snshub_wakeup_data_uevent(char* const dev_name)
{
	if (unlikely(!dev_name)) {
		pr_err("dev_name is NULL");
		return;
	}

	pr_err("uevent report wakeup data update");
	/*change@  uevent*/
	oplus_snshub_uevent("WAKEUP_UEVENT=update", dev_name);
}
EXPORT_SYMBOL_GPL(oplus_snshub_wakeup_data_uevent);
#endif

#ifdef MCU_DUMP_REPORT_UEVENT_ENABLE
#define SMALL_CORE_DUMP_ACTIVE_LEVEL                 1
#define DEBOUNCE_ONE_SECOND     1000

static void mcu_states_dump(struct work_struct *work)
{
	pr_err("entry mcu_states_dump\n");
	//hold wake lock
	__pm_stay_awake(&g_snshub->dump_wakesrc);

	oplus_snshub_states_uevent(MCU_STATES_DUMP, "snshub");
	//release wake lock
	__pm_relax(&g_snshub->dump_wakesrc);
}

static irqreturn_t mcu_dump_int_irq_fn(int irq, void *handle)
{
	struct device *dev = (struct device *)handle;
	int gpio_dump = 0;

	if (!dev ||!g_snshub) {
		/*NOTE:Submit code, please do not open "log_e".But own debug can open "log_e".*/
		return IRQ_HANDLED;
	}

	gpio_dump = gpio_get_value(g_snshub->pdata->gpio_dump);
	if (irq == g_snshub->dump_irq) {
		if (gpio_dump) {
			schedule_work(&g_snshub->dump_work);
		}
	}
	return IRQ_HANDLED;
}
#endif

static ssize_t oplus_snshub_store_reset(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct oplus_snshub *snshub = dev_get_drvdata(dev);
	long val;

	if (kstrtol(buf, 10, &val) != 0 || !val)
		return -EINVAL;

	reset_time = (int)val;

	__oplus_snshub_reset(snshub, NULL);

	reset_time = 1;

	return count;
}

static ssize_t oplus_snshub_store_power_mcu(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	//struct oplus_snshub *snshub = dev_get_drvdata(dev);
	long val;

	if (kstrtol(buf, 10, &val) != 0 || !val)
		return -EINVAL;

	//__oplus_snshub_power_off_once(snshub, NULL);

	return count;
}

static ssize_t oplus_snshub_store_dump_notify(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct oplus_snshub *snshub = dev_get_drvdata(dev);
	long val;

	if (kstrtol(buf, 10, &val) != 0 || !val)
		return -EINVAL;

	if (mutex_trylock(&snshub->dump_lock)) {
		/* assert reset pin */
		gpio_direction_output(1017,1);
		msleep(100);

		/* deassert reset pin */
		gpio_direction_output(1017,0);
		msleep(1);
	} else {
		/* another resetting is in progress, wait it done */
		mutex_lock(&snshub->dump_lock);
	}
	mutex_unlock(&snshub->dump_lock);

	return count;
}

uint oplus_snshub_get_disable_uevent(void)
{
	return g_snshub->disable_uevent;
}
EXPORT_SYMBOL_GPL(oplus_snshub_get_disable_uevent);

void oplus_snshub_set_disable_uevent(uint val)
{
	mutex_lock(&g_snshub->disable_uevent_lock);
	g_snshub->disable_uevent = !(!val);
	pr_err("disable_uevent %d", g_snshub->disable_uevent);
	mutex_unlock(&g_snshub->disable_uevent_lock);
}
EXPORT_SYMBOL_GPL(oplus_snshub_set_disable_uevent);

static ssize_t
oplus_snshub_show_disable_uevent(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t count = 0;
	struct oplus_snshub *snshub = dev_get_drvdata(dev);

	count = snprintf(buf, PAGE_SIZE-count, "%d", snshub->disable_uevent);
	return count;
}

static ssize_t oplus_snshub_store_disable_uevent(
	struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct oplus_snshub *snshub = dev_get_drvdata(dev);
	long val;

	if (kstrtol(buf, 10, &val) != 0 || !val)
		return -EINVAL;

	mutex_lock(&snshub->disable_uevent_lock);
	snshub->disable_uevent = !(!val);
	dev_err(dev, "disable_uevent %d\n", snshub->disable_uevent);
	mutex_unlock(&snshub->disable_uevent_lock);

	return count;
}

static DEVICE_ATTR(reset, 0200, NULL, oplus_snshub_store_reset);
static DEVICE_ATTR(power_mcu, 0200, NULL, oplus_snshub_store_power_mcu);
static DEVICE_ATTR(dump_notify, 0200, NULL, oplus_snshub_store_dump_notify);
static DEVICE_ATTR(disable_uevent, 0660, oplus_snshub_show_disable_uevent, oplus_snshub_store_disable_uevent);

static struct attribute *oplus_snshub_attributes[] = {
	&dev_attr_reset.attr,
	&dev_attr_power_mcu.attr,
	&dev_attr_dump_notify.attr,
	&dev_attr_disable_uevent.attr,
	NULL
};

static const struct attribute_group oplus_snshub_attr_group = {
	.attrs	= oplus_snshub_attributes,
};

static int oplus_snshub_suspend(struct device *dev)
{
	struct oplus_snshub *snshub = dev->driver_data;
	if (gpio_is_valid(snshub->pdata->gpio_ap_suspend)) {
		oplus_snshub_hw_ap_suspend(snshub, true);
	}
	return 0;
}

static int oplus_snshub_resume(struct device *dev)
{
	struct oplus_snshub *snshub = dev->driver_data;
	if (gpio_is_valid(snshub->pdata->gpio_ap_suspend)) {
		oplus_snshub_hw_ap_suspend(snshub, false);
	}

	return 0;
}

static const struct dev_pm_ops snshub_pm_ops = {
	.resume         = oplus_snshub_resume,
	.suspend        = oplus_snshub_suspend,
};

static struct oplus_snshub_platform_data *oplus_snshub_parse_dt(struct device *dev)
{
	struct oplus_snshub_platform_data *pdata;
	enum of_gpio_flags flags;
	int ret;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return NULL;

	/* reset gpio */
	ret = of_get_named_gpio_flags(dev->of_node, "reset-gpio", 0, &flags);
	if (ret < 0) {
		dev_err(dev, "Unable to read reset gpio\n");
		//return NULL;
	}

	pdata->gpio_reset = ret;
	pdata->gpio_reset_active_low = (flags & OF_GPIO_ACTIVE_LOW) ? true : false;

	/* power gpio */
	ret = of_get_named_gpio_flags(dev->of_node, "power-gpio", 0, &flags);
	if (ret < 0) {
		dev_err(dev, "Unable to read power gpio\n");
		//return NULL;
	}

	pdata->gpio_power = ret;
	pdata->gpio_power_active_low = (flags & OF_GPIO_ACTIVE_LOW) ? true : false;

	/* buck enable gpio */
	ret = of_get_named_gpio_flags(dev->of_node, "buck-enable-gpio", 0, &flags);
	if (ret < 0) {
		dev_err(dev, "Unable to read buck enable gpio\n");
		//return NULL;
	}

	pdata->gpio_buck_enable = ret;
	pdata->gpio_buck_enable_active_low = (flags & OF_GPIO_ACTIVE_LOW) ? true : false;

//	/*master runing  gpio */
//	ret = of_get_named_gpio_flags(dev->of_node, "ap-suspend-gpio", 0, &flags);
//	if (ret < 0) {
//		dev_err(dev, "Unable to read ap suspend gpio \n");
//		return NULL;
//	}
//
//	pdata->gpio_ap_suspend= ret;
//	pdata->gpio_ap_suspend_active_low = (flags & OF_GPIO_ACTIVE_LOW) ? true : false;

	/*master state  gpio */
	ret = of_get_named_gpio_flags(dev->of_node, "ap-state-gpio", 0, &flags);
	if (ret < 0) {
		dev_err(dev, "Unable to read ap state gpio \n");
		//return NULL;
	}

	pdata->gpio_ap_state= ret;
	pdata->gpio_ap_state_active_low = (flags & OF_GPIO_ACTIVE_LOW) ? true : false;

#ifdef MCU_DUMP_REPORT_UEVENT_ENABLE
	/* mcu dead gpio */
	ret = of_get_named_gpio_flags(dev->of_node, "dead-gpio", 0, &flags);
	if (ret < 0) {
		dev_err(dev, "Unable to read mcu dead gpio\n");
		return NULL;
	}
	pdata->gpio_dump = ret;
	pdata->gpio_dump_active_low = (flags & OF_GPIO_ACTIVE_LOW) ? true : false;
#endif

	return pdata;
}

static const struct mfd_cell oplus_snshub_devs[] = {
	{
		.name = "swd",
		.of_compatible = "oplus,swd",
	},
	{
		.name = "spidev-snshub",
		.of_compatible = "oplus,spidev-snshub",
	},
};

static int oplus_snshub_probe(struct platform_device *pdev)
{
	struct oplus_snshub_platform_data *pdata = dev_get_platdata(&pdev->dev);
	struct oplus_snshub *snshub;
	struct pinctrl *pinctrl;
	int ret;
	dev_err(&pdev->dev, "oplus_snshub_probe()\n");
	if (!pdata) {
		pdata = oplus_snshub_parse_dt(&pdev->dev);
		if (!pdata)
			return -ENOMEM;
	}

	snshub = devm_kzalloc(&pdev->dev, sizeof(*snshub), GFP_KERNEL);
	if (!snshub)
		return -ENOMEM;

	snshub->dev = &pdev->dev;
	snshub->pdata = pdata;
	BLOCKING_INIT_NOTIFIER_HEAD(&snshub->rst_nh);
	mutex_init(&snshub->reset_lock);
	BLOCKING_INIT_NOTIFIER_HEAD(&snshub->power_nh);
	mutex_init(&snshub->power_lock);

	mutex_init(&snshub->dump_lock);
	mutex_init(&snshub->disable_uevent_lock);

	snshub->mcureseting = 0;

	pinctrl = devm_pinctrl_get_select_default(&pdev->dev);

	ret = mfd_add_devices(&pdev->dev, 0, oplus_snshub_devs,
			ARRAY_SIZE(oplus_snshub_devs), NULL, 0, NULL);
	if (ret) {
		dev_err(&pdev->dev, "add mfd devices failed: %d\n", ret);
		return ret;
	}

	ret = sysfs_create_group(&pdev->dev.kobj, &oplus_snshub_attr_group);
	if (ret) {
		mfd_remove_devices(&pdev->dev);

		dev_err(&pdev->dev,
			"Failed to create attribute group: %d\n", ret);
		return ret;
	}


#ifdef MCU_DUMP_REPORT_UEVENT_ENABLE
	//init wake up lock
	memset(&snshub->dump_wakesrc, 0, sizeof(snshub->dump_wakesrc));
	snshub->dump_wakesrc.name = "mcu_dump_wakesrc";
	wakeup_source_add(&snshub->dump_wakesrc);

	//init work queue
	INIT_WORK(&snshub->dump_work, mcu_states_dump);

	/* Initialize IRQ */
	snshub->dump_irq = gpio_to_irq(pdata->gpio_dump);
	if (snshub->dump_irq < 0) {
		dev_err(&pdev->dev, "gpio(%d) gpio_to_irq failed: %d\n",
			pdata->gpio_dump, snshub->dump_irq);
		return -EINVAL;
	}

	if (request_irq(snshub->dump_irq, mcu_dump_int_irq_fn,
		IRQF_TRIGGER_RISING | IRQF_ONESHOT, dev_name(snshub->dev), snshub->dev) < 0) {
		dev_err(&pdev->dev, "Error, could not request irq\n");
		return -EINVAL;
	}

	if (enable_irq_wake(snshub->dump_irq) != 0) {
		dev_err(&pdev->dev, "Error, enable_irq_wake(%d)\n", snshub->dump_irq);
		return -EINVAL;
	}

	dev_info(&pdev->dev, "dump: direction(in), gpio(%d), irq(%d)",
		pdata->gpio_dump, snshub->dump_irq);
#endif

	platform_set_drvdata(pdev, snshub);
	g_snshub = snshub;
	dev_info(&pdev->dev, "Oplus SensorHub register successful\n");

	return 0;
}

static int oplus_snshub_remove(struct platform_device *pdev)
{
//	struct oplus_snshub *snshub = platform_get_drvdata(pdev); 
	g_snshub = NULL;
	sysfs_remove_group(&pdev->dev.kobj, &oplus_snshub_attr_group);

	mfd_remove_devices(&pdev->dev);

	return 0;
}

static struct of_device_id of_oplus_snshub_match_tbl[] = {
	{ .compatible = "oplus,sensor-hub", },
	{},
};

static struct platform_driver oplus_snshub_driver = {
	.driver = {
		.name	= "sensor-hub",
		.of_match_table = of_oplus_snshub_match_tbl,
		.pm    = &snshub_pm_ops,
	},
	.probe	= oplus_snshub_probe,
	.remove	= oplus_snshub_remove,
};
module_platform_driver(oplus_snshub_driver);

MODULE_AUTHOR("Oplus Team");
MODULE_DESCRIPTION("OPLUS SensorHub driver");
MODULE_LICENSE("GPL v2");
