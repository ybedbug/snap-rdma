#include <linux/virtio_ring.h>
#include <linux/virtio_blk.h>
#include <linux/virtio_pci.h>
#include "snap_virtio_blk_virtq.h"
#include "snap_dma.h"
#include "snap_virtio_blk.h"

#define NUM_HDR_FTR_DESCS 2

#define BDEV_SECTOR_SIZE 512
#define VIRTIO_NUM_DESC(seg_max) ((seg_max) + NUM_HDR_FTR_DESCS)
#define ERR_ON_CMD(cmd, ...) snap_error(" on queue %d, on command %d err:"\
					__VA_ARGS__,\
					cmd->vq_priv->vq_ctx.idx, cmd->idx);

struct blk_virtq_priv;

/**
 * struct split_tunnel_req_hdr - header of command received from FW
 *
 * Struct uses 2 rsvd so it will be aligned to 4B (and not 8B)
 */
struct split_tunnel_req_hdr {
	uint16_t avail_idx;
	uint16_t num_desc;
	uint32_t rsvd1;
	uint32_t rsvd2;
};

/**
 * struct split_tunnel_comp - header of completion sent to FW
 */
struct split_tunnel_comp {
	uint16_t avail_idx;
	uint16_t rsvd;
	uint32_t len;
};

/**
 * struct virtio_blk_outftr - footer of request, written to host memory
 */
struct virtio_blk_outftr {
	uint8_t status;
};

/**
 * enum virtq_cmd_sm_state - state of the sm handling a cmd
 * @VIRTQ_CMD_STATE_IDLE:            SM initialization state
 * @VIRTQ_CMD_STATE_FETCH_CMD_DESCS: SM received tunnel cmd and copied
 *                                   immediate data, now fetch cmd descs
 * @VIRTQ_CMD_STATE_READ_REQ:        Read request data from host memory
 * @VIRTQ_CMD_STATE_HANDLE_REQ:      Handle received request from host, perform
 *                                   READ/WRITE/FLUSH
 * @VIRTQ_CMD_STATE_T_OUT_IOV_DONE:  Finished writing to bdev, check write
 *                                   status
 * @VIRTQ_CMD_STATE_T_IN_IOV_DONE:   Write data pulled from bdev to host memory
 * @VIRTQ_CMD_STATE_WRITE_STATUS:    Write cmd status to host memory
 * @VIRTQ_CMD_STATE_SEND_COMP:       Send completion to FW
 * @VIRTQ_CMD_STATE_RELEASE:         Release command
 * @VIRTQ_CMD_STATE_FATAL_ERR:       Fatal error, SM stuck here (until reset)
 */
enum virtq_cmd_sm_state {
	VIRTQ_CMD_STATE_IDLE,
	VIRTQ_CMD_STATE_FETCH_CMD_DESCS,
	VIRTQ_CMD_STATE_READ_REQ,
	VIRTQ_CMD_STATE_HANDLE_REQ,
	VIRTQ_CMD_STATE_T_OUT_IOV_DONE,
	VIRTQ_CMD_STATE_T_IN_IOV_DONE,
	VIRTQ_CMD_STATE_SEND_COMP,
	VIRTQ_CMD_STATE_WRITE_STATUS,
	VIRTQ_CMD_STATE_RELEASE,
	VIRTQ_CMD_STATE_FATAL_ERR,
};

/**
 * enum virtq_cmd_sm_op_status - status of last operation
 * @VIRTQ_CMD_SM_OP_OK: 	Last operation finished without a problem
 * @VIRQT_CMD_SM_OP_ERR:	Last operation failed
 *
 * State machine operates asynchronously, usually by calling a function
 * and providing a callback. Once callback is called it calls the state machine
 * progress again and provides it with the status of the function called.
 * This enum describes the status of the function called.
 */
enum virtq_cmd_sm_op_status {
	VIRTQ_CMD_SM_OP_OK,
	VIRTQ_CMD_SM_OP_ERR,
};

/**
 * struct blk_virtq_cmd - command context
 * @idx:		avail_idx modulo queue size
 * @avail_idx:  	avail_idx
 * @num_desc:		number of descriptors in the command
 * @vq_priv:		virtqueue command belongs to, private context
 * @blk_req_ftr:	request footer written to host memory
 * @state:		state of sm processing the command
 * @req_buf:		buffer holding the request data
 * @req_mr:		memory region for req_buf
 * @descs:		memory holding command descriptors
 * @iovecs:		io vectors pointing to data to written/read by bdev
 * @desc_mr:		memory region for descs
 * @dma_comp:		struct given to snap library
 * @tunnel_comp:	completion sent to FW
 * @total_seg_len:	total length of the request data to be written/read
 * @total_in_len:	total length of data written to request buffers
 */
