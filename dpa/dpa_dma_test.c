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
#include "snap_dma.h"

static int test_read_sync(struct snap_dma_q *q, char *lbuf, struct snap_dpa_cmd_mr *cmd_mr)
{
	int ret;
	uint32_t lkey;

	lkey = snap_dma_q_dpa_mkey(q);
	ret = snap_dma_q_read(q, lbuf, cmd_mr->len, lkey, cmd_mr->va, cmd_mr->mkey, 0);
	snap_dma_q_flush(q);
	printf("LBUF: %s\n", lbuf);

	return ret;
}

static int test_write_sync(struct snap_dma_q *q, char *lbuf, struct snap_dpa_cmd_mr *cmd_mr)
{
	int ret;
	uint32_t lkey;

	lkey = snap_dma_q_dpa_mkey(q);
	strcpy(lbuf, "I am a DPA buffer\n");
	ret = snap_dma_q_write(q, lbuf, cmd_mr->len, lkey, cmd_mr->va, cmd_mr->mkey, 0);
	snap_dma_q_flush(q);

	return ret;
}

static int test_poll_tx(struct snap_dma_q *q, char *lbuf, struct snap_dpa_cmd_mr *cmd_mr)
{
	int poll_cycles = 100000;
	int ret;
	uint32_t lkey;
	struct snap_dma_completion comp, *out_comp;
	int n;

	comp.count = 1;
	lkey = snap_dma_q_dpa_mkey(q);
	strcpy(lbuf, "I am a DPA buffer\n");
	ret = snap_dma_q_write(q, lbuf, cmd_mr->len, lkey, cmd_mr->va, cmd_mr->mkey, &comp);
	if (ret)
		return ret;
	do {
		n = snap_dma_q_poll_tx(q, &out_comp, 1);
		if (n)
			break;
	} while (poll_cycles-- > 0);

	return n == 1 ? 0 : -1;
}

static int test_write_short_sync(struct snap_dma_q *q, struct snap_dpa_cmd_mr *cmd_mr)
{
	int ret;
	char status[16] = "status OK";

	ret = snap_dma_q_write_short(q, status, strlen(status),
			cmd_mr->va + cmd_mr->len - 16, cmd_mr->mkey);
	snap_dma_q_flush(q);
	return ret;
}

static int test_ping_pong(struct snap_dma_q *q)
{
	char msg[16] = "PING";
	int poll_cycles = 100000;
	int ret, n;
	struct snap_rx_completion comp;

	ret = snap_dma_q_send_completion(q, msg, sizeof(msg));
	if (ret)
		return ret;

	printf("Waiting for pong\n");
	do {
		n = snap_dma_q_poll_rx(q, &comp, 1);
		if (n)
			break;
	} while (poll_cycles-- > 0);

	printf("got %d completions\n", n);
	snap_dma_q_flush(q);
	if (n != 1)
		return -1;

	printf("GOT: %s len %d\n", (char *)comp.data, comp.byte_len);
	return 0;
}

int main(int argc, char *argv[])
{
	struct snap_dpa_tcb *tcb = (struct snap_dpa_tcb *)argv;
	struct snap_dpa_cmd *cmd;
	struct snap_dpa_cmd_mr *cmd_mr;
	struct snap_dma_q *q;
	char *lbuf;

	printf("DMA test starting\n");
	printf("Waiting for EP\n");
	cmd = snap_dpa_cmd_recv(dpa_mbox(tcb), SNAP_DPA_CMD_DMA_EP_COPY);
	q = dpa_dma_ep_cmd_copy(tcb, cmd);
	snap_dpa_rsp_send(dpa_mbox(tcb), SNAP_DPA_RSP_OK);
	printf("dma q at %p\n", q);

	printf("Waiting for MR to read/write\n");
	cmd_mr = (struct snap_dpa_cmd_mr *)snap_dpa_cmd_recv(dpa_mbox(tcb), SNAP_DPA_CMD_MR);
	snap_dpa_rsp_send(dpa_mbox(tcb), SNAP_DPA_RSP_OK);
	printf("dma mr at 0x%lx len %ld rkey 0x%x\n", cmd_mr->va, cmd_mr->len, cmd_mr->mkey);

	lbuf = dpa_thread_alloc(tcb, cmd_mr->len);

	test_read_sync(q, lbuf, cmd_mr);

	test_write_sync(q, lbuf, cmd_mr);

	test_poll_tx(q, lbuf, cmd_mr);

	test_write_short_sync(q, cmd_mr);

	/* ping pong */
	test_ping_pong(q);

	printf("All done. Waiting for DPU command\n");
	snap_dpa_cmd_recv(dpa_mbox(tcb), SNAP_DPA_CMD_STOP);
	snap_dpa_rsp_send(dpa_mbox(tcb), SNAP_DPA_RSP_OK);
	printf("DMA test done. Exiting\n");
	return 0;
}
