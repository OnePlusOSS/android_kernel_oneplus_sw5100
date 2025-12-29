/*
 * @file bes_tx_loop.c
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "rwnx_main.h"
#include "bes_tx_loop.h"
#include "rwnx_msg_tx.h"
#include "rwnx_strs.h"
#include "bes_sysfs.h"
#include "bes_hwio.h"

#define BES_SEND_BY_HW_PATH 0
#define BES_SEND_BY_TX_LOOP 1
#define BES_NOT_SEND_DATA 2

#define BES_LMAC_TX_TIMEOUT 5 * HZ

extern void bes_io_bus_status_dump(void);

static void bes_tx_loop_force_close_work(struct work_struct *work);
void bes_tx_loop_build_dev_status_ind(struct rwnx_plat *rwnx_plat, u32 wait_dev_status, bool target_val);
static void bes_tx_loop_send_msg(struct rwnx_plat *rwnx_plat);
static void bes_tx_loop_build_ind(struct rwnx_plat *rwnx_plat, struct rwnx_cmd_a2emsg *record_a2e_msg);
void bes_tx_loop_clear_ind(struct rwnx_plat *rwnx_plat, u16 msg_id);

static struct tx_loop_ind_timeout ind_timeout_tbl[] = {
	{.msg_req = SCANU_START_REQ,		.max_timeout = 10 * HZ, .end_time = 0, .record_a2emsg = NULL},
	{.msg_req = MM_REMAIN_ON_CHANNEL_REQ,	.max_timeout = 1 * HZ,  .end_time = 0, .record_a2emsg = NULL},
	{.msg_req = TWT_SETUP_REQ,		.max_timeout = 5 * HZ,  .end_time = 0, .record_a2emsg = NULL},
	{.msg_req = SM_CONNECT_REQ,		.max_timeout = 5 * HZ,  .end_time = 0, .record_a2emsg = NULL},
	{.msg_req = SM_DISCONNECT_REQ,		.max_timeout = 4 * HZ,  .end_time = 0, .record_a2emsg = NULL},
	{.msg_req = APM_PROBE_CLIENT_REQ,	.max_timeout = 4 * HZ,  .end_time = 0, .record_a2emsg = NULL},
	{.msg_req = TDLS_CHAN_SWITCH_REQ,	.max_timeout = 1 * HZ,  .end_time = 0, .record_a2emsg = NULL},
};

/*
* This timer is triggered when no indication is received within a certain
* period of time after somemsg that requires an indication reply is sent
*/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,15,0)
void bes_ind_timeout_monitor(struct timer_list* timer)
#else
void bes_ind_timeout_monitor(unsigned long data)
#endif
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();

	/* don't need to tx loop if wifi is unregistered */
	if (rwnx_plat == NULL)
		return;

	/* don't need to do if tx loop is already triggered */
	if (bes_tx_loop_is_enabled(rwnx_plat))
		return;

	bes_err(BES_DBG_TXLOOP, "link break between bes chip and host, msg:%s ind timeout\n",
		RWNX_ID2STR(rwnx_plat->tx_loop.ind_timeout_msg));
	bes_tx_loop_force_close(rwnx_plat, true);
}

/*
* This timer is triggered when no correspondingcfm is received within
* a certain period of time after the data skb is sent
*/
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,15,0)
void bes_lmac_active_monitor(struct timer_list* t)
#else
void bes_lmac_active_monitor(unsigned long data)
#endif
{
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();

	/* don't need to tx loop if wifi is unregistered */
	if (rwnx_plat == NULL)
		return;

	/* don't need to do if tx loop is already triggered */
	if (bes_tx_loop_is_enabled(rwnx_plat))
		return;

	bes_err(BES_DBG_TXLOOP, "link break between LMAC and host, lmac_tx_pending:%d\n",
				atomic_read(&rwnx_plat->tx_loop.lmac_tx_pending));
	bes_tx_loop_force_close(rwnx_plat, true);
}

/*
* When data is sent, use this function to increase lmac_tx_pending
* and start lmac_mon_timer
*/
int bes_tx_loop_inc_pending_data_count(struct rwnx_plat *rwnx_plat)
{
	struct timer_list *timer = &rwnx_plat->tx_loop.lmac_mon_timer;

	/* don't need to do if tx loop is already triggered */
	if (bes_tx_loop_is_enabled(rwnx_plat))
		return BES_NOT_SEND_DATA;

	bes_dbg(BES_DBG_TXLOOP, "%s, lmac_tx_pending: %d\n",
		 __func__, atomic_read(&rwnx_plat->tx_loop.lmac_tx_pending));

	/* When lmac_tx_pending is equal to 0, start lmac_mon_timer */
	if (atomic_read(&rwnx_plat->tx_loop.lmac_tx_pending) == 0) {
		bes_dbg(BES_DBG_TXLOOP, "start lmac_mon_timer\n");
		mod_timer(timer, jiffies + BES_LMAC_TX_TIMEOUT);
	}
	atomic_inc(&rwnx_plat->tx_loop.lmac_tx_pending);
	return BES_SEND_BY_HW_PATH;
}

/*
* When data cfm is received, use this function to decline
* lmac_tx_pending and close lmac_mon_timer
*/
void bes_tx_loop_dec_pending_data_count(struct rwnx_plat *rwnx_plat, int dec_num)
{
	struct timer_list *timer = &rwnx_plat->tx_loop.lmac_mon_timer;

	/* don't need to tx loop if wifi is unregistered */
	if (rwnx_plat == NULL)
		return;

	/* don't need to do if tx loop is already triggered */
	if (bes_tx_loop_is_enabled(rwnx_plat))
		return;

	bes_dbg(BES_DBG_TXLOOP, "%s, lmac_tx_pending: %d, dec_num: %d\n",
		 __func__, atomic_read(&rwnx_plat->tx_loop.lmac_tx_pending), dec_num);

	if (atomic_read(&rwnx_plat->tx_loop.lmac_tx_pending) == 0) {
		bes_err(BES_DBG_TXLOOP, "tx data pending count error\n");
		return;
	}

	if (atomic_sub_and_test(dec_num, &rwnx_plat->tx_loop.lmac_tx_pending)) {
		/* Disable lmac_mon_timer when lmac_tx_pending subtracted dec_num equals 0 */
		bes_dbg(BES_DBG_TXLOOP, "stop lmac_mon_timer\n");
		del_timer_sync(timer);
	} else {
		/* Restart lmac_mon_timer when lmac_tx_pending subtracted dec_num not equals 0 */
		bes_dbg(BES_DBG_TXLOOP, "start lmac_mon_timer again\n");
		mod_timer(timer, jiffies + BES_LMAC_TX_TIMEOUT);
	}
}

