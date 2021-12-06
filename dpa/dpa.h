/*
 * Copyright © 2021 NVIDIA CORPORATION & AFFILIATES. ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of Nvidia Corporation and its affiliates
 * (the "Company") and all right, title, and interest in and to the software
 * product, including all associated intellectual property rights, are and
 * shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 */

#ifndef _DPA_H
#define _DPA_H

/* TODO: should be configurable */
#define APU_BUILD 1

#include <libflexio-dev/flexio_dev.h>
#include <libflexio-dev/flexio_dev_debug.h>
#include <libflexio-os/flexio_os_syscall.h>
#include <libflexio-os/flexio_os.h>

#include "snap_dpa_common.h"

#if APU_BUILD
#define dpa_print_string(str)   print_sim_str((str), 0)
#define dpa_print_hex(num)      print_sim_hex((num), 0)
#else
#define dpa_print_string(str)
#define dpa_print_hex(num)
#endif

static inline void dpa_print_one_arg(char *msg, uint64_t arg)
{
	dpa_print_string(msg);
	dpa_print_hex(arg);
	dpa_print_string("\n");
}

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
	struct flexio_os_thread_ctx *ctx;
	uint32_t *window_u_cfg;

	/* this is based on flexio rpc entry_point.c */
	ctx = flexio_os_get_thread_ctx();
	window_u_cfg = (uint32_t *)ctx->window_config_base;
	*window_u_cfg = mkey;
}

/**
 * dpa_window_get_base() - get window base address
 *
 * Return: window base address
 */
static inline uint64_t dpa_window_get_base()
{
	return flexio_os_get_thread_ctx()->window_base;
}

/**
 * dpa_mbox() - get mailbox address
 *
 * Return:
 * Mailbox address
 */
static inline void *dpa_mbox(struct snap_dpa_tcb *tcb)
{

	dpa_window_set_mkey(tcb->mbox_lkey);
	return (void *)tcb->mbox_address;
}

#endif
