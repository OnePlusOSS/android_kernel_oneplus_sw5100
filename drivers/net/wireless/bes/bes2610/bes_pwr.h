/*
 * @file bes_pwr.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BES_PWR_
#define _BES_PWR_

#include "rwnx_defs.h"

/* The threshold of data rx traffic rate */
#define DATA_RX_TRAFFIC_RATE_THRESHOLD	20

/* The buffer size of pwr print */
#define BES_DBGFS_PWR_STATUS_BUFSIZE	1024

/* Max count for power save callback function */
#define PWR_CB_CNT_MAX		10

/*
 * Data type for power save callback
 * Note: This callback functions can have blocking operations
 */
typedef void (*bes_pwr_lp_cb)(void *arg, u8 status);

/* Data type for power save callback item */
struct bes_pwr_lp_cb_item {
    void *arg;
    bes_pwr_lp_cb cb;
};

/*
 * Low power state machine
 *
 * Flow chart:
 *               |------------------------
 *               v                       |
 * WAKING -> WAKE CONFIG -> WAKE -> SLEEP CONFIG -> SLEEPING -> SLEEPED
 *    ^                                                            |
 *    |------------------------------------------------------------|
 *
 * Note:
 *     If the WIFI_LP_SLEEP_CONFIG state in the sleep process is interrupted
 *     by the wake-up process (WIFI_LP_WAKING), it will directly enter the
 *     WIFI_LP_WAKE_CONFIG process.
 *
 * Description:
 *     WIFI_LP_WAKING: Raise GPIO to wake up WiFi
 *     WIFI_LP_WAKE_CONFIG: WiFi has been awakened (GPIO has been raised),
 *                          and the configuration work after WiFi wake-up
 *                          will be carried out next
 *     WIFI_LP_WAKE: WiFi wake-up process completed
 *     WIFI_LP_SLEEP_CONFIG: WiFi is still awake (GPIO has not been lowered yet),
 *                           and at this time, the main task is to configure before sleep
 *     WIFI_LP_SLEEPING: The configuration work before WiFi sleep has been completed,
 *                       and the next step is to proceed with the sleep WiFi
 *                       (lowering GPIO) operation
 *     WIFI_LP_SLEEPED: The WiFi sleep process is completed,
 *                      and at this time, the GPIO is at a low level
 */
enum pwr_wifi_lp_state {
	WIFI_LP_MIN = 0,
	WIFI_LP_WAKING = WIFI_LP_MIN,
	WIFI_LP_WAKE_CONFIG,
	WIFI_LP_WAKE,
	WIFI_LP_SLEEP_CONFIG,
	WIFI_LP_SLEEPING,
	WIFI_LP_SLEEPED,
	WIFI_LP_MAX
};

/* Pwr action
 *
 * Description:
 *     PWR_ACTION_WAKEUP: WiFi wake-up operation mainly involves raising the GPIO
 *     PWR_ACTION_WAKEUP_NOTIFY: Notify some events after WiFi wakes up and
 *                               perform some configuration work after wake-up
 *     PWR_ACTION_SLEEP_NOTIFY: Notify some events before WiFi sleep and
 *                              do some pre sleep configuration work
 *     PWR_ACTION_SLEEP: WiFi sleep operation mainly involves lowering the GPIO
 *     PWR_ACTION_SCHED: schedule lp work
 *     PWR_ACTION_WAIT_WAKE: Waiting lp state to WIFI_LP_WAKE
 */
enum pwr_action_type {
	PWR_ACTION_MIN = 0,
	PWR_ACTION_WAKEUP = PWR_ACTION_MIN,
	PWR_ACTION_WAKEUP_NOTIFY,
	PWR_ACTION_SLEEP_NOTIFY,
	PWR_ACTION_SLEEP,
	PWR_ACTION_SCHED,
	PWR_ACTION_WAIT_WAKE,
	PWR_ACTION_MAX
};

/*
 * Structure for pwr action param
 *
 * actions: action set, Each bit corresponds to a value in enum pwr_action_type.
 * cancel_sync: Judge whether to wait for the current completion before rescheduling.
 * sched_time: Record the delayed scheduling time planned.
 */
