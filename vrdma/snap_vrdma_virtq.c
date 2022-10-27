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

#include <linux/virtio_ring.h>
#include <linux/virtio_pci.h>
#include "snap_channel.h"
#include "snap_dma.h"
#include "snap_env.h"
#include "snap_vrdma_virtq.h"
#include "snap_vrdma_ctrl.h"

#define SNAP_DMA_Q_OPMODE   "SNAP_DMA_Q_OPMODE"

static inline bool 
vrdma_q_check_outstanding_prog_suspend(struct snap_vrdma_queue *q)
{
	if (snap_unlikely(q->cmd_cntrs.fatal)) {
		if (q->cmd_cntrs.outstanding_in_bdev == 0 &&
		    q->cmd_cntrs.outstanding_to_host == 0)
			return true;
	} else {
		if (q->cmd_cntrs.outstanding_total == 0 &&
		    q->cmd_cntrs.outstanding_in_bdev == 0 &&
		    q->cmd_cntrs.outstanding_to_host == 0)
			return true;
	}

	return false;
}

static void snap_vrdma_vq_progress_suspend(struct snap_vrdma_queue *q)
{
	int n;

	/* TODO: add option to ignore commands in the bdev layer */
	if (!vrdma_q_check_outstanding_prog_suspend(q))
		return;

	n = snap_dma_q_flush(q->dma_q);

#if 0
	qattr.vattr.state = SNAP_VIRTQ_STATE_SUSPEND;

	/* TODO: check with FLR/reset. I see modify fail where it should not */
	if (q->ops->progress_suspend(priv->snap_vbq, &qattr))
		snap_error("ctrl %p queue %d: failed to move to the SUSPENDED state\n", priv->vbq->ctrl, q->idx);
#endif
	/* at this point QP is in the error state and cannot be used anymore */
	snap_info("ctrl %p queue %d: moving to the SUSPENDED state (q_flush %d)\n", q->ctrl, q->idx, n);
	q->swq_state = SW_VIRTQ_SUSPENDED;
}

static struct snap_vrdma_queue *
snap_vrdma_vq_create(struct snap_vrdma_ctrl *vctrl,
							struct snap_vrdma_vq_create_attr *q_attr)
{
	struct snap_vrdma_queue *virtq;
	struct snap_dma_q_create_attr rdma_qp_create_attr = {};

	virtq = calloc(1, sizeof(*virtq));
	if (!virtq) {
		snap_error("create queue %d: no memory\n", q_attr->vqpn);
		return NULL;
	}

	rdma_qp_create_attr.tx_qsize = q_attr->sq_size;
	rdma_qp_create_attr.tx_elem_size = q_attr->tx_elem_size;
	rdma_qp_create_attr.rx_qsize = q_attr->rq_size;
	rdma_qp_create_attr.rx_elem_size = q_attr->rx_elem_size;
	rdma_qp_create_attr.uctx = virtq;
	rdma_qp_create_attr.rx_cb = NULL;
	rdma_qp_create_attr.mode = snap_env_getenv(SNAP_DMA_Q_OPMODE);
	virtq->dma_q = snap_dma_q_create(q_attr->pd, &rdma_qp_create_attr);
	if (!virtq->dma_q) {
		free(virtq);
		snap_error("create queue %d: create dma queue failed\n", q_attr->vqpn);
		return NULL;
	}
	virtq->ctrl = vctrl;
	virtq->idx = q_attr->vqpn;
	virtq->pd = q_attr->pd;
	virtq->dma_mkey = vctrl->xmkey->mkey;
	
	TAILQ_INSERT_TAIL(&vctrl->virtqs, virtq, vq);
	return virtq;
}

static void snap_vrdma_vq_destroy(struct snap_vrdma_ctrl *vctrl,
				struct snap_vrdma_queue *virtq)
{
	//TODO: add vq destroy handling
	snap_dma_q_destroy(virtq->dma_q);
	TAILQ_REMOVE(&vctrl->virtqs, virtq, vq);
	free(virtq);
}

static void snap_vrdma_vq_start(struct snap_vrdma_queue *q)
{
	//TODO: add start handling
}

static int snap_vrdma_vq_progress(struct snap_vrdma_queue *q)
{
	int n = 0;

	if (snap_unlikely(q->swq_state == SW_VIRTQ_SUSPENDED))
		goto out;

	n += snap_dma_q_progress(q->dma_q);
	
	/*
	 * need to wait until all inflight requests
	 * are finished before moving to the suspend state
	 */
	if (snap_unlikely(q->swq_state == SW_VIRTQ_FLUSHING))
		snap_vrdma_vq_progress_suspend(q);

out:
	return n;
}

/**
 * snap_vrdma_vq_suspend() - Request moving queue to suspend state
 * @q:	queue to move to suspend state
 *
 * When suspend is requested the queue stops receiving new commands
 * and moves to FLUSHING state. Once all commands already fetched are
 * finished, the queue moves to SUSPENDED state.
 *
 * Context: Function is not thread safe with regard to virtq_progress
 * and virtq_is_suspended. If called from a different thread than
 * thread calling progress/is_suspended then application must take care of
 * proper locking
 *
 * Return: 0 on success, else error code
 */
