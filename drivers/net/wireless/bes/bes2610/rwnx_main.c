/**
 ******************************************************************************
 *
 * @file rwnx_main.c
 *
 * @brief Entry point of the RWNX driver
 *
 * Copyright (C) RivieraWaves 2012-2021
 *
 ******************************************************************************
 */

#include <linux/etherdevice.h>
#include <linux/inetdevice.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <net/cfg80211.h>
#include <net/ip.h>
#include <linux/notifier.h>
#include <linux/inetdevice.h>
#include <net/ipv6.h>
#include <net/addrconf.h>
#include <linux/suspend.h>
#ifdef PLAT_ROCKCHIP
#include <linux/rfkill-wlan.h>
#endif

#include "rwnx_defs.h"
#if defined(CONFIG_RWNX_PCIE)
#include "rwnx_dini.h"
#endif
#include "hal_desc.h"
#include "reg_access.h"
#include "rwnx_cfgfile.h"
#include "rwnx_irqs.h"
#include "rwnx_msg_tx.h"
#include "rwnx_version.h"
#ifdef CONFIG_RWNX_BFMER
#include "rwnx_bfmer.h"
#endif //(CONFIG_RWNX_BFMER)
#include "rwnx_events.h"
#include "bes_psa.h"
#include "bes_sysfs.h"
#include "rwnx_tdls.h"
#include "rwnx_msg_rx.h"
#include "rwnx_main.h"
#include "bes_wpa_priv_cmd.h"
#include "bes_pwr.h"
#include "bes_wowlan.h"
#include "bes_sleep_manager.h"
#include "bes_dbac_cali.h"
#include "bes_gtk_offload.h"
#include "bes_fw_log_fifo_dump.h"
#include "bes_linkinfo.h"
#include "bes_vif_info.h"
#include "bes_filter.h"
#include "bes_eapol.h"
#ifdef CONFIG_RWNX_PCIE
#include "rwnx_pci.h"
#endif
#include "bes_coex.h"
#include "bes_linkinfo.h"
#include "bes_sched_scan.h"
#ifdef BES_WIRELESS_EXT
#include "bes_iw_priv_cmd.h"
#endif
#include "bes_data_monitor.h"
#include <net/ieee80211_radiotap.h>
#include "bes_conn_state.h"
#ifdef CONFIG_OPLUS_PLATFORM
#include "oplus_custom_wlan.h"
#endif
#include "bes_signal_test.h"
#include "bes_tx_loop.h"
#include "bes_signal_test.h"
#include "bes_tp_dbg.h"

#define RW_DRV_DESCRIPTION "Bestechnic 11ax driver for Linux cfg80211"
#define RW_DRV_COPYRIGHT "Copyright (C) Bestechnic 2015-2023"
#define RW_DRV_AUTHOR "Bestechnic"
#define STSTION_SCHED_SCAN_MAX 300
 
static int rwnx_cfg80211_mgmt_tx(struct wiphy *wiphy, struct wireless_dev *wdev, struct cfg80211_mgmt_tx_params *params,
					u64 *cookie);
static int rwnx_cfg80211_get_channel(struct wiphy *wiphy, struct wireless_dev *wdev,
#ifdef ADPT_GKI_KERNEL
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 81))
				     unsigned int link_id,
#endif
#else /* ADPT_GKI_KERNEL */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 19, 2))
				     unsigned int link_id,
#endif
#endif /* ADPT_GKI_KERNEL */
				     struct cfg80211_chan_def *chandef);

#define RWNX_HT_CAPABILITIES                                                                                           \
	{                                                                                                              \
		.ht_supported = true, .cap = 0, .ampdu_factor = IEEE80211_HT_MAX_AMPDU_64K,                            \
		.ampdu_density = IEEE80211_HT_MPDU_DENSITY_4,                                                         \
		.mcs = {                                                                                               \
			.rx_mask =                                                                                     \
				{                                                                                      \
					0xff,                                                                          \
					0,                                                                             \
					0,                                                                             \
					0,                                                                             \
					0,                                                                             \
					0,                                                                             \
					0,                                                                             \
					0,                                                                             \
					0,                                                                             \
					0,                                                                             \
				},                                                                                     \
			.rx_highest = cpu_to_le16(65),                                                                 \
			.tx_params = IEEE80211_HT_MCS_TX_DEFINED,                                                      \
		},                                                                                                     \
	}

#define RWNX_VHT_CAPABILITIES                                                                                          \
	{                                                                                                              \
		.vht_supported = false, .cap = (7 << IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT), .vht_mcs = { \
			.rx_mcs_map = cpu_to_le16(                                                                     \
				IEEE80211_VHT_MCS_SUPPORT_0_9 << 0 | IEEE80211_VHT_MCS_NOT_SUPPORTED << 2 |            \
				IEEE80211_VHT_MCS_NOT_SUPPORTED << 4 | IEEE80211_VHT_MCS_NOT_SUPPORTED << 6 |          \
				IEEE80211_VHT_MCS_NOT_SUPPORTED << 8 | IEEE80211_VHT_MCS_NOT_SUPPORTED << 10 |         \
				IEEE80211_VHT_MCS_NOT_SUPPORTED << 12 | IEEE80211_VHT_MCS_NOT_SUPPORTED << 14),        \
			.tx_mcs_map = cpu_to_le16(                                                                     \
				IEEE80211_VHT_MCS_SUPPORT_0_9 << 0 | IEEE80211_VHT_MCS_NOT_SUPPORTED << 2 |            \
				IEEE80211_VHT_MCS_NOT_SUPPORTED << 4 | IEEE80211_VHT_MCS_NOT_SUPPORTED << 6 |          \
				IEEE80211_VHT_MCS_NOT_SUPPORTED << 8 | IEEE80211_VHT_MCS_NOT_SUPPORTED << 10 |         \
				IEEE80211_VHT_MCS_NOT_SUPPORTED << 12 | IEEE80211_VHT_MCS_NOT_SUPPORTED << 14),        \
		}                                                                                                      \
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 20, 0)
#define RWNX_HE_CAPABILITIES                                                                                           \
	{                                                                                                              \
		.has_he = false,                                                                                       \
		.he_cap_elem =                                                                                         \
			{                                                                                              \
				.mac_cap_info[0] = 0,                                                                  \
				.mac_cap_info[1] = 0,                                                                  \
				.mac_cap_info[2] = 0,                                                                  \
				.mac_cap_info[3] = 0,                                                                  \
				.mac_cap_info[4] = 0,                                                                  \
				.mac_cap_info[5] = 0,                                                                  \
				.phy_cap_info[0] = 0,                                                                  \
				.phy_cap_info[1] = 0,                                                                  \
				.phy_cap_info[2] = 0,                                                                  \
				.phy_cap_info[3] = 0,                                                                  \
				.phy_cap_info[4] = 0,                                                                  \
				.phy_cap_info[5] = 0,                                                                  \
				.phy_cap_info[6] = 0,                                                                  \
				.phy_cap_info[7] = 0,                                                                  \
				.phy_cap_info[8] = 0,                                                                  \
				.phy_cap_info[9] = 0,                                                                  \
				.phy_cap_info[10] = 0,                                                                 \
			},                                                                                             \
		.he_mcs_nss_supp =                                                                                     \
			{                                                                                              \
				.rx_mcs_80 = cpu_to_le16(0xfffa),                                                      \
				.tx_mcs_80 = cpu_to_le16(0xfffa),                                                      \
				.rx_mcs_160 = cpu_to_le16(0xffff),                                                     \
				.tx_mcs_160 = cpu_to_le16(0xffff),                                                     \
				.rx_mcs_80p80 = cpu_to_le16(0xffff),                                                   \
				.tx_mcs_80p80 = cpu_to_le16(0xffff),                                                   \
			},                                                                                             \
		.ppe_thres = {0x00},                                                                                   \
	}
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
#define RWNX_HE_CAPABILITIES                                                                                           \
	{                                                                                                              \
		.has_he = false,                                                                                       \
		.he_cap_elem =                                                                                         \
			{                                                                                              \
				.mac_cap_info[0] = 0,                                                                  \
				.mac_cap_info[1] = 0,                                                                  \
				.mac_cap_info[2] = 0,                                                                  \
				.mac_cap_info[3] = 0,                                                                  \
				.mac_cap_info[4] = 0,                                                                  \
				.phy_cap_info[0] = 0,                                                                  \
				.phy_cap_info[1] = 0,                                                                  \
				.phy_cap_info[2] = 0,                                                                  \
				.phy_cap_info[3] = 0,                                                                  \
				.phy_cap_info[4] = 0,                                                                  \
				.phy_cap_info[5] = 0,                                                                  \
				.phy_cap_info[6] = 0,                                                                  \
				.phy_cap_info[7] = 0,                                                                  \
				.phy_cap_info[8] = 0,                                                                  \
			},                                                                                             \
		.he_mcs_nss_supp =                                                                                     \
			{                                                                                              \
				.rx_mcs_80 = cpu_to_le16(0xfffa),                                                      \
				.tx_mcs_80 = cpu_to_le16(0xfffa),                                                      \
				.rx_mcs_160 = cpu_to_le16(0xffff),                                                     \
				.tx_mcs_160 = cpu_to_le16(0xffff),                                                     \
				.rx_mcs_80p80 = cpu_to_le16(0xffff),                                                   \
				.tx_mcs_80p80 = cpu_to_le16(0xffff),                                                   \
			},                                                                                             \
		.ppe_thres = {0x00},                                                                                   \
	}
#endif

#define RATE(_bitrate, _hw_rate, _flags)                                                                               \
	{                                                                                                              \
		.bitrate = (_bitrate), .flags = (_flags), .hw_value = (_hw_rate),                                      \
	}

#define CHAN(_freq)                                                                                                    \
	{                                                                                                              \
		.center_freq = (_freq), .max_power = 30, /* FIXME */                                                   \
	}

#define CHAN2G(_channel, _freq, _flags) {			\
	.band			= NL80211_BAND_2GHZ,		\
	.center_freq		= (_freq),			\
	.hw_value		= 0,				\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}

#define CHAN5G(_channel, _freq, _flags) {			\
	.band			= NL80211_BAND_5GHZ,		\
	.center_freq		= (_freq),			\
	.hw_value		= 0,				\
	.flags			= (_flags),			\
	.max_antenna_gain	= 0,				\
	.max_power		= 30,				\
}

static struct ieee80211_rate rwnx_ratetable[] = {
	RATE(10, 0x00, 0),
	RATE(20, 0x01, IEEE80211_RATE_SHORT_PREAMBLE),
	RATE(55, 0x02, IEEE80211_RATE_SHORT_PREAMBLE),
	RATE(110, 0x03, IEEE80211_RATE_SHORT_PREAMBLE),
	RATE(60, 0x04, 0),
	RATE(90, 0x05, 0),
	RATE(120, 0x06, 0),
	RATE(180, 0x07, 0),
	RATE(240, 0x08, 0),
	RATE(360, 0x09, 0),
	RATE(480, 0x0A, 0),
	RATE(540, 0x0B, 0),
};

/* The channels indexes here are not used anymore */
static const struct ieee80211_channel rwnx_2ghz_channels[] = {
	CHAN2G(1, 2412, 0),
	CHAN2G(2, 2417, 0),
	CHAN2G(3, 2422, 0),
	CHAN2G(4, 2427, 0),
	CHAN2G(5, 2432, 0),
	CHAN2G(6, 2437, 0),
	CHAN2G(7, 2442, 0),
	CHAN2G(8, 2447, 0),
	CHAN2G(9, 2452, 0),
	CHAN2G(10, 2457, 0),
	CHAN2G(11, 2462, 0),
	CHAN2G(12, 2467, 0),
	CHAN2G(13, 2472, 0),
	CHAN2G(14, 2484, 0),
	// Extra channels defined only to be used for PHY measures.
	// Enabled only if custregd and custchan parameters are set
	CHAN(2390),
	CHAN(2400),
	CHAN(2410),
	CHAN(2420),
	CHAN(2430),
	CHAN(2440),
	CHAN(2450),
	CHAN(2460),
	CHAN(2470),
	CHAN(2480),
	CHAN(2490),
	CHAN(2500),
	CHAN(2510),
};

static const struct ieee80211_channel rwnx_5ghz_channels[] = {
	CHAN5G(36, 5180, 0),
	CHAN5G(40, 5200, 0),
	CHAN5G(44, 5220, 0),
	CHAN5G(48, 5240, 0),
	CHAN5G(52, 5260, 0),
	CHAN5G(56, 5280, 0),
	CHAN5G(60, 5300, 0),
	CHAN5G(64, 5320, 0),
	CHAN5G(100, 5500, 0),
	CHAN5G(104, 5520, 0),
	CHAN5G(108, 5540, 0),
	CHAN5G(112, 5560, 0),
	CHAN5G(116, 5580, 0),
	CHAN5G(120, 5600, 0),
	CHAN5G(124, 5620, 0),
	CHAN5G(128, 5640, 0),
	CHAN5G(132, 5660, 0),
	CHAN5G(136, 5680, 0),
	CHAN5G(140, 5700, 0),
	CHAN5G(144, 5720, 0),
	CHAN5G(149, 5745, 0),
	CHAN5G(153, 5765, 0),
	CHAN5G(157, 5785, 0),
	CHAN5G(161, 5805, 0),
	CHAN5G(165, 5825, 0),
	CHAN5G(168, 5845, 0),
	CHAN5G(173, 5865, 0),
	CHAN5G(177, 5885, 0),
	// Extra channels defined only to be used for PHY measures.
	// Enabled only if custregd and custchan parameters are set
	CHAN(5190),
	CHAN(5210),
	CHAN(5230),
	CHAN(5250),
	CHAN(5270),
	CHAN(5290),
	CHAN(5310),
	CHAN(5330),
	CHAN(5340),
	CHAN(5350),
	CHAN(5360),
	CHAN(5370),
	CHAN(5380),
	CHAN(5390),
	CHAN(5400),
	CHAN(5410),
	CHAN(5420),
	CHAN(5430),
	CHAN(5440),
	CHAN(5450),
	CHAN(5460),
	CHAN(5470),
	CHAN(5480),
	CHAN(5490),
	CHAN(5510),
	CHAN(5530),
	CHAN(5550),
	CHAN(5570),
	CHAN(5590),
	CHAN(5610),
	CHAN(5630),
	CHAN(5650),
	CHAN(5670),
	CHAN(5690),
	CHAN(5710),
	CHAN(5730),
	CHAN(5750),
	CHAN(5760),
	CHAN(5770),
	CHAN(5780),
	CHAN(5790),
	CHAN(5800),
	CHAN(5810),
	CHAN(5820),
	CHAN(5830),
	CHAN(5840),
	CHAN(5850),
	CHAN(5860),
	CHAN(5870),
	CHAN(5880),
	CHAN(5890),
	CHAN(5900),
	CHAN(5910),
	CHAN(5920),
	CHAN(5930),
	CHAN(5940),
	CHAN(5950),
	CHAN(5960),
	CHAN(5970),
};

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
static struct ieee80211_sband_iftype_data rwnx_he_capa = {
	.types_mask = BIT(NL80211_IFTYPE_STATION) | BIT(NL80211_IFTYPE_AP),
	.he_cap = RWNX_HE_CAPABILITIES,
};
#endif

static struct ieee80211_supported_band rwnx_band_2GHz = {
	.band = NL80211_BAND_2GHZ,
	.n_channels = ARRAY_SIZE(rwnx_2ghz_channels) - 13, // -13 to exclude extra channels
	.bitrates = rwnx_ratetable,
	.n_bitrates = ARRAY_SIZE(rwnx_ratetable),
	.ht_cap = RWNX_HT_CAPABILITIES,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
	.iftype_data = &rwnx_he_capa,
	.n_iftype_data = 1,
#endif
};

static struct ieee80211_supported_band rwnx_band_5GHz = {
	.band = NL80211_BAND_5GHZ,
	.n_channels = ARRAY_SIZE(rwnx_5ghz_channels) - 59, // -59 to exclude extra channels
	.bitrates = &rwnx_ratetable[4],
	.n_bitrates = ARRAY_SIZE(rwnx_ratetable) - 4,
	.ht_cap = RWNX_HT_CAPABILITIES,
	.vht_cap = RWNX_VHT_CAPABILITIES,
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
	.iftype_data = &rwnx_he_capa,
	.n_iftype_data = 1,
#endif
};

static int rwnx_band_init(void)
{
	/* Init 2G band */
	rwnx_band_2GHz.band = NL80211_BAND_2GHZ;
	if (!rwnx_band_2GHz.channels)
		rwnx_band_2GHz.channels = kmemdup(rwnx_2ghz_channels,
				  		  sizeof(struct ieee80211_channel) * (ARRAY_SIZE(rwnx_2ghz_channels) - 13),
				  		  GFP_KERNEL);
	else
		memcpy(rwnx_band_2GHz.channels,
		       rwnx_2ghz_channels,
		       sizeof(struct ieee80211_channel) * (ARRAY_SIZE(rwnx_2ghz_channels) - 13));

	rwnx_band_2GHz.n_channels = ARRAY_SIZE(rwnx_2ghz_channels) - 13;

	if (!rwnx_band_2GHz.channels)
		return -ENOMEM;

	/* Init 5G band */
	rwnx_band_5GHz.band = NL80211_BAND_5GHZ;
	if (!rwnx_band_5GHz.channels)
		rwnx_band_5GHz.channels = kmemdup(rwnx_5ghz_channels,
				  		  sizeof(struct ieee80211_channel) * (ARRAY_SIZE(rwnx_5ghz_channels) - 59),
				  		  GFP_KERNEL);
	else
		memcpy(rwnx_band_5GHz.channels,
		       rwnx_5ghz_channels,
		       sizeof(struct ieee80211_channel) * (ARRAY_SIZE(rwnx_5ghz_channels) - 59));
	rwnx_band_5GHz.n_channels = ARRAY_SIZE(rwnx_5ghz_channels) - 59;

	if (!rwnx_band_5GHz.channels) {
		kfree(rwnx_band_2GHz.channels);
		rwnx_band_2GHz.channels = NULL;
		return -ENOMEM;
	}

	return 0;
}

static void rwnx_band_deinit(void)
{
	if (rwnx_band_2GHz.channels) {
		kfree(rwnx_band_2GHz.channels);
		rwnx_band_2GHz.channels = NULL;
	}

	if (rwnx_band_5GHz.channels) {
		kfree(rwnx_band_5GHz.channels);
		rwnx_band_5GHz.channels = NULL;
	}
}

/*
 * [BES_MOD_START][Feature] MultiChannel_Concurrent
 * #{ managed, AP } <= 4, #{ P2P-client, P2P-GO } <= 1, total <= 4
 */
static struct ieee80211_iface_limit rwnx_limits[] = {
	{.max = NX_VIRT_DEV_MAX, .types = BIT(NL80211_IFTYPE_AP) | BIT(NL80211_IFTYPE_STATION)},
	{.max = 1, .types = BIT(NL80211_IFTYPE_P2P_GO) | BIT(NL80211_IFTYPE_P2P_CLIENT)}
};

static struct ieee80211_iface_limit rwnx_limits_dfs[] = {{.max = NX_VIRT_DEV_MAX, .types = BIT(NL80211_IFTYPE_AP)}};

static const struct ieee80211_iface_combination rwnx_combinations[] = {
	{
		.limits = rwnx_limits,
		.n_limits = ARRAY_SIZE(rwnx_limits),
		.num_different_channels = NX_CHAN_CTXT_CNT,
		.max_interfaces = NX_VIRT_DEV_MAX,
	},
	/* Keep this combination as the last one */
	{
		.limits = rwnx_limits_dfs,
		.n_limits = ARRAY_SIZE(rwnx_limits_dfs),
		.num_different_channels = 1,
		.max_interfaces = NX_VIRT_DEV_MAX,
		.radar_detect_widths = (BIT(NL80211_CHAN_WIDTH_20_NOHT) | BIT(NL80211_CHAN_WIDTH_20) |
					BIT(NL80211_CHAN_WIDTH_40) | BIT(NL80211_CHAN_WIDTH_80)),
	}};

/* There isn't a lot of sense in it, but you can transmit anything you like */
static struct ieee80211_txrx_stypes rwnx_default_mgmt_stypes[NUM_NL80211_IFTYPES] = {
	[NL80211_IFTYPE_STATION] =
		{
			.tx = 0xffff,
			.rx = (BIT(IEEE80211_STYPE_ACTION >> 4) | BIT(IEEE80211_STYPE_PROBE_REQ >> 4) |
			       BIT(IEEE80211_STYPE_AUTH >> 4)),
		},
	[NL80211_IFTYPE_AP] =
		{
			.tx = 0xffff,
			.rx = (BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) | BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
			       BIT(IEEE80211_STYPE_PROBE_REQ >> 4) | BIT(IEEE80211_STYPE_DISASSOC >> 4) |
			       BIT(IEEE80211_STYPE_AUTH >> 4) | BIT(IEEE80211_STYPE_DEAUTH >> 4) |
			       BIT(IEEE80211_STYPE_ACTION >> 4)),
		},
	[NL80211_IFTYPE_AP_VLAN] =
		{
			/* copy AP */
			.tx = 0xffff,
			.rx = (BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) | BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
			       BIT(IEEE80211_STYPE_PROBE_REQ >> 4) | BIT(IEEE80211_STYPE_DISASSOC >> 4) |
			       BIT(IEEE80211_STYPE_AUTH >> 4) | BIT(IEEE80211_STYPE_DEAUTH >> 4) |
			       BIT(IEEE80211_STYPE_ACTION >> 4)),
		},
	[NL80211_IFTYPE_P2P_CLIENT] =
		{
			.tx = 0xffff,
			.rx = (BIT(IEEE80211_STYPE_ACTION >> 4) | BIT(IEEE80211_STYPE_PROBE_REQ >> 4)),
		},
	[NL80211_IFTYPE_P2P_GO] =
		{
			.tx = 0xffff,
			.rx = (BIT(IEEE80211_STYPE_ASSOC_REQ >> 4) | BIT(IEEE80211_STYPE_REASSOC_REQ >> 4) |
			       BIT(IEEE80211_STYPE_PROBE_REQ >> 4) | BIT(IEEE80211_STYPE_DISASSOC >> 4) |
			       BIT(IEEE80211_STYPE_AUTH >> 4) | BIT(IEEE80211_STYPE_DEAUTH >> 4) |
			       BIT(IEEE80211_STYPE_ACTION >> 4)),
		},
	[NL80211_IFTYPE_P2P_DEVICE] =
		{
			.tx = 0xffff,
			.rx = (BIT(IEEE80211_STYPE_ACTION >> 4) | BIT(IEEE80211_STYPE_PROBE_REQ >> 4)),
		},
	[NL80211_IFTYPE_MESH_POINT] =
		{
			.tx = 0xffff,
			.rx = (BIT(IEEE80211_STYPE_ACTION >> 4) | BIT(IEEE80211_STYPE_AUTH >> 4) |
			       BIT(IEEE80211_STYPE_DEAUTH >> 4)),
		},
};

static u32 cipher_suites[] = {
	WLAN_CIPHER_SUITE_WEP40,
	WLAN_CIPHER_SUITE_WEP104,
	WLAN_CIPHER_SUITE_TKIP,
	WLAN_CIPHER_SUITE_CCMP,
	0, // reserved entries to enable AES-CMAC, GCMP-128/256, CCMP-256, SMS4
	0,
	0,
	0,
	0,
};

#define NB_RESERVED_CIPHER 5;

static const int rwnx_ac2hwq[1][NL80211_NUM_ACS] = {{[NL80211_TXQ_Q_VO] = RWNX_HWQ_VO,
						     [NL80211_TXQ_Q_VI] = RWNX_HWQ_VI,
						     [NL80211_TXQ_Q_BE] = RWNX_HWQ_BE,
						     [NL80211_TXQ_Q_BK] = RWNX_HWQ_BK}};

const int rwnx_tid2hwq[IEEE80211_NUM_TIDS] = {
	RWNX_HWQ_BE,
	RWNX_HWQ_BK,
	RWNX_HWQ_BK,
	RWNX_HWQ_BE,
	RWNX_HWQ_VI,
	RWNX_HWQ_VI,
	RWNX_HWQ_VO,
	RWNX_HWQ_VO,
	/* TID_8 is used for management frames */
	RWNX_HWQ_VO,
	/* At the moment, all others TID are mapped to BE */
	RWNX_HWQ_BE,
	RWNX_HWQ_BE,
	RWNX_HWQ_BE,
	RWNX_HWQ_BE,
	RWNX_HWQ_BE,
	RWNX_HWQ_BE,
	RWNX_HWQ_BE,
};

static const int rwnx_hwq2uapsd[NL80211_NUM_ACS] = {
	[RWNX_HWQ_VO] = IEEE80211_WMM_IE_STA_QOSINFO_AC_VO,
	[RWNX_HWQ_VI] = IEEE80211_WMM_IE_STA_QOSINFO_AC_VI,
	[RWNX_HWQ_BE] = IEEE80211_WMM_IE_STA_QOSINFO_AC_BE,
	[RWNX_HWQ_BK] = IEEE80211_WMM_IE_STA_QOSINFO_AC_BK,
};

static const struct obss_narrow_bw_ru_table narrow_bw_ru_table[] = {
	{.op_class = 115, .bandwidth= 20, .channel = 52},
	{.op_class = 115, .bandwidth= 20, .channel = 56},
	{.op_class = 115, .bandwidth= 20, .channel = 60},
	{.op_class = 115, .bandwidth= 20, .channel = 64},
	{.op_class = 121, .bandwidth= 20, .channel = 100},
	{.op_class = 121, .bandwidth= 20, .channel = 104},
	{.op_class = 121, .bandwidth= 20, .channel = 108},
	{.op_class = 121, .bandwidth= 20, .channel = 112},
	{.op_class = 121, .bandwidth= 20, .channel = 116},
	{.op_class = 121, .bandwidth= 20, .channel = 120},
	{.op_class = 121, .bandwidth= 20, .channel = 124},
	{.op_class = 121, .bandwidth= 20, .channel = 128},
	{.op_class = 121, .bandwidth= 20, .channel = 132},
	{.op_class = 121, .bandwidth= 20, .channel = 136},
	{.op_class = 121, .bandwidth= 20, .channel = 140},
	{.op_class = 121, .bandwidth= 20, .channel = 144},
	{.op_class = 122, .bandwidth= 40, .channel = 100},
	{.op_class = 122, .bandwidth= 40, .channel = 108},
	{.op_class = 122, .bandwidth= 40, .channel = 116},
	{.op_class = 122, .bandwidth= 40, .channel = 124},
	{.op_class = 122, .bandwidth= 40, .channel = 132},
	{.op_class = 122, .bandwidth= 40, .channel = 140},
	{.op_class = 123, .bandwidth= 40, .channel = 104},
	{.op_class = 123, .bandwidth= 40, .channel = 112},
	{.op_class = 123, .bandwidth= 40, .channel = 120},
	{.op_class = 123, .bandwidth= 40, .channel = 128},
	{.op_class = 123, .bandwidth= 40, .channel = 136},
	{.op_class = 123, .bandwidth= 40, .channel = 144},
};

static int rwnx_fill_station_info(struct rwnx_sta *sta, struct rwnx_vif *vif, struct station_info *sinfo);
static void rwnx_cfg80211_abort_scan(struct wiphy *wiphy, struct wireless_dev *wdev);

bool rwnx_is_obss_narrow_bw_ru_status(struct rwnx_hw *rwnx_hw, u16 freq,
				      enum nl80211_chan_width width)
{
	u16 channel = ieee80211_frequency_to_channel(freq);
	u16 bandwidth = 0;
	int i;

	if (width == NL80211_CHAN_WIDTH_20)
		bandwidth = 20;
	else if (width == NL80211_CHAN_WIDTH_40)
		bandwidth = 40;

	for (i = 0; i < ARRAY_SIZE(narrow_bw_ru_table); i++) {
		if (bandwidth == rwnx_hw->obss_narrow.cfg_table[i].bandwidth &&
		    channel == rwnx_hw->obss_narrow.cfg_table[i].channel)
			return true;
	}
	return false;
}

bool rwnx_narrow_ru_filter_acpt_all_beacon(struct rwnx_hw *rwnx_hw)
{
	bool acpt_all_beacon;

	spin_lock(&rwnx_hw->obss_narrow.lock);
	acpt_all_beacon = rwnx_hw->obss_narrow.acpt_all_beacon;
	spin_unlock(&rwnx_hw->obss_narrow.lock);

	return acpt_all_beacon;
}

void rwnx_narrow_ru_filter_reset(struct rwnx_hw *rwnx_hw)
{
	u8 disable = 0;
	struct mm_hw_filter hw_filter;

	/**
	 * First, stop receiving all beacons and block the completion of filter_work's work.
	 * And then, reset obss narrow ru status.
	 *
	 * Setting obss_narrow.state finaly, is to avoid possible state changes.
	 * Because after stop the receive all beacons, filter_work may still change the obss_narrow.state.
	 */
	spin_lock(&rwnx_hw->obss_narrow.lock);
	rwnx_hw->obss_narrow.acpt_all_beacon = false;
	spin_unlock(&rwnx_hw->obss_narrow.lock);
	cancel_delayed_work_sync(&rwnx_hw->obss_narrow.filter_work);

	/* status reset */
	bes_filter_get_hw_filter(rwnx_hw, &hw_filter);
	hw_filter.umac_filter &= (~NXMAC_ACCEPT_ALL_BEACON_BIT);
	bes_filter_set_hw_filter(rwnx_hw, &hw_filter);
	bes_send_drv2mcu_msg(rwnx_hw, &disable, sizeof(disable),
							MCU_WIFI_TB_PPDU_DISABLE_REQ,
							MCU_WIFI_TB_PPDU_DISABLE_CFM, NULL, 0);
	spin_lock(&rwnx_hw->obss_narrow.lock);
	rwnx_hw->obss_narrow.state = OBSS_NARROW_RU_UNPROCTED;
	spin_unlock(&rwnx_hw->obss_narrow.lock);
}

void rwnx_narrow_ru_filter_work(struct work_struct *work)
{
	struct rwnx_hw *rwnx_hw = container_of(work, struct rwnx_hw, obss_narrow.filter_work.work);
	struct obss_narrow_bw_ru *obss_narrow = &rwnx_hw->obss_narrow;
	u8 disable;

	spin_lock(&rwnx_hw->obss_narrow.lock);
	if (obss_narrow->state == OBSS_NARROW_RU_PROTECTING) {
		obss_narrow->state = OBSS_NARROW_RU_PROTECTED;
		spin_unlock(&rwnx_hw->obss_narrow.lock);
		disable = 1;
		bes_send_drv2mcu_msg(rwnx_hw, &disable, sizeof(disable),
				     MCU_WIFI_TB_PPDU_DISABLE_REQ,
				     MCU_WIFI_TB_PPDU_DISABLE_CFM, NULL, 0);
	} else if (obss_narrow->state == OBSS_NARROW_RU_PROTECTED) {
		obss_narrow->state = OBSS_NARROW_RU_UNPROCTED;
		spin_unlock(&rwnx_hw->obss_narrow.lock);
		disable = 0;
		bes_send_drv2mcu_msg(rwnx_hw, &disable, sizeof(disable),
				     MCU_WIFI_TB_PPDU_DISABLE_REQ,
				     MCU_WIFI_TB_PPDU_DISABLE_CFM, NULL, 0);
	} else {
		spin_unlock(&rwnx_hw->obss_narrow.lock);
	}
}

#ifdef CONFIG_PM
static const struct wiphy_wowlan_support rwnx_wowlan_support = {
	/* Support only for limited wowlan functionalities */
	.flags = WIPHY_WOWLAN_ANY |
		 WIPHY_WOWLAN_MAGIC_PKT |
#ifdef CONFIG_SCHED_SCAN
		 WIPHY_WOWLAN_NET_DETECT |
#endif
		 WIPHY_WOWLAN_4WAY_HANDSHAKE,
	.n_patterns = BES_MAX_PATTERN_FILTERS,
	.pattern_min_len = 1,
	.pattern_max_len = BES_MAX_PATTERN_LEN,
	.max_pkt_offset = BES_MAX_OFFSET_LEN,
#ifdef CONFIG_SCHED_SCAN
	.max_nd_match_sets = SCHED_SCANU_MATCH_SETS_NUM_LIMIT,
#endif
};
#endif
/*********************************************************************
 * helper
 *********************************************************************/