struct blk_virtq_cmd {
	int idx;
	uint16_t avail_idx;
	int num_desc;
	struct blk_virtq_priv *vq_priv;
	struct virtio_blk_outftr blk_req_ftr;
	enum virtq_cmd_sm_state state;
	uint8_t *req_buf;
	char *device_id;
	struct ibv_mr *req_mr;
	struct vring_desc *descs;
	struct iovec *iovecs;
	struct ibv_mr *desc_mr;
	struct snap_dma_completion dma_comp;
	struct split_tunnel_comp *tunnel_comp;
	uint32_t total_seg_len;
	uint32_t total_in_len;
	struct snap_bdev_io_done_ctx bdev_op_ctx;
};

/**
 * enum blk_sw_virtq_state - state of sw virtq
 * @BLK_SW_VIRTQ_RUNNING:	Queue receives and operates commands
 * @BLK_SW_VIRTQ_FLUSHING:	Queue stops recieving new commands and operates
 * 				commands already received
 * @BLK_SW_VIRTQ_SUSPENDED:	Queue doesn't receive new commands and has no
 * 				commands to operate
 *
 * This is the state of the sw virtq (as opposed to VIRTQ_BLK_Q PRM FW object)
 */
enum blk_sw_virtq_state {
	BLK_SW_VIRTQ_RUNNING,
	BLK_SW_VIRTQ_FLUSHING,
	BLK_SW_VIRTQ_SUSPENDED,
};

/**
 * struct virtq_bdev - Backend block device
 * @ctx:	Opaque bdev context given to block device functions
 * @ops:	Block device operation pointers
 */
struct virtq_bdev {
	void *ctx;
	struct snap_bdev_ops *ops;
};

struct blk_virtq_priv {
	volatile enum blk_sw_virtq_state swq_state;
	struct blk_virtq_ctx vq_ctx;
	struct virtq_bdev blk_dev;
	struct ibv_pd *pd;
	struct snap_virtio_blk_queue *snap_vbq;
	struct snap_virtio_blk_queue_attr snap_attr;
	struct snap_dma_q *dma_q;
	struct blk_virtq_cmd *cmd_arr;
	int cmd_cntr;
	int seg_max;
	int size_max;
};

static inline int req_buf_size(int size_max, int seg_max)
{
	return size_max * seg_max + sizeof(struct virtio_blk_outftr)
	       + sizeof(struct virtio_blk_outhdr);
}

static inline uint8_t *cmd2ftr(struct blk_virtq_cmd *cmd)
{
	return cmd->req_buf + sizeof(struct virtio_blk_outhdr) +
	       cmd->total_seg_len;
}

static void free_blk_virtq_cmd(struct blk_virtq_cmd *cmd)
{
	ibv_dereg_mr(cmd->desc_mr);
	ibv_dereg_mr(cmd->req_mr);
	cmd->vq_priv->blk_dev.ops->dma_free(cmd->req_buf);
	free(cmd->iovecs);
	free(cmd->descs);
}

static int blk_virtq_cmd_progress(struct blk_virtq_cmd *cmd,
				  enum virtq_cmd_sm_op_status status);

static void sm_dma_cb(struct snap_dma_completion *self, int status)
{
	enum virtq_cmd_sm_op_status op_status = VIRTQ_CMD_SM_OP_OK;
	struct blk_virtq_cmd *cmd = container_of(self,
						 struct blk_virtq_cmd,
						 dma_comp);

	if (status != IBV_WC_SUCCESS) {
		snap_error("error in dma for queue %d\n",
			   cmd->vq_priv->vq_ctx.idx);
		op_status = VIRTQ_CMD_SM_OP_ERR;
	}
	blk_virtq_cmd_progress(cmd, op_status);
}

static void bdev_io_comp_cb(enum snap_bdev_op_status status, void *done_arg);

static int init_blk_virtq_cmd(struct blk_virtq_cmd *cmd, int idx,
			      uint32_t size_max, uint32_t seg_max,
			      struct blk_virtq_priv *vq_priv)
{
	int num_descs = VIRTIO_NUM_DESC(seg_max);
	uint32_t buf_size;

	cmd->idx = idx;
	cmd->vq_priv = vq_priv;
	cmd->dma_comp.func = sm_dma_cb;
	cmd->descs = calloc(num_descs, sizeof(struct vring_desc));
	cmd->bdev_op_ctx.cb = bdev_io_comp_cb;
	cmd->bdev_op_ctx.user_arg = cmd;
	if (!cmd->descs) {
		snap_error("failed to alloc memory for virtq descs\n");
		goto err;
	}

