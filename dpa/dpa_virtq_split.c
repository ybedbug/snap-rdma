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
#include <string.h>

#include "dpa.h"
#include "snap_macros.h"
#include "snap_dma.h"
#include "snap_dpa_rt.h"
#include "snap_dpa_virtq.h"

/**
 * Single virtio/nvme queue per thread implementation. The thread can be
 * either in polling or in event mode
 */


/* currently set so that we have 1s polling interval on simx */
#define COMMAND_DELAY 10000

static inline struct dpa_virtq *get_vq()
{
	/* will redo to be mt safe */
	struct dpa_rt_context *rt_ctx = dpa_rt_ctx();

	/* vq is always allocated after rt context */
	return (struct dpa_virtq *)(rt_ctx + 1);
}

int dpa_virtq_create(struct snap_dpa_cmd *cmd)
{
	struct dpa_virtq_cmd *vcmd = (struct dpa_virtq_cmd *)cmd;
	struct dpa_virtq *vq = get_vq();
	uint16_t idx;
	uint16_t vhca_id;

	idx = vcmd->cmd_create.vq.common.idx;
	vhca_id = vcmd->cmd_create.vq.common.vhca_id;
	dpa_info("0x%0x virtq create: %d\n", vhca_id, idx);

	/* TODO: input validation/sanity check */
	memcpy(vq, &vcmd->cmd_create.vq, sizeof(vcmd->cmd_create.vq));
	return SNAP_DPA_RSP_OK;
}

int dpa_virtq_destroy(struct snap_dpa_cmd *cmd)
{
	struct dpa_virtq *vq = get_vq();

	dpa_info("0x%0x virtq destroy: %d\n", vq->common.vhca_id, vq->common.idx);
	return SNAP_DPA_RSP_OK;
}

int dpa_virtq_modify(struct snap_dpa_cmd *cmd)
{
	struct dpa_virtq *vq = get_vq();

	dpa_info("0x%0x virtq modify: %d\n", vq->common.vhca_id, vq->common.idx);
	return SNAP_DPA_RSP_OK;
}

int dpa_virtq_query(struct snap_dpa_cmd *cmd)
{
	struct dpa_virtq *vq = get_vq();

	dpa_info("0x%0x virtq modify: %d\n", vq->common.vhca_id, vq->common.idx);
	return SNAP_DPA_RSP_OK;
}

static int do_command(int *done)
{
	struct snap_dpa_tcb *tcb = dpa_tcb();

	struct snap_dpa_cmd *cmd;
	uint32_t rsp_status;

	*done = 0;
	dpa_debug("command check\n");

	cmd = snap_dpa_mbox_to_cmd(dpa_mbox());

	if (snap_likely(cmd->sn == tcb->cmd_last_sn))
		return 0;

	dpa_debug("new command", cmd->cmd);

	tcb->cmd_last_sn = cmd->sn;
	rsp_status = SNAP_DPA_RSP_OK;

	switch (cmd->cmd) {
		case SNAP_DPA_CMD_STOP:
			*done = 1;
			break;
		case DPA_VIRTQ_CMD_CREATE:
			rsp_status = dpa_virtq_create(cmd);
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

	snap_dpa_rsp_send(dpa_mbox(), rsp_status);
	return 0;
}

static inline int process_commands(int *done)
{
	if (snap_likely(dpa_tcb()->counter++ % COMMAND_DELAY)) {
		*done = 0;
		return 0;
	}

	return do_command(done);
}

static inline void virtq_progress()
{
	uint16_t host_avail_idx;
	struct dpa_virtq *vq = get_vq();
	struct virtq_device_ring *avail_ring;

	// hack to slow thing down on simx
#if SIMX_BUILD
	static unsigned count;
	if (count++ % COMMAND_DELAY)
		return;
#endif

	/* load avail index */
	dpa_window_set_mkey(vq->host_mkey); // TODO: only need to set once
	avail_ring = (void *)dpa_window_get_base() + vq->common.device;
	host_avail_idx = avail_ring->idx;
	//dpa_debug("vq->dpa_avail_idx: %d\n", vq->dpa_avail_idx);
	if (vq->hw_available_index == host_avail_idx)
		return;

	dpa_debug("==> New avail idx: %d\n", host_avail_idx);

	/* add actual processing logic */
	vq->hw_available_index = host_avail_idx;
}

int dpa_init()
{
	struct dpa_virtq *vq;

	dpa_rt_init();

	vq = dpa_thread_alloc(sizeof(*vq));
	if ((void *)vq != (void *)(dpa_rt_ctx() + 1))
		dpa_fatal("vq must follow rt context\n");

	dpa_debug("VirtQ init done!\n");
	return 0;
}

int dpa_run()
{
	int done;
	int ret;

	dpa_rt_start();

	do {
		ret = process_commands(&done);
		virtq_progress();
	} while (!done);

	dpa_debug("virtq_split done\n");

	return ret;
}
