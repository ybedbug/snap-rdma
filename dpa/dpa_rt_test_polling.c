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
#include <string.h>

#include "dpa.h"

int dpa_init()
{
	dpa_rt_init();
	printf("dpa runtime test init done!\n");
	return 0;
}

int dpa_run()
{
	printf("RT test starting\n");
	dpa_rt_start();

	printf("All done. Waiting for DPU command\n");
	snap_dpa_cmd_recv(dpa_mbox(), SNAP_DPA_CMD_STOP);
	snap_dpa_rsp_send(dpa_mbox(), SNAP_DPA_RSP_OK);

	printf("RT test done. Exiting\n");
	return 0;
}
