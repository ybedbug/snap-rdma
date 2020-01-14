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

struct snap_virtio_net_ctrl_attr {
	struct snap_virtio_ctrl_attr common;
};

struct snap_virtio_net_ctrl {
	struct snap_virtio_ctrl common;
};

struct snap_virtio_net_ctrl*
snap_virtio_net_ctrl_open(struct snap_context *sctx,
			  struct snap_virtio_net_ctrl_attr *attr);
void snap_virtio_net_ctrl_close(struct snap_virtio_net_ctrl *ctrl);
void snap_virtio_net_ctrl_progress(struct snap_virtio_net_ctrl *ctrl);
#endif
