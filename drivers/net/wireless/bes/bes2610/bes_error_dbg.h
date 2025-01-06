/*
 * @file bes_error_dbg.h
 *
 * Copyright (c) 2023, Bestechnic
 * Author:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef _BES_ERROR_DBG_
#define _BES_ERROR_DBG_

#include "rwnx_defs.h"

#ifdef CONFIG_BES_FW_ERR_DUMP
void bes_error_dbg_dump(struct dbg_debug_info_tag *dump);
#else
static inline void bes_error_dbg_dump(struct dbg_debug_info_tag *dump) {}
#endif

#endif /* _BES_ERROR_DBG_ */