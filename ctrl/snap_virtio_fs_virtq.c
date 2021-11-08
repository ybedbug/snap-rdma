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
#include <linux/fuse.h>
#include <linux/virtio_pci.h>
#include "snap_virtio_fs_virtq.h"
#include "snap_virtio_fs_ctrl.h"
#include "snap_dma.h"
#include "snap_channel.h"
#include "snap_env.h"

#ifdef VIRTQ_DEBUG_DATA

#define fs_virtq_dump_fs_opcode(cmd) \
{\
	const struct fuse_in_header *req_hdr_p; \
	req_hdr_p =  (const struct fuse_in_header *)&to_fs_cmd_aux(cmd->aux)->header; \
	snap_debug("\t fuse opcode: %d\n", req_hdr_p->opcode); \
}
#else
#define fs_virtq_dump_fs_opcode(cmd)
#endif

/* uncomment to enable fuse format checks
 * note: the fuse device does the same checks,
 * so it can be skipped in snap code
 */
#define FS_VIRTIO_CHECK_FS_FORMAT

#ifdef FS_VIRTIO_CHECK_FS_FORMAT
#define FS_VIRTQ_CHECK_FS_REQ_FORMAT(cmd) fs_virtq_check_fs_req_format(cmd)
#else
#define FS_VIRTQ_CHECK_FS_REQ_FORMAT(cmd) true
#endif

/**
 * struct virtio_fs_outftr - footer of request, written to host memory
 */
struct virtio_fs_outftr {
	struct fuse_out_header out_header;
};

struct fs_virtq_cmd_aux {
	struct fuse_in_header header;
	// TODO check why header len of q0 is 64
	uint8_t resrv[24];
	struct vring_desc descs[0];
};

/**
 * struct fs_virtq_cmd - command context
 * @common_cmd:		virio common fields
 * @fs_dev_op_ctx:	fs device operations
 * @iov:		io vectors pointing to data to be written/read by fs device
 * @pos_f_write:	zero based position of first writable descriptor
 */
struct fs_virtq_cmd {
	struct virtq_cmd common_cmd;
	struct snap_fs_dev_io_done_ctx fs_dev_op_ctx;
	struct iovec *iov;
	int16_t pos_f_write;
};

static inline struct snap_fs_dev_ops *to_fs_dev_ops(struct virtq_bdev *dev)
{
	return (struct snap_fs_dev_ops *)dev->ops;
}

static inline struct fs_virtq_ctx *to_fs_virtq_ctx(struct virtq_common_ctx *vq_ctx)
{
	return (struct fs_virtq_ctx *)vq_ctx;
}

static inline struct fs_virtq_cmd_aux *to_fs_cmd_aux(void *aux)
{
	return (struct fs_virtq_cmd_aux *)aux;
}

static inline struct virtio_fs_outftr *to_fs_cmd_ftr(void *ftr)
{
	return (struct virtio_fs_outftr *)ftr;
}

static inline struct fs_virtq_cmd *to_fs_cmd_arr(struct virtq_cmd *cmd_arr)
{
	return (struct fs_virtq_cmd *)cmd_arr;
}

static inline struct fs_virtq_cmd *to_fs_virtq_cmd(struct virtq_cmd *cmd)
{
	return container_of(cmd, struct fs_virtq_cmd, common_cmd);
}

/**
 * struct fs_virtq_dev - fs device
 * @ctx:	Opaque fs device context given to fs device functions
 * @ops:	FS device operation pointers
 */
struct fs_virtq_dev {
	void *ctx;
	struct snap_fs_dev_ops *ops;
};

static inline void set_cmd_error(struct virtq_cmd *cmd, int error)
{
	memset(&to_fs_cmd_ftr(cmd->ftr)->out_header, 0,
				sizeof(to_fs_cmd_ftr(cmd->ftr)->out_header));
	// For more detail refer to fuse_lowlevel.c::fuse_reply_err
	to_fs_cmd_ftr(cmd->ftr)->out_header.error = -error;
}

static void fs_sm_dma_cb(struct snap_dma_completion *self, int status)
{
	enum virtq_cmd_sm_op_status op_status = VIRTQ_CMD_SM_OP_OK;
	struct virtq_cmd *vcmd = container_of(self, struct virtq_cmd, dma_comp);

	if (status != IBV_WC_SUCCESS) {
		snap_error("error in dma for queue: %d cmd %p descs %ld state %d\n",
			   vcmd->vq_priv->vq_ctx->idx,
			   vcmd, vcmd->num_desc, vcmd->state);
		op_status = VIRTQ_CMD_SM_OP_ERR;
	}
	--vcmd->vq_priv->cmd_cntrs.outstanding_to_host;
	virtq_cmd_progress(vcmd, op_status);
}

static void fs_dev_io_comp_cb(enum snap_fs_dev_op_status status, void *done_arg);


static int init_fs_virtq_cmd(struct fs_virtq_cmd *cmd, int idx,
			     uint32_t size_max, uint32_t seg_max,
			     struct virtq_priv *vq_priv)
{
	uint32_t n_descs = seg_max;
	const size_t req_size = size_max * seg_max;
	const size_t descs_size = n_descs * sizeof(struct vring_desc);
	const size_t aux_size = sizeof(struct fs_virtq_cmd_aux) + descs_size;
	uint32_t iovcnt = seg_max + 2 /* + in_header & out_header*/;
	int ret;

