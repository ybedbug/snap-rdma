#include "snap_virtio_blk_ctrl.h"


/**
 * snap_virtio_blk_ctrl_open() - Create a new virtio-blk controller
 * @attr:       virtio-blk controller attributes
 *
 * Allocates a new virtio-blk controller based on the requested attributes.
 *
 * Return: Returns a new snap_virtio_blk_ctrl in case of success, NULL otherwise and
 *         errno will be set to indicate the failure reason.
 */
struct snap_virtio_blk_ctrl*
snap_virtio_blk_ctrl_open(struct snap_virtio_blk_ctrl_attr *attr)
{
	struct snap_virtio_blk_ctrl *ctrl;
	int ret;

	ctrl = calloc(1, sizeof(*ctrl));
	if (!ctrl) {
		errno = ENOMEM;
		goto err;
	}

	attr->common.type = SNAP_VIRTIO_BLK_CTRL;
	ret = snap_virtio_ctrl_open(&ctrl->common, &attr->common);
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
 * snap_virtio_blk_ctrl_close() - Destroy a virtio-blk controller
 * @sdev:       virtio-blk controller to close
 *
 * Destroy and free virtio-blk controller.
 */
void snap_virtio_blk_ctrl_close(struct snap_virtio_blk_ctrl *ctrl)
{
	snap_virtio_ctrl_close(&ctrl->common);
	free(ctrl);
}

/**
 * snap_virtio_blk_ctrl_progress() - Handles control path changes in
 *                                   virtio-blk controller
 * @ctrl:       controller instance to handle
 *
 * Looks for control path status in virtio-blk controller and respond
 * to any identified changes (e.g. new enabled queues, changes in
 * device status, etc.)
 */
void snap_virtio_blk_ctrl_progress(struct snap_virtio_blk_ctrl *ctrl)
{
}
