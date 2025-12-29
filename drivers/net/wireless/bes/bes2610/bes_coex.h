/*
 * @file bes_coex.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __BES_COEX_H__
#define __BES_COEX_H__

#include "rwnx_defs.h"

/* the sizeof of coex_bt dump buf */
#define BES_COEX_BT_STATE_DUMP_BUFSIZE	512

/* wifi/bt coex period */
#define BES_COEX_PERIOD						100000

/* wifi/bt coex command */
enum bes_coex_cmd_id {
	// Command id for setting coex mode（fdd|tdd）
	BES_COEX_CMD_MODE = 0,
	// Command id for setting time ratdio between wifi and bt
	BES_COEX_CMD_DURATION,
	// Command id for starting coex schedule
	BES_COEX_CMD_START,
	// Command id for stopping coex schedule
	BES_COEX_CMD_STOP,
	// Command id for set tx/rx info, unused
	BES_COEX_CMD_TXRX_INFO,
	// Command id for simulating transq request
	BES_COEX_CMD_TRANSQ_TEST,
	// Command id for setting tports info
	BES_COEX_CMD_GOIO_TEST,
	// Command id for setting pta falgs
	BES_COEX_CMD_SET_PTA_FLAG,
	// Command id for low power schedule
	BES_COEX_CMD_LP_SCHE,
	// Command id for low power setting
	BES_COEX_CMD_LP_MODE,
	// Command id for hardware epta setting
	BES_COEX_CMD_HW_EPTA,
	// Command id for pta test
	BES_COEX_CMD_PTA_TEST,
	// Command id for updating bt state
	COEX_CMD_ID_BT_STAT
};

/* wifi/bt coex mode */
enum bes_coex_mode {
	// TDD Mode that wifi and bt use same antenna
	BES_COEX_MODE_TDD = 0,
	// FDD Mode that wifi and bt use seperate antennas
	BES_COEX_MODE_FDD,
};

/* wifi/bt pta flags */
enum bes_coex_pta_flag{
	// dispense time to bt when wifi roc
	BES_COEX_ROC_PTA_SWITCH = 0,
	// dispense time to bt when beacon detection
	BES_COEX_BCN_DETECT_PTA_SWITCH,
	// dispense time to bt when wifi scan
	BES_COEX_SCAN_PTA_SWITCH,
};

/* Wifi Operation status that relates to wifi/bt coex */
enum bes_coex_wifi_state {
	// wifi is connecting to a 2.4g ap
	WIFI_2G4_CONNECTING = 0,
	// wifi is connected to a 2.4g ap
	WIFI_2G4_CONNECTED,
	// wifi is doing roc on 2.4g band
	WIFI_2G4_ROC,
	// wifi is scanning on 2.4g band
	WIFI_2G4_SCAN,
	// wifi is setting up an 2.4g ap
	WIFI_2G4_AP_SETTING,
	// a 2.4g ap is working
	WIFI_2G4_AP_SETTED,

	// max wifi coex event
	WIFI_2G4_COEX_MAX
};

// BT Audio Status
enum bes_coex_bt_audio_state {
	// No voice transfer
	BES_BT_AUDIO_NONE               = 0,
	// bt is playing music
	BES_BT_AUDIO_A2DP,
	// bt is calling
	BES_BT_AUDIO_SCO,
	// max bt audio state
	BES_BT_AUDIO_MAX
};

// Arbitrate Operation
enum bes_coex_arb_oper {
	// Get Current bt status
	BES_ARB_OPER_GET_BT_STATUS = 0,
	// Sync time ratio to device
	BES_ARB_OPER_DUR_SET,
	// Sync bt state to device
	BES_ARB_OPER_SYNC_BT_STATE,
	// Set Coex Work Mode
	BES_ARB_OPER_COEX_MODE,
	// Start coex schedule
	BES_ARB_OPER_COEX_START,
	// Stop coex schedule
	BES_ARB_OPER_COEX_STOP,
	// update pta flags
	BES_ARB_OPER_PTA_FLAGS,
	// enable/disable temporature calibration
	BES_ARB_OPER_RF_CALI,
	// Trigger AP Channel Switch
	BES_ARB_OPER_AP_CSA,
	// Max ARB Operation
	BES_ARB_OPER_MAX
};

