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

#include <stddef.h>

#include "dpa.h"
#include "snap_dpa_virtq.h"

/* At the moment we only run one thread with one virtq slot
 * TODO:
 * - multiple threads, per thread storage
 */
static int n_virtqs; //per thread

/* TODO: optimize field alignment */
struct dpa_virtq {
	// ro section
	uint16_t idx;
	uint16_t size;
	uint64_t desc;
	uint64_t driver;
	uint64_t device;

	uint32_t dpu_avail_mkey;
	// copy avail index there
	uint64_t dpu_avail_ring_addr;

	// a key that can be used to access host memory
	uint32_t host_mkey;

	//rw
	uint16_t dpa_avail_idx;
};

#define DPA_VIRTQ_MAX 8
static struct dpa_virtq *virtqs[DPA_VIRTQ_MAX]; // per thread

/* currently set so that we have 1s polling interval on simx */
#define COMMAND_DELAY 10000

int dpa_virtq_create(struct snap_dpa_tcb *tcb, struct snap_dpa_cmd *cmd)
{
	struct dpa_virtq_cmd *vcmd = (struct dpa_virtq_cmd *)cmd;
	struct dpa_virtq *vq;
	uint16_t idx;

	idx = vcmd->cmd_create.idx;
	dpa_info("virtq create: id: %d\n", idx);
	if (idx >= DPA_VIRTQ_MAX) {
		dpa_error("invalid vq number %d\n", idx);
		return SNAP_DPA_RSP_ERR;
	}

	vq = (struct dpa_virtq *) dpa_thread_alloc(tcb, sizeof(struct dpa_virtq));
	virtqs[idx] = vq;
	/* TODO: check that it is free and other validations */

	vq->idx = idx;
	vq->size = vcmd->cmd_create.size;
	vq->desc = vcmd->cmd_create.desc;
	vq->driver = vcmd->cmd_create.driver;
	vq->device = vcmd->cmd_create.device;
	vq->dpu_avail_mkey = vcmd->cmd_create.dpu_avail_mkey;
	vq->dpu_avail_ring_addr = vcmd->cmd_create.dpu_avail_ring_addr;
	vq->host_mkey = vcmd->cmd_create.host_mkey;

	vq->dpa_avail_idx = 0;

	n_virtqs++;
	return SNAP_DPA_RSP_OK;
}

int dpa_virtq_destroy(struct snap_dpa_cmd *cmd)
{
	dpa_info("virtq destroy\n");
	n_virtqs--;
	return SNAP_DPA_RSP_OK;
}

int dpa_virtq_modify(struct snap_dpa_cmd *cmd)
{
	dpa_info("virtq modify\n");
	return SNAP_DPA_RSP_OK;
}

int dpa_virtq_query(struct snap_dpa_cmd *cmd)
{
	dpa_info("virtq query\n");
	return SNAP_DPA_RSP_OK;
}

static int do_command(struct snap_dpa_tcb *tcb, int *done)
{
	static uint32_t last_sn; // per thread

	struct snap_dpa_cmd *cmd;
	uint32_t rsp_status;

	*done = 0;
	dpa_debug("command check\n");

	cmd = snap_dpa_mbox_to_cmd(dpa_mbox(tcb));

	if (cmd->sn == last_sn)
		return 0;

	dpa_debug("new command", cmd->cmd);

	last_sn = cmd->sn;
	rsp_status = SNAP_DPA_RSP_OK;

	switch (cmd->cmd) {
		case SNAP_DPA_CMD_STOP:
			*done = 1;
			break;
		case DPA_VIRTQ_CMD_CREATE:
			rsp_status = dpa_virtq_create(tcb, cmd);
			break;
		case DPA_VIRTQ_CMD_DESTROY:
			rsp_status = dpa_virtq_destroy(cmd);
			break;
		case DPA_VIRTQ_CMD_MODIFY:
			rsp_status = dpa_virtq_modify(cmd);
			break;
		case DPA_VIRTQ_CMD_QUERY:
			rsp_status = dpa_virtq_query(cmd);
			break;
		default:
			dpa_warn("unsupported command\n");
	}

	snap_dpa_rsp_send(dpa_mbox(tcb), rsp_status);
	return 0;
}

static inline int process_commands(struct snap_dpa_tcb *tcb, int *done)
{
	static unsigned count; //per thread

	if (count++ % COMMAND_DELAY) { // TODO: mark unlikely
		*done = 0;
		return 0;
	}

	return do_command(tcb, done);
}

static inline void virtq_progress()
{
	int i;
	uint16_t host_avail_idx;
	struct dpa_virtq *vq;
	struct virtq_device_ring *avail_ring;

	// hack to slow thing down on simx
	static unsigned count;
	if (count++ % COMMAND_DELAY)
		return;

	for (i = 0; i < n_virtqs; i++) {
		vq = virtqs[i];

		/* load avail index */
		dpa_window_set_mkey(vq->host_mkey);
		avail_ring = (void *)dpa_window_get_base() + vq->device;
		host_avail_idx = avail_ring->idx;
		dpa_debug("vq->dpa_avail_idx: %d\n", vq->dpa_avail_idx);
		if (vq->dpa_avail_idx == host_avail_idx)
			continue;

		dpa_debug("==> New avail idx: %d\n", host_avail_idx);

		vq->dpa_avail_idx = host_avail_idx;

		/* copy avail to dpu */
		dpa_window_set_mkey(vq->dpu_avail_mkey);
		avail_ring = (void *)dpa_window_get_base() + vq->dpu_avail_ring_addr;
		avail_ring->idx = host_avail_idx;
	}
}

int dpa_init(struct snap_dpa_tcb *tcb)
{
	dpa_debug("VirtQ init done!\n");
	return 0;
}

int dpa_run(struct snap_dpa_tcb *tcb)
{
	int done;
	int ret;

	do {
		ret = process_commands(tcb, &done);
		virtq_progress();
	} while (!done);

	//snap_dpa_cmd_recv(dpa_mbox(), SNAP_DPA_CMD_STOP);
	dpa_debug("virtq_split done\n");

	return ret;
}
