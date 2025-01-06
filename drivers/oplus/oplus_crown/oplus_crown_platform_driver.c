#include "oplus_crown.h"
#include <linux/vmalloc.h>

#undef pr_fmt
#define pr_fmt(fmt) OPLUS_CROWN_DEV_NAME ":%s(): " fmt, __func__

bool oplus_support_crown(void)
{
	return true;
}

char *proc_buff = NULL;
#define PROC_BUFF_LEN 2048

uint static_xcpi = OPLUS_CROWN_CPI_RESOLUTION_X_STATIC;
uint auto_suspend = OPLUS_CROWN_SAMPLING_AUTO_REDUCTION_TIMEOUT_US;
int stable_threshold = 10;

module_param(static_xcpi, uint, 0664);
MODULE_PARM_DESC(static_xcpi, "oplus crown cpi resolution x static");

module_param(auto_suspend, uint, 0664);
MODULE_PARM_DESC(auto_suspend, "oplus crown sampling auto reduction time");

module_param(stable_threshold, int, 0664);
MODULE_PARM_DESC(stable_threshold, "oplus crown sampling auto reduction time");

#ifndef OPLUS_CROWN_GET_CPI_X_BY_SETUP
uint cpi_x = OPLUS_CROWN_DEFAULT_CPI_RESOLUTION_X;

module_param(cpi_x, uint, 0664);
MODULE_PARM_DESC(cpi_x, "oplus crown cpi resolution x static");
#endif /* OPLUS_CROWN_GET_CPI_X_BY_SETUP */

/* Declaration of suspend and resume functions */
static int oplus_crown_suspend(struct device *dev);
static int oplus_crown_resume(struct device *dev);

static int oplus_crown_i2c_write(struct i2c_client *client, u8 reg, u8 *data,
		int len)
{
	u8 buf[MAX_BUF_SIZE];
	int ret = 0, i;
	struct device *dev = &client->dev;

	buf[0] = reg;
	if (len >= MAX_BUF_SIZE) {
		dev_err(dev, "%s Failed: buffer size is %d [Max Limit is %d]\n",
			__func__, len, MAX_BUF_SIZE);
		return -ENODEV;
	}
	for (i = 0 ; i < len; i++)
		buf[i+1] = data[i];
	/* Returns negative errno, or else the number of bytes written. */
	ret = i2c_master_send(client, buf, len+1);
	if (ret != len+1)
		dev_err(dev, "%s Failed: writing to reg 0x%x:0x%x\n", __func__, reg, *data);

	return ret;
}

static int oplus_crown_i2c_read(struct i2c_client *client, u8 reg, u8 *data)
{
	u8 buf[MAX_BUF_SIZE];
	int ret = 0;
#if 1
	struct device *dev = &client->dev;
	buf[0] = reg;
	/*
	 * If everything went ok (1 msg transmitted), return #bytes transmitted,
	 * else error code. thus if transmit is ok return value 1
	 */
	ret = i2c_master_send(client, buf, 1);
	if (ret != 1) {
		dev_err(dev, "%s Failed: writing to reg 0x%x\n", __func__, reg);
		return ret;
	}
	/* returns negative errno, or else the number of bytes read */
	ret = i2c_master_recv(client, buf, 1);
	if (ret != 1) {
		dev_err(dev, "%s Failed: reading reg 0x%x\n", __func__, reg);
		return ret;
	}
	ret = 0;
#else
	struct i2c_msg msg[2] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 1,
			.buf = &reg,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = buf,
		}
	};

	if (i2c_transfer(client->adapter, msg, 2) != 2) {
		dev_err(&client->dev, "i2c transfer failed rd 0x%x\n", reg);
		ret = -EIO;
	} else {
		ret = 0;
	}
#endif
	*data = buf[0];
	return ret;
}

u8 read_data(struct i2c_client *client, u8 addr)
{
	u8 data = 0x00;

	if (oplus_crown_i2c_read(client, addr, &data)) {
		data = 0x00;
	}
	return data;
}

void write_data(struct i2c_client *client, u8 addr, u8 data)
{
	oplus_crown_i2c_write(client, addr, &data, 1);
}

static void inline remove_dirty_data(struct oplus_crown_platform_data *pdata)
{
	struct oplus_crown_sensor_ops * ops = get_crown_ops(pdata);

	if ((SWITCH_DISCONNECT != pdata->switch_state)
		&& (DRM_BLANK_ACTIVED == pdata->drm_state)) {
		if (gpio_is_valid(pdata->irq_gpio) && (0 == gpio_get_value(pdata->irq_gpio))) {
			if (ops != NULL && ops->clear_data != NULL) {
				ops->clear_data(pdata);
			}
		}
	}
}

void set_cpi_res_x_reg(struct oplus_crown_platform_data *pdata, long val)
{
	struct oplus_crown_sensor_ops * ops = get_crown_ops(pdata);
	if (ops != NULL && ops->set_cpi_x_reg != NULL) {
		ops->set_cpi_x_reg(pdata, val);
	}
}

static irqreturn_t oplus_crown_irq_handler(int irq, void *dev_data)
{
	struct oplus_crown_platform_data *pdata = dev_data;
	struct oplus_crown_sensor_ops * ops = get_crown_ops(pdata);
	if (ops != NULL && ops->irq_handler != NULL) {
		ops->irq_handler(pdata, irq);
	} else {
		dev_err(&pdata->client->dev, "call irq_handler err\n");
		if (0 != pdata->irq_en) {
			dev_info(&(pdata->client->dev), "%s: disable_irq!\n", __func__);
			disable_irq_nosync(pdata->client->irq);
			irq_set_irq_type(pdata->client->irq, IRQ_TYPE_EDGE_RISING);
			pdata->irq_en = 0;
		}
	}
	return IRQ_HANDLED;
}

