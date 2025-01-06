/*
 * @file bes_csi.h
 *
 * Copyright (c) 2024, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __BES_CSI_H
#define __BES_CSI_H

#define CSI_CAP_WAIT_TIME_MAX (100)

/// Structure containing the parameters for driver
struct wifi_csi_cap_info {
	/// drv buf pointer
	uint8_t *buf;
	/// drv buf length
	uint32_t buf_len;
	/// csi data length
	uint16_t csi_len;
	/// completion
	struct completion csi_cmp;
};

union wifi_csi_cap_status {
	struct {
		u32 addr : 10;
		u32 formats : 4;
		u32 bw : 2;
		u32 stbc : 1;
		u32 state : 2;
		u32 finish : 1;
		u32 lsig_len : 12;
	};
	u32 value;
};

/// csi bw
#define WIFI_CFG_CSI_CAP_20M_EN            (1 << 0)
#define WIFI_CFG_CSI_CAP_40M_EN            (1 << 1)
#define WIFI_CFG_CSI_CAP_80M_EN            (1 << 2)
#define WIFI_CFG_CSI_CAP_ALL_BW_EN         WIFI_CFG_CSI_CAP_20M_EN | WIFI_CFG_CSI_CAP_40M_EN | WIFI_CFG_CSI_CAP_80M_EN

/// csi type
#define WIFI_CFG_CSI_CAP_HT_EN             (1 << 0)
#define WIFI_CFG_CSI_CAP_VHT_EN            (1 << 1)
#define WIFI_CFG_CSI_CAP_HESU_EN           (1 << 2)
#define WIFI_CFG_CSI_CAP_HEMU_EN           (1 << 3)
#define WIFI_CFG_CSI_CAP_HEER_EN           (1 << 4)
#define WIFI_CFG_CSI_CAP_HETB_EN           (1 << 5)
#define WIFI_CFG_CSI_CAP_ALL_TYPE_EN        WIFI_CFG_CSI_CAP_HT_EN | WIFI_CFG_CSI_CAP_VHT_EN | WIFI_CFG_CSI_CAP_HESU_EN | \
											WIFI_CFG_CSI_CAP_HEMU_EN | WIFI_CFG_CSI_CAP_HEER_EN | WIFI_CFG_CSI_CAP_HETB_EN

#ifdef CONFIG_RWNX_CSI_CAP
/**
 * bes_dbgfs_csi_cap_write - parse CSI cfg cmd
 *
 * @cmd: CSI cfg cmd
 *
 * @returns: errno
*/
int bes_dbgfs_csi_cap_write(char *cmd);

/**
 * bes_dbgfs_csi_cap_read - read CSI datas and convert them to dbgfs buf
 *
 * @rwnx_hw: main driver data
 * @buf: CSI dbgfs buf
 * @buf_len: CSI dbgfs buf length
 *
 * @returns: CSI dbgfs buf write length
*/
int bes_dbgfs_csi_cap_read(struct rwnx_hw *rwnx_hw, char *buf, int buf_len);

/**
 * bes_send_csi_cap_req - read CSI datas and write them to driver buf
 *
 @rwnx_hw: main driver data
 @req: CSI request structure
 @buf: driver buf
 @buf_len: driver buf length
 *
 * @returns: driver buf write length
*/
int bes_send_csi_cap_req(struct rwnx_hw *rwnx_hw, struct mm_csi_cap_req *req, u8 *buf, u32 buf_len);

/**
 * bes_csi_recv_cb - deal with CSI ind result
 *
 @rwnx_hw: main driver data
 @ind: CSI ind result
 *
 * @returns: errno
*/
int bes_csi_recv_cb(struct rwnx_hw *rwnx_hw, struct mm_csi_cap_ind *ind);

/**
 * bes_csi_cap_init - init CSI completion
 *
 @rwnx_hw: main driver data
 *
 * @returns: void
*/
void bes_csi_cap_init(struct rwnx_hw *rwnx_hw);
#else
static inline int bes_send_csi_cap_req(struct rwnx_hw *rwnx_hw, struct mm_csi_cap_req *req, u8 *buf, u32 buf_len) { return 0; }
static inline int bes_csi_recv_cb(struct rwnx_hw *rwnx_hw, struct mm_csi_cap_ind *ind) { return -ENXIO; }
static inline void bes_csi_cap_init(struct rwnx_hw *rwnx_hw) { return ; }
#endif
#endif /* __BES_CSI_H */

