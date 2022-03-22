/*
 * Copyright © 2021 NVIDIA CORPORATION & AFFILIATES. ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of Nvidia Corporation and its affiliates
 * (the "Company") and all right, title, and interest in and to the software
 * product, including all associated intellectual property rights, are and
 * shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 */

#include "snap_virtio_common_ctrl.h"
#include "snap_queue.h"
#include "snap_channel.h"
#include "snap_virtio_common.h"
#include "snap_vq_adm.h"

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
#define SNAP_VIRTIO_CTRL_RESET_DETECTED(vctrl) \
		(vctrl->bar_curr->reset)

#define SNAP_VIRTIO_CTRL_FLR_DETECTED(vctrl) \
		(!vctrl->bar_curr->enabled)

/*
 * DRIVER_OK bit indicates that the driver is set up and ready to drive the
 * device. Only at this point, device is considered "live".
 * Prior to that, it is not promised that any driver resource is available
 * for the device to use.
 */
#define SNAP_VIRTIO_CTRL_LIVE_DETECTED(vctrl) \
		!!(vctrl->bar_curr->status & SNAP_VIRTIO_DEVICE_S_DRIVER_OK)

/**
 * snap_virtio_ctrl_critical_bar_change_detected
 * @ctrl:	virtio controller
 * change of snap_virtio_device_attr that is critical
 * - device status change
 * - enabled bit, relating to flr flow
 * - reset bit, relating to reset flow
 * Return: True when critical change detected, otherwise False
 */
bool
snap_virtio_ctrl_critical_bar_change_detected(struct snap_virtio_ctrl *ctrl)
{
	return ((ctrl->bar_curr->status != ctrl->bar_prev->status) ||
		(ctrl->bar_curr->enabled != ctrl->bar_prev->enabled) ||
		 SNAP_VIRTIO_CTRL_RESET_DETECTED(ctrl) ||
		 SNAP_VIRTIO_CTRL_FLR_DETECTED(ctrl));
}

static const char *lm_state2str(enum snap_virtio_ctrl_lm_state state)
{
	switch (state) {
	case SNAP_VIRTIO_CTRL_LM_INIT:
		return "LM_INIT";
	case SNAP_VIRTIO_CTRL_LM_RUNNING:
		return "LM_RUNNING";
	case SNAP_VIRTIO_CTRL_LM_QUIESCED:
		return "LM_QUISCED";
	case SNAP_VIRTIO_CTRL_LM_FREEZED:
		return "LM_FREEZED";
	}

	return "LM_UNKNOWN";
}

/**
 * struct snap_virtio_ctrl_state_hdrs - helper struct.
 * Should be used for reservation place for sections before
 * saving the state of the controller.
 */
struct snap_virtio_ctrl_state_hdrs {
	struct snap_virtio_ctrl_section *ghdr;
	struct snap_virtio_ctrl_section *common_state_hdr;
	struct snap_virtio_ctrl_section *queues_state_hdr;
	struct snap_virtio_ctrl_section *dev_state_hdr;
};

static void snap_virtio_ctrl_init_section(struct snap_virtio_ctrl_section *hdr,
					  size_t len, const char *name)
{
	hdr->len = len;
	snprintf((char *)hdr->name, sizeof(hdr->name), "%s", name);
}

static struct snap_virtio_ctrl_section*
snap_virtio_ctrl_get_section(struct snap_virtio_ctrl_state_hdrs *state_hdrs,
			     uint32_t offset)
{
	char *hdr_start = (char *)state_hdrs->ghdr + offset;

	return (struct snap_virtio_ctrl_section *)hdr_start;
}

/**
 * snap_virtio_ctrl_save_init_hdrs() - init sections for controller's state
 * @ctrl:	virtio controller
 * @buf:	buffer to save the controller state
 * @len:	buffer length
 * @state_hdrs:	state sections

 * This function will initialize/setup all needed state sections
 * (state_hdrs) in the specified buffer. The 'place' for
 * the state sections' data will be reserved as well.
 *
 * Later, the state_hdrs should be used to save the controller's
 * state data under the relevant section.
 *
 * Return:
 * total state length on success, -errno on error
 */
static int snap_virtio_ctrl_save_init_hdrs(struct snap_virtio_ctrl *ctrl,
					   void *buf,
					   size_t len,
					   struct snap_virtio_ctrl_state_hdrs *state_hdrs
					  )
{
	int total_len;
	size_t dev_cfg_len, queue_cfg_len, common_cfg_len;
	size_t offset = 0;

	total_len = snap_virtio_ctrl_state_size(ctrl, &common_cfg_len,
						&queue_cfg_len,	&dev_cfg_len);
	if (len < total_len)
		return -EINVAL;

	state_hdrs->ghdr = buf;
	snap_virtio_ctrl_init_section(state_hdrs->ghdr,
				      total_len, "VIRTIO_CTRL_CFG");

	offset += sizeof(struct snap_virtio_ctrl_section);

	state_hdrs->common_state_hdr = snap_virtio_ctrl_get_section(state_hdrs,
								    offset);
	snap_virtio_ctrl_init_section(state_hdrs->common_state_hdr,
				      common_cfg_len, "COMMON_PCI_CFG");

	offset += sizeof(struct snap_virtio_ctrl_section) +
			sizeof(struct snap_virtio_ctrl_common_state);

	state_hdrs->queues_state_hdr = snap_virtio_ctrl_get_section(state_hdrs,
								    offset);
	snap_virtio_ctrl_init_section(state_hdrs->queues_state_hdr,
				      queue_cfg_len, "QUEUES_CFG");

	offset += sizeof(struct snap_virtio_ctrl_section) +
			sizeof(struct snap_virtio_ctrl_queue_state) * ctrl->max_queues;

	state_hdrs->dev_state_hdr = snap_virtio_ctrl_get_section(state_hdrs,
								 offset);
	snap_virtio_ctrl_init_section(state_hdrs->dev_state_hdr,
				      dev_cfg_len, "DEVICE_CFG");
	return total_len;
}

static void *section_hdr_to_data(const struct snap_virtio_ctrl_section *hdr)
{
	if (hdr->len)
		return (char *)hdr + sizeof(struct snap_virtio_ctrl_section);

	return NULL;
}

static inline struct snap_virtio_device_attr*
snap_virtio_ctrl_bar_create(struct snap_virtio_ctrl *ctrl)
{
	return ctrl->bar_ops->create(ctrl);
}

static inline void snap_virtio_ctrl_bar_destroy(struct snap_virtio_ctrl *ctrl,
					struct snap_virtio_device_attr *bar)
{
	ctrl->bar_ops->destroy(bar);
}

static inline void snap_virtio_ctrl_bar_copy(struct snap_virtio_ctrl *ctrl,
					struct snap_virtio_device_attr *orig,
					struct snap_virtio_device_attr *copy)
{
	ctrl->bar_ops->copy(orig, copy);
	copy->status = orig->status;
	copy->enabled = orig->enabled;
	copy->reset = orig->reset;
	copy->num_of_vfs = orig->num_of_vfs;
	copy->pci_hotplug_state = orig->pci_hotplug_state;
}

static inline int snap_virtio_ctrl_bar_update(struct snap_virtio_ctrl *ctrl,
					struct snap_virtio_device_attr *bar)
{
	snap_virtio_ctrl_bar_copy(ctrl, ctrl->bar_curr, ctrl->bar_prev);

	return ctrl->bar_ops->update(ctrl, bar);
}

static inline int snap_virtio_ctrl_bar_modify(struct snap_virtio_ctrl *ctrl,
					      uint64_t mask,
					      struct snap_virtio_device_attr *bar)
{
	return ctrl->bar_ops->modify(ctrl, mask, bar);
}

static inline struct snap_virtio_queue_attr*
to_virtio_queue_attr(struct snap_virtio_ctrl *ctrl,
		     struct snap_virtio_device_attr *vbar, int index)
{
	return ctrl->bar_ops->get_queue_attr(vbar, index);
}

static int snap_virtio_ctrl_bars_init(struct snap_virtio_ctrl *ctrl)
{
	int ret = 0;

	ctrl->bar_curr = snap_virtio_ctrl_bar_create(ctrl);
	if (!ctrl->bar_curr) {
		ret = -ENOMEM;
		goto err;
	}

	ctrl->bar_prev = snap_virtio_ctrl_bar_create(ctrl);
	if (!ctrl->bar_prev) {
		ret = -ENOMEM;
		goto free_curr;
	}

	return 0;

free_curr:
	snap_virtio_ctrl_bar_destroy(ctrl, ctrl->bar_curr);
err:
	return ret;
}

