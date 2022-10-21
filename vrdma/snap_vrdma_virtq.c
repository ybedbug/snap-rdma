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

#if 0
static struct snap_dma_q *virtq_rdma_qp_init(struct virtq_create_attr *attr,
		struct virtq_priv *vq_priv, int tx_elem_size, int rx_elem_size,
		snap_dma_rx_cb_t cb)
{
	struct snap_dma_q_create_attr rdma_qp_create_attr = { };

	rdma_qp_create_attr.tx_qsize = attr->queue_size;
	rdma_qp_create_attr.tx_elem_size = tx_elem_size;
	rdma_qp_create_attr.rx_qsize = attr->queue_size;
	rdma_qp_create_attr.rx_elem_size = rx_elem_size;
	rdma_qp_create_attr.uctx = vq_priv;
	rdma_qp_create_attr.rx_cb = cb;
	rdma_qp_create_attr.mode = snap_env_getenv(SNAP_DMA_Q_OPMODE);

	return snap_dma_q_create(attr->pd, &rdma_qp_create_attr);
}

static void virtq_vattr_from_attr(struct virtq_create_attr *attr,
		struct snap_virtio_queue_attr *vattr, uint16_t max_tunnel_desc)
{

	vattr->type = SNAP_VIRTQ_SPLIT_MODE;
	vattr->ev_mode =
			(attr->msix_vector == VIRTIO_MSI_NO_VECTOR) ?
					SNAP_VIRTQ_NO_MSIX_MODE : SNAP_VIRTQ_MSIX_MODE;
	vattr->virtio_version_1_0 = attr->virtio_version_1_0;
	vattr->offload_type = SNAP_VIRTQ_OFFLOAD_DESC_TUNNEL;
	vattr->idx = attr->idx;
	vattr->size = attr->queue_size;
	vattr->desc = attr->desc;
	vattr->driver = attr->driver;
	vattr->device = attr->device;
	vattr->hw_available_index = attr->hw_available_index;
	vattr->hw_used_index = attr->hw_used_index;
	vattr->full_emulation = true;
	vattr->max_tunnel_desc = snap_min(attr->max_tunnel_desc, max_tunnel_desc);
	vattr->event_qpn_or_msix = attr->msix_vector;
	vattr->pd = attr->pd;
}

/**
 * virtq_ctxt_init() - Creates a new virtq object, along with RDMA QPs.

 * @attr:	Configuration attributes
 *
 * Creates the snap queues, virtio attributes and RDMA queues. For RDMA queues
 * creates hw and sw qps, hw qps will be given to VIRTIO_BLK_Q.
 * Completion is sent inline, hence tx elem size is completion size
 * the rx queue size should match the number of possible descriptors
 * this in the worst case scenario is the VIRTQ size.
 *
 * Context: Calling function should attach the virtqueue to a polling group
 *
 * Return: true if successful, false otherwise
 */
bool virtq_ctx_init(struct virtq_common_ctx *vq_ctx,
		    struct virtq_create_attr *attr,
		    struct virtq_ctx_init_attr *ctxt_attr)
{
	struct virtq_priv *vq_priv = calloc(1, sizeof(struct virtq_priv));
	struct ibv_qp *fw_qp;
	uint16_t hw_used;

	if (!vq_priv)
		goto err;
	struct snap_virtio_common_queue_attr *snap_attr = calloc(1, sizeof(struct snap_virtio_common_queue_attr));

	if (!snap_attr)
		goto release_priv;
	vq_priv->vq_ctx = vq_ctx;
	vq_ctx->priv = vq_priv;
	vq_priv->virtq_dev.ctx = ctxt_attr->bdev;
	vq_priv->pd = attr->pd;
	vq_ctx->idx = attr->idx;
	vq_ctx->fatal_err = 0;
	vq_priv->seg_max = attr->seg_max;
	vq_priv->size_max = attr->size_max;
	vq_priv->swq_state = SW_VIRTQ_RUNNING;
	vq_priv->vbq = ctxt_attr->vq;
	memset(&vq_priv->cmd_cntrs, 0, sizeof(vq_priv->cmd_cntrs));
	vq_priv->force_in_order = attr->force_in_order;
	vq_priv->dma_q = virtq_rdma_qp_init(attr, vq_priv,
					    ctxt_attr->tx_elem_size,
					    ctxt_attr->rx_elem_size,
					    ctxt_attr->cb);
	if (!vq_priv->dma_q) {
		snap_error("failed creating rdma qp loop\n");
		goto destroy_attr;
	}