	cmd->common_cmd.idx = idx;
	cmd->common_cmd.vq_priv = vq_priv;
	cmd->common_cmd.dma_comp.func = fs_sm_dma_cb;
	cmd->fs_dev_op_ctx.user_arg = cmd;
	cmd->fs_dev_op_ctx.cb = fs_dev_io_comp_cb;
	cmd->common_cmd.cmd_available_index = 0;
	cmd->common_cmd.vq_priv->merge_descs = 0; // TODO
	cmd->common_cmd.ftr = calloc(1, sizeof(struct virtio_fs_outftr));
	if (!cmd->common_cmd.ftr) {
		snap_error("failed to allocate footer for virtq %d\n",
			   idx);
		return -ENOMEM;
	}
	cmd->iov = calloc(iovcnt, sizeof(struct iovec));
	if (!cmd->iov) {
		snap_error("failed to allocate iov for virtq %d cmd %d\n",
			   vq_priv->vq_ctx->idx, idx);
		return -ENOMEM;
	}

	if (cmd->common_cmd.vq_priv->use_mem_pool) {
		// TODO
	} else {
		cmd->common_cmd.req_size = req_size;

		cmd->common_cmd.buf = to_fs_dev_ops(&vq_priv->virtq_dev)->dma_malloc(req_size + aux_size);
		if (!cmd->common_cmd.buf) {
			snap_error("failed to allocate memory for virtq %d cmd %d\n",
				   vq_priv->vq_ctx->idx, idx);
			ret = -ENOMEM;
			goto free_iov;
		}

		cmd->common_cmd.mr = ibv_reg_mr(vq_priv->pd, cmd->common_cmd.buf, req_size + aux_size,
						IBV_ACCESS_REMOTE_READ |
						IBV_ACCESS_REMOTE_WRITE |
						IBV_ACCESS_LOCAL_WRITE);
		if (!cmd->common_cmd.mr) {
			snap_error("failed to register mr for virtq %d cmd %d\n",
				   vq_priv->vq_ctx->idx, idx);
			ret = -1;
			goto free_cmd_buf;
		}

		cmd->common_cmd.aux = (struct fs_virtq_cmd_aux *)((uint8_t *)cmd->common_cmd.buf + req_size);
		cmd->common_cmd.aux_mr = cmd->common_cmd.mr;
	}

	return 0;

free_cmd_buf:
	to_fs_dev_ops(&vq_priv->virtq_dev)->dma_free(cmd->common_cmd.buf);
free_iov:
	free(cmd->iov);

	return ret;
}

void free_fs_virtq_cmd(struct fs_virtq_cmd *cmd)
{
	if (cmd->common_cmd.vq_priv->use_mem_pool) {
		// TODO
	} else {
		ibv_dereg_mr(cmd->common_cmd.mr);
		to_fs_dev_ops(&cmd->common_cmd.vq_priv->virtq_dev)->dma_free(cmd->common_cmd.buf);
		free(cmd->iov);
	}
}

/**
 * alloc_fs_virtq_cmd_arr() - allocate memory for commands received from host
 * @size_max:	maximum size of any single segment
 * @seg_max:	maximum number of segments in a request
 * @vq_priv:	FS virtq private context
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
static struct fs_virtq_cmd *
alloc_fs_virtq_cmd_arr(uint32_t size_max, uint32_t seg_max,
		       struct virtq_priv *vq_priv)
{
	int i, k, ret, num = vq_priv->vattr->size;
	struct fs_virtq_cmd *cmd_arr;

	cmd_arr = calloc(num, sizeof(struct fs_virtq_cmd));
	if (!cmd_arr) {
		snap_error("failed to allocate memory for fs_virtq commands\n");
		goto out;
	}

	for (i = 0; i < num; i++) {
		ret = init_fs_virtq_cmd(&cmd_arr[i], i, size_max, seg_max, vq_priv);
		if (ret) {
			for (k = 0; k < i; ++k)
				free_fs_virtq_cmd(&cmd_arr[k]);
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

static void free_fs_virtq_cmd_arr(struct virtq_priv *vq_priv)
{
	const size_t num_cmds = vq_priv->vattr->size;
	size_t i;
	struct fs_virtq_cmd *cmd_arr = to_fs_cmd_arr(vq_priv->cmd_arr);

	for (i = 0; i < num_cmds; ++i)
		free_fs_virtq_cmd(&cmd_arr[i]);

	free(vq_priv->cmd_arr);
}

static void fs_dev_io_comp_cb(enum snap_fs_dev_op_status status, void *done_arg)
{
	struct fs_virtq_cmd *cmd = done_arg;
	enum virtq_cmd_sm_op_status op_status = VIRTQ_CMD_SM_OP_OK;

	if (snap_unlikely(status != SNAP_FS_DEV_OP_SUCCESS)) {
		snap_error("Failed iov completion!\n");
		op_status = VIRTQ_CMD_SM_OP_ERR;
	}

	--cmd->common_cmd.vq_priv->cmd_cntrs.outstanding_in_bdev;
	virtq_cmd_progress(&cmd->common_cmd, op_status);
}

/**
 * set_iovecs() - set iovec for fs device transactions
 * @cmd: command to which iov and descs belong to
 *
 * Access to virtio-fs device is done via iovecs. Function builds these iovecs to
 * transfer data to/from command buffers. Iovecs are created according
 * to the amount of descriptors given such that each iovec points to one
 * descriptor data. Relationship is iovec[i] points to desc[i].
 *
 * Note: the host should prepare request as described in
 * 5.11.6.1 - 'Device Operation: Request Queues'
 *
 * Return: 0 on success or EINVAL on error
 */