struct pwr_action_param {
	u32 actions;
	bool cancel_sync;
	unsigned long sched_time;
};

/*
 * Structure for pwr action
 *
 * action: pwr action type. Refer to enum pwr_action_type
 * pfun: callback processing function
 */
struct bes_pwr_action_process_t {
	u32 action;
	void (*pfun)(struct rwnx_hw *rwnx_hw, struct pwr_action_param *param);
};

/*
 * Wifi phy event
 *
 * Description:
 *     PHY_DELAY_EVENT_CONST_CLR: Set when the phy const  event is clear,
 *                                and automatically clear when the timeout expires
 *     PHY_CONST_EVENT_CONCURRENT: Set when coexisting
 *     PHY_CONST_EVENT_SCAN: Set when scanning
 *     PHY_CONST_EVENT_ROC: Set when doin roc
 *     PHY_CONST_EVENT_MSG_TX: Set when sending msg, and clear when sending completed
 *     PHY_CONST_EVENT_MSG_TX_PENDING: Prevent PHY_CONST_EVENT_MSG_TX from being cleared in advance
 *     PHY_CONST_EVENT_DATA_TX: Set when sending unbuffered data, and clear when sending completed
 *     PHY_CONST_EVENT_INIT: Set when initializing
 *     PHY_CONST_EVENT_DLY_WORK_FLUSH: Set this event to ensure that the completion varable of lp_delayed_work can be initialized and
 *                                     bes_wowlan_flush_delayed_work_handler function can be called before the host suspend
 *
 *     PHY_DELAY_EVENT_MIN/PHY_CONST_EVENT_MIN/PHY_EVENT_MAX:
 *         Used to count the number of events
 */
enum pwr_phy_event_type {
	PHY_DELAY_EVENT_MIN = 0,
	PHY_DELAY_EVENT_CONST_CLR = PHY_DELAY_EVENT_MIN,
	/* add new phy/delay event here */
	PHY_CONST_EVENT_MIN,
	PHY_CONST_EVENT_CONCURRENT = PHY_CONST_EVENT_MIN,
	PHY_CONST_EVENT_SCAN,
	PHY_CONST_EVENT_ROC,
	PHY_CONST_EVENT_MSG_TX,
	PHY_CONST_EVENT_MSG_TX_PENDING,
	PHY_CONST_EVENT_DATA_TX,
	PHY_CONST_EVENT_INIT,
	PHY_CONST_EVENT_DLY_WORK_FLUSH,
	/* add new phy/constant event here */
	PHY_EVENT_MAX
};

/*
 * Wifi vif event
 *
 * Description:
 *     VIF_DELAY_EVENT_CONST_CLR: Set when the vif const event is clear,
 *                                and automatically clear when the timeout expires
 *     VIF_DELAY_EVENT_TX_KEY_FRM: Set when the sending key frame,
 *                                 and automatically clear when the timeout expires
 *     VIF_DELAY_EVENT_DATA_TX_CLR: Set when VIF_CONST_EVENT_DATA_TX is clear,
 *                                  and automatically clear when the timeout expires
 *     VIF_DELAY_EVENT_DATA_RX: Set when rx data is match traffic standrad
 *                                  and automatically clear when the timeout expires
 *     VIF_DELAY_EVENT_ADD_VIF: Set when the adding vif,
 *                              and automatically clear when the timeout expires
 *     VIF_DELAY_EVENT_DEL_VIF: Set when the deleting vif,
 *                              and automatically clear when the timeout expires
 *     VIF_DELAY_EVENT_CH_LOAD_MEASURE: Set when measure channel load ,
 *                                      and automatically when the timeout expires
 *     VIF_CONST_EVENT_CONNECT: Set when the connecting
 *     VIF_CONST_EVENT_AP: Set when the existing ap
 *     VIF_CONST_EVENT_CSA: Set when the doing csa
 *     VIF_CONST_EVENT_ROAMED: Set during roaming reconnection
 *     VIF_CONST_EVENT_DATA_TX: Set when sending buffered data, and clear when sending completed
 *     VIF_CONST_EVENT_PSM: Set when setting the power save to MM_PS_MODE_OFF,
 *                          and clear when setting the power save to MM_PS_MODE_ON_DYN
 *     VIF_CONST_EVENT_MONITOR: Set when adding mointor interface
 *
 *     VIF_DELAY_EVENT_MIN/VIF_CONST_EVENT_MIN/VIF_EVENT_MAX:
 *         Used to count the number of events
 */