static void snap_virtio_ctrl_bars_teardown(struct snap_virtio_ctrl *ctrl)
{
	snap_virtio_ctrl_bar_destroy(ctrl, ctrl->bar_prev);
	snap_virtio_ctrl_bar_destroy(ctrl, ctrl->bar_curr);
}

static void snap_virtio_ctrl_sched_q_nolock(struct snap_virtio_ctrl *ctrl,
					    struct snap_virtio_ctrl_queue *vq,
					    struct snap_pg *pg)
{
	TAILQ_INSERT_TAIL(&pg->q_list, &vq->pg_q, entry);
	vq->pg = pg;
	if (ctrl->q_ops->start)
		ctrl->q_ops->start(vq);
}

static void snap_virtio_ctrl_sched_q(struct snap_virtio_ctrl *ctrl,
				     struct snap_virtio_ctrl_queue *vq)
{
	struct snap_pg *pg;

	pg = snap_pg_get_next(&ctrl->pg_ctx);

	pthread_spin_lock(&pg->lock);
	snap_virtio_ctrl_sched_q_nolock(ctrl, vq, pg);
	snap_debug("Virtio queue polling group id = %d\n", vq->pg->id);
	pthread_spin_unlock(&pg->lock);
}

static void snap_virtio_ctrl_desched_q_nolock(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_pg *pg = vq->pg;

	if (!pg)
		return;

	TAILQ_REMOVE(&pg->q_list, &vq->pg_q, entry);
	snap_pg_usage_decrease(vq->pg->id);
	vq->pg = NULL;
}

static void snap_virtio_ctrl_desched_q(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_pg *pg = vq->pg;

	if (!pg)
		return;

	pthread_spin_lock(&pg->lock);
	snap_virtio_ctrl_desched_q_nolock(vq);
	pthread_spin_unlock(&pg->lock);
}

static struct snap_virtio_ctrl_queue*
snap_virtio_ctrl_queue_create(struct snap_virtio_ctrl *ctrl, int index)
{
	struct snap_virtio_ctrl_queue *vq;

	vq = ctrl->q_ops->create(ctrl, index);
	if (!vq)
		return NULL;

	vq->ctrl = ctrl;
	vq->index = index;
	vq->log_writes_to_host = ctrl->log_writes_to_host;

	if (!snap_virtio_ctrl_is_suspended(ctrl))
		snap_virtio_ctrl_sched_q(ctrl, vq);

	return vq;
}

static void snap_virtio_ctrl_queue_destroy(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_virtio_ctrl *ctrl = vq->ctrl;

	snap_virtio_ctrl_desched_q(vq);
	ctrl->q_ops->destroy(vq);
}

static void snap_virtio_ctrl_queue_progress(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_virtio_ctrl *ctrl = vq->ctrl;

	ctrl->q_ops->progress(vq);
}

static int snap_virtio_ctrl_validate(struct snap_virtio_ctrl *ctrl)
{
	if (ctrl->bar_cbs.validate)
		return ctrl->bar_cbs.validate(ctrl->cb_ctx);

	return 0;
}

static int snap_virtio_ctrl_device_error(struct snap_virtio_ctrl *ctrl)
{
	ctrl->bar_curr->status |= SNAP_VIRTIO_DEVICE_S_DEVICE_NEEDS_RESET;
	return snap_virtio_ctrl_bar_modify(ctrl, SNAP_VIRTIO_MOD_DEV_STATUS,
					   ctrl->bar_curr);
}

static int snap_virtio_ctrl_reset(struct snap_virtio_ctrl *ctrl)
{
	int ret = 0;

	ret = snap_virtio_ctrl_stop(ctrl);
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
		ret = snap_virtio_ctrl_bar_modify(ctrl, SNAP_VIRTIO_MOD_RESET,
						  ctrl->bar_curr);
		/* The status should be 0 if Driver reset device. */
		ctrl->bar_curr->status = 0;
	}

	return ret;
}

static int snap_virtio_ctrl_change_status(struct snap_virtio_ctrl *ctrl)
{
	int ret = 0;

	/* NOTE: it is not allowed to reset or change bar while controller is
	 * freezed. It is the responsibility of the migration channel implementation
	 * to ensure it. Log error, the migration is probably going to fail.
	 */
	if (SNAP_VIRTIO_CTRL_RESET_DETECTED(ctrl)) {
		snap_info("virtio controller reset detected\n");

		if (ctrl->lm_state == SNAP_VIRTIO_CTRL_LM_FREEZED)
			snap_error("reset while in %s\n", lm_state2str(ctrl->lm_state));
		/*
		 * suspending virtio queues may take some time. In such case
		 * do reset once the controller is suspended.
		 */
		snap_virtio_ctrl_suspend(ctrl);
		if (snap_virtio_ctrl_is_stopped(ctrl) ||
		    snap_virtio_ctrl_is_suspended(ctrl)) {
			ret = snap_virtio_ctrl_reset(ctrl);
		} else
			ctrl->pending_reset = true;
	} else if (SNAP_VIRTIO_CTRL_FLR_DETECTED(ctrl)) {
		struct snap_context *sctx = ctrl->sdev->sctx;
		void *dd_data = ctrl->sdev->dd_data;
		int i;

		if (!snap_virtio_ctrl_is_stopped(ctrl)) {
			if (ctrl->state == SNAP_VIRTIO_CTRL_STARTED) {
				snap_info("stopping virtio controller before FLR\n");
				snap_virtio_ctrl_suspend(ctrl);
			}

			/*
			 * suspending virtio queues may take some time. In such
			 * case stop the controller once it is suspended.
			 */
			if (snap_virtio_ctrl_is_suspended(ctrl))
				ret = snap_virtio_ctrl_stop(ctrl);

			if (!ret && !snap_virtio_ctrl_is_stopped(ctrl))
				return 0;
		}

		snap_info("virtio controller FLR detected\n");

		if (ctrl->lm_state != SNAP_VIRTIO_CTRL_LM_RUNNING) {
			snap_info("clearing live migration state");
			snap_virtio_ctrl_set_lm_state(ctrl, SNAP_VIRTIO_CTRL_LM_RUNNING);
		}

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
			snap_error("virtio controller FLR failed\n");
			return -ENODEV;
		}
	} else {
		if (ctrl->lm_state == SNAP_VIRTIO_CTRL_LM_FREEZED)
			snap_error("bar change while in %s\n", lm_state2str(ctrl->lm_state));

		if (SNAP_VIRTIO_CTRL_LIVE_DETECTED(ctrl)) {
			ret = snap_virtio_ctrl_validate(ctrl);
			if (!ret)
				ret = snap_virtio_ctrl_start(ctrl);
		}
	}
	if (ret)
		snap_virtio_ctrl_device_error(ctrl);
	return ret;
}

/**
 * snap_virtio_ctrl_start() - start virtio controller
 * @ctrl:   virtio controller
 *
 * The function starts virtio controller. It enables all active queues and
 * assigns them to polling groups.
 *
 * The function can also start controller in the SUSPENDED mode. In such case
 * only placeholder queues are created but they are not activated.
 *
 * Return:
 * 0 on success, -errno of error
 */
int snap_virtio_ctrl_start(struct snap_virtio_ctrl *ctrl)
{
	int ret = 0;
	int n_enabled = 0;
	int i, j;
	const struct snap_virtio_queue_attr *vq;

	if (ctrl->state == SNAP_VIRTIO_CTRL_STARTED)
		goto out;

	/* controller can be created in the suspended state */
	if (ctrl->state == SNAP_VIRTIO_CTRL_SUSPENDING) {
		snap_error("cannot start controller while it is being suspended, ctrl state: %d\n",
			   ctrl->state);
		ret = -EINVAL;
		goto out;
	}

	for (i = 0; i < ctrl->max_queues; i++) {
		vq = to_virtio_queue_attr(ctrl, ctrl->bar_curr, i);

		if (vq->enable) {
			ctrl->queues[i] = snap_virtio_ctrl_queue_create(ctrl, i);
			if (!ctrl->queues[i]) {
				ret = -ENOMEM;
				goto vq_cleanup;
			}
			n_enabled++;
		}
	}

	if (ctrl->bar_cbs.start) {
		ret = ctrl->bar_cbs.start(ctrl->cb_ctx);
		if (ret) {
			snap_virtio_ctrl_device_error(ctrl);
			goto vq_cleanup;
		}
	}

	if (ctrl->state != SNAP_VIRTIO_CTRL_SUSPENDED) {
		snap_info("virtio controller started with %d queues\n", n_enabled);
		ctrl->state = SNAP_VIRTIO_CTRL_STARTED;
	} else
		snap_info("virtio controller SUSPENDED with %d queues\n", n_enabled);

	goto out;

vq_cleanup:
	for (j = 0; j < i; j++)
		if (ctrl->queues[j])
			snap_virtio_ctrl_queue_destroy(ctrl->queues[j]);

out:
	if (ctrl->state == SNAP_VIRTIO_CTRL_STARTED)
		snap_virtio_ctrl_set_lm_state(ctrl, SNAP_VIRTIO_CTRL_LM_RUNNING);

	return ret;
}

