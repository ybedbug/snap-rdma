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

#include <linux/virtio_ring.h>
#include <linux/virtio_blk.h>
#include <linux/virtio_pci.h>
#include "snap_channel.h"
#include "snap_virtio_blk_virtq.h"
#include "snap_dma.h"
#include "snap_env.h"
#include "snap_virtio_blk_ctrl.h"

#define NUM_HDR_FTR_DESCS 2

#define BDEV_SECTOR_SIZE 512
#define VIRTIO_NUM_DESC(seg_max) ((seg_max) + NUM_HDR_FTR_DESCS)

#define VIRTIO_BLK_SNAP_MERGE_DESCS "VIRTIO_BLK_SNAP_MERGE_DESCS"
SNAP_ENV_REG_ENV_VARIABLE(VIRTIO_BLK_SNAP_MERGE_DESCS, 1);

struct blk_virtq_priv;

/**
 * struct virtio_blk_outftr - footer of request, written to host memory
 */
struct virtio_blk_outftr {
	uint8_t status;
};

/**
 * enum virtq_cmd_sm_state - state of the sm handling a cmd
 * @VIRTQ_CMD_STATE_IDLE:	       SM initialization state
 * @VIRTQ_CMD_STATE_FETCH_CMD_DESCS:    SM received tunnel cmd and copied
 *				      immediate data, now fetch cmd descs
 * @VIRTQ_CMD_STATE_READ_REQ:	   Read request data from host memory
 * @VIRTQ_CMD_STATE_HANDLE_REQ:	 Handle received request from host, perform
 *				      READ/WRITE/FLUSH
 * @VIRTQ_CMD_STATE_T_OUT_IOV_DONE:     Finished writing to bdev, check write
 *				      status
 * @VIRTQ_CMD_STATE_T_IN_IOV_DONE:      Write data pulled from bdev to host memory
 * @VIRTQ_CMD_STATE_WRITE_STATUS:       Write cmd status to host memory
 * @VIRTQ_CMD_STATE_SEND_COMP:	  Send completion to FW
 * @VIRTQ_CMD_STATE_SEND_IN_ORDER_COMP: Send completion to FW for commands completed
 *				      unordered
 * @VIRTQ_CMD_STATE_RELEASE:	    Release command
 * @VIRTQ_CMD_STATE_FATAL_ERR:	  Fatal error, SM stuck here (until reset)
 */
enum virtq_cmd_sm_state {
	VIRTQ_CMD_STATE_IDLE,
	VIRTQ_CMD_STATE_FETCH_CMD_DESCS,
	VIRTQ_CMD_STATE_READ_HEADER,
	VIRTQ_CMD_STATE_PARSE_HEADER,
	VIRTQ_CMD_STATE_READ_DATA,
	VIRTQ_CMD_STATE_HANDLE_REQ,
	VIRTQ_CMD_STATE_T_OUT_IOV_DONE,
	VIRTQ_CMD_STATE_T_IN_IOV_DONE,
	VIRTQ_CMD_STATE_WRITE_STATUS,
	VIRTQ_CMD_STATE_SEND_COMP,
	VIRTQ_CMD_STATE_SEND_IN_ORDER_COMP,
	VIRTQ_CMD_STATE_RELEASE,
	VIRTQ_CMD_STATE_FATAL_ERR,
};

struct blk_virtq_cmd_aux {
	struct virtio_blk_outhdr header;
	struct vring_desc descs[0];
};

/**
 * struct blk_virtq_cmd - command context
 * @vq_priv:			virtqueue command belongs to, private context
 * @zcopy:			use ZCOPY
 * @iov:			command descriptors converted to the io vector
 * @iov_cnt:			number of io vectors in the command
 * @fake_iov:			fake io vector for zcopy usage
 * @fake_addr:			fake address for zcopy usage
 */
struct blk_virtq_cmd {
	struct virtq_cmd common_cmd;
	struct snap_bdev_io_done_ctx bdev_op_ctx;
	bool zcopy;
	struct iovec *iov;
	int iov_cnt;
	struct iovec *fake_iov;
	void *fake_addr;
	struct snap_blk_mempool_ctx dma_pool_ctx;
};

static inline struct snap_bdev_ops *to_blk_bdev_ops(struct virtq_bdev *bdev)
{
	return (struct snap_bdev_ops *)bdev->ops;
}

static inline struct blk_virtq_ctx *to_blk_virtq_ctx(struct virtq_common_ctx *vq_ctx)
{
	return (struct blk_virtq_ctx *)vq_ctx;
}

static inline struct blk_virtq_cmd_aux *to_blk_cmd_aux(void *aux)
{
	return (struct blk_virtq_cmd_aux *)aux;
}

static inline struct virtio_blk_outftr *to_blk_cmd_ftr(void *ftr)
{
	return (struct virtio_blk_outftr *)ftr;
}

static inline struct blk_virtq_cmd *to_blk_cmd_arr(struct virtq_cmd *cmd_arr)
{
	return (struct blk_virtq_cmd *)cmd_arr;
}

static inline void virtq_mark_dirty_mem(struct virtq_cmd *cmd, uint64_t pa,
					uint32_t len, bool is_completion)
{
	struct snap_virtio_ctrl_queue *vq = cmd->vq_priv->vbq;
	int rc;

	if (snap_likely(!vq->log_writes_to_host))
		return;

	if (is_completion) {
		/* spec 2.6 Split Virtqueues
		 * mark all of the device area as dirty, in the worst case
		 * it will cost an extra page or two. Device area size is
		 * calculated according to the spec.
		 */
		pa = cmd->vq_priv->vattr->device;
		len = 6 + 8 * cmd->vq_priv->vattr->size;
	}
	virtq_log_data(cmd, "MARK_DIRTY_MEM: pa 0x%lx len %u\n", pa, len);
	if (!vq->ctrl->lm_channel) {
		ERR_ON_CMD(cmd, "dirty memory logging enabled but migration channel is not present\n");
		return;
	}
	rc = snap_channel_mark_dirty_page(vq->ctrl->lm_channel, pa, len);
	if (rc)
		ERR_ON_CMD(cmd, "mark drity page failed: pa 0x%lx len %u\n", pa, len);
}

static int blk_virtq_cmd_progress(struct blk_virtq_cmd *cmd,
				  enum virtq_cmd_sm_op_status status);

static void sm_dma_cb(struct snap_dma_completion *self, int status)
{
	enum virtq_cmd_sm_op_status op_status = VIRTQ_CMD_SM_OP_OK;
	struct virtq_cmd *vcmd = container_of(self,
						 struct virtq_cmd,
						 dma_comp);
	struct blk_virtq_cmd *cmd = container_of(vcmd, struct blk_virtq_cmd, common_cmd);

	if (status != IBV_WC_SUCCESS) {
		snap_error("error in dma for queue %d\n",
			   cmd->common_cmd.vq_priv->vq_ctx->idx);
		op_status = VIRTQ_CMD_SM_OP_ERR;
	}
	blk_virtq_cmd_progress(cmd, op_status);
}

static void bdev_io_comp_cb(enum snap_bdev_op_status status, void *done_arg);

static inline struct snap_virtio_blk_ctrl*
to_blk_ctrl(struct snap_virtio_ctrl *vctrl)
{
	return container_of(vctrl, struct snap_virtio_blk_ctrl, common);
}

static void virtq_mem_ready(void *data, struct ibv_mr *mr, void *user);

void *blk_virtq_get_cmd_addr(void *ctx, void *ptr, size_t len)
{
	struct blk_virtq_cmd *cmd = ctx;
	void *addr = NULL;
	int i;

	for (i = 0; i < cmd->iov_cnt; i++) {
		void *fake_iov_base = cmd->fake_iov[i].iov_base;
		size_t iov_len = cmd->iov[i].iov_len;

		if (ptr >= fake_iov_base && ptr < fake_iov_base + iov_len) {
			size_t offset = ptr - fake_iov_base;
			void *iov_base = cmd->iov[i].iov_base;

			addr = iov_base + offset;
			len -= snap_min(iov_len - offset, len);
			break;
		}
	}

	if (snap_unlikely(len)) {
		snap_error("Couldn't translate all (left %lu bytes)\n", len);
		return NULL;
	}

	return addr;
}

struct snap_cross_mkey *blk_virtq_get_cross_mkey(void *ctx, struct ibv_pd *pd)
{
	struct blk_virtq_cmd *cmd = ctx;
	struct snap_virtio_blk_ctrl *ctrl = to_blk_ctrl(cmd->common_cmd.vq_priv->vbq->ctrl);

	if (snap_likely(ctrl->cross_mkey))
		return ctrl->cross_mkey;

	ctrl->cross_mkey = snap_create_cross_mkey(pd, cmd->common_cmd.vq_priv->vbq->ctrl->sdev);
	if (!ctrl->cross_mkey) {
		snap_error("Failed to create cross gvmi mkey\n");
		return NULL;
	}
	snap_info("Crossing mkey %p was created (key %u)\n",
		  ctrl->cross_mkey, ctrl->cross_mkey->mkey);

	return ctrl->cross_mkey;
}

static void blk_virtq_cmd_fill_addr(struct blk_virtq_cmd *cmd)
{
	struct snap_virtio_blk_ctrl *ctrl = to_blk_ctrl(cmd->common_cmd.vq_priv->vbq->ctrl);
	int ctrlid = ctrl->idx;
	int reqid = cmd->common_cmd.idx;
	int qid = cmd->common_cmd.vq_priv->vbq->index;
	void *fake_addr_table = ctrl->zcopy_ctx->fake_addr_table;
	uintptr_t *request_table = ctrl->zcopy_ctx->request_table;
	size_t max_num_ctrl_req = VIRTIO_BLK_CTRL_NUM_VIRTQ_MAX *
				  VIRTIO_BLK_MAX_VIRTQ_SIZE;
	size_t global_req_idx;

	global_req_idx = ctrlid * max_num_ctrl_req +
			qid * VIRTIO_BLK_MAX_VIRTQ_SIZE + reqid;

	cmd->fake_addr = fake_addr_table +
			 global_req_idx * VIRTIO_BLK_MAX_REQ_DATA;

	request_table[global_req_idx] = (uintptr_t)cmd;

	snap_debug("fake_addr %p ctrlid %d qid %d reqid %d req_idx %lu\n",
		   cmd->fake_addr, ctrlid, qid, reqid, global_req_idx);
}