static int set_iovecs(struct fs_virtq_cmd *cmd)
{
	uint32_t offset = 0;
	int i, num_desc;
	struct fs_virtq_cmd_aux *cmd_aux = to_fs_cmd_aux(cmd->common_cmd.aux);

	// Device-readable part - fuse in header
	cmd->iov[0].iov_base = cmd->common_cmd.aux;
	cmd->iov[0].iov_len = cmd_aux->descs[0].len;

	num_desc = cmd->pos_f_write > 0 ? cmd->pos_f_write : cmd->common_cmd.num_desc;

	virtq_log_data(&cmd->common_cmd, "RH: iov[0] pa 0x%llx va %p, %ld\n",
			cmd_aux->descs[0].addr, cmd->iov[0].iov_base, cmd->iov[0].iov_len);

	// Device-readable part
	for (i = 1; i < num_desc; ++i) {
		cmd->iov[i].iov_base = cmd->common_cmd.req_buf + offset;
		cmd->iov[i].iov_len = cmd_aux->descs[i].len;
		offset += cmd_aux->descs[i].len;
		virtq_log_data(&cmd->common_cmd, "RD: iov[%d] pa 0x%llx va %p, %ld\n",
			       i, cmd_aux->descs[i].addr, cmd->iov[i].iov_base,
			       cmd->iov[i].iov_len);
	}

	if (snap_likely(cmd->pos_f_write > 0)) {
		// Device-writable part - fuse out header
		cmd->iov[cmd->pos_f_write].iov_base = cmd->common_cmd.ftr;
		cmd->iov[cmd->pos_f_write].iov_len = sizeof(struct virtio_fs_outftr);

		virtq_log_data(&cmd->common_cmd, "WH: iov[%d] pa 0x%llx va %p, %ld\n", i,
			       cmd_aux->descs[cmd->pos_f_write].addr,
			       cmd->iov[cmd->pos_f_write].iov_base,
			       cmd->iov[cmd->pos_f_write].iov_len);

		// Device-writable part
		for (i = cmd->pos_f_write + 1; i < cmd->common_cmd.num_desc; ++i) {
			cmd->iov[i].iov_base = cmd->common_cmd.req_buf + offset;
			cmd->iov[i].iov_len = cmd_aux->descs[i].len;
			offset += cmd_aux->descs[i].len;
			virtq_log_data(&cmd->common_cmd, "WD: iov[%d] pa 0x%llx va %p, %ld\n", i,
				       cmd_aux->descs[i].addr, cmd->iov[i].iov_base,
				       cmd->iov[i].iov_len);

		}
	}

	if (snap_unlikely(offset > cmd->common_cmd.req_size)) {
		ERR_ON_CMD(&cmd->common_cmd, "Increase cmd's buffer - offset: %d req_size: %d!\n",
			   offset, cmd->common_cmd.req_size);
		return -EINVAL;
	}

	return 0;
}

static int virtq_alloc_req_dbuf(struct fs_virtq_cmd *cmd, size_t len)
{
	int mr_access = 0, error;
	struct snap_relaxed_ordering_caps ro_caps = {};

	cmd->common_cmd.req_buf = to_fs_dev_ops(&cmd->common_cmd.vq_priv->virtq_dev)->dma_malloc(len);
	if (!cmd->common_cmd.req_buf) {
		snap_error("failed to dynamically allocate %lu bytes for command %d request\n",
			   len, cmd->common_cmd.idx);
		error = ENOMEM;
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
		snap_error("failed to register mr for virtq %d cmd %d\n",
			   cmd->common_cmd.vq_priv->vq_ctx->idx, cmd->common_cmd.idx);
		error = EINVAL;
		goto free_buf;
	}
	cmd->common_cmd.use_dmem = true;
	return 0;

free_buf:
	cmd->common_cmd.req_mr = cmd->common_cmd.mr;
	free(cmd->common_cmd.req_buf);
err:
	cmd->common_cmd.req_buf = cmd->common_cmd.buf;
	set_cmd_error(&cmd->common_cmd, error);
	cmd->common_cmd.state = VIRTQ_CMD_STATE_WRITE_STATUS;
	return -1;
}

/**
 * fs_virtq_sm_read_header() - Read header from host
 * @cmd: Command being processed
 *
 * Return: True if state machine is moved synchronously to the new state
 * (error or no data to fetch) or false if the state transition will be
 * done asynchronously.
 */
static bool fs_virtq_sm_read_header(struct virtq_cmd *cmd,
				    enum virtq_cmd_sm_op_status status)
{
	int ret;
	struct virtq_priv *priv = cmd->vq_priv;
	struct fs_virtq_cmd_aux *fs_aux = to_fs_cmd_aux(cmd->aux);

	virtq_log_data(cmd, "READ_HEADER: pa 0x%llx len %u\n",
			fs_aux->descs[0].addr, fs_aux->descs[0].len);

	cmd->state = VIRTQ_CMD_STATE_PARSE_HEADER;

	cmd->dma_comp.count = 1;
	ret = snap_dma_q_read(priv->dma_q, &fs_aux->header,
		fs_aux->descs[0].len, cmd->aux_mr->lkey,
		fs_aux->descs[0].addr, priv->vattr->dma_mkey,
		&cmd->dma_comp);

	if (ret) {
		cmd->state = VIRTQ_CMD_STATE_FATAL_ERR;
		return true;
	}

	++cmd->vq_priv->cmd_cntrs.outstanding_to_host;
	return false;
}

/**
 * fs_virtq_sm_parse_header() - Parse received header
 * @cmd: Command being processed
 *
 * Return: True if state machine is moved synchronously to the new state
 * (error or no data to fetch) or false if the state transition will be
 * done asynchronously.
 */
static bool fs_virtq_sm_parse_header(struct virtq_cmd *cmd,
				     enum virtq_cmd_sm_op_status status)
{

