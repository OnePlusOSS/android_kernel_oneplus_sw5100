/**
 ******************************************************************************
 *
 * @file rwnx_defs.h
 *
 * @brief Main driver structure declarations for fullmac driver
 *
 * Copyright (C) RivieraWaves 2012-2021
 *
 ******************************************************************************
 */

#ifndef _RWNX_DEFS_H_
#define _RWNX_DEFS_H_

#include <linux/device.h>
#include <linux/dmapool.h>
#include <linux/interrupt.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <net/cfg80211.h>
#include <linux/kfifo.h>

#include "ipc_host.h"
#include "rwnx_compat.h"
#include "rwnx_cmds.h"
#include "rwnx_debugfs.h"
#include "rwnx_mod_params.h"
#include "rwnx_mu_group.h"
#include "rwnx_platform.h"
#include "rwnx_radar.h"
#include "rwnx_rx.h"
#include "rwnx_tx.h"
#include "rwnx_utils.h"
#include "bes_power_ctrl.h"
#include "bes_cfgvendor.h"
#include "bes_filter_manager.h"
#include "bes_fw_log_fifo_dump.h"
#include "lmac_log_ctrl.h"
#include "bes_coex.h"
#include "bes_common.h"
#include "rwnx_cfgfile.h"
#include "bes_regd.h"
#include "bes_csi.h"

#define LEGACY_PS_ID 0
#define UAPSD_ID 1

#define PS_SP_INTERRUPTED 255

// Maximum number of AP_VLAN interfaces allowed.
// At max we can have one AP_VLAN per station, but we also limit the
// maximum number of interface to 16 (to fit in avail_idx_map)
#define MAX_AP_VLAN_ITF (((16 - NX_VIRT_DEV_MAX) > NX_REMOTE_STA_MAX) ? NX_REMOTE_STA_MAX : (16 - NX_VIRT_DEV_MAX))

// Maximum number of interface at driver level
#define NX_ITF_MAX (NX_VIRT_DEV_MAX + MAX_AP_VLAN_ITF)

#define MAX_COEXIST_CHAN_CTXT		2
#define RWNX_MSG_MAX_LEN sizeof(struct ipc_a2e_msg) // @see driver tx cmd buf size

/* Index of the Remain on Channel Context */
#define CHAN_ROC_CTXT_IDX (NX_CHAN_CTXT_CNT + 1)

#ifdef ADPT_GKI_KERNEL
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 15, 81))
	#define RWNX_CFG80211_STA_PARA(params, member) params->link_sta_params.member
#else
	#define RWNX_CFG80211_STA_PARA(params, member) params->member
#endif
#else /* ADPT_GKI_KERNEL */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 0, 0))
	#define RWNX_CFG80211_STA_PARA(params, member) params->link_sta_params.member
#else
	#define RWNX_CFG80211_STA_PARA(params, member) params->member
#endif
#endif /* ADPT_GKI_KERNEL */

/* Extend element id for owe dh */
#define WLAN_EID_EXT_OWE_DH	32

#define BES_ULDELAY_LENGTH 5

#define WLAN_EXT_CAPA3_WNM_SLEEP_MODE	BIT(1)
#define WLAN_EXT_CAPA6_WNM_NOTIFCATION	BIT(6)
#define WLAN_EXT_CAPA10_CAPAB_FILS	BIT(0)

/**
 * enum rwnx_ieee80211_wnm_action_code - WNM Action field values
 * @RWNX_WNM_EVENT_REQ: wnm event req.
 * @RWNX_WNM_EVENT_REPORT: wnm event report.
 * @RWNX_WNM_DIAGNOSTIC_REQ: wnm diagnostic req.
 * @RWNX_WNM_DIAGNOSTIC_REPORT: wnm diagnostic report.
 * @RWNX_WNM_LOCATION_CFG_REQ: wnm location cfg req.
 * @RWNX_WNM_LOCATION_CFG_RESP: wnm location cfg resp.
 * @RWNX_WNM_BSS_TRANS_MGMT_QUERY: wnm bss trans query.
 * @RWNX_WNM_BSS_TRANS_MGMT_REQ: wnm bss trans req.
 * @RWNX_WNM_BSS_TRANS_MGMT_RESP: wnm bss trans resp.
 * @RWNX_WNM_FMS_REQ: wnm fms req.
 * @RWNX_WNM_FMS_RESP: wnm fms resp.
 * @RWNX_WNM_COLLOCATED_INTERFERENCE_REQ: wnm collocated interference req.
 * @RWNX_WNM_COLLOCATED_INTERFERENCE_REPORT: collocated interference report.
 * @RWNX_WNM_TFS_REQ: wnm tfs req.
 * @RWNX_WNM_TFS_RESP: wnm tfs resp.
 * @RWNX_WNM_TFS_NOTIFY: wnm tfs notify.
 * @RWNX_WNM_SLEEP_MODE_REQ: wnm sleep mode req.
 * @RWNX_WNM_SLEEP_MODE_RESP: wnm sleep mode resp.
 * @RWNX_WNM_TIM_BROADCAST_REQ: wnm tim broadcast req.
 * @RWNX_WNM_TIM_BROADCAST_RESP: wnm tim broadcast resp.
 * @RWNX_WNM_QOS_TRAFFIC_CAPAB_UPDATE: wnm qos traffic capab update.
 * @RWNX_WNM_CHANNEL_USAGE_REQ: wnm channel usage req.
 * @RWNX_WNM_CHANNEL_USAGE_RESP: wnm channel usage resp.
 * @RWNX_WNM_DMS_REQ: wnm dms req.
 * @RWNX_WNM_DMS_RESP: wnm dms resp.
 * @RWNX_WNM_TIMING_MEASUREMENT_REQ: wnm timing measurement req.
 * @RWNX_WNM_NOTIFICATION_REQ: wnm notification req.
 * @RWNX_WNM_NOTIFICATION_RESP: wnm notification resp.
*/
enum rwnx_ieee80211_wnm_action_code {
	RWNX_WNM_EVENT_REQ = 0,
	RWNX_WNM_EVENT_REPORT = 1,
	RWNX_WNM_DIAGNOSTIC_REQ = 2,
	RWNX_WNM_DIAGNOSTIC_REPORT = 3,
	RWNX_WNM_LOCATION_CFG_REQ = 4,
	RWNX_WNM_LOCATION_CFG_RESP = 5,
	RWNX_WNM_BSS_TRANS_MGMT_QUERY = 6,
	RWNX_WNM_BSS_TRANS_MGMT_REQ = 7,
	RWNX_WNM_BSS_TRANS_MGMT_RESP = 8,
	RWNX_WNM_FMS_REQ = 9,
	RWNX_WNM_FMS_RESP = 10,
	RWNX_WNM_COLLOCATED_INTERFERENCE_REQ = 11,
	RWNX_WNM_COLLOCATED_INTERFERENCE_REPORT = 12,
	RWNX_WNM_TFS_REQ = 13,
	RWNX_WNM_TFS_RESP = 14,
	RWNX_WNM_TFS_NOTIFY = 15,
	RWNX_WNM_SLEEP_MODE_REQ = 16,
	RWNX_WNM_SLEEP_MODE_RESP = 17,
	RWNX_WNM_TIM_BROADCAST_REQ = 18,
	RWNX_WNM_TIM_BROADCAST_RESP = 19,
	RWNX_WNM_QOS_TRAFFIC_CAPAB_UPDATE = 20,
	RWNX_WNM_CHANNEL_USAGE_REQ = 21,
	RWNX_WNM_CHANNEL_USAGE_RESP = 22,
	RWNX_WNM_DMS_REQ = 23,
	RWNX_WNM_DMS_RESP = 24,
	RWNX_WNM_TIMING_MEASUREMENT_REQ = 25,
	RWNX_WNM_NOTIFICATION_REQ = 26,
	RWNX_WNM_NOTIFICATION_RESP = 27
};

