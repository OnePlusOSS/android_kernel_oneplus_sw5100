#include "oplus_crown.h"
#include "mot6010.h"

extern unsigned int get_project(void);

extern int stable_threshold;

struct mot6010_sensor_data {
	int ic_delta_x;
	int driver_delta_x;
	int calibrate_value;
	int move_distance;
	int ads_distance;
	int last_print_log;
};

static struct mot6010_sensor_data sdata = {
	.ic_delta_x = 0,
	.driver_delta_x = 0,
	.calibrate_value = 0,
	.move_distance = 0,
	.ads_distance = 0,
	.last_print_log = 0,
};

static int callisto_2x_init(struct oplus_crown_platform_data *pdata)
{
	struct i2c_client *client = pdata->client;
	int ret         = 0;
	int read_id_ok  = 0;
	uint8_t id_high = 0x00;
	uint8_t id_low  = 0x00;
	uint8_t config2 = 0x00;
	unsigned int project = get_project();

	dev_info(&client->dev, "%s: start\n", __func__);

	write_data(pdata->client, (u8)MIXOSENSE_MOT6010_CONFIGURATION_REG, MIXOSENSE_MOT6010_REG_POWERDOWN_DIS);
	usleep_range(1000, 1500);

	write_data(client, 0x09, 0x5A);
	write_data(client, 0x3C, 0xAC);
	write_data(client, 0x09, 0xA5);
	write_data(client, 0x6D, 0x39);
	id_high = read_data(client, 0x00);  // Read chip-id high byte
	id_low = read_data(client, 0x01);  // Read chip-id low byte

	dev_info(&client->dev, "%s: id %02x%02x\n", __func__, id_high, id_low);
	if (id_high == MIXOSENSE_MOT6010_REG_PROTECT_ID_HIGH &&
		id_low == MIXOSENSE_MOT6010_REG_PROTECT_ID_LOW) {
		read_id_ok = 1;
	}

	if (read_id_ok) {
		write_data(client, 0x19, 0x04);
		write_data(client, 0x3E, 0x73);
		write_data(client, 0x48, 0x0F);
		write_data(client, 0x1F, 0xFF);
		write_data(client, 0x73, 0x08);
		write_data(client, 0x68, 0x67);
		write_data(client, 0x7D, 0x60);
		write_data(client, 0x60, 0x00);

		if (project != 24908 && project != 24912 && project != 24965 && project != 24966) {
			dev_info(&client->dev, "%s: Adjust exposure\n", __func__);
			write_data(client, 0x59, 0x6A);
			write_data(client, 0x5A, 0x4E);
			write_data(client, 0x52, 0x70);
		}

		write_data(client, 0x0D, 0x20);  // If cpi value is not assigned, use default value
		write_data(client, 0x0E, 0x20);  // If cpi value is not assigned, use default value

		write_data(client, 0x19, 0x04);  // sensor_option, WO, Enable 12bit mode;
		config2 = read_data(client, 0x19);
		dev_info(&client->dev, "%s: Config2[0x19] %02x\n", __func__, config2);
		ret = 0;
		dev_info(&client->dev, "%s: success\n", __func__);
	} else {
		ret = -1;
		dev_err(&client->dev, "%s: failed\n", __func__);
	}

	return ret;
}

int mot6010_sensor_init(struct oplus_crown_platform_data *pdata)
{

	int ret = 0;
	uint8_t chip_hardware_id = 0;
	uint8_t sensor_pid = 0;
	struct i2c_client *client = pdata->client;

	//clear data
	sdata.ic_delta_x = 0,
	sdata.driver_delta_x = 0;

	chip_hardware_id = read_data(client, 0x1B);
	dev_info(&client->dev, "%s: reg[0x%02X]=0x%02X\n", __func__, 0x1B, chip_hardware_id);

	ret = callisto_2x_init(pdata);
	if (ret) {
		goto on_error;
	}

	//refresh sensor id
	sensor_pid = read_data(client, OPLUS_CROWN_PRODUCT_ID_REG);
	dev_info(&client->dev, "%s: MXS init, sensor_pid: 0x%x\n", __func__, sensor_pid);
	set_oplus_crown_sensor_id(pdata, sensor_pid);

	if (sensor_pid != MIXOSENSE_MOT6010_SENSOR_ID) {
		goto on_error;
	}

	dev_info(&client->dev, "%s: sensor init success!\n", __func__);
	return 0;

on_error:
	dev_err(&client->dev, "%s: sensor init failed!\n", __func__);
	return -1;
}


