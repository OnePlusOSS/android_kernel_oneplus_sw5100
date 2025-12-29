/**
 ******************************************************************************
 *
 * rwnx_cmds.c
 *
 * Handles queueing (push to IPC, ack/cfm from IPC) of commands issued to
 * LMAC FW
 *
 * Copyright (C) RivieraWaves 2014-2021
 *
 ******************************************************************************
 */

#include <linux/list.h>

#include "rwnx_cmds.h"
#include "rwnx_defs.h"
#include "rwnx_strs.h"
#define CREATE_TRACE_POINTS
#include "rwnx_events.h"
#include "bes_log.h"
#include "bes_tx_loop.h"
#include "bes_fw_log_fifo_dump.h"
#include "bes_pwr.h"

/**
 *
 */
static void cmd_dump(const struct rwnx_cmd *cmd)
{
#ifndef CONFIG_RWNX_FHOST
	bes_err(BES_DBG_CMDS, "tkn[%d]  flags:%04x  result:%3d  cmd:%4d-%-24s - reqcfm(%4d-%-s)\n", cmd->tkn, cmd->flags, cmd->result,
	       cmd->id, RWNX_ID2STR(cmd->id), cmd->reqid,
	       cmd->reqid != (lmac_msg_id_t)-1 ? RWNX_ID2STR(cmd->reqid) : "none");
#endif
}

/**
 *
 */
static void cmd_complete(struct rwnx_cmd_mgr *cmd_mgr, struct rwnx_cmd *cmd)
{
	lockdep_assert_held(&cmd_mgr->lock);
	bes_psa_msga2e_hostid_clear(cmd);

	list_del(&cmd->list);
	cmd_mgr->queue_sz--;

	cmd->flags |= RWNX_CMD_FLAG_DONE;
	if (cmd->flags & RWNX_CMD_FLAG_NONBLOCK) {
		kfree(cmd->a2e_msg);
		kfree(cmd);
	} else {
		if (RWNX_CMD_WAIT_COMPLETE(cmd->flags)) {
			cmd->result = 0;
			complete(&cmd->complete);
		}
	}
}

static void cmd_error(struct rwnx_cmd_mgr *cmd_mgr, struct rwnx_cmd *cmd, int error_code)
{
	spin_lock_bh(&cmd_mgr->lock);
#ifndef CONFIG_BES_TXLOOP
	cmd_mgr->state = RWNX_CMD_MGR_STATE_CRASHED;
#endif
	if (!(cmd->flags & RWNX_CMD_FLAG_DONE)) {
		cmd->result = error_code;
		cmd_complete(cmd_mgr, cmd);
	}
	spin_unlock_bh(&cmd_mgr->lock);
}

/**
 *
 */
