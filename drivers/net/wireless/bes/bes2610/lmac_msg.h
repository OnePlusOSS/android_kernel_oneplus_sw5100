/**
 ****************************************************************************************
 *
 * @file lmac_msg.h
 *
 * @brief Main definitions for message exchanges with LMAC
 *
 * Copyright (C) RivieraWaves 2011-2021
 *
 ****************************************************************************************
 */

#ifndef LMAC_MSG_H_
#define LMAC_MSG_H_

/*
 * INCLUDE FILES
 ****************************************************************************************
 */
// for MAC related elements (mac_addr, mac_ssid...)
#include "lmac_mac.h"
#include "hal_desc.h"

/*
 ****************************************************************************************
 */
/////////////////////////////////////////////////////////////////////////////////
// COMMUNICATION WITH LMAC LAYER
/////////////////////////////////////////////////////////////////////////////////
/* Task identifiers for communication between LMAC and DRIVER */
enum {
	TASK_NONE = (u8_l)-1,

	// MAC Management task.
	TASK_MM = 0,
	// DEBUG task
	TASK_DBG,
	/// SCAN task
	TASK_SCAN,
	/// TDLS task
	TASK_TDLS,
	/// SCANU task
	TASK_SCANU,
	/// ME task
	TASK_ME,
	/// SM task
	TASK_SM,
	/// APM task
	TASK_APM,
	/// BAM task
	TASK_BAM,
	/// MESH task
	TASK_MESH,
	/// RXU task
	TASK_RXU,
	/// RM task
	TASK_RM,
	/// TWT task
	TASK_TWT,
	/// FTM task
	TASK_FTM,
#if defined CONFIG_RWNX_SOFTMAC
	// This is used to define the last task that is running on the EMB processor
	TASK_LAST_EMB = TASK_TDLS,
#elif defined CONFIG_RWNX_FULLMAC || defined CONFIG_RWNX_FHOST
	// This is used to define the last task that is running on the EMB processor
	TASK_LAST_EMB = TASK_FTM,
#else
#error "Need to define SOFTMAC or FULLMAC"
#endif
	// nX API task
	TASK_API,

	/// MCU task. The message is not sent to lmac, and must be kept at the previous position of TASK_MAX.
	TASK_MCU,
	// define all task number
	TASK_MAX,
};

/// For MAC HW States copied from "hal_machw.h"
enum {
	/// MAC HW IDLE State.
	HW_IDLE = 0,
	/// MAC HW RESERVED State.
	HW_RESERVED,
	/// MAC HW DOZE State.
	HW_DOZE,
	/// MAC HW ACTIVE State.
	HW_ACTIVE
};

/// Power Save mode setting
enum mm_ps_mode_state {
	MM_PS_MODE_OFF,
	MM_PS_MODE_ON,
	MM_PS_MODE_ON_DYN,
};

/// Status/error codes used in the MAC software.
enum {
	CO_OK,
	CO_FAIL,
	CO_EMPTY,
	CO_FULL,
	CO_BAD_PARAM,
	CO_NOT_FOUND,
	CO_NO_MORE_ELT_AVAILABLE,
	CO_NO_ELT_IN_USE,
	CO_BUSY,
	CO_OP_IN_PROGRESS,
};

/// Remain on channel operation codes
enum mm_remain_on_channel_op {
	MM_ROC_OP_START = 0,
	MM_ROC_OP_CANCEL,
};

#define NX_PRINT_NONE     0
#define NX_PRINT_IPC      1
#define NX_PRINT_PRINTF   2

/// Type of fw log
enum fw_log_type {
	FW_LOG_NONE = 0,
	FW_LOG_MCU_UPLOAD = 1<<0,
	FW_LOG_LMAC_UPLOAD = 1<<1,
	FW_LOG_LMAC_LOCAL = 1<<2,
};

/// Type of fw log cmd
enum fw_log_cmd_type {
	DUMP_FW_LOG = 0,
	GET_FW_LOG_TYPE,
};

#define DRV_TASK_ID 100

/// Message Identifier. The number of messages is limited to 0xFFFF.
/// The message ID is divided in two parts:
/// - bits[15..10] : task index (no more than 64 tasks supported).
/// - bits[9..0] : message index (no more that 1024 messages per task).
typedef u16 lmac_msg_id_t;

typedef u16 lmac_task_id_t;

/// Build the first message ID of a task.
#define LMAC_FIRST_MSG(task) ((lmac_msg_id_t)((task) << 10))

#define MSG_T(msg) ((lmac_task_id_t)((msg) >> 10))
#define MSG_I(msg) ((msg) & ((1 << 10) - 1))

/// Message structure.
struct lmac_msg {
#if !defined(CONFIG_RWNX_PCIE) && !defined(CONFIG_BES_IBUS)
	u32 psa_pattern;
#endif
	lmac_msg_id_t id;	///< Message id.
	lmac_task_id_t dest_id; ///< Destination kernel identifier.
	lmac_task_id_t src_id;	///< Source kernel identifier.
	u16 param_len;		///< Parameter embedded struct length.
	u32 param[];		///< Parameter embedded struct. Must be word-aligned.
};

/// List of messages related to the task.
enum mm_msg_tag {
	/// RESET Request.
	MM_RESET_REQ = LMAC_FIRST_MSG(TASK_MM),
	/// RESET Confirmation.
	MM_RESET_CFM,
	/// START Request.
	MM_START_REQ,
	/// START Confirmation.
	MM_START_CFM,
	/// Read Version Request.
	MM_VERSION_REQ,
	/// Read Version Confirmation.
	MM_VERSION_CFM,
	/// ADD INTERFACE Request.
	MM_ADD_IF_REQ,
	/// ADD INTERFACE Confirmation.
	MM_ADD_IF_CFM,
	/// REMOVE INTERFACE Request.
	MM_REMOVE_IF_REQ,
	/// REMOVE INTERFACE Confirmation.
	MM_REMOVE_IF_CFM,
	/// STA ADD Request.
	MM_STA_ADD_REQ, // 10
	/// STA ADD Confirm.
	MM_STA_ADD_CFM,
	/// STA DEL Request.
	MM_STA_DEL_REQ,
	/// STA DEL Confirm.
	MM_STA_DEL_CFM,
	/// RX FILTER CONFIGURATION Request.
	MM_SET_FILTER_REQ,
	/// RX FILTER CONFIGURATION Confirmation.
	MM_SET_FILTER_CFM,
	/// CHANNEL CONFIGURATION Request.
	MM_SET_CHANNEL_REQ,
	/// CHANNEL CONFIGURATION Confirmation.
	MM_SET_CHANNEL_CFM,
	/// DTIM PERIOD CONFIGURATION Request.
	MM_SET_DTIM_REQ,
	/// DTIM PERIOD CONFIGURATION Confirmation.
	MM_SET_DTIM_CFM,
	/// BEACON INTERVAL CONFIGURATION Request.
	MM_SET_BEACON_INT_REQ, // 20
	/// BEACON INTERVAL CONFIGURATION Confirmation.
	MM_SET_BEACON_INT_CFM,
	/// BASIC RATES CONFIGURATION Request.
	MM_SET_BASIC_RATES_REQ,
	/// BASIC RATES CONFIGURATION Confirmation.
	MM_SET_BASIC_RATES_CFM,
	/// BSSID CONFIGURATION Request.
	MM_SET_BSSID_REQ,
	/// BSSID CONFIGURATION Confirmation.
	MM_SET_BSSID_CFM,
	/// EDCA PARAMETERS CONFIGURATION Request.
	MM_SET_EDCA_REQ,
	/// EDCA PARAMETERS CONFIGURATION Confirmation.
	MM_SET_EDCA_CFM,
	/// ABGN MODE CONFIGURATION Request.
	MM_SET_MODE_REQ,
	/// ABGN MODE CONFIGURATION Confirmation.
	MM_SET_MODE_CFM,
	/// Request setting the VIF active state (i.e associated or AP started)
	MM_SET_VIF_STATE_REQ, // 30
	/// Confirmation of the @ref MM_SET_VIF_STATE_REQ message.
	MM_SET_VIF_STATE_CFM,
	/// SLOT TIME PARAMETERS CONFIGURATION Request.
	MM_SET_SLOTTIME_REQ,
	/// SLOT TIME PARAMETERS CONFIGURATION Confirmation.
	MM_SET_SLOTTIME_CFM,
	/// Power Mode Change Request.
	MM_SET_IDLE_REQ,
	/// Power Mode Change Confirm.
	MM_SET_IDLE_CFM,
	/// KEY ADD Request.
	MM_KEY_ADD_REQ,
	/// KEY ADD Confirm.
	MM_KEY_ADD_CFM,
	/// KEY DEL Request.
	MM_KEY_DEL_REQ,
	/// KEY DEL Confirm.
	MM_KEY_DEL_CFM,
	/// Block Ack agreement info addition
	MM_BA_ADD_REQ, // 40
	/// Block Ack agreement info addition confirmation
	MM_BA_ADD_CFM,
	/// Block Ack agreement info deletion
	MM_BA_DEL_REQ,
	/// Block Ack agreement info deletion confirmation
	MM_BA_DEL_CFM,
	/// Indication of the primary TBTT to the upper MAC. Upon the reception of this
	// message the upper MAC has to push the beacon(s) to the beacon transmission queue.
	MM_PRIMARY_TBTT_IND,
	/// Indication of the secondary TBTT to the upper MAC. Upon the reception of this
	// message the upper MAC has to push the beacon(s) to the beacon transmission queue.
	MM_SECONDARY_TBTT_IND,
	/// Request for changing the TX power
	MM_SET_POWER_REQ,
	/// Confirmation of the TX power change
	MM_SET_POWER_CFM,
	/// Request to the LMAC to trigger the embedded logic analyzer and forward the debug
	/// dump.
	MM_DBG_TRIGGER_REQ,
	/// Set Power Save mode
	MM_SET_PS_MODE_REQ,
	/// Set Power Save mode confirmation
	MM_SET_PS_MODE_CFM, // 50
	/// Request to add a channel context
	MM_CHAN_CTXT_ADD_REQ,
	/// Confirmation of the channel context addition
	MM_CHAN_CTXT_ADD_CFM,
	/// Request to delete a channel context
	MM_CHAN_CTXT_DEL_REQ,
	/// Confirmation of the channel context deletion
	MM_CHAN_CTXT_DEL_CFM,
	/// Request to link a channel context to a VIF
	MM_CHAN_CTXT_LINK_REQ,
	/// Confirmation of the channel context link
	MM_CHAN_CTXT_LINK_CFM,
	/// Request to unlink a channel context from a VIF
	MM_CHAN_CTXT_UNLINK_REQ,
	/// Confirmation of the channel context unlink
	MM_CHAN_CTXT_UNLINK_CFM,
	/// Request to update a channel context
	MM_CHAN_CTXT_UPDATE_REQ,
	/// Confirmation of the channel context update
	MM_CHAN_CTXT_UPDATE_CFM, // 60
	/// Request to schedule a channel context
	MM_CHAN_CTXT_SCHED_REQ,
	/// Confirmation of the channel context scheduling
	MM_CHAN_CTXT_SCHED_CFM,
	/// Request to change the beacon template in LMAC
	MM_BCN_CHANGE_REQ,
	/// Confirmation of the beacon change
	MM_BCN_CHANGE_CFM,
	/// Request to update the TIM in the beacon (i.e to indicate traffic bufferized at AP)
	MM_TIM_UPDATE_REQ,
	/// Confirmation of the TIM update
	MM_TIM_UPDATE_CFM,
	/// Connection loss indication
	MM_CONNECTION_LOSS_IND,
	/// Channel context switch indication to the upper layers
	MM_CHANNEL_SWITCH_IND,
	/// Channel context pre-switch indication to the upper layers
	MM_CHANNEL_PRE_SWITCH_IND,
	/// Request to remain on channel or cancel remain on channel
	MM_REMAIN_ON_CHANNEL_REQ, // 70
	/// Confirmation of the (cancel) remain on channel request
	MM_REMAIN_ON_CHANNEL_CFM,
	/// Remain on channel expired indication
	MM_REMAIN_ON_CHANNEL_EXP_IND,
	/// Indication of a PS state change of a peer device
	MM_PS_CHANGE_IND,
	/// Indication that some buffered traffic should be sent to the peer device
	MM_TRAFFIC_REQ_IND,
	/// Request to modify the STA Power-save mode options
	MM_SET_PS_OPTIONS_REQ,
	/// Confirmation of the PS options setting
	MM_SET_PS_OPTIONS_CFM,
	/// Indication of PS state change for a P2P VIF
	MM_P2P_VIF_PS_CHANGE_IND,
	/// Indication that CSA counter has been updated
	MM_CSA_COUNTER_IND,
	/// Channel occupation report indication
	MM_CHANNEL_SURVEY_IND,
	/// Message containing Beamformer Information
	MM_BFMER_ENABLE_REQ, // 80
	/// Request to Start/Stop/Update NOA - GO Only
	MM_SET_P2P_NOA_REQ,
	/// Request to Start/Stop/Update Opportunistic PS - GO Only
	MM_SET_P2P_OPPPS_REQ,
	/// Start/Stop/Update NOA Confirmation
	MM_SET_P2P_NOA_CFM,
	/// Start/Stop/Update Opportunistic PS Confirmation
	MM_SET_P2P_OPPPS_CFM,
	/// P2P NoA Update Indication - GO Only
	MM_P2P_NOA_UPD_IND,
	/// Request to set RSSI threshold and RSSI hysteresis
	MM_CFG_RSSI_REQ,
	/// Indication that RSSI level is below or above the threshold
	MM_RSSI_STATUS_IND,
	/// Indication that CSA is done
	MM_CSA_FINISH_IND,
	/// Indication that CSA is in prorgess (resp. done) and traffic must be stopped (resp. restarted)
	MM_CSA_TRAFFIC_IND,
	/// Request to update the group information of a station
	MM_MU_GROUP_UPDATE_REQ, // 90
	/// Confirmation of the @ref MM_MU_GROUP_UPDATE_REQ message
	MM_MU_GROUP_UPDATE_CFM,
	/// Request to initialize the antenna diversity algorithm
	MM_ANT_DIV_INIT_REQ,
	/// Request to stop the antenna diversity algorithm
	MM_ANT_DIV_STOP_REQ,
	/// Request to update the antenna switch status
	MM_ANT_DIV_UPDATE_REQ,
	/// Request to switch the antenna connected to path_0
	MM_SWITCH_ANTENNA_REQ,
	/// Indication that a packet loss has occurred
	MM_PKTLOSS_IND,
	/// MU EDCA PARAMETERS Configuration Request.
	MM_SET_MU_EDCA_REQ,
	/// MU EDCA PARAMETERS Configuration Confirmation.
	MM_SET_MU_EDCA_CFM,
	/// UORA PARAMETERS Configuration Request.
	MM_SET_UORA_REQ,
	/// UORA PARAMETERS Configuration Confirmation.
	MM_SET_UORA_CFM, // 100
	/// TXOP RTS THRESHOLD Configuration Request.
	MM_SET_TXOP_RTS_THRES_REQ,
	/// TXOP RTS THRESHOLD Configuration Confirmation.
	MM_SET_TXOP_RTS_THRES_CFM,
	/// HE BSS Color Configuration Request.
	MM_SET_BSS_COLOR_REQ,
	/// HE BSS Color Configuration Confirmation.
	MM_SET_BSS_COLOR_CFM,
	/// Test mode Request
	MM_TEST_MODE_REQ,
	/// Test mode Confirmation
	MM_TEST_MODE_CFM,
	/// pta config Request
	MM_PTA_CFG_REQ,
	/// pta config  Confirmation
	MM_PTA_CFG_CFM,
	/// set mac/ipv4/v6/arp/icmp NS address filter
	MM_SET_PACKET_FILTER_REQ,
	/// set mac/ipv4/v6/arp/icmp NS address filter confirm
	MM_SET_PACKET_FILTER_CFM,
	/// set arp keep alive period and ip address req
	MM_SET_ARP_KEEPALIVE_REQ,
	/// set arp keep alive period and ip address cnf
	MM_SET_ARP_KEEPALIVE_CFM,
	/// Custom Request, add by BES, 2023-3-4
	MM_CUSTOM_REQ,
	///0x72, Custom Confirmation, add by BES, 2023-3-4
	MM_CUSTOM_CFM,
	/*
	 * BES add external MM messages section. Other new MM messages should be defined below.
	 */
	MM_EXT_MSG_START = 0x90,
	/// Wi-Fi & BT coexistence event Indication
	MM_COEX_EVENT_IND = MM_EXT_MSG_START,
	/// CSI Cap Request
	MM_CSI_CAP_REQ,
	/// CSI Cap Confirmation
	MM_CSI_CAP_CFM,
	/// CSI Cap Indication
	MM_CSI_CAP_IND,
	/// RC Update Indication
	MM_RC_UPDATE_IND,
	/// Anomaly Detect Indication
	MM_ANOMALY_IND,
	/// Sched Scan Result Indication
	MM_SCHED_SCAN_RESULT_IND,
	/// Sched Scan Start Indication
	MM_SCHED_SCAN_START_IND,
	/// Sched Scan Stop Indication
	MM_SCHED_SCAN_STOP_IND,
	/// Diagnose Chip Reg Indication
	MM_DIAG_REG_IND,

