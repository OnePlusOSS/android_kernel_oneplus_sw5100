/**
 ****************************************************************************************
 *
 * @file rwnx_configparse.c
 *
 * Copyright (C) RivieraWaves 2012-2021
 *
 ****************************************************************************************
 */
#include <linux/firmware.h>
#include <linux/if_ether.h>
#include <linux/namei.h>
#include <linux/ctype.h>
#include "rwnx_defs.h"
#include "rwnx_cfgfile.h"
#include "bes_log.h"
#include "bes_common.h"

/**
 * rwnx_find_tag - Find labels with specified length parameters and
 * return its parameter starting address.
 * @file_data: The starting address of the configuration file.
 * @file_size: The size of the configuration file, in bytes.
 * @tag_name: String of tag names.
 * @tag_len: Specify the parameter length of the target label.
 *
 * Return the starting address of the tag parameter.
 * When unable to find tag, return NULL.
 */
const char *rwnx_find_tag(const u8 *file_data, unsigned int file_size, const char *tag_name,
			  unsigned int tag_len)
{
	unsigned int curr, line_start = 0, line_size;

	bes_dbg(BES_DBG_CFGFILE, RWNX_FN_ENTRY_STR);

	/* Walk through all the lines of the configuration file */
	while (line_start < file_size) {
		/* Search the end of the current line (or the end of the file) */
		for (curr = line_start; curr < file_size; curr++)
			if (file_data[curr] == '\n')
				break;

		/* Compute the line size */
		line_size = curr - line_start;

		/* Check if this line contains the expected tag */
		if ((line_size > strlen(tag_name)) &&
		    (line_size <= (strlen(tag_name) + tag_len)) &&
		    (!strncmp(&file_data[line_start], tag_name, strlen(tag_name))))
			return (&file_data[line_start + strlen(tag_name)]);

		/* Move to next line */
		line_start = curr + 1;
	}

	/* Tag not found */
	return NULL;
}

static int __rwnx_parse_configfile_edca(const struct firmware *config_fw, char *name, char *suffix,
					u8 tmp_len, u8 tag_len, u32 limit)
{
	const u8 *tag_ptr;
	int val = -1;

	name[tmp_len] = 0;
	if ((tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, strcat(name, suffix), tag_len)) != NULL) {
		sscanf(tag_ptr, "%d", &val);
		if (val < 0 || val > limit)
			return -EINVAL;
	} else {
		return -EINVAL;
	}

	return val;
}

static int _rwnx_parse_configfile_edca(const struct firmware *config_fw,
				       struct rwnx_edca_config *edca_config, bool is_sta)
{
	int i;
	char *edca_str[] = {
		[AC_BK] = "_BK",
		[AC_BE] = "_BE",
		[AC_VI] = "_VI",
		[AC_VO] = "_VO",
	};
	char temp_str[20] = {0};
	u8 tmp_len;
	int val;

	tmp_len = is_sta ? 3 : 2;
	if (is_sta)
		memcpy(temp_str, "STA", tmp_len);
	else
		memcpy(temp_str, "AP", tmp_len);

	for (i = 0; i < AC_MAX; i++) {
		struct rwnx_edca_config *edca = &edca_config[i];

		memcpy(temp_str + tmp_len, edca_str[i], 3);
		if ((val = __rwnx_parse_configfile_edca(config_fw, temp_str, "_AIFSN=", tmp_len + 3, 2, 0xF)) >= 0)
			edca->aifs = val;
		else
			goto inval_para;

		if ((val = __rwnx_parse_configfile_edca(config_fw, temp_str, "_CWMIN=", tmp_len + 3, 2, 0xF)) >= 0)
			edca->cwmin = val;
		else
			goto inval_para;

		if ((val = __rwnx_parse_configfile_edca(config_fw, temp_str, "_CWMAX=", tmp_len + 3, 2, 0xF)) >= 0)
			edca->cwmax = val;
		else
			goto inval_para;

		if ((val = __rwnx_parse_configfile_edca(config_fw, temp_str, "_TXOP=", tmp_len + 3, 5, 0xFFFF)) >= 0)
			edca->txop = val;
		else
			goto inval_para;
	}

