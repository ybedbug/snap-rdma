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

#include "snap_virtio_blk_ctrl.h"
#include "snap_virtio_blk_virtq.h"
#include <linux/virtio_blk.h>
#include <linux/virtio_config.h>
#include <sys/mman.h>
#include "snap_vq_adm.h"
#include "snap_buf.h"

#define SNAP_VIRTIO_BLK_MODIFIABLE_FTRS ((1ULL << VIRTIO_F_VERSION_1) |\
					 (1ULL << VIRTIO_BLK_F_MQ) |\
					 (1ULL << VIRTIO_BLK_F_SIZE_MAX) |\
					 (1ULL << VIRTIO_BLK_F_SEG_MAX) |\
					 (1ULL << VIRTIO_BLK_F_BLK_SIZE)|\
					 (1ULL << VIRTIO_F_ADMIN_VQ)|\
					 (1ULL << VIRTIO_F_ADMIN_MIGRATION))

static inline struct snap_virtio_blk_ctrl_queue*
to_blk_ctrl_q(struct snap_virtio_ctrl_queue *vq)
{
	return container_of(vq, struct snap_virtio_blk_ctrl_queue, common);
}

static inline struct snap_virtio_blk_ctrl*
to_blk_ctrl(struct snap_virtio_ctrl *vctrl)
{
	return container_of(vctrl, struct snap_virtio_blk_ctrl, common);
}

static struct snap_virtio_device_attr*
snap_virtio_blk_ctrl_bar_create(struct snap_virtio_ctrl *vctrl)
{
	struct snap_virtio_blk_device_attr *vbbar;

	vbbar = calloc(1, sizeof(*vbbar));
	if (!vbbar)
		goto err;

	/* Allocate queue attributes slots on bar */
	vbbar->queues = vctrl->max_queues;
	vbbar->q_attrs = calloc(vbbar->queues, sizeof(*vbbar->q_attrs));
	if (!vbbar->q_attrs)
		goto free_vbbar;

	return &vbbar->vattr;

free_vbbar:
	free(vbbar);
err:
	return NULL;
}

static void snap_virtio_blk_ctrl_bar_destroy(struct snap_virtio_device_attr *vbar)
{
	struct snap_virtio_blk_device_attr *vbbar = to_blk_device_attr(vbar);

	free(vbbar->q_attrs);
	free(vbbar);
}

static void snap_virtio_blk_ctrl_bar_copy(struct snap_virtio_device_attr *vorig,
					  struct snap_virtio_device_attr *vcopy)
{
	struct snap_virtio_blk_device_attr *vborig = to_blk_device_attr(vorig);
	struct snap_virtio_blk_device_attr *vbcopy = to_blk_device_attr(vcopy);

	memcpy(vbcopy->q_attrs, vborig->q_attrs,
	       vbcopy->queues * sizeof(*vbcopy->q_attrs));
}

static int snap_virtio_blk_ctrl_bar_update(struct snap_virtio_ctrl *vctrl,
					   struct snap_virtio_device_attr *vbar)
{
	struct snap_virtio_blk_device_attr *vbbar = to_blk_device_attr(vbar);

	return snap_virtio_blk_query_device(vctrl->sdev, vbbar);
}

static int snap_virtio_blk_ctrl_bar_modify(struct snap_virtio_ctrl *vctrl,
					   uint64_t mask,
					   struct snap_virtio_device_attr *vbar)
{
	struct snap_virtio_blk_device_attr *vbbar = to_blk_device_attr(vbar);

	return snap_virtio_blk_modify_device(vctrl->sdev, mask, vbbar);
}

 // Return: Returns 0 in case of success and attr is filled.
static int snap_virtio_blk_ctrl_bar_get_attr(struct snap_virtio_ctrl *vctrl,
					     struct snap_virtio_device_attr *vbar)
{
	int rc;
	struct snap_virtio_blk_device_attr blk_attr = {};

	rc = snap_virtio_blk_query_device(vctrl->sdev, &blk_attr);
	if (!rc)
		memcpy(vbar, &blk_attr.vattr, sizeof(blk_attr.vattr));
	else
		snap_error("Failed to query bar\n");

	return rc;
}

static struct snap_virtio_queue_attr*
snap_virtio_blk_ctrl_bar_get_queue_attr(struct snap_virtio_device_attr *vbar,
					int index)
{
	struct snap_virtio_blk_device_attr *vbbar = to_blk_device_attr(vbar);

	return &vbbar->q_attrs[index].vattr;
}

static size_t
snap_virtio_blk_ctrl_bar_get_state_size(struct snap_virtio_ctrl *ctrl)
{
	/* use block device config definition from linux/virtio_blk.h */
	return sizeof(struct virtio_blk_config);
}

static void
snap_virtio_blk_ctrl_bar_dump_state(struct snap_virtio_ctrl *ctrl, const void *buf, int len)
{
	const struct virtio_blk_config *dev_cfg;

	if (len < snap_virtio_blk_ctrl_bar_get_state_size(ctrl)) {
		snap_info(">>> blk_config: state is truncated (%d < %lu)\n", len,
			  snap_virtio_blk_ctrl_bar_get_state_size(ctrl));
		return;
	}

	dev_cfg = buf;
	snap_info(">>> capacity: %llu size_max: %u seg_max: %u blk_size: %u num_queues: %u\n",
		  dev_cfg->capacity, dev_cfg->size_max, dev_cfg->seg_max,
		  dev_cfg->blk_size, dev_cfg->num_queues);
}

static int
snap_virtio_blk_ctrl_bar_get_state(struct snap_virtio_ctrl *ctrl,
				   struct snap_virtio_device_attr *vbar,
				   void *buf, size_t len)
{
	struct snap_virtio_blk_device_attr *vbbar = to_blk_device_attr(vbar);
	struct virtio_blk_config *dev_cfg;

	if (len < snap_virtio_blk_ctrl_bar_get_state_size(ctrl))
		return -EINVAL;

	dev_cfg = buf;
	dev_cfg->capacity = vbbar->capacity;
	dev_cfg->size_max = vbbar->size_max;
	dev_cfg->seg_max = vbbar->seg_max;
	dev_cfg->blk_size = vbbar->blk_size;
	dev_cfg->num_queues = vbbar->max_blk_queues;
	return snap_virtio_blk_ctrl_bar_get_state_size(ctrl);
}

