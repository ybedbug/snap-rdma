#ifndef SNAP_VIRTIO_BLK_CTRL_H
#define SNAP_VIRTIO_BLK_CTRL_H

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include "snap_virtio_common_ctrl.h"
#include "snap_virtio_blk.h"

struct snap_virtio_blk_ctrl_attr {
	struct snap_virtio_ctrl_attr common;
};

struct snap_virtio_blk_ctrl {
	struct snap_virtio_ctrl common;
};

struct snap_virtio_blk_ctrl*
snap_virtio_blk_ctrl_open(struct snap_context *sctx,
			  struct snap_virtio_blk_ctrl_attr *attr);
void snap_virtio_blk_ctrl_close(struct snap_virtio_blk_ctrl *ctrl);
void snap_virtio_blk_ctrl_progress(struct snap_virtio_blk_ctrl *ctrl);
#endif
