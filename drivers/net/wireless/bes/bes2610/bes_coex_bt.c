/*
 * @file bes_coex_bt.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "bes_coex.h"
#include "bes_log.h"
#include "rwnx_msg_tx.h"

// BIT[2:0]
#define BT_REQUEST_STATUS_SHIFT       0
#define BT_REQUEST_STATUS_MASK        (0x7 << BT_REQUEST_STATUS_SHIFT)
#define BT_REQUEST_STATUS_VALUE(n)    (((n) & BT_REQUEST_STATUS_MASK) >> BT_REQUEST_STATUS_SHIFT)

// BIT[5:3]
#define BT_REQUEST_SCAN_SHIFT         3
#define BT_REQUEST_SCAN_MASK          (0x7 << BT_REQUEST_SCAN_SHIFT)
#define BT_REQUEST_SCAN_VALUE(n)      (((n) & BT_REQUEST_SCAN_MASK) >> BT_REQUEST_SCAN_SHIFT)

// BIT[8:6]
#define BT_REQUEST_AUDIO_SHIFT        6
#define BT_REQUEST_AUDIO_MASK         (0x7 << BT_REQUEST_AUDIO_SHIFT)
#define BT_REQUEST_AUDIO_VALUE(n)     (((n) & BT_REQUEST_AUDIO_MASK) >> BT_REQUEST_AUDIO_SHIFT)

// BIT[10:9]
#define BT_REQUEST_INQ_SHIFT          9
#define BT_REQUEST_INQ_MASK           (0x3 << BT_REQUEST_INQ_SHIFT)
#define BT_REQUEST_INQ_VALUE(n)       (((n) & BT_REQUEST_INQ_MASK) >> BT_REQUEST_INQ_SHIFT)

// BIT[12:11]
#define BT_REQUEST_LE_SCAN_SHIFT      11
#define BT_REQUEST_LE_SCAN_MASK       (0x3 << BT_REQUEST_LE_SCAN_SHIFT)
#define BT_REQUEST_LE_SCAN_VALUE(n)   (((n) & BT_REQUEST_LE_SCAN_MASK) >> BT_REQUEST_LE_SCAN_SHIFT)

// BIT[15:13]
#define BT_REQUEST_LE_ADV_SHIFT       13
#define BT_REQUEST_LE_ADV_MASK        (0x7 << BT_REQUEST_LE_ADV_SHIFT)
#define BT_REQUEST_LE_ADV_VALUE(n)    (((n) & BT_REQUEST_LE_ADV_MASK) >> BT_REQUEST_LE_ADV_SHIFT)

// BIT[17:16]
#define BT_REQUEST_LE_STATUS_SHIFT    16
#define BT_REQUEST_LE_STATUS_MASK     (0x3 << BT_REQUEST_LE_STATUS_SHIFT)
#define BT_REQUEST_LE_STATUS_VALUE(n) (((n) & BT_REQUEST_LE_STATUS_MASK) >> BT_REQUEST_LE_STATUS_SHIFT)

/* max duration for bt */
#define BT_TIME_MAX					80000

/* Durations for bt operation status */
// Durations for bt shutdown
#define BT_SHUTDOWN_TIME                    0
#define BT_SHUTDOWN_MIN_TIME                0
// Durations for bt disconnected
#define BT_DISCONNECTED_TIME                0
#define BT_DISCONNECTED_MIN_TIME            0
// Durations for bt connecting
#define BT_CONNECTING_TIME                  60000
#define BT_CONNECTING_MIN_TIME              0
// Durations for bt connected
#define BT_CONNECTED_TIME                   0
#define BT_CONNECTED_MIN_TIME               25000
// Durations for bt sniff mode
#define BT_CONNECTED_SNIFF_TIME             0
#define BT_CONNECTED_SNIFF_MIN_TIME         0

