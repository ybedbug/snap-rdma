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

#include <linux/virtio_pci.h>
#include "snap_macros.h"
#include "snap_vq_internal.h"
#include "snap_virtio_common.h"
#include "snap_env.h"
#include "snap_buf.h"
#include "snap_queue.h"
#include "snap.h"
#include "snap_dma.h"
#include "snap_vq_prm.h"


#define SNAP_VQ_NO_MSIX (0xffff)

enum snap_vq_hwq_modify {
	SNAP_VQ_HWQ_MOD_STATE	= 1 << 0,
};

static void snap_vq_cmd_process(struct snap_vq_cmd *cmd);

static void snap_vq_cmd_prefetch_header(struct snap_vq_cmd *cmd)
{
	if (snap_likely(cmd->vq->cmd_ops->prefetch))
		cmd->vq->cmd_ops->prefetch(cmd);
}

static void snap_vq_cmd_handle(struct snap_vq_cmd *cmd)
{
	cmd->vq->cmd_ops->handle(cmd);
}

static inline struct snap_vq_cmd_desc *
snap_vq_desc_pool_get(struct snap_vq *q)
{
	struct snap_vq_cmd_desc *desc;

	desc = TAILQ_FIRST(&q->desc_pool.free_descs);
	TAILQ_REMOVE(&q->desc_pool.free_descs, desc, entry);

	return desc;
}

static inline void snap_vq_desc_pool_put(struct snap_vq *q,
					struct snap_vq_cmd_desc *desc)
{
	TAILQ_INSERT_HEAD(&q->desc_pool.free_descs, desc, entry);
}

static inline struct snap_vq_cmd_desc *snap_vq_cmd_desc_get(struct snap_vq_cmd *cmd)
{
	struct snap_vq_cmd_desc *desc;

	desc = snap_vq_desc_pool_get(cmd->vq);
	TAILQ_INSERT_TAIL(&cmd->descs, desc, entry);
	cmd->num_descs++;

	return desc;
}

static inline void snap_vq_cmd_desc_put(struct snap_vq_cmd *cmd)
{
	struct snap_vq_cmd_desc *desc;

	cmd->num_descs--;
	desc = TAILQ_FIRST(&cmd->descs);
	TAILQ_REMOVE(&cmd->descs, desc, entry);
	snap_vq_desc_pool_put(cmd->vq, desc);
}

static void snap_vq_cmd_fetch_next_desc_done(struct snap_dma_completion *self,
						int status)
{
	struct snap_vq_cmd *cmd;

	cmd = container_of(self, struct snap_vq_cmd, dma_comp);

	if (snap_likely(status == IBV_WC_SUCCESS))
		snap_vq_cmd_process(cmd);
	else
		snap_vq_cmd_fatal(cmd);
}

static void snap_vq_cmd_fetch_next_desc(struct snap_vq_cmd *cmd)
{
	struct snap_vq_cmd_desc *last;
	struct snap_vq_cmd_desc *next;
	uint64_t next_addr;
	int ret;

	last = TAILQ_LAST(&cmd->descs, snap_vq_cmd_desc_list);
	next = snap_vq_cmd_desc_get(cmd);
	next_addr = cmd->vq->desc_pa + last->desc.next * sizeof(next->desc);
	cmd->dma_comp.count = 1;
	cmd->dma_comp.func = snap_vq_cmd_fetch_next_desc_done;
	ret = snap_dma_q_read(cmd->vq->dma_q, &next->desc,
			sizeof(struct vring_desc), cmd->vq->desc_pool.lkey,
			next_addr, cmd->vq->xmkey, &cmd->dma_comp);
	if (ret)
		snap_vq_cmd_fatal(cmd);
}

static void snap_vq_cmd_process(struct snap_vq_cmd *cmd)
{
	struct snap_vq_cmd_desc *desc;

	desc = TAILQ_LAST(&cmd->descs, snap_vq_cmd_desc_list);
	if (desc->desc.flags & VRING_DESC_F_NEXT)
		snap_vq_cmd_fetch_next_desc(cmd);
	else
		snap_vq_cmd_handle(cmd);
}

static void snap_vq_cmd_cleanup(struct snap_vq_cmd *cmd)
{
	while (!TAILQ_EMPTY(&cmd->descs))
		snap_vq_cmd_desc_put(cmd);
}

