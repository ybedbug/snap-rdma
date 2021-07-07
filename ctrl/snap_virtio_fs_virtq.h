/*
 * Copyright (c) 2021 NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
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
void fs_virtq_start(struct fs_virtq_ctx *q,
		    struct virtq_start_attr *attr);
int fs_virtq_progress(struct fs_virtq_ctx *q);
int fs_virtq_get_debugstat(struct fs_virtq_ctx *q,
			   struct snap_virtio_queue_debugstat *q_debugstat);
int fs_virtq_query_error_state(struct fs_virtq_ctx *q,
			       struct snap_virtio_fs_queue_attr *attr);
int fs_virtq_suspend(struct fs_virtq_ctx *q);
bool fs_virtq_is_suspended(struct fs_virtq_ctx *q);
int fs_virtq_get_state(struct fs_virtq_ctx *q,
		       struct snap_virtio_ctrl_queue_state *state);

/* debug */
struct snap_dma_q *fs_get_dma_q(struct fs_virtq_ctx *ctx);
int fs_set_dma_mkey(struct fs_virtq_ctx *ctx, uint32_t mkey);
#endif