static int cmd_mgr_queue(struct rwnx_cmd_mgr *cmd_mgr, struct rwnx_cmd *cmd)
{
	struct rwnx_hw *rwnx_hw = container_of(cmd_mgr, struct rwnx_hw, cmd_mgr);
	bool defer_push = false;
	long ret;

	bes_dbg(BES_DBG_CMDS, RWNX_FN_ENTRY_STR);
	trace_msg_send(cmd->id);

	/* Wifi low power processing for msg tx. For non-blocked msg, it will be defered
	   if it is as a wakeup source, and re-send it after wake-up is completed. */
	if (bes_pwr_msg_tx_process(rwnx_hw, cmd)) {
		cmd->result = 0;
		return 0;
	}

	spin_lock_bh(&cmd_mgr->lock);

#ifndef CONFIG_BES_TXLOOP
	if (cmd_mgr->state == RWNX_CMD_MGR_STATE_CRASHED) {
		bes_err(BES_DBG_CMDS, "cmd queue unavailable, abandon sending msg(%d-%s)\n",
			cmd->id, RWNX_ID2STR(cmd->id));
		cmd->result = -EPIPE;
		spin_unlock_bh(&cmd_mgr->lock);
		return -EPIPE;
	}
#endif

#ifndef CONFIG_RWNX_FHOST
	if (!list_empty(&cmd_mgr->cmds)) {
		struct rwnx_cmd *last;

		if (cmd_mgr->queue_sz == cmd_mgr->max_queue_sz) {
			bes_err(BES_DBG_CMDS, "Too many cmds (%d) already queued\n", cmd_mgr->max_queue_sz);
			cmd->result = -ENOMEM;
			spin_unlock_bh(&cmd_mgr->lock);
			return -ENOMEM;
		}
		last = list_entry(cmd_mgr->cmds.prev, struct rwnx_cmd, list);
		if (last->flags & (RWNX_CMD_FLAG_WAIT_ACK | RWNX_CMD_FLAG_WAIT_PUSH)) {
#if 0 // queue even NONBLOCK command.
            if (cmd->flags & RWNX_CMD_FLAG_NONBLOCK) {
                bes_err(BES_DBG_CMDS, "cmd queue busy\n");
                cmd->result = -EBUSY;
                spin_unlock_bh(&cmd_mgr->lock);
                return -EBUSY;
            }
#endif
			cmd->flags |= RWNX_CMD_FLAG_WAIT_PUSH;
			defer_push = true;
		}
	}
#endif

	if (cmd->flags & RWNX_CMD_FLAG_REQ_CFM)
		cmd->flags |= RWNX_CMD_FLAG_WAIT_CFM;
	// else {
	cmd->flags |= RWNX_CMD_FLAG_WAIT_ACK;
	cmd->tkn = cmd_mgr->next_tkn++;
	//}
	cmd->result = -EINTR;

	if (!(cmd->flags & RWNX_CMD_FLAG_NONBLOCK))
		init_completion(&cmd->complete);

	list_add_tail(&cmd->list, &cmd_mgr->cmds);
	cmd_mgr->queue_sz++;

	if (!defer_push) {
		rwnx_ipc_msg_push(rwnx_hw, cmd, RWNX_CMD_A2EMSG_LEN(cmd->a2e_msg));
	}

	if (!(cmd->flags & RWNX_CMD_FLAG_NONBLOCK)) {
		spin_unlock_bh(&cmd_mgr->lock);
#ifdef CONFIG_RWNX_FHOST
		if (wait_for_completion_killable(&cmd->complete)) {
			if (cmd->flags & RWNX_CMD_FLAG_WAIT_ACK)
				up(&rwnx_hw->term.fw_cmd);
			cmd->result = -EINTR;
			spin_lock_bh(&cmd_mgr->lock);
			cmd_complete(cmd_mgr, cmd);
			spin_unlock_bh(&cmd_mgr->lock);
			/* TODO: kill the cmd at fw level */
		} else {
			// possible when commands are aborted with cmd_mgr_drain
			if (cmd->flags & RWNX_CMD_FLAG_WAIT_ACK)
				up(&rwnx_hw->term.fw_cmd);
		}
#else
		ret = wait_for_completion_timeout(&cmd->complete,
			msecs_to_jiffies(RWNX_80211_CMD_TIMEOUT_MS * cmd_mgr->queue_sz));
		if (!ret) {
			bes_err(BES_DBG_CMDS, "cmd timed-out\n");
			bes_err(BES_DBG_CMDS, "[cmd timed-out] tx_thread_in: %d, tx_thread_out: %d, data_push_start: %d, data_push_stop: %d, msg_push_start: %d, msg_push_stop: %d\n",
				rwnx_hw->tx_thread_in, rwnx_hw->tx_thread_out, rwnx_hw->data_push_start, rwnx_hw->data_push_stop, rwnx_hw->msg_push_start, rwnx_hw->msg_push_stop);
			cmd_dump(cmd);
			bes_fw_log_fifo_dump_start(rwnx_hw, true);
#ifdef CONFIG_BES_TXLOOP
			bes_tx_loop_force_close(rwnx_hw->plat, true);
			ret = wait_for_completion_timeout(&cmd->complete,
				msecs_to_jiffies(RWNX_80211_CMD_TIMEOUT_MS * cmd_mgr->queue_sz));
			if (unlikely(!ret)) {
				bes_err(BES_DBG_CMDS, "tx_loop cmd %s timed-out\n", RWNX_ID2STR(cmd->id));
				bes_err(BES_DBG_CMDS, "[tx_loop cmd timed-out] tx_thread_in: %d, tx_thread_out: %d, data_push_start: %d, data_push_stop: %d, msg_push_start: %d, msg_push_stop: %d\n",
					rwnx_hw->tx_thread_in, rwnx_hw->tx_thread_out, rwnx_hw->data_push_start, rwnx_hw->data_push_stop, rwnx_hw->msg_push_start, rwnx_hw->msg_push_stop);
				cmd_error(cmd_mgr, cmd, -ETIMEDOUT);
			}
#else
			cmd_error(cmd_mgr, cmd, -ETIMEDOUT);
#endif
		}
#endif
	} else {
		cmd->result = 0;
		spin_unlock_bh(&cmd_mgr->lock);
	}

	return 0;
}