void snap_vq_cmd_dma_rw_done(struct snap_dma_completion *self, int status)
{
	struct snap_vq_cmd *cmd;

	cmd = container_of(self, struct snap_vq_cmd, dma_comp);
	cmd->done_cb(cmd, status);
}

static inline struct snap_vq_cmd *snap_vq_cmd_get(struct snap_vq *q)
{
	struct snap_vq_cmd *cmd;

	assert_debug(q != NULL);
	cmd = TAILQ_FIRST(&q->free_cmds);
	assert_debug(cmd != NULL);
	TAILQ_REMOVE(&q->free_cmds, cmd, entry);
	TAILQ_INSERT_HEAD(&q->inflight_cmds, cmd, entry);

	return cmd;
}

static inline void snap_vq_cmd_put(struct snap_vq *q, struct snap_vq_cmd *cmd)
{
	TAILQ_REMOVE(&q->inflight_cmds, cmd, entry);
	TAILQ_INSERT_HEAD(&q->free_cmds, cmd, entry);
}

static void snap_vq_cmd_start(struct snap_vq *q, const struct snap_vq_header *hdr)
{
	int i;
	struct snap_vq_cmd_desc *desc;
	struct snap_vq_cmd *cmd;

	cmd = snap_vq_cmd_get(q);
	cmd->id = hdr->desc_head_idx;
	cmd->len = 0;

	for (i = 0; i < hdr->num_descs; i++) {
		desc = snap_vq_cmd_desc_get(cmd);
		/* TODO avoid memcpy */
		memcpy(&desc->desc, &hdr->descs[i], sizeof(desc->desc));

		if (!(desc->desc.flags & VRING_DESC_F_NEXT))
			break;
	}

	snap_vq_cmd_prefetch_header(cmd);
	snap_vq_cmd_process(cmd);
}

int snap_vq_cmd_descs_rw(struct snap_vq_cmd *cmd,
		const struct snap_vq_cmd_desc *first_desc, size_t first_offset,
		void *lbuf, size_t total_len, uint32_t lbuf_mkey,
		snap_vq_cmd_done_cb_t done_cb, bool write)
{
	int ret = 0;
	char *laddr;
	const struct snap_vq_cmd_desc *desc;
	size_t offset, len, desc_len;
	uint64_t raddr;

	cmd->done_cb = done_cb;
	cmd->dma_comp.func = snap_vq_cmd_dma_rw_done;

	desc = first_desc;
	offset = first_offset;
	laddr = lbuf;
	while (total_len > 0 && desc) {
		desc_len = desc->desc.len - offset;
		len = snap_min(total_len, desc_len);
		raddr = desc->desc.addr + offset;

		/*
		 * Since we always process queue in a single thread, it is
		 * alright to gradually increase count - we cannot race with
		 * completion callbacks.
		 */
		cmd->dma_comp.count++;
		if (write) {
			ret = snap_dma_q_write(cmd->vq->dma_q,
				(void *)laddr, len, lbuf_mkey,
				raddr, cmd->vq->xmkey, &cmd->dma_comp);
			if (!ret)
				cmd->len += len;
		} else {
			ret = snap_dma_q_read(cmd->vq->dma_q,
				(void *)laddr, len, lbuf_mkey,
				raddr, cmd->vq->xmkey, &cmd->dma_comp);
		}
		if (snap_unlikely(ret))
			/* TODO add -EBUSY handling, pending list */
			return ret;

		laddr += len;
		total_len -= len;
		desc = TAILQ_NEXT(desc, entry);
		offset = 0;
	};

	return ret;
}

static void snap_vq_cmd_complete_execute(struct snap_vq_cmd *cmd)
{
	struct snap_vq_completion comp = {};
	int ret;

	comp.id = cmd->id;
	comp.len = cmd->len;
	ret = snap_dma_q_send_completion(cmd->vq->dma_q, &comp, sizeof(comp));
	if (snap_unlikely(ret))
		snap_vq_cmd_fatal(cmd);
	else
		snap_vq_cmd_cleanup(cmd);
	snap_vq_cmd_put(cmd->vq, cmd);
}