static int alloc_aux(struct blk_virtq_cmd *cmd, uint32_t seg_max)
{
	const size_t descs_size = VIRTIO_NUM_DESC(seg_max) * sizeof(struct vring_desc);
	const size_t aux_size = sizeof(struct blk_virtq_cmd_aux) + descs_size;

	cmd->common_cmd.aux = calloc(1, aux_size);
	if (!cmd->common_cmd.aux) {
		snap_error("failed to allocate aux memory for virtq %d\n", cmd->common_cmd.idx);
		return -ENOMEM;
	}

	cmd->common_cmd.aux_mr = ibv_reg_mr(cmd->common_cmd.vq_priv->pd, cmd->common_cmd.aux, aux_size,
					IBV_ACCESS_REMOTE_READ |
					IBV_ACCESS_REMOTE_WRITE |
					IBV_ACCESS_LOCAL_WRITE);
	if (!cmd->common_cmd.aux_mr) {
		snap_error("failed to register mr for virtq %d\n", cmd->common_cmd.idx);
		free(cmd->common_cmd.aux);
		return -1;
	}

	return 0;
}

static int init_blk_virtq_cmd(struct blk_virtq_cmd *cmd, int idx,
			      uint32_t size_max, uint32_t seg_max,
			      struct virtq_priv *vq_priv)
{
	int ret;
	const size_t req_size = size_max * seg_max;
	const size_t descs_size = VIRTIO_NUM_DESC(seg_max) * sizeof(struct vring_desc);
	const size_t aux_size = sizeof(struct blk_virtq_cmd_aux) + descs_size;

	cmd->common_cmd.idx = idx;
	cmd->common_cmd.vq_priv = vq_priv;
	cmd->common_cmd.dma_comp.func = sm_dma_cb;
	cmd->bdev_op_ctx.user_arg = cmd;
	cmd->bdev_op_ctx.cb = bdev_io_comp_cb;
	cmd->common_cmd.io_cmd_stat = NULL;
	cmd->common_cmd.cmd_available_index = 0;
	cmd->common_cmd.vq_priv->merge_descs = snap_env_getenv(VIRTIO_BLK_SNAP_MERGE_DESCS);
	cmd->common_cmd.ftr = calloc(1, sizeof(struct virtio_blk_outftr));
	if (!cmd->common_cmd.ftr) {
		snap_error("failed to allocate footer for virtq %d\n",
			   idx);
		return -ENOMEM;
	}
	if (vq_priv->zcopy) {
		if (req_size > VIRTIO_BLK_MAX_REQ_DATA) {
			snap_error("reached max command data size (%zu/%d)\n",
				   req_size, VIRTIO_BLK_MAX_REQ_DATA);
			return -ENOMEM;
		}

		cmd->iov = calloc(seg_max, sizeof(struct iovec));
		if (!cmd->iov) {
			snap_error("failed to allocate iov for virtq %d\n",
				   idx);
			return -ENOMEM;
		}

		cmd->fake_iov = calloc(seg_max, sizeof(struct iovec));
		if (!cmd->fake_iov) {
			snap_error("failed to allocate fake iov for virtq %d\n",
				   idx);
			ret = -ENOMEM;
			goto free_iov;
		}
		blk_virtq_cmd_fill_addr(cmd);
	}

	if (cmd->common_cmd.vq_priv->use_mem_pool) {
		ret = alloc_aux(cmd, seg_max);
		if (ret)
			goto free_fake_iov;

		cmd->dma_pool_ctx.ctx = vq_priv->blk_dev.ctx;
		cmd->dma_pool_ctx.user = cmd;
		cmd->dma_pool_ctx.callback = virtq_mem_ready;
	} else {
		cmd->common_cmd.req_size = req_size;
		cmd->common_cmd.buf = to_blk_bdev_ops(&vq_priv->blk_dev)->dma_malloc(req_size + aux_size);
		if (!cmd->common_cmd.buf) {
			snap_error("failed to allocate memory for virtq %d\n", idx);
			ret = -ENOMEM;
			goto free_fake_iov;
		}
		cmd->common_cmd.mr = ibv_reg_mr(vq_priv->pd, cmd->common_cmd.buf, req_size + aux_size,
						IBV_ACCESS_REMOTE_READ |
						IBV_ACCESS_REMOTE_WRITE |
						IBV_ACCESS_LOCAL_WRITE);
		if (!cmd->common_cmd.mr) {
			snap_error("failed to register mr for virtq %d\n", idx);
			ret = -1;
			goto free_cmd_buf;
		}

		cmd->common_cmd.aux = (struct blk_virtq_cmd_aux *)((uint8_t *)cmd->common_cmd.buf + req_size);
		cmd->common_cmd.aux_mr = cmd->common_cmd.mr;
	}

	return 0;

free_cmd_buf:
	to_blk_bdev_ops(&vq_priv->blk_dev)->dma_free(cmd->common_cmd.buf);
free_fake_iov:
	if (vq_priv->zcopy)
		free(cmd->fake_iov);
free_iov:
	if (vq_priv->zcopy)
		free(cmd->iov);
	return ret;
}

void free_blk_virtq_cmds(struct blk_virtq_cmd *cmd)
{
	if (cmd->common_cmd.vq_priv->use_mem_pool) {
		to_blk_bdev_ops(&cmd->common_cmd.vq_priv->blk_dev)->dma_pool_cancel(&cmd->dma_pool_ctx);
		ibv_dereg_mr(cmd->common_cmd.aux_mr);
		free(cmd->common_cmd.aux);
	} else {
		ibv_dereg_mr(cmd->common_cmd.mr);
		to_blk_bdev_ops(&cmd->common_cmd.vq_priv->blk_dev)->dma_free(cmd->common_cmd.buf);
	}

	if (cmd->common_cmd.vq_priv->zcopy) {
		free(cmd->fake_iov);
		free(cmd->iov);
	}
}

/**
 * alloc_blk_virtq_cmd_arr() - allocate memory for commands received from host
 * @size_max:	VIRTIO_BLK_F_SIZE_MAX (from virtio spec)
 * @seg_max:	VIRTIO_BLK_F_SEG_MAX (from virtio spec)
 * @vq_priv:	Block virtq private context
 *
 * Memory is allocated for command metadata, descriptors and request data.
 * Request data memory should be allocated such that it can be transfered
 * via RDMA queues and written/read to block device. Descriptors memory should
 * be allocated such that it can be written to by RDMA. Instead of registering
 * another memory region for completion allocate memory for completion mem at
 * end of the request buffer.
 * Note: for easy implementation there is a direct mapping between descr_head_idx
 * and command.
 * Todo: Unify memory into one block for all commands
 *
 * Return: Array of commands structs on success, NULL on error
 */
static struct blk_virtq_cmd *
alloc_blk_virtq_cmd_arr(uint32_t size_max, uint32_t seg_max,
			struct virtq_priv *vq_priv)
{
	int i, k, ret, num = vq_priv->vattr->size;
	struct blk_virtq_cmd *cmd_arr;

	cmd_arr = calloc(num, sizeof(struct blk_virtq_cmd));
	if (!cmd_arr) {
		snap_error("failed to allocate memory for blk_virtq commands\n");
		goto out;
	}

	for (i = 0; i < num; i++) {
		ret = init_blk_virtq_cmd(&cmd_arr[i], i, size_max, seg_max, vq_priv);
		if (ret) {
			for (k = 0; k < i; k++)
				free_blk_virtq_cmds(&cmd_arr[k]);
			goto free_mem;
		}
	}
	return cmd_arr;

free_mem:
	free(cmd_arr);
	snap_error("failed allocating commands for queue %d\n",
			  vq_priv->vq_ctx->idx);
out:
	return NULL;
}

static void free_blk_virtq_cmd_arr(struct virtq_priv *vq_priv)
{
	const size_t num_cmds = vq_priv->vattr->size;
	size_t i;
	struct blk_virtq_cmd *cmd_arr = to_blk_cmd_arr(vq_priv->cmd_arr);

	for (i = 0; i < num_cmds; i++)
		free_blk_virtq_cmds(&cmd_arr[i]);

	free(vq_priv->cmd_arr);
}

/**
 * enum virtq_fetch_desc_status - status of descriptors fetch process
 * @VIRTQ_FETCH_DESC_DONE:	All descriptors were fetched
 * @VIRTQ_FETCH_DESC_ERR:	Error while trying to fetch a descriptor
 * @VIRTQ_FETCH_DESC_READ:	An Asynchronous read for desc was called
 */
enum virtq_fetch_desc_status {
	VIRTQ_FETCH_DESC_DONE,
	VIRTQ_FETCH_DESC_ERR,
	VIRTQ_FETCH_DESC_READ,
};

static int virtq_alloc_desc_buf(struct virtq_cmd *cmd, size_t old_len, size_t len)
{
	int aux_size = len * sizeof(struct vring_desc) + sizeof(struct virtio_blk_outhdr);
	struct ibv_mr *new_aux_mr;
	struct blk_virtq_cmd_aux *new_aux  = malloc(aux_size);

	if (!new_aux) {
		snap_error("failed to dynamically allocate %lu bytes for command %d request\n",
				len, cmd->idx);
		goto err;
	}
	new_aux_mr = ibv_reg_mr(cmd->vq_priv->pd, new_aux, aux_size,
					IBV_ACCESS_REMOTE_READ |
					IBV_ACCESS_REMOTE_WRITE |
					IBV_ACCESS_LOCAL_WRITE);
	if (!new_aux_mr) {
		snap_error("failed to register mr for virtq %d\n", cmd->idx);
		free(new_aux);
		goto err;
	}
	memcpy(new_aux->descs, to_blk_cmd_aux(cmd->aux)->descs, old_len * sizeof(struct vring_desc));
	if (cmd->vq_priv->use_mem_pool) {
		//mem for aux was previously allocated with malloc
		ibv_dereg_mr(cmd->aux_mr);
		free(cmd->aux);
	}
	cmd->aux = new_aux;
	cmd->use_seg_dmem = true;
	cmd->aux_mr = new_aux_mr;

	return 0;

err:
	to_blk_cmd_ftr(cmd->ftr)->status = VIRTIO_BLK_S_IOERR;
	return -1;
}