	if (attr->in_recovery) {
		if (snap_virtio_get_used_index_from_host(vq_priv->dma_q,
				attr->device, attr->xmkey, &hw_used))
			goto destroy_dma_q;
	} else {
		hw_used = 0;
	}

	attr->hw_available_index = hw_used;
	attr->hw_used_index = hw_used;

	vq_priv->ctrl_available_index = attr->hw_available_index;
	vq_priv->ctrl_used_index = vq_priv->ctrl_available_index;

	snap_virtio_common_queue_config(snap_attr,
			attr->hw_available_index, attr->hw_used_index, vq_priv->dma_q);
	fw_qp = snap_dma_q_get_fw_qp(vq_priv->dma_q);
	snap_attr->vattr.tisn_or_qpn = fw_qp->qp_num;
	snap_attr->vattr.vhca_id = snap_get_dev_vhca_id(fw_qp->context);
	virtq_vattr_from_attr(attr, &snap_attr->vattr, ctxt_attr->max_tunnel_desc);
	vq_priv->vattr = &snap_attr->vattr;
	vq_priv->vattr->size = attr->queue_size;
	vq_priv->vattr->dma_mkey = attr->xmkey;

	return true;

destroy_dma_q:
	snap_dma_q_destroy(vq_priv->dma_q);
destroy_attr:
	free(snap_attr);
release_priv:
	free(vq_priv);
err:
	snap_error("failed creating virtq %d\n", attr->idx);
	return false;
}

/**
 * virtq_cmd_progress() - command state machine progress handle
 * @cmd:	command to be processed
 * @status:	status of calling function (can be a callback)
 *
 * Return: 0 (Currently no option to fail)
 */
int snap_vrdma_vq_cmd_progress(struct virtq_cmd *cmd,
		enum virtq_cmd_sm_op_status status)
{
	struct virtq_state_machine *sm;
	bool repeat = true;

	while (repeat) {
		repeat = false;
		snap_debug("virtq cmd sm state: %d\n", cmd->state);
		sm = cmd->vq_priv->custom_sm;
		if (snap_likely(cmd->state < VIRTQ_CMD_NUM_OF_STATES))
			repeat = sm->sm_array[cmd->state].sm_handler(cmd, status);
		else
			snap_error("reached invalid state %d\n", cmd->state);
	}

	return 0;
}

bool virtq_sm_idle(struct virtq_cmd *cmd, enum virtq_cmd_sm_op_status status)
{
	snap_error("command in invalid state %d\n",
					   VIRTQ_CMD_STATE_IDLE);
	return false;
}

/**
 * virtq_sm_write_back_done() - check write to bdev result status
 * @cmd:	command which requested the write
 * @status:	status of write operation
 */
bool virtq_sm_write_back_done(struct virtq_cmd *cmd,
				   enum virtq_cmd_sm_op_status status)
{
	cmd->state = VIRTQ_CMD_STATE_WRITE_STATUS;
	if (status != VIRTQ_CMD_SM_OP_OK)
		cmd->vq_priv->ops->error_status(cmd);

	return true;
}

int virtq_blk_dpa_send_status(struct snap_virtio_queue *vq, void *data, int size, uint64_t raddr);
/**
 * virtq_sm_write_status() - Write command status to host memory upon finish
 * @cmd:	command which requested the write
 * @status:	callback status, expected 0 for no errors
 *
 * Return: True if state machine is moved synchronously to the new state
 * (error cases) or false if the state transition will be done asynchronously.
 */
inline bool virtq_sm_write_status(struct virtq_cmd *cmd,
				   enum virtq_cmd_sm_op_status status)
{
	int ret;
	struct virtq_status_data sd;
	struct vring_desc *descs = cmd->vq_priv->ops->get_descs(cmd);

	cmd->vq_priv->ops->status_data(cmd, &sd);
	if (snap_unlikely(status != VIRTQ_CMD_SM_OP_OK))
		cmd->vq_priv->ops->error_status(cmd);