enum pwr_vif_event_type {
	VIF_DELAY_EVENT_MIN = 0,
	VIF_DELAY_EVENT_DATA_TX_CLR = VIF_DELAY_EVENT_MIN,
	VIF_DELAY_EVENT_DATA_RX,
	VIF_DELAY_EVENT_TX_KEY_FRM,
	VIF_DELAY_EVENT_ADD_VIF,
	VIF_DELAY_EVENT_DEL_VIF,
	VIF_DELAY_EVENT_CH_LOAD_MEASURE,
	/* add new vif/delay event here */
	VIF_CONST_EVENT_MIN,
	VIF_CONST_EVENT_CONNECT = VIF_CONST_EVENT_MIN,
	VIF_CONST_EVENT_AP,
	VIF_CONST_EVENT_CSA,
	VIF_CONST_EVENT_ROAMED,
	VIF_CONST_EVENT_DATA_TX,
	VIF_CONST_EVENT_GO_PRESENT,
	VIF_CONST_EVENT_PSM,
	VIF_CONST_EVENT_MONITOR,
	/* add new vif/constant event here */
	VIF_EVENT_MAX
};

/*
 * Wifi lp callback function
 *
 * Wakeup operation process:
 *      PWR_LP_CB_STATUS_WIFI_WAKEUP -> PWR_LP_CB_STATUS_IO_WAKEUP -> PWR_LP_CB_STATUS_AFTER_WIFI_WAKEUP
 * Sleep operation process:
 *      PWR_LP_CB_STATUS_BEFORE_WIFI_SLEEP -> PWR_LP_CB_STATUS_WIFI_SLEEP -> PWR_LP_CB_STATUS_IO_SLEEP
 */
enum pwr_lp_cb_status {
	PWR_LP_CB_STATUS_IO_SLEEP = 0,
	PWR_LP_CB_STATUS_IO_WAKEUP,
	PWR_LP_CB_STATUS_WIFI_SLEEP,
	PWR_LP_CB_STATUS_WIFI_WAKEUP,
	PWR_LP_CB_STATUS_BEFORE_WIFI_SLEEP,
	PWR_LP_CB_STATUS_AFTER_WIFI_WAKEUP,
};

/*
 * judge type
 *
 * PWR_LP_JUDGE_TYPE_AND: All bits must be set
 * PWR_LP_JUDGE_TYPE_OR: Only one bit needs to be set
 */
enum pwr_lp_judge_type {
	PWR_LP_JUDGE_TYPE_OR = 0,
	PWR_LP_JUDGE_TYPE_AND,
};

enum pwr_lp_event_type {
	PWR_LP_EVENT_FIRST = 0,
	PWR_LP_EVENT_JUDGE_SCHEDULE,
};

/* Data type for msg rx and tx */
typedef void (*bes_pwr_msg_rx_pfun)(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg);
typedef void (*bes_pwr_msg_tx_pfun)(struct rwnx_hw *rwnx_hw, struct rwnx_cmd_a2emsg *msg);
struct bes_pwr_msg_process_t {
	u16 id;
	union {
		bes_pwr_msg_rx_pfun rx_pfun;
		bes_pwr_msg_tx_pfun tx_pfun;
	};
};

/* Data type for managing delay event schedule time */
struct pwr_phy_d_evt_timeout {
	enum pwr_phy_event_type id;
	unsigned long timeout;
};
struct pwr_vif_d_evt_timeout {
	enum pwr_vif_event_type id;
	unsigned long timeout;
};