static ssize_t oplus_crown_suspend_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct oplus_crown_platform_data *pdata =
		(struct oplus_crown_platform_data *) dev_get_drvdata(dev);
	struct i2c_client *client = pdata->client;
	int mode;

	if (kstrtoint(buf, 10, &mode)) {
		dev_err(dev, "failed to read input for sysfs\n");
		return -EINVAL;
	}

	if (mode == 1)
		oplus_crown_suspend(&client->dev);
	else if (mode == 0)
		oplus_crown_resume(&client->dev);

	return count;
}

static ssize_t _oplus_crown_cpi_resolution_x_store(struct oplus_crown_platform_data *pdata,
		const char *buf, size_t count)
{
	long val;
	char val_str[15] = {0};
	int i;

	if (count >= sizeof(val_str) - 1) {
		pr_err("avg too long\n");
		return -EINVAL;
	}

	for (i = 0; i < count; i++) {
		if (buf[i] >= '0' && buf[i] <= '9') {
			val_str[i] = buf[i];
		} else {
			break;
		}
	}

	if (kstrtol(val_str, 10, &val) != 0) {
		pr_err("kstrtol failed\n");
		return -EINVAL;
	}

	if (val > 65535) {
		pr_err("kstrtol result lagre %d > 65535\n", val);
		return -EINVAL;
	}
	dev_err(&(pdata->client->dev), "%s: set cpi_x = %d\n", __func__, val);
	set_cpi_resolution_x((u16)val);

	set_cpi_res_x_reg(pdata, get_cpi_resolution_x());
	return count;
}

static ssize_t oplus_crown_cpi_resolution_x_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct oplus_crown_platform_data *pdata =
		(struct oplus_crown_platform_data *) dev_get_drvdata(dev);
	return _oplus_crown_cpi_resolution_x_store(pdata, buf, count);
}

static ssize_t crown_cpi_resolution_x_proc_write(struct file *file,
		const char __user *buf, size_t count, loff_t *off)
{
	struct oplus_crown_platform_data *pdata =
		(struct oplus_crown_platform_data *)PDE_DATA(file_inode(file));
	char strbuf[10] = {0};

	if (count > sizeof(strbuf) - 1) {
		return -EINVAL;
	}

	if (copy_from_user(strbuf, buf, sizeof(strbuf))) {
		return -EFAULT;
	}

	return _oplus_crown_cpi_resolution_x_store(pdata, strbuf, count);
}

static ssize_t oplus_crown_cpi_resolution_x_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d", get_cpi_resolution_x());
}

static ssize_t crown_cpi_resolution_x_proc_read(struct file *filp,
		char __user *buf, size_t count, loff_t *ppos)
{
	char strbuf[250] = {0};

	snprintf(strbuf, sizeof(strbuf) - 1, "%d", get_cpi_resolution_x());

	return simple_read_from_buffer(buf, count, ppos, strbuf,
		strlen(strbuf));
}

static ssize_t _oplus_crown_calibrate_store(struct oplus_crown_platform_data *pdata,
		const char *buf, size_t count)
{
	struct oplus_crown_sensor_ops * ops = get_crown_ops(pdata);

	if ((0 == buf[0])||('0' == buf[0])) { //stop calibrate
		if (0 != pdata->calibrateing) {
			if ((SWITCH_DISCONNECT != pdata->switch_state)
				&& (DRM_BLANK_ACTIVED == pdata->drm_state)) {
				remove_dirty_data(pdata);
				if (ops && ops->calibrate) {
					ops->calibrate(pdata, false);
				}
			} else {
				dev_err(&(pdata->client->dev), "%s: stop, disconnect or inactived\n", __func__);
			}
			pdata->calibrateing = 0;
		} else {
			dev_err(&(pdata->client->dev), "%s: already stop\n", __func__);
		}
	} else { //begin calibrate
		if (0 == pdata->calibrateing) {
			if ((SWITCH_DISCONNECT != pdata->switch_state)
				&& (DRM_BLANK_ACTIVED == pdata->drm_state)) {
				if (ops && ops->calibrate) {
					ops->calibrate(pdata, true);
				}
				pdata->calibrateing = 1;
			} else {
				dev_err(&(pdata->client->dev), "%s: begin, error return -1\n", __func__);
				count = -1;
			}
		} else {
			dev_err(&(pdata->client->dev), "%s: already start\n", __func__);
		}
	}

	return count;
}

static ssize_t oplus_crown_calibrate_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct oplus_crown_platform_data *pdata =
		(struct oplus_crown_platform_data *) dev_get_drvdata(dev);

	return _oplus_crown_calibrate_store(pdata, buf, count);
}

static ssize_t crown_calibrate_proc_write(struct file *file,
		const char __user *buf, size_t count, loff_t *off)
{
	struct oplus_crown_platform_data *pdata =
		(struct oplus_crown_platform_data *)PDE_DATA(file_inode(file));
	char strbuf[10] = {0};

	if (count > sizeof(strbuf) - 1) {
		return -EINVAL;
	}

	if (copy_from_user(strbuf, buf, sizeof(strbuf))) {
		return -EFAULT;
	}

	return _oplus_crown_calibrate_store(pdata, strbuf, count);
}

