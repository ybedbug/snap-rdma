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

#ifndef SNAP_VIRTIO_BLK_CTRL_H
#define SNAP_VIRTIO_BLK_CTRL_H

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include "snap_virtio_common_ctrl.h"
#include "snap_virtio_blk.h"
#include "snap_blk_ops.h"

#define VIRTIO_BLK_MAX_CTRL_NUM		128
#define VIRTIO_BLK_CTRL_NUM_VIRTQ_MAX	32
#define VIRTIO_BLK_MAX_VIRTQ_SIZE	256

#define VIRTIO_BLK_CTRL_PAGE_SHIFT	16
#define VIRTIO_BLK_CTRL_PAGE_SIZE	(1 << VIRTIO_BLK_CTRL_PAGE_SHIFT)
#define VIRTIO_BLK_CTRL_MDTS_MAX	6
#define VIRTIO_BLK_MAX_REQ_DATA		(VIRTIO_BLK_CTRL_PAGE_SIZE * (1 << VIRTIO_BLK_CTRL_MDTS_MAX))

typedef struct snap_virtio_blk_ctrl_zcopy_ctx {
	void *fake_addr_table;
	size_t fake_addr_table_size;
	uintptr_t *request_table;
	struct snap_context *sctx;

	LIST_ENTRY(snap_virtio_blk_ctrl_zcopy_ctx) entry;
} snap_virtio_blk_ctrl_zcopy_ctx_t;

struct snap_virtio_blk_ctrl_queue {
	struct snap_virtio_ctrl_queue common;
	const struct snap_virtio_common_queue_attr	*attr;
	void *q_impl;
	bool in_error;
};

struct snap_virtio_blk_ctrl_attr {
	struct snap_virtio_ctrl_attr common;
	struct snap_virtio_blk_registers regs;
};

struct snap_virtio_blk_ctrl {
	struct snap_virtio_ctrl common;
	struct snap_bdev_ops *bdev_ops;
	void *bdev;
	uint32_t network_error;
	uint32_t internal_error;
	int idx;
	snap_virtio_blk_ctrl_zcopy_ctx_t *zcopy_ctx;
	struct snap_cross_mkey *cross_mkey;
};

struct snap_virtio_blk_ctrl *
snap_virtio_blk_ctrl_open(struct snap_context *sctx,
			  struct snap_virtio_blk_ctrl_attr *attr,
			  struct snap_bdev_ops *bdev_ops,
			  void *bdev);
void snap_virtio_blk_ctrl_close(struct snap_virtio_blk_ctrl *ctrl);
int snap_virtio_blk_ctrl_bar_setup(struct snap_virtio_blk_ctrl *ctrl,
				   struct snap_virtio_blk_registers *regs,
				   uint16_t regs_mask);
int snap_virtio_blk_ctrl_get_debugstat(struct snap_virtio_blk_ctrl *ctrl,
			struct snap_virtio_ctrl_debugstat *ctrl_debugstat);
int snap_virtio_blk_ctrl_addr_trans(struct ibv_pd *pd, void *ptr, size_t len,
				    uint32_t *cross_mkey, void **addr);
void snap_virtio_blk_ctrl_progress(struct snap_virtio_blk_ctrl *ctrl);
void snap_virtio_blk_ctrl_io_progress(struct snap_virtio_blk_ctrl *ctrl);
void snap_virtio_blk_ctrl_io_progress_thread(struct snap_virtio_blk_ctrl *ctrl,
					     uint32_t thread_id);

#endif
