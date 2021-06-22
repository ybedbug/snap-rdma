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

/**
 * struct fs_virtq_ctx - Main struct for fs_virtq
 * @idx:	Virtqueue index
 * @fatal_err:	Fatal error flag
 * @priv:	Opaque privte struct used for implementation
 */
struct fs_virtq_ctx {
	int idx;
	bool fatal_err;
	void *priv;
};

/**
 * struct fs_virtq_create_attr - Attributes given for virtq creation
 *
 * @idx:	Virtqueue index
 * @size_max:	maximum size of any single segment
 * @seg_max:	maximum number of segments in a request
 * @queue_size:	maximum queue size supported by the device
 * @pd:		Protection domain on which rdma-qps will be opened
 * @desc:	Descriptor Area (from virtio spec Virtqueues section)
 * @driver:	Driver Area
 * @device:	Device Area
 *
 * @hw_available_index:	initial value of the driver available index.
 * @hw_used_index:	initial value of the device used index
 */
struct fs_virtq_create_attr {
	int idx;
	int size_max;
	int seg_max;
	int queue_size;
	struct ibv_pd *pd;
	uint64_t desc;
	uint64_t driver;
	uint64_t device;
	uint16_t max_tunnel_desc;
	uint16_t msix_vector;
	bool virtio_version_1_0;
	uint16_t hw_available_index;
	uint16_t hw_used_index;
	bool force_in_order;
};

struct fs_virtq_start_attr {
	int pg_id;
};

struct snap_virtio_fs_ctrl_queue;
struct fs_virtq_ctx *fs_virtq_create(struct snap_virtio_fs_ctrl_queue *vfsq,
				     struct snap_fs_dev_ops *fs_dev_ops,
				     void *fs_dev, struct snap_device *snap_dev,
				     struct fs_virtq_create_attr *attr);
void fs_virtq_destroy(struct fs_virtq_ctx *q);
void fs_virtq_start(struct fs_virtq_ctx *q,
		    struct fs_virtq_start_attr *attr);
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