static ssize_t _oplus_crown_calibrate_show(
		struct oplus_crown_platform_data *pdata, char *buf)
{
	int value = 0;
	struct oplus_crown_sensor_ops * ops = get_crown_ops(pdata);

	if (ops && ops->get_calibrate_value) {
		value = ops->get_calibrate_value(pdata);
	}

	return snprintf(buf, PAGE_SIZE, "%d", value);
}

static ssize_t oplus_crown_calibrate_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct oplus_crown_platform_data *pdata =
		(struct oplus_crown_platform_data *) dev_get_drvdata(dev);
	return _oplus_crown_calibrate_show(pdata, buf);
}

static ssize_t crown_calibrate_proc_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	struct oplus_crown_platform_data *pdata =
		(struct oplus_crown_platform_data *)PDE_DATA(file_inode(file));
	char strbuf[250] = {0};

	_oplus_crown_calibrate_show(pdata, strbuf);

	return simple_read_from_buffer(buf, count, ppos, strbuf,
		strlen(strbuf));
}

static int crown_dump_state_proc_open(struct inode *inode, struct file *file)
{
	struct oplus_crown_platform_data *pdata =
		(struct oplus_crown_platform_data *)PDE_DATA(file_inode(file));
	struct oplus_crown_sensor_ops * ops = get_crown_ops(pdata);

	if (proc_buff == NULL) {
		proc_buff = vmalloc(PROC_BUFF_LEN);
	}
	if (proc_buff == NULL) {
		return 0;
	}

	memset(proc_buff, 0, PROC_BUFF_LEN);

	if (ops && ops->dump_state) {
		ops->dump_state(pdata, proc_buff, PROC_BUFF_LEN);
	}

	return 0;
}

static ssize_t crown_dump_state_proc_read(struct file *file,
		char __user *buf, size_t count, loff_t *ppos)
{
	if (proc_buff == NULL) {
		return 0;
	}

	return simple_read_from_buffer(buf, count, ppos, proc_buff,
		strlen(proc_buff));
}

static DEVICE_ATTR(suspend, S_IRUGO | S_IWUSR | S_IWGRP,
		NULL, oplus_crown_suspend_store);

static DEVICE_ATTR(cpi_resolution_x, S_IRUGO | S_IWUSR | S_IWGRP,
		oplus_crown_cpi_resolution_x_show, oplus_crown_cpi_resolution_x_store);

static DEVICE_ATTR(calibrate, S_IRUGO | S_IWUSR | S_IWGRP,
		oplus_crown_calibrate_show, oplus_crown_calibrate_store);

static struct attribute *oplus_crown_attr_list[] = {
	&dev_attr_suspend.attr,
	&dev_attr_cpi_resolution_x.attr,
	&dev_attr_calibrate.attr,
	NULL,
};

