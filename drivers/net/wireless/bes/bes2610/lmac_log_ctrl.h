/*
 * @file lmac_log_ctrl.h
 *
 * Copyright (c) 2022, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __LMAC_LOG_FILTER_H__
#define __LMAC_LOG_FILTER_H__

/* LMAC DBG Log Mode */
#define DBG_MOD_ALL         0xFFFFFFFF

/* LMAC Log Dump Mode*/
enum auto_lmac_log_status {
	/** Dump mode of the LMAC log when host is suspend */
	AUTO_LMAC_LOG_SUSPEND_STATUS,
	/** Dump mode of the LMAC log when host is resume */
	AUTO_LMAC_LOG_RESUME_STATUS,
};

/* LMAC Log Component: Specific filter for KERNEL component */
enum trace_level_kernel
{
	/** Error */
	TRACE_LVL_KERNEL_ERR,
	/** Messages to API layer */
	TRACE_LVL_KERNEL_MSG_API,
	/** Messages from API layer or internal task */
	TRACE_LVL_KERNEL_MSG_LOCAL,
	/** State changes */
	TRACE_LVL_KERNEL_STATE,
	/** keep last */
	TRACE_LVL_KERNEL_MAX,
};

/* LMAC Log Component: Specific filter for LMAC */
enum trace_level_lmac
{
	/** Error */
	TRACE_LVL_LMAC_ERR,
	/** Trace all frame sent by LMAC (except beacons) */
	TRACE_LVL_LMAC_TX,
	/** Trace frame received by LMAC from known STA*/
	TRACE_LVL_LMAC_RX,
	/** Trace frame received by LMAC from unknown STA*/
	TRACE_LVL_LMAC_RX_ALL,
	/** Trace when MACHW enter/exit doze mode */
	TRACE_LVL_LMAC_DOZE,
	/** Trace when a block agreement is created/deleted */
	TRACE_LVL_LMAC_BA,
	/** Trace TX power configuration*/
	TRACE_LVL_LMAC_TPC,
	/** Trace Traffic Detection general status */
	TRACE_LVL_LMAC_TD,
	/** Trace Traffic Detection Debug messages*/
	TRACE_LVL_LMAC_TD_DBG,
	/** Trace frame sent by LMAC with TB */
	TRACE_LVL_LMAC_TX_TB,
	/** Trace frame by LMAC RC */
	TRACE_LVL_LMAC_RC,
	/** Trace frame by LMAC CCA */
	TRACE_LVL_LMAC_CCA,
	/** Trace frame by LMAC RF Debug messages*/
	TRACE_LVL_LMAC_RF_DBG,
	/** keep last */
	TRACE_LVL_LMAC_MAX,
};

/* LMAC Log Component: Specific filter for Channel context */
enum trace_level_chan
{
	/** Error */
	TRACE_LVL_CHAN_ERR,
	/** Info (actual channel switch)*/
	TRACE_LVL_CHAN_INF,
	/* create/update/destroy and Link/unlink */
	TRACE_LVL_CHAN_CREATE,
	/** Channel Switch progress */
	TRACE_LVL_CHAN_SWITCH,
	/** Ctxt status update */
	TRACE_LVL_CHAN_STATUS,
	/** Connection-less (SCAN, ROC) channel context */
	TRACE_LVL_CHAN_CON_LESS,
	/** Action related to Notice of Absence */
	TRACE_LVL_CHAN_NOA,
	/** Handling of TBTT for various vifs */
	TRACE_LVL_CHAN_TBTT,
	/** DEBUG COEX LOG info */
	TRACE_LVL_CHAN_COEX,
	/** keep last */
	TRACE_LVL_CHAN_MAX,
};

/* LMAC Log Component: Specific filter for STA component */
enum trace_level_sta
{
	/** Error */
	TRACE_LVL_STA_ERR,
	/** Beacon received on STA interfaces */
	TRACE_LVL_STA_BCN,
	/** Power Save status of STA interfaces */
	TRACE_LVL_STA_PS,
	/** Connection/disconnection of STA interfaces */
	TRACE_LVL_STA_CONNECT,
	/** Sched scan on STA interfaces */
	TRACE_LVL_STA_SCHED_SCAN,
	/** keep last */
	TRACE_LVL_STA_MAX,
};

