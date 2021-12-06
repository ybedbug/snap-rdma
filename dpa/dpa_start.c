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

extern int main();

// hack to force creation of the data section
void *mbox_base __attribute__((section (".data"))); // per thread
uint32_t mbox_lkey __attribute__((section (".data"))); // per thread

static void dpa_mbox_config(struct snap_dpa_tcb *tcb)
{
	struct flexio_os_thread_ctx *ctx;

	ctx = flexio_os_get_thread_ctx();
	dpa_window_set_mkey(tcb->mbox_lkey);
	tcb->mbox_address = ctx->window_base + tcb->mbox_address;
}

int __snap_dpa_thread_start(uint64_t tcb_addr)
{
	int ret;
	struct snap_dpa_tcb *tcb = (struct snap_dpa_tcb *)tcb_addr;

	dpa_mbox_config(tcb);

	dpa_print_string("==> Starting DPA thread\n");
	dpa_print_one_arg("TCB : ", tcb_addr);
	dpa_print_one_arg("Mailbox base: ", (uint64_t)tcb->mbox_address);

	snap_dpa_cmd_recv(dpa_mbox(tcb), SNAP_DPA_CMD_START);
	/* may be let main do it after init is done */
	snap_dpa_rsp_send(dpa_mbox(tcb), SNAP_DPA_RSP_OK);
	ret = main(1, tcb);
	dpa_print_string("==> DPA thread done\n");
	return ret;
}
