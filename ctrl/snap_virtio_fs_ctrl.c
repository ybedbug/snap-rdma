#include <stdint.h>
// TODO - add under configure flag
//#include <linux/virtio_fs.h>

struct virtio_fs_config {
        /* Filesystem name (UTF-8, not NUL-terminated, padded with NULs) */
        uint8_t tag[36];

        /* Number of request queues */
        uint32_t num_request_queues;
} __attribute__((packed));

#include <linux/virtio_config.h>

#include "snap_virtio_fs_ctrl.h"
#include "snap_virtio_fs_virtq.h"

#define SNAP_VIRTIO_FS_MODIFIABLE_FTRS (1ULL << VIRTIO_F_VERSION_1)
#define SNAP_VIRTIO_FS_SEG_SIZE_MAX (4096)


static inline struct snap_virtio_fs_ctrl_queue*
to_fs_ctrl_q(struct snap_virtio_ctrl_queue *vq)
{
	return container_of(vq, struct snap_virtio_fs_ctrl_queue, common);
}

static inline struct snap_virtio_fs_ctrl*
to_fs_ctrl(struct snap_virtio_ctrl *vctrl)
{
	return container_of(vctrl, struct snap_virtio_fs_ctrl, common);
}

static struct snap_virtio_device_attr*
snap_virtio_fs_ctrl_bar_create(struct snap_virtio_ctrl *vctrl)
{
	struct snap_virtio_fs_device_attr *vbbar;

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

static void snap_virtio_fs_ctrl_bar_destroy(struct snap_virtio_device_attr *vbar)
{
	struct snap_virtio_fs_device_attr *vbbar = to_fs_device_attr(vbar);

	free(vbbar->q_attrs);
	free(vbbar);
}

static void snap_virtio_fs_ctrl_bar_copy(struct snap_virtio_device_attr *vorig,
					 struct snap_virtio_device_attr *vcopy)
{
	struct snap_virtio_fs_device_attr *vborig = to_fs_device_attr(vorig);
	struct snap_virtio_fs_device_attr *vbcopy = to_fs_device_attr(vcopy);

	memcpy(vbcopy->q_attrs, vborig->q_attrs,
	       vbcopy->queues * sizeof(*vbcopy->q_attrs));
}

static int snap_virtio_fs_ctrl_bar_update(struct snap_virtio_ctrl *vctrl,
					  struct snap_virtio_device_attr *vbar)
{
	struct snap_virtio_fs_device_attr *vbbar = to_fs_device_attr(vbar);

	return snap_virtio_fs_query_device(vctrl->sdev, vbbar);
}

static int snap_virtio_fs_ctrl_bar_modify(struct snap_virtio_ctrl *vctrl,
					  uint64_t mask,
					  struct snap_virtio_device_attr *vbar)
{
	struct snap_virtio_fs_device_attr *vbbar = to_fs_device_attr(vbar);

	return snap_virtio_fs_modify_device(vctrl->sdev, mask, vbbar);
}

static int
snap_virtio_fs_ctrl_bar_add_status(struct snap_virtio_fs_ctrl *ctrl,
				   enum snap_virtio_common_device_status status)
{
	struct snap_virtio_fs_device_attr *bar;
	int ret = 0;

	bar = to_fs_device_attr(ctrl->common.bar_curr);
	if (!(bar->vattr.status & status)) {
		bar->vattr.status |= status;
		ret = snap_virtio_fs_modify_device(ctrl->common.sdev,
					     SNAP_VIRTIO_MOD_DEV_STATUS, bar);
	}

	return ret;
}

static struct snap_virtio_queue_attr*
snap_virtio_fs_ctrl_bar_get_queue_attr(struct snap_virtio_device_attr *vbar,
				       int index)
{
	struct snap_virtio_fs_device_attr *vbbar = to_fs_device_attr(vbar);

	return &vbbar->q_attrs[index].vattr;
}

static unsigned
snap_virtio_fs_ctrl_bar_get_state_size(struct snap_virtio_ctrl *ctrl)
{
	/* TODO use fs device config definition from linux/virtio_fs.h */
	return sizeof(struct virtio_fs_config);
}

static void
snap_virtio_fs_ctrl_bar_dump_state(struct snap_virtio_ctrl *ctrl, void *buf, int len)
{
	struct virtio_fs_config *dev_cfg;
	uint8_t last_ch;

	if (len < snap_virtio_fs_ctrl_bar_get_state_size(ctrl)) {
		snap_info(">>> fs_config: state is truncated (%d < %d)\n", len,
			  snap_virtio_fs_ctrl_bar_get_state_size(ctrl));
		return;
	}

	dev_cfg = buf;	
	last_ch = dev_cfg->tag[sizeof(dev_cfg->tag) - 1];
	if (last_ch != 0)
		dev_cfg->tag[sizeof(dev_cfg->tag) - 1] = 0;		
	
	snap_info(">>> tag: '%s num_request_queues: %u\n",
		       dev_cfg->tag, dev_cfg->num_request_queues);
		       
	dev_cfg->tag[sizeof(dev_cfg->tag) - 1] = last_ch;
}

static int
snap_virtio_fs_ctrl_bar_get_state(struct snap_virtio_ctrl *ctrl,
				  struct snap_virtio_device_attr *vbar,
				  void *buf, unsigned len)
{
	struct snap_virtio_fs_device_attr *vfsbar = to_fs_device_attr(vbar);
	struct virtio_fs_config *dev_cfg;

	if (sizeof(dev_cfg->tag) != sizeof(vfsbar->tag))
		return -EINVAL;

	if (len < snap_virtio_fs_ctrl_bar_get_state_size(ctrl))
		return -EINVAL;

	dev_cfg = buf;
	memcpy(dev_cfg->tag, vfsbar->tag, sizeof(vfsbar->tag));
	dev_cfg->num_request_queues = vfsbar->num_request_queues;
	return snap_virtio_fs_ctrl_bar_get_state_size(ctrl);
}

static int
snap_virtio_fs_ctrl_bar_set_state(struct snap_virtio_ctrl *ctrl,
				  struct snap_virtio_device_attr *vbar,
				  struct snap_virtio_ctrl_queue_state *queue_state,
				  void *buf, int len)
{
	struct snap_virtio_fs_device_attr *vfsbar = to_fs_device_attr(vbar);
	struct virtio_fs_config *dev_cfg;
	int i, ret;

	if (!buf)
		return -EINVAL;

	if (len < snap_virtio_fs_ctrl_bar_get_state_size(ctrl))
		return -EINVAL;

	if (!queue_state)
		return -EINVAL;
				
	for (i = 0; i < ctrl->max_queues; i++) {
		vfsbar->q_attrs[i].hw_available_index = queue_state[i].hw_available_index;
		vfsbar->q_attrs[i].hw_used_index = queue_state[i].hw_used_index;
	}

	dev_cfg = buf;
	
	if (sizeof(dev_cfg->tag) != sizeof(vfsbar->tag))
		return -EINVAL;
	
	memcpy(vfsbar->tag, dev_cfg->tag, sizeof(dev_cfg->tag));
	vfsbar->num_request_queues = dev_cfg->num_request_queues;

	ret = snap_virtio_fs_modify_device(ctrl->sdev,
					    SNAP_VIRTIO_MOD_ALL |
					    SNAP_VIRTIO_MOD_QUEUE_CFG,
					    vfsbar);
	if (ret)
		snap_error("Failed to restore virtio fs device config\n");

	return ret;
}

static bool
snap_virtio_fs_ctrl_bar_queue_attr_valid(struct snap_virtio_device_attr *vbar)
{
	struct snap_virtio_fs_device_attr *vfsbar = to_fs_device_attr(vbar);
	return vfsbar->q_attrs ? true : false;
}

static struct snap_virtio_ctrl_bar_ops snap_virtio_fs_ctrl_bar_ops = {
	.create = snap_virtio_fs_ctrl_bar_create,
	.destroy = snap_virtio_fs_ctrl_bar_destroy,
	.copy = snap_virtio_fs_ctrl_bar_copy,
	.update = snap_virtio_fs_ctrl_bar_update,
	.modify = snap_virtio_fs_ctrl_bar_modify,
	.get_queue_attr = snap_virtio_fs_ctrl_bar_get_queue_attr,
	.get_state_size = snap_virtio_fs_ctrl_bar_get_state_size,
	.dump_state = snap_virtio_fs_ctrl_bar_dump_state,
	.get_state = snap_virtio_fs_ctrl_bar_get_state,
	.set_state = snap_virtio_fs_ctrl_bar_set_state,
	.queue_attr_valid = snap_virtio_fs_ctrl_bar_queue_attr_valid
};

static bool
snap_virtio_fs_ctrl_bar_setup_valid(struct snap_virtio_fs_ctrl *ctrl,
				    const struct snap_virtio_fs_device_attr *bar,
				    const struct snap_virtio_fs_registers *regs)
{
	bool ret = true;

	/*
	 * Note: the virtiofs module creates num_request_queues + 1 queues
	 * for more detail refer to ../fs/fuse/virtio_fs.c::virtio_fs_setup_vqs
	 */
	if (regs->num_request_queues + 1 > ctrl->common.max_queues) {
		snap_error("Cannot create %d queues (max %lu)\n", regs->num_request_queues,
			   ctrl->common.max_queues);
		return false;
	}

	/* Everything is configurable as long as driver is still down */
	if (snap_virtio_ctrl_is_stopped(&ctrl->common))
		return true;

	/* virtio_common_pci_config registers */
	if ((regs->device_features ^ bar->vattr.device_feature) &
	    SNAP_VIRTIO_FS_MODIFIABLE_FTRS) {
		snap_error("Cant modify device_features, host driver is up\n");
		ret = false;
	}

	if (regs->queue_size &&
	    regs->queue_size != bar->vattr.max_queue_size) {
		snap_error("Cant modify queue_size, host driver is up\n");
		ret = false;
	}

	/* virtio_fs_config registers */

	if (memcmp(regs->tag, bar->tag, sizeof(regs->tag))) {
		snap_error("Cant modify tag, host driver is up\n");
		ret = false;
	}

	if (regs->num_request_queues &&
	    (regs->num_request_queues + 1) != bar->vattr.max_queues) {
		snap_error("Cant modify num_request_queues, host driver is up\n");
		ret = false;
	}

	return ret;
}

/**
 * snap_virtio_fs_ctrl_bar_setup() - Setup PCI BAR virtio registers
 * @ctrl:       controller instance
 * @regs:	registers struct to modify
 *
 * Update all configurable PCI BAR virtio register values, when possible.
 * Value of `0` means value is not to be updated (old value is kept).
 */
int snap_virtio_fs_ctrl_bar_setup(struct snap_virtio_fs_ctrl *ctrl,
				  struct snap_virtio_fs_registers *regs,
				  uint16_t regs_mask)
{
	struct snap_virtio_fs_device_attr bar = {};
	uint16_t extra_flags = 0;
	int ret;
	uint64_t new_ftrs;

	/* Get last bar values as a reference */
	ret = snap_virtio_fs_query_device(ctrl->common.sdev, &bar);
	if (ret) {
		snap_error("Failed to query bar\n");
		return -EINVAL;
	}

	if (!snap_virtio_fs_ctrl_bar_setup_valid(ctrl, &bar, regs)) {
		snap_error("Setup is not valid\n");
		return -EINVAL;
	}

	/*
	 * If num_request_queues was not initialized correctly on bar,
	 * and user didn't specify specific value for it, just
	 * use the maximal value possible
	 */
	if (!regs->num_request_queues) {
		if (bar.vattr.max_queues < 1 ||
		    bar.vattr.max_queues > ctrl->common.max_queues) {
			snap_warn("Invalid num_queues detected on bar. "
				  "Clamping down to max possible (%lu)\n",
				  ctrl->common.max_queues - 1);
			regs->num_request_queues = ctrl->common.max_queues - 1;
		}
	}

	if (regs_mask & SNAP_VIRTIO_MOD_PCI_COMMON_CFG) {
		/* Update only the device_feature modifiable bits */
		new_ftrs = regs->device_features ? : bar.vattr.device_feature;
		bar.vattr.device_feature = (bar.vattr.device_feature &
					    ~SNAP_VIRTIO_FS_MODIFIABLE_FTRS);
		bar.vattr.device_feature |= (new_ftrs &
					     SNAP_VIRTIO_FS_MODIFIABLE_FTRS);
		bar.vattr.max_queue_size = regs->queue_size ? :
					   bar.vattr.max_queue_size;

		bar.vattr.max_queues = regs->num_request_queues ? regs->num_request_queues + 1:
				       bar.vattr.max_queues;

		if (regs->num_request_queues) {
			/*
			 * We always wish to keep fs queues and
			 * virtio queues values aligned
			 */
			extra_flags |= SNAP_VIRTIO_MOD_DEV_CFG;
			bar.num_request_queues = regs->num_request_queues;
		}
	}

	if (regs_mask & SNAP_VIRTIO_MOD_DEV_CFG) {
		memcpy(bar.tag, regs->tag, sizeof(regs->tag));
		bar.num_request_queues = regs->num_request_queues ? : bar.num_request_queues;
	}

	ret = snap_virtio_fs_modify_device(ctrl->common.sdev,
					    regs_mask | extra_flags, &bar);
	if (ret) {
		snap_error("Failed to config virtio controller - ret: %d\n", ret);
		return ret;
	}

	return ret;
}

static int
snap_virtio_fs_ctrl_queue_get_debugstat(struct snap_virtio_ctrl_queue *vq,
			struct snap_virtio_queue_debugstat *q_debugstat)
{
	struct snap_virtio_fs_ctrl_queue *vfsq = to_fs_ctrl_q(vq);

	return fs_virtq_get_debugstat(vfsq->q_impl, q_debugstat);
}

static int
snap_virtio_fs_ctrl_count_error(struct snap_virtio_fs_ctrl *ctrl)
{
	int i, ret;
	struct snap_virtio_ctrl_queue *vq;
	struct snap_virtio_fs_ctrl_queue *vfsq;
	struct snap_virtio_fs_queue_attr *attr;
	struct snap_virtio_queue_attr *vattr;

	for (i = 0; i < ctrl->common.max_queues; i++) {
		vq = ctrl->common.queues[i];
		if (!vq)
			continue;

		vfsq = to_fs_ctrl_q(vq);
		if (vfsq->in_error)
			continue;

		attr = (struct snap_virtio_fs_queue_attr *)(void *)vfsq->attr;
		ret = fs_virtq_query_error_state(vfsq->q_impl, attr);
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

			vfsq->in_error = true;
		}
	}

	return 0;
}