	return 0;

inval_para:
	bes_err(BES_DBG_CFGFILE, "%s invalid %s%d\n", __func__, temp_str, val);

	return -EINVAL;
}

static void rwnx_parse_configfile_edca(const struct firmware *config_fw, struct rwnx_conf_file *config)
{
	int val;
	const u8 *tag_ptr;

	/* Check if use config file edca */
	tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "USE_CONFIG_EDCA=", 1);
	if (tag_ptr != NULL) {
		if (sscanf(tag_ptr, "%d", &val) != 1)
			bes_err(BES_DBG_CFGFILE, "%s invalid USE_CONFIG_EDCA value\n", __func__);
	} else {
		val = 0;
	}

	if (val < 0 || val > 1)
		config->edca_use_config = 0;
	else
		config->edca_use_config = val;

	if (config->edca_use_config == 0)
		goto inval_para;

	if (_rwnx_parse_configfile_edca(config_fw, config->sta_edca, true))
		goto inval_para;

	if (_rwnx_parse_configfile_edca(config_fw, config->ap_edca, false))
		goto inval_para;

	return;

inval_para:
	config->edca_use_config = 0;
	memset((void *)config->sta_edca, 0, sizeof(config->sta_edca));
	memset((void *)config->ap_edca, 0, sizeof(config->ap_edca));
}

static void rwnx_parse_configfile_oui_blacklist(const struct firmware *config_fw, struct rwnx_conf_file *config)
{
	const u8 *start_ptr;
	int len = 0, oui_argc, i;
	u8 *buf = NULL;
	char *oui_argv[BES_OUI_BLACKLIST_CNT_MAX];
	u32 oui_temp[3] = {0};

	/* Reset oui_blacklist_cnt firstly */
	config->oui_blacklist_cnt = 0;

	/* Find the starting position */
	if ((start_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "OUI_BLACKLIST=", 9 * BES_OUI_BLACKLIST_CNT_MAX -1)) == NULL)
		return;

	/* Calculate the length of oui blacklist string */
	while (*(start_ptr + len) != '\n' && *(start_ptr + len) != '\0' &&
	       (start_ptr + len) != &config_fw->data[config_fw->size - 1])
		len++;

	/* No assignment, return directly */
	if (len == 0)
		return;
	len = (*(start_ptr + len) != '\n' && *(start_ptr + len) != '\0') ? (len + 1) : (len + 2);

	/* Copy oui blacklist config for processing */
	buf = kzalloc(len, GFP_KERNEL);
	if (buf == NULL) {
		bes_err(BES_DBG_CFGFILE, "%s: allocation failed.len:%d\n", __func__, len);
		return;
	}
	memcpy(buf, start_ptr, len - 1);
	buf[len - 1] = '\0';

	/* Parse oui blacklist param */
	oui_argc = bes_common_parse_line((char *)buf, oui_argv, BES_OUI_BLACKLIST_CNT_MAX);
	if (oui_argc == 0)
		goto out;

	/* Update config value */
	for (i = 0; i < oui_argc; i++) {
		if (sscanf(oui_argv[i], "%x-%x-%x", &oui_temp[0], &oui_temp[1], &oui_temp[2]) != 3)
			continue;

		config->oui_blacklist[config->oui_blacklist_cnt] = (oui_temp[0] << 16) |
								   (oui_temp[1] << 8) |
								   oui_temp[2];
		config->oui_blacklist_cnt++;
	}

out:
	/* Free buf */
	kfree(buf);
}