static int snap_vrdma_vq_suspend(struct snap_vrdma_queue *q)
{
	if (q->swq_state != SW_VIRTQ_RUNNING) {
		snap_debug("queue %d: suspend was already requested\n", q->idx);
		return -EBUSY;
	}

	snap_info("ctrl %p queue %d: SUSPENDING command(s) - in %d bdev %d host %d fatal %d\n",
			q->ctrl, q->idx,
			q->cmd_cntrs.outstanding_total, q->cmd_cntrs.outstanding_in_bdev,
			q->cmd_cntrs.outstanding_to_host, q->cmd_cntrs.fatal);

	q->swq_state = SW_VIRTQ_FLUSHING;
	return 0;
}

/**
 * snap_vrdma_vq_is_suspended() - api for checking if queue in suspended state
 * @q:		queue to check
 *
 * Context: Function is not thread safe with regard to virtq_progress
 * and virtq_suspend. If called from a different thread than
 * thread calling progress/suspend then application must take care of
 * proper locking
 *
 * Return: True when queue suspended, and False for not suspended
 */
static bool snap_vrdma_vq_is_suspended(struct snap_vrdma_queue *q)
{
	return q->swq_state == SW_VIRTQ_SUSPENDED;
}

struct snap_vrdma_queue_ops snap_vrdma_queue_ops = {
	.create = snap_vrdma_vq_create,
	.destroy = snap_vrdma_vq_destroy,
	.progress = snap_vrdma_vq_progress,
	.start = snap_vrdma_vq_start,
	.suspend = snap_vrdma_vq_suspend,
	.is_suspended = snap_vrdma_vq_is_suspended,
	.resume = NULL,
};

static void snap_vrdma_sched_vq_nolock(struct snap_vrdma_ctrl *ctrl,
					    struct snap_vrdma_queue *vq,
					    struct snap_pg *pg)
{
	TAILQ_INSERT_TAIL(&pg->q_list, &vq->pg_q, entry);
	vq->pg = pg;
	if (ctrl->q_ops->start)
		ctrl->q_ops->start(vq);
}

void snap_vrdma_sched_vq(struct snap_vrdma_ctrl *ctrl,
				     struct snap_vrdma_queue *vq)
{
	struct snap_pg *pg;

	pg = snap_pg_get_next(&ctrl->pg_ctx);

	pthread_spin_lock(&pg->lock);
	snap_vrdma_sched_vq_nolock(ctrl, vq, pg);
	snap_debug("VRDMA queue polling group id = %d\n", vq->pg->id);
	pthread_spin_unlock(&pg->lock);
}

static void snap_vrdma_desched_vq_nolock(struct snap_vrdma_queue *vq)
{
	struct snap_pg *pg = vq->pg;

	if (!pg)
		return;

	TAILQ_REMOVE(&pg->q_list, &vq->pg_q, entry);
	snap_pg_usage_decrease(vq->pg->id);
	vq->pg = NULL;
}

void snap_vrdma_desched_vq(struct snap_vrdma_queue *vq)
{
	struct snap_pg *pg = vq->pg;

	if (!pg)
		return;

	pthread_spin_lock(&pg->lock);
	snap_vrdma_desched_vq_nolock(vq);
	pthread_spin_unlock(&pg->lock);
}

static inline struct snap_vrdma_queue *
pg_q_entry_to_vrdma_qp(struct snap_pg_q_entry *pg_q)
{
	return container_of(pg_q, struct snap_vrdma_queue, pg_q);
}

static int snap_vrdma_ctrl_pg_thread_io_progress(
		struct snap_vrdma_ctrl *ctrl, int pg_id, int thread_id)
{
	struct snap_pg *pg = &ctrl->pg_ctx.pgs[pg_id];
	struct snap_vrdma_queue *vq;
	struct snap_pg_q_entry *pg_q;
	int n = 0;

	pthread_spin_lock(&pg->lock);
	TAILQ_FOREACH(pg_q, &pg->q_list, entry) {
		vq = pg_q_entry_to_vrdma_qp(pg_q);
		vq->thread_id = thread_id;
		n += snap_vrdma_vq_progress(vq);
	}
	pthread_spin_unlock(&pg->lock);

	return n;
}

/**
 * snap_vrdma_ctrl_io_progress() - single-threaded IO requests handling
 * @ctrl:       controller instance
 *
 * Looks for any IO requests from host received on any QPs, and handles
 * them based on the request's parameters.
 */
int snap_vrdma_ctrl_io_progress(struct snap_vrdma_ctrl *ctrl)
{
	int i;
	int n = 0;

	for (i = 0; i < ctrl->pg_ctx.npgs; i++)
		n += snap_vrdma_ctrl_pg_thread_io_progress(ctrl, i, -1);

	return n;
}

/**
 * snap_vrdma_ctrl_io_progress_thread() - Handle IO requests for thread
 * @ctrl:       controller instance
 * @thread_id:	id queues belong to
 *
 * Looks for any IO requests from host received on QPs which belong to thread
 * thread_id, and handles them based on the request's parameters.
 */
int snap_vrdma_ctrl_io_progress_thread(struct snap_vrdma_ctrl *ctrl,
					     uint32_t thread_id)
{
	return snap_vrdma_ctrl_pg_thread_io_progress(ctrl, thread_id, thread_id);
}


