#include "oplus_crown.h"
#include "pat9125.h"


struct pat9125_sensor_data {
	int move_distance;
	int ads_distance;
	int last_print_log;
};

static struct pat9125_sensor_data sdata = {
	.move_distance = 0,
	.ads_distance = 0,
	.last_print_log = 0,
};

int pat9125_sensor_init(struct oplus_crown_platform_data *pdata)
{
	bool ret = TRUE;
	u8 read_value = 0;
	struct i2c_client *client = pdata->client;

	/*
	* PAT9125 sensor recommended settings:
	* switch to bank0, not allowed to perform oplus_crown_write_read
	*/
	write_data(client, PIXART_PAT9125_SELECT_BANK_REG,
			PIXART_PAT9125_BANK0);

	/*
	* software reset (i.e. set bit7 to 1).
	* It will reset to 0 automatically
	* so perform OTS_RegWriteRead is not allowed.
	* This action device will return nak,
	* and the supplier confirms that it does not care.
	*/
	write_data(client, PIXART_PAT9125_CONFIG_REG,
			PIXART_PAT9125_RESET);

	/* delay 15ms */
	usleep_range(RESET_DELAY_US, RESET_DELAY_US + 100);

	read_value = read_data(client, PIXART_PAT9125_CONFIG_REG);
	if ((PIXART_PAT9125_RESET & 0x7f) == read_value) {
		dev_info(&client->dev, "%s: reset cmd success\n", __func__);
	} else {
		dev_err(&client->dev, "%s: reset cmd failed\n", __func__);
		goto on_error;
	}

	/* disable write protect */
	ret = oplus_crown_write_read(client, PIXART_PAT9125_WRITE_PROTECT_REG,
			PIXART_PAT9125_DISABLE_WRITE_PROTECT);
	if (!ret)
		goto on_error;

	/* set X-axis resolution (depends on application) */
	ret = oplus_crown_write_read(client, PIXART_PAT9125_SET_CPI_RES_X_REG, static_xcpi);
			//OPLUS_CROWN_CPI_RESOLUTION_X_STATIC);
	if (!ret)
		goto on_error;

	/* set Y-axis resolution (depends on application) */
	ret = oplus_crown_write_read(client, PIXART_PAT9125_SET_CPI_RES_Y_REG,
			PIXART_PAT9125_CPI_RESOLUTION_Y);
	if (!ret)
		goto on_error;

	/* set 12-bit X/Y data format (depends on application) */
	ret = oplus_crown_write_read(client, PIXART_PAT9125_ORIENTATION_REG,
			PIXART_PAT9125_MOTION_DATA_LENGTH);
	if (!ret)
		goto on_error;

	/* ONLY for VDD=VDDA=1.7~1.9V: for power saving */
	ret = oplus_crown_write_read(client, PIXART_PAT9125_VOLTAGE_SEGMENT_SEL_REG,
			PIXART_PAT9125_LOW_VOLTAGE_SEGMENT);

	/* ONLY for V: for power saving */
	ret = oplus_crown_write_read(client, PIXART_PAT9125_7C_REG,
			PIXART_PAT9125_7C_INIT_VALUE);
	if (!ret)
		goto on_error;

	ret = oplus_crown_write_read(client, PIXART_PAT9125_2B_REG,
			PIXART_PAT9125_2B_INIT_VALUE);
	if (!ret)
		goto on_error;

	ret = oplus_crown_write_read(client, PIXART_PAT9125_2D_REG,
			PIXART_PAT9125_2D_INIT_VALUE);
	if (!ret)
		goto on_error;

	//if (read_data(client, PIXART_PAT9125_MISC2_REG) == 0x04) {
	//	oplus_crown_write_read(client, PIXART_PAT9125_MISC2_REG, 0x08);
	//	if (read_data(client, PIXART_PAT9125_MISC1_REG) == 0x10)
	//		oplus_crown_write_read(client, PIXART_PAT9125_MISC1_REG,
	//				0x19);
	//}
	/* enable write protect */
	ret = oplus_crown_write_read(client, PIXART_PAT9125_WRITE_PROTECT_REG,
			PIXART_PAT9125_ENABLE_WRITE_PROTECT);
	if (!ret)
		goto on_error;

	dev_info(&client->dev, "%s: sensor init success!\n", __func__);
	return 0;

on_error:
	dev_err(&client->dev, "%s: sensor init failed!\n", __func__);
	return -1;
}

int pat9125_sw_power(struct oplus_crown_platform_data *pdata, bool on)
{
	if (on) {
		write_data(pdata->client, (u8)PIXART_PAT9125_CONFIG_REG, PIXART_PAT9125_POWERDOWN_DIS);
	} else {
		write_data(pdata->client, (u8)PIXART_PAT9125_CONFIG_REG, PIXART_PAT9125_POWERDOWN_EN);
	}

	return 0;
}

int pat9125_force_sleep(struct oplus_crown_platform_data *pdata) {
	struct i2c_client *client = pdata->client;
	/*set sleep2 sleep1 all enable, and force sleep2*/
	write_data(client, PIXART_PAT9125_OP_MODE_REG,
		PIXART_PAT9125_FORCE_SLEEP2);
	return 0;
}