static void rwnx_parse_tx_max_power(const struct firmware *config_fw, struct rwnx_conf_file *config)
{
	int i, j, start;
	const char *tag_ptr;
	char temp_group[10] = {0};
	bool find_first_char = false;

	config->max_pwr_cfg_num = 0;

	/* Check if use config file edca */
	tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "TX_MAX_POWER_CFG_GROUP=", TX_MAX_POWER_CFG_NUM * 6 + 1);
	if (tag_ptr == NULL)
		return;

	/* find start char */
	while (*tag_ptr != '\n' && *tag_ptr != '\0') {
		if (*tag_ptr++ == '[') {
			find_first_char = true;
			break;
		}
	}

	if (find_first_char == false)
		goto inval_para;

	for (i = 0, start = 0; tag_ptr[i] != '\0' && tag_ptr[i] != '\n' &&
	     config->max_pwr_cfg_num < TX_MAX_POWER_CFG_NUM; i++) {
		if (tag_ptr[i] == ';' || tag_ptr[i] == ']') {
			if (i - start + 1 > sizeof(temp_group))
				goto inval_para;

			for (j = start; j < i; j++)
				temp_group[j - start] = tag_ptr[j];

			temp_group[i - start] = '\0';

			if (sscanf(temp_group, "%hd,%hd", &config->max_pwr[config->max_pwr_cfg_num].max_pwr_2g,
			        &config->max_pwr[config->max_pwr_cfg_num].max_pwr_5g) != 2) {
				goto inval_para;
			}

			if (config->max_pwr[config->max_pwr_cfg_num].max_pwr_2g < TX_MAX_POWER_2G_MIN ||
			    config->max_pwr[config->max_pwr_cfg_num].max_pwr_2g > TX_MAX_POWER_2G_MAX)
				goto inval_para;

			if (config->max_pwr[config->max_pwr_cfg_num].max_pwr_5g < TX_MAX_POWER_5G_MIN ||
			    config->max_pwr[config->max_pwr_cfg_num].max_pwr_5g > TX_MAX_POWER_5G_MAX)
				goto inval_para;

			start = i + 1;
			config->max_pwr_cfg_num++;
		}

		if (tag_ptr[i] == ']')
			break;
	}

	/* change the unit to 0.1dBm */
	for (i = 0; i < config->max_pwr_cfg_num; i++) {
		config->max_pwr[i].max_pwr_5g *= 5;
		config->max_pwr[i].max_pwr_2g *= 5;
	}

	return;

inval_para:
	config->max_pwr_cfg_num = 0;
	memset((void *)config->max_pwr, 0, sizeof(config->max_pwr));
	bes_warn(BES_DBG_CFGFILE, "%s: invalid max tx power config\n", __func__);
}

/**
 * Parse the Config file used at init time
 */