	virtq_log_data(cmd, "WRITE_STATUS: pa 0x%llx len %u\n",
		       descs[sd.desc].addr,
			   sd.status_size);
	/* hack... */
	if (snap_unlikely(cmd->vq_priv->ops->send_status))
		ret = cmd->vq_priv->ops->send_status(cmd->vq_priv->snap_vbq, sd.us_status, sd.status_size, descs[sd.desc].addr);
	else
		ret = snap_dma_q_write_short(cmd->vq_priv->dma_q, sd.us_status,
				sd.status_size,
				descs[sd.desc].addr,
				cmd->vq_priv->vattr->dma_mkey);

	if (snap_unlikely(ret)) {
		/* TODO: at some point we will have to do pending queue */
		ERR_ON_CMD(cmd, "failed to send status, err=%d", ret);
		cmd->state = VIRTQ_CMD_STATE_FATAL_ERR;
		return true;
	}

	virtq_mark_dirty_mem(cmd, descs[sd.desc].addr, sd.status_size, false);

	cmd->total_in_len += sd.status_size;
	cmd->state = VIRTQ_CMD_STATE_SEND_COMP;
	return true;
}

int virtq_sw_send_comp(struct virtq_cmd *cmd, struct snap_dma_q *q)
{
	struct snap_virtio_common_queue_attr *cmn_queue = to_common_queue_attr(cmd->vq_priv->vattr);
	uint64_t used_idx_addr, used_elem_addr;
	struct vring_used_elem elem;
	int ret;

	elem.id = cmd->descr_head_idx;
	elem.len = cmd->total_in_len;
	used_elem_addr = cmd->vq_priv->vattr->device +
			offsetof(struct vring_used, ring[cmn_queue->hw_used_index % cmd->vq_priv->vattr->size]);
	ret = snap_dma_q_write_short(q, &elem, sizeof(elem),
			used_elem_addr,
			cmd->vq_priv->vattr->dma_mkey);
	if (snap_unlikely(ret))
		return ret;

	used_idx_addr = cmd->vq_priv->vattr->device + offsetof(struct vring_used, idx);
	cmn_queue->hw_used_index = cmn_queue->hw_used_index + 1;
	ret = snap_dma_q_write_short(q, &cmn_queue->hw_used_index, sizeof(uint16_t),
						   used_idx_addr,
					       cmd->vq_priv->vattr->dma_mkey);

	return ret;
}

/**
 * sm_send_completion() - send command completion to FW
 * @cmd: Command being processed
 * @status: Status of callback
 *
 * Return:
 * True if state machine is moved synchronously to the new state
 * (error cases) or false if the state transition will be done asynchronously.
 */
inline bool virtq_sm_send_completion(struct virtq_cmd *cmd,
				     enum virtq_cmd_sm_op_status status)
{
	int ret;
	bool unordered = false;

	if (snap_unlikely(status != VIRTQ_CMD_SM_OP_OK)) {
		snap_error("failed to write the request status field\n");

		/* TODO: if VIRTQ_CMD_STATE_FATAL_ERR could be recovered in the future,
		 * handle case when cmd with VIRTQ_CMD_STATE_FATAL_ERR handled unordered.
		 */
		cmd->state = VIRTQ_CMD_STATE_FATAL_ERR;
		return true;
	}

	if (snap_unlikely(cmd->cmd_available_index != cmd->vq_priv->ctrl_used_index)) {
		virtq_log_data(cmd, "UNORD_COMP: cmd_idx:%d, in_num:%d, wait for in_num:%d\n",
			cmd->idx, cmd->cmd_available_index, cmd->vq_priv->ctrl_used_index);
		if (cmd->io_cmd_stat)
			++cmd->io_cmd_stat->unordered;
		unordered = true;
	}

	/* check order of completed command, if the command unordered - wait for
	 * other completions
	 */
	if (snap_unlikely(cmd->vq_priv->force_in_order) && snap_unlikely(unordered)) {
		cmd->state = VIRTQ_CMD_STATE_SEND_IN_ORDER_COMP;
		return false;
	}