int pat9125_set_cpi_x_reg(struct oplus_crown_platform_data *pdata, long val) {
	struct i2c_client *client = pdata->client;
	write_data(client, (u8)PIXART_PAT9125_SET_CPI_RES_X_REG, (u8)val);
	write_data(client, (u8)PIXART_PAT9125_SET_CPI_RES_Y_REG, 0);
	return 0;
}

static int pat9125_irq_handler(struct oplus_crown_platform_data *pdata, int irq)
{
	static int err_unmotion_cnt = 0;
	#ifdef DYNAMIC_RESOLUTION
	int unmotion_cnt = 0;
	#endif
	u8 motion;
	u16 delta_xy_hi = 0, delta_x = 0;
	struct input_dev *ipdev = pdata->input;
	struct device *dev = &pdata->client->dev;

	__pm_stay_awake(pdata->ws);

	if (unlikely(0 == pdata->irq_en)) {
		pr_err("irq_en is disabled, exit isr\n");
		goto Exit_IRQ;
	}

	motion = read_data(pdata->client, PIXART_PAT9125_MOTION_STATUS_REG);

	if (unlikely(!(motion & PIXART_PAT9125_VALID_MOTION_DATA))) {
		bool switch_status = get_switch_status_by_id (SWITCH_WHEEL);

		if ((!switch_status)||(SWITCH_DISCONNECT == pdata->switch_state)) {
			pr_err("switch_io_status:%d, switch_state:%d, disable irq&exit isr\n",
				switch_status, pdata->switch_state);
			if (0 != pdata->irq_en) {
				disable_irq_nosync(pdata->client->irq);
				irq_set_irq_type(pdata->client->irq, IRQ_TYPE_EDGE_RISING);
				pdata->irq_en = 0;
			}
			pdata->switch_state = SWITCH_DISCONNECT;
		} else if (err_unmotion_cnt % 10 == 0) {
			if((err_unmotion_cnt < 100)||(err_unmotion_cnt % 100 == 0)) {
				pr_err("switch_io_status:%d, switch_state:%d, cnt:%d exit isr\n",
					switch_status, pdata->switch_state, err_unmotion_cnt);
			} else {
				if (switch_status) {
					if (0 != pdata->irq_en) {
						disable_irq_nosync(pdata->client->irq);
						irq_set_irq_type(pdata->client->irq, IRQ_TYPE_EDGE_RISING);
						pdata->irq_en = 0;
					}
					pr_err("switch_io_status:%d, cnt:%d , set irq is rinsing, exit isr\n",
										switch_status, err_unmotion_cnt);
				}
			}
		} else {
			usleep_range(OPLUS_CROWN_SAMPLING_PERIOD_US_MIN,
						OPLUS_CROWN_SAMPLING_PERIOD_US_MAX);
		}
		err_unmotion_cnt++;
		goto Exit_IRQ;
	}
	err_unmotion_cnt = 0;

	#ifdef DYNAMIC_RESOLUTION
	set_cpi_res_x_reg(pdata, get_cpi_resolution_x());
	#endif

	while (motion & PIXART_PAT9125_VALID_MOTION_DATA) {
		/* check if MOTION bit is set or not */
		delta_x = read_data(pdata->client,
				PIXART_PAT9125_DELTA_X_LO_REG);

		delta_xy_hi = read_data(pdata->client,
				PIXART_PAT9125_DELTA_XY_HI_REG);

		delta_x |= (delta_xy_hi&0x00f0) << 4;
		if (delta_x & 0x0800) {
			delta_x |= 0xf000;
		}

		/* Inverse x depending upon the device orientation */
		delta_x = (get_display_vflip()) ? -delta_x : delta_x;
		dev_dbg(dev, "motion = %x, delta_x = %x\n",
			motion, delta_x);

		if (0 != delta_x) {
			/* Send delta_x as REL_WHEEL for rotation */
			input_report_rel(ipdev, REL_WHEEL, (s16) delta_x);
			input_sync(ipdev);

			if (delta_x > 0) {
				sdata.ads_distance += delta_x;
			} else {
				sdata.ads_distance -= delta_x;
			}
			sdata.move_distance += delta_x;
			if (sdata.ads_distance - sdata.last_print_log > 90) {
				dev_info(dev, "move = %d, ads = %d\n", sdata.move_distance, sdata.ads_distance);
				sdata.last_print_log = sdata.ads_distance;
			}
		}

		#ifdef DYNAMIC_RESOLUTION
		unmotion_cnt = 0;
		#endif

		do {
			usleep_range(OPLUS_CROWN_SAMPLING_PERIOD_US_MIN,
						OPLUS_CROWN_SAMPLING_PERIOD_US_MAX);

			if (unlikely(0 == pdata->irq_en)) {
				motion = 0;
				pr_err("irq disabled, exit isr\n");
				goto Exit_IRQ;
			}

			motion = read_data(pdata->client,
					PIXART_PAT9125_MOTION_STATUS_REG);

			#ifdef DYNAMIC_RESOLUTION
			unmotion_cnt++;
			#else
			break;
			#endif
		} while (!(motion & PIXART_PAT9125_VALID_MOTION_DATA)
		#ifdef DYNAMIC_RESOLUTION
				&& (unmotion_cnt < (auto_suspend / OPLUS_CROWN_SAMPLING_PERIOD_US_MIN)) //OPLUS_CROWN_SAMPLING_AUTO_REDUCTION_CNT
		#endif
			);

	};
	#ifdef DYNAMIC_RESOLUTION
	if (likely(0 != pdata->irq_en)) {
		write_data(pdata->client, (u8)PIXART_PAT9125_SET_CPI_RES_X_REG, static_xcpi);
			//OPLUS_CROWN_CPI_RESOLUTION_X_STATIC);
		write_data(pdata->client, (u8)PIXART_PAT9125_OP_MODE_REG,
			PIXART_PAT9125_FORCE_SLEEP2);
	}
	#endif

Exit_IRQ:
	__pm_relax(pdata->ws);
	return 0;
}