/**
 * enum rwnx_bss_trans_mgmt_status_code - WNM Status field values
 * @RWNX_WNM_BSS_TM_ACCEPT: acccept the wnm btm request.
 * @RWNX_WNM_BSS_TM_REJECT_UNSPECIFIED: reject the wnm btm request because of unspecified.
 * @RWNX_WNM_BSS_TM_REJECT_INSUFFICIENT_BEACON: reject the wnm btm request because of insufficient beacon.
 * @RWNX_WNM_BSS_TM_REJECT_INSUFFICIENT_CAPABITY: reject the wnm btm request because of insufficient capabity.
 * @RWNX_WNM_BSS_TM_REJECT_UNDESIRED: reject the wnm btm request because of undesired.
 * @RWNX_WNM_BSS_TM_REJECT_DELAY_REQUEST: reject the wnm btm request because of delay request.
 * @RWNX_WNM_BSS_TM_REJECT_STA_CANDIDATE_LIST_PROVIDED: reject the wnm btm request because of sta candidate list provided.
 * @RWNX_WNM_BSS_TM_REJECT_NO_SUITABLE_CANDIDATES: reject the wnm btm request because of no suitable candidates.
 * @RWNX_WNM_BSS_TM_REJECT_LEAVING_ESS: reject the wnm btm request because of leaving ess.
*/
enum rwnx_bss_trans_mgmt_status_code {
	RWNX_WNM_BSS_TM_ACCEPT = 0,
	RWNX_WNM_BSS_TM_REJECT_UNSPECIFIED = 1,
	RWNX_WNM_BSS_TM_REJECT_INSUFFICIENT_BEACON = 2,
	RWNX_WNM_BSS_TM_REJECT_INSUFFICIENT_CAPABITY = 3,
	RWNX_WNM_BSS_TM_REJECT_UNDESIRED = 4,
	RWNX_WNM_BSS_TM_REJECT_DELAY_REQUEST = 5,
	RWNX_WNM_BSS_TM_REJECT_STA_CANDIDATE_LIST_PROVIDED = 6,
	RWNX_WNM_BSS_TM_REJECT_NO_SUITABLE_CANDIDATES = 7,
	RWNX_WNM_BSS_TM_REJECT_LEAVING_ESS = 8
};


/**
 * struct timeout_interval_ie - Information of the timeout interval ie
 *
 * @id: element id
 * @length: length of timeout_interval_info
 * @timeout_interval_info: timeout interval type and timeout interval value
 */
struct timeout_interval_ie {
	u8 id;
	u8 length;
	struct ieee80211_timeout_interval_ie timeout_interval_info;
};

/**
 * struct rwnx_bcn - Information of the beacon in used (AP mode)
 *
 * @head: head portion of beacon (before TIM IE)
 * @tail: tail portion of beacon (after TIM IE)
 * @ies: extra IEs (not used ?)
 * @head_len: length of head data
 * @tail_len: length of tail data
 * @ies_len: length of extra IEs data
 * @tim_len: length of TIM IE
 * @len: Total beacon len (head + tim + tail + extra)
 * @dtim: dtim period
 */
struct rwnx_bcn {
	u8 *head;
	u8 *tail;
	u8 *ies;
	size_t head_len;
	size_t tail_len;
	size_t ies_len;
	size_t tim_len;
	size_t len;
	u8 dtim;
};

/**
 * struct stored_key_param - Stored key param
 *
 * @key: Pointer of stored key
 * @key_len: Length of stored key
 * @key_idx: Index of stored key
 * @cipher_suite: Cipher suite related to stored key
 */
struct stored_key_param {
	u8 *key;
	u8 key_len;
	u8 key_idx;
	u8 cipher_suite;
};

/**
 * struct bes_sta_stored_key - Stored key infomation
 *
 * @mutex: Mutex of stored key flow
 * @key_param: Stored key param
 * @defer_install: Flag indicates whether need to trigger delay_send_key work
 * @key_install_defer_work: Work of install delay key after MSG_4 send successfully
 */
struct bes_sta_stored_key {
	struct mutex mutex;
	struct stored_key_param key_param;
	atomic_t defer_install;
	struct work_struct key_install_defer_work;
};

/**
 * struct rwnx_key - Key information
 *
 * @hw_idx: Index of the key from hardware point of view
 * @valid: Indicate if the hw idx is valid
 */
struct rwnx_key {
	u8 hw_idx;
	bool valid;
};

/**
 * struct rwnx_mesh_path - Mesh Path information
 *
 * @list: List element of rwnx_vif.mesh_paths
 * @path_idx: Path index
 * @tgt_mac_addr: MAC Address it the path target
 * @nhop_sta: Next Hop STA in the path
 */
struct rwnx_mesh_path {
	struct list_head list;
	u8 path_idx;
	struct mac_addr tgt_mac_addr;
	struct rwnx_sta *nhop_sta;
};

/**
 * struct rwnx_mesh_path - Mesh Proxy information
 *
 * @list: List element of rwnx_vif.mesh_proxy
 * @ext_sta_addr: Address of the External STA
 * @proxy_addr: Proxy MAC Address
 * @local: Indicate if interface is a proxy for the device
 */
struct rwnx_mesh_proxy {
	struct list_head list;
	struct mac_addr ext_sta_addr;
	struct mac_addr proxy_addr;
	bool local;
};

/**
 * struct rwnx_csa - Information for CSA (Channel Switch Announcement)
 *
 * @vif: Pointer to the vif doing the CSA
 * @bcn: Beacon to use after CSA
 * @buf: IPC buffer to send the new beacon to the fw
 * @chandef: defines the channel to use after the switch
 * @count: Current csa counter
 * @status: Status of the CSA at fw level
 * @ch_idx: Index of the new channel context
 * @work: work scheduled at the end of CSA
 */
struct rwnx_csa {
	struct rwnx_vif *vif;
	struct rwnx_bcn bcn;
	struct rwnx_ipc_buf buf;
	struct cfg80211_chan_def chandef;
	int count;
	int status;
	int ch_idx;
	struct work_struct work;
};

/**
 * enum tdls_status_tag - States of the TDLS link
 *
 * @TDLS_LINK_IDLE: TDLS link is not active (no TDLS peer connected)
 * @TDLS_SETUP_REQ_TX: TDLS Setup Request transmitted
 * @TDLS_SETUP_RSP_TX: TDLS Setup Response transmitted
 * @TDLS_LINK_ACTIVE: TDLS link is active (TDLS peer connected)
 */
enum tdls_status_tag { TDLS_LINK_IDLE, TDLS_SETUP_REQ_TX, TDLS_SETUP_RSP_TX, TDLS_LINK_ACTIVE, TDLS_STATE_MAX };

/**
 * struct rwnx_tdls - Information relative to the TDLS peer
 *
 * @active: Whether TDLS link is active or not
 * @initiator: Whether TDLS peer is the TDLS initiator or not
 * @chsw_en: Whether channel switch is enabled or not
 * @chsw_allowed: Whether TDLS channel switch is allowed or not
 * @last_tid: TID of the latest MPDU transmitted over the TDLS link
 * @last_sn: Sequence number of the latest MPDU transmitted over the TDLS link
 * @ps_on: Whether power save mode is enabled on the TDLS peer or not
 */
struct rwnx_tdls {
	bool active;
	bool initiator;
	bool chsw_en;
	u8 last_tid;
	u16 last_sn;
	bool ps_on;
	bool chsw_allowed;
};

/**
 * enum rwnx_ap_flags - AP flags
 *
 * @RWNX_AP_ISOLATE: Isolate clients (i.e. Don't bridge packets transmitted by
 * one client to another one
 * @RWNX_AP_USER_MESH_PM: Mesh Peering Management is done by user space
 * @RWNX_AP_CREATE_MESH_PATH: A Mesh path is currently being created at fw level
 */
enum rwnx_ap_flags {
	RWNX_AP_ISOLATE = BIT(0),
	RWNX_AP_USER_MESH_PM = BIT(1),
	RWNX_AP_CREATE_MESH_PATH = BIT(2),
};

/**
 * enum rwnx_sta_flags - STATION flags
 *
 * @RWNX_STA_EXT_AUTH: External authentication is in progress
 */