/**
 * snap_virtio_ctrl_stop() - stop virtio controller
 * @ctrl:   virtio controller
 *
 * The function stops virtio controller. All active queues are destroyed.
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
int snap_virtio_ctrl_stop(struct snap_virtio_ctrl *ctrl)
{
	int i, ret = 0;

	if (ctrl->state == SNAP_VIRTIO_CTRL_STOPPED)
		goto out;

	for (i = 0; i < ctrl->max_queues; i++) {
		if (ctrl->queues[i]) {
			snap_virtio_ctrl_queue_destroy(ctrl->queues[i]);
			ctrl->queues[i] = NULL;
		}
	}

	if (ctrl->bar_cbs.stop) {
		ret = ctrl->bar_cbs.stop(ctrl->cb_ctx);
		if (ret)
			goto out;
	}

	ctrl->state = SNAP_VIRTIO_CTRL_STOPPED;
	snap_info("virtio controller stopped. state: %d\n", ctrl->state);
out:
	return ret;
}

/**
 * snap_virtio_ctrl_is_stopped() - check if virtio controller is stopped
 * @ctrl:   virtio controller
 *
 * Return:
 * true if virtio controller is stopped
 */
bool snap_virtio_ctrl_is_stopped(struct snap_virtio_ctrl *ctrl)
{
	return ctrl->state == SNAP_VIRTIO_CTRL_STOPPED;
}

/**
 * snap_virtio_ctrl_is_suspended() - check if virtio controller is suspended
 * @ctrl:   virtio controller
 *
 * Return:
 * true if virtio controller is suspended
 */
bool snap_virtio_ctrl_is_suspended(struct snap_virtio_ctrl *ctrl)
{
	return ctrl->state == SNAP_VIRTIO_CTRL_SUSPENDED;
}

/**
 * snap_virtio_ctrl_is_configurable() - check if virtio controller is
 * configurable, meaning PCI BAR can be modified.
 * @ctrl:   virtio controller
 *
 * Return:
 * true if virtio controller is configurable
 */
bool snap_virtio_ctrl_is_configurable(struct snap_virtio_ctrl *ctrl)
{
	struct snap_virtio_device_attr vattr = {};
	const int invalid_mask = ~(SNAP_VIRTIO_DEVICE_S_DEVICE_NEEDS_RESET |
				   SNAP_VIRTIO_DEVICE_S_ACKNOWLEDGE |
				   SNAP_VIRTIO_DEVICE_S_RESET |
				   SNAP_VIRTIO_DEVICE_S_FAILED);

	if (!ctrl->bar_ops->get_attr)
		return true;

	//TODO: On legacy driver, device is never configurable
	ctrl->bar_ops->get_attr(ctrl, &vattr);
	return (vattr.status & invalid_mask) == 0;
}

/**
 * snap_virtio_ctrl_suspend() - suspend virtio controller
 * @ctrl:   virtio controller
 *
 * The function suspends virtio block controller. All active queues will be
 * suspended. The controller must be in the STARTED state. Once controller is
 * suspended it can be resumed with the snap_virtio_ctrl_resume()
 *
 * The function is async. snap_virtio_ctrl_is_suspended() should be used to
 * check for the suspend completion.
 *
 * The function should be called in the snap_virtio_ctrl_progress() context.
 * Otherwise locking is required.
 *
 * Return:
 * 0 on success or -errno
 */
int snap_virtio_ctrl_suspend(struct snap_virtio_ctrl *ctrl)
{
	int i;

	if (ctrl->state == SNAP_VIRTIO_CTRL_SUSPENDING)
		return 0;

	if (ctrl->state != SNAP_VIRTIO_CTRL_STARTED)
		return -EINVAL;

	if (!ctrl->q_ops->suspend) {
		/* pretend that suspend was done. It is done for the compatibility
		 * reasons. TODO: return error
		 **/
		ctrl->state = SNAP_VIRTIO_CTRL_SUSPENDED;
		return 0;
	}

	snap_info("Suspending controller\n");

	snap_pgs_suspend(&ctrl->pg_ctx);
	for (i = 0; i < ctrl->max_queues; i++) {
		if (ctrl->queues[i])
			ctrl->q_ops->suspend(ctrl->queues[i]);
	}
	snap_pgs_resume(&ctrl->pg_ctx);

	ctrl->state = SNAP_VIRTIO_CTRL_SUSPENDING;
	return 0;
}

/**
 * snap_virtio_ctrl_resume() - resume virtio controller
 * @ctrl:    virtio controller
 *
 * The function resumes controller that was suspended by the
 * snap_virtio_ctrl_suspend() or started in the suspended state.
 *
 * All enabled queues will be recreated based on the current controller state.
 *
 * The function is synchrounous and should be called in the
 * snap_virtio_ctrl_progress() context. Otherwise locking is required.
 *
 * Return:
 * 0 on success or -errno
 */
int snap_virtio_ctrl_resume(struct snap_virtio_ctrl *ctrl)
{
	int n_enabled = 0;
	int i, ret;
	struct snap_pg *pg;

	if (snap_virtio_ctrl_is_stopped(ctrl))
		return 0;

	if (!snap_virtio_ctrl_is_suspended(ctrl))
		return -EINVAL;

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

		/* preserve pg across resume */
		pg = ctrl->queues[i]->pg;
		if (!pg)
			pg = snap_pg_get_next(&ctrl->pg_ctx);
		snap_virtio_ctrl_desched_q_nolock(ctrl->queues[i]);
		ctrl->queues[i]->pg = pg;
		ret = ctrl->q_ops->resume(ctrl->queues[i]);
		if (ret)
			return ret;
		snap_virtio_ctrl_sched_q_nolock(ctrl, ctrl->queues[i], pg);
		n_enabled++;
	}
	snap_pgs_resume(&ctrl->pg_ctx);
	if (n_enabled > 0)
		ctrl->state = SNAP_VIRTIO_CTRL_STARTED;
	snap_info("virtio controller: resumed with %d queues\n", n_enabled);
	return 0;
}

static int snap_virtio_ctrl_change_num_vfs(const struct snap_virtio_ctrl *ctrl)
{
	int ret;

	/* Give application a chance to clear resources */
	if (ctrl->bar_cbs.num_vfs_changed) {
		ret = ctrl->bar_cbs.num_vfs_changed(ctrl->cb_ctx,
						    ctrl->bar_curr->num_of_vfs);
		if (ret)
			return ret;
	} else {
		ret = snap_rescan_vfs(ctrl->sdev->pci, ctrl->bar_curr->num_of_vfs);
		if (ret) {
			snap_error("Failed to rescan vfs\n");
			return ret;
		}
	}

	return 0;
}

static void snap_virtio_ctrl_quiesce_adm_done(struct snap_virtio_ctrl *ctrl)
{
	snap_virtio_ctrl_set_lm_state(ctrl, SNAP_VIRTIO_CTRL_LM_QUIESCED);
	snap_info("%p: queisce: new state %s\n", ctrl,
		   lm_state2str(ctrl->lm_state));
	ctrl->is_quiesce = false;

	/* Complete modify internal state to quiesce command */
	snap_vaq_cmd_complete(ctrl->quiesce_cmd, SNAP_VIRTIO_ADM_STATUS_OK);
	ctrl->quiesce_cmd = NULL;
}


static void snap_virtio_ctrl_progress_suspend(struct snap_virtio_ctrl *ctrl)
{
	int i;
	int ret;

	snap_pgs_suspend(&ctrl->pg_ctx);
	for (i = 0; i < ctrl->max_queues; i++) {
		if (ctrl->queues[i] &&
				!ctrl->q_ops->is_suspended(ctrl->queues[i])) {
			snap_pgs_resume(&ctrl->pg_ctx);
			return;
		}
	}
	snap_pgs_resume(&ctrl->pg_ctx);

	ctrl->state = SNAP_VIRTIO_CTRL_SUSPENDED;
	snap_info("Controller SUSPENDED\n");

	if (ctrl->pending_reset) {
		ret = snap_virtio_ctrl_reset(ctrl);
		if (ret)
			snap_error("virtio controlelr pending reset failed\n");
		ctrl->pending_reset = false;
	}

	/* For live migration - finish ongoing quiesce command */
	if (ctrl->is_quiesce)
		snap_virtio_ctrl_quiesce_adm_done(ctrl);
}