/* Durations For BT Scan Operation */
// Durations For Both scan disabled
#define BT_BOTHSCAN_DISABLE_TIME            0
#define BT_BOTHSCAN_DISABLE_MIN_TIME        0
// Durations For Both scan enabled
#define BT_BOTHSCAN_ENABLE_TIME             0
#define BT_BOTHSCAN_ENABLE_MIN_TIME         20000
// Durations For Page Scan
#define BT_PSCAN_ENABLE_TIME                0
#define BT_PSCAN_ENABLE_MIN_TIME            0
// Durations For Inquiry Scan
#define BT_ISCAN_ENABLE_TIME                0
#define BT_ISCAN_ENABLE_MIN_TIME            20000

/* Durations For Bt Audio Operation */
// Durations For No Audio
#define BT_AUDIO_NONE_TIME                  0
#define BT_AUDIO_NONE_MIN_TIME              0
// Durations For A2DP Mode
#define BT_AUDIO_A2DP_TIME                  60000
#define BT_AUDIO_A2DP_MIN_TIME              0
// Durations For SCO Mode
#define BT_AUDIO_SCO_TIME                   80000
#define BT_AUDIO_SCO_MIN_TIME               0

/* Durations For Inquiry Operation */
// Durations For Inquiry Start
#define BT_INQ_START_TIME                   20000
#define BT_INQ_START_MIN_TIME               30000
// Durations For Inquiry Stop
#define BT_INQ_STOP_TIME                    0
#define BT_INQ_STOP_MIN_TIME                0

/* Durations For BLE Scan Operation */
// Durations For BLE Scan Start
#define BT_LE_SCAN_START_TIME               0
#define BT_LE_SCAN_START_MIN_TIME           0
// Durations For BLE Scan Stop
#define BT_LE_SCAN_STOP_TIME                0
#define BT_LE_SCAN_STOP_MIN_TIME            0

/* Durations For BLE Adv Operations */
// Durations For BLE Adv Start
#define BT_LE_ADV_START_TIME                0
#define BT_LE_ADV_START_MIN_TIME            0
// Durations For BLE Adv Stop
#define BT_LE_ADV_STOP_TIME                 0
#define BT_LE_ADV_STOP_MIN_TIME             0

/* Durations For BLE Connection Operation */
// Durations For BLE Connection
#define BT_LE_CONNECTED_TIME                0
#define BT_LE_CONNECTED_MIN_TIME            0
// Durations For BLE Disconnection
#define BT_LE_DISCONNECTED_TIME             0
#define BT_LE_DISCONNECTED_MIN_TIME         0

// BT Status
enum {
	BES_BT_STATUS_SHUTDOWN          = 0,
	BES_BT_STATUS_DISCONNECTED,
	BES_BT_STATUS_CONNECTING,
	BES_BT_STATUS_CONNECTED_SNIFF,
	BES_BT_STATUS_CONNECTED,
	BES_BT_STATUS_MAX,
};

// BT Scan Status
enum {
	BES_BT_BOTHSCAN_DISABLE         = 0,
	BES_BT_BOTHSCAN_ENABLE,
	BES_BT_PSCAN_ENABLE,
	BES_BT_ISCAN_ENABLE,
	BES_BT_SCAN_MAX
};

// BT Inquiry Scan Status
enum {
	BES_BT_INQ_STOP                 = 0,
	BES_BT_INQ_START,
	BES_BT_INQ_MAX
};
// BLE Scan Status
enum {
	BES_LE_SCAN_STOP                = 0,
	BES_LE_SCAN_START,
	BES_LE_SCAN_MAX,
};

// BLE Advertisment Status
enum {
	BES_LE_ADV_STOP                 = 0,
	BES_LE_ADV_START,
	BES_LE_ADV_MAX
};

// BLE Connection Status
enum {
	BES_LE_DISCONNECTED             = 0,
	BES_LE_CONNECTED,
	BES_LE_MAX
};

