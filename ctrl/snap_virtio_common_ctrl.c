#include "snap_virtio_common_ctrl.h"

int snap_virtio_ctrl_open(struct snap_virtio_ctrl *ctrl,
			  struct snap_context *sctx,
			  const struct snap_virtio_ctrl_attr *attr)
{
	int ret = 0;
	struct snap_device_attr sdev_attr = {0};

	if (!sctx) {
		ret = -ENODEV;
		goto err;
	}

	sdev_attr.pf_id = attr->pf_id;
	switch (attr->type) {
	case SNAP_VIRTIO_BLK_CTRL:
		sdev_attr.type = SNAP_VIRTIO_BLK_PF;
		break;
	case SNAP_VIRTIO_NET_CTRL:
		sdev_attr.type = SNAP_VIRTIO_NET_PF;
		break;
	default:
		ret = -EINVAL;
		goto err;
	};
	ctrl->sdev = snap_open_device(sctx, &sdev_attr);
	if (!ctrl->sdev) {
		ret = -ENODEV;
		goto err;
	}

	ctrl->type = attr->type;
	return 0;

err:
	return ret;
}

void snap_virtio_ctrl_close(struct snap_virtio_ctrl *ctrl)
{
	snap_close_device(ctrl->sdev);
}