	if (status != VIRTQ_CMD_SM_OP_OK) {
		ERR_ON_CMD(cmd, "failed to get header data, returning failure\n");
		set_cmd_error(cmd, EINVAL);
		cmd->state = VIRTQ_CMD_STATE_WRITE_STATUS;
		return true;
	}

	cmd->state = VIRTQ_CMD_STATE_READ_DATA;
	if (snap_unlikely(cmd->vq_priv->use_mem_pool)) {
		// TODO
	} else {
		if (snap_unlikely(cmd->total_seg_len > cmd->req_size)) {
			if (virtq_alloc_req_dbuf(to_fs_virtq_cmd(cmd), cmd->total_seg_len))
				return true;
		}
	}

	return true;
}

__attribute__((unused)) static bool fs_virtq_check_fs_req_format(const struct fs_virtq_cmd *cmd);

static int fs_seg_dmem(struct virtq_cmd *cmd)
{
	/* Note: there is no seg_max configuration parameter for fs.
	 * Currently, the number of num_desc which used upon
	 * instantiation of the fs is equal to queue's size.
	 *
	 * TODO - tune the value of the num_desc
	 */

	return 0;
}


/**
 * fs_virtq_process_desc() - Handle descriptors received
 * @cmd: Command being processed
 *
 * extract header, data and footer to separate descriptors
 * (in case data is sent as part of header or footer descriptors)
 *
 * Return: number of descs after processing
 */
static size_t fs_virtq_process_desc(struct vring_desc *descs, size_t num_desc,
				    uint32_t *num_merges, int merge_descs)
{
	// TODO - merge descriptors
	return num_desc;
}

/**
 * fs_virtq_sm_read_data() - Read request from host
 * @cmd: Command being processed
 *
 * RDMA READ the command request data from host memory.
 * Error after requesting the first RDMA READ is fatal because we can't
 * cancel previous RDMA READ requests done for this command, and since
 * the failing RDMA READ will not return the completion counter will not get
 * to 0 and the callback for the previous RDMA READ requests will not return.
 *
 * Handles also cases in which request is bigger than maximum buffer.
 * ToDo: add non-fatal error in case first read fails
 * Note: Last desc is always VRING_DESC_F_READ
 *
 * Return: True if state machine is moved synchronously to the new state
 * (error or no data to fetch) or false if the state transition will be
 * done asynchronously.
 */
static bool fs_virtq_sm_read_data(struct virtq_cmd *cmd,
				  enum virtq_cmd_sm_op_status status)
{
	struct virtq_priv *priv = cmd->vq_priv;
	uint32_t offset, num_desc;
	int i, ret;
	struct fs_virtq_cmd_aux *cmd_aux = to_fs_cmd_aux(cmd->aux);
	struct fs_virtq_cmd *fs_cmd = to_fs_virtq_cmd(cmd);

	cmd->state = VIRTQ_CMD_STATE_HANDLE_REQ;

	// Calculate number of descriptors we want to read
	cmd->dma_comp.count = 0;
	num_desc = fs_cmd->pos_f_write > 0 ? fs_cmd->pos_f_write : cmd->num_desc;
	for (i = 1; i < num_desc; ++i) {
		if ((cmd_aux->descs[i].flags & VRING_DESC_F_WRITE) == 0)
			++cmd->dma_comp.count;
	}

	offset = 0;
	cmd->state = VIRTQ_CMD_STATE_HANDLE_REQ;

	// If we have nothing to read - move synchronously to
	// VIRTQ_CMD_STATE_HANDLE_REQ
	if (!cmd->dma_comp.count)
		return true;

	for (i = 1; i < num_desc; ++i) {
		virtq_log_data(cmd, "READ_DATA: pa 0x%llx va %p len %u\n",
			       cmd_aux->descs[i].addr, cmd->req_buf + offset,
			       cmd_aux->descs[i].len);
		ret = snap_dma_q_read(priv->dma_q, cmd->req_buf + offset,
				cmd_aux->descs[i].len, cmd->req_mr->lkey,
				cmd_aux->descs[i].addr,
				priv->vattr->dma_mkey, &cmd->dma_comp);
		if (ret) {
			cmd->state = VIRTQ_CMD_STATE_FATAL_ERR;
			return true;
		}
		offset += cmd_aux->descs[i].len;
	}

	++priv->cmd_cntrs.outstanding_to_host;
	return false;
}

/**
 * fs_virtq_handle_req() - Handle received request from host
 * @cmd: Command being processed
 * @status: Callback status
 *
 * Perform fuse operation (OPEN/READ/WRITE/FLUSH/etc.) on fs device.
 *
 * Return: True if state machine is moved synchronously to the new state
 * (error cases) or false if the state transition will be done asynchronously.
 */
static bool fs_virtq_handle_req(struct virtq_cmd *cmd,
				enum virtq_cmd_sm_op_status status)
{
	struct fs_virtq_dev *fs_dev = (struct fs_virtq_dev *)(&cmd->vq_priv->virtq_dev);
	struct fs_virtq_cmd *fs_cmd = to_fs_virtq_cmd(cmd);
	int ret;
	uint32_t r_descs, w_descs;

	if (status != VIRTQ_CMD_SM_OP_OK) {
		ERR_ON_CMD(cmd, "failed to get request data, returning failure\n");
		set_cmd_error(cmd, EINVAL);
		cmd->state = VIRTQ_CMD_STATE_WRITE_STATUS;
		return true;
	}

	fs_virtq_dump_fs_opcode(cmd);

	ret = set_iovecs(fs_cmd);
	if (ret) {
		cmd->state = VIRTQ_CMD_STATE_WRITE_STATUS;
		set_cmd_error(cmd, EINVAL);
		return true;
	}

