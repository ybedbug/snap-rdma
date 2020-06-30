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

static int snap_virtio_ctrl_bars_teardown(struct snap_virtio_ctrl *ctrl)
{
	snap_virtio_ctrl_bar_destroy(ctrl, ctrl->bar_prev);
	snap_virtio_ctrl_bar_destroy(ctrl, ctrl->bar_curr);
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
	pthread_spin_lock(&ctrl->live_queues_lock);
	TAILQ_INSERT_HEAD(&ctrl->live_queues, vq, entry);
	pthread_spin_unlock(&ctrl->live_queues_lock);

	return vq;
}

static void snap_virtio_ctrl_queue_destroy(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_virtio_ctrl *ctrl = vq->ctrl;

	pthread_spin_lock(&ctrl->live_queues_lock);
	SNAP_TAILQ_REMOVE_SAFE(&ctrl->live_queues, vq, entry);
	pthread_spin_unlock(&ctrl->live_queues_lock);

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
		if (!ret) {
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

	for (i = 0; i < ctrl->num_queues; i++) {
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

	for (i = 0; i < ctrl->num_queues; i++) {
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
}

void snap_virtio_ctrl_io_progress(struct snap_virtio_ctrl *ctrl)
{
	struct snap_virtio_ctrl_queue *vq;

	pthread_spin_lock(&ctrl->live_queues_lock);
	TAILQ_FOREACH(vq, &ctrl->live_queues, entry)
		snap_virtio_ctrl_queue_progress(vq);
	pthread_spin_unlock(&ctrl->live_queues_lock);
}

int snap_virtio_ctrl_open(struct snap_virtio_ctrl *ctrl,
			  struct snap_virtio_ctrl_bar_ops *bar_ops,
			  struct snap_virtio_queue_ops *q_ops,
			  struct snap_context *sctx,
			  const struct snap_virtio_ctrl_attr *attr)
{
	int ret = 0;
	struct snap_device_attr sdev_attr = {0};

	if (!sctx) {
		ret = -ENODEV;
		goto err;
	}

	sdev_attr.pf_id = attr->pf_id;
	switch (attr->type) {
	case SNAP_VIRTIO_BLK_CTRL:
		ctrl->num_queues = sctx->virtio_blk_caps.max_emulated_virtqs;
		sdev_attr.type = SNAP_VIRTIO_BLK_PF;
		break;
	case SNAP_VIRTIO_NET_CTRL:
		ctrl->num_queues = sctx->virtio_net_caps.max_emulated_virtqs;
		sdev_attr.type = SNAP_VIRTIO_NET_PF;
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
	ctrl->queues = calloc(ctrl->num_queues, sizeof(*ctrl->queues));
	if (!ctrl->queues) {
		ret = -ENOMEM;
		goto mutex_destroy;
	}

	TAILQ_INIT(&ctrl->live_queues);
	ret = pthread_spin_init(&ctrl->live_queues_lock, 0);
	if (ret)
		goto free_queues;

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
	if (!TAILQ_EMPTY(&ctrl->live_queues))
		snap_warn("Closing ctrl with queues still active");
	pthread_spin_destroy(&ctrl->live_queues_lock);
	free(ctrl->queues);
	pthread_mutex_destroy(&ctrl->state_lock);
	snap_virtio_ctrl_bars_teardown(ctrl);
	snap_close_device(ctrl->sdev);
}
