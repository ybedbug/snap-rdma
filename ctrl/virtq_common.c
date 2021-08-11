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

#include "virtq_common.h"
#include <linux/virtio_ring.h>
#include <linux/virtio_pci.h>
#include "snap_channel.h"
#include "snap_dma.h"
#include "snap_env.h"

#define SNAP_DMA_Q_OPMODE   "SNAP_DMA_Q_OPMODE"

static struct snap_dma_q *virtq_rdma_qp_init(struct virtq_create_attr *attr,
		struct virtq_priv *vq_priv, int rx_elem_size, snap_dma_rx_cb_t cb)
{
	struct snap_dma_q_create_attr rdma_qp_create_attr = { };

	rdma_qp_create_attr.tx_qsize = attr->queue_size;
	rdma_qp_create_attr.tx_elem_size = sizeof(struct virtq_split_tunnel_comp);
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
	vattr->desc = attr->desc;
	vattr->driver = attr->driver;
	vattr->device = attr->device;
	vattr->full_emulation = true;
	vattr->max_tunnel_desc = snap_min(attr->max_tunnel_desc, max_tunnel_desc);
	vattr->event_qpn_or_msix = attr->msix_vector;
	vattr->pd = attr->pd;
}

/**
 * virtq_ctxt_init() - Creates a new virtq object, along with RDMA QPs.

 * @attr:	Configuration attributes
 *
 * Creates the snap queues, and RDMA queues. For RDMA queues
 * creates hw and sw qps, hw qps will be given to VIRTIO_BLK_Q.
 * Completion is sent inline, hence tx elem size is completion size
 * the rx queue size should match the number of possible descriptors
 * this in the worst case scenario is the VIRTQ size.
 *
 * Context: Calling function should attach the virtqueue to a polling group
 *
 * Return: false on failure, true on success
 */
bool virtq_ctx_init(struct virtq_common_ctx *vq_ctx,
		struct virtq_create_attr *attr, struct snap_virtio_queue_attr *vattr,
		struct snap_virtio_ctrl_queue *vq, void *bdev, int rx_elem_size,
		uint16_t max_tunnel_desc, snap_dma_rx_cb_t cb)
{
	struct virtq_priv *vq_priv = calloc(1, sizeof(struct virtq_priv));

	if (!vq_priv)
		goto err;

	vq_priv->vq_ctx = vq_ctx;
	vq_ctx->priv = vq_priv;
	vq_priv->vattr = vattr;
	vq_priv->virtq_dev.ctx = bdev;
	vq_priv->pd = attr->pd;
	vq_ctx->idx = attr->idx;
	vq_ctx->fatal_err = 0;
	vq_priv->seg_max = attr->seg_max;
	vq_priv->size_max = attr->size_max;
	vq_priv->vattr->size = attr->queue_size;
	vq_priv->swq_state = SW_VIRTQ_RUNNING;
	vq_priv->vbq = vq;
	vq_priv->cmd_cntr = 0;
	vq_priv->ctrl_available_index = attr->hw_available_index;
	vq_priv->ctrl_used_index = vq_priv->ctrl_available_index;
	vq_priv->force_in_order = attr->force_in_order;

	vq_priv->dma_q = virtq_rdma_qp_init(attr, vq_priv, rx_elem_size, cb);
	if (!vq_priv->dma_q) {
		snap_error("failed creating rdma qp loop\n");
		goto release_priv;
	}

	virtq_vattr_from_attr(attr, vattr, max_tunnel_desc);

	return vq_ctx;

release_priv:
	free(vq_priv);
err:
	snap_error("failed creating virtq %d\n", attr->idx);
	return NULL;
}

void virtq_ctx_destroy(struct virtq_priv *vq_priv)
{
	snap_dma_q_destroy(vq_priv->dma_q);
	free(vq_priv);
}

/**
 * virtq_cmd_progress() - command state machine progress handle
 * @cmd:	commad to be processed
 * @status:	status of calling function (can be a callback)
 *
 * Return: 0 (Currently no option to fail)
 */