static int
snap_virtio_blk_ctrl_bar_set_state(struct snap_virtio_ctrl *ctrl,
				   struct snap_virtio_device_attr *vbar,
				   const struct snap_virtio_ctrl_queue_state *queue_state,
				   const void *buf, int len)
{
	struct snap_virtio_blk_device_attr *vbbar = to_blk_device_attr(vbar);
	const struct virtio_blk_config *dev_cfg;
	int i, ret;

	if (!buf)
		return -EINVAL;

	if (len < snap_virtio_blk_ctrl_bar_get_state_size(ctrl))
		return -EINVAL;

	if (!queue_state)
		return -EINVAL;

	for (i = 0; i < ctrl->max_queues; i++) {
		vbbar->q_attrs[i].hw_available_index = queue_state[i].hw_available_index;
		vbbar->q_attrs[i].hw_used_index = queue_state[i].hw_used_index;
	}

	dev_cfg = buf;
	vbbar->capacity = dev_cfg->capacity;
	vbbar->size_max = dev_cfg->size_max;
	vbbar->seg_max = dev_cfg->seg_max;
	vbbar->blk_size = dev_cfg->blk_size;
	vbbar->max_blk_queues = dev_cfg->num_queues;

	ret = snap_virtio_blk_modify_device(ctrl->sdev,
					    SNAP_VIRTIO_MOD_ALL |
					    SNAP_VIRTIO_MOD_QUEUE_CFG,
					    vbbar);
	if (ret)
		snap_error("Failed to restore virtio blk device config\n");

	return ret;
}

static bool
snap_virtio_blk_ctrl_bar_queue_attr_valid(struct snap_virtio_device_attr *vbar)
{
	struct snap_virtio_blk_device_attr *vbbar = to_blk_device_attr(vbar);

	return vbbar->q_attrs ? true : false;
}

static struct snap_virtio_ctrl_bar_ops snap_virtio_blk_ctrl_bar_ops = {
	.create = snap_virtio_blk_ctrl_bar_create,
	.destroy = snap_virtio_blk_ctrl_bar_destroy,
	.copy = snap_virtio_blk_ctrl_bar_copy,
	.update = snap_virtio_blk_ctrl_bar_update,
	.modify = snap_virtio_blk_ctrl_bar_modify,
	.get_queue_attr = snap_virtio_blk_ctrl_bar_get_queue_attr,
	.get_state_size = snap_virtio_blk_ctrl_bar_get_state_size,
	.dump_state = snap_virtio_blk_ctrl_bar_dump_state,
	.get_state = snap_virtio_blk_ctrl_bar_get_state,
	.set_state = snap_virtio_blk_ctrl_bar_set_state,
	.queue_attr_valid = snap_virtio_blk_ctrl_bar_queue_attr_valid,
	.get_attr = snap_virtio_blk_ctrl_bar_get_attr
};

static bool
snap_virtio_blk_ctrl_bar_is_setup_valid(const struct snap_virtio_blk_device_attr *bar,
					const struct snap_virtio_blk_registers *regs,
					bool recover)
{
	bool ret = true;

	/* virtio_common_pci_config registers */
	if ((regs->device_features ^ bar->vattr.device_feature) &
	    SNAP_VIRTIO_BLK_MODIFIABLE_FTRS) {
		snap_error("Can't modify device_features, host driver is up - conf.device_features: 0x%lx bar.device_features: 0x%lx\n",
			   regs->device_features, bar->vattr.device_feature);
		ret = false;
	}

	if (regs->max_queues &&
	    regs->max_queues != bar->vattr.max_queues) {
		snap_error("Can't modify max_queues, host driver is up - conf.queues: %d bar.queues: %d\n",
			   regs->max_queues, bar->vattr.max_queues);
		ret = false;
	}

	if (regs->queue_size &&
	    regs->queue_size != bar->vattr.max_queue_size) {
		snap_error("Can't modify queue_size host driver is up - conf.queue_size: %d bar.queue_size: %d\n",
			   regs->queue_size, bar->vattr.max_queue_size);
		ret = false;
	}

	/* virtio_blk_config registers */
	if (recover && regs->capacity != bar->capacity) {
		snap_error("Can't change capacity, host driver is up - conf.capacity: %ld bar.capacity: %ld\n",
			   regs->capacity, bar->capacity);
		ret = false;
	}

	if (regs->blk_size && regs->blk_size != bar->blk_size) {
		snap_error("Can't modify blk_size, host driver is up - conf.blk_size: %d bar.blk_size: %d\n",
			   regs->blk_size, bar->blk_size);
		ret = false;
	}

	if (regs->size_max && regs->size_max != bar->size_max) {
		snap_error("Can't modify size_max, host driver is up - conf.size_max: %d bar.size_max: %d\n",
			   regs->size_max, bar->size_max);
		ret = false;
	}

	if (regs->seg_max && regs->seg_max != bar->seg_max) {
		snap_error("Can't modify seg_max, host driver is up - conf.seg_max: %d bar.seg_max: %d\n",
			   regs->seg_max, bar->seg_max);
		ret = false;
	}

	return ret;
}

static bool
snap_virtio_blk_ctrl_bar_setup_valid(struct snap_virtio_blk_ctrl *ctrl,
				     const struct snap_virtio_blk_device_attr *bar,
				     const struct snap_virtio_blk_registers *regs)
{
	struct snap_virtio_blk_registers regs_whitelist = {};

	/* If only capacity is asked to be changed, allow it */
	regs_whitelist.capacity = regs->capacity;
	/* snap_virtio_blk_ctrl_bar_setup changes max queues 0 to default */
	regs_whitelist.max_queues = bar->max_blk_queues;
	if (!memcmp(regs, &regs_whitelist, sizeof(regs_whitelist)))
		return true;

	if (regs->max_queues > ctrl->common.max_queues) {
		snap_error("Cannot create %d queues (max %lu)\n", regs->max_queues,
			   ctrl->common.max_queues);
		return false;
	}

	/* Everything is configurable as long as driver is still down */
	if (snap_virtio_ctrl_is_configurable(&ctrl->common))
		return true;

	return snap_virtio_blk_ctrl_bar_is_setup_valid(bar, regs, false);
}

/**
 * snap_virtio_blk_ctrl_bar_setup() - Setup PCI BAR virtio registers
 * @ctrl:       controller instance
 * @regs:	registers struct to modify
 *
 * Update all configurable PCI BAR virtio register values, when possible.
 * Value of `0` means value is not to be updated (old value is kept).
 */