int mot6010_sw_power(struct oplus_crown_platform_data *pdata, bool on)
{
	if (on) {
		//do this in callisto_2x_init()
		//write_data(pdata->client, (u8)MIXOSENSE_MOT6010_CONFIGURATION_REG, MIXOSENSE_MOT6010_REG_POWERDOWN_DIS);
	} else {
		write_data(pdata->client, (u8)MIXOSENSE_MOT6010_CONFIGURATION_REG, MIXOSENSE_MOT6010_REG_POWERDOWN_EN);
	}

	return 0;
}

int mot6010_set_cpi_x_reg(struct oplus_crown_platform_data *pdata, long val) {
	//keep cpi_x_reg 0x20
	return 0;
}

static int mot6010_irq_handler(struct oplus_crown_platform_data *pdata, int irq)
{
	u8 motion;
	u16 delta_xy_hi = 0, delta_x = 0, delta_y = 0;
	struct input_dev *ipdev = pdata->input;
	struct device *dev = &pdata->client->dev;
	int sys_cpi_x = get_cpi_resolution_x();
	int ic_cpi_x = 0x20;
	int vendor_cpi_x = 0x2E;
	int step_delta_x = 0;
	int report_delta_x = 0;
	int report_total_delta_x = 0;
	int i;
	bool start_report = false;

	__pm_stay_awake(pdata->ws);

	motion = read_data(pdata->client, 0x02);
	while((motion & 0x80) == 0x80) {
		delta_x = read_data(pdata->client, 0x03);
		delta_y = read_data(pdata->client, 0x04);
		delta_xy_hi = read_data(pdata->client, 0x12);
		//dev_err(dev, "TEST: motion = %x, delta_x = %x, delta_y = %x, delts_xy = %x\n",
		//	motion, delta_x, delta_y, delta_xy_hi);

		delta_x |= (delta_xy_hi&0x00f0) << 4;
		if (delta_x & 0x0800) {
			delta_x |= 0xf000;
			step_delta_x = 0xffff0000 | delta_x;
		} else {
			step_delta_x = delta_x;
		}

		sdata.ic_delta_x += ((get_display_vflip()) ? -step_delta_x : step_delta_x);
		report_total_delta_x = sdata.ic_delta_x * sys_cpi_x / (ic_cpi_x * vendor_cpi_x);

		if (pdata->calibrateing) {
			if (sdata.calibrate_value != report_total_delta_x) {
				sdata.calibrate_value = report_total_delta_x;
				dev_err(dev, "origin = %d, calibrate_value = %d\n",
						sdata.ic_delta_x, sdata.calibrate_value);
			}
		} else {
			report_delta_x = report_total_delta_x - sdata.driver_delta_x;
			if (!start_report) {
				dev_err(dev, "report_delta_x = %d, stable_threshold = %d\n",
							report_delta_x, stable_threshold);
				if (report_delta_x > -stable_threshold && report_delta_x < stable_threshold) {
					dev_err(dev, "clear report\n");
					report_delta_x = 0;
				} else {
					start_report = true;
				}
			}

			if (report_delta_x != 0) {
				/* Send driver_delta_x as REL_WHEEL for rotation */
				input_report_rel(ipdev, REL_WHEEL, report_delta_x);
				input_sync(ipdev);

				if (report_delta_x > 0) {
					sdata.ads_distance += report_delta_x;
				} else {
					sdata.ads_distance -= report_delta_x;
				}
				sdata.move_distance += report_delta_x;
				if (sdata.ads_distance - sdata.last_print_log > 90) {
					dev_info(dev, "move = %d, ads = %d\n", sdata.move_distance, sdata.ads_distance);
					sdata.last_print_log = sdata.ads_distance;
				}

				sdata.driver_delta_x = report_total_delta_x;
			}
		}

		usleep_range(OPLUS_CROWN_SAMPLING_PERIOD_US_MIN, OPLUS_CROWN_SAMPLING_PERIOD_US_MAX);
		if (unlikely(0 == pdata->irq_en)) {
			motion = 0;
			pr_err("irq disabled, exit isr\n");
			goto Exit_IRQ;
		}
		motion = read_data(pdata->client, 0x02);
		if ((!pdata->calibrateing) && !((motion & 0x80) == 0x80)) {
			for (i=0; i < auto_suspend / OPLUS_CROWN_SAMPLING_PERIOD_US_MIN; i++) {
				usleep_range(OPLUS_CROWN_SAMPLING_PERIOD_US_MIN, OPLUS_CROWN_SAMPLING_PERIOD_US_MAX);
				if (unlikely(0 == pdata->irq_en)) {
					motion = 0;
					pr_err("irq disabled, exit isr\n");
					goto Exit_IRQ;
				}
				motion = read_data(pdata->client, 0x02);
				if ((motion & 0x80) == 0x80) {
					break;
				}
			}
		}
	}

Exit_IRQ:
	if (!(pdata->calibrateing)) {
		//clear data
		sdata.ic_delta_x = 0,
		sdata.driver_delta_x = 0;
	}
	__pm_relax(pdata->ws);
	return 0;
}