void rwnx_reset_capability(void)
{
	struct ieee80211_sta_ht_cap ht_cap = RWNX_HT_CAPABILITIES;
	struct ieee80211_sta_vht_cap vht_cap = RWNX_VHT_CAPABILITIES;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
	struct ieee80211_sta_he_cap he_cap = RWNX_HE_CAPABILITIES;
#endif
	rwnx_band_2GHz.ht_cap = ht_cap;
	rwnx_band_5GHz.ht_cap = ht_cap;
	rwnx_band_5GHz.vht_cap = vht_cap;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
	rwnx_he_capa.he_cap = he_cap;
#endif
}

struct rwnx_sta *rwnx_get_sta(struct rwnx_hw *rwnx_hw, const u8 *mac_addr)
{
	int i;

	for (i = 0; i < NX_REMOTE_STA_MAX; i++) {
		struct rwnx_sta *sta = &rwnx_hw->sta_table[i];
		if (sta->valid && (memcmp(mac_addr, &sta->mac_addr, 6) == 0))
			return sta;
	}

	return NULL;
}

void rwnx_enable_wapi(struct rwnx_hw *rwnx_hw)
{
	cipher_suites[rwnx_hw->wiphy->n_cipher_suites] = WLAN_CIPHER_SUITE_SMS4;
	rwnx_hw->wiphy->n_cipher_suites++;
	rwnx_hw->wiphy->flags |= WIPHY_FLAG_CONTROL_PORT_PROTOCOL;
}

void rwnx_enable_mfp(struct rwnx_hw *rwnx_hw)
{
	cipher_suites[rwnx_hw->wiphy->n_cipher_suites] = WLAN_CIPHER_SUITE_AES_CMAC;
	rwnx_hw->wiphy->n_cipher_suites++;
}

void rwnx_enable_gcmp(struct rwnx_hw *rwnx_hw)
{
	// Assume that HW supports CCMP-256 if it supports GCMP
	cipher_suites[rwnx_hw->wiphy->n_cipher_suites++] = WLAN_CIPHER_SUITE_CCMP_256;
	cipher_suites[rwnx_hw->wiphy->n_cipher_suites++] = WLAN_CIPHER_SUITE_GCMP;
	cipher_suites[rwnx_hw->wiphy->n_cipher_suites++] = WLAN_CIPHER_SUITE_GCMP_256;
}

u8 *rwnx_build_bcn(struct rwnx_bcn *bcn, struct cfg80211_beacon_data *new)
{
	u8 *buf, *pos;

	if (new->head) {
		u8 *head = kmalloc(new->head_len, GFP_KERNEL);

		if (!head)
			return NULL;

		if (bcn->head)
			kfree(bcn->head);

		bcn->head = head;
		bcn->head_len = new->head_len;
		memcpy(bcn->head, new->head, new->head_len);
	}
	if (new->tail) {
		u8 *tail = kmalloc(new->tail_len, GFP_KERNEL);

		if (!tail)
			return NULL;

		if (bcn->tail)
			kfree(bcn->tail);

		bcn->tail = tail;
		bcn->tail_len = new->tail_len;
		memcpy(bcn->tail, new->tail, new->tail_len);
	}

	if (!bcn->head)
		return NULL;

	bcn->tim_len = 6;
	bcn->len = bcn->head_len + bcn->tail_len + bcn->ies_len + bcn->tim_len;

	buf = kmalloc(bcn->len, GFP_KERNEL);
	if (!buf)
		return NULL;

	// Build the beacon buffer
	pos = buf;
	memcpy(pos, bcn->head, bcn->head_len);
	pos += bcn->head_len;
	*pos++ = WLAN_EID_TIM;
	*pos++ = 4;
	*pos++ = 0;
	*pos++ = bcn->dtim;
	*pos++ = 0;
	*pos++ = 0;
	if (bcn->tail) {
		memcpy(pos, bcn->tail, bcn->tail_len);
		pos += bcn->tail_len;
	}
	if (bcn->ies) {
		memcpy(pos, bcn->ies, bcn->ies_len);
	}

	return buf;
}

static void rwnx_del_bcn(struct rwnx_bcn *bcn)
{
	if (bcn->head) {
		kfree(bcn->head);
		bcn->head = NULL;
	}
	bcn->head_len = 0;

	if (bcn->tail) {
		kfree(bcn->tail);
		bcn->tail = NULL;
	}
	bcn->tail_len = 0;

	if (bcn->ies) {
		kfree(bcn->ies);
		bcn->ies = NULL;
	}
	bcn->ies_len = 0;
	bcn->tim_len = 0;
	bcn->dtim = 0;
	bcn->len = 0;
}

/**
 * Link channel ctxt to a vif and thus increments count for this context.
 */
void rwnx_chanctx_link(struct rwnx_vif *vif, u8 ch_idx, struct cfg80211_chan_def *chandef)
{
	struct rwnx_chanctx *ctxt;
	const struct cfg80211_chan_def *comp_chandef = NULL;

	if (ch_idx >= NX_CHAN_CTXT_CNT) {
		WARN(1, "Invalid channel ctxt id %d", ch_idx);
		return;
	}

	vif->ch_index = ch_idx;
	ctxt = &vif->rwnx_hw->chanctx_table[ch_idx];
	ctxt->count++;

	// For now chandef is NULL for STATION interface
	if (chandef) {
		if (!ctxt->chan_def.chan) {
			ctxt->chan_def = *chandef;
			atomic_inc(&vif->rwnx_hw->chanctx_count);
		} else {
			// TODO. check that chandef is the same as the one already
			// set for this ctxt

			// If the channel is compatible, use a larger bandwidth
			comp_chandef = cfg80211_chandef_compatible(&ctxt->chan_def, chandef);
			if (comp_chandef == chandef) {
				ctxt->chan_def = *chandef;
			}
		}
	}
}

/**
 * Unlink channel ctxt from a vif and thus decrements count for this context
 */
void rwnx_chanctx_unlink(struct rwnx_vif *vif)
{
	struct rwnx_chanctx *ctxt;

	if (vif->ch_index == RWNX_CH_NOT_SET)
		return;

	ctxt = &vif->rwnx_hw->chanctx_table[vif->ch_index];

	if (ctxt->count == 0) {
		WARN(1, "Chan ctxt ref count is already 0");
	} else {
		ctxt->count--;
	}

	if (ctxt->count == 0) {
		if (vif->ch_index == vif->rwnx_hw->cur_chanctx) {
			/* If current chan ctxt is no longer linked to a vif
			   disable radar detection (no need to check if it was activated) */
			rwnx_radar_detection_enable(&vif->rwnx_hw->radar, RWNX_RADAR_DETECT_DISABLE, RWNX_RADAR_RIU);
		}
		/* set chan to null, so that if this ctxt is relinked to a vif that
		   don't have channel information, don't use wrong information */
		ctxt->chan_def.chan = NULL;
		atomic_dec(&vif->rwnx_hw->chanctx_count);
	}
	vif->ch_index = RWNX_CH_NOT_SET;
}

int rwnx_chanctx_valid(struct rwnx_hw *rwnx_hw, u8 ch_idx)
{
	if (ch_idx >= NX_CHAN_CTXT_CNT || rwnx_hw->chanctx_table[ch_idx].chan_def.chan == NULL) {
		return 0;
	}
	bes_dbg(BES_DBG_CHAN, "%s, chidx=%d, chan=%d\n", __func__, ch_idx, rwnx_hw->chanctx_table[ch_idx].chan_def.chan ? 1 : 0);
	return 1;
}

static void rwnx_del_csa(struct rwnx_vif *vif)
{
	struct rwnx_hw *rwnx_hw = vif->rwnx_hw;
	struct rwnx_csa *csa = vif->ap.csa;

	if (!csa)
		return;

	rwnx_ipc_buf_dealloc(rwnx_hw, &csa->buf);
	rwnx_del_bcn(&csa->bcn);
	kfree(csa);
	vif->ap.csa = NULL;
}

static void rwnx_csa_finish(struct work_struct *ws)
{
	struct rwnx_csa *csa = container_of(ws, struct rwnx_csa, work);
	struct rwnx_vif *vif = csa->vif;
	struct rwnx_hw *rwnx_hw = vif->rwnx_hw;
	int error = csa->status;

	bes_dbg(BES_DBG_CSA, "%s, ret=%d\n", __func__, error);
	if (!error) {
		error = rwnx_send_bcn_change(rwnx_hw, vif->vif_index, &csa->buf, csa->bcn.head_len,
					     csa->bcn.tim_len, NULL);
	}

	if (error)
		cfg80211_stop_iface(rwnx_hw->wiphy, &vif->wdev, GFP_KERNEL);
	else {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 7, 0))
		wiphy_lock(rwnx_hw->wiphy);
#else
		mutex_lock(&vif->wdev.mtx);
		__acquire(&vif->wdev.mtx);
#endif
		spin_lock_bh(&rwnx_hw->cb_lock);
		rwnx_chanctx_unlink(vif);
		rwnx_chanctx_link(vif, csa->ch_idx, &csa->chandef);
		if (rwnx_hw->cur_chanctx == csa->ch_idx) {
			rwnx_radar_detection_enable_on_cur_channel(rwnx_hw);
			rwnx_txq_vif_start(vif, RWNX_TXQ_STOP_CHAN, rwnx_hw);
		} else
			rwnx_txq_vif_stop(vif, RWNX_TXQ_STOP_CHAN, rwnx_hw);
		spin_unlock_bh(&rwnx_hw->cb_lock);
		vif->vif_chandef = csa->chandef;
#ifdef ADPT_GKI_KERNEL
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 144))
		cfg80211_ch_switch_notify(vif->ndev, &csa->chandef, 0, 0);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 81))
		cfg80211_ch_switch_notify(vif->ndev, &csa->chandef, 0);
#else
		cfg80211_ch_switch_notify(vif->ndev, &csa->chandef);
#endif
#else /* ADPT_GKI_KERNEL */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0))
		cfg80211_ch_switch_notify(vif->ndev, &csa->chandef, 0, 0);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 19, 2))
		cfg80211_ch_switch_notify(vif->ndev, &csa->chandef, 0);
#else
		cfg80211_ch_switch_notify(vif->ndev, &csa->chandef);
#endif
#endif /* ADPT_GKI_KERNEL */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 7, 0))
		wiphy_unlock(rwnx_hw->wiphy);
#else
		mutex_unlock(&vif->wdev.mtx);
		__release(&vif->wdev.mtx);
#endif

		/* Recalibration of AP or GO after CSA. */
		bes_dbac_cali_after_csa(vif);
	}
	rwnx_del_csa(vif);
}

/**
 * rwnx_external_auth_enable - Enable external authentication on a vif
 *
 * @vif: VIF on which external authentication must be enabled
 *
 * External authentication requires to start TXQ for unknown STA in
 * order to send auth frame pusehd by user space.
 * Note: It is assumed that fw is on the correct channel.
 */
void rwnx_external_auth_enable(struct rwnx_vif *vif)
{
	bes_info(BES_DBG_LINK, RWNX_FN_ENTRY_STR);
	spin_lock_bh(&vif->rwnx_hw->tx_lock);
	vif->sta.flags |= RWNX_STA_EXT_AUTH;
	rwnx_txq_unk_vif_init(vif);
	rwnx_txq_start(rwnx_txq_vif_get(vif, NX_UNK_TXQ_TYPE), 0);
	spin_unlock_bh(&vif->rwnx_hw->tx_lock);
}

/**
 * rwnx_external_auth_disable - Disable external authentication on a vif
 *
 * @vif: VIF on which external authentication must be disabled
 */
void rwnx_external_auth_disable(struct rwnx_vif *vif)
{
	bes_info(BES_DBG_LINK, RWNX_FN_ENTRY_STR);
	if (!(vif->sta.flags & RWNX_STA_EXT_AUTH))
		return;

	vif->sta.flags &= ~RWNX_STA_EXT_AUTH;
	rwnx_txq_unk_vif_deinit(vif);
}

/**
 * rwnx_update_mesh_power_mode -
 *
 * @vif: mesh VIF  for which power mode is updated
 *
 * Does nothing if vif is not a mesh point interface.
 * Since firmware doesn't support one power save mode per link select the
 * most "active" power mode among all mesh links.
 * Indeed as soon as we have to be active on one link we might as well be
 * active on all links.
 *
 * If there is no link then the power mode for next peer is used;
 */
void rwnx_update_mesh_power_mode(struct rwnx_vif *vif)
{
	enum nl80211_mesh_power_mode mesh_pm;
	struct rwnx_sta *sta;
	struct mesh_config mesh_conf;
	struct mesh_update_cfm cfm;
	u32 mask;

	if (RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_MESH_POINT)
		return;

	if (list_empty(&vif->ap.sta_list)) {
		mesh_pm = vif->ap.next_mesh_pm;
	} else {
		mesh_pm = NL80211_MESH_POWER_DEEP_SLEEP;
		list_for_each_entry(sta, &vif->ap.sta_list, list) {
			if (sta->valid && (sta->mesh_pm < mesh_pm)) {
				mesh_pm = sta->mesh_pm;
			}
		}
	}

	if (mesh_pm == vif->ap.mesh_pm)
		return;

	mask = BIT(NL80211_MESHCONF_POWER_MODE - 1);
	mesh_conf.power_mode = mesh_pm;
	if (rwnx_send_mesh_update_req(vif->rwnx_hw, vif, mask, &mesh_conf, &cfm) || cfm.status)
		return;

	vif->ap.mesh_pm = mesh_pm;
}

/**
 * rwnx_save_assoc_ie_for_ft - Save association request elements if Fast
 * Transition has been configured.
 *
 * @vif: VIF that just connected
 * @sme: Connection info
 */
void rwnx_save_assoc_info_for_ft(struct rwnx_vif *vif, struct cfg80211_connect_params *sme)
{
	int ies_len = sme->ie_len + sme->ssid_len + 2;
	u8 *pos;

	if (!vif->sta.ft_assoc_ies) {
		if (!cfg80211_find_ie(WLAN_EID_MOBILITY_DOMAIN, sme->ie, sme->ie_len))
			return;

		vif->sta.ft_assoc_ies_len = ies_len;
		vif->sta.ft_assoc_ies = kmalloc(ies_len, GFP_KERNEL);
	} else if (vif->sta.ft_assoc_ies_len < ies_len) {
		kfree(vif->sta.ft_assoc_ies);
		vif->sta.ft_assoc_ies = kmalloc(ies_len, GFP_KERNEL);
	}

	if (!vif->sta.ft_assoc_ies)
		return;

	// Also save SSID (as an element) in the buffer
	pos = vif->sta.ft_assoc_ies;
	*pos++ = WLAN_EID_SSID;
	*pos++ = sme->ssid_len;
	memcpy(pos, sme->ssid, sme->ssid_len);
	pos += sme->ssid_len;
	memcpy(pos, sme->ie, sme->ie_len);
	vif->sta.ft_assoc_ies_len = ies_len;
}

/**
 * rwnx_rsne_to_connect_params - Initialise cfg80211_connect_params from
 * RSN element.
 *
 * @rsne: RSN element
 * @sme: Structure cfg80211_connect_params to initialize
 *
 * The goal is only to initialize enough for rwnx_send_sm_connect_req
 */
int rwnx_rsne_to_connect_params(const struct element *rsne, struct cfg80211_connect_params *sme)
{
	int len = rsne->datalen;
	int clen;
	const u8 *pos = rsne->data;

	if (len < 8)
		return 1;

	sme->crypto.control_port_no_encrypt = false;
	sme->crypto.control_port = true;
	sme->crypto.control_port_ethertype = cpu_to_be16(ETH_P_PAE);

	pos += 2;
	sme->crypto.cipher_group = ntohl(*((u32 *)pos));
	pos += 4;
	clen = le16_to_cpu(*((u16 *)pos)) * 4;
	pos += 2;
	len -= 8;
	if (len < clen + 2)
		return 1;
	// only need one cipher suite
	sme->crypto.n_ciphers_pairwise = 1;
	sme->crypto.ciphers_pairwise[0] = ntohl(*((u32 *)pos));
	pos += clen;
	len -= clen;

	// no need for AKM
	clen = le16_to_cpu(*((u16 *)pos)) * 4;
	pos += 2;
	len -= 2;
	if (len < clen)
		return 1;
	pos += clen;
	len -= clen;

	if (len < 4)
		return 0;

	pos += 2;
	clen = le16_to_cpu(*((u16 *)pos)) * 16;
	len -= 4;
	if (len > clen)
		sme->mfp = NL80211_MFP_REQUIRED;

	return 0;
}

#ifdef CONFIG_2G_20M_ONLY
static int rwnx_update_conn_param(struct rwnx_hw *rwnx_hw, enum nl80211_band band)
{
	bool update = false;
	if ((band == NL80211_BAND_2GHZ) &&
		(rwnx_hw->mod_params->use_2040 || rwnx_hw->mod_params->use_80)) {
		rwnx_hw->mod_params->use_2040 = false;
		rwnx_hw->mod_params->use_80 = false;
		update = true;
	} else if (band == NL80211_BAND_5GHZ &&
		((rwnx_hw->mod_params->use_2040 != true) ||
		(rwnx_hw->mod_params->use_80 != true))) {
		/* recover default config */
		rwnx_hw->mod_params->use_2040 = true;
		rwnx_hw->mod_params->use_80 = true;
		update = true;
	}

	if (update) {
		bes_dbg(BES_DBG_MSG_TX, "%s %d.\n", __func__, band);
		rwnx_update_capa(rwnx_hw, rwnx_hw->wiphy);
		rwnx_send_me_config_req(rwnx_hw);
	}
	return 0;
}
#endif

bool rwnx_wlan_enabled(struct rwnx_hw *rwnx_hw)
{
	if (!test_bit(RWNX_DEV_STARTED, &rwnx_hw->flags))
		return false;

	return true;
}

static int bes_ignore_bcn_edca(struct rwnx_hw *rwnx_hw, bool start)
{
	if (!rwnx_hw->conf_file.edca_use_config)
		return 0;

	if (start)
		return bes_send_ignore_bcn_edca_req(rwnx_hw, true);
	else
		return bes_send_ignore_bcn_edca_req(rwnx_hw, false);
}

/*********************************************************************
 * netdev callbacks
 ********************************************************************/
/**
 * int (*ndo_open)(struct net_device *dev);
 *     This function is called when network device transistions to the up
 *     state.
 *
 * - Start FW if this is the first interface opened
 * - Add interface at fw level
 */
static int rwnx_open(struct net_device *dev)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	struct mm_add_if_cfm add_if_cfm;
	int error = 0;

	/* Operation not allowed in non-signaling mode */
	if (!bes_sysfs_is_signal_mode())
		return -EPERM;

	// Check if it is the first opened VIF
	if (rwnx_hw->vif_started == 0) {
		/* switch on wlan */
#if defined(CONFIG_BES_SDIO) || defined(CONFIG_BES_USB) || defined(CONFIG_BES_MSPI)
		if ((error = bes_power_ctrl_wlan_open(rwnx_hw)))
			return error;
#endif

		// Start the FW
		if ((error = rwnx_send_start(rwnx_hw)))
			return error;

		// Set coex parameter
		bes_coex_arb_up(rwnx_hw);

		// Set parameters to firmware
		rwnx_send_me_config_req(rwnx_hw);
		// Set channel parameters to firmware
		rwnx_send_me_chan_config_req(rwnx_hw);

		/* Device is now started */
		rwnx_wf_status_set(rwnx_hw, RWNX_DEV_STARTED);

		/* Apply user log configuration */
		bes_sysfs_restore_log_config();

		/* Ignore beacon edca config */
		bes_ignore_bcn_edca(rwnx_hw, true);

		/* Get wowlan filter param, such as bes_capacity, etc */
		if ((error = bes_filter_manager_get_config(rwnx_hw)))
			return error;

		bes_lstats_phy_start(rwnx_hw);
	}

	if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_AP_VLAN) {
		/* For AP_vlan use same fw and drv indexes. We ensure that this index
		   will not be used by fw for another vif by taking index >= NX_VIRT_DEV_MAX */
		add_if_cfm.inst_nbr = rwnx_vif->drv_vif_index;
		netif_tx_stop_all_queues(dev);
	} else {
		/* Forward the information to the LMAC,
		 *     p2p value not used in FMAC configuration, iftype is sufficient */
		if ((error = rwnx_send_add_if(rwnx_hw, dev->dev_addr, RWNX_VIF_TYPE(rwnx_vif), false, &add_if_cfm)))
			return error;

		if (add_if_cfm.status != CO_OK ||
		    add_if_cfm.inst_nbr >= NX_ITF_MAX) {
			bes_err(BES_DBG_IFACE, "%s: Status Error(%d), inst_nbr:%u\n",
				__func__, add_if_cfm.status, add_if_cfm.inst_nbr);
			return -EIO;
		}
	}

	/* Save the index retrieved from LMAC */
	spin_lock_bh(&rwnx_hw->cb_lock);
	rwnx_vif->vif_index = add_if_cfm.inst_nbr;
	rwnx_vif->up = true;
	rwnx_hw->vif_started++;
	rwnx_hw->vif_table[add_if_cfm.inst_nbr] = rwnx_vif;
	bes_wpa_priv_cmd_set_rxfilte(rwnx_vif, 0);
	spin_unlock_bh(&rwnx_hw->cb_lock);
	bes_lstats_vif_start(rwnx_vif);

	/* Init the ip parameters of vif */
	bes_conn_state_init(rwnx_vif);

	netdev_info(dev, "OPEN " TR_VIF_STR, TR_VIF(rwnx_vif));

	/* Initialize the completion of EAPOL. */
	bes_eapol_sta_stored_key_init(rwnx_vif);

#ifdef CONFIG_RWNX_PCIE
	rwnx_pci_vif_start(rwnx_hw, rwnx_vif->vif_index);
#endif
	bes_filter_manager_interface_open(rwnx_vif);
	netif_tx_stop_all_queues(dev);

	/* Initialize the CSA calibration module of STA. */
	bes_dbac_sta_csa_init(rwnx_vif);
#ifdef CONFIG_BES_NAPI
	WARN_ON(skb_queue_len(&rwnx_vif->rx_napi_skb_queue) != 0);
	napi_enable(&rwnx_vif->napi);
#endif

	if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_MONITOR) {
		struct cfg80211_chan_def *mon_chandef = NULL;

		rwnx_hw->monitor_vif = rwnx_vif->vif_index;
		if (rwnx_vif->ch_index != RWNX_CH_NOT_SET) {
			/* Configure the monitor channel */
			error = rwnx_send_config_monitor_req(
				rwnx_hw, &rwnx_hw->chanctx_table[rwnx_vif->ch_index].chan_def, NULL);
		} else {
			/* get monitor channel */
			rwnx_cfg80211_get_channel(rwnx_vif->rwnx_hw->wiphy, &rwnx_vif->wdev,
#ifdef ADPT_GKI_KERNEL
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 81))
						  0,
#endif
#else /* ADPT_GKI_KERNEL */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 19, 2))
						  0,
#endif
#endif /* ADPT_GKI_KERNEL */
						  mon_chandef);
		}

		/* init vif txq */
		spin_lock_bh(&rwnx_vif->rwnx_hw->cb_lock);
		rwnx_txq_vif_init(rwnx_vif->rwnx_hw, rwnx_vif, 0);
		spin_unlock_bh(&rwnx_vif->rwnx_hw->cb_lock);

		netif_tx_start_all_queues(dev);
		netif_carrier_on(dev);
	}

	return error;
}

/**
 * int (*ndo_stop)(struct net_device *dev);
 *     This function is called when network device transistions to the down
 *     state.
 *
 * - Remove interface at fw level
 * - Reset FW if this is the last interface opened
 */
static int rwnx_close(struct net_device *dev)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	long status = 0;
	bool roc_enable = false;
	struct rwnx_roc *roc = NULL;
	u8 vif_idx = rwnx_vif->vif_index;
	struct rwnx_sta *rwnx_sta;

	/* Operation not allowed in non-signaling mode */
	if (!bes_sysfs_is_signal_mode())
		return -EPERM;

	netdev_info(dev, "CLOSE " TR_VIF_STR, TR_VIF(rwnx_vif));

#ifdef CONFIG_BES_NAPI
	napi_disable(&rwnx_vif->napi);
	skb_queue_purge(&rwnx_vif->rx_napi_skb_queue);
#endif

	rwnx_radar_cancel_cac(&rwnx_hw->radar);

	/* Abort scan request on the vif */
	if (rwnx_hw->scan_request && rwnx_hw->scan_request->wdev == &rwnx_vif->wdev) {
		rwnx_cfg80211_abort_scan(rwnx_hw->wiphy, &rwnx_vif->wdev);
	}

	/* Check the validity of vif_idx */
	if (vif_idx >= NX_ITF_MAX) {
		bes_err(BES_DBG_INIT, "invalid " TR_VIF_STR, TR_VIF(rwnx_vif));
		return -EINVAL;
	}

#ifdef CONFIG_RWNX_PCIE
	rwnx_pci_vif_stop(rwnx_hw, vif_idx);
#endif

	rwnx_send_remove_if(rwnx_hw, vif_idx);

	spin_lock_bh(&rwnx_hw->roc_lock);
	roc_enable = (rwnx_hw->roc && (rwnx_hw->roc->vif == rwnx_vif));
	spin_unlock_bh(&rwnx_hw->roc_lock);

	if (roc_enable) {
		status = wait_event_timeout(rwnx_hw->roc_wait_wq,
			   (rwnx_hw->roc == NULL), msecs_to_jiffies(500));
		if (status == 0) {
			bes_warn(BES_DBG_IFACE, "wait roc done timeout\n");

			spin_lock_bh(&rwnx_hw->roc_lock);
			roc = rwnx_hw->roc;
			rwnx_hw->roc = NULL;
			spin_unlock_bh(&rwnx_hw->roc_lock);

			if (roc != NULL) {
				kfree(roc);
				roc = NULL;
			}
		}
	}

	/* Flush deferred_e2amsg_work brfore vif is closed */
	rwnx_flush_deferred_e2amsg_work(rwnx_hw);

	/* Ensure that we won't process disconnect ind */
	spin_lock_bh(&rwnx_hw->cb_lock);

	rwnx_vif->up = false;
	if (netif_carrier_ok(dev)) {
		if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION ||
		    RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_CLIENT) {
			if (rwnx_vif->sta.ft_assoc_ies) {
				kfree(rwnx_vif->sta.ft_assoc_ies);
				rwnx_vif->sta.ft_assoc_ies = NULL;
				rwnx_vif->sta.ft_assoc_ies_len = 0;
			}
			cfg80211_disconnected(dev, WLAN_REASON_DEAUTH_LEAVING, NULL, 0, true, GFP_ATOMIC);

			rwnx_sta = rwnx_vif->sta.ap;
			if (rwnx_sta) {
				rwnx_txq_sta_deinit(rwnx_hw, rwnx_sta);
				rwnx_reset_sta_stats(rwnx_sta);
				bes_tp_dbg_reset_tx_rx_info(rwnx_hw, rwnx_sta);
				rwnx_txq_tdls_vif_deinit(rwnx_vif);
			}
			netif_tx_stop_all_queues(dev);
			netif_carrier_off(dev);
		} else if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_AP_VLAN) {
			netif_carrier_off(dev);
		} else {
			netdev_warn(dev, "AP not stopped when disabling interface");
		}
	}

	rwnx_hw->vif_table[vif_idx] = NULL;
	rwnx_vif->vif_index = RWNX_INVALID_VIF;
	bes_wpa_priv_cmd_set_rxfilte(rwnx_vif, 0);
	rwnx_hw->vif_started--;
	spin_unlock_bh(&rwnx_hw->cb_lock);

	bes_filter_manager_interface_close(rwnx_hw, vif_idx);

	rwnx_chanctx_unlink(rwnx_vif);

	if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_MONITOR)
		rwnx_hw->monitor_vif = RWNX_INVALID_VIF;

	bes_lstats_vif_stop(rwnx_vif);
	if (rwnx_hw->vif_started == 0) {
		/* This also lets both ipc sides remain in sync before resetting */
		rwnx_ipc_tx_drain(rwnx_hw);

		bes_lstats_phy_stop(rwnx_hw);

		bes_ignore_bcn_edca(rwnx_hw, false);

		bes_coex_arb_down(rwnx_hw);
		/* stop fw */
		rwnx_wf_status_clear(rwnx_hw, RWNX_DEV_STARTED);
#if defined(CONFIG_RWNX_PCIE) || defined(CONFIG_BES_IBUS)
		rwnx_send_reset(rwnx_hw);
#endif
#if defined(CONFIG_BES_POWER_CTRL) && !defined(CONFIG_RWNX_PCIE)
		bes_power_ctrl_wlan_close(rwnx_hw);
#endif
	}

	return 0;
}

#if LINUX_VERSION_CODE <  KERNEL_VERSION(5, 15, 0)
static int rwnx_do_ioctl(struct net_device *net, struct ifreq *req, int cmd)
#else
static int rwnx_do_ioctl(struct net_device *net, struct ifreq *req, void __user *data, int cmd)
#endif
{
	int ret = 0;
	///TODO: add ioctl command handler later
	switch (cmd) {
	case (SIOCDEVPRIVATE+1):
		ret = android_priv_cmd(net, req, cmd);
		break;
	default:
		ret = -EOPNOTSUPP;
	}
	return ret;
}

/**
 * struct net_device_stats* (*ndo_get_stats)(struct net_device *dev);
 *	Called when a user wants to get the network device usage
 *	statistics. Drivers must do one of the following:
 *	1. Define @ndo_get_stats64 to fill in a zero-initialised
 *	   rtnl_link_stats64 structure passed by the caller.
 *	2. Define @ndo_get_stats to update a net_device_stats structure
 *	   (which should normally be dev->stats) and return a pointer to
 *	   it. The structure may be changed asynchronously only if each
 *	   field is written atomically.
 *	3. Update dev->stats asynchronously and atomically, and define
 *	   neither operation.
 */
static struct net_device_stats *rwnx_get_stats(struct net_device *dev)
{
	struct rwnx_vif *vif = netdev_priv(dev);

	return &vif->net_stats;
}

/**
 * u16 (*ndo_select_queue)(struct net_device *dev, struct sk_buff *skb,
 *                         struct net_device *sb_dev);
 *	Called to decide which queue to when device supports multiple
 *	transmit queues.
 */
u16 rwnx_select_queue(struct net_device *dev, struct sk_buff *skb, struct net_device *sb_dev)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	return rwnx_select_txq(rwnx_vif, skb);
}

/**
 * int (*ndo_set_mac_address)(struct net_device *dev, void *addr);
 *	This function  is called when the Media Access Control address
 *	needs to be changed. If this interface is not defined, the
 *	mac address can not be changed.
 */
