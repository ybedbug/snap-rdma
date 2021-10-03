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

#ifndef SNAP_VIRTIO_FS_VIRTQ_H
#define SNAP_VIRTIO_FS_VIRTQ_H

#include "snap.h"
#include <sys/uio.h>
#include "snap_fs_ops.h"
#include "snap_virtio_common_ctrl.h"
#include "snap_virtio_fs.h"
#include "virtq_common.h"

struct fs_virtq_ctx {
	struct virtq_common_ctx common_ctx;
};

struct snap_virtio_fs_ctrl_queue;
struct fs_virtq_ctx *fs_virtq_create(struct snap_virtio_fs_ctrl_queue *vfsq,
				     struct snap_fs_dev_ops *fs_dev_ops,
				     void *fs_dev, struct snap_device *snap_dev,
				     struct virtq_create_attr *attr);
void fs_virtq_destroy(struct fs_virtq_ctx *q);
int fs_virtq_get_debugstat(struct fs_virtq_ctx *q,
			   struct snap_virtio_queue_debugstat *q_debugstat);
int fs_virtq_query_error_state(struct fs_virtq_ctx *q,
			       struct snap_virtio_common_queue_attr *attr);
int fs_virtq_get_state(struct fs_virtq_ctx *q,
		       struct snap_virtio_ctrl_queue_state *state);

struct fs_virtq_ctx *to_fs_ctx(void *ctx);
/* debug */
struct snap_dma_q *fs_get_dma_q(struct fs_virtq_ctx *ctx);
int fs_set_dma_mkey(struct fs_virtq_ctx *ctx, uint32_t mkey);
#endif