/**
 *
 */
static int cmd_mgr_llind(struct rwnx_cmd_mgr *cmd_mgr, struct rwnx_cmd *cmd)
{
	struct rwnx_cmd *cur, *acked = NULL, *next = NULL;

	bes_dbg(BES_DBG_CMDS, RWNX_FN_ENTRY_STR);

	spin_lock_bh(&cmd_mgr->lock);
	if (cmd == NULL) {
		/**
		 * ipc_host_msg_push and ipc_host_get_last_pushed_msg must be protected
		 * by the same lock: cmd_mgr->lock
		 */
		struct rwnx_hw *rwnx_hw = container_of(cmd_mgr, struct rwnx_hw, cmd_mgr);
		cmd = ipc_host_get_last_pushed_msg(rwnx_hw->ipc_env);
		if (cmd == NULL) {
			spin_unlock_bh(&cmd_mgr->lock);
			return -1;
		}
	}
	list_for_each_entry(cur, &cmd_mgr->cmds, list) {
		if (!acked) {
			if (cur->tkn == cmd->tkn) {
				if (WARN_ON_ONCE(cur != cmd)) {
					cmd_dump(cmd);
				}
				acked = cur;
				continue;
			}
		}
		if (cur->flags & RWNX_CMD_FLAG_WAIT_PUSH) {
			next = cur;
			break;
		}
	}
	if (!acked) {
		bes_err(BES_DBG_CMDS, "Error: acked cmd not found\n");
	} else {
		cmd->flags &= ~RWNX_CMD_FLAG_WAIT_ACK;
		if (RWNX_CMD_WAIT_COMPLETE(cmd->flags))
			cmd_complete(cmd_mgr, cmd);
	}
	if (next) {
		struct rwnx_hw *rwnx_hw = container_of(cmd_mgr, struct rwnx_hw, cmd_mgr);
		next->flags &= ~RWNX_CMD_FLAG_WAIT_PUSH;
		rwnx_ipc_msg_push(rwnx_hw, next, RWNX_CMD_A2EMSG_LEN(next->a2e_msg));
	}
	spin_unlock_bh(&cmd_mgr->lock);

	return 0;
}

static int cmd_mgr_run_callback(struct rwnx_hw *rwnx_hw, struct rwnx_cmd *cmd, struct rwnx_cmd_e2amsg *msg,
				msg_cb_fct cb)
{
	if (!cb)
		return 0;

	spin_lock_bh(&rwnx_hw->cb_lock);
	cb(rwnx_hw, cmd, msg);
	spin_unlock_bh(&rwnx_hw->cb_lock);

	/* ignore msg callback return value */
	return 0;
}

static bool cmd_mgr_msgind_check(struct rwnx_cmd *cmd, struct rwnx_cmd_e2amsg *msg)
{
	struct mm_custom_req_hdr *req = NULL;

	if (msg->param_len > cmd->e2a_buf_len) {
		bes_err(BES_DBG_CMDS, "Error: Unexpect E2A msg len %d > %d for msg %d\n",
			msg->param_len, cmd->e2a_buf_len, cmd->reqid);

		if (cmd->reqid == MM_CUSTOM_CFM) {
			req = (struct mm_custom_req_hdr *)&cmd->a2e_msg->param[0];
			bes_err(BES_DBG_CMDS, "Error: The Cfm is for custom msg id:%d ops:%d\n", req->id, req->ops);
		}

		return false;
        }

	return true;
}

/**
 *

 */