	r_descs = fs_cmd->pos_f_write > 0 ? fs_cmd->pos_f_write : cmd->num_desc;
	w_descs = fs_cmd->pos_f_write > 0 ? cmd->num_desc - fs_cmd->pos_f_write : 0;

	/* Following is correct for request queue only (not for hiprio queue):
	 *
	 *	cmd->iov[0] - fs_virtq_cmd_aux
	 *	cmd->iov[1 ... cmd->pos_f_write - 1] device-readable part:
	 *		cmd->desc[1 ... ].flag & VRING_DESC_F_WRITE == 0
	 *	cmd->iov[cmd->pos_f_write] - virtio_fs_outftr
	 *	cmd->iov[cmd->pos_f_write + 1 ... cmd->common_cmd.num_desc] - device-writable part:
	 *		corresponded cmd->desc[1 ... ].flag & VRING_DESC_F_WRITE != 0
	 */
	ret = fs_dev->ops->handle_req(fs_dev->ctx, fs_cmd->iov,
				      r_descs,
				      (w_descs > 0) ? &fs_cmd->iov[fs_cmd->pos_f_write] : NULL,
				      w_descs,
				      &fs_cmd->fs_dev_op_ctx);

	if (ret) {
		ERR_ON_CMD(cmd, "failed while executing command\n");
		set_cmd_error(cmd, EIO);
		cmd->state = VIRTQ_CMD_STATE_WRITE_STATUS;
		return true;
	}

	++cmd->vq_priv->cmd_cntrs.outstanding_in_bdev;

	/* For request queues:
	 * Start handle the VRING_DESC_F_WRITE (writable) descriptors first.
	 * Writable, meaning the descriptor's data was 'filled' by fs device.
	 */
	if (snap_likely(cmd->vq_priv->vq_ctx->idx > 0))
		cmd->state = VIRTQ_CMD_STATE_IN_DATA_DONE;
	else {
		/* hiprio queue - do nothing, send tunneling completion only */
		virtq_log_data(cmd, "hiprio - send completion\n");
		cmd->state = VIRTQ_CMD_STATE_SEND_COMP;
	}

	--cmd->vq_priv->cmd_cntrs.outstanding_in_bdev;

	return true;
}

/**
 * sm_handle_in_iov_done() - write read data to host
 * @cmd: Command being processed
 * @status: Status of callback
 *
 * Return: True if state machine is moved synchronously to the new state
 * (error cases) or false if the state transition will be done asynchronously.
 */
static bool sm_handle_in_iov_done(struct virtq_cmd *cmd,
				  enum virtq_cmd_sm_op_status status)
{
	int i, ret;
	struct fs_virtq_cmd_aux *cmd_aux = to_fs_cmd_aux(cmd->aux);
	struct fs_virtq_cmd *fs_cmd = to_fs_virtq_cmd(cmd);

	if (status != VIRTQ_CMD_SM_OP_OK) {
		ERR_ON_CMD(cmd, "failed to read from block device, send ioerr to host\n");
		set_cmd_error(cmd, EIO);
		cmd->state = VIRTQ_CMD_STATE_WRITE_STATUS;
		return true;
	}

	cmd->state = VIRTQ_CMD_STATE_WRITE_STATUS;
	cmd->dma_comp.count = cmd->num_desc - (fs_cmd->pos_f_write + 1);
	if (snap_likely(cmd->dma_comp.count > 0)) {

		/* Note: the desc at position cmd->pos_f_write is desciptor for
		 * fuse_out_header status.
		 */
		for (i = fs_cmd->pos_f_write + 1; i < cmd->num_desc; ++i) {
			virtq_log_data(cmd, "WRITE_DATA: pa 0x%llx va %p len %u\n",
				       cmd_aux->descs[i].addr, fs_cmd->iov[i].iov_base,
				       cmd_aux->descs[i].len);
			virtq_mark_dirty_mem(cmd, cmd_aux->descs[i].addr,
					     cmd_aux->descs[i].len, false);
			ret = snap_dma_q_write(cmd->vq_priv->dma_q,
					       fs_cmd->iov[i].iov_base,
					       cmd_aux->descs[i].len,
					       cmd->req_mr->lkey,
					       cmd_aux->descs[i].addr,
					       cmd->vq_priv->vattr->dma_mkey,
					       &(cmd->dma_comp));
			if (ret) {
				set_cmd_error(cmd, -ret);
				cmd->state = VIRTQ_CMD_STATE_WRITE_STATUS;
				return true;
			}
			cmd->total_in_len += cmd_aux->descs[i].len;
		}

		++cmd->vq_priv->cmd_cntrs.outstanding_to_host;
		return false;
	}
	return true;
}

/**
 * sm_handle_out_iov_done() - check write to fs device result status
 * @cmd:	command which requested the write
 * @status:	status of write operation
 */
static bool sm_handle_out_iov_done(struct virtq_cmd *cmd,
				   enum virtq_cmd_sm_op_status status)
{
	cmd->state = VIRTQ_CMD_STATE_WRITE_STATUS;
	if (status != VIRTQ_CMD_SM_OP_OK)
		set_cmd_error(cmd, EIO);

	return true;
}

/**
 * fs_virtq_sm_write_status() - Write command status to host memory upon finish
 * @cmd:	command which requested the write
 * @status:	callback status, expected 0 for no errors
 *
 * Return: True if state machine is moved synchronously to the new state
 * (error cases) or false if the state transition will be done asynchronously.
 */
static inline bool fs_virtq_sm_write_status(struct virtq_cmd *cmd,
					    enum virtq_cmd_sm_op_status status)
{
	struct fs_virtq_cmd *fs_cmd = to_fs_virtq_cmd(cmd);

