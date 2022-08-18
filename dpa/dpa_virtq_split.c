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

static inline bool is_event_mode()
{
	return dpa_tcb()->user_flag == SNAP_DPA_RT_THR_EVENT;
}

static inline struct dpa_virtq *get_vq()
{
	/* will redo to be mt safe */
	struct dpa_rt_context *rt_ctx = dpa_rt_ctx();

	/* vq is always allocated after rt context */
	return (struct dpa_virtq *)SNAP_ALIGN_CEIL((uint64_t)(rt_ctx + 1), DPA_CACHE_LINE_BYTES);
}

static void dump_stats(struct dpa_virtq *vq)
{
	dpa_info("vq 0x%x#%d : sends %lu long_sends %lu delta_total %lu vq_heads %lu vq_tables %lu\n",
		vq->common.vhca_id, vq->common.idx,
		vq->stats.n_sends,
		vq->stats.n_long_sends,
		vq->stats.n_delta_total,
		vq->stats.n_vq_heads,
		vq->stats.n_vq_tables);
}

static inline void dpa_virtq_duar_arm()
{
	struct dpa_virtq *vq = get_vq();
	struct dpa_rt_context *rt_ctx = dpa_rt_ctx();

	/* todo: use always armed in event mode */
	if (is_event_mode())
		snap_dv_arm_cq(&rt_ctx->db_cq);

	dpa_duar_arm(vq->duar_id, rt_ctx->db_cq.cq_num);
}

static void dpa_virtq_write_rsp(struct dpa_virtq *vq)
{
	struct dpa_virtq_rsp *rsp;

	rsp = (struct dpa_virtq_rsp *)snap_dpa_mbox_to_rsp(dpa_mbox());

	rsp->vq_state.state = vq->state;
	rsp->vq_state.hw_available_index = vq->hw_available_index;
	rsp->vq_state.hw_used_index = vq->hw_used_index;
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
	dpa_info("vhca_id 0x%0x duar_id 0x%0x %s virtq create: %d size %d dpa_xmkey 0x%x dpu_xmkey 0x%x\n",
			vhca_id, vq->duar_id,
			is_event_mode() ? "event" : "polling",
			idx, vq->common.size, vq->dpa_xmkey, vq->dpu_xmkey);
	/* TODO: input validation/sanity check */

	/* allow queue creation in the rdy state, save 3-5usec on extra modify
	 */
	if (vq->state == DPA_VIRTQ_STATE_RDY) {
		vq->pending = 1;
		dpa_virtq_duar_arm();
	} else
		vq->state = DPA_VIRTQ_STATE_INIT;

	dpa_virtq_write_rsp(vq);
	return SNAP_DPA_RSP_OK;
}

int dpa_virtq_destroy(struct snap_dpa_cmd *cmd)
{
	struct dpa_virtq *vq = get_vq();

	dpa_info("0x%0x virtq destroy: %d hw_avail: %d\n", vq->common.vhca_id, vq->common.idx, vq->hw_available_index);
	dump_stats(vq);
	vq->state = DPA_VIRTQ_STATE_ERR;
	return SNAP_DPA_RSP_OK;
}

int dpa_virtq_modify(struct snap_dpa_cmd *cmd)
{
	struct dpa_virtq *vq = get_vq();
	struct dpa_rt_context *rt_ctx = dpa_rt_ctx();
	struct dpa_virtq_cmd *vcmd = (struct dpa_virtq_cmd *)cmd;
	enum dpa_virtq_state next_state = vcmd->cmd_modify.state;

	dpa_info("0x%0x virtq modify: %d state %d new_state %d\n", vq->common.vhca_id,
			vq->common.idx, vq->state, next_state);

	if (vq->state == next_state)
		return SNAP_DPA_RSP_OK;

	if (next_state == DPA_VIRTQ_STATE_ERR)
		goto done;

	switch (vq->state) {
		case DPA_VIRTQ_STATE_INIT:
			if (next_state != DPA_VIRTQ_STATE_RDY)
				return SNAP_DPA_RSP_ERR;
			/* there is a race between vq enable and doorbell. Basically driver
			 * can send doorbell before we armed it
			 */
			vq->pending = 1;
			dpa_virtq_duar_arm();
			break;

		case DPA_VIRTQ_STATE_RDY:
			if (next_state != DPA_VIRTQ_STATE_SUSPEND)
				goto done_bad_state;

			/*
			 * It is nice to guarantee that after suspend all outstanding
			 * tx to dpu are completed. But is it needed ?
			 */
			snap_dma_q_flush(rt_ctx->dpa_cmd_chan.dma_q);
			break;

		case DPA_VIRTQ_STATE_SUSPEND:
		case DPA_VIRTQ_STATE_ERR:
			if (next_state != DPA_VIRTQ_STATE_ERR)
				goto done_bad_state;
			break;
	}

done:
	vq->state = next_state;
	dpa_virtq_write_rsp(vq);
	return SNAP_DPA_RSP_OK;

done_bad_state:
	dpa_error("0x%0x bad state transition\n", vq->common.vhca_id);
	return SNAP_DPA_RSP_ERR;
}

