#include "snap_virtio_blk_ctrl.h"
#include <linux/virtio_blk.h>
#include <linux/virtio_config.h>
#include <sys/mman.h>


#define SNAP_VIRTIO_BLK_MODIFIABLE_FTRS ((1ULL << VIRTIO_F_VERSION_1) |\
					 (1ULL << VIRTIO_BLK_F_MQ) |\
					 (1ULL << VIRTIO_BLK_F_SIZE_MAX) |\
					 (1ULL << VIRTIO_BLK_F_SEG_MAX) |\
					 (1ULL << VIRTIO_BLK_F_BLK_SIZE))


static bool taken_cntlids[VIRTIO_BLK_MAX_CTRL_NUM];
LIST_HEAD(, snap_virtio_blk_ctrl_zcopy_ctx) snap_virtio_blk_ctrl_zcopy_ctx_list =
			LIST_HEAD_INITIALIZER(snap_virtio_blk_ctrl_zcopy_ctx);

static void snap_virtio_blk_zcopy_ctxs_clear()
{
	snap_virtio_blk_ctrl_zcopy_ctx_t *zcopy_ctx;

	while ((zcopy_ctx = LIST_FIRST(&snap_virtio_blk_ctrl_zcopy_ctx_list)) != NULL) {
		LIST_REMOVE(zcopy_ctx, entry);
		free(zcopy_ctx->request_table);
		munmap(zcopy_ctx->fake_addr_table, zcopy_ctx->fake_addr_table_size);
		free(zcopy_ctx);
	}
}

static snap_virtio_blk_ctrl_zcopy_ctx_t *
snap_virtio_blk_zcopy_ctx_init(struct snap_context *sctx)
{
	snap_virtio_blk_ctrl_zcopy_ctx_t *zcopy_ctx;
	size_t req_num;
	size_t size;

	zcopy_ctx = calloc(1, sizeof(*zcopy_ctx));
	if (!zcopy_ctx)
		return NULL;

	zcopy_ctx->sctx = sctx;

	req_num = VIRTIO_BLK_MAX_CTRL_NUM * VIRTIO_BLK_CTRL_NUM_VIRTQ_MAX *
	          VIRTIO_BLK_MAX_VIRTQ_SIZE;
	size = req_num * VIRTIO_BLK_MAX_REQ_DATA;

	zcopy_ctx->fake_addr_table = mmap(NULL, size,
					  PROT_NONE,
					  MAP_PRIVATE | MAP_ANONYMOUS,
					  -1, 0);
	if (zcopy_ctx->fake_addr_table == MAP_FAILED) {
		snap_error("mmap call failed: %m\n");
		goto free_zcopy_ctx;
	}
	zcopy_ctx->fake_addr_table_size = size;

	zcopy_ctx->request_table = calloc(req_num, sizeof(uintptr_t));
	if (!zcopy_ctx->request_table) {
		snap_error("failed to alloc request_table\n");
		goto unmap_fake_addr_table;
	}

	snap_info("Created fake_addr_table %p size %lu\n",
		  zcopy_ctx->fake_addr_table, size);

	LIST_INSERT_HEAD(&snap_virtio_blk_ctrl_zcopy_ctx_list, zcopy_ctx, entry);

	return zcopy_ctx;

unmap_fake_addr_table:
	munmap(zcopy_ctx->fake_addr_table, size);
free_zcopy_ctx:
	free(zcopy_ctx);
	return NULL;
}

static snap_virtio_blk_ctrl_zcopy_ctx_t *
snap_virtio_blk_get_zcopy_ctx(struct snap_context *sctx)
{
	snap_virtio_blk_ctrl_zcopy_ctx_t *tmp;
	snap_virtio_blk_ctrl_zcopy_ctx_t *zcopy_ctx = NULL;

	LIST_FOREACH(tmp, &snap_virtio_blk_ctrl_zcopy_ctx_list, entry) {
		if (tmp->sctx == sctx) {
			zcopy_ctx = tmp;
			break;
		}
	}

	if (!zcopy_ctx)
		zcopy_ctx = snap_virtio_blk_zcopy_ctx_init(sctx);

	return zcopy_ctx;
}