static struct attribute_group oplus_crown_attr_grp = {
	.attrs = oplus_crown_attr_list,
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
static const struct proc_ops crown_cpi_resolution_x_proc_fops = {
	.proc_read = crown_cpi_resolution_x_proc_read,
	.proc_write = crown_cpi_resolution_x_proc_write,
};
#else
static const struct file_operations crown_cpi_resolution_x_proc_fops = {
	.read = crown_cpi_resolution_x_proc_read,
	.write = crown_cpi_resolution_x_proc_write,
};
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
static const struct proc_ops crown_calibrate_proc_fops = {
	.proc_read = crown_calibrate_proc_read,
	.proc_write = crown_calibrate_proc_write,
};
#else
static const struct file_operations crown_calibrate_proc_fops = {
	.read = crown_calibrate_proc_read,
	.write = crown_calibrate_proc_write,
};
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
static const struct proc_ops crown_dump_state_proc_fops = {
	.proc_open = crown_dump_state_proc_open,
	.proc_read = crown_dump_state_proc_read,
};
#else
static const struct file_operations crown_dump_state_proc_fops = {
	.open = crown_dump_state_proc_open,
	.read = crown_dump_state_proc_read,
};
#endif

static int oplus_crown_pinctrl_init(struct oplus_crown_platform_data *pdata)
{
	int err;
	struct device *dev = &pdata->client->dev;

	pdata->pinctrl = devm_pinctrl_get(&(pdata->client->dev));
	if (IS_ERR_OR_NULL(pdata->pinctrl)) {
		err = PTR_ERR(pdata->pinctrl);
		dev_err(dev, "Target does not use pinctrl %d\n", err);
		return err;
	}

	pdata->pinctrl_state_active = pinctrl_lookup_state(pdata->pinctrl,
			PINCTRL_STATE_ACTIVE);
	if (IS_ERR_OR_NULL(pdata->pinctrl_state_active)) {
		err = PTR_ERR(pdata->pinctrl_state_active);
		dev_err(dev, "Can not lookup active pinctrl state %d\n", err);
		return err;
	}

	pdata->pinctrl_state_suspend = pinctrl_lookup_state(pdata->pinctrl,
			PINCTRL_STATE_SUSPEND);
	if (IS_ERR_OR_NULL(pdata->pinctrl_state_suspend)) {
		err = PTR_ERR(pdata->pinctrl_state_suspend);
		dev_err(dev, "Can not lookup suspend pinctrl state %d\n", err);
		return err;
	}

	pdata->pinctrl_state_release = pinctrl_lookup_state(pdata->pinctrl,
			PINCTRL_STATE_RELEASE);
	if (IS_ERR_OR_NULL(pdata->pinctrl_state_release)) {
		err = PTR_ERR(pdata->pinctrl_state_release);
		dev_err(dev, "Can not lookup release pinctrl state %d\n", err);
		return err;
	}
	return 0;
}

int oplus_crown_regulator_init(struct oplus_crown_platform_data *pdata)
{
#if (defined(REGULATOR_VDD) || defined(REGULATOR_VLD))
	struct device *dev = &pdata->client->dev;
#endif

#ifdef REGULATOR_VDD
	pdata->vdd = devm_regulator_get(dev, "vdd");
	if (IS_ERR(pdata->vdd)) {
		dev_err(dev, "Failed to get regulator vdd %ld\n",
					PTR_ERR(pdata->vdd));
		//return PTR_ERR(pdata->vdd);
		pdata->vdd = NULL;
	} else {
		//int err = 0;
		//err = regulator_set_voltage(pdata->vdd, VDD_VTG_MIN_UV, VDD_VTG_MAX_UV);
		//if (err) {
		//	dev_err(dev, "Failed to set voltage for vdd reg %d\n", err);
		//	return err;
		//}
		//err = regulator_set_load(pdata->vdd, VDD_ACTIVE_LOAD_UA);
		//if (err < 0) {
		//	dev_err(dev, "Failed to set opt mode for vdd reg %d\n", err);
		//	return err;
		//}
	}
#endif

#ifdef REGULATOR_VLD
	pdata->vld = devm_regulator_get(dev, "vld");
	if (IS_ERR(pdata->vld)) {
		dev_err(dev, "Failed to get regulator vld %ld\n",
					PTR_ERR(pdata->vld));
		return PTR_ERR(pdata->vld);
	} else {
		//int err = 0;
		//err = regulator_set_voltage(pdata->vld, VLD_VTG_MIN_UV, VLD_VTG_MAX_UV);
		//if (err) {
		//	dev_err(dev, "Failed to set voltage for vld reg %d\n", err);
		//	return err;
		//}
		//err = regulator_set_load(pdata->vld, VLD_ACTIVE_LOAD_UA);
		//if (err < 0) {
		//	dev_err(dev, "Failed to set opt mode for vld reg %d\n", err);
		//	return err;
		//}
	}
#endif
	return 0;
}

static int oplus_crown_power_on(struct oplus_crown_platform_data *pdata, bool on)
{
	int err = 0;
	struct device *dev = &pdata->client->dev;

	dev_err(dev, "%s(%d)\n", __func__, on);
	if (on) {
		#ifdef REGULATOR_VDD
		err = regulator_enable(pdata->vdd);
		if (err) {
			dev_err(dev, "Failed to enable vdd reg %d\n", err);
			return err;
		}
		//usleep_range(DELAY_BETWEEN_REG_US, DELAY_BETWEEN_REG_US + 1);
		#endif /*REGULATOR_VDD*/

		#ifdef REGULATOR_VLD
		err = regulator_enable(pdata->vld);
		dev_err(dev, "regulator_enable vld \n");
		if (err) {
			dev_err(dev, "Failed to enable vld reg %d\n", err);
			return err;
		}
		usleep_range(DELAY_BETWEEN_REG_US, DELAY_BETWEEN_REG_US + 100);
		#endif

		/*
		 * Initialize sensor after some delay, when vdd
		 * regulator is enabled
		 */
		if ((0 != pdata->switch_state) && (!oplus_crown_sensor_init(pdata))) {
			err = -ENODEV;
			dev_err(dev, "Failed to initialize sensor %d\n", err);
			return err;
		}

		remove_dirty_data(pdata);

	} else {
		#ifdef REGULATOR_VLD
		dev_err(dev, "regulator_disable vld \n");
		err = regulator_disable(pdata->vld);
		if (err) {
			dev_err(dev, "Failed to disable vld reg %d\n", err);
			return err;
		}
		usleep_range(DELAY_POWERDOWN, DELAY_POWERDOWN + 100);
		#endif

		#ifdef REGULATOR_VDD
		err = regulator_disable(pdata->vdd);
		if (err) {
			dev_err(dev, "Failed to disable vdd reg %d\n", err);
			return err;
		}
		#endif
	}

	return 0;
}

static int oplus_crown_parse_dt(struct device *dev,
		struct oplus_crown_platform_data *pdata)
{
	struct device_node *np = dev->of_node;
	int ret = 0;
	u32 temp_val;

	pdata->irq_gpio = of_get_named_gpio_flags(np, "oplus_crown,irq-gpio",
						0, NULL);

	ret = of_property_read_u32(np, "oplus_crown,notify_id",
					&temp_val);
	if (!ret) {
		pdata->notify_id = temp_val;
	} else {
		pdata->notify_id = 0;
		ret = 0;
	}

	ret = of_property_read_u32(np, "oplus_crown,force_init",
					&temp_val);
	if (!ret) {
		pdata->force_init = temp_val;
	} else {
		pdata->force_init = 0;
		ret = 0;
	}

	ret = of_property_read_u32(np, "oplus_crown,default_init",
					&temp_val);
	if (!ret) {
		pdata->default_init = temp_val;
	} else {
		pdata->default_init = 0;
		ret = 0;
	}

