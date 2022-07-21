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

#include "snap_virtio_net_ctrl.h"
#include <linux/virtio_net.h>
#include <linux/virtio_config.h>


static struct snap_virtio_device_attr*
snap_virtio_net_ctrl_bar_create(struct snap_virtio_ctrl *vctrl)
{
	struct snap_virtio_net_device_attr *vnbar;

	vnbar = calloc(1, sizeof(*vnbar));
	if (!vnbar)
		goto err;

	/* Allocate queue attributes slots on bar */
	vnbar->queues = vctrl->max_queues;
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
	int ret, i;

	/* Query with max VQ buffer */
	ret = snap_virtio_net_query_device(vctrl->sdev, vnbar);
	if (ret)
		return ret;

	/* Update enabled queue count */
	vctrl->enabled_queues = 0;
	for (i = 0; i < vnbar->queues; ++i)
		if (vnbar->q_attrs[i].vattr.enable)
			vctrl->enabled_queues++;

	return 0;
}

static int snap_virtio_net_ctrl_bar_modify(struct snap_virtio_ctrl *vctrl,
					   uint64_t mask,
					   struct snap_virtio_device_attr *vbar)
{
	struct snap_virtio_net_device_attr *vbbar = to_net_device_attr(vbar);

	return snap_virtio_net_modify_device(vctrl->sdev, mask, vbbar);
}

static struct snap_virtio_queue_attr*
snap_virtio_net_ctrl_bar_get_queue_attr(struct snap_virtio_device_attr *vbar,
					int index)
{
	struct snap_virtio_net_device_attr *vnbar = to_net_device_attr(vbar);

	return &vnbar->q_attrs[index].vattr;
}

static size_t
snap_virtio_net_ctrl_bar_get_state_size(struct snap_virtio_ctrl *ctrl)
{
	struct snap_virtio_net_ctrl *nctrl = to_net_ctrl(ctrl);
	/* use net device config definition from linux/virtio_blk.h */
	size_t net_state_size = sizeof(struct virtio_net_config);
	size_t net_state_internal_size = nctrl->lm_cbs.get_internal_state_size(ctrl->cb_ctx);

	return net_state_size + net_state_internal_size;
}

static int
snap_virtio_net_ctrl_bar_get_state(struct snap_virtio_ctrl *ctrl,
				   struct snap_virtio_device_attr *vbar,
				   void *buf, size_t len)
{
	struct snap_virtio_net_device_attr *vnbar = to_net_device_attr(vbar);
	struct virtio_net_config *dev_cfg;
	int written_len = 0;
	struct snap_virtio_net_ctrl *nctrl = to_net_ctrl(ctrl);

	if (len < snap_virtio_net_ctrl_bar_get_state_size(ctrl))
		return -EINVAL;

	dev_cfg = buf;
	memcpy(&dev_cfg->mac[0], &vnbar->mac, 6);
	dev_cfg->status = vnbar->status;
	dev_cfg->max_virtqueue_pairs  = vnbar->max_queue_pairs;
	dev_cfg->mtu = vnbar->mtu;
	buf += sizeof(struct virtio_net_config);
	written_len += sizeof(struct virtio_net_config);

	written_len += nctrl->lm_cbs.get_internal_state(ctrl->cb_ctx, buf, len - sizeof(struct virtio_net_config));

	return written_len;
}

static void
snap_virtio_net_ctrl_bar_dump_state(struct snap_virtio_ctrl *ctrl,
				    const void *buf, int len)
{
	const struct virtio_net_config *dev_cfg;
	struct snap_virtio_net_ctrl *nctrl = to_net_ctrl(ctrl);

	if (len < snap_virtio_net_ctrl_bar_get_state_size(ctrl)) {
		snap_info(">>> net_config: state is truncated (%d < %lu)\n", len,
			  snap_virtio_net_ctrl_bar_get_state_size(ctrl));
		return;
	}
	dev_cfg = buf;
	snap_info(">>> mac: %02X:%02X:%02X:%02X:%02X:%02X status: 0x%x max_virtqueue_pairs: 0x%x mtu: 0x%x\n",
		  dev_cfg->mac[5], dev_cfg->mac[4], dev_cfg->mac[3], dev_cfg->mac[2], dev_cfg->mac[1], dev_cfg->mac[0],
		  dev_cfg->status, dev_cfg->max_virtqueue_pairs, dev_cfg->mtu);

	buf += sizeof(struct virtio_net_config);
	nctrl->lm_cbs.dump_internal_state(ctrl->cb_ctx, buf, len - sizeof(struct virtio_net_config));
}

