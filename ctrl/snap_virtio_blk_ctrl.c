#include "snap_virtio_blk_ctrl.h"
#include <linux/virtio_blk.h>
#include <linux/virtio_config.h>


#define SNAP_VIRTIO_BLK_MODIFIABLE_FTRS ((1ULL << VIRTIO_F_VERSION_1) |\
					 (1ULL << VIRTIO_BLK_F_MQ) |\
					 (1ULL << VIRTIO_BLK_F_SIZE_MAX) |\
					 (1ULL << VIRTIO_BLK_F_SEG_MAX) |\
					 (1ULL << VIRTIO_BLK_F_BLK_SIZE))


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
	vbbar->queues = vctrl->num_queues;
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

static int
snap_virtio_blk_ctrl_bar_add_status(struct snap_virtio_blk_ctrl *ctrl,
				    enum snap_virtio_common_device_status status)
{
	struct snap_virtio_blk_device_attr *bar;
	int ret = 0;

	bar = to_blk_device_attr(ctrl->common.bar_curr);
	if (!(bar->vattr.status & status)) {
		bar->vattr.status |= status;
		ret = snap_virtio_blk_modify_device(ctrl->common.sdev,
					     SNAP_VIRTIO_MOD_DEV_STATUS, bar);
	}

	return ret;
}

static struct snap_virtio_queue_attr*
snap_virtio_blk_ctrl_bar_get_queue_attr(struct snap_virtio_device_attr *vbar,
					int index)
{
	struct snap_virtio_blk_device_attr *vbbar = to_blk_device_attr(vbar);

	return &vbbar->q_attrs[index].vattr;
}

static struct snap_virtio_ctrl_bar_ops snap_virtio_blk_ctrl_bar_ops = {
	.create = snap_virtio_blk_ctrl_bar_create,
	.destroy = snap_virtio_blk_ctrl_bar_destroy,
	.copy = snap_virtio_blk_ctrl_bar_copy,
	.update = snap_virtio_blk_ctrl_bar_update,
	.modify = snap_virtio_blk_ctrl_bar_modify,
	.get_queue_attr = snap_virtio_blk_ctrl_bar_get_queue_attr,
};