	return ret;
}

#ifdef CONFIG_OPLUS_SWITCH
static int switch_notifier_callback(struct notifier_block *self,
		unsigned long event, void *data)
{
	struct oplus_crown_platform_data *pdata =
		container_of(self, struct oplus_crown_platform_data, switch_nb);
	char *switch_states = (char *)data;
	if(NULL == switch_states) {
		pr_err("pdata error\n");
		return -1;
	}

	//pr_info("switch callback enable:%d current:%d\n", switch_states[1], switch_states[2]);
	if(((event == SWITCH_PRE_ACTION)
			&& (switch_states[1] == SWITCH_DISCONNECT))        /*switch to mcu*/
		|| ((event == SWITCH_POST_ACTION)
			&& (switch_states[1] != SWITCH_DISCONNECT))) {/*switch to ap*/
		pdata->switch_state = switch_states[1];

		if (DRM_BLANK_ACTIVED == pdata->drm_state) {
			if (SWITCH_DISCONNECT == pdata->switch_state) {
				if (0 != pdata->irq_en) {
					dev_info(&(pdata->client->dev), "%s: disable_irq!(%d)\n",
						__func__, pdata->switch_state);
					pdata->irq_en = 0;
					disable_irq_nosync(pdata->client->irq);
					irq_set_irq_type(pdata->client->irq, IRQ_TYPE_EDGE_RISING);
				}
			} else {
				if (0 == pdata->irq_en) {
					dev_info(&(pdata->client->dev), "%s: enable_irq!(%d)\n",
						__func__, pdata->switch_state);
					pdata->irq_en = 1;
					irq_set_irq_type(pdata->client->irq, IRQ_TYPE_EDGE_FALLING|IRQ_TYPE_LEVEL_LOW);
					enable_irq(pdata->client->irq);
				}
			}
		}
	}

	return 0;
}
#endif


static int oplus_crown_suspend(struct device *dev)
{
	struct oplus_crown_platform_data *pdata =
		(struct oplus_crown_platform_data *) dev_get_drvdata(dev);
	struct oplus_crown_sensor_ops * ops = get_crown_ops(pdata);

	if (DRM_BLANK_IDLE == pdata->drm_state) {
		dev_info(&(pdata->client->dev), "%s: already suspend\n",
			__func__);
		return 0;
	}

	if (0 != pdata->irq_en) {
		dev_info(&(pdata->client->dev), "%s: disable_irq!(%d)\n",
			__func__, pdata->drm_state);
		disable_irq_nosync(pdata->client->irq);
		irq_set_irq_type(pdata->client->irq, IRQ_TYPE_EDGE_RISING);
		pdata->irq_en = 0;
	}

	if ((pdata->pinctrl) && (pdata->pinctrl_state_release)) {
		int err = pinctrl_select_state(pdata->pinctrl, pdata->pinctrl_state_release);
		if (err < 0) {
			dev_err(&(pdata->client->dev), "Could not set pin to release state %d\n", err);
		}
	} else {
		dev_err(&(pdata->client->dev), "pdata->pinctrl or pdata->pinctrl_state_release is null\n");
	}


	if (pdata->drm_state == DRM_BLANK_ACTIVED) {
		if (SWITCH_DISCONNECT != pdata->switch_state) {
			if (ops != NULL && ops->sw_power != NULL) {
				ops->sw_power(pdata, false);
			}
			set_cpi_res_x_reg(pdata, 0);
			remove_dirty_data(pdata);
		}
		if (oplus_crown_power_on(pdata, false) < 0) {
			dev_err(&(pdata->client->dev), "Failed to disable regulators\n");
		}
	}

	pdata->drm_state = DRM_BLANK_IDLE;

	return 0;
}

#if defined(CONFIG_PANEL_NOTIFIER)
static void oplus_crown_recovery(void)
{
	struct panel_event_notification notifier;
	enum panel_event_notifier_tag panel_type;

	panel_type = PANEL_EVENT_NOTIFICATION_PRIMARY;

	memset(&notifier, 0, sizeof(notifier));
	notifier.panel = (void *)OPLUS_PANEL_EVENT_MAGIC_NUM;
	notifier.notif_type = DRM_PANEL_EVENT_CROWN_DEAD;

	panel_event_notification_trigger(panel_type, &notifier);
}
#elif defined(CONFIG_DRM)
extern int msm_drm_notifier_call_chain(unsigned long val, void *v);

static void oplus_crown_recovery(void)
{
	int blank;
	struct msm_drm_notifier notifier_data;

	blank = 0;
	notifier_data.data = &blank;
	notifier_data.id = 0;
	msm_drm_notifier_call_chain(MSM_DRM_EVENT_CROWN_DEAD,
		&notifier_data);

}
#endif

static int oplus_crown_resume(struct device *dev)
{
	struct oplus_crown_platform_data *pdata =
		(struct oplus_crown_platform_data *) dev_get_drvdata(dev);
	struct oplus_crown_sensor_ops * ops = get_crown_ops(pdata);
	static int retry_time = 3;

	if (DRM_BLANK_ACTIVED == pdata->drm_state) {
		dev_info(&(pdata->client->dev), "%s: already resume\n",
			__func__);
		goto on_error;
	}

	if (oplus_crown_power_on(pdata, true) < 0) {
		dev_err(&(pdata->client->dev), "Failed to enable regulators\n");
		if (retry_time > 0) {
			retry_time --;
			msleep(10);
			dev_err(&(pdata->client->dev), "%s: recovery\n", __func__);
			oplus_crown_recovery();
			goto on_error;
		}
	} else {
		retry_time = 3;
	}

	if (SWITCH_DISCONNECT != pdata->switch_state) {
		dev_info(&(pdata->client->dev), "%s: enable_irq\n", __func__);
		if ((pdata->pinctrl) && (pdata->pinctrl_state_active)) {
			int err = pinctrl_select_state(pdata->pinctrl, pdata->pinctrl_state_active);
			if (err < 0) {
				dev_err(&(pdata->client->dev), "Could not set pin to active state %d\n", err);
			}
		} else {
			dev_err(&(pdata->client->dev), "pdata->pinctrl or pdata->pinctrl_state_active is null\n");
		}

		if (0 == pdata->irq_en) {
			pdata->irq_en = 1;
			irq_set_irq_type(pdata->client->irq, IRQ_TYPE_EDGE_FALLING|IRQ_TYPE_LEVEL_LOW);
			enable_irq(pdata->client->irq);
			set_cpi_res_x_reg(pdata, static_xcpi);
			if (ops != NULL && ops->sw_power != NULL) {
				ops->sw_power(pdata, true);
			}
		}
	}

on_error:
	pdata->drm_state = DRM_BLANK_ACTIVED;

	return 0;
}

static void oplus_crown_pwron_retry_work(struct work_struct *work)
{
	struct oplus_crown_platform_data *pdata = container_of(
			work, struct oplus_crown_platform_data, pwr_work);
	struct device *dev = &pdata->client->dev;

	oplus_crown_resume(dev);
}

#if defined(CONFIG_PANEL_NOTIFIER)
static void panel_event_notifier_callback(enum panel_event_notifier_tag tag,
		struct panel_event_notification *notification, void *client_data)
{
	struct oplus_crown_platform_data *pdata = client_data;
	struct device *dev = &pdata->client->dev;

