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
	req_hdr_p =  (const struct fuse_in_header *)&cmd->aux->header; \
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

struct fs_virtq_priv;

/**
 * struct virtio_fs_outftr - footer of request, written to host memory
 */
struct virtio_fs_outftr {
	struct fuse_out_header out_header;
};

/**
 * enum fs_virtq_cmd_sm_state - state of the sm handling a cmd
 * @FS_VIRTQ_CMD_STATE_IDLE:			SM initialization state
 * @FS_VIRTQ_CMD_STATE_FETCH_CMD_DESCS:		SM received tunnel cmd and copied
 *						immediate data, now fetch cmd descs
 * @FS_VIRTQ_CMD_STATE_READ_REQ:		Read request data from host memory
 * @FS_VIRTQ_CMD_STATE_HANDLE_REQ:		Handle received request from host, perform
 *						fuse operation (open/read/write/etc.)
 * @FS_VIRTQ_CMD_STATE_OUT_IOV_DONE:		Finished writing to fs device, check write
 *						status
 * @FS_VIRTQ_CMD_STATE_IN_IOV_DONE:		Write data pulled from fs device to host memory
 * @FS_VIRTQ_CMD_STATE_WRITE_STATUS:		Write cmd status to host memory
 * @FS_VIRTQ_CMD_STATE_SEND_COMP:		Send completion to FW
 * @FS_VIRTQ_CMD_STATE_SEND_IN_ORDER_COMP:	Send completion to FW for commands completed
 *						unordered
 * @FS_VIRTQ_CMD_STATE_RELEASE:			Release command
 * @FS_VIRTQ_CMD_STATE_FATAL_ERR:		Fatal error, SM stuck here (until reset)
 */
enum fs_virtq_cmd_sm_state {
	FS_VIRTQ_CMD_STATE_IDLE,
	FS_VIRTQ_CMD_STATE_FETCH_CMD_DESCS,
	FS_VIRTQ_CMD_STATE_READ_REQ,
	FS_VIRTQ_CMD_STATE_HANDLE_REQ,
	FS_VIRTQ_CMD_STATE_OUT_IOV_DONE,
	FS_VIRTQ_CMD_STATE_IN_IOV_DONE,
	FS_VIRTQ_CMD_STATE_WRITE_STATUS,
	FS_VIRTQ_CMD_STATE_SEND_COMP,
	FS_VIRTQ_CMD_STATE_SEND_IN_ORDER_COMP,
	FS_VIRTQ_CMD_STATE_RELEASE,
	FS_VIRTQ_CMD_STATE_FATAL_ERR,
};

struct fs_virtq_cmd_aux {
	struct fuse_in_header header;
	// TODO check why header len of q0 is 64
	uint8_t resrv[24];
};

/**
 * struct fs_virtq_cmd - command context
 * @idx:			descr_head_idx modulo queue size
 * @descr_head_idx:		descriptor head index
 * @num_desc:			number of descriptors in the command
 * @vq_priv:			virtqueue command belongs to, private context
 * @state:			state of sm processing the command
 * @descs:			memory holding command descriptors
 * @buf:			buffer holding the request data and aux data
 * @req_size:			allocated request buffer size
 * @aux:			aux data resided in dma/mr memory
 * @mr:				buf mr
 * @req_buf:			pointer to request buffer
 * @req_mr:			request buffer mr
 * @dma_comp:			struct given to snap library
 * @total_seg_len:		total length (sum) of the request data to be written &read
 *				not including the fs_virtq_cmd_aux & virtio_fs_outftr headers
 * @total_in_len:		total length of data written to request buffers
 *				length of data filled by fs_device (inluding fuse_out_header)
 * @use_dmem:			command uses dynamic mem for req_buf
 * @cmd_available_index:	sequential number of the command according to arrival
 * @iov:			io vectors pointing to data to be written/read by fs device
 * @pos_f_write:		zero based position of first writable descriptor
 */
struct fs_virtq_cmd {
	int idx;
	uint16_t descr_head_idx;
	size_t num_desc;
	struct fs_virtq_priv *vq_priv;
	enum fs_virtq_cmd_sm_state state;
	struct vring_desc *descs;
	uint8_t *buf;
	uint32_t req_size;
	struct fs_virtq_cmd_aux *aux;
	struct ibv_mr *mr;
	uint8_t *req_buf;
	struct ibv_mr *req_mr;
	struct snap_dma_completion dma_comp;
	uint32_t total_seg_len;
	uint32_t total_in_len;
	struct snap_fs_dev_io_done_ctx fs_dev_op_ctx;
	bool use_dmem;
	uint16_t cmd_available_index;
	struct virtio_fs_outftr *fs_req_ftr;
	struct iovec *iov;
	int16_t pos_f_write;
};

/**
 * struct fs_virtq_dev - fs device
 * @ctx:	Opaque fs device context given to fs device functions
 * @ops:	FS device operation pointers
 */
struct fs_virtq_dev {
	void *ctx;
	struct snap_fs_dev_ops *ops;
};

struct fs_virtq_priv {
	volatile enum virtq_sw_state swq_state;
	struct fs_virtq_ctx vq_ctx;
	struct fs_virtq_dev fs_dev;
	struct ibv_pd *pd;
	struct snap_virtio_fs_queue *snap_vfsq;
	struct snap_virtio_fs_queue_attr snap_attr;
	struct snap_dma_q *dma_q;
	struct fs_virtq_cmd *cmd_arr;
	int cmd_cntr;
	int seg_max;
	int size_max;
	int pg_id;
	struct snap_virtio_fs_ctrl_queue *vfsq;
	uint16_t ctrl_available_index;
	bool force_in_order;
	/* current inorder value, for which completion should be sent */
	uint16_t ctrl_used_index;
};

static inline void set_cmd_error(struct fs_virtq_cmd *cmd, int error)
{
	memset(&cmd->fs_req_ftr->out_header, 0, sizeof(cmd->fs_req_ftr->out_header));
	// For more detail refer to fuse_lowlevel.c::fuse_reply_err
	cmd->fs_req_ftr->out_header.error = -error;
}

