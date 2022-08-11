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
#include "snap_dma_internal.h"

// hack to force creation of the data section
uint32_t dummy_var __attribute__((section (".data"))); // per thread

static inline void dpa_thread_config(struct snap_dpa_tcb *tcb)
{
	struct flexio_os_thread_ctx *ctx;

	ctx = flexio_os_get_thread_ctx();

	/**
	 * TODO: metadata can be set at thread creation however flexio
	 * attaches its own metadata
	 */
	ctx->metadata_parameter = (uint64_t)tcb;
	dpa_window_set_mkey(tcb->active_lkey);
}

static void dpa_do_init(struct snap_dpa_tcb *tcb)
{
	struct snap_dpa_cmd_start *cmd_start;
	struct flexio_os_thread_ctx *ctx;

	ctx = flexio_os_get_thread_ctx();
	tcb->mbox_address = ctx->window_base + tcb->mbox_address;

	flexio_os_outbox_set_cfg(ctx->outbox_config_id);

	dpa_debug("==> Starting DPA thread\n");
	dpa_debug("TCB         : 0x%lx\n", (uint64_t)tcb);
	dpa_debug("Mailbox base: 0x%lx\n", tcb->mbox_address);
	dpa_debug("Heap base   : 0x%lx\n", tcb->data_address);
	dpa_debug("Heap size   : %ld\n", tcb->heap_size);
	dpa_debug("Thread data : 0x%lx\n", *(uint64_t *)(ctx + 1));

	snap_memory_bus_fence();
	dpa_init();

	cmd_start = (struct snap_dpa_cmd_start *)snap_dpa_cmd_recv(dpa_mbox(), SNAP_DPA_CMD_START);
	memcpy(&tcb->cmd_cq, &cmd_start->cmd_cq, sizeof(tcb->cmd_cq));
	dpa_debug("Command cq  : 0x%x addr=0x%lx, cqe_cnt=%d cqe_size=%d\n",
			tcb->cmd_cq.cq_num, tcb->cmd_cq.cq_addr, tcb->cmd_cq.cqe_cnt, tcb->cmd_cq.cqe_size);
	snap_dv_poll_cq(&tcb->cmd_cq, 64);
	tcb->cmd_last_sn = cmd_start->base.sn;
	snap_dpa_rsp_send(dpa_mbox(), SNAP_DPA_RSP_OK);
	tcb->init_done = 1;
}

void __snap_dpa_thread_start(uint64_t tcb_addr)
{
	struct snap_dpa_tcb *tcb = (struct snap_dpa_tcb *)tcb_addr;

	dpa_thread_config(tcb);

	if (snap_unlikely(!tcb->init_done))
		dpa_do_init(tcb);

	dpa_run();

	flexio_dev_return();
}
