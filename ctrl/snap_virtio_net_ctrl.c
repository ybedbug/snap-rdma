#include "snap_virtio_net_ctrl.h"


static struct snap_virtio_device_attr*
snap_virtio_net_ctrl_bar_create(struct snap_virtio_ctrl *vctrl)
{
	struct snap_virtio_net_device_attr *vnbar;

	vnbar = calloc(1, sizeof(*vnbar));
	if (!vnbar)
		goto err;

	/* Allocate queue attributes slots on bar */
	vnbar->queues = vctrl->num_queues;
	vnbar->q_attrs = calloc(vnbar->queues, sizeof(*vnbar->q_attrs));
	if (!vnbar->q_attrs)
		goto free_vnbar;

	return &vnbar->vattr;

free_vnbar:
	free(vnbar);
err:
	return NULL;
}

static void snap_virtio_net_ctrl_bar_destroy(struct snap_virtio_device_attr *vbar)
{
	struct snap_virtio_net_device_attr *vnbar = to_net_device_attr(vbar);

	free(vnbar->q_attrs);
	free(vnbar);
}

static void snap_virtio_net_ctrl_bar_copy(struct snap_virtio_device_attr *vorig,
					  struct snap_virtio_device_attr *vcopy)
{
	struct snap_virtio_net_device_attr *vnorig = to_net_device_attr(vorig);
	struct snap_virtio_net_device_attr *vncopy = to_net_device_attr(vcopy);

	memcpy(vncopy->q_attrs, vnorig->q_attrs,
	       vncopy->queues * sizeof(*vncopy->q_attrs));
}

static int snap_virtio_net_ctrl_bar_update(struct snap_virtio_ctrl *vctrl,
					   struct snap_virtio_device_attr *vbar)
{
	struct snap_virtio_net_device_attr *vnbar = to_net_device_attr(vbar);

	return snap_virtio_net_query_device(vctrl->sdev, vnbar);
}

static int snap_virtio_net_ctrl_bar_modify(struct snap_virtio_ctrl *vctrl,
					   uint64_t mask,
					   struct snap_virtio_device_attr *vbar)
{
	struct snap_virtio_net_device_attr *vbbar = to_net_device_attr(vbar);

	return snap_virtio_net_modify_device(vctrl->sdev, mask, vbbar);
}

static int
snap_virtio_net_ctrl_bar_add_status(struct snap_virtio_net_ctrl *ctrl,
				    enum snap_virtio_common_device_status status)
{
	struct snap_virtio_net_device_attr *bar;

	bar = to_net_device_attr(ctrl->common.bar_curr);
	bar->vattr.status |= status;
	return snap_virtio_net_modify_device(ctrl->common.sdev,
					     SNAP_VIRTIO_MOD_DEV_STATUS, bar);
}

static struct snap_virtio_ctrl_bar_ops snap_virtio_net_ctrl_bar_ops = {
	.create = snap_virtio_net_ctrl_bar_create,
	.destroy = snap_virtio_net_ctrl_bar_destroy,
	.copy = snap_virtio_net_ctrl_bar_copy,
	.update = snap_virtio_net_ctrl_bar_update,
	.modify = snap_virtio_net_ctrl_bar_modify,
};

/**
 * snap_virtio_net_ctrl_open() - Create a new virtio-net controller
 * @sctx:       snap context to open a new controller
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
	ret = snap_virtio_ctrl_open(&ctrl->common,
				    &snap_virtio_net_ctrl_bar_ops,
				    sctx, &attr->common);
	if (ret) {
		errno = ENODEV;
		goto free_ctrl;
	}

	ret = snap_virtio_net_init_device(ctrl->common.sdev);
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
 * snap_virtio_net_ctrl_close() - Destroy a virtio-net controller
 * @ctrl:       virtio-net controller to close
 *
 * Destroy and free virtio-net controller.
 */
void snap_virtio_net_ctrl_close(struct snap_virtio_net_ctrl *ctrl)
{
	/* We must first notify host the device is no longer operational */
	snap_virtio_net_ctrl_bar_add_status(ctrl,
				SNAP_VIRTIO_DEVICE_S_DEVICE_NEEDS_RESET);
	snap_virtio_ctrl_stop(&ctrl->common);
	snap_virtio_net_teardown_device(ctrl->common.sdev);
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
	snap_virtio_ctrl_progress(&ctrl->common);
}