static int rwnx_set_mac_address(struct net_device *dev, void *addr)
{
	struct sockaddr *sa = addr;
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = rwnx_vif->rwnx_hw;
	u8 dev_addr[ETH_ALEN] = {0};
#if defined(CONFIG_NOTIFY_CHANGED_MAC)
	u32 drv_vif_idx_map = 0;
#else
	struct mm_add_if_cfm add_if_cfm;
	int ret = 0;
#endif
	u8 vif_index = rwnx_vif->vif_index;

	bes_info(BES_DBG_IFACE, RWNX_FN_ENTRY_STR);

	/* Check if the address is valid */
	if (!is_valid_ether_addr(sa->sa_data))
		return -EADDRNOTAVAIL;

	if (vif_index >= NX_ITF_MAX) {
		return -EADDRNOTAVAIL;
	}

	/* finish scan in advance */
	if (test_bit(RWNX_DEV_SCANNING, &rwnx_hw->flags)) {
		rwnx_cfg80211_abort_scan(rwnx_hw->wiphy, &rwnx_vif->wdev);
	}

	/* Check if there is an another that prevent changing mac */
	if (down_trylock(&rwnx_hw->phy_op_sema)) {
		bes_info(BES_DBG_IFACE, "Change Mac not allowed now, Try again\n");
		return -EBUSY;
	}

	/* If there is an interface is working, reject the request */
	list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
		if (netif_running(rwnx_vif->ndev) &&
		    netif_carrier_ok(rwnx_vif->ndev)) {
			up(&rwnx_hw->phy_op_sema);
			return -EBUSY;
		}
	}

	/* Calculate perm_addr based on vif drv index  */
	rwnx_vif = netdev_priv(dev);
	memcpy(dev_addr, sa->sa_data, ETH_ALEN);
	dev_addr[5] ^= rwnx_vif->drv_vif_index;
	memcpy(rwnx_hw->wiphy->perm_addr, dev_addr, ETH_ALEN);

	/* Commit dev address for all interface */
	list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
		memcpy(dev_addr, rwnx_hw->wiphy->perm_addr, ETH_ALEN);
		dev_addr[5] ^= rwnx_vif->drv_vif_index;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
		dev_addr_set(rwnx_vif->ndev, dev_addr);
#else
		memcpy((void*)rwnx_vif->ndev->dev_addr, dev_addr, ETH_ALEN);
#endif
#ifdef CONFIG_OPLUS_PLATFORM
		netdev_info(rwnx_vif->ndev, "Change mac to "OPLUSMACDBG"\n", OPLUSMAC2STRDBG(dev_addr));
#else
		netdev_info(rwnx_vif->ndev, "Change mac to %pM\n", dev_addr);
#endif
	}

#if defined(CONFIG_NOTIFY_CHANGED_MAC)
	do {
		spin_lock_bh(&rwnx_hw->cb_lock);
		/* No interface that was added */
		if (!rwnx_hw->vif_started) {
			spin_unlock_bh(&rwnx_hw->cb_lock);
			break;
		}

		/* Manually increase vif_started value to avoid power down */
		rwnx_hw->vif_started++;
		spin_unlock_bh(&rwnx_hw->cb_lock);

		/* Close vif interface that was added */
		list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
			if (!netif_running(rwnx_vif->ndev))
				continue;

			dev_close(rwnx_vif->ndev);
			drv_vif_idx_map |= BIT(rwnx_vif->drv_vif_index);
		}

		/* Re-open vif interface that was added previously */
		list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
			if (!(drv_vif_idx_map & BIT(rwnx_vif->drv_vif_index)))
				continue;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5,0,0))
			dev_open(rwnx_vif->ndev, NULL);
#else
			dev_open(rwnx_vif->ndev);
#endif
		}

		/* Manually decrease vif_started value */
		spin_lock_bh(&rwnx_hw->cb_lock);
		rwnx_hw->vif_started--;
		spin_unlock_bh(&rwnx_hw->cb_lock);
	} while (0);
#else
	/* Remove vif interface that was added */
	list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
		if (netif_running(rwnx_vif->ndev)) {
			if ((ret = rwnx_send_remove_if(rwnx_hw, rwnx_vif->vif_index))) {
				up(&rwnx_hw->phy_op_sema);
				netdev_err(rwnx_vif->ndev, "%s: del vif fail\n", __func__);
				return ret;
			}
		}
	}

	/* Re-add vif interface that was added previously */
	list_for_each_entry(rwnx_vif, &rwnx_hw->vifs, list) {
		if (netif_running(rwnx_vif->ndev)) {
			u8 old_vif_index = rwnx_vif->vif_index;
			if ((ret = rwnx_send_add_if(rwnx_hw, rwnx_vif->ndev->dev_addr, RWNX_VIF_TYPE(rwnx_vif), false, &add_if_cfm))) {
				up(&rwnx_hw->phy_op_sema);
				netdev_err(rwnx_vif->ndev, "%s: add vif fail\n", __func__);
				return ret;
			}

			if (add_if_cfm.status != CO_OK || (add_if_cfm.inst_nbr >= NX_ITF_MAX)) {
				up(&rwnx_hw->phy_op_sema);
				netdev_err(rwnx_vif->ndev, "%s: Status Error(%d), inst_nbr:%u\n", __func__,
					   add_if_cfm.status, add_if_cfm.inst_nbr);
				return -EIO;
			}

			spin_lock_bh(&rwnx_hw->cb_lock);
			if (old_vif_index < NX_ITF_MAX)
				rwnx_hw->vif_table[old_vif_index] = NULL;
			rwnx_vif->vif_index = add_if_cfm.inst_nbr;
			rwnx_hw->vif_table[add_if_cfm.inst_nbr] = rwnx_vif;
			rwnx_hw->vif_started--;
			spin_unlock_bh(&rwnx_hw->cb_lock);
			if (old_vif_index < NX_ITF_MAX)
				bes_filter_manager_interface_close(rwnx_hw, old_vif_index);

			spin_lock_bh(&rwnx_hw->cb_lock);
			rwnx_hw->vif_started++;
			spin_unlock_bh(&rwnx_hw->cb_lock);

			bes_filter_manager_interface_open(rwnx_vif);
			netdev_info(rwnx_vif->ndev, "set mac, CHANGE VIF_IDX " TR_VIF_STR, TR_VIF(rwnx_vif));
		}
	}
#endif

	/* release phy_op_sema */
	up(&rwnx_hw->phy_op_sema);

	return 0;
}

static void rwnx_set_multicast_list(struct net_device *dev)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct wireless_dev *wdev = dev->ieee80211_ptr;
	struct rwnx_hw *rwnx_hw = NULL;
	int ret =0;
	bool promisc = false, promisc_old = false;
	u8 rxfilter = 0;
	u8 vif_index;

	/* Determine whether the control is effective and wifi is on */
	if (!rwnx_vif || !(rwnx_hw = rwnx_vif->rwnx_hw) || !wdev ||
	    wdev->wiphy != rwnx_hw->wiphy || !bes_get_wlan_enabled(rwnx_hw))
		return;

	/* Only support sta or p2p gc */
	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_STATION &&
	    RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_P2P_CLIENT)
		return;

	/* Check the validity of vif_index */
	vif_index = rwnx_vif->vif_index;
	if (vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_FILTER_MANAGER, "invalid " TR_VIF_STR, TR_VIF(rwnx_vif));
		return;
	}

	/* Check the validity of parameters and vif is up */
	if (!rwnx_vif->up) {
		bes_err(BES_DBG_FILTER_MANAGER, "%s ERROR: Vif is not up\n", __func__);
		return;
	}

	/* Local variables and state synchronization */
	promisc_old = !!(rwnx_hw->filter.local_flags[vif_index] & IFF_PROMISC);
	rwnx_hw->filter.local_flags[vif_index] = dev->flags;

	// IFF_PROMISC: Indicates that interface receives all packets.
	promisc = !!(rwnx_hw->filter.local_flags[vif_index] & IFF_PROMISC);

	/* The state of promiscuous mode changes, and multicast related operations are not performed */
	if (promisc_old != promisc) {
		bes_dbg(BES_DBG_FILTER_MANAGER, "%s State change of promiscuous mode.\n", __func__);
		return;
	}

	do {
		/* Get rx filter */
		rxfilter = bes_wpa_priv_cmd_get_rxfilte(rwnx_vif);

		/* Ctrl multicast filter in ifl_map */
		ret = bes_filter_manager_ctrl_multicast_filter(rwnx_vif, rxfilter);
		if (ret)
			break;

		/* Queue work to sync filter */
		ret = bes_filter_manager_sync_filter(rwnx_hw, false);
	} while(0);
	bes_err_with_cond(ret, BES_DBG_FILTER_MANAGER,
			"%s ERROR(%d): Fail to set multicast list\n", __func__, ret);
}

static const struct net_device_ops rwnx_netdev_ops = {
	.ndo_open               = rwnx_open,
	.ndo_stop               = rwnx_close,
#if LINUX_VERSION_CODE <  KERNEL_VERSION(5, 15, 0)
	.ndo_do_ioctl           = rwnx_do_ioctl,
#else
	.ndo_siocdevprivate     = rwnx_do_ioctl,
#endif
	.ndo_start_xmit         = rwnx_start_xmit,
	.ndo_get_stats          = rwnx_get_stats,
	.ndo_select_queue       = rwnx_select_queue,
	.ndo_set_mac_address    = rwnx_set_mac_address,
	//  .ndo_set_features       = rwnx_set_features,
	.ndo_set_rx_mode        = rwnx_set_multicast_list,
};

static netdev_tx_t rwnx_monitor_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ieee80211_radiotap_header *prthdr = (struct ieee80211_radiotap_header *)skb->data;
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct ieee80211_channel *mon_chan;
	struct cfg80211_mgmt_tx_params mon_params;
	u16 len_rthdr;
	u64 cookie = 0;

	bes_dbg_dump(BES_DBG_TX, "", (u8 *)skb->data, skb->len);
	if (unlikely(skb->len < sizeof(struct ieee80211_radiotap_header)))
		goto free;

	if (unlikely(prthdr->it_version))
		goto free;

	/* then there must be a radiotap header with a length we can use */
	len_rthdr = ieee80211_get_radiotap_len(skb->data);
	if (unlikely(skb->len < len_rthdr))
		goto free;

	/* get monitor channel info */
	if (rwnx_chanctx_valid(rwnx_vif->rwnx_hw, rwnx_vif->ch_index)) {
		struct rwnx_chanctx *ctxt;
		ctxt = &rwnx_vif->rwnx_hw->chanctx_table[rwnx_vif->ch_index];
		mon_chan = ctxt->chan_def.chan;
		bes_dbg(BES_DBG_TX, "%s %d rwnx_vif->ch_index:%d monitor_chan->center_freq:%d skb->len:%d",
			__func__, __LINE__, rwnx_vif->ch_index, mon_chan->center_freq, skb->len);
	} else {
		bes_err(BES_DBG_TX, "%s %d error", __func__, __LINE__);
		goto free;
	}

	/* set mon_params for tx */
	memset(&mon_params, 0, sizeof(struct cfg80211_mgmt_tx_params));
	mon_params.chan = mon_chan;
	mon_params.offchan = false;
	mon_params.buf = skb->data + len_rthdr;
	mon_params.len = skb->len - len_rthdr;

	rwnx_cfg80211_mgmt_tx(rwnx_vif->rwnx_hw->wiphy, &rwnx_vif->wdev, &mon_params, &cookie);
free:
	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

static const struct net_device_ops rwnx_netdev_monitor_ops = {
	.ndo_open = rwnx_open,
	.ndo_stop = rwnx_close,
	.ndo_start_xmit = rwnx_monitor_start_xmit,
	.ndo_get_stats = rwnx_get_stats,
	.ndo_set_mac_address = rwnx_set_mac_address,
};

#ifdef CONFIG_BES_NAPI
static int napi_recv(struct rwnx_vif *vif, int budget)
{
	struct sk_buff *pskb;
	int work_done = 0;

	while ((work_done < budget) &&
	       (!skb_queue_empty(&vif->rx_napi_skb_queue))) {
		pskb = skb_dequeue(&vif->rx_napi_skb_queue);
		if (!pskb)
			break;

#ifdef CONFIG_BES_GRO
		/**
		 * cloned SKB use dataref to avoid kernel release it.
		 * But dataref changed in napi_gro_receive.
		 * So, we should prevent cloned SKB go into napi_gro_receive.
		 */
		if (!skb_cloned(pskb)) {
			napi_gro_receive(&vif->napi, pskb);
			work_done++;
			continue;
		}
#endif /* CONFIG_BES_GRO */

		pskb->dev = vif->ndev;
		if (netif_receive_skb(pskb) == NET_RX_SUCCESS)
			work_done++;
	}

	return work_done;
}

static int rwnx_recv_napi_poll(struct napi_struct *napi, int budget)
{
	struct rwnx_vif *vif = container_of(napi, struct rwnx_vif, napi);
	int work_done = 0;

	work_done = napi_recv(vif, budget);
	if (work_done < budget) {
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0))
		napi_complete_done(napi, work_done);
#else
		napi_complete(napi);
#endif
		if (!skb_queue_empty(&vif->rx_napi_skb_queue))
			napi_schedule(napi);
	}

	return work_done;
}
#endif

static void rwnx_netdev_setup(struct net_device *dev)
{
	ether_setup(dev);
	netif_carrier_off(dev);
	dev->priv_flags &= ~IFF_TX_SKB_SHARING;
	dev->netdev_ops = &rwnx_netdev_ops;
#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 12, 0)
	dev->destructor = free_netdev;
#else
	dev->needs_free_netdev = true;
#endif
	dev->watchdog_timeo = RWNX_TX_LIFETIME_MS;

	dev->needed_headroom = RWNX_TX_MAX_HEADROOM;
#ifdef CONFIG_RWNX_AMSDUS_TX
	dev->needed_headroom =
		max(dev->needed_headroom, (unsigned short)(sizeof(struct rwnx_amsdu_txhdr) + sizeof(struct ethhdr) + 4 + RWNX_TX_PSA_HEAD_LEN +
							   sizeof(rfc1042_header) + 2));
#endif /* CONFIG_RWNX_AMSDUS_TX */

	dev->hw_features = 0;
#ifdef CONFIG_TRX_COPY_LESS
	dev->features = NETIF_F_SG;
#endif
#ifdef BES_WIRELESS_EXT
	dev->wireless_handlers = &bes_iw_handler_def;
#endif
}

/*********************************************************************
 * Cfg80211 callbacks (and helper)
 *********************************************************************/
static struct wireless_dev *rwnx_interface_add(struct rwnx_hw *rwnx_hw, const char *name,
					       unsigned char name_assign_type, enum nl80211_iftype type,
					       struct vif_params *params)
{
	struct net_device *ndev;
	struct rwnx_vif *vif;
	int min_idx, max_idx;
	int vif_idx = -1;
	int i;
	unsigned char dev_addr[ETH_ALEN];

	// Look for an available VIF
	if (type == NL80211_IFTYPE_AP_VLAN) {
		min_idx = NX_VIRT_DEV_MAX;
		max_idx = NX_ITF_MAX;
	} else {
		min_idx = 0;
		max_idx = NX_VIRT_DEV_MAX;
	}

	for (i = min_idx; i < max_idx; i++) {
		if ((rwnx_hw->avail_idx_map) & BIT(i)) {
			vif_idx = i;
			break;
		}
	}
	if (vif_idx < 0)
		return NULL;

#ifndef CONFIG_RWNX_MON_DATA
	list_for_each_entry(vif, &rwnx_hw->vifs, list) {
		// Check if monitor interface already exists or type is monitor
		if ((RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_MONITOR) || (type == NL80211_IFTYPE_MONITOR)) {
			wiphy_err(rwnx_hw->wiphy, "Monitor+Data interface support (MON_DATA) disabled\n");
			return NULL;
		}
	}
#endif

	ndev = alloc_netdev_mqs(sizeof(*vif), name, name_assign_type, rwnx_netdev_setup, NX_NB_NDEV_TXQ, 1);
	if (!ndev)
		return NULL;

	vif = netdev_priv(ndev);
	ndev->ieee80211_ptr = &vif->wdev;
	vif->wdev.wiphy = rwnx_hw->wiphy;
	vif->rwnx_hw = rwnx_hw;
	vif->ndev = ndev;
	vif->drv_vif_index = vif_idx;
	vif->vif_index = RWNX_INVALID_VIF;
	SET_NETDEV_DEV(ndev, wiphy_dev(vif->wdev.wiphy));
	vif->wdev.netdev = ndev;
	vif->wdev.iftype = type;
	vif->up = false;
	vif->ch_index = RWNX_CH_NOT_SET;
	vif->generation = 0;
	memset(&vif->net_stats, 0, sizeof(vif->net_stats));
#ifdef CONFIG_BES_NAPI
	skb_queue_head_init(&vif->rx_napi_skb_queue);
	// napi_add before register_netdevice
	netif_napi_add(ndev, &vif->napi, rwnx_recv_napi_poll, NAPI_POLL_WEIGHT);
#endif
	INIT_DELAYED_WORK(&vif->connect_retry_work, rwnx_connect_retry_work_func);

	switch (type) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		vif->sta.flags = 0;
		vif->sta.ap = NULL;
		vif->sta.tdls_sta = NULL;
		vif->sta.ft_assoc_ies = NULL;
		vif->sta.ft_assoc_ies_len = 0;
		vif->sta.wnm_process = false;
		vif->sta.conn_abort = false;
		break;
	case NL80211_IFTYPE_MESH_POINT:
		INIT_LIST_HEAD(&vif->ap.mpath_list);
		INIT_LIST_HEAD(&vif->ap.proxy_list);
		vif->ap.mesh_pm = NL80211_MESH_POWER_ACTIVE;
		vif->ap.next_mesh_pm = NL80211_MESH_POWER_ACTIVE;
		// no break
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
		INIT_LIST_HEAD(&vif->ap.sta_list);
		memset(&vif->ap.bcn, 0, sizeof(vif->ap.bcn));
		vif->ap.flags = 0;
		vif->ap.bcmc_index = NX_REMOTE_STA_MAX;
		vif->ap.csa = NULL;
		break;
	case NL80211_IFTYPE_AP_VLAN: {
		struct rwnx_vif *master_vif;
		bool found = false;
		list_for_each_entry(master_vif, &rwnx_hw->vifs, list) {
			if ((RWNX_VIF_TYPE(master_vif) == NL80211_IFTYPE_AP) &&
			    !(!memcmp(master_vif->ndev->dev_addr, params->macaddr, ETH_ALEN))) {
				found = true;
				break;
			}
		}

		if (!found)
			goto err;

		vif->ap_vlan.master = master_vif;
		vif->ap_vlan.sta_4a = NULL;
		break;
	}
	case NL80211_IFTYPE_MONITOR:
		ndev->type = ARPHRD_IEEE80211_RADIOTAP;
		ndev->netdev_ops = &rwnx_netdev_monitor_ops;
		break;
	default:
		break;
	}

	if (type == NL80211_IFTYPE_AP_VLAN)
		memcpy(dev_addr, params->macaddr, ETH_ALEN);
	else {
		memcpy(dev_addr, rwnx_hw->wiphy->perm_addr, ETH_ALEN);
		dev_addr[5] ^= vif_idx;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 0))
	dev_addr_set(ndev, dev_addr);
#else
	memcpy(ndev->dev_addr, dev_addr, ETH_ALEN);
#endif

	if (params) {
		vif->use_4addr = params->use_4addr;
		ndev->ieee80211_ptr->use_4addr = params->use_4addr;
	} else
		vif->use_4addr = false;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0))
	if (cfg80211_register_netdevice(ndev))
#else
	if (register_netdevice(ndev))
#endif
		goto err;

	spin_lock_bh(&rwnx_hw->cb_lock);
	list_add_tail(&vif->list, &rwnx_hw->vifs);
	spin_unlock_bh(&rwnx_hw->cb_lock);
	rwnx_hw->avail_idx_map &= ~BIT(vif_idx);

	rwnx_dbgfs_register_vif(rwnx_hw, vif);

	return &vif->wdev;

err:
	free_netdev(ndev);
	return NULL;
}

/**
 * @brief Retrieve the rwnx_sta object allocated for a given MAC address
 * and a given role.
 */
static struct rwnx_sta *rwnx_retrieve_sta(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, u8 *addr, __le16 fc,
					  bool ap)
{
	if (ap) {
		/* only deauth, disassoc and action are bufferable MMPDUs */
		bool bufferable = ieee80211_is_deauth(fc) || ieee80211_is_disassoc(fc) || ieee80211_is_action(fc);

		/* Check if the packet is bufferable or not */
		if (bufferable) {
			/* Check if address is a broadcast or a multicast address */
			if (is_broadcast_ether_addr(addr) || is_multicast_ether_addr(addr)) {
				/* Returned STA pointer */
				struct rwnx_sta *rwnx_sta = rwnx_rx_get_sta(rwnx_hw, rwnx_vif->ap.bcmc_index);

				if (rwnx_sta && rwnx_sta->valid)
					return rwnx_sta;
			} else {
				/* Returned STA pointer */
				struct rwnx_sta *rwnx_sta;

				/* Go through list of STAs linked with the provided VIF */
				list_for_each_entry(rwnx_sta, &rwnx_vif->ap.sta_list, list) {
					if (rwnx_sta->valid && ether_addr_equal(rwnx_sta->mac_addr, addr)) {
						/* Return the found STA */
						return rwnx_sta;
					}
				}
			}
		}
	} else {
		return rwnx_vif->sta.ap;
	}

	return NULL;
}

/**
 * @add_virtual_intf: create a new virtual interface with the given name,
 *	must set the struct wireless_dev's iftype. Beware: You must create
 *	the new netdev in the wiphy's network namespace! Returns the struct
 *	wireless_dev, or an ERR_PTR. For P2P device wdevs, the driver must
 *	also set the address member in the wdev.
 */
static struct wireless_dev *rwnx_cfg80211_add_iface(struct wiphy *wiphy, const char *name,
						    unsigned char name_assign_type, enum nl80211_iftype type,
						    struct vif_params *params)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct wireless_dev *wdev;

	bes_dbg(BES_DBG_IFACE, RWNX_FN_ENTRY_STR);
	wdev = rwnx_interface_add(rwnx_hw, name, name_assign_type, type, params);

	if (!wdev)
		return ERR_PTR(-EINVAL);

	return wdev;
}

/**
 * @del_virtual_intf: remove the virtual interface
 */
static int rwnx_cfg80211_del_iface(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	struct net_device *dev = wdev->netdev;
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);

	bes_dbg(BES_DBG_IFACE, RWNX_FN_ENTRY_STR);

	rwnx_dbgfs_unregister_vif(rwnx_hw, rwnx_vif);
	if (dev->reg_state == NETREG_REGISTERED) {
		/* Will call rwnx_close if interface is UP */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0)) || defined(PLAT_HONOR_PLAT)
		cfg80211_unregister_netdevice(dev);
#else
		unregister_netdevice(dev);
#endif
	}

	spin_lock_bh(&rwnx_hw->cb_lock);
	list_del(&rwnx_vif->list);
	spin_unlock_bh(&rwnx_hw->cb_lock);
	rwnx_hw->avail_idx_map |= BIT(rwnx_vif->drv_vif_index);
	rwnx_vif->ndev = NULL;

	/* Clear the priv in adapter */
	dev->ieee80211_ptr = NULL;

	return 0;
}

/**
 * @change_virtual_intf: change type/configuration of virtual interface,
 *	keep the struct wireless_dev's iftype updated.
 */
static int rwnx_cfg80211_change_iface(struct wiphy *wiphy, struct net_device *dev, enum nl80211_iftype type,
				      struct vif_params *params)
{
#ifndef CONFIG_RWNX_MON_DATA
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
#endif
	struct rwnx_vif *vif = netdev_priv(dev);

	bes_dbg(BES_DBG_IFACE, RWNX_FN_ENTRY_STR);

	if (vif->up)
		return (-EBUSY);

#ifndef CONFIG_RWNX_MON_DATA
	if ((type == NL80211_IFTYPE_MONITOR) && (RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_MONITOR)) {
		struct rwnx_vif *vif_el;
		list_for_each_entry(vif_el, &rwnx_hw->vifs, list) {
			// Check if data interface already exists
			if ((vif_el != vif) && (RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_MONITOR)) {
				wiphy_err(rwnx_hw->wiphy, "Monitor+Data interface support (MON_DATA) disabled\n");
				return -EIO;
			}
		}
	}
#endif

	// Reset to default case (i.e. not monitor)
	dev->type = ARPHRD_ETHER;
	dev->netdev_ops = &rwnx_netdev_ops;

	switch (type) {
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
		vif->sta.flags = 0;
		vif->sta.ap = NULL;
		vif->sta.tdls_sta = NULL;
		vif->sta.ft_assoc_ies = NULL;
		vif->sta.ft_assoc_ies_len = 0;
		break;
	case NL80211_IFTYPE_MESH_POINT:
		INIT_LIST_HEAD(&vif->ap.mpath_list);
		INIT_LIST_HEAD(&vif->ap.proxy_list);
		vif->ap.mesh_pm = NL80211_MESH_POWER_ACTIVE;
		vif->ap.next_mesh_pm = NL80211_MESH_POWER_ACTIVE;
		// no break
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
		INIT_LIST_HEAD(&vif->ap.sta_list);
		memset(&vif->ap.bcn, 0, sizeof(vif->ap.bcn));
		vif->ap.flags = 0;
		vif->ap.bcmc_index = NX_REMOTE_STA_MAX;
		vif->ap.csa = NULL;
		break;
	case NL80211_IFTYPE_AP_VLAN:
		return -EPERM;
	case NL80211_IFTYPE_MONITOR:
		dev->type = ARPHRD_IEEE80211_RADIOTAP;
		dev->netdev_ops = &rwnx_netdev_monitor_ops;
		break;
	default:
		break;
	}

	vif->generation = 0;
	vif->wdev.iftype = type;
	if (params->use_4addr != -1)
		vif->use_4addr = params->use_4addr;

	return 0;
}

/**
 * @scan: Request to do a scan. If returning zero, the scan request is given
 *	the driver, and will be valid until passed to cfg80211_scan_done().
 *	For scan results, call cfg80211_inform_bss(); you can call this outside
 *	the scan/scan_done bracket too.
 */

static int rwnx_cfg80211_scan(struct wiphy *wiphy, struct cfg80211_scan_request *request)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = container_of(request->wdev, struct rwnx_vif, wdev);
	int error;
	u8 vif_index;

	bes_info(BES_DBG_SCAN, RWNX_FN_ENTRY_STR);

	/* Check the validity of vif_index */
	vif_index = rwnx_vif->vif_index;
	if (vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_SCAN, "%s, vif_index %u invalid\n", __func__, vif_index);
		return -EINVAL;
	}

	/* Check that no other Scan procedure has been launched */
	if (rwnx_hw->scan_request)
		return -EBUSY;

	if (!bes_sysfs_support_online_scan() &&
	    rwnx_vif_is_connected(rwnx_vif)) {
		bes_err(BES_DBG_SCAN, "skip online scan\n");
		return -EAGAIN;
	}

	if (down_trylock(&rwnx_hw->phy_op_sema)) {
		bes_info(BES_DBG_SCAN, "Scan not allowed now, Try again\n");
		return -EAGAIN;
	}

	rwnx_wf_status_set(rwnx_hw, RWNX_DEV_SCANNING);

	rwnx_hw->scan_request = request;

	if ((error = rwnx_send_scanu_req(rwnx_hw, rwnx_vif, request))) {
		rwnx_wf_status_clear(rwnx_hw, RWNX_DEV_SCANNING);
		up(&rwnx_hw->phy_op_sema);
		rwnx_hw->scan_request = NULL;
		bes_err(BES_DBG_SCAN, "rwnx_send_scanu_req failed,error=%d\n", error);
		return error;
	}

	bes_dbg(BES_DBG_SCAN, "%s %d.\n", __func__, __LINE__);

	return 0;
}
/*
 * @abort_scan: Tell the driver to abort an ongoing scan. The driver shall
 *	indicate the status of the scan through cfg80211_scan_done().
 */
static void rwnx_cfg80211_abort_scan(struct wiphy *wiphy, struct wireless_dev *wdev)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);

	bes_info(BES_DBG_MSG_RX, RWNX_FN_ENTRY_STR);

	if (test_bit(RWNX_DEV_SCANNING, &rwnx_hw->flags)) {
		rwnx_send_scan_cancel_req(rwnx_hw, NULL);
	} else {
		bes_warn(BES_DBG_SCAN, "No scanning in progress");
	}
}

#ifdef CONFIG_SCHED_SCAN
/* @sched_scan_start: Tell the driver to start a scheduled scan.
 */
int rwnx_cfg80211_sched_scan_start(struct wiphy *wiphy, struct net_device *dev,
				   struct cfg80211_sched_scan_request *request)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *vif = netdev_priv(dev);
	int ret = 0;

	bes_dbg(BES_DBG_SCHED_SCAN, "%s in.\n", __func__);

	request->scan_plans[1].interval = STSTION_SCHED_SCAN_MAX ;
	/* Show sched scan cfg */
	bes_sched_scan_cfg_show(rwnx_hw, vif->vif_index, request);

	/* Sched scan start */
	ret = bes_sched_scan_start(rwnx_hw, vif->vif_index, request);
	bes_err_with_cond(ret, BES_DBG_SCHED_SCAN, "Fail to start sched scan. ret=%d\n", ret);

	bes_dbg(BES_DBG_SCHED_SCAN, "%s out.\n", __func__);

	return ret;
}

/* @sched_scan_stop: Tell the driver to stop an ongoing scheduled scan with
 *	given request id. This call must stop the scheduled scan and be ready
 *	for starting a new one before it returns, i.e. @sched_scan_start may be
 *	called immediately after that again and should not fail in that case.
 *	The driver should not call cfg80211_sched_scan_stopped() for a requested
 *	stop (when this method returns 0).
 */
int rwnx_cfg80211_sched_scan_stop(struct wiphy *wiphy, struct net_device *dev, u64 reqid)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *vif = netdev_priv(dev);
	int ret = 0;

	bes_dbg(BES_DBG_SCHED_SCAN, "%s in.\n", __func__);

	/* Sched scan stop */
	ret = bes_sched_scan_stop(rwnx_hw, vif->vif_index, reqid);
	bes_err_with_cond(ret, BES_DBG_SCHED_SCAN, "Fail to stop sched scan. ret=%d\n", ret);

	bes_dbg(BES_DBG_SCHED_SCAN, "%s out.\n", __func__);

	return ret;
}
#endif

/**
 * @add_key: add a key with the given parameters. @mac_addr will be %NULL
 *	when adding a group key.
 */
static int rwnx_cfg80211_add_key(struct wiphy *wiphy, struct net_device *netdev,
#ifdef ADPT_GKI_KERNEL
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 81))
				 int link_id,
#endif
#else /* ADPT_GKI_KERNEL */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
				 int link_id,
#endif
#endif /* ADPT_GKI_KERNEL */
				 u8 key_index, bool pairwise, const u8 *mac_addr, struct key_params *params)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *vif = netdev_priv(netdev);
	int i, error = 0;
	struct mm_key_add_cfm key_add_cfm;
	u8_l cipher = 0;
	struct rwnx_sta *sta = NULL;
	struct rwnx_key *rwnx_key;

	bes_dbg(BES_DBG_KEY, RWNX_FN_ENTRY_STR);

	/* Check the validity of vif_index */
	if (vif->vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_LINK, "%s, vif_index %u invalid\n", __func__, vif->vif_index);
		return -EINVAL;
	}

	if (mac_addr) {
		sta = rwnx_get_sta(rwnx_hw, mac_addr);
		if (!sta)
			return -EINVAL;
		rwnx_key = &sta->key;
	} else
		rwnx_key = &vif->key[key_index];

	/* Retrieve the cipher suite selector */
	switch (params->cipher) {
	case WLAN_CIPHER_SUITE_WEP40:
		cipher = MAC_CIPHER_WEP40;
		break;
	case WLAN_CIPHER_SUITE_WEP104:
		cipher = MAC_CIPHER_WEP104;
		break;
	case WLAN_CIPHER_SUITE_TKIP:
		cipher = MAC_CIPHER_TKIP;
		break;
	case WLAN_CIPHER_SUITE_CCMP:
		cipher = MAC_CIPHER_CCMP;
		break;
	case WLAN_CIPHER_SUITE_AES_CMAC:
		cipher = MAC_CIPHER_BIP_CMAC_128;
		break;
	case WLAN_CIPHER_SUITE_SMS4: {
		// Need to reverse key order
		u8 tmp, *key = (u8 *)params->key;
		cipher = MAC_CIPHER_WPI_SMS4;
		for (i = 0; i < WPI_SUBKEY_LEN / 2; i++) {
			tmp = key[i];
			key[i] = key[WPI_SUBKEY_LEN - 1 - i];
			key[WPI_SUBKEY_LEN - 1 - i] = tmp;
		}
		for (i = 0; i < WPI_SUBKEY_LEN / 2; i++) {
			tmp = key[i + WPI_SUBKEY_LEN];
			key[i + WPI_SUBKEY_LEN] = key[WPI_KEY_LEN - 1 - i];
			key[WPI_KEY_LEN - 1 - i] = tmp;
		}
		break;
	}
	case WLAN_CIPHER_SUITE_GCMP:
		cipher = MAC_CIPHER_GCMP_128;
		break;
	case WLAN_CIPHER_SUITE_GCMP_256:
		cipher = MAC_CIPHER_GCMP_256;
		break;
	case WLAN_CIPHER_SUITE_CCMP_256:
		cipher = MAC_CIPHER_CCMP_256;
		break;
	default:
		return -EINVAL;
	}

	/* Bug fix: Sometimes EAPOL frames(message 4) are sent encrypted.
	 * So wait for EAPOL to complete adding the key. */
	if (bes_eapol_key_install_defer_store(vif, pairwise, key_index, cipher, params))
		return 0;

	if ((error = rwnx_send_key_add(rwnx_hw, vif->vif_index, (sta ? sta->sta_idx : 0xFF), pairwise,
				       (u8 *)params->key, params->key_len, key_index, cipher, &key_add_cfm)))
		return error;

	if (key_add_cfm.status != 0) {
		bes_err(BES_DBG_KEY, "%s: Status Error(%d)\n", __func__,(&key_add_cfm)->status);
		return -EIO;
	}

	/* Save the index retrieved from LMAC */
	rwnx_key->hw_idx = key_add_cfm.hw_key_idx;
	rwnx_key->valid = true;

	return 0;
}

