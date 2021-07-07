/*
 * Copyright (c) 2020 Mellanox Technologies, Inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef SNAP_VIRTIO_BLK_VIRTQ_H
#define SNAP_VIRTIO_BLK_VIRTQ_H

#include "snap.h"
#include <sys/uio.h>
#include "snap_blk_ops.h"
#include "snap_virtio_common_ctrl.h"
#include "snap_virtio_blk.h"
#include "virtq_common.h"

struct blk_virtq_ctx {
	struct virtq_common_ctx common_ctx;
	struct snap_virtio_ctrl_queue_stats io_stat;
};

struct snap_virtio_blk_ctrl_queue;
struct blk_virtq_ctx *blk_virtq_create(struct snap_virtio_blk_ctrl_queue *vbq,
				       struct snap_bdev_ops *bdev_ops,
				       void *bdev, struct snap_device *snap_dev,
				       struct virtq_create_attr *attr);
void blk_virtq_destroy(struct blk_virtq_ctx *q);
void blk_virtq_start(struct blk_virtq_ctx *q,
		     struct virtq_start_attr *attr);
int blk_virtq_progress(struct blk_virtq_ctx *q);
int blk_virtq_get_debugstat(struct blk_virtq_ctx *q,
			    struct snap_virtio_queue_debugstat *q_debugstat);
int blk_virtq_query_error_state(struct blk_virtq_ctx *q,
				struct snap_virtio_blk_queue_attr *attr);
void *blk_virtq_get_cmd_addr(void *ctx, void *ptr, size_t len);
struct snap_cross_mkey *blk_virtq_get_cross_mkey(void *ctx, struct ibv_pd *pd);
int blk_virtq_suspend(struct blk_virtq_ctx *q);
bool blk_virtq_is_suspended(struct blk_virtq_ctx *q);
int blk_virtq_get_state(struct blk_virtq_ctx *q,
			struct snap_virtio_ctrl_queue_state *state);
const struct snap_virtio_ctrl_queue_stats *
blk_virtq_get_io_stats(struct blk_virtq_ctx *q);

/* debug */
struct snap_dma_q *get_dma_q(struct blk_virtq_ctx *ctx);
int set_dma_mkey(struct blk_virtq_ctx *ctx, uint32_t mkey);
#endif