	/// MAX number of messages
	MM_MAX,
};

/// Interface types
enum {
	/// ESS STA interface
	MM_STA,
	/// IBSS STA interface
	MM_IBSS,
	/// AP interface
	MM_AP,
	// Mesh Point interface
	MM_MESH_POINT,
	// Monitor interface
	MM_MONITOR,
};

/// BA agreement types
enum {
	/// BlockAck agreement for TX
	BA_AGMT_TX,
	/// BlockAck agreement for RX
	BA_AGMT_RX,
};

/// BA agreement related status
enum {
	/// Correct BA agreement establishment
	BA_AGMT_ESTABLISHED,
	/// BA agreement already exists for STA+TID requested, cannot override it (should have been deleted first)
	BA_AGMT_ALREADY_EXISTS,
	/// Correct BA agreement deletion
	BA_AGMT_DELETED,
	/// BA agreement for the (STA, TID) doesn't exist so nothing to delete
	BA_AGMT_DOESNT_EXIST,
};

/// Features supported by LMAC - Positions
enum mm_features {
	/// Beaconing
	MM_FEAT_BCN_BIT = 0,
	/// Radar Detection
	MM_FEAT_RADAR_BIT,
	/// Power Save
	MM_FEAT_PS_BIT,
	/// UAPSD
	MM_FEAT_UAPSD_BIT,
	/// A-MPDU
	MM_FEAT_AMPDU_BIT,
	/// A-MSDU
	MM_FEAT_AMSDU_BIT,
	/// P2P
	MM_FEAT_P2P_BIT,
	/// P2P Go
	MM_FEAT_P2P_GO_BIT,
	/// UMAC Present
	MM_FEAT_UMAC_BIT,
	/// VHT support
	MM_FEAT_VHT_BIT,
	/// Beamformee
	MM_FEAT_BFMEE_BIT,
	/// Beamformer
	MM_FEAT_BFMER_BIT,
	/// WAPI
	MM_FEAT_WAPI_BIT,
	/// MFP
	MM_FEAT_MFP_BIT,
	/// Mu-MIMO RX support
	MM_FEAT_MU_MIMO_RX_BIT,
	/// Mu-MIMO TX support
	MM_FEAT_MU_MIMO_TX_BIT,
	/// Wireless Mesh Networking
	MM_FEAT_MESH_BIT,
	/// TDLS support
	MM_FEAT_TDLS_BIT,
	/// Antenna Diversity support
	MM_FEAT_ANT_DIV_BIT,
	/// UF support
	MM_FEAT_UF_BIT,
	/// A-MSDU maximum size (bit0)
	MM_AMSDU_MAX_SIZE_BIT0,
	/// A-MSDU maximum size (bit1)
	MM_AMSDU_MAX_SIZE_BIT1,
	/// MON_DATA support
	MM_FEAT_MON_DATA_BIT,
	/// HE (802.11ax) support
	MM_FEAT_HE_BIT,
	/// TWT support
	MM_FEAT_TWT_BIT,
	/// FTM initiator support
	MM_FEAT_FTM_INIT_BIT,
	/// Fake FTM responder support
	MM_FEAT_FAKE_FTM_RSP_BIT,
};

/// Maximum number of words in the configuration buffer
#define PHY_CFG_BUF_SIZE 16

/// Structure containing the parameters of the PHY configuration
struct phy_cfg_tag {
	/// Buffer containing the parameters specific for the PHY used
	u32_l parameters[PHY_CFG_BUF_SIZE];
};

/// Structure containing the parameters of the Trident PHY configuration
struct phy_trd_cfg_tag {
	/// MDM type(nxm)(upper nibble) and MDM2RF path mapping(lower nibble)
	u8_l path_mapping;
	/// TX DC offset compensation
	u32_l tx_dc_off_comp;
};

/// Structure containing the parameters of the Karst PHY configuration
struct phy_karst_cfg_tag {
	/// TX IQ mismatch compensation in 2.4GHz
	u32_l tx_iq_comp_2_4G[2];
	/// RX IQ mismatch compensation in 2.4GHz
	u32_l rx_iq_comp_2_4G[2];
	/// TX IQ mismatch compensation in 5GHz
	u32_l tx_iq_comp_5G[2];
	/// RX IQ mismatch compensation in 5GHz
	u32_l rx_iq_comp_5G[2];
	/// RF path used by default (0 or 1)
	u8_l path_used;
};

/// Structure containing the parameters of the Cataxia PHY configuration
struct phy_cataxia_cfg_tag {
	/// RF path used by default (0 or 1)
	u8_l path_used;
};

/// Structure containing the parameters of the @ref MM_START_REQ message
struct mm_start_req {
	/// PHY configuration
	struct phy_cfg_tag phy_cfg;
	/// UAPSD timeout
	u32_l uapsd_timeout;
	/// Local LP clock accuracy (in ppm)
	u16_l lp_clk_accuracy;
	/// Array of TX timeout values (in ms, one per TX queue) - 0 sets default value
	u16_l tx_timeout[AC_MAX];
	/// Size of Host RX buffer (in bytes)
	u16_l rx_hostbuf_size;
};

/// Structure containing the parameters of the @ref MM_SET_CHANNEL_REQ message
struct mm_set_channel_req {
	/// Channel information
	struct mac_chan_op chan;
	/// Index of the RF for which the channel has to be set (0: operating (primary), 1: secondary
	/// RF (used for additional radar detection). This parameter is reserved if no secondary RF
	/// is available in the system
	u8_l index;
};

/// Structure containing the parameters of the @ref MM_SET_CHANNEL_CFM message
struct mm_set_channel_cfm {
	/// Radio index to be used in policy table
	u8_l radio_idx;
	/// TX power configured (in dBm)
	s8_l power;
};

/// Structure containing the parameters of the @ref MM_SET_DTIM_REQ message
struct mm_set_dtim_req {
	/// DTIM period
	u8_l dtim_period;
};

/// Structure containing the parameters of the @ref MM_SET_POWER_REQ message
struct mm_set_power_req {
	/// Index of the interface for which the parameter is configured
	u8_l inst_nbr;
	/// TX power (in dBm)
	s8_l power;
};

/// Structure containing the parameters of the @ref MM_SET_POWER_CFM message
struct mm_set_power_cfm {
	/// Radio index to be used in policy table
	u8_l radio_idx;
	/// TX power configured (in dBm)
	s8_l power;
};

/// Structure containing the parameters of the @ref MM_SET_BEACON_INT_REQ message
struct mm_set_beacon_int_req {
	/// Beacon interval
	u16_l beacon_int;
	/// Index of the interface for which the parameter is configured
	u8_l inst_nbr;
};

/// Structure containing the parameters of the @ref MM_SET_BASIC_RATES_REQ message
struct mm_set_basic_rates_req {
	/// Basic rate set (as expected by bssBasicRateSet field of Rates MAC HW register)
	u32_l rates;
	/// Index of the interface for which the parameter is configured
	u8_l inst_nbr;
	/// Band on which the interface will operate
	u8_l band;
};

/// Structure containing the parameters of the @ref MM_SET_BSSID_REQ message
struct mm_set_bssid_req {
	/// BSSID to be configured in HW
	struct mac_addr bssid;
	/// Index of the interface for which the parameter is configured
	u8_l inst_nbr;
};

/// Structure containing the parameters of the @ref MM_SET_FILTER_REQ message
struct mm_set_filter_req {
	/// RX filter to be put into rxCntrlReg HW register
	u32_l filter;
};

/// Structure containing the parameters of the @ref MM_ADD_IF_REQ message.
struct mm_add_if_req {
	/// Type of the interface (AP, STA, ADHOC, ...)
	u8_l type;
	/// MAC ADDR of the interface to start
	struct mac_addr addr;
	/// P2P Interface
	bool_l p2p;
};

/// Structure containing the parameters of the @ref MM_SET_EDCA_REQ message
struct mm_set_edca_req {
	/// EDCA parameters of the queue (as expected by edcaACxReg HW register)
	u32_l ac_param;
	/// Flag indicating if UAPSD can be used on this queue
	bool_l uapsd;
	/// HW queue for which the parameters are configured
	u8_l hw_queue;
	/// Index of the interface for which the parameters are configured
	u8_l inst_nbr;
};

/// Structure containing the parameters of the @ref MM_SET_MU_EDCA_REQ message
struct mm_set_mu_edca_req {
	/// MU EDCA parameters of the different HE queues
	u32_l param[AC_MAX];
};

/// Structure containing the parameters of the @ref MM_SET_UORA_REQ message
struct mm_set_uora_req {
	/// Minimum exponent of OFDMA Contention Window.
	u8_l eocw_min;
	/// Maximum exponent of OFDMA Contention Window.
	u8_l eocw_max;
};

/// Structure containing the parameters of the @ref MM_SET_TXOP_RTS_THRES_REQ message
struct mm_set_txop_rts_thres_req {
	/// TXOP RTS threshold
	u16_l txop_dur_rts_thres;
	/// Index of the interface for which the parameter is configured
	u8_l inst_nbr;
};

/// Structure containing the parameters of the @ref MM_SET_BSS_COLOR_REQ message
struct mm_set_bss_color_req {
	/// HE BSS color, formatted as per BSS_COLOR MAC HW register
	u32_l bss_color;
};

struct mm_set_idle_req {
	u8_l hw_idle;
};

/// Structure containing the parameters of the @ref MM_SET_SLOTTIME_REQ message
struct mm_set_slottime_req {
	/// Slot time expressed in us
	u8_l slottime;
};

/// Structure containing the parameters of the @ref MM_SET_MODE_REQ message
struct mm_set_mode_req {
	/// abgnMode field of macCntrl1Reg register
	u8_l abgnmode;
};

/// Structure containing the parameters of the @ref MM_SET_VIF_STATE_REQ message
struct mm_set_vif_state_req {
	/// Association Id received from the AP (valid only if the VIF is of STA type)
	u16_l aid;
	/// Flag indicating if the VIF is active or not
	bool_l active;
	/// Interface index
	u8_l inst_nbr;
};

/// Structure containing the parameters of the @ref MM_ADD_IF_CFM message.
struct mm_add_if_cfm {
	/// Status of operation (different from 0 if unsuccessful)
	u8_l status;
	/// Interface index assigned by the LMAC
	u8_l inst_nbr;
};

/// Structure containing the parameters of the @ref MM_REMOVE_IF_REQ message.
struct mm_remove_if_req {
	/// Interface index assigned by the LMAC
	u8_l inst_nbr;
};

/// Structure containing the parameters of the @ref MM_VERSION_CFM message.
struct mm_version_cfm {
	/// Version of the LMAC FW
	u32_l version_lmac;
	/// Version1 of the MAC HW (as encoded in version1Reg MAC HW register)
	u32_l version_machw_1;
	/// Version2 of the MAC HW (as encoded in version2Reg MAC HW register)
	u32_l version_machw_2;
	/// Version1 of the PHY (depends on actual PHY)
	u32_l version_phy_1;
	/// Version2 of the PHY (depends on actual PHY)
	u32_l version_phy_2;
	/// Supported Features
	u32_l features;
	/// Maximum number of supported stations
	u16_l max_sta_nb;
	/// Maximum number of supported virtual interfaces
	u8_l max_vif_nb;
};

/// Structure containing the parameters of the @ref MM_STA_ADD_REQ message.
struct mm_sta_add_req {
	/// Bitfield showing some capabilities of the STA (@ref enum mac_sta_flags)
	u32_l capa_flags;
	/// Maximum A-MPDU size, in bytes, for HE frames
	u32_l ampdu_size_max_he;
	/// Maximum A-MPDU size, in bytes, for VHT frames
	u32_l ampdu_size_max_vht;
	/// PAID/GID
	u32_l paid_gid;
	/// Maximum A-MPDU size, in bytes, for HT frames
	u16_l ampdu_size_max_ht;
	/// MAC address of the station to be added
	struct mac_addr mac_addr;
	/// A-MPDU spacing, in us
	u8_l ampdu_spacing_min;
	/// Interface index
	u8_l inst_nbr;
	/// TDLS station
	bool_l tdls_sta;
	/// Indicate if the station is TDLS link initiator station
	bool_l tdls_sta_initiator;
	/// Indicate if the TDLS Channel Switch is allowed
	bool_l tdls_chsw_allowed;
	/// nonTransmitted BSSID index, set to the BSSID index in case the STA added is an AP
	/// that is a nonTransmitted BSSID. Should be set to 0 otherwise
	u8_l bssid_index;
	/// Maximum BSSID indicator, valid if the STA added is an AP that is a nonTransmitted
	/// BSSID
	u8_l max_bssid_ind;
};

/// Structure containing the parameters of the @ref MM_STA_ADD_CFM message.
struct mm_sta_add_cfm {
	/// Status of the operation (different from 0 if unsuccessful)
	u8_l status;
	/// Index assigned by the LMAC to the newly added station
	u8_l sta_idx;
	/// MAC HW index of the newly added station
	u8_l hw_sta_idx;
};

/// Structure containing the parameters of the @ref MM_STA_DEL_REQ message.
struct mm_sta_del_req {
	/// Index of the station to be deleted
	u8_l sta_idx;
};

/// Structure containing the parameters of the @ref MM_STA_DEL_CFM message.
struct mm_sta_del_cfm {
	/// Status of the operation (different from 0 if unsuccessful)
	u8_l status;
};

/// Structure containing the parameters of the SET_POWER_MODE REQ message.
struct mm_setpowermode_req {
	u8_l mode;
	u8_l sta_idx;
};

/// Structure containing the parameters of the SET_POWER_MODE CFM message.
struct mm_setpowermode_cfm {
	u8_l status;
};