static int
snap_virtio_fs_ctrl_global_get_debugstat(struct snap_virtio_fs_ctrl *ctrl,
			struct snap_virtio_ctrl_debugstat *ctrl_debugstat)
{
	int i, ret;
	struct snap_virtio_fs_device *vfsdev;
	struct snap_virtio_fs_queue *virtq;
	struct snap_virtio_queue_counters_attr vqc_attr = {};

	vfsdev = ctrl->common.sdev->dd_data;
	for (i = 0; i < vfsdev->num_queues; i++) {
		virtq = &vfsdev->virtqs[i];

		ret = snap_virtio_query_queue_counters(virtq->virtq.ctrs_obj, &vqc_attr);
		if (ret) {
			snap_error("Failed to query virtio_q_counter obj\n");
			return ret;
		}

		ctrl_debugstat->bad_descriptor_error += vqc_attr.bad_desc_errors;
		ctrl_debugstat->invalid_buffer += vqc_attr.invalid_buffer;
		ctrl_debugstat->desc_list_exceed_limit += vqc_attr.exceed_max_chain;
	}

	ret = snap_virtio_fs_ctrl_count_error(ctrl);
	if (ret) {
		snap_error("Failed to count queue error stats\n");
		return ret;
	}

	ctrl_debugstat->network_error = ctrl->network_error;
	ctrl_debugstat->internal_error = ctrl->internal_error;

