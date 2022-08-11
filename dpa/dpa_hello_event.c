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

#include <stdio.h>

#include "dpa.h"
#include "snap_dma_internal.h"

int dpa_init()
{
	printf("Init done!\n");
	return 0;
}

int dpa_run()
{
	struct snap_dpa_tcb *tcb = dpa_tcb();
	struct mlx5_cqe64 *cqe;

	printf("thread_ctx@%p size %ld\n", flexio_os_get_thread_ctx(), sizeof(struct flexio_os_thread_ctx));
	printf("HELLO [EVENT], i am dummy dpa code tcb %p\n", tcb);

	cqe = snap_dv_poll_cq(&tcb->cmd_cq, 64);
	if (!cqe)
		return 0;

	printf("new command\n");
	snap_dpa_cmd_recv(dpa_mbox(), SNAP_DPA_CMD_STOP);
	printf("All done. Exiting\n");
	snap_dpa_rsp_send(dpa_mbox(), SNAP_DPA_RSP_OK);
	return 0;
}
