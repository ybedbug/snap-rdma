#ifndef SNAP_VIRTIO_COMMON_CTRL_H
#define SNAP_VIRTIO_COMMON_CTRL_H

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include "snap.h"
#include "snap_virtio_common.h"

struct snap_virtio_ctrl;

enum snap_virtio_ctrl_type {
	SNAP_VIRTIO_BLK_CTRL,
	SNAP_VIRTIO_NET_CTRL,
};

struct snap_virtio_ctrl_attr {
	enum snap_virtio_ctrl_type type;
	int pf_id;
};

struct snap_virtio_ctrl_bar_ops {
	struct snap_virtio_device_attr *(*create)(struct snap_virtio_ctrl *ctrl);
	void (*destroy)(struct snap_virtio_device_attr *ctrl);
	void (*copy)(struct snap_virtio_device_attr *orig,
		     struct snap_virtio_device_attr *copy);
	int (*update)(struct snap_virtio_ctrl *ctrl,
		      struct snap_virtio_device_attr *attr);
};

struct snap_virtio_ctrl {
	enum snap_virtio_ctrl_type type;
	struct snap_device *sdev;
	size_t num_queues;
	struct snap_virtio_ctrl_bar_ops *bar_ops;
	struct snap_virtio_device_attr *bar_curr;
	struct snap_virtio_device_attr *bar_prev;
};

void snap_virtio_ctrl_progress(struct snap_virtio_ctrl *ctrl);
int snap_virtio_ctrl_open(struct snap_virtio_ctrl *ctrl,
			  struct snap_virtio_ctrl_bar_ops *bar_ops,
			  struct snap_context *sctx,
			  const struct snap_virtio_ctrl_attr *attr);
void snap_virtio_ctrl_close(struct snap_virtio_ctrl *ctrl);
#endif
