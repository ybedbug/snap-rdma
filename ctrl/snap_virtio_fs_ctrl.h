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