/**
 * fetch_next_desc() - Fetches command descriptors from host memory
 * @cmd: command descriptors belongs to
 *
 * Function checks if there are descriptors that were not sent in the
 * tunnled command, and if so it reads them from host memory one by one.
 * Reading from host memory is done asynchronous
 *
 * Return: virtq_fetch_desc_status
 */
static enum virtq_fetch_desc_status fetch_next_desc(struct virtq_cmd *cmd)
{
	uint64_t srcaddr;
	uint16_t in_ring_desc_addr;
	int ret;

	if (cmd->num_desc == 0)
		in_ring_desc_addr = cmd->descr_head_idx % cmd->vq_priv->vattr->size;
	else if (to_blk_cmd_aux(cmd->aux)->descs[cmd->num_desc - 1].flags & VRING_DESC_F_NEXT)
		in_ring_desc_addr = to_blk_cmd_aux(cmd->aux)->descs[cmd->num_desc - 1].next;
	else
		return VIRTQ_FETCH_DESC_DONE;

	if (snap_unlikely(cmd->num_desc >=
			VIRTIO_NUM_DESC(cmd->vq_priv->seg_max))) {
		if (virtq_alloc_desc_buf(cmd, cmd->num_desc,
				cmd->vq_priv->vattr->size))
			return VIRTQ_FETCH_DESC_ERR;
	}

	srcaddr = cmd->vq_priv->vattr->desc +
		  in_ring_desc_addr * sizeof(struct vring_desc);
	cmd->dma_comp.count = 1;
	virtq_log_data(cmd, "READ_DESC: pa 0x%lx len %lu\n", srcaddr, sizeof(struct vring_desc));
	ret = snap_dma_q_read(cmd->vq_priv->dma_q, &to_blk_cmd_aux(cmd->aux)->descs[cmd->num_desc],
			sizeof(struct vring_desc), cmd->aux_mr->lkey, srcaddr,
			cmd->vq_priv->vattr->dma_mkey,
			&(cmd->dma_comp));
	if (ret)
		return VIRTQ_FETCH_DESC_ERR;
	cmd->num_desc++;
	return VIRTQ_FETCH_DESC_READ;
}

static void bdev_io_comp_cb(enum snap_bdev_op_status status, void *done_arg)
{
	struct blk_virtq_cmd *cmd = done_arg;
	enum virtq_cmd_sm_op_status op_status = VIRTQ_CMD_SM_OP_OK;

	if (snap_unlikely(status != SNAP_BDEV_OP_SUCCESS)) {
		snap_error("Failed iov completion!\n");
		op_status = VIRTQ_CMD_SM_OP_ERR;
		cmd->common_cmd.io_cmd_stat->fail++;
	} else
		cmd->common_cmd.io_cmd_stat->success++;

	blk_virtq_cmd_progress(cmd, op_status);
}


static inline void virtq_descs_to_iovec(struct blk_virtq_cmd *cmd)
{
	int i;

	for (i = 0; i < cmd->common_cmd.num_desc - NUM_HDR_FTR_DESCS; i++) {
		cmd->iov[i].iov_base = (void *)to_blk_cmd_aux(cmd->common_cmd.aux)->descs[i + 1].addr;
		cmd->iov[i].iov_len = to_blk_cmd_aux(cmd->common_cmd.aux)->descs[i + 1].len;

		virtq_log_data(&cmd->common_cmd, "pa 0x%llx len %u\n",
			       to_blk_cmd_aux(cmd->common_cmd.aux)->descs[i + 1].addr, to_blk_cmd_aux(cmd->common_cmd.aux)->descs[i + 1].len);
	}
	cmd->iov_cnt = cmd->common_cmd.num_desc - NUM_HDR_FTR_DESCS;
}

static inline bool zcopy_check(struct blk_virtq_cmd *cmd)
{
	struct virtq_priv *priv = cmd->common_cmd.vq_priv;

	if (!priv->zcopy)
		return false;

	if (cmd->common_cmd.num_desc == NUM_HDR_FTR_DESCS)
		return false;

	if (!to_blk_bdev_ops(&priv->blk_dev)->is_zcopy_aligned)
		return false;

	/* cannot use zcopy if the first data addr is not zcopy aligned */
	return to_blk_bdev_ops(&priv->blk_dev)->is_zcopy_aligned(priv->blk_dev.ctx,
						   (void *)to_blk_cmd_aux(cmd->common_cmd.aux)->descs[1].addr);
}

/**
 * sequential_data_descs_merge() - merge descriptors with sequential addresses and data
 * @cmd: Command being processed
 *
 * merge 2 data descriptors that are a continuation of one another into one,
 * as a result, following descriptors that are not merged will be moved in the data_desc array
 *
 * Return: number of descs after merge
 */
static size_t virtq_sequential_data_descs_merge(struct vring_desc *descs,
		size_t num_desc, uint32_t *num_merges)
{
	uint32_t merged_desc_num = num_desc;
	uint32_t merged_index = 1;
	uint32_t index_to_copy_to = 2;
	uint32_t i;
	*num_merges = 0;

	for (i = 2; i < num_desc - 1; i++) {
		if ((descs[i].addr == descs[merged_index].addr + descs[merged_index].len)
				&& ((descs[merged_index].flags & VRING_DESC_F_WRITE)
						== (descs[i].flags & VRING_DESC_F_WRITE))) {
			/* merge two descriptors */
			descs[merged_index].len += descs[i].len;
			descs[merged_index].next = descs[i].next;
			merged_desc_num--;
			(*num_merges)++;
		} else {
			if (i != index_to_copy_to)
				descs[index_to_copy_to] = descs[i];
			merged_index = index_to_copy_to;
			index_to_copy_to++;
		}
	}
	if (i != index_to_copy_to)
		descs[index_to_copy_to] = descs[i];
	return merged_desc_num;
}

/**
 * virtq_process_desc() - Handle descriptors received
 * @cmd: Command being processed
 *
 * extract header, data and footer to separate descriptors
 * (in case data is sent as part of header or footer descriptors)
 *
 * Return: number of descs after processing
 */
static size_t virtq_blk_process_desc(struct vring_desc *descs, size_t num_desc,
		uint32_t *num_merges, int merge_descs)
{
	uint32_t footer_len = sizeof(struct virtio_blk_outftr);
	uint32_t header_len = sizeof(struct virtio_blk_outhdr);

	if (snap_unlikely(num_desc < NUM_HDR_FTR_DESCS))
		return num_desc;

	if (snap_unlikely(descs[0].len != header_len)) {
		/* header desc contains data, move data and header to seperate desc */
		uint32_t i;

		for (i = num_desc; i > 0; i--) {
			descs[i].addr = descs[i - 1].addr;
			descs[i].len = descs[i - 1].len;
			descs[i].flags = descs[i - 1].flags;
		}
		descs[0].len = header_len;
		descs[1].addr = descs[1].addr + header_len;
		descs[1].len = descs[1].len - header_len;
		descs[num_desc - 1].flags |= VRING_DESC_F_NEXT;
		descs[num_desc - 1].next = num_desc;
		descs[num_desc].next = 0;
		num_desc++;
	} else if (snap_unlikely(descs[num_desc - 1].len != footer_len)) {
		/* footer desc contains data, move data and footer to seperate desc*/
		descs[num_desc - 1].len = descs[num_desc - 1].len - footer_len;
		descs[num_desc - 1].flags |= VRING_DESC_F_NEXT;
		descs[num_desc - 1].next = num_desc;
		descs[num_desc].addr = descs[num_desc - 1].addr
				+ (descs[num_desc - 1].len - footer_len);
		descs[num_desc].len = footer_len;
		descs[num_desc].flags = descs[num_desc - 1].flags;
		num_desc++;
	}
	if (merge_descs)
		num_desc = virtq_sequential_data_descs_merge(descs, num_desc, num_merges);

	return num_desc;
}

/**
 * sm_fetch_cmd_descs() - Fetch all of commands descs
 * @cmd: Command being processed
 * @status: Callback status
 *
 * Function collects all of the commands descriptors. Descriptors can be either
 * in the tunnel command itself, or in host memory.
 *
 * Return: True if state machine is moved to a new state synchronously (error
 * or all descs were fetched), false if the state transition will be done
 * asynchronously.
 */
static bool sm_fetch_cmd_descs(struct blk_virtq_cmd *cmd,
			       enum virtq_cmd_sm_op_status status)
{
	size_t i;
	enum virtq_fetch_desc_status ret;

	if (status != VIRTQ_CMD_SM_OP_OK) {
		ERR_ON_CMD(&cmd->common_cmd, "failed to fetch commands descs, dumping command without response\n");
		cmd->common_cmd.state = VIRTQ_CMD_STATE_FATAL_ERR;
		return true;
	}
	ret = fetch_next_desc(&cmd->common_cmd);
	if (ret == VIRTQ_FETCH_DESC_ERR) {
		ERR_ON_CMD(&cmd->common_cmd, "failed to RDMA READ desc from host\n");
		cmd->common_cmd.state = VIRTQ_CMD_STATE_FATAL_ERR;
		return true;
	} else if (ret == VIRTQ_FETCH_DESC_DONE) {
		cmd->common_cmd.num_desc = virtq_blk_process_desc(to_blk_cmd_aux(cmd->common_cmd.aux)->descs, cmd->common_cmd.num_desc,
					&cmd->common_cmd.num_merges, cmd->common_cmd.vq_priv->merge_descs);

		if (cmd->common_cmd.num_desc < NUM_HDR_FTR_DESCS) {
			ERR_ON_CMD(&cmd->common_cmd, "failed to fetch commands descs, dumping command without response\n");
			cmd->common_cmd.state = VIRTQ_CMD_STATE_FATAL_ERR;
			return true;
		}

		cmd->zcopy = zcopy_check(cmd);
		if (cmd->zcopy)
			virtq_descs_to_iovec(cmd);

		for (i = 1; i < cmd->common_cmd.num_desc - 1; i++)
			cmd->common_cmd.total_seg_len += to_blk_cmd_aux(cmd->common_cmd.aux)->descs[i].len;

		cmd->common_cmd.state = VIRTQ_CMD_STATE_READ_HEADER;

		return true;
	} else {
		return false;
	}
}