	return 0;
}

int snap_virtio_fs_ctrl_get_debugstat(struct snap_virtio_fs_ctrl *ctrl,
			struct snap_virtio_ctrl_debugstat *ctrl_debugstat)
{
	int i;
	int enabled_queues = 0;
	int ret = 0;

	if (ctrl->common.state != SNAP_VIRTIO_CTRL_STARTED)
		goto out;

	ret = snap_virtio_fs_ctrl_global_get_debugstat(ctrl, ctrl_debugstat);
	if (ret)
		goto out;

	for (i = 0; i < ctrl->common.max_queues; i++) {
		struct snap_virtio_ctrl_queue *vq = ctrl->common.queues[i];

		if (!vq)
			continue;

		ret = snap_virtio_fs_ctrl_queue_get_debugstat(vq,
				&ctrl_debugstat->queues[enabled_queues]);
		if (ret)
			goto out;
		enabled_queues++;
	}
	ctrl_debugstat->num_queues = enabled_queues;

out:
	return ret;
}

static int fs_virtq_create_helper(struct snap_virtio_fs_ctrl_queue *vfsq,
				  struct snap_virtio_ctrl *vctrl, int index)
{
	struct fs_virtq_create_attr attr = {0};
	struct snap_virtio_fs_ctrl *fs_ctrl = to_fs_ctrl(vctrl);
	struct snap_context *sctx = vctrl->sdev->sctx;
	struct snap_virtio_fs_device_attr *dev_attr;