/// Structure containing the parameters of the @ref MM_KEY_ADD REQ message.
struct mm_key_add_req {
	/// Key index (valid only for default keys)
	u8_l key_idx;
	/// STA index (valid only for pairwise or mesh group keys)
	u8_l sta_idx;
	/// Key material
	struct mac_sec_key key;
	/// Cipher suite (WEP64, WEP128, TKIP, CCMP)
	u8_l cipher_suite;
	/// Index of the interface for which the key is set (valid only for default keys or mesh group keys)
	u8_l inst_nbr;
	/// A-MSDU SPP parameter
	u8_l spp;
	/// Indicate if provided key is a pairwise key or not
	bool_l pairwise;
};

/// Structure containing the parameters of the @ref MM_KEY_ADD_CFM message.
struct mm_key_add_cfm {
	/// Status of the operation (different from 0 if unsuccessful)
	u8_l status;
	/// HW index of the key just added
	u8_l hw_key_idx;
};

/// Structure containing the parameters of the @ref MM_KEY_DEL_REQ message.
struct mm_key_del_req {
	/// HW index of the key to be deleted
	u8_l hw_key_idx;
};

/// Structure containing the parameters of the @ref MM_BA_ADD_REQ message.
struct mm_ba_add_req {
	/// Type of agreement (0: TX, 1: RX)
	u8_l type;
	/// Index of peer station with which the agreement is made
	u8_l sta_idx;
	/// TID for which the agreement is made with peer station
	u8_l tid;
	/// Buffer size - number of MPDUs that can be held in its buffer per TID
	u8_l bufsz;
	/// Start sequence number negotiated during BA setup - the one in first aggregated MPDU counts more
	u16_l ssn;
};

/// Structure containing the parameters of the @ref MM_BA_ADD_CFM message.
struct mm_ba_add_cfm {
	/// Index of peer station for which the agreement is being confirmed
	u8_l sta_idx;
	/// TID for which the agreement is being confirmed
	u8_l tid;
	/// Status of ba establishment
	u8_l status;
};

/// Structure containing the parameters of the @ref MM_BA_DEL_REQ message.
struct mm_ba_del_req {
	/// Type of agreement (0: TX, 1: RX)
	u8_l type;
	/// Index of peer station for which the agreement is being deleted
	u8_l sta_idx;
	/// TID for which the agreement is being deleted
	u8_l tid;
};

/// Structure containing the parameters of the @ref MM_BA_DEL_CFM message.
struct mm_ba_del_cfm {
	/// Index of peer station for which the agreement deletion is being confirmed
	u8_l sta_idx;
	/// TID for which the agreement deletion is being confirmed
	u8_l tid;
	/// Status of ba deletion
	u8_l status;
};

/// Structure containing the parameters of the @ref MM_CHAN_CTXT_ADD_REQ message
struct mm_chan_ctxt_add_req {
	/// Operating channel
	struct mac_chan_op chan;
};

/// Structure containing the parameters of the @ref MM_CHAN_CTXT_ADD_REQ message
struct mm_chan_ctxt_add_cfm {
	/// Status of the addition
	u8_l status;
	/// Index of the new channel context
	u8_l index;
};

/// Structure containing the parameters of the @ref MM_CHAN_CTXT_DEL_REQ message
struct mm_chan_ctxt_del_req {
	/// Index of the new channel context to be deleted
	u8_l index;
};

/// Structure containing the parameters of the @ref MM_CHAN_CTXT_LINK_REQ message
struct mm_chan_ctxt_link_req {
	/// VIF index
	u8_l vif_index;
	/// Channel context index
	u8_l chan_index;
	/// Indicate if this is a channel switch (unlink current ctx first if true)
	u8_l chan_switch;
};

/// Structure containing the parameters of the @ref MM_CHAN_CTXT_UNLINK_REQ message
struct mm_chan_ctxt_unlink_req {
	/// VIF index
	u8_l vif_index;
};

/// Structure containing the parameters of the @ref MM_CHAN_CTXT_UPDATE_REQ message
struct mm_chan_ctxt_update_req {
	/// Channel context index
	u8_l chan_index;
	/// New channel information
	struct mac_chan_op chan;
};

/// Structure containing the parameters of the @ref MM_CHAN_CTXT_SCHED_REQ message
struct mm_chan_ctxt_sched_req {
	/// VIF index
	u8_l vif_index;
	/// Channel context index
	u8_l chan_index;
	/// Type of the scheduling request (0: normal scheduling, 1: derogatory
	/// scheduling)
	u8_l type;
};

/// Structure containing the parameters of the @ref MM_CHANNEL_SWITCH_IND message
struct mm_channel_switch_ind {
	/// Index of the channel context we will switch to
	u8_l chan_index;
	/// Indicate if the switch has been triggered by a Remain on channel request
	bool_l roc;
	/// VIF on which remain on channel operation has been started (if roc == 1)
	u8_l vif_index;
	/// Indicate if the switch has been triggered by a TDLS Remain on channel request
	bool_l roc_tdls;
};

/// Structure containing the parameters of the @ref MM_CHANNEL_PRE_SWITCH_IND message
struct mm_channel_pre_switch_ind {
	/// Index of the channel context we will switch to
	u8_l chan_index;
};

/// Structure containing the parameters of the @ref MM_CONNECTION_LOSS_IND message.
struct mm_connection_loss_ind {
	/// VIF instance number
	u8_l inst_nbr;
};

/// Structure containing the parameters of the @ref MM_DBG_TRIGGER_REQ message.
struct mm_dbg_trigger_req {
	/// Error trace to be reported by the LMAC
	char error[64];
};

/// Structure containing the parameters of the @ref MM_SET_PS_MODE_REQ message.
struct mm_set_ps_mode_req {
	/// Power Save is activated or deactivated
	u8_l new_state;
};

/// Structure containing the parameters of the @ref MM_BCN_CHANGE_REQ message.
#define BCN_MAX_CSA_CPT 2
struct mm_bcn_change_req {
	/// Pointer, in host memory, to the new beacon template
	u32_l bcn_ptr;
	/// Length of the beacon template
	u16_l bcn_len;
	/// Offset of the TIM IE in the beacon
	u16_l tim_oft;
	/// Length of the TIM IE
	u8_l tim_len;
	/// Index of the VIF for which the beacon is updated
	u8_l inst_nbr;
	/// Offset of CSA (channel switch announcement) counters (0 means no counter)
	u8_l csa_oft[BCN_MAX_CSA_CPT];
#if !defined(CONFIG_RWNX_PCIE) && !defined(CONFIG_BES_IBUS)
	u8_l payload[];
#endif
};

/// Structure containing the parameters of the @ref MM_TIM_UPDATE_REQ message.
struct mm_tim_update_req {
	/// Association ID of the STA the bit of which has to be updated (0 for BC/MC traffic)
	u16_l aid;
	/// Flag indicating the availability of data packets for the given STA
	u8_l tx_avail;
	/// Index of the VIF for which the TIM is updated
	u8_l inst_nbr;
};

/// Structure containing the parameters of the @ref MM_REMAIN_ON_CHANNEL_REQ message.
struct mm_remain_on_channel_req {
	/// Operation Code
	u8_l op_code;
	/// VIF Index
	u8_l vif_index;
	/// Channel parameter
	struct mac_chan_op chan;
	/// Duration (in ms)
	u32_l duration_ms;
};

/// Structure containing the parameters of the @ref MM_REMAIN_ON_CHANNEL_CFM message
struct mm_remain_on_channel_cfm {
	/// Operation Code
	u8_l op_code;
	/// Status of the operation
	u8_l status;
	/// Channel Context index
	u8_l chan_ctxt_index;
};

/// Structure containing the parameters of the @ref MM_REMAIN_ON_CHANNEL_EXP_IND message
struct mm_remain_on_channel_exp_ind {
	/// VIF Index
	u8_l vif_index;
	/// Channel Context index
	u8_l chan_ctxt_index;
	/// Frequency of the channel
	u16_l freq;
};

/// Structure containing the parameters of the @ref MM_SET_UAPSD_TMR_REQ message.
struct mm_set_uapsd_tmr_req {
	/// action: Start or Stop the timer
	u8_l action;
	/// timeout value, in milliseconds
	u32_l timeout;
};

/// Structure containing the parameters of the @ref MM_SET_UAPSD_TMR_CFM message.
struct mm_set_uapsd_tmr_cfm {
	/// Status of the operation (different from 0 if unsuccessful)
	u8_l status;
};

/// Structure containing the parameters of the @ref MM_PS_CHANGE_IND message
struct mm_ps_change_ind {
	/// Index of the peer device that is switching its PS state
	u8_l sta_idx;
	/// New PS state of the peer device (0: active, 1: sleeping)
	u8_l ps_state;
};

/// Structure containing the parameters of the @ref MM_P2P_VIF_PS_CHANGE_IND message
struct mm_p2p_vif_ps_change_ind {
	/// Index of the P2P VIF that is switching its PS state
	u8_l vif_index;
	/// New PS state of the P2P VIF interface (0: active, 1: sleeping)
	u8_l ps_state;
};

/// Structure containing the parameters of the @ref MM_TRAFFIC_REQ_IND message
struct mm_traffic_req_ind {
	/// Index of the peer device that needs traffic
	u8_l sta_idx;
	/// Number of packets that need to be sent (if 0, all buffered traffic shall be sent and
	/// if set to @ref PS_SP_INTERRUPTED, it means that current service period has been interrupted)
	u8_l pkt_cnt;
	/// Flag indicating if the traffic request concerns U-APSD queues or not
	bool_l uapsd;
};

/// Structure containing the parameters of the @ref MM_SET_PS_OPTIONS_REQ message.
struct mm_set_ps_options_req {
	/// VIF Index
	u8_l vif_index;
	/// Listen interval (0 if wake up shall be based on DTIM period)
	u16_l listen_interval;
	/// Flag indicating if we shall listen the BC/MC traffic or not
	bool_l dont_listen_bc_mc;
};

/// Structure containing the parameters of the @ref MM_CSA_COUNTER_IND message
struct mm_csa_counter_ind {
	/// Index of the VIF
	u8_l vif_index;
	/// Updated CSA counter value
	u8_l csa_count;
};

/// Structure containing the parameters of the @ref MM_CHANNEL_SURVEY_IND message
struct mm_channel_survey_ind {
	/// Frequency of the channel
	u16_l freq;
	/// Noise in dbm
	s8_l noise_dbm;
	/// Amount of time spent of the channel (in ms)
	u32_l chan_time_ms;
	/// Amount of time the primary channel was sensed busy
	u32_l chan_time_busy_ms;
};

/// Structure containing the parameters of the @ref MM_BFMER_ENABLE_REQ message.
struct mm_bfmer_enable_req {
	/**
	 * Address of the beamforming report space allocated in host memory
	 * (Valid only if vht_su_bfmee is true)
	 */
	u32_l host_bfr_addr;
	/**
	 * Size of the beamforming report space allocated in host memory. This space should
	 * be twice the maximum size of the expected beamforming reports as the FW will
	 * divide it in two in order to be able to upload a new report while another one is
	 * used in transmission
	 */
	u16_l host_bfr_size;
	/// AID
	u16_l aid;
	/// Station Index
	u8_l sta_idx;
	/// Maximum number of spatial streams the station can receive
	u8_l rx_nss;
	/**
	 * Indicate if peer STA is MU Beamformee (VHT) capable
	 * (Valid only if vht_su_bfmee is true)
	 */
	bool_l vht_mu_bfmee;
};

/// Structure containing the parameters of the @ref MM_SET_P2P_NOA_REQ message.
struct mm_set_p2p_noa_req {
	/// VIF Index
	u8_l vif_index;
	/// Allocated NOA Instance Number - Valid only if count = 0
	u8_l noa_inst_nb;
	/// Count
	u8_l count;
	/// Indicate if NoA can be paused for traffic reason
	bool_l dyn_noa;
	/// Duration (in us)
	u32_l duration_us;
	/// Interval (in us)
	u32_l interval_us;
	/// Start Time offset from next TBTT (in us)
	u32_l start_offset;
};

/// Structure containing the parameters of the @ref MM_SET_P2P_OPPPS_REQ message.
struct mm_set_p2p_oppps_req {
	/// VIF Index
	u8_l vif_index;
	/// CTWindow
	u8_l ctwindow;
};

/// Structure containing the parameters of the @ref MM_SET_P2P_NOA_CFM message.
struct mm_set_p2p_noa_cfm {
	/// Request status
	u8_l status;
};

/// Structure containing the parameters of the @ref MM_SET_P2P_OPPPS_CFM message.
struct mm_set_p2p_oppps_cfm {
	/// Request status
	u8_l status;
};

/// Structure containing the parameters of the @ref MM_P2P_NOA_UPD_IND message.
struct mm_p2p_noa_upd_ind {
	/// VIF Index
	u8_l vif_index;
	/// NOA Instance Number
	u8_l noa_inst_nb;
	/// NoA Type
	u8_l noa_type;
	/// Count
	u8_l count;
	/// Duration (in us)
	u32_l duration_us;
	/// Interval (in us)
	u32_l interval_us;
	/// Start Time
	u32_l start_time;
};

/// Structure containing the parameters of the @ref MM_CFG_RSSI_REQ message
struct mm_cfg_rssi_req {
	/// Index of the VIF
	u8_l vif_index;
	/// RSSI threshold
	s8_l rssi_thold;
	/// RSSI hysteresis
	u8_l rssi_hyst;
};

/// Structure containing the parameters of the @ref MM_RSSI_STATUS_IND message
struct mm_rssi_status_ind {
	/// Index of the VIF
	u8_l vif_index;
	/// Status of the RSSI
	bool_l rssi_status;
	/// Current RSSI
	s8_l rssi;
};

/// Structure containing the parameters of the @ref MM_PKTLOSS_IND message
struct mm_pktloss_ind {
	/// Index of the VIF
	u8_l vif_index;
	/// Address of the STA for which there is a packet loss
	struct mac_addr mac_addr;
	/// Number of packets lost
	u32 num_packets;
};

/// The maximum length of anomaly information
#define ANOMALY_INFO_MAX_LENGTH             (200)

struct timer_err_param
{
	/// Address of the timer
	uint32_t timer_adss;
	/// Value of the time argument
	uint32_t time_value;
	/// The location at which the function is called
	uint32_t location;
	/// The machw time
	uint32_t machw_time[3];
};

enum anomaly_detect_event {
	/// TBTT drift event
	ANOMALY_DETECT_TBTT_DRIFT = 0,
	/// TIM error event
	ANOMALY_DETECT_TIM_ERROR,
	/// ASSERT recovery event
	ANOMALY_DETECT_ASSERT_RECOVERY,
	/// Channel switch timeout
	ANOMALY_DETECT_CS_TIMEOUT,
	/// Timer set error
	ANOMALY_DETECT_TIMER_SET_ERROR,

	/// The Maximum anomaly event
	ANOMALY_DETECT_MAX
};

/// Structure containing the parameters of the @ref MM_ANOMALY_IND message
struct anomaly_detect_ind {
	/// Anomaly event Id
	u8_l id;
	/// Anomaly event information
	char info[ANOMALY_INFO_MAX_LENGTH];
};

struct sched_scan_result_ind {
	/// Index of the VIF
	uint8_t vif_idx;
	/// Identifies this request.
	uint64_t reqid;
};

enum coex_evt_id {
	/// Allow Coex Schedule Event
	COEX_EVT_SCHE_ALLOW,

	/// The Maximum Coex event
	MM_COEX_EVT_MAX
};