static int
snap_virtio_net_ctrl_bar_set_state(struct snap_virtio_ctrl *ctrl,
				   struct snap_virtio_device_attr *vbar,
				   const struct snap_virtio_ctrl_queue_state *queue_state,
				   const void *buf, int len)
{
	struct snap_virtio_net_device_attr *vnbar = to_net_device_attr(vbar);
	const struct virtio_net_config *dev_cfg;
	struct snap_virtio_net_ctrl *nctrl = to_net_ctrl(ctrl);
	int i, ret;

	if (!buf)
		return -EINVAL;

	if (len < snap_virtio_net_ctrl_bar_get_state_size(ctrl))
		return -EINVAL;

	if (!queue_state)
		return -EINVAL;

	for (i = 0; i < ctrl->max_queues; i++) {
		vnbar->q_attrs[i].hw_available_index = queue_state[i].hw_available_index;
		vnbar->q_attrs[i].hw_used_index = queue_state[i].hw_used_index;
		snap_info("[%s %d]dev %s q 0x%x , restore avl ix:0x%x, used ix:0x%x\n",
			  __func__, __LINE__, ctrl->sdev->pci->pci_number, i,
			  queue_state[i].hw_available_index, queue_state[i].hw_used_index);
	}

	dev_cfg = buf;
	memcpy(&vnbar->mac, &dev_cfg->mac[0], 6);
	vnbar->status = dev_cfg->status;
	vnbar->max_queue_pairs = dev_cfg->max_virtqueue_pairs;
	vnbar->queues = ctrl->max_queues;
	vnbar->mtu = dev_cfg->mtu;

	ret = snap_virtio_net_modify_device(ctrl->sdev,
					    SNAP_VIRTIO_MOD_ALL |
					    SNAP_VIRTIO_MOD_QUEUE_CFG,
					    vnbar);
	if (ret)
		snap_error("Failed to restore virtio net device config\n");

	//restore internal state
	nctrl->lm_cbs.set_internal_state(ctrl->cb_ctx, buf + sizeof(struct virtio_net_config),
					len - sizeof(struct virtio_net_config));

	return ret;
}

static struct snap_virtio_ctrl_bar_ops snap_virtio_net_ctrl_bar_ops = {
	.create = snap_virtio_net_ctrl_bar_create,
	.destroy = snap_virtio_net_ctrl_bar_destroy,
	.copy = snap_virtio_net_ctrl_bar_copy,
	.update = snap_virtio_net_ctrl_bar_update,
	.modify = snap_virtio_net_ctrl_bar_modify,
	.get_queue_attr = snap_virtio_net_ctrl_bar_get_queue_attr,

	.get_state_size = snap_virtio_net_ctrl_bar_get_state_size,
	.dump_state = snap_virtio_net_ctrl_bar_dump_state,
	.get_state = snap_virtio_net_ctrl_bar_get_state,
	.set_state = snap_virtio_net_ctrl_bar_set_state,
};

static struct snap_virtio_ctrl_queue*
snap_virtio_net_ctrl_queue_create(struct snap_virtio_ctrl *vctrl, int index)
{
	struct snap_virtio_net_ctrl_queue *vnq;

	vnq = calloc(1, sizeof(*vnq));
	if (!vnq)
		return NULL;

	vnq->attr = &to_net_device_attr(vctrl->bar_curr)->q_attrs[index];
	return &vnq->common;
}

static void snap_virtio_net_ctrl_queue_destroy(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_virtio_net_ctrl_queue *vnq = container_of(vq,
					struct snap_virtio_net_ctrl_queue, common);
	struct snap_virtio_net_queue_attr *vnqa;

	vnqa = (struct snap_virtio_net_queue_attr *)vnq->attr;
	vnqa->hw_available_index = 0;
	vnqa->hw_used_index      = 0;

	free(vnq);
}

static int snap_virtio_net_ctrl_queue_progress(struct snap_virtio_ctrl_queue *vq)
{
	return 0;
}