enum rwnx_sta_flags {
	RWNX_STA_EXT_AUTH = BIT(0),
	RWNX_STA_FT_OVER_DS = BIT(1),
	RWNX_STA_FT_OVER_AIR = BIT(2),
	RWNX_STA_AUTO_AUTH = BIT(3),
	RWNX_STA_HAS_KEY = BIT(4),
};
/**
 * enum rwnx_mcu_cali - calibration
 */
enum bes_mcu_cali {
	BES_MCU_WIFI_CALI_NONE = 0,
	BES_MCU_WIFI_CALI_DAC,
	BES_MCU_WIFI_CALI_RX,
	BES_MCU_WIFI_CALI_TX,
	BES_MCU_WIFI_CALI_DYNA,
	BES_MCU_WIFI_CALI_MAX,
};

/**
 * struct bes_vif_status - VIF status statistics
 * @radio_on_start_time: start time
 * @radio_on_with_ps_time: the total time the driver entered low power
 * consumption when establishing a virtual interface.
 * @on_time: msecs the radio is awake (32 bits number accruing over time)
 * @tx_time: msecs the radio is transmitting (32 bits number accruing over time)
 * @rx_time: msecs the radio is in active receive (32 bits number accruing over time)
 * @beacon_rx: number of beacons received.
 * @mgmt_count: number of management frames received.
 * @rssi_mgmt_sum:sum of management frames rssi.
 * @rssi_mgmt: average value of rssi for management frames.
 * @ac_stat: access categories status statistics.
 */
struct bes_vif_stat {
	ktime_t radio_on_start_time;
	ktime_t radio_on_with_ps_time;
	ktime_t on_time;
	ktime_t tx_time;
	ktime_t rx_time;

	u32 beacon_rx;
	int mgmt_count;
	int rssi_mgmt_sum;
	int rssi_mgmt;
	struct bes_wmm_ac_stat ac_stat[AC_MAX];
};

/**
 * struct rwnx_lstats_vif - link stats in rwnx_vif
 * @v_stat: vif status statistics
 * @rssi_monitor: rssi monitor
 */
struct bes_lstats_vif {
	struct bes_vif_stat v_stat;
#ifdef CONFIG_VENDOR_CMD
	struct bes_cfgvendor_rssi_monitor rssi_monitor;
#endif
};

/**
 * struct bes_dbac_sta_csa - STA processing CSA
 * @csa: Information about current Channel Switch Announcement.
 * @chan_update_pending: Indicates that STA needs to update channel context.
 * @csa_lock: Status of protection csa and chan_update_pending.
 * @in_csa: Record whether CSA-related fields are detected from the management frame.
*/
struct bes_dbac_sta_csa {
	struct rwnx_csa *csa;
	bool chan_update_pending;
	spinlock_t csa_lock;
	bool in_csa;
};

/**
 * struct bss_tm_resp - Used to identify whether the driver is sending a BTM rsp frame
 * @category: category field. category equals WLAN_CATEGORY_WNM, which means WNM.
 * @action: action field. reference to enum rwnx_ieee80211_wnm_action_code
 * @dialog_token: dialog_token field.
 * @status_code: status_code field. reference to enum rwnx_bss_trans_mgmt_status_code
 * @bss_termination_delay: bss_termination_delay field.
 * @variable: variable field.
*/
struct rwnx_bss_tm_resp{
	u8 category;
	u8 action;
	u8 dialog_token;
	u8 status_code;
	u8 bss_termination_delay;
	u8 variable[];
};

/**
 * struct bes_vif_wpa_priv_cmd - Wpa private cmd information for vif
 * @rxfilter: record rxfilter for wpa_cmd
 */
struct bes_vif_wpa_priv_cmd {
	u8 rxfilter;
};

/**
 * struct rwnx_vif - VIF information
 *
 * @list: List element for rwnx_hw->vifs
 * @rwnx_hw: Pointer to driver main data
 * @wdev: Wireless device
 * @ndev: Pointer to the associated net device
 * @net_stats: Stats structure for the net device
 * @key: Conversion table between protocol key index and MACHW key index
 * @drv_vif_index: VIF index at driver level (only use to identify active
 * vifs in rwnx_hw->avail_idx_map)
 * @vif_index: VIF index at fw level (used to index rwnx_hw->vif_table, and
 * rwnx_sta->vif_idx)
 * @ch_index: Channel context index (within rwnx_hw->chanctx_table)
 * @in_csa: Indicate if vif is in the CSA process
 * @up: Indicate if associated netdev is up (i.e. Interface is created at fw level)
 * @use_4addr: Whether 4address mode should be use or not
 * @is_resending: Whether a frame is being resent on this interface
 * @roc_tdls: Indicate if the ROC has been called by a TDLS station
 * @tdls_status: Status of the TDLS link
 * @tdls_chsw_prohibited: Whether TDLS Channel Switch is prohibited or not
 * @generation: Generation ID. Increased each time a sta is added/removed
 * @vif_chandef: Record the actual channel used by the interface,
 *               which will be read by the upper layer.
 * @ipv4_addr: IPv4 address of the interface
 * @ipv6_addr: IPv6 address of the interface
 * @priv_cmd: Wpa private cmd information
 *
 * STA / P2P_CLIENT interfaces
 * @flags: see rwnx_sta_flags
 * @ap: Pointer to the peer STA entry allocated for the AP
 * @tdls_sta: Pointer to the TDLS station
 * @ft_assoc_ies: Association Request Elements (only allocated for FT connection)
 * @ft_assoc_ies_len: Size, in bytes, of the Association request elements.
 * @ft_target_ap: Target AP for a BSS transition for FT over DS
 * @gateway_ip: gateway ip for arp keep alive
 * @ap_ssid: connected ap ssid
 * @sta_csa: Structure for the station Channel Switch Announcement.
 * @eapol_status: Record the current status of EAPOL
 * @sta_stored_key: Stored key of sta
 * @deferred_txq: Queue of deferred skb
 * @wnm_process: Whether STA is in the WNM process
 * @conn_abort: Means aborting the connection or not
 * @retry_cnt: station connection retry count
 *
 * AP/P2P GO/ MESH POINT interfaces
 * @flags: see rwnx_ap_flags
 * @sta_list: List of station connected to the interface
 * @bcn: Beacon data
 * @bcn_interval: beacon interval in TU
 * @bcmc_index: Index of the BroadCast/MultiCast station
 * @csa: Information about current Channel Switch Announcement (NULL if no CSA)
 * @mpath_list: List of Mesh Paths (MESH Point only)
 * @proxy_list: List of Proxies Information (MESH Point only)
 * @mesh_pm: Mesh power save mode currently set in firmware
 * @next_mesh_pm: Mesh power save mode for next peer
 *
 * AP_VLAN interfaces
 * @mater: Pointer to the master interface
 * @sta_4a: When AP_VLAN interface are used for WDS (i.e. wireless connection
 * between several APs) this is the 'gateway' sta to 'master' AP
 * @lstats_vif: vif link stats
 * @connect_retry_work: delayed_work for reconnect AP
 */
struct rwnx_vif {
	struct list_head list;
	struct rwnx_hw *rwnx_hw;
	struct wireless_dev wdev;
	struct net_device *ndev;
	struct net_device_stats net_stats;
	struct rwnx_key key[6];
	u8 drv_vif_index;
	u8 vif_index;
	u8 ch_index;
	bool in_csa;
	bool up;
	bool use_4addr;
	bool is_resending;
	bool roc_tdls;
	u8 tdls_status;
	bool tdls_chsw_prohibited;
	int generation;
	struct cfg80211_chan_def vif_chandef;
	u32 ipv4_addr;
	struct in6_addr ipv6_addr;
#ifdef CONFIG_WPA_PRIV_CMD
	struct bes_vif_wpa_priv_cmd priv_cmd;
#endif
	union {
		struct {
			u32 flags;
			struct rwnx_sta *ap;
			struct rwnx_sta *tdls_sta;
			u8 *ft_assoc_ies;
			int ft_assoc_ies_len;
			u8 ft_target_ap[ETH_ALEN];
			size_t connect_param[RWNX_MSG_MAX_LEN / sizeof(size_t)];
			u32 gateway_ip;
			struct mac_ssid ap_ssid;
			struct bes_dbac_sta_csa sta_csa;
			struct wpa_ie_data sec;
			atomic_t eapol_status;
			struct bes_sta_stored_key sta_stored_key;
			struct sk_buff_head deferred_txq;
			bool wnm_process;
			bool conn_abort;
			u8 retry_cnt;
		} sta;
		struct {
			u32 flags;
			struct list_head sta_list;
			struct rwnx_bcn bcn;
			int bcn_interval;
			u8 bcmc_index;
			struct rwnx_csa *csa;

