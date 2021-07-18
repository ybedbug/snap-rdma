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

#ifndef SNAP_VIRTIO_FS_CTRL_H
#define SNAP_VIRTIO_FS_CTRL_H

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include "snap_virtio_common_ctrl.h"
#include "snap_virtio_fs.h"
#include "snap_fs_ops.h"

struct snap_virtio_fs_ctrl_queue {
	struct snap_virtio_ctrl_queue common;
	const struct snap_virtio_fs_queue_attr	*attr;
	struct fs_virtq_ctx *q_impl;
	bool in_error;
};

struct snap_virtio_fs_ctrl_attr {
	struct snap_virtio_ctrl_attr common;
	struct snap_virtio_fs_registers regs;
};

struct snap_virtio_fs_ctrl {
	struct snap_virtio_ctrl common;
	struct snap_fs_dev_ops *fs_dev_ops;
	void *fs_dev;
	uint32_t network_error;
	uint32_t internal_error;
};

struct snap_virtio_fs_ctrl *
snap_virtio_fs_ctrl_open(struct snap_context *sctx,
			 struct snap_virtio_fs_ctrl_attr *attr,
			 struct snap_fs_dev_ops *fs_dev_ops,
			 void *fs_dev);
void snap_virtio_fs_ctrl_close(struct snap_virtio_fs_ctrl *ctrl);
int snap_virtio_fs_ctrl_bar_setup(struct snap_virtio_fs_ctrl *ctrl,
				  struct snap_virtio_fs_registers *regs,
				  uint16_t regs_mask);
int snap_virtio_fs_ctrl_get_debugstat(struct snap_virtio_fs_ctrl *ctrl,
			struct snap_virtio_ctrl_debugstat *ctrl_debugstat);
void snap_virtio_fs_ctrl_progress(struct snap_virtio_fs_ctrl *ctrl);
void snap_virtio_fs_ctrl_io_progress(struct snap_virtio_fs_ctrl *ctrl);
void snap_virtio_fs_ctrl_io_progress_thread(struct snap_virtio_fs_ctrl *ctrl,
					    uint32_t thread_id);

#endif