/**
 * snap_virtio_ctrl_progress_lock() - lock virtio controller progress thread
 * @ctrl:   virtio controller
 *
 * The function suspends execution of the snap_virtio_ctrl_progress() by
 * taking a global progress lock.
 *
 * The function should be used when calling functions that change internal controller
 * state from a thread context different from the one of the snap_virtio_ctrl_progress()
 *
 * Example of functions that require lock:
 *  - snap_virtio_ctrl_suspend()
 *  - snap_virtio_ctrl_resume()
 *  - snap_virtio_ctrl_state_save()
 *  - snap_virtio_ctrl_state_restore()
 *  - snap_virtio_ctrl_state_size()
 */
void snap_virtio_ctrl_progress_lock(struct snap_virtio_ctrl *ctrl)
{
	pthread_mutex_lock(&ctrl->progress_lock);
}

/**
 * snap_virtio_ctrl_progress_lock() - unlock virtio controller progress thread
 * @ctrl:   virtio controller
 *
 * The function resumes execution of the snap_virtio_ctrl_progress() by
 * releasing a global progress lock.
 */
void snap_virtio_ctrl_progress_unlock(struct snap_virtio_ctrl *ctrl)
{
	pthread_mutex_unlock(&ctrl->progress_lock);
}

/**
 * snap_virtio_ctrl_progress() - progress virtio controller
 * @ctrl:   virtio controller
 *
 * The function polls virtio controller configuration areas for changes and
 * processes them. Ultimately the function is responsible for starting the
 * controller with snap_virtio_ctrl_start(), suspending it with
 * snap_virtio_ctrl_suspend() and stopping it with snap_virtio_ctrl_stop()
 *
 * The function does not progress io.
 *
 * snap_virtio_ctrl_pg_io_progress() or snap_virtio_ctrl_io_progress() should
 * be called to progress virtio queueus.
 */
void snap_virtio_ctrl_progress(struct snap_virtio_ctrl *ctrl)
{
	int ret;

	snap_virtio_ctrl_progress_lock(ctrl);

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

	if (ctrl->state == SNAP_VIRTIO_CTRL_SUSPENDING)
		snap_virtio_ctrl_progress_suspend(ctrl);

	ret = snap_virtio_ctrl_bar_update(ctrl, ctrl->bar_curr);
	if (ret)
		goto out;

	/* Handle device_status changes */
	if (snap_virtio_ctrl_critical_bar_change_detected(ctrl)) {
		snap_virtio_ctrl_change_status(ctrl);
		if (ctrl->pending_flr)
			goto out;
	}

	if (ctrl->bar_curr->num_of_vfs != ctrl->bar_prev->num_of_vfs)
		snap_virtio_ctrl_change_num_vfs(ctrl);

out:
	snap_virtio_ctrl_progress_unlock(ctrl);
}

int snap_virtio_ctrl_hotunplug(struct snap_virtio_ctrl *ctrl)
{
	struct snap_virtio_device_attr *attr = ctrl->bar_curr;
	uint64_t mask = SNAP_VIRTIO_MOD_PCI_HOTPLUG_STATE;

	attr->pci_hotplug_state = MLX5_EMULATION_HOTPLUG_STATE_HOTUNPLUG_PREPARE;
	return snap_virtio_ctrl_bar_modify(ctrl, mask, attr);
}

static inline struct snap_virtio_ctrl_queue *
pg_q_entry_to_virtio_ctrl_queue(struct snap_pg_q_entry *pg_q)
{
	return container_of(pg_q, struct snap_virtio_ctrl_queue, pg_q);
}

static void snap_virtio_ctrl_pg_thread_io_progress(
		struct snap_virtio_ctrl *ctrl, int pg_id, int thread_id)
{
	struct snap_pg *pg = &ctrl->pg_ctx.pgs[pg_id];
	struct snap_virtio_ctrl_queue *vq;
	struct snap_pg_q_entry *pg_q;

	pthread_spin_lock(&pg->lock);
	TAILQ_FOREACH(pg_q, &pg->q_list, entry) {
		vq = pg_q_entry_to_virtio_ctrl_queue(pg_q);
		vq->thread_id = thread_id;
		snap_virtio_ctrl_queue_progress(vq);
	}
	pthread_spin_unlock(&pg->lock);
}

void snap_virtio_ctrl_pg_io_progress(struct snap_virtio_ctrl *ctrl, int pg_id)
{
	snap_virtio_ctrl_pg_thread_io_progress(ctrl, pg_id, pg_id);
}

void snap_virtio_ctrl_io_progress(struct snap_virtio_ctrl *ctrl)
{
	int i;

	for (i = 0; i < ctrl->pg_ctx.npgs; i++)
		snap_virtio_ctrl_pg_thread_io_progress(ctrl, i, -1);
}

int snap_virtio_ctrl_open(struct snap_virtio_ctrl *ctrl,
			  struct snap_virtio_ctrl_bar_ops *bar_ops,
			  struct snap_virtio_queue_ops *q_ops,
			  struct snap_context *sctx,
			  const struct snap_virtio_ctrl_attr *attr)
{
	int ret = 0;
	uint32_t npgs;
	struct snap_cross_mkey_attr cm_attr = {};

	if (!sctx) {
		ret = -ENODEV;
		goto err;
	}

	if (attr->npgs == 0) {
		snap_debug("virtio requires at least one poll group\n");
		npgs = 1;
	} else {
		npgs = attr->npgs;
	}

	ctrl->sdev_attr.pf_id = attr->pf_id;
	ctrl->sdev_attr.vf_id = attr->vf_id;
	switch (attr->type) {
	case SNAP_VIRTIO_BLK_CTRL:
		ctrl->max_queues = sctx->virtio_blk_caps.max_emulated_virtqs;
		ctrl->sdev_attr.type = attr->pci_type;
		break;
	case SNAP_VIRTIO_NET_CTRL:
		ctrl->max_queues = sctx->virtio_net_caps.max_emulated_virtqs;
		ctrl->sdev_attr.type = attr->pci_type;
		break;
	case SNAP_VIRTIO_FS_CTRL:
		ctrl->max_queues = sctx->virtio_fs_caps.max_emulated_virtqs;
		ctrl->sdev_attr.type = attr->pci_type;
		break;
	default:
		ret = -EINVAL;
		goto err;
	};
	if (attr->event)
		ctrl->sdev_attr.flags |= SNAP_DEVICE_FLAGS_EVENT_CHANNEL;
	if (attr->vf_dynamic_msix_supported)
		ctrl->sdev_attr.flags |= SNAP_DEVICE_FLAGS_VF_DYN_MSIX;
	ctrl->sdev_attr.context = attr->context;
	ctrl->sdev = snap_open_device(sctx, &ctrl->sdev_attr);
	if (!ctrl->sdev) {
		ret = -ENODEV;
		goto err;
	}

	ctrl->bar_ops = bar_ops;
	ctrl->bar_cbs = *attr->bar_cbs;
	ctrl->cb_ctx = attr->cb_ctx;
	ctrl->lb_pd = attr->pd;
	ret = snap_virtio_ctrl_bars_init(ctrl);
	if (ret)
		goto close_device;

	ret = pthread_mutex_init(&ctrl->progress_lock, NULL);
	if (ret)
		goto teardown_bars;

	ctrl->q_ops = q_ops;
	ctrl->queues = calloc(ctrl->max_queues, sizeof(*ctrl->queues));
	if (!ctrl->queues) {
		ret = -ENOMEM;
		goto mutex_destroy;
	}

	if (snap_pgs_alloc(&ctrl->pg_ctx, npgs)) {
		snap_error("allocate poll groups failed");
		goto free_queues;
	}

	cm_attr.vtunnel = ctrl->sdev->mdev.vtunnel;
	cm_attr.dma_rkey = ctrl->sdev->dma_rkey;
	cm_attr.vhca_id = snap_get_vhca_id(ctrl->sdev);
	cm_attr.crossed_vhca_mkey = ctrl->sdev->crossed_vhca_mkey;

	ctrl->xmkey = snap_create_cross_mkey_by_attr(attr->pd, &cm_attr);
	if (!ctrl->xmkey)
		goto free_pgs;

	ctrl->type = attr->type;
	ctrl->force_in_order = attr->force_in_order;
	return 0;

free_pgs:
	snap_pgs_free(&ctrl->pg_ctx);
free_queues:
	free(ctrl->queues);
mutex_destroy:
	pthread_mutex_destroy(&ctrl->progress_lock);
teardown_bars:
	snap_virtio_ctrl_bars_teardown(ctrl);
close_device:
	snap_close_device(ctrl->sdev);
err:
	return ret;
}

