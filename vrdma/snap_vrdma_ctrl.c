/*
 * Copyright © 2022 NVIDIA CORPORATION & AFFILIATES. ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of Nvidia Corporation and its affiliates
 * (the "Company") and all right, title, and interest in and to the software
 * product, including all associated intellectual property rights, are and
 * shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 */
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include "snap_vrdma_ctrl.h"
#include "snap_dma.h"

static struct snap_vrdma_device_attr*
snap_vrdma_ctrl_bar_create(void)
{
	struct snap_vrdma_device_attr *vbar;

	vbar = calloc(1, sizeof(*vbar));
	if (!vbar)
		return NULL;
	return vbar;
}

static void snap_vrdma_ctrl_bars_teardown(struct snap_vrdma_ctrl *ctrl)
{
	free(ctrl->bar_prev);
	free(ctrl->bar_curr);
}

static int snap_vrdma_ctrl_bar_update(struct snap_vrdma_ctrl *vctrl,
				struct snap_vrdma_device_attr *vbar)
{
	int ret;

	ret = snap_vrdma_query_device(vctrl->sdev, vbar);
	if (ret)
		return ret;
	return 0;
}

static int snap_vrdma_ctrl_bar_modify(struct snap_vrdma_ctrl *vctrl,
					   uint64_t mask,
					   struct snap_vrdma_device_attr *vbar)
{
	return snap_vrdma_modify_device(vctrl->sdev, mask, vbar);
}

static int snap_vrdma_ctrl_bars_init(struct snap_vrdma_ctrl *ctrl)
{
	int ret = 0;

	ctrl->bar_curr = snap_vrdma_ctrl_bar_create();
	if (!ctrl->bar_curr) {
		ret = -ENOMEM;
		goto err;
	}

	ctrl->bar_prev = snap_vrdma_ctrl_bar_create();
	if (!ctrl->bar_prev) {
		ret = -ENOMEM;
		goto free_curr;
	}

	return 0;

free_curr:
	free(ctrl->bar_curr);
err:
	return ret;
}

static int snap_vrdma_ctrl_open_internal(struct snap_vrdma_ctrl *ctrl,
			  struct snap_context *sctx,
			  const struct snap_vrdma_ctrl_attr *attr)
{
	int ret = 0;
	uint32_t npgs;
	struct snap_cross_mkey_attr cm_attr = {};

	if (!sctx) {
		ret = -ENODEV;
		goto err;
	}

	if (attr->npgs == 0) {
		snap_debug("vrdma requires at least one poll group\n");
		npgs = 1;
	} else {
		npgs = attr->npgs;
	}

	ctrl->sdev_attr.pf_id = attr->pf_id;
	ctrl->sdev_attr.type = attr->pci_type;
	if (attr->event)
		ctrl->sdev_attr.flags |= SNAP_DEVICE_FLAGS_EVENT_CHANNEL;
	ctrl->sdev_attr.context = attr->context;
	ctrl->sdev = snap_open_device(sctx, &ctrl->sdev_attr);
	if (!ctrl->sdev) {
		ret = -ENODEV;
		goto err;
	}

	ctrl->bar_cbs = *attr->bar_cbs;
	ctrl->cb_ctx = attr->cb_ctx;
	ctrl->adminq_pd = attr->pd;
	ctrl->adminq_mr = attr->mr;
	ctrl->adminq_buf = attr->adminq_buf;
	ctrl->adminq_size = attr->adminq_size;
	ctrl->adminq_dma_comp = attr->adminq_dma_comp;
	ret = snap_vrdma_ctrl_bars_init(ctrl);
	if (ret)
		goto close_device;

	ret = pthread_mutex_init(&ctrl->progress_lock, NULL);
	if (ret)
		goto teardown_bars;

	if (snap_pgs_alloc(&ctrl->pg_ctx, npgs)) {
		snap_error("allocate poll groups failed");
		ret = -EINVAL;
		goto mutex_destroy;
	}

	cm_attr.vtunnel = ctrl->sdev->mdev.vtunnel;
	cm_attr.dma_rkey = ctrl->sdev->dma_rkey;
	cm_attr.vhca_id = snap_get_vhca_id(ctrl->sdev);
	cm_attr.crossed_vhca_mkey = ctrl->sdev->crossed_vhca_mkey;

	ctrl->xmkey = snap_create_cross_mkey_by_attr(attr->pd, &cm_attr);
	if (!ctrl->xmkey) {
		ret = -EACCES;
		goto free_pgs;
	}

	ctrl->force_in_order = attr->force_in_order;
	return 0;

free_pgs:
	snap_pgs_free(&ctrl->pg_ctx);
mutex_destroy:
	pthread_mutex_destroy(&ctrl->progress_lock);
teardown_bars:
	snap_vrdma_ctrl_bars_teardown(ctrl);
close_device:
	snap_close_device(ctrl->sdev);
err:
	return ret;
}