static int virtq_alloc_req_dbuf(struct blk_virtq_cmd *cmd, size_t len)
{
	int mr_access = 0;
	struct snap_relaxed_ordering_caps ro_caps = {};

	cmd->common_cmd.req_buf = to_blk_bdev_ops(&cmd->common_cmd.vq_priv->blk_dev)->dma_malloc(len);
	if (!cmd->common_cmd.req_buf) {
		snap_error("failed to dynamically allocate %lu bytes for command %d request\n",
			   len, cmd->common_cmd.idx);
		goto err;
	}

	mr_access = IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE |
		    IBV_ACCESS_LOCAL_WRITE;
	if (!snap_query_relaxed_ordering_caps(cmd->common_cmd.vq_priv->pd->context,
					      &ro_caps)) {
		if (ro_caps.relaxed_ordering_write &&
		    ro_caps.relaxed_ordering_read)
			mr_access |= IBV_ACCESS_RELAXED_ORDERING;
	} else
		snap_warn("Failed to query relaxed ordering caps\n");

	cmd->common_cmd.req_mr = ibv_reg_mr(cmd->common_cmd.vq_priv->pd, cmd->common_cmd.req_buf, len,
				 mr_access);
	if (!cmd->common_cmd.req_mr) {
		snap_error("failed to register mr for commmand %d\n", cmd->common_cmd.idx);
		goto free_buf;
	}
	cmd->common_cmd.use_dmem = true;
	return 0;

free_buf:
	cmd->common_cmd.req_mr = cmd->common_cmd.mr;
	free(cmd->common_cmd.req_buf);
err:
	cmd->common_cmd.req_buf = cmd->common_cmd.buf;
	to_blk_cmd_ftr(cmd->common_cmd.ftr)->status = VIRTIO_BLK_S_IOERR;
	cmd->common_cmd.state = VIRTQ_CMD_STATE_WRITE_STATUS;
	return -1;
}

static void virtq_rel_req_dbuf(struct blk_virtq_cmd *cmd)
{
	ibv_dereg_mr(cmd->common_cmd.req_mr);
	to_blk_bdev_ops(&cmd->common_cmd.vq_priv->blk_dev)->dma_free(cmd->common_cmd.req_buf);
	cmd->common_cmd.req_buf = cmd->common_cmd.buf;
	cmd->common_cmd.req_mr = cmd->common_cmd.mr;
	cmd->common_cmd.use_dmem = false;
}

/**
 * virtq_rel_req_desc() - release aux in case of extra segs received
 * @cmd: Command being processed
 *
 * In case extra mem was allocated to accommodate unexpected segments,
 * at release extra memory is freed, and aux size is returned to regular init size
 *
 * Return: True if state machine is moved to error state (alloc error),
 *  false otherwise
 */
static bool virtq_rel_req_desc(struct blk_virtq_cmd *cmd)
{
	bool repeat = false;

	ibv_dereg_mr(cmd->common_cmd.aux_mr);
	free(cmd->common_cmd.aux);
	if (cmd->common_cmd.vq_priv->use_mem_pool) {
		if (alloc_aux(cmd, cmd->common_cmd.vq_priv->seg_max)) {
			cmd->common_cmd.state = VIRTQ_CMD_STATE_FATAL_ERR;
			//alloc fail, move to error state
			repeat = true;
		}
	} else {
		cmd->common_cmd.aux = (struct blk_virtq_cmd_aux *)((uint8_t *)cmd->common_cmd.buf + cmd->common_cmd.req_size);
		cmd->common_cmd.aux_mr = cmd->common_cmd.mr;
	}
	cmd->common_cmd.use_seg_dmem = false;

	return repeat;
}

/**
 * virtq_read_header() - Read header from host
 * @cmd: Command being processed
 *
 * Return: True if state machine is moved synchronously to the new state
 * (error or no data to fetch) or false if the state transition will be
 * done asynchronously.
 */
static bool virtq_read_header(struct blk_virtq_cmd *cmd)
{
	int ret;
	struct virtq_priv *priv = cmd->common_cmd.vq_priv;

	virtq_log_data(&cmd->common_cmd, "READ_HEADER: pa 0x%llx len %u\n",
			to_blk_cmd_aux(cmd->common_cmd.aux)->descs[0].addr, to_blk_cmd_aux(cmd->common_cmd.aux)->descs[0].len);

	cmd->common_cmd.state = VIRTQ_CMD_STATE_PARSE_HEADER;

	cmd->common_cmd.dma_comp.count = 1;
	ret = snap_dma_q_read(priv->dma_q, &to_blk_cmd_aux(cmd->common_cmd.aux)->header,
		to_blk_cmd_aux(cmd->common_cmd.aux)->descs[0].len, cmd->common_cmd.aux_mr->lkey,
		to_blk_cmd_aux(cmd->common_cmd.aux)->descs[0].addr, priv->vattr->dma_mkey,
		&cmd->common_cmd.dma_comp);

	if (ret) {
		cmd->common_cmd.state = VIRTQ_CMD_STATE_FATAL_ERR;
		return true;
	}

	return false;
}

static void virtq_mem_ready(void *data, struct ibv_mr *mr, void *user)
{
	struct blk_virtq_cmd *cmd = user;

	cmd->common_cmd.req_buf = data;
	cmd->common_cmd.req_mr = mr;
	blk_virtq_cmd_progress(cmd, VIRTQ_CMD_SM_OP_OK);
}

/**
 * virtq_parse_header() - Parse received header
 * @cmd: Command being processed
 *
 * Return: True if state machine is moved synchronously to the new state
 * (error or no data to fetch) or false if the state transition will be
 * done asynchronously.
 */
static bool virtq_parse_header(struct blk_virtq_cmd *cmd,
					enum virtq_cmd_sm_op_status status)
{
	int rc;
	size_t req_len;

	if (status != VIRTQ_CMD_SM_OP_OK) {
		ERR_ON_CMD(&cmd->common_cmd, "failed to get header data, returning failure\n");
		to_blk_cmd_ftr(cmd->common_cmd.ftr)->status = VIRTIO_BLK_S_IOERR;
		cmd->common_cmd.state = VIRTQ_CMD_STATE_WRITE_STATUS;
		return true;
	}

	if (cmd->zcopy) {
		cmd->common_cmd.state = VIRTQ_CMD_STATE_HANDLE_REQ;
		return true;
	}

	switch (to_blk_cmd_aux(cmd->common_cmd.aux)->header.type) {
	case VIRTIO_BLK_T_OUT:
		req_len = cmd->common_cmd.total_seg_len;
		cmd->common_cmd.state = VIRTQ_CMD_STATE_READ_DATA;
		break;
	case VIRTIO_BLK_T_IN:
	case VIRTIO_BLK_T_GET_ID:
		req_len = cmd->common_cmd.total_seg_len;
		cmd->common_cmd.state = VIRTQ_CMD_STATE_HANDLE_REQ;
		break;
	default:
		req_len = 0;
		cmd->common_cmd.state = VIRTQ_CMD_STATE_HANDLE_REQ;
		break;
	}

	if (cmd->common_cmd.vq_priv->use_mem_pool) {

		if (!req_len)
			return true;

		rc = to_blk_bdev_ops(&cmd->common_cmd.vq_priv->blk_dev)->dma_pool_malloc(
					req_len, &cmd->dma_pool_ctx);

		if (rc) {
			ERR_ON_CMD(&cmd->common_cmd, "failed to allocate memory, returning failure\n");
			to_blk_cmd_ftr(cmd->common_cmd.ftr)->status = VIRTIO_BLK_S_IOERR;
			cmd->common_cmd.state = VIRTQ_CMD_STATE_WRITE_STATUS;
			return true;
		}

		return false;
	}

	if (snap_unlikely(cmd->common_cmd.total_seg_len > cmd->common_cmd.req_size)) {
		if (virtq_alloc_req_dbuf(cmd, cmd->common_cmd.total_seg_len))
			return true;
	}

	return true;
}

/**
 * virtq_read_req_from_host() - Read request from host
 * @cmd: Command being processed
 *
 * RDMA READ the command request data from host memory.
 * Error after requesting the first RDMA READ is fatal because we can't
 * cancel previous RDMA READ requests done for this command, and since
 * the failing RDMA READ will not return the completion counter will not get
 * to 0 and the callback for the previous RDMA READ requests will not return.
 *
 * Handles also cases in which request is bigger than maximum buffer, so that
 * drivers which don't support the VIRTIO_BLK_F_SIZE_MAX feature will not
 * crash
 * ToDo: add non-fatal error in case first read fails
 * Note: Last desc is always VRING_DESC_F_READ
 *
 * Return: True if state machine is moved synchronously to the new state
 * (error or no data to fetch) or false if the state transition will be
 * done asynchronously.
 */
static bool virtq_read_data(struct blk_virtq_cmd *cmd)
{
	struct virtq_priv *priv = cmd->common_cmd.vq_priv;
	size_t offset, i;
	int ret;

	cmd->common_cmd.state = VIRTQ_CMD_STATE_HANDLE_REQ;

	// Calculate number of descriptors we want to read
	cmd->common_cmd.dma_comp.count = 0;
	for (i = 1; i < cmd->common_cmd.num_desc - 1; i++) {
		if (to_blk_cmd_aux(cmd->common_cmd.aux)->descs[i].flags & VRING_DESC_F_WRITE)
			continue;
		cmd->common_cmd.dma_comp.count++;
	}

	// If we have nothing to read - move synchronously to
	// VIRTQ_CMD_STATE_HANDLE_REQ
	if (!cmd->common_cmd.dma_comp.count)
		return true;

	offset = 0;
	for (i = 1; i < cmd->common_cmd.num_desc - 1; i++) {
		if (to_blk_cmd_aux(cmd->common_cmd.aux)->descs[i].flags & VRING_DESC_F_WRITE)
			continue;

		virtq_log_data(&cmd->common_cmd, "READ_DATA: pa 0x%llx len %u\n",
				to_blk_cmd_aux(cmd->common_cmd.aux)->descs[i].addr, to_blk_cmd_aux(cmd->common_cmd.aux)->descs[i].len);
		ret = snap_dma_q_read(priv->dma_q, cmd->common_cmd.req_buf + offset,
				to_blk_cmd_aux(cmd->common_cmd.aux)->descs[i].len, cmd->common_cmd.req_mr->lkey, to_blk_cmd_aux(cmd->common_cmd.aux)->descs[i].addr,
				priv->vattr->dma_mkey, &cmd->common_cmd.dma_comp);
		if (ret) {
			cmd->common_cmd.state = VIRTQ_CMD_STATE_FATAL_ERR;
			return true;
		}
		offset += to_blk_cmd_aux(cmd->common_cmd.aux)->descs[i].len;
	}

	return false;
}