struct coex_evt_hdr {
	/// Event Id
	uint8_t id;
	/// The length of event hdr and parameter
	uint8_t len;
} __packed;

struct coex_bcn_detect_end_ind {
	/// The header
	struct coex_evt_hdr hdr;
	/// The vif index that finished beacon detection
	uint8_t vif_idx;
} __packed;

/// Structure containing the parameters of the @ref MM_CSA_FINISH_IND message
struct mm_csa_finish_ind {
	/// Index of the VIF
	u8_l vif_index;
	/// Status of the operation
	u8_l status;
	/// New channel ctx index
	u8_l chan_idx;
};

/// Structure containing the parameters of the @ref MM_CSA_TRAFFIC_IND message
struct mm_csa_traffic_ind {
	/// Index of the VIF
	u8_l vif_index;
	/// Is tx traffic enable or disable
	bool_l enable;
};

/// Structure containing the parameters of the @ref MM_MU_GROUP_UPDATE_REQ message.
/// Size allocated for the structure depends of the number of group
struct mm_mu_group_update_req {
	/// Station index
	u8_l sta_idx;
	/// Number of groups the STA belongs to
	u8_l group_cnt;
	/// Group information
	struct {
		/// Group Id
		u8_l group_id;
		/// User position
		u8_l user_pos;
	} groups[0];
};

///////////////////////////////////////////////////////////////////////////////
/////////// For Scan messages
///////////////////////////////////////////////////////////////////////////////
enum scan_msg_tag {
	/// Scanning start Request.
	SCAN_START_REQ = LMAC_FIRST_MSG(TASK_SCAN),
	/// Scanning start Confirmation.
	SCAN_START_CFM,
	/// End of scanning indication.
	SCAN_DONE_IND,
	/// Cancel scan request
	SCAN_CANCEL_REQ,
	/// Cancel scan confirmation
	SCAN_CANCEL_CFM,

	/// MAX number of messages
	SCAN_MAX,
};

/// Maximum number of SSIDs in a scan request
#define SCAN_SSID_MAX 2

/// Maximum number of channels in a scan request
#define SCAN_CHANNEL_MAX (MAC_DOMAINCHANNEL_24G_MAX + MAC_DOMAINCHANNEL_5G_MAX)

/// Maximum length of the ProbeReq IEs (SoftMAC mode)
#define SCAN_MAX_IE_LEN 300

/// Maximum number of PHY bands supported
#define SCAN_BAND_MAX 2

/// Maximum number of scheduled scan requests that
/// the device can run concurrently.
#define SCHED_SCANU_REQS_NUM_LIMIT 1

/// Maximum number of SSIDs the device can scan
/// for in any given scheduled scan
#define SCHED_SCANU_SSIDS_NUM_LIMIT SCAN_SSID_MAX

/// Maximum number of match sets the device can handle
/// when performing a scheduled scan, 0 if filtering is not
/// supported.
#define SCHED_SCANU_MATCH_SETS_NUM_LIMIT 10

/// Maximum length of user defined IEs device can
/// add to probe request frames transmitted during a scheduled scan, must not
/// include fixed IEs like supported rates
#define SCHED_SCANU_IE_LEN_LIMIT SCANU_MAX_IE_LEN

/// Maximum number of scan plans (scan interval and number
/// of iterations) for scheduled scan supported by the device.
#define SCHED_SCANU_SCHEME_NUM_LIMIT 3

/// Maximum interval (in seconds) for a
/// scan plan supported by the device.
#define SCHED_SCANU_SCHEME_INTERVAL_LIMIT 4096

/// Maximum number of iterations for a single
/// scan plan supported by the device.
/// And the firmware uses 8-bits for num of iterations, but 0xff is saved for
/// infinite loop, so the maximum number of iterations is actually (0xff-1).
#define SCHED_SCANU_SCHEME_ITERATIONS_LIMIT (0xff - 1)

/// Maximum delay (in seconds) for a
/// scan plan supported by the device.
#define SCHED_SCANU_DELAY_LIMIT 4096

/// Structure containing the parameters of the @ref SCAN_START_REQ message
struct scan_start_req {
	/// List of channel to be scanned
	struct mac_chan_def chan[SCAN_CHANNEL_MAX];
	/// List of SSIDs to be scanned
	struct mac_ssid ssid[SCAN_SSID_MAX];
	/// BSSID to be scanned
	struct mac_addr bssid;
	/// Pointer (in host memory) to the additional IEs that need to be added to the ProbeReq
	/// (following the SSID element)
	u32_l add_ies;
	/// Length of the additional IEs
	u16_l add_ie_len;
	/// Index of the VIF that is scanning
	u8_l vif_idx;
	/// Number of channels to scan
	u8_l chan_cnt;
	/// Number of SSIDs to scan for
	u8_l ssid_cnt;
	/// no CCK - For P2P frames not being sent at CCK rate in 2GHz band.
	bool no_cck;
	/// Scan duration, in us
	u32_l duration;
};

/// Structure containing the parameters of the @ref SCAN_START_CFM message
struct scan_start_cfm {
	/// Status of the request
	u8_l status;
};

/// Structure containing the parameters of the @ref SCAN_CANCEL_REQ message
struct scan_cancel_req {
};

/// Structure containing the parameters of the @ref SCAN_START_CFM message
struct scan_cancel_cfm {
	/// Status of the request
	u8_l status;
};

///////////////////////////////////////////////////////////////////////////////
/////////// For Scanu messages
///////////////////////////////////////////////////////////////////////////////
/// Messages that are logically related to the task.
enum {
	/// Scan request from host.
	SCANU_START_REQ = LMAC_FIRST_MSG(TASK_SCANU),
	/// Scanning start Confirmation.
	SCANU_START_CFM,
	/// Join request
	SCANU_JOIN_REQ,
	/// Join confirmation.
	SCANU_JOIN_CFM,
	/// Scan result indication.
	SCANU_RESULT_IND,
	/// Fast scan request from any other module.
	SCANU_FAST_REQ,
	/// Confirmation of fast scan request.
	SCANU_FAST_CFM,

	/// MAX number of messages
	SCANU_MAX,
};

/// Extend scan mode
enum scan_mode_ext {
	NORMAL_SCAN = 0,
	EXTEND_SCAN,
	SCAN_MODE_MAX
};

/// Scan extend param
struct scan_start_req_ext {
	/// Time between TX of successive probes. First probe goes out immediately after channel switch
	u32_l probe_interval_us;
	/// Max number probe requests to send out on each channel, limited to maximum(check in the driver)
	/// 0 is passive scan
	u8_l max_probes;
	/// Scan passive duration in us
	u32_l passive_duration;
	/// Revert status
	u8_l reserved[3];
};

/// Maximum length of the additional ProbeReq IEs (FullMAC mode)
#define SCANU_MAX_IE_LEN 200

/// Structure containing the parameters of the @ref SCANU_START_REQ message
struct scanu_start_req {
	/// List of channel to be scanned
	struct mac_chan_def chan[SCAN_CHANNEL_MAX];
	/// List of SSIDs to be scanned
	struct mac_ssid ssid[SCAN_SSID_MAX];
	/// BSSID to be scanned (or WILDCARD BSSID if no BSSID is searched in particular)
	struct mac_addr bssid;
	/// Address (in host memory) of the additional IEs that need to be added to the ProbeReq
	/// (following the SSID element)
	u32_l add_ies;
	/// Length of the additional IEs
	u16_l add_ie_len;
	/// Index of the VIF that is scanning
	u8_l vif_idx;
	/// Number of channels to scan
	u8_l chan_cnt;
	/// Number of SSIDs to scan for
	u8_l ssid_cnt;
	/// no CCK - For P2P frames not being sent at CCK rate in 2GHz band.
	bool no_cck;
	/// Scan active duration, in us
	u32_l duration;
	/// Scan extend param
	struct scan_start_req_ext ext;
#if !defined(CONFIG_RWNX_PCIE) && !defined(CONFIG_BES_IBUS)
	u8_l payload[];
#endif
};

/// Structure containing the parameters of the @ref SCANU_START_CFM message
struct scanu_start_cfm {
	/// Index of the VIF that was scanning
	u8_l vif_idx;
	/// Status of the request
	u8_l status;
	/// Number of scan results available
	u8_l result_cnt;
};

/// Rx frame legacy information
struct rx_leg_info {
	/// Format Modulation
	u32_l format_mod : 4;
	/// Channel Bandwidth
	u32_l ch_bw : 3;
	/// Preamble Type
	u32_l pre_type : 1;
	/// Legacy Length
	u32_l leg_length : 12;
	/// Legacy rate
	u32_l leg_rate : 4;
} __packed;

/// Parameters of the @SCANU_RESULT_IND message
struct scanu_result_ind {
	/// Length of the frame
	u16_l length;
	/// Frame control field of the frame.
	u16_l framectrl;
	/// Center frequency on which we received the packet
	u16_l center_freq;
	/// PHY band
	u8_l band;
	/// Index of the station that sent the frame. 0xFF if unknown.
	u8_l sta_idx;
	/// Index of the VIF that received the frame. 0xFF if unknown.
	u8_l inst_nbr;
	/// RSSI of the received frame.
	s8_l rssi;
	/// Rx frame legacy information
	struct rx_leg_info rx_leg_inf;
	/// Frame payload.
	u32_l payload[];
};

/// Structure containing the parameters of the message.
struct scanu_fast_req {
	/// The SSID to scan in the channel.
	struct mac_ssid ssid;
	/// BSSID.
	struct mac_addr bssid;
	/// Probe delay.
	u16_l probe_delay;
	/// Minimum channel time.
	u16_l minch_time;
	/// Maximum channel time.
	u16_l maxch_time;
	/// The channel number to scan.
	u16_l ch_nbr;
};

///////////////////////////////////////////////////////////////////////////////
/////////// For ME messages
///////////////////////////////////////////////////////////////////////////////
/// Messages that are logically related to the task.
enum {
	/// Configuration request from host.
	ME_CONFIG_REQ = LMAC_FIRST_MSG(TASK_ME),
	/// Configuration confirmation.
	ME_CONFIG_CFM,
	/// Configuration request from host.
	ME_CHAN_CONFIG_REQ,
	/// Configuration confirmation.
	ME_CHAN_CONFIG_CFM,
	/// Set control port state for a station.
	ME_SET_CONTROL_PORT_REQ,
	/// Control port setting confirmation.
	ME_SET_CONTROL_PORT_CFM,
	/// TKIP MIC failure indication.
	ME_TKIP_MIC_FAILURE_IND,
	/// Add a station to the FW (AP mode)
	ME_STA_ADD_REQ,
	/// Confirmation of the STA addition
	ME_STA_ADD_CFM,
	/// Delete a station from the FW (AP mode)
	ME_STA_DEL_REQ,
	/// Confirmation of the STA deletion
	ME_STA_DEL_CFM, // 10
			/// Indication of a TX RA/TID queue credit update
	ME_TX_CREDITS_UPDATE_IND,
	/// Request indicating to the FW that there is traffic buffered on host
	ME_TRAFFIC_IND_REQ,
	/// Confirmation that the @ref ME_TRAFFIC_IND_REQ has been executed
	ME_TRAFFIC_IND_CFM,
	/// Request of RC statistics to a station
	ME_RC_STATS_REQ,
	/// RC statistics confirmation
	ME_RC_STATS_CFM,
	/// RC fixed rate request
	ME_RC_SET_RATE_REQ,
	/// Configure monitor interface
	ME_CONFIG_MONITOR_REQ,
	/// Configure monitor interface response
	ME_CONFIG_MONITOR_CFM,
	/// Setting power Save mode request from host
	ME_SET_PS_MODE_REQ,
	/// Set power Save mode confirmation
	ME_SET_PS_MODE_CFM, // 20
	/// Internal request to indicate that a VIF needs to get the HW going to ACTIVE or IDLE
	ME_SET_ACTIVE_REQ,
	/// Confirmation that the switch to ACTIVE or IDLE has been executed
	ME_SET_ACTIVE_CFM,
	/// Internal request to indicate that a VIF desires to de-activate/activate the Power-Save mode
	ME_SET_PS_DISABLE_REQ,
	/// Confirmation that the PS state de-activate/activate has been executed
	ME_SET_PS_DISABLE_CFM,
	/// Indication that data path is flushed for a given station
	ME_DATA_PATH_FLUSHED_IND,
	/// MAX number of messages
	ME_MAX,
};

/// Structure containing the parameters of the @ref ME_START_REQ message
struct me_config_req {
	/// HT Capabilities
	struct mac_htcapability ht_cap;
	/// VHT Capabilities
	struct mac_vhtcapability vht_cap;
	/// HE capabilities
	struct mac_hecapability he_cap;
	/// Lifetime of packets sent under a BlockAck agreement (expressed in TUs)
	u16_l tx_lft;
	/// Maximum supported BW
	u8_l phy_bw_max;
	/// Boolean indicating if HT is supported or not
	bool_l ht_supp;
	/// Boolean indicating if VHT is supported or not
	bool_l vht_supp;
	/// Boolean indicating if HE is supported or not
	bool_l he_supp;
	/// Boolean indicating if HE OFDMA UL is enabled or not
	bool_l he_ul_on;
	/// Boolean indicating if PS mode shall be enabled or not
	bool_l ps_on;
	/// Boolean indicating if Antenna Diversity shall be enabled or not
	bool_l ant_div_on;
	/// Boolean indicating if Dynamic PS mode shall be used or not
	bool_l dpsm;
	/// Indicates whether AMSDU shall be forced or not (0-if advertised, 1-yes, 2-no)
	/// Use u8 to align the lmac enum type
	u8 amsdu_tx;
};

/// Structure containing the parameters of the @ref ME_CHAN_CONFIG_REQ message
struct me_chan_config_req {
	/// List of 2.4GHz supported channels
	struct mac_chan_def chan2G4[MAC_DOMAINCHANNEL_24G_MAX];
	/// List of 5GHz supported channels
	struct mac_chan_def chan5G[MAC_DOMAINCHANNEL_5G_MAX];
	/// Number of 2.4GHz channels in the list
	u8_l chan2G4_cnt;
	/// Number of 5GHz channels in the list
	u8_l chan5G_cnt;
};

/// Structure containing the parameters of the @ref ME_SET_CONTROL_PORT_REQ message
struct me_set_control_port_req {
	/// Index of the station for which the control port is opened
	u8_l sta_idx;
	/// Control port state
	bool_l control_port_open;
};

/// Structure containing the parameters of the @ref ME_TKIP_MIC_FAILURE_IND message
struct me_tkip_mic_failure_ind {
	/// Address of the sending STA
	struct mac_addr addr;
	/// TSC value
	u64_l tsc;
	/// Boolean indicating if the packet was a group or unicast one (true if group)
	bool_l ga;
	/// Key Id
	u8_l keyid;
	/// VIF index
	u8_l vif_idx;
};

/// Structure containing the parameters of the @ref ME_STA_ADD_REQ message
struct me_sta_add_req {
	/// MAC address of the station to be added
	struct mac_addr mac_addr;
	/// Supported legacy rates
	struct mac_rateset rate_set;
	/// HT Capabilities
	struct mac_htcapability ht_cap;
	/// VHT Capabilities
	struct mac_vhtcapability vht_cap;
	/// HE capabilities
	struct mac_hecapability he_cap;
	/// Flags giving additional information about the station (@ref mac_sta_flags)
	u32_l flags;
	/// Association ID of the station
	u16_l aid;
	/// Bit field indicating which queues have U-APSD enabled
	u8_l uapsd_queues;
	/// Maximum size, in frames, of a APSD service period
	u8_l max_sp_len;
	/// Operation mode information (valid if bit @ref STA_OPMOD_NOTIF is
	/// set in the flags)
	u8_l opmode;
	/// Index of the VIF the station is attached to
	u8_l vif_idx;
	/// Whether the the station is TDLS station
	bool_l tdls_sta;
	/// Indicate if the station is TDLS link initiator station
	bool_l tdls_sta_initiator;
	/// Indicate if the TDLS Channel Switch is allowed
	bool_l tdls_chsw_allowed;
};