static void snap_vq_flush_pending_completions(struct snap_vq *q)
{
	struct snap_vq_cmd *cmd;

	while (!TAILQ_EMPTY(&q->inflight_cmds)) {
		cmd = TAILQ_LAST(&q->inflight_cmds, snap_vq_inflight_cmds);
		if (!cmd->pending_completion)
			break;

		cmd->pending_completion = false;
		snap_vq_cmd_complete_execute(cmd);
	}
}

void snap_vq_cmd_complete(struct snap_vq_cmd *cmd)
{
	if (cmd->vq->op_flags & SNAP_VQ_OP_FLAGS_IN_ORDER_COMPLETIONS) {
		/* inflight_cmds can implicitly enforce in-order completions */
		cmd->pending_completion = true;
		return snap_vq_flush_pending_completions(cmd->vq);
	} else {
		return snap_vq_cmd_complete_execute(cmd);
	}
}

void snap_vq_cmd_fatal(struct snap_vq_cmd *cmd)
{
	/* TODO add pending list handling */
	TAILQ_REMOVE(&cmd->vq->inflight_cmds, cmd, entry);
	TAILQ_INSERT_HEAD(&cmd->vq->fatal_cmds, cmd, entry);
	snap_error("Request %p entered fatal state and cannot be completed\n",
			cmd);
}

void snap_vq_cmd_create(struct snap_vq *q, struct snap_vq_cmd *cmd)
{
	cmd->vq = q;
	TAILQ_INIT(&cmd->descs);
}

void snap_vq_cmd_destroy(struct snap_vq_cmd *cmd)
{
}

static void snap_vq_new_cmd(struct snap_dma_q *dma_q, const void *data,
			uint32_t data_len, uint32_t imm_data)
{
	struct snap_vq *q = dma_q->uctx;
	const struct snap_vq_header *hdr = data;

	return snap_vq_cmd_start(q, hdr);
}

static int snap_vq_dma_q_create(struct snap_vq *q,
			const struct snap_vq_create_attr *attr,
			const struct snap_vq_cmd_ops *ops)
{
	struct snap_dma_q_create_attr dma_attr = {};

	dma_attr.tx_qsize = attr->size;
	dma_attr.tx_elem_size = snap_max(ops->ftr_size,
					 sizeof(struct snap_vq_completion));
	dma_attr.rx_qsize = attr->size;
	dma_attr.rx_elem_size = sizeof(struct snap_vq_header) +
		attr->caps->max_tunnel_desc * sizeof(struct vring_desc);
	dma_attr.uctx = q;
	dma_attr.rx_cb = snap_vq_new_cmd;
	dma_attr.mode = snap_env_getenv(SNAP_DMA_Q_OPMODE);
	dma_attr.comp_channel = attr->comp_channel;
	dma_attr.comp_vector = attr->comp_vector;
	dma_attr.comp_context = q;
	q->dma_q = snap_dma_q_create(attr->pd, &dma_attr);
	if (!q->dma_q)
		return -EINVAL;

	if (attr->comp_channel) {
		q->comp_channel = attr->comp_channel;
		(void)snap_dma_q_arm(q->dma_q);
	}

	return 0;
}

static void snap_vq_dma_q_destroy(struct snap_vq *q)
{
	snap_dma_q_destroy(q->dma_q);
}

static void snap_vq_cmds_destroy(struct snap_vq *q)
{
	struct snap_vq_cmd *cmd;

	assert_debug(TAILQ_EMPTY(&q->inflight_cmds));

	while (!TAILQ_EMPTY(&q->free_cmds)) {
		cmd = TAILQ_FIRST(&q->free_cmds);
		TAILQ_REMOVE(&q->free_cmds, cmd, entry);
		q->cmd_ops->delete(cmd);
	}
}

static int snap_vq_cmds_create(struct snap_vq *q, size_t num_cmds,
				const struct snap_vq_cmd_ops *ops)
{
	int i;
	struct snap_vq_cmd *cmd;

	q->cmd_ops = ops;
	TAILQ_INIT(&q->free_cmds);
	TAILQ_INIT(&q->inflight_cmds);
	TAILQ_INIT(&q->fatal_cmds);
	for (i = 0; i < num_cmds; i++) {
		cmd = ops->create(q, i);
		if (!cmd)
			goto destroy_cmds;
		TAILQ_INSERT_TAIL(&q->free_cmds, cmd, entry);
	}

	return 0;

destroy_cmds:
	snap_vq_cmds_destroy(q);
	return -EINVAL;
}

