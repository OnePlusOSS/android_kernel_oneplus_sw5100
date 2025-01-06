/**
 ****************************************************************************************
 *
 * @file rwnx_cfgfile.h
 *
 * Copyright (C) RivieraWaves 2012-2021
 *
 ****************************************************************************************
 */

#ifndef _RWNX_CFGFILE_H_
#define _RWNX_CFGFILE_H_

#define BCN_LOSS_MAX 255
#define BCN_LOSS_MIN 5

#define BCN_LOSS_NULL_DATA_MAX 255
#define BCN_LOSS_NULL_DATA_MIN 1

#define BEACON_LOSS_DEFAULT           10   //1s
#define BEACON_LOSS_NULL_DATA_DEFAULT 20   //2s, 20 cnt

/* Default duration on channel (in us) when actively scanning */
#define SCAN_ACTIVE_DURATION    40000
/* Default duration on channel (in us) when passively scanning */
#define SCAN_PASSIVE_DURATION   110000
/* duration (in us) */
#define SCAN_DUR_MAX 255000
#define SCAN_DUR_MIN 30000

#define TX_MAX_POWER_CFG_NUM 10
#define TX_MAX_POWER_5G_MAX 42 /* 0.5dBm */
#define TX_MAX_POWER_5G_MIN 16 /* 0.5dBm */
#define TX_MAX_POWER_2G_MAX 46 /* 0.5dBm */
#define TX_MAX_POWER_2G_MIN 16 /* 0.5dBm */

/* Record EDCA structure */
struct rwnx_edca_config {
	u8 aifs;
	u8 cwmin;
	u8 cwmax;
	u16 txop;
};

struct wifi_tx_max_pwr {
	s16 max_pwr_2g; /* 0.1dBm */
	s16 max_pwr_5g; /* 0.1dBm */
};

#define BES_OUI_BLACKLIST_CNT_MAX 5
/*
 * Structure used to retrieve information from the Config file
 */
struct rwnx_conf_file {
	struct mac_addr mac_addr;
	int listen_itv;
	int ht_on;
	int vht_on;
	int mcs_map;
	int he_on;
	int he_mcs_map;
	int uapsd_timeout;
	int keepalive_period;
	int ps_on;
	int use_2040;
	int use_80;
	int user_ps_mgmt_disable;
	bool edca_use_config;
	struct rwnx_edca_config sta_edca[AC_MAX];
	struct rwnx_edca_config ap_edca[AC_MAX];
	int bcn_loss_thd;
	int bcn_loss_null_data_thd;
	char country_code[2];
	u32 scan_active_dur;
	u32 scan_passive_dur;
	int bss_rssi_stable;
	int oui_blacklist_cnt;
	u32 oui_blacklist[BES_OUI_BLACKLIST_CNT_MAX];
	u8 max_pwr_cfg_num;
	struct wifi_tx_max_pwr max_pwr[TX_MAX_POWER_CFG_NUM];
};

/*
 * Structure used to retrieve information from the PHY Config file used at Initialization time
 */
struct rwnx_phy_conf_file {
	struct phy_trd_cfg_tag trd;
	struct phy_karst_cfg_tag karst;
	struct phy_cataxia_cfg_tag cataxia;
};

int rwnx_parse_configfile(struct rwnx_hw *rwnx_hw, const char *filename, struct rwnx_conf_file *config);

int rwnx_parse_phy_configfile(struct rwnx_hw *rwnx_hw, const char *filename, struct rwnx_phy_conf_file *config,
			      int path);
int rwnx_bss_rssi_stable_get(struct rwnx_hw *rwnx_hw);
void rwnx_bss_rssi_stable_set(struct rwnx_hw *rwnx_hw, int enable);
#endif /* _RWNX_CFGFILE_H_ */
