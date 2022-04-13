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
#include "snap_dma_internal.h"
#include "snap_dpa_rt.h"
#include "snap_dpa_virtq.h"

/**
 * Single virtio/nvme queue per thread implementation. The thread can be
 * either in polling or in event mode
 */


/* currently set so that we have 1s polling interval on simx */
#if SIMX_BUILD
#define COMMAND_DELAY 10000
#else
#define COMMAND_DELAY 100000
#endif

static inline struct dpa_virtq *get_vq()
{
	/* will redo to be mt safe */
	struct dpa_rt_context *rt_ctx = dpa_rt_ctx();

	/* vq is always allocated after rt context */
	return (struct dpa_virtq *)SNAP_ALIGN_CEIL((uint64_t)(rt_ctx + 1), DPA_CACHE_LINE_BYTES);
}

int dpa_virtq_create(struct snap_dpa_cmd *cmd)
{
	struct dpa_virtq_cmd *vcmd = (struct dpa_virtq_cmd *)cmd;
	struct dpa_virtq *vq = get_vq();
	uint16_t idx;
	uint16_t vhca_id;

	memcpy(vq, &vcmd->cmd_create.vq, sizeof(vcmd->cmd_create.vq));

	idx = vcmd->cmd_create.vq.common.idx;
	vhca_id = vcmd->cmd_create.vq.common.vhca_id;
	dpa_info("0x%0x virtq create: %d host_mkey 0x%x\n", vhca_id, idx, vq->host_mkey);
	//dpa_window_set_active_mkey(vq->host_mkey);
	//dpa_debug("set active mkey 0x%x\n", vq->host_mkey);

	/* TODO: input validation/sanity check */
	vq->enabled = 1;
	return SNAP_DPA_RSP_OK;
}

int dpa_virtq_destroy(struct snap_dpa_cmd *cmd)
{
	struct dpa_virtq *vq = get_vq();

	//dpa_window_set_active_mkey(dpa_tcb()->mbox_lkey);
	dpa_info("0x%0x virtq destroy: %d hw_avail: %d\n", vq->common.vhca_id, vq->common.idx, vq->hw_available_index);
	vq->enabled = 0;
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
	struct dpa_virtq *vq = get_vq();
	struct mlx5_cqe64 *cqe;
	struct snap_dpa_cmd *cmd;
	uint32_t rsp_status;

	cqe = snap_dv_poll_cq(&tcb->cmd_cq, 64);
	if (!cqe)
		return 0;

	/**
	 * Set mbox key as active because logger macros will restore
	 * current active key. It will lead to the crash if cmd is
	 * accessed after the dpa_debug and friends
	 */
	dpa_window_set_active_mkey(tcb->mbox_lkey);
	cmd = snap_dpa_mbox_to_cmd(dpa_mbox());

	if (snap_likely(cmd->sn == tcb->cmd_last_sn))
		goto cmd_done;

	dpa_debug("sn %d: new command 0x%x\n", cmd->sn, cmd->cmd);

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
			dpa_warn("unsupported command %d\n", cmd->cmd);
	}

	dpa_debug("sn %d: done command 0x%x status %d\n", cmd->sn, cmd->cmd, rsp_status);
	snap_dpa_rsp_send(dpa_mbox(), rsp_status);
cmd_done:
	if (vq->enabled)
		dpa_window_set_active_mkey(vq->host_mkey);

	return 0;
}

static inline int process_commands(int *done)
{
	if (snap_likely(dpa_tcb()->counter++ % COMMAND_DELAY)) {
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
	if (count++ % COMMAND_DELAY)
		return;
#endif

	/* load avail index */
	if (!vq->enabled)
		return;

	avail_ring = (void *)dpa_window_get_base() + vq->common.driver;
	/* use load fence (i) ? */
	snap_memory_bus_fence();
	/* NOTE: window mapping is going to be invalidated on controller reset
	 * flr etc. It means that there is a chance that thread will be
	 * reading available index from the invalid window.
	 * Currently it will cause hart crash.
	 *
	 * It means that:
	 * - we cannot really work in the polling mode, most probably we will
	 *   be here when controller is reset
	 * - in doorbell mode we still can be here, but not in a 'good' flow.
	 *   bad flow still can happen
	 */
	host_avail_idx = avail_ring->idx;

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
	if (vq != get_vq())
		dpa_fatal("vq must follow rt context: vq@%p expected@%p\n", vq, get_vq());

	dpa_debug("VirtQ init done! vq@%p\n", vq);
	return 0;
}

int dpa_run()
{
	struct snap_dpa_tcb *tcb = dpa_tcb();
	int done;
	int ret;
	struct snap_dpa_cmd *cmd;

	dpa_rt_start();
	cmd = dpa_mbox();
	tcb->cmd_last_sn = cmd->sn;
	done = 0;

	do {
		ret = process_commands(&done);
		virtq_progress();
	} while (!done);

	dpa_debug("virtq_split done\n");

	return ret;
}