static int snap_vq_descs_create(struct snap_vq *q,
			const struct snap_vq_create_attr *attr)
{
	int i;
	struct snap_vq_desc_pool *pool = &q->desc_pool;

	pool->entries = snap_buf_alloc(attr->pd,
					attr->size * sizeof(*pool->entries));
	if (!pool->entries)
		return -ENOMEM;

	/* TODO consider more advanced poo impl, e.x. DPDK */
	pool->lkey = snap_buf_get_mkey(pool->entries);
	TAILQ_INIT(&pool->free_descs);
	for (i = 0; i < attr->size; i++)
		snap_vq_desc_pool_put(q, &pool->entries[i]);

	return 0;
}

static void snap_vq_descs_destroy(struct snap_vq_desc_pool *pool)
{
	snap_buf_free(pool->entries);
}

static int snap_vq_hwq_modify_state(struct snap_vq *q, enum snap_virtq_state state)
{
	struct snap_virtio_queue *hw_q = q->hw_q;
	struct snap_virtio_common_queue_attr attr = {};
	int ret;

	if (!hw_q->mod_allowed_mask) {
		ret = snap_virtio_get_mod_fields_queue(hw_q);
		if (ret) {
			snap_error("Failed to query queue mod fields\n");
			return ret;
		}
	}

	attr.vattr.state = state;
	/* The mask we are using is set as a generic mask for modifying state
	 * instead of using the existing masks SNAP_VIRTIO_BLK/NET/FS_QUEUE_MOD_STATE
	 * for each type. We can use this mask because the typed MOD_STATE have
	 * the same value.
	 */
	ret = snap_virtio_modify_queue(hw_q, SNAP_VQ_HWQ_MOD_STATE, &attr.vattr);
	if (ret) {
		snap_error("Failed to modify snap_vq hw_q\n");
		return ret;
	}

	return 0;
}

static int snap_vq_hwq_create(struct snap_vq *q, struct snap_dma_q *dma_q,
				const struct snap_vq_create_attr *qattr)
{

	struct snap_virtio_queue_attr hw_qattr = {};
	struct snap_virtio_queue *hw_q;
	struct ibv_qp *qp = snap_dma_q_get_fw_qp(dma_q);
	int ret;

	hw_q = calloc(1, sizeof(*hw_q));
	if (!hw_q) {
		ret = -ENOMEM;
		goto err;
	}

	hw_q->ctrs_obj = snap_virtio_create_queue_counters(qattr->sdev);
	if (!hw_q->ctrs_obj) {
		ret = -ENODEV;
		goto free_hwq;
	}

	hw_qattr.type = SNAP_VIRTQ_SPLIT_MODE;
	hw_qattr.offload_type = SNAP_VIRTQ_OFFLOAD_DESC_TUNNEL;
	hw_qattr.full_emulation = true;
	hw_qattr.idx = qattr->index;
	hw_qattr.size = qattr->size;
	hw_qattr.desc = qattr->desc_pa;
	hw_qattr.driver = qattr->driver_pa;
	hw_qattr.device = qattr->device_pa;
	hw_qattr.hw_available_index = qattr->hw_avail_index;
	hw_qattr.hw_used_index = qattr->hw_used_index;
	hw_qattr.event_qpn_or_msix = qattr->msix_vector;
	if (qp) {
		hw_qattr.tisn_or_qpn = qp->qp_num;
		hw_qattr.vhca_id = snap_get_dev_vhca_id(qp->context);
	}
	hw_qattr.max_tunnel_desc = qattr->caps->max_tunnel_desc;
	hw_qattr.pd = qattr->pd;
	hw_qattr.ev_mode = (qattr->msix_vector == SNAP_VQ_NO_MSIX) ?
				SNAP_VIRTQ_NO_MSIX_MODE : SNAP_VIRTQ_MSIX_MODE;
	hw_qattr.virtio_version_1_0 = 1;
	hw_qattr.queue_period_mode = 0;
	hw_qattr.queue_period = 0;
	hw_qattr.queue_max_count = 0;
	ret = snap_virtio_create_hw_queue(qattr->sdev, hw_q, qattr->caps,
					  &hw_qattr);
	if (ret)
		goto free_ctrs;

	q->hw_q = hw_q;
	ret = snap_vq_hwq_modify_state(q, SNAP_VIRTQ_STATE_RDY);
	if (ret)
		goto free_ctrs;

	return 0;

free_ctrs:
	snap_devx_obj_destroy(hw_q->ctrs_obj);
free_hwq:
	free(hw_q);
err:
	return ret;
}