/**
 * snap_vrdma_ctrl_open() - Create a new vrdma controller
 * @sctx:       snap context to open a new controller
 * @attr:       vrdma controller attributes
 *
 * Allocates a new vrdma controller based on the requested attributes.
 *
 * Return: Returns a new snap_vrdma_ctrl in case of success, NULL otherwise and
 *         errno will be set to indicate the failure reason.
 */
struct snap_vrdma_ctrl*
snap_vrdma_ctrl_open(struct snap_context *sctx,
			  struct snap_vrdma_ctrl_attr *attr)
{
	struct snap_vrdma_ctrl *ctrl;
	int ret;

	ctrl = calloc(1, sizeof(*ctrl));
	if (!ctrl) {
		errno = ENOMEM;
		goto err;
	}

	ret = snap_vrdma_ctrl_open_internal(ctrl, sctx, attr);
	if (ret) {
		errno = ENODEV;
		goto free_ctrl;
	}

	ret = snap_vrdma_init_device(ctrl->sdev, attr->pf_id);
	if (ret)
		goto close_ctrl;

	return ctrl;

close_ctrl:
	snap_vrdma_ctrl_close(ctrl);
free_ctrl:
	free(ctrl);
err:
	return NULL;
}

/**
 * snap_vrdma_ctrl_close() - Destroy a vrdma controller
 * @ctrl:       vrdma controller to close
 *
 * Destroy and free vrdma controller.
 */
void snap_vrdma_ctrl_close(struct snap_vrdma_ctrl *ctrl)
{
	int i;

	snap_vrdma_ctrl_stop(ctrl);
	if (!ctrl->pending_flr)
		snap_vrdma_teardown_device(ctrl->sdev);
		
	for (i = 0; i < ctrl->pg_ctx.npgs; i++)
		if (!TAILQ_EMPTY(&ctrl->pg_ctx.pgs[i].q_list))
			snap_warn("Closing ctrl %p with queue %d still active", ctrl, i);
	(void)snap_destroy_cross_mkey(ctrl->xmkey);
	snap_pgs_free(&ctrl->pg_ctx);
	pthread_mutex_destroy(&ctrl->progress_lock);
	snap_vrdma_ctrl_bars_teardown(ctrl);
	if (!ctrl->pending_flr)
		snap_close_device(ctrl->sdev);
	free(ctrl);
}

/*
 * Driver may choose to reset device for numerous reasons:
 * during initialization, on error, or during FLR.
 * Driver executes reset by writing `0` to `device_status` bar register.
 * According to virtio v0.95 spec., driver is not obligated to wait
 * for device to finish the RESET command, which may cause race conditions
 * to occur between driver and controller.
 * Issue is solved by using the extra internal `reset` bit:
 *  - FW set bit to `1` on driver reset.
 *  - Controller set it back to `0` once finished.
 */
#define SNAP_VRDMA_CTRL_RESET_DETECTED(vctrl) \
		(vctrl->bar_curr->reset)