static inline void fs_virtq_mark_dirty_mem(struct fs_virtq_cmd *cmd, uint64_t pa,
					   uint32_t len, bool is_completion)
{
	struct snap_virtio_ctrl_queue *vq = &cmd->vq_priv->vfsq->common;
	int rc;

	if (snap_likely(!vq->log_writes_to_host))
		return;

	if (is_completion) {
		/* spec 2.6 Split Virtqueues
		 * mark all of the device area as dirty, in the worst case
		 * it will cost an extra page or two. Device area size is
		 * calculated according to the spec.
		 **/
		pa = cmd->vq_priv->snap_attr.vattr.device;
		len = 6 + 8 * cmd->vq_priv->snap_attr.vattr.size;
	}
	virtq_log_data(cmd, "MARK_DIRTY_MEM: pa 0x%lx len %u\n", pa, len);
	if (!vq->ctrl->lm_channel) {
		ERR_ON_CMD_FS(cmd, "dirty memory logging enabled but migration channel is not present\n");
		return;
	}
	rc = snap_channel_mark_dirty_page(vq->ctrl->lm_channel, pa, len);
	if (rc)
		ERR_ON_CMD_FS(cmd, "mark drity page failed: pa 0x%lx len %u\n", pa, len);
}

static int fs_virtq_cmd_progress(struct fs_virtq_cmd *cmd,
				 enum virtq_cmd_sm_op_status status);

static void fs_sm_dma_cb(struct snap_dma_completion *self, int status)
{
	enum virtq_cmd_sm_op_status op_status = VIRTQ_CMD_SM_OP_OK;
	struct fs_virtq_cmd *cmd = container_of(self,
						struct fs_virtq_cmd,
						dma_comp);

	if (status != IBV_WC_SUCCESS) {
		ERR_ON_CMD_FS(cmd, "error in dma for queue %d\n",
			   cmd->vq_priv->vq_ctx.common_ctx.idx);
		op_status = VIRTQ_CMD_SM_OP_ERR;
	}
	fs_virtq_cmd_progress(cmd, op_status);
}

static void fs_dev_io_comp_cb(enum snap_fs_dev_op_status status, void *done_arg);


static int init_fs_virtq_cmd(struct fs_virtq_cmd *cmd, int idx,
			     uint32_t size_max, uint32_t seg_max,
			     struct fs_virtq_priv *vq_priv)
{
	uint32_t n_descs = seg_max;
	const size_t req_size = size_max * seg_max;
	const size_t descs_size = n_descs * sizeof(struct vring_desc);
	const size_t buf_size = req_size + descs_size +
		sizeof(struct fs_virtq_cmd_aux) + sizeof(struct virtio_fs_outftr);
	uint32_t iovcnt = seg_max + 2 /* + in_header & out_header*/;
	int ret;

	cmd->idx = idx;
	cmd->vq_priv = vq_priv;
	cmd->dma_comp.func = fs_sm_dma_cb;
	cmd->fs_dev_op_ctx.user_arg = cmd;
	cmd->fs_dev_op_ctx.cb = fs_dev_io_comp_cb;
	cmd->cmd_available_index = 0;
	cmd->iov = calloc(iovcnt, sizeof(struct iovec));
	if (!cmd->iov) {
		snap_error("failed to allocate iov for virtq %d\n", idx);
		return -ENOMEM;
	}

	cmd->req_size = req_size;

	cmd->buf = vq_priv->fs_dev.ops->dma_malloc(buf_size);
	if (!cmd->buf) {
		snap_error("failed to allocate memory for virtq %d\n", idx);
		ret = -ENOMEM;
		goto free_iov;
	}

	cmd->descs = (struct vring_desc *) ((uint8_t *) cmd->buf + req_size);
	cmd->aux = (struct fs_virtq_cmd_aux *) ((uint8_t *) cmd->descs + descs_size);
	cmd->fs_req_ftr = (struct virtio_fs_outftr *) ((uint8_t *) cmd->aux + sizeof(struct fs_virtq_cmd_aux));

	cmd->mr = ibv_reg_mr(vq_priv->pd, cmd->buf, buf_size,
					IBV_ACCESS_REMOTE_READ |
					IBV_ACCESS_REMOTE_WRITE |
					IBV_ACCESS_LOCAL_WRITE);
	if (!cmd->mr) {
		snap_error("failed to register mr for virtq %d\n", idx);
		ret = -1;
		goto free_cmd_buf;
	}

	return 0;

free_cmd_buf:
	vq_priv->fs_dev.ops->dma_free(cmd->buf);
free_iov:
	free(cmd->iov);

	return ret;
}

void free_fs_virtq_cmd(struct fs_virtq_cmd *cmd)
{
	ibv_dereg_mr(cmd->mr);
	cmd->vq_priv->fs_dev.ops->dma_free(cmd->buf);
	free(cmd->iov);
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
		       struct fs_virtq_priv *vq_priv)
{
	int i, k, ret, num = vq_priv->snap_attr.vattr.size;
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
			vq_priv->vq_ctx.common_ctx.idx);
out:
	return NULL;
}