	if (notification) {
		if ((notification->notif_type == DRM_PANEL_EVENT_UNBLANK) && (!notification->notif_data.early_trigger)) {
			dev_info(&(pdata->client->dev), "%s: UNBLANK!\n", __func__);
			queue_work(pdata->pwr_on_workqueue, &pdata->pwr_work);
		} else if ((notification->notif_type == DRM_PANEL_EVENT_BLANK) && (notification->notif_data.early_trigger)) {
			dev_info(&(pdata->client->dev), "%s: BLANK!(%d)\n", __func__, pdata->drm_state);
			cancel_work_sync(&pdata->pwr_work);
			oplus_crown_suspend(dev);
		}
	}
	return;
}

static void oplus_crown_setup_panel_event_notifier(struct oplus_crown_platform_data *pdata)
{
	pdata->drm_state = DRM_BLANK_ACTIVED;

	pdata->panel_event_notifier = panel_event_notifier_register(
		(pdata->notify_id == 1 ? PANEL_EVENT_NOTIFICATION_SECONDARY : PANEL_EVENT_NOTIFICATION_PRIMARY),
		PANEL_EVENT_NOTIFIER_CLIENT_CROWN,
		(void *)OPLUS_PANEL_EVENT_MAGIC_NUM, &panel_event_notifier_callback, pdata);
}
#elif defined(CONFIG_DRM)
static int drm_notifier_callback(struct notifier_block *self,
		unsigned long event, void *data)
{
	struct oplus_crown_platform_data *pdata =
		container_of(self, struct oplus_crown_platform_data, drm_notifier);
	struct device *dev = &pdata->client->dev;
	struct msm_drm_notifier *evdata = data;
	int *blank;
	int equivalent_blank = 0;

	if ((event != MSM_DRM_EVENT_BLANK && event != MSM_DRM_EVENT_IDLE) || !evdata)
		goto exit;

	if (evdata->id != pdata->notify_id) {
		goto exit;
	}

	blank = evdata->data;
	if(MSM_DRM_EVENT_IDLE == event) {
		if(MSM_DRM_ENTER_IDLE == *blank) {
			equivalent_blank = MSM_DRM_BLANK_POWERDOWN;
			blank = &equivalent_blank;
			dev_info(&(pdata->client->dev), "%s: Handle enter idle event as POWERDOWN!\n", __func__);
		} else if (MSM_DRM_EXIT_IDLE == *blank) {
			equivalent_blank = MSM_DRM_BLANK_UNBLANK;
			blank = &equivalent_blank;
			dev_info(&(pdata->client->dev), "%s: Handle exit idle as UNBLANK!\n", __func__);
		} else {
			dev_err(&(pdata->client->dev), "%s: Error:unknown event(%d) and blank(%d)\n",
				__func__, event, *blank);
			goto exit;
		}
	}

	if (*blank == DRM_BLANK_ACTIVED) {
		dev_info(&(pdata->client->dev), "%s: UNBLANK!\n", __func__);
		queue_work(pdata->pwr_on_workqueue, &pdata->pwr_work);
	} else if (*blank != DRM_BLANK_ACTIVED /*== MSM_DRM_BLANK_POWERDOWN*/) {
		cancel_work_sync(&pdata->pwr_work);
		dev_info(&(pdata->client->dev), "%s: BLANK!(%d)\n", __func__, pdata->drm_state);
		oplus_crown_suspend(dev);
	}

exit:
	return 0;
}

static void oplus_crown_setup_drm_notifier(struct oplus_crown_platform_data *pdata)
{
	int rc;

	pdata->drm_state = DRM_BLANK_ACTIVED;

	pdata->drm_notifier.notifier_call = drm_notifier_callback;

	rc = msm_drm_register_client(&pdata->drm_notifier);
	if (rc)
		dev_err(&(pdata->client->dev), "Unable to register drm_notifier: %d\n", rc);
}
#endif

static int init_proc_group(struct oplus_crown_platform_data *pdata) {
	int ret = 0;
	pdata->proc_entry = proc_mkdir("crown", NULL);
	if (IS_ERR_OR_NULL(pdata->proc_entry)) {
		dev_err(&pdata->client->dev, "%s: Error, could not create base proc directory\n",
				__func__);
		goto unregister_base_dir;
	}

	if (IS_ERR_OR_NULL(proc_create_data("cpi_resolution_x", S_IRUGO | S_IWUGO,
		pdata->proc_entry, &crown_cpi_resolution_x_proc_fops, pdata))) {
		dev_err(&pdata->client->dev, "%s: Error, could not create proc %s\n",
				__func__, "cpi_resolution_x");
		goto unregister_base_dir;
	}

	if (IS_ERR_OR_NULL(proc_create_data("calibrate", S_IRUGO | S_IWUGO,
		pdata->proc_entry, &crown_calibrate_proc_fops, pdata))) {
		dev_err(&pdata->client->dev, "%s: Error, could not create proc %s\n",
				__func__, "calibrate");
		goto unregister_base_dir;
	}

	if (IS_ERR_OR_NULL(proc_create_data("dump_state", S_IRUGO,
		pdata->proc_entry, &crown_dump_state_proc_fops, pdata))) {
		dev_err(&pdata->client->dev, "%s: Error, could not create proc %s\n",
				__func__, "dump_state");
		goto unregister_base_dir;
	}

	return ret;

unregister_base_dir:
	remove_proc_entry("crown", NULL);
	pdata->proc_entry = NULL;
	return ret;
}

static int oplus_crown_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int err = 0;
	struct oplus_crown_platform_data *pdata;
	struct input_dev *input;
	struct device *dev = &client->dev;
	static bool already_init = false;