int snap_virtio_blk_ctrl_addr_trans(struct ibv_pd *pd, void *ptr, size_t len,
				    uint32_t *cross_mkey, void **addr)
{
	snap_virtio_blk_ctrl_zcopy_ctx_t *tmp;
	snap_virtio_blk_ctrl_zcopy_ctx_t *zcopy_ctx = NULL;
	struct blk_virtq_cmd *cmd;
	size_t req_idx;
	struct snap_cross_mkey *snap_cross_mkey;
	void *tmp_addr;

	if (snap_unlikely(!len))
		return 0;

	LIST_FOREACH(tmp, &snap_virtio_blk_ctrl_zcopy_ctx_list, entry) {
		if (ptr >= tmp->fake_addr_table &&
		    ptr + len <= tmp->fake_addr_table + tmp->fake_addr_table_size) {
			zcopy_ctx = tmp;
			break;
		}
	}

	if (!zcopy_ctx) {
		snap_debug("Address %p not in range\n", ptr);
		return -1;
	}

	req_idx = (uintptr_t)(ptr - zcopy_ctx->fake_addr_table) / VIRTIO_BLK_MAX_REQ_DATA;
	cmd = (void *)zcopy_ctx->request_table[req_idx];

	snap_cross_mkey = blk_virtq_get_cross_mkey(cmd, pd);
	if (snap_unlikely(!snap_cross_mkey))
		return -1;
	*cross_mkey = snap_cross_mkey->mkey;

	tmp_addr = blk_virtq_get_cmd_addr(cmd, ptr, len);
	if (snap_unlikely(!tmp_addr))
		return -1;
	*addr = tmp_addr;

	return 0;
}

static bool snap_virtio_blk_is_ctrlid_empty()
{
	int i;

	for (i = 0; i < VIRTIO_BLK_MAX_CTRL_NUM; i++)
		if (taken_cntlids[i])
			return false;

	return true;
}

static int snap_virtio_blk_acquire_cntlid()
{
	int i;

	for (i = 0; i < VIRTIO_BLK_MAX_CTRL_NUM; i++) {
		if (!taken_cntlids[i]) {
			taken_cntlids[i] = true;
			snap_debug("cntlid %d was acquired\n", i);
			return i;
		}
	}

	/* Not found */
	snap_error("Failed to find unused cntlid value\n");
	return -EINVAL;
}

static inline void snap_virtio_blk_release_cntlid(int cntlid)
{
	taken_cntlids[cntlid] = false;
	snap_debug("cntlid %d was released\n", cntlid);
}

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

static unsigned
snap_virtio_blk_ctrl_bar_get_state_size(struct snap_virtio_ctrl *ctrl)
{
	/* use block device config definition from linux/virtio_blk.h */
	return sizeof(struct virtio_blk_config);
}