/**
 * @get_key: get information about the key with the given parameters.
 *	@mac_addr will be %NULL when requesting information for a group
 *	key. All pointers given to the @callback function need not be valid
 *	after it returns. This function should return an error if it is
 *	not possible to retrieve the key, -ENOENT if it doesn't exist.
 *
 */
static int rwnx_cfg80211_get_key(struct wiphy *wiphy, struct net_device *netdev,
#ifdef ADPT_GKI_KERNEL
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 81))
				 int link_id,
#endif
#else /* ADPT_GKI_KERNEL */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
				 int link_id,
#endif
#endif /* ADPT_GKI_KERNEL */
				 u8 key_index, bool pairwise, const u8 *mac_addr, void *cookie,
				 void (*callback)(void *cookie, struct key_params*))
{
	bes_dbg(BES_DBG_KEY, RWNX_FN_ENTRY_STR);

	return -1;
}

/**
 * @del_key: remove a key given the @mac_addr (%NULL for a group key)
 *	and @key_index, return -ENOENT if the key doesn't exist.
 */
static int rwnx_cfg80211_del_key(struct wiphy *wiphy, struct net_device *netdev,
#ifdef ADPT_GKI_KERNEL
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 81))
				 int link_id,
#endif
#else /* ADPT_GKI_KERNEL */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
				 int link_id,
#endif
#endif /* ADPT_GKI_KERNEL */
				 u8 key_index, bool pairwise, const u8 *mac_addr)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *vif = netdev_priv(netdev);
	int error = 0;
	struct rwnx_sta *sta = NULL;
	struct rwnx_key *rwnx_key = NULL;

	bes_dbg(BES_DBG_KEY, RWNX_FN_ENTRY_STR);

	if (!rwnx_wlan_enabled(rwnx_hw)) {
		error = -ENODEV;
		goto err;
	}

	/* Check the validity of vif_index */
	if (vif->vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_LINK, "%s, vif_index %u invalid\n", __func__, vif->vif_index);
		return -EINVAL;
	}

	if (mac_addr) {
		/* Reject when rwnx_sta is deleted */
		sta = rwnx_get_sta(rwnx_hw, mac_addr);
		if (!sta) {
			goto err;
		}

		/* Get rwnx_key instance */
		rwnx_key = &sta->key;

		/*
		 * Reject delete unicast key if vif is sta/gc
		 * beacuse lmac can process by itself
		 */
		if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_STATION ||
		    RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_CLIENT) {
			goto err;
		}
	} else
		rwnx_key = &vif->key[key_index];

	/* Output debug information when delete pairwise key */
	bes_info_with_cond(mac_addr != NULL, BES_DBG_KEY, "Delete key, idx:%d pairwise:%d mac:%pM\n",
			   key_index, pairwise, mac_addr);

	/* Cancel key_install_defer_work or wait it finish */
	if (mac_addr)
		bes_eapol_cancel_key_install_defer_work(vif);

	if (rwnx_key->valid)
		error = rwnx_send_key_del(rwnx_hw, rwnx_key->hw_idx);

err:
	if (rwnx_key)
		rwnx_key->valid = false;

	return error;
}

/**
 * @set_default_key: set the default key on an interface
 */
static int rwnx_cfg80211_set_default_key(struct wiphy *wiphy, struct net_device *netdev,
#ifdef ADPT_GKI_KERNEL
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 81))
					 int link_id,
#endif
#else /* ADPT_GKI_KERNEL */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
					 int link_id,
#endif
#endif /* ADPT_GKI_KERNEL */
					 u8 key_index, bool unicast, bool multicast)
{
	bes_dbg(BES_DBG_KEY, RWNX_FN_ENTRY_STR);

	return 0;
}

/**
 * @set_default_mgmt_key: set the default management frame key on an interface
 */
static int rwnx_cfg80211_set_default_mgmt_key(struct wiphy *wiphy, struct net_device *netdev,
#ifdef ADPT_GKI_KERNEL
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 81))
					      int link_id,
#endif
#else /* ADPT_GKI_KERNEL */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
					      int link_id,
#endif
#endif /* ADPT_GKI_KERNEL */
					      u8 key_index)
{
	return 0;
}

/**
 * @connect: Connect to the ESS with the specified parameters. When connected,
 *	call cfg80211_connect_result() with status code %WLAN_STATUS_SUCCESS.
 *	If the connection fails for some reason, call cfg80211_connect_result()
 *	with the status from the AP.
 *	(invoked with the wireless_dev mutex held)
 */
static int rwnx_cfg80211_connect(struct wiphy *wiphy, struct net_device *dev, struct cfg80211_connect_params *sme)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct sm_connect_cfm sm_connect_cfm = {0};
	int error = 0;
	int wnm_rst = 0;

	bes_info(BES_DBG_LINK, RWNX_FN_ENTRY_STR);

	/* check if the sta link meets the coex requimrent */
	if (!bes_coex_arb_check_sta_assoc(rwnx_hw, sme->channel)) {
		return -ENOTSUPP;
	}

	/* Start connect new AP indicates the end of WNM process for STA*/
	spin_lock_bh(&rwnx_hw->cb_lock);
	if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION && rwnx_vif->sta.wnm_process) {
		wnm_rst = 1;
		rwnx_vif->sta.wnm_process = false;
	}
	spin_unlock_bh(&rwnx_hw->cb_lock);

	bes_dbg_with_cond(wnm_rst, BES_DBG_LINK, "%s, STA stop the WNM process\n", __func__);

	/* finish scan in advance */
	if (test_bit(RWNX_DEV_SCANNING, &rwnx_hw->flags)) {
		rwnx_cfg80211_abort_scan(wiphy, &rwnx_vif->wdev);
	}

	/* Update the conn state of STA */
	bes_conn_state_process(rwnx_vif, rwnx_vif->sta.ap, STA_CONN_ACTION_CONN_REQ);

	/* wait other mutex operation down */
	if (down_timeout(&rwnx_hw->phy_op_sema,
			 msecs_to_jiffies(PHY_OP_WAIT_TIMEOUT_IN_MS))) {
		WARN(1, "%s wait mutex operation timeout\n", __func__);
		return -EFAULT;
	}
	rwnx_wf_status_set(rwnx_hw, RWNX_DEV_CONNECTING);

	/* Reset the value of conn_abort*/
	rwnx_vif->sta.conn_abort = false;

	if (sme->auth_type == NL80211_AUTHTYPE_AUTOMATIC) {
		rwnx_vif->sta.flags |= RWNX_STA_AUTO_AUTH;
	}
	/* For SHARED-KEY authentication, must install key first */
	if ((sme->auth_type == NL80211_AUTHTYPE_SHARED_KEY ||
		sme->auth_type == NL80211_AUTHTYPE_AUTOMATIC) && sme->key) {
		struct key_params key_params;
		key_params.key = sme->key;
		key_params.seq = NULL;
		key_params.key_len = sme->key_len;
		key_params.seq_len = 0;
		key_params.cipher = sme->crypto.cipher_group;
#ifdef ADPT_GKI_KERNEL
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 81))
		rwnx_cfg80211_add_key(wiphy, dev, 0, sme->key_idx, false, NULL, &key_params);
#else
		rwnx_cfg80211_add_key(wiphy, dev, sme->key_idx, false, NULL, &key_params);
#endif
#else /* ADPT_GKI_KERNEL */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
		rwnx_cfg80211_add_key(wiphy, dev, 0, sme->key_idx, false, NULL, &key_params);
#else
		rwnx_cfg80211_add_key(wiphy, dev, sme->key_idx, false, NULL, &key_params);
#endif
#endif /* ADPT_GKI_KERNEL */
		rwnx_vif->sta.flags |= RWNX_STA_HAS_KEY;
	}
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
	else if ((sme->auth_type == NL80211_AUTHTYPE_SAE) && !(sme->flags & CONNECT_REQ_EXTERNAL_AUTH_SUPPORT)) {
		netdev_err(dev, "Doesn't support SAE without external authentication\n");
		error = -EINVAL;
		goto error_exit;
	}
#endif

	if (sme->channel) {
		error = bes_dbac_cali_connect(rwnx_vif, sme);
		if (error) {
			bes_err(BES_DBG_CALI, "%s Calibration failed err:%d\n", __func__, error);
		} else {
#ifdef CONFIG_2G_20M_ONLY
			rwnx_update_conn_param(rwnx_hw, sme->channel->band);
#endif
		}
	} else {
		bes_err(BES_DBG_MSG_TX, "Unable to start calibration, sme->channel is null.\n");
		bes_info_with_cond(sme->channel_hint, BES_DBG_MSG_TX,
				   "channel_hint is not null\n");
	}

	/* Forward the information to the LMAC */
	if ((error = rwnx_send_sm_connect_req(rwnx_hw, rwnx_vif, sme, &sm_connect_cfm)))
		goto error_conn_fail;

	// Check the status
	switch (sm_connect_cfm.status) {
	case CO_OK:
		rwnx_save_assoc_info_for_ft(rwnx_vif, sme);
		rwnx_vif->sta.retry_cnt = 0;
		error = 0;
		break;
	case CO_BUSY:
		error = -EINPROGRESS;
		break;
	case CO_BAD_PARAM:
		error = -EINVAL;
		break;
	case CO_OP_IN_PROGRESS:
		error = -EALREADY;
		break;
	default:
		error = -EIO;
		break;
	}

error_conn_fail:
	if (error < 0)
		bes_pwr_clear_busy_event(rwnx_hw, rwnx_vif->vif_index, VIF_CONST_EVENT_CONNECT);
error_exit:
	if (error < 0) {
		bes_dbac_cali_check(rwnx_hw);

		rwnx_wf_status_clear(rwnx_hw, RWNX_DEV_CONNECTING);
		up(&rwnx_hw->phy_op_sema);
		bes_err(BES_DBG_LINK, "%s failed, error=%d\n", __func__, error);
	}
	bes_dbg(BES_DBG_LINK, "%s,exit,ret=%d\n", __func__, error);
	return error;
}

/**
 * @disconnect: Disconnect from the BSS/ESS.
 *	(invoked with the wireless_dev mutex held)
 */
static int rwnx_cfg80211_disconnect(struct wiphy *wiphy, struct net_device *dev, u16 reason_code)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	int wnm_rst = 0;
	int ret;

	bes_info(BES_DBG_LINK, ">>> %s reason_code %d\n", __func__, reason_code);

	/* Update the conn state of STA */
	bes_conn_state_process(rwnx_vif, NULL, STA_CONN_ACTION_DISCONN_REQ);

	if (!rwnx_wf_status_test_and_wait(rwnx_hw, RWNX_DEV_CONNECTING, false, 5 * HZ)) {
		return -EBUSY;
	}

	spin_lock_bh(&rwnx_hw->cb_lock);
	if (!rwnx_vif_is_connected(rwnx_vif)) {
		if (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_STATION && rwnx_vif->sta.wnm_process) {
			wnm_rst = 1;
			rwnx_vif->sta.wnm_process = false;
			cfg80211_disconnected(dev, WLAN_REASON_DEAUTH_LEAVING, NULL, 0, true, GFP_ATOMIC);

		}
		spin_unlock_bh(&rwnx_hw->cb_lock);
		bes_warn(BES_DBG_LINK, "vif %d is already disconnected, wnm_rst: %d, just skip\n",
			 rwnx_vif->vif_index, wnm_rst);
		return 0;
	}
	spin_unlock_bh(&rwnx_hw->cb_lock);

	if (test_bit(RWNX_DEV_SCANNING, &rwnx_hw->flags)) {
		rwnx_cfg80211_abort_scan(wiphy, &rwnx_vif->wdev);
	}

	/* wait other mutex operation down */
	if (down_timeout(&rwnx_hw->phy_op_sema,
			 msecs_to_jiffies(PHY_OP_WAIT_TIMEOUT_IN_MS))) {
		WARN(1, "%s wait mutex operation timeout\n", __func__);
		return -EFAULT;
	}

	/* Check the validity of vif_index */
	if (rwnx_vif->vif_index >= NX_ITF_MAX) {
		up(&rwnx_hw->phy_op_sema);
		bes_err(BES_DBG_LINK, "%s, vif_index %u invalid\n", __func__, rwnx_vif->vif_index);
		return -EINVAL;
	}

	rwnx_wf_status_set(rwnx_hw, RWNX_DEV_DISCONNECTING);

	if ((ret = rwnx_send_sm_disconnect_req(rwnx_hw, rwnx_vif, reason_code))) {
		rwnx_wf_status_clear(rwnx_hw, RWNX_DEV_DISCONNECTING);
		up(&rwnx_hw->phy_op_sema);
		return ret;
	}
	/**
	 *  If it's connected, SM_DISCONNECT_CFM comes before SM_DISCONNECT_IND, 100 ms is enough;
	 *  otherwise SM_DISCONNECT_CFM comes without SM_DISCONNECT_IND
	 */
	if (!rwnx_wf_status_test_and_wait(rwnx_hw,
			RWNX_DEV_DISCONNECTING, false, msecs_to_jiffies(100))) {
		if (rwnx_wf_status_test_and_clear(rwnx_hw, RWNX_DEV_DISCONNECTING)) {
			/* If disconnect_ind does not reply, need to clear related records in
			 * the tx_loop mechanism to avoid miscontact by the tx_loop mechanism.
			 * Then delete or restart the ind_timer.
			 */
			bes_tx_loop_del_specific_ind_timer(rwnx_hw->plat, SM_DISCONNECT_REQ);
			up(&rwnx_hw->phy_op_sema);
		}
		return -EAGAIN;
	}

	return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
/**
 * @external_auth: indicates result of offloaded authentication processing from
 *     user space
 */
static int rwnx_cfg80211_external_auth(struct wiphy *wiphy, struct net_device *dev,
				       struct cfg80211_external_auth_params *params)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);

	/* Check the validity of vif_index */
	if (rwnx_vif->vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_LINK, "%s, vif_index %u invalid\n", __func__, rwnx_vif->vif_index);
		return -EINVAL;
	}

	if (!(rwnx_vif->sta.flags & RWNX_STA_EXT_AUTH))
		return -EINVAL;

	rwnx_external_auth_disable(rwnx_vif);
	return rwnx_send_sm_external_auth_required_rsp(rwnx_hw, rwnx_vif, params->status);
}
#endif

/**
 * @add_station: Add a new station.
 */
static int rwnx_cfg80211_add_station(struct wiphy *wiphy, struct net_device *dev, const u8 *mac,
				     struct station_parameters *params)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct me_sta_add_cfm me_sta_add_cfm;
	struct cfg80211_chan_def *chan_def = NULL;
	struct station_info *sinfo = NULL;
	int error = 0;

	bes_dbg(BES_DBG_AP, RWNX_FN_ENTRY_STR);

	/* Check the validity of vif_index */
	if (rwnx_vif->vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_LINK, "%s, vif_index %u invalid\n", __func__, rwnx_vif->vif_index);
		return -EINVAL;
	}

	WARN_ON(RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_AP_VLAN);

	/* Do not add TDLS station */
	if (params->sta_flags_set & BIT(NL80211_STA_FLAG_TDLS_PEER))
		return 0;

	/* Indicate we are in a STA addition process - This will allow handling
	 * potential PS mode change indications correctly
	 */
	rwnx_wf_status_set(rwnx_hw, RWNX_DEV_ADDING_STA);

	/* Forward the information to the LMAC */
	if ((error = rwnx_send_me_sta_add(rwnx_hw, params, mac, rwnx_vif->vif_index, &me_sta_add_cfm)))
		return error;

	// Check the status
	switch (me_sta_add_cfm.status) {
	case CO_OK: {
		struct rwnx_sta *sta = rwnx_rx_get_sta(rwnx_hw, me_sta_add_cfm.sta_idx);
		int tid;

		if (WARN_ON(sta == NULL))
			return -EFAULT;

		sta->aid = params->aid;
		sta->sta_idx = me_sta_add_cfm.sta_idx;
		sta->ch_idx = rwnx_vif->ch_index;
		sta->vif_idx = rwnx_vif->vif_index;
		sta->vlan_idx = sta->vif_idx;
		sta->stats.connected_start_time = ktime_get();
		sta->qos = (params->sta_flags_set & BIT(NL80211_STA_FLAG_WME)) != 0;
		sta->ht = RWNX_CFG80211_STA_PARA(params, ht_capa) ? 1 : 0;
		sta->vht = RWNX_CFG80211_STA_PARA(params, vht_capa) ? 1 : 0;
		sta->he = (RWNX_CFG80211_STA_PARA(params, he_capa) != NULL);
		sta->acm = 0;
		sta->listen_interval = params->listen_interval;
		chan_def = &rwnx_hw->chanctx_table[rwnx_vif->ch_index].chan_def;
		sta->center_freq = chan_def->chan->center_freq;
		sta->twt_ind.sta_idx = RWNX_INVALID_STA;

		if (params->local_pm != NL80211_MESH_POWER_UNKNOWN)
			sta->mesh_pm = params->local_pm;
		else
			sta->mesh_pm = rwnx_vif->ap.next_mesh_pm;
		rwnx_update_mesh_power_mode(rwnx_vif);

		for (tid = 0; tid < NX_NB_TXQ_PER_STA; tid++) {
			int uapsd_bit = rwnx_hwq2uapsd[rwnx_tid2hwq[tid]];
			if (params->uapsd_queues & uapsd_bit)
				sta->uapsd_tids |= 1 << tid;
			else
				sta->uapsd_tids &= ~(1 << tid);
		}
		memcpy(sta->mac_addr, mac, ETH_ALEN);
		bes_sta_txrx_rate_stat_init(rwnx_hw, sta);
		rwnx_dbgfs_register_sta(rwnx_hw, sta);

		/* Ensure that we won't process PS change or channel switch ind*/
		spin_lock_bh(&rwnx_hw->cb_lock);
		rwnx_txq_sta_init(rwnx_hw, sta, rwnx_txq_vif_get_status(rwnx_vif));
		list_add_tail(&sta->list, &rwnx_vif->ap.sta_list);
		rwnx_vif->generation++;
		sta->valid = true;
		rwnx_ps_bh_enable(rwnx_hw, sta, sta->ps.active || me_sta_add_cfm.pm_state);
		spin_unlock_bh(&rwnx_hw->cb_lock);

		/* notify userspace that a new station is added */
		sinfo = kzalloc(sizeof(struct station_info), GFP_KERNEL);
		if (sinfo) {
			sinfo->generation = rwnx_vif->generation;
			cfg80211_new_sta(dev, sta->mac_addr, sinfo, GFP_KERNEL);
			kfree(sinfo);
		}

		error = 0;

#ifdef CONFIG_RWNX_BFMER
		if (rwnx_hw->mod_params->bfmer)
			rwnx_send_bfmer_enable(rwnx_hw, sta, RWNX_CFG80211_STA_PARA(params, vht_capa));

		rwnx_mu_group_sta_init(sta, RWNX_CFG80211_STA_PARA(params, vht_capa));
#endif /* CONFIG_RWNX_BFMER */

#define PRINT_STA_FLAG(f) (params->sta_flags_set & BIT(NL80211_STA_FLAG_##f) ? "[" #f "]" : "")

		netdev_info(dev, "Add sta %d (%pM) flags=%s%s%s%s%s%s%s", sta->sta_idx, mac, PRINT_STA_FLAG(AUTHORIZED),
			    PRINT_STA_FLAG(SHORT_PREAMBLE), PRINT_STA_FLAG(WME), PRINT_STA_FLAG(MFP),
			    PRINT_STA_FLAG(AUTHENTICATED), PRINT_STA_FLAG(TDLS_PEER), PRINT_STA_FLAG(ASSOCIATED));
#undef PRINT_STA_FLAG
		break;
	}
	default:
		error = -EBUSY;
		break;
	}

	rwnx_wf_status_clear(rwnx_hw, RWNX_DEV_ADDING_STA);

	return error;
}

void rwnx_reset_sta_stats(struct rwnx_sta *sta)
{
	struct rwnx_sta_stats *stats = &sta->stats;

	/* reset rx_rate by debugfs */
	stats->rx_pkts = 0;
	stats->tx_pkts = 0;
	stats->rx_bytes = 0;
	stats->tx_bytes = 0;
	stats->tx_fail_stats.life_timeout_pkts = 0;
	stats->tx_fail_stats.over_retry_pkts = 0;
	stats->tx_fail_stats.tx_fail_pkts = 0;
	stats->tx_cont_fail_pkts = 0;
	stats->drop_tx = 0;
	stats->last_act = 0;
	memset(&stats->last_rx, 0, sizeof(stats->last_rx));
	memset(&stats->tx_rate_vector, 0, sizeof(stats->tx_rate_vector));
	memset(&stats->tx_traffic_info, 0, sizeof(stats->tx_traffic_info));
	memset(&stats->rx_traffic_info, 0, sizeof(stats->rx_traffic_info));
}

/**
 * @del_station: Remove a station
 */
static int rwnx_cfg80211_del_station(struct wiphy *wiphy, struct net_device *dev, struct station_del_parameters *params)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_sta *cur, *tmp;
	struct station_info *sinfo = NULL;
	int error = 0, found = 0;
	const u8 *mac = NULL;

	/* Check the validity of vif_index */
	if (rwnx_vif->vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_LINK, "%s, vif_index %u invalid\n", __func__, rwnx_vif->vif_index);
		return -EINVAL;
	}

	if (params)
		mac = params->mac;

	list_for_each_entry_safe(cur, tmp, &rwnx_vif->ap.sta_list, list) {
		if ((!mac) || (!memcmp(cur->mac_addr, mac, ETH_ALEN))) {
			netdev_info(dev, "Del sta %d (%pM)", cur->sta_idx, cur->mac_addr);
			/* Ensure that we won't process PS change ind */
			spin_lock_bh(&rwnx_hw->cb_lock);
			cur->ps.active = false;
			cur->valid = false;
			cur->twt_ind.sta_idx = RWNX_INVALID_STA;
			spin_unlock_bh(&rwnx_hw->cb_lock);

			if (cur->vif_idx != cur->vlan_idx) {
				struct rwnx_vif *vlan_vif;
				vlan_vif = rwnx_rx_get_vif(rwnx_hw, cur->vlan_idx);
				if (vlan_vif && vlan_vif->up) {
					if ((RWNX_VIF_TYPE(vlan_vif) == NL80211_IFTYPE_AP_VLAN) &&
					    (vlan_vif->use_4addr)) {
						vlan_vif->ap_vlan.sta_4a = NULL;
					} else {
						WARN(1, "Deleting sta belonging to VLAN other than AP_VLAN 4A");
					}
				}
			}

			rwnx_txq_sta_deinit(rwnx_hw, cur);
			error = rwnx_send_me_sta_del(rwnx_hw, cur->sta_idx, false);
			if ((error != 0) && (error != -EPIPE))
				return error;

#ifdef CONFIG_RWNX_BFMER
			// Disable Beamformer if supported
			rwnx_bfmer_report_del(rwnx_hw, cur);
			rwnx_mu_group_sta_del(rwnx_hw, cur);
#endif /* CONFIG_RWNX_BFMER */

			/* notify that a station is removed */
			rwnx_vif->generation++;
			sinfo = kzalloc(sizeof(*sinfo), GFP_KERNEL);
			if (sinfo) {
				rwnx_fill_station_info(cur, rwnx_vif, sinfo);
				cfg80211_del_sta_sinfo(dev, cur->mac_addr, sinfo, GFP_KERNEL);
				kfree(sinfo);
			}

			/* place after rwnx_fill_station_info() */
			rwnx_reset_sta_stats(cur);
			bes_tp_dbg_reset_tx_rx_info(rwnx_hw, cur);
			list_del(&cur->list);
			rwnx_dbgfs_unregister_sta(rwnx_hw, cur);
			bes_sta_txrx_rate_stat_deinit(rwnx_hw, cur);
			found++;

			break;
		}
	}

	if (!found)
		return -ENOENT;

	rwnx_update_mesh_power_mode(rwnx_vif);

	return 0;
}

/**
 * @change_station: Modify a given station. Note that flags changes are not much
 *	validated in cfg80211, in particular the auth/assoc/authorized flags
 *	might come to the driver in invalid combinations -- make sure to check
 *	them, also against the existing state! Drivers must call
 *	cfg80211_check_station_change() to validate the information.
 */
static int rwnx_cfg80211_change_station(struct wiphy *wiphy, struct net_device *dev, const u8 *mac,
					struct station_parameters *params)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *vif = netdev_priv(dev);
	struct rwnx_sta *sta;
	struct mm_hw_filter hw_filter;

	/* Check the validity of vif_index */
	if (vif->vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_LINK, "%s, vif_index %u invalid\n", __func__, vif->vif_index);
		return -EINVAL;
	}

	sta = rwnx_get_sta(rwnx_hw, mac);
	if (!sta) {
		/* Add the TDLS station */
		if (params->sta_flags_set & BIT(NL80211_STA_FLAG_TDLS_PEER)) {
			struct rwnx_vif *rwnx_vif = netdev_priv(dev);
			struct me_sta_add_cfm me_sta_add_cfm;
			int error = 0;

			/* Indicate we are in a STA addition process - This will allow handling
			 * potential PS mode change indications correctly
			 */
			rwnx_wf_status_set(rwnx_hw, RWNX_DEV_ADDING_STA);

			/* Forward the information to the LMAC */
			if ((error = rwnx_send_me_sta_add(rwnx_hw, params, mac, rwnx_vif->vif_index, &me_sta_add_cfm)))
				return error;

			// Check the status
			switch (me_sta_add_cfm.status) {
			case CO_OK: {
				int tid;
				sta = rwnx_rx_get_sta(rwnx_hw, me_sta_add_cfm.sta_idx);

				if (WARN_ON(sta == NULL))
					return -EFAULT;

				sta->aid = params->aid;
				sta->sta_idx = me_sta_add_cfm.sta_idx;
				sta->ch_idx = rwnx_vif->ch_index;
				sta->vif_idx = rwnx_vif->vif_index;
				sta->vlan_idx = sta->vif_idx;
				sta->qos = (params->sta_flags_set & BIT(NL80211_STA_FLAG_WME)) != 0;
				sta->ht = RWNX_CFG80211_STA_PARA(params, ht_capa) ? 1 : 0;
				sta->vht = RWNX_CFG80211_STA_PARA(params, vht_capa) ? 1 : 0;
				sta->he = RWNX_CFG80211_STA_PARA(params, he_capa) ? 1 : 0;
				sta->acm = 0;
				sta->twt_ind.sta_idx = RWNX_INVALID_STA;
				for (tid = 0; tid < NX_NB_TXQ_PER_STA; tid++) {
					int uapsd_bit = rwnx_hwq2uapsd[rwnx_tid2hwq[tid]];
					if (params->uapsd_queues & uapsd_bit)
						sta->uapsd_tids |= 1 << tid;
					else
						sta->uapsd_tids &= ~(1 << tid);
				}
				memcpy(sta->mac_addr, mac, ETH_ALEN);
				bes_sta_txrx_rate_stat_init(rwnx_hw, sta);
				rwnx_dbgfs_register_sta(rwnx_hw, sta);

				/* Ensure that we won't process PS change or channel switch ind*/
				spin_lock_bh(&rwnx_hw->cb_lock);
				rwnx_txq_sta_init(rwnx_hw, sta, rwnx_txq_vif_get_status(rwnx_vif));
				if (rwnx_vif->tdls_status == TDLS_SETUP_RSP_TX) {
					rwnx_vif->tdls_status = TDLS_LINK_ACTIVE;
					sta->tdls.initiator = true;
					sta->tdls.active = true;
				}
				/* Set TDLS channel switch capability */
				if ((params->ext_capab[3] & WLAN_EXT_CAPA4_TDLS_CHAN_SWITCH) &&
				    !rwnx_vif->tdls_chsw_prohibited)
					sta->tdls.chsw_allowed = true;
				rwnx_vif->sta.tdls_sta = sta;
				sta->valid = true;
				spin_unlock_bh(&rwnx_hw->cb_lock);
#ifdef CONFIG_RWNX_BFMER
				if (rwnx_hw->mod_params->bfmer)
					rwnx_send_bfmer_enable(rwnx_hw, sta, RWNX_CFG80211_STA_PARA(params, vht_capa));

				rwnx_mu_group_sta_init(sta, NULL);
#endif /* CONFIG_RWNX_BFMER */

#define PRINT_STA_FLAG(f) (params->sta_flags_set & BIT(NL80211_STA_FLAG_##f) ? "[" #f "]" : "")

				netdev_info(dev, "Add %s TDLS sta %d (%pM) flags=%s%s%s%s%s%s%s",
					    sta->tdls.initiator ? "initiator" : "responder", sta->sta_idx, mac,
					    PRINT_STA_FLAG(AUTHORIZED), PRINT_STA_FLAG(SHORT_PREAMBLE),
					    PRINT_STA_FLAG(WME), PRINT_STA_FLAG(MFP), PRINT_STA_FLAG(AUTHENTICATED),
					    PRINT_STA_FLAG(TDLS_PEER), PRINT_STA_FLAG(ASSOCIATED));
#undef PRINT_STA_FLAG

				break;
			}
			default:
				error = -EBUSY;
				break;
			}

			rwnx_wf_status_clear(rwnx_hw, RWNX_DEV_ADDING_STA);
		} else {
			return -EINVAL;
		}
	}

	if (params->sta_flags_mask & BIT(NL80211_STA_FLAG_AUTHORIZED)) {
		if (sta) {
			rwnx_send_me_set_control_port_req(
				rwnx_hw, (params->sta_flags_set & BIT(NL80211_STA_FLAG_AUTHORIZED)) != 0, sta->sta_idx);
		}

		/* Update the conn state of STA */
		bes_conn_state_process(vif, sta, STA_CONN_ACTION_PORT_AUTH);
	}

	if (sta->he && (params->sta_flags_mask & BIT(NL80211_STA_FLAG_AUTHORIZED)) &&
	    rwnx_is_obss_narrow_bw_ru_status(rwnx_hw, sta->center_freq, sta->width)) {
		bes_filter_get_hw_filter(rwnx_hw, &hw_filter);
		hw_filter.umac_filter &= (~NXMAC_ACCEPT_ALL_BEACON_BIT);
		bes_filter_set_hw_filter(rwnx_hw, &hw_filter);
		spin_lock(&rwnx_hw->obss_narrow.lock);
		rwnx_hw->obss_narrow.acpt_all_beacon = true;
		spin_unlock(&rwnx_hw->obss_narrow.lock);
	}

	if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_MESH_POINT) {
		if (params->sta_modify_mask & STATION_PARAM_APPLY_PLINK_STATE) {
			if (params->plink_state < NUM_NL80211_PLINK_STATES) {
				rwnx_send_mesh_peer_update_ntf(rwnx_hw, vif, sta->sta_idx, params->plink_state);
			}
		}

		if (params->local_pm != NL80211_MESH_POWER_UNKNOWN) {
			sta->mesh_pm = params->local_pm;
			rwnx_update_mesh_power_mode(vif);
		}
	}

	if (params->vlan) {
		uint8_t vlan_idx;

		vif = netdev_priv(params->vlan);
		vlan_idx = vif->vif_index;

		if (sta->vlan_idx != vlan_idx) {
			struct rwnx_vif *old_vif;
			old_vif = rwnx_rx_get_vif(rwnx_hw, sta->vlan_idx);
			if (WARN_ON(old_vif == NULL))
				return -EFAULT;

			rwnx_txq_sta_switch_vif(sta, old_vif, vif);
			sta->vlan_idx = vlan_idx;

			if ((RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_AP_VLAN) && (vif->use_4addr)) {
				WARN((vif->ap_vlan.sta_4a), "4A AP_VLAN interface with more than one sta");
				vif->ap_vlan.sta_4a = sta;
			}

			if ((RWNX_VIF_TYPE(old_vif) == NL80211_IFTYPE_AP_VLAN) && (old_vif->use_4addr)) {
				old_vif->ap_vlan.sta_4a = NULL;
			}
		}
	}

	return 0;
}