	dev_attr = to_fs_device_attr(vctrl->bar_curr);
	vfsq->attr = &dev_attr->q_attrs[index];
	attr.idx = index;
	attr.size_max = SNAP_VIRTIO_FS_SEG_SIZE_MAX;
	attr.seg_max = vfsq->attr->vattr.size;
	attr.queue_size = vfsq->attr->vattr.size;
	attr.pd = fs_ctrl->common.lb_pd;
	attr.desc = vfsq->attr->vattr.desc;
	attr.driver = vfsq->attr->vattr.driver;
	attr.device = vfsq->attr->vattr.device;
	attr.max_tunnel_desc = sctx->virtio_fs_caps.max_tunnel_desc;
	attr.msix_vector = vfsq->attr->vattr.msix_vector;
	attr.virtio_version_1_0 = vfsq->attr->vattr.virtio_version_1_0;
	attr.force_in_order = fs_ctrl->common.force_in_order;

	attr.hw_available_index = vfsq->attr->hw_available_index;
	attr.hw_used_index = vfsq->attr->hw_used_index;

	vfsq->common.ctrl = vctrl;
	vfsq->common.index = index;

	vfsq->q_impl = fs_virtq_create(vfsq, fs_ctrl->fs_dev_ops, fs_ctrl->fs_dev,
				       vctrl->sdev, &attr);
	if (!vfsq->q_impl) {
		snap_error("controller failed to create fs virtq\n");
		return -EINVAL;
	}