static inline void virtq_fill_fake_iov(struct blk_virtq_cmd *cmd)
{
	int i;

	/* The first iov_base is always the same */
	cmd->fake_iov[0].iov_base = cmd->fake_addr;
	cmd->fake_iov[0].iov_len = cmd->iov[0].iov_len;

	for (i = 1; i < cmd->iov_cnt; i++) {
		void *prev_iov_base = cmd->fake_iov[i - 1].iov_base;
		size_t prev_iov_len = cmd->fake_iov[i - 1].iov_len;

		cmd->fake_iov[i].iov_base = prev_iov_base + prev_iov_len;
		cmd->fake_iov[i].iov_len = cmd->iov[i].iov_len;
	}
}

/**
 * virtq_handle_req() - Handle received request from host
 * @cmd: Command being processed
 * @status: Callback status
 *
 * Perform commands operation (READ/WRITE/FLUSH) on backend block device.
 *
 * Return: True if state machine is moved synchronously to the new state
 * (error cases) or false if the state transition will be done asynchronously.
 */
static bool virtq_handle_req(struct blk_virtq_cmd *cmd,
			     enum virtq_cmd_sm_op_status status)
{
	struct virtq_bdev *bdev = &cmd->common_cmd.vq_priv->blk_dev;
	int ret, len;
	uint64_t num_blocks;
	uint32_t blk_size;
	const char *dev_name;
	uint64_t offset;

	if (status != VIRTQ_CMD_SM_OP_OK) {
		ERR_ON_CMD(&cmd->common_cmd, "failed to get request data, returning failure\n");
		to_blk_cmd_ftr(cmd->common_cmd.ftr)->status = VIRTIO_BLK_S_IOERR;
		cmd->common_cmd.state = VIRTQ_CMD_STATE_WRITE_STATUS;
		return true;
	}

	cmd->common_cmd.io_cmd_stat = NULL;
	switch (to_blk_cmd_aux(cmd->common_cmd.aux)->header.type) {
	case VIRTIO_BLK_T_OUT:
		cmd->common_cmd.io_cmd_stat = &(to_blk_virtq_ctx(cmd->common_cmd.vq_priv->vq_ctx)->io_stat.write);
		cmd->common_cmd.state = VIRTQ_CMD_STATE_T_OUT_IOV_DONE;
		offset = to_blk_cmd_aux(cmd->common_cmd.aux)->header.sector * BDEV_SECTOR_SIZE;
		if (cmd->zcopy) {
			virtq_fill_fake_iov(cmd);
			ret = to_blk_bdev_ops(bdev)->writev(bdev->ctx, cmd->fake_iov,
						cmd->iov_cnt, offset,
						cmd->common_cmd.total_seg_len,
						&cmd->bdev_op_ctx,
						cmd->common_cmd.vq_priv->pg_id);
		} else {
			ret = to_blk_bdev_ops(bdev)->write(bdev->ctx, cmd->common_cmd.req_buf,
					       offset, cmd->common_cmd.total_seg_len,
					       &cmd->bdev_op_ctx,
					       cmd->common_cmd.vq_priv->pg_id);
		}
		break;
	case VIRTIO_BLK_T_IN:
		cmd->common_cmd.io_cmd_stat = &(to_blk_virtq_ctx(cmd->common_cmd.vq_priv->vq_ctx)->io_stat.read);
		offset = to_blk_cmd_aux(cmd->common_cmd.aux)->header.sector * BDEV_SECTOR_SIZE;
		if (cmd->zcopy) {
			virtq_fill_fake_iov(cmd);
			cmd->common_cmd.total_in_len += cmd->common_cmd.total_seg_len;
			cmd->common_cmd.state = VIRTQ_CMD_STATE_WRITE_STATUS;
			ret = to_blk_bdev_ops(bdev)->readv(bdev->ctx, cmd->fake_iov,
					       cmd->iov_cnt, offset,
					       cmd->common_cmd.total_seg_len,
					       &cmd->bdev_op_ctx,
					       cmd->common_cmd.vq_priv->pg_id);
		} else {
			cmd->common_cmd.state = VIRTQ_CMD_STATE_T_IN_IOV_DONE;
			ret = to_blk_bdev_ops(bdev)->read(bdev->ctx, cmd->common_cmd.req_buf,
					      offset, cmd->common_cmd.total_seg_len,
					      &cmd->bdev_op_ctx,
					      cmd->common_cmd.vq_priv->pg_id);
		}
		break;
	case VIRTIO_BLK_T_FLUSH:
		cmd->common_cmd.io_cmd_stat = &(to_blk_virtq_ctx(cmd->common_cmd.vq_priv->vq_ctx)->io_stat.flush);
		if (to_blk_cmd_aux(cmd->common_cmd.aux)->header.sector != 0) {
			ERR_ON_CMD(&cmd->common_cmd, "sector must be zero for flush command\n");
			ret = -1;
		} else {
			cmd->common_cmd.state = VIRTQ_CMD_STATE_WRITE_STATUS;
			num_blocks = to_blk_bdev_ops(bdev)->get_num_blocks(bdev->ctx);
			blk_size = to_blk_bdev_ops(bdev)->get_block_size(bdev->ctx);
			ret = to_blk_bdev_ops(bdev)->flush(bdev->ctx, 0,
					       num_blocks * blk_size,
					       &cmd->bdev_op_ctx, cmd->common_cmd.vq_priv->pg_id);
		}
		break;
	case VIRTIO_BLK_T_GET_ID:
		cmd->common_cmd.state = VIRTQ_CMD_STATE_WRITE_STATUS;
		dev_name = to_blk_bdev_ops(bdev)->get_bdev_name(bdev->ctx);
		ret = snprintf((char *)cmd->common_cmd.req_buf, cmd->common_cmd.req_size, "%s",
			       dev_name);
		if (ret < 0) {
			snap_error("failed to read block id\n");
			to_blk_cmd_ftr(cmd->common_cmd.ftr)->status = VIRTIO_BLK_S_UNSUPP;
			return true;
		}
		cmd->common_cmd.dma_comp.count = 1;
		len = snap_min(ret, to_blk_cmd_aux(cmd->common_cmd.aux)->descs[1].len);
		cmd->common_cmd.total_in_len += len;
		virtq_log_data(&cmd->common_cmd, "WRITE_DEVID: pa 0x%llx len %u\n",
				to_blk_cmd_aux(cmd->common_cmd.aux)->descs[1].addr, len);
		virtq_mark_dirty_mem(&cmd->common_cmd, to_blk_cmd_aux(cmd->common_cmd.aux)->descs[1].addr, len, false);
		ret = snap_dma_q_write(cmd->common_cmd.vq_priv->dma_q,
				       cmd->common_cmd.req_buf,
				       len,
				       cmd->common_cmd.req_mr->lkey,
				       to_blk_cmd_aux(cmd->common_cmd.aux)->descs[1].addr,
				       cmd->common_cmd.vq_priv->vattr->dma_mkey,
				       &(cmd->common_cmd.dma_comp));
		break;
	default:
		ERR_ON_CMD(&cmd->common_cmd, "invalid command - requested command type 0x%x is not implemented\n",
			   to_blk_cmd_aux(cmd->common_cmd.aux)->header.type);
		cmd->common_cmd.state = VIRTQ_CMD_STATE_WRITE_STATUS;
		to_blk_cmd_ftr(cmd->common_cmd.ftr)->status = VIRTIO_BLK_S_UNSUPP;
		return true;
	}

	if (cmd->common_cmd.io_cmd_stat) {
		cmd->common_cmd.io_cmd_stat->total++;
		if (ret)
			cmd->common_cmd.io_cmd_stat->fail++;
		if (cmd->common_cmd.vq_priv->merge_descs)
			cmd->common_cmd.io_cmd_stat->merged_desc += cmd->common_cmd.num_merges;
		if (cmd->common_cmd.use_dmem)
			cmd->common_cmd.io_cmd_stat->large_in_buf++;
		if (cmd->common_cmd.use_seg_dmem)
			cmd->common_cmd.io_cmd_stat->long_desc_chain++;
	}

	if (ret) {
		ERR_ON_CMD(&cmd->common_cmd, "failed while executing command %d\n",
			to_blk_cmd_aux(cmd->common_cmd.aux)->header.type);
		to_blk_cmd_ftr(cmd->common_cmd.ftr)->status = VIRTIO_BLK_S_IOERR;
		cmd->common_cmd.state = VIRTQ_CMD_STATE_WRITE_STATUS;
		return true;
	} else {
		return false;
	}
}

/**
 * sm_handle_in_iov_done() - write read data to host
 * @cmd: Command being processed
 * @status: Status of callback
 *
 * Return: True if state machine is moved synchronously to the new state
 * (error cases) or false if the state transition will be done asynchronously.
 */
static bool sm_handle_in_iov_done(struct blk_virtq_cmd *cmd,
				  enum virtq_cmd_sm_op_status status)
{
	int i, ret;
	size_t offset = 0;

	if (status != VIRTQ_CMD_SM_OP_OK) {
		ERR_ON_CMD(&cmd->common_cmd, "failed to read from block device, send ioerr to host\n");
		to_blk_cmd_ftr(cmd->common_cmd.ftr)->status = VIRTIO_BLK_S_IOERR;
		cmd->common_cmd.state = VIRTQ_CMD_STATE_WRITE_STATUS;
		return true;
	}