int rwnx_parse_configfile(struct rwnx_hw *rwnx_hw, const char *filename, struct rwnx_conf_file *config)
{
	const struct firmware *config_fw;
	int ret;
	const u8 *tag_ptr;

	bes_dbg(BES_DBG_CFGFILE, RWNX_FN_ENTRY_STR);

	if ((filename == NULL) || (strlen(filename) == 0)) {
		bes_warn(BES_DBG_CFGFILE, "config file name is invalid\n");
		return -EINVAL;
	}

	if ((ret = request_firmware(&config_fw, filename, rwnx_hw->dev))) {
		bes_warn(BES_DBG_CFGFILE, "%s: Not find %s (%d)\n", __func__, filename, ret);
		return ret;
	}

	/* Get MAC Address */
	memset(&config->mac_addr, 0, ETH_ALEN);
	tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "MAC_ADDR=", strlen("00:00:00:00:00:00"));
	if (tag_ptr != NULL) {
		u8 *addr = (u8 *)&config->mac_addr;
		if (sscanf(tag_ptr, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", addr + 0, addr + 1, addr + 2, addr + 3, addr + 4,
			   addr + 5) != ETH_ALEN)
			memset(&config->mac_addr, 0, ETH_ALEN);
	}

	/* Get the interval for listening to beacons */
	tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "LISTEN_INTERVAL=", strlen("00"));
	if (tag_ptr != NULL) {
		if (sscanf(tag_ptr, "%d", &config->listen_itv) != 1) {
			/* If parameters cannot be obtained from the configuration file,
			 * invalid values will be set by default. */
			bes_err(BES_DBG_CFGFILE, "%s invalid listen interval\n", __func__);
			config->listen_itv = -1;
		}
	} else
		config->listen_itv = -1;

	/* Get the HT_ON config */
	tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "HT_ON=", strlen("0"));
	if (tag_ptr != NULL) {
		if ((sscanf(tag_ptr, "%d", &config->ht_on) != 1) ||
		    (config->ht_on != 0 && config->ht_on != 1)) {
			/* If parameters cannot be obtained from the configuration file,
			 * invalid values will be set by default. */
			bes_err(BES_DBG_CFGFILE, "%s invalid ht_on\n", __func__);
			config->ht_on = -1;
		}
	} else
		config->ht_on = -1;

	/* Get the VHT_ON config */
	tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "VHT_ON=", strlen("0"));
	if (tag_ptr != NULL) {
		if ((sscanf(tag_ptr, "%d", &config->vht_on) != 1) ||
		    (config->vht_on != 0 && config->vht_on != 1)) {
			/* If parameters cannot be obtained from the configuration file,
			 * invalid values will be set by default. */
			bes_err(BES_DBG_CFGFILE, "%s invalid vht_on\n", __func__);
			config->vht_on = -1;
		}
	} else
		config->vht_on = -1;

	/* Get the VHT MCS map value configuration */
	tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "MCS_MAP=", strlen("0"));
	if (tag_ptr != NULL) {
		if (sscanf(tag_ptr, "%d", &config->mcs_map) != 1) {
			/* If parameters cannot be obtained from the configuration file,
			 * invalid values will be set by default. */
			bes_err(BES_DBG_CFGFILE, "%s invalid VHT MCS map\n", __func__);
			config->mcs_map = -1;
		}
	} else
		config->mcs_map = -1;

	/* Get the HE_ON config */
	tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "HE_ON=", strlen("0"));
	if (tag_ptr != NULL) {
		if ((sscanf(tag_ptr, "%d", &config->he_on) != 1) ||
		    (config->he_on != 0 && config->he_on != 1)) {
			/* If parameters cannot be obtained from the configuration file,
			 * invalid values will be set by default. */
			bes_err(BES_DBG_CFGFILE, "%s invalid he_on\n", __func__);
			config->he_on = -1;
		}
	} else
		config->he_on = -1;

	/* Get the HE MCS map value configuration */
	tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "HE_MCS_MAP=", strlen("0"));
	if (tag_ptr != NULL) {
		if (sscanf(tag_ptr, "%d", &config->he_mcs_map) != 1) {
			/* If parameters cannot be obtained from the configuration file,
			 * invalid values will be set by default. */
			bes_err(BES_DBG_CFGFILE, "%s invalid HE MCS map\n", __func__);
			config->he_mcs_map = -1;
		}
	} else
		config->he_mcs_map = -1;

	/* Get the UAPSD timer timeout configuration */
	tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "UAPSD_TIMEOUT=", strlen("0000"));
	if (tag_ptr != NULL) {
		if (sscanf(tag_ptr, "%d", &config->uapsd_timeout) != 1) {
			/* If parameters cannot be obtained from the configuration file,
			 * invalid values will be set by default. */
			bes_err(BES_DBG_CFGFILE, "%s invalid UAPSD timeout value\n", __func__);
			config->uapsd_timeout = -1;
		}
	} else
		config->uapsd_timeout = -1;

	/* Get the KeepAlive period configuration */
	tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "KEEPALIVE_PERIOD=", strlen("00"));
	if (tag_ptr != NULL) {
		if (sscanf(tag_ptr, "%d", &config->keepalive_period) != 1) {
			/* If parameters cannot be obtained from the configuration file,
			 * invalid values will be set by default. */
			bes_err(BES_DBG_CFGFILE, "%s invalid KeepAlvie Period value\n", __func__);
			config->keepalive_period = -1;
		}
	} else
		config->keepalive_period = -1;

	/* Get the PowerSaving configuration */
	tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "PS_ON=", strlen("0"));
	if (tag_ptr != NULL) {
		if (sscanf(tag_ptr, "%d", &config->ps_on) != 1 ||
		    (config->ps_on != 0 && config->ps_on != 1)) {
			/* If parameters cannot be obtained from the configuration file,
			 * invalid values will be set by default. */
			bes_err(BES_DBG_CFGFILE, "%s invalid PS config\n", __func__);
			config->ps_on = -1;
		}
	} else
		config->ps_on = -1;

	/* Get the 40MHz Bandwidth configuration */
	tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "USE_2040=", strlen("0"));
	if (tag_ptr != NULL) {
		if (sscanf(tag_ptr, "%d", &config->use_2040) != 1 ||
		    (config->use_2040 != 0 && config->use_2040 != 1)) {
			/* If parameters cannot be obtained from the configuration file,
			 * invalid values will be set by default. */
			bes_err(BES_DBG_CFGFILE, "%s invalid 40MHz Bandwidth config\n", __func__);
			config->use_2040 = -1;
		}
	} else
		config->use_2040 = -1;

	/* Get the 80MHz Bandwidth configuration */
	tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "USE_80=", strlen("0"));
	if (tag_ptr != NULL) {
		if (sscanf(tag_ptr, "%d", &config->use_80) != 1 ||
		    (config->use_80 != 0 && config->use_80 != 1)) {
			/* If parameters cannot be obtained from the configuration file,
			 * invalid values will be set by default. */
			bes_err(BES_DBG_CFGFILE, "%s invalid 80MHz Bandwidth config\n", __func__);
			config->use_80 = -1;
		}
	} else
		config->use_80 = -1;

	/* Set User PowerSaving Management configuration */
	tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "DISABLE_USER_PS_MGMT=", strlen("0"));
	if (tag_ptr != NULL) {
		if (sscanf(tag_ptr, "%d", &config->user_ps_mgmt_disable) != 1 ||
		    (config->user_ps_mgmt_disable != 0 && config->user_ps_mgmt_disable != 1)) {
			/* If parameters cannot be obtained from the configuration file,
			 * invalid values will be set by default. */
			bes_err(BES_DBG_CFGFILE, "%s invalid DISABLE_USER_PS_MGMT config\n", __func__);
			config->user_ps_mgmt_disable = -1;
		}
	} else
		config->user_ps_mgmt_disable = -1;

	/* Get edca configuration */
	rwnx_parse_configfile_edca(config_fw, config);

	/* Get the beacon loss count configuration */
	if ((tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "BCN_LOSS_THD=", 3)) != NULL) {
		sscanf(tag_ptr, "%d", &config->bcn_loss_thd);
		if (config->bcn_loss_thd < BCN_LOSS_MIN || config->bcn_loss_thd > BCN_LOSS_MAX) {
			bes_err(BES_DBG_CFGFILE, "%s invalid BCN_LOSS_THD config\n", __func__);
			config->bcn_loss_thd = BEACON_LOSS_DEFAULT;
		}
	} else {
		config->bcn_loss_thd = BEACON_LOSS_DEFAULT;
	}

	/* Get beacon loss null data threshold configuration */
	if ((tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "BCN_LOSS_NULL_DATA_THD=", 3)) != NULL) {
		sscanf(tag_ptr, "%d", &config->bcn_loss_null_data_thd);
		if (config->bcn_loss_null_data_thd < BCN_LOSS_NULL_DATA_MIN ||
		    config->bcn_loss_null_data_thd > BCN_LOSS_NULL_DATA_MAX) {
			bes_err(BES_DBG_CFGFILE, "%s invalid BCN_LOSS_NULL_DATA_THD config\n", __func__);
			config->bcn_loss_null_data_thd = BEACON_LOSS_NULL_DATA_DEFAULT;
		}
	} else {
		config->bcn_loss_null_data_thd = BEACON_LOSS_NULL_DATA_DEFAULT;
	}

	/* Get custregd configuration */
	if ((tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "COUNTRY_CODE=", 2)) != NULL) {
		sscanf(tag_ptr, "%c%c", &config->country_code[0], &config->country_code[1]);
		// convert to uppercase letters
		config->country_code[0] = toupper(config->country_code[0]);
		config->country_code[1] = toupper(config->country_code[1]);

		if((!isalpha(config->country_code[0]) || !isalpha(config->country_code[1])) &&
		   memcmp(config->country_code, "00", 2) != 0) {
			bes_err(BES_DBG_CFGFILE, "%s country_code: %c%c config is incorrect, set to 00\n",
				__func__, config->country_code[0], config->country_code[1]);
			memcpy(&config->country_code[0], "00", 2);
		}
	} else {
		memcpy(&config->country_code[0], "00", 2);
	}

	/* Get scan active duration configuration */
	if ((tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "SCAN_ACTIVE_DUR=", 3)) != NULL) {
		sscanf(tag_ptr, "%u", &config->scan_active_dur);
		config->scan_active_dur *= 1000;
		if (config->scan_active_dur < SCAN_DUR_MIN ||
		    config->scan_active_dur > SCAN_DUR_MAX) {
			bes_err(BES_DBG_CFGFILE, "%s invalid scan active dur config\n", __func__);
			config->scan_active_dur = SCAN_ACTIVE_DURATION;
		}
	} else {
		config->scan_active_dur = SCAN_ACTIVE_DURATION;
	}

	/* Get scan passive duration configuration */
	if ((tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "SCAN_PASSIVE_DUR=", 3)) != NULL) {
		sscanf(tag_ptr, "%u", &config->scan_passive_dur);
		config->scan_passive_dur *= 1000;
		if (config->scan_passive_dur < SCAN_DUR_MIN ||
		    config->scan_passive_dur > SCAN_DUR_MAX) {
			bes_err(BES_DBG_CFGFILE, "%s invalid scan passive dur config\n", __func__);
			config->scan_passive_dur = SCAN_PASSIVE_DURATION;
		}
	} else {
		config->scan_passive_dur = SCAN_PASSIVE_DURATION;
	}

	/* Get scan result filter configuration */
	if ((tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "BSS_RSSI_STABLE=", strlen("0"))) != NULL) {
		sscanf(tag_ptr, "%d", &config->bss_rssi_stable);

		if (sscanf(tag_ptr, "%d", &config->bss_rssi_stable) != 1 ||
		    (config->bss_rssi_stable != 0 && config->bss_rssi_stable != 1)) {
			/* If parameters cannot be obtained from the configuration file,
			 * invalid values will be set by default. */
			bes_err(BES_DBG_CFGFILE, "%s invalid bss_rssi_stable config\n", __func__);
			config->bss_rssi_stable = 0;
		}
	} else {
		config->bss_rssi_stable = 0;
	}

	/* Get oui blacklist configuration */
	rwnx_parse_configfile_oui_blacklist(config_fw, config);

	/* Get tx max power configuration */
	rwnx_parse_tx_max_power(config_fw, config);

	/* Release the configuration file */
	release_firmware(config_fw);
	return 0;
}