	return 0;
}

static struct snap_virtio_ctrl_queue *
snap_virtio_fs_ctrl_queue_create(struct snap_virtio_ctrl *vctrl, int index)
{
	struct snap_virtio_fs_ctrl_queue *vfsq;

	vfsq = calloc(1, sizeof(*vfsq));
	if (!vfsq)
		return NULL;

	vfsq->in_error = false;

	/* queue creation will be finished during resume */
	if (vctrl->state == SNAP_VIRTIO_CTRL_SUSPENDED)
		return &vfsq->common;

	if (fs_virtq_create_helper(vfsq, vctrl, index)) {
		free(vfsq);
		return NULL;
	}

	return &vfsq->common;
}

/**
 * snap_virtio_fs_ctrl_queue_destroy() - destroys and deletes queue
 * @vq: queue to destroy
 *
 * Function moves the queue to suspend state before destroying it.
 *
 * Context: Function assumes queue isnt progressed outside of its scope
 *
 * Return: void
 */
static void snap_virtio_fs_ctrl_queue_destroy(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_virtio_fs_ctrl_queue *vfsq = to_fs_ctrl_q(vq);
	struct snap_virtio_fs_device_attr *dev_attr;

	/* in the case of resume failure vfsq->q_impl may be NULL */
	if (vfsq->q_impl)
		fs_virtq_destroy(vfsq->q_impl);

	/* make sure that next time the queue is created with
	 * the default hw_avail and used values
	 */
	dev_attr = to_fs_device_attr(vq->ctrl->bar_curr);
	dev_attr->q_attrs[vq->index].hw_available_index = 0;
	dev_attr->q_attrs[vq->index].hw_used_index = 0;
	free(vfsq);
}