	if (already_init) {
		dev_info(dev, "%s: already init, skip\n", __func__);
		return 0;
	}

	if (!oplus_support_crown()) {
		dev_err(dev, "%s: not support\n", __func__);
		return 0;
	}

	dev_info(dev, "%s: entry\n", __func__);

	err = i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE);
	if (err < 0) {
		dev_err(dev, "I2C not supported\n");
		return -ENXIO;
	}

	if (client->dev.of_node) {
		pdata = devm_kzalloc(dev, sizeof(struct oplus_crown_platform_data),
				GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;
		err = oplus_crown_parse_dt(dev, pdata);
		if (err) {
			dev_err(dev, "DT parsing failed, errno:%d\n", err);
			return err;
		}
	} else {
		pdata = client->dev.platform_data;
		if (!pdata) {
			dev_err(dev, "Invalid oplus crown data\n");
			return -EINVAL;
		}
	}
	pdata->client = client;

	input = devm_input_allocate_device(dev);
	if (!input) {
		dev_err(dev, "Failed to alloc input device\n");
		return -ENOMEM;
	}

	input_set_capability(input, EV_REL, REL_WHEEL);
#ifdef PRESS_KEY_ENABLE
	if (pdata->press_en)
		input_set_capability(input, EV_KEY, pdata->press_keycode);
#endif

	i2c_set_clientdata(client, pdata);
	input_set_drvdata(input, pdata);
	input->name = OPLUS_CROWN_DEV_NAME;

	pdata->input = input;
	err = input_register_device(pdata->input);
	if (err < 0) {
		dev_err(dev, "Failed to register input device\n");
		return err;
	}

	err = oplus_crown_pinctrl_init(pdata);
	if (!err && pdata->pinctrl) {
		/*
		 * Pinctrl handle is optional. If pinctrl handle is found
		 * let pins to be configured in active state. If not
		 * found continue further without error.
		 */
		err = pinctrl_select_state(pdata->pinctrl,
				pdata->pinctrl_state_active);
		if (err < 0)
			dev_err(dev, "Could not set pin to active state %d\n", err);
	} else {
		if (gpio_is_valid(pdata->irq_gpio)) {
			err = devm_gpio_request(dev, pdata->irq_gpio,
						"oplus_crown_irq_gpio");
			if (err) {
				dev_err(dev, "Couldn't request gpio %d\n", err);
				return err;
			}
			err = gpio_direction_input(pdata->irq_gpio);
			if (err) {
				dev_err(dev, "Couldn't set dir for gpio %d\n", err);
				return err;
			}

		} else {
			dev_err(dev, "Invalid gpio %d\n", pdata->irq_gpio);
			return -EINVAL;
		}
	}

	err = oplus_crown_regulator_init(pdata);
	if (err) {
		dev_err(dev, "Failed to init regulator, %d\n", err);
		return err;
	}

#ifdef CONFIG_OPLUS_SWITCH
	pdata->switch_state = 1;
#endif

	if (!pdata->force_init) {
		err = oplus_crown_power_on(pdata, true);
		if (err) {
			dev_err(dev, "Failed to power-on the sensor %d\n", err);
			if (!pdata->default_init) {
				goto err_power_on;
			}
		}
	}

	pdata->ws = wakeup_source_register(dev, "oplus_crown");
	if (!pdata->ws) {
		err = -ENOMEM;
		goto err_wakeup_source_register;
	}

	err = sysfs_create_group(&(input->dev.kobj), &oplus_crown_attr_grp);
	if (err) {
		dev_err(dev, "Failed to create sysfs group, errno:%d\n", err);
		goto err_sysfs_create;
	}

	pdata->irq_en = 1;

	err = devm_request_threaded_irq(dev, client->irq, NULL, oplus_crown_irq_handler,
			IRQF_ONESHOT | IRQF_TRIGGER_FALLING | IRQF_TRIGGER_LOW,
			"oplus_crown_irq", pdata);

	if (err) {
		dev_err(dev, "Req irq %d failed, errno:%d\n", client->irq, err);
		goto err_request_threaded_irq;
	}

#ifdef CONFIG_OPLUS_SWITCH
	pdata->switch_nb.notifier_call = switch_notifier_callback;
	err = oplus_switch_notifier_register(SWITCH_WHEEL, dev, &pdata->switch_nb);
	if (err) {
		dev_err(dev, "%s: oplus_switch_notifier_register failed\n", __func__);
	}
#endif

	INIT_WORK(&pdata->pwr_work, oplus_crown_pwron_retry_work);
	pdata->pwr_on_workqueue = create_singlethread_workqueue("oplus_crown_pwron_wq");
	if(!pdata->pwr_on_workqueue)
		dev_err(dev, "%s: create oplus crown power workqueue failed\n", __func__);

#if defined(CONFIG_PANEL_NOTIFIER)
	oplus_crown_setup_panel_event_notifier(pdata);
#elif defined(CONFIG_DRM)
	oplus_crown_setup_drm_notifier(pdata);
#endif

	init_proc_group(pdata);

	if (pdata->force_init) {
		pdata->drm_state = DRM_BLANK_IDLE;
		if (0 != pdata->irq_en) {
			dev_info(&(pdata->client->dev), "%s: disable_irq!\n", __func__);
			disable_irq_nosync(pdata->client->irq);
			irq_set_irq_type(pdata->client->irq, IRQ_TYPE_EDGE_RISING);
			pdata->irq_en = 0;
		}
	}

	already_init = true;

	dev_info(dev, "%s: succeed\n", __func__);
	return 0;

err_request_threaded_irq:
err_sysfs_create:
	wakeup_source_unregister(pdata->ws);
err_power_on:
	#ifdef REGULATOR_VDD
	//regulator_set_load(pdata->vdd, 0);
	#endif
	#ifdef REGULATOR_VLD
	//regulator_set_load(pdata->vld, 0);
	#endif
	if (oplus_crown_power_on(pdata, false) < 0)
		dev_err(dev, "Failed to disable regulators\n");
err_wakeup_source_register:
	/*if (pdata->pinctrl)
		if (pinctrl_select_state(pdata->pinctrl,
				pdata->pinctrl_state_release) < 0)
			dev_err(dev, "Couldn't set pin to release state\n");*/

	return err;
}

