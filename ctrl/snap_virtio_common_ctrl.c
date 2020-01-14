#include "snap_virtio_common_ctrl.h"

/*
 * Device ERROR can be discovered in 2 cases:
 *  - host driver raises FAILED bit in device_status,
 *    declaring that it gave up the device.
 *  - device/driver raises DEVICE_NEEDS_RESET bit.
 *
 * In both cases, the only way for the host driver to recover from such
 * status is by resetting the device.
 */
#define SNAP_VIRTIO_CTRL_DEV_STATUS_ERR(status) \
		!!((status & SNAP_VIRTIO_DEVICE_S_DEVICE_NEEDS_RESET) || \
		   (status & SNAP_VIRTIO_DEVICE_S_FAILED))
#define SNAP_VIRTIO_CTRL_ERR_DETECTED(vctrl) \
		(!SNAP_VIRTIO_CTRL_DEV_STATUS_ERR(vctrl->bar_prev->status) && \
		 SNAP_VIRTIO_CTRL_DEV_STATUS_ERR(vctrl->bar_curr->status))

/*
 * Device RESET can be discovered when `device_status` register is zeroed
 * out by host driver (a.k.a moved to RESET state).
 */
#define SNAP_VIRTIO_CTRL_RST_DETECTED(vctrl) \
		((vctrl->bar_prev->status != SNAP_VIRTIO_DEVICE_S_RESET) && \
		 (vctrl->bar_curr->status == SNAP_VIRTIO_DEVICE_S_RESET))

/*
 * PCI FLR (Function Level Reset) is discovered by monitoring the `enabled`
 * snap emulation device attribute (When moving from `1` to `0`).
 */
#define SNAP_VIRTIO_CTRL_FLR_DETECTED(vctrl) \
		(vctrl->bar_prev->enabled && !vctrl->bar_curr->enabled)

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
	TAILQ_REMOVE(&ctrl->live_queues, vq, entry);
	pthread_spin_unlock(&ctrl->live_queues_lock);

	ctrl->q_ops->destroy(vq);
}

static void snap_virtio_ctrl_queue_progress(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_virtio_ctrl *ctrl = vq->ctrl;

	ctrl->q_ops->progress(vq);
}

static inline bool snap_virtio_ctrl_needs_reset(struct snap_virtio_ctrl *ctrl)
{
	/*
	 * Device needs to be reset in a few cases:
	 *  - DEVICE_NEEDS_RESET/FAILED status raised
	 *  - FLR occured (enabled bit removed)
	 *  - device_status was changed to RESET ("0").
	 */
	return SNAP_VIRTIO_CTRL_ERR_DETECTED(ctrl) ||
	       SNAP_VIRTIO_CTRL_RST_DETECTED(ctrl) ||
	       SNAP_VIRTIO_CTRL_FLR_DETECTED(ctrl);
}

static int snap_virtio_ctrl_change_status(struct snap_virtio_ctrl *ctrl)
{
	int ret = 0;

	if (snap_virtio_ctrl_needs_reset(ctrl))
		ret = snap_virtio_ctrl_stop(ctrl);
	else if (SNAP_VIRTIO_CTRL_LIVE_DETECTED(ctrl))
		ret = snap_virtio_ctrl_start(ctrl);

	return ret;
}

int snap_virtio_ctrl_start(struct snap_virtio_ctrl *ctrl)
{
	int ret = 0;
	int i;
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
				break;
			}
		}
	}

	if (!ret)
		ctrl->state = SNAP_VIRTIO_CTRL_STARTED;
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

	if (SNAP_VIRTIO_CTRL_RST_DETECTED(ctrl)) {
		/*
		 * Host driver is waiting for device RESET process completion
		 * by polling device_status until reading `0`.
		 */
		ctrl->bar_curr->status = 0;
		ret = snap_virtio_ctrl_bar_modify(ctrl,
						  SNAP_VIRTIO_MOD_DEV_STATUS,
						  ctrl->bar_curr);
	}

	if (!ret)
		ctrl->state = SNAP_VIRTIO_CTRL_STOPPED;
out:
	pthread_mutex_unlock(&ctrl->state_lock);
	return ret;
}

void snap_virtio_ctrl_progress(struct snap_virtio_ctrl *ctrl)
{
	int ret;

	ret = snap_virtio_ctrl_bar_update(ctrl, ctrl->bar_curr);
	if (ret)
		return;

	/* Handle device_status changes */
	if ((ctrl->bar_curr->status != ctrl->bar_prev->status) ||
	    (ctrl->bar_curr->enabled != ctrl->bar_prev->enabled)) {
		ret = snap_virtio_ctrl_change_status(ctrl);
		if (ret)
			return;
	}

	snap_virtio_ctrl_bar_copy(ctrl, ctrl->bar_curr, ctrl->bar_prev);
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
	ctrl->sdev = snap_open_device(sctx, &sdev_attr);
	if (!ctrl->sdev) {
		ret = -ENODEV;
		goto err;
	}

	ctrl->bar_ops = bar_ops;
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
