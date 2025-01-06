/**
 ******************************************************************************
 * @file bes_vif_info.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 ******************************************************************************
 */

#ifndef _BES_VIF_INFO_H_
#define _BES_VIF_INFO_H_

#include "rwnx_defs.h"

#define VIF_INFO_MM_AP_AGE_OUT_DEFAULT                   30   //3s
#define VIF_INFO_MM_AP_AGE_OUT_NULL_DATA_DEFAULT         5    //5 cnt
#define VIF_INFO_MM_AP_AGE_OUT_NULL_DATA_CHECK_DEFAULT   15   //15 cnt

struct vif_info_ext_tag {
	u8 bss_lst;
	u8 bss_null_data_check_thd;
	u8 age_out_thd;
	u8 age_null_data_check_thd;
	u8 age_active_null_data_check_thd;
	u8 reserved8;
	/* rc module param start
	driver config rc_opt flag*/
	u8 rc_opt;
	u8 prot_type;
	u8 prot_psr;
	u8 shortgi_psr;
	u8 tx_max_bw;
	/* lmac dynamic config rc_opt result*/
	u8 up_rc_opt;
	/* rc module param end */
	/* umac config, 100000 = 102400us(default:beacon intervalue),
	50000=50000/100000 * 102400us */
	u32 config_tdd_repartition_dur;
	u8 flags;
	/* number of TBTT check process cnt */
	u8 tbtt_check_cnt;
	/* number of TBTT drift cnt */
	u8 tbtt_drift_cnt;
	/* must keep last */
	u8 reserved[16];
};

enum vif_info_ext_ops {
	VIF_INFO_EXT_OPS_SET = 0,
	VIF_INFO_EXT_OPS_GET,
};

struct vif_info_item {
	u8 offset;
	u8 length;
	u8 value[];
};

struct vif_info_ext_req {
	struct mm_custom_req_hdr hdr;
	u8 vif_index;
	struct vif_info_item item;
};

enum rc_opt_id {
	RC_OPT_RTSCTS  = 0,
	RC_OPT_SHORT_GI,
	RC_OPT_BW,
	RC_OPT_EDCA,
	RC_OPT_AGC,
	RC_OPT_CCA,
};

enum vif_info_ops {
	/* Get EDCA parameter */
	VIF_INFO_OPS_EDCA_GET = 0,
	/* Trigger switch channel operation */
	VIF_INFO_OPS_SWITCH_CHANNEL,
	/* Switch power save mode */
	VIF_INFO_OPS_AP_PERFORM_PS,
	/* Stop beacon transmitting */
	VIF_INFO_OPS_AP_STOP_BEACONING,
	/* Trigger NoA Update for STA+GO coex, used when config_tdd_repartition_dur changed */
	VIF_INFO_OPS_GO_NOA_UPDATE,
	/* Set CSA info, for advanced CSA operation */
	VIF_INFO_OPS_SET_CSA_INFO,
	/* Get channel load */
	VIF_INFO_OPS_GET_CH_LOAD,
};

struct vif_info_edca_get_req {
	struct mm_custom_req_hdr hdr;
	u8 vif_index;
};

struct vif_info_noa_update_req {
	struct mm_custom_req_hdr hdr;
	u8 vif_index;
};

struct vif_info_get_chan_load_req {
	struct mm_custom_req_hdr hdr;
	u8 vif_index;
};

struct vif_info_get_ch_load_cfm {
	/* CCA measurement duration (microsecond) */
	u32 measure_time;
	/* CCA busy time (microsecond) */
	u32 busy_time;
};

void bes_set_vif_ext_param(struct rwnx_vif *vif, bool start);
int bes_vif_info_edca_get(struct rwnx_vif *vif, u32 *acm_param, u16 len);
void bes_set_vif_partition(struct rwnx_vif *vif1, struct rwnx_vif *vif2, u32 part1, u32 part2);
int bes_vif_info_get_chan_load(struct rwnx_hw *rwnx_hw, u8 vif_idx, u32 *busy_ratio);

int bes_vif_info_print_rate(char *buf, int size, int format, int nss, int mcs, int bw, int sgi, int pre, int dcm, int *r_idx);
int bes_vif_info_print_rate_from_cfg(char *buf, int size, u32 rate_config, int *r_idx, int ru_size);
int bes_vif_info_get_hist_len(const char **hist);
int bes_vif_info_tx_rate_dump(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta, char *buf, int buf_max_size,
			      int *rate_sort, int rate_print_num);
int bes_vif_info_rx_rate_dump(struct rwnx_hw *rwnx_hw, struct rwnx_sta *sta, char *buf, int buf_max_size,
			      int *rate_sort, int rate_print_num);
#endif /* _BES_VIF_INFO_H_ */