			struct list_head mpath_list;
			struct list_head proxy_list;
			enum nl80211_mesh_power_mode mesh_pm;
			enum nl80211_mesh_power_mode next_mesh_pm;
		} ap;
		struct {
			struct rwnx_vif *master;
			struct rwnx_sta *sta_4a;
		} ap_vlan;
	};

	struct bes_lstats_vif lstats_vif;
#ifdef CONFIG_BES_NAPI
	struct napi_struct napi;
	struct sk_buff_head rx_napi_skb_queue;
#endif
	struct delayed_work connect_retry_work;
};

#define RWNX_INVALID_VIF 0xFF
#define RWNX_VIF_TYPE(vif) (vif->wdev.iftype)

/**
 * Structure used to store information relative to PS mode.
 *
 * @active: True when the sta is in PS mode.
 *          If false, other values should be ignored
 * @pkt_ready: Number of packets buffered for the sta in drv's txq
 *             (1 counter for Legacy PS and 1 for U-APSD)
 * @sp_cnt: Number of packets that remain to be pushed in the service period.
 *          0 means that no service period is in progress
 *          (1 counter for Legacy PS and 1 for U-APSD)
 */
struct rwnx_sta_ps {
	bool active;
	u16 pkt_ready[2];
	u16 sp_cnt[2];
};

/**
 * struct rwnx_rx_rate_stats - Store statistics for RX rates
 *
 * @table: Table indicating how many frame has been receive which each
 * rate index. Rate index is the same as the one used by RC algo for TX
 * @size: Size of the table array
 * @cpt: number of frames received
 * @rate_cnt: number of rate for which at least one frame has been received
 */
struct rwnx_rx_rate_stats {
	int *table;
	int size;
	int cpt;
	int rate_cnt;
};

struct rwnx_tx_rate_table {
	bool enable;           /* rate being used */
	u32 tx_mpdu;           /* number of successfully transmitted data pkts (ACK rcvd) */
	u32 mpdu_lost;         /* number of received data pkts */
	// u32 retries;        /* total number of data pkt retries. TODO */
};

/**
 * struct rwnx_tx_rate_stats - Store statistics for TX rates
 *
 * @table: Table indicating how many frame has been receive which each rate index.
 * @size: Size of the table array
 * @rate_cnt: number of rate for which at least one frame has been received
 * @tx_mpdu: number of successfully transmitted data pkts in all rate
 * @attempts: total number of packets
 * @retries: total number of data pkt retries in all rate
 */
struct rwnx_tx_rate_stats {
	struct rwnx_tx_rate_table *table;
	int size;
	int rate_cnt;
	u32 tx_mpdu;
	u32 attempts;
	// u32 retries; /* TODO */
};

/**
 * struct rwnx_tx_fail_stats - Structure Used to store statistics tx fail ptk
 *
 * @life_timeout_pkts: Number of packets that tx timed out
 * @over_retry_pkts: Number of packets that tx over retry
 * @tx_fail_pkts: Statistics the number of tx fail skb
 */
struct rwnx_tx_fail_stats {
	u32 life_timeout_pkts;
	u32 over_retry_pkts;
	u32 tx_fail_pkts;
};

/** struct bes_traffic_info - Structure Used to store traffic information
 *
 * @set: Indicates whether it has been set
 * @last_pkts: Number of packets received/sent last time
 * @last_bytes: Total received bytes received/sent last time
 * @delta_pkts: Delta count of packets received/sent
 * @delta_bytes: Delta bytes of packets received/sent
 * @pkts_rate: Statistics of the received/sent packet rates
 * @bytes_rate: Statistics of the received/sent bytes rates
 */
struct bes_traffic_info {
	bool set;
	u32 last_pkts;
	u64 last_bytes;
	u32 delta_pkts;
	u64 delta_bytes;
	u32 pkts_rate;
	u64 bytes_rate;
};

/**
 * struct rwnx_sta_stats - Structure Used to store statistics specific to a STA
 *
 * @rx_pkts: Number of MSDUs and MMPDUs received from this STA
 * @tx_pkts: Number of MSDUs and MMPDUs sent to this STA
 * @rx_bytes: Total received bytes (MPDU length) from this STA
 * @tx_bytes: Total transmitted bytes (MPDU length) to this STA
 * @tx_fail_stats: statistics the number of tx fail skb.
 * @tx_cont_fail_pkts: Continuous number of MSDUs and MMPDUs sent to this STA but not received corresponding ACK frames
 * @drop_tx: Count the number of dropped skb for sta.
 * @last_act: Timestamp (jiffies) when the station was last active (i.e. sent a
 * frame: data, mgmt or ctrl )
 * @last_rx: Hardware vector of the last received frame
 * @rx_rate: Statistics of the received rates
 * @tx_rate_vector: current tx_rate info
 * @connected_start_time: staion connected start time
 * @ul_delay: Times of uplink delay
 * @ul_count: Number of uplink delay recorded for the sta
 * @tx_traffic_info: Traffic information for data tx
 * @rx_traffic_info: Traffic information for data rx
 */
struct rwnx_sta_stats {
	u32 rx_pkts;
	u32 tx_pkts;
	u64 rx_bytes;
	u64 tx_bytes;
	struct rwnx_tx_fail_stats tx_fail_stats;
	u32 tx_cont_fail_pkts;
	u32 drop_tx;
	unsigned long last_act;
	struct hw_vect last_rx;
	struct rwnx_rx_rate_stats rx_rate;
	struct rwnx_tx_rate_stats tx_rate;
	struct tx_rate_vector tx_rate_vector;
	ktime_t connected_start_time;
	u32 ul_delay[BES_ULDELAY_LENGTH];
	u8 ul_count;
	struct bes_traffic_info tx_traffic_info;
	struct bes_traffic_info rx_traffic_info;
};

/**
 * struct rwnx_sta - Managed STATION information
 *
 * @list: List element of rwnx_vif->ap.sta_list
 * @valid: Flag indicating if the entry is valid
 * @mac_addr:  MAC address of the station
 * @aid: association ID
 * @sta_idx: Firmware identifier of the station
 * @vif_idx: Firmware of the VIF the station belongs to
 * @vlan_idx: Identifier of the VLAN VIF the station belongs to (= vif_idx if
 * no vlan in used)
 * @band: Band (only used by TDLS, can be replaced by channel context)
 * @width: Channel width
 * @center_freq: Center frequency
 * @center_freq1: Center frequency 1
 * @center_freq2: Center frequency 2
 * @ch_idx: Identifier of the channel context linked to the station
 * @qos: Flag indicating if the station supports QoS
 * @acm: Bitfield indicating which queues have AC mandatory
 * @uapsd_tids: Bitfield indicating which tids are subject to UAPSD
 * @key: Information on the pairwise key install for this station
 * @ps: Information when STA is in PS (AP only)
 * @bfm_report: Beamforming report to be used for VHT TX Beamforming
 * @group_info: MU grouping information for the STA
 * @ht: Flag indicating if the station supports HT
 * @vht: Flag indicating if the station supports VHT
 * @ac_param[AC_MAX]: EDCA parameters
 * @tdls: TDLS station information
 * @stats: Station statistics
 * @mesh_pm: link-specific mesh power save mode
 * @listen_interval: listen interval (only for AP client)
 * @flush_cmp: completion used for data flush operation
 * @wait_conn_end: Wait the end of connection
 * @ip_tmo_work: Call when obtaining IP timeout
 */