/**
 * @start_ap: Start acting in AP mode defined by the parameters.
 */
static int rwnx_cfg80211_start_ap(struct wiphy *wiphy, struct net_device *dev, struct cfg80211_ap_settings *settings)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct apm_start_cfm apm_start_cfm;
	struct rwnx_sta *sta;
	int error = 0;

	bes_dbg(BES_DBG_AP, RWNX_FN_ENTRY_STR);

	/* check if the ap meets coex requirements */
	if(!bes_coex_arb_check_ap_setup(rwnx_hw, &settings->chandef))
		return -ENOTSUPP;

	/* stop scan in advance */
	if (test_bit(RWNX_DEV_SCANNING, &rwnx_hw->flags)) {
		rwnx_cfg80211_abort_scan(wiphy, &rwnx_vif->wdev);
	}

	/* wait other mutex operation down */
	if (down_timeout(&rwnx_hw->phy_op_sema,
			 msecs_to_jiffies(PHY_OP_WAIT_TIMEOUT_IN_MS))) {
		WARN(1, "%s wait mutex operation timeout\n", __func__);
		return -EFAULT;
	}
	rwnx_wf_status_set(rwnx_hw, RWNX_DEV_STARTING_AP);

	/* trigger wifi step3 calibration */
	error = bes_dbac_cali_start_ap(rwnx_vif, settings);
	if (error) {
		bes_err(BES_DBG_AP, "AP mode calibration failed  err:%d\n", error);
	}

	if ((error = rwnx_send_apm_start_req(rwnx_hw, rwnx_vif, settings, &apm_start_cfm)))
		goto end;
	bes_dbg(BES_DBG_AP, "%s, status=%d\n", __func__, apm_start_cfm.status);
	// Check the status
	switch (apm_start_cfm.status) {
	case CO_OK: {
		u8 txq_status = 0;
		rwnx_vif->ap.bcmc_index = apm_start_cfm.bcmc_idx;
		rwnx_vif->ap.flags = 0;
		rwnx_vif->ap.bcn_interval = settings->beacon_interval;
		rwnx_vif->ap.csa = NULL;
		sta = rwnx_rx_get_sta(rwnx_hw, apm_start_cfm.bcmc_idx);

		if (WARN_ON(sta == NULL)) {
			error = -EFAULT;
			goto end;
		}

		sta->valid = true;
		sta->aid = 0;
		sta->sta_idx = apm_start_cfm.bcmc_idx;
		sta->ch_idx = apm_start_cfm.ch_idx;
		sta->vif_idx = rwnx_vif->vif_index;
		sta->qos = false;
		sta->acm = 0;
		sta->ps.active = false;
		sta->listen_interval = 5;
		rwnx_mu_group_sta_init(sta, NULL);
		spin_lock_bh(&rwnx_hw->cb_lock);
		rwnx_chanctx_link(rwnx_vif, apm_start_cfm.ch_idx, &settings->chandef);
		if (rwnx_hw->cur_chanctx != apm_start_cfm.ch_idx) {
			txq_status = (1 << RWNX_TXQ_STOP_CHAN);
		}
		rwnx_txq_vif_init(rwnx_hw, rwnx_vif, txq_status);
		spin_unlock_bh(&rwnx_hw->cb_lock);
		netif_tx_start_all_queues(dev);
		netif_carrier_on(dev);
		error = 0;
		/* If the AP channel is already the active, we probably skip radar
		   activation on MM_CHANNEL_SWITCH_IND (unless another vif use this
		   ctxt). In anycase retest if radar detection must be activated
		 */
		if (txq_status == 0) {
			rwnx_radar_detection_enable_on_cur_channel(rwnx_hw);
		}
		/* set activity management parameters */
		bes_set_vif_ext_param(rwnx_vif, true);
		break;
	}
	case CO_BUSY:
		error = -EINPROGRESS;
		break;
	case CO_OP_IN_PROGRESS:
		error = -EALREADY;
		break;
	default:
		error = -EIO;
		break;
	}

end:
	rwnx_wf_status_clear(rwnx_hw, RWNX_DEV_STARTING_AP);
	up(&rwnx_hw->phy_op_sema);

	bes_dbg(BES_DBG_AP, "%s, ret=%d,\n", __func__, error);
	if (error) {
		bes_dbac_cali_check(rwnx_hw);
		netdev_info(dev, "Failed to start AP (%d)", error);
	} else {
		netdev_info(dev, "AP started: ch_idx=%d, bcmc_idx=%d vif_idx:%d",
					rwnx_vif->ch_index, rwnx_vif->ap.bcmc_index, rwnx_vif->vif_index);
	}

	return error;
}

/**
 * @change_beacon: Change the beacon parameters for an access point mode
 *	interface. This should reject the call when AP mode wasn't started.
 */
static int rwnx_cfg80211_change_beacon(struct wiphy *wiphy, struct net_device *dev, struct cfg80211_beacon_data *info)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *vif = netdev_priv(dev);
	struct rwnx_bcn *bcn = &vif->ap.bcn;
	struct rwnx_ipc_buf buf;
	u8 *bcn_buf;
	int error = 0;

	bes_dbg(BES_DBG_AP, RWNX_FN_ENTRY_STR);

	// Build the beacon
	bcn_buf = rwnx_build_bcn(bcn, info);
	if (!bcn_buf)
		return -ENOMEM;

	// Sync buffer for FW
	if ((error = rwnx_ipc_buf_a2e_init(rwnx_hw, &buf, bcn_buf, bcn->len))) {
		netdev_err(dev, "Failed to allocate IPC buf for new beacon\n");
		kfree(bcn_buf);
		return error;
	}

	// Forward the information to the LMAC
	error = rwnx_send_bcn_change(rwnx_hw, vif->vif_index, &buf, bcn->head_len, bcn->tim_len, NULL);
	rwnx_ipc_buf_dealloc(rwnx_hw, &buf);
	bes_dbg(BES_DBG_AP, "%s, ret=%d\n", __func__, error);
	return error;
}

/**
 * * @stop_ap: Stop being an AP, including stopping beaconing.
 */
#ifdef ADPT_GKI_KERNEL
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 81))
static int rwnx_cfg80211_stop_ap(struct wiphy *wiphy, struct net_device *dev, unsigned int link_id)
#else
static int rwnx_cfg80211_stop_ap(struct wiphy *wiphy, struct net_device *dev)
#endif
#else /* ADPT_GKI_KERNEL */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 19, 2))
static int rwnx_cfg80211_stop_ap(struct wiphy *wiphy, struct net_device *dev, unsigned int link_id)
#else
static int rwnx_cfg80211_stop_ap(struct wiphy *wiphy, struct net_device *dev)
#endif
#endif /* ADPT_GKI_KERNEL */
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);

	if (test_bit(RWNX_DEV_SCANNING, &rwnx_hw->flags)) {
		rwnx_cfg80211_abort_scan(wiphy, &rwnx_vif->wdev);
	}

	/* wait other mutex operation down */
	if (down_timeout(&rwnx_hw->phy_op_sema,
			 msecs_to_jiffies(PHY_OP_WAIT_TIMEOUT_IN_MS))) {
		WARN(1, "%s wait mutex operation timeout\n", __func__);
		return -EFAULT;
	}
	rwnx_wf_status_set(rwnx_hw, RWNX_DEV_STOPPING_AP);

	rwnx_radar_cancel_cac(&rwnx_hw->radar);
	/*
	 * [BES_MOD_START][Bug]: Fix potential stuck issue when stop AP.
	 *
	 * If AP is abruptly stopped whereas there is ongoing traffic with client,
	 * ME_STA_DEL_REQ may be stucked in waiting for tx data flushed but
	 * never get confirmed since APM_STOP_REQ is sent before and MAC HW
	 * is already set to IDLE.
	 * So clear stations before apm stop.
	 */
	/* delete any remaining STA*/
	while (!list_empty(&rwnx_vif->ap.sta_list)) {
		rwnx_cfg80211_del_station(wiphy, dev, NULL);
	}
	/* [BES_MOD_END][Bug]: Fix potential stuck issue when stop AP. */
	rwnx_send_apm_stop_req(rwnx_hw, rwnx_vif);

	spin_lock_bh(&rwnx_hw->cb_lock);
	rwnx_chanctx_unlink(rwnx_vif);
	spin_unlock_bh(&rwnx_hw->cb_lock);

	memset(&rwnx_vif->vif_chandef, 0, sizeof(struct cfg80211_chan_def));

	rwnx_txq_vif_deinit(rwnx_hw, rwnx_vif);
	rwnx_del_bcn(&rwnx_vif->ap.bcn);
	rwnx_del_csa(rwnx_vif);

	netif_tx_stop_all_queues(dev);
	netif_carrier_off(dev);

	bes_dbac_cali_check(rwnx_hw);

	/* set activity management parameters */
	bes_set_vif_ext_param(rwnx_vif, false);
	rwnx_wf_status_clear(rwnx_hw, RWNX_DEV_STOPPING_AP);
	up(&rwnx_hw->phy_op_sema);
	netdev_info(dev, "AP Stopped");
	return 0;
}

/**
 * @set_monitor_channel: Set the monitor mode channel for the device. If other
 *	interfaces are active this callback should reject the configuration.
 *	If no interfaces are active or the device is down, the channel should
 *	be stored for when a monitor interface becomes active.
 *
 * Also called internaly with chandef set to NULL simply to retrieve the channel
 * configured at firmware level.
 */
static int rwnx_cfg80211_set_monitor_channel(struct wiphy *wiphy, struct cfg80211_chan_def *chandef)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif;
	struct me_config_monitor_cfm cfm;
	struct ieee80211_channel *chan;
	bes_dbg(BES_DBG_CHAN, RWNX_FN_ENTRY_STR);

	if (rwnx_hw->monitor_vif == RWNX_INVALID_VIF)
		return -EINVAL;

	rwnx_vif = rwnx_rx_get_vif(rwnx_hw, rwnx_hw->monitor_vif);

	if (!rwnx_vif)
		return -EINVAL;

	// Do nothing if monitor interface is already configured with the requested channel
	if (rwnx_chanctx_valid(rwnx_hw, rwnx_vif->ch_index)) {
		struct rwnx_chanctx *ctxt;
		ctxt = &rwnx_vif->rwnx_hw->chanctx_table[rwnx_vif->ch_index];
		if (chandef && cfg80211_chandef_identical(&ctxt->chan_def, chandef))
			return 0;
	}

	// Always send command to firmware. It allows to retrieve channel context index
	// and its configuration.
	if (rwnx_send_config_monitor_req(rwnx_hw, chandef, &cfm))
		return -EIO;

	// Always re-set channel context info
	rwnx_chanctx_unlink(rwnx_vif);

	// If there is also a STA interface not yet connected then monitor interface
	// will only have a channel context after the connection of the STA interface.
	if (cfm.chan_index != RWNX_CH_NOT_SET) {
		struct cfg80211_chan_def mon_chandef;

		if (rwnx_hw->vif_started > 1) {
			// In this case we just want to update the channel context index not
			// the channel configuration
			rwnx_chanctx_link(rwnx_vif, cfm.chan_index, NULL);
			return -EBUSY;
		}

		memset(&mon_chandef, 0, sizeof(mon_chandef));
		chan = ieee80211_get_channel(wiphy, cfm.chan.prim20_freq);
		if (!chan)
			return -EINVAL;
		mon_chandef.chan = chan;
		mon_chandef.center_freq1 = cfm.chan.center1_freq;
		mon_chandef.center_freq2 = cfm.chan.center2_freq;
		mon_chandef.width = chnl2bw[cfm.chan.type];
		rwnx_chanctx_link(rwnx_vif, cfm.chan_index, &mon_chandef);
	}

	return 0;
}

/**
 * @probe_client: probe an associated client, must return a cookie that it
 *	later passes to cfg80211_probe_status().
 */
int rwnx_cfg80211_probe_client(struct wiphy *wiphy, struct net_device *dev, const u8 *peer, u64 *cookie)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *vif = netdev_priv(dev);
	struct rwnx_sta *sta = NULL;
	struct apm_probe_client_cfm cfm;
	int ret;

	if ((RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_AP) && (RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_AP_VLAN) &&
	    (RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_P2P_GO) && (RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_MESH_POINT))
		return -EINVAL;

	list_for_each_entry(sta, &vif->ap.sta_list, list) {
		if (sta->valid && ether_addr_equal(sta->mac_addr, peer))
			break;
	}

	if (!sta)
		return -EINVAL;

	ret = rwnx_send_apm_probe_req(rwnx_hw, vif, sta, &cfm);

	if (ret) {
		netdev_err(dev, "%s set channel fail\n", __func__);
		return -EINVAL;
	}

	if (cfm.status != CO_OK)
		return -EINVAL;

	*cookie = (u64)cfm.probe_id;
	return 0;
}

/**
 * @set_wiphy_params: Notify that wiphy parameters have changed;
 *	@changed bitfield (see &enum wiphy_params_flags) describes which values
 *	have changed. The actual parameter values are available in
 *	struct wiphy. If returning an error, no value should be changed.
 */
static int rwnx_cfg80211_set_wiphy_params(struct wiphy *wiphy, u32 changed)
{
	return 0;
}

/**
 * @set_tx_power: set the transmit power according to the parameters,
 *	the power passed is in mBm, to get dBm use MBM_TO_DBM(). The
 *	wdev may be %NULL if power was set for the wiphy, and will
 *	always be %NULL unless the driver supports per-vif TX power
 *	(as advertised by the nl80211 feature flag.)
 */
static int rwnx_cfg80211_set_tx_power(struct wiphy *wiphy, struct wireless_dev *wdev,
				      enum nl80211_tx_power_setting type, int mbm)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *vif;
	s8 pwr;
	int res = 0;

	if (type == NL80211_TX_POWER_AUTOMATIC) {
		pwr = 0x7f;
	} else {
		pwr = MBM_TO_DBM(mbm);
	}

	if (wdev) {
		vif = container_of(wdev, struct rwnx_vif, wdev);
		/* Check the validity of vif_index */
		if (vif->vif_index >= NX_ITF_MAX) {
			bes_err(BES_DBG_LINK, "%s, vif_index %u invalid\n", __func__, vif->vif_index);
			return -EINVAL;
		}
		res = rwnx_send_set_power(rwnx_hw, vif->vif_index, pwr, NULL);
	} else {
		list_for_each_entry(vif, &rwnx_hw->vifs, list) {
			res = rwnx_send_set_power(rwnx_hw, vif->vif_index, pwr, NULL);
			if (res)
				break;
		}
	}

	return res;
}

/**
 * @set_power_mgmt: set the power save to one of those two modes:
 *  Power-save off
 *  Power-save on - Dynamic mode
 */
static int rwnx_cfg80211_set_power_mgmt(struct wiphy *wiphy, struct net_device *dev, bool enabled, int timeout)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Check the validity of vif_index */
	if (rwnx_vif->vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_LINK, "%s, vif_index %u invalid\n", __func__, rwnx_vif->vif_index);
		return -EINVAL;
	}

	if (timeout >= 0)
		netdev_info(dev, "Ignore timeout value %d", timeout);

	if (!netif_running(rwnx_vif->ndev))
		return -ENOTSUPP;

	/* Ignore power save commands from user if user_ps_mgmt_disable is set */
	if (rwnx_hw->mod_params->user_ps_mgmt_disable) {
		bes_info(BES_DBG_PWR, "%s, vif:%u discard ps mgmt cmd\n", __func__, rwnx_vif->vif_index);
		return 0;
	}

	if (!(rwnx_hw->version_cfm.features & BIT(MM_FEAT_PS_BIT)))
		enabled = false;

	if (enabled) {
		/* Switch to Dynamic Power Save */
		bes_pwr_clear_busy_event(rwnx_hw, rwnx_vif->vif_index, VIF_CONST_EVENT_PSM);
	} else {
		/* Exit Power Save */
		bes_pwr_set_busy_event(rwnx_hw, rwnx_vif->vif_index, VIF_CONST_EVENT_PSM);
	}

	return 0;
}

/**
 * @set_txq_params: Set TX queue parameters
 */
static int rwnx_cfg80211_set_txq_params(struct wiphy *wiphy, struct net_device *dev,
					struct ieee80211_txq_params *params)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	u8 hw_queue, aifs, cwmin, cwmax;
	u32 param;

	bes_dbg(BES_DBG_MSG_TX, RWNX_FN_ENTRY_STR);

	/* Check the validity of vif_index */
	if (rwnx_vif->vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_LINK, "%s, vif_index %u invalid\n", __func__, rwnx_vif->vif_index);
		return -EINVAL;
	}

	if (rwnx_hw->conf_file.edca_use_config)
		return 0;

	hw_queue = rwnx_ac2hwq[0][params->ac];

	aifs = params->aifs;
	cwmin = fls(params->cwmin);
	cwmax = fls(params->cwmax);

	/* Store queue information in general structure */
	param = (u32)(aifs << 0);
	param |= (u32)(cwmin << 4);
	param |= (u32)(cwmax << 8);
	param |= (u32)(params->txop) << 12;

	/* Send the MM_SET_EDCA_REQ message to the FW */
	return rwnx_send_set_edca(rwnx_hw, hw_queue, param, false, rwnx_vif->vif_index);
}

/**
 * @remain_on_channel: Request the driver to remain awake on the specified
 *	channel for the specified duration to complete an off-channel
 *	operation (e.g., public action frame exchange). When the driver is
 *	ready on the requested channel, it must indicate this with an event
 *	notification by calling cfg80211_ready_on_channel().
 */
int rwnx_cfg80211_remain_on_channel(struct wiphy *wiphy, struct wireless_dev *wdev,
					   struct ieee80211_channel *chan, unsigned int duration, u64 *cookie)
{
	struct mm_remain_on_channel_cfm roc_cfm;
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(wdev->netdev);
	struct rwnx_roc *roc;
	u8 try_cnt = 0;
	bool roc_idle = false;
	int error;

	bes_dbg(BES_DBG_ROC, RWNX_FN_ENTRY_STR);

	/* Check the validity of vif_index */
	if (rwnx_vif->vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_LINK, "%s, vif_index %u invalid\n", __func__, rwnx_vif->vif_index);
		return -EINVAL;
	}

	/* For debug purpose (use ftrace kernel option) */
	trace_roc(rwnx_vif->vif_index, chan->center_freq, duration);
	bes_dbg(BES_DBG_ROC, "%s, vif_idx=%d freq=%u, dur=%d\n",
		__func__, rwnx_vif->vif_index, chan->center_freq, duration);

	/* Allocate a temporary RoC element */
	roc = kmalloc(sizeof(struct rwnx_roc), GFP_KERNEL);
	if (!roc)
		return -ENOMEM;

	/* Forward the information to the FMAC */
	if (wait_event_interruptible_timeout(rwnx_hw->roc_wait_wq,
					     ({
					       spin_lock_bh(&rwnx_hw->roc_lock);
					       roc_idle = ((rwnx_hw->roc == NULL) &&
					        !test_bit(RWNX_DEV_FW_LEVEL_ROC, &rwnx_hw->flags));
					       if (roc_idle)
						   rwnx_hw->roc = roc;
					       spin_unlock_bh(&rwnx_hw->roc_lock);
					       roc_idle;
					     }),
					    msecs_to_jiffies(500)) <= 0) {
		kfree(roc);
		bes_err(BES_DBG_ROC, "%s wait old roc exp timeout\n", __func__);
		return -EAGAIN;
	}

	/* Initialize the RoC information element */
	roc->vif = rwnx_vif;
	roc->chan = chan;
	roc->duration = duration;
	roc->internal = false;
	roc->on_chan = false;
	roc->tx_cnt = 0;
	memset(roc->tx_cookie, 0, sizeof(roc->tx_cookie));

	/* Initialize the OFFCHAN TX queue to allow off-channel transmissions */
	rwnx_txq_offchan_init(rwnx_vif);

	/* Forward the information to the FMAC */
	try_cnt = 0;
	do {
		memset(&roc_cfm, 0 , sizeof(struct mm_remain_on_channel_cfm));

		error = rwnx_send_roc(rwnx_hw, rwnx_vif, chan, duration, &roc_cfm);
		if (error) {
			bes_err(BES_DBG_ROC, "%s send roc cmd fail\n", __func__);
		} else {
			if (roc_cfm.status) {
				bes_err(BES_DBG_ROC, "%s roc fail,cfm_status:%d retry:%d\n",
					__func__, roc_cfm.status, try_cnt);
				/* The reason for the CFM status failure here is that the firmware level ROC
				* may conflict with the ROC here, but the driver is currently unaware of this status.
				* Waiting for MM_CHANNEL_SWITCH_IND arrival briefly. */
				msleep(20);

				/* Wait fw level roc end */
				rwnx_wf_status_test_and_wait(rwnx_hw, RWNX_DEV_FW_LEVEL_ROC, false, HZ);
			}
		}
		try_cnt++;

	} while (try_cnt < 3 && !error && roc_cfm.status);

	if (error || roc_cfm.status) {
		spin_lock_bh(&rwnx_hw->roc_lock);
		rwnx_hw->roc = NULL;
		spin_unlock_bh(&rwnx_hw->roc_lock);

		wake_up(&rwnx_hw->roc_wait_wq);
		kfree(roc);
		rwnx_txq_offchan_deinit(rwnx_vif);
	} else if (cookie)
		*cookie = (unsigned long)(roc);

	return (roc_cfm.status ? roc_cfm.status : error);
}

/**
 * @cancel_remain_on_channel: Cancel an on-going remain-on-channel operation.
 *	This allows the operation to be terminated prior to timeout based on
 *	the duration value.
 */
static int rwnx_cfg80211_cancel_remain_on_channel(struct wiphy *wiphy, struct wireless_dev *wdev, u64 cookie)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(wdev->netdev);

	bes_dbg(BES_DBG_ROC, RWNX_FN_ENTRY_STR);

	/* Check the validity of vif_index */
	if (rwnx_vif->vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_LINK, "%s, vif_index %u invalid\n", __func__, rwnx_vif->vif_index);
		return -EINVAL;
	}

	trace_cancel_roc(rwnx_vif->vif_index);

	if (!rwnx_hw->roc)
		return 0;

	if (cookie != (unsigned long)rwnx_hw->roc)
		return -EINVAL;

	/* Forward the information to the FMAC */
	return rwnx_send_cancel_roc(rwnx_hw, rwnx_vif);
}

/**
 * @dump_survey: get site survey information.
 */
static int rwnx_cfg80211_dump_survey(struct wiphy *wiphy, struct net_device *netdev, int idx, struct survey_info *info)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct ieee80211_supported_band *sband;
	struct rwnx_survey_info *rwnx_survey;

	bes_dbg(BES_DBG_CHAN, RWNX_FN_ENTRY_STR);

	if (idx >= ARRAY_SIZE(rwnx_hw->survey))
		return -ENOENT;

	rwnx_survey = &rwnx_hw->survey[idx];

	// Check if provided index matches with a supported 2.4GHz channel
	sband = wiphy->bands[NL80211_BAND_2GHZ];
	if (sband && idx >= sband->n_channels) {
		idx -= sband->n_channels;
		sband = NULL;
	}

	if (!sband) {
		// Check if provided index matches with a supported 5GHz channel
		sband = wiphy->bands[NL80211_BAND_5GHZ];

		if (!sband || idx >= sband->n_channels)
			return -ENOENT;
	}

	// Fill the survey
	info->channel = &sband->channels[idx];
	info->filled = rwnx_survey->filled;

	if (rwnx_survey->filled != 0) {
		info->time = (u64)rwnx_survey->chan_time_ms;
		info->time_busy = (u64)rwnx_survey->chan_time_busy_ms;
		info->noise = rwnx_survey->noise_dbm;

		// TODO: clear survey after some time ?
	}

	return 0;
}

/**
 * @get_channel: Get the current operating channel for the virtual interface.
 *	For monitor interfaces, it should return %NULL unless there's a single
 *	current monitoring channel.
 */
static int rwnx_cfg80211_get_channel(struct wiphy *wiphy, struct wireless_dev *wdev,
#ifdef ADPT_GKI_KERNEL
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 81))
				     unsigned int link_id,
#endif
#else /* ADPT_GKI_KERNEL */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 19, 2))
				     unsigned int link_id,
#endif
#endif /* ADPT_GKI_KERNEL */
				     struct cfg80211_chan_def *chandef)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = container_of(wdev, struct rwnx_vif, wdev);

	bes_dbg(BES_DBG_CHAN, RWNX_FN_ENTRY_STR);

	/* Check the validity of vif_index */
	if (rwnx_vif->vif_index >= NX_ITF_MAX) {
		return -EINVAL;
	}

	if (!rwnx_vif->up) {
		return -ENODATA;
	}

	if (rwnx_vif->vif_index == rwnx_hw->monitor_vif) {
		// retrieve channel from firmware
		rwnx_cfg80211_set_monitor_channel(wiphy, NULL);
	}

	// Check if channel context is valid
	if (!cfg80211_chandef_valid(&rwnx_vif->vif_chandef)) {
		return -ENODATA;
	}

	*chandef = rwnx_vif->vif_chandef;

	return 0;
}

/**
 * @mgmt_tx: Transmit a management frame.
 */
static int rwnx_cfg80211_mgmt_tx(struct wiphy *wiphy, struct wireless_dev *wdev, struct cfg80211_mgmt_tx_params *params,
				 u64 *cookie)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(wdev->netdev);
	struct rwnx_sta *rwnx_sta;
	struct ieee80211_mgmt *mgmt = (void *)params->buf;
	bool ap = false;
	bool offchan = false;
	int res;
	unsigned int duration = 30;
	int error = 0;

	bes_dbg(BES_DBG_TX, "%s, vif_type=%d,vif_index=%d,drv_vif_index=%d\n", __func__, RWNX_VIF_TYPE(rwnx_vif),
		 rwnx_vif->vif_index, rwnx_vif->drv_vif_index);

	/* Check the validity of vif_index */
	if (rwnx_vif->vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_LINK, "%s, vif_index %u invalid\n", __func__, rwnx_vif->vif_index);
		return -EINVAL;
	}

	switch (RWNX_VIF_TYPE(rwnx_vif)) {
	case NL80211_IFTYPE_AP_VLAN:
		rwnx_vif = rwnx_vif->ap_vlan.master;
	case NL80211_IFTYPE_AP:
	case NL80211_IFTYPE_P2P_GO:
	case NL80211_IFTYPE_MESH_POINT:
		ap = true;
		break;
	case NL80211_IFTYPE_STATION:
	case NL80211_IFTYPE_P2P_CLIENT:
	default:
		break;
	}

	// Get STA on which management frame has to be sent
	rwnx_sta = rwnx_retrieve_sta(rwnx_hw, rwnx_vif, mgmt->da, mgmt->frame_control, ap);
	bes_dbg(BES_DBG_TX, "%s, params offchan=%d,chan=%p, vif->chidx=%d, rwnx_hw->roc=%p\n", __func__, params->offchan,
		 params->chan, rwnx_vif->ch_index, rwnx_hw->roc);
	if (params->offchan) {
		if (!params->chan)
			return -EINVAL;

		offchan = true;
		if (rwnx_chanctx_valid(rwnx_hw, rwnx_vif->ch_index)) {
			struct rwnx_chanctx *ctxt = &rwnx_hw->chanctx_table[rwnx_vif->ch_index];
			if (ctxt->chan_def.chan->center_freq == params->chan->center_freq)
				offchan = false;
		}
	}

	trace_mgmt_tx((offchan) ? params->chan->center_freq : 0, rwnx_vif->vif_index,
		      (rwnx_sta) ? rwnx_sta->sta_idx : 0xFF, mgmt);

	if (offchan) {
		// Offchannel transmission, need to start a RoC
		spin_lock_bh(&rwnx_hw->roc_lock);
		if (rwnx_hw->roc) {
			// Test if current RoC can be re-used
			if ((rwnx_hw->roc->vif != rwnx_vif) ||
				   (rwnx_hw->roc->chan->center_freq != params->chan->center_freq)) {
				spin_unlock_bh(&rwnx_hw->roc_lock);
				return -EINVAL;
			}
			spin_unlock_bh(&rwnx_hw->roc_lock);
			// TODO: inform FW to increase RoC duration
		} else {
			spin_unlock_bh(&rwnx_hw->roc_lock);

			/* Start a new ROC procedure */
			if (params->wait)
				duration = params->wait;

			error = rwnx_cfg80211_remain_on_channel(wiphy, wdev, params->chan, duration, NULL);
			if (error)
				return error;

			// internal RoC, no need to inform user space about it
			spin_lock_bh(&rwnx_hw->roc_lock);
			if (!rwnx_hw->roc) {
				spin_unlock_bh(&rwnx_hw->roc_lock);
				WARN(1, "%s roc fail\n", __func__);
				return -EFAULT;
			}
			rwnx_hw->roc->internal = true;
			spin_unlock_bh(&rwnx_hw->roc_lock);
		}
	}

	res = rwnx_start_mgmt_xmit(rwnx_vif, rwnx_sta, params, offchan, cookie);
	if (offchan) {
		spin_lock_bh(&rwnx_hw->roc_lock);
		if (!rwnx_hw->roc) {
			spin_unlock_bh(&rwnx_hw->roc_lock);
			bes_warn(BES_DBG_TX, "%s RoC expired\n", __func__);
			return res;
		}

		if (rwnx_hw->roc->tx_cnt < NX_ROC_TX) {
			rwnx_hw->roc->tx_cookie[rwnx_hw->roc->tx_cnt] = *cookie;
			rwnx_hw->roc->tx_cnt++;
			spin_unlock_bh(&rwnx_hw->roc_lock);
		} else {
			int roc_tx_cnt;
			roc_tx_cnt = ++rwnx_hw->roc->tx_cnt;
			spin_unlock_bh(&rwnx_hw->roc_lock);
			wiphy_warn(wiphy, "%d frames sent within the same Roc (> NX_ROC_TX)",
				   roc_tx_cnt);
		}
	}
	bes_dbg(BES_DBG_TX, "%s, ret=%d\n", __func__, res);
	return res;
}