static void free_fs_virtq_cmd_arr(struct fs_virtq_priv *vq_priv)
{
	const size_t num_cmds = vq_priv->snap_attr.vattr.size;
	size_t i;

	for (i = 0; i < num_cmds; ++i)
		free_fs_virtq_cmd(&vq_priv->cmd_arr[i]);

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
static enum virtq_fetch_desc_status fetch_next_desc(struct fs_virtq_cmd *cmd)
{
	uint64_t srcaddr;
	uint16_t in_ring_desc_addr;
	int ret;

	if (cmd->num_desc == 0)
		in_ring_desc_addr = cmd->descr_head_idx %
				    cmd->vq_priv->snap_attr.vattr.size;
	else if (cmd->descs[cmd->num_desc - 1].flags & VRING_DESC_F_NEXT)
		in_ring_desc_addr = cmd->descs[cmd->num_desc - 1].next;
	else
		return VIRTQ_FETCH_DESC_DONE;

	srcaddr = cmd->vq_priv->snap_attr.vattr.desc +
		in_ring_desc_addr * sizeof(struct vring_desc);
	cmd->dma_comp.count = 1;
	virtq_log_data(cmd, "READ_DESC: pa 0x%lx len %lu\n", srcaddr, sizeof(struct vring_desc));
	ret = snap_dma_q_read(cmd->vq_priv->dma_q, &cmd->descs[cmd->num_desc],
			sizeof(struct vring_desc), cmd->mr->lkey,
			srcaddr, cmd->vq_priv->snap_attr.vattr.dma_mkey,
			&(cmd->dma_comp));
	if (ret)
		return VIRTQ_FETCH_DESC_ERR;
	++cmd->num_desc;
	return VIRTQ_FETCH_DESC_READ;
}

static void fs_dev_io_comp_cb(enum snap_fs_dev_op_status status, void *done_arg)
{
	struct fs_virtq_cmd *cmd = done_arg;
	enum virtq_cmd_sm_op_status op_status = VIRTQ_CMD_SM_OP_OK;

	if (snap_unlikely(status != SNAP_FS_DEV_OP_SUCCESS)) {
		snap_error("Failed iov completion!\n");
		op_status = VIRTQ_CMD_SM_OP_ERR;
	}

	fs_virtq_cmd_progress(cmd, op_status);
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

	// Device-readable part - fuse in header
	cmd->iov[0].iov_base = cmd->aux;
	cmd->iov[0].iov_len = cmd->descs[0].len;

	num_desc = cmd->pos_f_write > 0 ? cmd->pos_f_write : cmd->num_desc;

	virtq_log_data(cmd, "RH: iov[0] pa 0x%llx va %p, %ld\n",
		       cmd->descs[0].addr, cmd->iov[0].iov_base, cmd->iov[0].iov_len);

	// Device-readable part
	for (i = 1; i < num_desc; ++i) {
		cmd->iov[i].iov_base = cmd->req_buf + offset;
		cmd->iov[i].iov_len = cmd->descs[i].len;
		offset += cmd->descs[i].len;
		virtq_log_data(cmd, "RD: iov[%d] pa 0x%llx va %p, %ld\n",
			       i, cmd->descs[i].addr, cmd->iov[i].iov_base,
			       cmd->iov[i].iov_len);
	}

	if (snap_likely(cmd->pos_f_write > 0)) {
		// Device-writable part - fuse out header
		cmd->iov[cmd->pos_f_write].iov_base = cmd->fs_req_ftr;
		cmd->iov[cmd->pos_f_write].iov_len = sizeof(struct virtio_fs_outftr);

		virtq_log_data(cmd, "WH: iov[%d] pa 0x%llx va %p, %ld\n", i,
			       cmd->descs[cmd->pos_f_write].addr,
			       cmd->iov[cmd->pos_f_write].iov_base,
			       cmd->iov[cmd->pos_f_write].iov_len);

		// Device-writable part
		for (i = cmd->pos_f_write + 1; i < cmd->num_desc; ++i) {
			cmd->iov[i].iov_base = cmd->req_buf + offset;
			cmd->iov[i].iov_len = cmd->descs[i].len;
			offset += cmd->descs[i].len;
			virtq_log_data(cmd, "WD: iov[%d] pa 0x%llx va %p, %ld\n", i,
				       cmd->descs[i].addr, cmd->iov[i].iov_base,
				       cmd->iov[i].iov_len);

		}
	}

	if (snap_unlikely(offset > cmd->req_size)) {
		ERR_ON_CMD_FS(cmd, "Increase cmd's buffer - offset: %d req_size: %d!\n",
			   offset, cmd->req_size);
		return -EINVAL;
	}

	return 0;
}

/**
 * sm_fetch_cmd_descs() - Fetch all of commands descs
 * @cmd: Command being processed
 * @status: Callback status
 *
 * Function collects all of the commands descriptors. Descriptors can be
 * either in the tunnel command itself, or in host memory.
 *
 * Return: True if state machine is moved to a new state synchronously (error
 * or all descs were fetched), false if the state transition will be done
 * asynchronously.
 */
static bool sm_fetch_cmd_descs(struct fs_virtq_cmd *cmd,
			       enum virtq_cmd_sm_op_status status)
{
	enum virtq_fetch_desc_status ret;

	if (status != VIRTQ_CMD_SM_OP_OK) {
		ERR_ON_CMD_FS(cmd, "failed to fetch commands descs, dumping command without response\n");
		cmd->state = FS_VIRTQ_CMD_STATE_FATAL_ERR;
		return true;
	}
	ret = fetch_next_desc(cmd);
	if (ret == VIRTQ_FETCH_DESC_ERR) {
		ERR_ON_CMD_FS(cmd, "failed to RDMA READ desc from host\n");
		cmd->state = FS_VIRTQ_CMD_STATE_FATAL_ERR;
		return true;
	} else if (ret == VIRTQ_FETCH_DESC_DONE) {
		cmd->state = FS_VIRTQ_CMD_STATE_READ_REQ;
		return true;
	} else {
		return false;
	}
}

static int virtq_alloc_req_dbuf(struct fs_virtq_cmd *cmd, size_t len)
{
	int mr_access = 0, error;
	struct snap_relaxed_ordering_caps ro_caps = {};

	cmd->req_buf = cmd->vq_priv->fs_dev.ops->dma_malloc(len);
	if (!cmd->req_buf) {
		snap_error("failed to dynamically allocate %lu bytes for command %d request\n",
			   len, cmd->idx);
		error = ENOMEM;
		goto err;
	}

	mr_access = IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE |
		    IBV_ACCESS_LOCAL_WRITE;
	if (!snap_query_relaxed_ordering_caps(cmd->vq_priv->pd->context,
					      &ro_caps)) {
		if (ro_caps.relaxed_ordering_write &&
		    ro_caps.relaxed_ordering_read)
			mr_access |= IBV_ACCESS_RELAXED_ORDERING;
	} else
		snap_warn("Failed to query relaxed ordering caps\n");

	cmd->req_mr = ibv_reg_mr(cmd->vq_priv->pd, cmd->req_buf, len,
				 mr_access);
	if (!cmd->req_mr) {
		snap_error("failed to register mr for commmand %d\n", cmd->idx);
		error = EINVAL;
		goto free_buf;
	}
	cmd->use_dmem = true;
	return 0;

free_buf:
	cmd->req_mr = cmd->mr;
	free(cmd->req_buf);
err:
	cmd->req_buf = cmd->buf;
	set_cmd_error(cmd, error);
	cmd->state = FS_VIRTQ_CMD_STATE_WRITE_STATUS;
	return -1;
}

static void fs_virtq_rel_req_dbuf(struct fs_virtq_cmd *cmd)
{
	ibv_dereg_mr(cmd->req_mr);
	cmd->vq_priv->fs_dev.ops->dma_free(cmd->req_buf);
	cmd->req_buf = cmd->buf;
	cmd->req_mr = cmd->mr;
	cmd->use_dmem = false;
}

__attribute__((unused)) static bool fs_virtq_check_fs_req_format(const struct fs_virtq_cmd *cmd);

/**
 * fs_virtq_read_req_from_host() - Read request from host
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
static bool fs_virtq_read_req_from_host(struct fs_virtq_cmd *cmd)
{
	struct fs_virtq_priv *priv = cmd->vq_priv;
	uint32_t offset, num_desc;
	int i, ret;


	cmd->dma_comp.count = 1;
	for (i = 1; i < cmd->num_desc; i++) {
		snap_debug("\t desc[%d] --> pa 0x%llx len %d fl 0x%x F_WR %d\n", i,
			   cmd->descs[i].addr, cmd->descs[i].len, cmd->descs[i].flags,
			   (cmd->descs[i].flags & VRING_DESC_F_WRITE) ? 1 : 0);

		cmd->total_seg_len += cmd->descs[i].len;
		if ((cmd->descs[i].flags & VRING_DESC_F_WRITE) == 0) {
			++cmd->dma_comp.count;
		} else {
			if (snap_unlikely(!cmd->pos_f_write))
				cmd->pos_f_write = i;
		}
	}

	if (snap_likely(cmd->pos_f_write > 0))
		cmd->total_seg_len -= sizeof(struct virtio_fs_outftr);

	if (!FS_VIRTQ_CHECK_FS_REQ_FORMAT(cmd)) {
		set_cmd_error(cmd, EINVAL);
		cmd->state = FS_VIRTQ_CMD_STATE_WRITE_STATUS;
		return true;
	}

	if (snap_unlikely(cmd->total_seg_len > cmd->req_size)) {
		if (virtq_alloc_req_dbuf(cmd, cmd->total_seg_len))
			return true;
	}

	offset = 0;
	cmd->state = FS_VIRTQ_CMD_STATE_HANDLE_REQ;
	num_desc = cmd->pos_f_write > 0 ? cmd->pos_f_write : cmd->num_desc;

	virtq_log_data(cmd, "READ_HEADER: pa 0x%llx len %u\n",
		       cmd->descs[0].addr, cmd->descs[0].len);
	ret = snap_dma_q_read(priv->dma_q, &cmd->aux->header, cmd->descs[0].len,
		cmd->mr->lkey, cmd->descs[0].addr, priv->snap_attr.vattr.dma_mkey,
		&cmd->dma_comp);
	if (ret) {
		cmd->state = FS_VIRTQ_CMD_STATE_FATAL_ERR;
		return true;
	}

	for (i = 1; i < num_desc; ++i) {
		virtq_log_data(cmd, "READ_DATA: pa 0x%llx va %p len %u\n",
			       cmd->descs[i].addr, cmd->req_buf + offset, cmd->descs[i].len);
		ret = snap_dma_q_read(priv->dma_q, cmd->req_buf + offset,
				cmd->descs[i].len, cmd->req_mr->lkey, cmd->descs[i].addr,
				priv->snap_attr.vattr.dma_mkey, &cmd->dma_comp);
		if (ret) {
			cmd->state = FS_VIRTQ_CMD_STATE_FATAL_ERR;
			return true;
		}
		offset += cmd->descs[i].len;
	}
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
static bool fs_virtq_handle_req(struct fs_virtq_cmd *cmd,
				enum virtq_cmd_sm_op_status status)
{
	struct fs_virtq_dev *fs_dev = &cmd->vq_priv->fs_dev;
	int ret;
	uint32_t r_descs, w_descs;

	if (status != VIRTQ_CMD_SM_OP_OK) {
		ERR_ON_CMD_FS(cmd, "failed to get request data, returning failure\n");
		set_cmd_error(cmd, EINVAL);
		cmd->state = FS_VIRTQ_CMD_STATE_WRITE_STATUS;
		return true;
	}

	fs_virtq_dump_fs_opcode(cmd);

	ret = set_iovecs(cmd);
	if (ret) {
		cmd->state = FS_VIRTQ_CMD_STATE_WRITE_STATUS;
		set_cmd_error(cmd, EINVAL);
		return true;
	}

	r_descs = cmd->pos_f_write > 0 ? cmd->pos_f_write : cmd->num_desc;
	w_descs = cmd->pos_f_write > 0 ? cmd->num_desc - cmd->pos_f_write : 0;

	/* Following is correct for request queue only (not for hiprio queue):
	 *
	 *	cmd->iov[0] - fs_virtq_cmd_aux
	 *	cmd->iov[1 ... cmd->pos_f_write - 1] device-readable part:
	 *		cmd->desc[1 ... ].flag & VRING_DESC_F_WRITE == 0
	 *	cmd->iov[cmd->pos_f_write] - virtio_fs_outftr
	 *	cmd->iov[cmd->pos_f_write + 1 ... cmd->num_desc] - device-writable part:
	 *		corresponded cmd->desc[1 ... ].flag & VRING_DESC_F_WRITE != 0
	 */
	ret = fs_dev->ops->handle_req(fs_dev->ctx, cmd->iov,
				      r_descs,
				      (w_descs > 0) ? &cmd->iov[cmd->pos_f_write] : NULL,
				      w_descs,
				      &cmd->fs_dev_op_ctx);

	if (ret) {
		ERR_ON_CMD_FS(cmd, "failed while executing command\n");
		set_cmd_error(cmd, EIO);
		cmd->state = FS_VIRTQ_CMD_STATE_WRITE_STATUS;
		return true;
	}

	/* For request queues:
	 * Start handle the VRING_DESC_F_WRITE (writable) descriptors first.
	 * Writable, meaning the descriptor's data was 'filled' by fs device.
	 */
	if (snap_likely(cmd->vq_priv->vq_ctx.common_ctx.idx > 0))
		cmd->state = FS_VIRTQ_CMD_STATE_IN_IOV_DONE;
	else {
		/* hiprio queue - do nothing, send tunneling completion only */
		virtq_log_data(cmd, "hiprio - send completion\n");
		cmd->state = FS_VIRTQ_CMD_STATE_SEND_COMP;
	}

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
static bool sm_handle_in_iov_done(struct fs_virtq_cmd *cmd,
				  enum virtq_cmd_sm_op_status status)
{
	int i, ret;

	if (status != VIRTQ_CMD_SM_OP_OK) {
		ERR_ON_CMD_FS(cmd, "failed to read from block device, send ioerr to host\n");
		set_cmd_error(cmd, EIO);
		cmd->state = FS_VIRTQ_CMD_STATE_WRITE_STATUS;
		return true;
	}

	cmd->state = FS_VIRTQ_CMD_STATE_WRITE_STATUS;
	cmd->dma_comp.count = cmd->num_desc - (cmd->pos_f_write + 1);
	if (snap_likely(cmd->dma_comp.count > 0)) {

		/* Note: the desc at position cmd->pos_f_write is desciptor for
		 * fuse_out_header status.
		 */
		for (i = cmd->pos_f_write + 1; i < cmd->num_desc; ++i) {
			virtq_log_data(cmd, "WRITE_DATA: pa 0x%llx va %p len %u\n",
				       cmd->descs[i].addr, cmd->iov[i].iov_base,
				       cmd->descs[i].len);
			fs_virtq_mark_dirty_mem(cmd, cmd->descs[i].addr,
						cmd->descs[i].len, false);
			ret = snap_dma_q_write(cmd->vq_priv->dma_q,
					       cmd->iov[i].iov_base,
					       cmd->descs[i].len,
					       cmd->req_mr->lkey,
					       cmd->descs[i].addr,
					       cmd->vq_priv->snap_attr.vattr.dma_mkey,
					       &(cmd->dma_comp));
			if (ret) {
				set_cmd_error(cmd, -ret);
				cmd->state = FS_VIRTQ_CMD_STATE_WRITE_STATUS;
				return true;
			}
			cmd->total_in_len += cmd->descs[i].len;
		}
		return false;
	}
	return true;
}

/**
 * sm_handle_out_iov_done() - check write to fs device result status
 * @cmd:	command which requested the write
 * @status:	status of write operation
 */
static void sm_handle_out_iov_done(struct fs_virtq_cmd *cmd,
				   enum virtq_cmd_sm_op_status status)
{
	cmd->state = FS_VIRTQ_CMD_STATE_WRITE_STATUS;
	if (status != VIRTQ_CMD_SM_OP_OK)
		set_cmd_error(cmd, EIO);
}

/**
 * sm_write_status() - Write command status to host memory upon finish
 * @cmd:	command which requested the write
 * @status:	callback status, expected 0 for no errors
 *
 * Return: True if state machine is moved synchronously to the new state
 * (error cases) or false if the state transition will be done asynchronously.
 */
static inline bool sm_write_status(struct fs_virtq_cmd *cmd,
				   enum virtq_cmd_sm_op_status status)
{
	if (snap_likely(cmd->pos_f_write > 0)) {

		int ret;

		if (snap_unlikely(status != VIRTQ_CMD_SM_OP_OK))
			set_cmd_error(cmd, EIO);

		virtq_log_data(cmd, "WRITE_STATUS: pa 0x%llx va %p len %lu\n",
			       cmd->descs[cmd->pos_f_write].addr,
			       cmd->iov[cmd->pos_f_write].iov_base,
			       sizeof(struct virtio_fs_outftr));
		fs_virtq_mark_dirty_mem(cmd, cmd->descs[cmd->pos_f_write].addr,
					sizeof(struct virtio_fs_outftr), false);

		// tx_inline is 60 bytes, struct fuse_out_header is 16 bytes
		ret = snap_dma_q_write_short(cmd->vq_priv->dma_q, cmd->fs_req_ftr,
					sizeof(struct virtio_fs_outftr),
					cmd->descs[cmd->pos_f_write].addr,
					cmd->vq_priv->snap_attr.vattr.dma_mkey);
		if (snap_unlikely(ret)) {
			/* TODO: at some point we will have to do pending queue */
			ERR_ON_CMD_FS(cmd, "failed to send status, err=%d\n", ret);
			cmd->state = FS_VIRTQ_CMD_STATE_FATAL_ERR;
			return true;
		}

		cmd->total_in_len += sizeof(struct virtio_fs_outftr);
	}

	cmd->state = FS_VIRTQ_CMD_STATE_SEND_COMP;
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
static inline int sm_send_completion(struct fs_virtq_cmd *cmd,
				     enum virtq_cmd_sm_op_status status)
{
	int ret;
	struct virtq_split_tunnel_comp tunnel_comp;

	if (snap_unlikely(status != VIRTQ_CMD_SM_OP_OK)) {
		snap_error("failed to write the request status field\n");

		/* TODO: if FS_VIRTQ_CMD_STATE_FATAL_ERR could be recovered in the future,
		 * handle case when cmd with FS_VIRTQ_CMD_STATE_FATAL_ERR handled unordered.
		 */
		cmd->state = FS_VIRTQ_CMD_STATE_FATAL_ERR;
		return true;
	}

	/* check order of completed command, if the command unordered - wait for
	 * other completions
	 */
	if (snap_unlikely(cmd->vq_priv->force_in_order)) {
		if (snap_unlikely(cmd->cmd_available_index != cmd->vq_priv->ctrl_used_index)) {
			virtq_log_data(cmd, "UNORD_COMP: cmd_idx:%d, in_num:%d, wait for in_num:%d\n",
				       cmd->idx, cmd->cmd_available_index,
				       cmd->vq_priv->ctrl_used_index);
			cmd->state = FS_VIRTQ_CMD_STATE_SEND_IN_ORDER_COMP;
			return false;
		}
	}

	tunnel_comp.descr_head_idx = cmd->descr_head_idx;
	tunnel_comp.len = cmd->total_in_len;
	virtq_log_data(cmd, "SEND_COMP: descr_head_idx %d len %d send_size %lu\n",
		       tunnel_comp.descr_head_idx, tunnel_comp.len,
		       sizeof(struct virtq_split_tunnel_comp));
	fs_virtq_mark_dirty_mem(cmd, 0, 0, true);
	ret = snap_dma_q_send_completion(cmd->vq_priv->dma_q,
					 &tunnel_comp,
					 sizeof(struct virtq_split_tunnel_comp));
	if (snap_unlikely(ret)) {
		/* TODO: pending queue */
		ERR_ON_CMD_FS(cmd, "failed to second completion\n");
		cmd->state = FS_VIRTQ_CMD_STATE_FATAL_ERR;
	} else {
		cmd->state = FS_VIRTQ_CMD_STATE_RELEASE;
		++cmd->vq_priv->ctrl_used_index;
	}

	return true;
}

/**
 * fs_virtq_cmd_progress() - command state machine progress handle
 * @cmd:	commad to be processed
 * @status:	status of calling function (can be a callback)
 *
 * Return: 0 (Currently no option to fail)
 */
static int fs_virtq_cmd_progress(struct fs_virtq_cmd *cmd,
				 enum virtq_cmd_sm_op_status status)
{
	bool repeat = true;

	while (repeat) {
		repeat = false;
		snap_debug("virtq cmd sm state: %d\n", cmd->state);
		switch (cmd->state) {
		case FS_VIRTQ_CMD_STATE_IDLE:
			snap_error("command in invalid state %d\n",
				   FS_VIRTQ_CMD_STATE_IDLE);
			break;
		case FS_VIRTQ_CMD_STATE_FETCH_CMD_DESCS:
			repeat = sm_fetch_cmd_descs(cmd, status);
			break;
		case FS_VIRTQ_CMD_STATE_READ_REQ:
			repeat = fs_virtq_read_req_from_host(cmd);
			break;
		case FS_VIRTQ_CMD_STATE_HANDLE_REQ:
			repeat = fs_virtq_handle_req(cmd, status);
			break;
		case FS_VIRTQ_CMD_STATE_IN_IOV_DONE:
			repeat = sm_handle_in_iov_done(cmd, status);
			break;
		case FS_VIRTQ_CMD_STATE_OUT_IOV_DONE:
			sm_handle_out_iov_done(cmd, status);
			repeat = true;
			break;
		case FS_VIRTQ_CMD_STATE_WRITE_STATUS:
			repeat = sm_write_status(cmd, status);
			break;
		case FS_VIRTQ_CMD_STATE_SEND_COMP:
		case FS_VIRTQ_CMD_STATE_SEND_IN_ORDER_COMP:
			repeat = sm_send_completion(cmd, status);
			break;
		case FS_VIRTQ_CMD_STATE_RELEASE:
			if (snap_unlikely(cmd->use_dmem))
				fs_virtq_rel_req_dbuf(cmd);
			--cmd->vq_priv->cmd_cntr;
			break;
		case FS_VIRTQ_CMD_STATE_FATAL_ERR:
			if (snap_unlikely(cmd->use_dmem))
				fs_virtq_rel_req_dbuf(cmd);
			cmd->vq_priv->vq_ctx.common_ctx.fatal_err = -1;
			/*
			 * TODO: propagate fatal error to the controller.
			 * At the moment attempt to resume/state copy
			 * of such controller will have unpredictable
			 * results.
			 */
			--cmd->vq_priv->cmd_cntr;
			break;
		default:
			ERR_ON_CMD_FS(cmd, "reached invalid state %d\n", cmd->state);
			break;
		}
	};

	return 0;
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
	struct fs_virtq_priv *priv = (struct fs_virtq_priv *)q->uctx;
	void *descs = data + sizeof(struct virtq_split_tunnel_req_hdr);
	enum virtq_cmd_sm_op_status status = VIRTQ_CMD_SM_OP_OK;
	int cmd_idx, len;
	struct fs_virtq_cmd *cmd;
	struct virtq_split_tunnel_req_hdr *split_hdr;

	split_hdr = (struct virtq_split_tunnel_req_hdr *)data;

	cmd_idx = priv->ctrl_available_index % priv->snap_attr.vattr.size;
	cmd = &priv->cmd_arr[cmd_idx];
	cmd->num_desc = split_hdr->num_desc;
	cmd->descr_head_idx = split_hdr->descr_head_idx;
	cmd->total_seg_len = 0;
	cmd->total_in_len = 0;
	cmd->fs_req_ftr->out_header.error = 0;
	cmd->use_dmem = false;
	cmd->req_buf = cmd->buf;
	cmd->req_mr = cmd->mr;
	cmd->pos_f_write = 0;

	if (snap_unlikely(cmd->vq_priv->force_in_order))
		cmd->cmd_available_index = priv->ctrl_available_index;

	/* If new commands are not dropped there is a risk of never
	 * completing the flush
	 **/
	if (snap_unlikely(priv->swq_state == SW_VIRTQ_FLUSHING)) {
		virtq_log_data(cmd, "DROP_CMD: %ld inline descs, rxlen %d\n",
			       cmd->num_desc, data_len);
		return;
	}

	if (split_hdr->num_desc) {
		len = sizeof(struct vring_desc) * split_hdr->num_desc;
		memcpy(cmd->descs, descs, len);
	}

	++priv->cmd_cntr;
	++priv->ctrl_available_index;
	cmd->state = FS_VIRTQ_CMD_STATE_FETCH_CMD_DESCS;
	virtq_log_data(cmd, "NEW_CMD: %lu inline descs, descr_head_idx %d, va %p (%d), rxlen %u\n",
		       cmd->num_desc, cmd->descr_head_idx, cmd->req_buf, cmd->req_size, data_len);
	fs_virtq_cmd_progress(cmd, status);
}

static bool fs_virtq_check_fs_req_format(const struct fs_virtq_cmd *cmd)
{
	/* 5.11.2 Virtqueues
	 * 0 - hiprio
	 * 5.11.6.2 Device Operation: High Priority Queue
	 * for hiprio queue, the fuse format of requests is different.
	 * There are no writable desciptors !
	 */
	if (cmd->vq_priv->vq_ctx.common_ctx.idx > 0) {
		if (cmd->pos_f_write == cmd->num_desc) {
			ERR_ON_CMD_FS(cmd, "No writable desciptor found !\n");
			return false;
		}

		// First writable descriptor should point to virtio_fs_outftr
		if (snap_unlikely(cmd->descs[cmd->pos_f_write].len != sizeof(struct virtio_fs_outftr))) {
			ERR_ON_CMD_FS(cmd, "Unexpected len: %d in desc[%d] - expected %ld bytes !\n",
				   cmd->descs[cmd->pos_f_write].len,
				   cmd->pos_f_write, sizeof(struct virtio_fs_outftr));
			return false;
		}
	} else {
		if (snap_unlikely(cmd->descs[0].len > sizeof(struct fs_virtq_cmd_aux))) {
			ERR_ON_CMD_FS(cmd, "Unexpected len: %d of in header !\n",
				   cmd->descs[0].len);
			return false;
		}
		if (snap_unlikely(cmd->pos_f_write != 0)) {
			ERR_ON_CMD_FS(cmd, "Writable desciptor found !\n");
			return false;
		}
	}

	return true;
}

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
	struct snap_dma_q_create_attr rdma_qp_create_attr = {};
	struct snap_virtio_fs_queue_attr qattr = {};
	struct fs_virtq_ctx *vq_ctx;
	struct fs_virtq_priv *vq_priv;
	int num_descs = attr->seg_max;

	vq_priv = calloc(1, sizeof(struct fs_virtq_priv));
	if (!vq_priv)
		goto err;

	vq_ctx = &vq_priv->vq_ctx;
	vq_ctx->common_ctx.priv = vq_priv;
	vq_priv->fs_dev.ops = fs_dev_ops;
	vq_priv->fs_dev.ctx = fs_dev;
	vq_priv->pd = attr->pd;
	vq_ctx->common_ctx.idx = attr->idx;
	vq_ctx->common_ctx.fatal_err = 0;
	vq_priv->seg_max = attr->seg_max;
	vq_priv->size_max = attr->size_max;
	vq_priv->snap_attr.vattr.size = attr->queue_size;
	vq_priv->swq_state = SW_VIRTQ_RUNNING;
	vq_priv->vfsq = vfsq;

	vq_priv->cmd_arr = alloc_fs_virtq_cmd_arr(attr->size_max,
							attr->seg_max, vq_priv);
	if (!vq_priv->cmd_arr) {
		snap_error("failed allocating cmd list for queue %d\n",
			   attr->idx);
		goto release_priv;
	}
	vq_priv->cmd_cntr = 0;
	vq_priv->ctrl_available_index = attr->hw_available_index;
	vq_priv->ctrl_used_index = vq_priv->ctrl_available_index;

	rdma_qp_create_attr.tx_qsize = attr->queue_size;
	rdma_qp_create_attr.tx_elem_size = snap_max(sizeof(struct virtq_split_tunnel_comp),
						sizeof(struct virtio_fs_outftr));
	rdma_qp_create_attr.rx_qsize = attr->queue_size;
	rdma_qp_create_attr.rx_elem_size = sizeof(struct virtq_split_tunnel_req_hdr) +
					   num_descs * sizeof(struct vring_desc);
	rdma_qp_create_attr.uctx = vq_priv;
	rdma_qp_create_attr.rx_cb = fs_virtq_rx_cb;
	rdma_qp_create_attr.mode = snap_env_getenv(SNAP_DMA_Q_OPMODE);
	vq_priv->dma_q = snap_dma_q_create(attr->pd, &rdma_qp_create_attr);
	if (!vq_priv->dma_q) {
		snap_error("failed creating rdma qp loop\n");
		goto dealloc_cmd_arr;
	}

	vq_priv->snap_attr.vattr.type = SNAP_VIRTQ_SPLIT_MODE;
	vq_priv->snap_attr.vattr.ev_mode = (attr->msix_vector == VIRTIO_MSI_NO_VECTOR) ?
					    SNAP_VIRTQ_NO_MSIX_MODE :
					    SNAP_VIRTQ_MSIX_MODE;
	vq_priv->snap_attr.vattr.virtio_version_1_0 = attr->virtio_version_1_0;
	vq_priv->snap_attr.vattr.offload_type = SNAP_VIRTQ_OFFLOAD_DESC_TUNNEL;
	vq_priv->snap_attr.vattr.idx = attr->idx;
	vq_priv->snap_attr.vattr.desc = attr->desc;
	vq_priv->snap_attr.vattr.driver = attr->driver;
	vq_priv->snap_attr.vattr.device = attr->device;
	vq_priv->snap_attr.vattr.full_emulation = true;
	vq_priv->snap_attr.vattr.max_tunnel_desc = snap_min(attr->max_tunnel_desc,
							    attr->seg_max);
	vq_priv->snap_attr.vattr.event_qpn_or_msix = attr->msix_vector;
	vq_priv->snap_attr.vattr.pd = attr->pd;
	vq_priv->snap_attr.hw_available_index = attr->hw_available_index;
	vq_priv->snap_attr.hw_used_index = attr->hw_used_index;
	vq_priv->snap_attr.qp = snap_dma_q_get_fw_qp(vq_priv->dma_q);
	if (!vq_priv->snap_attr.qp) {
		snap_error("no fw qp exist when trying to create virtq\n");
		goto release_rdma_qp;
	}
	vq_priv->snap_vfsq = snap_virtio_fs_create_queue(snap_dev,
							 &vq_priv->snap_attr);
	if (!vq_priv->snap_vfsq) {
		snap_error("failed creating VIRTQ fw element\n");
		goto release_rdma_qp;
	}
	if (snap_virtio_fs_query_queue(vq_priv->snap_vfsq,
					&vq_priv->snap_attr)) {
		snap_error("failed query created snap virtio fs queue\n");
		goto destroy_virtio_fs_queue;
	}
	qattr.vattr.state = SNAP_VIRTQ_STATE_RDY;
	if (snap_virtio_fs_modify_queue(vq_priv->snap_vfsq,
					SNAP_VIRTIO_FS_QUEUE_MOD_STATE,
					&qattr)) {
		snap_error("failed to change virtq to READY state\n");
		goto destroy_virtio_fs_queue;
	}

	vq_priv->force_in_order = attr->force_in_order;
	snap_debug("created VIRTQ %d succesfully in_order %d\n", attr->idx,
		   attr->force_in_order);
	return vq_ctx;

destroy_virtio_fs_queue:
	snap_virtio_fs_destroy_queue(vq_priv->snap_vfsq);
release_rdma_qp:
	snap_dma_q_destroy(vq_priv->dma_q);
dealloc_cmd_arr:
	free_fs_virtq_cmd_arr(vq_priv);
release_priv:
	free(vq_priv);
err:
	snap_error("failed creating fs_virtq %d\n", attr->idx);
	return NULL;
}

/**
 * fs_virtq_destroy() - Destroyes fs virtq object
 * @q: queue to be destryoed
 *
 * Context: 1. Destroy should be called only when queue is in suspended state.
 *	    2. fs_virtq_progress() should not be called during destruction.
 *
 * Return: void
 */
void fs_virtq_destroy(struct fs_virtq_ctx *q)
{
	struct fs_virtq_priv *vq_priv = q->common_ctx.priv;

	snap_debug("destroying queue %d\n", q->common_ctx.idx);

	if (vq_priv->swq_state != SW_VIRTQ_SUSPENDED && vq_priv->cmd_cntr)
		snap_warn("queue %d: destroying while not in the SUSPENDED state, %d commands outstanding\n",
			  q->common_ctx.idx, vq_priv->cmd_cntr);

	if (snap_virtio_fs_destroy_queue(vq_priv->snap_vfsq))
		snap_error("queue %d: error destroying fs_virtq\n", q->common_ctx.idx);

	snap_dma_q_destroy(vq_priv->dma_q);
	free_fs_virtq_cmd_arr(vq_priv);
	free(vq_priv);
}

int fs_virtq_get_debugstat(struct fs_virtq_ctx *q,
			   struct snap_virtio_queue_debugstat *q_debugstat)
{
	struct fs_virtq_priv *vq_priv = q->common_ctx.priv;
	struct snap_virtio_fs_queue_attr virtq_attr = {};
	struct snap_virtio_queue_counters_attr vqc_attr = {};
	struct vring_avail vra;
	struct vring_used vru;
	uint64_t drv_addr = vq_priv->snap_attr.vattr.driver;
	uint64_t dev_addr = vq_priv->snap_attr.vattr.device;
	int ret;

	ret = snap_virtio_get_vring_indexes_from_host(vq_priv->pd, drv_addr, dev_addr,
						      vq_priv->snap_attr.vattr.dma_mkey,
						      &vra, &vru
						     );
	if (ret) {
		snap_error("failed to get vring indexes from host memory for queue %d\n",
			   q->common_ctx.idx);
		return ret;
	}

	ret = snap_virtio_fs_query_queue(vq_priv->snap_vfsq, &virtq_attr);
	if (ret) {
		snap_error("failed query queue %d debugstat\n", q->common_ctx.idx);
		return ret;
	}

	ret = snap_virtio_query_queue_counters(vq_priv->snap_vfsq->virtq.ctrs_obj, &vqc_attr);
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
			       struct snap_virtio_fs_queue_attr *attr)
{
	int ret;
	struct fs_virtq_priv *vq_priv = q->common_ctx.priv;

	ret = snap_virtio_fs_query_queue(vq_priv->snap_vfsq, attr);
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

static int fs_virtq_progress_suspend(struct fs_virtq_ctx *q)
{
	struct fs_virtq_priv *priv = q->common_ctx.priv;
	struct snap_virtio_fs_queue_attr qattr = {};

	/* TODO: add option to ignore commands in the fs device layer */
	if (priv->cmd_cntr != 0)
		return 0;

	snap_dma_q_flush(priv->dma_q);

	qattr.vattr.state = SNAP_VIRTQ_STATE_SUSPEND;
	/* TODO: check with FLR/reset. I see modify fail where it should not */
	if (snap_virtio_fs_modify_queue(priv->snap_vfsq, SNAP_VIRTIO_FS_QUEUE_MOD_STATE,
					&qattr)) {
		snap_error("queue %d: failed to move to the SUSPENDED state\n",
			   q->common_ctx.idx);
	}
	/* at this point QP is in the error state and cannot be used anymore */
	snap_info("queue %d: moving to the SUSPENDED state\n", q->common_ctx.idx);
	priv->swq_state = SW_VIRTQ_SUSPENDED;
	return 0;
}

/**
 * fs_virq_progress_unordered() - Check & complete unordered commands
 * @vq_priv:	queue to progress
 */
static void fs_virq_progress_unordered(struct fs_virtq_priv *vq_priv)
{
	uint16_t cmd_idx = vq_priv->ctrl_used_index % vq_priv->snap_attr.vattr.size;
	struct fs_virtq_cmd *cmd = &vq_priv->cmd_arr[cmd_idx];

	while (cmd->state == FS_VIRTQ_CMD_STATE_SEND_IN_ORDER_COMP &&
	       cmd->cmd_available_index == cmd->vq_priv->ctrl_used_index) {
		virtq_log_data(cmd, "PEND_COMP: ino_num:%d state:%d\n",
			       cmd->cmd_available_index, cmd->state);

		fs_virtq_cmd_progress(cmd, VIRTQ_CMD_SM_OP_OK);

		cmd_idx = vq_priv->ctrl_used_index % vq_priv->snap_attr.vattr.size;
		cmd = &vq_priv->cmd_arr[cmd_idx];
	}
}

/**
 * fs_virtq_progress() - Progress RDMA QPs,  Polls on QPs CQs
 * @q:	queue to progress
 *
 * Context: Not thread safe
 *
 * Return: error code on failure, 0 on success
 */
int fs_virtq_progress(struct fs_virtq_ctx *q)
{
	struct fs_virtq_priv *priv = q->common_ctx.priv;

	if (snap_unlikely(priv->swq_state == SW_VIRTQ_SUSPENDED))
		return 0;

	snap_dma_q_progress(priv->dma_q);

	if (snap_unlikely(priv->force_in_order))
		fs_virq_progress_unordered(priv);

	/*
	 * need to wait until all inflight requests
	 * are finished before moving to the suspend state
	 */
	if (snap_unlikely(priv->swq_state == SW_VIRTQ_FLUSHING))
		return fs_virtq_progress_suspend(q);

	return 0;
}

/**
 * fs_virtq_suspend() - Request moving queue to suspend state
 * @q:	queue to move to suspend state
 *
 * When suspend is requested the queue stops receiving new commands
 * and moves to FLUSHING state. Once all commands already fetched are
 * finished, the queue moves to SUSPENDED state.
 *
 * Context: Function is not thread safe with regard to fs_virtq_progress
 * and fs_virtq_is_suspended. If called from a different thread than
 * thread calling progress/is_suspended then application must take care of
 * proper locking
 *
 * Return: 0 on success, else error code
 */
int fs_virtq_suspend(struct fs_virtq_ctx *q)
{
	struct fs_virtq_priv *priv = q->common_ctx.priv;

	if (priv->swq_state != SW_VIRTQ_RUNNING) {
		snap_debug("queue %d: suspend was already requested\n",
			   q->common_ctx.idx);
		return -EBUSY;
	}

	snap_info("queue %d: SUSPENDING %d command(s) outstanding\n",
		  q->common_ctx.idx, priv->cmd_cntr);

	if (priv->vq_ctx.common_ctx.fatal_err)
		snap_warn("queue %d: fatal error. Resuming or live migration will not be possible\n",
			  q->common_ctx.idx);

	priv->swq_state = SW_VIRTQ_FLUSHING;
	return 0;
}

/**
 * fs_virtq_is_suspended() - api for checking if queue in suspended state
 * @q:		queue to check
 *
 * Context: Function is not thread safe with regard to fs_virtq_progress
 * and fs_virtq_suspend. If called from a different thread than
 * thread calling progress/suspend then application must take care of
 * proper locking
 *
 * Return: True when queue suspended, and False for not suspended
 */
bool fs_virtq_is_suspended(struct fs_virtq_ctx *q)
{
	struct fs_virtq_priv *priv = q->common_ctx.priv;

	return priv->swq_state == SW_VIRTQ_SUSPENDED;
}

/**
 * fs_virtq_start() - set virtq attributes used for operating
 * @q:		queue to start
 * @attr:	attrs used to start the quue
 *
 * Function set attributes queue needs in order to operate.
 *
 * Return: void
 */
void fs_virtq_start(struct fs_virtq_ctx *q,
		    struct virtq_start_attr *attr)
{
	struct fs_virtq_priv *priv = q->common_ctx.priv;

	priv->pg_id = attr->pg_id;
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
	struct fs_virtq_priv *priv = q->common_ctx.priv;
	struct snap_virtio_fs_queue_attr attr = {};
	int ret;

	ret = snap_virtio_fs_query_queue(priv->snap_vfsq, &attr);
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

struct snap_dma_q *fs_get_dma_q(struct fs_virtq_ctx *ctx)
{
	struct fs_virtq_priv *vpriv = ctx->common_ctx.priv;

	return vpriv->dma_q;
}

int fs_set_dma_mkey(struct fs_virtq_ctx *ctx, uint32_t mkey)
{
	struct fs_virtq_priv *vpriv = ctx->common_ctx.priv;

	vpriv->snap_attr.vattr.dma_mkey = mkey;
	return 0;
}
