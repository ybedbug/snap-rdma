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

int main()
{
	dpa_print_string("hello, i am dummy dpa code\n");
	snap_dpa_cmd_recv(dpa_mbox(), SNAP_DPA_CMD_STOP);
	snap_dpa_rsp_send(dpa_mbox(), SNAP_DPA_RSP_OK);
	dpa_print_string("All done. Exiting\n");

	return 0;
}