/// Structure containing the parameters of the @ref ME_STA_ADD_CFM message
struct me_sta_add_cfm {
	/// Station index
	u8_l sta_idx;
	/// Status of the station addition
	u8_l status;
	/// PM state of the station
	u8_l pm_state;
};

/// Structure containing the parameters of the @ref ME_STA_DEL_REQ message.
struct me_sta_del_req {
	/// Index of the station to be deleted
	u8_l sta_idx;
	/// Whether the the station is TDLS station
	bool_l tdls_sta;
};

/// Structure containing the parameters of the @ref ME_TX_CREDITS_UPDATE_IND message.
struct me_tx_credits_update_ind {
	/// Index of the station for which the credits are updated
	u8_l sta_idx;
	/// TID for which the credits are updated
	u8_l tid;
	/// Offset to be applied on the credit count
	s8_l credits;
};

/// Structure containing the parameters of the @ref ME_DATA_PATH_FLUSHED_IND message.
struct me_data_path_flushed_ind
{
	/// Index of the station
	uint8_t sta_idx;
	/// General purpose user data provided at the end of the flushing process
	void *env;
};


/// Structure containing the parameters of the @ref ME_TRAFFIC_IND_REQ message.
struct me_traffic_ind_req {
	/// Index of the station for which UAPSD traffic is available on host
	u8_l sta_idx;
	/// Flag indicating the availability of UAPSD packets for the given STA
	u8_l tx_avail;
	/// Indicate if traffic is on uapsd-enabled queues
	bool_l uapsd;
};

/// Structure containing the parameters of the @ref ME_RC_STATS_REQ message.
struct me_rc_stats_req {
	/// Index of the station for which the RC statistics are requested
	u8_l sta_idx;
};

/// Structure containing the rate control statistics
struct rc_rate_stats {
	/// Number of attempts (per sampling interval)
	u16_l attempts;
	/// Number of success (per sampling interval)
	u16_l success;
	/// Estimated probability of success (EWMA)
	u16_l probability;
	/// Rate configuration of the sample
	u16_l rate_config;
	union {
		struct {
			/// Number of times the sample has been skipped (per sampling interval)
			u8_l sample_skipped;
			/// Whether the old probability is available
			bool_l old_prob_available;
			/// Whether the rate can be used in the retry chain
			bool_l rate_allowed;
		};
		struct {
			/// RU size and UL length received in the latest HE trigger frame
			u16_l ru_and_length;
		};
	};
};

/// Number of RC samples
#define RC_MAX_N_SAMPLE 10
/// Index of the HE statistics element in the table
#define RC_HE_STATS_IDX RC_MAX_N_SAMPLE

/// Structure containing the parameters of the @ref ME_RC_STATS_CFM message.
struct me_rc_stats_cfm {
	/// Index of the station for which the RC statistics are provided
	u8_l sta_idx;
	/// Number of samples used in the RC algorithm
	u16_l no_samples;
	/// Number of MPDUs transmitted (per sampling interval)
	u16_l ampdu_len;
	/// Number of AMPDUs transmitted (per sampling interval)
	u16_l ampdu_packets;
	/// Average number of MPDUs in each AMPDU frame (EWMA)
	u32_l avg_ampdu_len;
	// Current step 0 of the retry chain
	u8_l sw_retry_step;
	/// Trial transmission period
	u8_l sample_wait;
	/// Retry chain steps
	u16_l retry_step_idx[4];
	/// RC statistics - Max number of RC samples, plus one for the HE TB statistics
	struct rc_rate_stats rate_stats[RC_MAX_N_SAMPLE + 1];
	/// Throughput - Max number of RC samples, plus one for the HE TB statistics
	u32_l tp[RC_MAX_N_SAMPLE + 1];
};

/// Structure containing the parameters of the @ref ME_RC_SET_RATE_REQ message.
struct me_rc_set_rate_req {
	/// Index of the station for which the fixed rate is set
	u8_l sta_idx;
	/// Rate configuration to be set
	u16_l fixed_rate_cfg;
};

/// Structure containing the parameters of the @ref ME_CONFIG_MONITOR_REQ message.
struct me_config_monitor_req {
	/// Channel to configure
	struct mac_chan_op chan;
	/// Is channel data valid
	bool_l chan_set;
	/// Enable report of unsupported HT frames
	bool_l uf;
};

/// Structure containing the parameters of the @ref ME_CONFIG_MONITOR_CFM message.
struct me_config_monitor_cfm {
	/// Channel context index
	u8_l chan_index;
	/// Channel parameters
	struct mac_chan_op chan;
};

/// Structure containing the parameters of the @ref ME_SET_PS_MODE_REQ message.
struct me_set_ps_mode_req {
	/// Power Save is activated or deactivated
	u8_l ps_state;
};

///////////////////////////////////////////////////////////////////////////////
/////////// For TWT messages
///////////////////////////////////////////////////////////////////////////////
/// Message API of the TWT task
enum {
	/// Request Individual TWT Establishment
	TWT_SETUP_REQ = LMAC_FIRST_MSG(TASK_TWT),
	/// Confirm Individual TWT Establishment
	TWT_SETUP_CFM,
	/// Indicate TWT Setup response from peer
	TWT_SETUP_IND,
	/// Request to destroy a TWT Establishment or all of them
	TWT_TEARDOWN_REQ,
	/// Confirm to destroy a TWT Establishment or all of them
	TWT_TEARDOWN_CFM,

	/// MAX number of messages
	TWT_MAX,
};

/// TWT Group assignment
struct twt_grp_assignment_tag {
	/// TWT Group assignment byte array
	u8_l grp_assignment[9];
};

/// TWT Flow configuration
struct twt_conf_tag {
	/// Flow Type (0: Announced, 1: Unannounced)
	u8_l flow_type;
	/// Wake interval Exponent
	u8_l wake_int_exp;
	/// Unit of measurement of TWT Minimum Wake Duration (0:256us, 1:tu)
	bool_l wake_dur_unit;
	/// Nominal Minimum TWT Wake Duration
	u8_l min_twt_wake_dur;
	/// TWT Wake Interval Mantissa
	u16_l wake_int_mantissa;
};

/// TWT Setup request message structure
struct twt_setup_req {
	/// VIF Index
	u8_l vif_idx;
	/// Setup request type
	u8_l setup_type;
	/// TWT Setup configuration
	struct twt_conf_tag conf;
	/// Flow ID
	uint8_t flow_id;
};

/// TWT Setup confirmation message structure
struct twt_setup_cfm {
	/// Status (0 = TWT Setup Request has been transmitted to peer)
	u8_l status;
};

/// TWT Setup command
enum twt_setup_types {
	MAC_TWT_SETUP_REQ = 0,
	MAC_TWT_SETUP_SUGGEST,
	MAC_TWT_SETUP_DEMAND,
	MAC_TWT_SETUP_GROUPING,
	MAC_TWT_SETUP_ACCEPT,
	MAC_TWT_SETUP_ALTERNATE,
	MAC_TWT_SETUP_DICTATE,
	MAC_TWT_SETUP_REJECT,
};

/// TWT Setup indication message structure
struct twt_setup_ind {
	/// Response type
	u8_l resp_type;
	/// STA Index
	u8_l sta_idx;
	/// TWT Setup configuration
	struct twt_conf_tag conf;
};

/// TWT Teardown request message structure
struct twt_teardown_req {
	/// TWT Negotiation type
	u8_l neg_type;
	/// All TWT
	u8_l all_twt;
	/// TWT flow ID
	u8_l id;
	/// VIF Index
	u8_l vif_idx;
};

/// TWT Teardown confirmation message structure
struct twt_teardown_cfm {
	/// Status (0 = TWT Teardown Request has been transmitted to peer)
	u8_l status;
};

///////////////////////////////////////////////////////////////////////////////
/////////// For SM messages
///////////////////////////////////////////////////////////////////////////////
/// Message API of the SM task
enum sm_msg_tag {
	/// Request to connect to an AP
	SM_CONNECT_REQ = LMAC_FIRST_MSG(TASK_SM),
	/// Confirmation of connection
	SM_CONNECT_CFM,
	/// Indicates that the SM associated to the AP
	SM_CONNECT_IND,
	/// Request to disconnect
	SM_DISCONNECT_REQ,
	/// Confirmation of disconnection
	SM_DISCONNECT_CFM,
	/// Indicates that the SM disassociated the AP
	SM_DISCONNECT_IND,
	/// Request to start external authentication
	SM_EXTERNAL_AUTH_REQUIRED_IND,
	/// Response to external authentication request
	SM_EXTERNAL_AUTH_REQUIRED_RSP,
	/// Request to update assoc elements after FT over the air authentication
	SM_FT_AUTH_IND,
	/// Response to FT authentication with updated assoc elements
	SM_FT_AUTH_RSP,
	/// Transfer management frame to driver, add by BES, 2024-5-8
	SM_MGT_FRAME_IND,

	/// MAX number of messages
	SM_MAX,
};

/// Structure containing the parameters of @ref SM_CONNECT_REQ and SM_FT_AUTH_RSP message.
struct sm_connect_req {
	/// SSID to connect to
	struct mac_ssid ssid;
	/// BSSID to connect to (if not specified, set this field to WILDCARD BSSID)
	struct mac_addr bssid;
	/// Channel on which we have to connect (if not specified, set -1 in the chan.freq field)
	struct mac_chan_def chan;
	/// Connection flags (see @ref mac_connection_flags)
	u32_l flags;
	/// Control port Ethertype (in network endianness)
	u16_l ctrl_port_ethertype;
	/// Listen interval to be used for this connection
	u16_l listen_interval;
	/// Flag indicating if the we have to wait for the BC/MC traffic after beacon or not
	bool_l dont_wait_bcmc;
	/// Authentication type
	u8_l auth_type;
	/// UAPSD queues (bit0: VO, bit1: VI, bit2: BE, bit3: BK)
	u8_l uapsd_queues;
	/// VIF index
	u8_l vif_idx;
	/// Length of the association request IEs
	u16_l ie_len;
	/// Buffer containing the additional information elements to be put in the
	/// association request
	u32_l ie_buf[0];
};

/// Structure containing the parameters of the @ref SM_CONNECT_CFM message.
struct sm_connect_cfm {
	/// Status. If 0, it means that the connection procedure will be performed and that
	/// a subsequent @ref SM_CONNECT_IND message will be forwarded once the procedure is
	/// completed
	u8_l status;
};

#define SM_ASSOC_IE_LEN 800
/// Structure containing the parameters of the @ref SM_CONNECT_IND message.
struct sm_connect_ind {
	/// Status code of the connection procedure
	u16_l status_code;
	/// BSSID
	struct mac_addr bssid;
	/// Flag indicating if the indication refers to an internal roaming or from a host request
	bool_l roamed;
	/// Index of the VIF for which the association process is complete
	u8_l vif_idx;
	/// Index of the STA entry allocated for the AP
	u8_l ap_idx;
	/// Index of the LMAC channel context the connection is attached to
	u8_l ch_idx;
	/// Flag indicating if the AP is supporting QoS
	bool_l qos;
	/// ACM bits set in the AP WMM parameter element
	u8_l acm;
	/// Length of the AssocReq IEs
	u16_l assoc_req_ie_len;
	/// Length of the AssocRsp IEs
	u16_l assoc_rsp_ie_len;
	/// Association Id allocated by the AP for this connection
	u16_l aid;
	/// AP operating channel
	struct mac_chan_op chan;
	/// EDCA parameters
	u32_l ac_param[AC_MAX];
	/// IE buffer
	u32_l assoc_ie_buf[0];
};

/// Structure containing the parameters of the @ref SM_DISCONNECT_REQ message.
struct sm_disconnect_req {
	/// Reason of the deauthentication.
	u16_l reason_code;
	/// Index of the VIF.
	u8_l vif_idx;
};

/// Structure containing the parameters of SM_ASSOCIATION_IND the message
struct sm_association_ind {
	// MAC ADDR of the STA
	struct mac_addr me_mac_addr;
};

/// Structure containing the parameters of the @ref SM_DISCONNECT_IND message.
struct sm_disconnect_ind {
	/// Reason of the disconnection.
	u16_l reason_code;
	/// Index of the VIF.
	u8_l vif_idx;
	/// Disconnection happen before a re-association
	bool_l reassoc;
};

/// Structure containing the parameters of the @ref SM_EXTERNAL_AUTH_REQUIRED_IND
struct sm_external_auth_required_ind {
	/// Index of the VIF.
	u8_l vif_idx;
	/// SSID to authenticate to
	struct mac_ssid ssid;
	/// BSSID to authenticate to
	struct mac_addr bssid;
	/// AKM suite of the respective authentication
	u32_l akm;
};

/// Structure containing the parameters of the @ref SM_EXTERNAL_AUTH_REQUIRED_RSP
struct sm_external_auth_required_rsp {
	/// Index of the VIF.
	u8_l vif_idx;
	/// Authentication status
	u16_l status;
};

/// Structure containing the parameters of the @ref SM_FT_AUTH_IND
struct sm_ft_auth_ind {
	/// Index of the VIF.
	u8_l vif_idx;
	/// Size of the FT elements
	u16_l ft_ie_len;
	/// Fast Transition elements in the authentication
	u32_l ft_ie_buf[0];
};

/// Structure containing the parameters of the @ref SM_MGT_FRAME_IND
struct sm_mgt_frame_ind {
    /// Index of the VIF.
    uint8_t vif_idx;
    /// Tx or Rx
    bool tx;
    union {
        /// Tx Status
        uint32_t tx_st;
        /// Rx Rssi
        int8_t rx_rssi;
    };
    /// Total length of the buffer
    uint32_t length;
    /// Payload area
    uint32_t payload[];
};

///////////////////////////////////////////////////////////////////////////////
/////////// For APM messages
///////////////////////////////////////////////////////////////////////////////
/// Message API of the APM task
enum apm_msg_tag {
	/// Request to start the AP.
	APM_START_REQ = LMAC_FIRST_MSG(TASK_APM),
	/// Confirmation of the AP start.
	APM_START_CFM,
	/// Request to stop the AP.
	APM_STOP_REQ,
	/// Confirmation of the AP stop.
	APM_STOP_CFM,
	/// Request to start CAC
	APM_START_CAC_REQ,
	/// Confirmation of the CAC start
	APM_START_CAC_CFM,
	/// Request to stop CAC
	APM_STOP_CAC_REQ,
	/// Confirmation of the CAC stop
	APM_STOP_CAC_CFM,
	/// Request to Probe Client
	APM_PROBE_CLIENT_REQ,
	/// Confirmation of Probe Client
	APM_PROBE_CLIENT_CFM,
	/// Indication of Probe Client status
	APM_PROBE_CLIENT_IND,

	/// MAX number of messages
	APM_MAX,
};

