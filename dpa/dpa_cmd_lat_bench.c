/*
 * Copyright © 202 NVIDIA CORPORATION & AFFILIATES. ALL RIGHTS RESERVED.
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
#include <string.h>

#include "dpa.h"
#include "snap_dma_internal.h"

int dpa_init()
{
	printf("dpa runtime test init done!\n");
	return 0;
}

static void do_command(int *done)
{
	struct snap_dpa_tcb *tcb = dpa_tcb();

	struct snap_dpa_cmd *cmd;
	uint32_t rsp_status;

	*done = 0;
	dpa_debug("command check\n");

	cmd = snap_dpa_mbox_to_cmd(dpa_mbox());

	if (snap_likely(cmd->sn == tcb->cmd_last_sn))
		return;

	dpa_debug("new command %d\n", cmd->cmd);

	tcb->cmd_last_sn = cmd->sn;
	rsp_status = SNAP_DPA_RSP_OK;

	if (cmd->cmd == SNAP_DPA_CMD_STOP)
		*done = 1;

	snap_dpa_rsp_send(dpa_mbox(), rsp_status);
}

int dpa_run()
{
	int done = 0;
	struct snap_dpa_tcb *tcb = dpa_tcb();
	struct mlx5_cqe64 *cqe;

	if (tcb->user_arg == 0 || tcb->user_arg == 1) {
		if (tcb->user_arg == 0) {
			cqe = snap_dv_poll_cq(&tcb->cmd_cq, 64);
			if (!cqe)
				return 0;
		}

		/* sched_in is currently a fence */
		do_command(&done);
	} else {
		do {
			if (tcb->user_arg == 2) {
				cqe = snap_dv_poll_cq(&tcb->cmd_cq, 64);
				if (!cqe)
					continue;
			}
			snap_memory_bus_fence();
			do_command(&done);
		} while (done == 0);
	}

	return 0;
}