struct rwnx_sta {
	struct list_head list;
	bool valid;
	u8 mac_addr[ETH_ALEN];
	u16 aid;
	u8 sta_idx;
	u8 vif_idx;
	u8 vlan_idx;
	enum nl80211_band band;
	enum nl80211_chan_width width;
	u16 center_freq;
	u32 center_freq1;
	u32 center_freq2;
	u8 ch_idx;
	bool qos;
	u8 acm;
	u16 uapsd_tids;
	struct rwnx_key key;
	struct rwnx_sta_ps ps;
#ifdef CONFIG_RWNX_BFMER
	struct rwnx_bfmer_report *bfm_report;
#ifdef CONFIG_RWNX_MUMIMO_TX
	struct rwnx_sta_group_info group_info;
#endif /* CONFIG_RWNX_MUMIMO_TX */
#endif /* CONFIG_RWNX_BFMER */
	bool ht;
	bool vht;
	bool he;
	u32 ac_param[AC_MAX];
	struct rwnx_tdls tdls;
	struct rwnx_sta_stats stats;
	enum nl80211_mesh_power_mode mesh_pm;
	int listen_interval;
	struct twt_setup_ind twt_ind; /*TWT Setup indication*/
	struct completion flush_cmp;
	spinlock_t conn_state_lock;
	u32 wait_conn_end;
	struct delayed_work ip_tmo_work;
};

#define RWNX_INVALID_STA 0xFF

/**
 * rwnx_sta_addr - Return MAC address of a STA
 *
 * @sta: Station whose address is returned
 */
static inline const u8 *rwnx_sta_addr(struct rwnx_sta *sta)
{
	return sta->mac_addr;
}

#ifdef CONFIG_RWNX_SPLIT_TX_BUF
/**
 * struct rwnx_amsdu_stats - A-MSDU statistics
 *
 * @done: Number of A-MSDU push the firmware
 * @failed: Number of A-MSDU that failed to transit
 */
struct rwnx_amsdu_stats {
	int done;
	int failed;
};
#endif

/**
 * struct bes_ps_time - power save time statistics
 *
 * @total_time: power save total time
 * @start_time: power save start time
 */
struct bes_ps_time {
	ktime_t total_time;
	ktime_t start_time;
};

/**
 * struct bes_phy_time - phy time statistics
 *
 * @phy_on: one vif open at least
 * @start_time: phy power save start time
 */
struct bes_phy_time {
	bool phy_on;
	ktime_t start_time;
};

/**
 * struct rwnx_stats - Global statistics
 *
 * @cfm_balance: Number of buffer currently pushed to firmware per HW queue
 * @ampdus_tx: Number of A-MPDU transmitted (indexed by A-MPDU length)
 * @ampdus_rx: Number of A-MPDU received (indexed by A-MPDU length)
 * @ampdus_rx_map: Internal variable to distinguish A-MPDU
 * @ampdus_rx_miss: Number of MPDU non missing while receiving a-MPDU
 * @ampdu_rx_last: Index (of ampdus_rx_map) of the last A-MPDU received.
 * @amsdus: statistics of a-MSDU transmitted
 * @amsdus_rx: Number of A-MSDU received (indexed by A-MSDU length)
 */
struct rwnx_stats {
	int cfm_balance[NX_TXQ_CNT];
	int ampdus_tx[IEEE80211_MAX_AMPDU_BUF_HE];
	int ampdus_rx[IEEE80211_MAX_AMPDU_BUF_HE];
	int ampdus_rx_map[4];
	int ampdus_rx_miss;
	int ampdus_rx_last;
#ifdef CONFIG_RWNX_SPLIT_TX_BUF
	struct rwnx_amsdu_stats amsdus[NX_TX_PAYLOAD_MAX];
#endif
	int amsdus_rx[64];
	struct bes_ps_time ps_time;
	struct bes_phy_time phy_time;
};

// maximum number of TX frame per RoC
#define NX_ROC_TX 5
/**
 * struct rwnx_roc - Remain On Channel information
 *
 * @vif: VIF for which RoC is requested
 * @chan: Channel to remain on
 * @duration: Duration in ms
 * @internal: Whether RoC has been started internally by the driver (e.g. to send
 * mgmt frame) or requested by user space
 * @on_chan: Indicate if we have switch on the RoC channel
 * @tx_cnt: Number of MGMT frame sent using this RoC
 * @tx_cookie: Cookie as returned by rwnx_cfg80211_mgmt_tx if Roc has been started
 * to send mgmt frame (valid only if internal is true)
 */
struct rwnx_roc {
	struct rwnx_vif *vif;
	struct ieee80211_channel *chan;
	unsigned int duration;
	bool internal;
	bool on_chan;
	int tx_cnt;
	u64 tx_cookie[NX_ROC_TX];
};

/**
 * struct rwnx_survey_info - Channel Survey Information
 *
 * @filled: filled bitfield as per struct survey_info
 * @chan_time_ms: Amount of time in ms the radio spent on the channel
 * @chan_time_busy_ms: Amount of time in ms the primary channel was sensed busy
 * @noise_dbm: Noise in dbm
 */
struct rwnx_survey_info {
	u32 filled;
	u32 chan_time_ms;
	u32 chan_time_busy_ms;
	s8 noise_dbm;
};

/**
 * rwnx_chanctx - Channel context information
 *
 * @chan_def: Channel description
 * @count: number of vif using this channel context
 */
struct rwnx_chanctx {
	struct cfg80211_chan_def chan_def;
	u8 count;
};

#define RWNX_CH_NOT_SET 0xFF

/**
 * rwnx_phy_info - Phy information
 *
 * @cnt: Number of phy interface
 * @cfg: Configuration send to firmware
 * @sec_chan: Channel configuration of the second phy interface (if phy_cnt > 1)
 * @limit_bw: Set to true to limit BW on requested channel. Only set to use
 * VHT with old radio that don't support 80MHz (deprecated)
 */
struct rwnx_phy_info {
	u8 cnt;
	struct phy_cfg_tag cfg;
	struct mac_chan_op sec_chan;
	bool limit_bw;
};

/**
 * rwnx_deferred_msg - deferred msg information
 *
 * @list: List element for deferred msg
 * @msg: Message structure information
 * @vif: VIF pointer for this deferred msg
 * @sta: STA pointer for this deferred msg
 */
struct rwnx_deferred_msg {
	struct list_head list;
	struct ipc_e2a_msg msg;
	struct rwnx_vif *vif;
	struct rwnx_sta *sta;
};

/**
 * obss_narrow_bw_ru_table - obss narrow bandwidth ru extended capabilty table
 *
 * @op_class: operating class
 * @bandwidth: bandwidth
 * @channel: channel
 */
struct obss_narrow_bw_ru_table {
	u16 op_class;
	u16 bandwidth;
	u16 channel;
};

enum obss_narrow_ru_state {
	OBSS_NARROW_RU_UNPROCTED = 0,
	OBSS_NARROW_RU_PROTECTING,
	OBSS_NARROW_RU_PROTECTED
};

/**
 * obss_narrow_bw_ru - obss narrow bandwidth ru in OFDMA tolerance support
 *
 * @obss_narrow_bw_ru_table: obss narrow bw ru table data
 * @filter_work: delayed work of obss narrow bandwidth ru filter
 * @state: current status of obss narrow bandwidth
 * @acpt_all_beacon: symbol of accept_all_beacon
 * @lock: state lock
 */
struct obss_narrow_bw_ru {
	const struct obss_narrow_bw_ru_table *cfg_table;
	struct delayed_work filter_work;
	enum obss_narrow_ru_state state;
	bool acpt_all_beacon;
	spinlock_t lock;
};

/* Pointer type for the function of wakeup or sleep operation */
typedef void (*bes_pwr_wake_ctrl_ptr)(struct rwnx_hw *rwnx_hw, bool wakeup);

/* Declaration for power save callback item */
struct bes_pwr_lp_cb_item;

