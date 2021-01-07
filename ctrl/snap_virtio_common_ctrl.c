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
	vq->log_writes_to_host = ctrl->log_writes_to_host;
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
 * @ctrl:              virtio controller
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
int snap_virtio_ctrl_state_size(struct snap_virtio_ctrl *ctrl, unsigned *common_cfg_len,
				unsigned *queue_cfg_len, unsigned *dev_cfg_len)
{
	if (ctrl->bar_ops->get_state_size)
		*dev_cfg_len = ctrl->bar_ops->get_state_size(ctrl);
	else
		*dev_cfg_len = 0;

	if (*dev_cfg_len)
		*dev_cfg_len += sizeof(struct snap_virtio_ctrl_section);

	*queue_cfg_len = ctrl->max_queues * sizeof(struct snap_virtio_ctrl_queue_state) +
			 sizeof(struct snap_virtio_ctrl_section);

	*common_cfg_len = sizeof(struct snap_virtio_ctrl_section) +
			  sizeof(struct snap_virtio_ctrl_common_state);

	snap_debug("common_cfg %d dev_cfg %d queue_cfg %d max_queue %d\n",
		   *common_cfg_len, *dev_cfg_len, *queue_cfg_len, (int)ctrl->max_queues);

	return sizeof(struct snap_virtio_ctrl_section) + *dev_cfg_len +
	       *queue_cfg_len + *common_cfg_len;
}

static __attribute__ ((unused)) void dump_state(struct snap_virtio_ctrl *ctrl,
						void *buf)
{
	struct snap_virtio_ctrl_section *hdr;
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
	snap_info(">>> ctrl_state: %d dev_ftr_sel: %d dev_ftrs: 0x%lx "
		  "drv_ftr_sel: %d drv_ftrs: 0x%lx msi_x: 0x%0x num_queues: %d "
		  "queue_select: %d status: 0x%0x config_gen: %d\n",
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
		snap_info(">>> size: %d msix: %d enable: %d notify_offset: %d "
			  "desc 0x%lx driver 0x%lx device 0x%lx hw_avail_idx: %d "
			  "hw_used_idx: %d\n",
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

/**
 * snap_virtio_ctrl_state_save() - Save virtio controllerr state
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
int snap_virtio_ctrl_state_save(struct snap_virtio_ctrl *ctrl, void *buf, unsigned len)
{
	unsigned dev_cfg_len, queue_cfg_len, common_cfg_len;
	int total_len;
	int i, ret;
	struct snap_virtio_ctrl_section *ghdr, *hdr;
	struct snap_virtio_ctrl_common_state *common_state;
	struct snap_virtio_ctrl_queue_state *queue_state;

	/* at the moment always allow saving state but limit restore */
	total_len = snap_virtio_ctrl_state_size(ctrl, &common_cfg_len, &queue_cfg_len,
						&dev_cfg_len);
	if (len < total_len)
		return -EINVAL;

	ghdr = buf;
	ghdr->len = total_len;
	snprintf(ghdr->name, sizeof(ghdr->name), "VIRTIO_CTRL_CFG");

	hdr = ghdr + 1;
	common_state = (struct snap_virtio_ctrl_common_state *)(hdr + 1);

	hdr->len = common_cfg_len;
	snprintf(hdr->name, sizeof(hdr->name), "COMMON_PCI_CFG");

	/* save common and device configs */
	common_state->ctrl_state = ctrl->state;

	common_state->device_feature_select = ctrl->bar_curr->device_feature_select;
	common_state->driver_feature_select = ctrl->bar_curr->driver_feature_select;
	common_state->queue_select = ctrl->bar_curr->queue_select;

	common_state->device_feature = ctrl->bar_curr->device_feature;
	common_state->driver_feature = ctrl->bar_curr->driver_feature;
	common_state->msix_config = ctrl->bar_curr->msix_config;
	common_state->num_queues = ctrl->bar_curr->max_queues;
	common_state->device_status = ctrl->bar_curr->status;
	common_state->config_generation = ctrl->bar_curr->config_generation;

	/* save queue state for every queue */
	hdr = (struct snap_virtio_ctrl_section *)(common_state + 1);
	hdr->len = queue_cfg_len;
	snprintf(hdr->name, sizeof(hdr->name), "QUEUES_CFG");
	queue_state = (struct snap_virtio_ctrl_queue_state *)(hdr + 1);

	snap_pgs_suspend(&ctrl->pg_ctx);
	for (i = 0; i < ctrl->max_queues; i++) {
		struct snap_virtio_queue_attr *vq;
		vq = to_virtio_queue_attr(ctrl, ctrl->bar_curr, i);
		queue_state[i].queue_size = vq->size;
		queue_state[i].queue_msix_vector = vq->msix_vector;
		queue_state[i].queue_enable = vq->enable;
		queue_state[i].queue_notify_off = vq->notify_off;
		queue_state[i].queue_desc = vq->desc;
		queue_state[i].queue_driver = vq->driver;
		queue_state[i].queue_device = vq->device;
		queue_state[i].hw_available_index = 0;
		queue_state[i].hw_used_index = 0;

		/* if enabled, call specific queue impl to get
		 * hw_avail and used */
		if (vq->enable && ctrl->q_ops->get_state) {
			ret = ctrl->q_ops->get_state(ctrl->queues[i], &queue_state[i]);
			if (ret) {
				snap_pgs_resume(&ctrl->pg_ctx);
				return -EINVAL;
			}
		}
	}
	snap_pgs_resume(&ctrl->pg_ctx);

	if (dev_cfg_len) {
		hdr = (struct snap_virtio_ctrl_section *)((char *)hdr + hdr->len);
		hdr->len = dev_cfg_len;
		snprintf(hdr->name, sizeof(hdr->name), "DEVICE_CFG");
		ret = ctrl->bar_ops->get_state(ctrl, ctrl->bar_curr, (void *)(hdr + 1), dev_cfg_len);
		if (ret < 0)
			return -EINVAL;
	}

	dump_state(ctrl, buf);
	return total_len;
}
