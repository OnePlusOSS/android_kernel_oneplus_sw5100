#include "oplus_crown.h"

static unsigned short cpi_resolution_x = OPLUS_CROWN_DEFAULT_CPI_RESOLUTION_X;

#ifdef OPLUS_CROWN_GET_CPI_X_BY_SETUP
static int __init setup_cpi_resolution_x(char *str)
{
	char *end;
	long new = simple_strtol(str, &end, 0);
	if (end == str || new > 65535)
		return -EINVAL;
	cpi_resolution_x = new;
	return 1;
}
__setup("cpi_x=", setup_cpi_resolution_x);
#endif /* OPLUS_CROWN_GET_CPI_X_BY_SETUP */

unsigned short get_cpi_resolution_x(void)
{
	return cpi_resolution_x;
}

void set_cpi_resolution_x(unsigned short new)
{
	pr_err("%s(0x%02x)", __func__, new);
	cpi_resolution_x = new;
}

extern struct oplus_crown_sensor_ops pat9125_ops;
extern struct oplus_crown_sensor_ops mot6010_ops;

struct oplus_crown_id_ops_table crown_id_ops_table[] = {
	{.id = 0x31, .ops = &pat9125_ops},
	{.id = 0x30, .ops = &mot6010_ops},
	{.id = 0x58, .ops = &mot6010_ops},
};

void set_oplus_crown_sensor_id(struct oplus_crown_platform_data * pdata, unsigned char id)
{
	int i;
	pdata->current_ops = NULL;

	for (i = 0; i < sizeof(crown_id_ops_table)/sizeof(crown_id_ops_table[0]); i++) {
		if (crown_id_ops_table[i].id == id) {
			pdata->current_ops = crown_id_ops_table[i].ops;
			break;
		}
	}
}

struct oplus_crown_sensor_ops * get_crown_ops(struct oplus_crown_platform_data * pdata) {
	return pdata->current_ops;
}

bool oplus_crown_write_read(struct i2c_client *client, u8 address, u8 wdata)
{
	u8 read_value, try_cnt = 3;

	do {
		write_data(client, address, wdata);
		read_value = read_data(client, address);
		try_cnt--;
	} while ((read_value != wdata) && (try_cnt));
	if (read_value != wdata) {
		pr_err("%s(): A:0x%02x W:0x%02x R:0x%02x", __func__,
			address, wdata, read_value);
		return FALSE;
	}
	return TRUE;
}

bool oplus_crown_sensor_init(struct oplus_crown_platform_data * pdata)
{
	u8 sensor_pid = 0;
	bool read_id_ok = false;
	int try_cnt = 0;
	struct oplus_crown_sensor_ops * ops = NULL;
	struct i2c_client *client = pdata->client;

	for (try_cnt = 0; try_cnt < RESET_RETRY_CNT_MAX; try_cnt++) {
		/*
		* Read sensor_pid in address 0x00 to check if the
		* serial link is valid, read value should be 0x31.
		*/
		sensor_pid = read_data(client, OPLUS_CROWN_PRODUCT_ID_REG);

		dev_info(&client->dev, "sensor_pid: 0x%x\n", sensor_pid);
		set_oplus_crown_sensor_id(pdata, sensor_pid);

		ops = get_crown_ops(pdata);

		if (ops && ops->init) {
			if (ops->init(pdata) == 0) {
				dev_info(&client->dev, "try %d init success\n", try_cnt);
				read_id_ok = true;
				if (ops && ops->force_sleep) {
					ops->force_sleep(pdata);
				}
				break;
			}
		}

		dev_err(&client->dev, "try %d reset failed\n", try_cnt);
		/* delay 20ms */
		usleep_range(DELAY_BETWEEN_REG_US, DELAY_BETWEEN_REG_US + 100);
	}

	return read_id_ok;
}