/**
 * bes_pwr - wifi lower power related information
 *
 * @pwr_lock: Spinlock to protect power manage
 * @state: current state of power manage, reference to enum pwr_wifi_lp_state
 * @switch_wq: wait queue head to protect critical section
 * @lp_wakeup_wait: indicates that waiting for LP_SLEEPED is already in wakeup process
 * @lp_work: delayed work of low-power processing
 * @pwr_task: record the task_struct of lp_work
 * @wakeup_event: wakeup event
 * @wakeup_vif: if wakeup event is phy event, set -1. otherwise, set vif idx
 * @vif_up_map: indicates the current up interfaces
 * @vif_link_map: indicates the current connected interfaces
 * @phy_event: phy event, contain phy_delay_event and phy_constant_event
 * @vif_event: vif event, contain vif_delay_event and vif_constant_event
 * @phy_d_evt_timeout: schedule time for phy delay event, ponit to struct pwr_phy_d_evt_timeout
 * @vif_d_evt_timeout: schedule time for vif delay event, ponit to struct pwr_vif_d_evt_timeout
 * @phy_timeout: timeout for phy_delay_event
 * @vif_timeout: timeout for vif_delay_event
 * @flag_suspend: flag for dealing event before host suspend
 * @pwr_cb_lock: power save callback lock
 * @cb_cnt: the count of power save callback function
 * @cb_tbl: the table of power save callback function
 * @delay_event_timeout: timeout tbl(jiffies) for delay_event
 * @wait_wake: indicates if it is waitting wake state
 * @cur_chanctx: Currently active Channel context for pwr used
 * @wake_ctrl: The control function of wakeup or sleep operation
 * @defer_cmds: The list to storage deferred cmds
 */
struct bes_pwr {
	spinlock_t pwr_lock;
	u8 state;
	wait_queue_head_t switch_wq;
	bool lp_wakeup_wait;
	struct delayed_work lp_work;
	struct task_struct *pwr_task;
	u8 wakeup_event;
	int wakeup_vif;
	u32 vif_up_map;
	u32 vif_link_map;
	u32 phy_event;
	u32 vif_event[NX_ITF_MAX];
	void *phy_d_evt_timeout;
	void *vif_d_evt_timeout;
	unsigned long *phy_timeout;
	unsigned long *vif_timeout[NX_ITF_MAX];
	bool flag_suspend;
	spinlock_t pwr_cb_lock;
	int cb_cnt;
	struct bes_pwr_lp_cb_item *cb_tbl;
	bool wait_wake;
	u8 cur_chanctx;
	bes_pwr_wake_ctrl_ptr wake_ctrl;
	struct list_head defer_cmds;
};

/**
 * bes_sched_scan - wifi sched scan related information
 *
 * @ws: the wakeup ource for sched scan
 */
struct bes_sched_scan {
	struct wakeup_source *ws;
};

/* Declaration for data monitor callback item */
struct bes_data_monitor_cb_item;

/**
 * bes_data_monitor - data monitor related information
 *
 * @start: indicates the data monitor sampling is start
 * @last_time: the last sampling time
 * @cb_lock: the spinlock of callback function control
 * @cb_cnt: the count of data monitor callback function
 * @cb_tbl: the table of data monitor callback function
 */
struct bes_data_monitor {
	bool start;
	unsigned long last_time;
	spinlock_t cb_lock;
	int cb_cnt;
	struct bes_data_monitor_cb_item *cb_tbl;
};

/**
 * bes_cali_result - Calibration result related information
 * @type: see enum bes_cali_type
 * @channel: Calibrated channel (The center frequency of segment 1 is converted to the channel)
 * @count: number of vif using this channel context
 */
struct bes_cali_result {
	u8 type;
	u8 channel;
	u8 count;
};

/**
 * bes_rf_cali - RF calibration related information
 * @cali_res: Record calibrated channel information.
 * @cali_res_cnt: The current calibration result count value.
 * @cur_cali_ch_ctxt_idx: Record the channel context index for the current temperature calibration.
 * @dyna_cali_enabled: Record whether dynamic calibration on the MCU end is enabled.
 * @defer_cali_vif_map: Record those whose VIF calibrations have been defered.
 * @using_chan_ctxt: Channel context in use.
 * @temp_comp_work: Work for temperature compensation calibration.
 */
struct bes_rf_cali {
	struct bes_cali_result cali_res[MAX_COEXIST_CHAN_CTXT];
	u8 cali_res_cnt;
	u8 cur_cali_ch_ctxt_idx;
	bool dyna_cali_enabled;
	u8 defer_cali_vif_map;
	struct rwnx_chanctx *using_chan_ctxt[MAX_COEXIST_CHAN_CTXT];
	struct work_struct temp_comp_work;
};

/**
 * bes_wowlan - wowlan information
 * @mode: wowlan mode(see enum in wowlan_operation), reference to enum wowlan_mode
 * @wakeup_source: marks whether bes2610 is the wakeup souce or not
 * @parse_msg_step: used together with parse_msg_tjiff to parse msg to determine if it is a disconnected wake-up reason
 * @parse_msg_tjiff: used together with parse_msg_step to parse msg to determine if it is a disconnected wake-up reason
 * @complete: notify flush delayed work has been completed
 */
struct bes_wowlan {
	u8 mode;
	bool wakeup_source;
	u8 parse_msg_step;
	unsigned long parse_msg_tjiff;
	struct completion complete;
};

/**
 * struct bes_fl_map - filter list map
 * @list_lock: spinlock to protet control map
 * @stats: record filter consumed bes_capacity
 * @valid: whether it can set to device
 * @fl_tbl: filter storage
 */
struct bes_fl_map {
	spinlock_t list_lock;
	struct bes_capacity stats;
	bool valid;
	struct bes_fl fl_tbl[NX_ITF_MAX][FL_MAX];
};

/**
 * struct bes_filter - Filter information
 * @mutex: mutex to protect filter add/delete/set
 * @ifl_map: internal filter list map and its parameters
 * @ifl_map_disable_rcd: Disable type record for internal filter list map
 * @filter_en: whether filter is enabled on a interface
 * @local_flags: Local backup of dev->flags. Only care about IFF_PROMISC/IFF_ALLMULTI/IFF_MULTICAST variables, defult set IFF_MULTICAST
 * @arp_offload: whether arp offload filter is enabled on a interface
 * @ns_offload: whether ns offload filter is enabled on a interface
 * @signal_test_filter: whether signal test filter is enabled on a interface
 * @max: record filter bes_capacity maximum
 * @mismatch_action: filter mismatch action
 * @sync_cnt: synchronization operations marked
 * @filter_work_lock: spinlock to protet control filter work list
 * @filter_work_list:filter related work list
 * @filter_work: filter related work processing functions
 */
struct bes_filter {
	struct mutex mutex;
	struct bes_fl_map ifl_map;
	u8 ifl_map_disable_rcd;
	bool filter_en[NX_ITF_MAX];
	unsigned long local_flags[NX_ITF_MAX];
	bool arp_offload[NX_ITF_MAX];
	bool ns_offload[NX_ITF_MAX];
	bool signal_test_filter[NX_ITF_MAX];
	struct bes_capacity max;
	u8 mismatch_action;
	atomic_t sync_cnt;
	u8 gtk_rekey_vif_idx;
	spinlock_t filter_work_lock;
	struct list_head filter_work_list;
	struct work_struct filter_work;
};

/**
 * struct bes_tp_tx_rx_info - tx/rx information for thoughput debug
 * @start: indicates the start of tx/rx rate calculation
 * @last_time: last calculation time
 * @tx_traffic: tx traffic statistics
 * @rx_traffic: rx traffic statistics
 * @last_tx_fail_stats: last statistics the number of tx fail skb.
 * @last_tx_cont_fail_pkts: last continuous number of MSDUs and MMPDUs
 * 			    sent to this STA but not received corresponding ACK frames
 * @last_drop_tx: last count the number of dropped skb for sta.
 *
 */
struct bes_tp_tx_rx_info {
	bool start;
	unsigned long last_time;
	struct bes_traffic_info tx_traffic;
	struct bes_traffic_info rx_traffic;
	struct rwnx_tx_fail_stats last_tx_fail_stats;
	u32 last_tx_cont_fail_pkts;
	u32 last_drop_tx;
};

