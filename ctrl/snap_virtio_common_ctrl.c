#include "snap_virtio_common_ctrl.h"
#include "snap_queue.h"

/*
 * Driver may choose to reset device for numerous reasons:
 * during initialization, on error, or during FLR.
 * Driver executes reset by writing `0` to `device_status` bar register.
 * According to virtio v0.95 spec., driver is not obligated to wait
 * for device to finish the RESET command, which may cause race conditions
 * to occur between driver and controller.
 * Issue is solved by using the extra internal `enabled` bit:
 *  - FW set bit to `0` on driver reset.
 *  - Controller set it back to `1` once finished.
 */
#define SNAP_VIRTIO_CTRL_RESET_DETECTED(vctrl) \
		(!vctrl->bar_curr->enabled)

/*
 * DRIVER_OK bit indicates that the driver is set up and ready to drive the
 * device. Only at this point, device is considered "live".
 * Prior to that, it is not promised that any driver resource is available
 * for the device to use.
 */
#define SNAP_VIRTIO_CTRL_LIVE_DETECTED(vctrl) \
		!!(!(vctrl->bar_prev->status & SNAP_VIRTIO_DEVICE_S_DRIVER_OK) && \
		    (vctrl->bar_curr->status & SNAP_VIRTIO_DEVICE_S_DRIVER_OK))

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
	copy->num_of_vfs = orig->num_of_vfs;
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

static void snap_virtio_ctrl_sched_q(struct snap_virtio_ctrl *ctrl,
				     struct snap_virtio_ctrl_queue *vq)
{
	struct snap_pg *pg;

	pg = snap_pg_get_next(&ctrl->pg_ctx);
	pthread_spin_lock(&pg->lock);
	TAILQ_INSERT_TAIL(&pg->q_list, &vq->pg_q, entry);
	vq->pg = pg;
	if (ctrl->q_ops->start)
		ctrl->q_ops->start(vq);
	pthread_spin_unlock(&pg->lock);
}

static void snap_virtio_ctrl_desched_q(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_pg *pg = vq->pg;

	if (!pg)
		return;

	pthread_spin_lock(&pg->lock);
	TAILQ_REMOVE(&pg->q_list, &vq->pg_q, entry);
	vq->pg = NULL;
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
	snap_virtio_ctrl_sched_q(ctrl, vq);

	return vq;
}

