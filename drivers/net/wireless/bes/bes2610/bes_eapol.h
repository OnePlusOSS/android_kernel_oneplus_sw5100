/*
 * @file bes_eapol.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __BES_EAPOL_H__
#define __BES_EAPOL_H__

#include "rwnx_defs.h"

enum bes_eapol_status {
	BES_EAPOL_NONE = 0,
	BES_EAPOL_PROCESSING,
	BES_EAPOL_FAIL,
	BES_EAPOL_SUCCESS,
};

bool bes_eapol_defer_send_skb(struct sk_buff *skb);

void bes_tx_eapol_completion_notify(struct rwnx_sw_txhdr *sw_txhdr, struct tx_cfm_tag *cfm);

void bes_eapol_sta_stored_key_init(struct rwnx_vif *rwnx_vif);

void bes_eapol_sta_stored_key_deinit(struct rwnx_vif *rwnx_vif);

enum msg_eapol_t bes_eapol_get_pkt_type(struct eapol_hdr *eapol);

void bes_eapol_cancel_key_install_defer_work(struct rwnx_vif *rwnx_vif);

bool bes_eapol_key_install_defer_store(struct rwnx_vif *vif, bool pairwise, u8 key_index,
				       u8 cipher, struct key_params *params);

#endif /* __BES_EAPOL_H__ */