// BT Operation
enum {
	BES_BT_OPER_STATUS,
	BES_BT_OPER_SCAN,
	BES_BT_OPER_AUDIO,
	BES_BT_OPER_INQ,
	BES_BT_OPER_LE_SCAN,
	BES_BT_OPER_LE_ADV,
	BES_BT_OPER_LE_STATUS,

	BES_BT_OPER_NUM,
};

struct bt_oper_info {
	u8 oper_type;
	u32 time;
	u32 min_time;
};

static struct bt_oper_info  g_coex_bt_oper[BES_BT_OPER_NUM];

void bes_coex_bt_init(void)
{
	memset(g_coex_bt_oper, 0, sizeof(g_coex_bt_oper));
	g_coex_bt_oper[BES_BT_OPER_STATUS].oper_type = BES_BT_STATUS_SHUTDOWN;
	g_coex_bt_oper[BES_BT_OPER_SCAN].oper_type = BES_BT_BOTHSCAN_DISABLE;
	g_coex_bt_oper[BES_BT_OPER_AUDIO].oper_type = BES_BT_AUDIO_NONE;
	g_coex_bt_oper[BES_BT_OPER_INQ].oper_type = BES_BT_INQ_STOP;
	g_coex_bt_oper[BES_BT_OPER_LE_SCAN].oper_type = BES_LE_SCAN_STOP;
	g_coex_bt_oper[BES_BT_OPER_LE_ADV].oper_type = BES_LE_ADV_STOP;
	g_coex_bt_oper[BES_BT_OPER_LE_STATUS].oper_type = BES_LE_DISCONNECTED;
}

static void bes_coex_bt_oper_status(u8 bt_oper_status)
{
	bes_dbg(BES_DBG_COEX, "%s type:%d", __func__, bt_oper_status);

	switch (bt_oper_status) {
	case BES_BT_STATUS_SHUTDOWN:
		bes_coex_bt_init();
		break;
	case BES_BT_STATUS_DISCONNECTED:
		g_coex_bt_oper[BES_BT_OPER_STATUS].oper_type = BES_BT_STATUS_DISCONNECTED;
		g_coex_bt_oper[BES_BT_OPER_STATUS].time = BT_DISCONNECTED_TIME;
		g_coex_bt_oper[BES_BT_OPER_STATUS].min_time = BT_DISCONNECTED_MIN_TIME;
		break;
	case BES_BT_STATUS_CONNECTING:
		g_coex_bt_oper[BES_BT_OPER_STATUS].oper_type = BES_BT_STATUS_CONNECTING;
		g_coex_bt_oper[BES_BT_OPER_STATUS].time = BT_CONNECTING_TIME;
		g_coex_bt_oper[BES_BT_OPER_STATUS].min_time = BT_CONNECTING_MIN_TIME;
		break;
	case BES_BT_STATUS_CONNECTED:
		g_coex_bt_oper[BES_BT_OPER_STATUS].oper_type = BES_BT_STATUS_CONNECTED;
		g_coex_bt_oper[BES_BT_OPER_STATUS].time = BT_CONNECTED_TIME;
		g_coex_bt_oper[BES_BT_OPER_STATUS].min_time = BT_CONNECTED_MIN_TIME;
		break;
	case BES_BT_STATUS_CONNECTED_SNIFF:
		g_coex_bt_oper[BES_BT_OPER_STATUS].oper_type = BES_BT_STATUS_CONNECTED_SNIFF;
		g_coex_bt_oper[BES_BT_OPER_STATUS].time = BT_CONNECTED_SNIFF_TIME;
		g_coex_bt_oper[BES_BT_OPER_STATUS].min_time = BT_CONNECTED_SNIFF_MIN_TIME;
		break;
	default:
		bes_err(BES_DBG_COEX, "%s type error:%d", __func__, bt_oper_status);
		break;
	}
}