static void
snap_virtio_blk_ctrl_bar_dump_state(struct snap_virtio_ctrl *ctrl, void *buf, int len)
{
	struct virtio_blk_config *dev_cfg;

	if (len < snap_virtio_blk_ctrl_bar_get_state_size(ctrl)) {
		snap_info(">>> blk_config: state is truncated (%d < %d)\n", len,
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
				   void *buf, unsigned len)
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
				   struct snap_virtio_ctrl_queue_state *queue_state,
				   void *buf, int len)
{
	struct snap_virtio_blk_device_attr *vbbar = to_blk_device_attr(vbar);
	struct virtio_blk_config *dev_cfg;
	int i, ret;

	if (!buf)
		return -EINVAL;

	if (len < snap_virtio_blk_ctrl_bar_get_state_size(ctrl))
		return -EINVAL;

	if(!queue_state)
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
	.queue_attr_valid = snap_virtio_blk_ctrl_bar_queue_attr_valid
};

static bool
snap_virtio_blk_ctrl_bar_setup_valid(struct snap_virtio_blk_ctrl *ctrl,
				     const struct snap_virtio_blk_device_attr *bar,
				     const struct snap_virtio_blk_registers *regs)
{
	bool ret = true;
	struct snap_virtio_blk_registers regs_whitelist = {};

	/* If only capacity is asked to be changed, allow it */
	regs_whitelist.capacity = regs->capacity;
	if (!memcmp(regs, &regs_whitelist, sizeof(regs_whitelist)))
		return true;

	if (regs->max_queues > ctrl->common.max_queues) {
		snap_error("Cannot create %d queues (max %lu)\n", regs->max_queues,
			   ctrl->common.max_queues);
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
	 * If max_queues was not initialized correctly on bar,
	 * and user didn't specify specific value for it, just
	 * use the maximal value possible
	 */
	if (!regs->max_queues) {
		if (bar.vattr.max_queues < 1 ||
		    bar.vattr.max_queues > ctrl->common.max_queues) {
			snap_warn("Invalid num_queues detected on bar. "
				  "Clamping down to max possible (%lu)\n",
				  ctrl->common.max_queues);
			regs->max_queues = ctrl->common.max_queues;
		}
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
	struct snap_virtio_blk_queue_attr *attr;
	struct snap_virtio_queue_attr *vattr;

	for (i = 0; i < ctrl->common.max_queues; i++) {
		vq = ctrl->common.queues[i];
		if (!vq)
			continue;

		vbq = to_blk_ctrl_q(vq);
		if (vbq->in_error)
			continue;

		attr = (struct snap_virtio_blk_queue_attr *)(void *)vbq->attr;
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
	for (i = 0; i < vbdev->num_queues; i++) {
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

	for (i = 0; i < ctrl->common.max_queues; i++) {
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

static int blk_virtq_create_helper(struct snap_virtio_blk_ctrl_queue *vbq,
				   struct snap_virtio_ctrl *vctrl, int index)
{
	struct blk_virtq_create_attr attr = {0};
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

	attr.hw_available_index = vbq->attr->hw_available_index;
	attr.hw_used_index = vbq->attr->hw_used_index;

	vbq->common.ctrl = vctrl;
	vbq->common.index = index;

	vbq->q_impl = blk_virtq_create(vbq, blk_ctrl->bdev_ops, blk_ctrl->bdev,
				       vctrl->sdev, &attr);
	if (!vbq->q_impl) {
		snap_error("controller failed to create blk virtq\n");
		return -EINVAL;
	}

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

	if (blk_virtq_create_helper(vbq, vctrl, index)) {
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
	struct snap_virtio_blk_device_attr *dev_attr;

	/* in the case of resume failure vbq->q_impl may be NULL */
	if (vbq->q_impl)
		blk_virtq_destroy(vbq->q_impl);

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

	blk_virtq_suspend(vbq->q_impl);
}

static bool snap_virtio_blk_ctrl_queue_is_suspended(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_virtio_blk_ctrl_queue *vbq = to_blk_ctrl_q(vq);

	if (!blk_virtq_is_suspended(vbq->q_impl))
		return false;

	snap_info("queue %d: pg_id %d SUSPENDED\n", vq->index, vq->pg->id);
	return true;
}

static int snap_virtio_blk_ctrl_queue_resume(struct snap_virtio_ctrl_queue *vq)
{
	struct snap_virtio_blk_ctrl_queue *vbq = to_blk_ctrl_q(vq);
	struct snap_virtio_ctrl_queue_state state = {};
	int ret, index;
	struct snap_virtio_blk_device_attr *dev_attr;
	struct snap_virtio_ctrl *ctrl;

	index = vq->index;
	ctrl = vq->ctrl;
	dev_attr = to_blk_device_attr(ctrl->bar_curr);

	/* if q_impl is NULL it means that we are resuming after
	 * the state restore
	 */
	if (vbq->q_impl) {
		if (!blk_virtq_is_suspended(vbq->q_impl))
			return -EINVAL;

		/* save hw_used and hw_avail to allow resume */
		ret = blk_virtq_get_state(vbq->q_impl, &state);
		if (ret) {
			snap_error("queue %d: failed to get state, cannot resume.\n",
					vq->index);
			return -EINVAL;
		}

		blk_virtq_destroy(vbq->q_impl);
		dev_attr->q_attrs[index].hw_available_index = state.hw_available_index;
		dev_attr->q_attrs[index].hw_used_index = state.hw_used_index;
	}

	ret = blk_virtq_create_helper(vbq, ctrl, index);
	if (ret)
		return ret;

	snap_info("queue %d: pg_id %d RESUMED with hw_avail %hu hw_used %hu\n",
		  vq->index, vq->pg->id,
		  dev_attr->q_attrs[index].hw_available_index,
		  dev_attr->q_attrs[index].hw_used_index);
	return 0;
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

static int snap_virtio_blk_ctrl_queue_get_state(struct snap_virtio_ctrl_queue *vq,
						struct snap_virtio_ctrl_queue_state *state)
{
	struct snap_virtio_blk_ctrl_queue *vbq = to_blk_ctrl_q(vq);

	return blk_virtq_get_state(vbq->q_impl, state);
}

static int snap_virtio_blk_ctrl_recover(struct snap_virtio_blk_ctrl *ctrl)
{
	int ret;
	struct snap_virtio_blk_device_attr blk_attr = {};

	snap_info("create controller in recover mode - ctrl=%p"
		  " max_queues=%ld enabled_queues=%ld \n",
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
	.get_state = snap_virtio_blk_ctrl_queue_get_state
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
		ret = snap_virtio_blk_ctrl_recover(ctrl);
		if (ret)
			goto teardown_dev;
	}

	if (bdev_ops->is_zcopy(bdev)) {
		ctrl->zcopy_ctx = snap_virtio_blk_get_zcopy_ctx(sctx);
		if (!ctrl->zcopy_ctx) {
			snap_error("Failed to get zcopy_ctx\n");
			errno = -ENOMEM;
			goto teardown_dev;
		}
		ctrl->idx = snap_virtio_blk_acquire_cntlid();
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
	if (ctrl->zcopy_ctx) {
		if (ctrl->cross_mkey) {
			snap_destroy_cross_mkey(ctrl->cross_mkey);
			ctrl->cross_mkey = NULL;
		}
		snap_virtio_blk_release_cntlid(ctrl->idx);

		if (snap_virtio_blk_is_ctrlid_empty())
			snap_virtio_blk_zcopy_ctxs_clear();
	}

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