	ret = cmd->vq_priv->ops->send_comp(cmd, cmd->vq_priv->dma_q);
	if (snap_unlikely(ret)) {
		/* TODO: pending queue */
		ERR_ON_CMD(cmd, "failed to send completion ret %d\n", ret);
		cmd->state = VIRTQ_CMD_STATE_FATAL_ERR;
	} else {
		virtq_mark_dirty_mem(cmd, 0, 0, true);

		cmd->state = VIRTQ_CMD_STATE_RELEASE;
		++cmd->vq_priv->ctrl_used_index;
	}

	return true;
}

bool snap_vrdma_vq_sm_release(struct virtq_cmd *cmd, enum virtq_cmd_sm_op_status status)
{
	bool repeat = false;

	repeat = virtq_common_release(cmd);
	--cmd->vq_priv->cmd_cntrs.outstanding_total;
	return repeat;
}

bool snap_vrdma_vq_sm_fatal_error(struct virtq_cmd *cmd, enum virtq_cmd_sm_op_status status)
{
	virtq_common_release(cmd);
	cmd->vq_priv->vq_ctx->fatal_err = -1;
	++cmd->vq_priv->cmd_cntrs.fatal;
	/*
	 * TODO: propagate fatal error to the controller.
	 * At the moment attempt to resume/state copy
	 * of such controller will have unpredictable
	 * results.
	 */

	return false;
}
#endif

void snap_vrdma_vq_ctx_destroy(struct snap_vrdma_queue *q)
{
	snap_dma_q_destroy(q->dma_q);
}

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
	//struct snap_vrdma_common_queue_attr qattr = { };
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
snap_vrdma_vq_create(struct snap_vrdma_ctrl *vctrl)
{
	struct snap_vrdma_queue *virtq;

	virtq = calloc(1, sizeof(*virtq));
	//TODO: add vq create handling
	TAILQ_INSERT_TAIL(&vctrl->virtqs, virtq, vq);
	return virtq;
}

static void snap_vrdma_vq_destroy(struct snap_vrdma_ctrl *vctrl,
										struct snap_vrdma_queue *virtq)
{
	struct snap_virtio_blk_ctrl_queue *vbq = to_blk_ctrl_q(vq);
	struct snap_virtio_blk_device_attr *dev_attr;

	//TODO: add vq destroy handling
	TAILQ_REMOVE(&vctrl->virtqs, virtq, vq);
	free(virtq);
}

static void snap_vrdma_vq_start(struct snap_vrdma_queue *q, 
							struct snap_vrdma_vq_start_attr *attr)
{
	q->pg_id = attr->pg_id;
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

static void snap_vrdma_ctrl_sched_q_nolock(struct snap_vrdma_ctrl *ctrl,
					    struct snap_vrdma_queue *vq,
					    struct snap_pg *pg)
{
	TAILQ_INSERT_TAIL(&pg->q_list, &vq->pg_q, entry);
	vq->pg = pg;
	if (ctrl->q_ops->start)
		ctrl->q_ops->start(vq);
}

void snap_vrdma_ctrl_sched_q(struct snap_vrdma_ctrl *ctrl,
				     struct snap_vrdma_queue *vq)
{
	struct snap_pg *pg;

	pg = snap_pg_get_next(&ctrl->pg_ctx);

	pthread_spin_lock(&pg->lock);
	snap_vrdma_ctrl_sched_q_nolock(ctrl, vq, pg);
	snap_debug("VRDMA queue polling group id = %d\n", vq->pg->id);
	pthread_spin_unlock(&pg->lock);
}

static void snap_vrdma_ctrl_desched_q_nolock(struct snap_vrdma_queue *vq)
{
	struct snap_pg *pg = vq->pg;

	if (!pg)
		return;

	TAILQ_REMOVE(&pg->q_list, &vq->pg_q, entry);
	snap_pg_usage_decrease(vq->pg->id);
	vq->pg = NULL;
}

void snap_vrdma_ctrl_desched_q(struct snap_vrdma_queue *vq)
{
	struct snap_pg *pg = vq->pg;

	if (!pg)
		return;

	pthread_spin_lock(&pg->lock);
	snap_vrdma_ctrl_desched_q_nolock(vq);
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
		n += snap_vrdma_qp_progress(vq);
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