/* LMAC Log Component: Specific filter for AP component */
enum trace_level_ap
{
	/** Error */
	TRACE_LVL_AP_ERR,
	/** Beacon transmitted on AP interfaces */
	TRACE_LVL_AP_BCN,
	/** PS traffic requested/sent by/to connected STA */
	TRACE_LVL_AP_PS,
	/** keep last */
	TRACE_LVL_AP_MAX,
};

/* LMAC Log Component: Specific filter for P2P component */
enum trace_level_p2p
{
	/** Error */
	TRACE_LVL_P2P_ERR,
	/** creation/deletion/activation of P2P interfaces */
	TRACE_LVL_P2P_ITF,
	/** Opportunistic PS information */
	TRACE_LVL_P2P_PS,
	/** Notice of Absence */
	TRACE_LVL_P2P_NOA,
	/** Notice of Absence details*/
	TRACE_LVL_P2P_NOA_DET,
	/** TBTT */
	TRACE_LVL_P2P_TBTT,
	/** keep last */
	TRACE_LVL_P2P_MAX,
};

/* LMAC Log Component: Specific filter for MESH component */
enum trace_level_mesh
{
	/** Error */
	TRACE_LVL_MESH_ERR,
	/** General status */
	TRACE_LVL_MESH_INF,
	/** Transition of power save mode */
	TRACE_LVL_MESH_PS,
	/** Start/end of Service period */
	TRACE_LVL_MESH_SP,
	/** Start/end of Awake window (local and peer) */
	TRACE_LVL_MESH_AWIN,
	/** When a VIF/PEER prevent to enter sleep mode */
	TRACE_LVL_MESH_SLEEP,
	/** Received frames from peers */
	TRACE_LVL_MESH_RX,
	/** Received beacons */
	TRACE_LVL_MESH_BCN_RX,
	/** Transmitted beacons */
	TRACE_LVL_MESH_BCN_TX,
	/** Postponed traffic (because of Power Save) */
	TRACE_LVL_MESH_POST_TRAFFIC,
	/** Frame send/receiving for Hybrid Wireless Mesh Protocol */
	TRACE_LVL_MESH_HWMP,
	/** Detailed information about mesh path */
	TRACE_LVL_MESH_MPATH,
	/** Mesh Peering Management */
	TRACE_LVL_MESH_MPM,
	/** TBTT of local mesh interfaces and connected peers */
	TRACE_LVL_MESH_TBTT,
	/** keep last */
	TRACE_LVL_MESH_MAX,
};

/* LMAC Log Component: Specific filter for TDLS component */
enum trace_level_tdls
{
	/** Error */
	TRACE_LVL_TDLS_ERR,
	/** TDLS channel Switch progress  */
	TRACE_LVL_TDLS_CHSWITCH,
	/** keep last */
	TRACE_LVL_TDLS_MAX,
};

/* LMAC Log Component: Specific filter for Radio Measurement component */
enum trace_level_rm
{
	/** Error */
	TRACE_LVL_RM_ERR,
	/** Warning */
	TRACE_LVL_RM_WARN,
	/** Radio Measurement  */
	TRACE_LVL_RM_MEASURE,
	/** Radio Measurement Report */
	TRACE_LVL_RM_REPORT,
	/** Radio Measurement Request  */
	TRACE_LVL_RM_REQUEST,
	/** keep last */
	TRACE_LVL_RM_MAX,
};

/* LMAC Log Component: Specific filter for RTOS component */
enum trace_level_rtos
{
	/** Error */
	TRACE_LVL_RTOS_ERR,
	/** Memory allocation */
	TRACE_LVL_RTOS_ALLOC,
	/** Memory liberation */
	TRACE_LVL_RTOS_FREE,
	/** Creation/destruction of RTOS tasks */
	TRACE_LVL_RTOS_CREATE,
	/** Suspension/resumption of RTOS tasks */
	TRACE_LVL_RTOS_SUSPEND,
	/** RTOS Task becomes active */
	TRACE_LVL_RTOS_SWITCH_IN,
	/** RTOS Task is no longer active */
	TRACE_LVL_RTOS_SWITCH_OUT,
	/** keep last */
	TRACE_LVL_RTOS_MAX,
};

