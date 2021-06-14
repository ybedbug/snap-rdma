/*
 * Copyright (c) 2019 Mellanox Technologies, Inc.  All rights reserved.
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

#ifndef SNAP_VIRTIO_BLK_H
#define SNAP_VIRTIO_BLK_H

#include "snap_virtio_common.h"

struct snap_virtio_blk_device;

enum snap_virtio_blk_queue_modify {
	SNAP_VIRTIO_BLK_QUEUE_MOD_STATE	= 1 << 0,
};

struct snap_virtio_blk_queue_attr {
	uint64_t			modifiable_fields;//mask of snap_virtio_blk_queue_modify
	struct ibv_qp			*qp;
	uint16_t			hw_available_index;
	uint16_t			hw_used_index;

	struct snap_virtio_queue_attr   vattr;
};

struct snap_virtio_blk_queue {
	struct snap_virtio_queue	virtq;
	struct blk_virtq_q_ops		*q_ops;
	struct snap_virtio_blk_device	*vbdev;
};

struct snap_virtio_blk_device_attr {
	struct snap_virtio_device_attr		vattr;
	struct snap_virtio_blk_queue_attr	*q_attrs;
	unsigned int				queues;

	uint64_t				modifiable_fields;//mask of snap_virtio_dev_modify
	uint64_t				capacity;
	uint32_t				size_max;
	uint32_t				seg_max;
	uint32_t				blk_size;
	uint16_t				max_blk_queues;
	uint32_t				crossed_vhca_mkey;
};

struct snap_virtio_blk_device {
	uint32_t				num_queues;
	struct snap_virtio_blk_queue		*virtqs;
};

struct blk_virtq_q_ops {
	struct snap_virtio_blk_queue *(*create)(struct snap_device *sdev,
			struct snap_virtio_blk_queue_attr *attr);
	int (*destroy)(struct snap_virtio_blk_queue *vbq);
	int (*query)(struct snap_virtio_blk_queue *vbq,
			struct snap_virtio_blk_queue_attr *attr);
	int (*modify)(struct snap_virtio_blk_queue *vbq,
			uint64_t mask, struct snap_virtio_blk_queue_attr *attr);
};

enum {
	SNAP_HW_Q_PROVIDER = 0,
	SNAP_SW_Q_PROVIDER = 1,
	SNAP_DPA_Q_PROVIDER = 2,
};

int snap_virtio_blk_init_device(struct snap_device *sdev);
int snap_virtio_blk_teardown_device(struct snap_device *sdev);
int snap_virtio_blk_query_device(struct snap_device *sdev,
	struct snap_virtio_blk_device_attr *attr);
int snap_virtio_blk_modify_device(struct snap_device *sdev, uint64_t mask,
		struct snap_virtio_blk_device_attr *attr);
struct snap_virtio_blk_queue*
snap_virtio_blk_create_queue(struct snap_device *sdev,
	struct snap_virtio_blk_queue_attr *attr);
int snap_virtio_blk_destroy_queue(struct snap_virtio_blk_queue *vbq);
int snap_virtio_blk_query_queue(struct snap_virtio_blk_queue *vbq,
		struct snap_virtio_blk_queue_attr *attr);
int snap_virtio_blk_modify_queue(struct snap_virtio_blk_queue *vbq,
		uint64_t mask, struct snap_virtio_blk_queue_attr *attr);

static inline struct snap_virtio_blk_queue_attr*
to_blk_queue_attr(struct snap_virtio_queue_attr *vattr)
{
    return container_of(vattr, struct snap_virtio_blk_queue_attr,
			vattr);
}

static inline struct snap_virtio_blk_queue*
to_blk_queue(struct snap_virtio_queue *virtq)
{
    return container_of(virtq, struct snap_virtio_blk_queue, virtq);
}

static inline struct snap_virtio_blk_device_attr*
to_blk_device_attr(struct snap_virtio_device_attr *vattr)
{
	return container_of(vattr, struct snap_virtio_blk_device_attr, vattr);
}

#endif
