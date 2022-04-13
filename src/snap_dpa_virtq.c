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
#include <stdlib.h>

#include "snap_macros.h"
#include "config.h"
#include "snap_virtio_blk.h"
#include "snap_dpa_p2p.h"
#include "snap_dpa_virtq.h"
#include "snap_dpa_rt.h"

#if HAVE_FLEXIO
#include "snap_dpa.h"
#include "snap_dpa_virtq.h"


#define SNAP_DPA_VIRTQ_BBUF_ALIGN 4096

/* make sure our virtq commands are fit into mailbox */
SNAP_STATIC_ASSERT(sizeof(struct dpa_virtq_cmd) < SNAP_DMA_THREAD_MBOX_CMD_SIZE,
		"Ooops, struct dpa_virtq_cmd is too big");

struct snap_dpa_virtq_attr {
	// hack to fit into 'standard' type
	size_t type_size;
};

static struct snap_dpa_virtq *snap_dpa_virtq_create(struct snap_device *sdev,
		struct snap_dpa_virtq_attr *dpa_vq_attr, struct snap_virtio_common_queue_attr *vq_attr)
{
	/* TODO: should get these from the upper layer */
	struct snap_dpa_rt_filter f = {
		.mode = SNAP_DPA_RT_THR_POLLING,
		.queue_mux_mode = SNAP_DPA_RT_THR_SINGLE
	};
	struct snap_dpa_rt_attr attr = {};

	struct snap_dpa_virtq *vq;
	void *mbox;
	struct dpa_virtq_cmd *cmd;
	struct snap_dpa_rsp *rsp;
	size_t desc_shadow_size;
	int ret;

	snap_debug("create dpa virtq\n");

	vq = calloc(1, sizeof(*vq));
	if (!vq)
		return NULL;

	vq->rt = snap_dpa_rt_get(sdev->sctx->context, SNAP_DPA_VIRTQ_APP, &attr);
	if (!vq->rt)
		goto free_vq;

	vq->rt_thr = snap_dpa_rt_thread_get(vq->rt, &f);
	if (!vq->rt_thr)
		goto put_rt;

	/* pass queue data to the worker */
	mbox = snap_dpa_thread_mbox_acquire(vq->rt_thr->thread);

	cmd = (struct dpa_virtq_cmd *)snap_dpa_mbox_to_cmd(mbox);
	//sleep(1);
	snap_dpa_log_print(vq->rt_thr->thread->dpa_log);
	//printf("wait1...\n"); getchar();
	/* at the momemnt pass only avail/used/descr addresses */
	vq->common.idx = vq_attr->vattr.idx;
	vq->common.size = vq_attr->vattr.size;
	vq->common.desc = vq_attr->vattr.desc;
	vq->common.driver = vq_attr->vattr.driver;
	vq->common.device = vq_attr->vattr.device;

	/* register mr for the avail staging buffer */
	desc_shadow_size = vq->common.size * sizeof(struct virtq_desc);

	ret = posix_memalign((void **)&vq->desc_shadow, SNAP_DPA_VIRTQ_DESC_SHADOW_ALIGN, desc_shadow_size);
	if (ret) {
		snap_error("Failed to allocate virtq dpa window buffer: %d\n", ret);
		goto release_mbox;
	}

	memset(vq->desc_shadow, 0, desc_shadow_size);

	/* TODO: rename to descr staging buffer */
	vq->desc_shadow_mr = snap_reg_mr(vq->rt->dpa_proc->pd, vq->desc_shadow, desc_shadow_size);
	if (!vq->desc_shadow_mr) {
		snap_error("Failed to allocate virtq dpa window mr: %m\n");
		goto free_dpa_window;
	}

	memset(&cmd->cmd_create, 0, sizeof(cmd->cmd_create));
	memcpy(&cmd->cmd_create.vq.common, &vq->common, sizeof(vq->common));
	cmd->cmd_create.vq.hw_available_index = vq_attr->hw_available_index;
	cmd->cmd_create.vq.hw_used_index = vq_attr->hw_used_index;
	cmd->cmd_create.vq.dpu_desc_shadow_mkey = vq->desc_shadow_mr->lkey;
	cmd->cmd_create.vq.dpu_desc_shadow_addr = (uint64_t)vq->desc_shadow;