static void bes_tx_loop_build_timeout_ind(struct rwnx_plat *rwnx_plat)
{
	int tbl_inx = 0;
	int tbl_size = ARRAY_SIZE(ind_timeout_tbl);

	/* Return the indication required when ind_timeout_tbl contains a member whose end_time is not 0 */
	spin_lock_bh(&rwnx_plat->tx_loop.wait_ind_lock);
	for(tbl_inx = 0; tbl_inx < tbl_size; tbl_inx++) {
		if (ind_timeout_tbl[tbl_inx].end_time != 0 && ind_timeout_tbl[tbl_inx].record_a2emsg != NULL) {
			bes_dbg(BES_DBG_TXLOOP, "%s, build timeout ind for msg: %s\n",
				__func__, RWNX_ID2STR(ind_timeout_tbl[tbl_inx].msg_req));
			bes_tx_loop_build_ind(rwnx_plat, ind_timeout_tbl[tbl_inx].record_a2emsg);

			/* After constructing the corresponding indication,
			clear the corresponding msg from ind_timeout_tbl */
			ind_timeout_tbl[tbl_inx].end_time = 0;
			kfree(ind_timeout_tbl[tbl_inx].record_a2emsg);
			ind_timeout_tbl[tbl_inx].record_a2emsg = NULL;
		}
	}
	spin_unlock_bh(&rwnx_plat->tx_loop.wait_ind_lock);
}

bool bes_tx_loop_is_enabled(struct rwnx_plat *rwnx_plat)
{
	return rwnx_plat->tx_loop.enabled;
}

void bes_tx_loop_set_dev_disconnect(struct rwnx_plat *rwnx_plat)
{
	rwnx_plat->tx_loop.dev_disconnect = true;
}

static void bes_tx_loop_timer_init(struct rwnx_plat *rwnx_plat)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4,10,0))
	timer_setup(&rwnx_plat->tx_loop.lmac_mon_timer, bes_lmac_active_monitor, 0);
	timer_setup(&rwnx_plat->tx_loop.wait_ind_timer, bes_ind_timeout_monitor, 0);
#else
	setup_timer(&rwnx_plat->tx_loop.lmac_mon_timer, bes_lmac_active_monitor,(unsigned long)(rwnx_plat->tx_loop));
	setup_timer(&rwnx_plat->tx_loop.wait_ind_timer, bes_ind_timeout_monitor,(unsigned long)(rwnx_plat->tx_loop));
#endif
}

void bes_tx_loop_deinit(struct rwnx_plat *rwnx_plat)
{
	del_timer_sync(&rwnx_plat->tx_loop.lmac_mon_timer);
	del_timer_sync(&rwnx_plat->tx_loop.wait_ind_timer);
}

/*
* When re-executing rwnx_register_net_dev, need
* to initialize the necessary variables
*/
void bes_tx_loop_status_clear(struct rwnx_plat *rwnx_plat)
{
	rwnx_plat->tx_loop.enabled = 0;
	rwnx_plat->tx_loop.dev_disconnect = false;
	rwnx_plat->tx_loop.wait_dev_status.dev_status = 0;
	rwnx_plat->tx_loop.wait_dev_status.target_val = false;
	rwnx_plat->tx_loop.ind_timeout_msg = 0;
	atomic_set(&rwnx_plat->tx_loop.lmac_tx_pending, 0);
}

void bes_tx_loop_init(struct rwnx_plat *rwnx_plat)
{
	bes_tx_loop_status_clear(rwnx_plat);
	skb_queue_head_init(&rwnx_plat->tx_loop.rx_queue);
	bes_tx_loop_timer_init(rwnx_plat);
	spin_lock_init(&rwnx_plat->tx_loop.wait_ind_lock);
	INIT_WORK(&rwnx_plat->tx_loop.tx_loop_close_work, bes_tx_loop_force_close_work);
}

struct sk_buff *bes_tx_loop_get_skb(struct rwnx_plat *rwnx_plat)
{
	struct sk_buff *skb;

	if (rwnx_plat == NULL)
		return NULL;

	skb = skb_dequeue(&rwnx_plat->tx_loop.rx_queue);

	return skb;
}

static void bes_tx_loop_notify(void)
{
#ifdef CONFIG_BES_MDEV_SUPPORT
	bes_mdev_unregister();
#else
	char wifi_state[15];
	char bt_state[15];
	char sub_event[] = BES_UEVET_ACTION_PREFIX BES_UEVET_RECOVER;
	struct bes_power_state power_state;
	char *env[] = { sub_event, wifi_state, bt_state, NULL};
	struct bes_sysfs *bes_sysfs = bes_get_sysfs();

	/* When the tx_loop is triggered, notify userspace */
	bes_power_ctrl_get_power_state(&power_state);
	snprintf(wifi_state, sizeof(wifi_state), "WIFI_OPENED=%d", power_state.wifi_power);
	snprintf(bt_state, sizeof(bt_state), "BT_OPENED=%d", power_state.bt_power);
	kobject_uevent_env(&bes_sysfs->dev->kobj, KOBJ_CHANGE, env);
#endif
}

static void bes_tx_loop_auto_wifi_off(struct rwnx_plat *rwnx_plat)
{
#ifdef CONFIG_BES_POWER_CTRL
	struct bes_power_ctrl *bes_power_ctrl = priv_to_power_ctrl(rwnx_plat->power_priv);
	bes_power_ctrl_wifi_power(bes_power_ctrl, false);
#else
	rwnx_unregister_net_dev(rwnx_plat);
#endif
}

static void bes_tx_loop_force_close_work(struct work_struct *work)
{
	struct bes_tx_loop *bes_tx_loop = container_of(work, struct bes_tx_loop, tx_loop_close_work);
	struct rwnx_plat *rwnx_plat = container_of(bes_tx_loop, struct rwnx_plat, tx_loop);
	bool auto_wifi_off = !!CONFIG_BES_TXLOOP_AUTO_WIFI_OFF;

	bes_info(BES_DBG_TXLOOP,  "%s, %d.\n", __func__, __LINE__);

	/* When dev_disconnect is true, the USB has already performed
	the unregister_net_dev operation */
	if (!rwnx_plat->tx_loop.dev_disconnect) {
		bes_info(BES_DBG_TXLOOP,  "%s, %d auto _wifi_off: %d .\n", __func__, __LINE__, auto_wifi_off);
		/* switch on/off wifi based on user configuration */
		if (auto_wifi_off) {
			bes_tx_loop_auto_wifi_off(rwnx_plat);
		}

		/* Notify user wifi crashed */
		bes_tx_loop_notify();
	}
}