int snap_virtio_blk_ctrl_bar_setup(struct snap_virtio_blk_ctrl *ctrl,
				   struct snap_virtio_blk_registers *regs,
				   uint16_t regs_mask)
{
	struct snap_virtio_blk_device_attr bar = {};
	uint16_t extra_flags = 0;
	int ret;
	uint64_t new_ftrs;

	/* Get last bar values as a reference */
	ret = snap_virtio_blk_query_device(ctrl->common.sdev, &bar);
	if (ret) {
		snap_error("Failed to query bar\n");
		return -EINVAL;
	}

	if (regs->max_queues > 0) {
		/*
		 * snap_virtio_blk_ctrl_bar_setup_valid() will make sure later
		 * that max_queues is either configurable, or same as in PCI bar.
		 * That means we only left to verify it doesn't break capacity
		 */
		if (snap_unlikely(regs->max_queues > ctrl->common.max_queues)) {
			snap_error("Too many queues were requested (max allowed %lu)\n",
				ctrl->common.max_queues);
			return -EINVAL;
		}
	} else {
		if (snap_virtio_ctrl_is_configurable(&ctrl->common)) {
			/*
			 * Default configuration is optimized to kernel driver case,
			 * which assumes num_queues+1 <= num_msix equation for
			 * best performance
			 */
			regs->max_queues = snap_min(ctrl->common.max_queues,
						bar.vattr.num_msix - 1);
		} else
			regs->max_queues = bar.vattr.max_queues;
	}

	/* If we set admin queue feature make sure we create at least 1 io queue as well */
	if (regs->device_features & 1ULL << VIRTIO_F_ADMIN_VQ && regs->max_queues == 1)
		regs->max_queues++;

	if (!snap_virtio_blk_ctrl_bar_setup_valid(ctrl, &bar, regs)) {
		snap_error("Setup is not valid\n");
		return -EINVAL;
	}

	if (regs_mask & SNAP_VIRTIO_MOD_PCI_COMMON_CFG) {
		/* Update only the device_feature modifiable bits */
		new_ftrs = regs->device_features ? : bar.vattr.device_feature;
		bar.vattr.device_feature = (bar.vattr.device_feature &
					    ~SNAP_VIRTIO_BLK_MODIFIABLE_FTRS);
		bar.vattr.device_feature |= (new_ftrs &
					     SNAP_VIRTIO_BLK_MODIFIABLE_FTRS);
		bar.vattr.max_queue_size = regs->queue_size ? :
					   bar.vattr.max_queue_size;
		bar.vattr.max_queues = regs->max_queues;
		/*
		 * We always wish to keep blk queues and
		 * virtio queues values aligned
		 */
		extra_flags |= SNAP_VIRTIO_MOD_DEV_CFG;
		bar.max_blk_queues = regs->max_queues;
	}

	if (regs_mask & SNAP_VIRTIO_MOD_DEV_CFG) {
		/*
		 * We must be able to set capacity to 0.
		 * This means we cannot change DEV_CFG without
		 * updating capacity (unless its of same size)
		 */
		bar.capacity = regs->capacity;
		bar.blk_size = regs->blk_size ? : bar.blk_size;
		bar.size_max = regs->size_max ? : bar.size_max;
		if (regs->seg_max > bar.vattr.max_queue_size - 2) {
			regs->seg_max = bar.vattr.max_queue_size - 2;
			snap_warn("Seg_max cannot be larger than queue depth - 2. Changed seg_max to %d.\n", regs->seg_max);
		}
		bar.seg_max = regs->seg_max ? : bar.seg_max;
	}

	ret = snap_virtio_blk_modify_device(ctrl->common.sdev,
					    regs_mask | extra_flags, &bar);
	if (ret) {
		snap_error("Failed to config virtio controller\n");
		return ret;
	}

	return ret;
}

static int
snap_virtio_blk_ctrl_queue_get_debugstat(struct snap_virtio_ctrl_queue *vq,
			struct snap_virtio_queue_debugstat *q_debugstat)
{
	struct snap_virtio_blk_ctrl_queue *vbq = to_blk_ctrl_q(vq);

	return blk_virtq_get_debugstat(vbq->q_impl, q_debugstat);
}

static int
snap_virtio_blk_ctrl_count_error(struct snap_virtio_blk_ctrl *ctrl)
{
	int i, ret;
	struct snap_virtio_ctrl_queue *vq;
	struct snap_virtio_blk_ctrl_queue *vbq;
	struct snap_virtio_common_queue_attr *attr;
	struct snap_virtio_queue_attr *vattr;

	for (i = ctrl->has_adm_vq; i < ctrl->common.max_queues; i++) {
		vq = ctrl->common.queues[i];
		if (!vq)
			continue;

		vbq = to_blk_ctrl_q(vq);
		if (vbq->in_error)
			continue;

		attr = (struct snap_virtio_common_queue_attr *)(void *)vbq->attr;
		ret = blk_virtq_query_error_state(vbq->q_impl, attr);
		if (ret) {
			snap_error("Failed to query queue error state\n");
			return ret;
		}

		vattr = &attr->vattr;
		if (vattr->state == SNAP_VIRTQ_STATE_ERR) {
			if (vattr->error_type == SNAP_VIRTQ_ERROR_TYPE_NETWORK_ERROR)
				ctrl->network_error++;
			else if (vattr->error_type == SNAP_VIRTQ_ERROR_TYPE_INTERNAL_ERROR)
				ctrl->internal_error++;

			vbq->in_error = true;
		}
	}

	return 0;
}

static int
snap_virtio_blk_ctrl_global_get_debugstat(struct snap_virtio_blk_ctrl *ctrl,
			struct snap_virtio_ctrl_debugstat *ctrl_debugstat)
{
	int i, ret;
	struct snap_virtio_blk_device *vbdev;
	struct snap_virtio_blk_queue *virtq;
	struct snap_virtio_queue_counters_attr vqc_attr = {};

	vbdev = ctrl->common.sdev->dd_data;
	for (i = ctrl->has_adm_vq; i < vbdev->num_queues; i++) {
		virtq = &vbdev->virtqs[i];

		ret = snap_virtio_query_queue_counters(virtq->virtq.ctrs_obj, &vqc_attr);
		if (ret) {
			snap_error("Failed to query virtio_q_counter obj\n");
			return ret;
		}

		ctrl_debugstat->bad_descriptor_error += vqc_attr.bad_desc_errors;
		ctrl_debugstat->invalid_buffer += vqc_attr.invalid_buffer;
		ctrl_debugstat->desc_list_exceed_limit += vqc_attr.exceed_max_chain;
	}

	ret = snap_virtio_blk_ctrl_count_error(ctrl);
	if (ret) {
		snap_error("Failed to count queue error stats\n");
		return ret;
	}

	ctrl_debugstat->network_error = ctrl->network_error;
	ctrl_debugstat->internal_error = ctrl->internal_error;

	return 0;
}

/**
 * snap_virtio_blk_ctrl_get_debugstat() - Get debug statistics for
 *  virtio-blk controller
 *
 * @ctrl:       virtio-blk controller
 * @attr:       debug statistics attributes (output)
 *
 * The function queries for HW/FW/SW debug statistics attributes.
 * The function is intended to be used for debug purposes only, and
 * should NOT be called repeatedly, as it might damage performance.
 *
 * Return: 0 on success, errno on failure.
 */
int snap_virtio_blk_ctrl_get_debugstat(struct snap_virtio_blk_ctrl *ctrl,
			struct snap_virtio_ctrl_debugstat *ctrl_debugstat)
{
	int i;
	int enabled_queues = 0;
	int ret = 0;

	if (ctrl->common.state != SNAP_VIRTIO_CTRL_STARTED)
		goto out;

	ret = snap_virtio_blk_ctrl_global_get_debugstat(ctrl, ctrl_debugstat);
	if (ret)
		goto out;