/// Structure containing the parameters of the @ref APM_START_REQ message.
struct apm_start_req {
	/// Basic rate set
	struct mac_rateset basic_rates;
	/// Operating channel on which we have to enable the AP
	struct mac_chan_op chan;
	/// Address, in host memory, to the beacon template
	u32_l bcn_addr;
	/// Length of the beacon template
	u16_l bcn_len;
	/// Offset of the TIM IE in the beacon
	u16_l tim_oft;
	/// Beacon interval
	u16_l bcn_int;
	/// Flags (@ref mac_connection_flags)
	u32_l flags;
	/// Control port Ethertype
	u16_l ctrl_port_ethertype;
	/// Length of the TIM IE
	u8_l tim_len;
	/// Index of the VIF for which the AP is started
	u8_l vif_idx;
#if !defined(CONFIG_RWNX_PCIE) && !defined(CONFIG_BES_IBUS)
	u8_l payload[];
#endif
};

/// Structure containing the parameters of the @ref APM_START_CFM message.
struct apm_start_cfm {
	/// Status of the AP starting procedure
	u8_l status;
	/// Index of the VIF for which the AP is started
	u8_l vif_idx;
	/// Index of the channel context attached to the VIF
	u8_l ch_idx;
	/// Index of the STA used for BC/MC traffic
	u8_l bcmc_idx;
};

/// Structure containing the parameters of the @ref APM_STOP_REQ message.
struct apm_stop_req {
	/// Index of the VIF for which the AP has to be stopped
	u8_l vif_idx;
};

/// Structure containing the parameters of the @ref APM_START_CAC_REQ message.
struct apm_start_cac_req {
	/// Channel configuration
	struct mac_chan_op chan;
	/// Index of the VIF for which the CAC is started
	u8_l vif_idx;
};

/// Structure containing the parameters of the @ref APM_START_CAC_CFM message.
struct apm_start_cac_cfm {
	/// Status of the CAC starting procedure
	u8_l status;
	/// Index of the channel context attached to the VIF for CAC
	u8_l ch_idx;
};

/// Structure containing the parameters of the @ref APM_STOP_CAC_REQ message.
struct apm_stop_cac_req {
	/// Index of the VIF for which the CAC has to be stopped
	u8_l vif_idx;
};

/// Structure containing the parameters of the @ref APM_PROBE_CLIENT_REQ message.
struct apm_probe_client_req {
	/// Index of the VIF
	u8_l vif_idx;
	/// Index of the Station to probe
	u8_l sta_idx;
};

/// Structure containing the parameters of the @ref APM_PROBE_CLIENT_CFM message.
struct apm_probe_client_cfm {
	/// Status of the probe request
	u8_l status;
	/// Unique ID to distinguish @ref APM_PROBE_CLIENT_IND message
	u32_l probe_id;
};

/// Structure containing the parameters of the @ref APM_PROBE_CLIENT_CFM message.
struct apm_probe_client_ind {
	/// Index of the VIF
	u8_l vif_idx;
	/// Index of the Station to probe
	u8_l sta_idx;
	/// Whether client is still present or not
	bool_l client_present;
	/// Unique ID as returned in @ref APM_PROBE_CLIENT_CFM
	u32_l probe_id;
};

///////////////////////////////////////////////////////////////////////////////
/////////// For MESH messages
///////////////////////////////////////////////////////////////////////////////

/// Maximum length of the Mesh ID
#define MESH_MESHID_MAX_LEN (32)

/// Message API of the MESH task
enum mesh_msg_tag {
	/// Request to start the MP
	MESH_START_REQ = LMAC_FIRST_MSG(TASK_MESH),
	/// Confirmation of the MP start.
	MESH_START_CFM,

	/// Request to stop the MP.
	MESH_STOP_REQ,
	/// Confirmation of the MP stop.
	MESH_STOP_CFM,

	// Request to update the MP
	MESH_UPDATE_REQ,
	/// Confirmation of the MP update
	MESH_UPDATE_CFM,

	/// Request information about a given link
	MESH_PEER_INFO_REQ,
	/// Response to the MESH_PEER_INFO_REQ message
	MESH_PEER_INFO_CFM,

	/// Request automatic establishment of a path with a given mesh STA
	MESH_PATH_CREATE_REQ,
	/// Confirmation to the MESH_PATH_CREATE_REQ message
	MESH_PATH_CREATE_CFM,

	/// Request a path update (delete path, modify next hop mesh STA)
	MESH_PATH_UPDATE_REQ,
	/// Confirmation to the MESH_PATH_UPDATE_REQ message
	MESH_PATH_UPDATE_CFM,

	/// Indication from Host that the indicated Mesh Interface is a proxy for an external STA
	MESH_PROXY_ADD_REQ,

	/// Indicate that a connection has been established or lost
	MESH_PEER_UPDATE_IND,
	/// Notification that a connection has been established or lost (when MPM handled by userspace)
	MESH_PEER_UPDATE_NTF = MESH_PEER_UPDATE_IND,

	/// Indicate that a path is now active or inactive
	MESH_PATH_UPDATE_IND,
	/// Indicate that proxy information have been updated
	MESH_PROXY_UPDATE_IND,

	/// MAX number of messages
	MESH_MAX,
};

/// Structure containing the parameters of the @ref MESH_START_REQ message.
struct mesh_start_req {
	/// Basic rate set
	struct mac_rateset basic_rates;
	/// Operating channel on which we have to enable the AP
	struct mac_chan_op chan;
	/// DTIM Period
	u8_l dtim_period;
	/// Beacon Interval
	u16_l bcn_int;
	/// Index of the VIF for which the MP is started
	u8_l vif_index;
	/// Length of the Mesh ID
	u8_l mesh_id_len;
	/// Mesh ID
	u8_l mesh_id[MESH_MESHID_MAX_LEN];
	/// Address of the IEs to download
	u32_l ie_addr;
	/// Length of the provided IEs
	u8_l ie_len;
	/// Indicate if Mesh Peering Management (MPM) protocol is handled in userspace
	bool_l user_mpm;
	/// Indicate if Mesh Point is using authentication
	bool_l is_auth;
	/// Indicate which authentication method is used
	u8_l auth_id;
};

/// Structure containing the parameters of the @ref MESH_START_CFM message.
struct mesh_start_cfm {
	/// Status of the MP starting procedure
	u8_l status;
	/// Index of the VIF for which the MP is started
	u8_l vif_idx;
	/// Index of the channel context attached to the VIF
	u8_l ch_idx;
	/// Index of the STA used for BC/MC traffic
	u8_l bcmc_idx;
};

/// Structure containing the parameters of the @ref MESH_STOP_REQ message.
struct mesh_stop_req {
	/// Index of the VIF for which the MP has to be stopped
	u8_l vif_idx;
};

/// Structure containing the parameters of the @ref MESH_STOP_CFM message.
struct mesh_stop_cfm {
	/// Index of the VIF for which the MP has to be stopped
	u8_l vif_idx;
	/// Status
	u8_l status;
};

/// Bit fields for mesh_update_req message's flags value
enum mesh_update_flags_bit {
	/// Root Mode
	MESH_UPDATE_FLAGS_ROOT_MODE_BIT = 0,
	/// Gate Mode
	MESH_UPDATE_FLAGS_GATE_MODE_BIT,
	/// Mesh Forwarding
	MESH_UPDATE_FLAGS_MESH_FWD_BIT,
	/// Local Power Save Mode
	MESH_UPDATE_FLAGS_LOCAL_PSM_BIT,
};

/// Structure containing the parameters of the @ref MESH_UPDATE_REQ message.
struct mesh_update_req {
	/// Flags, indicate fields which have been updated
	u8_l flags;
	/// VIF Index
	u8_l vif_idx;
	/// Root Mode
	u8_l root_mode;
	/// Gate Announcement
	bool_l gate_announ;
	/// Mesh Forwarding
	bool_l mesh_forward;
	/// Local PS Mode
	u8_l local_ps_mode;
};

/// Structure containing the parameters of the @ref MESH_UPDATE_CFM message.
struct mesh_update_cfm {
	/// Status
	u8_l status;
};

/// Structure containing the parameters of the @ref MESH_PEER_INFO_REQ message.
struct mesh_peer_info_req {
	/// Index of the station allocated for the peer
	u8_l sta_idx;
};

/// Structure containing the parameters of the @ref MESH_PEER_INFO_CFM message.
struct mesh_peer_info_cfm {
	/// Response status
	u8_l status;
	/// Index of the station allocated for the peer
	u8_l sta_idx;
	/// Local Link ID
	u16_l local_link_id;
	/// Peer Link ID
	u16_l peer_link_id;
	/// Local PS Mode
	u8_l local_ps_mode;
	/// Peer PS Mode
	u8_l peer_ps_mode;
	/// Non-peer PS Mode
	u8_l non_peer_ps_mode;
	/// Link State
	u8_l link_state;
	/// Time elapsed since last received beacon (in us)
	u32_l last_bcn_age;
};

/// Structure containing the parameters of the @ref MESH_PATH_CREATE_REQ message.
struct mesh_path_create_req {
	/// Index of the interface on which path has to be created
	u8_l vif_idx;
	/// Indicate if originator MAC Address is provided
	bool_l has_orig_addr;
	/// Path Target MAC Address
	struct mac_addr tgt_mac_addr;
	/// Originator MAC Address
	struct mac_addr orig_mac_addr;
};

/// Structure containing the parameters of the @ref MESH_PATH_CREATE_CFM message.
struct mesh_path_create_cfm {
	/// Confirmation status
	u8_l status;
	/// VIF Index
	u8_l vif_idx;
};

/// Structure containing the parameters of the @ref MESH_PATH_UPDATE_REQ message.
struct mesh_path_update_req {
	/// Indicate if path must be deleted
	bool_l delete;
	/// Index of the interface on which path has to be created
	u8_l vif_idx;
	/// Path Target MAC Address
	struct mac_addr tgt_mac_addr;
	/// Next Hop MAC Address
	struct mac_addr nhop_mac_addr;
};

/// Structure containing the parameters of the @ref MESH_PATH_UPDATE_CFM message.
struct mesh_path_update_cfm {
	/// Confirmation status
	u8_l status;
	/// VIF Index
	u8_l vif_idx;
};

/// Structure containing the parameters of the @ref MESH_PROXY_ADD_REQ message.
struct mesh_proxy_add_req {
	/// VIF Index
	u8_l vif_idx;
	/// MAC Address of the External STA
	struct mac_addr ext_sta_addr;
};

/// Structure containing the parameters of the @ref MESH_PROXY_UPDATE_IND
struct mesh_proxy_update_ind {
	/// Indicate if proxy information has been added or deleted
	bool_l delete;
	/// Indicate if we are a proxy for the external STA
	bool_l local;
	/// VIF Index
	u8_l vif_idx;
	/// MAC Address of the External STA
	struct mac_addr ext_sta_addr;
	/// MAC Address of the proxy (only valid if local is false)
	struct mac_addr proxy_mac_addr;
};

/// Structure containing the parameters of the @ref MESH_PEER_UPDATE_IND message.
struct mesh_peer_update_ind {
	/// Indicate if connection has been established or lost
	bool_l estab;
	/// VIF Index
	u8_l vif_idx;
	/// STA Index
	u8_l sta_idx;
	/// Peer MAC Address
	struct mac_addr peer_addr;
};

/// Structure containing the parameters of the @ref MESH_PEER_UPDATE_NTF message.
struct mesh_peer_update_ntf {
	/// VIF Index
	u8_l vif_idx;
	/// STA Index
	u8_l sta_idx;
	/// Mesh Link State
	u8_l state;
};

/// Structure containing the parameters of the @ref MESH_PATH_UPDATE_IND message.
struct mesh_path_update_ind {
	/// Indicate if path is deleted or not
	bool_l delete;
	/// Indicate if path is towards an external STA (not part of MBSS)
	bool_l ext_sta;
	/// VIF Index
	u8_l vif_idx;
	/// Path Index
	u8_l path_idx;
	/// Target MAC Address
	struct mac_addr tgt_mac_addr;
	/// External STA MAC Address (only if ext_sta is true)
	struct mac_addr ext_sta_mac_addr;
	/// Next Hop STA Index
	u8_l nhop_sta_idx;
};

///////////////////////////////////////////////////////////////////////////////
/////////// For Debug messages
///////////////////////////////////////////////////////////////////////////////

/// Messages related to Debug Task
enum dbg_msg_tag {
	/// Memory read request
	DBG_MEM_READ_REQ = LMAC_FIRST_MSG(TASK_DBG),
	/// Memory read confirm
	DBG_MEM_READ_CFM,
	/// Memory write request
	DBG_MEM_WRITE_REQ,
	/// Memory write confirm
	DBG_MEM_WRITE_CFM,
	/// Module filter request
	DBG_SET_MOD_FILTER_REQ,
	/// Module filter confirm
	DBG_SET_MOD_FILTER_CFM,
	/// Severity filter request
	DBG_SET_SEV_FILTER_REQ,
	/// Severity filter confirm
	DBG_SET_SEV_FILTER_CFM,
	/// LMAC/MAC HW fatal error indication
	DBG_ERROR_IND,
	/// Request to get system statistics
	DBG_GET_SYS_STAT_REQ,
	/// COnfirmation of system statistics
	DBG_GET_SYS_STAT_CFM,
	/// Max number of Debug messages
	DBG_MAX,
};

/// Structure containing the parameters of the @ref DBG_MEM_READ_REQ message.
struct dbg_mem_read_req {
	u32_l memaddr;
};

/// Structure containing the parameters of the @ref DBG_MEM_READ_CFM message.
struct dbg_mem_read_cfm {
	u32_l memaddr;
	u32_l memdata;
};

/// Structure containing the parameters of the @ref DBG_MEM_WRITE_REQ message.
struct dbg_mem_write_req {
	u32_l memaddr;
	u32_l memdata;
};

/// Structure containing the parameters of the @ref DBG_MEM_WRITE_CFM message.
struct dbg_mem_write_cfm {
	u32_l memaddr;
	u32_l memdata;
};

/// Structure containing the parameters of the @ref DBG_SET_MOD_FILTER_REQ message.
struct dbg_set_mod_filter_req {
	/// Bit field indicating for each module if the traces are enabled or not
	u32_l mod_filter;
};

/// Structure containing the parameters of the @ref DBG_SEV_MOD_FILTER_REQ message.
struct dbg_set_sev_filter_req {
	/// Bit field indicating the severity threshold for the traces
	u32_l sev_filter;
};

/// Structure containing the parameters of the @ref DBG_GET_SYS_STAT_CFM message.
struct dbg_get_sys_stat_cfm {
	/// Time spent in CPU sleep since last reset of the system statistics
	u32_l cpu_sleep_time;
	/// Time spent in DOZE since last reset of the system statistics
	u32_l doze_time;
	/// Total time spent since last reset of the system statistics
	u32_l stats_time;
};

///////////////////////////////////////////////////////////////////////////////
/////////// For TDLS messages
///////////////////////////////////////////////////////////////////////////////

/// List of messages related to the task.
enum tdls_msg_tag {
	/// TDLS channel Switch Request.
	TDLS_CHAN_SWITCH_REQ = LMAC_FIRST_MSG(TASK_TDLS),
	/// TDLS channel switch confirmation.
	TDLS_CHAN_SWITCH_CFM,
	/// TDLS channel switch indication.
	TDLS_CHAN_SWITCH_IND,
	/// TDLS channel switch to base channel indication.
	TDLS_CHAN_SWITCH_BASE_IND,
	/// TDLS cancel channel switch request.
	TDLS_CANCEL_CHAN_SWITCH_REQ,
	/// TDLS cancel channel switch confirmation.
	TDLS_CANCEL_CHAN_SWITCH_CFM,
	/// TDLS peer power save indication.
	TDLS_PEER_PS_IND,
	/// TDLS peer traffic indication request.
	TDLS_PEER_TRAFFIC_IND_REQ,
	/// TDLS peer traffic indication confirmation.
	TDLS_PEER_TRAFFIC_IND_CFM,
	/// MAX number of messages
	TDLS_MAX
};