static void bes_tx_loop_set_enable(struct rwnx_plat *rwnx_plat, bool need_warn)
{
	u32 record_dev_status;
	bool record_target_val;

	if (rwnx_plat == NULL)
		return;

	WARN_ON(need_warn);

	record_dev_status = rwnx_plat->tx_loop.wait_dev_status.dev_status;
	record_target_val = rwnx_plat->tx_loop.wait_dev_status.target_val;

	/* When record_dev_status is not 0, the required indication needs to be returned */
	if (record_dev_status != 0) {
		bes_tx_loop_build_dev_status_ind(rwnx_plat, record_dev_status, record_target_val);
	}

	/* When there is an uncleared cmd in the cmd_mgr list, construct the
	 corresponding skb to reply to the cmd */
	bes_tx_loop_send_msg(rwnx_plat);

	/* If ind_timeout_tbl still has no reply ind,
	the corresponding msg ind needs to be replied */
	bes_tx_loop_build_timeout_ind(rwnx_plat);
}

void bes_tx_loop_force_close(struct rwnx_plat *rwnx_plat, bool need_warn)
{
	struct rwnx_hw *rwnx_hw;

	if (rwnx_plat == NULL || rwnx_plat->drvdata == NULL)
		return;

	rwnx_hw = (struct rwnx_hw *)rwnx_plat->drvdata;

	/* don't need to do if tx loop is already triggered */
	if (test_and_set_bit(0, &rwnx_plat->tx_loop.enabled))
		return;
	/* dump io bus status */
	bes_io_bus_status_dump();
	/* When the tx_loop mechanism is enabled or tx_loop_close_work is in pending state,
	no further operation is performed */
	if (!work_pending(&rwnx_plat->tx_loop.tx_loop_close_work)) {
		bes_tx_loop_set_enable(rwnx_plat, need_warn);
		/* Trigger tx loop close when system exception with wifi enabled */
		if (bes_power_ctrl_recovery_trigger()) {
			bes_info(BES_DBG_TXLOOP, "%s. trigger tx_loop_close_work\n", __func__);
			schedule_work(&rwnx_plat->tx_loop.tx_loop_close_work);
		}
	}
}

void bes_tx_loop_record_dev_status(struct rwnx_plat *rwnx_plat, u32 rd_status, bool target_val)
{
	if (!bes_tx_loop_is_enabled(rwnx_plat)) {
		/* When the tx_loop mechanism is not enabled, the dev_status
		that needs to be waited is normally recorded */
		bes_dbg(BES_DBG_TXLOOP, "%s. record dev_status: %d, target_val: %d\n",
		 	__func__, rd_status, target_val);
		rwnx_plat->tx_loop.wait_dev_status.dev_status = rd_status;
		rwnx_plat->tx_loop.wait_dev_status.target_val = target_val;
	} else {
		/* When the tx_loop mechanism is enabled, build the indication for the recorded dev_status*/
		bes_dbg(BES_DBG_TXLOOP, "%s. build msg for dev_status: %d, target_val: %d\n",
		 	__func__, rd_status, target_val);
		bes_tx_loop_build_dev_status_ind(rwnx_plat, rd_status, target_val);
	}
}

void bes_tx_loop_clear_dev_status(struct rwnx_plat *rwnx_plat)
{
	/* Clear the returned dev_status variables */
	bes_dbg(BES_DBG_TXLOOP, "%s. clear dev_status: %d, target_val: %d\n",
		 __func__, rwnx_plat->tx_loop.wait_dev_status.dev_status, rwnx_plat->tx_loop.wait_dev_status.target_val);
	rwnx_plat->tx_loop.wait_dev_status.dev_status = 0;
	rwnx_plat->tx_loop.wait_dev_status.target_val = false;

}

void bes_tx_loop_wakeup_rx_process(struct rwnx_plat *rwnx_plat)
{
#if defined CONFIG_BES_MSPI
	bes_info(BES_DBG_TXLOOP, "%s\n", __func__);
	atomic_inc(&rwnx_plat->wifi_rx);
	queue_work(rwnx_plat->bus_rx_wq, &rwnx_plat->bus_rx_work);
#elif defined CONFIG_BES_SDIO
	atomic_inc(&rwnx_plat->wifi_rx);
	wake_up(&rwnx_plat->wifi_rx_wait_q);
#elif defined CONFIG_BES_USB
	struct sk_buff *skb = NULL;

	while (!skb_queue_empty(&rwnx_plat->tx_loop.rx_queue)) {
		skb = bes_tx_loop_get_skb(rwnx_plat);
		if (skb)
			bes_usb_rx_complete(rwnx_plat, skb);
		else
			break;
	}
#endif
}

/*
* The generated packet is add to the tx loop rx queue
*/
static void bes_tx_loop_skb_queue_tail(struct rwnx_plat *rwnx_plat, struct sk_buff *rx_skb)
{
	/* Put the generated skb into the rx_queue and wake up the rx_thread */
	skb_queue_tail(&rwnx_plat->tx_loop.rx_queue, rx_skb);
}

/*
* Construct the required dev_status skb
*/
void bes_tx_loop_build_dev_status_ind(struct rwnx_plat *rwnx_plat, u32 wait_dev_status, bool target_val)
{
	struct sk_buff *ind_skb;
	struct dev_status_t *dev_status;
	u32 msg_len = sizeof(struct dev_status_t);

	bes_dbg(BES_DBG_TXLOOP, "%s, build mag for wait_dev_status: %d, target_val: %d\n", __func__, wait_dev_status, target_val);

	/* don't need to do if tx loop is already triggered */
	if (!bes_tx_loop_is_enabled(rwnx_plat)) {
		return;
	}

	if (wait_dev_status == BES_SLAVE_STATUS_MCU_READY && target_val == true)
		return;

	ind_skb = bes_dev_alloc_skb(msg_len);
	if (WARN_ON(!ind_skb))
		return;

	skb_put(ind_skb, msg_len);
	dev_status = (struct dev_status_t *)ind_skb->data;
#ifndef CONFIG_RWNX_PCIE
	dev_status->header.type = EVENT_E2A_DEV_READY_IND;
	dev_status->header.size = msg_len;
#else
	dev_status->id = MCU_DEV_STATE_IND;
	dev_status->dest_id = DRV_TASK_ID;
	dev_status->src_id = TASK_MCU;
	dev_status->param_len = sizeof(u32);
#endif
	dev_status->stat = 0;

	if (target_val)
		dev_status->stat = wait_dev_status + BES_SLAVE_STATUS_MCU_READY;
	else
		dev_status->stat = BES_SLAVE_STATUS_MCU_READY;

	bes_tx_loop_skb_queue_tail(rwnx_plat, ind_skb);
}