#define SNAP_VRDMA_CTRL_FLR_DETECTED(vctrl) \
		(!vctrl->bar_curr->enabled)

/*
 * DRIVER_OK bit indicates that the driver is set up and ready to drive the
 * device. Only at this point, device is considered "live".
 * Prior to that, it is not promised that any driver resource is available
 * for the device to use.
 */
#define SNAP_VRDMA_CTRL_LIVE_DETECTED(vctrl) \
		!!(vctrl->bar_curr->status & SNAP_VRDMA_DEVICE_S_DRIVER_OK)

/**
 * snap_vrdma_ctrl_critical_bar_change_detected
 * @ctrl:	vrdma controller
 * change of snap_vrdma_device_attr that is critical
 * - device status change
 * - enabled bit, relating to flr flow
 * - reset bit, relating to reset flow
 * Return: True when critical change detected, otherwise False
 */
static bool
snap_vrdma_ctrl_critical_bar_change_detected(struct snap_vrdma_ctrl *ctrl)
{
	return ((ctrl->bar_curr->status != ctrl->bar_prev->status) ||
		(ctrl->bar_curr->enabled != ctrl->bar_prev->enabled) ||
		 SNAP_VRDMA_CTRL_RESET_DETECTED(ctrl) ||
		 SNAP_VRDMA_CTRL_FLR_DETECTED(ctrl));
}

/**
 * snap_vrdma_ctrl_stop() - stop virtio controller
 * @ctrl:   virtio controller
 *
 * The function stops vrdma controller. All active queues are destroyed.
 *
 * The function shall be called once the controller has been suspended. Otherwise
 * destroying a queue with outstanding commands can lead to unpredictable
 * results. The check is not enforced yet in order preserve backward
 * compatibility.
 *
 * TODO: return error if controller is not suspended
 *
 * Return:
 * 0 on success, -errno of error
 */
int snap_vrdma_ctrl_stop(struct snap_vrdma_ctrl *ctrl)
{
	//int i, ret = 0;
	int ret = 0;

	if (ctrl->state == SNAP_VRDMA_CTRL_STOPPED)
		return ret;
#if 0
	for (i = 0; i < ctrl->max_queues; i++) {
		if (ctrl->queues[i]) {
			snap_virtio_ctrl_queue_destroy(ctrl->queues[i]);
			ctrl->queues[i] = NULL;
		}
	}
#endif
	if (ctrl->bar_cbs.stop) {
		ret = ctrl->bar_cbs.stop(ctrl->cb_ctx);
		if (ret)
			return ret;
	}

	ctrl->state = SNAP_VRDMA_CTRL_STOPPED;
	snap_info("vrdma controller %p (bdf 0x%x) stopped. state: %d\n",
		ctrl, ctrl->bar_curr->pci_bdf, ctrl->state);
	return ret;
}

/**
 * snap_vrdma_ctrl_is_stopped() - check if vrdma controller is stopped
 * @ctrl:   vrdma controller
 *
 * Return:
 * true if vrdma controller is stopped
 */
bool snap_vrdma_ctrl_is_stopped(struct snap_vrdma_ctrl *ctrl)
{
	return ctrl->state == SNAP_VRDMA_CTRL_STOPPED;
}

/**
 * snap_vrdma_ctrl_is_suspended() - check if vrdma controller is suspended
 * @ctrl:   vrdma controller
 *
 * Return:
 * true if vrdma controller is suspended
 */
bool snap_vrdma_ctrl_is_suspended(struct snap_vrdma_ctrl *ctrl)
{
	return ctrl->state == SNAP_VRDMA_CTRL_SUSPENDED;
}
#if 0
/**
 * snap_vrdma_ctrl_is_configurable() - check if vrdma controller is
 * configurable, meaning PCI BAR can be modified.
 * @ctrl:   vrdma controller
 *
 * Return:
 * true if vrdma controller is configurable
 */
