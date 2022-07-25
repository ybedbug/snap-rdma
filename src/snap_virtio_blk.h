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

#ifndef SNAP_VIRTIO_BLK_H
#define SNAP_VIRTIO_BLK_H

#include "snap_virtio_common.h"

struct snap_virtio_blk_device;

enum snap_virtio_blk_queue_modify {
	SNAP_VIRTIO_BLK_QUEUE_MOD_STATE	= 1 << 0,
};

struct snap_virtio_blk_queue {
	struct snap_virtio_queue	virtq;
	struct snap_virtio_blk_device	*vbdev;
	uint32_t uncomp_bdev_cmds; // For bdev detach
};

struct snap_virtio_blk_device_attr {
	struct snap_virtio_device_attr		vattr;
	struct snap_virtio_common_queue_attr	*q_attrs;
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

int snap_virtio_blk_init_device(struct snap_device *sdev);
int snap_virtio_blk_teardown_device(struct snap_device *sdev);
int snap_virtio_blk_query_device(struct snap_device *sdev,
	struct snap_virtio_blk_device_attr *attr);
int snap_virtio_blk_modify_device(struct snap_device *sdev, uint64_t mask,
		struct snap_virtio_blk_device_attr *attr);
struct snap_virtio_blk_queue*
snap_virtio_blk_create_queue(struct snap_device *sdev,
	struct snap_virtio_common_queue_attr *attr);
int snap_virtio_blk_destroy_queue(struct snap_virtio_blk_queue *vbq);
int snap_virtio_blk_query_queue(struct snap_virtio_blk_queue *vbq,
		struct snap_virtio_common_queue_attr *attr);
int snap_virtio_blk_modify_queue(struct snap_virtio_blk_queue *vbq,
		uint64_t mask, struct snap_virtio_common_queue_attr *attr);
struct virtq_q_ops *get_hw_queue_ops(void);
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

void snap_virtio_blk_pci_functions_cleanup(struct snap_context *sctx);
#endif