/*
* Construct the required ack skb
*/
static void bes_tx_loop_build_generic_msg_ack(struct rwnx_plat *rwnx_plat)
{
	struct sk_buff *ack_skb;
	struct ipc_header *pHdr;
	struct ipc_e2a_msg *msg;
	u32 msg_len = sizeof(struct ipc_header) + sizeof(struct ipc_e2a_msg);

	ack_skb = bes_dev_alloc_skb(msg_len);
	if (WARN_ON(!ack_skb))
		return;

	skb_put(ack_skb, msg_len);
	pHdr = (struct ipc_header *)ack_skb->data;
	pHdr->type = EVENT_E2A_MSG_ACK;
	pHdr->size = msg_len;

	msg = (struct ipc_e2a_msg *)(ack_skb->data + 4);
	memset(msg, 0, sizeof(struct ipc_e2a_msg));

	bes_tx_loop_skb_queue_tail(rwnx_plat, ack_skb);
}

/*
* Construct the required cfm skb
*/
static void bes_tx_loop_build_generic_msg_cfm(struct rwnx_plat *rwnx_plat, struct rwnx_cmd *cmd)
{
	struct sk_buff *cfm_skb;
	struct ipc_header *pHdr;
	struct ipc_e2a_msg *msg;
	u32 msg_len = sizeof(struct ipc_header) + sizeof(struct ipc_e2a_msg);

	cfm_skb = bes_dev_alloc_skb(msg_len);
	if (WARN_ON(!cfm_skb))
		return;

	skb_put(cfm_skb, msg_len);
	pHdr = (struct ipc_header *)cfm_skb->data;
	pHdr->type = EVENT_E2A_MSG;
	pHdr->size = msg_len;

	msg = (struct ipc_e2a_msg *)(cfm_skb->data + 4);
	memset(msg, 0, sizeof(struct ipc_e2a_msg));

	msg->id = cmd->reqid;
	msg->dummy_src_id = cmd->a2e_msg->dest_id;
	msg->dummy_dest_id = cmd->a2e_msg->src_id;
	msg->param_len = 0;

	bes_tx_loop_skb_queue_tail(rwnx_plat, cfm_skb);
}

/*
* Construct the required SCANU_START_REQ cfm skb
*/
static void bes_build_scanu_req_cfm(struct rwnx_plat *rwnx_plat, struct rwnx_cmd_a2emsg *record_a2e_msg)
{
	struct sk_buff *cfm_skb;
	struct ipc_header *pHdr;
	struct ipc_e2a_msg *msg;
	u32 msg_len = sizeof(struct ipc_header) + sizeof(struct ipc_e2a_msg);

	cfm_skb = bes_dev_alloc_skb(msg_len);
	if (WARN_ON(!cfm_skb))
		return;

	skb_put(cfm_skb, msg_len);
	pHdr = (struct ipc_header *)cfm_skb->data;
	pHdr->type = EVENT_E2A_MSG;
	pHdr->size = msg_len;

	msg = (struct ipc_e2a_msg *)(cfm_skb->data + 4);
	memset(msg, 0, sizeof(struct ipc_e2a_msg));

	msg->id = SCANU_START_CFM;
	msg->dummy_src_id = record_a2e_msg->dest_id;
	msg->dummy_dest_id = record_a2e_msg->src_id;
	msg->param_len = 0;

	bes_tx_loop_skb_queue_tail(rwnx_plat, cfm_skb);
}

/*
* Construct the required MM_REMAIN_ON_CHANNEL_REQ indication skb
*/
static void bes_build_roc_ind(struct rwnx_plat *rwnx_plat, struct rwnx_cmd_a2emsg *record_a2e_msg)
{
	struct sk_buff *ind_skb;
	struct ipc_header *pHdr;
	struct ipc_e2a_msg *msg;
	u32 msg_len = sizeof(struct ipc_header) + sizeof(struct ipc_e2a_msg);

	ind_skb = bes_dev_alloc_skb(msg_len);
	if (WARN_ON(!ind_skb))
		return;

	skb_put(ind_skb, msg_len);
	pHdr = (struct ipc_header *)ind_skb->data;
	pHdr->type = EVENT_E2A_MSG;
	pHdr->size = msg_len;

	msg = (struct ipc_e2a_msg *)(ind_skb->data + 4);
	memset(msg, 0, sizeof(struct ipc_e2a_msg));

	msg->id = MM_REMAIN_ON_CHANNEL_EXP_IND;
	msg->dummy_src_id = record_a2e_msg->dest_id;
	msg->dummy_dest_id = record_a2e_msg->src_id;
	msg->param_len = 0;

	bes_tx_loop_skb_queue_tail(rwnx_plat, ind_skb);
}

static void bes_build_roc(struct rwnx_plat *rwnx_plat, struct rwnx_cmd_a2emsg *a2e_msg)
{
	struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)rwnx_plat->drvdata;

	if (rwnx_hw->roc)
		bes_build_roc_ind(rwnx_plat, a2e_msg);
}

/*
* Construct the required TWT_SETUP_REQ indication skb
*/
static void bes_build_twt_setup_ind(struct rwnx_plat *rwnx_plat, struct rwnx_cmd_a2emsg *record_a2e_msg)
{
	struct sk_buff *ind_skb;
	struct ipc_header *pHdr;
	struct ipc_e2a_msg *msg;
	struct twt_setup_ind *twt_ind;
	u32 msg_len = sizeof(struct ipc_header) + sizeof(struct ipc_e2a_msg);

	ind_skb = bes_dev_alloc_skb(msg_len);
	if (WARN_ON(!ind_skb))
		return;

	skb_put(ind_skb, msg_len);
	pHdr = (struct ipc_header *)ind_skb->data;
	pHdr->type = EVENT_E2A_MSG;
	pHdr->size = msg_len;

	msg = (struct ipc_e2a_msg *)(ind_skb->data + 4);
	memset(msg, 0, sizeof(struct ipc_e2a_msg));

	msg->id = TWT_SETUP_IND;
	msg->dummy_src_id = record_a2e_msg->dest_id;
	msg->dummy_dest_id = record_a2e_msg->src_id;

	msg->param_len = sizeof(struct twt_setup_ind);
	twt_ind = (struct twt_setup_ind *)msg->param;
	twt_ind->sta_idx = RWNX_INVALID_STA;
	twt_ind->resp_type = MAC_TWT_SETUP_REJECT;

	bes_tx_loop_skb_queue_tail(rwnx_plat, ind_skb);
}