void snap_virtio_ctrl_close(struct snap_virtio_ctrl *ctrl)
{
	int i;

	for (i = 0; i < ctrl->pg_ctx.npgs; i++)
		if (!TAILQ_EMPTY(&ctrl->pg_ctx.pgs[i].q_list))
			snap_warn("Closing ctrl with queue %d still active", i);
	(void)snap_destroy_cross_mkey(ctrl->xmkey);
	snap_pgs_free(&ctrl->pg_ctx);
	free(ctrl->queues);
	pthread_mutex_destroy(&ctrl->progress_lock);
	snap_virtio_ctrl_bars_teardown(ctrl);
	if (!ctrl->pending_flr)
		snap_close_device(ctrl->sdev);
}

/**
 * snap_virtio_ctrl_log_write() - Enable/disable dirty memory tracking
 * @ctrl:    virtio controller
 * @enable:  toggle dirty memory tracking
 *
 * The function toggles dirty memory tracking by the controller. The tracking
 * itself should be implemented by the virtio queue implementation of the
 * specific controller. The queue should use snap_channel_mark_dirty_page()
 * to report dirty memory to the migration channel.
 *
 * The function should be called by the start/stop_dirty_pages_track migration
 * channel callbacks.
 */
void snap_virtio_ctrl_log_writes(struct snap_virtio_ctrl *ctrl, bool enable)
{
	int i;

	snap_pgs_suspend(&ctrl->pg_ctx);
	for (i = 0; i < ctrl->max_queues; i++) {
		if (ctrl->queues[i])
			ctrl->queues[i]->log_writes_to_host = enable;
	}
	snap_pgs_resume(&ctrl->pg_ctx);
	ctrl->log_writes_to_host = enable;
}

/**
 * snap_virtio_ctrl_state_size() - Get virtio controller state size
 * @ctrl:	      virtio controller
 * @common_cfg_len:    on return holds size of the pci_common config section
 * @queue_cfg_len:     on return holds size of the queue configuration section
 * @dev_cfg_len:       on return holds size of the device configuration section
 *
 * Note: size of the section includes size of its header
 *
 * Return:
 * Total state size including section headers
 * < 0 on error
 */
int snap_virtio_ctrl_state_size(struct snap_virtio_ctrl *ctrl, size_t *common_cfg_len,
				size_t *queue_cfg_len, size_t *dev_cfg_len)
{
	size_t tmp_common_cfg_len, tmp_queue_cfg_len, tmp_dev_cfg_len;
	size_t *common_cfg_len_p = common_cfg_len ? common_cfg_len : &tmp_common_cfg_len;
	size_t *queue_cfg_len_p  = queue_cfg_len  ? queue_cfg_len  : &tmp_queue_cfg_len;
	size_t *dev_cfg_len_p    = dev_cfg_len    ? dev_cfg_len    : &tmp_dev_cfg_len;

	if (ctrl->bar_ops->get_state_size)
		*dev_cfg_len_p = ctrl->bar_ops->get_state_size(ctrl);
	else
		*dev_cfg_len_p = 0;

	*dev_cfg_len_p += sizeof(struct snap_virtio_ctrl_section);

	*queue_cfg_len_p = ctrl->max_queues * sizeof(struct snap_virtio_ctrl_queue_state) +
			 sizeof(struct snap_virtio_ctrl_section);

	*common_cfg_len_p = sizeof(struct snap_virtio_ctrl_section) +
			  sizeof(struct snap_virtio_ctrl_common_state);

	snap_debug("common_cfg %lu dev_cfg %lu queue_cfg %lu max_queue %d\n",
		   *common_cfg_len_p, *dev_cfg_len_p, *queue_cfg_len_p, (int)ctrl->max_queues);

	return sizeof(struct snap_virtio_ctrl_section) + *dev_cfg_len_p +
	       *queue_cfg_len_p + *common_cfg_len_p;
}

__attribute__((unused)) static void dump_state(struct snap_virtio_ctrl *ctrl,
					       const void *buf)
{
	const struct snap_virtio_ctrl_section *hdr;
	struct snap_virtio_ctrl_common_state *common_state;
	struct snap_virtio_ctrl_queue_state *queue_state;
	int i;
	int total_len, len;

	hdr = buf;
	snap_info("--- %s %d bytes ---\n", hdr->name, hdr->len);
	total_len = hdr->len;

	hdr++;
	len = hdr->len;
	snap_info(">> %s %d bytes\n", hdr->name, hdr->len);
	common_state = (struct snap_virtio_ctrl_common_state *)(hdr + 1);
	snap_info(">>> ctrl_state: %d dev_ftr_sel: %d dev_ftrs: 0x%lx drv_ftr_sel: %d drv_ftrs: 0x%lx msi_x: 0x%0x num_queues: %d queue_select: %d status: 0x%0x config_gen: %d\n",
		  common_state->ctrl_state,
		  common_state->device_feature_select,
		  common_state->device_feature,
		  common_state->driver_feature_select,
		  common_state->driver_feature,
		  common_state->msix_config,
		  common_state->num_queues,
		  common_state->queue_select,
		  common_state->device_status,
		  common_state->config_generation);

	hdr = (struct snap_virtio_ctrl_section *)(common_state + 1);
	len += hdr->len;
	snap_info(">> %s %d bytes\n", hdr->name, hdr->len);
	queue_state = (struct snap_virtio_ctrl_queue_state *)(hdr + 1);
	for (i = 0; i < common_state->num_queues; i++) {
		snap_info(">>> size: %d msix: %d enable: %d notify_offset: %d desc 0x%lx driver 0x%lx device 0x%lx hw_avail_idx: %d hw_used_idx: %d\n",
			  queue_state[i].queue_size,
			  queue_state[i].queue_msix_vector,
			  queue_state[i].queue_enable,
			  queue_state[i].queue_notify_off,
			  queue_state[i].queue_desc,
			  queue_state[i].queue_driver,
			  queue_state[i].queue_device,
			  queue_state[i].hw_available_index,
			  queue_state[i].hw_used_index);
	}

	if (len > total_len)
		goto done;

	hdr = (struct snap_virtio_ctrl_section *)((char *)hdr + hdr->len);
	snap_info(">> %s %d bytes\n", hdr->name, hdr->len);
	if (ctrl->bar_ops->dump_state)
		ctrl->bar_ops->dump_state(ctrl, (void *)(hdr + 1), hdr->len);
	else
		snap_info("*** cannot handle %s section ***\n", hdr->name);

done:
	snap_info("--- state end ---\n");
}

static void
snap_virtio_ctrl_save_common_state(struct snap_virtio_ctrl_common_state *common_state,
				   enum snap_virtio_ctrl_state ctrl_state,
				   const struct snap_virtio_device_attr *attr)
{
	/* save common and device configs */
	common_state->ctrl_state = ctrl_state;

	common_state->device_feature_select = attr->device_feature_select;
	common_state->driver_feature_select = attr->driver_feature_select;
	common_state->queue_select = attr->queue_select;

	common_state->device_feature = attr->device_feature;
	common_state->driver_feature = attr->driver_feature;
	common_state->msix_config = attr->msix_config;
	common_state->num_queues = attr->max_queues;
	common_state->device_status = attr->status;
	common_state->config_generation = attr->config_generation;
}

static void
snap_virtio_ctrl_save_queue_state(struct snap_virtio_ctrl_queue_state *queue_state,
				  const struct snap_virtio_queue_attr *vq)
{
	queue_state->queue_size = vq->size;
	queue_state->queue_msix_vector = vq->msix_vector;
	queue_state->queue_enable = vq->enable;
	queue_state->queue_notify_off = vq->notify_off;
	queue_state->queue_desc = vq->desc;
	queue_state->queue_driver = vq->driver;
	queue_state->queue_device = vq->device;
	queue_state->hw_available_index = 0;
	queue_state->hw_used_index = 0;
}