static void bes_coex_bt_oper_scan(u8 bt_scan_status)
{
	bes_dbg(BES_DBG_COEX, "%s type:%d", __func__, bt_scan_status);

	switch (bt_scan_status) {
	case BES_BT_BOTHSCAN_DISABLE:
		g_coex_bt_oper[BES_BT_OPER_SCAN].oper_type = BES_BT_BOTHSCAN_DISABLE;
		g_coex_bt_oper[BES_BT_OPER_SCAN].time = BT_BOTHSCAN_DISABLE_TIME;
		g_coex_bt_oper[BES_BT_OPER_SCAN].min_time = BT_BOTHSCAN_DISABLE_MIN_TIME;
		break;
	case BES_BT_BOTHSCAN_ENABLE:
		g_coex_bt_oper[BES_BT_OPER_SCAN].oper_type = BES_BT_BOTHSCAN_ENABLE;
		g_coex_bt_oper[BES_BT_OPER_SCAN].time = BT_BOTHSCAN_ENABLE_TIME;
		g_coex_bt_oper[BES_BT_OPER_SCAN].min_time = BT_BOTHSCAN_ENABLE_MIN_TIME;
		break;
	case BES_BT_PSCAN_ENABLE:
		g_coex_bt_oper[BES_BT_OPER_SCAN].oper_type = BES_BT_PSCAN_ENABLE;
		g_coex_bt_oper[BES_BT_OPER_SCAN].time = BT_PSCAN_ENABLE_TIME;
		g_coex_bt_oper[BES_BT_OPER_SCAN].min_time = BT_PSCAN_ENABLE_MIN_TIME;
		break;
	case BES_BT_ISCAN_ENABLE:
		g_coex_bt_oper[BES_BT_OPER_SCAN].oper_type = BES_BT_ISCAN_ENABLE;
		g_coex_bt_oper[BES_BT_OPER_SCAN].time = BT_ISCAN_ENABLE_TIME;
		g_coex_bt_oper[BES_BT_OPER_SCAN].min_time = BT_ISCAN_ENABLE_MIN_TIME;
		break;
	default:
		bes_err(BES_DBG_COEX, "%s type error:%d", __func__, bt_scan_status);
		break;
	}
}

static void bes_coex_bt_oper_audio(u8 bt_audio_status)
{
	bes_dbg(BES_DBG_COEX, "%s type:%d", __func__, bt_audio_status);

	switch (bt_audio_status) {
	case BES_BT_AUDIO_NONE:
		g_coex_bt_oper[BES_BT_OPER_AUDIO].oper_type = BES_BT_AUDIO_NONE;
		g_coex_bt_oper[BES_BT_OPER_AUDIO].time = BT_AUDIO_NONE_TIME;
		g_coex_bt_oper[BES_BT_OPER_AUDIO].min_time = BT_AUDIO_NONE_MIN_TIME;
		break;
	case BES_BT_AUDIO_A2DP:
		g_coex_bt_oper[BES_BT_OPER_AUDIO].oper_type = BES_BT_AUDIO_A2DP;
		g_coex_bt_oper[BES_BT_OPER_AUDIO].time = BT_AUDIO_A2DP_TIME;
		g_coex_bt_oper[BES_BT_OPER_AUDIO].min_time = BT_AUDIO_A2DP_MIN_TIME;
		break;
	case BES_BT_AUDIO_SCO:
		g_coex_bt_oper[BES_BT_OPER_AUDIO].oper_type = BES_BT_AUDIO_SCO;
		g_coex_bt_oper[BES_BT_OPER_AUDIO].time = BT_AUDIO_SCO_TIME;
		g_coex_bt_oper[BES_BT_OPER_AUDIO].min_time = BT_AUDIO_SCO_MIN_TIME;
		break;
	default:
		bes_err(BES_DBG_COEX, "%s type error:%d", __func__, bt_audio_status);
		break;
	}
}

