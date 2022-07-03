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
#include "snap_dp_map.h"

#define SNAP_DMA_Q_OPMODE   "SNAP_DMA_Q_OPMODE"

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

void virtq_ctx_destroy(struct virtq_priv *vq_priv)
{
	snap_dma_q_destroy(vq_priv->dma_q);
	free(to_common_queue_attr(vq_priv->vattr));
	free(vq_priv);
}

/**
 * virtq_cmd_progress() - command state machine progress handle
 * @cmd:	command to be processed
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
	size_t len;
	int ret;
	struct vring_desc *descs = cmd->vq_priv->ops->get_descs(cmd);

	if (snap_unlikely(cmd->is_indirect)) {
		/*IN_ORDER is not support now, so we need to sort indirect table*/
		struct vring_desc *descs_tmp = calloc(1, cmd->indirect_len);
		struct vring_desc *descs_indrect = &descs[cmd->indirect_pos];
		uint16_t indirect_num = cmd->indirect_len/sizeof(struct vring_desc);
		int i = 0, j = 0;

		if (snap_unlikely(!descs_tmp)) {
			ERR_ON_CMD(cmd, "failed to malloc data for cmd\n");
			return VIRTQ_FETCH_DESC_ERR;
		}
		for (; i < indirect_num; i++) {
			memcpy(&descs_tmp[i], &descs_indrect[j], sizeof(struct vring_desc));
			if (descs_indrect[j].flags & VRING_DESC_F_NEXT)
				j = descs_indrect[j].next;
			else
				break;
		}
		cmd->num_desc += i+1;
		memcpy(descs_indrect, descs_tmp, cmd->indirect_len);
		free(descs_tmp);
		return VIRTQ_FETCH_DESC_DONE;
	}

	if (cmd->num_desc == 0) {
		in_ring_desc_addr = cmd->descr_head_idx % cmd->vq_priv->vattr->size;
		srcaddr = cmd->vq_priv->vattr->desc +
			  in_ring_desc_addr * sizeof(struct vring_desc);
		len = sizeof(struct vring_desc);
		/* TODO add some indication about this case */
	} else if (descs[cmd->num_desc - 1].flags & VRING_DESC_F_NEXT) {
		in_ring_desc_addr = descs[cmd->num_desc - 1].next;
		srcaddr = cmd->vq_priv->vattr->desc +
		  in_ring_desc_addr * sizeof(struct vring_desc);
		len = sizeof(struct vring_desc);
	} else if (descs[cmd->num_desc - 1].flags & VRING_DESC_F_INDIRECT) {
		srcaddr = descs[cmd->num_desc - 1].addr;
		len = descs[cmd->num_desc - 1].len;
		cmd->num_desc--;
		cmd->is_indirect = true;
		cmd->indirect_pos = cmd->num_desc;
		cmd->indirect_len = len;
	} else
		return VIRTQ_FETCH_DESC_DONE;

	if (cmd->vq_priv->ops->seg_dmem(cmd))
		return VIRTQ_FETCH_DESC_ERR;

	cmd->dma_comp.count = 1;
	virtq_log_data(cmd, "READ_DESC: pa 0x%lx len %lu\n", srcaddr, sizeof(struct vring_desc));
	ret = snap_dma_q_read(cmd->vq_priv->dma_q, &cmd->vq_priv->ops->get_descs(cmd)[cmd->num_desc],
			len, cmd->aux_mr->lkey, srcaddr,
			cmd->vq_priv->vattr->dma_mkey,
			&(cmd->dma_comp));
	if (ret)
		return VIRTQ_FETCH_DESC_ERR;
	/* Note: the num_desc should be incremented in case the success completion only.
	 * The completion result is tested in virtq_sm_fetch_cmd_descs
	 */
	if (!cmd->is_indirect)
		cmd->num_desc++;
	++cmd->vq_priv->cmd_cntrs.outstanding_to_host;
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
	enum virtq_fetch_desc_status ret;

	if (status != VIRTQ_CMD_SM_OP_OK) {
		--cmd->num_desc;
		ERR_ON_CMD(cmd, "failed to fetch commands descs - num_desc: %ld, dumping command without response\n",
			   cmd->num_desc);
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
	if (vq->ctrl->lm_channel) {
		rc = snap_channel_mark_dirty_page(vq->ctrl->lm_channel, pa, len);
	} else if (vq->ctrl->dp_map) {
		int64_t total_size = len;
		/* this is what can be written inline: 8 bytes, 32k for pagemap 128k
		 * for bitmap
		 */
		char pbuf[sizeof(struct virtq_split_tunnel_comp)];
		int b_off;
		uint32_t size;
		uint64_t start_pa;

		rc = 0;
		memset(pbuf, 0xFF, sizeof(pbuf));
		do {
			rc = snap_dp_bmap_get_start_pa(vq->ctrl->dp_map, pa, len, &start_pa, &b_off, &size);
			if (rc < 0)
				break;

			virtq_log_data(cmd, "MARK_DIRTY_MEM: start_pa 0x%lx n_bytes %d byte_len %u\n", start_pa, size, rc);
			total_size -= rc;
			/*
			 * Amount of pages we can set is defined by the max inline
			 * We need to do more writes for bigger buffers.
			 * TODO: handle -EAGAIN, force flush in such case
			 */
			do {
				size_t to_write = size <= sizeof(pbuf) ? size : sizeof(pbuf);

				rc = snap_dma_q_write_short(cmd->vq_priv->dma_q, pbuf,
						to_write,
						start_pa,
						snap_dp_bmap_get_mkey(vq->ctrl->dp_map));
				if (rc < 0) {
					ERR_ON_CMD(cmd, "rdma_write failed: %d\n", rc);
					goto done;
				}
				size -= to_write;
			} while (size > 0);

			/* most probably region will stay in the single memory range */
		} while (total_size > 0);

	} else {
		ERR_ON_CMD(cmd, "dirty memory logging enabled but migration channel is not present\n");
		return;
	}
done:
	if (rc)
		ERR_ON_CMD(cmd, "mark dirty page failed: pa 0x%lx len %u\n", pa, len);
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

int virtq_tunnel_send_comp(struct virtq_cmd *cmd, struct snap_dma_q *q)
{
	struct virtq_split_tunnel_comp tunnel_comp;
	int ret;

	tunnel_comp.descr_head_idx = cmd->descr_head_idx;
	tunnel_comp.len = cmd->total_in_len;
	virtq_log_data(cmd, "SEND_COMP: descr_head_idx %d len %d send_size %lu\n",
		       tunnel_comp.descr_head_idx, tunnel_comp.len,
		       sizeof(struct virtq_split_tunnel_comp));
	ret = snap_dma_q_send_completion(cmd->vq_priv->dma_q,
					 &tunnel_comp,
					 sizeof(struct virtq_split_tunnel_comp));

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

static void virtq_rel_req_dbuf(struct virtq_cmd *cmd)
{
	ibv_dereg_mr(cmd->req_mr);
	cmd->vq_priv->ops->release_cmd(cmd);
	cmd->req_buf = cmd->buf;
	cmd->req_mr = cmd->mr;
	cmd->use_dmem = false;
}

static bool virtq_common_release(struct virtq_cmd *cmd)
{
	bool repeat = false;

	if (snap_unlikely(cmd->use_dmem))
		virtq_rel_req_dbuf(cmd);
	if (cmd->vq_priv->use_mem_pool)
		cmd->vq_priv->ops->mem_pool_release(cmd);
	if (snap_unlikely(cmd->use_seg_dmem))
		repeat = cmd->vq_priv->ops->seg_dmem_release(cmd);

	return repeat;
}

bool virtq_sm_release(struct virtq_cmd *cmd, enum virtq_cmd_sm_op_status status)
{
	bool repeat = false;

	repeat = virtq_common_release(cmd);
	--cmd->vq_priv->cmd_cntrs.outstanding_total;
	return repeat;
}

bool virtq_sm_fatal_error(struct virtq_cmd *cmd, enum virtq_cmd_sm_op_status status)
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

static void virtq_progress_suspend(struct virtq_common_ctx *q)
{
	struct virtq_priv *priv = q->priv;
	struct snap_virtio_common_queue_attr qattr = { };
	int n;

	/* TODO: add option to ignore commands in the bdev layer */
	if (!virtq_check_outstanding_progress_suspend(priv))
		return;

	n = snap_dma_q_flush(priv->dma_q);

	qattr.vattr.state = SNAP_VIRTQ_STATE_SUSPEND;
	/* TODO: check with FLR/reset. I see modify fail where it should not */
	if (priv->ops->progress_suspend(priv->snap_vbq, &qattr))
		snap_error("ctrl %p queue %d: failed to move to the SUSPENDED state\n", priv->vbq->ctrl, q->idx);

	/* at this point QP is in the error state and cannot be used anymore */
	snap_info("ctrl %p queue %d: moving to the SUSPENDED state (q_flush %d)\n", priv->vbq->ctrl, q->idx, n);
	priv->swq_state = SW_VIRTQ_SUSPENDED;
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
	int n = 0;

	if (snap_unlikely(priv->swq_state == SW_VIRTQ_SUSPENDED))
		goto out;

	priv->thread_id = thread_id;
	n += snap_dma_q_progress(priv->dma_q);

#ifdef VIRTIO_QUEUE_PROGRESS_ENABLED
	if (priv->snap_vbq->q_ops->progress)
		n += priv->snap_vbq->q_ops->progress(priv->snap_vbq);
#endif
	if (snap_unlikely(priv->force_in_order))
		virtq_progress_unordered(priv);

	/*
	 * need to wait until all inflight requests
	 * are finished before moving to the suspend state
	 */
	if (snap_unlikely(priv->swq_state == SW_VIRTQ_FLUSHING))
		virtq_progress_suspend(q);

out:
	return n;
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

	snap_info("ctrl %p queue %d: SUSPENDING command(s) - in %d bdev %d host %d fatal %d\n",
			priv->vbq->ctrl, q->idx,
			priv->cmd_cntrs.outstanding_total, priv->cmd_cntrs.outstanding_in_bdev,
			priv->cmd_cntrs.outstanding_to_host, priv->cmd_cntrs.fatal);

	if (priv->vq_ctx->fatal_err)
		snap_warn("ctrl %p queue %d: fatal error. Resuming or live migration will not be possible\n",
			  priv->vbq->ctrl, q->idx);

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

/**
 * virtq_rx_cb_common_set() - common setter for new command received from host
 * @vq_priv:	virtqueue command belongs to, private context
 * @data:	pointer to data sent for the command - should be
 *
 * Return: pointer to virtq_cmd command
 */
struct virtq_cmd *
virtq_rx_cb_common_set(struct virtq_priv *vq_priv, const void *data)
{
	int cmd_idx;
	struct virtq_cmd *cmd;
	struct virtq_split_tunnel_req_hdr *split_hdr = (struct virtq_split_tunnel_req_hdr *)data;

	if (vq_priv->force_in_order)
		cmd_idx = vq_priv->ctrl_available_index % vq_priv->vattr->size;
	else
		cmd_idx = split_hdr->descr_head_idx % vq_priv->vattr->size;
	cmd = vq_priv->ops->get_avail_cmd(vq_priv->cmd_arr, cmd_idx);
	cmd->num_desc = split_hdr->num_desc;
	cmd->descr_head_idx = split_hdr->descr_head_idx;
	cmd->total_seg_len = 0;
	cmd->total_in_len = 0;
	cmd->vq_priv->ops->clear_status(cmd);
	cmd->use_dmem = false;
	cmd->use_seg_dmem = false;
	cmd->req_buf = cmd->buf;
	cmd->req_mr = cmd->mr;
	cmd->cmd_available_index = vq_priv->ctrl_available_index;
	cmd->is_indirect = false;
	return cmd;
}

static int arrange_descs(const struct vring_desc *descs_table, struct vring_desc *arranged, int header_idx)
{
	struct vring_desc curr = descs_table[header_idx];
	int i = 0;

	arranged[i] = curr;
	i++;
	while (curr.flags & VRING_DESC_F_NEXT) {
		curr = descs_table[curr.next];
		arranged[i] = curr;
		i++;
	}

	return i;
}


/**
 * virtq_rx_cb_common_proc() - common processing api for new command received from host
 * @cmd:	virtqueue command received from virtq_rx_cb_common_set
 * @data:	pointer to data sent for the command - should be
 *		command header and optional descriptor list
 * @data_len:	length of data
 * @imm_data:	immediate data
 *
 * Return: True when command sent to sm for processing, False if command was dropped.
 */
bool virtq_rx_cb_common_proc(struct virtq_cmd *cmd, const void *data,
			     uint32_t data_len, uint32_t imm_data)
{
	enum virtq_cmd_sm_op_status status = VIRTQ_CMD_SM_OP_OK;
	const struct virtq_split_tunnel_req_hdr *split_hdr = (const struct virtq_split_tunnel_req_hdr *)data;

	/* If new commands are not dropped there is a risk of never
	 * completing the flush
	 **/
	if (snap_unlikely(cmd->vq_priv->swq_state == SW_VIRTQ_FLUSHING)) {
		virtq_log_data(cmd, "DROP_CMD: %ld inline descs, rxlen %d\n",
			       cmd->num_desc, data_len);
		return false;
	}

	if (split_hdr->num_desc) {
		const void *tunn_descs = data + sizeof(struct virtq_split_tunnel_req_hdr);
		struct vring_desc *aux_descs = cmd->vq_priv->ops->get_descs(cmd);
		int len = sizeof(struct vring_desc) * split_hdr->num_desc;

		memcpy(aux_descs, tunn_descs, len);
	}

	if (split_hdr->dpa_vq_table_flag == VQ_TABLE_REC) {
		const void *data_descs = data + sizeof(struct virtq_split_tunnel_req_hdr);
		struct vring_desc *aux_descs = cmd->vq_priv->ops->get_descs(cmd);

		cmd->num_desc = arrange_descs(data_descs, aux_descs, split_hdr->descr_head_idx);
	}

	++cmd->vq_priv->cmd_cntrs.outstanding_total;
	++cmd->vq_priv->ctrl_available_index;
	cmd->state = VIRTQ_CMD_STATE_FETCH_CMD_DESCS;
	virtq_log_data(cmd, "NEW_CMD: %lu inline descs, rxlen %u\n", cmd->num_desc, data_len);
	virtq_cmd_progress(cmd, status);
	return true;
}

void virtq_reg_mr_fail_log_error(const struct virtq_cmd *cmd)
{
	struct snap_virtio_ctrl_queue *vq = cmd->vq_priv->vbq;

	/* On failure, errno indicates the failure reason */
	snap_error("failed to register mr: ctrl %p queue %d cmd %p cmd_idx %d - error %d (%s)\n",
		   vq->ctrl, cmd->vq_priv->vq_ctx->idx, cmd, cmd->idx, errno, strerror(errno));
}