static void snap_vq_hwq_destroy(struct snap_vq *q)
{
	snap_virtio_destroy_hw_queue(q->hw_q);
	snap_devx_obj_destroy(q->hw_q->ctrs_obj);
	free(q->hw_q);
	q->hw_q = NULL;
}


int snap_vq_create(struct snap_vq *q, struct snap_vq_create_attr *attr,
			const struct snap_vq_cmd_ops *cmd_ops)
{
	uint16_t hw_used;

	if (snap_vq_dma_q_create(q, attr, cmd_ops))
		goto err;

	if (attr->in_recovery) {
		if (snap_virtio_get_used_index_from_host(q->dma_q,
					attr->device_pa, attr->xmkey, &hw_used))
			goto destroy_dma_q;
	} else {
		hw_used = 0;
	}

	attr->hw_avail_index = hw_used;
	attr->hw_used_index = hw_used;

	if (snap_vq_cmds_create(q, attr->size, cmd_ops))
		goto destroy_dma_q;

	if (snap_vq_descs_create(q, attr))
		goto destroy_cmds;

	if (snap_vq_hwq_create(q, q->dma_q, attr))
		goto destroy_descs;

	q->index = attr->index;
	q->state = SNAP_VQ_STATE_RUNNING;
	q->size = attr->size;
	q->desc_pa = attr->desc_pa;
	q->driver_pa = attr->driver_pa;
	q->device_pa = attr->device_pa;
	q->op_flags = attr->op_flags;
	q->xmkey = attr->xmkey;
	q->caps = attr->caps;
	q->vctrl = attr->vctrl;
	return 0;

destroy_descs:
	snap_vq_descs_destroy(&q->desc_pool);
destroy_cmds:
	snap_vq_cmds_destroy(q);
destroy_dma_q:
	snap_vq_dma_q_destroy(q);
err:
	return -EINVAL;
}

void snap_vq_destroy(struct snap_vq *q)
{
	snap_vq_hwq_destroy(q);
	snap_vq_descs_destroy(&q->desc_pool);
	snap_vq_cmds_destroy(q);
	snap_vq_dma_q_destroy(q);
}

/**
 * snap_vq_cmd_get_descs() - Get snap virtio command's descriptor list
 *
 * Get snap virtio command's descriptor list. Should be called during
 * command processing stage to get the list of descriptors describing
 * the command.
 *
 * Return: command's descriptor list.
 */
inline const struct snap_vq_cmd_desc_list *
snap_vq_cmd_get_descs(struct snap_vq_cmd *cmd)
{
	return &cmd->descs;
}

/**
 * snap_vq_handle_events() - Handle queue events
 * @q: queue
 *
 * Listen to queue events (e.g. commands waiting to be processed),
 * and handle them when they arrive.
 *
 * Context: The function should be used in events mode, as it
 *          blocks until a new event is detected.
 */
int snap_vq_handle_events(struct snap_vq *q)
{
	struct ibv_cq *cq;
	void *cq_context;
	int n, ret;

	ret = ibv_get_cq_event(q->comp_channel, &cq, &cq_context);
	if (ret) {
		snap_info("Failed to get CQ event\n");
		return -errno;
	}

	ibv_ack_cq_events(cq, 1);
	ret = snap_dma_q_arm(q->dma_q);
	if (ret) {
		snap_error("Couldn't request CQ notification\n");
		return -errno;
	}

	do {
		n = snap_dma_q_progress(q->dma_q);
	} while (n > 0);

	return 0;
}