static void bes_build_twt_setup(struct rwnx_plat *rwnx_plat, struct rwnx_cmd_a2emsg *a2e_msg)
{
	bes_build_twt_setup_ind(rwnx_plat, a2e_msg);
}

/*
* Construct the required SM_CONNECT_REQ indication skb
*/
static void bes_build_connect_ind(struct rwnx_plat *rwnx_plat, struct rwnx_cmd_a2emsg *record_a2e_msg,
				  u8 record_vif_idx)
{
	struct sk_buff *ind_skb;
	struct ipc_header *pHdr;
	struct ipc_e2a_msg *msg;
	struct sm_connect_ind *connect_ind;
	u32 msg_len = sizeof(struct ipc_header) + sizeof(struct ipc_e2a_msg);

	ind_skb = bes_dev_alloc_skb(msg_len);
	if (WARN_ON(!ind_skb))
		return;

	skb_put(ind_skb, msg_len);
	pHdr = (struct ipc_header *)ind_skb->data;
	pHdr->type = EVENT_E2A_MSG;
	pHdr->size = msg_len;

	msg = (struct ipc_e2a_msg *)(ind_skb->data + 4);
	memset(msg, 0, sizeof(struct ipc_e2a_msg));

	msg->id = SM_CONNECT_IND;
	msg->dummy_src_id = record_a2e_msg->dest_id;
	msg->dummy_dest_id = record_a2e_msg->src_id;

	msg->param_len = sizeof(struct sm_connect_ind);
	connect_ind = (struct sm_connect_ind *)msg->param;
	connect_ind->vif_idx = record_vif_idx;
	connect_ind->status_code = -1;

	bes_tx_loop_skb_queue_tail(rwnx_plat, ind_skb);
}

static void bes_build_connect(struct rwnx_plat *rwnx_plat, struct rwnx_cmd_a2emsg *a2e_msg)
{
	u8 record_vif_idx;
	struct sm_connect_req *connect_req;

	connect_req = (struct sm_connect_req *)a2e_msg->param;
	record_vif_idx = connect_req->vif_idx;

	bes_build_connect_ind(rwnx_plat, a2e_msg, record_vif_idx);
}

/*
* Construct the required SM_DISCONNECT_REQ indication skb
*/
static void bes_build_disconnect_ind(struct rwnx_plat *rwnx_plat, struct rwnx_cmd_a2emsg *record_a2e_msg,
	 u16 record_reason_code, u8 record_vif_idx)
{
	struct sk_buff *ind_skb;
	struct ipc_header *pHdr;
	struct ipc_e2a_msg *msg;
	struct sm_disconnect_ind *disconnect_ind;
	u32 msg_len = sizeof(struct ipc_header) + sizeof(struct ipc_e2a_msg);

	ind_skb = bes_dev_alloc_skb(msg_len);
	if (WARN_ON(!ind_skb))
		return;

	skb_put(ind_skb, msg_len);
	pHdr = (struct ipc_header *)ind_skb->data;
	pHdr->type = EVENT_E2A_MSG;
	pHdr->size = msg_len;

	msg = (struct ipc_e2a_msg *)(ind_skb->data + 4);
	memset(msg, 0, sizeof(struct ipc_e2a_msg));

	msg->id = SM_DISCONNECT_IND;
	msg->dummy_src_id = record_a2e_msg->dest_id;
	msg->dummy_dest_id = record_a2e_msg->src_id;

	msg->param_len = sizeof(struct sm_disconnect_ind);
	disconnect_ind = (struct sm_disconnect_ind *)msg->param;
	disconnect_ind->reason_code = record_reason_code;
	disconnect_ind->vif_idx = record_vif_idx;

	bes_tx_loop_skb_queue_tail(rwnx_plat, ind_skb);
}

static void bes_build_disconnect(struct rwnx_plat *rwnx_plat, struct rwnx_cmd_a2emsg *a2e_msg)
{
	u16 record_reason_code;
	u8 record_vif_idx;
	struct sm_disconnect_req *disconnect_req;

	disconnect_req = (struct sm_disconnect_req *)a2e_msg->param;
	record_reason_code = disconnect_req->reason_code;
	record_vif_idx = disconnect_req->vif_idx;

	bes_build_disconnect_ind(rwnx_plat, a2e_msg, record_reason_code, record_vif_idx);
}

/*
* Construct the required APM_PROBE_CLIENT_REQ indication skb
*/
static void bes_build_probe_cli_ind(struct rwnx_plat *rwnx_plat, struct rwnx_cmd_a2emsg *record_a2e_msg,
	 u8 record_vif_idx, u8 record_sta_idx)
{
	struct sk_buff *ind_skb;
	struct ipc_header *pHdr;
	struct ipc_e2a_msg *msg;
	struct apm_probe_client_ind *probe_client_ind;
	u32 msg_len = sizeof(struct ipc_header) + sizeof(struct ipc_e2a_msg);

	ind_skb = bes_dev_alloc_skb(msg_len);
	if (WARN_ON(!ind_skb))
		return;

	skb_put(ind_skb, msg_len);
	pHdr = (struct ipc_header *)ind_skb->data;
	pHdr->type = EVENT_E2A_MSG;
	pHdr->size = msg_len;

	msg = (struct ipc_e2a_msg *)(ind_skb->data + 4);
	memset(msg, 0, sizeof(struct ipc_e2a_msg));

	msg->id = APM_PROBE_CLIENT_IND;
	msg->dummy_src_id = record_a2e_msg->dest_id;
	msg->dummy_dest_id = record_a2e_msg->src_id;

	msg->param_len = sizeof(struct apm_probe_client_ind);
	probe_client_ind = (struct apm_probe_client_ind *)msg->param;
	probe_client_ind->vif_idx = record_vif_idx;
	probe_client_ind->sta_idx = record_sta_idx;

	bes_tx_loop_skb_queue_tail(rwnx_plat, ind_skb);
}

static void bes_build_probe_cli(struct rwnx_plat *rwnx_plat, struct rwnx_cmd_a2emsg *a2e_msg)
{
	u8 record_vif_idx;
	u8 record_sta_idx;
	struct apm_probe_client_req *probe_client_req;

	probe_client_req = (struct apm_probe_client_req *)a2e_msg->param;
	record_vif_idx = probe_client_req->vif_idx;
	record_sta_idx = probe_client_req->sta_idx;

	bes_build_probe_cli_ind(rwnx_plat, a2e_msg, record_vif_idx, record_sta_idx);
}