	cmd->iovecs = calloc(seg_max, sizeof(struct iovec));
	if (!cmd->iovecs) {
		snap_error("failed to alloc memory for virtq iovecs\n");
		goto free_descs;
	}

	buf_size = req_buf_size(size_max, seg_max)
		   + sizeof(struct split_tunnel_comp)
		   + VIRTIO_BLK_ID_BYTES;
	cmd->req_buf = vq_priv->blk_dev.ops->dma_malloc(buf_size);
	if (!cmd->req_buf) {
		snap_error("failed to allocate memory for blk request for queue"
			   " %d\n", vq_priv->vq_ctx.idx);
		goto free_iovecs;
	}
	cmd->device_id = (char *)(cmd->req_buf +
			 req_buf_size(size_max, seg_max) +
			 sizeof(struct split_tunnel_comp));
	cmd->tunnel_comp = (struct split_tunnel_comp *)
			   (cmd->req_buf + buf_size);
	cmd->req_mr = ibv_reg_mr(vq_priv->pd, cmd->req_buf, buf_size,
				 IBV_ACCESS_REMOTE_READ |
				 IBV_ACCESS_REMOTE_WRITE |
				 IBV_ACCESS_LOCAL_WRITE);
	if (!cmd->req_mr) {
		snap_error("failed to register data mr for queue %d cmd"
			   " reqs\n", vq_priv->vq_ctx.idx);
		goto free_req_bufs;
	}

	cmd->desc_mr = ibv_reg_mr(vq_priv->pd, cmd->descs,
				  sizeof(struct vring_desc) * num_descs,
				  IBV_ACCESS_REMOTE_READ |
				  IBV_ACCESS_REMOTE_WRITE |
				  IBV_ACCESS_LOCAL_WRITE);
	if (!cmd->desc_mr) {
		snap_error("failed to register mr for queue %d cmd"
			   " descs\n", vq_priv->vq_ctx.idx);
		goto dereg_req_mr;
	}
	return 0;

dereg_req_mr:
	 ibv_dereg_mr(cmd->req_mr);
free_req_bufs:
	vq_priv->blk_dev.ops->dma_free(cmd->req_buf);
free_iovecs:
	free(cmd->iovecs);
free_descs:
	free(cmd->descs);
err:
	snap_error("failed to initialize cmd %d\n", idx);
	return -1;
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
 * Note: for easy implementation there is a direct mapping between avail_idx
 * and command.
 * Todo: Unify memory into one block for all commands
 *
 * Return: Array of commands structs on success, NULL on error
 */
static struct blk_virtq_cmd *
alloc_blk_virtq_cmd_arr(uint32_t size_max, uint32_t seg_max,
			struct blk_virtq_priv *vq_priv)
{
	int i, j, ret, num = vq_priv->snap_attr.vattr.size;
	struct blk_virtq_cmd *cmd_arr;

	cmd_arr = calloc(num, sizeof(struct blk_virtq_cmd));
	if (!cmd_arr) {
		snap_error("failed to allocate memory for blk_virtq commands\n");
		return NULL;
	}

