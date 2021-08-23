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

#include "dpa.h"

extern int main();

// hack to force creation of the data section
void *mbox_base __attribute__((section (".data")));
uint32_t mbox_lkey __attribute__((section (".data")));

static void dpa_mbox_config(struct snap_dpa_tcb *tcb)
{
	dpa_window_set_mkey(tcb->mbox_lkey);
	mbox_base = (void *)window_get_base() + tcb->mbox_address;
	mbox_lkey = tcb->mbox_lkey;
}

int __snap_dpa_thread_start(uint64_t tcb_addr)
{
	int ret;
	struct snap_dpa_tcb *tcb = (struct snap_dpa_tcb *)tcb_addr;

	dpa_mbox_config(tcb);

	dpa_print_string("==> Starting DPA thread\n");
	dpa_print_string("TCB : "); dpa_print_hex(tcb_addr); dpa_print_string("\n");
	dpa_print_string("Mailbox base: "); dpa_print_hex((uint64_t)mbox_base); dpa_print_string("\n");

	snap_dpa_cmd_recv(dpa_mbox(), SNAP_DPA_CMD_START);
	/* may be let main do it after init is done */
	snap_dpa_rsp_send(dpa_mbox(), SNAP_DPA_RSP_OK);

	ret = main();
	dpa_print_string("==> DPA thread done\n");
	return ret;
}
