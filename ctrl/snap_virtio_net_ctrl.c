#include "snap_virtio_net_ctrl.h"


/**
 * snap_virtio_net_ctrl_open() - Create a new virtio-net controller
 * @attr:       virtio-net controller attributes
 *
 * Allocates a new virtio-net controller based on the requested attributes.
 *
 * Return: Returns a new snap_virtio_net_ctrl in case of success, NULL otherwise and
 *         errno will be set to indicate the failure reason.
 */
struct snap_virtio_net_ctrl*
snap_virtio_net_ctrl_open(struct snap_context *sctx,
			  struct snap_virtio_net_ctrl_attr *attr)
{
	struct snap_virtio_net_ctrl *ctrl;
	int ret;

	ctrl = calloc(1, sizeof(*ctrl));
	if (!ctrl) {
		errno = ENOMEM;
		goto err;
	}

	attr->common.type = SNAP_VIRTIO_NET_CTRL;
	ret = snap_virtio_ctrl_open(&ctrl->common, sctx, &attr->common);
	if (ret) {
		errno = ENODEV;
		goto free_ctrl;
	}

	return ctrl;

free_ctrl:
	free(ctrl);
err:
	return NULL;
}

/**
 * snap_virtio_net_ctrl_close() - Destroy a virtio-net controller
 * @sdev:       virtio-net controller to close
 *
 * Destroy and free virtio-net controller.
 */
void snap_virtio_net_ctrl_close(struct snap_virtio_net_ctrl *ctrl)
{
	snap_virtio_ctrl_close(&ctrl->common);
	free(ctrl);
}

/**
 * snap_virtio_net_ctrl_progress() - Handles control path changes in
 *                                   virtio-net controller
 * @ctrl:       controller instance to handle
 *
 * Looks for control path status in virtio-net controller and respond
 * to any identified changes (e.g. new enabled queues, changes in
 * device status, etc.)
 */
void snap_virtio_net_ctrl_progress(struct snap_virtio_net_ctrl *ctrl)
{
}
