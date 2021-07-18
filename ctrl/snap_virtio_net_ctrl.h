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