	for (i = ctrl->has_adm_vq; i < ctrl->common.max_queues; i++) {
		struct snap_virtio_ctrl_queue *vq = ctrl->common.queues[i];

		if (!vq)
			continue;

		ret = snap_virtio_blk_ctrl_queue_get_debugstat(vq,
				&ctrl_debugstat->queues[enabled_queues]);
		if (ret)
			goto out;
		enabled_queues++;
	}
	ctrl_debugstat->num_queues = enabled_queues;

out:
	return ret;
}

static struct snap_virtio_ctrl*
snap_virtio_blk_ctrl_get_vf(struct snap_virtio_ctrl *vctrl, struct snap_vq_cmd *cmd)
{
	struct snap_virtio_blk_ctrl *pf_ctrl = to_blk_ctrl(vctrl);
	struct snap_virtio_blk_ctrl *vf_ctrl;
	int vdev_id;

	/* vdev_id as given in cmd_in starts count with 1 */
	vdev_id = snap_vaq_cmd_layout_get(cmd)->in.vdev_id - 1;
	if (vdev_id < 0)
		return NULL;

	if (pf_ctrl->common.sdev->pci->num_vfs > vdev_id && pf_ctrl->vfs_ctrl) {
		vf_ctrl =  pf_ctrl->vfs_ctrl[vdev_id];
		if (vf_ctrl) {
			snap_debug("Got adm cmd to run on VF %s\n",
				   vf_ctrl->common.sdev->pci->pci_number);
			return &vf_ctrl->common;
		}
	}
	return NULL;
}

static void snap_virtio_blk_ctrl_lm_status_get(struct snap_virtio_ctrl *vctrl,
						struct snap_vq_cmd *cmd)
{
	struct snap_virtio_ctrl *ctrl;
	struct snap_vq_adm_get_status_result *res;

	res = &snap_vaq_cmd_layout_get(cmd)->out.get_status_res;
	ctrl = snap_virtio_blk_ctrl_get_vf(vctrl, cmd);
	if (!ctrl) {
		snap_vaq_cmd_complete(cmd, SNAP_VIRTIO_ADM_STATUS_DEVICE_INTERNAL_ERR);
		return;
	}

	res->internal_status = snap_virtio_ctrl_get_lm_state(ctrl);
	snap_vaq_cmd_complete(cmd, SNAP_VIRTIO_ADM_STATUS_OK);
}

static void snap_virtio_blk_ctrl_lm_status_modify(struct snap_virtio_ctrl *vctrl, struct snap_vq_cmd *cmd)
{
	struct snap_vq_adm_modify_status_data data;
	enum snap_virtio_ctrl_lm_state cur_status, new_status;
	struct snap_virtio_ctrl *ctrl;
	int ret = -1;

	data = snap_vaq_cmd_layout_get(cmd)->in.modify_status_data;
	new_status = data.internal_status;
	ctrl = snap_virtio_blk_ctrl_get_vf(vctrl, cmd);
	if (!ctrl) {
		snap_vaq_cmd_complete(cmd, SNAP_VIRTIO_ADM_STATUS_DEVICE_INTERNAL_ERR);
		return;
	}

	cur_status =  snap_virtio_ctrl_get_lm_state(ctrl);
	switch (new_status) {
	case SNAP_VIRTIO_CTRL_LM_QUIESCED:
		if (cur_status == SNAP_VIRTIO_CTRL_LM_FREEZED)
			ret = snap_virtio_ctrl_unfreeze(ctrl);
		else {
			/*
			 * snap_vaq_cmd_complete will be called from
			 * snap_virtio_ctrl_adm_quiesce_done after ctrl moved to
			 * SUSPENDED state, or from snap_virtio_ctrl_adm_quiesce.
			 */
			ctrl->quiesce_cmd = cmd;
			snap_virtio_ctrl_quiesce_adm(ctrl);
			return;
		}
		break;
	case SNAP_VIRTIO_CTRL_LM_FREEZED:
		ret = snap_virtio_ctrl_freeze(ctrl);
		break;
	case SNAP_VIRTIO_CTRL_LM_RUNNING:
		ret = snap_virtio_ctrl_unquiesce(ctrl);
	default:
		break;
	}

	if (ret)
		snap_vaq_cmd_complete(cmd, SNAP_VIRTIO_ADM_STATUS_DEVICE_INTERNAL_ERR);
	else
		snap_vaq_cmd_complete(cmd, SNAP_VIRTIO_ADM_STATUS_OK);
}

static void snap_virtio_blk_ctrl_lm_pending_bytes_get(struct snap_virtio_ctrl *vctrl,
					struct snap_vq_cmd *cmd)
{
	struct snap_virtio_ctrl *ctrl;
	struct snap_vq_adm_get_pending_bytes_result *res;

	res = &snap_vaq_cmd_layout_get(cmd)->out.pending_bytes_res;
	ctrl = snap_virtio_blk_ctrl_get_vf(vctrl, cmd);
	if (!ctrl) {
		snap_vaq_cmd_complete(cmd, SNAP_VIRTIO_ADM_STATUS_DEVICE_INTERNAL_ERR);
		return;
	}

	res->pending_bytes = snap_virtio_ctrl_get_state_size(ctrl);
	if (res->pending_bytes < 0)
		snap_vaq_cmd_complete(cmd, SNAP_VIRTIO_ADM_STATUS_DEVICE_INTERNAL_ERR);
	else
		snap_vaq_cmd_complete(cmd, SNAP_VIRTIO_ADM_STATUS_OK);
}

static void snap_virtio_blk_ctrl_lm_state_save_cb(struct snap_vq_cmd *vcmd,
				  enum ibv_wc_status status)
{
	struct snap_virtio_blk_ctrl *blk_ctrl;

	if (snap_unlikely(status != IBV_WC_SUCCESS))
		return snap_vaq_cmd_complete(vcmd, SNAP_VIRTIO_ADM_STATUS_ERR);

	blk_ctrl = to_blk_ctrl(snap_vaq_cmd_ctrl_get(vcmd));
	if (blk_ctrl->lm_buf) {
		snap_buf_free(blk_ctrl->lm_buf);
		blk_ctrl->lm_buf = NULL;
	}

	snap_vaq_cmd_complete(vcmd, SNAP_VIRTIO_ADM_STATUS_OK);
}

static void snap_virtio_blk_ctrl_lm_state_restore_cb(struct snap_vq_cmd *vcmd,
						  enum ibv_wc_status status)
{
	struct snap_virtio_ctrl *vf_ctrl;
	struct snap_virtio_blk_ctrl *blk_ctrl;
	struct snap_vq_adm_restore_state_data data;
	int ret;

	if (status != IBV_WC_SUCCESS) {
		snap_vaq_cmd_complete(vcmd, SNAP_VIRTIO_ADM_STATUS_DATA_TRANSFER_ERR);
		return;
	}

	vf_ctrl = snap_virtio_blk_ctrl_get_vf(snap_vaq_cmd_ctrl_get(vcmd), vcmd);
	if (!vf_ctrl) {
		snap_vaq_cmd_complete(vcmd, SNAP_VIRTIO_ADM_STATUS_DEVICE_INTERNAL_ERR);
		return;
	}