int dpa_virtq_query(struct snap_dpa_cmd *cmd)
{
	struct dpa_virtq *vq = get_vq();

	dpa_info("0x%0x virtq query: %d\n", vq->common.vhca_id, vq->common.idx);
	dpa_virtq_write_rsp(vq);
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
	if (vq->state == DPA_VIRTQ_STATE_RDY)
		dpa_window_set_active_mkey(vq->dpa_xmkey);

	return 0;
}

static inline int process_commands(int *done)
{
	if (snap_likely(dpa_tcb()->counter++ % COMMAND_DELAY)) {
		return 0;
	}

	return do_command(done);
}

#define VIRTQ_DPA_NUM_P2P_MSGS 16
#define DPA_TABLE_THRESHOLD 4

static inline void virtq_progress()
{
	struct dpa_virtq *vq = get_vq();
	struct dpa_rt_context *rt_ctx;
	struct virtq_device_ring *avail_ring;
	uint16_t delta, host_avail_idx;
	struct mlx5_cqe64 *cqe;
	struct snap_dpa_p2p_msg *msgs[VIRTQ_DPA_NUM_P2P_MSGS];
	int i, n;
	//int cr_update;

	if (vq->state != DPA_VIRTQ_STATE_RDY)
		return;

	rt_ctx = dpa_rt_ctx();

	/* recv messages from DPU */
	//cr_update = 0;
	n = snap_dpa_p2p_recv_msg(&rt_ctx->dpa_cmd_chan, msgs, VIRTQ_DPA_NUM_P2P_MSGS);
	for (i = 0; i < n; i++) {
		rt_ctx->dpa_cmd_chan.credit_count += msgs[i]->base.credit_delta;
		if (msgs[i]->base.type == SNAP_DPA_P2P_MSG_CR_UPDATE) {
			//cr_update = 1;
			continue;
		}
		if (msgs[i]->base.type != SNAP_DPA_P2P_MSG_VQ_MSIX)
			continue;
		/* TODO: trigger msix, log bad messages */
	}

	if (n)
		dpa_debug("recv %d new messages\n", n);

#if 0
	/* todo: fix credit logic */
	if (cr_update) {
		int ret;

		ret = snap_dpa_p2p_send_cr_update(&rt_ctx->dpa_cmd_chan, n);
		/* should never fail, todo: move queue to fatal state */
		if (ret) {
			dpa_info("failed to send credit update\n");
			goto fatal_err;
		}
	}
#endif
	/* event mode: arm channel */

	/* we can collapse doorbells and just pick up last avail index,
	 * todo use 1 entry cq
	 */
	for (n = 0; n < SNAP_DPA_RT_THR_SINGLE_DB_CQE_CNT; n++) {
		cqe = snap_dv_poll_cq(&rt_ctx->db_cq, 64);
		if (!cqe)
			break;
		n++;
	}

	if (n == 0 && !vq->pending)
		return;

	vq->pending = 0;

	/* note: this is going to disable db batching, optimize */
	if (is_event_mode())
		snap_dma_q_arm(rt_ctx->dpa_cmd_chan.dma_q);

	dpa_virtq_duar_arm();

	/* todo: consider keeping window adjusted 'driver' address */
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
	 * - polling mode w/a is to check doorbell first before fetching avail
	 *   index
	 */
	host_avail_idx = avail_ring->idx;

	/* todo: unlikely */
	if (vq->hw_available_index == host_avail_idx)
		return;

	delta = host_avail_idx - vq->hw_available_index;
	/*
	if (delta < XX)
		send_desc_heads
	else
		send_desc_heads + table
		*/

	dpa_debug("==> New avail idx: %d delta %d\n", host_avail_idx, delta);

	vq->stats.n_delta_total += delta;

	if (delta < DPA_TABLE_THRESHOLD) {
		/* post send */
		n = snap_dpa_p2p_send_vq_heads(&rt_ctx->dpa_cmd_chan, vq->common.idx,
				vq->common.size,
				vq->hw_available_index, host_avail_idx, vq->common.driver,
				vq->dpu_xmkey);
		if (n <= 0) {
			/* todo: error handling if not EGAIN */
			dpa_info("error sending vq heads\n");
			// should not happen, atm qp is large enough to handle all tx
			goto fatal_err;
		}
		vq->stats.n_vq_heads++;
	} else {
		/* rdma_write 4k; post send */
		n = snap_dpa_p2p_send_vq_table(&rt_ctx->dpa_cmd_chan, vq->common.idx,
				vq->common.size,
				vq->hw_available_index, host_avail_idx, vq->common.driver,
				vq->dpu_xmkey,
				vq->common.desc, vq->dpu_desc_shadow_addr, vq->dpu_desc_shadow_mkey);
		if (n <= 0) {
			/* todo: error handling if not EGAIN */
			dpa_info("error sending vq table\n");
			// should not happen, atm qp is large enough to handle all tx
			goto fatal_err;
		}
		vq->stats.n_vq_tables++;
	}

	vq->stats.n_sends++;

	/* unroll, only 1 iteration is expected */
	if (snap_unlikely(n != delta)) {
again:
		vq->hw_available_index += n;
		if (delta < DPA_TABLE_THRESHOLD) {
			n = snap_dpa_p2p_send_vq_heads(&rt_ctx->dpa_cmd_chan, vq->common.idx,
					vq->common.size,
					vq->hw_available_index, host_avail_idx, vq->common.driver,
					vq->dpu_xmkey);
			vq->stats.n_vq_heads++;
		} else {
			n = snap_dpa_p2p_send_vq_table_cont(&rt_ctx->dpa_cmd_chan, vq->common.idx,
					vq->common.size,
					vq->hw_available_index, host_avail_idx, vq->common.driver,
					vq->dpu_xmkey);
			vq->stats.n_vq_tables++;
		}

		if (n <= 0) {
			/* todo: error handling if not EGAIN */
			dpa_error("error sending vq heads, err=%d, delta=%d, hw_avail=%d host_avail=%d\n",
					n, delta, vq->hw_available_index, host_avail_idx);
			// should not happen, atm qp is large enough to handle all tx
			goto fatal_err;
		}

		vq->stats.n_sends++;
		vq->stats.n_long_sends++;

		if ((uint16_t)(vq->hw_available_index + (uint16_t)n) != host_avail_idx) {
			goto again;
		}
	}

	dpa_debug("===> send vq heads done %d\n", n);
	vq->hw_available_index = host_avail_idx;

	/* kick off doorbells, pickup completions */
	rt_ctx->dpa_cmd_chan.dma_q->ops->progress_tx(rt_ctx->dpa_cmd_chan.dma_q);
	return;

fatal_err:
	/* todo: add logic */
	dpa_debug("FATAL processing error, disabling vq\n");
	vq->state = DPA_VIRTQ_STATE_ERR;
	return;
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

static void dpa_run_polling()
{
	int done = 0;

	dpa_rt_start();

	do {
		process_commands(&done);
		virtq_progress();
	} while (!done);
}

static inline void dpa_run_event()
{
	struct snap_dpa_tcb *tcb = dpa_tcb();
	int done;

	/* may be add post init ?? */
	if (snap_unlikely(tcb->init_done == 1)) {
		dpa_rt_start();
		tcb->init_done = 2;
	}

	do_command(&done);
	virtq_progress();
}

int dpa_run()
{
	if (snap_likely(is_event_mode())) {
		dpa_run_event();
	} else {
		dpa_run_polling();
	}
	return 0;
}