static int oplus_crown_i2c_remove(struct i2c_client *client)
{
	struct oplus_crown_platform_data *pdata = i2c_get_clientdata(client);
	struct device *dev = &pdata->client->dev;

	if (!oplus_support_crown()) {
		return 0;
	}

	if(pdata->pwr_on_workqueue)
		destroy_workqueue(pdata->pwr_on_workqueue);
#if defined(CONFIG_PANEL_NOTIFIER)
	panel_event_notifier_unregister(pdata->panel_event_notifier);
#elif defined(CONFIG_DRM)
	msm_drm_unregister_client(&pdata->drm_notifier);
#endif

#ifdef CONFIG_OPLUS_SWITCH
	if (oplus_switch_notifier_unregister(SWITCH_WHEEL, dev, &pdata->switch_nb)) {
		dev_err(dev, "%s: oplus_switch_notifier_unregister failed\n", __func__);
	}
#endif

	wakeup_source_unregister(pdata->ws);

	sysfs_remove_group(&(pdata->input->dev.kobj), &oplus_crown_attr_grp);
	if (pdata->pinctrl)
		if (pinctrl_select_state(pdata->pinctrl,
				pdata->pinctrl_state_release) < 0)
			dev_err(dev, "Couldn't set pin to release state\n");
	#ifdef REGULATOR_VDD
	regulator_set_load(pdata->vdd, 0);
	#endif
	#ifdef REGULATOR_VLD
	//regulator_set_load(pdata->vld, 0);
	#endif
	//oplus_crown_power_on(pdata, false);
	return 0;
}

static const struct i2c_device_id oplus_crown_device_id[] = {
	{OPLUS_CROWN_DEV_NAME, 0},
	{}
};
MODULE_DEVICE_TABLE(i2c, oplus_crown_device_id);

static const struct of_device_id oplus_crown_match_table[] = {
	{ .compatible = "pixart,pat9125",},
	{ .compatible = "mixosense,mot6010",},
	{ },
};

static struct i2c_driver oplus_crown_i2c_driver = {
	.driver = {
		   .name = OPLUS_CROWN_DEV_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table = oplus_crown_match_table,
		   },
	.probe = oplus_crown_i2c_probe,
	.remove = oplus_crown_i2c_remove,
	.id_table = oplus_crown_device_id,
};

static int __init oplus_crown_init(void)
{
#ifndef OPLUS_CROWN_GET_CPI_X_BY_SETUP
	set_cpi_resolution_x(cpi_x);
#endif /* OPLUS_CROWN_GET_CPI_X_BY_SETUP */
	return i2c_add_driver(&oplus_crown_i2c_driver);
}

static void __exit oplus_crown_exit(void)
{

	i2c_del_driver(&oplus_crown_i2c_driver);
}

late_initcall(oplus_crown_init);
module_exit(oplus_crown_exit);

MODULE_AUTHOR("oplus");
MODULE_DESCRIPTION("oplus crown driver");
MODULE_LICENSE("GPL");

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
MODULE_SOFTDEP("pre: focaltech_fts zinitix");
#endif
