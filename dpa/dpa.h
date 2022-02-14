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

#include <stddef.h>

#include <libflexio-dev/flexio_dev.h>
#include <libflexio-dev/flexio_dev_debug.h>
#include <libflexio-dev/flexio_dev_queue_access.h>
#include <libflexio-os/flexio_os_syscall.h>
#include <libflexio-os/flexio_os.h>

#include "dpa_log.h"

#include "snap_dpa_common.h"

#if SIMX_BUILD
#define dpa_print_string(str)   print_sim_str((str), 0)
#else
#define dpa_print_string(str)
#endif

#define DPA_CACHE_LINE_BYTES 64

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
	/* consider weaker barrier */
	snap_memory_bus_fence();
	*window_u_cfg = mkey;
	snap_memory_bus_fence();
}

/**
 * dpa_window_get_base() - get window base address
 *
 * Return: window base address
 */
static inline uint64_t dpa_window_get_base(void)
{
	return flexio_os_get_thread_ctx()->window_base;
}

/**
 * dpa_tcb() - get thread control block
 *
 * Return:
 * Thread control block
 */
static inline struct snap_dpa_tcb *dpa_tcb(void)
{
	struct flexio_os_thread_ctx *ctx;

	ctx = flexio_os_get_thread_ctx();

	/* TODO: need to replace with the real thing,
	 * at the moment we use hacked rtos that adds extra 8 bytes
	 * to the thread contexts and sets it to thread arg
	 */
	return (struct snap_dpa_tcb *)(*(uint64_t *)(ctx + 1));
}

/**
 * dpa_mbox() - get mailbox address
 *
 * Return:
 * Mailbox address
 */
static inline void *dpa_mbox(void)
{

	struct snap_dpa_tcb *tcb = dpa_tcb();

	dpa_window_set_mkey(tcb->mbox_lkey);
	return (void *)tcb->mbox_address;
}

void *dpa_thread_alloc(size_t size);
void dpa_thread_free(void *addr);

struct snap_dma_q *dpa_dma_ep_cmd_copy(struct snap_dpa_cmd *cmd);

static inline void dpa_dma_q_ring_tx_db(uint16_t qpnum, uint16_t pi)
{
	struct flexio_os_thread_ctx *ctx;

	/* NOTE: thread context is not a syscall but a read of tp register
	 * this is fast but flexio should make these inline 
	 */
	ctx = flexio_os_get_thread_ctx();
	flexio_dev_qp_sq_ring_db((struct flexio_dev_thread_ctx *)ctx, pi, qpnum);
}

static inline void dpa_dma_q_arm_cq(uint16_t cqnum, uint16_t ci)
{
	struct flexio_os_thread_ctx *ctx;

	ctx = flexio_os_get_thread_ctx();
	flexio_dev_cq_arm((struct flexio_dev_thread_ctx *)ctx, ci, cqnum);
}

/**
 * dpa_init() - initialize thread
 * @tcb: thread control block
 *
 * The function is called ONCE when the thread is run for the first
 * time.
 *
 * The function must be provided by the DPA application code
 *
 * Return:
 * 0 on success
 */
int dpa_init(void);

/**
 * dpa_run() - run thread
 * @tcb: thread control block
 *
 * The function is called every time the thread is invoked.
 *
 * The function must be provided by the DPA application code
 *
 * Return:
 * 0 on success
 */
int dpa_run(void);

static inline struct dpa_rt_context *dpa_rt_ctx()
{
	return (struct dpa_rt_context *)dpa_tcb()->data_address;
}

void dpa_rt_init(void);
void dpa_rt_start(void);
#endif
