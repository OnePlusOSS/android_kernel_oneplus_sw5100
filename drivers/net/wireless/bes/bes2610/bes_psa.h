/*
 * @file bes_psa.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _BES_PSA_H_
#define _BES_PSA_H_

#include "rwnx_defs.h"
#include "bes_log.h"

/*same with FW*/
enum EVENT_TYPE_A2E {
	EVENT_A2E_MSG,
	EVENT_A2E_MSG_WITH_ACK,
	EVENT_A2E_DATA_REQ,
	EVENT_A2E_MAX_EVENT,
};
enum EVENT_TYPE_E2A {
	EVENT_E2A_MSG_ACK,
	EVENT_E2A_MSG,
	EVENT_E2A_TX_CFM,
	EVENT_E2A_DATA_IND,
	EVENT_E2A_DATA_STATUS_UPDT,
	EVENT_E2A_RADAR_IND,
	EVENT_E2A_UNSUP_IND,
	EVENT_E2A_DEV_READY_IND,
	/* keep last */
	EVENT_E2A_MSG_CRC_ERR_IND,
	EVENT_E2A_DATA_CRC_ERR_IND,
	EVENT_E2A_MAX_EVENT,
};

struct rxu_mpdu_stat_val {
	u32_l host_id; // token value, shall not be 0
	u16_l frame_len;
	u16_l status;
};
struct hostid_to_ptr {
	u32_l hostid;
	void *hostid_ptr;
};

struct dev_status_t {
#ifndef CONFIG_RWNX_PCIE
	struct ipc_header header;
#else
	u16 id;
	u16 dest_id;
	u16 src_id;
	u16 param_len;
#endif
	u32 stat;
};

#define BES_PSA_E2A_HDR_LEN (4)
#define RX_PRI_CONT_LIMIT (1) // mainly msg need ack and confirm
#define DATA_OFFSET (14)      // len of ethernet header, corresponding to fw

#define BES_PSA_DATA_RX_HEADER_LEN (sizeof(struct hw_rxhdr) + 2 + BES_PSA_E2A_HDR_LEN)
#ifdef CONFIG_BES_AMSDU_DEAGG
// header: DA(6) + SA(6) + Len(2) + LLC/SNAP(8), used in bes_psa_e2a_data_rx, etc.
#define BES_PSA_LINEAR_SKB_THRESHOLD (BES_PSA_DATA_RX_HEADER_LEN + 22)
#else
// header: ethhdr(14), used in eth_type_trans, etc.
#define BES_PSA_LINEAR_SKB_THRESHOLD (BES_PSA_DATA_RX_HEADER_LEN + 14)
#endif

void bes_psa_msga2e_hostid_clear(struct rwnx_cmd *rwnx_cmd);
void bes_psa_msg_push(struct rwnx_hw *rwnx_hw, void *msg_buf, uint16_t len);
void bes_psa_e2a_handler(struct rwnx_hw *rwnx_hw, struct sk_buff *skb);
void bes_psa_rx_handler(struct rwnx_plat *rwnx_plat, struct sk_buff *skb);
void bes_psa_data_push(struct rwnx_hw *rwnx_hw, struct txdesc_host *txdesc_host, struct sk_buff *skb, int hw_queue);
int bes_bus_tx_init(struct rwnx_plat *rwnx_plat);
void bes_bus_tx_stop(struct rwnx_plat *rwnx_plat);

struct tx_buffer *bes_psa_alloc_tx_buffer(void);
void bes_psa_free_tx_buffer(struct list_head *list);

#define ALLOC_SKB_MAX_RETRY 10000

static inline struct sk_buff *bes_dev_alloc_skb(unsigned int len)
{
	int retry = ALLOC_SKB_MAX_RETRY;
	struct sk_buff *skb = NULL;

	if (in_atomic()) {
		while (retry -- > 0) {
			skb = __netdev_alloc_skb(NULL, len, GFP_ATOMIC);
			if (!IS_ERR_OR_NULL(skb))
				break;
		}
	} else {
		skb = __netdev_alloc_skb(NULL, len, GFP_KERNEL|__GFP_RETRY_MAYFAIL);
	}

	if (WARN_ON(IS_ERR_OR_NULL(skb))) {
		bes_err(BES_DBG_PSA, "%s alloc len %d failed\n", __func__, len);
	}

	return skb;
}

static inline struct sk_buff *bes_alloc_skb(unsigned int len, gfp_t gfp_mask)
{
	int retry = ALLOC_SKB_MAX_RETRY;
	struct sk_buff *skb = NULL;

	while (retry -- > 0) {
		skb = alloc_skb(len, gfp_mask);
		if (skb)
			break;
	}

	if (WARN_ON(!skb))
		bes_err(BES_DBG_PSA, "%s alloc len %d failed\n", __func__, len);

	return skb;
}

static inline struct page *bes_dev_alloc_pages(unsigned int len, gfp_t gfp_mask)
{
	int retry = ALLOC_SKB_MAX_RETRY;
	struct page *page = NULL;

	while (retry -- > 0) {
		page = __dev_alloc_pages(gfp_mask, get_order(len));
		if (page)
			break;
	}

	if (WARN_ON(!page))
		bes_err(BES_DBG_PSA, "%s alloc len %d failed\n", __func__, len);

	return page;
}

#endif