/**
 * snap_vq_suspend() - Suspends virtqueue
 * @q: queue to suspend
 *
 * The function initiates queue suspending process.
 * After the function returns, it is guaranteed that no new commands
 * will be fetched from host. However, there might still be inflight
 * commands in the pipeline. To ensure queue is fully suspended, user
 * needs to query it by calling snap_vq_is_suspended(). If user is working in
 * non-polling mode, user must first call snap_vq_progress() to move queue to
 * the SUSPENDED state.
 */
void snap_vq_suspend(struct snap_vq *q)
{
	q->state = SNAP_VQ_STATE_FLUSHING;
}

/**
 * snap_vq_is_suspended() - Check whether a queue is suspended
 * @q: queue to check
 *
 * The function checks whether a queue is fully suspended, e.g.
 * has no inflight commands in the pipeline.
 *
 * Return: true if queue is suspended, false otherwise.
 */
bool snap_vq_is_suspended(const struct snap_vq *q)
{
	return q->state == SNAP_VQ_STATE_SUSPENDED;
}

/**
 * snap_vq_resume() - Resumes a virtqueue
 * @q: queue to resume
 *
 * Resume a (previously suspended) virtqueue.
 * After the function returns, new commands from host may now
 * start to be processed.
 */
void snap_vq_resume(struct snap_vq *q)
{
	q->state = SNAP_VQ_STATE_RUNNING;
}

/**
 * snap_vq_progress() - Progress virtqueue
 * @q: queue to progress
 *
 * Progress a virtqueue, e.g. advance its commands' pipeline.
 *
 * Context: The function should be used in polling mode, and
 *          be called iteratively for continuous progress of
 *          the queue.
 *
 * Return: number of processed events
 */
int snap_vq_progress(struct snap_vq *q)
{
	int n;

	n = q->dma_q->ops->progress_tx(q->dma_q);
	if (snap_likely(q->state == SNAP_VQ_STATE_RUNNING))
		n += q->dma_q->ops->progress_rx(q->dma_q);

	if (snap_unlikely(q->state == SNAP_VQ_STATE_FLUSHING &&
			TAILQ_EMPTY(&q->inflight_cmds)))
		q->state = SNAP_VQ_STATE_SUSPENDED;

	return n;
}

struct snap_dma_q *snap_vq_get_dma_q(struct snap_vq *q)
{
	return q->dma_q;
}

struct snap_virtio_queue *snap_vq_get_hw_q(struct snap_vq *q)
{
	return q->hw_q;
}

struct ibv_cq *snap_vq_get_vcq(struct snap_vq *q)
{
	return q->dma_q->sw_qp.rx_cq->verbs_cq;
}

int snap_vq_get_debugstat(struct snap_vq *q,
			  struct snap_virtio_queue_debugstat *q_debugstat)
{
	struct snap_virtio_queue_attr vq_attr = {};
	struct snap_virtio_queue_counters_attr vqc_attr = {};
	uint16_t vru, vra;
	int ret;

	ret = snap_virtio_get_used_index_from_host(q->dma_q, q->device_pa,
						q->xmkey, &vru);
	if (ret) {
		snap_error("failed to get vring used index from host memory for queue %d\n",
			   q->index);
		return ret;
	}

	ret = snap_virtio_get_avail_index_from_host(q->dma_q, q->driver_pa,
						q->xmkey, &vra);
	if (ret) {
		snap_error("failed to get vring avail index from host memory for queue %d\n",
			   q->index);
		return ret;
	}

	ret = snap_virtio_query_queue(q->hw_q, &vq_attr);
	if (ret) {
		snap_error("failed query queue %d\n", q->index);
		return ret;
	}

	ret = snap_virtio_query_queue_counters(q->hw_q->ctrs_obj, &vqc_attr);
	if (ret) {
		snap_error("failed query virtio_q_counters %d\n", q->index);
		return ret;
	}

	q_debugstat->qid = q->index;
	q_debugstat->hw_available_index = vq_attr.hw_available_index;
	q_debugstat->sw_available_index = vra;
	q_debugstat->hw_used_index = vq_attr.hw_used_index;
	q_debugstat->sw_used_index = vru;
	q_debugstat->hw_received_descs = vqc_attr.received_desc;
	q_debugstat->hw_completed_descs = vqc_attr.completed_desc;

	return 0;
}