/*
* Construct the required TDLS_CHAN_SWITCH_REQ indication skb
*/
static void bes_build_chan_switch_ind(struct rwnx_plat *rwnx_plat, struct rwnx_cmd_a2emsg *record_a2e_msg)
{
	struct sk_buff *ind_skb;
	struct ipc_header *pHdr;
	struct ipc_e2a_msg *msg;
	u32 msg_len = sizeof(struct ipc_header) + sizeof(struct ipc_e2a_msg);

	ind_skb = bes_dev_alloc_skb(msg_len);
	if (WARN_ON(!ind_skb))
		return;

	skb_put(ind_skb, msg_len);
	pHdr = (struct ipc_header *)ind_skb->data;
	pHdr->type = EVENT_E2A_MSG;
	pHdr->size = msg_len;

	msg = (struct ipc_e2a_msg *)(ind_skb->data + 4);
	memset(msg, 0, sizeof(struct ipc_e2a_msg));

	msg->id = TDLS_CHAN_SWITCH_IND;
	msg->dummy_src_id = record_a2e_msg->dest_id;
	msg->dummy_dest_id = record_a2e_msg->src_id;

	bes_tx_loop_skb_queue_tail(rwnx_plat, ind_skb);
}

static void bes_build_chan_switch(struct rwnx_plat *rwnx_plat, struct rwnx_cmd_a2emsg *a2e_msg)
{
	bes_build_chan_switch_ind(rwnx_plat, a2e_msg);
}

static void bes_tx_loop_build_ind(struct rwnx_plat *rwnx_plat, struct rwnx_cmd_a2emsg *record_a2e_msg)
{
	/* SCANU_START_REQ does not wait for cfm, but tx_loop needs
	to reply the corresponding cfm to SCANU_START_REQ */
	if (record_a2e_msg->id == SCANU_START_REQ) {
		bes_build_scanu_req_cfm(rwnx_plat, record_a2e_msg);
	} else if (record_a2e_msg->id == MM_REMAIN_ON_CHANNEL_REQ) {
		bes_build_roc(rwnx_plat, record_a2e_msg);
	} else if (record_a2e_msg->id == TWT_SETUP_REQ) {
		bes_build_twt_setup(rwnx_plat, record_a2e_msg);
	} else if (record_a2e_msg->id == SM_CONNECT_REQ) {
		bes_build_connect(rwnx_plat, record_a2e_msg);
	} else if (record_a2e_msg->id == SM_DISCONNECT_REQ) {
		bes_build_disconnect(rwnx_plat, record_a2e_msg);
	} else if (record_a2e_msg->id == APM_PROBE_CLIENT_REQ) {
		bes_build_probe_cli(rwnx_plat, record_a2e_msg);
	} else if (record_a2e_msg->id == TDLS_CHAN_SWITCH_REQ) {
		bes_build_chan_switch(rwnx_plat, record_a2e_msg);
	}
}

static int64_t bes_tx_loop_min_end_time_get(struct rwnx_plat *rwnx_plat, int *timer_req_id)
{
	int tbl_inx = 0;
	unsigned long min_end_time = 0;
	int tbl_size = ARRAY_SIZE(ind_timeout_tbl);

	/* Gets smallest end_time from ind_timeout_tbl */
	for(tbl_inx = 0; tbl_inx < tbl_size; tbl_inx++) {
		if (ind_timeout_tbl[tbl_inx].end_time != 0) {
			if (min_end_time == 0 || time_after(min_end_time, ind_timeout_tbl[tbl_inx].end_time)) {
				bes_dbg(BES_DBG_TXLOOP, "%s, get min_end_time from msg: %s\n",
					 __func__, RWNX_ID2STR(ind_timeout_tbl[tbl_inx].msg_req));
				*timer_req_id = ind_timeout_tbl[tbl_inx].msg_req;
				min_end_time = ind_timeout_tbl[tbl_inx].end_time;
			}
		}
	}

	return min_end_time;
}

void bes_tx_loop_start_ind_timer(struct rwnx_plat *rwnx_plat, struct rwnx_cmd_a2emsg *a2e_msg)
{
	int tbl_inx = 0, timer_req_id = 0;
	int64_t min_end_time = 0;
	int tbl_size = ARRAY_SIZE(ind_timeout_tbl);

	/* don't need to tx loop if wifi is unregistered */
	if (rwnx_plat == NULL)
		return;

	/* don't need to tx loop if tx loop is already triggered */
	if (bes_tx_loop_is_enabled(rwnx_plat))
		return;

	/* start the wait_ind_timer for a2e_msg */
	spin_lock_bh(&rwnx_plat->tx_loop.wait_ind_lock);
	for(tbl_inx = 0; tbl_inx < tbl_size; tbl_inx++) {
		if (ind_timeout_tbl[tbl_inx].msg_req == a2e_msg->id) {
			if (unlikely(ind_timeout_tbl[tbl_inx].record_a2emsg != NULL)) {
				/* handle MM_REMAIN_ON_CHANNEL_REQ */
				bes_dbg(BES_DBG_TXLOOP, "un-ack monitor msg: %s\n", RWNX_ID2STR(a2e_msg->id));
				ind_timeout_tbl[tbl_inx].end_time = 0;
				kfree(ind_timeout_tbl[tbl_inx].record_a2emsg);
				ind_timeout_tbl[tbl_inx].record_a2emsg = NULL;
			}

			bes_dbg(BES_DBG_TXLOOP, "start wait_ind_timer for msg: %s, tbl_inx: %d\n", RWNX_ID2STR(a2e_msg->id), tbl_inx);
			ind_timeout_tbl[tbl_inx].end_time = jiffies + ind_timeout_tbl[tbl_inx].max_timeout;
			ind_timeout_tbl[tbl_inx].record_a2emsg = kmemdup(a2e_msg, sizeof(struct rwnx_cmd_a2emsg) + a2e_msg->param_len, GFP_ATOMIC);

			/* After recording the msg information in ind_timeout_tbl,
			select the minimum timeout period to start the timer */
			min_end_time = bes_tx_loop_min_end_time_get(rwnx_plat, &timer_req_id);
			rwnx_plat->tx_loop.ind_timeout_msg = timer_req_id;
			mod_timer(&rwnx_plat->tx_loop.wait_ind_timer, min_end_time);

			spin_unlock_bh(&rwnx_plat->tx_loop.wait_ind_lock);
			return;
		}
	}
	spin_unlock_bh(&rwnx_plat->tx_loop.wait_ind_lock);
}

