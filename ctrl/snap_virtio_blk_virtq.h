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

/**
 * struct blk_virtq_ctx - Main struct for blk_virtq
 * @idx:	Virtqueue index
 * @fatal_err:	Fatal error flag
 * @priv:	Opaque privte struct used for implementation
 */
struct blk_virtq_ctx {
	int idx;
	bool fatal_err;
	void *priv;
};

/**
 * struct blk_virtq_create_attr - Attributes given for virtq creation
 * @idx:	Virtqueue index
 * @size_max:	VIRTIO_BLK_F_SIZE_MAX (from virtio spec)
 * @seg_max:	VIRTIO_BLK_F_SEG_MAX (from virtio spec)
 * @queue_size:	VIRTIO_QUEUE_SIZE (from virtio spec)
 * @pd:		Protection domain on which rdma-qps will be opened
 * @desc:	Descriptor Area (from virtio spec Virtqueues section)
 * @driver	Driver Area
 * @device	Device Area
 */
struct blk_virtq_create_attr {
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
};

struct blk_virtq_ctx *blk_virtq_create(struct snap_bdev_ops *bdev_ops,
				       void *bdev, struct snap_device *snap_dev,
				       struct blk_virtq_create_attr *attr);
void blk_virtq_destroy(struct blk_virtq_ctx *q);
int blk_virtq_progress(struct blk_virtq_ctx *q);
int blk_virtq_suspend(struct blk_virtq_ctx *q);
bool blk_virtq_is_suspended(struct blk_virtq_ctx *q);

/* debug */
struct snap_dma_q *get_dma_q(struct blk_virtq_ctx *ctx);
int set_dma_mkey(struct blk_virtq_ctx *ctx, uint32_t mkey);
#endif