bool snap_vrdma_ctrl_is_configurable(struct snap_vrdma_ctrl *ctrl)
{
	struct snap_vrdma_device_attr vattr = {};
	const int invalid_mask = ~(SNAP_VRDMA_DEVICE_S_DEVICE_NEEDS_RESET |
				   SNAP_VRDMA_DEVICE_S_ACKNOWLEDGE |
				   SNAP_VRDMA_DEVICE_S_RESET |
				   SNAP_VRDMA_DEVICE_S_FAILED);
	return true;

	if (!ctrl->bar_ops->get_attr)
		return true;

	//TODO: On legacy driver, device is never configurable
	ctrl->bar_ops->get_attr(ctrl, &vattr);

	return (vattr.status & invalid_mask) == 0;
}
#endif


static int snap_vrdma_ctrl_validate(struct snap_vrdma_ctrl *ctrl)
{
	if (ctrl->bar_cbs.validate)
		return ctrl->bar_cbs.validate(ctrl->cb_ctx);

	return 0;
}

static int snap_vrdma_ctrl_device_error(struct snap_vrdma_ctrl *ctrl)
{
	ctrl->bar_curr->status |= SNAP_VRDMA_DEVICE_S_DEVICE_NEEDS_RESET;
	return snap_vrdma_ctrl_bar_modify(ctrl, SNAP_VRDMA_MOD_DEV_STATUS,
					   ctrl->bar_curr);
}

static int snap_vrdma_ctrl_reset(struct snap_vrdma_ctrl *ctrl)
{
	int ret = 0;

	ret = snap_vrdma_ctrl_stop(ctrl);
	if (ret)
		return ret;

	if (ctrl->bar_curr->pci_bdf) {
		/*
		 * When done with reset process, need to set reset bit
		 * back to `0` which signal FW to update `device_status`
		 * if needed. Host driver might be waiting for device
		 * RESET process completion by polling device_status
		 * until reading `0`.
		 */
		ctrl->bar_curr->reset = 0;
		ret = snap_vrdma_ctrl_bar_modify(ctrl, SNAP_VRDMA_MOD_RESET,
						  ctrl->bar_curr);
		/* The status should be 0 if Driver reset device. */
		ctrl->bar_curr->status = 0;
	}

	return ret;
}

/**
 * snap_vrdma_ctrl_suspend() - suspend vrdma controller
 * @ctrl:   vrdma controller
 *
 * The function suspends vrdma block controller. All active queues will be
 * suspended. The controller must be in the STARTED state. Once controller is
 * suspended it can be resumed with the snap_vrdma_ctrl_resume()
 *
 * The function is async. snap_vrdma_ctrl_is_suspended() should be used to
 * check for the suspend completion.
 *
 * The function should be called in the snap_vrdma_ctrl_progress() context.
 * Otherwise locking is required.
 *
 * Return:
 * 0 on success or -errno
 */
int snap_vrdma_ctrl_suspend(struct snap_vrdma_ctrl *ctrl)
{
	//int i;

	if (ctrl->state == SNAP_VRDMA_CTRL_SUSPENDING)
		return 0;

	if (ctrl->state != SNAP_VRDMA_CTRL_STARTED)
		return -EINVAL;

#if 0
	if (!ctrl->q_ops->suspend) {
		/* pretend that suspend was done. It is done for the compatibility
		 * reasons. TODO: return error
		 **/
		ctrl->state = SNAP_VRDMA_CTRL_SUSPENDED;
		return 0;
	}
#endif
	snap_info("Suspending controller %p\n", ctrl);

	snap_pgs_suspend(&ctrl->pg_ctx);
	/*for (i = 0; i < ctrl->max_queues; i++) {
		if (ctrl->queues[i])
			ctrl->q_ops->suspend(ctrl->queues[i]);
	}*/
	snap_pgs_resume(&ctrl->pg_ctx);

	ctrl->state = SNAP_VRDMA_CTRL_SUSPENDING;
	return 0;
}