static int cmd_mgr_msgind(struct rwnx_cmd_mgr *cmd_mgr, struct rwnx_cmd_e2amsg *msg, msg_cb_fct cb)
{
	struct rwnx_hw *rwnx_hw = container_of(cmd_mgr, struct rwnx_hw, cmd_mgr);
	struct rwnx_cmd *cmd = NULL, *tmp_cmd = NULL;
	bool found = false;

	bes_dbg(BES_DBG_CMDS, RWNX_FN_ENTRY_STR);
	trace_msg_recv(msg->id);

	spin_lock_bh(&cmd_mgr->lock);
	list_for_each_entry_safe(cmd, tmp_cmd, &cmd_mgr->cmds, list) {
		if (cmd->reqid == msg->id && (cmd->flags & RWNX_CMD_FLAG_WAIT_CFM)) {
			if (!cmd_mgr_run_callback(rwnx_hw, cmd, msg, cb)) {
				found = true;
				cmd->flags &= ~RWNX_CMD_FLAG_WAIT_CFM;

				if (WARN((msg->param_len > RWNX_CMD_E2AMSG_LEN_MAX), "Unexpect E2A msg len %d > %d\n",
					 msg->param_len, RWNX_CMD_E2AMSG_LEN_MAX)) {
					msg->param_len = RWNX_CMD_E2AMSG_LEN_MAX;
				}

				if (cmd->e2a_msg && msg->param_len) {
					if (!cmd_mgr_msgind_check(cmd, msg))
						msg->param_len = cmd->e2a_buf_len;
					memcpy(cmd->e2a_msg, &msg->param, msg->param_len);
				}

				if (RWNX_CMD_WAIT_COMPLETE(cmd->flags))
					cmd_complete(cmd_mgr, cmd);

				break;
			}
		}
	}
	spin_unlock_bh(&cmd_mgr->lock);

	if (!found)
		cmd_mgr_run_callback(rwnx_hw, NULL, msg, cb);

	return 0;
}

/**
 *
 */
static void cmd_mgr_print(struct rwnx_cmd_mgr *cmd_mgr)
{
	struct rwnx_cmd *cur;

	spin_lock_bh(&cmd_mgr->lock);
	bes_dbg(BES_DBG_CMDS, "q_sz/max: %2d / %2d - next tkn: %d\n", cmd_mgr->queue_sz, cmd_mgr->max_queue_sz, cmd_mgr->next_tkn);
	list_for_each_entry(cur, &cmd_mgr->cmds, list) {
		cmd_dump(cur);
	}
	spin_unlock_bh(&cmd_mgr->lock);
}

/**
 *
 */
static void cmd_mgr_drain(struct rwnx_cmd_mgr *cmd_mgr)
{
	struct rwnx_cmd *cur, *nxt;
	struct rwnx_plat *rwnx_plat = rwnx_get_plat_data();

	bes_dbg(BES_DBG_CMDS, RWNX_FN_ENTRY_STR);

	spin_lock_bh(&cmd_mgr->lock);
	list_for_each_entry_safe(cur, nxt, &cmd_mgr->cmds, list) {
		list_del(&cur->list);
		cmd_mgr->queue_sz--;
		if (!(cur->flags & RWNX_CMD_FLAG_NONBLOCK))
			complete(&cur->complete);
	}
	spin_unlock_bh(&cmd_mgr->lock);

	/* free the memory allocated by the req to avoid memory leakage */
	bes_tx_loop_all_clear(rwnx_plat);
}

/**
 *
 */
void rwnx_cmd_mgr_init(struct rwnx_cmd_mgr *cmd_mgr)
{
	bes_dbg(BES_DBG_CMDS, RWNX_FN_ENTRY_STR);

	INIT_LIST_HEAD(&cmd_mgr->cmds);
	spin_lock_init(&cmd_mgr->lock);
	cmd_mgr->max_queue_sz = RWNX_CMD_MAX_QUEUED;
	cmd_mgr->queue = &cmd_mgr_queue;
	cmd_mgr->print = &cmd_mgr_print;
	cmd_mgr->drain = &cmd_mgr_drain;
	cmd_mgr->llind = &cmd_mgr_llind;
	cmd_mgr->msgind = &cmd_mgr_msgind;
}

/**
 *
 */
void rwnx_cmd_mgr_deinit(struct rwnx_cmd_mgr *cmd_mgr)
{
	cmd_mgr->print(cmd_mgr);
	cmd_mgr->drain(cmd_mgr);
	cmd_mgr->print(cmd_mgr);
	memset(cmd_mgr, 0, sizeof(*cmd_mgr));
}