void pat9125_clear_data(struct oplus_crown_platform_data *pdata)
{
	u8 motion = read_data(pdata->client, PIXART_PAT9125_MOTION_STATUS_REG);
	u8 REL_x = 0, REL_y = 0, REL_xh = 0;
	if (motion & PIXART_PAT9125_VALID_MOTION_DATA) {
		/*Ignore dirty data*/
		REL_x = read_data(pdata->client, PIXART_PAT9125_DELTA_X_LO_REG);
		REL_y = read_data(pdata->client, PIXART_PAT9125_DELTA_Y_LO_REG);
		REL_xh= read_data(pdata->client, PIXART_PAT9125_DELTA_XY_HI_REG);
	}
	dev_info(&pdata->client->dev, "%s: Ignore dirty data(0x%02x, 0x%02x, 0x%02x, 0x%02x)\n",
		__func__, motion, REL_x, REL_y, REL_xh);
}

void pat9125_calibrate(struct oplus_crown_platform_data *pdata, bool start)
{
	struct i2c_client *client = pdata->client;

	if (start) {
		if (0 != pdata->irq_en) {
			dev_info(&pdata->client->dev, "%s: start calibrate\n", __func__);
			pdata->irq_en = 0;
			disable_irq_nosync(client->irq);
			irq_set_irq_type(client->irq, IRQ_TYPE_EDGE_RISING);
			set_cpi_res_x_reg(pdata, get_cpi_resolution_x());
			write_data(client, (u8)PIXART_PAT9125_OP_MODE_REG,
				PIXART_PAT9125_SLEEP1_SLEEP2_DIS);
		} else {
			dev_err(&pdata->client->dev, "%s: start state failed\n", __func__);
		}
	} else {
		if (0 == pdata->irq_en) {
			dev_info(&pdata->client->dev, "%s: stop calibrate\n", __func__);
			pdata->irq_en = 1;
			irq_set_irq_type(client->irq, IRQ_TYPE_EDGE_FALLING|IRQ_TYPE_LEVEL_LOW);
			enable_irq(client->irq);
			set_cpi_res_x_reg(pdata, static_xcpi);
			write_data(client, (u8)PIXART_PAT9125_OP_MODE_REG,
				PIXART_PAT9125_FORCE_SLEEP2);
		} else {
			dev_err(&pdata->client->dev, "%s: stop state failed\n", __func__);
		}
	}
}

int pat9125_get_calibrate_value(struct oplus_crown_platform_data *pdata)
{
	u8 motion;
	u16 delta_xy_hi = 0, delta_x = 0;
	int result = 0;

	motion = read_data(pdata->client, PIXART_PAT9125_MOTION_STATUS_REG);
	/* check if MOTION bit is set or not */
	if (motion & PIXART_PAT9125_VALID_MOTION_DATA) {
		delta_x = read_data(pdata->client,
				PIXART_PAT9125_DELTA_X_LO_REG);
		delta_xy_hi = read_data(pdata->client,
				PIXART_PAT9125_DELTA_XY_HI_REG);
		delta_x |= (delta_xy_hi&0x00f0) << 4;
		if (delta_x & 0x0800) {
			delta_x |= 0xf000;
		}
		/* Fixed inverse x during calibration*/
		delta_x = -delta_x;

		dev_info(&pdata->client->dev, "%s: motion = 0x%x, delta_x = 0x%x\n",
			__func__, motion, delta_x);
	}

	if (delta_x & 0x0800) {
		result = (int)delta_x | 0xfffff000;
	} else {
		result = (int)delta_x;
	}
	return result;
}

struct oplus_crown_sensor_ops pat9125_ops = {
	.init = pat9125_sensor_init,
	.sw_power = pat9125_sw_power,
	.force_sleep = pat9125_force_sleep,
	.set_cpi_x_reg = pat9125_set_cpi_x_reg,
	.irq_handler = pat9125_irq_handler,
	.clear_data = pat9125_clear_data,
	.calibrate = pat9125_calibrate,
	.get_calibrate_value = pat9125_get_calibrate_value,
};