static void snap_virtio_fs_ctrl_queue_suspend(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_virtio_fs_ctrl_queue *vfsq = to_fs_ctrl_q(vq);

	fs_virtq_suspend(vfsq->q_impl);
}

static bool snap_virtio_fs_ctrl_queue_is_suspended(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_virtio_fs_ctrl_queue *vfsq = to_fs_ctrl_q(vq);

	if (!fs_virtq_is_suspended(vfsq->q_impl))
		return false;

	snap_info("queue %d: pg_id %d SUSPENDED\n", vq->index, vq->pg->id);
	return true;
}

static int snap_virtio_fs_ctrl_queue_resume(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_virtio_fs_ctrl_queue *vfsq = to_fs_ctrl_q(vq);
	struct snap_virtio_ctrl_queue_state state = {};
	int ret, index;
	struct snap_virtio_fs_device_attr *dev_attr;
	struct snap_virtio_ctrl *ctrl;

	index = vq->index;
	ctrl = vq->ctrl;
	dev_attr = to_fs_device_attr(ctrl->bar_curr);

	/* if q_impl is NULL it means that we are resuming after
	 * the state restore
	 */
	if (vfsq->q_impl) {
		if (!fs_virtq_is_suspended(vfsq->q_impl))
			return -EINVAL;

		/* save hw_used and hw_avail to allow resume */
		ret = fs_virtq_get_state(vfsq->q_impl, &state);
		if (ret) {
			snap_error("queue %d: failed to get state, cannot resume.\n",
					vq->index);
			return -EINVAL;
		}

		fs_virtq_destroy(vfsq->q_impl);
		dev_attr->q_attrs[index].hw_available_index = state.hw_available_index;
		dev_attr->q_attrs[index].hw_used_index = state.hw_used_index;
	}

	ret = fs_virtq_create_helper(vfsq, ctrl, index);
	if (ret)
		return ret;

	snap_info("queue %d: pg_id %d RESUMED with hw_avail %hu hw_used %hu\n",
		  vq->index, vq->pg->id,
		  dev_attr->q_attrs[index].hw_available_index,
		  dev_attr->q_attrs[index].hw_used_index);
	return 0;
}

static void snap_virtio_fs_ctrl_queue_progress(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_virtio_fs_ctrl_queue *vfsq = to_fs_ctrl_q(vq);

	fs_virtq_progress(vfsq->q_impl);
}

static void snap_virtio_fs_ctrl_queue_start(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_virtio_fs_ctrl_queue *vfsq = to_fs_ctrl_q(vq);
	struct fs_virtq_start_attr attr = {};

	attr.pg_id = vq->pg->id;
	fs_virtq_start(vfsq->q_impl, &attr);
}

static int snap_virtio_fs_ctrl_queue_get_state(struct snap_virtio_ctrl_queue *vq,
					       struct snap_virtio_ctrl_queue_state *state)
{
	struct snap_virtio_fs_ctrl_queue *vfsq = to_fs_ctrl_q(vq);