static int snap_virtio_ctrl_save_dev_state(struct snap_virtio_ctrl *ctrl,
					   struct snap_virtio_device_attr *attr,
					   void *buf, size_t dev_cfg_len)
{
	int ret = 0;

	if (dev_cfg_len)
		ret = ctrl->bar_ops->get_state(ctrl, attr, buf, dev_cfg_len);

	return ret;
}

static int snap_virtio_ctrl_save_all_queues(struct snap_virtio_ctrl *ctrl,
					    struct snap_virtio_ctrl_queue_state *queue_state)
{
	int i;
	int ret = 0;

	snap_pgs_suspend(&ctrl->pg_ctx);
	for (i = 0; i < ctrl->max_queues; i++) {
		struct snap_virtio_queue_attr *vq;

		vq = to_virtio_queue_attr(ctrl, ctrl->bar_curr, i);
		snap_virtio_ctrl_save_queue_state(&queue_state[i], vq);

		/* if enabled, call specific queue impl to get
		 * hw_avail and used
		 **/
		if (vq->enable && ctrl->q_ops->get_state) {
			ret = ctrl->q_ops->get_state(ctrl->queues[i], &queue_state[i]);
			if (ret) {
				snap_pgs_resume(&ctrl->pg_ctx);
				return -EINVAL;
			}
		}
	}
	snap_pgs_resume(&ctrl->pg_ctx);

	return ret;
}

/**
 * snap_virtio_ctrl_state_save() - Save virtio controller state
 * @ctrl:     virtio controller
 * @buf:      buffer to save the controller state
 * @len:      buffer length
 *
 * The function saves virtio state into the provided buffer. The buffer must
 * be large enough to hold the state. snap_virtio_ctrl_state_size() can be used
 * to find the minimum required buffer size.
 *
 * The function stops all queue polling groups while saving the state. As the
 * result it must be called from the admin thread context. If called from another
 * context, the caller is repsonsible for locking admin polling thread.
 *
 * NOTES:
 *  - since function stops all queues, calling it freaquently will impact
 *    performance.
 *  - For snap_virtio_ctrl_state_restore() to work the controller state must be
 *    either SNAP_VIRTIO_CTRL_STOPPED or SNAP_VIRTIO_CTRL_SUSPENDED
 *
 * Return:
 * total state length or -errno on error
 */
int snap_virtio_ctrl_state_save(struct snap_virtio_ctrl *ctrl, void *buf, size_t len)
{
	int total_len;
	int ret;
	struct snap_virtio_ctrl_common_state *common_state;
	struct snap_virtio_ctrl_queue_state *queue_state;
	void *device_state;
	struct snap_virtio_ctrl_state_hdrs state_hdrs;

	/* reserve the 'place' for the sections and the sections data */
	total_len = snap_virtio_ctrl_save_init_hdrs(ctrl, buf, len, &state_hdrs);
	if (total_len < 0)
		return -EINVAL;

	common_state = section_hdr_to_data(state_hdrs.common_state_hdr);
	snap_virtio_ctrl_save_common_state(common_state, ctrl->state, ctrl->bar_curr);

	/* save queue state for every queue */
	queue_state = section_hdr_to_data(state_hdrs.queues_state_hdr);
	if (!queue_state)
		return -EINVAL;

	/* save queue state for every queue */
	ret = snap_virtio_ctrl_save_all_queues(ctrl, queue_state);
	if (ret < 0)
		return ret;

	device_state = section_hdr_to_data(state_hdrs.dev_state_hdr);
	if (device_state) {
		ret = snap_virtio_ctrl_save_dev_state(ctrl, ctrl->bar_curr,
						      device_state,
						      state_hdrs.dev_state_hdr->len);
		if (ret < 0)
			return -EINVAL;
	}

	dump_state(ctrl, buf);
	return total_len;
}

/**
 * snap_virtio_ctrl_state_restore() - Restore virtio controllerr state
 * @ctrl:     virtio controller
 * @buf:      buffer to restore the controller state from
 * @len:      buffer length
 *
 * The function restores virtio state from the provided buffer. The buffer must
 * be large enough to hold the state. snap_virtio_ctrl_state_size() can be used
 * to find the minimum required buffer size.
 *
 * The function should be called from the admin thread context. If called from
 * another context, the caller is repsonsible for locking admin polling thread.
 *
 * The internal controller state must be either SNAP_VIRTIO_CTRL_STOPPED or
 * SNAP_VIRTIO_CTRL_SUSPENDED. Otherwise the function will fail.
 *
 * Return:
 * total state length or -errno on error
 */
int snap_virtio_ctrl_state_restore(struct snap_virtio_ctrl *ctrl,
				   const void *buf, size_t len)
{
	const struct snap_virtio_ctrl_section *hdr;
	struct snap_virtio_ctrl_common_state *common_state;
	const struct snap_virtio_ctrl_queue_state *queue_state;
	const void *device_state;
	int total_len = 0;
	int ret = 0;
	int i;

	dump_state(ctrl, buf);

	/* there is no way to restore just common state */
	if (!ctrl->bar_ops->set_state)
		return -ENOTSUP;

	/* controller must be either stopped or suspended */
	if (!snap_virtio_ctrl_is_stopped(ctrl) &&
	    !snap_virtio_ctrl_is_suspended(ctrl)) {
		snap_error("controller state (%d) must be either STOPPED or SUSPENDED\n",
			   ctrl->state);
		return -EINVAL;
	}

	/* header */
	hdr = buf;
	if (hdr->len > len) {
		snap_error("controller state is truncated\n");
		return -EINVAL;
	}

	/* common */
	hdr++;
	common_state = (struct snap_virtio_ctrl_common_state *)(hdr + 1);
	total_len += hdr->len;

	/* queues */
	hdr = (struct snap_virtio_ctrl_section *)(common_state + 1);
	queue_state = (struct snap_virtio_ctrl_queue_state *)(hdr + 1);
	total_len += hdr->len;

	/* device config */
	if (total_len < len) {
		hdr = (struct snap_virtio_ctrl_section *)((char *)hdr + hdr->len);
		device_state = hdr + 1;
	} else
		device_state = NULL;

	if (common_state->ctrl_state != SNAP_VIRTIO_CTRL_STOPPED &&
	    common_state->ctrl_state != SNAP_VIRTIO_CTRL_SUSPENDED) {
		snap_error("original controller state (%d) must be either STOPPED or SUSPENDED\n",
			   ctrl->state);
		return -EINVAL;
	}

	snap_info("state: %d -> %d  status: %d -> %d\n",
		  common_state->ctrl_state, ctrl->state,
		  common_state->device_status, ctrl->bar_curr->status);

	if (snap_virtio_ctrl_is_suspended(ctrl))
		snap_virtio_ctrl_stop(ctrl);

	ctrl->state = common_state->ctrl_state;

	ctrl->bar_curr->device_feature = common_state->device_feature;
	ctrl->bar_curr->driver_feature = common_state->driver_feature;
	ctrl->bar_curr->msix_config = common_state->msix_config;
	ctrl->bar_curr->max_queues = common_state->num_queues;
	ctrl->bar_curr->status = common_state->device_status;
	ctrl->bar_curr->config_generation = common_state->config_generation;

	ctrl->bar_curr->queue_select = common_state->queue_select;
	ctrl->bar_curr->device_feature_select = common_state->device_feature_select;
	ctrl->bar_curr->driver_feature_select = common_state->driver_feature_select;

	for (i = 0; i < ctrl->max_queues; i++) {
		struct snap_virtio_queue_attr *vq;

		vq = to_virtio_queue_attr(ctrl, ctrl->bar_curr, i);

		vq->size = queue_state[i].queue_size;
		vq->msix_vector = queue_state[i].queue_msix_vector;
		vq->enable = queue_state[i].queue_enable;
		vq->notify_off = queue_state[i].queue_notify_off;
		vq->desc = queue_state[i].queue_desc;
		vq->driver = queue_state[i].queue_driver;
		vq->device = queue_state[i].queue_device;
	}

	ret = ctrl->bar_ops->set_state(ctrl, ctrl->bar_curr, queue_state,
				       device_state, hdr->len);
	if (ret)
		return ret;

	/* start controller in the suspended state. Otherwise the controller
	 * will start doing dma to/from host memory even before it is
	 * unfreezed and unquiesced.
	 */
	if (common_state->ctrl_state == SNAP_VIRTIO_CTRL_SUSPENDED)
		ret = snap_virtio_ctrl_start(ctrl);

	snap_virtio_ctrl_bar_copy(ctrl, ctrl->bar_curr, ctrl->bar_prev);
	return ret;
}