/**
 * snap_vrdma_ctrl_resume() - resume vrdma controller
 * @ctrl:    vrdma controller
 *
 * The function resumes controller that was suspended by the
 * snap_vrdma_ctrl_suspend() or started in the suspended state.
 *
 * All enabled queues will be recreated based on the current controller state.
 *
 * The function is synchrounous and should be called in the
 * snap_vrdma_ctrl_progress() context. Otherwise locking is required.
 *
 * Return:
 * 0 on success or -errno
 */
static int snap_vrdma_ctrl_resume(struct snap_vrdma_ctrl *ctrl)
{
	int n_enabled = 0;
	//int i, ret;
	//struct snap_pg *pg;

	if (snap_vrdma_ctrl_is_stopped(ctrl))
		return 0;

	if (!snap_vrdma_ctrl_is_suspended(ctrl)) {
		/*Resume ctrl after it reached SUSPENDED state */
		ctrl->pending_resume = true;
		return 0;
	}

#if 0
	if (!ctrl->q_ops->suspend) {
		/* pretend that resume was done. It is done for the compatibility
		 * reasons. TODO: return error
		 **/
		ctrl->state = SNAP_VIRTIO_CTRL_STARTED;
		return 0;
	}

	if (!ctrl->q_ops->resume) {
		snap_error("virtio controller: resume is not implemented\n");
		return -ENOTSUP;
	}

	snap_pgs_suspend(&ctrl->pg_ctx);
	for (i = 0; i < ctrl->max_queues; i++) {
		if (!ctrl->queues[i])
			continue;

		ret = ctrl->q_ops->resume(ctrl->queues[i]);
		if (ret) {
			snap_warn("virtio controller %p: resume failed for q %d\n", ctrl, i);
			snap_pgs_resume(&ctrl->pg_ctx);
			return ret;
		}

		/* preserve pg across resume */
		pg = ctrl->queues[i]->pg;
		if (!pg)
			pg = snap_pg_get_next(&ctrl->pg_ctx);
		snap_virtio_ctrl_desched_q_nolock(ctrl->queues[i]);
		snap_virtio_ctrl_sched_q_nolock(ctrl, ctrl->queues[i], pg);
		snap_info("ctrl %p queue %d: pg_id %d RESUMED\n", ctrl, ctrl->queues[i]->index,
		ctrl->queues[i]->pg->id);
		n_enabled++;
	}
	snap_pgs_resume(&ctrl->pg_ctx);
	if (n_enabled > 0)
		ctrl->state = SNAP_VIRTIO_CTRL_STARTED;
#else
	ctrl->state = SNAP_VRDMA_CTRL_STARTED;
#endif
	snap_info("vrdma controller %p: resumed with %d queues\n", ctrl, n_enabled);
	return 0;
}

static void snap_vrdma_ctrl_progress_suspend(struct snap_vrdma_ctrl *ctrl)
{
	//int i;
	int ret;

#if 0
	snap_pgs_suspend(&ctrl->pg_ctx);
	for (i = 0; i < ctrl->max_queues; i++) {
		if (ctrl->queues[i] &&
				!ctrl->q_ops->is_suspended(ctrl->queues[i])) {
			snap_pgs_resume(&ctrl->pg_ctx);
			return;
		}
	}
	snap_pgs_resume(&ctrl->pg_ctx);
#endif

	ctrl->state = SNAP_VRDMA_CTRL_SUSPENDED;
	snap_info("Controller %p SUSPENDED\n", ctrl);

	if (ctrl->pending_reset) {
		ret = snap_vrdma_ctrl_reset(ctrl);
		if (ret)
			snap_error("vrdma controller %p pending reset failed\n", ctrl);
		ctrl->pending_reset = false;
	}

	/* For live migration - finish ongoing quiesce command */
	//if (ctrl->is_quiesce)
	//	snap_virtio_ctrl_quiesce_adm_done(ctrl);

	if (ctrl->pending_resume) {
		ret = snap_vrdma_ctrl_resume(ctrl);
		if (ret)
			snap_error("vrdma controller %p pending resume failed\n", ctrl);
		ctrl->pending_resume = false;
	}

}