	data = snap_vaq_cmd_layout_get(vcmd)->in.restore_state_data;
	blk_ctrl = to_blk_ctrl(snap_vaq_cmd_ctrl_get(vcmd));
	ret = snap_virtio_ctrl_state_restore(vf_ctrl, blk_ctrl->lm_buf, data.length);
	if (ret >= 0)
		snap_vaq_cmd_complete(vcmd, SNAP_VIRTIO_ADM_STATUS_OK);
	else
		snap_vaq_cmd_complete(vcmd, SNAP_VIRTIO_ADM_STATUS_DEVICE_INTERNAL_ERR);

	if (blk_ctrl->lm_buf) {
		snap_buf_free(blk_ctrl->lm_buf);
		blk_ctrl->lm_buf = NULL;
	}
}


static void snap_virtio_blk_ctrl_lm_state_save(struct snap_virtio_ctrl *vctrl,
						struct snap_vq_cmd *cmd)
{
	struct snap_virtio_ctrl *vf_vctrl;
	struct snap_virtio_blk_ctrl *blk_ctrl;
	struct snap_vq_adm_save_state_data data;
	int ret;

	vf_vctrl = snap_virtio_blk_ctrl_get_vf(vctrl, cmd);
	if (!vf_vctrl) {
		snap_vaq_cmd_complete(cmd, SNAP_VIRTIO_ADM_STATUS_DEVICE_INTERNAL_ERR);
		return;
	}

	data = snap_vaq_cmd_layout_get(cmd)->in.save_state_data;
	blk_ctrl = to_blk_ctrl(vctrl);
	blk_ctrl->lm_buf = snap_buf_alloc(vctrl->lb_pd, data.length * sizeof(uint8_t));
	if (!blk_ctrl->lm_buf) {
		snap_error("Failed allocating data buf for save internal state.\n");
		snap_vaq_cmd_complete(cmd, SNAP_VIRTIO_ADM_STATUS_DEVICE_INTERNAL_ERR);
	}
	ret = snap_virtio_ctrl_state_save(vf_vctrl, blk_ctrl->lm_buf, data.length);
	if (ret < 0) {
		snap_vaq_cmd_complete(cmd, SNAP_VIRTIO_ADM_STATUS_DEVICE_INTERNAL_ERR);
		return;
	}
	ret = snap_vaq_cmd_layout_data_write(cmd, data.length, blk_ctrl->lm_buf,
				  snap_buf_get_mkey(blk_ctrl->lm_buf),
				snap_virtio_blk_ctrl_lm_state_save_cb);
	if (ret)
		snap_vaq_cmd_complete(cmd, SNAP_VIRTIO_ADM_STATUS_ERR);

}

static void snap_virtio_blk_ctrl_lm_state_restore(struct snap_virtio_ctrl *vctrl,
						struct snap_vq_cmd *cmd)
{
	struct snap_vq_adm_restore_state_data data;
	struct snap_virtio_blk_ctrl *blk_ctrl;
	size_t offset;
	int ret;

	data = snap_vaq_cmd_layout_get(cmd)->in.restore_state_data;
	blk_ctrl = to_blk_ctrl(vctrl);

	blk_ctrl->lm_buf = snap_buf_alloc(vctrl->lb_pd, data.length * sizeof(uint8_t));
	if (!blk_ctrl->lm_buf) {
		snap_error("Failed allocating data buf for restore internal state.\n");
		snap_vaq_cmd_complete(cmd, SNAP_VIRTIO_ADM_STATUS_DEVICE_INTERNAL_ERR);
	}

	offset = sizeof(struct snap_virtio_adm_cmd_hdr) +
		sizeof(struct snap_vq_adm_restore_state_data);

	ret = snap_vaq_cmd_layout_data_read(cmd, data.length, blk_ctrl->lm_buf,
			snap_buf_get_mkey(blk_ctrl->lm_buf),
			snap_virtio_blk_ctrl_lm_state_restore_cb, offset);
	if (ret)
		snap_vaq_cmd_complete(cmd, SNAP_VIRTIO_ADM_STATUS_ERR);
}

static void snap_virtio_blk_adm_cmd_process(struct snap_virtio_ctrl *vctrl,
					struct snap_vq_cmd *cmd)
{
	struct snap_virtio_adm_cmd_hdr hdr = snap_vaq_cmd_layout_get(cmd)->hdr;

	snap_debug("Proceessing adm cmd class %d cmd %d\n", hdr.cmd_class, hdr.command);
	switch (hdr.cmd_class) {
	case SNAP_VQ_ADM_MIG_CTRL:
		switch (hdr.command) {
		case SNAP_VQ_ADM_MIG_GET_STATUS:
			snap_virtio_blk_ctrl_lm_status_get(vctrl, cmd);
			break;
		case SNAP_VQ_ADM_MIG_MODIFY_STATUS:
			snap_virtio_blk_ctrl_lm_status_modify(vctrl, cmd);
			break;
		case SNAP_VQ_ADM_MIG_GET_STATE_PENDING_BYTES:
			snap_virtio_blk_ctrl_lm_pending_bytes_get(vctrl, cmd);
			break;
		case SNAP_VQ_ADM_MIG_SAVE_STATE:
			snap_virtio_blk_ctrl_lm_state_save(vctrl, cmd);
			break;
		case SNAP_VQ_ADM_MIG_RESTORE_STATE:
			snap_virtio_blk_ctrl_lm_state_restore(vctrl, cmd);
			break;
		default:
			snap_error("Invalid admin commamd %d for class %d\n", hdr.command, hdr.cmd_class);
			snap_vaq_cmd_complete(cmd, SNAP_VIRTIO_ADM_STATUS_INVALID_COMMAND);
			break;
		}
		break;
	case SNAP_VQ_ADM_DP_TRACK_CTRL:
		switch (hdr.command) {
		case SNAP_VQ_ADM_DP_IDENTITY:
		case SNAP_VQ_ADM_DP_START_TRACK:
		case SNAP_VQ_ADM_DP_STOP_TRACK:
		case SNAP_VQ_ADM_DP_GET_MAP_PENDING_BYTES:
		case SNAP_VQ_ADM_DP_REPORT_MAP:
		default:
			snap_error("Invalid admin commamd %d for class %d\n", hdr.command, hdr.cmd_class);
			snap_vaq_cmd_complete(cmd, SNAP_VIRTIO_ADM_STATUS_INVALID_COMMAND);
			break;
		}
		break;
	default:
		snap_error("Invalid admin cmd class %d\n", hdr.cmd_class);
		snap_vaq_cmd_complete(cmd, SNAP_VIRTIO_ADM_STATUS_INVALID_CLASS);
		break;
	}
}

static int blk_adm_virtq_create_helper(struct snap_virtio_blk_ctrl_queue *vbq,
				   struct snap_virtio_ctrl *vctrl, int index, bool in_recovery)
{
	struct snap_vq_adm_create_attr attr = {};
	struct snap_virtio_blk_device_attr *dev_attr;
	struct snap_virtio_blk_ctrl *blk_ctrl = to_blk_ctrl(vctrl);