enum bes_tport_io
{
    /* set high when tbtt start, process rx beacon set low */
    BES_TPORT_IO_BEACON = 0,
    /* set high when pta grant wifi, set low when pta grant bt */
    BES_TPORT_IO_WIFI_WIN,
    /* set high when system up, set low when system doze */
    BES_TPORT_IO_WAKEUP,
    /* set high when pta unblock bt/wifi txrx, others set low */
    BES_TPORT_IO_FDD,
    /* set high when send null data(pm=1), set low when null data send success */
    BES_TPORT_IO_PM_NULL_DATA,
    /* set high when coex chan start, set low when coex chan stop */
    BES_TPORT_IO_CHAN_START,
    /* set high when transq win start, set low when transq win end */
    BES_TPORT_IO_TRANSQ_WIN,
    /* limit with sizeof(@COEX_CMD_T), now is 8. should not change it */
    BES_TPORT_IO_MAX = 8,
};

enum bes_pta_mode
{
	/* pta mode of exiting calibration mode */
    COEX_TEST_PTA_EXIT_CAL_MODE  = 0,
	/* pta mode of entering calibration mode */
    COEX_TEST_PTA_SET_CAL_MODE,
	/* pta mode fdd mode */
    COEX_TEST_PTA_SET_FDD_MODE,
	/* pta mode of unblock bt mode */
    COEX_TEST_PTA_SET_UNBLOCKBT_MODE,
	/* pta mode of block bt mode */
    COEX_TEST_PTA_SET_BLOCKBT_MODE,
	/* the number of pta mode */
    COEX_TEST_PTA_MAX_MODE,
};

/* wifi and bt time ratio for coex */
struct bes_coex_time_ratio {
	// wifi duration
	int wifi_duration;
	// bt duration
	int bt_duration;
};

/* wifi stream information */
struct bes_coex_txrx_info {
	u32 total_bps[3];
};

/* transq request information */
struct bes_coex_transq_info {
	// the time length of transq request
	u32 duration;
	// 0 is immediate start, 1 is stop.
	u32 start;
	// the period of transq request
	u32 period;
};

/* coex tports information */
struct bes_coex_tpors_info_v0 {
	// enable/disable tports
	u32 enable;
#if defined(__LITTLE_ENDIAN_BITFIELD)
	// io for beacon receive
	u32 io_bcn:8;
	// io for wifi window
	u32 io_wifi_win:8;
	// io for doze
	u32 io_doze:8;
	// io for work mode
	u32 io_fdd:8;
#elif defined (__BIG_ENDIAN_BITFIELD)
	// io for work mode
	u32 io_fdd:8;
	// io for doze
	u32 io_doze:8;
	// io for wifi window
	u32 io_wifi_win:8;
	// io for beacon receive
	u32 io_bcn:8;
#endif
};

struct bes_coex_tpors_info_v1 {
	// enable/disable tports
    u32 enable;
	// array for io index
    u8 gpio[BES_TPORT_IO_MAX];
};

/* structure for coex command */
struct bes_coex_cmd {
	/* coex command id */
	u32 coex_cmd_id;
	union {
		/* coex mode */
		int coex_mode;
		/* coex time ratio info */
		struct bes_coex_time_ratio time_ratio;
		/* wifi txrx info */
		struct bes_coex_txrx_info txrx_info;
		/* transq request info */
		struct bes_coex_transq_info coex_transq;
		/* coex tports info(version 0) */
		struct bes_coex_tpors_info_v0 tports_v0;
		/* coex tports info(version 1) */
		struct bes_coex_tpors_info_v1 tports_v1;
		/* coex pta switch flags */
		u32 pta_switch_flags;
		/* coex low power schedule */
		u32 lp_sche;
		/* coex pta work mode */
		u32 pta_mode;
		/* bt state */
		u32 bt_state;
	} u;
};