void bes_tx_loop_stop_ind_timer(struct rwnx_plat *rwnx_plat, struct ipc_e2a_msg *msg)
{
	int tbl_inx = 0, req_id = 0, timer_req_id = 0;
	int64_t min_end_time = 0;
	int tbl_size = ARRAY_SIZE(ind_timeout_tbl);
	u16 msg_id = msg->id;
	struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)rwnx_plat->drvdata;

	/* don't need to tx loop if wifi is unregistered */
	if (rwnx_plat == NULL)
		return;

	/* don't need to tx loop if tx loop is already triggered */
	if (bes_tx_loop_is_enabled(rwnx_plat))
		return;

	if (msg_id == SCANU_START_CFM || msg_id == SCAN_CANCEL_CFM) {
		/* If the received msg id is SCANU_START_CFM, the corresponding
		msg id in ind_timeout_tbl is SCANU_START_REQ */
		req_id = SCANU_START_REQ;
	} else if (msg_id == SM_CONNECT_CFM && ((struct sm_connect_cfm*)(msg->param))->status != CO_OK) {
		req_id = SM_CONNECT_REQ;
	} else if (msg_id == SM_DISCONNECT_CFM) {
		/**
		 *  If fw is disconnected, SM_DISCONNECT_CFM comes without SM_DISCONNECT_IND;
		 *  so stop wait_ind_timer for SM_DISCONNECT_REQ.
		 */
		if (rwnx_hw && !test_bit(RWNX_DEV_DISCONNECTING, &rwnx_hw->flags)) {
			req_id = SM_DISCONNECT_REQ;
		}
	} else {
		/* If the received msg id is XXX_IND, the corresponding msg id
		in ind_timeout_tbl is XXX_REQ */
		req_id = msg_id - 2;
	}

	/* stop or restart the wait_ind_timer for a2e_msg */
	spin_lock_bh(&rwnx_plat->tx_loop.wait_ind_lock);
	for(tbl_inx = 0; tbl_inx < tbl_size; tbl_inx++) {
		if (ind_timeout_tbl[tbl_inx].msg_req == req_id) {
			bes_dbg(BES_DBG_TXLOOP, "stop wait_ind_timer for msg: %s, tbl_inx: %d\n", RWNX_ID2STR(req_id), tbl_inx);
			ind_timeout_tbl[tbl_inx].end_time = 0;
			kfree(ind_timeout_tbl[tbl_inx].record_a2emsg);
			ind_timeout_tbl[tbl_inx].record_a2emsg = NULL;

			/* After the msg information is deleted from ind_timeout_tbl,
			the minimum timeout period is selected to start or delete the timer */
			min_end_time = bes_tx_loop_min_end_time_get(rwnx_plat, &timer_req_id);
			if (min_end_time == 0) {
				rwnx_plat->tx_loop.ind_timeout_msg = 0;
				del_timer_sync(&rwnx_plat->tx_loop.wait_ind_timer);
			} else {
				rwnx_plat->tx_loop.ind_timeout_msg = timer_req_id;
				mod_timer(&rwnx_plat->tx_loop.wait_ind_timer, min_end_time);
			}

			spin_unlock_bh(&rwnx_plat->tx_loop.wait_ind_lock);
			return;
		}
	}
	spin_unlock_bh(&rwnx_plat->tx_loop.wait_ind_lock);
}

void bes_tx_loop_del_specific_ind_timer(struct rwnx_plat *rwnx_plat, lmac_msg_id_t msg_id)
{
	int min_end_time, timer_req_id;

	/* Clear the record msg in ind_timeout_tbl */
	bes_tx_loop_clear_ind(rwnx_plat, msg_id);

	spin_lock_bh(&rwnx_plat->tx_loop.wait_ind_lock);
	/* If the currently running timer is triggered by the target msg, restart/delete the timer */
	if (rwnx_plat->tx_loop.ind_timeout_msg == msg_id) {
		min_end_time = bes_tx_loop_min_end_time_get(rwnx_plat, &timer_req_id);
		if (min_end_time == 0) {
			rwnx_plat->tx_loop.ind_timeout_msg = 0;
			del_timer_sync(&rwnx_plat->tx_loop.wait_ind_timer);
		} else {
			rwnx_plat->tx_loop.ind_timeout_msg = timer_req_id;
			mod_timer(&rwnx_plat->tx_loop.wait_ind_timer, min_end_time);
		}
	}
	spin_unlock_bh(&rwnx_plat->tx_loop.wait_ind_lock);
}

void bes_tx_loop_rx_handler(struct rwnx_hw *rwnx_hw, struct sk_buff *skb)
{
	struct ipc_e2a_msg *msg;
	struct rwnx_plat *rwnx_plat = rwnx_hw->plat;
	struct ipc_header *pHdr = (struct ipc_header *)skb->data;

	if (pHdr->type == EVENT_E2A_MSG) {
		/* When skb is msg, disable the wait_ind_timer according to msg */
		msg = (struct ipc_e2a_msg *)(skb->data + 4);
		bes_tx_loop_stop_ind_timer(rwnx_plat, msg);
	}
}

void bes_tx_loop_clear_ind(struct rwnx_plat *rwnx_plat, u16 msg_id)
{
	int tbl_inx = 0;
	int tbl_size = ARRAY_SIZE(ind_timeout_tbl);

	/* don't need to run if wifi is unregistered */
	if (rwnx_plat == NULL) {
		bes_err(BES_DBG_TXLOOP, "%s, rwnx_plat == NULL\n", __func__);
		return;
	}

	/* Clear the msg that has returned indication from ind_timeout_tbl */
	spin_lock_bh(&rwnx_plat->tx_loop.wait_ind_lock);
	for(tbl_inx = 0; tbl_inx < tbl_size; tbl_inx++) {
		if (ind_timeout_tbl[tbl_inx].end_time != 0 && ind_timeout_tbl[tbl_inx].record_a2emsg != NULL) {
			if (ind_timeout_tbl[tbl_inx].msg_req == msg_id) {
				bes_dbg(BES_DBG_TXLOOP, "%s, clear_ind for msg: %s\n",
					__func__, RWNX_ID2STR(ind_timeout_tbl[tbl_inx].msg_req));
				ind_timeout_tbl[tbl_inx].end_time = 0;
				kfree(ind_timeout_tbl[tbl_inx].record_a2emsg);
				ind_timeout_tbl[tbl_inx].record_a2emsg = NULL;

				spin_unlock_bh(&rwnx_plat->tx_loop.wait_ind_lock);
				return;
			}
		}
	}
	spin_unlock_bh(&rwnx_plat->tx_loop.wait_ind_lock);
}