static void bes_coex_bt_oper_inq(u8 bt_inquiry_status)
{
	bes_dbg(BES_DBG_COEX, "%s type:%d", __func__, bt_inquiry_status);

	switch (bt_inquiry_status) {
	case BES_BT_INQ_START:
		g_coex_bt_oper[BES_BT_OPER_INQ].oper_type = BES_BT_INQ_START;
		g_coex_bt_oper[BES_BT_OPER_INQ].time = BT_INQ_START_TIME;
		g_coex_bt_oper[BES_BT_OPER_INQ].min_time = BT_INQ_START_MIN_TIME;
		break;
	case BES_BT_INQ_STOP:
		g_coex_bt_oper[BES_BT_OPER_INQ].oper_type = BES_BT_INQ_STOP;
		g_coex_bt_oper[BES_BT_OPER_INQ].time = BT_INQ_STOP_TIME;
		g_coex_bt_oper[BES_BT_OPER_INQ].min_time = BT_INQ_STOP_MIN_TIME;
		break;
	default:
		bes_err(BES_DBG_COEX, "%s type error:%d", __func__, bt_inquiry_status);
		break;
	}
}

static void bes_coex_bt_oper_le_scan(u8 ble_scan_status)
{
	bes_dbg(BES_DBG_COEX, "%s type:%d", __func__, ble_scan_status);

	switch (ble_scan_status) {
	case BES_LE_SCAN_START:
		g_coex_bt_oper[BES_BT_OPER_LE_SCAN].oper_type = BES_LE_SCAN_START;
		g_coex_bt_oper[BES_BT_OPER_LE_SCAN].time = BT_LE_SCAN_START_TIME;
		g_coex_bt_oper[BES_BT_OPER_LE_SCAN].min_time = BT_LE_SCAN_START_MIN_TIME;
		break;
	case BES_LE_SCAN_STOP:
		g_coex_bt_oper[BES_BT_OPER_LE_SCAN].oper_type = BES_LE_SCAN_STOP;
		g_coex_bt_oper[BES_BT_OPER_LE_SCAN].time = BT_LE_SCAN_STOP_TIME;
		g_coex_bt_oper[BES_BT_OPER_LE_SCAN].min_time = BT_LE_SCAN_STOP_MIN_TIME;
		break;
	default:
		bes_err(BES_DBG_COEX, "%s type error:%d", __func__, ble_scan_status);
		break;
	}
}

static void bes_coex_bt_oper_le_adv(u8 ble_adv_status)
{
	bes_dbg(BES_DBG_COEX, "%s type:%d", __func__, ble_adv_status);

	switch (ble_adv_status) {
	case BES_LE_ADV_START:
		g_coex_bt_oper[BES_BT_OPER_LE_ADV].oper_type = BES_LE_ADV_START;
		g_coex_bt_oper[BES_BT_OPER_LE_ADV].time = BT_LE_ADV_START_TIME;
		g_coex_bt_oper[BES_BT_OPER_LE_ADV].min_time = BT_LE_ADV_START_MIN_TIME;
		break;
	case BES_LE_ADV_STOP:
		g_coex_bt_oper[BES_BT_OPER_LE_ADV].oper_type = BES_LE_ADV_STOP;
		g_coex_bt_oper[BES_BT_OPER_LE_ADV].time = BT_LE_ADV_STOP_TIME;
		g_coex_bt_oper[BES_BT_OPER_LE_ADV].min_time = BT_LE_ADV_STOP_MIN_TIME;
		break;
	default:
		bes_err(BES_DBG_COEX, "%s type error:%d", __func__, ble_adv_status);
		break;
	}
}