/* LMAC Log Component: Specific filter for FHOST component */
enum trace_level_fhost
{
	/** Error */
	TRACE_LVL_FHOST_ERR,
	/** wpa_supplicant task state machine */
	TRACE_LVL_FHOST_WPA_FSM,
	/** Trace command sent to wpa_supplicant */
	TRACE_LVL_FHOST_WPA_CMD,
	/** Trace update of wpa_supplicant status */
	TRACE_LVL_FHOST_WPA_INFO,
	/** DHCP */
	TRACE_LVL_FHOST_DHCP,
	/** FHOST Tasks initialization */
	TRACE_LVL_FHOST_TASK_INIT,
	/** smart_config */
	TRACE_LVL_FHOST_SMARTCONF,
	/** keep last */
	TRACE_LVL_FHOST_MAX,
};

/* LMAC Log Component: Specific filter for FHOST APPLICATION component */
enum trace_level_app
{
	/** Error */
	TRACE_LVL_APP_ERR,
	/** Warning */
	TRACE_LVL_APP_WARN,
	/** Information (depends of the application used) */
	TRACE_LVL_APP_INF,
	/** Debug (depends of the application used) */
	TRACE_LVL_APP_DBG,
	/** keep last */
	TRACE_LVL_APP_MAX,
};

/* LMAC Log Component: Specific filter for TWT component */
enum trace_level_twt
{
	/** Error */
	TRACE_LVL_TWT_ERR,
	/** General status */
	TRACE_LVL_TWT_INF,
	/** Received frames from peers */
	TRACE_LVL_TWT_RX,
	/** Transmitted frames to peers */
	TRACE_LVL_TWT_TX,
	/** Debug traces*/
	TRACE_LVL_TWT_DBG,
	/** keep last */
	TRACE_LVL_TWT_MAX,
};

/* LMAC Log Component: Specific filter for FTM component */
enum trace_level_ftm
{
	/** Error */
	TRACE_LVL_FTM_ERR,
	/** General status */
	TRACE_LVL_FTM_INF,
	/** keep last */
	TRACE_LVL_FTM_MAX,
};

/* LMAC Log Component: Specific filter for COEX component */
enum trace_level_coex
{
	/** PTA transform information */
	TRACE_LVL_COEX_PTA,
	/** COEX channel information */
	TRACE_LVL_COEX_CHAN,
	/** Transmitted/received frames from peers */
	TRACE_LVL_COEX_TXRX,
	/** BT transq information */
	TRACE_LVL_COEX_TRANSQ,
	/** Notice of absence */
	TRACE_LVL_COEX_NOA,
	/** coext main */
	TRACE_LVL_COEX_MAIN,
	/** Bt control transq */
	TRACE_LVL_COEX_BTC,
	/** Bt SCO stat */
	TRACE_LVL_COEX_SCO,
	/** Bt PROTECT stat */
	TRACE_LVL_COEX_PROT,
	/** keep last */
	TRACE_LVL_COEX_MAX,
};

int lmac_log_filter_show(struct rwnx_hw *rwnx_hw, char *show_buf, u32 buf_len);
int lmac_log_filter_set(struct rwnx_hw *rwnx_hw, const char __user *user_buf, size_t count, loff_t *ppos);

int bes_get_lmac_log_level(struct rwnx_hw *rwnx_hw, u32 *trace_compo_level);
int bes_set_lmac_log_level(struct rwnx_hw *rwnx_hw, u32 *trace_compo_level, u32 compon_mask);
int bes_get_lmac_log_mode(struct rwnx_hw *rwnx_hw, u8 *log_mode);
int bes_set_lmac_log_mode(struct rwnx_hw *rwnx_hw, u8 log_mode);

void bes_auto_set_fw_log(struct rwnx_hw *rwnx_hw, enum auto_lmac_log_status lmac_log_status);
#endif // __LMAC_LOG_FILTER_H__