/// Structure containing the parameters of the @ref TDLS_CHAN_SWITCH_REQ message
struct tdls_chan_switch_req {
	/// Index of the VIF
	u8_l vif_index;
	/// STA Index
	u8_l sta_idx;
	/// MAC address of the TDLS station
	struct mac_addr peer_mac_addr;
	/// Flag to indicate if the TDLS peer is the TDLS link initiator
	bool_l initiator;
	/// Channel configuration
	struct mac_chan_op chan;
	/// Operating class
	u8_l op_class;
};

/// Structure containing the parameters of the @ref TDLS_CANCEL_CHAN_SWITCH_REQ message
struct tdls_cancel_chan_switch_req {
	/// Index of the VIF
	u8_l vif_index;
	/// STA Index
	u8_l sta_idx;
	/// MAC address of the TDLS station
	struct mac_addr peer_mac_addr;
};

/// Structure containing the parameters of the @ref TDLS_CHAN_SWITCH_CFM message
struct tdls_chan_switch_cfm {
	/// Status of the operation
	u8_l status;
};

/// Structure containing the parameters of the @ref TDLS_CANCEL_CHAN_SWITCH_CFM message
struct tdls_cancel_chan_switch_cfm {
	/// Status of the operation
	u8_l status;
};

/// Structure containing the parameters of the @ref TDLS_CHAN_SWITCH_IND message
struct tdls_chan_switch_ind {
	/// VIF Index
	u8_l vif_index;
	/// Channel Context Index
	u8_l chan_ctxt_index;
	/// Status of the operation
	u8_l status;
};

/// Structure containing the parameters of the @ref TDLS_CHAN_SWITCH_BASE_IND message
struct tdls_chan_switch_base_ind {
	/// VIF Index
	u8_l vif_index;
	/// Channel Context index
	u8_l chan_ctxt_index;
};

/// Structure containing the parameters of the @ref TDLS_PEER_PS_IND message
struct tdls_peer_ps_ind {
	/// VIF Index
	u8_l vif_index;
	/// STA Index
	u8_l sta_idx;
	/// MAC ADDR of the TDLS STA
	struct mac_addr peer_mac_addr;
	/// Flag to indicate if the TDLS peer is going to sleep
	bool ps_on;
};

/// Structure containing the parameters of the @ref TDLS_PEER_TRAFFIC_IND_REQ message
struct tdls_peer_traffic_ind_req {
	/// VIF Index
	u8_l vif_index;
	/// STA Index
	u8_l sta_idx;
	// MAC ADDR of the TDLS STA
	struct mac_addr peer_mac_addr;
	/// Dialog token
	u8_l dialog_token;
	/// TID of the latest MPDU transmitted over the TDLS direct link to the TDLS STA
	u8_l last_tid;
	/// Sequence number of the latest MPDU transmitted over the TDLS direct link
	/// to the TDLS STA
	u16_l last_sn;
};

/// Structure containing the parameters of the @ref TDLS_PEER_TRAFFIC_IND_CFM message
struct tdls_peer_traffic_ind_cfm {
	/// Status of the operation
	u8_l status;
};

///////////////////////////////////////////////////////////////////////////////
/////////// For FTM messages
///////////////////////////////////////////////////////////////////////////////

/// Maximum number of FTM responders handled in one command
#define FTM_RSP_MAX 5

/// List of messages related to the task.
enum ftm_msg_tag {
	/// Setup a FTM measurement
	FTM_START_REQ = LMAC_FIRST_MSG(TASK_FTM),
	/// Confirmation of the MP start.
	FTM_START_CFM,
	/// FTM measurement available
	FTM_DONE_IND,
	/// MAX number of messages
	FTM_MAX
};

/// Structure containing the parameters of the @ref FTM_START_REQ message.
struct ftm_start_req {
	/// Vif Index
	u8_l fhost_vif_idx;
};

/// Structure containing the parameters of the @ref FTM_START_CFM message.
struct ftm_start_cfm {
	/// Status of the FTM starting procedure
	u8_l status;
	/// Index of the VIF for which the FTM is started
	u8_l vif_idx;
};

/// Structure containing the parameters of the @ref FTM_DONE_IND message.
struct ftm_done_ind {
	/// Index of the VIF for which the FTM is started
	u8_l vif_idx;
	/// Results
	struct mac_ftm_results results;
};

/// Structure containing the parameters of the @ref MCU_WIFI_CALI_REQ message.
struct mcu_wf_cali_req {
	/// cailbration step (@ref enum rwnx_mcu_cali)
	u16_l step;
	/// calibration frequency if need
	u16_l freq;
	/// calibration bandwidth if need (in enum mac_chan_bandwidth)
	u16_l band;
};

/// Structure containing the parameters of the @ref MCU_WIFI_CALI_CFM message.
struct mcu_wf_cali_cfm {
	/// cailbration step cfm
	u16_l step;
	/// Status of the devc caliration
	u16_l status;
};

/// Structure containing the parameters of the @ref MCU_WIFI_IEEE80211B_RATE_REQ message.
struct mcu_11b_mode {
	/// Flag to indicate if the package is IEEE80211b
	bool_l ieee80211b_mode;
};

struct packet_filter_elt {
	/// cmd id
	uint8_t id;
	/// false: disable; true: enable
	bool state;
	/// valid data length in filter_param
	uint8_t param_len;
	/// various meaning for different cmd
	uint8_t addition_id;
};

#define BES_FILTER_PARAM_MAX_LEN 100

struct mm_set_packet_filter_req {
	/// either vif index or filter index
	uint8_t vif_rxf_idx;
	/// packet filter element
	struct packet_filter_elt filter;
	/// filter parameter
	uint8_t filter_param[BES_FILTER_PARAM_MAX_LEN];
};

struct mm_set_arp_keepalive_req {
	/// in seconds
	uint16_t period;
	/// deprecated
	uint8_t encrypt_mode;
	/// Index of the VIF
	uint8_t vif_idx;
	/// in u32 big endian format
	uint8_t sender_ip[4];
	uint8_t target_ip[4];
};

/// Header of the structure containing the parameters of the @ref MM_CUSTOM_REQ message.
struct mm_custom_req_hdr {
	/// command id
	uint32_t id:6;
	/// length of custom req msg structure
	uint32_t length:9;
	/// operations
	uint32_t ops:5;
	/// reserved field
	uint32_t reserve:12;
};

/// Custom command id
enum custom_id {
	CUSTOM_ID_HW_FILTER = 0,
	CUSTOM_ID_MGMT_FILTER,
	CUSTOM_ID_LINK_INFO_REQ,
	CUSTOM_ID_WAKEUP_INFO_PRINT,
	CUSTOM_ID_LP_SLEEP_CONFIG,
	CUSTOM_ID_VIF_INFO_EXT,
	CUSTOM_ID_VIF_INFO,
	CUSTOM_ID_LMAC_LOG,
	CUSTOM_ID_NARROW_BW,
	CUSTOM_ID_PHY_STATES_GET,
	CUSTOM_ID_CRASH_LMAC,
	CUSTOM_ID_SCHED_SCAN,
	CUSTOM_ID_ADAPTIVE_CCA,
	CUSTOM_ID_FW_TP_DBG,
	CUSTOM_ID_CS_NOTIFY_TRIGGER,
};

/// Hw filter ops type
enum hw_filter_ops {
	HW_FILTER_OPS_SET = 0,
	HW_FILTER_OPS_GET,
};

/// Management filter ops type
enum mgmt_filter_ops
{
    // Configure the management frame filter.
    BEACON_FILTER_OPS_SET = 0,
    // Control the beacon filter.
    BEACON_FILTER_OPS_SWITCH,
    // Control of the filter for filtering the management frames that
    // don't match the destination.
    UNMATCH_DST_MGMT_FILTER_SWITCH,
};

/// Lp sleep config type
enum lp_sleep_ops{
	LP_SLEEP_OPS_BCN_LOST_THRES_SET = 0,
	LP_SLEEP_OPS_NULL_KEEPALIVE_SET,
	LP_SLEEP_OPS_GTK_REKEY_SET,
	LP_SLEEP_OPS_GTK_REKEY_GET,
};

/// Lmac Log operation type
enum lmac_log_ops {
	LMAC_LOG_OPS_LEVEL_GET = 0,
	LMAC_LOG_OPS_LEVEL_SET,
	LMAC_LOG_OPS_MODE_GET,
	LMAC_LOG_OPS_MODE_SET,
	LMAC_LOG_OPS_FLUSH,
};

struct lmac_log_config {
	union {
		struct {
			/// When operating a component, its corresponding mask is 1
			u32_l trace_compo_mask;
			/// Returned log printing level
			u32_l trace_compo_level[TRACE_COMPO_MAX];
		};
		u8_l log_mode;
	};
};

struct lmac_log_set_req {
	/// Message header
	struct mm_custom_req_hdr hdr;
	struct lmac_log_config config;
};

struct lmac_log_get_req {
	/// Message header
	struct mm_custom_req_hdr hdr;
};

struct lmac_log_get_cfm {
	struct lmac_log_config config;
};

struct phy_states_get_req {
	/// Message header
	struct mm_custom_req_hdr hdr;
};

struct phy_states_get_cfm {
	// counts of the receive FCS errors
	u32_l fcs_error_count;
	// counts of the PHY errors
	u32_l phy_error_count;
	// Tx busy time in last sample period
	u32 last_period_busy_time;
	// Sample time in last sample period
	u32 last_period_sample_time;
};

/// Crash lmac operation type
enum crash_lmac_ops {
	CRASH_LMAC_OPS_ILLEGAL_ACCESS = 0,
	CRASH_LMAC_OPS_ASSERT,
};

struct mm_custom_general_status_cfm {
	// Confirm Status
	u8 status;
};

#ifdef DUMP_FW_LOG_ENABLE
/// Structure containing the parameters of the @ref MCU_DUMP_FW_LOG_REQ message.
struct mcu_dump_fw_log_req {
	/// Type of the fw log cmd(enum fw_log_cmd_type)
	u8 cmd_type;
	/// Type of the fw log(enum fw_log_type)
	u8 type;
};
#endif

struct mcu_buf_cnt_cfm {
	u16_l rxbuf_cnt;
};

/// Structure containing the parameters of the @ref MCU_GET_MAC_ADDR_CFM message.
struct mcu_get_mac_cfm {
	/// obtain mac address from mcu status
	s32 status;
	/// mac address
	u8 mac_fixed[6];
};

enum mcu_dbac_cali_sub_cmd {
	/// Explain to the MCU that the calibration data of a certain channel is invalid
	MCU_CALI_INVAL_CHAN_REQ,
	MCU_CALI_INVAL_CHAN_CFM,

	/// Start temperature compensation for a channel
	MCU_TEMP_COMPENSATION_REQ,
	MCU_TEMP_COMPENSATION_CFM,
};

/// Structure containing the parameters of the @ref MCU_DBAC_CALI_REQ message.
struct mcu_dbac_cali_cmd {
	/// sub cmd in enum mcu_dbac_cali_sub_cmd
	u8 sub_cmd;
	u32 data_len;
	/// be word-aligned.
	u32 data[];
};

/// Structure containing the parameters of the @ref MCU_CALI_INVAL_CHAN_REQ message.
struct mcu_cali_inval_ch {
	u8 channel;
};

// Structure containing the parameters of the @ref MCU_TEMP_COMPENSATION message.
struct mcu_temp_compensation_data {
	u8 roc_chan;
};

enum ltrace_msg_type {
	LTR_MSG_NORMAL = 0,
	LTR_MSG_ASSERT,
	LTR_MSG_EXCEPTION,
};

struct ltrace_assert_msg {
	uint32_t sp;
	char str[0];
};

struct ltrace_exception_info {
	uint32_t ID;
	uint32_t CPU_ID;
	uint32_t REGS; 	/* (origin:uint32_t *REGS)force change to 4 byte size */
	uint32_t MSP;
	uint32_t PSP;
	uint8_t PRIMASK;
	uint8_t FAULTMASK;
	uint8_t BASEPRI;
	uint8_t CONTROL;
	uint32_t ICSR;
	uint32_t AIRCR;
	uint32_t SCR;
	uint32_t CCR;
	uint32_t SHCSR;
	uint32_t CFSR;
	uint32_t HFSR;
	uint32_t AFSR;
	uint32_t DFSR;
	uint32_t MMFAR;
	uint32_t BFAR;
	uint32_t MSPLIM;
	uint32_t PSPLIM;
};

/// CCA  lmac operation : enable or config operation
enum adaptive_cca_req_ops {
    ADAPTIVE_CCA_ENABLE,
    ADAPTIVE_CCA_CONFIG,
};

//CCA enable req msg
struct adaptive_cca_enable_req {
    struct mm_custom_req_hdr hdr;
    uint8_t adaptive_cca_en;
};

/// Structure containing the parameters of the @ref CUSTOM_ID_ADAPTIVE_CCA message.
struct adaptive_cca_config {
    uint8_t test_mode_cca_eng;
    uint8_t noise_thd_low;
    uint8_t noise_thd_high;
    uint32_t enter_test_mode_thd;
    uint32_t exit_test_mode_thd;
    uint32_t rx_abort_cnt_limit;
};

///CCA config req msg
struct adaptive_cca_config_req {
	struct mm_custom_req_hdr hdr;
	struct adaptive_cca_config cca_cfg;
};

/// List of driver and MCU messages related to the task.
enum mcu_msg_tag
{
	/// Request of nosignal RF cmd
	MCU_VENDOR_RF_CMD_REQ = LMAC_FIRST_MSG(TASK_MCU),
	/// Confirmation of nosignal RF cmd
	MCU_VENDOR_RF_CMD_CFM,
	/// Request to read MCU reg
	MCU_REG_READ_REQ,
	/// Confirmation to read MCU reg
	MCU_REG_READ_CFM,
	/// Request to write MCU reg
	MCU_REG_WRITE_REQ,
	/// Confirmation to write MCU reg
	MCU_REG_WRITE_CFM,
	/// Request to setup wifi calibration procedure
	MCU_WIFI_CALI_REQ,
	/// Confirmation to setup wifi calibration procedure
	MCU_WIFI_CALI_CFM,
	/// Request to turn on/off wifi
	MCU_WIFI_ON_OFF_REQ,
	/// Confirmation to to turn on/off wifi
	MCU_WIFI_ON_OFF_CFM,
	/// Request to disable wifi tb ppdu
	MCU_WIFI_TB_PPDU_DISABLE_REQ,
	/// Confirmation to disable wifi tb ppdu
	MCU_WIFI_TB_PPDU_DISABLE_CFM,
	/// Request to get dpd cail data size
	MCU_WIFI_GET_DPD_SIZE_REQ,
	/// Confirmation to get dpd cail data size
	MCU_WIFI_GET_DPD_SIZE_CFM,
	/// Request to get dpd cail data
	MCU_WIFI_GET_DPD_DATA_REQ,
	/// Confirmation to get dpd cail data
	MCU_WIFI_GET_DPD_DATA_CFM,
	/// Request to send dpd cail data
	MCU_WIFI_SEND_DPD_DATA_REQ,
	/// Confirmation to send dpd cail data
	MCU_WIFI_SEND_DPD_DATA_CFM,
	/// Request to setup wifi now package IEEE80211b Rate
	MCU_WIFI_IEEE80211B_RATE_REQ,
	/// Request to get buffer count
	MCU_BUF_CNT_REQ,
	/// Confirmation to get buffer count
	MCU_BUF_CNT_CFM,
	/// Request to send reboot MCU
	MCU_FW_REBOOT_REQ,
	/// Confirmation to reboot MCU
	MCU_FW_REBOOT_CFM,
	/// Request to send dump fw log
	MCU_DUMP_FW_LOG_REQ,
	/// Confirmation to dump fw log
	MCU_DUMP_FW_LOG_CFM,
	/// Indication of dump fw log message
	MCU_DUMP_FW_LOG_IND,
	/// Request to send get mac addr from MCU
	MCU_GET_MAC_ADDR_REQ,
	/// Confirmation to get mac addr from MCU
	MCU_GET_MAC_ADDR_CFM,