	dev_attr = to_blk_device_attr(vctrl->bar_curr);
	vbq->attr = &dev_attr->q_attrs[index];
	vbq->common.ctrl = vctrl;
	vbq->common.index = index;

	attr.common.index = index;
	attr.common.size = vbq->attr->vattr.size;
	attr.common.desc_pa = vbq->attr->vattr.desc;
	attr.common.driver_pa = vbq->attr->vattr.driver;
	attr.common.device_pa = vbq->attr->vattr.device;
	attr.common.msix_vector = vbq->attr->vattr.msix_vector;
	attr.common.op_flags = SNAP_VQ_OP_FLAGS_IN_ORDER_COMPLETIONS;
	attr.common.xmkey = vctrl->xmkey->mkey;
	attr.common.pd = vctrl->lb_pd;
	attr.common.sdev = vctrl->sdev;
	attr.common.caps = &vctrl->sdev->sctx->virtio_blk_caps;
	attr.common.vctrl = vctrl;
	attr.common.in_recovery = in_recovery;

	attr.adm_process_fn = snap_virtio_blk_adm_cmd_process;

	vbq->q_impl = snap_vq_adm_create(&attr);
	if (!vbq->q_impl) {
		snap_error("Controller failed to create admin virtq\n");
		return -EINVAL;
	}

	vbq->is_adm_vq = true;
	blk_ctrl->has_adm_vq = true;

	return 0;
}

static int blk_virtq_create_helper(struct snap_virtio_blk_ctrl_queue *vbq,
				   struct snap_virtio_ctrl *vctrl, int index, bool in_recovery)
{
	struct virtq_create_attr attr = {0};
	struct snap_virtio_blk_ctrl *blk_ctrl = to_blk_ctrl(vctrl);
	struct snap_context *sctx = vctrl->sdev->sctx;
	struct snap_virtio_blk_device_attr *dev_attr;

	dev_attr = to_blk_device_attr(vctrl->bar_curr);
	vbq->attr = &dev_attr->q_attrs[index];
	attr.idx = index;
	attr.size_max = dev_attr->size_max;
	attr.seg_max = dev_attr->seg_max;
	attr.queue_size = vbq->attr->vattr.size;
	attr.pd = blk_ctrl->common.lb_pd;
	attr.desc = vbq->attr->vattr.desc;
	attr.driver = vbq->attr->vattr.driver;
	attr.device = vbq->attr->vattr.device;
	attr.max_tunnel_desc = sctx->virtio_blk_caps.max_tunnel_desc;
	attr.msix_vector = vbq->attr->vattr.msix_vector;
	attr.virtio_version_1_0 = vbq->attr->vattr.virtio_version_1_0;
	attr.force_in_order = blk_ctrl->common.force_in_order;
	attr.in_recovery = in_recovery;

	attr.xmkey = vctrl->xmkey->mkey;

	vbq->common.ctrl = vctrl;
	vbq->common.index = index;

	vbq->q_impl = blk_virtq_create(vbq, blk_ctrl->bdev_ops, blk_ctrl->bdev,
				       vctrl->sdev, &attr);
	if (!vbq->q_impl) {
		snap_error("controller %p bdf 0x%x - failed to create blk virtq\n", vctrl,
			   dev_attr->vattr.pci_bdf);
		return -EINVAL;
	}

	dev_attr->q_attrs[index].hw_available_index = attr.hw_available_index;
	dev_attr->q_attrs[index].hw_used_index = attr.hw_used_index;

	return 0;
}

static struct snap_virtio_ctrl_queue *
snap_virtio_blk_ctrl_queue_create(struct snap_virtio_ctrl *vctrl, int index)
{
	struct snap_virtio_blk_ctrl_queue *vbq;

	vbq = calloc(1, sizeof(*vbq));
	if (!vbq)
		return NULL;

	vbq->in_error = false;

	/* queue creation will be finished during resume */
	if (vctrl->state == SNAP_VIRTIO_CTRL_SUSPENDED)
		return &vbq->common;

	if (index == 0 && vctrl->sdev->pci->type == SNAP_VIRTIO_BLK_PF &&
	    vctrl->bar_curr->driver_feature & (1ULL << VIRTIO_F_ADMIN_VQ)) {
		if (blk_adm_virtq_create_helper(vbq, vctrl, index, false)) {
			free(vbq);
			return NULL;
		}
	} else {
		if (blk_virtq_create_helper(vbq, vctrl, index, false)) {
			free(vbq);
			return NULL;
		}
	}
	return &vbq->common;
}

/**
 * snap_virtio_blk_ctrl_queue_destroy() - destroys and deletes queue
 * @vq: queue to destroy
 *
 * Function moves the queue to suspend state before destroying it.
 *
 * Context: Function assumes queue isn't progressed outside of its scope
 *
 * Return: void
 */
static void snap_virtio_blk_ctrl_queue_destroy(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_virtio_blk_ctrl_queue *vbq = to_blk_ctrl_q(vq);
	struct snap_virtio_blk_device_attr *dev_attr;

	/* in the case of resume failure vbq->q_impl may be NULL */
	if (vbq->q_impl) {
		if (vbq->is_adm_vq)
			snap_vq_adm_destroy(vbq->q_impl);
		else
			blk_virtq_destroy(vbq->q_impl);
	}
	/* make sure that next time the queue is created with
	 * the default hw_avail and used values
	 */
	dev_attr = to_blk_device_attr(vq->ctrl->bar_curr);
	dev_attr->q_attrs[vq->index].hw_available_index = 0;
	dev_attr->q_attrs[vq->index].hw_used_index = 0;
	free(vbq);
}

static void snap_virtio_blk_ctrl_queue_suspend(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_virtio_blk_ctrl_queue *vbq = to_blk_ctrl_q(vq);

	if (!vbq->is_adm_vq) {
		virtq_suspend(&to_blk_ctx(vbq->q_impl)->common_ctx);
		return;
	}
	if (snap_unlikely(!vbq->q_impl))
		return;

	snap_vq_suspend(vbq->q_impl);
	snap_info("ctrl %p qid %d is FLUSHING\n", vq->ctrl, vq->index);
}

static bool snap_virtio_blk_ctrl_queue_is_suspended(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_virtio_blk_ctrl_queue *vbq = to_blk_ctrl_q(vq);
	bool suspended;

	if (!vbq->is_adm_vq) {
		if (!virtq_is_suspended(&to_blk_ctx(vbq->q_impl)->common_ctx))
			return false;

		snap_info("ctrl %p queue %d: pg_id %d SUSPENDED\n", vbq->common.ctrl, vq->index, vq->pg->id);
		return true;
	}

	if (snap_unlikely(!vbq->q_impl))
		return true;
	suspended = snap_vq_is_suspended(vbq->q_impl);
	if (suspended)
		snap_info("ctrl %p qid %d is SUSPENDED\n", vq->ctrl, vq->index);
	return suspended;
}