	if (snap_likely(fs_cmd->pos_f_write > 0))
		return virtq_sm_write_status(cmd, status);

	cmd->state = VIRTQ_CMD_STATE_SEND_COMP;
	return true;
}

/**
 * fs_virtq_rx_cb() - callback for new command received from host
 * @q:		queue on which command was received
 * @data:	pointer to data sent for the command - should be
 *		command header and optional descriptor list
 * @data_len:	length of data
 * @imm_data:	immediate data
 *
 * Received command is assigned to a memory slot in the command array according
 * to descr_head_idx. Function starts the state machine processing for this command
 */
static void fs_virtq_rx_cb(struct snap_dma_q *q, void *data,
			   uint32_t data_len, uint32_t imm_data)
{
	struct virtq_priv *priv = (struct virtq_priv *)q->uctx;
	struct virtq_cmd *cmd = virtq_rx_cb_common_set(priv, data);
	struct fs_virtq_cmd *fs_cmd = to_fs_virtq_cmd(cmd);

	fs_cmd->pos_f_write = 0;
	virtq_rx_cb_common_proc(cmd, data, data_len, imm_data);
}

static bool fs_virtq_check_fs_req_format(const struct fs_virtq_cmd *cmd)
{
	/* 5.11.2 Virtqueues
	 * 0 - hiprio
	 * 5.11.6.2 Device Operation: High Priority Queue
	 * for hiprio queue, the fuse format of requests is different.
	 * There are no writable desciptors !
	 */
	if (cmd->common_cmd.vq_priv->vq_ctx->idx > 0) {
		if (cmd->pos_f_write == cmd->common_cmd.num_desc) {
			ERR_ON_CMD(&cmd->common_cmd, "No writable desciptor found !\n");
			return false;
		}

		// First writable descriptor should point to virtio_fs_outftr
		if (snap_unlikely(to_fs_cmd_aux(cmd->common_cmd.aux)->descs[cmd->pos_f_write].len != sizeof(struct virtio_fs_outftr))) {
			ERR_ON_CMD(&cmd->common_cmd, "Unexpected len: %d in desc[%d] - expected %ld bytes !\n",
				   to_fs_cmd_aux(cmd->common_cmd.aux)->descs[cmd->pos_f_write].len,
				   cmd->pos_f_write, sizeof(struct virtio_fs_outftr));
			return false;
		}
	} else {
		if (snap_unlikely(to_fs_cmd_aux(cmd->common_cmd.aux)->descs[0].len > sizeof(struct fs_virtq_cmd_aux))) {
			ERR_ON_CMD(&cmd->common_cmd, "Unexpected len: %d of in header !\n",
				   to_fs_cmd_aux(cmd->common_cmd.aux)->descs[0].len);
			return false;
		}
		if (snap_unlikely(cmd->pos_f_write != 0)) {
			ERR_ON_CMD(&cmd->common_cmd, "Writable desciptor found !\n");
			return false;
		}
	}

	return true;
}

static struct vring_desc *fs_virtq_get_descs(struct virtq_cmd *cmd)
{
	return ((struct fs_virtq_cmd_aux *)cmd->aux)->descs;
}

static void fs_virtq_error_status(struct virtq_cmd *cmd)
{
	// TODO
	to_fs_cmd_ftr(cmd->ftr)->out_header.error = -EIO;
}

static void fs_virtq_clear_status(struct virtq_cmd *cmd)
{
	to_fs_cmd_ftr(cmd->ftr)->out_header.error = 0;
}

static void fs_virtq_status_data(struct virtq_cmd *cmd, struct virtq_status_data *sd)
{
	sd->us_status = to_fs_cmd_ftr(cmd->ftr);
	sd->status_size = sizeof(struct virtio_fs_outftr);
	sd->desc = to_fs_virtq_cmd(cmd)->pos_f_write;
}

static void fs_virtq_release_cmd(struct virtq_cmd *cmd)
{
	to_fs_dev_ops(&cmd->vq_priv->virtq_dev)->dma_free(cmd->req_buf);
}

static void fs_virtq_proc_desc(struct virtq_cmd *cmd)
{
	int i;
	struct fs_virtq_cmd *fs_cmd = to_fs_virtq_cmd(cmd);
	struct fs_virtq_cmd_aux *cmd_aux = to_fs_cmd_aux(cmd->aux);
	struct vring_desc *descs = fs_virtq_get_descs(cmd);

	for (i = 1; i < cmd->num_desc; i++) {
		snap_debug("\t desc[%d] --> pa 0x%llx len %d fl 0x%x F_WR %d\n", i,
			cmd_aux->descs[i].addr,
			cmd_aux->descs[i].len,
			cmd_aux->descs[i].flags,
			(cmd_aux->descs[i].flags & VRING_DESC_F_WRITE) ? 1 : 0);

		cmd->total_seg_len += cmd_aux->descs[i].len;
		if (cmd_aux->descs[i].flags & VRING_DESC_F_WRITE) {
			if (snap_unlikely(!fs_cmd->pos_f_write))
				fs_cmd->pos_f_write = i;
		}
	}

	if (!FS_VIRTQ_CHECK_FS_REQ_FORMAT(fs_cmd)) {
		set_cmd_error(cmd, EINVAL);
		cmd->state = VIRTQ_CMD_STATE_WRITE_STATUS;
		return;
	}

	cmd->num_desc = fs_virtq_process_desc(descs, cmd->num_desc,
					      &cmd->num_merges,
					      cmd->vq_priv->merge_descs);
}

static inline struct virtq_cmd*
fs_virtq_get_avail_cmd(struct virtq_cmd *cmd_arr, uint16_t idx)
{
	struct fs_virtq_cmd *fs_cmd_arr = to_fs_cmd_arr(cmd_arr);

	return &fs_cmd_arr[idx].common_cmd;
}