static void bes_coex_bt_oper_le_status(u8 ble_conn_status)
{
	bes_dbg(BES_DBG_COEX, "%s type:%d", __func__, ble_conn_status);

	switch (ble_conn_status) {
	case BES_LE_CONNECTED:
		g_coex_bt_oper[BES_BT_OPER_LE_STATUS].oper_type = BES_LE_CONNECTED;
		g_coex_bt_oper[BES_BT_OPER_LE_STATUS].time = BT_LE_CONNECTED_TIME;
		g_coex_bt_oper[BES_BT_OPER_LE_STATUS].min_time = BT_LE_CONNECTED_MIN_TIME;
		break;
	case BES_LE_DISCONNECTED:
		g_coex_bt_oper[BES_BT_OPER_LE_STATUS].oper_type = BES_LE_DISCONNECTED;
		g_coex_bt_oper[BES_BT_OPER_LE_STATUS].time = BT_LE_DISCONNECTED_TIME;
		g_coex_bt_oper[BES_BT_OPER_LE_STATUS].min_time = BT_LE_DISCONNECTED_MIN_TIME;
		break;
	default:
		bes_err(BES_DBG_COEX, "%s type error:%d", __func__, ble_conn_status);
		break;
	}
}

static void bes_coex_bt_status_update(u32 bt_status)
{
	bes_coex_bt_oper_scan(BT_REQUEST_SCAN_VALUE(bt_status));
	bes_coex_bt_oper_audio(BT_REQUEST_AUDIO_VALUE(bt_status));
	bes_coex_bt_oper_inq(BT_REQUEST_INQ_VALUE(bt_status));
	bes_coex_bt_oper_le_scan(BT_REQUEST_LE_SCAN_VALUE(bt_status));
	bes_coex_bt_oper_le_adv(BT_REQUEST_LE_ADV_VALUE(bt_status));
	bes_coex_bt_oper_le_status(BT_REQUEST_LE_STATUS_VALUE(bt_status));
	bes_coex_bt_oper_status(BT_REQUEST_STATUS_VALUE(bt_status));
}

int bes_coex_bt_state_info(char *buf, int maxlen, u32 bt_status)
{
	const char *bt_opr_str[] = {"Off", "Break", "Linking", "Sniff", "Link"};
	const char *bt_scan_str[] = {"Off", "Both", "PScan", "IScan"};
	const char *bt_inquiry_str[] = {"Off", "On"};
	const char *ble_scan_str[] = {"Off", "On"};
	const char *ble_adv_str[] = {"Off", "On"};
	const char *ble_link_str[] = {"Break", "Link"};
	const char *bt_audio_str[] = {"None", "A2DP", "SCO"};
	u32 bt_scan_state, bt_audio_state, bt_inq_state;
	u32 ble_scan_state, ble_adv_state, ble_link_state, bt_opr_state;
	int len = 0;

	/* check if parameters are valid */
	if (!buf || maxlen - 1 <= 0)
		goto out;

	/* output bt status information */
	bt_scan_state = BT_REQUEST_SCAN_VALUE(bt_status);
	bt_audio_state = BT_REQUEST_AUDIO_VALUE(bt_status);
	bt_inq_state = BT_REQUEST_INQ_VALUE(bt_status);
	bt_opr_state = BT_REQUEST_STATUS_VALUE(bt_status);
	len += scnprintf(&buf[len], maxlen - 1, "bt scan:%s audio:%s inq:%s oper:%s\n",
			 (bt_scan_state < BES_BT_SCAN_MAX) ? bt_scan_str[bt_scan_state] : "Unknow",
			 (bt_audio_state < BES_BT_AUDIO_MAX) ? bt_audio_str[bt_audio_state] : "Unknow",
			 (bt_inq_state < BES_BT_INQ_MAX) ? bt_inquiry_str[bt_inq_state] : "Unknow",
			 (bt_opr_state < BES_BT_STATUS_MAX) ? bt_opr_str[bt_opr_state] : "Unknow");
	if (len >= maxlen - 1)
		goto out;

	/* output ble status information */
	ble_scan_state = BT_REQUEST_LE_SCAN_VALUE(bt_status);
	ble_adv_state = BT_REQUEST_LE_ADV_VALUE(bt_status);
	ble_link_state = BT_REQUEST_LE_STATUS_VALUE(bt_status);
	len += scnprintf(&buf[len], maxlen - 1 - len, "ble scan:%s adv:%s link:%s\n",
			 (ble_scan_state < BES_LE_SCAN_MAX) ? ble_scan_str[ble_scan_state] : "Unknow",
			 (ble_adv_state < BES_LE_ADV_MAX) ? ble_adv_str[ble_adv_state] : "Unknow",
			 (ble_link_state < BES_LE_MAX) ? ble_link_str[ble_link_state] : "Unknow");

out:
	return len;
}