static int snap_virtio_blk_ctrl_queue_resume(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_virtio_blk_ctrl_queue *vbq = to_blk_ctrl_q(vq);
	struct snap_virtio_ctrl_queue_state state = {};
	int ret, index;
	struct snap_virtio_blk_device_attr *dev_attr;
	struct snap_virtio_ctrl *ctrl;

	if (vbq->is_adm_vq) {
		if (snap_unlikely(!vbq->q_impl))
			return 0;

		snap_vq_resume(vbq->q_impl);
		snap_info("ctrl %p qid %d is RESUMED\n", vq->ctrl, vq->index);
		return 0;
	}

	index = vq->index;
	ctrl = vq->ctrl;
	dev_attr = to_blk_device_attr(ctrl->bar_curr);
	/* if q_impl is NULL it means that we are resuming after
	 * the state restore
	 */
	if (vbq->q_impl) {
		if (!virtq_is_suspended(&to_blk_ctx(vbq->q_impl)->common_ctx))
			return -EINVAL;

		/* save hw_used and hw_avail to allow resume */
		ret = blk_virtq_get_state(vbq->q_impl, &state);
		if (ret) {
			snap_error("queue %d: failed to get state, cannot resume.\n",
					vq->index);
			return -EINVAL;
		}

		blk_virtq_destroy(to_blk_ctx(vbq->q_impl));
		dev_attr->q_attrs[index].hw_available_index = state.hw_available_index;
		dev_attr->q_attrs[index].hw_used_index = state.hw_used_index;
	}

	// The check here is only for when is_adm_vq isn't set because we are before the vq create-
	// this should use the new blk vq implementation when its added.
	if (index == 0 && ctrl->sdev->pci->type == SNAP_VIRTIO_BLK_PF &&
		ctrl->bar_curr->driver_feature & (1ULL << VIRTIO_F_ADMIN_VQ))
		ret = blk_adm_virtq_create_helper(vbq, ctrl, index, true);
	else
		ret = blk_virtq_create_helper(vbq, ctrl, index, true);
	if (ret)
		return ret;

	snap_info("ctrl %p queue %d: RESUMED with hw_avail %u hw_used %u\n",
		  vq->ctrl, vq->index,
		  dev_attr->q_attrs[index].hw_available_index,
		  dev_attr->q_attrs[index].hw_used_index);
	return 0;
}

static int snap_virtio_blk_ctrl_queue_progress(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_virtio_blk_ctrl_queue *vbq = to_blk_ctrl_q(vq);
	struct virtq_common_ctx *q = &to_blk_ctx(vbq->q_impl)->common_ctx;

	if (vbq->is_adm_vq) {
		if (snap_likely(vbq->q_impl))
			return snap_vq_progress(vbq->q_impl);
	} else
		return virtq_progress(q, vq->thread_id);

	return 0;
}

static void snap_virtio_blk_ctrl_queue_start(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_virtio_blk_ctrl_queue *vbq = to_blk_ctrl_q(vq);
	struct virtq_start_attr attr = {};

	if (vbq->is_adm_vq)
		return;

	attr.pg_id = vq->pg->id;
	virtq_start(&to_blk_ctx(vbq->q_impl)->common_ctx, &attr);
}

static int snap_virtio_blk_ctrl_queue_get_state(struct snap_virtio_ctrl_queue *vq,
						struct snap_virtio_ctrl_queue_state *state)
{
	struct snap_virtio_blk_ctrl_queue *vbq = to_blk_ctrl_q(vq);

	if (!vbq->is_adm_vq)
		return blk_virtq_get_state(vbq->q_impl, state);
	return 0;
}

const struct snap_virtio_ctrl_queue_stats *
snap_virtio_blk_ctrl_queue_get_io_stats(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_virtio_blk_ctrl_queue *vbq = to_blk_ctrl_q(vq);

	if (!vbq->is_adm_vq)
		return blk_virtq_get_io_stats(vbq->q_impl);
	return NULL;
}

static int snap_virtio_blk_ctrl_recover(struct snap_virtio_blk_ctrl *ctrl,
					struct snap_virtio_blk_ctrl_attr *attr)
{
	int ret;
	struct snap_virtio_blk_device_attr blk_attr = {};
	bool check_cap;

	snap_info("create controller in recover mode - ctrl=%p max_queues=%ld enabled_queues=%ld\n",
		   ctrl, ctrl->common.max_queues, ctrl->common.enabled_queues);

	blk_attr.queues = ctrl->common.max_queues;
	blk_attr.q_attrs = calloc(blk_attr.queues, sizeof(*blk_attr.q_attrs));
	if (!blk_attr.q_attrs) {
		snap_error("Failed to allocate memory for Qs attribute\n");
		ret = -ENOMEM;
		goto err;
	}

	ret = snap_virtio_blk_query_device(ctrl->common.sdev, &blk_attr);
	if (ret) {
		snap_error("Failed to query bar during recovery of controller\n");
		ret = -EINVAL;
		goto err;
	}

	/* Allow capacity to change while driver is up if we are forcing recover */
	check_cap = !attr->common.force_recover;
	if (!snap_virtio_blk_ctrl_bar_is_setup_valid(&blk_attr, &attr->regs, check_cap)) {
		snap_error("The configured parameters don't fit bar data\n");
		ret = -EINVAL;
		goto err;
	}

	ret = snap_virtio_ctrl_recover(&ctrl->common, &blk_attr.vattr);
err:
	free(blk_attr.q_attrs);
	return ret;
}

static struct snap_virtio_queue_ops snap_virtio_blk_queue_ops = {
	.create = snap_virtio_blk_ctrl_queue_create,
	.destroy = snap_virtio_blk_ctrl_queue_destroy,
	.progress = snap_virtio_blk_ctrl_queue_progress,
	.start = snap_virtio_blk_ctrl_queue_start,
	.suspend = snap_virtio_blk_ctrl_queue_suspend,
	.is_suspended = snap_virtio_blk_ctrl_queue_is_suspended,
	.resume = snap_virtio_blk_ctrl_queue_resume,
	.get_state = snap_virtio_blk_ctrl_queue_get_state,
	.get_io_stats = snap_virtio_blk_ctrl_queue_get_io_stats
};

/**
 * snap_virtio_blk_ctrl_open() - Create a new virtio-blk controller
 * @sctx:       snap context to open new controller
 * @attr:       virtio-blk controller attributes
 * @bdev_ops:   operations on backend block device
 * @bdev:       backend block device
 *
 * Allocates a new virtio-blk controller based on the requested attributes.
 *
 * Return: Returns a new snap_virtio_blk_ctrl in case of success, NULL otherwise and
 *	 errno will be set to indicate the failure reason.
 */
