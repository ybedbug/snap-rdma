#ifndef SNAP_VIRTIO_COMMON_CTRL_H
#define SNAP_VIRTIO_COMMON_CTRL_H

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include "snap.h"

enum snap_virtio_ctrl_type {
	SNAP_VIRTIO_BLK_CTRL,
	SNAP_VIRTIO_NET_CTRL,
};

struct snap_virtio_ctrl_attr {
	enum snap_virtio_ctrl_type type;
	int pf_id;
};

struct snap_virtio_ctrl {
	enum snap_virtio_ctrl_type type;
	struct snap_device *sdev;
};

int snap_virtio_ctrl_open(struct snap_virtio_ctrl *ctrl,
			  struct snap_context *sctx,
			  const struct snap_virtio_ctrl_attr *attr);
void snap_virtio_ctrl_close(struct snap_virtio_ctrl *ctrl);
#endif