	cmd->common_cmd.dma_comp.count = cmd->common_cmd.num_desc - NUM_HDR_FTR_DESCS;
	cmd->common_cmd.state = VIRTQ_CMD_STATE_WRITE_STATUS;
	for (i = 0; i < cmd->common_cmd.num_desc - NUM_HDR_FTR_DESCS; i++) {
		virtq_log_data(&cmd->common_cmd, "WRITE_DATA: pa 0x%llx len %u\n",
			       to_blk_cmd_aux(cmd->common_cmd.aux)->descs[i + 1].addr, to_blk_cmd_aux(cmd->common_cmd.aux)->descs[i + 1].len);
		virtq_mark_dirty_mem(&cmd->common_cmd, to_blk_cmd_aux(cmd->common_cmd.aux)->descs[i + 1].addr,
				     to_blk_cmd_aux(cmd->common_cmd.aux)->descs[i + 1].len, false);
		ret = snap_dma_q_write(cmd->common_cmd.vq_priv->dma_q,
				       cmd->common_cmd.req_buf + offset,
				       to_blk_cmd_aux(cmd->common_cmd.aux)->descs[i + 1].len,
				       cmd->common_cmd.req_mr->lkey,
				       to_blk_cmd_aux(cmd->common_cmd.aux)->descs[i + 1].addr,
				       cmd->common_cmd.vq_priv->vattr->dma_mkey,
				       &(cmd->common_cmd.dma_comp));
		if (ret) {
			to_blk_cmd_ftr(cmd->common_cmd.ftr)->status = VIRTIO_BLK_S_IOERR;
			cmd->common_cmd.state = VIRTQ_CMD_STATE_WRITE_STATUS;
			return true;
		}
		offset += to_blk_cmd_aux(cmd->common_cmd.aux)->descs[i + 1].len;
		cmd->common_cmd.total_in_len += to_blk_cmd_aux(cmd->common_cmd.aux)->descs[i + 1].len;
	}
	return false;
}

/**
 * sm_handle_out_iov_done() - check write to bdev result status
 * @cmd:	command which requested the write
 * @status:	status of write operation
 */
static void sm_handle_out_iov_done(struct blk_virtq_cmd *cmd,
				   enum virtq_cmd_sm_op_status status)
{
	cmd->common_cmd.state = VIRTQ_CMD_STATE_WRITE_STATUS;
	if (status != VIRTQ_CMD_SM_OP_OK)
		to_blk_cmd_ftr(cmd->common_cmd.ftr)->status = VIRTIO_BLK_S_IOERR;
}

/**
 * sm_write_status() - Write command status to host memory upon finish
 * @cmd:	command which requested the write
 * @status:	callback status, expected 0 for no errors
 *
 * Return: True if state machine is moved synchronously to the new state
 * (error cases) or false if the state transition will be done asynchronously.
 */
static inline bool sm_write_status(struct blk_virtq_cmd *cmd,
				   enum virtq_cmd_sm_op_status status)
{
	int ret;

	if (snap_unlikely(status != VIRTQ_CMD_SM_OP_OK))
		to_blk_cmd_ftr(cmd->common_cmd.ftr)->status = VIRTIO_BLK_S_IOERR;

	virtq_log_data(&cmd->common_cmd, "WRITE_STATUS: pa 0x%llx len %lu\n",
		       to_blk_cmd_aux(cmd->common_cmd.aux)->descs[cmd->common_cmd.num_desc - 1].addr,
		       sizeof(struct virtio_blk_outftr));
	virtq_mark_dirty_mem(&cmd->common_cmd, to_blk_cmd_aux(cmd->common_cmd.aux)->descs[cmd->common_cmd.num_desc - 1].addr,
			     sizeof(struct virtio_blk_outftr), false);
	ret = snap_dma_q_write_short(cmd->common_cmd.vq_priv->dma_q, to_blk_cmd_ftr(cmd->common_cmd.ftr),
				     sizeof(struct virtio_blk_outftr),
				     to_blk_cmd_aux(cmd->common_cmd.aux)->descs[cmd->common_cmd.num_desc - 1].addr,
				     cmd->common_cmd.vq_priv->vattr->dma_mkey);
	if (snap_unlikely(ret)) {
		/* TODO: at some point we will have to do pending queue */
		ERR_ON_CMD(&cmd->common_cmd, "failed to send status, err=%d", ret);
		cmd->common_cmd.state = VIRTQ_CMD_STATE_FATAL_ERR;
		return true;
	}

	cmd->common_cmd.total_in_len += sizeof(struct virtio_blk_outftr);
	cmd->common_cmd.state = VIRTQ_CMD_STATE_SEND_COMP;
	return true;
}

/**
 * sm_send_completion() - send command completion to FW
 * @cmd: Command being processed
 * @status: Status of callback
 *
 * Return:
 * True if state machine is moved synchronously to the new state
 * (error cases) or false if the state transition will be done asynchronously.
 */
static inline int sm_send_completion(struct blk_virtq_cmd *cmd,
				     enum virtq_cmd_sm_op_status status)
{
	int ret;
	struct virtq_split_tunnel_comp tunnel_comp;
	bool unordered = false;

	if (snap_unlikely(status != VIRTQ_CMD_SM_OP_OK)) {
		snap_error("failed to write the request status field\n");

		/* TODO: if VIRTQ_CMD_STATE_FATAL_ERR could be recovered in the future,
		 * handle case when cmd with VIRTQ_CMD_STATE_FATAL_ERR handled unordered.
		 */
		cmd->common_cmd.state = VIRTQ_CMD_STATE_FATAL_ERR;
		return true;
	}

	if (snap_unlikely(cmd->common_cmd.cmd_available_index != cmd->common_cmd.vq_priv->ctrl_used_index)) {
		virtq_log_data(&cmd->common_cmd, "UNORD_COMP: cmd_idx:%d, in_num:%d, wait for in_num:%d\n",
			cmd->common_cmd.idx, cmd->common_cmd.cmd_available_index, cmd->common_cmd.vq_priv->ctrl_used_index);
		if (cmd->common_cmd.io_cmd_stat)
			++cmd->common_cmd.io_cmd_stat->unordered;
		unordered = true;
	}

	/* check order of completed command, if the command unordered - wait for
	 * other completions
	 */
	if (snap_unlikely(cmd->common_cmd.vq_priv->force_in_order) && snap_unlikely(unordered)) {
		cmd->common_cmd.state = VIRTQ_CMD_STATE_SEND_IN_ORDER_COMP;
		return false;
	}

	tunnel_comp.descr_head_idx = cmd->common_cmd.descr_head_idx;
	tunnel_comp.len = cmd->common_cmd.total_in_len;
	virtq_log_data(&cmd->common_cmd, "SEND_COMP: descr_head_idx %d len %d send_size %lu\n",
		       tunnel_comp.descr_head_idx, tunnel_comp.len,
		       sizeof(struct virtq_split_tunnel_comp));
	virtq_mark_dirty_mem(&cmd->common_cmd, 0, 0, true);
	ret = snap_dma_q_send_completion(cmd->common_cmd.vq_priv->dma_q,
					 &tunnel_comp,
					 sizeof(struct virtq_split_tunnel_comp));
	if (snap_unlikely(ret)) {
		/* TODO: pending queue */
		ERR_ON_CMD(&cmd->common_cmd, "failed to second completion\n");
		cmd->common_cmd.state = VIRTQ_CMD_STATE_FATAL_ERR;
	} else {
		cmd->common_cmd.state = VIRTQ_CMD_STATE_RELEASE;
		++cmd->common_cmd.vq_priv->ctrl_used_index;
	}

	return true;
}

static void virtq_rel_req_mempool_buf(struct blk_virtq_cmd *cmd)
{
	if (cmd->common_cmd.req_buf)
		to_blk_bdev_ops(&cmd->common_cmd.vq_priv->blk_dev)->dma_pool_free(&cmd->dma_pool_ctx, cmd->common_cmd.req_buf);
}

/**
 * blk_virtq_cmd_progress() - command state machine progress handle
 * @cmd:	commad to be processed
 * @status:	status of calling function (can be a callback)
 *
 * Return: 0 (Currently no option to fail)
 */
static int blk_virtq_cmd_progress(struct blk_virtq_cmd *cmd,
				  enum virtq_cmd_sm_op_status status)
{
	bool repeat = true;

	while (repeat) {
		repeat = false;
		snap_debug("virtq cmd sm state: %d\n", cmd->common_cmd.state);
		switch (cmd->common_cmd.state) {
		case VIRTQ_CMD_STATE_IDLE:
			snap_error("command in invalid state %d\n",
				   VIRTQ_CMD_STATE_IDLE);
			break;
		case VIRTQ_CMD_STATE_FETCH_CMD_DESCS:
			repeat = sm_fetch_cmd_descs(cmd, status);
			break;
		case VIRTQ_CMD_STATE_READ_HEADER:
			repeat = virtq_read_header(cmd);
			break;
		case VIRTQ_CMD_STATE_PARSE_HEADER:
			repeat = virtq_parse_header(cmd, status);
			break;
		case VIRTQ_CMD_STATE_READ_DATA:
			repeat = virtq_read_data(cmd);
			break;
		case VIRTQ_CMD_STATE_HANDLE_REQ:
			repeat = virtq_handle_req(cmd, status);
			break;
		case VIRTQ_CMD_STATE_T_IN_IOV_DONE:
			repeat = sm_handle_in_iov_done(cmd, status);
			break;
		case VIRTQ_CMD_STATE_T_OUT_IOV_DONE:
			sm_handle_out_iov_done(cmd, status);
			repeat = true;
			break;
		case VIRTQ_CMD_STATE_WRITE_STATUS:
			repeat = sm_write_status(cmd, status);
			break;
		case VIRTQ_CMD_STATE_SEND_COMP:
		case VIRTQ_CMD_STATE_SEND_IN_ORDER_COMP:
			repeat = sm_send_completion(cmd, status);
			break;
		case VIRTQ_CMD_STATE_RELEASE:
			if (snap_unlikely(cmd->common_cmd.use_dmem))
				virtq_rel_req_dbuf(cmd);
			if (cmd->common_cmd.vq_priv->use_mem_pool)
				virtq_rel_req_mempool_buf(cmd);
			if (snap_unlikely(cmd->common_cmd.use_seg_dmem))
				repeat = virtq_rel_req_desc(cmd);
			cmd->common_cmd.vq_priv->cmd_cntr--;
			break;
		case VIRTQ_CMD_STATE_FATAL_ERR:
			if (snap_unlikely(cmd->common_cmd.use_dmem))
				virtq_rel_req_dbuf(cmd);
			if (cmd->common_cmd.vq_priv->use_mem_pool)
				virtq_rel_req_mempool_buf(cmd);
			if (snap_unlikely(cmd->common_cmd.use_seg_dmem))
				virtq_rel_req_desc(cmd);
			cmd->common_cmd.vq_priv->vq_ctx->fatal_err = -1;
			/*
			 * TODO: propagate fatal error to the controller.
			 * At the moment attempt to resume/state copy
			 * of such controller will have unpredictable
			 * results.
			 */
			cmd->common_cmd.vq_priv->cmd_cntr--;
			break;
		default:
			snap_error("reached invalid state %d\n", cmd->common_cmd.state);
		}
	};