/**
 * struct bes_tp_dbg - throughput debug information
 * @feature: Support features
 * @work: throughput debug work
 * @tx_rx_info: contain tx/rx information for thoughput debug
 *
 */
struct bes_tp_dbg {
	u32 feature;
	struct work_struct work;
	struct bes_tp_tx_rx_info tx_rx_info[NX_REMOTE_STA_MAX];
};

/**
 * struct bes_phy_wpa_priv_cmd - Wpa private cmd information for phy
 * @lock: spinlock to protet parameters
 * @event_cnt: Value increases when occurring a certain event that affects screen-off filters
 * @last_event_cnt: Record the last value of event count when refreshing screen-off filters
 */
struct bes_phy_wpa_priv_cmd {
	spinlock_t lock;
	u16 event_cnt;
	u16 last_event_cnt;
};

/**
 * struct bes_fw_log_fifo_dump - Transferring firmware logs
 * @log_buffer: Buffer for storing firmware logs.
 * @dump_work: The work of copying the firmware log to the kernel log.
 * @kfifo: struct kfifo_rec_ptr_2 can handle records of a length between 0 and 65535 bytes.
 * @fifo_sema: Protect the semaphore of the kfifo.
*/
struct bes_fw_log_fifo_dump {
	u8 *log_buffer;
	struct work_struct dump_work;
	struct kfifo_rec_ptr_2 kfifo;
	struct semaphore fifo_sema;
#if (DUMP_FW_LOG_METHOD == 2)
	spinlock_t dbg_lock;
#endif
};

/**
 * struct bes_coex - wifi/bt coex structure
 * @hw_fdd: hardware coex mode
 * @lock: spinlock used for exclusive access
 * @inited: indicate coex module is initialized
 * @wifi_coex_state: wifi coex status
 * @bt_coex_state: bt coex status
 * @pending_action: pending actions for wifi/bt coex
 * @pta_switch_flags: current pta switch flags
 * @coex_mode: current coex mode
 * @work: work for processing coex actions
 */
struct bes_coex {
	bool hw_fdd;
	spinlock_t lock;
	u32 inited;
	u32 wifi_coex_state;
	u32 bt_coex_state;
	u32 pending_action;
	u32 pta_switch_flags;
	int coex_mode;
	struct work_struct work;
};

#ifdef CONFIG_RWNX_DEBUGFS

/**
 * struct rwnx_debugfs - debug fs
 *
 * @rateidx: rate index
 * @dir: debugfs file directory
 * @dir_stas: all stations file directory
 * @trace_prst: trace_prst
 * @helper_cmd: helper command
 * @helper_work: helper work
 * @helper_scheduled: helper work schedule state
 * @umh_lock: helper lock
 * @unregistering: unregistering state'
 *
 * @sta_work: work for sta add debugfs
 * @dir_sta: one sta file directory
 * @sta_idx: sta index when adding
 * @dir_rc_sta: one sta's rc file directory
 * @rc_config: rc config
 * @rc_config_save: saved rc config list
 * @dir_twt: twt file directory
 * @dir_twt_sta: one sta's twt file directory
 *
 * @dir_vifs: vifs file directory
 * @dir_vif: one vif's file directory
 * @unmatch_dst_mgmt_filter_en:  The state of the filter of
 * management frames not matching the destination.
 */
struct rwnx_debugfs {
	unsigned long long rateidx;
	struct dentry *dir;
	struct dentry *dir_stas;
	bool trace_prst;

	char helper_cmd[64];
	struct work_struct helper_work;
	bool helper_scheduled;
	spinlock_t umh_lock;
	bool unregistering;

#ifndef CONFIG_RWNX_FHOST
	struct rwnx_fw_trace fw_trace;
#endif /* CONFIG_RWNX_FHOST */

#ifdef CONFIG_RWNX_FULLMAC
	struct work_struct sta_work;
	struct dentry *dir_sta[NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX];
	uint8_t sta_idx;
	struct dentry *dir_rc_sta[NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX];
	int rc_config[NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX];
	struct list_head rc_config_save;
	struct dentry *dir_twt;
	struct dentry *dir_twt_sta[NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX];
#endif

	struct dentry *dir_vifs;
	struct dentry *dir_vif[NX_ITF_MAX];
	bool unmatch_dst_mgmt_filter_en;
};

#else

struct rwnx_debugfs {
};

#endif /* CONFIG_RWNX_DEBUGFS */

/**
 * struct rwnx_hw - RWNX driver main data
 *
 * @dev: Device structure
 *
 * @plat: Underlying RWNX platform information
 * @phy: PHY Configuration
 * @version_cfm: MACSW/HW versions (as obtained via MM_VERSION_REQ)
 * @machw_type: Type of MACHW (see RWNX_MACHW_xxx)
 *
 * @mod_params: Driver parameters
 * @wf_conf_wait_q: WiFi configuration waiting queue
 * @flags: Global flags, see enum rwnx_dev_flag
 * @phy_op_sema: The mutex exclusion relationship between protection scan, disconnect
 * connect, start_ap, stop_ap and temperature compensation.
 * @task: Tasklet to execute firmware IRQ bottom half
 * @wiphy: Wiphy structure
 * @ext_capa: extended capabilities supported
 * @ext_capa_mask: extended capabilities mask
 *
 * @vifs: List of VIFs currently created
 * @vif_table: Table of all possible VIFs, indexed with fw id.
 * (NX_REMOTE_STA_MAX extra elements for AP_VLAN interface)
 * @vif_started: Number of vif created at firmware level
 * @avail_idx_map: Bitfield of created interface (indexed by rwnx_vif.drv_vif_index)
 * @monitor_vif:  FW id of the monitor interface (RWNX_INVALID_VIF if no monitor vif)
 *
 * @sta_table: Table of all possible Stations
 * (NX_VIRT_DEV_MAX] extra elements for BroadCast/MultiCast stations)
 *
 * @chanctx_table: Table of all possible Channel contexts
 * @cur_chanctx: Currently active Channel context
 * @chanctx_count: The number of using channel context
 * @survey: Table of channel surveys
 * @roc: Information of current Remain on Channel request (NULL if Roc requested)
 * @roc_lock: Spinlock to protect RoC pointer
 * @scan_request: Information of current scan request
 * @radar: Radar detection information
 *
 * @tx_lock: Spinlock to protect TX path
 * @txq: Table of STA/TID TX queues
 * @hwq: Table of MACHW queues
 * @txq_cleanup: Timer list to drop packet queued too long in TXQs
 * @sw_txhdr_cache: Cache to allocate
 * @tcp_pacing_shift: TCP buffering configuration (buffering is ~ 2^(10-tps) ms)
 * @mu: Information for Multiuser TX groups
 *
 * @defer_rx: Work to defer RX processing out of IRQ tasklet. Only use for specific mgmt frames
 *
 * @ipc_env: IPC environment
 * @cmd_mgr: FW command manager
 * @cb_lock: Spinlock to protect code from FW confirmation/indication message
 *
 * @msgbuf_pool: Pool of shared buffers to retrieve FW messages
 * @dbgbuf_pool: Pool of shared buffers to retrieve FW debug messages
 * @radar_pool: Pool of shared buffers to retrieve FW radar events
 * @tx_pattern: Shared buffer for the FW to download end of TX buf pattern from
 * @dbgdump: Shared buffer to retrieve FW debug dump
 * @rxdesc_pool: Pool of shared buffers to retrieve RX descriptors
 * @rxbufs: Table of shared buffers to retrieve RX data
 * @rxbuf_idx: Index of the last allocated buffer in rxbufs table
 * @unsuprxvecs: Table of shared buffers to retrieve FW unsupported frames
 * @scan_ie: Shared buffer, allocated to push probe request elements to the FW
 *
 * @debugfs: Debug FS entries
 * @stats: global statistics
 * @user_ps_mode: used for recording user pm setting
 * @cur_ps_mode: indicate current pm mode
 * @rwnx_wlan_power_ctrl: Manage net device switch, wlan down or up
 *
 * @rf_cali: Record RF calibration status
 * @fw_fifo_dump: Firmware logs can be dumped through FIFO
 * @coex: wifi/bt coex status
 * @conf_file: config file
 * @custregd: parameters related to the country code
 */