int virtq_cmd_progress(struct virtq_cmd *cmd,
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
 * enum virtq_fetch_desc_status - status of descriptors fetch process
 * @VIRTQ_FETCH_DESC_DONE:	All descriptors were fetched
 * @VIRTQ_FETCH_DESC_ERR:	Error while trying to fetch a descriptor
 * @VIRTQ_FETCH_DESC_READ:	An Asynchronous read for desc was called
 */
enum virtq_fetch_desc_status {
	VIRTQ_FETCH_DESC_DONE, VIRTQ_FETCH_DESC_ERR, VIRTQ_FETCH_DESC_READ,
};

/**
 * fetch_next_desc() - Fetches command descriptors from host memory
 * @cmd: command descriptors belongs to
 *
 * Function checks if there are descriptors that were not sent in the
 * tunnled command, and if so it reads them from host memory one by one.
 * Reading from host memory is done asynchronous
 *
 * Return: virtq_fetch_desc_status
 */
static enum virtq_fetch_desc_status fetch_next_desc(struct virtq_cmd *cmd)
{
	uint64_t srcaddr;
	uint16_t in_ring_desc_addr;
	int ret;
	struct vring_desc *descs = cmd->vq_priv->ops->get_descs(cmd);

	if (cmd->num_desc == 0)
		in_ring_desc_addr = cmd->descr_head_idx % cmd->vq_priv->vattr->size;
	else if (descs[cmd->num_desc - 1].flags & VRING_DESC_F_NEXT)
		in_ring_desc_addr = descs[cmd->num_desc - 1].next;
	else
		return VIRTQ_FETCH_DESC_DONE;

	if (cmd->vq_priv->ops->seg_dmem(cmd))
		return VIRTQ_FETCH_DESC_ERR;

	srcaddr = cmd->vq_priv->vattr->desc +
		  in_ring_desc_addr * sizeof(struct vring_desc);
	cmd->dma_comp.count = 1;
	virtq_log_data(cmd, "READ_DESC: pa 0x%lx len %lu\n", srcaddr, sizeof(struct vring_desc));
	ret = snap_dma_q_read(cmd->vq_priv->dma_q, &cmd->vq_priv->ops->get_descs(cmd)[cmd->num_desc],
			sizeof(struct vring_desc), cmd->aux_mr->lkey, srcaddr,
			cmd->vq_priv->vattr->dma_mkey,
			&(cmd->dma_comp));
	if (ret)
		return VIRTQ_FETCH_DESC_ERR;
	cmd->num_desc++;
	return VIRTQ_FETCH_DESC_READ;
}

/**
 * virtq_sm_fetch_cmd_descs() - Fetch all of commands descs
 * @cmd: Command being processed
 * @status: Callback status
 *
 * Function collects all of the commands descriptors. Descriptors can be either
 * in the tunnel command itself, or in host memory.
 *
 * Return: True if state machine is moved to a new state synchronously (error
 * or all descs were fetched), false if the state transition will be done
 * asynchronously.
 */
bool virtq_sm_fetch_cmd_descs(struct virtq_cmd *cmd,
			       enum virtq_cmd_sm_op_status status)
{
	size_t i;
	enum virtq_fetch_desc_status ret;
	struct vring_desc *descs = cmd->vq_priv->ops->get_descs(cmd);

	if (status != VIRTQ_CMD_SM_OP_OK) {
		ERR_ON_CMD(cmd, "failed to fetch commands descs, dumping command without response\n");
		cmd->state = VIRTQ_CMD_STATE_FATAL_ERR;
		return true;
	}
	ret = fetch_next_desc(cmd);
	if (ret == VIRTQ_FETCH_DESC_ERR) {
		ERR_ON_CMD(cmd, "failed to RDMA READ desc from host\n");
		cmd->state = VIRTQ_CMD_STATE_FATAL_ERR;
		return true;
	} else if (ret == VIRTQ_FETCH_DESC_DONE) {
		cmd->vq_priv->ops->descs_processing(cmd);
		if (cmd->state == VIRTQ_CMD_STATE_FATAL_ERR)
			return true;

		for (i = 1; i < cmd->num_desc - 1; i++)
			cmd->total_seg_len += descs[i].len;

		cmd->state = VIRTQ_CMD_STATE_READ_HEADER;

		return true;
	} else {
		return false;
	}
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

inline void virtq_mark_dirty_mem(struct virtq_cmd *cmd, uint64_t pa,
					uint32_t len, bool is_completion)
{
	struct snap_virtio_ctrl_queue *vq = cmd->vq_priv->vbq;
	int rc;

	if (snap_likely(!vq->log_writes_to_host))
		return;

	if (is_completion) {
		/* spec 2.6 Split Virtqueues
		 * mark all of the device area as dirty, in the worst case
		 * it will cost an extra page or two. Device area size is
		 * calculated according to the spec.
		 */
		pa = cmd->vq_priv->vattr->device;
		len = 6 + 8 * cmd->vq_priv->vattr->size;
	}
	virtq_log_data(cmd, "MARK_DIRTY_MEM: pa 0x%lx len %u\n", pa, len);
	if (!vq->ctrl->lm_channel) {
		ERR_ON_CMD(cmd, "dirty memory logging enabled but migration channel is not present\n");
		return;
	}
	rc = snap_channel_mark_dirty_page(vq->ctrl->lm_channel, pa, len);
	if (rc)
		ERR_ON_CMD(cmd, "mark drity page failed: pa 0x%lx len %u\n", pa, len);
}

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
	virtq_mark_dirty_mem(cmd, descs[sd.desc].addr,
			sd.status_size, false);
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

	cmd->total_in_len += sd.status_size;
	cmd->state = VIRTQ_CMD_STATE_SEND_COMP;
	return true;
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
	struct virtq_split_tunnel_comp tunnel_comp;
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

	tunnel_comp.descr_head_idx = cmd->descr_head_idx;
	tunnel_comp.len = cmd->total_in_len;
	virtq_log_data(cmd, "SEND_COMP: descr_head_idx %d len %d send_size %lu\n",
		       tunnel_comp.descr_head_idx, tunnel_comp.len,
		       sizeof(struct virtq_split_tunnel_comp));
	virtq_mark_dirty_mem(cmd, 0, 0, true);
	ret = snap_dma_q_send_completion(cmd->vq_priv->dma_q,
					 &tunnel_comp,
					 sizeof(struct virtq_split_tunnel_comp));
	if (snap_unlikely(ret)) {
		/* TODO: pending queue */
		ERR_ON_CMD(cmd, "failed to second completion\n");
		cmd->state = VIRTQ_CMD_STATE_FATAL_ERR;
	} else {
		cmd->state = VIRTQ_CMD_STATE_RELEASE;
		++cmd->vq_priv->ctrl_used_index;
	}

	return true;
}

static void virtq_rel_req_dbuf(struct virtq_cmd *cmd)
{
	ibv_dereg_mr(cmd->req_mr);
	cmd->vq_priv->ops->release_cmd(cmd);
	cmd->req_buf = cmd->buf;
	cmd->req_mr = cmd->mr;
	cmd->use_dmem = false;
}

bool virtq_sm_release(struct virtq_cmd *cmd, enum virtq_cmd_sm_op_status status)
{
	bool repeat = false;

	if (snap_unlikely(cmd->use_dmem))
		virtq_rel_req_dbuf(cmd);
	if (cmd->vq_priv->use_mem_pool)
		cmd->vq_priv->ops->mem_pool_release(cmd);
	if (snap_unlikely(cmd->use_seg_dmem))
		repeat = cmd->vq_priv->ops->seg_dmem_release(cmd);
	cmd->vq_priv->cmd_cntr--;

	return repeat;
}

bool virtq_sm_fatal_error(struct virtq_cmd *cmd, enum virtq_cmd_sm_op_status status)
{
	if (snap_unlikely(cmd->use_dmem))
		virtq_rel_req_dbuf(cmd);
	if (cmd->vq_priv->use_mem_pool)
		cmd->vq_priv->ops->mem_pool_release(cmd);
	if (snap_unlikely(cmd->use_seg_dmem))
		cmd->vq_priv->ops->seg_dmem_release(cmd);
	cmd->vq_priv->vq_ctx->fatal_err = -1;
	/*
	 * TODO: propagate fatal error to the controller.
	 * At the moment attempt to resume/state copy
	 * of such controller will have unpredictable
	 * results.
	 */

	return false;
}


static int virtq_progress_suspend(struct virtq_common_ctx *q)
{
	struct virtq_priv *priv = q->priv;
	struct snap_virtio_common_queue_attr qattr = { };

	/* TODO: add option to ignore commands in the bdev layer */
	if (priv->cmd_cntr != 0)
		return 0;

	snap_dma_q_flush(priv->dma_q);

	qattr.vattr.state = SNAP_VIRTQ_STATE_SUSPEND;
	/* TODO: check with FLR/reset. I see modify fail where it should not */
	if (priv->ops->progress_suspend(priv->snap_vbq, &qattr))
		snap_error("queue %d: failed to move to the SUSPENDED state\n", q->idx);

	/* at this point QP is in the error state and cannot be used anymore */
	snap_info("queue %d: moving to the SUSPENDED state\n", q->idx);
	priv->swq_state = SW_VIRTQ_SUSPENDED;
	return 0;
}

/**
 * blk_virq_progress_unordered() - Check & complete unordered commands
 * @vq_priv:	queue to progress
 */
static void virtq_progress_unordered(struct virtq_priv *vq_priv)
{
	uint16_t cmd_idx = vq_priv->ctrl_used_index % vq_priv->vattr->size;
	struct virtq_cmd *cmd = vq_priv->ops->get_avail_cmd(vq_priv->cmd_arr,
			cmd_idx);

	while (cmd->state == VIRTQ_CMD_STATE_SEND_IN_ORDER_COMP
			&& cmd->cmd_available_index == cmd->vq_priv->ctrl_used_index) {
		virtq_log_data(cmd, "PEND_COMP: ino_num:%d state:%d\n",
				cmd->cmd_available_index, cmd->state);

		virtq_cmd_progress(cmd, VIRTQ_CMD_SM_OP_OK);

		cmd_idx = vq_priv->ctrl_used_index % vq_priv->vattr->size;
		cmd = vq_priv->ops->get_avail_cmd(vq_priv->cmd_arr, cmd_idx);
	}
}

/**
 * virtq_progress() - Progress RDMA QPs,  Polls on QPs CQs
 * @q:	queue to progress
 *
 * Context: Not thread safe
 *
 * Return: error code on failure, 0 on success
 */
int virtq_progress(struct virtq_common_ctx *q, int thread_id)
{
	struct virtq_priv *priv = q->priv;

	if (snap_unlikely(priv->swq_state == SW_VIRTQ_SUSPENDED))
		return 0;

	priv->thread_id = thread_id;
	snap_dma_q_progress(priv->dma_q);

	if (snap_unlikely(priv->force_in_order))
		virtq_progress_unordered(priv);

	/*
	 * need to wait until all inflight requests
	 * are finished before moving to the suspend state
	 */
	if (snap_unlikely(priv->swq_state == SW_VIRTQ_FLUSHING))
		return virtq_progress_suspend(q);

	return 0;
}

/**
 * virtq_start() - set virtq attributes used for operating
 * @q:		queue to start
 * @attr:	attrs used to start the quue
 *
 * Function set attributes queue needs in order to operate.
 *
 * Return: void
 */
void virtq_start(struct virtq_common_ctx *q, struct virtq_start_attr *attr)
{
	struct virtq_priv *priv = q->priv;

	priv->pg_id = attr->pg_id;
}

void virtq_destroy(struct virtq_common_ctx *q)
{
	if (q)
		free(q->priv);
}

/**
 * virtq_suspend() - Request moving queue to suspend state
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
int virtq_suspend(struct virtq_common_ctx *q)
{
	struct virtq_priv *priv = q->priv;

	if (priv->swq_state != SW_VIRTQ_RUNNING) {
		snap_debug("queue %d: suspend was already requested\n", q->idx);
		return -EBUSY;
	}

	snap_info("queue %d: SUSPENDING %d command(s) outstanding\n", q->idx,
			priv->cmd_cntr);

	if (priv->vq_ctx->fatal_err)
		snap_warn("queue %d: fatal error. Resuming or live migration will not be possible\n", q->idx);

	priv->swq_state = SW_VIRTQ_FLUSHING;
	return 0;
}

/**
 * virtq_is_suspended() - api for checking if queue in suspended state
 * @q:		queue to check
 *
 * Context: Function is not thread safe with regard to virtq_progress
 * and virtq_suspend. If called from a different thread than
 * thread calling progress/suspend then application must take care of
 * proper locking
 *
 * Return: True when queue suspended, and False for not suspended
 */
bool virtq_is_suspended(struct virtq_common_ctx *q)
{
	struct virtq_priv *priv = q->priv;

	return priv->swq_state == SW_VIRTQ_SUSPENDED;
}