#ifdef CONFIG_BES_WIFI_LP
/* pwr event mask */
extern const u32 phy_delay_event_mask;
extern const u32 phy_const_event_mask;
extern const u32 vif_delay_event_mask;
extern const u32 vif_const_event_mask;

/* get bit value */
static inline bool val_get_bit(u32 val, u8 bit)
{
	if (bit > 31)
		return 0;

	return !!(val & BIT(bit));
}

/* get event */
static inline u32 bes_pwr_get_phy_const_event(struct bes_pwr *pwr)
{
	return pwr->phy_event & phy_const_event_mask;
}

static inline u32 bes_pwr_get_phy_delay_event(struct bes_pwr *pwr)
{
	return pwr->phy_event & phy_delay_event_mask;
}

static inline u32 bes_pwr_get_vif_const_event(struct bes_pwr *pwr, u8 idx)
{
	return pwr->vif_event[idx] & vif_const_event_mask;
}

static inline u32 bes_pwr_get_vif_delay_event(struct bes_pwr *pwr, u8 idx)
{
	return pwr->vif_event[idx] & vif_delay_event_mask;
}

static inline u32 bes_pwr_get_phy_const_event_lock(struct rwnx_hw *rwnx_hw)
{
	u32 event = 0;

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	event = rwnx_hw->pwr.phy_event & phy_const_event_mask;
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

	return event;
}

static inline u32 bes_pwr_get_phy_delay_event_lock(struct rwnx_hw *rwnx_hw)
{
	u32 event = 0;

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	event = rwnx_hw->pwr.phy_event & phy_delay_event_mask;
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

	return event;
}

static inline u32 bes_pwr_get_vif_const_event_lock(struct rwnx_hw *rwnx_hw, u8 idx)
{
	u32 event = 0;

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	event = rwnx_hw->pwr.vif_event[idx] & vif_const_event_mask;
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

	return event;
}

static inline u32 bes_pwr_get_vif_delay_event_lock(struct rwnx_hw *rwnx_hw, u8 idx)
{
	u32 event = 0;

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	event = rwnx_hw->pwr.vif_event[idx] & vif_delay_event_mask;
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

	return event;
}

/* clear delay event */
static inline void bes_pwr_clear_vif_delay_event(struct rwnx_hw *rwnx_hw, u8 vif_idx, enum pwr_vif_event_type event)
{
	rwnx_hw->pwr.vif_event[vif_idx] &= ~BIT(event);
}

static inline void bes_pwr_clear_phy_delay_event(struct rwnx_hw *rwnx_hw, enum pwr_phy_event_type event)
{
	rwnx_hw->pwr.phy_event &= ~BIT(event);
}

/* get the count of up or link vif */
static inline u8 bes_pwr_get_vif_up_cnt(struct rwnx_hw *rwnx_hw)
{
	u8 up_cnt = 0;

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	up_cnt = hweight32(rwnx_hw->pwr.vif_up_map);
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

	return up_cnt;
}

static inline u8 bes_pwr_get_vif_link_cnt(struct rwnx_hw *rwnx_hw)
{
	u8 link_cnt = 0;

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	link_cnt = hweight32(rwnx_hw->pwr.vif_link_map);
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

	return link_cnt;
}

static inline u32 bes_pwr_get_vif_up_map(struct rwnx_hw *rwnx_hw)
{
	u32 vif_up_map = 0;

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	vif_up_map = rwnx_hw->pwr.vif_up_map;
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

	return vif_up_map;
}

static inline u32 bes_pwr_is_vif_up(struct rwnx_hw *rwnx_hw, u8 vif_idx)
{
	bool is_vif_up = false;

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	is_vif_up = val_get_bit(rwnx_hw->pwr.vif_up_map, vif_idx);
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

	return is_vif_up;
}

/* Setting and checking skip lp processing flag for msg */
static inline bool bes_pwr_msg_check_skip(struct rwnx_hw *rwnx_hw)
{
	bool in = false;

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	in = (rwnx_hw->pwr.pwr_task == current);
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

	return in;
}

