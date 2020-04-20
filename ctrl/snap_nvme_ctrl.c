#include "snap_nvme_ctrl.h"

/**
 * snap_nvme_ctrl_open() - Create a new nvme controller
 * @sctx:       snap context to manage the new controller
 * @attr:       snap ctrl attributes for creation
 *
 * Allocates a new nvme controller based on the requested attributes.
 *
 * Return: Returns a new snap_nvme_ctrl in case of success, NULL otherwise and
 *         errno will be set to indicate the failure reason.
 */
struct snap_nvme_ctrl*
snap_nvme_ctrl_open(struct snap_context *sctx,
		    struct snap_nvme_ctrl_attr *attr)
{
	struct snap_device_attr sdev_attr = {};
	struct snap_nvme_ctrl *ctrl;

	ctrl = calloc(1, sizeof(*ctrl));
	if (!ctrl) {
		errno = ENOMEM;
		goto out_err;
	}

	if (attr->type != SNAP_NVME_CTRL_PF) {
		errno = ENOTSUP;
		goto out_free;
	}

	sdev_attr.type = SNAP_NVME_PF;
	sdev_attr.pf_id = attr->pf_id;
	ctrl->sdev = snap_open_device(sctx, &sdev_attr);
	if (!ctrl->sdev)
		goto out_free;

	ctrl->sctx = sctx;

	return ctrl;

out_free:
	free(ctrl);
out_err:
	return NULL;
}

/**
 * snap_nvme_ctrl_close() - Destroy a nvme controller
 * @ctrl:       nvme controller to close
 *
 * Destroy and free nvme controller.
 */
void snap_nvme_ctrl_close(struct snap_nvme_ctrl *ctrl)
{
	snap_close_device(ctrl->sdev);
	free(ctrl);
}