static int fs_progress_suspend(struct snap_virtio_queue *snap_vbq,
			       struct snap_virtio_common_queue_attr *qattr)
{
	/* TODO: check with FLR/reset. I see modify fail where it should not */
	return snap_virtio_fs_modify_queue(snap_vbq,
					   SNAP_VIRTIO_FS_QUEUE_MOD_STATE,
					   qattr);
}

static const struct virtq_impl_ops fs_impl_ops = {
	.get_descs		= fs_virtq_get_descs,
	.error_status		= fs_virtq_error_status,
	.clear_status		= fs_virtq_clear_status,
	.status_data		= fs_virtq_status_data,
	.release_cmd		= fs_virtq_release_cmd,
	.descs_processing	= fs_virtq_proc_desc,
	.get_avail_cmd		= fs_virtq_get_avail_cmd,
	.progress_suspend	= fs_progress_suspend,
	.mem_pool_release	= NULL,
	.seg_dmem		= fs_seg_dmem,
	.seg_dmem_release	= NULL,
	.send_comp = virtq_tunnel_send_comp,
};

//sm array states must be according to the order of virtq_cmd_sm_state
static struct virtq_sm_state fs_sm_arr[] = {
/*VIRTQ_CMD_STATE_IDLE			*/	{virtq_sm_idle},
/*VIRTQ_CMD_STATE_FETCH_CMD_DESCS	*/	{virtq_sm_fetch_cmd_descs},
/*VIRTQ_CMD_STATE_READ_HEADER		*/	{fs_virtq_sm_read_header},
/*VIRTQ_CMD_STATE_PARSE_HEADER		*/	{fs_virtq_sm_parse_header},
/*VIRTQ_CMD_STATE_READ_DATA		*/	{fs_virtq_sm_read_data},
/*VIRTQ_CMD_STATE_HANDLE_REQ		*/	{fs_virtq_handle_req},
/*VIRTQ_CMD_STATE_OUT_DATA_DONE		*/	{sm_handle_out_iov_done},
/*VIRTQ_CMD_STATE_IN_DATA_DONE		*/	{sm_handle_in_iov_done},
/*VIRTQ_CMD_STATE_WRITE_STATUS		*/	{fs_virtq_sm_write_status},
/*VIRTQ_CMD_STATE_SEND_COMP		*/	{virtq_sm_send_completion},
/*VIRTQ_CMD_STATE_SEND_IN_ORDER_COMP	*/	{virtq_sm_send_completion},
/*VIRTQ_CMD_STATE_RELEASE		*/	{virtq_sm_release},
/*VIRTQ_CMD_STATE_FATAL_ERR		*/	{virtq_sm_fatal_error},
};

struct virtq_state_machine fs_sm  = { fs_sm_arr, sizeof(fs_sm_arr) / sizeof(struct virtq_sm_state) };

/**
 * fs_virtq_create() - Creates a new fs virtq object, along with RDMA QPs.
 * @vfsq:	parent virt queue
 * @fs_dev_ops:	operations provided by fs device
 * @fs_dev:	fs device
 * @snap_dev:	Snap device on top virtq is created
 * @attr:	Configuration attributes
 *
 * Creates the snap queues, and RDMA queues. For RDMA queues
 * creates hw and sw qps, hw qps will be given to VIRTIO_FS_Q.
 * Completion is sent inline, hence tx elem size is completion size
 * the rx queue size should match the number of possible descriptors
 * this in the worst case scenario is the VIRTQ size.
 *
 * Context: Calling function should attach the virtqueue to a polling group
 *
 * Return: NULL on failure, new block virtqueue context on success
 */
struct fs_virtq_ctx *fs_virtq_create(struct snap_virtio_fs_ctrl_queue *vfsq,
				     struct snap_fs_dev_ops *fs_dev_ops,
				     void *fs_dev, struct snap_device *snap_dev,
				     struct virtq_create_attr *attr)
{
	struct snap_virtio_common_queue_attr qattr = {};
	struct fs_virtq_ctx *vq_ctx;
	struct virtq_priv *vq_priv;
	int num_descs = attr->seg_max;
	// The size will be used for RDMA send 'inline'
	int tx_elem_size = snap_max(sizeof(struct virtq_split_tunnel_comp),
					sizeof(struct virtio_fs_outftr));
	int rx_elem_size = sizeof(struct virtq_split_tunnel_req_hdr) +
			   num_descs * sizeof(struct vring_desc);
	struct virtq_ctx_init_attr ctx_attr = { .vq = &vfsq->common,
						.bdev = fs_dev,
						.tx_elem_size = tx_elem_size,
						.rx_elem_size = rx_elem_size,
						.max_tunnel_desc = attr->seg_max, // TODO - tune
						.cb = fs_virtq_rx_cb
					      };
	struct snap_virtio_fs_queue *fs_q;

	vq_ctx = calloc(1, sizeof(struct fs_virtq_ctx));
	if (!vq_ctx)
		goto err;

	if (!virtq_ctx_init(&vq_ctx->common_ctx, attr, &ctx_attr))
		goto release_ctx;

	vq_priv = vq_ctx->common_ctx.priv;
	vq_priv->custom_sm = &fs_sm;
	vq_priv->ops = &fs_impl_ops;
	vq_priv->virtq_dev.ops = fs_dev_ops;
	vq_priv->virtq_dev.ctx = fs_dev;
	vq_priv->use_mem_pool = 0;
	vq_priv->pd = attr->pd;

