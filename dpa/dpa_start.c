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

#include "dpa.h"

// hack to force creation of the data section
void *mbox_base __attribute__((section (".data"))); // per thread
uint32_t mbox_lkey __attribute__((section (".data"))); // per thread

static void dpa_thread_config(struct snap_dpa_tcb *tcb)
{
	struct flexio_os_thread_ctx *ctx;

	ctx = flexio_os_get_thread_ctx();
	dpa_window_set_mkey(tcb->mbox_lkey);
	tcb->mbox_address = ctx->window_base + tcb->mbox_address;
	flexio_os_outbox_set_cfg(ctx->outbox_config_id);
	/* TODO: use real meta param */
	dpa_debug("Thread data : 0x%lx\n", *(uint64_t *)(ctx + 1));
}

void __snap_dpa_thread_start(uint64_t tcb_addr)
{
	struct snap_dpa_tcb *tcb = (struct snap_dpa_tcb *)tcb_addr;

	dpa_thread_config(tcb);

	dpa_debug("==> Starting DPA thread\n");
	dpa_debug("TCB         : 0x%lx\n", tcb_addr);
	dpa_debug("Mailbox base: 0x%lx\n", tcb->mbox_address);
	dpa_debug("Heap base   : 0x%lx\n", tcb->data_address);
	dpa_debug("Heap size   : %ld\n", tcb->heap_size);

	/* TODO:
	 * interrupt thread support:
	 * - init should be run only once
	 * - start barrier should be run only once
	 */
	dpa_init();

	snap_dpa_cmd_recv(dpa_mbox(), SNAP_DPA_CMD_START);
	snap_dpa_rsp_send(dpa_mbox(), SNAP_DPA_RSP_OK);
	dpa_run();

	dpa_debug("==> DPA thread done\n");
	flexio_dev_return();
}