void mot6010_clear_data(struct oplus_crown_platform_data *pdata)
{
	u8 motion = read_data(pdata->client, 0x02);
	u8 REL_x = 0, REL_y = 0, REL_xh = 0;
	if ((motion & 0x80) == 0x80) {
		/*Ignore dirty data*/
		REL_x = read_data(pdata->client, 0x03);
		REL_y = read_data(pdata->client, 0x04);
		REL_xh= read_data(pdata->client, 0x12);
	}
	dev_info(&pdata->client->dev, "%s: Ignore dirty data(0x%02x, 0x%02x, 0x%02x, 0x%02x)\n",
		__func__, motion, REL_x, REL_y, REL_xh);

	//clear data
	sdata.ic_delta_x = 0,
	sdata.driver_delta_x = 0;
}

void mot6010_calibrate(struct oplus_crown_platform_data *pdata, bool start)
{
	if (start) {
		dev_info(&pdata->client->dev, "%s: start calibrate\n", __func__);
		//clear data
		sdata.calibrate_value = 0;
	} else {
		dev_info(&pdata->client->dev, "%s: stop calibrate\n", __func__);
	}
	sdata.ic_delta_x = 0,
	sdata.driver_delta_x = 0;
}

int mot6010_get_calibrate_value(struct oplus_crown_platform_data *pdata)
{
	return -(sdata.calibrate_value);
}

void mot6010_dump_state(struct oplus_crown_platform_data *pdata, char * buf, int len)
{
	u8 reg = 0x00;
	u8 val = 0x00;
	int l = 0;
	for (reg = 0x00; reg <= 0x7F; reg++) {
		val = read_data(pdata->client, reg);
		if (l+1<len) {
			l += snprintf(&buf[l], len - l - 1, "%02X:%02X\n", reg, val);
		}
	}
}

struct oplus_crown_sensor_ops mot6010_ops = {
	.init = mot6010_sensor_init,
	.sw_power = mot6010_sw_power,
	.set_cpi_x_reg = mot6010_set_cpi_x_reg,
	.irq_handler = mot6010_irq_handler,
	.clear_data = mot6010_clear_data,
	.calibrate = mot6010_calibrate,
	.get_calibrate_value = mot6010_get_calibrate_value,
	.dump_state = mot6010_dump_state,
};

