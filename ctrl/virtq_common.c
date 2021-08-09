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
	vq_priv->blk_dev.ctx = bdev;
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