struct snap_virtio_blk_ctrl*
snap_virtio_blk_ctrl_open(struct snap_context *sctx,
			  struct snap_virtio_blk_ctrl_attr *attr,
			  struct snap_bdev_ops *bdev_ops,
			  void *bdev)
{
	struct snap_virtio_blk_ctrl *ctrl;
	int ret;
	int flags;

	ctrl = calloc(1, sizeof(*ctrl));
	if (!ctrl) {
		errno = ENOMEM;
		goto err;
	}

	ctrl->bdev_ops = bdev_ops;
	ctrl->bdev = bdev;

	if (attr->common.pf_id < 0 ||
	    attr->common.pf_id >= sctx->virtio_blk_pfs.max_pfs) {
		snap_error("Bad PF id (%d). Only %d PFs are supported\n",
			   attr->common.pf_id, sctx->virtio_blk_pfs.max_pfs);
		errno = -ENODEV;
		goto free_ctrl;
	}

	if (attr->common.pci_type == SNAP_VIRTIO_BLK_VF &&
	    (attr->common.vf_id < 0 ||
	    attr->common.vf_id >= sctx->virtio_blk_pfs.pfs[attr->common.pf_id].num_vfs)) {
		snap_error("Bad VF id (%d). Only %d VFs are supported for PF %d\n",
			   attr->common.vf_id,
			   sctx->virtio_blk_pfs.pfs[attr->common.pf_id].num_vfs,
			   attr->common.pf_id);
		errno = -ENODEV;
		goto free_ctrl;
	}

	attr->common.type = SNAP_VIRTIO_BLK_CTRL;
	ret = snap_virtio_ctrl_open(&ctrl->common,
				    &snap_virtio_blk_ctrl_bar_ops,
				    &snap_virtio_blk_queue_ops,
				    sctx, &attr->common);
	if (ret) {
		errno = ENODEV;
		goto free_ctrl;
	}

	ret = snap_virtio_blk_init_device(ctrl->common.sdev);
	if (ret)
		goto close_ctrl;

	if (attr->common.recover) {
		/* Started from release 08/2021, the recovery flag
		 * should be used as default during the creation of the controller.
		 * We need to distinguish between 'real' recovery or
		 * 'new creation' of the controller.
		 * This can be done by testing the reset flag.
		 * For 'recovery' it should be as following:
		 * enabled=1 reset=0 status=X when X indicates
		 * driver is set up and ready to drive
		 * the device (refer to 2.1 Device Status Field)
		 */

		ret = snap_virtio_ctrl_should_recover(&ctrl->common);
		if (ret < 0)
			goto close_ctrl;

		ret |= attr->common.force_recover;
		attr->common.recover = ret;
	}

	if (attr->common.suspended || attr->common.recover) {
		/* Creating controller in the suspended state or recovery mode.
		 * When created in the suspended state means that
		 * there will be a state restore that will override current
		 * bar config.
		 * Also it means that the host is not going to touch
		 * anything. So let state restore do the actual configuration.
		 *
		 * When created in recover mode means the state of controller
		 * should be recovered - see snap_virtio_blk_ctrl_recover function
		 * for more details.
		 */
		ctrl->common.state = SNAP_VIRTIO_CTRL_SUSPENDED;
		flags = 0;
		snap_info("creating virtio block controller in the SUSPENDED state\n");
	} else
		flags = SNAP_VIRTIO_MOD_PCI_COMMON_CFG | SNAP_VIRTIO_MOD_DEV_CFG;

	ret = snap_virtio_blk_ctrl_bar_setup(ctrl, &attr->regs, flags);
	if (ret)
		goto teardown_dev;

	if (attr->common.recover) {
		ret = snap_virtio_blk_ctrl_recover(ctrl, attr);
		if (ret)
			goto teardown_dev;
	}

	return ctrl;

teardown_dev:
	snap_virtio_blk_teardown_device(ctrl->common.sdev);
close_ctrl:
	snap_virtio_ctrl_close(&ctrl->common);
free_ctrl:
	free(ctrl);
err:
	return NULL;
}

/**
 * snap_virtio_blk_ctrl_close() - Destroy a virtio-blk controller
 * @ctrl:       virtio-blk controller to close
 *
 * Destroy and free virtio-blk controller.
 */
void snap_virtio_blk_ctrl_close(struct snap_virtio_blk_ctrl *ctrl)
{
	snap_virtio_ctrl_stop(&ctrl->common);
	if (!ctrl->common.pending_flr)
		snap_virtio_blk_teardown_device(ctrl->common.sdev);
	snap_virtio_ctrl_close(&ctrl->common);
	free(ctrl);
}

static void snap_virtio_blk_ctrl_bdev_detach_check(struct snap_virtio_ctrl *ctrl)
{
	struct snap_virtio_blk_ctrl *blk_ctrl = to_blk_ctrl(ctrl);
	struct snap_virtio_blk_ctrl_queue *vbq;
	struct virtq_priv *priv;
	int i;

	if (!blk_ctrl->pending_bdev_detach || !blk_ctrl->bdev_detach_cb)
		return;

	for (i = 0; i < ctrl->max_queues; i++) {
		if (!ctrl->queues[i])
			continue;
		vbq = to_blk_ctrl_q(ctrl->queues[i]);
		if (!vbq->q_impl)
			continue;
		priv = to_blk_ctx(vbq->q_impl)->common_ctx.priv;
		if (to_blk_queue(priv->snap_vbq)->uncomp_bdev_cmds != 0)
			return;
	}
	blk_ctrl->bdev_detach_cb(blk_ctrl->bdev_detach_cb_arg);
	blk_ctrl->bdev_detach_cb = NULL;
}

/**
 * snap_virtio_blk_ctrl_progress() - Handles control path changes in
 *				   virtio-blk controller
 * @ctrl:       controller instance to handle
 *
 * Looks for control path status in virtio-blk controller and respond
 * to any identified changes (e.g. new enabled queues, changes in
 * device status, etc.)
 */
void snap_virtio_blk_ctrl_progress(struct snap_virtio_blk_ctrl *ctrl)
{
	/*Check if we need to detach bdev */
	snap_virtio_blk_ctrl_bdev_detach_check(&ctrl->common);

	snap_virtio_ctrl_progress(&ctrl->common);
}

/**
 * snap_virtio_blk_ctrl_io_progress() - single-threaded IO requests handling
 * @ctrl:       controller instance
 *
 * Looks for any IO requests from host received on any QPs, and handles
 * them based on the request's parameters.
 */
int snap_virtio_blk_ctrl_io_progress(struct snap_virtio_blk_ctrl *ctrl)
{
	return snap_virtio_ctrl_io_progress(&ctrl->common);
}

/**
 * snap_virtio_blk_ctrl_io_progress_thread() - Handle IO requests for thread
 * @ctrl:       controller instance
 * @thread_id:	id queues belong to
 *
 * Looks for any IO requests from host received on QPs which belong to thread
 * thread_id, and handles them based on the request's parameters.
 */
int snap_virtio_blk_ctrl_io_progress_thread(struct snap_virtio_blk_ctrl *ctrl,
					     uint32_t thread_id)
{
	return snap_virtio_ctrl_pg_io_progress(&ctrl->common, thread_id);
}