static int snap_vrdma_ctrl_change_status(struct snap_vrdma_ctrl *ctrl)
{
	int ret = 0;

	/* NOTE: it is not allowed to reset or change bar while controller is
	 * freezed. It is the responsibility of the migration channel implementation
	 * to ensure it. Log error, the migration is probably going to fail.
	 */
	if (SNAP_VRDMA_CTRL_RESET_DETECTED(ctrl)) {
		snap_info("vrdma controller %p reset detected\n", ctrl);

		//if (ctrl->lm_state == SNAP_VIRTIO_CTRL_LM_FREEZED)
		//	snap_error("ctrl %p reset while in %s\n", ctrl, lm_state2str(ctrl->lm_state));
		/*
		 * suspending vrdma queues may take some time. In such case
		 * do reset once the controller is suspended.
		 */
		snap_vrdma_ctrl_suspend(ctrl);
		if (snap_vrdma_ctrl_is_stopped(ctrl) ||
		    snap_vrdma_ctrl_is_suspended(ctrl)) {
			ret = snap_vrdma_ctrl_reset(ctrl);
		} else
			ctrl->pending_reset = true;
	} else if (SNAP_VRDMA_CTRL_FLR_DETECTED(ctrl)) {
		struct snap_context *sctx = ctrl->sdev->sctx;
		void *dd_data = ctrl->sdev->dd_data;
		int i;

		if (!snap_vrdma_ctrl_is_stopped(ctrl)) {
			if (ctrl->state == SNAP_VRDMA_CTRL_STARTED) {
				snap_info("stopping vrdma controller %p before FLR\n", ctrl);
				snap_vrdma_ctrl_suspend(ctrl);
			}

			/*
			 * suspending vrdma queues may take some time. In such
			 * case stop the controller once it is suspended.
			 */
			if (snap_vrdma_ctrl_is_suspended(ctrl))
				ret = snap_vrdma_ctrl_stop(ctrl);

			if (!ret && !snap_vrdma_ctrl_is_stopped(ctrl))
				return 0;
		}

		snap_info("vrdma controller %p FLR detected\n", ctrl);

		/*if (ctrl->lm_state != SNAP_VIRTIO_CTRL_LM_RUNNING) {
			snap_info("clearing live migration state");
			snap_virtio_ctrl_set_lm_state(ctrl, SNAP_VIRTIO_CTRL_LM_RUNNING);
		}*/

		if (ctrl->bar_cbs.pre_flr) {
			if (ctrl->bar_cbs.pre_flr(ctrl->cb_ctx))
				return 0;
		}

		snap_close_device(ctrl->sdev);
		ctrl->pending_flr = true;

		/*
		 * Per PCIe r4.0, sec 6.6.2, a device must complete a FLR
		 * within 100ms. Creating a device emulation object succeed
		 * only after FLR completes, so polling on this command.
		 * Be more graceful and try to recover for 1 second.
		 */

		/* TODO: do this part asynchrounously */
		for (i = 0; i < 100; i++) {
			usleep(10000);
			ctrl->sdev = snap_open_device(sctx, &ctrl->sdev_attr);
			if (ctrl->sdev) {
				if (i > 9)
					snap_warn("FLR took more than 100ms");
				ctrl->sdev->dd_data = dd_data;
				ctrl->pending_flr = false;
				break;
			}
		}

		if (ctrl->bar_cbs.post_flr)
			ctrl->bar_cbs.post_flr(ctrl->cb_ctx);

		if (!ctrl->sdev) {
			snap_error("vrdma controller %p FLR failed\n", ctrl);
			return -ENODEV;
		}
	} else {
		//if (ctrl->lm_state == SNAP_VIRTIO_CTRL_LM_FREEZED)
		//	snap_error("bar change while in %s\n", lm_state2str(ctrl->lm_state));

		if (SNAP_VRDMA_CTRL_LIVE_DETECTED(ctrl)) {
			ret = snap_vrdma_ctrl_validate(ctrl);
			if (!ret)
				ret = snap_vrdma_ctrl_start(ctrl);
		}
	}
	if (ret)
		snap_vrdma_ctrl_device_error(ctrl);
	return ret;
}

