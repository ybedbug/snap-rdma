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

#include "config.h"
#include "snap_virtio_blk.h"

#if HAVE_FLEXIO
#include "snap_dpa.h"
#include "snap_dpa_virtq.h"

static struct snap_dpa_app dpa_virtq_app = SNAP_DPA_APP_INIT_ATTR;

#define SNAP_DPA_VIRTQ_BBUF_SIZE  4096
#define SNAP_DPA_VIRTQ_BBUF_ALIGN 4096

SNAP_STATIC_ASSERT(sizeof(struct snap_dpa_virtq) < sizeof(struct snap_virtio_queue),
		"Ooops, struct snap_dpa_virtq is too big");

/* make sure our virtq commands are fit into mailbox */
SNAP_STATIC_ASSERT(sizeof(struct dpa_virtq_cmd) < SNAP_DPA_THREAD_MBOX_LEN/2,
		"Ooops, struct dpa_virtq_cmd is too big");

struct snap_dpa_virtq_attr {
	// hack to fit into 'standard' type
	size_t type_size;
};

static struct snap_dpa_virtq *snap_dpa_virtq_create(struct snap_device *sdev,
		struct snap_dpa_virtq_attr *dpa_vq_attr, struct snap_virtio_common_queue_attr *vq_attr)
{
	struct snap_dpa_virtq *vq;
	void *mbox;
	struct dpa_virtq_cmd *cmd;
	struct snap_dpa_rsp *rsp;
	int ret;

	struct snap_dpa_app_attr app_attr = {
		.ctx = sdev->sctx->context,
		.name = "dpa_virtq_split",
		.n_workers = 1
	};

	snap_info("create dpa virtq\n");

	if (snap_dpa_app_start(&dpa_virtq_app, &app_attr))
		return NULL;

	vq = calloc(1, dpa_vq_attr->type_size);
	if (!vq)
		return NULL;

	/* TODO: scedule vq to the worker */
	vq->dpa_worker = dpa_virtq_app.dpa_workers[0];

	/* pass queue data to the worker */
	mbox = snap_dpa_thread_mbox_acquire(vq->dpa_worker);

	cmd = (struct dpa_virtq_cmd *)snap_dpa_mbox_to_cmd(mbox);
	/* convert vq_attr to the create command */
	memset(&cmd->cmd_create, 0, sizeof(cmd->cmd_create));
	/* at the momemnt pass only avail/used/descr addresses */
	cmd->cmd_create.idx = vq_attr->vattr.idx;
	cmd->cmd_create.size = vq_attr->vattr.size;

	cmd->cmd_create.desc = vq_attr->vattr.desc;
	cmd->cmd_create.driver = vq_attr->vattr.driver;
	cmd->cmd_create.device = vq_attr->vattr.device;

	/* register mr for the avail staging buffer */
	ret = posix_memalign(&vq->dpa_window, SNAP_DPA_VIRTQ_BBUF_ALIGN,
			SNAP_DPA_VIRTQ_BBUF_SIZE);
	if (ret) {
		snap_error("Failed to allocate virtq dpa window buffer: %d\n", ret);
		goto release_mbox;
	}

	vq->dpa_window_mr = snap_reg_mr(dpa_virtq_app.dctx->pd, vq->dpa_window, SNAP_DPA_VIRTQ_BBUF_SIZE);
	if (!vq->dpa_window_mr) {
		snap_error("Failed to allocate virtq dpa window mr: %m\n");
		goto free_dpa_window;
	}

	cmd->cmd_create.dpu_avail_mkey = vq->dpa_window_mr->lkey;
	cmd->cmd_create.dpu_avail_ring_addr = (uint64_t)vq->dpa_window;

	/* HACK!!! TODO: build cross gvmi mkey
	 * The hack allows us to run unit test on simx
	 * TODO: generate cros sgvmi mkey
	 */
	//cmd->cmd_create.host_mkey = ;
	vq->host_driver_mr = ibv_reg_mr(dpa_virtq_app.dctx->pd, (void *)vq_attr->vattr.device, 4096,
			IBV_ACCESS_LOCAL_WRITE|IBV_ACCESS_REMOTE_WRITE|IBV_ACCESS_REMOTE_READ);
	if (!vq->host_driver_mr) {
		printf("oops host_driver_mr\n");
		goto free_dpa_window_mr;
	}

	cmd->cmd_create.host_mkey = vq->host_driver_mr->lkey;
	snap_dpa_cmd_send(&cmd->base, DPA_VIRTQ_CMD_CREATE);

	rsp = snap_dpa_rsp_wait(mbox);
	if (rsp->status != SNAP_DPA_RSP_OK) {
		snap_error("Failed to create DPA virtio queue: %d\n", rsp->status);
		goto free_dpa_window_mr;
	}

	snap_dpa_thread_mbox_release(vq->dpa_worker);
	return vq;

free_dpa_window_mr:
	ibv_dereg_mr(vq->dpa_window_mr);
free_dpa_window:
	free(vq->dpa_window);
release_mbox:
	snap_dpa_thread_mbox_release(vq->dpa_worker);
	free(vq);
	return NULL;

}

static void snap_dpa_virtq_destroy(struct snap_dpa_virtq *vq)
{
	void *mbox;
	struct dpa_virtq_cmd *cmd;
	struct snap_dpa_rsp *rsp;

	snap_info("destroy dpa virtq\n");
	mbox = snap_dpa_thread_mbox_acquire(vq->dpa_worker);

	cmd = (struct dpa_virtq_cmd *)snap_dpa_mbox_to_cmd(mbox);
	snap_dpa_cmd_send(&cmd->base, DPA_VIRTQ_CMD_DESTROY);

	rsp = snap_dpa_rsp_wait(mbox);
	if (rsp->status != SNAP_DPA_RSP_OK) {
		snap_dpa_thread_mbox_release(vq->dpa_worker);
		snap_error("Failed to destroy DPA virtio queue: %d\n", rsp->status);
	}
	snap_dpa_thread_mbox_release(vq->dpa_worker);

	ibv_dereg_mr(vq->dpa_window_mr);
	ibv_dereg_mr(vq->host_driver_mr);
	free(vq->dpa_window);
	free(vq);
	snap_dpa_app_stop(&dpa_virtq_app);
}

int snap_dpa_virtq_query(struct snap_dpa_virtq *vq,
		struct snap_virtio_common_queue_attr *attr)
{
	struct virtq_device_ring *avail_ring;

	avail_ring = (struct virtq_device_ring *)vq->dpa_window;
	attr->hw_available_index = avail_ring->idx;
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
	return -1;
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

struct virtq_q_ops snap_virtq_blk_dpa_ops = {
	.create = virtq_blk_dpa_create,
	.destroy = virtq_blk_dpa_destroy,
	.query = virtq_blk_dpa_query,
	.modify = virtq_blk_dpa_modify,
};