static inline bool bes_pwr_get_lp_not_care_flag(struct rwnx_cmd *cmd)
{
	if ((cmd->flags & RWNX_CMD_FLAG_LP_NOT_CARE_ACK) ||
	    (cmd->flags & RWNX_CMD_FLAG_LP_NOT_CARE_CFM))
		return true;

	return false;
}

/* Set/Get pwr state */
static inline void bes_pwr_wifi_lp_state_set(struct rwnx_hw *rwnx_hw, enum pwr_wifi_lp_state state)
{
	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	rwnx_hw->pwr.state = state;
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);
}

static inline u8 bes_pwr_get_pwr_state(struct rwnx_hw *rwnx_hw)
{
	u8 state = 0;

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	state = rwnx_hw->pwr.state;
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

	return state;
}

static inline bool bes_pwr_wifi_lp_state_check(struct rwnx_hw *rwnx_hw, enum pwr_wifi_lp_state state)
{
	bool match = false;

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	match = (rwnx_hw->pwr.state == state);
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

	return match;
}

static inline bool bes_pwr_wifi_is_wake(struct rwnx_hw *rwnx_hw)
{
	bool awake = false;

	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	awake = (rwnx_hw->pwr.state == WIFI_LP_WAKE_CONFIG ||
		 rwnx_hw->pwr.state == WIFI_LP_WAKE);
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);

	return awake;
}

static inline bool bes_pwr_in_wakeup_process(struct rwnx_hw *rwnx_hw)
{
	return (rwnx_hw->pwr.state == WIFI_LP_WAKING ||
		rwnx_hw->pwr.state == WIFI_LP_WAKE_CONFIG ||
		rwnx_hw->pwr.state == WIFI_LP_WAKE);
}

/* Set the flag to indicate before host suspend */
static inline void bes_pwr_set_flag_suspend(struct rwnx_hw *rwnx_hw, bool en)
{
	spin_lock_bh(&rwnx_hw->pwr.pwr_lock);
	rwnx_hw->pwr.flag_suspend = en;
	spin_unlock_bh(&rwnx_hw->pwr.pwr_lock);
}

/* lp init and deinit */
int bes_pwr_init(struct rwnx_hw *rwnx_hw, bes_pwr_wake_ctrl_ptr wake_ctrl);
void bes_pwr_deinit(struct rwnx_hw *rwnx_hw);

/* common lp processing function*/
void bes_pwr_set_busy_event(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 event);
void bes_pwr_set_busy_event_async(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 event);
void bes_pwr_clear_busy_event(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 event);

/* lp processing for msg or data transmission */
void bes_pwr_msg_ack_rx_process(struct rwnx_hw *rwnx_hw);
void bes_pwr_msg_rx_process(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg);
void bes_pwr_data_rx_process(struct rwnx_hw *rwnx_hw, const struct sk_buff *skb,
			      const struct hw_rxhdr *hw_rxhdr);
bool bes_pwr_msg_tx_process(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd);
void bes_pwr_data_tx_process(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta, bool tx, bool buffered);

/* lp processing for getting gateway ip */
void bes_pwr_get_gateway_ip(struct rwnx_hw *rwnx_hw, struct rwnx_vif *rwnx_vif, struct sk_buff *skb);

/* register and unregister lp cb */
int bes_pwr_register_lp_cb(struct rwnx_hw *rwnx_hw, bes_pwr_lp_cb cb, void *arg);
void bes_pwr_unregister_lp_cb(struct rwnx_hw *rwnx_hw, bes_pwr_lp_cb cb);
void bes_pwr_call_lp_cb(struct rwnx_hw *rwnx_hw, enum pwr_lp_cb_status status);

/* low-power event info print*/
int bes_pwr_event_dump(struct rwnx_hw *rwnx_hw, char *buf, size_t size, size_t count);

/* get internal state from low power module */
bool bes_pwr_exist_pwr_events(struct rwnx_hw *rwnx_hw);

