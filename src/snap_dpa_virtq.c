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

#include "config.h"
#include "snap_virtio_blk.h"

#if HAVE_FLEXIO
#include "snap_dpa.h"
#include "snap_dpa_virtq.h"

static struct snap_dpa_app dpa_virtq_app = SNAP_DPA_APP_INIT_ATTR;

struct snap_dpa_virtq {
	struct snap_dpa_thread *dpa_worker;
};

_Static_assert(sizeof(struct snap_dpa_virtq) < sizeof(struct snap_virtio_queue),
		"Ooops, struct snap_dpa_virtq is too big");

/* make sure our virtq commands are fit into mailbox */
_Static_assert(sizeof(struct dpa_virtq_cmd) < SNAP_DPA_THREAD_MBOX_LEN/2,
		"Ooops, struct dpa_virtq_cmd is too big");

struct snap_dpa_virtq_attr {
	// hack to fit into 'standard' type
	size_t type_size;
};

static struct snap_dpa_virtq *snap_dpa_virtq_create(struct snap_device *sdev, struct snap_dpa_virtq_attr *attr)
{
	struct snap_dpa_virtq *vq;
	void *mbox;
	struct dpa_virtq_cmd *cmd;
	struct snap_dpa_rsp *rsp;

	struct snap_dpa_app_attr app_attr = {
		.sctx = sdev->sctx,
		.name = "dpa_virtq_split",
		.n_workers = 1
	};

	snap_info("create dpa virtq\n");

	if (snap_dpa_app_start(&dpa_virtq_app, &app_attr))
		return NULL;

	vq = calloc(1, attr->type_size);
	if (!vq)
		return NULL;

	/* TODO: scedule vq to the worker */
	vq->dpa_worker = dpa_virtq_app.dpa_workers[0];

	/* pass queue data to the worker */
	mbox = snap_dpa_thread_mbox_acquire(vq->dpa_worker);

	cmd = (struct dpa_virtq_cmd *)snap_dpa_mbox_to_cmd(mbox);
	snap_dpa_cmd_send(&cmd->base, DPA_VIRTQ_CMD_CREATE);

	rsp = snap_dpa_rsp_wait(mbox);
	if (rsp->status != SNAP_DPA_RSP_OK) {
		snap_dpa_thread_mbox_release(vq->dpa_worker);
		snap_error("Failed to create DPA virtio queue: %d\n", rsp->status);
		free(vq);
		return NULL;
	}

	snap_dpa_thread_mbox_release(vq->dpa_worker);
	return vq;
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

	free(vq);
	snap_dpa_app_stop(&dpa_virtq_app);
}

struct snap_virtio_blk_queue *virtq_blk_dpa_create(struct snap_device *sdev,
		struct snap_virtio_common_queue_attr *attr)
{
	struct snap_dpa_virtq *vq;
	struct snap_dpa_virtq_attr dpa_attr = {};

	dpa_attr.type_size = sizeof(struct snap_virtio_blk_queue);
	vq = snap_dpa_virtq_create(sdev, &dpa_attr);

	return (struct snap_virtio_blk_queue *)vq;
}

int virtq_blk_dpa_destroy(struct snap_virtio_blk_queue *vbq)
{
	struct snap_dpa_virtq *vq;

	vq = (struct snap_dpa_virtq *)vbq;
	snap_dpa_virtq_destroy(vq);
	return 0;
}

int virtq_blk_dpa_query(struct snap_virtio_blk_queue *vbq,
		struct snap_virtio_common_queue_attr *attr)
{
	return -1;
}

int virtq_blk_dpa_modify(struct snap_virtio_blk_queue *vbq,
		uint64_t mask, struct snap_virtio_common_queue_attr *attr)
{
	return -1;
}

#else

static struct snap_virtio_blk_queue *virtq_blk_dpa_create(struct snap_device *sdev,
		struct snap_virtio_common_queue_attr *attr)
{
	return NULL;
}

static int virtq_blk_dpa_destroy(struct snap_virtio_blk_queue *vbq)
{
	return -1;
}

static int virtq_blk_dpa_query(struct snap_virtio_blk_queue *vbq,
		struct snap_virtio_common_queue_attr *attr)
{
	return -1;
}

static int virtq_blk_dpa_modify(struct snap_virtio_blk_queue *vbq,
		uint64_t mask, struct snap_virtio_common_queue_attr *attr)
{
	return -1;
}

#endif

struct blk_virtq_q_ops snap_virtq_blk_dpa_ops = {
	.create = virtq_blk_dpa_create,
	.destroy = virtq_blk_dpa_destroy,
	.query = virtq_blk_dpa_query,
	.modify = virtq_blk_dpa_modify,
};