	return fs_virtq_get_state(vfsq->q_impl, state);
}

static int snap_virtio_fs_ctrl_recover(struct snap_virtio_fs_ctrl *ctrl)
{
	int ret;
	struct snap_virtio_fs_device_attr fs_attr = {};

	snap_info("create controller in recover mode - ctrl=%p"
		  " max_queues=%ld enabled_queues=%ld \n",
		   ctrl, ctrl->common.max_queues, ctrl->common.enabled_queues);

	fs_attr.queues = ctrl->common.max_queues;
	fs_attr.q_attrs = calloc(fs_attr.queues, sizeof(*fs_attr.q_attrs));
	if (!fs_attr.q_attrs) {
		snap_error("Failed to allocate memory for Qs attribute\n");
		ret = -ENOMEM;
		goto err;
	}

	ret = snap_virtio_fs_query_device(ctrl->common.sdev, &fs_attr);
	if (ret) {
		snap_error("Failed to query bar during recovery of controller\n");
		ret = -EINVAL;
		goto err;
	}

	ret = snap_virtio_ctrl_recover(&ctrl->common, &fs_attr.vattr);
err:
	free(fs_attr.q_attrs);
	return ret;
}

static struct snap_virtio_queue_ops snap_virtio_fs_queue_ops = {
	.create = snap_virtio_fs_ctrl_queue_create,
	.destroy = snap_virtio_fs_ctrl_queue_destroy,
	.progress = snap_virtio_fs_ctrl_queue_progress,
	.start = snap_virtio_fs_ctrl_queue_start,
	.suspend = snap_virtio_fs_ctrl_queue_suspend,
	.is_suspended = snap_virtio_fs_ctrl_queue_is_suspended,
	.resume = snap_virtio_fs_ctrl_queue_resume,
	.get_state = snap_virtio_fs_ctrl_queue_get_state
};

/**
 * snap_virtio_fs_ctrl_open() - Create a new virtio-fs controller
 * @sctx:	snap context to open new controller
 * @attr:	virtio-fs controller attributes
 * @fs_dev_ops:	operations on backend fs device
 * @fs_dev:	backend block device
 *
 * Allocates a new virtio-fs controller based on the requested attributes.
 *
 * Return: Returns a new snap_virtio_fs_ctrl in case of success, NULL otherwise and
 *         errno will be set to indicate the failure reason.
 */
