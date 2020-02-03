#include "snap_virtio_blk_ctrl.h"


static struct snap_virtio_device_attr*
snap_virtio_blk_ctrl_bar_create(struct snap_virtio_ctrl *vctrl)
{
	struct snap_virtio_blk_device_attr *vbbar;

	vbbar = calloc(1, sizeof(*vbbar));
	if (!vbbar)
		goto err;

	/* Allocate queue attributes slots on bar */
	vbbar->queues = vctrl->num_queues;
	vbbar->q_attrs = calloc(vbbar->queues, sizeof(*vbbar->q_attrs));
	if (!vbbar->q_attrs)
		goto free_vbbar;

	return &vbbar->vattr;

free_vbbar:
	free(vbbar);
err:
	return NULL;
}

static void snap_virtio_blk_ctrl_bar_destroy(struct snap_virtio_device_attr *vbar)
{
	struct snap_virtio_blk_device_attr *vbbar = to_blk_device_attr(vbar);

	free(vbbar->q_attrs);
	free(vbbar);
}

static void snap_virtio_blk_ctrl_bar_copy(struct snap_virtio_device_attr *vorig,
					  struct snap_virtio_device_attr *vcopy)
{
	struct snap_virtio_blk_device_attr *vborig = to_blk_device_attr(vorig);
	struct snap_virtio_blk_device_attr *vbcopy = to_blk_device_attr(vcopy);

	memcpy(vbcopy->q_attrs, vborig->q_attrs,
	       vbcopy->queues * sizeof(*vbcopy->q_attrs));
}

static int snap_virtio_blk_ctrl_bar_update(struct snap_virtio_ctrl *vctrl,
					   struct snap_virtio_device_attr *vbar)
{
	struct snap_virtio_blk_device_attr *vbbar = to_blk_device_attr(vbar);

	return snap_virtio_blk_query_device(vctrl->sdev, vbbar);
}

static struct snap_virtio_ctrl_bar_ops snap_virtio_blk_ctrl_bar_ops = {
	.create = snap_virtio_blk_ctrl_bar_create,
	.destroy = snap_virtio_blk_ctrl_bar_destroy,
	.copy = snap_virtio_blk_ctrl_bar_copy,
	.update = snap_virtio_blk_ctrl_bar_update,
};

/**
 * snap_virtio_blk_ctrl_open() - Create a new virtio-blk controller
 * @sctx:       snap context to open new controller
 * @attr:       virtio-blk controller attributes
 *
 * Allocates a new virtio-blk controller based on the requested attributes.
 *
 * Return: Returns a new snap_virtio_blk_ctrl in case of success, NULL otherwise and
 *         errno will be set to indicate the failure reason.
 */
struct snap_virtio_blk_ctrl*
snap_virtio_blk_ctrl_open(struct snap_context *sctx,
			  struct snap_virtio_blk_ctrl_attr *attr)
{
	struct snap_virtio_blk_ctrl *ctrl;
	int ret;

	ctrl = calloc(1, sizeof(*ctrl));
	if (!ctrl) {
		errno = ENOMEM;
		goto err;
	}

	attr->common.type = SNAP_VIRTIO_BLK_CTRL;
	ret = snap_virtio_ctrl_open(&ctrl->common,
				    &snap_virtio_blk_ctrl_bar_ops,
				    sctx, &attr->common);
	if (ret) {
		errno = ENODEV;
		goto free_ctrl;
	}

	ret = snap_virtio_blk_init_device(ctrl->common.sdev);
	if (ret)
		goto close_ctrl;

	return ctrl;

close_ctrl:
	snap_virtio_ctrl_close(&ctrl->common);
free_ctrl:
	free(ctrl);
err:
	return NULL;
}

/**
 * snap_virtio_blk_ctrl_close() - Destroy a virtio-blk controller
 * @ctrl:       virtio-blk controller to close
 *
 * Destroy and free virtio-blk controller.
 */
void snap_virtio_blk_ctrl_close(struct snap_virtio_blk_ctrl *ctrl)
{
	snap_virtio_blk_teardown_device(ctrl->common.sdev);
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
	snap_virtio_ctrl_progress(&ctrl->common);
}