static bool
snap_virtio_blk_ctrl_bar_setup_valid(struct snap_virtio_blk_ctrl *ctrl,
				     const struct snap_virtio_blk_device_attr *bar,
				     const struct snap_virtio_blk_registers *regs)
{
	const struct snap_virtio_caps *vblk_caps;
	bool ret = true;
	const struct snap_pci *spci = ctrl->common.sdev->pci;

	vblk_caps = &ctrl->common.sdev->sctx->virtio_blk_caps;

	if (regs->max_queues > vblk_caps->max_emulated_virtqs) {
		snap_error("Cannot create %d queues (max %d)\n", regs->max_queues,
			   vblk_caps->max_emulated_virtqs);
		return false;
	} else if (regs->max_queues > spci->pci_attr.num_msix - 1) {
		snap_error("No sufficient msix for %d queues (max %d)\n",
			   regs->max_queues, spci->pci_attr.num_msix - 1);
		return false;
	}

	/* Everything is configurable as long as driver is still down */
	if (snap_virtio_ctrl_is_stopped(&ctrl->common))
		return true;

	/* virtio_common_pci_config registers */
	if ((regs->device_features ^ bar->vattr.device_feature) &
	    SNAP_VIRTIO_BLK_MODIFIABLE_FTRS) {
		snap_error("Cant modify device_features, host driver is up\n");
		ret = false;
	}

	if (regs->max_queues &&
	    regs->max_queues != bar->vattr.max_queues) {
		snap_error("Cant modify max_queues, host driver is up\n");
		ret = false;
	}

	if (regs->queue_size &&
	    regs->queue_size != bar->vattr.max_queue_size) {
		snap_error("Cant modify queue_size, host driver is up\n");
		ret = false;
	}

	/* virtio_blk_config registers */
	if (regs->capacity < bar->capacity) {
		snap_error("Cant reduce capacity, host driver is up\n");
		ret = false;
	}

	if (regs->blk_size && regs->blk_size != bar->blk_size) {
		snap_error("Cant modify blk_size, host driver is up\n");
		ret = false;
	}

	if (regs->size_max && regs->size_max != bar->size_max) {
		snap_error("Cant modify size_max, host driver is up\n");
		ret = false;
	}

	if (regs->seg_max && regs->seg_max != bar->seg_max) {
		snap_error("Cant modify seg_max, host driver is up\n");
		ret = false;
	}

	return ret;
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
	const struct snap_pci *spci = ctrl->common.sdev->pci;

	/* Get last bar values as a reference */
	ret = snap_virtio_blk_query_device(ctrl->common.sdev, &bar);
	if (ret) {
		snap_error("Failed to query bar\n");
		return -EINVAL;
	}

	if (!snap_virtio_blk_ctrl_bar_setup_valid(ctrl, &bar, regs)) {
		snap_error("Setup is not valid\n");
		return -EINVAL;
	}

	/*
	 * if max_queues value wasn't configured on neither hotplug nor
	 * ctrl creation - just configure to max possible number
	 */
	if (!regs->max_queues && !bar.vattr.max_queues) {
		regs->max_queues = spci->pci_attr.num_msix - 1;
		snap_warn("No num_queues. Setting to max possible (%d)\n",
			  regs->max_queues);
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
		bar.vattr.max_queues = regs->max_queues ? :
				       bar.vattr.max_queues;
		if (regs->max_queues) {
			/*
			 * We always wish to keep blk queues and
			 * virtio queues values aligned
			 */
			extra_flags |= SNAP_VIRTIO_MOD_DEV_CFG;
			bar.max_blk_queues = regs->max_queues;
		}
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

static struct snap_virtio_ctrl_queue*
snap_virtio_blk_ctrl_queue_create(struct snap_virtio_ctrl *vctrl, int index)
{
	struct blk_virtq_create_attr attr = {0};
	struct snap_virtio_blk_ctrl *blk_ctrl = to_blk_ctrl(vctrl);
	struct snap_context *sctx = vctrl->sdev->sctx;
	struct snap_virtio_blk_ctrl_queue *vbq;
	struct snap_virtio_blk_device_attr *dev_attr;

	vbq = calloc(1, sizeof(*vbq));
	if (!vbq)
		return NULL;

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

	vbq->q_impl = blk_virtq_create(blk_ctrl->bdev_ops, blk_ctrl->bdev,
				       vctrl->sdev, &attr);
	if (!vbq->q_impl) {
		snap_error("controller failed to create blk virtq\n");
		free(vbq);
		return NULL;
	}

	return &vbq->common;
}

/**
 * snap_virtio_blk_ctrl_queue_destroy() - destroys and deletes queue
 * @vq: queue to destroy
 *
 * Function moves the queue to suspend state before destroying it.
 *
 * Context: Function assumes queue isnt progressed outside of its scope
 *
 * Return: void
 */
static void snap_virtio_blk_ctrl_queue_destroy(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_virtio_blk_ctrl_queue *vbq = to_blk_ctrl_q(vq);

	blk_virtq_destroy(vbq->q_impl);
	free(vbq);
}

static void snap_virtio_blk_ctrl_queue_suspend(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_virtio_blk_ctrl_queue *vbq = to_blk_ctrl_q(vq);

	blk_virtq_suspend(vbq->q_impl);
	while (!blk_virtq_is_suspended(vbq->q_impl)) {
		if (vq->pg->id == 0)
			blk_virtq_progress(vbq->q_impl);
		else
			usleep(1000);
	}
}

static void snap_virtio_blk_ctrl_queue_progress(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_virtio_blk_ctrl_queue *vbq = to_blk_ctrl_q(vq);

	blk_virtq_progress(vbq->q_impl);
}

static void snap_virtio_blk_ctrl_queue_start(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_virtio_blk_ctrl_queue *vbq = to_blk_ctrl_q(vq);
	struct blk_virtq_start_attr attr = {};

	attr.pg_id = vq->pg->id;
	blk_virtq_start(vbq->q_impl, &attr);
}

static struct snap_virtio_queue_ops snap_virtio_blk_queue_ops = {
	.create = snap_virtio_blk_ctrl_queue_create,
	.destroy = snap_virtio_blk_ctrl_queue_destroy,
	.progress = snap_virtio_blk_ctrl_queue_progress,
	.start = snap_virtio_blk_ctrl_queue_start,
	.suspend = snap_virtio_blk_ctrl_queue_suspend,
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
 *         errno will be set to indicate the failure reason.
 */
struct snap_virtio_blk_ctrl*
snap_virtio_blk_ctrl_open(struct snap_context *sctx,
			  struct snap_virtio_blk_ctrl_attr *attr,
			  struct snap_bdev_ops *bdev_ops,
			  void *bdev)
{
	struct snap_virtio_blk_ctrl *ctrl;
	int ret;

	ctrl = calloc(1, sizeof(*ctrl));
	if (!ctrl) {
		errno = ENOMEM;
		goto err;
	}

	ctrl->bdev_ops = bdev_ops;
	ctrl->bdev = bdev;

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

	ret = snap_virtio_blk_ctrl_bar_setup(ctrl, &attr->regs,
					     SNAP_VIRTIO_MOD_PCI_COMMON_CFG |
					     SNAP_VIRTIO_MOD_DEV_CFG);
	if (ret)
		goto teardown_dev;

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
	/* We must first notify host the device is no longer operational */
	snap_virtio_blk_ctrl_bar_add_status(ctrl,
				SNAP_VIRTIO_DEVICE_S_DEVICE_NEEDS_RESET);
	snap_virtio_ctrl_stop(&ctrl->common);
	snap_virtio_blk_teardown_device(ctrl->common.sdev);
	snap_virtio_ctrl_close(&ctrl->common);
	free(ctrl);
}

/**
 * snap_virtio_blk_ctrl_progress() - Handles control path changes in
 *                                   virtio-blk controller
 * @ctrl:       controller instance to handle
 *
 * Looks for control path status in virtio-blk controller and respond
 * to any identified changes (e.g. new enabled queues, changes in
 * device status, etc.)
 */
void snap_virtio_blk_ctrl_progress(struct snap_virtio_blk_ctrl *ctrl)
{
	snap_virtio_ctrl_progress(&ctrl->common);
}


/**
 * snap_virtio_blk_ctrl_io_progress() - single-threaded IO requests handling
 * @ctrl:       controller instance
 *
 * Looks for any IO requests from host recieved on any QPs, and handles
 * them based on the request's parameters.
 */
void snap_virtio_blk_ctrl_io_progress(struct snap_virtio_blk_ctrl *ctrl)
{
	snap_virtio_ctrl_io_progress(&ctrl->common);
}

/**
 * snap_virtio_blk_ctrl_io_progress_thread() - Handle IO requests for thread
 * @ctrl:       controller instance
 * @thread_id: 	id queues belong to
 *
 * Looks for any IO requests from host recieved on QPs which belong to thread
 * thread_id, and handles them based on the request's parameters.
 */
void snap_virtio_blk_ctrl_io_progress_thread(struct snap_virtio_blk_ctrl *ctrl,
					     uint32_t thread_id)
{
	snap_virtio_ctrl_pg_io_progress(&ctrl->common, thread_id);
}
