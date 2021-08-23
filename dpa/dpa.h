/*
 * Copyright (c) 2021 NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef _DPA_H
#define _DPA_H

/* TODO: should be configurable */
#define APU_BUILD 1

#include <libflexio-dev/flexio_dev.h>
#include <libflexio-dev/flexio_dev_debug.h>
#include <apu_syscall.h>

#include "snap_dpa_common.h"

#if APU_BUILD
#define dpa_print_string(str)   print_sim_str((str), 0)
#define dpa_print_hex(num)      print_sim_hex((num), 0)
#else
#define dpa_print_string(str)
#define dpa_print_hex(num)
#endif

/**
 * dpa_window_set_mkey() - set window memory key
 * @mkey:  memory key
 *
 * Each address range that is mapped into the window has its own memory key
 * The key must be set each time a new mapping is accessed. Subsequent accesses
 * can reuse current key.
 */
static inline void dpa_window_set_mkey(uint32_t mkey)
{
	uint32_t *window_u_cfg;

	/* currently this is a hack based on flexio rpc entry_point.c */
	window_u_cfg = (uint32_t *)window_get_cfg();
	*window_u_cfg = mkey;
}

/**
 * dpa_mbox() - get mailbox address
 *
 * Return:
 * Mailbox address
 */
static inline void *dpa_mbox()
{
	extern void *mbox_base;

	return mbox_base;
}

#endif