int snap_virtio_ctrl_quiesce_adm(void *data)
{
	struct snap_virtio_ctrl *ctrl = data;
	int ret = 0;

	snap_virtio_ctrl_progress_lock(ctrl);

	if (ctrl->lm_state != SNAP_VIRTIO_CTRL_LM_RUNNING) {
		ret = -EINVAL;
		goto err;
	}

	if (snap_virtio_ctrl_is_stopped(ctrl))
		goto done;

	ret = snap_virtio_ctrl_suspend(ctrl);
	if (ret)
		goto err;

	/*
	 * Mark ctrl as in process of quiesce,
	 * checked in snap_virtio_ctrl_progress_suspend()
	 */
	ctrl->is_quiesce = true;
	snap_virtio_ctrl_progress_unlock(ctrl);
	return 0;
done:
	snap_virtio_ctrl_set_lm_state(ctrl, SNAP_VIRTIO_CTRL_LM_QUIESCED);
err:
	snap_virtio_ctrl_progress_unlock(ctrl);
	snap_info("%p: queisce: new state %s ret %d\n", ctrl,
		  lm_state2str(ctrl->lm_state), ret);

	/* Complete modify internal state to quiesce command */
	if (ret)
		snap_vaq_cmd_complete(ctrl->quiesce_cmd, SNAP_VIRTIO_ADM_STATUS_ERR);
	else
		snap_vaq_cmd_complete(ctrl->quiesce_cmd, SNAP_VIRTIO_ADM_STATUS_OK);
	ctrl->quiesce_cmd = NULL;
	return ret;
}

static int snap_virtio_ctrl_quiesce(void *data)
{
	struct snap_virtio_ctrl *ctrl = data;
	int ret = 0;

	snap_virtio_ctrl_progress_lock(ctrl);

	if (ctrl->lm_state != SNAP_VIRTIO_CTRL_LM_RUNNING) {
		ret = -EINVAL;
		goto err;
	}

	if (snap_virtio_ctrl_is_stopped(ctrl))
		goto done;

	ret = snap_virtio_ctrl_suspend(ctrl);
	if (ret)
		goto err;

	/* TODO: add timeout */
	while (!snap_virtio_ctrl_is_suspended(ctrl)) {
		snap_virtio_ctrl_progress_unlock(ctrl);
		usleep(100);
		snap_virtio_ctrl_progress_lock(ctrl);
	}
done:
	snap_virtio_ctrl_set_lm_state(ctrl, SNAP_VIRTIO_CTRL_LM_QUIESCED);
err:
	snap_virtio_ctrl_progress_unlock(ctrl);
	snap_info("%p: queisce: new state %s ret %d\n", ctrl,
		  lm_state2str(ctrl->lm_state), ret);
	return ret;
}

int snap_virtio_ctrl_unquiesce(void *data)
{
	struct snap_virtio_ctrl *ctrl = data;
	int ret = 0;

	snap_virtio_ctrl_progress_lock(ctrl);

	if (ctrl->lm_state != SNAP_VIRTIO_CTRL_LM_QUIESCED) {
		ret = -EINVAL;
		goto err;
	}

	ret = snap_virtio_ctrl_resume(ctrl);
	if (ret)
		goto err;

	snap_virtio_ctrl_set_lm_state(ctrl, SNAP_VIRTIO_CTRL_LM_RUNNING);
err:
	snap_virtio_ctrl_progress_unlock(ctrl);
	snap_info("%p: unqueisce: new state %s ret %d\n", ctrl,
		  lm_state2str(ctrl->lm_state), ret);
	return ret;
}

int snap_virtio_ctrl_freeze(void *data)
{
	struct snap_virtio_ctrl *ctrl = data;
	int ret = 0;

	snap_virtio_ctrl_progress_lock(ctrl);

	if (ctrl->lm_state != SNAP_VIRTIO_CTRL_LM_QUIESCED) {
		ret = -EINVAL;
		goto err;
	}
	snap_virtio_ctrl_set_lm_state(ctrl, SNAP_VIRTIO_CTRL_LM_FREEZED);
err:
	snap_virtio_ctrl_progress_unlock(ctrl);
	snap_info("%p: freeze: new state %s ret %d\n", ctrl,
		  lm_state2str(ctrl->lm_state), ret);
	return ret;
}

int snap_virtio_ctrl_unfreeze(void *data)
{
	struct snap_virtio_ctrl *ctrl = data;
	int ret = 0;

	snap_virtio_ctrl_progress_lock(ctrl);

	if (ctrl->lm_state != SNAP_VIRTIO_CTRL_LM_FREEZED) {
		ret = -EINVAL;
		goto err;
	}
	snap_virtio_ctrl_set_lm_state(ctrl, SNAP_VIRTIO_CTRL_LM_QUIESCED);
err:
	snap_virtio_ctrl_progress_unlock(ctrl);
	snap_info("%p: unfreeze: new state %s ret %d\n", ctrl,
		  lm_state2str(ctrl->lm_state), ret);
	return ret;
}

int snap_virtio_ctrl_get_state_size(void *data)
{
	struct snap_virtio_ctrl *ctrl = data;
	size_t dev_cfg_len, queue_cfg_len, common_cfg_len, len;

	snap_info("%p: get_state_size ", ctrl);
	snap_virtio_ctrl_progress_lock(ctrl);

	if (ctrl->lm_state != SNAP_VIRTIO_CTRL_LM_FREEZED) {
		/* act as if we don't support state tracking */
		snap_virtio_ctrl_progress_unlock(ctrl);
		snap_info("not freezed, no state tracking - zero state size\n");
		return 0;
	}
	len = snap_virtio_ctrl_state_size(ctrl, &common_cfg_len, &queue_cfg_len,
					  &dev_cfg_len);

	snap_virtio_ctrl_progress_unlock(ctrl);
	snap_info("%lu\n", len);
	return len;
}

static int snap_virtio_ctrl_copy_state(void *data, void *buf, int len,
				       bool copy_from_buffer)
{
	struct snap_virtio_ctrl *ctrl = data;
	int ret;

	snap_virtio_ctrl_progress_lock(ctrl);

	if (!copy_from_buffer)
		ret = snap_virtio_ctrl_state_save(ctrl, buf, len);
	else
		ret = snap_virtio_ctrl_state_restore(ctrl, buf, len);

	snap_virtio_ctrl_progress_unlock(ctrl);
	snap_info("%p: lm_state %s: copy state: dir %s buf %p len %d ret %d\n", ctrl,
		  lm_state2str(ctrl->lm_state),
		  copy_from_buffer ? "from_buffer" : "to_buffer", buf, len, ret);
	return ret < 0 ? -1 : 0;
}

static int snap_virtio_ctrl_start_dirty_pages_track(void *data)
{
	struct snap_virtio_ctrl *ctrl = data;

	snap_virtio_ctrl_progress_lock(ctrl);
	snap_virtio_ctrl_log_writes(ctrl, true);
	snap_virtio_ctrl_progress_unlock(ctrl);
	snap_info("%p: start dirty pages track\n", ctrl);
	return 0;
}

static int snap_virtio_ctrl_stop_dirty_pages_track(void *data)
{
	struct snap_virtio_ctrl *ctrl = data;

	snap_virtio_ctrl_progress_lock(ctrl);
	snap_virtio_ctrl_log_writes(ctrl, false);
	snap_virtio_ctrl_progress_unlock(ctrl);
	snap_info("%p: stop dirty pages track\n", ctrl);
	return 0;
}

static uint16_t snap_virtio_ctrl_get_pci_bdf(void *data)
{
	struct snap_virtio_ctrl *ctrl = data;
	uint16_t bdf;

	snap_virtio_ctrl_progress_lock(ctrl);
	bdf = ctrl->sdev->pci->pci_bdf.raw;
	snap_virtio_ctrl_progress_unlock(ctrl);
	snap_info("%p: get_pci_bdf: 0x0%x\n", ctrl, bdf);
	return bdf;
}

enum snap_virtio_ctrl_lm_state snap_virtio_ctrl_get_lm_state(void *data)
{
	struct snap_virtio_ctrl *ctrl = data;
	enum snap_virtio_ctrl_lm_state lm_state;

	snap_virtio_ctrl_progress_lock(ctrl);
	lm_state = ctrl->lm_state;
	snap_virtio_ctrl_progress_unlock(ctrl);
	return lm_state;
}

