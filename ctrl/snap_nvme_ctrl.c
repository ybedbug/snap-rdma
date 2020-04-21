#include "snap_nvme_ctrl.h"

static int snap_nvme_ctrl_bar_read(struct snap_nvme_ctrl *ctrl)
{
	struct snap_device *sdev = ctrl->sdev;
	struct snap_nvme_device_attr attr = {};
	int ret;

	ret = snap_nvme_query_device(sdev, &attr);
	if (snap_unlikely(ret)) {
		snap_error("sdev 0x%p read host BAR. ret=%d\n", sdev, ret);
		return ret;
	}

	ctrl->curr_enabled = attr.enabled;
	if (ctrl->curr_enabled != ctrl->prev_enabled) {
		/* FLR happened (device moved from enabled --> disabled) */
		if (ctrl->curr_enabled == 0 && ctrl->prev_enabled == 1) {
			if (sdev->mdev.vtunnel) {
				struct nvme_bar *bar;
				union nvme_cc_register *cc;

				/*
				 * In Bluefiled-1 we must clear the cc.en bit
				 * in case the HW device state changed to
				 * disabled. In this case, probably caused by
				 * FLR, the host will eventually set the cc.en
				 * to 1 and SW controller might miss this
				 * reset/FLR case. This is not good since all
				 * the HW resources that where created on the
				 * NVMe function where destroyed but the
				 * parallel SW resources are still alive. SW
				 * must destroy (and later re-create) those
				 * resources to avoid having stale resources
				 * and to configure the HW correctly. For that
				 * we clear the cc.en bit so the NVMe SW
				 * controller will be notified and perform the
				 * needed destruction.
				 */
				bar = (struct nvme_bar *)sdev->pci->bar.data;
				cc = (union nvme_cc_register *)&bar->cc;
				cc->bits.en = 0;
			} else {
				/*
				 * In Bluefiled-2 we must reset the entire
				 * emulation device object since there is no
				 * trigger for setting the enabled bit after
				 * FLR.
				 */
				ctrl->reset_device = true;
			}
			ctrl->prev_enabled = ctrl->curr_enabled;
		}
	}

	return 0;
}

static int snap_nvme_ctrl_bar_write(struct snap_nvme_ctrl *ctrl)
{
	struct snap_device *sdev = ctrl->sdev;
	struct snap_nvme_device_attr attr = {};
	int ret;

	/* copy ctrl bar to host bar (sdev) */
	memcpy(attr.bar.regs, &ctrl->bar, sizeof(ctrl->bar));
	ret = snap_nvme_modify_device(sdev, SNAP_NVME_DEV_MOD_BAR, &attr);
	if (snap_unlikely(ret)) {
		snap_error("sdev 0x%p modify host BAR. ret=%d\n", sdev, ret);
		return ret;
	}

	return 0;
}

static int snap_nvme_ctrl_set_initial_bar(struct snap_nvme_ctrl *ctrl)
{
	struct snap_device *sdev = ctrl->sdev;
	int ret;

	ret = snap_nvme_ctrl_bar_read(ctrl);
	if (ret) {
		snap_error("sdev 0x%p failed to read BAR. ret=%d\n", sdev,
			   ret);
		return ret;
	}

	ret = nvme_initial_register_check(sdev->pci->bar.data);
	if (ret) {
		snap_error("sdev 0x%p failed to verify BAR. ret=%d\n", sdev,
			   ret);
		return ret;
	}

	/* copy initial BAR values to ctrl */
	memcpy(&ctrl->bar, sdev->pci->bar.data, sizeof(ctrl->bar));
	nvme_bar_dump(&ctrl->bar);

	return 0;
}

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
	int ret;

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

	ret = snap_nvme_ctrl_set_initial_bar(ctrl);
	if (ret) {
		snap_error("failed to set ctrl 0x%p bar\n", ctrl);
		goto out_close_dev;
	}
	ctrl->sctx = sctx;

	return ctrl;

out_close_dev:
	snap_close_device(ctrl->sdev);
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

static void snap_nvme_ctrl_progress_mmio(struct snap_nvme_ctrl *ctrl)
{
	int ret;

	if (snap_unlikely(ctrl->reset_device)) {
		struct snap_device_attr sdev_attr = {};

		sdev_attr.pf_id = ctrl->sdev->pci->id;
		sdev_attr.type = ctrl->sdev->pci->type;

		snap_close_device(ctrl->sdev);
		ctrl->sdev = snap_open_device(ctrl->sctx, &sdev_attr);
		if (!ctrl->sdev)
			return;
		else
			ctrl->reset_device = false;
	}

	ret = snap_nvme_ctrl_bar_read(ctrl);
	if (snap_unlikely(ret))
		return;

	//TODO: add state machine for bar changes

	memcpy(&ctrl->bar, ctrl->sdev->pci->bar.data, sizeof(ctrl->bar));
}

/**
 * snap_nvme_ctrl_progress() - Handles bar changes in NVMe controller
 * @ctrl:       controller instance to handle
 *
 * Looks for bar changes in according to host driver state machine and respond
 * according to NVMe spec to any identified change.
 */
void snap_nvme_ctrl_progress(struct snap_nvme_ctrl *ctrl)
{
	clock_t diff;

	diff = clock() - ctrl->last_bar_cb;
	if (diff >= SNAP_NVME_BAR_CB_INTERVAL) {
		snap_nvme_ctrl_progress_mmio(ctrl);
		ctrl->last_bar_cb = clock();
	}
}