int rwnx_cfg80211_mgmt_tx_cancel_wait(struct wiphy *wiphy, struct wireless_dev *wdev, u64 cookie)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	int i, nb_tx_cookie = 0;
	u64 roc_cookie = 0;

	spin_lock_bh(&rwnx_hw->roc_lock);
	if (!rwnx_hw->roc || !rwnx_hw->roc->tx_cnt) {
		spin_unlock_bh(&rwnx_hw->roc_lock);
		return 0;
	}

	for (i = 0; i < NX_ROC_TX; i++) {
		if (!rwnx_hw->roc->tx_cookie[i])
			continue;

		nb_tx_cookie++;
		if (rwnx_hw->roc->tx_cookie[i] == cookie) {
			rwnx_hw->roc->tx_cookie[i] = 0;
			rwnx_hw->roc->tx_cnt--;
			break;
		}
	}

	if (i == NX_ROC_TX) {
		// Didn't find the cookie but this frame may still have been sent within this
		// Roc if more than NX_ROC_TX frame have been sent
		if (nb_tx_cookie != rwnx_hw->roc->tx_cnt)
			rwnx_hw->roc->tx_cnt--;
		else {
			spin_unlock_bh(&rwnx_hw->roc_lock);
			return 0;
		}

	}

	// Stop the RoC if started to send TX frame and all frames have been "wait cancelled"
	if ((!rwnx_hw->roc->internal) || (rwnx_hw->roc->tx_cnt > 0)) {
		spin_unlock_bh(&rwnx_hw->roc_lock);
		return 0;
	}
	roc_cookie = (unsigned long)rwnx_hw->roc;
	spin_unlock_bh(&rwnx_hw->roc_lock);

	return rwnx_cfg80211_cancel_remain_on_channel(wiphy, wdev, roc_cookie);
}

/**
 * @start_radar_detection: Start radar detection in the driver.
 */
static int rwnx_cfg80211_start_radar_detection(struct wiphy *wiphy, struct net_device *dev,
					       struct cfg80211_chan_def *chandef, u32 cac_time_ms)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct apm_start_cac_cfm cfm;
	int ret = 0;

	/* Check the validity of vif_index */
	if (rwnx_vif->vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_LINK, "%s, vif_index %u invalid\n", __func__, rwnx_vif->vif_index);
		return -EINVAL;
	}

	rwnx_radar_start_cac(&rwnx_hw->radar, cac_time_ms, rwnx_vif);
	ret = rwnx_send_apm_start_cac_req(rwnx_hw, rwnx_vif, chandef, &cfm);

	if (ret) {
		netdev_err(dev, "%s start radar detection\n", __func__);
		return -EINVAL;
	}

	if (cfm.status == CO_OK) {
		spin_lock_bh(&rwnx_hw->cb_lock);
		rwnx_chanctx_link(rwnx_vif, cfm.ch_idx, chandef);
		if (rwnx_hw->cur_chanctx == rwnx_vif->ch_index)
			rwnx_radar_detection_enable(&rwnx_hw->radar, RWNX_RADAR_DETECT_REPORT, RWNX_RADAR_RIU);
		spin_unlock_bh(&rwnx_hw->cb_lock);
	} else {
		return -EIO;
	}

	return 0;
}

/**
 * @update_ft_ies: Provide updated Fast BSS Transition information to the
 *	driver. If the SME is in the driver/firmware, this information can be
 *	used in building Authentication and Reassociation Request frames.
 */
#ifdef CONFIG_OPLUS_PLATFORM
__maybe_unused
#endif
static int rwnx_cfg80211_update_ft_ies(struct wiphy *wiphy, struct net_device *dev,
				       struct cfg80211_update_ft_ies_params *ftie)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *vif = netdev_priv(dev);
	const struct element *rsne = NULL, *mde = NULL, *fte = NULL, *elem;
	bool ft_in_non_rsn = false;
	int fties_len = 0;
	u8 *ft_assoc_ies, *pos;

	/* Check the validity of vif_index */
	if (vif->vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_LINK, "%s, vif_index %u invalid\n", __func__, vif->vif_index);
		return -EINVAL;
	}

	if ((RWNX_VIF_TYPE(vif) != NL80211_IFTYPE_STATION) || (vif->sta.ft_assoc_ies == NULL))
		return 0;

	for_each_element(elem, ftie->ie, ftie->ie_len)
	{
		if (elem->id == WLAN_EID_RSN)
			rsne = elem;
		else if (elem->id == WLAN_EID_MOBILITY_DOMAIN)
			mde = elem;
		else if (elem->id == WLAN_EID_FAST_BSS_TRANSITION)
			fte = elem;
		else
			netdev_warn(dev, "Unexpected FT element %d\n", elem->id);
	}
	if (!mde) {
		// maybe just test MDE for
		netdev_warn(dev, "Didn't find Mobility_Domain Element\n");
		return 0;
	} else if (!rsne && !fte) {
		// not sure this happen in real life ...
		ft_in_non_rsn = true;
	} else if (!rsne || !fte) {
		netdev_warn(dev, "Didn't find RSN or Fast Transition Element\n");
		return 0;
	}

	for_each_element(elem, vif->sta.ft_assoc_ies, vif->sta.ft_assoc_ies_len) {
		if ((elem->id == WLAN_EID_RSN) || (elem->id == WLAN_EID_MOBILITY_DOMAIN) ||
		    (elem->id == WLAN_EID_FAST_BSS_TRANSITION))
			fties_len += elem->datalen + sizeof(struct element);
	}

	ft_assoc_ies = kmalloc(vif->sta.ft_assoc_ies_len - fties_len + ftie->ie_len, GFP_KERNEL);
	if (!ft_assoc_ies) {
		netdev_warn(dev, "Fail to allocate buffer for association elements");
		return 0;
	}

	// Recopy current Association Elements one at a time and replace FT
	// element with updated version.
	pos = ft_assoc_ies;
	for_each_element(elem, vif->sta.ft_assoc_ies, vif->sta.ft_assoc_ies_len) {
		if (elem->id == WLAN_EID_RSN) {
			if (ft_in_non_rsn) {
				netdev_warn(dev, "Found RSN element in non RSN FT");
				goto abort;
			} else if (!rsne) {
				netdev_warn(dev, "Found several RSN element");
				goto abort;
			} else {
				memcpy(pos, rsne, sizeof(*rsne) + rsne->datalen);
				pos += sizeof(*rsne) + rsne->datalen;
				rsne = NULL;
			}
		} else if (elem->id == WLAN_EID_MOBILITY_DOMAIN) {
			if (!mde) {
				netdev_warn(dev, "Found several Mobility Domain element");
				goto abort;
			} else {
				memcpy(pos, mde, sizeof(*mde) + mde->datalen);
				pos += sizeof(*mde) + mde->datalen;
				mde = NULL;
			}
		} else if (elem->id == WLAN_EID_FAST_BSS_TRANSITION) {
			if (ft_in_non_rsn) {
				netdev_warn(dev, "Found Fast Transition element in non RSN FT");
				goto abort;
			} else if (!fte) {
				netdev_warn(dev, "found several Fast Transition element");
				goto abort;
			} else {
				memcpy(pos, fte, sizeof(*fte) + fte->datalen);
				pos += sizeof(*fte) + fte->datalen;
				fte = NULL;
			}
		} else {
			// Put FTE after MDE if non present in Association Element
			if (fte && !mde) {
				memcpy(pos, fte, sizeof(*fte) + fte->datalen);
				pos += sizeof(*fte) + fte->datalen;
				fte = NULL;
			}
			memcpy(pos, elem, sizeof(*elem) + elem->datalen);
			pos += sizeof(*elem) + elem->datalen;
		}
	}
	if (fte) {
		memcpy(pos, fte, sizeof(*fte) + fte->datalen);
		pos += sizeof(*fte) + fte->datalen;
		fte = NULL;
	}

	kfree(vif->sta.ft_assoc_ies);
	vif->sta.ft_assoc_ies = ft_assoc_ies;
	vif->sta.ft_assoc_ies_len = pos - ft_assoc_ies;

	if (vif->sta.flags & RWNX_STA_FT_OVER_DS) {
		struct sm_connect_cfm sm_connect_cfm;
		struct cfg80211_connect_params sme;

		memset(&sme, 0, sizeof(sme));
		rsne = cfg80211_find_elem(WLAN_EID_RSN, vif->sta.ft_assoc_ies, vif->sta.ft_assoc_ies_len);
		if (rsne && rwnx_rsne_to_connect_params(rsne, &sme)) {
			netdev_warn(dev, "FT RSN parsing failed\n");
			return 0;
		}

		sme.ssid_len = vif->sta.ft_assoc_ies[1];
		sme.ssid = &vif->sta.ft_assoc_ies[2];
		sme.bssid = vif->sta.ft_target_ap;
		sme.ie = &vif->sta.ft_assoc_ies[2 + sme.ssid_len];
		sme.ie_len = vif->sta.ft_assoc_ies_len - (2 + sme.ssid_len);
		sme.auth_type = NL80211_AUTHTYPE_FT;
		rwnx_send_sm_connect_req(rwnx_hw, vif, &sme, &sm_connect_cfm);
		vif->sta.flags &= ~RWNX_STA_FT_OVER_DS;

	} else if (vif->sta.flags & RWNX_STA_FT_OVER_AIR) {
		uint8_t ssid_len;
		vif->sta.flags &= ~RWNX_STA_FT_OVER_AIR;

		// Skip the first element (SSID)
		ssid_len = vif->sta.ft_assoc_ies[1] + 2;
		if (rwnx_send_sm_ft_auth_rsp(rwnx_hw, vif, &vif->sta.ft_assoc_ies[ssid_len],
					     vif->sta.ft_assoc_ies_len - ssid_len))
			netdev_err(dev, "FT Over Air: Failed to send updated assoc elem\n");
	}

	return 0;

abort:
	kfree(ft_assoc_ies);
	return 0;
}

/**
 * @set_cqm_rssi_config: Configure connection quality monitor RSSI threshold.
 */
static int rwnx_cfg80211_set_cqm_rssi_config(struct wiphy *wiphy, struct net_device *dev, int32_t rssi_thold,
					     uint32_t rssi_hyst)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);

	/* Check the validity of vif_index */
	if (rwnx_vif->vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_LINK, "%s, vif_index %u invalid\n", __func__, rwnx_vif->vif_index);
		return -EINVAL;
	}

	return rwnx_send_cfg_rssi_req(rwnx_hw, rwnx_vif->vif_index, rssi_thold, rssi_hyst);
}

/**
 *
 * @channel_switch: initiate channel-switch procedure (with CSA). Driver is
 *	responsible for veryfing if the switch is possible. Since this is
 *	inherently tricky driver may decide to disconnect an interface later
 *	with cfg80211_stop_iface(). This doesn't mean driver can accept
 *	everything. It should do it's best to verify requests and reject them
 *	as soon as possible.
 */
static int rwnx_cfg80211_channel_switch(struct wiphy *wiphy, struct net_device *dev,
					struct cfg80211_csa_settings *params)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *vif = netdev_priv(dev);
	struct rwnx_ipc_buf buf;
	struct rwnx_bcn *bcn, *bcn_after;
	struct rwnx_csa *csa;
	u16 csa_oft[BCN_MAX_CSA_CPT];
	u8 *bcn_buf;
	int i, error = 0;

	bes_dbg(BES_DBG_CHAN, RWNX_FN_ENTRY_STR);

	/* Check the validity of vif_index */
	if (vif->vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_LINK, "%s, vif_index %u invalid\n", __func__, vif->vif_index);
		return -EINVAL;
	}

	if (vif->ap.csa)
		return -EINVAL;

	if (params->n_counter_offsets_beacon > BCN_MAX_CSA_CPT)
		return -EINVAL;

	/* Build the new beacon with CSA IE */
	bcn = &vif->ap.bcn;
	bcn_buf = rwnx_build_bcn(bcn, &params->beacon_csa);
	if (!bcn_buf)
		return -ENOMEM;

	memset(csa_oft, 0, sizeof(csa_oft));
	for (i = 0; i < params->n_counter_offsets_beacon; i++) {
		csa_oft[i] = params->counter_offsets_beacon[i] + bcn->head_len + bcn->tim_len;
	}

	/* If count is set to 0 (i.e anytime after this beacon) force it to 2 */
	if (params->count == 0) {
		params->count = 2;
		for (i = 0; i < params->n_counter_offsets_beacon; i++) {
			bcn_buf[csa_oft[i]] = 2;
		}
	}

	if ((error = rwnx_ipc_buf_a2e_init(rwnx_hw, &buf, bcn_buf, bcn->len))) {
		netdev_err(dev, "Failed to allocate IPC buf for CSA beacon\n");
		kfree(bcn_buf);
		return error;
	}

	/* Build the beacon to use after CSA. It will only be sent to fw once
	   CSA is over, but do it before sending the beacon as it must be ready
	   when CSA is finished. */
	csa = kzalloc(sizeof(struct rwnx_csa), GFP_KERNEL);
	if (!csa) {
		error = -ENOMEM;
		goto end;
	}

	bcn_after = &csa->bcn;
	bcn_buf = rwnx_build_bcn(bcn_after, &params->beacon_after);
	if (!bcn_buf) {
		error = -ENOMEM;
		kfree(csa);
		rwnx_del_csa(vif);
		goto end;
	}

	if ((error = rwnx_ipc_buf_a2e_init(rwnx_hw, &csa->buf, bcn_buf, bcn_after->len))) {
		netdev_err(dev, "Failed to allocate IPC buf for after CSA beacon\n");
		kfree(csa);
		rwnx_del_csa(vif);
		kfree(bcn_buf);
		goto end;
	}

	vif->ap.csa = csa;
	csa->vif = vif;
	csa->chandef = params->chandef;

	/* Send new Beacon. FW will extract channel and count from the beacon */
	error = rwnx_send_bcn_change(rwnx_hw, vif->vif_index, &buf, bcn->head_len, bcn->tim_len, csa_oft);
	bes_dbg(BES_DBG_CHAN, "%s, ret=%d\n", __func__, error);
	if (error) {
		rwnx_del_csa(vif);
	} else {
		INIT_WORK(&csa->work, rwnx_csa_finish);

#ifdef ADPT_GKI_KERNEL
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 144))
		cfg80211_ch_switch_started_notify(dev, &csa->chandef, 0, params->count, 0, 0);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 81))
		cfg80211_ch_switch_started_notify(dev, &csa->chandef, 0, params->count, 0);
#else
		cfg80211_ch_switch_started_notify(dev, &csa->chandef, params->count);
#endif
#else /* ADPT_GKI_KERNEL */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 3, 0))
		cfg80211_ch_switch_started_notify(dev, &csa->chandef, 0, params->count, 0, 0);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 1, 0))
		cfg80211_ch_switch_started_notify(dev, &csa->chandef, 0, params->count, 0);
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(5, 11, 0)
		cfg80211_ch_switch_started_notify(dev, &csa->chandef, params->count, 0);
#else
		cfg80211_ch_switch_started_notify(dev, &csa->chandef, params->count);
#endif
#endif /* ADPT_GKI_KERNEL */
	}

end:
	rwnx_ipc_buf_dealloc(rwnx_hw, &buf);
	return error;
}

/**
 * @@tdls_mgmt: Transmit a TDLS management frame.
 */
static int rwnx_cfg80211_tdls_mgmt(struct wiphy *wiphy, struct net_device *dev, const u8 *peer, u8 action_code,
				   u8 dialog_token, u16 status_code, u32 peer_capability, bool initiator, const u8 *buf,
				   size_t len)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	int ret = 0;

	/* Check the validity of vif_index */
	if (rwnx_vif->vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_LINK, "%s, vif_index %u invalid\n", __func__, rwnx_vif->vif_index);
		return -EINVAL;
	}

	/* make sure we support TDLS */
	if (!(wiphy->flags & WIPHY_FLAG_SUPPORTS_TDLS))
		return -ENOTSUPP;

	/* make sure we are in station mode (and connected) */
	if ((RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_STATION) || (!rwnx_vif->up) || (!rwnx_vif->sta.ap))
		return -ENOTSUPP;

	/* only one TDLS link is supported */
	if ((action_code == WLAN_TDLS_SETUP_REQUEST) && (rwnx_vif->sta.tdls_sta) &&
	    (rwnx_vif->tdls_status == TDLS_LINK_ACTIVE)) {
		netdev_err(dev, "%s: only one TDLS link is supported!\n", __func__);
		return -ENOTSUPP;
	}

	if ((action_code == WLAN_TDLS_DISCOVERY_REQUEST) && (rwnx_hw->mod_params->ps_on)) {
		netdev_err(dev,
			   "%s: discovery request is not supported when "
			   "power-save is enabled!\n",
			   __func__);
		return -ENOTSUPP;
	}

	switch (action_code) {
	case WLAN_TDLS_SETUP_RESPONSE:
		/* only one TDLS link is supported */
		if ((status_code == 0) && (rwnx_vif->sta.tdls_sta) && (rwnx_vif->tdls_status == TDLS_LINK_ACTIVE)) {
			netdev_err(dev, "%s: only one TDLS link is supported!\n", __func__);
			status_code = WLAN_STATUS_REQUEST_DECLINED;
		}
		/* fall-through */
	case WLAN_TDLS_SETUP_REQUEST:
	case WLAN_TDLS_TEARDOWN:
	case WLAN_TDLS_DISCOVERY_REQUEST:
	case WLAN_TDLS_SETUP_CONFIRM:
	case WLAN_PUB_ACTION_TDLS_DISCOVER_RES:
		ret = rwnx_tdls_send_mgmt_packet_data(rwnx_hw, rwnx_vif, peer, action_code, dialog_token, status_code,
						      peer_capability, initiator, buf, len, 0, NULL);
		break;

	default:
		netdev_err(dev, "%s: Unknown TDLS mgmt/action frame %pM\n", __func__, peer);
		ret = -EOPNOTSUPP;
		break;
	}

	if (action_code == WLAN_TDLS_SETUP_REQUEST) {
		rwnx_vif->tdls_status = TDLS_SETUP_REQ_TX;
	} else if (action_code == WLAN_TDLS_SETUP_RESPONSE) {
		rwnx_vif->tdls_status = TDLS_SETUP_RSP_TX;
	} else if ((action_code == WLAN_TDLS_SETUP_CONFIRM) && (ret == CO_OK)) {
		rwnx_vif->tdls_status = TDLS_LINK_ACTIVE;
		/* Set TDLS active */
		rwnx_vif->sta.tdls_sta->tdls.active = true;
	}

	return ret;
}

/**
 * @tdls_oper: Perform a high-level TDLS operation (e.g. TDLS link setup).
 */
static int rwnx_cfg80211_tdls_oper(struct wiphy *wiphy, struct net_device *dev, const u8 *peer,
				   enum nl80211_tdls_operation oper)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	int error;

	/* Check the validity of vif_index */
	if (rwnx_vif->vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_LINK, "%s, vif_index %u invalid\n", __func__, rwnx_vif->vif_index);
		return -EINVAL;
	}

	if (oper != NL80211_TDLS_DISABLE_LINK)
		return 0;

	if (!rwnx_vif->sta.tdls_sta) {
		netdev_err(dev, "%s: TDLS station %pM does not exist\n", __func__, peer);
		return -ENOLINK;
	}

	if (memcmp(rwnx_vif->sta.tdls_sta->mac_addr, peer, ETH_ALEN) == 0) {
		/* Disable Channel Switch */
		if (!rwnx_send_tdls_cancel_chan_switch_req(rwnx_hw, rwnx_vif, rwnx_vif->sta.tdls_sta, NULL))
			rwnx_vif->sta.tdls_sta->tdls.chsw_en = false;

		netdev_info(dev, "Del TDLS sta %d (%pM)", rwnx_vif->sta.tdls_sta->sta_idx,
			    rwnx_vif->sta.tdls_sta->mac_addr);
		/* Ensure that we won't process PS change ind */
		spin_lock_bh(&rwnx_hw->cb_lock);
		rwnx_vif->sta.tdls_sta->ps.active = false;
		rwnx_vif->sta.tdls_sta->valid = false;
		rwnx_vif->sta.tdls_sta->twt_ind.sta_idx = RWNX_INVALID_STA;
		spin_unlock_bh(&rwnx_hw->cb_lock);
		rwnx_txq_sta_deinit(rwnx_hw, rwnx_vif->sta.tdls_sta);
		error = rwnx_send_me_sta_del(rwnx_hw, rwnx_vif->sta.tdls_sta->sta_idx, true);
		if ((error != 0) && (error != -EPIPE))
			return error;

#ifdef CONFIG_RWNX_BFMER
		// Disable Beamformer if supported
		rwnx_bfmer_report_del(rwnx_hw, rwnx_vif->sta.tdls_sta);
		rwnx_mu_group_sta_del(rwnx_hw, rwnx_vif->sta.tdls_sta);
#endif /* CONFIG_RWNX_BFMER */

		/* Set TDLS not active */
		rwnx_reset_sta_stats(rwnx_vif->sta.tdls_sta);
		bes_tp_dbg_reset_tx_rx_info(rwnx_hw, rwnx_vif->sta.tdls_sta);
		rwnx_dbgfs_unregister_sta(rwnx_hw, rwnx_vif->sta.tdls_sta);
		bes_sta_txrx_rate_stat_deinit(rwnx_hw, rwnx_vif->sta.tdls_sta);
		// Remove TDLS station
		rwnx_vif->tdls_status = TDLS_LINK_IDLE;
		rwnx_vif->sta.tdls_sta = NULL;
	}

	return 0;
}

/**
 *  @tdls_channel_switch: Start channel-switching with a TDLS peer. The driver
 *	is responsible for continually initiating channel-switching operations
 *	and returning to the base channel for communication with the AP.
 */
static int rwnx_cfg80211_tdls_channel_switch(struct wiphy *wiphy, struct net_device *dev, const u8 *addr, u8 oper_class,
					     struct cfg80211_chan_def *chandef)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_sta *rwnx_sta = rwnx_vif->sta.tdls_sta;
	struct tdls_chan_switch_cfm cfm;
	int error;

	/* Check the validity of vif_index */
	if (rwnx_vif->vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_LINK, "%s, vif_index %u invalid\n", __func__, rwnx_vif->vif_index);
		return -EINVAL;
	}

	if ((!rwnx_sta) || (memcmp(addr, rwnx_sta->mac_addr, ETH_ALEN))) {
		netdev_err(dev, "%s: TDLS station %pM doesn't exist\n", __func__, addr);
		return -ENOLINK;
	}

	if (!rwnx_sta->tdls.chsw_allowed) {
		netdev_err(dev, "%s: TDLS station %pM does not support TDLS channel switch\n", __func__, addr);
		return -ENOTSUPP;
	}

	error = rwnx_send_tdls_chan_switch_req(rwnx_hw, rwnx_vif, rwnx_sta, rwnx_sta->tdls.initiator, oper_class,
					       chandef, &cfm);
	if (error)
		return error;

	if (!cfm.status) {
		rwnx_sta->tdls.chsw_en = true;
		return 0;
	} else {
		netdev_err(dev, "%s: TDLS channel switch already enabled and only one is supported\n", __func__);
		return -EALREADY;
	}
}

/**
 * @tdls_cancel_channel_switch: Stop channel-switching with a TDLS peer. Both
 *	peers must be on the base channel when the call completes.
 */
static void rwnx_cfg80211_tdls_cancel_channel_switch(struct wiphy *wiphy, struct net_device *dev, const u8 *addr)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_sta *rwnx_sta = rwnx_vif->sta.tdls_sta;
	struct tdls_cancel_chan_switch_cfm cfm;

	if (!rwnx_sta)
		return;

	if (!rwnx_send_tdls_cancel_chan_switch_req(rwnx_hw, rwnx_vif, rwnx_sta, &cfm))
		rwnx_sta->tdls.chsw_en = false;
}

/**
 * @change_bss: Modify parameters for a given BSS (mainly for AP mode).
 */
static int rwnx_cfg80211_change_bss(struct wiphy *wiphy, struct net_device *dev, struct bss_parameters *params)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	int res = -EOPNOTSUPP;

	if (((RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_AP) || (RWNX_VIF_TYPE(rwnx_vif) == NL80211_IFTYPE_P2P_GO)) &&
	    (params->ap_isolate > -1)) {
		if (params->ap_isolate)
			rwnx_vif->ap.flags |= RWNX_AP_ISOLATE;
		else
			rwnx_vif->ap.flags &= ~RWNX_AP_ISOLATE;

		res = 0;
	}

	return res;
}

/* idx for struct tx_rate_vector rate_info member */
const int tx_rate_type_idx[RWNX_TX_RATE_TYPE_NUM] = {
	[RWNX_TX_RATE_NEGOT_MAX_RATE] = -1,
	[RWNX_TX_RATE_HIGHEST_THROUGHPUT] = 0,
	[RWNX_TX_RATE_HIGHEST_PROBABILITY] = 2,
};

int rwnx_fill_txrx_rate(struct txrx_rate_info *tx_rate_info, struct rate_info *txrate)
{
	int i;
	u8 legrates_lut_size = rwnx_get_legrates_lut_size();

	memset((void *)txrate, 0, sizeof(*txrate));

	switch (tx_rate_info->ch_bw) {
	case PHY_CHNL_BW_20:
		txrate->bw = RATE_INFO_BW_20;
		break;
	case PHY_CHNL_BW_40:
		txrate->bw = RATE_INFO_BW_40;
		break;
	case PHY_CHNL_BW_80:
		txrate->bw = RATE_INFO_BW_80;
		break;
	case PHY_CHNL_BW_160:
		txrate->bw = RATE_INFO_BW_160;
		break;
	default:
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
		txrate->bw = RATE_INFO_BW_HE_RU;
#endif
		break;
	}

	switch (tx_rate_info->format_mod) {
	case FORMATMOD_NON_HT:
	case FORMATMOD_NON_HT_DUP_OFDM:
		txrate->flags = 0;
		for (i = 0; i < legrates_lut_size; i++) {
			if (tx_rate_info->leg_rate == legrates_lut[i].idx) {
				txrate->legacy = legrates_lut[i].rate;
				break;
			}
		}
		if (i >= legrates_lut_size)
			return -EINVAL;
		break;
	case FORMATMOD_HT_MF:
	case FORMATMOD_HT_GF:
		txrate->flags = RATE_INFO_FLAGS_MCS;
		if (tx_rate_info->short_gi)
			txrate->flags |= RATE_INFO_FLAGS_SHORT_GI;
		txrate->mcs = tx_rate_info->mcs;
		break;
	case FORMATMOD_VHT:
		txrate->flags = RATE_INFO_FLAGS_VHT_MCS;
		if (tx_rate_info->short_gi)
			txrate->flags |= RATE_INFO_FLAGS_SHORT_GI;
		txrate->mcs = tx_rate_info->mcs;
		txrate->nss = tx_rate_info->nss + 1;
		break;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
	case FORMATMOD_HE_MU:
		txrate->he_ru_alloc = tx_rate_info->ru_size;
	case FORMATMOD_HE_SU:
	case FORMATMOD_HE_ER:
	case FORMATMOD_HE_TB:
		txrate->flags = RATE_INFO_FLAGS_HE_MCS;
		txrate->mcs = tx_rate_info->mcs;
		txrate->nss = tx_rate_info->nss + 1;
		txrate->he_gi = tx_rate_info->he_gi_type;
		txrate->he_dcm = tx_rate_info->dcm;
		break;
#endif
	default:
		return -EINVAL;
	}

	return 0;
}

static int rwnx_update_tx_rate_info(struct rwnx_hw *rwnx_hw, struct station_info *sinfo, struct rwnx_sta *sta)
{
	int ret = 0;
	struct tx_rate_vector *tx_rate_vector = &sta->stats.tx_rate_vector;
	struct txrx_rate_info *tx_rate_info;
	u8 rate_type = CONFIG_BES_GET_TX_RATE_TYPE;

#if (CONFIG_BES_GET_TX_RATE_TYPE >= RWNX_TX_RATE_TYPE_NUM || \
     CONFIG_BES_GET_TX_RATE_TYPE < RWNX_TX_RATE_NEGOT_MAX_RATE)
#error "The range of CONFIG_BES_GET_TX_RATE_TYPE range is 0~2"
#endif

	if (!(rate_type == RWNX_TX_RATE_NEGOT_MAX_RATE &&
	     (tx_rate_vector->status == 0 && *((u32 *)(&tx_rate_vector->negot_max_rate)) != 0)))
		ret = bes_get_tx_rate(rwnx_hw, sta, tx_rate_vector);
	if (ret)
		return -EINVAL;

	if (rate_type == RWNX_TX_RATE_NEGOT_MAX_RATE)
		tx_rate_info = &tx_rate_vector->negot_max_rate;
	else
		tx_rate_info = &tx_rate_vector->rate_info[tx_rate_type_idx[rate_type]];


	ret = rwnx_fill_txrx_rate(tx_rate_info, &sinfo->txrate);
	if (ret)
		return ret;

	sinfo->filled |= BIT(NL80211_STA_INFO_TX_BITRATE);

	return 0;
}

static int rwnx_fill_station_info(struct rwnx_sta *sta, struct rwnx_vif *vif, struct station_info *sinfo)
{
	struct rwnx_sta_stats *stats = &sta->stats;
	struct rx_vector_1 *rx_vect1 = &stats->last_rx.rx_vect1;
	struct rwnx_sta *rwnx_sta = vif->sta.ap;

	// Generic info
	sinfo->generation = vif->generation;

	sinfo->inactive_time = jiffies_to_msecs(jiffies - stats->last_act);
	sinfo->connected_time = ktime_to_ms(div_s64(ktime_sub(ktime_get(), stats->connected_start_time), 1000));
	sinfo->rx_bytes = stats->rx_bytes;
	sinfo->tx_bytes = stats->tx_bytes;
	sinfo->tx_packets = stats->tx_pkts;
	sinfo->rx_packets = stats->rx_pkts;
	sinfo->tx_failed = stats->tx_fail_stats.tx_fail_pkts;
	if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_STATION || RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_CLIENT) {
		if(rwnx_sta)
			sinfo->signal = bes_get_beacon_rssi(vif->rwnx_hw, rwnx_sta);
	} else {
		sinfo->signal = rx_vect1->rssi1;
	}

	switch (rx_vect1->ch_bw) {
	case PHY_CHNL_BW_20:
		sinfo->rxrate.bw = RATE_INFO_BW_20;
		break;
	case PHY_CHNL_BW_40:
		sinfo->rxrate.bw = RATE_INFO_BW_40;
		break;
	case PHY_CHNL_BW_80:
		sinfo->rxrate.bw = RATE_INFO_BW_80;
		break;
	case PHY_CHNL_BW_160:
		sinfo->rxrate.bw = RATE_INFO_BW_160;
		break;
	default:
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0))
		sinfo->rxrate.bw = RATE_INFO_BW_HE_RU;
#endif
		break;
	}

	switch (rx_vect1->format_mod) {
	case FORMATMOD_NON_HT:
	case FORMATMOD_NON_HT_DUP_OFDM:
		sinfo->rxrate.flags = 0;
		sinfo->rxrate.legacy = legrates_lut[rx_vect1->leg_rate].rate;
		break;
	case FORMATMOD_HT_MF:
	case FORMATMOD_HT_GF:
		sinfo->rxrate.flags = RATE_INFO_FLAGS_MCS;
		if (rx_vect1->ht.short_gi)
			sinfo->rxrate.flags |= RATE_INFO_FLAGS_SHORT_GI;
		sinfo->rxrate.mcs = rx_vect1->ht.mcs;
		break;
	case FORMATMOD_VHT:
		sinfo->rxrate.flags = RATE_INFO_FLAGS_VHT_MCS;
		if (rx_vect1->vht.short_gi)
			sinfo->rxrate.flags |= RATE_INFO_FLAGS_SHORT_GI;
		sinfo->rxrate.mcs = rx_vect1->vht.mcs;
		sinfo->rxrate.nss = rx_vect1->vht.nss + 1;
		break;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 19, 0)
	case FORMATMOD_HE_MU:
		sinfo->rxrate.he_ru_alloc = rx_vect1->he.ru_size;
	case FORMATMOD_HE_SU:
	case FORMATMOD_HE_ER:
	case FORMATMOD_HE_TB:
		sinfo->rxrate.flags = RATE_INFO_FLAGS_HE_MCS;
		sinfo->rxrate.mcs = rx_vect1->he.mcs;
		sinfo->rxrate.nss = rx_vect1->he.nss + 1;
		sinfo->rxrate.he_gi = rx_vect1->he.gi_type;
		sinfo->rxrate.he_dcm = rx_vect1->he.dcm;
		break;