	vq_priv->cmd_arr = (struct virtq_cmd *)alloc_fs_virtq_cmd_arr(attr->size_max,
								      attr->seg_max, vq_priv);
	if (!vq_priv->cmd_arr) {
		snap_error("failed allocating cmd list for queue %d\n",
			   attr->idx);
		goto release_priv;
	}

	fs_q = to_fs_queue(snap_virtio_fs_create_queue(snap_dev, to_common_queue_attr(vq_priv->vattr)));
	if (!fs_q) {
		snap_error("failed creating VIRTQ fw element\n");
		goto dealloc_cmd_arr;
	}
	vq_priv->snap_vbq = &fs_q->virtq;
	if (snap_virtio_fs_query_queue(vq_priv->snap_vbq, to_common_queue_attr(vq_priv->vattr))) {
		snap_error("failed query created snap virtio fs queue\n");
		goto destroy_virtio_fs_queue;
	}
	qattr.vattr.state = SNAP_VIRTQ_STATE_RDY;
	if (snap_virtio_fs_modify_queue(vq_priv->snap_vbq, SNAP_VIRTIO_FS_QUEUE_MOD_STATE, &qattr)) {
		snap_error("failed to change virtq to READY state\n");
		goto destroy_virtio_fs_queue;
	}

	vq_priv->force_in_order = attr->force_in_order;
	snap_debug("created VIRTQ %d succesfully in_order %d\n", attr->idx,
		   attr->force_in_order);
	return vq_ctx;

destroy_virtio_fs_queue:
	snap_virtio_fs_destroy_queue(vq_priv->snap_vbq);
dealloc_cmd_arr:
	free_fs_virtq_cmd_arr(vq_priv);
release_priv:
	virtq_ctx_destroy(vq_priv);
release_ctx:
	free(vq_ctx);
err:
	snap_error("failed creating fs_virtq %d\n", attr->idx);
	return NULL;
}

/**
 * fs_virtq_destroy() - Destroyes fs virtq object
 * @q: queue to be destryoed
 *
 * Context: 1. Destroy should be called only when queue is in suspended state.
 *	    2. virtq_progress() should not be called during destruction.
 *
 * Return: void
 */
void fs_virtq_destroy(struct fs_virtq_ctx *q)
{
	struct virtq_priv *vq_priv = q->common_ctx.priv;

	snap_debug("destroying queue %d\n", q->common_ctx.idx);

	if (vq_priv->swq_state != SW_VIRTQ_SUSPENDED && vq_priv->cmd_cntrs.outstanding_total)
		snap_warn("queue %d: destroying while not in the SUSPENDED state, %d commands outstanding\n",
			  q->common_ctx.idx, vq_priv->cmd_cntrs.outstanding_total);

	if (vq_priv->cmd_cntrs.fatal)
		snap_warn("queue %d: destroying while %d commands completed with fatal error\n",
			  q->common_ctx.idx, vq_priv->cmd_cntrs.fatal);

	if (snap_virtio_fs_destroy_queue(vq_priv->snap_vbq))
		snap_error("queue %d: error destroying fs_virtq\n", q->common_ctx.idx);

	free_fs_virtq_cmd_arr(vq_priv);
	virtq_ctx_destroy(vq_priv);
}

int fs_virtq_get_debugstat(struct fs_virtq_ctx *q,
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
						      &vra, &vru
						     );
	if (ret) {
		snap_error("failed to get vring indexes from host memory for queue %d\n",
			   q->common_ctx.idx);
		return ret;
	}

	ret = snap_virtio_fs_query_queue(vq_priv->snap_vbq, &virtq_attr);
	if (ret) {
		snap_error("failed query queue %d debugstat\n", q->common_ctx.idx);
		return ret;
	}

	ret = snap_virtio_query_queue_counters(to_fs_queue(vq_priv->snap_vbq)->virtq.ctrs_obj, &vqc_attr);
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

int fs_virtq_query_error_state(struct fs_virtq_ctx *q,
			       struct snap_virtio_common_queue_attr *attr)
{
	int ret;
	struct virtq_priv *vq_priv = q->common_ctx.priv;

	ret = snap_virtio_fs_query_queue(vq_priv->snap_vbq, attr);
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

/**
 * fs_virtq_get_state() - get hw state of the queue
 * @q:      queue
 * @state:  queue state to fill
 *
 * The function fills hw_avail and hw_used indexes as seen by the controller.
 * Later the indexes can be used by the fs_virtq_create() to resume queue
 * operations.
 *
 * All other queue fields are already available in the emulation object.
 *
 * NOTE: caller should suspend queue's polling group when calling from different
 *       context.
 *
 * Return: 0 on success, -errno on failure.
 */
int fs_virtq_get_state(struct fs_virtq_ctx *q,
		       struct snap_virtio_ctrl_queue_state *state)
{
	struct virtq_priv *priv = q->common_ctx.priv;
	struct snap_virtio_common_queue_attr attr = {};
	int ret;

	ret = snap_virtio_fs_query_queue(priv->snap_vbq, &attr);
	if (ret < 0) {
		snap_error("failed to query fs queue %d\n", q->common_ctx.idx);
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

inline struct fs_virtq_ctx *to_fs_ctx(void *ctx)
{
	return (struct fs_virtq_ctx *)ctx;
}

struct snap_dma_q *fs_get_dma_q(struct fs_virtq_ctx *ctx)
{
	struct virtq_priv *vpriv = ctx->common_ctx.priv;

	return vpriv->dma_q;
}

int fs_set_dma_mkey(struct fs_virtq_ctx *ctx, uint32_t mkey)
{
	struct virtq_priv *vpriv = ctx->common_ctx.priv;

	vpriv->vattr->dma_mkey = mkey;
	return 0;
}
