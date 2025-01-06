/*
 * @file bes_power_ctrl_msg.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __BES_POWER_CTRL_MSG_H
#define __BES_POWER_CTRL_MSG_H

struct bes_power_ctrl_msg_ops {
	int (*iowrite)(void *priv, u32 *io_dmabuf, u32 val);
	void (*unregister_netdev)(void *priv);
	void (*exit)(void *priv);
};

struct bes_power_ctrl_msg {
	const struct bes_power_ctrl_msg_ops *ops;
	u32 io_dmabuf;
	void *priv;
};

#ifdef CONFIG_BES_POWER_CTRL_MSG
void *bes_power_ctrl_msg_register(const struct bes_power_ctrl_msg_ops *ops,
                                   struct rwnx_plat *rwnx_plat, size_t priv_data_len, plat_power_cb_t cb);
void bes_power_ctrl_msg_unregister(void *priv);
void bes_power_ctrl_set_chip_power(int (*f)(struct bes_power_ctrl *, bool));
#else
static inline void *bes_power_ctrl_msg_register(struct bes_power_ctrl_msg_ops *ops,
                                                 struct rwnx_plat *rwnx_plat, size_t priv_data_len,
                                                 plat_power_cb_t cb) { return NULL; }
static inline void bes_power_ctrl_msg_unregister(void *priv) {}
#endif /* CONFIG_BES_POWER_CTRL_MSG */

#endif /* __BES_POWER_CTRL_MSG_H */