void bes_tx_loop_all_clear(struct rwnx_plat *rwnx_plat)
{
	int tbl_inx = 0;
	int tbl_size = ARRAY_SIZE(ind_timeout_tbl);

	/* don't need to run if wifi is unregistered */
	if (rwnx_plat == NULL) {
		bes_err(BES_DBG_TXLOOP, "%s, rwnx_plat == NULL\n", __func__);
		return;
	}

	/* Clear the msg that has returned indication from ind_timeout_tbl */
	spin_lock_bh(&rwnx_plat->tx_loop.wait_ind_lock);
	for(tbl_inx = 0; tbl_inx < tbl_size; tbl_inx++) {
		if (ind_timeout_tbl[tbl_inx].end_time != 0 || ind_timeout_tbl[tbl_inx].record_a2emsg != NULL) {
			bes_dbg(BES_DBG_TXLOOP, "%s, clear_ind for msg: %s\n",
				__func__, RWNX_ID2STR(ind_timeout_tbl[tbl_inx].msg_req));

			ind_timeout_tbl[tbl_inx].end_time = 0;
			if (ind_timeout_tbl[tbl_inx].record_a2emsg != NULL) {
				kfree(ind_timeout_tbl[tbl_inx].record_a2emsg);
				ind_timeout_tbl[tbl_inx].record_a2emsg = NULL;
			}
		}
	}
	spin_unlock_bh(&rwnx_plat->tx_loop.wait_ind_lock);
}

/*
* Reply the corresponding skb according to cmd requirements
*/
static void bes_tx_loop_pipe_send(struct rwnx_plat *rwnx_plat, struct rwnx_cmd *cmd)
{
	struct rwnx_cmd_a2emsg *record_a2e_msg;

	static struct ipc_a2e_msg ipc_a2e_msg;

	/* don't need to tx loop if wifi is unregistered */
	if (rwnx_plat == NULL)
		return;

	if (!(cmd->flags & RWNX_CMD_FLAG_WAIT_PUSH)) {
		memcpy(&ipc_a2e_msg, cmd->a2e_msg, sizeof(struct rwnx_cmd_a2emsg) + cmd->a2e_msg->param_len);
		record_a2e_msg = (struct rwnx_cmd_a2emsg *)(&ipc_a2e_msg);

		bes_dbg(BES_DBG_TXLOOP, "tx loop pipe pending send msg: %s\n", RWNX_ID2STR(cmd->a2e_msg->id));

		/* When tx loop replies to msg, it will also reply to the corresponding indication,
		so it is necessary to clear the corresponding msg in ind_timeout_tbl */
		bes_tx_loop_clear_ind(rwnx_plat, record_a2e_msg->id);

		/* Sending cfm before ack ensures that when multiple MSGS with the same id are sent
		consecutively, the previous msg is cleared from the cmd_mgr queue when the next msg is sent */

		/* Constructs the cfm skb corresponding to this msg */
		if (cmd->flags & RWNX_CMD_FLAG_WAIT_CFM) {
			bes_tx_loop_build_generic_msg_cfm(rwnx_plat, cmd);
		}
		/* Constructs the ack skb corresponding to this msg */
		if (cmd->flags & RWNX_CMD_FLAG_WAIT_ACK) {
			bes_tx_loop_build_generic_msg_ack(rwnx_plat);
		}

		/* The specific msg also needs to construct an indication */
		bes_tx_loop_build_ind(rwnx_plat, record_a2e_msg);
	}
}

static void bes_tx_loop_send_msg(struct rwnx_plat *rwnx_plat)
{
	struct rwnx_cmd *self_handle_cmd = NULL, *self_handle_tmp_cmd = NULL;
	struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)rwnx_plat->drvdata;

	if (rwnx_hw == NULL)
		return;

	/* Constructs the corresponding skb based on the cmd that has
	not been cleared from the cmd_mgr list */
	spin_lock_bh(&rwnx_hw->cmd_mgr.lock);
	if (!list_empty(&rwnx_hw->cmd_mgr.cmds)) {
		list_for_each_entry_safe(self_handle_cmd, self_handle_tmp_cmd, &rwnx_hw->cmd_mgr.cmds, list) {
			if (self_handle_cmd->flags & RWNX_CMD_FLAG_WAIT_PUSH)
				break;
			bes_info(BES_DBG_TXLOOP, "%s, msg->id: %s\n", __func__, RWNX_ID2STR(self_handle_cmd->a2e_msg->id));
			bes_tx_loop_pipe_send(rwnx_plat, self_handle_cmd);

		}
	}
	spin_unlock_bh(&rwnx_hw->cmd_mgr.lock);
	bes_tx_loop_wakeup_rx_process(rwnx_plat);
}

int bes_tx_loop_direct_send(struct rwnx_plat *rwnx_plat, struct rwnx_cmd_a2emsg *a2e_msg)
{
	struct rwnx_cmd *self_handle_cmd = NULL, *self_handle_tmp_cmd = NULL;
	struct rwnx_hw *rwnx_hw = (struct rwnx_hw *)rwnx_plat->drvdata;

	/* When msg is sent, wait_ind_timer is triggered first */
	bes_tx_loop_start_ind_timer(rwnx_hw->plat, a2e_msg);

	/* When the tx loop is not triggered, msg is sent to the fw normally */
	if (!bes_tx_loop_is_enabled(rwnx_hw->plat)) {
		return BES_SEND_BY_HW_PATH;
	}

	/* When the tx loop is triggered, msg is not sent to the fw */
	spin_lock_bh(&rwnx_hw->cmd_mgr.lock);
	if (!list_empty(&rwnx_hw->cmd_mgr.cmds)) {
		list_for_each_entry_safe(self_handle_cmd, self_handle_tmp_cmd, &rwnx_hw->cmd_mgr.cmds, list) {
			if (a2e_msg->id == self_handle_cmd->a2e_msg->id) {
				bes_info(BES_DBG_TXLOOP, "%s, msg: %s, cmd:%p\n", __func__, RWNX_ID2STR(self_handle_cmd->a2e_msg->id), self_handle_cmd);
				bes_tx_loop_pipe_send(rwnx_plat, self_handle_cmd);

				spin_unlock_bh(&rwnx_hw->cmd_mgr.lock);
				bes_tx_loop_wakeup_rx_process(rwnx_plat);
				return BES_SEND_BY_TX_LOOP;
			}
		}
		bes_err(BES_DBG_TXLOOP, "%s, do not find cmd in cmd_mgr list.\n", __func__);
	} else {
		bes_err(BES_DBG_TXLOOP, "%s, cmd_mgr list is empty.\n", __func__);
	}
	spin_unlock_bh(&rwnx_hw->cmd_mgr.lock);

	return BES_SEND_BY_TX_LOOP;
}