	return 0;
}

/**
 * blk_virtq_rx_cb() - callback for new command received from host
 * @q:		queue on which command was received
 * @data:	pointer to data sent for the command - should be
 *		command header and optional descriptor list
 * @data_len:	length of data
 * @imm_data:	immediate data
 *
 * Received command is assigned to a memory slot in the command array according
 * to descr_head_idx. Function starts the state machine processing for this command
 */
static void blk_virtq_rx_cb(struct snap_dma_q *q, void *data,
			    uint32_t data_len, uint32_t imm_data)
{
	struct virtq_priv *priv = (struct virtq_priv *)q->uctx;
	void *descs = data + sizeof(struct virtq_split_tunnel_req_hdr);
	enum virtq_cmd_sm_op_status status = VIRTQ_CMD_SM_OP_OK;
	int cmd_idx, len;
	struct blk_virtq_cmd *cmd;
	struct virtq_split_tunnel_req_hdr *split_hdr;

	split_hdr = (struct virtq_split_tunnel_req_hdr *)data;

	cmd_idx = priv->ctrl_available_index % priv->vattr->size;
	cmd = &to_blk_cmd_arr(priv->cmd_arr)[cmd_idx];
	cmd->common_cmd.num_desc = split_hdr->num_desc;
	cmd->common_cmd.descr_head_idx = split_hdr->descr_head_idx;
	cmd->common_cmd.total_seg_len = 0;
	cmd->common_cmd.total_in_len = 0;
	to_blk_cmd_ftr(cmd->common_cmd.ftr)->status = VIRTIO_BLK_S_OK;
	cmd->common_cmd.use_dmem = false;
	cmd->common_cmd.use_seg_dmem = false;
	cmd->common_cmd.req_buf = cmd->common_cmd.buf;
	cmd->common_cmd.req_mr = cmd->common_cmd.mr;
	cmd->dma_pool_ctx.thread_id = priv->thread_id;
	cmd->common_cmd.cmd_available_index = priv->ctrl_available_index;

	/* If new commands are not dropped there is a risk of never
	 * completing the flush
	 **/
	if (snap_unlikely(priv->swq_state == SW_VIRTQ_FLUSHING)) {
		virtq_log_data(&cmd->common_cmd, "DROP_CMD: %ld inline descs, rxlen %d\n",
			       cmd->common_cmd.num_desc, data_len);
		return;
	}

	if (split_hdr->num_desc) {
		len = sizeof(struct vring_desc) * split_hdr->num_desc;
		memcpy(to_blk_cmd_aux(cmd->common_cmd.aux)->descs, descs, len);
	}

	priv->cmd_cntr++;
	priv->ctrl_available_index++;
	cmd->common_cmd.state = VIRTQ_CMD_STATE_FETCH_CMD_DESCS;
	virtq_log_data(&cmd->common_cmd, "NEW_CMD: %lu inline descs, rxlen %u\n", cmd->common_cmd.num_desc,
		       data_len);
	blk_virtq_cmd_progress(cmd, status);
}


/**
 * blk_virtq_create() - Creates a new blk virtq object, along with RDMA QPs.
 * @vbq:	parent virt queue
 * @bdev_ops:	operations provided by bdev
 * @bdev:	Backend block device
 * @snap_dev:	Snap device on top virtq is created
 * @attr:	Configuration attributes
 *
 * Creates the snap queues, and RDMA queues. For RDMA queues
 * creates hw and sw qps, hw qps will be given to VIRTIO_BLK_Q.
 * Completion is sent inline, hence tx elem size is completion size
 * the rx queue size should match the number of possible descriptors
 * this in the worst case scenario is the VIRTQ size.
 *
 * Context: Calling function should attach the virtqueue to a polling group
 *
 * Return: NULL on failure, new block virtqueue context on success
 */
struct blk_virtq_ctx *blk_virtq_create(struct snap_virtio_blk_ctrl_queue *vbq,
				       struct snap_bdev_ops *bdev_ops,
				       void *bdev, struct snap_device *snap_dev,
				       struct virtq_create_attr *attr)
{
	struct snap_virtio_common_queue_attr qattr = {};
	struct blk_virtq_ctx *vq_ctx;
	struct snap_virtio_common_queue_attr *snap_attr;
	struct virtq_priv *vq_priv;
	struct snap_virtio_blk_queue *snap_vbq;
	int num_descs = VIRTIO_NUM_DESC(attr->seg_max);
	int rx_elem_size = sizeof(struct virtq_split_tunnel_req_hdr) +
				   num_descs * sizeof(struct vring_desc);

	vq_ctx = calloc(1, sizeof(struct blk_virtq_ctx));
	if (!vq_ctx)
		goto err;

	snap_attr = calloc(1, sizeof(struct snap_virtio_common_queue_attr));
	if (!snap_attr)
		goto release_ctx;
	if (!virtq_ctx_init(&vq_ctx->common_ctx, attr,
			     &snap_attr->vattr, &vbq->common,
			     bdev, rx_elem_size,
			     attr->seg_max + NUM_HDR_FTR_DESCS, blk_virtq_rx_cb))
		goto release_snap_attr;
	vq_priv = vq_ctx->common_ctx.priv;
	vq_priv->blk_dev.ops = bdev_ops;
	vq_priv->use_mem_pool = bdev_ops->dma_pool_enabled(vq_priv->blk_dev.ctx);
	if (bdev_ops->is_zcopy)
		vq_priv->zcopy = bdev_ops->is_zcopy(vq_priv->blk_dev.ctx);
	vq_priv->cmd_arr = (struct virtq_cmd *) alloc_blk_virtq_cmd_arr(attr->size_max,
						   attr->seg_max, vq_priv);
	if (!vq_priv->cmd_arr) {
		snap_error("failed allocating cmd list for queue %d\n",
			   attr->idx);
		goto release_priv;
	}

	snap_attr->hw_available_index = attr->hw_available_index;
	snap_attr->hw_used_index = attr->hw_used_index;
	snap_attr->qp = snap_dma_q_get_fw_qp(vq_priv->dma_q);
	if (!snap_attr->qp) {
		snap_error("no fw qp exist when trying to create virtq\n");
		goto dealloc_cmd_arr;
	}
	snap_vbq = snap_virtio_blk_create_queue(snap_dev, snap_attr);
	if (!snap_vbq) {
		snap_error("failed creating VIRTQ fw element\n");
		goto dealloc_cmd_arr;
	}
	vq_priv->snap_vbq = &snap_vbq->virtq;

	if (snap_virtio_blk_query_queue(snap_vbq, snap_attr)) {
		snap_error("failed query created snap virtio blk queue\n");
		goto destroy_virtio_blk_queue;
	}
	qattr.vattr.state = SNAP_VIRTQ_STATE_RDY;
	if (snap_virtio_blk_modify_queue(snap_vbq,
					 SNAP_VIRTIO_BLK_QUEUE_MOD_STATE,
					 &qattr)) {
		snap_error("failed to change virtq to READY state\n");
		goto destroy_virtio_blk_queue;
	}
	snap_debug("created VIRTQ %d succesfully in_order %d\n", attr->idx,
		   attr->force_in_order);
	return vq_ctx;

destroy_virtio_blk_queue:
	snap_virtio_blk_destroy_queue(snap_vbq);
dealloc_cmd_arr:
	free_blk_virtq_cmd_arr(vq_priv);
release_priv:
	virtq_ctx_destroy(vq_priv);
release_snap_attr:
	free(snap_attr);
release_ctx:
	free(vq_ctx);
err:
	snap_error("failed creating blk_virtq %d\n", attr->idx);
	return NULL;
}

/**
 * blk_virtq_destroy() - Destroyes blk virtq object
 * @q: queue to be destryoed
 *
 * Context: 1. Destroy should be called only when queue is in suspended state.
 *	    2. blk_virtq_progress() should not be called during destruction.
 *
 * Return: void
 */
void blk_virtq_destroy(struct blk_virtq_ctx *q)
{
	struct virtq_priv *vq_priv = q->common_ctx.priv;

	snap_debug("destroying queue %d\n", q->common_ctx.idx);

	if (vq_priv->swq_state != SW_VIRTQ_SUSPENDED && vq_priv->cmd_cntr)
		snap_warn("queue %d: destroying while not in the SUSPENDED state, %d commands outstanding\n",
			  q->common_ctx.idx, vq_priv->cmd_cntr);

	if (snap_virtio_blk_destroy_queue(to_blk_queue(vq_priv->snap_vbq)))
		snap_error("queue %d: error destroying blk_virtq\n", q->common_ctx.idx);

	free_blk_virtq_cmd_arr(vq_priv);
	virtq_ctx_destroy(vq_priv);
}

int blk_virtq_get_debugstat(struct blk_virtq_ctx *q,
			    struct snap_virtio_queue_debugstat *q_debugstat)
{
	struct virtq_priv *vq_priv = q->common_ctx.priv;
	struct snap_virtio_common_queue_attr virtq_attr = {};
	struct snap_virtio_queue_counters_attr vqc_attr = {};
	struct vring_avail vra;
	struct vring_used vru;
	uint64_t drv_addr = vq_priv->vattr->driver;
	uint64_t dev_addr = vq_priv->vattr->device;
	int ret;