static void snap_virtio_ctrl_queue_destroy(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_virtio_ctrl *ctrl = vq->ctrl;

	snap_virtio_ctrl_desched_q(vq);
	if (ctrl->q_ops->suspend)
		ctrl->q_ops->suspend(vq);
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

static int snap_virtio_ctrl_change_status(struct snap_virtio_ctrl *ctrl)
{
	int ret = 0;

	if (SNAP_VIRTIO_CTRL_RESET_DETECTED(ctrl)) {
		ret = snap_virtio_ctrl_stop(ctrl);
		if (!ret && ctrl->bar_curr->pci_bdf) {
			/*
			 * When done with reset process, need to set enabled bit
			 * back to `1` which signal FW to update `device_status`
			 * if needed. Host driver might be waiting for device
			 * RESET process completion by polling device_status
			 * until reading `0`.
			 */
			ctrl->bar_curr->enabled = 1;
			ret = snap_virtio_ctrl_bar_modify(ctrl,
							  SNAP_VIRTIO_MOD_ENABLED,
							  ctrl->bar_curr);
			/* The status should be 0 if Driver reset device. */
			ctrl->bar_curr->status = 0;
		}
	} else {
		ret = snap_virtio_ctrl_validate(ctrl);
		if (!ret && SNAP_VIRTIO_CTRL_LIVE_DETECTED(ctrl))
			ret = snap_virtio_ctrl_start(ctrl);
	}
	if (ret)
		snap_virtio_ctrl_device_error(ctrl);
	return ret;
}

int snap_virtio_ctrl_start(struct snap_virtio_ctrl *ctrl)
{
	int ret = 0;
	int i, j;
	const struct snap_virtio_queue_attr *vq;

	pthread_mutex_lock(&ctrl->state_lock);
	if (ctrl->state == SNAP_VIRTIO_CTRL_STARTED)
		goto out;

	for (i = 0; i < ctrl->max_queues; i++) {
		vq = to_virtio_queue_attr(ctrl, ctrl->bar_curr, i);

		if (vq->enable) {
			ctrl->queues[i] = snap_virtio_ctrl_queue_create(ctrl, i);
			if (!ctrl->queues[i]) {
				ret = -ENOMEM;
				goto vq_cleanup;
			}
		}
	}

	if (ctrl->bar_cbs.start) {
		ret = ctrl->bar_cbs.start(ctrl->cb_ctx);
		if (ret) {
			snap_virtio_ctrl_device_error(ctrl);
			goto vq_cleanup;
		}
	}
	ctrl->state = SNAP_VIRTIO_CTRL_STARTED;
	goto out;

vq_cleanup:
	for (j = 0; j < i; j++)
		if (ctrl->queues[j])
			snap_virtio_ctrl_queue_destroy(ctrl->queues[j]);

out:
	pthread_mutex_unlock(&ctrl->state_lock);
	return ret;
}

int snap_virtio_ctrl_stop(struct snap_virtio_ctrl *ctrl)
{
	int i, ret = 0;

	pthread_mutex_lock(&ctrl->state_lock);
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
out:
	pthread_mutex_unlock(&ctrl->state_lock);
	return ret;
}

bool snap_virtio_ctrl_is_stopped(struct snap_virtio_ctrl *ctrl)
{
	return ctrl->state == SNAP_VIRTIO_CTRL_STOPPED;
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

void snap_virtio_ctrl_progress(struct snap_virtio_ctrl *ctrl)
{
	int ret;

	ret = snap_virtio_ctrl_bar_update(ctrl, ctrl->bar_curr);
	if (ret)
		return;

	/* Handle device_status changes */
	if ((ctrl->bar_curr->status != ctrl->bar_prev->status) ||
	    (SNAP_VIRTIO_CTRL_RESET_DETECTED(ctrl))) {
		snap_virtio_ctrl_change_status(ctrl);
	}

	if (ctrl->bar_curr->num_of_vfs != ctrl->bar_prev->num_of_vfs)
		snap_virtio_ctrl_change_num_vfs(ctrl);
}

static inline struct snap_virtio_ctrl_queue *
pg_q_entry_to_virtio_ctrl_queue(struct snap_pg_q_entry *pg_q)
{
	return container_of(pg_q, struct snap_virtio_ctrl_queue, pg_q);
}

void snap_virtio_ctrl_pg_io_progress(struct snap_virtio_ctrl *ctrl, int pg_id)
{
	struct snap_pg *pg = &ctrl->pg_ctx.pgs[pg_id];
	struct snap_virtio_ctrl_queue *vq;
	struct snap_pg_q_entry *pg_q;

	pthread_spin_lock(&pg->lock);
	TAILQ_FOREACH(pg_q, &pg->q_list, entry) {
		vq = pg_q_entry_to_virtio_ctrl_queue(pg_q);
		snap_virtio_ctrl_queue_progress(vq);
	}
	pthread_spin_unlock(&pg->lock);
}

void snap_virtio_ctrl_io_progress(struct snap_virtio_ctrl *ctrl)
{
	int i;

	for (i = 0; i < ctrl->pg_ctx.npgs; i++)
		snap_virtio_ctrl_pg_io_progress(ctrl, i);
}

int snap_virtio_ctrl_open(struct snap_virtio_ctrl *ctrl,
			  struct snap_virtio_ctrl_bar_ops *bar_ops,
			  struct snap_virtio_queue_ops *q_ops,
			  struct snap_context *sctx,
			  const struct snap_virtio_ctrl_attr *attr)
{
	int ret = 0;
	struct snap_device_attr sdev_attr = {0};
	uint32_t npgs;

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

	sdev_attr.pf_id = attr->pf_id;
	sdev_attr.vf_id = attr->vf_id;
	switch (attr->type) {
	case SNAP_VIRTIO_BLK_CTRL:
		ctrl->max_queues = sctx->virtio_blk_caps.max_emulated_virtqs;
		sdev_attr.type = attr->pci_type;
		break;
	case SNAP_VIRTIO_NET_CTRL:
		ctrl->max_queues = sctx->virtio_net_caps.max_emulated_virtqs;
		sdev_attr.type = attr->pci_type;
		break;
	default:
		ret = -EINVAL;
		goto err;
	};
	if (attr->event)
		sdev_attr.flags |= SNAP_DEVICE_FLAGS_EVENT_CHANNEL;
	ctrl->sdev = snap_open_device(sctx, &sdev_attr);
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

	ret = pthread_mutex_init(&ctrl->state_lock, NULL);
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

	ctrl->type = attr->type;
	return 0;

free_queues:
	free(ctrl->queues);
mutex_destroy:
	pthread_mutex_destroy(&ctrl->state_lock);
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
	snap_pgs_free(&ctrl->pg_ctx);
	free(ctrl->queues);
	pthread_mutex_destroy(&ctrl->state_lock);
	snap_virtio_ctrl_bars_teardown(ctrl);
	snap_close_device(ctrl->sdev);
}