static void bes_coex_bt_state_show(u32 bt_status)
{
	char *buf= NULL;

	buf = (char *)kmalloc(BES_COEX_BT_STATE_DUMP_BUFSIZE, GFP_ATOMIC);
	if (!buf) {
		bes_err(BES_DBG_COEX, "%s Fail to alloc memory\n", __func__);
		return;
	}

	if (bes_coex_bt_state_info(buf, BES_COEX_BT_STATE_DUMP_BUFSIZE, bt_status) > 1)
		bes_dbg(BES_DBG_COEX, "%s", buf);

	kfree(buf);
}

u32 bes_coex_bt_calc_time(void)
{
	u32 i;
	u32 time = 0, min_time = 0;

	for (i = 0; i < BES_BT_OPER_NUM; ++i) {
		time += g_coex_bt_oper[i].time;
		if (min_time < g_coex_bt_oper[i].min_time)
			min_time = g_coex_bt_oper[i].min_time;
	}

	bes_dbg(BES_DBG_COEX, "%s time:%u, min_time:%u", __func__, time, min_time);
	time = time < min_time ? min_time : time;
	return time < BT_TIME_MAX ? time : BT_TIME_MAX;
}

int bes_coex_bt_status_ind(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct ipc_e2a_msg *msg)
{
	u32 bt_state = msg->param[0];

	bes_coex_bt_status_update(bt_state);
	bes_coex_bt_state_show(bt_state);
	bes_coex_arb_set_bt_state(rwnx_hw, bt_state, 0);

	return 0;
}

void bes_coex_bt_sync_status(struct rwnx_hw *rwnx_hw)
{
	u32 bt_state = 0;


	bes_dbg(BES_DBG_COEX, "get bt status actively\n");
	bes_send_bt_status_get(rwnx_hw, &bt_state);
	bes_coex_bt_status_update(bt_state);
	bes_coex_bt_state_show(bt_state);

	bes_coex_arb_set_bt_state(rwnx_hw, bt_state, 0);
}

u32 bes_coex_bt_get_audio_state(struct rwnx_hw *rwnx_hw)
{
	return BT_REQUEST_AUDIO_VALUE(rwnx_hw->coex.bt_coex_state);
}

bool bes_coex_bt_closed(struct rwnx_hw *rwnx_hw)
{
	return (BT_REQUEST_STATUS_VALUE(rwnx_hw->coex.bt_coex_state) == BES_BT_STATUS_SHUTDOWN);
}

bool bes_coex_bt_audio_state_change(u32 old_state, u32 new_state)
{
	return (BT_REQUEST_AUDIO_VALUE(old_state) != BT_REQUEST_AUDIO_VALUE(new_state));
}

bool bes_coex_bt_state_change(u32 old_state, u32 new_state)
{
	u32 old_bt_state, new_bt_state;

	old_bt_state = BT_REQUEST_STATUS_VALUE(old_state);
	new_bt_state = BT_REQUEST_STATUS_VALUE(new_state);

	return ((old_bt_state != new_bt_state) &&
	        (old_bt_state == BES_BT_STATUS_SHUTDOWN ||
	         new_bt_state == BES_BT_STATUS_SHUTDOWN));
}
