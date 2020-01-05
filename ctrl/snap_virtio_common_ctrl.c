#include "snap_virtio_common_ctrl.h"

int snap_virtio_ctrl_open(struct snap_virtio_ctrl *ctrl,
			  const struct snap_virtio_ctrl_attr *attr)
{
	ctrl->type = attr->type;
	return 0;
}

void snap_virtio_ctrl_close(struct snap_virtio_ctrl *ctrl)
{
}