/* Deal with lp event before host suspend */
bool bes_pwr_handle_lp_evt_before_suspend(struct rwnx_hw *rwnx_hw);
#else
/* get event */
static inline u32 bes_pwr_get_phy_const_event_lock(struct rwnx_hw *rwnx_hw) { return 0; }
static inline u32 bes_pwr_get_phy_delay_event_lock(struct rwnx_hw *rwnx_hw) { return 0; }
static inline u32 bes_pwr_get_vif_const_event_lock(struct rwnx_hw *rwnx_hw, u8 idx) { return 0; }
static inline u32 bes_pwr_get_vif_delay_event_lock(struct rwnx_hw *rwnx_hw, u8 idx) { return 0; }

/* get the count of up or link vif */
static inline u8 bes_pwr_get_vif_up_cnt(struct rwnx_hw *rwnx_hw) { return 0; }
static inline u8 bes_pwr_get_vif_link_cnt(struct rwnx_hw *rwnx_hw) { return 0; }

/* Setting and checking skip lp processing flag for msg */
static inline bool bes_pwr_msg_check_skip(struct rwnx_hw *rwnx_hw) { return false; }

/* Get pwr state */
static inline u8 bes_pwr_get_pwr_state(struct rwnx_hw *rwnx_hw) { return WIFI_LP_WAKE; }
static inline bool bes_pwr_wifi_is_wake(struct rwnx_hw *rwnx_hw) { return true; }

/* lp init and deinit */
static inline int bes_pwr_init(struct rwnx_hw *rwnx_hw, bes_pwr_wake_ctrl_ptr wake_ctrl) { return 0; }
static inline void bes_pwr_deinit(struct rwnx_hw *rwnx_hw) {}

/* common lp processing function*/
static inline void bes_pwr_set_busy_event(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 event) {}
static inline void bes_pwr_set_busy_event_async(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 event) {}
static inline void bes_pwr_clear_busy_event(struct rwnx_hw *rwnx_hw, u8 vif_idx, u8 event) {}

/* lp processing for msg or data transmission */
static inline void bes_pwr_msg_ack_rx_process(struct rwnx_hw *rwnx_hw) {}
static inline void bes_pwr_msg_rx_process(struct rwnx_hw *rwnx_hw, struct ipc_e2a_msg *msg) {}
static inline void bes_pwr_data_rx_process(struct rwnx_hw *rwnx_hw, const struct sk_buff *skb,
		const struct hw_rxhdr *hw_rxhdr) {}
static inline bool bes_pwr_msg_tx_process(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd) { return false; }
static inline void bes_pwr_data_tx_process(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta, bool tx, bool buffered) {}

/* lp processing for getting gateway ip */
static inline void bes_pwr_get_gateway_ip(struct rwnx_hw *rwnx_hw,
		struct rwnx_vif *rwnx_vif, struct sk_buff *skb) {}

/* register and unregister lp cb */
static inline int bes_pwr_register_lp_cb(struct rwnx_hw *rwnx_hw, bes_pwr_lp_cb cb, void *arg) { return 0; }
static inline void bes_pwr_unregister_lp_cb(struct rwnx_hw *rwnx_hw, bes_pwr_lp_cb cb) {}
static inline void bes_pwr_call_lp_cb(struct rwnx_hw *rwnx_hw, enum pwr_lp_cb_status status) {}

/* low-power event info print*/
static inline int bes_pwr_event_dump(struct rwnx_hw *rwnx_hw,
				     char *buf, size_t size, size_t count) { return 0; }

/* get internal state from low power module */
static inline bool bes_pwr_exist_pwr_events(struct rwnx_hw *rwnx_hw) { return true; }

/* Deal with lp event before host suspend */
/* Set the flag to indicate before host suspend */
static inline bool bes_pwr_handle_lp_evt_before_suspend(struct rwnx_hw *rwnx_hw) { return false; }
static inline void bes_pwr_set_flag_suspend(struct rwnx_hw *rwnx_hw, bool en) {}
#endif

#endif /* _BES_PWR_ */