#endif
	default:
		return -EINVAL;
	}

	sinfo->filled =
		(BIT(NL80211_STA_INFO_INACTIVE_TIME) | BIT(NL80211_STA_INFO_RX_BYTES64) |
		 BIT(NL80211_STA_INFO_TX_BYTES64) | BIT(NL80211_STA_INFO_RX_PACKETS) |
		 BIT(NL80211_STA_INFO_TX_PACKETS) | BIT(NL80211_STA_INFO_SIGNAL) |
		 BIT(NL80211_STA_INFO_RX_BITRATE) | BIT(NL80211_STA_INFO_TX_FAILED) |
		 BIT(NL80211_STA_INFO_CONNECTED_TIME));

	if (rwnx_update_tx_rate_info(vif->rwnx_hw, sinfo, sta))
		return -EINVAL;

	// Mesh specific info
	if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_MESH_POINT) {
		struct mesh_peer_info_cfm peer_info_cfm;
		if (rwnx_send_mesh_peer_info_req(vif->rwnx_hw, vif, sta->sta_idx, &peer_info_cfm))
			return -ENOMEM;

		peer_info_cfm.last_bcn_age = peer_info_cfm.last_bcn_age / 1000;
		if (peer_info_cfm.last_bcn_age < sinfo->inactive_time)
			sinfo->inactive_time = peer_info_cfm.last_bcn_age;

		sinfo->llid = peer_info_cfm.local_link_id;
		sinfo->plid = peer_info_cfm.peer_link_id;
		sinfo->plink_state = peer_info_cfm.link_state;
		sinfo->local_pm = peer_info_cfm.local_ps_mode;
		sinfo->peer_pm = peer_info_cfm.peer_ps_mode;
		sinfo->nonpeer_pm = peer_info_cfm.non_peer_ps_mode;

		sinfo->filled |= (BIT(NL80211_STA_INFO_LLID) | BIT(NL80211_STA_INFO_PLID) |
				  BIT(NL80211_STA_INFO_PLINK_STATE) | BIT(NL80211_STA_INFO_LOCAL_PM) |
				  BIT(NL80211_STA_INFO_PEER_PM) | BIT(NL80211_STA_INFO_NONPEER_PM));
	}

	return 0;
}

/**
 * @get_station: get station information for the station identified by @mac
 */
static int rwnx_cfg80211_get_station(struct wiphy *wiphy, struct net_device *dev, const u8 *mac,
				     struct station_info *sinfo)
{
	struct rwnx_vif *vif = netdev_priv(dev);
	struct rwnx_sta *sta = NULL;
	struct rwnx_sta *rwnx_sta = vif->sta.ap;

	if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_MONITOR)
		return -EINVAL;
	else if ((RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_STATION) || (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_CLIENT)) {
		if (rwnx_sta && ether_addr_equal(rwnx_sta->mac_addr, mac))
			sta = rwnx_sta;
	} else {
		struct rwnx_sta *sta_iter;
		list_for_each_entry(sta_iter, &vif->ap.sta_list, list) {
			if (sta_iter->valid && ether_addr_equal(sta_iter->mac_addr, mac)) {
				sta = sta_iter;
				break;
			}
		}
	}

	if (sta)
		return rwnx_fill_station_info(sta, vif, sinfo);

	return -EINVAL;
}

/**
 * @dump_station: dump station callback -- resume dump at index @idx
 */
static int rwnx_cfg80211_dump_station(struct wiphy *wiphy, struct net_device *dev, int idx, u8 *mac,
				      struct station_info *sinfo)
{
	struct rwnx_vif *vif = netdev_priv(dev);
	struct rwnx_sta *sta = NULL;
	struct rwnx_sta *rwnx_sta = vif->sta.ap;

	if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_MONITOR)
		return -EINVAL;
	else if ((RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_STATION) || (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_CLIENT)) {
		if ((idx == 0) && rwnx_sta && rwnx_sta->valid)
			sta = rwnx_sta;
	} else {
		struct rwnx_sta *sta_iter;
		int i = 0;
		list_for_each_entry(sta_iter, &vif->ap.sta_list, list) {
			if (i == idx) {
				sta = sta_iter;
				break;
			}
			i++;
		}
	}

	if (sta == NULL)
		return -ENOENT;

	/* Copy peer MAC address */
	memcpy(mac, &sta->mac_addr, ETH_ALEN);

	return rwnx_fill_station_info(sta, vif, sinfo);
}

#ifdef CONFIG_PM
/**
 * @suspend: wiphy device needs to be suspended. The variable @wow will
 *  be %NULL or contain the enabled Wake-on-Wireless triggers that are
 *  configured for the device.
 */
static int rwnx_cfg80211_suspend(struct wiphy *wiphy, struct cfg80211_wowlan *wow)
{
	int ret = 0;
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);

	bes_info(BES_DBG_WOWLAN, "<--- Enter rwnx_cfg80211_suspend --->\n");

#ifdef CONFIG_RWNX_PCIE
	/* set wowlan filter and hw filter */
	bes_wowlan_set_filter(rwnx_hw, wow);

	/* Set beacon filter */
	bes_sleep_manager_ctrl_beacon_filter(rwnx_hw, true);
#else
	ret = bes_wowlan_suspend_cfg(rwnx_hw, wow);
	bes_err_with_cond(ret, BES_DBG_WOWLAN, "%s ERROR(%d): fail to set wowlan suspend config\n", __func__, ret);
#endif

	bes_info(BES_DBG_WOWLAN, "<--- Leave rwnx_cfg80211_suspend --->\n");

	return ret;
}

/*
 * @resume: wiphy device needs to be resumed.
 */
static int rwnx_cfg80211_resume(struct wiphy *wiphy)
{
	int ret = 0;
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);

	bes_info(BES_DBG_WOWLAN, "<--- Enter rwnx_cfg80211_resume --->\n");

#ifdef CONFIG_RWNX_PCIE
	/* resume wowlan filter and hw filter */
	bes_wowlan_resume_filter(rwnx_hw);

	/* Clear beacon filter */
	bes_sleep_manager_ctrl_beacon_filter(rwnx_hw, false);
#else
	ret = bes_wowlan_resume_cfg(rwnx_hw);
	bes_err_with_cond(ret, BES_DBG_WOWLAN, "%s ERROR(%d): fail to set wowlan resume config\n", __func__, ret);
#endif

	bes_info(BES_DBG_WOWLAN, "<--- Leave rwnx_cfg80211_resume --->\n");

	return ret;
}
#endif

/**
 * @add_mpath: add a fixed mesh path
 */
static int rwnx_cfg80211_add_mpath(struct wiphy *wiphy, struct net_device *dev, const u8 *dst, const u8 *next_hop)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct mesh_path_update_cfm cfm;

	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
		return -ENOTSUPP;

	return rwnx_send_mesh_path_update_req(rwnx_hw, rwnx_vif, dst, next_hop, &cfm);
}

/**
 * @del_mpath: delete a given mesh path
 */
static int rwnx_cfg80211_del_mpath(struct wiphy *wiphy, struct net_device *dev, const u8 *dst)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct mesh_path_update_cfm cfm;

	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
		return -ENOTSUPP;

	return rwnx_send_mesh_path_update_req(rwnx_hw, rwnx_vif, dst, NULL, &cfm);
}

/**
 * @change_mpath: change a given mesh path
 */
static int rwnx_cfg80211_change_mpath(struct wiphy *wiphy, struct net_device *dev, const u8 *dst, const u8 *next_hop)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct mesh_path_update_cfm cfm;

	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
		return -ENOTSUPP;

	return rwnx_send_mesh_path_update_req(rwnx_hw, rwnx_vif, dst, next_hop, &cfm);
}

/**
 * @get_mpath: get a mesh path for the given parameters
 */
static int rwnx_cfg80211_get_mpath(struct wiphy *wiphy, struct net_device *dev, u8 *dst, u8 *next_hop,
				   struct mpath_info *pinfo)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_mesh_path *mesh_path = NULL;
	struct rwnx_mesh_path *cur;

	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
		return -ENOTSUPP;

	list_for_each_entry(cur, &rwnx_vif->ap.mpath_list, list) {
		/* Compare the path target address and the provided destination address */
		if (memcmp(dst, &cur->tgt_mac_addr, ETH_ALEN)) {
			continue;
		}

		mesh_path = cur;
		break;
	}

	if (mesh_path == NULL)
		return -ENOENT;

	/* Copy next HOP MAC address */
	if (mesh_path->nhop_sta)
		memcpy(next_hop, &mesh_path->nhop_sta->mac_addr, ETH_ALEN);

	/* Fill path information */
	pinfo->filled = 0;
	pinfo->generation = rwnx_vif->generation;

	return 0;
}

/**
 * @dump_mpath: dump mesh path callback -- resume dump at index @idx
 */
static int rwnx_cfg80211_dump_mpath(struct wiphy *wiphy, struct net_device *dev, int idx, u8 *dst, u8 *next_hop,
				    struct mpath_info *pinfo)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_mesh_path *mesh_path = NULL;
	struct rwnx_mesh_path *cur;
	int i = 0;

	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
		return -ENOTSUPP;

	list_for_each_entry(cur, &rwnx_vif->ap.mpath_list, list) {
		if (i < idx) {
			i++;
			continue;
		}

		mesh_path = cur;
		break;
	}

	if (mesh_path == NULL)
		return -ENOENT;

	/* Copy target and next hop MAC address */
	memcpy(dst, &mesh_path->tgt_mac_addr, ETH_ALEN);
	if (mesh_path->nhop_sta)
		memcpy(next_hop, &mesh_path->nhop_sta->mac_addr, ETH_ALEN);

	/* Fill path information */
	pinfo->filled = 0;
	pinfo->generation = rwnx_vif->generation;

	return 0;
}

/**
 * @get_mpp: get a mesh proxy path for the given parameters
 */
static int rwnx_cfg80211_get_mpp(struct wiphy *wiphy, struct net_device *dev, u8 *dst, u8 *mpp,
				 struct mpath_info *pinfo)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_mesh_proxy *mesh_proxy = NULL;
	struct rwnx_mesh_proxy *cur;

	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
		return -ENOTSUPP;

	list_for_each_entry(cur, &rwnx_vif->ap.proxy_list, list) {
		if (cur->local) {
			continue;
		}

		/* Compare the path target address and the provided destination address */
		if (memcmp(dst, &cur->ext_sta_addr, ETH_ALEN)) {
			continue;
		}

		mesh_proxy = cur;
		break;
	}

	if (mesh_proxy == NULL)
		return -ENOENT;

	memcpy(mpp, &mesh_proxy->proxy_addr, ETH_ALEN);

	/* Fill path information */
	pinfo->filled = 0;
	pinfo->generation = rwnx_vif->generation;

	return 0;
}

/**
 * @dump_mpp: dump mesh proxy path callback -- resume dump at index @idx
 */
static int rwnx_cfg80211_dump_mpp(struct wiphy *wiphy, struct net_device *dev, int idx, u8 *dst, u8 *mpp,
				  struct mpath_info *pinfo)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_mesh_proxy *mesh_proxy = NULL;
	struct rwnx_mesh_proxy *cur;
	int i = 0;

	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
		return -ENOTSUPP;

	list_for_each_entry(cur, &rwnx_vif->ap.proxy_list, list) {
		if (cur->local) {
			continue;
		}

		if (i < idx) {
			i++;
			continue;
		}

		mesh_proxy = cur;
		break;
	}

	if (mesh_proxy == NULL)
		return -ENOENT;

	/* Copy target MAC address */
	memcpy(dst, &mesh_proxy->ext_sta_addr, ETH_ALEN);
	memcpy(mpp, &mesh_proxy->proxy_addr, ETH_ALEN);

	/* Fill path information */
	pinfo->filled = 0;
	pinfo->generation = rwnx_vif->generation;

	return 0;
}

/**
 * @get_mesh_config: Get the current mesh configuration
 */
static int rwnx_cfg80211_get_mesh_config(struct wiphy *wiphy, struct net_device *dev, struct mesh_config *conf)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);

	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
		return -ENOTSUPP;

	return 0;
}

/**
 * @update_mesh_config: Update mesh parameters on a running mesh.
 */
static int rwnx_cfg80211_update_mesh_config(struct wiphy *wiphy, struct net_device *dev, u32 mask,
					    const struct mesh_config *nconf)
{
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct mesh_update_cfm cfm;
	int status;

	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
		return -ENOTSUPP;

	if (mask & CO_BIT(NL80211_MESHCONF_POWER_MODE - 1)) {
		rwnx_vif->ap.next_mesh_pm = nconf->power_mode;

		if (!list_empty(&rwnx_vif->ap.sta_list)) {
			// If there are mesh links we don't want to update the power mode
			// It will be updated with rwnx_update_mesh_power_mode() when the
			// ps mode of a link is updated or when a new link is added/removed
			mask &= ~BIT(NL80211_MESHCONF_POWER_MODE - 1);

			if (!mask)
				return 0;
		}
	}

	status = rwnx_send_mesh_update_req(rwnx_hw, rwnx_vif, mask, nconf, &cfm);

	if (!status && (cfm.status != 0))
		status = -EINVAL;

	return status;
}

/**
 * @join_mesh: join the mesh network with the specified parameters
 * (invoked with the wireless_dev mutex held)
 */
static int rwnx_cfg80211_join_mesh(struct wiphy *wiphy, struct net_device *dev, const struct mesh_config *conf,
				   const struct mesh_setup *setup)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct mesh_start_cfm mesh_start_cfm;
	int error = 0;
	u8 txq_status = 0;
	/* STA for BC/MC traffic */
	struct rwnx_sta *sta;

	bes_dbg(BES_DBG_INIT, RWNX_FN_ENTRY_STR);

	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_MESH_POINT)
		return -ENOTSUPP;

	/* Forward the information to the UMAC */
	if ((error = rwnx_send_mesh_start_req(rwnx_hw, rwnx_vif, conf, setup, &mesh_start_cfm))) {
		return error;
	}

	/* Check the status */
	switch (mesh_start_cfm.status) {
	case CO_OK:
		rwnx_vif->ap.bcmc_index = mesh_start_cfm.bcmc_idx;
		rwnx_vif->ap.flags = 0;
		rwnx_vif->ap.bcn_interval = setup->beacon_interval;
		rwnx_vif->use_4addr = true;
		if (setup->user_mpm)
			rwnx_vif->ap.flags |= RWNX_AP_USER_MESH_PM;

		sta = rwnx_rx_get_sta(rwnx_hw, mesh_start_cfm.bcmc_idx);

		if (WARN_ON(sta == NULL))
			return -EFAULT;

		sta->valid = true;
		sta->aid = 0;
		sta->sta_idx = mesh_start_cfm.bcmc_idx;
		sta->ch_idx = mesh_start_cfm.ch_idx;
		sta->vif_idx = rwnx_vif->vif_index;
		sta->qos = true;
		sta->acm = 0;
		sta->ps.active = false;
		sta->listen_interval = 5;
		rwnx_mu_group_sta_init(sta, NULL);
		spin_lock_bh(&rwnx_hw->cb_lock);
		rwnx_chanctx_link(rwnx_vif, mesh_start_cfm.ch_idx, (struct cfg80211_chan_def *)(&setup->chandef));
		if (rwnx_hw->cur_chanctx != mesh_start_cfm.ch_idx) {
			txq_status = (1 << RWNX_TXQ_STOP_CHAN);
		}
		rwnx_txq_vif_init(rwnx_hw, rwnx_vif, txq_status);
		spin_unlock_bh(&rwnx_hw->cb_lock);

		netif_tx_start_all_queues(dev);
		netif_carrier_on(dev);

		/* If the AP channel is already the active, we probably skip radar
		   activation on MM_CHANNEL_SWITCH_IND (unless another vif use this
		   ctxt). In anycase retest if radar detection must be activated
		 */
		if (rwnx_hw->cur_chanctx == mesh_start_cfm.ch_idx) {
			rwnx_radar_detection_enable_on_cur_channel(rwnx_hw);
		}
		break;

	case CO_BUSY:
		error = -EINPROGRESS;
		break;

	default:
		error = -EIO;
		break;
	}

	/* Print information about the operation */
	if (error) {
		netdev_info(dev, "Failed to start MP (%d)", error);
	} else {
		netdev_info(dev, "MP started: ch=%d, bcmc_idx=%d", rwnx_vif->ch_index, rwnx_vif->ap.bcmc_index);
	}

	return error;
}

/**
 * @leave_mesh: leave the current mesh network
 * (invoked with the wireless_dev mutex held)
 */
static int rwnx_cfg80211_leave_mesh(struct wiphy *wiphy, struct net_device *dev)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);
	struct rwnx_vif *rwnx_vif = netdev_priv(dev);
	struct mesh_stop_cfm mesh_stop_cfm;
	int error = 0;

	error = rwnx_send_mesh_stop_req(rwnx_hw, rwnx_vif, &mesh_stop_cfm);

	if (error == 0) {
		/* Check the status */
		switch (mesh_stop_cfm.status) {
		case CO_OK:
			spin_lock_bh(&rwnx_hw->cb_lock);
			rwnx_chanctx_unlink(rwnx_vif);
			rwnx_radar_cancel_cac(&rwnx_hw->radar);
			spin_unlock_bh(&rwnx_hw->cb_lock);
			/* delete BC/MC STA */
			rwnx_txq_vif_deinit(rwnx_hw, rwnx_vif);
			rwnx_del_bcn(&rwnx_vif->ap.bcn);

			netif_tx_stop_all_queues(dev);
			netif_carrier_off(dev);

			break;

		default:
			error = -EIO;
			break;
		}
	}

	if (error) {
		netdev_info(dev, "Failed to stop MP");
	} else {
		netdev_info(dev, "MP Stopped");
	}

	return 0;
}

static struct cfg80211_ops rwnx_cfg80211_ops = {
	.add_virtual_intf = rwnx_cfg80211_add_iface,
	.del_virtual_intf = rwnx_cfg80211_del_iface,
	.change_virtual_intf = rwnx_cfg80211_change_iface,
	.scan = rwnx_cfg80211_scan,
	.abort_scan = rwnx_cfg80211_abort_scan,
#ifdef CONFIG_SCHED_SCAN
	.sched_scan_start = rwnx_cfg80211_sched_scan_start,
	.sched_scan_stop = rwnx_cfg80211_sched_scan_stop,
#endif
	.connect = rwnx_cfg80211_connect,
	.disconnect = rwnx_cfg80211_disconnect,
	.add_key = rwnx_cfg80211_add_key,
	.get_key = rwnx_cfg80211_get_key,
	.del_key = rwnx_cfg80211_del_key,
	.set_default_key = rwnx_cfg80211_set_default_key,
	.set_default_mgmt_key = rwnx_cfg80211_set_default_mgmt_key,
	.add_station = rwnx_cfg80211_add_station,
	.del_station = rwnx_cfg80211_del_station,
	.change_station = rwnx_cfg80211_change_station,
	.mgmt_tx = rwnx_cfg80211_mgmt_tx,
	.mgmt_tx_cancel_wait = rwnx_cfg80211_mgmt_tx_cancel_wait,
	.start_ap = rwnx_cfg80211_start_ap,
	.change_beacon = rwnx_cfg80211_change_beacon,
	.stop_ap = rwnx_cfg80211_stop_ap,
	.set_monitor_channel = rwnx_cfg80211_set_monitor_channel,
	.probe_client = rwnx_cfg80211_probe_client,
	.set_wiphy_params = rwnx_cfg80211_set_wiphy_params,
	.set_txq_params = rwnx_cfg80211_set_txq_params,
	.set_tx_power = rwnx_cfg80211_set_tx_power,
	//    .get_tx_power = rwnx_cfg80211_get_tx_power,
	.set_power_mgmt = rwnx_cfg80211_set_power_mgmt,
	.get_station = rwnx_cfg80211_get_station,
	.dump_station = rwnx_cfg80211_dump_station,
	.remain_on_channel = rwnx_cfg80211_remain_on_channel,
	.cancel_remain_on_channel = rwnx_cfg80211_cancel_remain_on_channel,
	.dump_survey = rwnx_cfg80211_dump_survey,
	.get_channel = rwnx_cfg80211_get_channel,
	.start_radar_detection = rwnx_cfg80211_start_radar_detection,
#ifndef CONFIG_OPLUS_PLATFORM
	.update_ft_ies = rwnx_cfg80211_update_ft_ies,
#endif
	.set_cqm_rssi_config = rwnx_cfg80211_set_cqm_rssi_config,
	.channel_switch = rwnx_cfg80211_channel_switch,
	.tdls_channel_switch = rwnx_cfg80211_tdls_channel_switch,
	.tdls_cancel_channel_switch = rwnx_cfg80211_tdls_cancel_channel_switch,
	.tdls_mgmt = rwnx_cfg80211_tdls_mgmt,
	.tdls_oper = rwnx_cfg80211_tdls_oper,
	.change_bss = rwnx_cfg80211_change_bss,
	.set_rekey_data = bes_set_rekey_data,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
	.external_auth = rwnx_cfg80211_external_auth,
#endif
#ifdef CONFIG_PM
	.suspend = rwnx_cfg80211_suspend,
	.resume = rwnx_cfg80211_resume,
#endif
};

/*********************************************************************
 * Init/Exit functions
 *********************************************************************/
static void rwnx_wdev_unregister(struct rwnx_hw *rwnx_hw)
{
	struct rwnx_vif *rwnx_vif, *tmp;

	rtnl_lock();

	/* In versions 5.12 and above, it is necessary to call dev_close first,
	 * otherwise a deadlock will occur during the notification chain NETDEV_GOING-DOWN. */
	list_for_each_entry_safe(rwnx_vif, tmp, &rwnx_hw->vifs, list) {
		if (!netif_running(rwnx_vif->ndev))
			continue;

		dev_close(rwnx_vif->ndev);
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0))
	wiphy_lock(rwnx_hw->wiphy);
#endif

	list_for_each_entry_safe(rwnx_vif, tmp, &rwnx_hw->vifs, list) {
		rwnx_cfg80211_del_iface(rwnx_hw->wiphy, &rwnx_vif->wdev);
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0))
	wiphy_unlock(rwnx_hw->wiphy);
#endif
	rtnl_unlock();
}

static void rwnx_set_vers(struct rwnx_hw *rwnx_hw)
{
	u32 vers = rwnx_hw->version_cfm.version_lmac;

	bes_dbg(BES_DBG_INIT, RWNX_FN_ENTRY_STR);

	snprintf(rwnx_hw->wiphy->fw_version, sizeof(rwnx_hw->wiphy->fw_version), "%d.%d.%d.%d",
		 (vers & (0xff << 24)) >> 24, (vers & (0xff << 16)) >> 16, (vers & (0xff << 8)) >> 8,
		 (vers & (0xff << 0)) >> 0);

	rwnx_hw->machw_type = rwnx_machw_type(rwnx_hw->version_cfm.version_machw_2);
}

static void rwnx_reg_notifier(struct wiphy *wiphy, struct regulatory_request *request)
{
	struct rwnx_hw *rwnx_hw = wiphy_priv(wiphy);

	// For now trust all initiator
	rwnx_radar_set_domain(&rwnx_hw->radar, request->dfs_region);
	rwnx_send_me_chan_config_req(rwnx_hw);
}

static void rwnx_mac_init(struct rwnx_hw *rwnx_hw, struct mac_addr *mac)
{
	u8 mac_fixed[ETH_ALEN] = {0x0, 0x2, 0x4, 0x6, 0x8, 0xb};

#ifdef CONFIG_OPLUS_PLATFORM
	if (!oplus_wlan_get_factory_mac((void *)mac, ETH_ALEN)) {
		bes_warn(BES_DBG_INIT, "using custom generate mac:"OPLUSMACDBG"\n", OPLUSMAC2STRDBG(mac));
		return;
	}
	bes_err(BES_DBG_INIT, "using default generate mac");
#endif

#if (GET_MAC_ADDR_METHOD == 1)
	u8 mac_temp[ETH_ALEN];
	int ret;

	ret = rockchip_wifi_mac_addr(mac_temp);
	if (!(ret != 0 || (mac_temp[0] & 0x01))) {
		memcpy((void *)mac, mac_temp, ETH_ALEN);
		return;
	}
#elif (GET_MAC_ADDR_METHOD == 2)
	struct mcu_get_mac_cfm mac_cfm = {
		.status = -1,
		.mac_fixed = {0},
	};

	if (!bes_send_drv2mcu_msg(rwnx_hw, NULL, 0,
	    MCU_GET_MAC_ADDR_REQ, MCU_GET_MAC_ADDR_CFM, &mac_cfm, sizeof(mac_cfm)) &&
	    !mac_cfm.status && is_valid_ether_addr(mac_cfm.mac_fixed)) {
		memcpy((void *)mac, mac_cfm.mac_fixed, ETH_ALEN);
		return;
	}

#elif (GET_MAC_ADDR_METHOD == 3)
	if (is_valid_ether_addr(bes_sysfs_custom_mac_addr_get())) {
		memcpy((void *)mac, bes_sysfs_custom_mac_addr_get(), ETH_ALEN);
		return;
	}
#endif

	if (!is_valid_ether_addr((u8 *)mac)) {
		get_random_bytes(mac_fixed + 3, 3);
		memcpy((void *)mac, mac_fixed, ETH_ALEN);
	}
}

static void rwnx_apply_cfg_params(struct rwnx_hw *rwnx_hw, struct rwnx_conf_file *conf)
{
	/* set mac addr */
	rwnx_mac_init(rwnx_hw, &conf->mac_addr);
	memcpy(rwnx_hw->wiphy->perm_addr, &conf->mac_addr, ETH_ALEN);
#ifdef CONFIG_OPLUS_PLATFORM
	bes_info(BES_DBG_INIT, "set MAC Address: [ "OPLUSMACDBG" ]  for wlan0\n",
		 OPLUSMAC2STRDBG(&rwnx_hw->wiphy->perm_addr));
#else
	bes_info(BES_DBG_INIT, "set MAC Address: [ %pM ]  for wlan0\n",
		 &rwnx_hw->wiphy->perm_addr);
#endif

	/* Set the interval for listening to beacons */
	if (conf->listen_itv != -1) {
		rwnx_mod_params.listen_itv = conf->listen_itv;
		bes_dbg(BES_DBG_CFGFILE, "%s Set listen interval(%d)\n",
			__func__, conf->listen_itv);
	}

	/* Set ht_on */
	if (conf->ht_on != -1) {
		rwnx_mod_params.ht_on = !!conf->ht_on;
		bes_dbg(BES_DBG_CFGFILE, "%s HT_ON:%d\n", __func__, conf->ht_on);
	}

	/* Set vht_on */
	if (conf->vht_on != -1) {
		rwnx_mod_params.vht_on = !!conf->vht_on;
		bes_dbg(BES_DBG_CFGFILE, "%s VHT_ON:%d\n", __func__, conf->vht_on);
	}

	/* Set the VHT MCS map value */
	if (conf->mcs_map >= 0 && conf->mcs_map <= 2) {
		rwnx_mod_params.mcs_map = conf->mcs_map;
		bes_dbg(BES_DBG_CFGFILE, "%s Set VHT MCS map value(%d)\n",
			__func__, conf->mcs_map);
	}

	/* Set he_on */
	if (conf->he_on != -1) {
		rwnx_mod_params.he_on = !!conf->he_on;
		bes_dbg(BES_DBG_CFGFILE, "%s HE_ON:%d\n", __func__, conf->he_on);
	}

	/* Set the HE MCS map value */
	if (conf->he_mcs_map >= 0 && conf->he_mcs_map <= 2) {
		rwnx_mod_params.he_mcs_map = conf->he_mcs_map;
		bes_dbg(BES_DBG_CFGFILE, "%s Set HE MCS map value(%d)\n",
			__func__, conf->he_mcs_map);
	}

	/* Set the UAPSD timer timeout */
	if (conf->uapsd_timeout >= 0) {
		rwnx_mod_params.uapsd_timeout = conf->uapsd_timeout;
		bes_dbg(BES_DBG_CFGFILE, "%s Set UAPSD timeout value(%d)\n",
			__func__, conf->uapsd_timeout);
	}

	/* Set the KeepAlive period */
	if (conf->keepalive_period >= 0) {
		rwnx_mod_params.keepalive_period = conf->keepalive_period;
		bes_dbg(BES_DBG_CFGFILE, "%s Set KeepAlive Period value(%d)\n",
			__func__, conf->keepalive_period);
	}

	/* Set the PowerSaving configuration */
	if (conf->ps_on != -1) {
		rwnx_mod_params.ps_on = (bool)conf->ps_on;
		bes_dbg(BES_DBG_CFGFILE, "%s Set PowerSaving enable:%d\n",
			__func__, conf->ps_on);
	}

	/* Set the 40MHz Bandwidth configuration */
	if (conf->use_2040 != -1) {
		rwnx_mod_params.use_2040 = (bool)conf->use_2040;
		bes_dbg(BES_DBG_CFGFILE, "%s Set 40MHz Bandwidth enable:%d\n",
			__func__, conf->use_2040);
	}

	/* Set the 80MHz Bandwidth configuration */
	if (conf->use_80 != -1) {
		rwnx_mod_params.use_80 = (bool)conf->use_80;
		bes_dbg(BES_DBG_CFGFILE, "%s Set 80MHz Bandwidth enable:%d\n",
			__func__, conf->use_80);
	}

	/* Set the User PowerSaving Management configuration */
	if (conf->user_ps_mgmt_disable != -1) {
		rwnx_mod_params.user_ps_mgmt_disable = (bool)conf->user_ps_mgmt_disable;
		bes_dbg(BES_DBG_CFGFILE, "%s Set User PowerSaving Management disable:%d\n",
			__func__, conf->user_ps_mgmt_disable);
	}
}

static void rwnx_apply_default_params(struct rwnx_hw *rwnx_hw)
{
	/* Set default MAC address */
	rwnx_mac_init(rwnx_hw, (struct mac_addr *)&rwnx_hw->wiphy->perm_addr);
#ifdef CONFIG_OPLUS_PLATFORM
	bes_info(BES_DBG_INIT, "set MAC Address: [ "OPLUSMACDBG" ]  for wlan0\n",
		 OPLUSMAC2STRDBG(&rwnx_hw->wiphy->perm_addr));
#else
	bes_info(BES_DBG_INIT, "set MAC Address: [ %pM ]  for wlan0\n",
		 &rwnx_hw->wiphy->perm_addr);
#endif

	rwnx_hw->conf_file.bcn_loss_thd = BEACON_LOSS_DEFAULT;
	rwnx_hw->conf_file.bcn_loss_null_data_thd = BEACON_LOSS_NULL_DATA_DEFAULT;

	rwnx_hw->conf_file.scan_active_dur = SCAN_ACTIVE_DURATION;
	rwnx_hw->conf_file.scan_passive_dur = SCAN_PASSIVE_DURATION;
	rwnx_hw->conf_file.max_pwr_cfg_num = 0;
}

static void rwnx_cfg_params_check(int device_id)
{
	if (device_id == 0x207a) {
		/* device 0x207a(chip 2007p) do not support MCS0_11 */
		if(rwnx_mod_params.he_mcs_map == 2) {
			bes_warn(BES_DBG_INIT, "chip 2007p do not support MCS0_11, set he_mcs_map to MCS0_9\n");
			rwnx_mod_params.he_mcs_map = 1;
		}
	} else if (device_id == 0x1700) {
		/* device 0x1700(chip 1700) do not support sgi80 */
		if(rwnx_mod_params.sgi80 == true) {
			bes_warn(BES_DBG_INIT, "chip 1700 do not support sgi 80MHz, set sgi80 to false\n");
			rwnx_mod_params.sgi80 = false;
		}
		/* device 0x1700(chip 1700) do not support 40MHz */
		if(rwnx_mod_params.use_2040 == true) {
			bes_warn(BES_DBG_INIT, "chip 1700 do not support 40MHz, set use_2040 to false\n");
			rwnx_mod_params.use_2040 = false;
		}
		/* device 0x1700(chip 1700) do not support 80MHz */
		if(rwnx_mod_params.use_80 == true) {
			bes_warn(BES_DBG_INIT, "chip 1700 do not support 80MHz, set use_80 to false\n");
			rwnx_mod_params.use_80 = false;
		}
	}
}