#ifdef CONFIG_BES_COEX_SUPPORT
/* bt releated coex interface */
void bes_coex_bt_init(void);
u32 bes_coex_bt_calc_time(void);
int bes_coex_bt_status_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg);
void bes_coex_bt_sync_status(struct rwnx_hw *rwnx_hw);
u32 bes_coex_bt_get_audio_state(struct rwnx_hw *rwnx_hw);
bool bes_coex_bt_closed(struct rwnx_hw *rwnx_hw);
bool bes_coex_bt_audio_state_change(u32 old_state, u32 new_state);
bool bes_coex_bt_state_change(u32 old_state, u32 new_state);

/* wifi releated coex interface */
void bes_coex_wifi_msg_tx_proc(struct rwnx_hw *rwnx_hw, struct rwnx_cmd_a2emsg *msg);
bool bes_coex_wifi_msg_rx_proc(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg);
void bes_coex_wifi_rx_process(struct rwnx_hw *rwnx_hw, const struct sk_buff *skb);
u32 bes_coex_wifi_get_status(void);
void bes_coex_wifi_set_state(u8 vif_idx, u8 event, bool set);
void bes_coex_wifi_init(void);

/* arb releated coex interface */
void bes_coex_arb_up(struct rwnx_hw *rwnx_hw);
void bes_coex_arb_down(struct rwnx_hw *rwnx_hw);
void bes_coex_arb_init(struct rwnx_hw *rwnx_hw, bool fdd);
void bes_coex_arb_deinit(struct rwnx_hw *rwnx_hw);
bool bes_coex_arb_check_sta_assoc(struct rwnx_hw *rwnx_hw, struct ieee80211_channel *channel);
bool bes_coex_arb_check_ap_setup(struct rwnx_hw *rwnx_hw, struct cfg80211_chan_def *chandef);
void bes_coex_arb_set_wifi_state(struct rwnx_hw *rwnx_hw, u32 wifi_state, u32 action);
void bes_coex_arb_set_bt_state(struct rwnx_hw *rwnx_hw, u32 bt_state, u32 action);
void bes_coex_arb_set_pta_flags(struct rwnx_hw *rwnx_hw, u32 flag, bool set);

/* dbg interface */
int bes_coex_bt_state_info(char *buf, int maxlen, u32 bt_status);
#else
static inline void bes_coex_arb_up(struct rwnx_hw *rwnx_hw) { }
static inline void bes_coex_arb_down(struct rwnx_hw *rwnx_hw) { }
static inline void bes_coex_arb_init(struct rwnx_hw *rwnx_hw, bool fdd) { }
static inline void bes_coex_arb_deinit(struct rwnx_hw *rwnx_hw) { }
static inline bool bes_coex_arb_check_sta_assoc(struct rwnx_hw *rwnx_hw, struct ieee80211_channel *channel) { return true; }
static inline bool bes_coex_arb_check_ap_setup(struct rwnx_hw *rwnx_hw, struct cfg80211_chan_def *chandef) { return true; }
static inline int bes_coex_bt_status_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg) { return 0; }
static inline u32 bes_coex_bt_get_audio_state(struct rwnx_hw *rwnx_hw) { return BES_BT_AUDIO_NONE; }
static inline bool bes_coex_bt_closed(struct rwnx_hw *rwnx_hw) { return false; }
static inline void bes_coex_wifi_msg_tx_proc(struct rwnx_hw *rwnx_hw, struct rwnx_cmd_a2emsg *msg) { }
static inline bool bes_coex_wifi_msg_rx_proc(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg) { return false; }
static inline void bes_coex_wifi_rx_process(struct rwnx_hw *rwnx_hw, const struct sk_buff *skb) { }
static inline int bes_coex_bt_state_info(char *buf, int maxlen, u32 bt_status) { return 0; }
#endif  // CONFIG_BES_COEX_SUPPORT

#endif