static struct snap_migration_ops snap_virtio_ctrl_migration_ops = {
	.quiesce = snap_virtio_ctrl_quiesce,
	.unquiesce = snap_virtio_ctrl_unquiesce,
	.freeze = snap_virtio_ctrl_freeze,
	.unfreeze = snap_virtio_ctrl_unfreeze,
	.get_state_size = snap_virtio_ctrl_get_state_size,
	.copy_state = snap_virtio_ctrl_copy_state,
	.start_dirty_pages_track = snap_virtio_ctrl_start_dirty_pages_track,
	.stop_dirty_pages_track = snap_virtio_ctrl_stop_dirty_pages_track,
	.get_pci_bdf = snap_virtio_ctrl_get_pci_bdf,
	.get_lm_state = snap_virtio_ctrl_get_lm_state
};

/**
 * snap_virtio_ctrl_lm_enable() - enable virtio controlelr live migration
 * @ctrl:   virtio controller
 * @name:   live migration channel name
 *
 * The function opens live migration channel @name and attaches it to the
 * controller.
 *
 * Return:
 * 0 or -errno on error
 */
int snap_virtio_ctrl_lm_enable(struct snap_virtio_ctrl *ctrl, const char *name)
{
	int ret = 0;

	if (ctrl->lm_channel) {
		snap_error("%p: controller already has a migration channel %s\n",
			   ctrl, ctrl->lm_channel->channel_ops->name);
		return -EEXIST;
	}

	ctrl->lm_channel = snap_channel_open(name, &snap_virtio_ctrl_migration_ops, ctrl);
	if (!ctrl->lm_channel)
		ret = -errno;
	return ret;
}

/**
 * snap_virtio_ctrl_lm_disable() - disable virtio controller live migration
 * @ctrl:   virtio controller
 * @name:   live migration channel name
 *
 * The function disables live migration channel
 */
void snap_virtio_ctrl_lm_disable(struct snap_virtio_ctrl *ctrl)
{
	if (!ctrl->lm_channel)
		return;
	snap_channel_close(ctrl->lm_channel);
	ctrl->lm_channel = NULL;
}

static int snap_virtio_ctrl_queue_recover_indexes(struct snap_virtio_ctrl *ctrl,
						  struct snap_virtio_ctrl_queue_state *q_state)
{
	struct vring_avail vra;
	struct vring_used vru;
	int ret;

	/* Get available & used indexes of virtio ctrl queue (vring) from host memory */

	ret = snap_virtio_get_vring_indexes_from_host(ctrl->lb_pd,
						      q_state->queue_driver,
						      q_state->queue_device,
						      ctrl->xmkey->mkey, &vra, &vru);
	if (!ret) {
		q_state->hw_available_index = vra.idx;
		q_state->hw_used_index = vru.idx;
	}

	return ret;
}

/**
 * snap_virtio_ctrl_recover() - Recover virtio controller
 * @ctrl:     virtio controller
 * @attr:     virtio device atttribute
 *
 * This function will recover the virtio controller:
 *
 * The state of the controller will be saved.
 *
 * Note: at this point, the controller was not started yet,
 * controller state must be SNAP_VIRTIO_CTRL_SUSPENDED.
 * The state of contoller's queues will be saved as well.
 * The avail & used indexes of the queues (if the queue is enabled)
 * will be retrieved from the host's memory.
 *
 * The controller will be restored (using the saved state)
 * and after that, the controller will be resumed.
 *
 * Return:
 * 0 on success or -errno on error
 */
int snap_virtio_ctrl_recover(struct snap_virtio_ctrl *ctrl,
			     struct snap_virtio_device_attr *attr)
{
	const struct snap_virtio_queue_attr *vq;

	void *buf;
	struct snap_virtio_ctrl_state_hdrs state_hdrs;
	struct snap_virtio_ctrl_common_state *common_state;
	struct snap_virtio_ctrl_queue_state *queue_state;
	void *device_state;
	int i, ret, total_len;
	int outstanding = 0;

	snap_debug("recover the virtio ctrl\n");

	if (!snap_virtio_ctrl_is_suspended(ctrl)) {
		snap_error("original controller state (%d) must be SUSPENDED\n",
			   ctrl->state);
		return -EINVAL;
	}

	if (!ctrl->bar_ops->queue_attr_valid(attr)) {
		snap_error("virtio device q attributes are not valid\n");
		return -EINVAL;
	}

	/* at the moment always allow saving state but limit restore */
	total_len = snap_virtio_ctrl_state_size(ctrl, NULL, NULL, NULL);
	buf = calloc(1, total_len);
	if (!buf)
		return -ENOMEM;

	/* reserve the 'place' for the sections and the sections data */
	ret = snap_virtio_ctrl_save_init_hdrs(ctrl, buf, total_len, &state_hdrs);
	if (ret < 0)
		goto free_buf;

	common_state = section_hdr_to_data(state_hdrs.common_state_hdr);
	/* save common and device configs */
	snap_virtio_ctrl_save_common_state(common_state, ctrl->state, attr);

	/* save queue state for every queue */
	queue_state = section_hdr_to_data(state_hdrs.queues_state_hdr);

	for (i = 0; i < ctrl->max_queues; ++i) {
		vq = to_virtio_queue_attr(ctrl, attr, i);
		snap_virtio_ctrl_save_queue_state(&queue_state[i], vq);

		/* if enabled, read hw_avail and used from host memory*/
		if (vq->enable) {
			ret = snap_virtio_ctrl_queue_recover_indexes(ctrl, &queue_state[i]);
			snap_info("q: %d avail: %d used: %d\n", i,
				  queue_state[i].hw_available_index,
				  queue_state[i].hw_used_index);
			if (ret != 0)
				goto free_buf;

			/* will cause re-send all requests between available & used again */
			if (queue_state[i].hw_available_index != queue_state[i].hw_used_index) {
				++outstanding;
				queue_state[i].hw_available_index = queue_state[i].hw_used_index;
			}
		}
	}

	if (outstanding)
		snap_warn("Outstanding requests are detected on queue(s). If the previous controller was not run in ordered mode the recovery may not work correctly.\n");
	device_state = section_hdr_to_data(state_hdrs.dev_state_hdr);
	if (device_state) {
		ret = snap_virtio_ctrl_save_dev_state(ctrl, attr, device_state,
						      state_hdrs.dev_state_hdr->len);
		if (ret < 0)
			goto free_buf;
	}

	ret = snap_virtio_ctrl_state_restore(ctrl, buf, total_len);
	if (!ret)
		ret = snap_virtio_ctrl_resume(ctrl);

	if (!ret && ctrl->state == SNAP_VIRTIO_CTRL_SUSPENDED)
		ret = snap_virtio_ctrl_stop(ctrl);

free_buf:
	free(buf);
	return ret;
}

/**
 * snap_virtio_ctrl_should_recover() - check if controller should recover
 * @ctrl:	controller instance
 *
 * Query the status fields of the virtio bar (reset/enable/status).
 * Check if recovery should be applied based on the values
 * of the fileds.
 *
 * Return: -errno in case of error, otherwise if recovery
 * mode should be applied.
 */
int snap_virtio_ctrl_should_recover(struct snap_virtio_ctrl *ctrl)
{
	int rc;
	struct snap_virtio_device_attr vattr;
	int is_reset = 0, is_recovery_needed = 0;

	if (!ctrl->bar_ops->get_attr)
		return -EINVAL;

	rc = ctrl->bar_ops->get_attr(ctrl, &vattr);
	if (rc)
		return rc < 0 ? rc : -rc;
	if (vattr.reset || (vattr.status & SNAP_VIRTIO_DEVICE_S_DEVICE_NEEDS_RESET))
		is_reset = 1;
	if (vattr.enabled) {
		if (!is_reset && (vattr.status & SNAP_VIRTIO_DEVICE_S_DRIVER_OK))
			is_recovery_needed = 1;
	}
	if (!is_recovery_needed) {
		snap_info("Bar status - enabled: %d reset: %d status: 0x%x, recovery mode not applied.\n",
				vattr.enabled, vattr.reset, vattr.status);
	}
	return is_recovery_needed;
}

const struct snap_virtio_ctrl_queue_stats *
snap_virtio_ctrl_q_io_stats(struct snap_virtio_ctrl *ctrl, uint16_t q_idx)
{
	if (q_idx < ctrl->max_queues) {
		if (ctrl->queues[q_idx] && ctrl->q_ops->get_io_stats)
			return  ctrl->q_ops->get_io_stats(ctrl->queues[q_idx]);
	}

	return NULL;
}
