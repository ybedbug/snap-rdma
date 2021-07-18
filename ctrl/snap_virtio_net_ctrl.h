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

#ifndef SNAP_VIRTIO_NET_CTRL_H
#define SNAP_VIRTIO_NET_CTRL_H

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include "snap_virtio_common_ctrl.h"
#include "snap_virtio_net.h"

struct snap_virtio_net_ctrl_queue {
	struct snap_virtio_ctrl_queue common;
	const struct snap_virtio_net_queue_attr *attr;
};

struct snap_virtio_net_ctrl_lm_cbs {
	size_t (*get_internal_state_size)(void *cb_ctx);
	int (*get_internal_state)(void *cb_ctx, void *buf, size_t len);
	size_t (*dump_internal_state)(void *cb_ctx, void *buff, size_t len);
	int (*set_internal_state)(void *cb_ctx, void *buf, size_t len);
};

struct snap_virtio_net_ctrl_attr {
	struct snap_virtio_ctrl_attr common;
	struct snap_virtio_net_ctrl_lm_cbs  *lm_cbs;
};

struct snap_virtio_net_ctrl {
	struct snap_virtio_ctrl common;
	struct snap_virtio_net_ctrl_lm_cbs lm_cbs;
};

static inline struct snap_virtio_net_ctrl*
to_net_ctrl(struct snap_virtio_ctrl *vctrl)
{
	return container_of(vctrl, struct snap_virtio_net_ctrl, common);
}

struct snap_virtio_net_ctrl*
snap_virtio_net_ctrl_open(struct snap_context *sctx,
			  struct snap_virtio_net_ctrl_attr *attr);
void snap_virtio_net_ctrl_close(struct snap_virtio_net_ctrl *ctrl);
void snap_virtio_net_ctrl_progress(struct snap_virtio_net_ctrl *ctrl);
void snap_virtio_net_ctrl_io_progress(struct snap_virtio_net_ctrl *ctrl);

#endif