/**
 * snap_vrdma_ctrl_start() - start vrdma controller
 * @ctrl:   vrdma controller
 *
 * The function starts vrdma controller. It enables all active queues and
 * assigns them to polling groups.
 *
 * The function can also start controller in the SUSPENDED mode. In such case
 * only placeholder queues are created but they are not activated.
 *
 * Return:
 * 0 on success, -errno of error
 */
int snap_vrdma_ctrl_start(struct snap_vrdma_ctrl *ctrl)
{
	struct snap_dma_q_create_attr dma_q_attr = {};
	uint32_t rkey, lkey;
	int ret = 0;

	if (ctrl->state == SNAP_VRDMA_CTRL_STARTED)
		goto out;

	/* controller can be created in the suspended state */
	if (ctrl->state == SNAP_VRDMA_CTRL_SUSPENDING) {
		snap_error("cannot start controller %p while it is being suspended, ctrl state: %d\n",
			   ctrl, ctrl->state);
		ret = -EINVAL;
		goto out;
	}
	/* Create dma queue for admin-queue*/
	dma_q_attr.tx_qsize = SNAP_VRDMA_ADMINQ_DMA_Q_SIZE;
	dma_q_attr.rx_qsize = SNAP_VRDMA_ADMINQ_DMA_Q_SIZE;
	dma_q_attr.tx_elem_size = ctrl->adminq_size;
	dma_q_attr.rx_elem_size = ctrl->adminq_size;
	dma_q_attr.mode = SNAP_DMA_Q_MODE_DV;
	dma_q_attr.use_devx = true;
	//dma_q_attr.rx_cb = dummy_rx_cb;
	ctrl->adminq_dma_q = snap_dma_q_create(ctrl->adminq_pd, &dma_q_attr);
	if (!ctrl->adminq_dma_q) {
		snap_error("Failed to create dma for admin queue controller %p ",
			  ctrl);
		ret = -EINVAL;
		goto out;
	}
	/* Init adminq_buf for admin queue */;
	rkey = ctrl->xmkey->mkey;
	lkey = ctrl->adminq_mr->lkey;
	ret = snap_dma_q_read(ctrl->adminq_dma_q, ctrl->adminq_buf, ctrl->adminq_size,
			      lkey, (uint64_t)ctrl->bar_curr->adminq_base_addr, rkey,
				  ctrl->adminq_dma_comp);
	if (ret) {
		snap_error("Failed to read admin queue for controller %p ",
			  ctrl);
		ret = -EINVAL;
		goto out;
	}

	if (ctrl->bar_cbs.start) {
		ret = ctrl->bar_cbs.start(ctrl->cb_ctx);
		if (ret) {
			snap_vrdma_ctrl_device_error(ctrl);
			goto out;
		}
	}

	if (ctrl->state != SNAP_VRDMA_CTRL_SUSPENDED) {
		snap_info("vrdma controller %p started\n", ctrl);
		ctrl->state = SNAP_VRDMA_CTRL_STARTED;
	} else
		snap_info("vrdma controller %p SUSPENDED\n", ctrl);

out:
	return ret;
}

/**
 * snap_vrdma_ctrl_progress_lock() - lock vrdma controller progress thread
 * @ctrl:   vrdma controller
 *
 * The function suspends execution of the snap_vrdma_ctrl_progress() by
 * taking a global progress lock.
 *
 * The function should be used when calling functions that change internal controller
 * state from a thread context different from the one of the snap_vrdma_ctrl_progress()
 *
 * Example of functions that require lock:
 *  - snap_vrdma_ctrl_suspend()
 *  - snap_vrdma_ctrl_resume()
 */