	vq->cross_mkey = snap_create_cross_mkey(vq->rt->dpa_proc->pd, sdev);
	if (!vq->cross_mkey) {
		printf("Failed to create virtq cross mkey\n");
		goto free_dpa_window_mr;
	}

	//sleep(1);
	//snap_dpa_log_print(vq->rt_thr->thread->dpa_log);
	//printf("wait... xmkey 0x%x\n", vq->cross_mkey->mkey); //getchar();

	cmd->cmd_create.vq.host_mkey = vq->cross_mkey->mkey;
	snap_dpa_cmd_send(vq->rt_thr->thread, &cmd->base, DPA_VIRTQ_CMD_CREATE);

	rsp = snap_dpa_rsp_wait(mbox);
	if (rsp->status != SNAP_DPA_RSP_OK) {
		snap_dpa_log_print(vq->rt_thr->thread->dpa_log);
		snap_error("Failed to create DPA virtio queue: %d\n", rsp->status);
		goto free_dpa_window_mr;
	}

	snap_dpa_thread_mbox_release(vq->rt_thr->thread);
	return vq;

free_dpa_window_mr:
	ibv_dereg_mr(vq->desc_shadow_mr);
free_dpa_window:
	free(vq->desc_shadow);
release_mbox:
	snap_dpa_thread_mbox_release(vq->rt_thr->thread);
	snap_dpa_rt_thread_put(vq->rt_thr);
put_rt:
	snap_dpa_rt_put(vq->rt);
free_vq:
	free(vq);
	return NULL;
}

static void snap_dpa_virtq_destroy(struct snap_dpa_virtq *vq)
{
	void *mbox;
	struct dpa_virtq_cmd *cmd;
	struct snap_dpa_rsp *rsp;

	snap_info("destroy dpa virtq\n");
	snap_dpa_log_print(vq->rt_thr->thread->dpa_log);
	mbox = snap_dpa_thread_mbox_acquire(vq->rt_thr->thread);

	cmd = (struct dpa_virtq_cmd *)snap_dpa_mbox_to_cmd(mbox);
	//printf("wait... b4 destroy command\n");getchar();
	snap_dpa_cmd_send(vq->rt_thr->thread, &cmd->base, DPA_VIRTQ_CMD_DESTROY);

	rsp = snap_dpa_rsp_wait(mbox);
	if (rsp->status != SNAP_DPA_RSP_OK)
		snap_error("Failed to destroy DPA virtio queue: %d\n", rsp->status);

	snap_dpa_thread_mbox_release(vq->rt_thr->thread);
	snap_dpa_log_print(vq->rt_thr->thread->dpa_log);
	//printf("wait... a4 destroy command\n");getchar();
	snap_dpa_rt_thread_put(vq->rt_thr);
	snap_dpa_rt_put(vq->rt);
	snap_destroy_cross_mkey(vq->cross_mkey);
	ibv_dereg_mr(vq->desc_shadow_mr);
	free(vq->desc_shadow);
	free(vq);
}

int snap_dpa_virtq_query(struct snap_dpa_virtq *vq,
		struct snap_virtio_common_queue_attr *attr)
{
#if 0
	/* TODO */
	struct virtq_device_ring *avail_ring;

	avail_ring = (struct virtq_device_ring *)vq->dpa_window;
	attr->hw_available_index = avail_ring->idx;
#endif
	snap_warn("DPA query is not supported. Returning OK\n");
	return 0;
}

struct snap_virtio_queue *virtq_blk_dpa_create(struct snap_device *sdev,
		struct snap_virtio_common_queue_attr *attr)
{
	struct snap_dpa_virtq *vq;
	struct snap_dpa_virtq_attr dpa_attr = {};

	dpa_attr.type_size = sizeof(struct snap_virtio_queue);
	vq = snap_dpa_virtq_create(sdev, &dpa_attr, attr);

	return (struct snap_virtio_queue *)vq;
}

int virtq_blk_dpa_destroy(struct snap_virtio_queue *vbq)
{
	struct snap_dpa_virtq *vq;

	vq = (struct snap_dpa_virtq *)vbq;
	snap_dpa_virtq_destroy(vq);
	return 0;
}

int virtq_blk_dpa_query(struct snap_virtio_queue *vbq,
		struct snap_virtio_common_queue_attr *attr)
{
	struct snap_dpa_virtq *vq;

	vq = (struct snap_dpa_virtq *)vbq;
	return snap_dpa_virtq_query(vq, attr);
}