struct rwnx_hw {
	struct device *dev;

	// Hardware info
	struct rwnx_plat *plat;
	struct rwnx_phy_info phy;
	struct mm_version_cfm version_cfm;
	int machw_type;

	// Global wifi config
	struct rwnx_mod_params *mod_params;
	wait_queue_head_t wf_conf_wait_q;
	unsigned long flags;
	struct semaphore phy_op_sema;
	struct wiphy *wiphy;
	u8 ext_capa[10];
	u8 ext_capa_mask[10];

	// VIFs
	struct list_head vifs;
	struct rwnx_vif *vif_table[NX_ITF_MAX];
	int vif_started;
	u16 avail_idx_map;
	u8 monitor_vif;

	// Stations
	struct rwnx_sta sta_table[NX_REMOTE_STA_MAX + NX_VIRT_DEV_MAX];

	// Channels
	struct rwnx_chanctx chanctx_table[NX_CHAN_CTXT_CNT];
	atomic_t chanctx_count;
	u8 cur_chanctx;
	struct rwnx_survey_info survey[SCAN_CHANNEL_MAX];
	struct rwnx_roc *roc;
	wait_queue_head_t roc_wait_wq;
	spinlock_t roc_lock;
	struct cfg80211_scan_request *scan_request;
	struct rwnx_radar radar;
	// TX path
	spinlock_t tx_lock;
	struct rwnx_txq txq[NX_NB_TXQ];
	struct rwnx_hwq hwq[NX_TXQ_CNT];
	struct timer_list txq_cleanup;
	atomic_t txq_cleanup_available;
	struct kmem_cache *sw_txhdr_cache;
	u32 tcp_pacing_shift;
#ifdef CONFIG_RWNX_MUMIMO_TX
	struct rwnx_mu_info mu;
#endif

	// RX path
	struct rwnx_defer_rx defer_rx;

	// IRQ
	struct tasklet_struct task;

	// IPC
	struct ipc_host_env_tag *ipc_env;
	struct rwnx_cmd_mgr cmd_mgr;
	spinlock_t cb_lock;

	// Shared buffers
	struct rwnx_ipc_buf_pool msgbuf_pool;
	struct rwnx_ipc_buf_pool dbgbuf_pool;
	struct rwnx_ipc_buf_pool radar_pool;
	struct rwnx_ipc_buf tx_pattern;
	struct rwnx_ipc_dbgdump dbgdump;
	struct rwnx_ipc_buf_pool rxdesc_pool;
	struct rwnx_ipc_buf rxbufs[RWNX_RXBUFF_MAX];
	int rxbuf_idx;
	struct rwnx_ipc_buf unsuprxvecs[IPC_UNSUPRXVECBUF_CNT];
	struct rwnx_ipc_buf scan_ie;
	struct rwnx_ipc_buf_pool txcfm_pool;

	// Debug FS and stats
	struct rwnx_debugfs debugfs;
	struct rwnx_stats stats;

	// delay list for rf calibration
	struct rwnx_deferred_msg deferred_e2amsg_list;

	// user power mode setting
	u8 user_ps_mode;
	u8 cur_ps_mode;

	// wifi and bt switch
#ifdef CONFIG_BES_POWER_CTRL
	struct bes_wlan_power_ctrl wlan_power_ctrl;
#endif

	// HE obss narrow control
	struct obss_narrow_bw_ru obss_narrow;
	struct workqueue_struct *rwnx_wq;

	// wifi low power
#ifdef CONFIG_BES_WIFI_LP
	struct bes_pwr pwr;
#endif

	// data monitor
	struct bes_data_monitor data_tx_monitor;
	struct bes_data_monitor data_rx_monitor;

	// arp and ns offload
	struct notifier_block inetaddr_notifier;
	struct notifier_block inet6addr_notifier;

	// filter
	struct bes_filter filter;

	// wowlan
	struct bes_wowlan wowlan;

	// throughput debug
#ifdef CONFIG_BES_TP_DBG
	struct bes_tp_dbg tp_dbg;
#endif

	// wpa private cmd
#ifdef CONFIG_WPA_PRIV_CMD
	struct bes_phy_wpa_priv_cmd wpa_priv_cmd;
#endif

	// RF calibration
	struct bes_rf_cali rf_cali;

	// Firmware logs dump by fifo
#ifdef CONFIG_FW_LOG_FIFO_DUMP
	struct bes_fw_log_fifo_dump fw_fifo_dump;
#endif

	// coexistence between wifi and bt
#ifdef CONFIG_BES_COEX_SUPPORT
	struct bes_coex coex;
#endif
	// Config file
	struct rwnx_conf_file conf_file;

	// parameters related to the country code
	struct bes_custregd custregd;

	// parameters related to the csi cap
#ifdef CONFIG_RWNX_CSI_CAP
	struct wifi_csi_cap_info csi_cap;
#endif
	// parameters related to system sleep
#ifdef CONFIG_PM
	struct notifier_block pm_notify;
#endif
	u32 tx_thread_in;
	u32 tx_thread_out;

	u32 data_push_start;
	u32 data_push_stop;

	u32 msg_push_start;
	u32 msg_push_stop;

	// parameters related to sched scan
#ifdef CONFIG_SCHED_SCAN
	struct bes_sched_scan sched_scan;
#endif
};

/**
 * struct rwnx_mac_ac_param - AC parameters for firmware
 *
 * @aifs: Arbitration interframe space.
 * @cwmin: Minimum contention window.
 * @cwmax: Maximum contention window.
 * @txop: Maximum burst time in units of 32 usecs, 0 meaning disabled.
 */
struct rwnx_mac_ac_param {
	u32 aifs:4;
	u32 cwmin:4;
	u32 cwmax:4;
	u32 txop:16;
};

u8 *rwnx_build_bcn(struct rwnx_bcn *bcn, struct cfg80211_beacon_data *new);

void rwnx_chanctx_link(struct rwnx_vif *vif, u8 ch_idx, struct cfg80211_chan_def *chandef);
void rwnx_chanctx_unlink(struct rwnx_vif *vif);
int rwnx_chanctx_valid(struct rwnx_hw *rwnx_hw, u8 idx);

static inline bool is_multicast_sta(int sta_idx)
{
	return (sta_idx >= NX_REMOTE_STA_MAX);
}
struct rwnx_sta *rwnx_get_sta(struct rwnx_hw *rwnx_hw, const u8 *mac_addr);

static inline uint8_t master_vif_idx(struct rwnx_vif *vif)
{
	if (unlikely(vif->wdev.iftype == NL80211_IFTYPE_AP_VLAN)) {
		return vif->ap_vlan.master->vif_index;
	} else {
		return vif->vif_index;
	}
}

static inline void *rwnx_get_shared_trace_buf(struct rwnx_hw *rwnx_hw)
{
#if defined(CONFIG_RWNX_DEBUGFS)
	return (void *)&(rwnx_hw->debugfs.fw_trace.buf);
#else
	return NULL;
#endif
}

static inline bool rwnx_vif_is_connected(const struct rwnx_vif *vif)
{
	struct rwnx_sta *ap_sta = NULL;

	if (RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_STATION ||
		RWNX_VIF_TYPE(vif) == NL80211_IFTYPE_P2P_CLIENT) {
		ap_sta = vif->sta.ap;
		return ap_sta && ap_sta->valid;
	}
	return false;
}

void rwnx_external_auth_enable(struct rwnx_vif *vif);
void rwnx_external_auth_disable(struct rwnx_vif *vif);

void rwnx_narrow_ru_filter_reset(struct rwnx_hw *rwnx_hw);
bool rwnx_narrow_ru_filter_acpt_all_beacon(struct rwnx_hw *rwnx_hw);

#endif /* _RWNX_DEFS_H_ */