	ret = snap_virtio_get_vring_indexes_from_host(vq_priv->pd, drv_addr, dev_addr,
						      vq_priv->vattr->dma_mkey,
						      &vra, &vru);
	if (ret) {
		snap_error("failed to get vring indexes from host memory for queue %d\n",
			   q->common_ctx.idx);
		return ret;
	}

	ret = snap_virtio_blk_query_queue(to_blk_queue(vq_priv->snap_vbq), &virtq_attr);
	if (ret) {
		snap_error("failed query queue %d debugstat\n", q->common_ctx.idx);
		return ret;
	}

	ret = snap_virtio_query_queue_counters(to_blk_queue(vq_priv->snap_vbq)->virtq.ctrs_obj, &vqc_attr);
	if (ret) {
		snap_error("failed query virtio_q_counters %d debugstat\n", q->common_ctx.idx);
		return ret;
	}

	q_debugstat->qid = q->common_ctx.idx;
	q_debugstat->hw_available_index = virtq_attr.hw_available_index;
	q_debugstat->sw_available_index = vra.idx;
	q_debugstat->hw_used_index = virtq_attr.hw_used_index;
	q_debugstat->sw_used_index = vru.idx;
	q_debugstat->hw_received_descs = vqc_attr.received_desc;
	q_debugstat->hw_completed_descs = vqc_attr.completed_desc;

	return 0;
}

int blk_virtq_query_error_state(struct blk_virtq_ctx *q,
				struct snap_virtio_common_queue_attr *attr)
{
	int ret;
	struct virtq_priv *vq_priv = q->common_ctx.priv;

	ret = snap_virtio_blk_query_queue(to_blk_queue(vq_priv->snap_vbq), attr);
	if (ret) {
		snap_error("failed query queue %d (update)\n", q->common_ctx.idx);
		return ret;
	}

	if (attr->vattr.state == SNAP_VIRTQ_STATE_ERR &&
		attr->vattr.error_type == SNAP_VIRTQ_ERROR_TYPE_NO_ERROR)
		snap_warn("queue %d state is in error but error type is 0\n", q->common_ctx.idx);

	if (attr->vattr.state != SNAP_VIRTQ_STATE_ERR &&
		attr->vattr.error_type != SNAP_VIRTQ_ERROR_TYPE_NO_ERROR) {
		snap_warn("queue %d state is not in error but with error type %d\n",
					q->common_ctx.idx, attr->vattr.error_type);
	}

	return 0;
}

static int blk_virtq_progress_suspend(struct blk_virtq_ctx *q)
{
	struct virtq_priv *priv = q->common_ctx.priv;
	struct snap_virtio_common_queue_attr qattr = {};

	/* TODO: add option to ignore commands in the bdev layer */
	if (priv->cmd_cntr != 0)
		return 0;

	snap_dma_q_flush(priv->dma_q);

	qattr.vattr.state = SNAP_VIRTQ_STATE_SUSPEND;
	/* TODO: check with FLR/reset. I see modify fail where it should not */
	if (snap_virtio_blk_modify_queue(to_blk_queue(priv->snap_vbq), SNAP_VIRTIO_BLK_QUEUE_MOD_STATE,
					 &qattr)) {
		snap_error("queue %d: failed to move to the SUSPENDED state\n", q->common_ctx.idx);
	}
	/* at this point QP is in the error state and cannot be used anymore */
	snap_info("queue %d: moving to the SUSPENDED state\n", q->common_ctx.idx);
	priv->swq_state = SW_VIRTQ_SUSPENDED;
	return 0;
}

/**
 * blk_virq_progress_unordered() - Check & complete unordered commands
 * @vq_priv:	queue to progress
 */
static void blk_virq_progress_unordered(struct virtq_priv *vq_priv)
{
	uint16_t cmd_idx = vq_priv->ctrl_used_index % vq_priv->vattr->size;
	struct blk_virtq_cmd *cmd = &to_blk_cmd_arr(vq_priv->cmd_arr)[cmd_idx];

	while (cmd->common_cmd.state == VIRTQ_CMD_STATE_SEND_IN_ORDER_COMP &&
	       cmd->common_cmd.cmd_available_index == cmd->common_cmd.vq_priv->ctrl_used_index) {
		virtq_log_data(&cmd->common_cmd, "PEND_COMP: ino_num:%d state:%d\n",
			       cmd->common_cmd.cmd_available_index, cmd->common_cmd.state);

		blk_virtq_cmd_progress(cmd, VIRTQ_CMD_SM_OP_OK);

		cmd_idx = vq_priv->ctrl_used_index % vq_priv->vattr->size;
		cmd = &to_blk_cmd_arr(vq_priv->cmd_arr)[cmd_idx];
	}
}

/**
 * blk_virtq_progress() - Progress RDMA QPs,  Polls on QPs CQs
 * @q:	queue to progress
 *
 * Context: Not thread safe
 *
 * Return: error code on failure, 0 on success
 */
int blk_virtq_progress(struct blk_virtq_ctx *q, int thread_id)
{
	struct virtq_priv *priv = q->common_ctx.priv;

	if (snap_unlikely(priv->swq_state == SW_VIRTQ_SUSPENDED))
		return 0;

	priv->thread_id = thread_id;
	snap_dma_q_progress(priv->dma_q);

	if (snap_unlikely(priv->force_in_order))
		blk_virq_progress_unordered(priv);

	/*
	 * need to wait until all inflight requests
	 * are finished before moving to the suspend state
	 */
	if (snap_unlikely(priv->swq_state == SW_VIRTQ_FLUSHING))
		return blk_virtq_progress_suspend(q);

	return 0;
}

/**
 * blk_virtq_suspend() - Request moving queue to suspend state
 * @q:	queue to move to suspend state
 *
 * When suspend is requested the queue stops receiving new commands
 * and moves to FLUSHING state. Once all commands already fetched are
 * finished, the queue moves to SUSPENDED state.
 *
 * Context: Function is not thread safe with regard to blk_virtq_progress
 * and blk_virtq_is_suspended. If called from a different thread than
 * thread calling progress/is_suspended then application must take care of
 * proper locking
 *
 * Return: 0 on success, else error code
 */
int blk_virtq_suspend(struct blk_virtq_ctx *q)
{
	struct virtq_priv *priv = q->common_ctx.priv;

	if (priv->swq_state != SW_VIRTQ_RUNNING) {
		snap_debug("queue %d: suspend was already requested\n",
			   q->common_ctx.idx);
		return -EBUSY;
	}

	snap_info("queue %d: SUSPENDING %d command(s) outstanding\n",
		  q->common_ctx.idx, priv->cmd_cntr);

	if (priv->vq_ctx->fatal_err)
		snap_warn("queue %d: fatal error. Resuming or live migration will not be possible\n",
			  q->common_ctx.idx);

	priv->swq_state = SW_VIRTQ_FLUSHING;
	return 0;
}

/**
 * blk_virtq_is_suspended() - api for checking if queue in suspended state
 * @q:		queue to check
 *
 * Context: Function is not thread safe with regard to blk_virtq_progress
 * and blk_virtq_suspend. If called from a different thread than
 * thread calling progress/suspend then application must take care of
 * proper locking
 *
 * Return: True when queue suspended, and False for not suspended
 */
bool blk_virtq_is_suspended(struct blk_virtq_ctx *q)
{
	struct virtq_priv *priv = q->common_ctx.priv;

	return priv->swq_state == SW_VIRTQ_SUSPENDED;
}

/**
 * blk_virtq_start() - set virtq attributes used for operating
 * @q:		queue to start
 * @attr:	attrs used to start the quue
 *
 * Function set attributes queue needs in order to operate.
 *
 * Return: void
 */
void blk_virtq_start(struct blk_virtq_ctx *q,
		     struct virtq_start_attr *attr)
{
	struct virtq_priv *priv = q->common_ctx.priv;

	priv->pg_id = attr->pg_id;
}

/**
 * blk_virtq_get_state() - get hw state of the queue
 * @q:      queue
 * @state:  queue state to fill
 *
 * The function fills hw_avail and hw_used indexes as seen by the controller.
 * Later the indexes can be used by the blk_virtq_create() to resume queue
 * operations.
 *
 * All other queue fields are already available in the emulation object.
 *
 * NOTE: caller should suspend queue's polling group when calling from different
 *       context.
 * Return: 0 on success, -errno on failure.
 *
 */
int blk_virtq_get_state(struct blk_virtq_ctx *q,
			struct snap_virtio_ctrl_queue_state *state)
{
	struct virtq_priv *priv = q->common_ctx.priv;
	struct snap_virtio_common_queue_attr attr = {};
	int ret;

	ret = snap_virtio_blk_query_queue(to_blk_queue(priv->snap_vbq), &attr);
	if (ret < 0) {
		snap_error("failed to query blk queue %d\n", q->common_ctx.idx);
		return ret;
	}

	/* Everything between ctrl_available_index and hw_available_index has
	 * not been touched by us. It means that the ordering still holds and
	 * it is safe to ask hw to replay all these descriptors when queue is
	 * created.
	 */
	state->hw_available_index = priv->ctrl_available_index;
	state->hw_used_index = attr.hw_used_index;
	return 0;
}

const struct snap_virtio_ctrl_queue_stats *
blk_virtq_get_io_stats(struct blk_virtq_ctx *q)
{
	struct virtq_priv *priv = q->common_ctx.priv;

	return &to_blk_virtq_ctx(priv->vq_ctx)->io_stat;
}

struct snap_dma_q *get_dma_q(struct blk_virtq_ctx *ctx)
{
	struct virtq_priv *vpriv = ctx->common_ctx.priv;

	return vpriv->dma_q;
}

int set_dma_mkey(struct blk_virtq_ctx *ctx, uint32_t mkey)
{
	struct virtq_priv *vpriv = ctx->common_ctx.priv;

	vpriv->vattr->dma_mkey = mkey;
	return 0;
}
