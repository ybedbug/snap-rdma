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
#include "mlx5_ifc.h"

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

static void snap_vrdma_vq_dummy_rx_cb(struct snap_dma_q *q, const void *data, uint32_t data_len, uint32_t imm_data)
{
	snap_error("VRDMA: rx cb called\n");
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
	rdma_qp_create_attr.rx_cb = snap_vrdma_vq_dummy_rx_cb;
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

struct snap_vrdma_queue_ops *get_vrdma_queue_ops(void)
{
	return &snap_vrdma_queue_ops;
}

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

int snap_vrdma_create_qp_helper(struct ibv_pd *pd, 
			struct snap_vrdma_backend_qp *qp)
{
	struct snap_qp_attr *qp_attr = &qp->qp_attr;
	struct snap_cq_attr cq_attr = {0};
	int rc;

	snap_error("\nlizh snap_vrdma_create_qp_helper...start");
	cq_attr.cq_type = SNAP_OBJ_DEVX;
	cq_attr.cqe_size = SNAP_VRDMA_BACKEND_CQE_SIZE;
	if (qp_attr->sq_size) {
		cq_attr.cqe_cnt = qp_attr->sq_size;
		qp_attr->sq_cq = snap_cq_create(pd->context, &cq_attr);
		snap_error("\nlizh snap_vrdma_create_qp_helper qp_attr->sq_cq %p", qp_attr->sq_cq);
		if (!qp_attr->sq_cq)
			return -EINVAL;
	} else {
		qp_attr->sq_cq = NULL;
	}

	if (qp_attr->rq_size) {
		cq_attr.cqe_cnt = qp_attr->rq_size;
		qp_attr->rq_cq = snap_cq_create(pd->context, &cq_attr);
		snap_error("\nlizh snap_vrdma_create_qp_helper qp_attr->rq_cq %p", qp_attr->rq_cq);
		if (!qp_attr->rq_cq)
			goto free_sq_cq;
	} else {
		qp_attr->rq_cq = NULL;
	}

	qp_attr->qp_type = SNAP_OBJ_DEVX;
	qp->sqp = snap_qp_create(pd, qp_attr);
	snap_error("\nlizh snap_vrdma_create_qp_helper snap_qp_create qp->sqp %p", qp->sqp);
	if (!qp->sqp)
		goto free_rq_cq;
	qp->qpnum = snap_qp_get_qpnum(qp->sqp);

	rc = snap_qp_to_hw_qp(qp->sqp, &qp->hw_qp);
	snap_error("\nlizh snap_vrdma_create_qp_helper snap_qp_to_hw_qp rc %d", rc);
	if (rc)
		goto free_qp;

	if (qp_attr->sq_cq) {
		rc = snap_cq_to_hw_cq(qp_attr->sq_cq, &qp->sq_hw_cq);
		snap_error("\nlizh snap_vrdma_create_qp_helper snap_cq_to_hw_cq sq_cq rc %d", rc);
		if (rc)
			goto free_qp;
	}

	if (qp_attr->rq_cq) {
		rc = snap_cq_to_hw_cq(qp_attr->rq_cq, &qp->rq_hw_cq);
		snap_error("\nlizh snap_vrdma_create_qp_helper snap_cq_to_hw_cq rx_cq rc %d", rc);
		if (rc)
			goto free_qp;
	}
	return 0;

free_qp:
	snap_qp_destroy(qp->sqp);
free_rq_cq:
	if (qp_attr->rq_cq)
		snap_cq_destroy(qp_attr->rq_cq);
free_sq_cq:
	if (qp_attr->sq_cq)
		snap_cq_destroy(qp_attr->sq_cq);
	return -EINVAL;
}

void snap_vrdma_destroy_qp_helper(struct snap_vrdma_backend_qp *qp)
{
	if (qp->sqp)
		snap_qp_destroy(qp->sqp);
	if (qp->qp_attr.rq_cq)
		snap_cq_destroy(qp->qp_attr.rq_cq);
	if (qp->qp_attr.sq_cq)
		snap_cq_destroy(qp->qp_attr.sq_cq);
}

#if 0
/**
 * snap_vrdma_virtq_destroy() - Destroys vrdma virtq
 * @q: queue to be destroyed
 *
 * Context: Destroy should be called only when queue is in suspended state.
 *
 * Return: void
 */
void snap_vrdma_virtq_destroy(struct snap_vrdma_ctrl *ctrl,
			struct snap_vrdma_queue *queue)
{
	struct virtq_priv *vq_priv = q->common_ctx.priv;

	snap_debug("destroying queue %d\n", q->common_ctx.idx);

	if (vq_priv->swq_state != SW_VIRTQ_SUSPENDED && vq_priv->cmd_cntrs.outstanding_total)
		snap_warn("queue %d: destroying while not in the SUSPENDED state, %d commands outstanding\n",
			  q->common_ctx.idx, vq_priv->cmd_cntrs.outstanding_total);

	if (vq_priv->cmd_cntrs.fatal)
		snap_warn("queue %d: destroying while %d command(s) completed with fatal error\n",
			  q->common_ctx.idx, vq_priv->cmd_cntrs.fatal);
	ctrl->q_ops->destroy(ctrl, queue);

	free_blk_virtq_cmd_arr(vq_priv);
	virtq_ctx_destroy(vq_priv);
	free(q);
}
#endif

int snap_vrdma_modify_bankend_qp_rst2init(struct snap_qp *qp,
				     struct ibv_qp_attr *qp_attr, int attr_mask)
{
	uint8_t in[DEVX_ST_SZ_BYTES(rst2init_qp_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(rst2init_qp_out)] = {0};
	void *qpc = DEVX_ADDR_OF(rst2init_qp_in, in, qpc);
	int ret;

	DEVX_SET(rst2init_qp_in, in, opcode, MLX5_CMD_OP_RST2INIT_QP);
	DEVX_SET(rst2init_qp_in, in, qpn, snap_qp_get_qpnum(qp));
	DEVX_SET(qpc, qpc, pm_state, MLX5_QP_PM_MIGRATED);
	DEVX_SET(qpc, qpc, primary_address_path.vhca_port_num, 1);

	if (attr_mask & IBV_QP_ACCESS_FLAGS) {
		if (qp_attr->qp_access_flags & IBV_ACCESS_REMOTE_READ)
			DEVX_SET(qpc, qpc, rre, 1);
		if (qp_attr->qp_access_flags & IBV_ACCESS_REMOTE_WRITE)
			DEVX_SET(qpc, qpc, rwe, 1);
		if (qp_attr->qp_access_flags & IBV_ACCESS_REMOTE_ATOMIC) {
			DEVX_SET(qpc, qpc, rae, 1);
			DEVX_SET(qpc, qpc, atomic_mode, MLX5_QPC_ATOMIC_MODE_UP_TO_8B);
		}
	}

	ret = snap_qp_modify(qp, in, sizeof(in), out, sizeof(out));
	if (ret)
		snap_error("failed to modify qp to init with errno = %d\n", ret);
	return ret;
}

int snap_vrdma_modify_bankend_qp_init2rtr(struct snap_qp *qp,
			struct ibv_qp_attr *qp_attr, int attr_mask,
			struct snap_vrdma_bk_qp_rdy_attr *rdy_attr)
{
	uint8_t in[DEVX_ST_SZ_BYTES(init2rtr_qp_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(init2rtr_qp_out)] = {0};
	void *qpc = DEVX_ADDR_OF(init2rtr_qp_in, in, qpc);
	void *address_path;
	int ret;

	DEVX_SET(init2rtr_qp_in, in, opcode, MLX5_CMD_OP_INIT2RTR_QP);
	DEVX_SET(init2rtr_qp_in, in, qpn, snap_qp_get_qpnum(qp));

	/* 30 is the maximum value for Infiniband QPs*/
	DEVX_SET(qpc, qpc, log_msg_max, 30);

	/* TODO: add more attributes */
	if (attr_mask & IBV_QP_PATH_MTU)
		DEVX_SET(qpc, qpc, mtu, qp_attr->path_mtu);
	if (attr_mask & IBV_QP_DEST_QPN)
		DEVX_SET(qpc, qpc, remote_qpn, qp_attr->dest_qp_num);
	if (attr_mask & IBV_QP_RQ_PSN)
		DEVX_SET(qpc, qpc, next_rcv_psn, qp_attr->rq_psn & 0xffffff);
	if (attr_mask & IBV_QP_TIMEOUT)
		DEVX_SET(qpc, qpc, primary_address_path.ack_timeout,
			 qp_attr->timeout);
	if (attr_mask & IBV_QP_MAX_DEST_RD_ATOMIC)
		DEVX_SET(qpc, qpc, log_rra_max,
			 snap_u32log2(qp_attr->max_dest_rd_atomic));
	if (attr_mask & IBV_QP_MIN_RNR_TIMER)
		DEVX_SET(qpc, qpc, min_rnr_nak, qp_attr->min_rnr_timer);
	if (attr_mask & IBV_QP_AV) {
		DEVX_SET(qpc, qpc, primary_address_path.fl, 1);
	} else {
		address_path = DEVX_ADDR_OF(qpc, qpc, primary_address_path);
		/* Only connection type supported is ETH - ROCE */
		memcpy(DEVX_ADDR_OF(qpc, qpc, primary_address_path.rmac_47_32),
		       rdy_attr->dest_mac, MAC_ADDR_2MSBYTES_LEN);
		memcpy(DEVX_ADDR_OF(qpc, qpc, primary_address_path.rmac_31_0),
		       rdy_attr->dest_mac + MAC_ADDR_2MSBYTES_LEN,
		       MAC_ADDR_LEN - MAC_ADDR_2MSBYTES_LEN);
		memcpy(DEVX_ADDR_OF(qpc, qpc, primary_address_path.rgid_rip),
			   rdy_attr->rgid_rip.raw,
		       DEVX_FLD_SZ_BYTES(qpc, primary_address_path.rgid_rip));
		DEVX_SET(ads, address_path, src_addr_index, rdy_attr->src_addr_index);
		DEVX_SET(ads, address_path, hop_limit, 255); /* High value so it won't limit */
		DEVX_SET(ads, address_path, udp_sport, 0xc000);
	}

	ret = snap_qp_modify(qp, in, sizeof(in), out, sizeof(out));
	if (ret)
		snap_error("failed to modify qp to rtr with errno = %d\n", ret);
	return ret;
}

int snap_vrdma_modify_bankend_qp_rtr2rts(struct snap_qp *qp,
				    struct ibv_qp_attr *qp_attr, int attr_mask)
{
	uint8_t in[DEVX_ST_SZ_BYTES(rtr2rts_qp_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(rtr2rts_qp_out)] = {0};
	void *qpc = DEVX_ADDR_OF(rtr2rts_qp_in, in, qpc);
	int ret;

	DEVX_SET(rtr2rts_qp_in, in, opcode, MLX5_CMD_OP_RTR2RTS_QP);
	DEVX_SET(rtr2rts_qp_in, in, qpn, snap_qp_get_qpnum(qp));

	if (attr_mask & IBV_QP_TIMEOUT)
		DEVX_SET(qpc, qpc, primary_address_path.ack_timeout,
			 qp_attr->timeout);
	if (attr_mask & IBV_QP_RETRY_CNT)
		DEVX_SET(qpc, qpc, retry_count, qp_attr->retry_cnt);
	if (attr_mask & IBV_QP_SQ_PSN)
		DEVX_SET(qpc, qpc, next_send_psn, qp_attr->sq_psn & 0xffffff);
	if (attr_mask & IBV_QP_RNR_RETRY)
		DEVX_SET(qpc, qpc, rnr_retry, qp_attr->rnr_retry);
	if (attr_mask & IBV_QP_MAX_QP_RD_ATOMIC)
		DEVX_SET(qpc, qpc, log_sra_max,
			 snap_u32log2(qp_attr->max_rd_atomic));

	ret = snap_qp_modify(qp, in, sizeof(in), out, sizeof(out));
	if (ret)
		snap_error("failed to modify qp to rts with errno = %d\n", ret);
	return ret;
}