struct snap_virtio_fs_ctrl*
snap_virtio_fs_ctrl_open(struct snap_context *sctx,
			 struct snap_virtio_fs_ctrl_attr *attr,
			 struct snap_fs_dev_ops *fs_dev_ops,
			 void *fs_dev)
{
	struct snap_virtio_fs_ctrl *ctrl;
	int ret, flags;

	ctrl = calloc(1, sizeof(*ctrl));
	if (!ctrl) {
		errno = ENOMEM;
		goto err;
	}

	ctrl->fs_dev_ops = fs_dev_ops;
	ctrl->fs_dev = fs_dev;

	if (attr->common.pf_id < 0 ||
	    attr->common.pf_id >= sctx->virtio_fs_pfs.max_pfs) {
		snap_error("Bad PF id (%d). Only %d PFs are supported\n",
			   attr->common.pf_id, sctx->virtio_fs_pfs.max_pfs);
		errno = -ENODEV;
		goto free_ctrl;
	}

	if (attr->common.pci_type == SNAP_VIRTIO_FS_VF &&
	    (attr->common.vf_id < 0 ||
	     attr->common.vf_id >= sctx->virtio_fs_pfs.pfs[attr->common.pf_id].num_vfs)) {
		snap_error("Bad VF id (%d). Only %d VFs are supported for PF %d\n",
			   attr->common.vf_id,
			   sctx->virtio_fs_pfs.pfs[attr->common.pf_id].num_vfs,
			   attr->common.pf_id);
		errno = -ENODEV;
		goto free_ctrl;
	}

	attr->common.type = SNAP_VIRTIO_FS_CTRL;
	ret = snap_virtio_ctrl_open(&ctrl->common,
				    &snap_virtio_fs_ctrl_bar_ops,
				    &snap_virtio_fs_queue_ops,
				    sctx, &attr->common);
	if (ret) {
		errno = ENODEV;
		goto free_ctrl;
	}

	ret = snap_virtio_fs_init_device(ctrl->common.sdev);
	if (ret)
		goto close_ctrl;

	if (attr->common.suspended || attr->common.recover) {
		/* Creating controller in the suspended state or recovery mode.
		 * When created in the suspended state means that
		 * there will be a state restore that will override current
		 * bar config.
		 * Also it means that the host is not going to touch
		 * anything. So let state restore do the actual configuration.
		 *
		 * When created in recover mode means the state of controller
		 * should be recovered - see snap_virtio_fs_ctrl_recover function
		 * for more details.
		 */
		ctrl->common.state = SNAP_VIRTIO_CTRL_SUSPENDED;
		flags = 0;
		snap_info("creating virtio fs controller in the SUSPENDED state\n");
	} else
		flags = SNAP_VIRTIO_MOD_PCI_COMMON_CFG | SNAP_VIRTIO_MOD_DEV_CFG;

	ret = snap_virtio_fs_ctrl_bar_setup(ctrl, &attr->regs, flags);
	if (ret)
		goto teardown_dev;

	if (attr->common.recover) {
		ret = snap_virtio_fs_ctrl_recover(ctrl);
		if (ret)
			goto teardown_dev;
	}

	return ctrl;

teardown_dev:
	snap_virtio_fs_teardown_device(ctrl->common.sdev);
close_ctrl:
	snap_virtio_ctrl_close(&ctrl->common);
free_ctrl:
	free(ctrl);
err:
	return NULL;
}

/**
 * snap_virtio_fs_ctrl_close() - Destroy a virtio-fs controller
 * @ctrl:       virtio-fs controller to close
 *
 * Destroy and free virtio-fs controller.
 */
void snap_virtio_fs_ctrl_close(struct snap_virtio_fs_ctrl *ctrl)
{
	/* We must first notify host the device is no longer operational */
	snap_virtio_fs_ctrl_bar_add_status(ctrl,
				SNAP_VIRTIO_DEVICE_S_DEVICE_NEEDS_RESET);
	snap_virtio_ctrl_stop(&ctrl->common);
	snap_virtio_fs_teardown_device(ctrl->common.sdev);
	snap_virtio_ctrl_close(&ctrl->common);
	free(ctrl);
}

/**
 * snap_virtio_fs_ctrl_progress() - Handles control path changes in
 *                                   virtio-fs controller
 * @ctrl:       controller instance to handle
 *
 * Looks for control path status in virtio-fs controller and respond
 * to any identified changes (e.g. new enabled queues, changes in
 * device status, etc.)
 */
void snap_virtio_fs_ctrl_progress(struct snap_virtio_fs_ctrl *ctrl)
{
	snap_virtio_ctrl_progress(&ctrl->common);
}


/**
 * snap_virtio_fs_ctrl_io_progress() - single-threaded IO requests handling
 * @ctrl:       controller instance
 *
 * Looks for any IO requests from host recieved on any QPs, and handles
 * them based on the request's parameters.
 */
void snap_virtio_fs_ctrl_io_progress(struct snap_virtio_fs_ctrl *ctrl)
{
	snap_virtio_ctrl_io_progress(&ctrl->common);
}

/**
 * snap_virtio_fs_ctrl_io_progress_thread() - Handle IO requests for thread
 * @ctrl:       controller instance
 * @thread_id: 	id queues belong to
 *
 * Looks for any IO requests from host recieved on QPs which belong to thread
 * thread_id, and handles them based on the request's parameters.
 */
void snap_virtio_fs_ctrl_io_progress_thread(struct snap_virtio_fs_ctrl *ctrl,
					    uint32_t thread_id)
{
	snap_virtio_ctrl_pg_io_progress(&ctrl->common, thread_id);
}