	for (i = 0; i < num; i++) {
		ret = init_blk_virtq_cmd(&cmd_arr[i], i, size_max, seg_max,
					 vq_priv);
		if (ret)
			goto free_reqs;
	}
	return cmd_arr;

free_reqs:
	for (j = 0; j < i; j++)
		free_blk_virtq_cmd(&cmd_arr[j]);
	free(cmd_arr);
	snap_error("failed allocating commands for queue\n");
	return NULL;
}

static void free_blk_virtq_cmd_arr(struct blk_virtq_priv *priv)
{
	int i;

	for (i = 0; i < priv->snap_attr.vattr.size; i++)
		free_blk_virtq_cmd(&priv->cmd_arr[i]);
	free(priv->cmd_arr);
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
static enum virtq_fetch_desc_status fetch_next_desc(struct blk_virtq_cmd *cmd)
{
	uint64_t srcaddr;
	uint16_t in_ring_desc_addr;
	int ret;

	if (cmd->num_desc == 0)
		in_ring_desc_addr = cmd->avail_idx %
				    cmd->vq_priv->snap_attr.vattr.size;
	else if (cmd->descs[cmd->num_desc - 1].flags & VRING_DESC_F_NEXT)
		in_ring_desc_addr = cmd->descs[cmd->num_desc].next;
	else
		return VIRTQ_FETCH_DESC_DONE;

	srcaddr = cmd->vq_priv->snap_attr.vattr.desc +
		  in_ring_desc_addr * sizeof(struct vring_desc);
	cmd->dma_comp.count = 1;
	ret = snap_dma_q_read(cmd->vq_priv->dma_q, &cmd->descs[cmd->num_desc],
			      sizeof(struct vring_desc), cmd->desc_mr->lkey,
			      srcaddr, cmd->vq_priv->snap_attr.vattr.dma_mkey,
			      &(cmd->dma_comp));
	if (ret)
		return VIRTQ_FETCH_DESC_ERR;
	cmd->num_desc++;
	return VIRTQ_FETCH_DESC_READ;
}

/**
 * set_iovecs() - set iovec for bdev transactions
 * @cmd: command to which iovecs and descs belong to
 *
 * Access to bdev is done via iovecs. Function builds these iovecs to
 * transfer data to/from command buffers. Iovecs are created according
 * to the amount of descriptors given such that each iovec points to one
 * descriptor data. Relationship is iovec[i] points to desc[i+1] since
 * first descriptor is request header which shouldn't be copied to/from bdev.
 *
 * Return: length of data to transfer
 */
static int set_iovecs(struct blk_virtq_cmd *cmd)
{
	uint64_t total_len = 0;
	uint32_t offset = sizeof(struct virtio_blk_outhdr);
	int i;

	for (i = 0; i < cmd->num_desc - NUM_HDR_FTR_DESCS; i++) {
		cmd->iovecs[i].iov_base = cmd->req_buf + offset;
		cmd->iovecs[i].iov_len = cmd->descs[i + 1].len;
		offset += cmd->descs[i + 1].len;
		total_len = total_len + cmd->descs[i + 1].len;
	}
	return total_len;
}

static void bdev_io_comp_cb(enum snap_bdev_op_status status, void *done_arg)
{
	struct blk_virtq_cmd *cmd = done_arg;
	enum virtq_cmd_sm_op_status op_status = VIRTQ_CMD_SM_OP_OK;

	if (snap_unlikely(status != SNAP_BDEV_OP_SUCCESS)) {
		snap_error("Failed iov completion!\n");
		op_status = VIRTQ_CMD_SM_OP_ERR;
	}

	blk_virtq_cmd_progress(cmd, op_status);
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
	enum virtq_fetch_desc_status ret;

	if (status != VIRTQ_CMD_SM_OP_OK) {
		ERR_ON_CMD(cmd, "failed to fetch commands descs, dumping "
			   "command without response\n");
		cmd->state = VIRTQ_CMD_STATE_FATAL_ERR;
		return true;
	}
	ret = fetch_next_desc(cmd);
	if (ret == VIRTQ_FETCH_DESC_ERR) {
		ERR_ON_CMD(cmd, "failed to RDMA READ desc from host\n");
		cmd->state = VIRTQ_CMD_STATE_FATAL_ERR;
		return true;
	} else if (ret == VIRTQ_FETCH_DESC_DONE) {
		cmd->state = VIRTQ_CMD_STATE_READ_REQ;
		return true;
	} else {
		return false;
	}
}

/**
 * virtq_read_req_from_host() - Read request from host
 * @cmd: Command being processed
 *
 * RDMA READ the command request data from host memory.
 * Error after requesting the first RDMA READ is fatal because we can't
 * cancel previous RDMA READ requests done for this command, and since
 * the failing RDMA READ will not return the completion counter will not get
 * to 0 and the callback for the previous RDMA READ requests will not return
 * ToDo: add non-fatal error in case first read fails
 * Note: Last desc is always VRING_DESC_F_READ
 *
 * Return: True if state machine is moved synchronously to the new state
 * (error or no data to fetch) or false if the state transition will be
 * done asynchronously.
 */
static bool virtq_read_req_from_host(struct blk_virtq_cmd *cmd)
{
	struct blk_virtq_priv *priv = cmd->vq_priv;
	int i, avail_buf, ret;
	uint32_t offset;

	cmd->dma_comp.count = 0;
	for (i = 0; i < cmd->num_desc - 1; i++) {
		if ((cmd->descs[i].flags & VRING_DESC_F_WRITE) == 0)
			cmd->dma_comp.count++;
	}

	offset = 0;
	cmd->state = VIRTQ_CMD_STATE_HANDLE_REQ;
	avail_buf = req_buf_size(priv->size_max, priv->seg_max);
	for (i = 0; i < cmd->num_desc - 1; i++) {
		if (cmd->descs[i].flags & VRING_DESC_F_WRITE)
			continue;

		ret = snap_dma_q_read(priv->dma_q, cmd->req_buf + offset,
				      cmd->descs[i].len, cmd->req_mr->lkey,
				      cmd->descs[i].addr,
				      priv->snap_attr.vattr.dma_mkey,
				      &cmd->dma_comp);
		if (ret) {
			cmd->state = VIRTQ_CMD_STATE_FATAL_ERR;
			return true;
		}
		offset += cmd->descs[i].len;
		avail_buf -= cmd->descs[i].len;
	}
	return false;
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
	struct virtq_bdev *bdev = &cmd->vq_priv->blk_dev;
	int ret, len, qid = cmd->vq_priv->vq_ctx.idx;
	struct virtio_blk_outhdr *req_hdr_p;
	uint64_t num_blocks;
	uint32_t blk_size;
	const char *dev_name;

	if (status != VIRTQ_CMD_SM_OP_OK) {
		ERR_ON_CMD(cmd, "failed to get request data, returning"
			   " failure\n");
		cmd->blk_req_ftr.status = VIRTIO_BLK_S_IOERR;
		cmd->state = VIRTQ_CMD_STATE_WRITE_STATUS;
		return true;
	}

	req_hdr_p = (struct virtio_blk_outhdr *)cmd->req_buf;
	switch (req_hdr_p->type) {
	case VIRTIO_BLK_T_OUT:
		cmd->total_seg_len = set_iovecs(cmd);
		cmd->state = VIRTQ_CMD_STATE_T_OUT_IOV_DONE;
		ret = bdev->ops->write(bdev->ctx, cmd->iovecs,
				       cmd->num_desc - NUM_HDR_FTR_DESCS,
				       req_hdr_p->sector * BDEV_SECTOR_SIZE,
				       cmd->total_seg_len,
				       &cmd->bdev_op_ctx, qid);
		break;
	case VIRTIO_BLK_T_IN:
		cmd->total_seg_len = set_iovecs(cmd);
		cmd->state = VIRTQ_CMD_STATE_T_IN_IOV_DONE;
		ret = bdev->ops->read(bdev->ctx, cmd->iovecs,
				      cmd->num_desc - NUM_HDR_FTR_DESCS,
				      req_hdr_p->sector * BDEV_SECTOR_SIZE,
				      cmd->total_seg_len,
				      &cmd->bdev_op_ctx, qid);
		break;
	case VIRTIO_BLK_T_FLUSH:
		req_hdr_p = (struct virtio_blk_outhdr *)cmd->req_buf;
		if (req_hdr_p->sector != 0) {
			ERR_ON_CMD(cmd, "sector must be zero for flush "
				   "command\n");
			ret = -1;
		} else {
			cmd->state = VIRTQ_CMD_STATE_WRITE_STATUS;
			num_blocks = bdev->ops->get_num_blocks(bdev->ctx);
			blk_size = bdev->ops->get_block_size(bdev->ctx);
			ret = bdev->ops->flush(bdev->ctx, 0,
					       num_blocks * blk_size,
					       &cmd->bdev_op_ctx, qid);
		}
		break;
	case VIRTIO_BLK_T_GET_ID:
		cmd->state = VIRTQ_CMD_STATE_WRITE_STATUS;
		dev_name = bdev->ops->get_bdev_name(bdev->ctx);
		ret = snprintf(cmd->device_id, VIRTIO_BLK_ID_BYTES, "%s",
			       dev_name);
		if (ret < 0) {
			snap_error("failed to read block id\n");
			cmd->blk_req_ftr.status = VIRTIO_BLK_S_UNSUPP;
			return true;
		}
		cmd->dma_comp.count = 1;
		if (VIRTIO_BLK_ID_BYTES > cmd->descs[1].len)
			len = cmd->descs[1].len;
		else
			len = VIRTIO_BLK_ID_BYTES;
		ret = snap_dma_q_write(cmd->vq_priv->dma_q,
				       cmd->device_id,
				       len,
				       cmd->req_mr->lkey,
				       cmd->descs[1].addr,
				       cmd->vq_priv->snap_attr.vattr.dma_mkey,
				       &(cmd->dma_comp));
		break;
	default:
		ERR_ON_CMD(cmd, "invalid command - requested command type "
				"0x%x is not implemented\n", req_hdr_p->type);
		cmd->state = VIRTQ_CMD_STATE_WRITE_STATUS;
		cmd->blk_req_ftr.status = VIRTIO_BLK_S_UNSUPP;
		return true;
	}

	if (ret) {
		ERR_ON_CMD(cmd, "failed while executing command %d \n",
			   req_hdr_p->type);
		cmd->blk_req_ftr.status = VIRTIO_BLK_S_IOERR;
		cmd->state = VIRTQ_CMD_STATE_WRITE_STATUS;
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

	if (status != VIRTQ_CMD_SM_OP_OK) {
		ERR_ON_CMD(cmd, "failed to read from block device, send ioerr"
			   " to host\n");
		cmd->blk_req_ftr.status = VIRTIO_BLK_S_IOERR;
		cmd->state = VIRTQ_CMD_STATE_WRITE_STATUS;
		return true;
	}

	cmd->dma_comp.count = cmd->num_desc - NUM_HDR_FTR_DESCS;
	cmd->state = VIRTQ_CMD_STATE_WRITE_STATUS;
	for (i = 0; i < cmd->num_desc - NUM_HDR_FTR_DESCS; i++) {
		ret = snap_dma_q_write(cmd->vq_priv->dma_q,
				       cmd->iovecs[i].iov_base,
				       cmd->iovecs[i].iov_len,
				       cmd->req_mr->lkey,
				       cmd->descs[i + 1].addr,
				       cmd->vq_priv->snap_attr.vattr.dma_mkey,
				       &(cmd->dma_comp));
		if (ret) {
			cmd->blk_req_ftr.status = VIRTIO_BLK_S_IOERR;
			cmd->state = VIRTQ_CMD_STATE_WRITE_STATUS;
			return true;
		}
		cmd->total_in_len += cmd->iovecs[i].iov_len;
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
	cmd->state = VIRTQ_CMD_STATE_WRITE_STATUS;
	if (status != VIRTQ_CMD_SM_OP_OK)
		cmd->blk_req_ftr.status = VIRTIO_BLK_S_IOERR;
}

/**
 * sm_write_status() - Write command status to host memory upon finish
 * @cmd:	command which requested the write
 * @status:	callback status, expected 0 for no errors
 *
 * Return: True if state machine is moved synchronously to the new state
 * (error cases) or false if the state transition will be done asynchronously.
 */
static bool sm_write_status(struct blk_virtq_cmd *cmd,
			    enum virtq_cmd_sm_op_status status)
{
	int ret;

	if (status != VIRTQ_CMD_SM_OP_OK)
		cmd->blk_req_ftr.status = VIRTIO_BLK_S_IOERR;

	memcpy(cmd2ftr(cmd), &cmd->blk_req_ftr,
	       sizeof(struct virtio_blk_outftr));
	cmd->state = VIRTQ_CMD_STATE_SEND_COMP;
	cmd->dma_comp.count = 1;
	ret = snap_dma_q_write(cmd->vq_priv->dma_q, cmd2ftr(cmd),
			       sizeof(struct virtio_blk_outftr),
			       cmd->req_mr->lkey,
			       cmd->descs[cmd->num_desc - 1].addr,
			       cmd->vq_priv->snap_attr.vattr.dma_mkey,
			       &cmd->dma_comp);
	if (ret) {
		cmd->state = VIRTQ_CMD_STATE_FATAL_ERR;
		return true;
	}
	cmd->total_in_len += sizeof(struct virtio_blk_outftr);
	return false;
}

/**
 * sm_send_completion() - send command completion to FW
 * @cmd: Command being processed
 * @status: Status of callback
 */
static void sm_send_completion(struct blk_virtq_cmd *cmd,
			       enum virtq_cmd_sm_op_status status)
{
	struct virtio_blk_outhdr *req_hdr_p;
	int ret;

	if (status != VIRTQ_CMD_SM_OP_OK) {
		snap_error("failed to write the request status field\n");
		cmd->state = VIRTQ_CMD_STATE_FATAL_ERR;
		return;
	}

	req_hdr_p = (struct virtio_blk_outhdr *)cmd->req_buf;
	cmd->tunnel_comp->avail_idx = cmd->avail_idx;
	cmd->tunnel_comp->len = cmd->total_in_len;
	ret = snap_dma_q_send_completion(cmd->vq_priv->dma_q,
					 cmd->tunnel_comp,
					 sizeof(struct split_tunnel_comp));
	if (ret) {
		ERR_ON_CMD(cmd, "failed to second completion\n");
		cmd->state = VIRTQ_CMD_STATE_FATAL_ERR;
	} else {
		cmd->state = VIRTQ_CMD_STATE_RELEASE;
	}
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
		snap_debug("virtq cmd sm state: %d\n", cmd->state);
		switch (cmd->state) {
		case VIRTQ_CMD_STATE_IDLE:
			snap_error("command in invalid state %d\n",
				   VIRTQ_CMD_STATE_IDLE);
			break;
		case VIRTQ_CMD_STATE_FETCH_CMD_DESCS:
			repeat = sm_fetch_cmd_descs(cmd, status);
			break;
		case VIRTQ_CMD_STATE_READ_REQ:
			repeat = virtq_read_req_from_host(cmd);
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
			sm_send_completion(cmd, status);
			repeat = true;
			break;
		case VIRTQ_CMD_STATE_RELEASE:
			cmd->vq_priv->cmd_cntr--;
			break;
		case VIRTQ_CMD_STATE_FATAL_ERR:
			cmd->vq_priv->vq_ctx.fatal_err = -1;
			break;
		default:
			snap_error("reached invalid state %d\n", cmd->state);
		}
	};

	return 0;
}

/**
 * blk_virtq_rx_cb() - callback for new command received from host
 * @q:   	queue on which command was received
 * @data:	pointer to data sent for the command - should be
 * 		command header and optional descriptor list
 * @data_len:	length of data
 * @imm_data:	immediate data
 *
 * Received command is assigned to a memory slot in the command array according
 * to avail_idx. Function starts the state machine processing for this command
 */
static void blk_virtq_rx_cb(struct snap_dma_q *q, void *data,
			    uint32_t data_len, uint32_t imm_data)
{
	struct blk_virtq_priv *priv = (struct blk_virtq_priv *)q->uctx;
	void *descs = data + sizeof(struct split_tunnel_req_hdr);
	enum virtq_cmd_sm_op_status status = VIRTQ_CMD_SM_OP_OK;
	int cmd_idx, len;
	struct blk_virtq_cmd *cmd;
	struct split_tunnel_req_hdr *split_hdr;

	split_hdr = (struct split_tunnel_req_hdr *)data;

	cmd_idx = split_hdr->avail_idx % priv->snap_attr.vattr.size;
	cmd = &priv->cmd_arr[cmd_idx];
	cmd->num_desc = split_hdr->num_desc;
	cmd->avail_idx = split_hdr->avail_idx;
	cmd->total_seg_len = 0;
	cmd->total_in_len = 0;
	cmd->blk_req_ftr.status = VIRTIO_BLK_S_OK;

	if (split_hdr->num_desc) {
		len = sizeof(struct vring_desc) * split_hdr->num_desc;
		memcpy(cmd->descs, descs, len);
	}

	priv->cmd_cntr++;
	cmd->state = VIRTQ_CMD_STATE_FETCH_CMD_DESCS;
	blk_virtq_cmd_progress(cmd, status);
}

/**
 * blk_virtq_create() - Creates a new blk virtq object, along with RDMA QPs.
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
struct blk_virtq_ctx *blk_virtq_create(struct snap_bdev_ops *bdev_ops,
				       void *bdev, struct snap_device *snap_dev,
				       struct blk_virtq_create_attr *attr)
{
	struct snap_dma_q_create_attr rdma_qp_create_attr = {};
	struct snap_virtio_blk_queue_attr qattr = {};
	struct blk_virtq_ctx *vq_ctx;
	struct blk_virtq_priv *vq_priv;
	int num_descs = VIRTIO_NUM_DESC(attr->seg_max);

	vq_priv = calloc(1, sizeof(struct blk_virtq_priv));
	if (!vq_priv)
		goto err;

	vq_ctx = &vq_priv->vq_ctx;
	vq_ctx->priv = vq_priv;
	vq_priv->blk_dev.ops = bdev_ops;
	vq_priv->blk_dev.ctx = bdev;
	vq_priv->pd = attr->pd;
	vq_ctx->idx = attr->idx;
	vq_ctx->fatal_err = 0;
	vq_priv->seg_max = attr->seg_max;
	vq_priv->size_max = attr->size_max;
	vq_priv->snap_attr.vattr.size = attr->queue_size;
	vq_priv->swq_state = BLK_SW_VIRTQ_RUNNING;

	vq_priv->cmd_arr = alloc_blk_virtq_cmd_arr(attr->size_max,
						   attr->seg_max, vq_priv);
	if (!vq_priv->cmd_arr) {
		snap_error("failed allocating cmd list for queue %d\n",
			   attr->idx);
		goto release_priv;
	}
	vq_priv->cmd_cntr = 0;

	rdma_qp_create_attr.tx_qsize = attr->queue_size;
	rdma_qp_create_attr.tx_elem_size = sizeof(struct split_tunnel_comp);
	rdma_qp_create_attr.rx_qsize = attr->queue_size;
	rdma_qp_create_attr.rx_elem_size = sizeof(struct split_tunnel_req_hdr) +
					   num_descs * sizeof(struct vring_desc);
	rdma_qp_create_attr.uctx = vq_priv;
	rdma_qp_create_attr.rx_cb = blk_virtq_rx_cb;

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
	vq_priv->snap_attr.vattr.max_tunnel_desc = attr->max_tunnel_desc;
	vq_priv->snap_attr.vattr.event_qpn_or_msix = attr->msix_vector;
	vq_priv->snap_attr.vattr.pd = attr->pd;
	vq_priv->snap_attr.qp = snap_dma_q_get_fw_qp(vq_priv->dma_q);
	if (!vq_priv->snap_attr.qp) {
		snap_error("no fw qp exist when trying to create virtq\n");
		goto release_rdma_qp;
	}
	vq_priv->snap_vbq = snap_virtio_blk_create_queue(snap_dev,
							 &vq_priv->snap_attr);
	if (!vq_priv->snap_vbq) {
		snap_error("failed creating VIRTQ fw element\n");
		goto release_rdma_qp;
	}
	if (snap_virtio_blk_query_queue(vq_priv->snap_vbq,
					&vq_priv->snap_attr)) {
		snap_error("failed query created snap virtio blk queue\n");
		goto destroy_virtio_blk_queue;
	}
	qattr.vattr.state = SNAP_VIRTQ_STATE_RDY;
	if (snap_virtio_blk_modify_queue(vq_priv->snap_vbq,
					 SNAP_VIRTIO_BLK_QUEUE_MOD_STATE,
					 &qattr)) {
		snap_error("failed to change virtq to READY state\n");
		goto destroy_virtio_blk_queue;
	}

	snap_debug("created VIRTQ %d succesfully\n", attr->idx);
	return vq_ctx;

destroy_virtio_blk_queue:
	snap_virtio_blk_destroy_queue(vq_priv->snap_vbq);
release_rdma_qp:
	snap_dma_q_destroy(vq_priv->dma_q);
dealloc_cmd_arr:
	free_blk_virtq_cmd_arr(vq_priv);
release_priv:
	free(vq_priv);
err:
	snap_error("failed creating blk_virtq %d\n", attr->idx);
	return NULL;
}

/**
 * blk_virtq_destroy() - Destroyes blk virtq object
 * @q: queue to be destryoed
 *
 * Context: 1. Destroy should be called only when queue is in suspended state.
 * 	    2. blk_virtq_progress() should not be called during destruction.
 *
 * Return: void
 */
void blk_virtq_destroy(struct blk_virtq_ctx *q)
{
	struct blk_virtq_priv *vq_priv = q->priv;

	snap_debug("destroying queue %d\n", q->idx);

	if (vq_priv->swq_state != BLK_SW_VIRTQ_SUSPENDED)
		snap_error("Error destroying queue %d while not in suspended"
			   " state\n", q->idx);

	if (snap_virtio_blk_destroy_queue(vq_priv->snap_vbq))
		snap_error("error destroying blk_virtq\n");

	snap_dma_q_destroy(vq_priv->dma_q);
	free_blk_virtq_cmd_arr(vq_priv);
	free(vq_priv);
}

/**
 * blk_virtq_progress() - Progress RDMA QPs,  Polls on QPs CQs
 * @q:	queue to progress
 *
 * Context: Not thread safe
 *
 * Return: error code on failure, 0 on success
 */
int blk_virtq_progress(struct blk_virtq_ctx *q)
{
	struct blk_virtq_priv *priv = q->priv;

	if (priv->swq_state == BLK_SW_VIRTQ_FLUSHING && priv->cmd_cntr == 0) {
		priv->swq_state = BLK_SW_VIRTQ_SUSPENDED;
		return 0;
	}

	return snap_dma_q_progress(priv->dma_q);
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
	struct blk_virtq_priv *priv = q->priv;

	if (priv->swq_state != BLK_SW_VIRTQ_RUNNING) {
		snap_debug("Suspend for queue %d was already requested\n",
			   q->idx);
		return -EBUSY;
	}

	priv->swq_state = BLK_SW_VIRTQ_FLUSHING;
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
	struct blk_virtq_priv *priv = q->priv;
	return priv->swq_state == BLK_SW_VIRTQ_SUSPENDED;
}

struct snap_dma_q *get_dma_q(struct blk_virtq_ctx *ctx) {
	struct blk_virtq_priv *vpriv = ctx->priv;
	return vpriv->dma_q;
}

int set_dma_mkey(struct blk_virtq_ctx *ctx, uint32_t mkey)
{
	struct blk_virtq_priv *vpriv = ctx->priv;
	vpriv->snap_attr.vattr.dma_mkey = mkey;
	return 0;
}