static int snap_virtio_net_ctrl_queue_get_state(struct snap_virtio_ctrl_queue *vq,
						struct snap_virtio_ctrl_queue_state *state)
{
	struct snap_device                *sdev  = vq->ctrl->sdev;
	struct snap_virtio_net_device     *ndev = (struct snap_virtio_net_device *)sdev->dd_data;
	struct snap_virtio_net_queue      *vnq  = &ndev->virtqs[vq->index];
	struct snap_virtio_net_queue_attr attr = {};
	int ret;

	if (!vnq->virtq.virtq) {
		snap_error("virtq obj not created yet for q %d\n", vq->index);
		return -1;
	}

	ret = snap_virtio_net_query_queue(vnq, &attr);
	if (ret < 0) {
		snap_error("failed to query net queue %d\n", vq->index);
		return ret;
	}
	state->hw_available_index = attr.hw_available_index;
	state->hw_used_index = attr.hw_used_index;

	return 0;
}

static void snap_virtio_net_ctrl_queue_suspend(struct snap_virtio_ctrl_queue *vq)
{
	snap_debug("queue %d: suspend\n", vq->index);
}

static int snap_virtio_net_ctrl_queue_resume(struct snap_virtio_ctrl_queue *vq)
{
	int index = vq->index;
	struct snap_virtio_net_device_attr *dev_attr;
	struct snap_virtio_ctrl *ctrl;

	ctrl = vq->ctrl;
	dev_attr = to_net_device_attr(ctrl->bar_curr);

	snap_info("queue %d: pg_id %d RESUMED with hw_avail %u hw_used %u\n",
		  vq->index, vq->pg->id,
		  dev_attr->q_attrs[index].hw_available_index,
		  dev_attr->q_attrs[index].hw_used_index);

	return 0;
}

static bool snap_virtio_net_ctrl_queue_is_suspended(struct snap_virtio_ctrl_queue *vq)
{
	return true;
}

static bool snap_virtio_net_ctrl_queue_reset_check(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_virtio_net_device_attr *dev_attr;
	struct snap_virtio_ctrl *ctrl;
	int index = vq->index;

	ctrl = vq->ctrl;
	dev_attr = to_net_device_attr(ctrl->bar_curr);

	if (dev_attr->q_attrs[index].vattr.enable &&
	    dev_attr->q_attrs[index].vattr.reset) {
		dev_attr->q_attrs[index].vattr.reset = 0;
		return true;
	}

	return false;
}

static bool
snap_virtio_net_ctrl_queue_enable_check(struct snap_virtio_ctrl *ctrl, int index)
{
	struct snap_virtio_net_device_attr *prev_attr, *curr_attr;

	prev_attr = to_net_device_attr(ctrl->bar_prev);
	curr_attr = to_net_device_attr(ctrl->bar_curr);

	return !prev_attr->q_attrs[index].vattr.enable &&
		curr_attr->q_attrs[index].vattr.enable;
}

static struct snap_virtio_queue_ops snap_virtio_net_queue_ops = {
	.create = snap_virtio_net_ctrl_queue_create,
	.destroy = snap_virtio_net_ctrl_queue_destroy,
	.progress = snap_virtio_net_ctrl_queue_progress,
	.suspend = snap_virtio_net_ctrl_queue_suspend,
	.is_suspended = snap_virtio_net_ctrl_queue_is_suspended,
	.resume = snap_virtio_net_ctrl_queue_resume,
	.get_state = snap_virtio_net_ctrl_queue_get_state,
	.reset_check = snap_virtio_net_ctrl_queue_reset_check,
	.enable_check = snap_virtio_net_ctrl_queue_enable_check,
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
				    &snap_virtio_net_queue_ops,
				    sctx, &attr->common);
	if (ret) {
		errno = ENODEV;
		goto free_ctrl;
	}

	ctrl->lm_cbs = *attr->lm_cbs;
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
	snap_virtio_ctrl_stop(&ctrl->common);
	if (!ctrl->common.pending_flr)
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

/**
 * snap_virtio_net_ctrl_io_progress() - Handles IO requests from host
 * @ctrl:       controller instance
 *
 * Looks for any IO requests from host received on QPs, and handles
 * them based on the request's parameters.
 */
void snap_virtio_net_ctrl_io_progress(struct snap_virtio_net_ctrl *ctrl)
{
	snap_virtio_ctrl_io_progress(&ctrl->common);
}