int virtq_blk_dpa_modify(struct snap_virtio_queue *vbq,
		uint64_t mask, struct snap_virtio_common_queue_attr *attr)
{
	snap_warn("DPA modify is not supported. Returning OK\n");
	return 0;
}

#else

static struct snap_virtio_queue *virtq_blk_dpa_create(struct snap_device *sdev,
		struct snap_virtio_common_queue_attr *attr)
{
	return NULL;
}

static int virtq_blk_dpa_destroy(struct snap_virtio_queue *vbq)
{
	return -1;
}

static int virtq_blk_dpa_query(struct snap_virtio_queue *vq,
		struct snap_virtio_common_queue_attr *attr)
{
	return -1;
}

static int virtq_blk_dpa_modify(struct snap_virtio_queue *vq,
		uint64_t mask, struct snap_virtio_common_queue_attr *attr)
{
	return -1;
}

#endif

struct snap_dpa_virtq *to_dpa_queue(struct snap_virtio_queue *vq)
{
	return container_of(vq, struct snap_dpa_virtq, vq);
}

static void vq_heads_msg_handle(struct snap_dpa_virtq *vq,
		struct snap_dpa_p2p_msg_vq_update *msg)
{
	struct virtq_split_tunnel_req_hdr hdr;
	int i;

	hdr.num_desc = 0;

	for (i = 0; i < msg->descr_head_count; i++) {
		hdr.descr_head_idx = msg->descr_heads[i];
		vq->rt_thr->dpu_cmd_chan.dma_q->rx_cb(vq->rt_thr->dpu_cmd_chan.dma_q, &hdr, 0, 0);
	}
}

static void vq_table_msg_handle(struct snap_dpa_virtq *vq,
		struct snap_dpa_p2p_msg_vq_update *msg)
{
	struct virtq_split_tunnel_req req;
	int i;

	req.hdr.num_desc = 0;
	/* TODO: use our own vring desc type */
	req.tunnel_descs = (struct vring_desc *)vq->desc_shadow;
	req.hdr.dpa_vq_table_flag = VQ_TABLE_REC;
	for (i = 0; i < msg->descr_head_count; i++) {
		 /* TODO: arrange descriptors in parallel and avoid extra memcpy */
		req.hdr.descr_head_idx = msg->descr_heads[i];
		vq->rt_thr->dpu_cmd_chan.dma_q->rx_cb(vq->rt_thr->dpu_cmd_chan.dma_q, &req, 0, 0);
	}
}

static int snap_virtio_blk_progress_dpa_queue(struct snap_virtio_queue *vq)
{
	struct snap_dpa_virtq *dpa_q = to_dpa_queue(vq);
	int n, i;
#ifdef __COVERITY__
	struct snap_dpa_p2p_msg msgs[64] = {};
#else
	struct snap_dpa_p2p_msg msgs[64];
#endif

	n = snap_dpa_p2p_recv_msg(&dpa_q->rt_thr->dpu_cmd_chan, msgs, 64);
	for (i = 0; i < n; i++) {
		dpa_q->rt_thr->dpu_cmd_chan.credit_count += msgs[i].base.credit_delta;
		switch (msgs[i].base.type) {
		case SNAP_DPA_P2P_MSG_CR_UPDATE:
			break;
		case SNAP_DPA_P2P_MSG_VQ_HEADS:
			vq_heads_msg_handle(dpa_q, (struct snap_dpa_p2p_msg_vq_update *) &msgs[i]);
			break;
		case SNAP_DPA_P2P_MSG_VQ_TABLE:
			vq_table_msg_handle(dpa_q, (struct snap_dpa_p2p_msg_vq_update *) &msgs[i]);
			break;
		default:
			snap_error("invalid message type %d\n", msgs[i].base.type);
			break;
		}
	}
	if (n)
		snap_dpa_p2p_send_cr_update(&dpa_q->rt_thr->dpu_cmd_chan, n);

	return 0;
}

struct virtq_q_ops snap_virtq_blk_dpa_ops = {
	.create = virtq_blk_dpa_create,
	.destroy = virtq_blk_dpa_destroy,
	.query = virtq_blk_dpa_query,
	.modify = virtq_blk_dpa_modify,
	.progress = snap_virtio_blk_progress_dpa_queue
};

struct virtq_q_ops *get_dpa_queue_ops(void)
{
	return &snap_virtq_blk_dpa_ops;
}
