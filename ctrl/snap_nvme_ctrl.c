#include "snap_nvme_ctrl.h"

/**
 * snap_nvme_ctrl_open() - Create a new nvme controller
 * @sctx:       snap context to manage the new controller
 *
 * Allocates a new nvme controller based on the requested attributes.
 *
 * Return: Returns a new snap_nvme_ctrl in case of success, NULL otherwise and
 *         errno will be set to indicate the failure reason.
 */
struct snap_nvme_ctrl*
snap_nvme_ctrl_open(struct snap_context *sctx)
{
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
}