/**
 * Parse the Config file used at init time
 */
int rwnx_parse_phy_configfile(struct rwnx_hw *rwnx_hw, const char *filename, struct rwnx_phy_conf_file *config,
			      int path)
{
	const struct firmware *config_fw;
	int ret;
	const u8 *tag_ptr;

	bes_dbg(BES_DBG_CFGFILE, RWNX_FN_ENTRY_STR);

	if ((ret = request_firmware(&config_fw, filename, rwnx_hw->dev))) {
		bes_err(BES_DBG_CFGFILE, "%s: Failed to get %s (%d)\n", __func__, filename, ret);
		/* use default parameter if not found config file */
		config->trd.path_mapping = path;
		config->trd.tx_dc_off_comp = 0;
		config->karst.tx_iq_comp_2_4G[0] = 0x01000000;
		config->karst.tx_iq_comp_2_4G[1] = 0x01000000;
		config->karst.rx_iq_comp_2_4G[0] = 0x01000000;
		config->karst.rx_iq_comp_2_4G[1] = 0x01000000;
		config->karst.tx_iq_comp_5G[0] = 0x01000000;
		config->karst.tx_iq_comp_5G[1] = 0x01000000;
		config->karst.rx_iq_comp_5G[0] = 0x01000000;
		config->karst.rx_iq_comp_5G[1] = 0x01000000;
		config->karst.path_used = path;
		return ret;
	}

	/* Get Trident path mapping */
	tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "TRD_PATH_MAPPING=", strlen("00"));
	if (tag_ptr != NULL) {
		u8 val;
		if (sscanf(tag_ptr, "%hhx", &val) == 1)
			config->trd.path_mapping = val;
		else
			config->trd.path_mapping = path;
	} else
		config->trd.path_mapping = path;

	bes_dbg(BES_DBG_CFGFILE, "Trident path mapping is: %d\n", config->trd.path_mapping);

	/* Get DC offset compensation */
	tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "TX_DC_OFF_COMP=", strlen("00000000"));
	if (tag_ptr != NULL) {
		if (sscanf(tag_ptr, "%08x", &config->trd.tx_dc_off_comp) != 1)
			config->trd.tx_dc_off_comp = 0;
	} else
		config->trd.tx_dc_off_comp = 0;

	bes_dbg(BES_DBG_CFGFILE, "TX DC offset compensation is: %08X\n", config->trd.tx_dc_off_comp);

	/* Get Karst TX IQ compensation value for path0 on 2.4GHz */
	tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "KARST_TX_IQ_COMP_2_4G_PATH_0=", strlen("00000000"));
	if (tag_ptr != NULL) {
		if (sscanf(tag_ptr, "%08x", &config->karst.tx_iq_comp_2_4G[0]) != 1)
			config->karst.tx_iq_comp_2_4G[0] = 0x01000000;
	} else
		config->karst.tx_iq_comp_2_4G[0] = 0x01000000;

	bes_dbg(BES_DBG_CFGFILE, "Karst TX IQ compensation for path 0 on 2.4GHz is: %08X\n", config->karst.tx_iq_comp_2_4G[0]);

	/* Get Karst TX IQ compensation value for path1 on 2.4GHz */
	tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "KARST_TX_IQ_COMP_2_4G_PATH_1=", strlen("00000000"));
	if (tag_ptr != NULL) {
		if (sscanf(tag_ptr, "%08x", &config->karst.tx_iq_comp_2_4G[1]) != 1)
			config->karst.tx_iq_comp_2_4G[1] = 0x01000000;
	} else
		config->karst.tx_iq_comp_2_4G[1] = 0x01000000;

	bes_dbg(BES_DBG_CFGFILE, "Karst TX IQ compensation for path 1 on 2.4GHz is: %08X\n", config->karst.tx_iq_comp_2_4G[1]);

	/* Get Karst RX IQ compensation value for path0 on 2.4GHz */
	tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "KARST_RX_IQ_COMP_2_4G_PATH_0=", strlen("00000000"));
	if (tag_ptr != NULL) {
		if (sscanf(tag_ptr, "%08x", &config->karst.rx_iq_comp_2_4G[0]) != 1)
			config->karst.rx_iq_comp_2_4G[0] = 0x01000000;
	} else
		config->karst.rx_iq_comp_2_4G[0] = 0x01000000;

	bes_dbg(BES_DBG_CFGFILE, "Karst RX IQ compensation for path 0 on 2.4GHz is: %08X\n", config->karst.rx_iq_comp_2_4G[0]);

	/* Get Karst RX IQ compensation value for path1 on 2.4GHz */
	tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "KARST_RX_IQ_COMP_2_4G_PATH_1=", strlen("00000000"));
	if (tag_ptr != NULL) {
		if (sscanf(tag_ptr, "%08x", &config->karst.rx_iq_comp_2_4G[1]) != 1)
			config->karst.rx_iq_comp_2_4G[1] = 0x01000000;
	} else
		config->karst.rx_iq_comp_2_4G[1] = 0x01000000;

	bes_dbg(BES_DBG_CFGFILE, "Karst RX IQ compensation for path 1 on 2.4GHz is: %08X\n", config->karst.rx_iq_comp_2_4G[1]);

	/* Get Karst TX IQ compensation value for path0 on 5GHz */
	tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "KARST_TX_IQ_COMP_5G_PATH_0=", strlen("00000000"));
	if (tag_ptr != NULL) {
		if (sscanf(tag_ptr, "%08x", &config->karst.tx_iq_comp_5G[0]) != 1)
			config->karst.tx_iq_comp_5G[0] = 0x01000000;
	} else
		config->karst.tx_iq_comp_5G[0] = 0x01000000;

	bes_dbg(BES_DBG_CFGFILE, "Karst TX IQ compensation for path 0 on 5GHz is: %08X\n", config->karst.tx_iq_comp_5G[0]);

	/* Get Karst TX IQ compensation value for path1 on 5GHz */
	tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "KARST_TX_IQ_COMP_5G_PATH_1=", strlen("00000000"));
	if (tag_ptr != NULL) {
		if (sscanf(tag_ptr, "%08x", &config->karst.tx_iq_comp_5G[1]) != 1)
			config->karst.tx_iq_comp_5G[1] = 0x01000000;
	} else
		config->karst.tx_iq_comp_5G[1] = 0x01000000;

	bes_dbg(BES_DBG_CFGFILE, "Karst TX IQ compensation for path 1 on 5GHz is: %08X\n", config->karst.tx_iq_comp_5G[1]);

	/* Get Karst RX IQ compensation value for path0 on 5GHz */
	tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "KARST_RX_IQ_COMP_5G_PATH_0=", strlen("00000000"));
	if (tag_ptr != NULL) {
		if (sscanf(tag_ptr, "%08x", &config->karst.rx_iq_comp_5G[0]) != 1)
			config->karst.rx_iq_comp_5G[0] = 0x01000000;
	} else
		config->karst.rx_iq_comp_5G[0] = 0x01000000;

	bes_dbg(BES_DBG_CFGFILE, "Karst RX IQ compensation for path 0 on 5GHz is: %08X\n", config->karst.rx_iq_comp_5G[0]);

	/* Get Karst RX IQ compensation value for path1 on 5GHz */
	tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "KARST_RX_IQ_COMP_5G_PATH_1=", strlen("00000000"));
	if (tag_ptr != NULL) {
		if (sscanf(tag_ptr, "%08x", &config->karst.rx_iq_comp_5G[1]) != 1)
			config->karst.rx_iq_comp_5G[1] = 0x01000000;
	} else
		config->karst.rx_iq_comp_5G[1] = 0x01000000;

	bes_dbg(BES_DBG_CFGFILE, "Karst RX IQ compensation for path 1 on 5GHz is: %08X\n", config->karst.rx_iq_comp_5G[1]);

	/* Get Karst default path */
	tag_ptr = rwnx_find_tag(config_fw->data, config_fw->size, "KARST_DEFAULT_PATH=", strlen("00"));
	if (tag_ptr != NULL) {
		u8 val;
		if (sscanf(tag_ptr, "%hhx", &val) == 1)
			config->karst.path_used = val;
		else
			config->karst.path_used = path;
	} else
		config->karst.path_used = path;

	bes_dbg(BES_DBG_CFGFILE, "Karst default path is: %d\n", config->karst.path_used);

	/* Release the configuration file */
	release_firmware(config_fw);

	return 0;
}

int rwnx_bss_rssi_stable_get(struct rwnx_hw *rwnx_hw)
{
	if (rwnx_hw != NULL)
		return rwnx_hw->conf_file.bss_rssi_stable;
	else {
		bes_warn(BES_DBG_CFGFILE, "rwnx_hw is NULL\n");
		return 0;
	}
}

void rwnx_bss_rssi_stable_set(struct rwnx_hw *rwnx_hw, int enable)
{
	rwnx_hw->conf_file.bss_rssi_stable = enable;
}