static void snap_vrdma_ctrl_progress_lock(struct snap_vrdma_ctrl *ctrl)
{
	pthread_mutex_lock(&ctrl->progress_lock);
}

/**
 * snap_vrdma_ctrl_progress_lock() - unlock vrdma controller progress thread
 * @ctrl:   vrdma controller
 *
 * The function resumes execution of the snap_vrdma_ctrl_progress() by
 * releasing a global progress lock.
 */
static void snap_vrdma_ctrl_progress_unlock(struct snap_vrdma_ctrl *ctrl)
{
	pthread_mutex_unlock(&ctrl->progress_lock);
}

/**
 * snap_vrdma_ctrl_progress() - progress vrdma controller
 * @ctrl:   vrdma controller
 *
 * The function polls vrdma controller configuration areas for changes and
 * processes them. Ultimately the function is responsible for starting the
 * controller with snap_vrdma_ctrl_start(), suspending it with
 * snap_vrdma_ctrl_suspend() and stopping it with snap_vrdma_ctrl_stop()
 *
 * The function does not progress io.
 *
 * snap_vrdma_ctrl_pg_io_progress() or snap_vrdma_ctrl_io_progress() should
 * be called to progress vrdma queueus.
 */
void snap_vrdma_ctrl_progress(struct snap_vrdma_ctrl *ctrl)
{
	int ret;

	snap_vrdma_ctrl_progress_lock(ctrl);

	/* TODO: do flr asynchrounously, in order not to block progress
	 * when we have many VFs
	 *
	 * If flr was not finished we can only:
	 * - finish flr, open snap device
	 * - destroy the controller
	 * Anything else is dangerous because snap device is not available
	 */
	if (ctrl->pending_flr)
		goto out;

	if (ctrl->state == SNAP_VRDMA_CTRL_SUSPENDING)
		snap_vrdma_ctrl_progress_suspend(ctrl);

	ret = snap_vrdma_ctrl_bar_update(ctrl, ctrl->bar_curr);
	if (ret)
		goto out;

	/* Handle device_status changes */
	if (snap_vrdma_ctrl_critical_bar_change_detected(ctrl)) {
		snap_vrdma_ctrl_change_status(ctrl);
		if (ctrl->pending_flr)
			goto out;
	}

#if 0
	if (ctrl->bar_curr->num_of_vfs != ctrl->bar_prev->num_of_vfs)
		snap_virtio_ctrl_change_num_vfs(ctrl);

	if (ctrl->state == SNAP_VRDMA_CTRL_STARTED) {
		ret = snap_virtio_ctrl_queue_reset_check(ctrl);
		if (ret)
			goto out;

		ret = snap_virtio_ctrl_queue_enable_check(ctrl);
		if (ret)
			goto out;
	}
#endif
out:
	snap_vrdma_ctrl_progress_unlock(ctrl);
}

/**
 * snap_vrdma_ctrl_io_progress() - single-threaded IO requests handling
 * @ctrl:       controller instance
 *
 * Looks for any IO requests from host received on any QPs, and handles
 * them based on the request's parameters.
 */
int snap_vrdma_ctrl_io_progress(struct snap_vrdma_ctrl *ctrl)
{
	/*TBD*/
	return 0;
}

/**
 * snap_vrdma_ctrl_io_progress_thread() - Handle IO requests for thread
 * @ctrl:       controller instance
 * @thread_id:	id queues belong to
 *
 * Looks for any IO requests from host received on QPs which belong to thread
 * thread_id, and handles them based on the request's parameters.
 */
int snap_vrdma_ctrl_io_progress_thread(struct snap_vrdma_ctrl *ctrl,
					     uint32_t thread_id)
{
	/*TBD*/
	return 0;
}