static void rwnx_enable_mesh(struct rwnx_hw *rwnx_hw)
{
	struct wiphy *wiphy = rwnx_hw->wiphy;

	if (!rwnx_mod_params.mesh)
		return;

	rwnx_cfg80211_ops.add_mpath = rwnx_cfg80211_add_mpath;
	rwnx_cfg80211_ops.del_mpath = rwnx_cfg80211_del_mpath;
	rwnx_cfg80211_ops.change_mpath = rwnx_cfg80211_change_mpath;
	rwnx_cfg80211_ops.get_mpath = rwnx_cfg80211_get_mpath;
	rwnx_cfg80211_ops.dump_mpath = rwnx_cfg80211_dump_mpath;
	rwnx_cfg80211_ops.get_mpp = rwnx_cfg80211_get_mpp;
	rwnx_cfg80211_ops.dump_mpp = rwnx_cfg80211_dump_mpp;
	rwnx_cfg80211_ops.get_mesh_config = rwnx_cfg80211_get_mesh_config;
	rwnx_cfg80211_ops.update_mesh_config = rwnx_cfg80211_update_mesh_config;
	rwnx_cfg80211_ops.join_mesh = rwnx_cfg80211_join_mesh;
	rwnx_cfg80211_ops.leave_mesh = rwnx_cfg80211_leave_mesh;

	wiphy->flags |= (WIPHY_FLAG_MESH_AUTH | WIPHY_FLAG_IBSS_RSN);
	wiphy->features |= NL80211_FEATURE_USERSPACE_MPM;
	wiphy->interface_modes |= BIT(NL80211_IFTYPE_MESH_POINT);

	rwnx_limits[0].types |= BIT(NL80211_IFTYPE_MESH_POINT);
	rwnx_limits_dfs[0].types |= BIT(NL80211_IFTYPE_MESH_POINT);
}

#ifdef CONFIG_INET
static int rwnx_inetaddr_changed(struct notifier_block *nb, unsigned long event, void *arg)
{
	int ret = 0;
	struct rwnx_hw *rwnx_hw = container_of(nb, struct rwnx_hw, inetaddr_notifier);
	struct in_ifaddr *ifa = arg;
	struct net_device *ndev = ifa->ifa_dev->dev;
	struct rwnx_vif *rwnx_vif = netdev_priv(ndev);
	struct rwnx_sta *rwnx_sta;
	struct wireless_dev *wdev = ndev->ieee80211_ptr;
	u8 vif_index;

	/* Make sure it's our interface that got changed and wifi is on */
	if (!wdev || !rwnx_hw || wdev->wiphy != rwnx_hw->wiphy ||
	    !rwnx_vif || !rwnx_vif->up)
		return NOTIFY_DONE;

	/* Get the pointer of sta */
	rwnx_sta = rwnx_vif->sta.ap;

	/* Only care about NETDEV_DOWN and NETDEV_UP event */
	if (event != NETDEV_DOWN && event != NETDEV_UP)
		return NOTIFY_DONE;

	/* Only support sta or p2p gc */
	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_STATION &&
	    RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_P2P_CLIENT)
		return NOTIFY_DONE;

	/* Check the validity of vif_index */
	vif_index = rwnx_vif->vif_index;
	if (vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_LINK, "%s, vif_index %u invalid\n", __func__, vif_index);
		return NOTIFY_DONE;
	}

	/* Avoiding duplicate clearing arp offload filter */
	if ((!rwnx_vif_is_connected(rwnx_vif) || event == NETDEV_DOWN) &&
	    !rwnx_hw->filter.arp_offload[vif_index])
		return NOTIFY_DONE;

	/* Add/Delete arp offload function according to connection status and event */
	if (rwnx_vif_is_connected(rwnx_vif) && (event == NETDEV_UP)) {
		do {
			/* Set arp offlad */
			if (!rwnx_hw->filter.arp_offload[vif_index]) {
				ret = bes_filter_manager_set_arp_offload(rwnx_hw, rwnx_vif, ifa->ifa_local);
				if (ret)
					break;
			}

			/* Add signal test filter */
			ret = bes_signal_test_set_filter(rwnx_hw, rwnx_vif);
			if (ret)
				break;

			/* Update the conn state of STA */
			bes_conn_state_process(rwnx_vif, rwnx_sta, STA_CONN_ACTION_GET_IP);
		} while(0);
	} else {
		ret = bes_filter_manager_clear_arp_offload(rwnx_hw, rwnx_vif);
	}
	bes_err_with_cond(ret, BES_DBG_FILTER_MANAGER,
			   "%s ERROR(%d): fail to add/delete arp offload function. vif: %d, ip:%pI4\n",
			   __func__, ret, vif_index, &rwnx_vif->ipv4_addr);

	return ret;
}
#endif

#if IS_ENABLED(CONFIG_IPV6)
static int rwnx_inet6addr_changed(struct notifier_block *nb, unsigned long event, void *arg)
{
	int ret = 0;
	struct rwnx_hw *rwnx_hw = container_of(nb, struct rwnx_hw, inet6addr_notifier);
	struct inet6_ifaddr *ifa = (struct inet6_ifaddr *)arg;
	struct net_device *ndev = ifa->idev->dev;
	struct rwnx_vif *rwnx_vif = netdev_priv(ndev);
	struct wireless_dev *wdev = ndev->ieee80211_ptr;
	u8 vif_index;

	/* Make sure it's our interface that got changed and wifi is on */
	if (!wdev || !rwnx_hw || wdev->wiphy != rwnx_hw->wiphy ||
	    !rwnx_vif || !rwnx_vif->up)
		return NOTIFY_DONE;

	/* Only care about NETDEV_DOWN and NETDEV_UP event */
	if (event != NETDEV_DOWN && event != NETDEV_UP)
		return NOTIFY_DONE;

	/* Only support sta or p2p gc */
	if (RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_STATION &&
	    RWNX_VIF_TYPE(rwnx_vif) != NL80211_IFTYPE_P2P_CLIENT)
		return NOTIFY_DONE;

	/* Check the validity of vif_index */
	vif_index = rwnx_vif->vif_index;
	if (vif_index >= NX_ITF_MAX) {
		bes_err(BES_DBG_LINK, "%s, vif_index %u invalid\n", __func__, vif_index);
		return NOTIFY_DONE;
	}

	/* Avoiding duplicate clearing ns offload filter */
	if ((!rwnx_vif_is_connected(rwnx_vif) || event == NETDEV_DOWN) &&
	    !rwnx_hw->filter.ns_offload[vif_index])
		return NOTIFY_DONE;

	/* Add/Delete ns offload function according to connection status and event */
	if (rwnx_vif_is_connected(rwnx_vif) && (event == NETDEV_UP)) {
		if (!rwnx_hw->filter.ns_offload[vif_index])
			ret = bes_filter_manager_set_ns_offload(rwnx_hw, rwnx_vif, &ifa->addr);
	} else {
		ret = bes_filter_manager_clear_ns_offload(rwnx_hw, rwnx_vif);
	}
	bes_err_with_cond(ret, BES_DBG_FILTER_MANAGER,
			   "%s ERROR(%d): fail to add/delete ns offload function. vif: %d, ipv6 addr: %pI6\n",
			   __func__, ret, vif_index, &ifa->addr);

	return ret;
}
#endif

#ifdef CONFIG_PM
static int rwnx_system_pm_notifier(struct notifier_block *notifier,
				   unsigned long pm_event,
				   void *unused)
{
	struct rwnx_hw *rwnx_hw = container_of(notifier, struct rwnx_hw, pm_notify);

	switch (pm_event) {
	case PM_POST_RESTORE:
	case PM_POST_HIBERNATION:
	case PM_POST_SUSPEND:
		rwnx_send_system_state_req(rwnx_hw);
		bes_auto_set_fw_log(rwnx_hw, AUTO_LMAC_LOG_RESUME_STATUS);
		break;
	case PM_RESTORE_PREPARE:
	case PM_HIBERNATION_PREPARE:
	case PM_SUSPEND_PREPARE:
		bes_auto_set_fw_log(rwnx_hw, AUTO_LMAC_LOG_SUSPEND_STATUS);
		break;
	default:
		break;
	}

	return NOTIFY_DONE;
}
#endif

int rwnx_cfg80211_preinit(struct rwnx_plat *rwnx_plat)
{
	struct wiphy *wiphy = NULL;
	struct rwnx_hw *rwnx_hw = NULL;

	if (!rwnx_plat->pre_alloc)
		return 0;

	wiphy = wiphy_new(&rwnx_cfg80211_ops, sizeof(struct rwnx_hw));
	if (!wiphy)
		return -ENOMEM;

	rwnx_hw = wiphy_priv(wiphy);
	rwnx_plat->drvdata = rwnx_hw;
	rwnx_hw->wiphy = wiphy;

	return 0;
}

/**
 *
 */
int rwnx_cfg80211_init(struct rwnx_plat *rwnx_plat, void **platform_data)
{
	struct rwnx_hw *rwnx_hw;
	int ret = 0;
	struct wiphy *wiphy = NULL;
	struct wireless_dev *wdev;
#if defined(CONFIG_WIFI_CALI) && \
    (defined(CONFIG_RWNX_PCIE) || defined(CONFIG_BES_IBUS))
	struct mcu_wf_cali_req cali_req;
#endif
#ifdef CONFIG_BES_P2P_MULTIVIF
	struct wireless_dev *p2p_wdev;
#endif
#if !defined(CONFIG_RWNX_PCIE) && !defined(CONFIG_BES_IBUS)
	struct mcu_buf_cnt_cfm buf_cfm = {0};
#endif
	int i;

	bes_dbg(BES_DBG_INIT, RWNX_FN_ENTRY_STR);

	/* Create a new wiphy for use with cfg80211 */
	if (!rwnx_plat->pre_alloc) {
		wiphy = wiphy_new(&rwnx_cfg80211_ops, sizeof(struct rwnx_hw));
	} else if (rwnx_plat->drvdata) {
		wiphy = ((struct rwnx_hw *)rwnx_plat->drvdata)->wiphy;
	}

	if (!wiphy) {
		dev_err(rwnx_platform_get_dev(rwnx_plat), "Failed to create new wiphy\n");
		ret = -ENOMEM;
		goto err_out;
	}

	rwnx_hw = wiphy_priv(wiphy);
	memset(rwnx_hw, 0, sizeof(struct rwnx_hw));
	rwnx_hw->wiphy = wiphy;
	rwnx_hw->plat = rwnx_plat;
	rwnx_plat->drvdata = rwnx_hw;
	rwnx_hw->dev = rwnx_platform_get_dev(rwnx_plat);
	rwnx_hw->mod_params = &rwnx_mod_params;
	rwnx_hw->tcp_pacing_shift = 0;

	rwnx_hw->tx_thread_in = 0;
	rwnx_hw->tx_thread_out = 0;
	rwnx_hw->data_push_start = 0;
	rwnx_hw->data_push_stop = 0;
	rwnx_hw->msg_push_start = 0;
	rwnx_hw->msg_push_stop = 0;

	rwnx_hw->obss_narrow.cfg_table = narrow_bw_ru_table;
	spin_lock_init(&rwnx_hw->obss_narrow.lock);
	spin_lock(&rwnx_hw->obss_narrow.lock);
	rwnx_hw->obss_narrow.state = OBSS_NARROW_RU_UNPROCTED;
	rwnx_hw->obss_narrow.acpt_all_beacon = false;
	spin_unlock(&rwnx_hw->obss_narrow.lock);

#ifdef CONFIG_INET
	rwnx_hw->inetaddr_notifier.notifier_call = rwnx_inetaddr_changed;
	ret = register_inetaddr_notifier(&rwnx_hw->inetaddr_notifier);
	bes_err_with_cond(ret, BES_DBG_INIT, "%s inetaddr notifier register err", __func__);
	if (ret)
		goto err_register_inet;
#endif

#if IS_ENABLED(CONFIG_IPV6)
	rwnx_hw->inet6addr_notifier.notifier_call = rwnx_inet6addr_changed;
	ret = register_inet6addr_notifier(&rwnx_hw->inet6addr_notifier);
	bes_err_with_cond(ret, BES_DBG_INIT, "%s inet6addr notifier register err", __func__);
	if (ret)
		goto err_register_ipv6;
#endif
	/* init channel context count */
	atomic_set(&rwnx_hw->chanctx_count, 0);

	/* set device pointer for wiphy */
	set_wiphy_dev(wiphy, rwnx_hw->dev);

	/* Create cache to allocate sw_txhdr */
	rwnx_hw->sw_txhdr_cache = KMEM_CACHE(rwnx_sw_txhdr, SLAB_HWCACHE_ALIGN);
	if (!rwnx_hw->sw_txhdr_cache) {
		wiphy_err(wiphy, "Cannot allocate cache for sw TX header\n");
		ret = -ENOMEM;
		goto err_cache;
	}

	rwnx_hw->vif_started = 0;
	rwnx_hw->monitor_vif = RWNX_INVALID_VIF;

	rwnx_hw->scan_ie.addr = NULL;

	for (i = 0; i < NX_ITF_MAX; i++)
		rwnx_hw->avail_idx_map |= BIT(i);

	rwnx_hwq_init(rwnx_hw);
	rwnx_txq_prepare(rwnx_hw);

	rwnx_mu_group_init(rwnx_hw);

	rwnx_hw->roc = NULL;
	init_waitqueue_head(&rwnx_hw->roc_wait_wq);
	spin_lock_init(&rwnx_hw->roc_lock);
	init_waitqueue_head(&rwnx_hw->wf_conf_wait_q);
	sema_init(&rwnx_hw->phy_op_sema, 1);

	bes_dbac_cali_init(rwnx_hw);

	bes_sched_scan_init(rwnx_hw);

	bes_wpa_priv_cmd_init(rwnx_hw);

	ret = bes_data_monitor_init(rwnx_hw);
	if (ret)
		goto err_traffic;

	bes_tp_dbg_init(rwnx_hw);

	wiphy->mgmt_stypes = rwnx_default_mgmt_stypes;

	/* Initialize rwnx_band_2GHz and rwnx_band_5GHz */
	ret = rwnx_band_init();
	if (ret)
		goto err_band_init;

	wiphy->bands[NL80211_BAND_2GHZ] = &rwnx_band_2GHz;
	wiphy->bands[NL80211_BAND_5GHZ] = &rwnx_band_5GHz;
	wiphy->interface_modes =
		BIT(NL80211_IFTYPE_STATION)     |
		BIT(NL80211_IFTYPE_AP)          |
		BIT(NL80211_IFTYPE_AP_VLAN)     |
	   	BIT(NL80211_IFTYPE_P2P_CLIENT)  |
	   	BIT(NL80211_IFTYPE_P2P_GO)      |
		BIT(NL80211_IFTYPE_MONITOR);

	wiphy->flags |= WIPHY_FLAG_HAS_REMAIN_ON_CHANNEL |
		WIPHY_FLAG_HAS_CHANNEL_SWITCH |
		WIPHY_FLAG_4ADDR_STATION |
		WIPHY_FLAG_4ADDR_AP |
		WIPHY_FLAG_REPORTS_OBSS |
		WIPHY_FLAG_OFFCHAN_TX;

	wiphy->max_scan_ssids = SCAN_SSID_MAX;
	wiphy->max_scan_ie_len = SCANU_MAX_IE_LEN;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 1, 0)
	wiphy->support_mbssid = 1;
#endif

	wiphy->max_num_csa_counters = BCN_MAX_CSA_CPT;

	wiphy->max_remain_on_channel_duration = rwnx_hw->mod_params->roc_dur_max;

	wiphy->features |= NL80211_FEATURE_NEED_OBSS_SCAN | NL80211_FEATURE_SK_TX_STATUS | NL80211_FEATURE_VIF_TXPOWER |
			   NL80211_FEATURE_ACTIVE_MONITOR | NL80211_FEATURE_AP_MODE_CHAN_WIDTH_CHANGE;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 17, 0)
	wiphy->features |= NL80211_FEATURE_SAE;
#endif

	wiphy->iface_combinations = rwnx_combinations;
	/* -1 not to include combination with radar detection, will be re-added in
	   rwnx_handle_dynparams if supported */
	wiphy->n_iface_combinations = ARRAY_SIZE(rwnx_combinations) - 1;
	wiphy->reg_notifier = rwnx_reg_notifier;

	wiphy->signal_type = CFG80211_SIGNAL_TYPE_MBM;

	wiphy->cipher_suites = cipher_suites;
	wiphy->n_cipher_suites = ARRAY_SIZE(cipher_suites) - NB_RESERVED_CIPHER;

	rwnx_hw->ext_capa[0] = WLAN_EXT_CAPA1_EXT_CHANNEL_SWITCHING;
	rwnx_hw->ext_capa[2] = WLAN_EXT_CAPA3_MULTI_BSSID_SUPPORT;
	rwnx_hw->ext_capa[7] = WLAN_EXT_CAPA8_OPMODE_NOTIF;

	memcpy(rwnx_hw->ext_capa_mask, rwnx_hw->ext_capa, ARRAY_SIZE(rwnx_hw->ext_capa));
	rwnx_hw->ext_capa_mask[2] |= WLAN_EXT_CAPA3_WNM_SLEEP_MODE;
	rwnx_hw->ext_capa_mask[5] |= WLAN_EXT_CAPA6_WNM_NOTIFCATION;
	rwnx_hw->ext_capa_mask[9] |= WLAN_EXT_CAPA10_CAPAB_FILS;

	wiphy->extended_capabilities = rwnx_hw->ext_capa;
	wiphy->extended_capabilities_mask = rwnx_hw->ext_capa_mask;
	wiphy->extended_capabilities_len = ARRAY_SIZE(rwnx_hw->ext_capa);

	bes_set_vendor_command(wiphy);

#ifdef CONFIG_PM
	wiphy->wowlan = &rwnx_wowlan_support;
#endif

#if defined(CONFIG_RWNX_PCIE) || defined(CONFIG_BES_IBUS)
	tasklet_init(&rwnx_hw->task, rwnx_task, (unsigned long)rwnx_hw);
#endif
	INIT_LIST_HEAD(&rwnx_hw->vifs);
	INIT_LIST_HEAD(&rwnx_hw->deferred_e2amsg_list.list);
	rwnx_hw->rwnx_wq = alloc_workqueue("rwnx_wq", __WQ_LEGACY | WQ_MEM_RECLAIM, 0);
	if (!rwnx_hw->rwnx_wq)
		goto err_msg_q;
	INIT_WORK(&(rwnx_hw->plat->deferred_e2amsg_work), rwnx_defer_msg_work);
	INIT_DELAYED_WORK(&rwnx_hw->obss_narrow.filter_work, rwnx_narrow_ru_filter_work);
	mutex_init(&rwnx_hw->dbgdump.mutex);
	spin_lock_init(&rwnx_hw->tx_lock);
	spin_lock_init(&rwnx_hw->cb_lock);
	bes_wlan_power_ctrl_init(rwnx_hw);
	bes_csi_cap_init(rwnx_hw);

#ifdef CONFIG_PM
	rwnx_hw->pm_notify.notifier_call = rwnx_system_pm_notifier;
	register_pm_notifier(&rwnx_hw->pm_notify);
#endif

	if ((ret = rwnx_platform_on(rwnx_hw, NULL)))
		goto err_platon;

#ifdef CONFIG_DPD_CALI_DATA_SAVE
	/* send dpd cail data before reset fw */
	if (bes_check_dpd_cali_data(rwnx_hw) &&
	   (ret = bes_send_dpd_cail_data(rwnx_hw)))
		goto err_lmac_reqs;
#endif

	/* Reset FW */
	if ((ret = rwnx_send_reset(rwnx_hw)))
		goto err_lmac_reqs;

	/* Get parameters from configuration file */
	if (!rwnx_parse_configfile(rwnx_hw, RWNX_CONFIG_FW_NAME, &rwnx_hw->conf_file)) {
		/* Apply the parameters in the configuration file */
		rwnx_apply_cfg_params(rwnx_hw, &rwnx_hw->conf_file);
	} else {
		/* Default config value */
		rwnx_apply_default_params(rwnx_hw);
	}

	/* Modify the parameters according to the device id */
	rwnx_cfg_params_check(bes_sysfs_get_device_id());

	/* Update fw switch status */
	bes_set_wlan_enabled(rwnx_hw, true);

#if (defined(CONFIG_RWNX_PCIE) || defined(CONFIG_BES_IBUS))
	memset(&cali_req, 0, sizeof(cali_req));
	cali_req.step = BES_MCU_WIFI_CALI_RX;
	if ((ret = bes_send_wf_cali_req(rwnx_hw, &cali_req, NULL)) < 0)
		goto err_lmac_reqs;
#endif

#ifdef CONFIG_DPD_CALI_DATA_SAVE
	if (!bes_check_dpd_cali_data(rwnx_hw) &&
	   (ret = bes_get_dpd_cail_data(rwnx_hw)))
		goto err_lmac_reqs;
#endif

	if ((ret = rwnx_send_version_req(rwnx_hw, &rwnx_hw->version_cfm)))
		goto err_lmac_reqs;
	rwnx_set_vers(rwnx_hw);

#if !defined(CONFIG_RWNX_PCIE) && !defined(CONFIG_BES_IBUS)
	if ((ret = bes_send_mcu_buf_cnt_req(rwnx_hw, &buf_cfm))) {
		goto err_lmac_reqs;
	}
	bes_info(BES_DBG_INIT, "init buf_tx %d\n", buf_cfm.rxbuf_cnt);
	atomic_set(&rwnx_hw->plat->buf_tx, buf_cfm.rxbuf_cnt);

	/* Adjusting the scheduling cycle of hwq to avoid tx_queue starvation. */
	rwnx_hwq_update_credits(rwnx_hw, buf_cfm.rxbuf_cnt);
#endif

	if ((ret = rwnx_handle_dynparams(rwnx_hw, rwnx_hw->wiphy)))
		goto err_lmac_reqs;

	rwnx_enable_mesh(rwnx_hw);
	rwnx_radar_detection_init(&rwnx_hw->radar);

	// Set coex parameters
	bes_coex_arb_init(rwnx_hw, BES_COEX_HW_MODE);

	/* Set parameters to firmware */
	rwnx_send_me_config_req(rwnx_hw);

	/* Only monitor mode supported when custom channels are enabled */
	if (rwnx_mod_params.custchan) {
		rwnx_limits[0].types = BIT(NL80211_IFTYPE_MONITOR);
		rwnx_limits_dfs[0].types = BIT(NL80211_IFTYPE_MONITOR);
	}

	if ((ret = wiphy_register(wiphy))) {
		wiphy_err(wiphy, "Could not register wiphy device\n");
		goto err_register_wiphy;
	}

	/* Work to defer processing of rx buffer */
	INIT_WORK(&rwnx_hw->defer_rx.work, rwnx_rx_deferred);
	skb_queue_head_init(&rwnx_hw->defer_rx.sk_list);

	/* Update regulatory (if needed) and set channel parameters to firmware
	   (must be done after WiPHY registration) */
	rwnx_custregd(rwnx_hw, wiphy);
	rwnx_send_me_chan_config_req(rwnx_hw);
	ret = rwnx_send_country_code_cfg(rwnx_hw);
	if (ret) {
		wiphy_err(wiphy, "set country code to mcu faied %d.\n", ret);
	}

	if (platform_data) {
		*platform_data = rwnx_hw;
	}

	if ((ret = rwnx_dbgfs_register(rwnx_hw, "rwnx"))) {
		wiphy_err(wiphy, "Failed to register debugfs entries");
		goto err_debugfs;
	}

	/**
	 * Before this, it is allowed to change wifi or bt power status
	 * place it before add interface
	 */
	bes_set_boot_wait_state(BES_BOOT_WAIT_PROBE_DONE);
	/* do not add interface when wifi power off */
	if (!bes_power_ctrl_wifi_enabled())
		return 0;

	rtnl_lock();
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0))
	wiphy_lock(rwnx_hw->wiphy);
#endif
	/* close fw, and after this, messages should not be sent to FW */
	bes_power_ctrl_schedule_wlan_close(rwnx_hw);

	/* Add an initial interface */
	wdev = rwnx_interface_add(rwnx_hw, "wlan%d", NET_NAME_UNKNOWN,
				rwnx_mod_params.custchan ? NL80211_IFTYPE_MONITOR : NL80211_IFTYPE_STATION,
				NULL);

#ifdef CONFIG_BES_P2P_MULTIVIF
	p2p_wdev = rwnx_interface_add(rwnx_hw, "p2p%d", NET_NAME_UNKNOWN,
				NL80211_IFTYPE_STATION, NULL);
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 12, 0))
	wiphy_unlock(rwnx_hw->wiphy);
#endif
	rtnl_unlock();

	if (!wdev) {
		wiphy_err(wiphy, "Failed to instantiate a network device\n");
		ret = -ENOMEM;
		goto err_add_interface;
	}

#ifdef CONFIG_BES_P2P_MULTIVIF
	if (!p2p_wdev) {
		wiphy_err(wiphy, "Failed to instantiate a p2p device\n");
		ret = -ENOMEM;
		goto err_add_interface;
	}
#endif

	bes_sysfs_set_hw(rwnx_hw);

	wiphy_info(wiphy, "New interface create %s", wdev->netdev->name);

	if (!bes_sysfs_is_signal_mode()) {
		rwnx_wf_status_set(rwnx_hw, RWNX_DEV_STARTED);
		bes_info(BES_DBG_INIT, "non_signaling mode start\n");
	}

	bes_fw_fifo_dump_init(rwnx_hw, DEFAULT_FW_LOG_BUFFER_SIZE, (FW_LOG_MCU_UPLOAD | FW_LOG_LMAC_UPLOAD));

#if defined(CONFIG_RWNX_PCIE) || defined(CONFIG_BES_IBUS)
	bes_pwr_clear_busy_event(rwnx_hw, RWNX_INVALID_VIF, PHY_CONST_EVENT_INIT);
#endif

	return 0;

err_add_interface:
	bes_power_ctrl_cancel_wlan_close_work(rwnx_hw);
	rwnx_dbgfs_unregister(rwnx_hw);
err_debugfs:
	wiphy_unregister(rwnx_hw->wiphy);
err_register_wiphy:
err_lmac_reqs:
	rwnx_fw_trace_dump(rwnx_hw);
	rwnx_platform_off(rwnx_hw, NULL);
err_platon:
	if (rwnx_hw->rwnx_wq)
		destroy_workqueue(rwnx_hw->rwnx_wq);
#ifdef CONFIG_PM
	unregister_pm_notifier(&rwnx_hw->pm_notify);
#endif
err_msg_q:
	rwnx_band_deinit();
err_band_init:
	bes_tp_dbg_deinit(rwnx_hw);
	bes_data_monitor_deinit(rwnx_hw);
err_traffic:
	bes_sched_scan_deinit(rwnx_hw);
	kmem_cache_destroy(rwnx_hw->sw_txhdr_cache);
err_cache:
#if IS_ENABLED(CONFIG_IPV6)
	unregister_inet6addr_notifier(&rwnx_hw->inet6addr_notifier);
err_register_ipv6:
#endif
#ifdef CONFIG_INET
	unregister_inetaddr_notifier(&rwnx_hw->inetaddr_notifier);
err_register_inet:
#endif
	if (platform_data)
		*platform_data = NULL;

	if (!rwnx_plat->pre_alloc) {
		rwnx_hw->plat = NULL;
		rwnx_plat->drvdata = NULL;
		wiphy_free(wiphy);
	}

err_out:
	return ret;
}

/**
 *
 */
void rwnx_cfg80211_deinit(struct rwnx_hw *rwnx_hw)
{
	bes_dbg(BES_DBG_INIT, RWNX_FN_ENTRY_STR);

	bes_coex_arb_deinit(rwnx_hw);
	bes_fw_fifo_dump_deinit(rwnx_hw);
	rwnx_dbgfs_unregister(rwnx_hw);
	atomic_set(&rwnx_hw->txq_cleanup_available, 0);
	del_timer_sync(&rwnx_hw->txq_cleanup);
	rwnx_wdev_unregister(rwnx_hw);

	bes_vendor_command_detach(rwnx_hw->wiphy);
	/* the unregister operation related to wiphy is completed before this */
	wiphy_unregister(rwnx_hw->wiphy);
	rwnx_radar_detection_deinit(&rwnx_hw->radar);

	bes_sched_scan_deinit(rwnx_hw);

	bes_tp_dbg_deinit(rwnx_hw);
	bes_data_monitor_deinit(rwnx_hw);

	rwnx_platform_off(rwnx_hw, NULL);
	kmem_cache_destroy(rwnx_hw->sw_txhdr_cache);
	bes_sysfs_set_hw(NULL);

#ifdef CONFIG_INET
	unregister_inetaddr_notifier(&rwnx_hw->inetaddr_notifier);
#endif

#if IS_ENABLED(CONFIG_IPV6)
	unregister_inet6addr_notifier(&rwnx_hw->inet6addr_notifier);
#endif

#ifdef CONFIG_PM
	unregister_pm_notifier(&rwnx_hw->pm_notify);
#endif

	/* keep last */
	if (!rwnx_hw->plat->pre_alloc) {
		rwnx_band_deinit();
		wiphy_free(rwnx_hw->wiphy);
	} else {
		rwnx_hw->wiphy->wowlan_config = NULL;
#if (LINUX_VERSION_CODE <= KERNEL_VERSION(6, 4, 3))
		rwnx_hw->wiphy->regulatory_flags &= ~REGULATORY_IGNORE_STALE_KICKOFF;
#endif
		rwnx_hw->wiphy->regulatory_flags &= ~REGULATORY_WIPHY_SELF_MANAGED;
	}
}

void rwnx_cfg80211_post_deinit(struct rwnx_plat *rwnx_plat)
{
	struct wiphy *wiphy = NULL;
	struct rwnx_hw *rwnx_hw = NULL;

	if (rwnx_plat->pre_alloc) {
		rwnx_hw = (struct rwnx_hw *)rwnx_plat->drvdata;

		rwnx_band_deinit();

		if (rwnx_hw)
			wiphy = rwnx_hw->wiphy;

		if (wiphy)
			wiphy_free(wiphy);
	}
}

/**
 *
 */
static int __init rwnx_mod_init(void)
{
	bes_dbg(BES_DBG_INIT, RWNX_FN_ENTRY_STR);
	rwnx_print_version();
#ifdef CONFIG_OPLUS_PLATFORM
	oplus_wlan_log_init(); // oplus
#endif
	return rwnx_platform_register_drv();
}

/**
 *
 */
static void __exit rwnx_mod_exit(void)
{
	bes_dbg(BES_DBG_INIT, RWNX_FN_ENTRY_STR);

	bes_platform_unregister_drv();
#ifdef CONFIG_OPLUS_PLATFORM
	oplus_wlan_log_deinit(); // oplus
#endif
}

module_init(rwnx_mod_init);
module_exit(rwnx_mod_exit);

MODULE_FIRMWARE(RWNX_CONFIG_FW_NAME);

MODULE_DESCRIPTION(RW_DRV_DESCRIPTION);
MODULE_VERSION(RWNX_VERS_MOD);
MODULE_AUTHOR(RW_DRV_COPYRIGHT " " RW_DRV_AUTHOR);
MODULE_LICENSE("GPL");
#if defined(PLAT_SIGMASTAR)
MODULE_IMPORT_NS(VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver);
#endif