	/// Request of multichannel calibration adaptation
	MCU_DBAC_CALI_REQ,
	/// Confirmation of multi-channel calibration adaptation
	MCU_DBAC_CALI_CFM,
	// Indication of temporature caliboration
	MCU_DBAC_TEMP_CALI_IND,

	/// Request to notify MCU host to suspend
	MCU_HOST_LP_SUSPEND_REQ,
	/// Confirmation to notify MCU host to suspend
	MCU_HOST_LP_SUSPEND_CFM,
	/// Request to notify MCU host to resume
	MCU_HOST_LP_RESUME_REQ,
	/// Confirmation to notify MCU host to resume
	MCU_HOST_LP_RESUME_CFM,

	/// Request to change MCU
	MCU_FW_CHANGE_MODE_REQ,
	/// Confirmation to change MCU
	MCU_FW_CHANGE_MODE_CFM,
	/// Request to get BT status
	MCU_GET_BT_STATUS_REQ,
	/// Confirmation to get BT status
	MCU_GET_BT_STATUS_CFM,
	/// Indication of BT status
	MCU_BT_STATUS_IND,
	/// Indication of dev state
	MCU_DEV_STATE_IND,

	/// Indication of first msg of lmac trace
	MCU_DUMP_LTRACE_START_IND,
	/// Indication of continue msg of lmac trace
	MCU_DUMP_LTRACE_CONT_IND,
	/// Indication of end msg of lmac trace
	MCU_DUMP_LTRACE_END_IND,
	/// Indication of single msg of lmac trace
	MCU_DUMP_LTRACE_SINGLE_IND,
	/// Indication of fault msg of lmac trace
	MCU_DUMP_LTRACE_FAULT_IND,

	/// Request of MCU multi-scan
	MCU_SCANU_MC_SCAN_REQ,
	/// Confirmation of MCU multi-scan
	MCU_SCANU_MC_SCAN_CFM,

	/// Request to sync drv info
	MCU_SYNC_DRV_INFO_REQ,
	/// Confirmation to sync drv info
	MCU_SYNC_DRV_INFO_CFM,
	/// Request to ignore beacon edca
	MCU_IGNORE_BCN_EDCA_REQ,

	/// Request to get temperature values
	MCU_GET_TEMPRTR_REQ,
	/// Confirmation to get temperature values
	MCU_GET_TEMPRTR_CFM,
	/// Request to set tx max power
	MCU_SET_TX_MAX_POWER_REQ,
	/// Confirmation to set tx max power
	MCU_SET_TX_MAX_POWER_CFM,
	/// Request to change wifi wake state
	MCU_WIFI_WAKE_CTRL_REQ,
	/// Confirmation to change wifi wake state
	MCU_WIFI_WAKE_CTRL_CFM,
	/// Request to get current system state
	MCU_SYS_STATE_REQ,
	/// Confirmation to get current system state
	MCU_SYS_STATE_CFM,
	/// Request to get pll state
	MCU_GET_PLL_STATE_REQ,
	///Confirmation to get get pll state
	MCU_GET_PLL_STATE_CFM,

	/// request to update power table by country code
	MCU_SEND_COUNTRY_CODE_REQ,
	/// Confirmation to get update power table state
	MCU_SEND_COUNTRY_CODE_CFM,

	/// Request to get ring data save size
	MCU_GET_RING_DATA_SAVE_SIZE_REQ,
	/// Confirmation to get ring data save size
	MCU_GET_RING_DATA_SAVE_SIZE_CFM,
	/// Request to get ring data
	MCU_GET_RING_DATA_REQ,
	/// Confirmation to get ring data
	MCU_GET_RING_DATA_CFM,

	/// Request to get delay statistics information
	MCU_GET_DELAY_STATS_INFO_REQ,
	/// Confirmation to get delay statistics information
	MCU_GET_DELAY_STATS_INFO_CFM,

	/// MAX number of messages
	MCU_MSG_MAX,
};

// Structure containing the parameters of the @ref MCU_SET_TX_MAX_POWER_CFM message
struct wifi_pwr_range {
	/// maximum tx power
	s16 max_pwr;
	/// minmum tx power
	s16 min_pwr;
};

/// Structure containing the parameters of the @ref MM_CSI_CAP_REQ message
struct mm_csi_cap_req {
	/// bit0 is 20M, bit1 is 40M, bit2 is 80M
	uint8_t bw;
	/// bit0 is ht, bit1 is vht, bit2 is he_su, bit3 is he_mu, bit4 is he_er, bit5 is he_tb
	uint8_t type;
	/// Space-Time Block Coding, 1 is need to capture
	uint8_t stbc;
	/// mac address match, 1 is need to match our mac
	uint8_t mac_match;
	/// lsig min len
	uint16_t lsig_len_min;
	/// lsig max len
	uint16_t lsig_len_max;
	/// csi sniff mode, 1 is open
	uint8_t csi_sniff;
};

/// Structure containing the parameters of the @ref MM_CSI_CAP_IND message.
struct mm_csi_cap_result {
	/// csi status
	uint32_t status;
	/// csi data
	uint32_t csi_data[];
};

/// Structure containing the parameters of the @ref mm_csi_cap_drv_ind message
struct mm_csi_cap_ind {
	/// receive csi data total len
	uint16_t tot_len;
	/// this packet csi data offset
	uint16_t data_offset;
	/// this packet csi data len
	uint16_t data_len;
	/// csi result
	struct mm_csi_cap_result csi_result;
};

/// Structure containing the current states
struct mcu_sys_state_cfm {
	/// current bluetooth state
	uint32_t bt_state;
	/// the latest switch ind message
	struct mm_channel_switch_ind cur_switch_ind;
};

/// Structure containing the country code,2byte
struct bes_notify_country_code_req {
	/// country code
	uint8_t country_num[2];
};

/// List of coex mode config.
enum coex_mode_config_type {
	/// coex mode block bt
	COEX_MODE_CONFIG_BLOCKBT = 0,
	/// coex mode unblock bt
	COEX_MODE_CONFIG_UNBLOCKBT,
	/// coex mode fdd
	COEX_MODE_CONFIG_FDD,
	/// coex mode mode max
	COEX_MODE_CONFIG_UNVILD,
};

/// Structure containing the parameters of the coex mode proportion calculate
struct coex_mode_prop_calc {
	/// start time
	uint32_t start_time;
	/// total time
	uint32_t total_time;
};

/// Structure containing the parameters of the transq stats cnt
struct transq_cnt_stats {
	/// The count of successful transq
	uint32_t sucess;
	/// The count of failed transq
	uint32_t fail;
};

/// Structure containing the parameters of the coex tp info
struct coex_tp_info {
	/// The proportion calculation parameters for coex_mode
	struct coex_mode_prop_calc prop_calc[COEX_MODE_CONFIG_UNVILD];
	/// The count statistics for transq
	struct transq_cnt_stats transq_stats;
};

/// Structure containing the parameters of the @ref CUSTOM_ID_FW_TP_DBG message
struct tp_fw_info_get_req {
	/// Message header
	struct mm_custom_req_hdr hdr;
};

/// Structure containing the parameters of the cfm for CUSTOM_ID_FW_TP_DBG message
struct tp_fw_info_get_cfm {
	/// Coex throughput information
	struct coex_tp_info coex_tp_info;
	/// The count of host_rxbuf being unavailable
	uint32_t hostrxbuf_unavail;
};

/// Structure containing the parameters of the @ref MCU_GET_RING_DATA_SAVE_SIZE_REQ message
struct bes_ring_data_save_size_get_req {
	/// ring data id
	uint16_t ring_id;
};

/// Structure containing the parameters of the @ref MCU_GET_RING_DATA_REQ message
struct bes_ring_data_get_req {
	/// ring data id
	uint16_t ring_id;
	/// recv size each time
	uint16_t recv_size;
};

/// Structure containing the parameters of the @ref MCU_GET_RING_DATA_CFM message
struct bes_ring_data_get_cfm {
	/// the length of data got in mcu
	uint32_t get_data_len;
	/// get ring data
	uint8_t ring_data[];
};

/// Structure containing the parameters of reg debug stats debug info
struct dbg_reg_stats {
	/// reg
	uint32_t reg;
	/// reg value
	uint32_t val;
};

/// Structure containing the parameters of coex stats debug info
struct dbg_coex_stats {
	/// The type of channel mode.
	uint8_t chan_mode;
	/// The direct difference between the current HW time and the latest unockbt time.
	uint32_t del_time;
	/// The transq status.
	uint8_t transq_stat;
	/// The coex throughput information.
	struct coex_tp_info coex_tp;
};

/// Reference struct machw_mib_tag
struct diag_reg_dump {
	/// Counts the number of unencrypted frames that have been discarded
	uint32_t dot11_wep_excluded_count;
	/// Counts the receive FCS errors
	uint32_t dot11_fcs_error_count;
	/// Counts the number of PHY Errors reported during a receive transaction
	uint32_t rw_rx_phy_error_count;
	/// Counts the number of times the receive FIFO has overflowed
	uint32_t rw_rd_fifo_overflow_count;
	/// Counts the number of times underrun has occurred on the transmit side
	uint32_t rw_tx_underrun_count;
	/**
	 * Counts the number of times an overflow occurs at the MPIF FIFO when receiving a
	 * frame from the PHY
	 */
	uint32_t rw_rx_mpif_overflow_count;
	/// Counts the number of unicast A-MPDU's received
	uint32_t rw_uampdu_received_count;
	/// Counts the number of group addressed A-MPDU's received
	uint32_t rw_gampdu_received_count;
	/// Counts the number of unicast A-MPDUs received not destined to this device
	uint32_t rw_other_ampdu_received_count;
	/// Counts the number of MPDUs received in an A-MPDU
	uint32_t dot11_mpdu_in_received_ampdu_count;
	/// Counts the number of CRC errors in MPDU delimeter of and A-MPDU
	uint32_t dot11_ampdu_delimiter_crc_error_count;
	/// Counts the number of failed attempts made to acquire a 20 MHz TXOP
	uint32_t rw_failed_20mhz_txop;
	/// Counts the number of successful attempts made to acquire a 20 MHz TXOP
	uint32_t rw_successful_20mhz_txop;
	/// Counts the number of failed attempts made to acquire a 40 MHz TXOP
	uint32_t rw_failed_40mhz_txop;
	/// Counts the number of successful attempts made to acquire a 40 MHz TXOP
	uint32_t rw_successful_40mhz_txop;
	/// Counts the number of failed attempts made to acquire a 80 MHz TXOP
	uint32_t rw_failed_80mhz_txop;
	/// Counts the number of successful attempts made to acquire a 80 MHz TXOP
	uint32_t rw_successful_80mhz_txop;
	/// diag rx: rx_ctl(filter)
	uint32_t rx_ctl_filter;
	/// Checks if a host buffer is currently available
	uint32_t rx_buf_check;
	/// txrx int
	uint32_t txrx_int;
	/// if print ipc rxbuf
	bool ipc_rxbuf_print;
	/// diag rx2, rx_idx
	uint32_t ipc_rxbuf_idx;
	/// diag rx2, dma_idx_buf0
	uint32_t dma_idx_buf0;
	/// diag rx2, dma_idx_buf0
	uint32_t dma_idx_buf31;
	/// rfpll reg
	uint16_t rf_reg_202;
	/// bbpll reg
	uint16_t rf_reg_3043;
	/// tpc with center freq
	uint16_t center_freq;
	/// dbg coex stats info.
	struct dbg_coex_stats dbg_coex_stats;
	/// diag dbg regs number
	uint16_t dbg_reg_num;
	/// diag dbg regs and value
	struct dbg_reg_stats dbg_reg[0];
};

struct phy_err_dbg {
	/// Access category
	uint8_t ac;
	/// ampdu
	bool ampdu;
	/// Buffer length already available for this mpdu or ampdu
	uint16_t available_len;
	/// lmac assert recovery phy error count
	uint32_t  phy_err_cnt;

	/// Current number of MPDUs inside the A-MPDU
	uint8_t  curr_cnt;
	/// PHY related flags field - NSS, rate, GI type, BW type used for the A-MPDU TX
	uint32_t phy_flags;

	/// Total length of the frame on air.
	uint32_t frmlen;
	/// Valid only for A-MPDU header descriptor and for singleton MPDUs.
	uint32_t phyctrlinfo;
	/// Valid only for A-MPDU header descriptor and for singleton MPDUs.
	uint32_t macctrlinfo1;
	// Valid only for MPDU header descriptor and for singleton MPDUs.
	uint32_t macctrlinfo2;
	/// Policy table (filled by the upper MAC)
	struct tx_policy_tbl tx_pol;
};

/// Diagnose chip register ind type
enum diag_reg_ind_type {
	/// dump diag reg in normal case
	DIAG_REG_IND_NORMAL = 0,
	/// dump diag reg when beacon lost
	DIAG_REG_IND_BEACON_LOST,
	/// dump diag reg when wait some status timeout
	DIAG_REG_IND_WAIT_TIMEOUT,
	/// dump diag reg when assert recovery occurs
	DIAG_REG_IND_ASSERT_RECOVERY,
	/// dump diag reg when assert recovery occurs
	DIAG_REG_IND_PHY_ERR,

	DIAG_REG_IND_MAX
};

/// info for DIAG_REG_IND_NORMAL
struct diag_normal_pram
{
	/// vif index
	uint8_t vif_index;
};

/// info for DIAG_REG_IND_BEACON_LOST
struct diag_beacon_lost_pram
{
	/// vif index
	uint8_t vif_index;
	/// beacon lost number
	uint8_t beacon_lost_nm;
	/// vif not on the working channel
	uint8_t off_chan_num;
};

/// info for DIAG_REG_IND_WAIT_TIMEOUT
struct diag_wait_timeout_pram
{
	/// wait timeout locationc
	uint32_t location;
};

struct mm_diag_reg_ind
{
	/// diag reg ind type
	uint8_t ind_type;
	/// diag parameter
	uint8_t diag_param[16];
	/// diag dump regs
	struct diag_reg_dump diag_reg;
};

/// Structure containing the parameters of delay statistics
struct delay_stats_result {
	/// Maximum delay value
	uint32_t max;
	/// Minimum delay value
	uint32_t min;
	/// Average delay value
	uint32_t avg;
};

/// Structure containing the parameters of the @ref MCU_GET_DELAY_STATS_INFO_CFM message
struct mcu_get_delay_stats_info_cfm {
	/// Bus delay statistics
	struct delay_stats_result bus_delay_stats;
	/// Ipc delay statistics
	struct delay_stats_result ipc_delay_stats;
};

#endif // LMAC_MSG_H_
