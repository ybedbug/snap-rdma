#include <unistd.h>
#include <semaphore.h>
#include <time.h>

#include "snap_channel.h"
#include "snap_rdma_channel.h"

#define SNAP_CHANNEL_POLL_BATCH 16
#define SNAP_CHANNEL_MAX_COMPLETIONS 64
/* aligns x to y, y must be a power of 2 */
#define ALIGN(x, y)  (((x)+(y)-1) & ~((y)-1))


static inline bool is_power_of_two(uint x)
{
	return (x != 0) && ((x & (x - 1)) == 0);
}

static pthread_mutex_t port_lock = PTHREAD_MUTEX_INITIALIZER;
static int rdma_port_idx;

static int snap_channel_rdma_rw(struct snap_rdma_channel *schannel,
		uint64_t local_addr, uint32_t lkey, int len,
		uint64_t remote_addr, uint32_t rkey, int opcode,
		struct ibv_send_wr *send_wr)
{
	struct ibv_send_wr rdma_wr = {};
	struct ibv_send_wr *bad_wr;
	struct ibv_sge sge;
	int ret;

	sge.addr = local_addr;
	sge.length = len;
	sge.lkey = lkey;

	rdma_wr.opcode = opcode;
	rdma_wr.wr.rdma.rkey = rkey;
	rdma_wr.wr.rdma.remote_addr = remote_addr;
	rdma_wr.send_flags = IBV_SEND_SIGNALED;
	rdma_wr.sg_list = &sge;
	rdma_wr.num_sge = 1;
	rdma_wr.next = NULL;
	rdma_wr.wr_id = (uintptr_t)send_wr;

	ret = ibv_post_send(schannel->qp, &rdma_wr, &bad_wr);
	if (ret)
		snap_channel_error("schannel 0x%p failed to post rdma\n",
				   schannel);
	else
		snap_channel_info("schannel 0x%p issued rdma %d op rkey 0x%x remote_addr 0x%lx len %d\n",
				  schannel, opcode, rkey, remote_addr, len);

	return ret;
}

static int snap_channel_start_dirty_track(struct snap_rdma_channel *schannel,
		struct mlx5_snap_common_command *cmd,
		struct mlx5_snap_completion *cqe)
{
	struct mlx5_snap_start_dirty_log_command *dirty_cmd;
	struct snap_dirty_pages *dirty_pages;
	int ret = 0;

	dirty_cmd = (struct mlx5_snap_start_dirty_log_command *)cmd;
	if (!is_power_of_two(dirty_cmd->page_size)) {
		ret = -EINVAL;
		snap_channel_error("page_size must be a power of 2\n");
		cqe->status = MLX5_SNAP_SC_INVALID_FIELD;
		goto out;
	}
	snap_channel_info("schannel 0x%p start track with %u page size\n",
			  schannel, dirty_cmd->page_size);

	dirty_pages = &schannel->dirty_pages;
	pthread_mutex_lock(&dirty_pages->copy_lock);
	if (dirty_pages->bmap) {
		ret = -EPERM;
		snap_channel_error("dirty pages logging have been started\n");
		cqe->status = MLX5_SNAP_SC_ALREADY_STARTED_LOG;
		goto out_unlock;
	}
	dirty_pages->bmap = calloc(1, SNAP_CHANNEL_INITIAL_BITMAP_SIZE);
	if (!dirty_pages->bmap) {
		ret = -ENOMEM;
		snap_channel_error("failed to allocate dirty pages bitmap\n");
		cqe->status = MLX5_SNAP_SC_INTERNAL;
		goto out_unlock;
	}

	dirty_pages->bmap_num_elements = SNAP_CHANNEL_INITIAL_BITMAP_ARRAY_SZ;
	dirty_pages->highest_dirty_element = 0;
	dirty_pages->page_size = dirty_cmd->page_size;

	ret = schannel->base.ops->start_dirty_pages_track(schannel->base.data);
	if (ret) {
		snap_channel_info("schannel 0x%p failed to start track\n",
				  schannel);
		free(dirty_pages->bmap);
		dirty_pages->bmap = NULL;
		cqe->status = MLX5_SNAP_SC_INTERNAL;
	} else {
		snap_channel_info("schannel 0x%p started dirty track\n",
				  schannel);
		cqe->status = MLX5_SNAP_SC_SUCCESS;
	}
out_unlock:
	pthread_mutex_unlock(&dirty_pages->copy_lock);
out:
	return ret;
}

static int snap_channel_stop_dirty_track(struct snap_rdma_channel *schannel,
		struct mlx5_snap_common_command *cmd,
		struct mlx5_snap_completion *cqe)
{
	struct snap_dirty_pages *dirty_pages;
	int ret = 0;

	snap_channel_info("schannel 0x%p stop tracking\n", schannel);

	dirty_pages = &schannel->dirty_pages;
	pthread_mutex_lock(&dirty_pages->copy_lock);
	if (!dirty_pages->bmap) {
		ret = -EPERM;
		snap_channel_error("dirty pages logging already stopped or "
				   "didn't start\n");
		cqe->status = MLX5_SNAP_SC_ALREADY_STOPPED_LOG;
		goto out;
	}

	pthread_mutex_unlock(&dirty_pages->copy_lock);
	/* on success, all the dirty pages were reported to the channel */
	ret = schannel->base.ops->stop_dirty_pages_track(schannel->base.data);
	pthread_mutex_lock(&dirty_pages->copy_lock);
	if (ret) {
		snap_channel_info("schannel 0x%p failed to stop tracking\n",
				  schannel);
		cqe->status = MLX5_SNAP_SC_INTERNAL;
	} else {
		snap_channel_info("schannel 0x%p stop dirty track\n",
				  schannel);
		cqe->status = MLX5_SNAP_SC_SUCCESS;
	}

out:
	pthread_mutex_unlock(&dirty_pages->copy_lock);
	return ret;
}

static int snap_channel_get_dirty_size(struct snap_rdma_channel *schannel,
		struct mlx5_snap_common_command *cmd,
		struct mlx5_snap_completion *cqe)
{
	struct snap_dirty_pages *dirty_pages;
	int ret = 0;

	dirty_pages = &schannel->dirty_pages;
	pthread_mutex_lock(&dirty_pages->copy_lock);
	/* In case we've already cached the dirty pages, don't cache more. */
	if (dirty_pages->copy_bmap_num_elements) {
		cqe->result = dirty_pages->copy_bmap_num_elements * SNAP_CHANNEL_BITMAP_ELEM_SZ;
		cqe->status = MLX5_SNAP_SC_SUCCESS;
		goto out_unlock_copy;
	}
	/*
	 * copy current dirty "valid" bitmap to a bounce buffer and return the
	 * size of the buffer. Also reset the "valid" bitmap.
	 */
	pthread_mutex_lock(&dirty_pages->lock);
	if (dirty_pages->highest_dirty_element) {
		dirty_pages->copy_bmap_num_elements = dirty_pages->highest_dirty_element;
		dirty_pages->copy_bmap = calloc(dirty_pages->highest_dirty_element,
						SNAP_CHANNEL_BITMAP_ELEM_SZ);
		if (!dirty_pages->copy_bmap) {
			ret = -ENOMEM;
			snap_channel_error("failed to allocate copy bitmap\n");
			cqe->status = MLX5_SNAP_SC_INTERNAL;
			goto out_unlock;
		}
		memcpy(dirty_pages->copy_bmap, dirty_pages->bmap,
		       dirty_pages->highest_dirty_element * SNAP_CHANNEL_BITMAP_ELEM_SZ);
		/* reset the bitmap */
		memset(dirty_pages->bmap, 0,
		       dirty_pages->highest_dirty_element * SNAP_CHANNEL_BITMAP_ELEM_SZ);
		dirty_pages->highest_dirty_element = 0;
		cqe->result = dirty_pages->copy_bmap_num_elements * SNAP_CHANNEL_BITMAP_ELEM_SZ;
		cqe->status = MLX5_SNAP_SC_SUCCESS;
	} else {
		cqe->result = 0;
		cqe->status = MLX5_SNAP_SC_SUCCESS;
	}
out_unlock:
	pthread_mutex_unlock(&dirty_pages->lock);
out_unlock_copy:
	pthread_mutex_unlock(&dirty_pages->copy_lock);

	return ret;
}

static int snap_channel_report_dirty_pages(struct snap_rdma_channel *schannel,
		struct mlx5_snap_common_command *cmd,
		struct mlx5_snap_completion *cqe,
		struct ibv_send_wr *send_wr)
{
	struct snap_dirty_pages *dirty_pages;
	__u32 length;
	int ret = 0;

	dirty_pages = &schannel->dirty_pages;
	pthread_mutex_lock(&dirty_pages->copy_lock);
	length = dirty_pages->copy_bmap_num_elements * SNAP_CHANNEL_BITMAP_ELEM_SZ;
	if (cmd->length != length) {
		cqe->status = MLX5_SNAP_SC_INVALID_FIELD;
		ret = -EINVAL;
		goto out_unlock;
	}

	if (length) {
		struct ibv_mr *mr;

		mr = ibv_reg_mr(schannel->pd, dirty_pages->copy_bmap, length,
				IBV_ACCESS_LOCAL_WRITE);
		if (!mr) {
			snap_channel_error("schannel 0x%p bitmap reg_mr failed\n",
					   schannel);
			cqe->status = MLX5_SNAP_SC_INTERNAL;
			goto out_unlock;
		}
		ret = snap_channel_rdma_rw(schannel, (uintptr_t)dirty_pages->copy_bmap,
					   mr->lkey, length, cmd->addr,
					   cmd->key, IBV_WR_RDMA_WRITE, send_wr);
		if (ret) {
			ibv_dereg_mr(mr);
			cqe->status = MLX5_SNAP_SC_INTERNAL;
			goto out_unlock;
		}

		/* dereg prev MR */
		if (dirty_pages->copy_mr)
			ibv_dereg_mr(dirty_pages->copy_mr);
		dirty_pages->copy_mr = mr;
	} else {
		snap_channel_error("schannel 0x%p no dirty pages to report\n",
				   schannel);
		cqe->status = MLX5_SNAP_SC_INVALID_FIELD;
		ret = -EINVAL;
	}
out_unlock:
	pthread_mutex_unlock(&dirty_pages->copy_lock);

	return ret;
}

static int snap_channel_freeze_device(struct snap_rdma_channel *schannel,
		struct mlx5_snap_common_command *cmd,
		struct mlx5_snap_completion *cqe)
{
	int ret;

	ret = schannel->base.ops->freeze(schannel->base.data);
	if (!ret) {
		snap_channel_info("schannel 0x%p freezed device success\n",
				  schannel);
		cqe->status = MLX5_SNAP_SC_SUCCESS;
	} else {
		snap_channel_error("schannel 0x%p failed to freeze device\n",
				   schannel);
		cqe->status = MLX5_SNAP_SC_INTERNAL;
	}

	return ret;
}

static int snap_channel_unfreeze_device(struct snap_rdma_channel *schannel,
		struct mlx5_snap_common_command *cmd,
		struct mlx5_snap_completion *cqe)
{
	int ret;

	ret = schannel->base.ops->unfreeze(schannel->base.data);
	if (!ret) {
		snap_channel_info("schannel 0x%p unfreezed device success\n",
				  schannel);
		cqe->status = MLX5_SNAP_SC_SUCCESS;
	} else {
		snap_channel_error("schannel 0x%p failed to unfreeze device\n",
				   schannel);
		cqe->status = MLX5_SNAP_SC_INTERNAL;
	}

	return ret;
}

static int snap_channel_quiesce_device(struct snap_rdma_channel *schannel,
		struct mlx5_snap_common_command *cmd,
		struct mlx5_snap_completion *cqe)
{
	int ret;

	ret = schannel->base.ops->quiesce(schannel->base.data);
	if (!ret) {
		snap_channel_info("schannel 0x%p quiesce device success\n",
				  schannel);
		cqe->status = MLX5_SNAP_SC_SUCCESS;
	} else {
		snap_channel_error("schannel 0x%p failed to quiesce device\n",
				   schannel);
		cqe->status = MLX5_SNAP_SC_INTERNAL;
	}

	return ret;
}

static int snap_channel_unquiesce_device(struct snap_rdma_channel *schannel,
		struct mlx5_snap_common_command *cmd,
		struct mlx5_snap_completion *cqe)
{
	int ret;

	ret = schannel->base.ops->unquiesce(schannel->base.data);
	if (!ret) {
		snap_channel_info("schannel 0x%p unquiesce device success\n",
				  schannel);
		cqe->status = MLX5_SNAP_SC_SUCCESS;
	} else {
		snap_channel_error("schannel 0x%p failed to unquiesce device\n",
				   schannel);
		cqe->status = MLX5_SNAP_SC_INTERNAL;
	}

	return ret;
}

static int snap_channel_get_state_size(struct snap_rdma_channel *schannel,
		struct mlx5_snap_common_command *cmd,
		struct mlx5_snap_completion *cqe)
{
	struct snap_internal_state *state = &schannel->state;
	int ret = 0, size;

	pthread_mutex_lock(&state->lock);
	if (state->state_size) {
		snap_channel_info("schannel 0x%p state size is already set to %u\n",
				  schannel, state->state_size);
		cqe->result = state->state_size;
		cqe->status = MLX5_SNAP_SC_SUCCESS;
		goto out_unlock;
	}

	size = schannel->base.ops->get_state_size(schannel->base.data);
	if (size < 0) {
		snap_channel_error("failed to get state size\n");
		ret = -EINVAL;
		cqe->status = MLX5_SNAP_SC_INTERNAL;
	} else {
		if (size) {
			snap_channel_info("schannel 0x%p state size is %d\n",
					  schannel, size);

			state->state = calloc(size, 1);
			if (!state->state) {
				ret = -ENOMEM;
				snap_channel_error("failed to allocate state\n");
				cqe->status = MLX5_SNAP_SC_INTERNAL;
				goto out_unlock;
			}
			ret = schannel->base.ops->copy_state(schannel->base.data,
							     state->state,
							     size, false);
			if (ret) {
				snap_channel_error("failed to copy state to buffer\n");
				cqe->status = MLX5_SNAP_SC_INTERNAL;
				goto out_unlock;
			}
		}
		state->state_size = size;
		cqe->result = size;
		cqe->status = MLX5_SNAP_SC_SUCCESS;
	}
out_unlock:
	pthread_mutex_unlock(&state->lock);

	return ret;
}

static int snap_channel_read_state(struct snap_rdma_channel *schannel,
		struct mlx5_snap_common_command *cmd,
		struct mlx5_snap_completion *cqe,
		struct ibv_send_wr *send_wr)
{
	struct snap_internal_state *state = &schannel->state;
	struct mlx5_snap_rw_command *rw;
	__u32 length;
	int ret;

	rw = (struct mlx5_snap_rw_command *)cmd;
	length = rw->length;

	if (rw->offset) {
		snap_channel_error("state offset is not supported\n");
		cqe->status = MLX5_SNAP_SC_INTERNAL;
		ret = -EINVAL;
		goto out;
	}
	pthread_mutex_lock(&state->lock);

	if (state->state_size < length) {
		snap_channel_error("invalid state length asked\n");
		cqe->status = MLX5_SNAP_SC_INVALID_FIELD;
		ret = -EINVAL;
		goto out_unlock;
	}

	if (length) {
		struct ibv_mr *mr;

		mr = ibv_reg_mr(schannel->pd, state->state, length,
				IBV_ACCESS_LOCAL_WRITE);
		if (!mr) {
			snap_channel_error("schannel 0x%p state reg_mr failed\n",
					   schannel);
			cqe->status = MLX5_SNAP_SC_INTERNAL;
			ret = -EINVAL;
			goto out_unlock;
		}
		ret = snap_channel_rdma_rw(schannel, (uintptr_t)state->state,
					   mr->lkey, length, rw->addr,
					   rw->key, IBV_WR_RDMA_WRITE,
					   send_wr);
		if (ret) {
			ibv_dereg_mr(mr);
			cqe->status = MLX5_SNAP_SC_DATA_XFER_ERROR;
			ret = -EINVAL;
			goto out_unlock;
		}

		/* dereg prev MR */
		if (state->state_mr)
			ibv_dereg_mr(state->state_mr);
		state->state_mr = mr;
		cqe->status = MLX5_SNAP_SC_SUCCESS;
	} else {
		snap_channel_info("schannel 0x%p no state to report\n",
				  schannel);
		cqe->status = MLX5_SNAP_SC_INVALID_FIELD;
		ret = -EINVAL;
	}

out_unlock:
	pthread_mutex_unlock(&state->lock);
out:
	return ret;
}

static int snap_channel_write_state(struct snap_rdma_channel *schannel,
		struct mlx5_snap_common_command *cmd,
		struct mlx5_snap_completion *cqe,
		struct ibv_send_wr *send_wr)
{
	struct snap_internal_state *state = &schannel->state;
	struct mlx5_snap_rw_command *rw;
	__u32 length;
	int ret = 0;

	rw = (struct mlx5_snap_rw_command *)cmd;
	length = rw->length;

	if (rw->offset) {
		snap_channel_error("state offset is not supported\n");
		ret = -EINVAL;
		cqe->status = MLX5_SNAP_SC_INTERNAL;
		goto out;
	}
	pthread_mutex_lock(&state->lock);

	if (length) {
		struct ibv_mr *mr;

		/* clean prev state */
		if (state->state) {
			free(state->state);
			state->state = NULL;
			state->state_size = 0;
		}
		/* dereg prev MR */
		if (state->state_mr) {
			ibv_dereg_mr(state->state_mr);
			state->state_mr = NULL;
		}

		state->state = calloc(length, 1);
		if (!state->state) {
			ret = -ENOMEM;
			snap_channel_error("failed to alloc resume state\n");
			cqe->status = MLX5_SNAP_SC_INTERNAL;
			goto out_unlock;
		}

		mr = ibv_reg_mr(schannel->pd, state->state, length,
				IBV_ACCESS_LOCAL_WRITE);
		if (!mr) {
			snap_channel_error("schannel 0x%p state reg_mr failed\n",
					   schannel);
			free(state->state);
			state->state = NULL;
			state->state_size = 0;
			cqe->status = MLX5_SNAP_SC_INTERNAL;
			ret = -EINVAL;
			goto out_unlock;
		}

		ret = snap_channel_rdma_rw(schannel, (uintptr_t)state->state,
					   mr->lkey, length, rw->addr,
					   rw->key, IBV_WR_RDMA_READ, send_wr);
		if (ret) {
			free(state->state);
			state->state = NULL;
			state->state_size = 0;
			ibv_dereg_mr(mr);
			cqe->status = MLX5_SNAP_SC_DATA_XFER_ERROR;
			goto out_unlock;
		}

		state->state_mr = mr;

		ret = schannel->base.ops->copy_state(schannel->base.data,
						     state->state,
						     length, true);
		if (ret) {
			snap_channel_error("failed to copy state from buffer\n");
			cqe->status = MLX5_SNAP_SC_INTERNAL;
			goto out_unlock;
		}

		state->state_size = length;
		cqe->status = MLX5_SNAP_SC_SUCCESS;
	} else {
		snap_channel_info("schannel 0x%p no state to write\n",
				  schannel);
		cqe->status = MLX5_SNAP_SC_INVALID_FIELD;
		ret = -EINVAL;
	}

out_unlock:
	pthread_mutex_unlock(&state->lock);
out:
	return ret;
}

static int snap_channel_process_cmd(struct snap_rdma_channel *schannel,
		struct mlx5_snap_common_command *cmd)
{
	struct ibv_send_wr *send_wr, *bad_wr = NULL;
	struct mlx5_snap_completion *cqe;
	__u8 opcode = cmd->opcode;
	 __u64 idx;
	int ret = 0;
	bool send_rsp = true;

	snap_channel_info("schannel 0x%p got CMD opcode %u id %u\n",
			  schannel, opcode, cmd->command_id);

	if (SNAP_CHANNEL_QUEUE_SIZE < cmd->command_id) {
		snap_channel_error("schannel 0x%p invalid CMDID opcode %u id %u\n", schannel,
			  opcode, cmd->command_id);
		return -EINVAL;
	}

	idx = cmd->command_id - 1;
	send_wr = &schannel->rsp_wr[idx];
	cqe = (struct mlx5_snap_completion *) (schannel->rsp_buf +
			idx * SNAP_CHANNEL_RSP_SIZE);
	cqe->status = MLX5_SNAP_SC_SUCCESS;

	cqe->command_id = cmd->command_id;

	switch (opcode) {
	case MLX5_SNAP_CMD_START_LOG:
		ret = snap_channel_start_dirty_track(schannel, cmd, cqe);
		break;
	case MLX5_SNAP_CMD_STOP_LOG:
		ret = snap_channel_stop_dirty_track(schannel, cmd, cqe);
		break;
	case MLX5_SNAP_CMD_GET_LOG_SZ:
		ret = snap_channel_get_dirty_size(schannel, cmd, cqe);
		break;
	case MLX5_SNAP_CMD_REPORT_LOG:
		ret = snap_channel_report_dirty_pages(schannel, cmd, cqe, send_wr);
		if (!ret)
			send_rsp = false;
		break;
	case MLX5_SNAP_CMD_FREEZE_DEV:
		ret = snap_channel_freeze_device(schannel, cmd, cqe);
		break;
	case MLX5_SNAP_CMD_UNFREEZE_DEV:
		ret = snap_channel_unfreeze_device(schannel, cmd, cqe);
		break;
	case MLX5_SNAP_CMD_QUIESCE_DEV:
		ret = snap_channel_quiesce_device(schannel, cmd, cqe);
		break;
	case MLX5_SNAP_CMD_UNQUIESCE_DEV:
		ret = snap_channel_unquiesce_device(schannel, cmd, cqe);
		break;
	case MLX5_SNAP_CMD_GET_STATE_SZ:
		ret = snap_channel_get_state_size(schannel, cmd, cqe);
		break;
	case MLX5_SNAP_CMD_READ_STATE:
		ret = snap_channel_read_state(schannel, cmd, cqe, send_wr);
		if (!ret)
			send_rsp = false;
		break;
	case MLX5_SNAP_CMD_WRITE_STATE:
		ret = snap_channel_write_state(schannel, cmd, cqe, send_wr);
		if (!ret)
			send_rsp = false;
		break;
	default:
		cqe->status = MLX5_SNAP_SC_INVALID_OPCODE;
		snap_channel_error("got unexpected opcode %u\n", opcode);
		break;
	}

	if (send_rsp) {

		/*
		 * some internal error happened during processing cmd and
		 * the status wasn't updated
		 */
		if (ret && cqe->status == MLX5_SNAP_SC_SUCCESS)
			cqe->status = MLX5_SNAP_SC_INTERNAL;

		snap_channel_info("schannel 0x%p posting cid %d response\n",
				  schannel, cqe->command_id);

		send_wr->wr_id = opcode;
		ret = ibv_post_send(schannel->qp, send_wr, &bad_wr);
		if (ret) {
			snap_channel_error("schannel 0x%p failed to post send\n",
					   schannel);
			return ret;
		}
	}

	return 0;
}

static int snap_channel_recv_handler(struct snap_rdma_channel *schannel,
		__u64 idx)
{
	struct ibv_recv_wr *recv_wr = &schannel->recv_wr[idx];
	struct ibv_recv_wr *bad_wr;
	struct mlx5_snap_common_command *cmd;
	int ret, retry = 5;

	cmd = (struct mlx5_snap_common_command *) (schannel->recv_buf +
			idx * SNAP_CHANNEL_DESC_SIZE);
retry:
	/* error means that rsp failed to be posted, retry as best effort */
	ret = snap_channel_process_cmd(schannel, cmd);
	if (ret && retry-- > 0) {
		usleep(200000);
		goto retry;
	}

	ret = ibv_post_recv(schannel->qp, recv_wr, &bad_wr);
	if (ret)
		snap_channel_error("schannel failed posting rdma recv,"
				   " ret = %d index %lld\n", ret, idx);

	return ret;
}

static int snap_channel_handle_completion(struct ibv_wc *wc,
		struct snap_rdma_channel *schannel)
{
	struct ibv_send_wr *send_wr, *bad_wr;
	__u8 opcode;
	int ret;

	if (wc->status == IBV_WC_SUCCESS) {
		switch (wc->opcode) {
		case IBV_WC_RECV:
			if (wc->byte_len != SNAP_CHANNEL_DESC_SIZE) {
				snap_channel_error("recv length %u is different"
						   " than expected size\n",
						   wc->byte_len);
				return -1;
			}
			ret = snap_channel_recv_handler(schannel, wc->wr_id);
			if (ret) {
				snap_channel_error("recv processing failed\n");
				return -1;
			}
			break;
		case IBV_WC_SEND:
			/* nothing to do in send completion */
			opcode = wc->wr_id;
			if (opcode == MLX5_SNAP_CMD_REPORT_LOG) {
				pthread_mutex_lock(&schannel->dirty_pages.copy_lock);
				if (schannel->dirty_pages.copy_bmap_num_elements) {
					schannel->dirty_pages.copy_bmap_num_elements = 0;
					free(schannel->dirty_pages.copy_bmap);
					schannel->dirty_pages.copy_bmap = NULL;
				}
				if (schannel->dirty_pages.copy_mr) {
					ibv_dereg_mr(schannel->dirty_pages.copy_mr);
					schannel->dirty_pages.copy_mr = NULL;
				}
				pthread_mutex_unlock(&schannel->dirty_pages.copy_lock);
			}
			break;
		case IBV_WC_RDMA_READ:
		case IBV_WC_RDMA_WRITE:
			snap_channel_info("received %d completion\n", wc->opcode);
			send_wr = (struct ibv_send_wr *)wc->wr_id;
			ret = ibv_post_send(schannel->qp, send_wr, &bad_wr);
			if (ret) {
				snap_channel_error("schannel 0x%p failed to post rw send\n",
						   schannel);
				return ret;
			}
			break;
		default:
			snap_channel_error("Received an unexpected completion "
					   "with opcode: %d\n", wc->opcode);
			return -1;
		}
	} else if (wc->status == IBV_WC_WR_FLUSH_ERR) {
		snap_channel_info("received IBV_WC_WR_FLUSH_ERR comp status\n");
	} else {
		snap_channel_error("received %d comp status\n", wc->status);
	}

	return 0;

}

static int snap_channel_cq_event_handler(struct snap_rdma_channel *schannel)
{
	struct ibv_cq *cq = schannel->cq;
	struct ibv_wc wc[SNAP_CHANNEL_POLL_BATCH];
	unsigned int n, i, completed = 0;
	int ret;

	while ((n = ibv_poll_cq(cq, SNAP_CHANNEL_POLL_BATCH, wc)) > 0) {
		for (i = 0; i < n; i++) {
			ret = snap_channel_handle_completion(&wc[i], schannel);
			if (ret)
				return -1;
		}

		completed += n;
		if (completed >= SNAP_CHANNEL_MAX_COMPLETIONS)
			goto out;
	}

	if (n) {
		snap_channel_error("still have %d elements to process for "
				   "channel 0x%p\n", n, schannel);
		return -1;
	}

out:
	return 0;
}

static void *cq_thread(void *arg)
{
	struct snap_rdma_channel *schannel = arg;
	struct ibv_cq *ev_cq;
	void *ev_ctx;
	int ret;
	bool handle;

	while (1) {
		pthread_testcancel();

		ret = ibv_get_cq_event(schannel->channel, &ev_cq, &ev_ctx);
		if (ret) {
			snap_channel_error("Failed to get cq event !\n");
			continue;
		}

		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
		handle = true;
		if (ev_cq != schannel->cq) {
			snap_channel_error("Unknown CQ!\n");
			handle = false;
		}

		ret = ibv_req_notify_cq(schannel->cq, 0);
		if (ret) {
			snap_channel_error("Failed to set notify!\n");
			handle = false;
		}

		ibv_ack_cq_events(schannel->cq, 1);
		if (handle) {
			ret = snap_channel_cq_event_handler(schannel);
			if (ret)
				snap_channel_error("Failed to handle cq "
						   "event\n");
		}
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	}

	return NULL;
}

static void snap_channel_clean_buffers(struct snap_rdma_channel *schannel)
{
	ibv_dereg_mr(schannel->recv_mr);
	ibv_dereg_mr(schannel->rsp_mr);
}

static int snap_channel_setup_buffers(struct snap_rdma_channel *schannel)
{
	int i, ret;

	schannel->rsp_mr = ibv_reg_mr(schannel->pd, schannel->rsp_buf,
				      SNAP_CHANNEL_QUEUE_SIZE * SNAP_CHANNEL_RSP_SIZE,
				      IBV_ACCESS_LOCAL_WRITE);
	if (!schannel->rsp_mr) {
		snap_channel_error("schannel 0x%p rsp_buf reg_mr failed\n",
				   schannel);
		return errno;
	}

	for (i = 0; i < SNAP_CHANNEL_QUEUE_SIZE; i++) {
		struct ibv_sge *rsp_sgl = &schannel->rsp_sgl[i];
		struct ibv_send_wr *rsp_wr = &schannel->rsp_wr[i];

		rsp_sgl->addr = (uint64_t)(schannel->rsp_buf + i * SNAP_CHANNEL_RSP_SIZE);
		rsp_sgl->length = SNAP_CHANNEL_RSP_SIZE;
		rsp_sgl->lkey = schannel->rsp_mr->lkey;

		rsp_wr->next = NULL;
		rsp_wr->opcode = IBV_WR_SEND;
		rsp_wr->send_flags = IBV_SEND_SIGNALED;
		rsp_wr->sg_list = rsp_sgl;
		rsp_wr->num_sge = 1;
		rsp_wr->imm_data = 0;
	}

	schannel->recv_mr = ibv_reg_mr(schannel->pd, schannel->recv_buf,
			SNAP_CHANNEL_QUEUE_SIZE * SNAP_CHANNEL_DESC_SIZE,
			IBV_ACCESS_LOCAL_WRITE);
	if (!schannel->recv_mr) {
		snap_channel_error("schannel 0x%p recv_buf reg_mr failed\n",
				   schannel);
		ret = errno;
		goto out_dereg_rsp_mr;
	}

	for (i = 0; i < SNAP_CHANNEL_QUEUE_SIZE; i++) {
		struct ibv_sge *recv_sgl = &schannel->recv_sgl[i];
		struct ibv_recv_wr *recv_wr = &schannel->recv_wr[i];
		struct ibv_recv_wr *bad_wr;

		recv_sgl->addr = (uint64_t)(schannel->recv_buf + i * SNAP_CHANNEL_DESC_SIZE);
		recv_sgl->length = SNAP_CHANNEL_DESC_SIZE;
		recv_sgl->lkey = schannel->recv_mr->lkey;

		recv_wr->wr_id = i;
		recv_wr->next = NULL;
		recv_wr->sg_list = recv_sgl;
		recv_wr->num_sge = 1;

		ret = ibv_post_recv(schannel->qp, recv_wr, &bad_wr);
		if (ret) {
			snap_channel_error("schannel failed posting rdma recv, "
					   "ret = %d index %d\n", ret, i);
			goto out_dereg_mr;
		}
	}

	return 0;

out_dereg_mr:
	ibv_dereg_mr(schannel->recv_mr);
out_dereg_rsp_mr:
	ibv_dereg_mr(schannel->rsp_mr);

	return ret;
}

static void snap_channel_destroy_qp(struct snap_rdma_channel *schannel)
{
	ibv_destroy_qp(schannel->qp);
}

static int snap_channel_create_qp(struct snap_rdma_channel *schannel,
		struct rdma_cm_id *cm_id)
{
	struct ibv_qp_init_attr init_attr = {};
	int ret;

	init_attr.cap.max_send_wr = SNAP_CHANNEL_QUEUE_SIZE;
	init_attr.cap.max_recv_wr = SNAP_CHANNEL_QUEUE_SIZE;
	init_attr.cap.max_recv_sge = 1;
	init_attr.cap.max_send_sge = 1;
	init_attr.qp_type = IBV_QPT_RC;
	init_attr.qp_context = schannel;
	init_attr.send_cq = schannel->cq;
	init_attr.recv_cq = schannel->cq;

	ret = rdma_create_qp(cm_id, schannel->pd, &init_attr);
	if (!ret)
		schannel->qp = cm_id->qp;
	else
		snap_channel_error("rdma_create_qp failed\n");

	return ret;
}

static int snap_channel_setup_qp(struct snap_rdma_channel *schannel,
		struct rdma_cm_id *cm_id)
{
	int ret;

	schannel->pd = ibv_alloc_pd(cm_id->verbs);
	if (!schannel->pd) {
		snap_channel_error("ibv_alloc_pd failed\n");
		return errno;
	}

	schannel->channel = ibv_create_comp_channel(cm_id->verbs);
	if (!schannel->channel) {
		snap_channel_error("ibv_create_comp_channel failed\n");
		ret = errno;
		goto err1;
	}

	/* +1 for drain */
	schannel->cq = ibv_create_cq(cm_id->verbs,
				     SNAP_CHANNEL_QUEUE_SIZE * 2 + 1,
				     schannel, schannel->channel, 0);
	if (!schannel->cq) {
		snap_channel_error("ibv_create_cq failed\n");
		ret = errno;
		goto err2;
	}

	ret = ibv_req_notify_cq(schannel->cq, 0);
	if (ret) {
		snap_channel_error("ibv_req_notify_cq failed\n");
		ret = errno;
		goto err3;
	}

	ret = snap_channel_create_qp(schannel, cm_id);
	if (ret)
		goto err3;

	ret = snap_channel_setup_buffers(schannel);
	if (ret)
		goto err4;

	ret = pthread_create(&schannel->cqthread, NULL, cq_thread, schannel);
	if (ret) {
		snap_channel_error("failed to pthread_create cq_thread\n");
		goto err5;
	}

	snap_channel_info("RDMA resources created for 0x%p\n", schannel);
	return 0;

err5:
	snap_channel_clean_buffers(schannel);
err4:
	snap_channel_destroy_qp(schannel);
err3:
	ibv_destroy_cq(schannel->cq);
err2:
	ibv_destroy_comp_channel(schannel->channel);
err1:
	ibv_dealloc_pd(schannel->pd);
	return ret;
}

static void snap_channel_clean_qp(struct snap_rdma_channel *schannel)
{
	pthread_cancel(schannel->cqthread);
	pthread_join(schannel->cqthread, NULL);
	snap_channel_clean_buffers(schannel);
	snap_channel_destroy_qp(schannel);
	ibv_destroy_cq(schannel->cq);
	ibv_destroy_comp_channel(schannel->channel);
	ibv_dealloc_pd(schannel->pd);
}

static inline void write_bit(uint8_t *int_block, int nbit)
{
	*int_block = *int_block | (1 << nbit);
}

static int snap_channel_cm_event_handler(struct rdma_cm_id *cm_id,
					 struct rdma_cm_event *event)
{
	int ret = 0;
	struct snap_rdma_channel *schannel = cm_id->context;

	snap_channel_info("cm_event type %s cma_id %p (%s)\n",
			  rdma_event_str(event->event), cm_id,
			  (cm_id == schannel->listener) ? "parent" : "child");

	switch (event->event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
	case RDMA_CM_EVENT_ROUTE_RESOLVED:
	case RDMA_CM_EVENT_CONNECT_RESPONSE:
	case RDMA_CM_EVENT_ESTABLISHED:
		break;
	case RDMA_CM_EVENT_CONNECT_REQUEST:
		ret = snap_channel_setup_qp(schannel, cm_id);
		if (!ret) {
			schannel->cm_id = cm_id;
			ret = rdma_accept(cm_id, NULL);
			if (ret) {
				snap_channel_error("schannel 0x%p failed to "
						   "accept connection ID 0x%p\n",
						   schannel, cm_id);
				snap_channel_clean_qp(schannel);
			}
		}
		break;
	case RDMA_CM_EVENT_DISCONNECTED:
		snap_channel_clean_qp(schannel);
		if (schannel->cm_id == cm_id)
			ret = -1; //destroy child id
		break;
	default:
		break;
	}

	return ret;
}

static void *cm_thread(void *arg)
{
	struct snap_rdma_channel *schannel = arg;
	struct rdma_cm_event *event;
	int ret;

	while (1) {
		pthread_testcancel();

		/* This function is blocking */
		ret = rdma_get_cm_event(schannel->cm_channel, &event);
		if (ret) {
			snap_channel_error("failed to get event 0x%p ret %d\n",
					   schannel->cm_channel, ret);
			continue;
		}
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
		ret = snap_channel_cm_event_handler(event->id, event);
		rdma_ack_cm_event(event);
		if (ret)
			snap_channel_error("failed to handle CM event 0x%p ret"
					   " %d\n", schannel, ret);
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
	}

	return NULL;
}

static int snap_channel_bind_id(struct rdma_cm_id *cm_id, struct addrinfo *res)
{
	int ret;

	ret = rdma_bind_addr(cm_id, res->ai_addr);
	if (ret) {
		snap_channel_error("rdma_bind_addr id 0x%p ret %d\n", cm_id,
				   ret);
		return ret;
	}

	ret = rdma_listen(cm_id, 3);
	if (ret) {
		snap_channel_error("rdma_listen 0x%p ret %d\n", cm_id, ret);
		return ret;
	}

	return 0;
}

static int snap_channel_get_num_elements(uint64_t start_address, int length, int element_size)
{
	uint64_t aligned_pa;
	int len_to_next_element;

	/* align to element size (ceiling) */
	aligned_pa = ALIGN(start_address, element_size);
	if (aligned_pa == start_address)
		return (length - 1) / element_size + 1;
	/* the length to next element */
	len_to_next_element = aligned_pa - start_address;
	if (length <= len_to_next_element)
		return 1;
	/* length > len_to_next_element, thus, we dirty at least 2 pages (+2) */
	return ((length - 1) - len_to_next_element) / element_size + 2;
}

/**
 * snap_rdma_channel_mark_dirty_page() - Report on a new contiguous memory region
 * that was dirtied by a snap controller.
 * @schannel: snap channel
 * @guest_pa: guest base physical address that was dirtied by the device
 * @length: length in bytes of the dirtied memory for the reported transaction
 *
 * Return: Returns 0 on success, Or negative error value otherwise.
 */
int snap_rdma_channel_mark_dirty_page(struct snap_channel *channel, uint64_t guest_pa,
				 int length)
{
	struct snap_rdma_channel *schannel = (struct snap_rdma_channel *)channel;
	int i, bit_num, ret = 0;
	int page_size, num_dirty_pages, bytes_per_element, num_elements;
	uint64_t start_page, cur_page, start_element, cur_element, end_element;

	pthread_mutex_lock(&schannel->dirty_pages.lock);
	if (!schannel->dirty_pages.bmap) {
		errno = EPERM;
		snap_channel_error("dirty pages logging have not been started\n");
		goto out_unlock;
	}
	page_size = schannel->dirty_pages.page_size;
	start_page = guest_pa / page_size;
	num_dirty_pages = snap_channel_get_num_elements(guest_pa, length, page_size);

	bytes_per_element = page_size * SNAP_CHANNEL_BITMAP_ELEM_BIT_SZ;
	start_element = guest_pa / bytes_per_element;
	num_elements = snap_channel_get_num_elements(guest_pa, length, bytes_per_element);

	end_element = start_element + num_elements;
	snap_channel_info("total dirty bits: %d, end_element: %ld\n", num_dirty_pages, end_element);

	/* realloc case */
	if (end_element > schannel->dirty_pages.bmap_num_elements) {
		while (end_element >= schannel->dirty_pages.bmap_num_elements)
			schannel->dirty_pages.bmap_num_elements <<= 1;
		schannel->dirty_pages.bmap = realloc(schannel->dirty_pages.bmap,
			schannel->dirty_pages.bmap_num_elements * SNAP_CHANNEL_BITMAP_ELEM_SZ);
		if (!schannel->dirty_pages.bmap) {
			ret = -ENOMEM;
			snap_channel_error("unable to realloc dirty pages bitmap\n");
			schannel->dirty_pages.bmap_num_elements = 0;
			goto out_unlock;
		}
		snap_channel_info("reallocated memory, now the size is %ld\n",
				  schannel->dirty_pages.bmap_num_elements);
	}
	/* write to the dirty_pages.bmap */
	for (i = 0; i < num_dirty_pages; i++) {
		cur_page = start_page + i;
		cur_element = cur_page / SNAP_CHANNEL_BITMAP_ELEM_BIT_SZ;
		bit_num = cur_page % SNAP_CHANNEL_BITMAP_ELEM_BIT_SZ;
		write_bit(&schannel->dirty_pages.bmap[cur_element], bit_num);
		snap_channel_info("\tcur_element %ld bit_num %d UP\n", cur_element, bit_num);
	}
	schannel->dirty_pages.highest_dirty_element = snap_channel_max(end_element,
					schannel->dirty_pages.highest_dirty_element);
out_unlock:
	pthread_mutex_unlock(&schannel->dirty_pages.lock);
	return ret;
}

static void snap_channel_reset_dirty_pages(struct snap_rdma_channel *schannel)
{
	struct snap_dirty_pages *dirty_pages = &schannel->dirty_pages;

	pthread_mutex_lock(&dirty_pages->copy_lock);
	if (dirty_pages->copy_bmap) {
		free(dirty_pages->copy_bmap);
		dirty_pages->copy_bmap = NULL;
	}
	if (schannel->dirty_pages.copy_mr) {
		ibv_dereg_mr(schannel->dirty_pages.copy_mr);
		dirty_pages->copy_mr = NULL;
	}
	dirty_pages->copy_bmap_num_elements = 0;
	pthread_mutex_unlock(&dirty_pages->copy_lock);

	pthread_mutex_destroy(&dirty_pages->copy_lock);

	pthread_mutex_lock(&dirty_pages->lock);
	if (dirty_pages->bmap) {
		free(dirty_pages->bmap);
		dirty_pages->bmap = NULL;
	}
	dirty_pages->highest_dirty_element = 0;
	dirty_pages->bmap_num_elements = 0;
	dirty_pages->page_size = 0;
	pthread_mutex_unlock(&dirty_pages->lock);

	pthread_mutex_destroy(&dirty_pages->lock);
}

static int snap_channel_init_dirty_pages(struct snap_rdma_channel *schannel)
{
	struct snap_dirty_pages *dirty_pages = &schannel->dirty_pages;
	int ret;

	dirty_pages->page_size = 0;
	dirty_pages->bmap_num_elements = 0;
	dirty_pages->highest_dirty_element = 0;
	dirty_pages->bmap = NULL;
	dirty_pages->copy_bmap_num_elements = 0;
	dirty_pages->copy_bmap = NULL;
	dirty_pages->copy_mr = NULL;

	ret = pthread_mutex_init(&dirty_pages->lock, NULL);
	if (ret) {
		snap_channel_error("dirty pages mutex init failed\n");
		goto out;
	}

	ret = pthread_mutex_init(&dirty_pages->copy_lock, NULL);
	if (ret) {
		snap_channel_error("dirty pages copy_mutex init failed\n");
		goto out_free_mutex;
	}

	return 0;

out_free_mutex:
	pthread_mutex_destroy(&dirty_pages->lock);
out:
	return ret;
}

static int snap_channel_init_internal_state(struct snap_rdma_channel *schannel)
{
	struct snap_internal_state *state = &schannel->state;
	int ret;

	state->state_size = 0;
	state->state = NULL;
	state->state_mr = NULL;
	ret = pthread_mutex_init(&state->lock, NULL);
	if (ret)
		snap_channel_error("state mutex init failed\n");

	return ret;
}

static void snap_channel_reset_internal_state(struct snap_rdma_channel *schannel)
{
	struct snap_internal_state *state = &schannel->state;

	pthread_mutex_lock(&state->lock);
	if (state->state) {
		free(state->state);
		state->state = NULL;
		state->state_size = 0;
	}
	if (state->state_mr) {
		ibv_dereg_mr(state->state_mr);
		state->state_mr = NULL;
	}
	pthread_mutex_unlock(&state->lock);

	pthread_mutex_destroy(&state->lock);
}

/**
 * snap_rdma_channel_open() - Opens a channel that will listen to host commands.
 * This channel is dedicated for live migration communication between device
 * and host.
 *
 * @ops: migration ops struct of functions that contains the basic migration
 * operations (provided by the controller).
 * @data: controller_data that will be associated with the
 * caller or application.
 *
 * Return: Returns a pointer to the communication channel on success,
 * or NULL otherwise.
 *
 * You may assume that the migration_ops, controller_data and the returned
 * snap channel won't be freed as long as the channel is open.
 */
struct snap_channel *snap_rdma_channel_open(struct snap_migration_ops *ops,
					    void *data)
{
	struct snap_rdma_channel *schannel;
	struct addrinfo *res;
	struct addrinfo hints;
	char *rdma_ip;
	char *rdma_port;
	int ret;

	if (!ops ||
	    !ops->quiesce ||
	    !ops->unquiesce ||
	    !ops->freeze ||
	    !ops->unfreeze ||
	    !ops->get_state_size ||
	    !ops->copy_state ||
	    !ops->start_dirty_pages_track ||
	    !ops->stop_dirty_pages_track) {
		errno = EINVAL;
		goto out;
	}

	schannel = calloc(1, sizeof(*schannel));
	if (!schannel) {
		errno = ENOMEM;
		goto out;
	}

	ret = snap_channel_init_internal_state(schannel);
	if (ret) {
		errno = ret;
		snap_channel_error("init internal state failed\n");
		goto out_free;
	}

	ret = snap_channel_init_dirty_pages(schannel);
	if (ret) {
		errno = ret;
		snap_channel_error("init dirty pages failed\n");
		goto out_reset_state;
	}

	/* for communication channel */
	schannel->cm_channel = rdma_create_event_channel();
	if (!schannel->cm_channel)
		goto out_reset_dirty_pages;

	ret = rdma_create_id(schannel->cm_channel, &schannel->listener, schannel,
			     RDMA_PS_TCP);
	if (ret)
		goto out_free_cm_channel;

	ret = pthread_create(&schannel->cmthread, NULL, cm_thread, schannel);
	if (ret)
		goto out_destroy_id;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_flags = AI_NUMERICSERV;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;

	rdma_ip = getenv(SNAP_CHANNEL_RDMA_IP);
	if (!rdma_ip)
		goto out_free_cmthread;

	pthread_mutex_lock(&port_lock);
	if (!rdma_port_idx)
		rdma_port = getenv(SNAP_CHANNEL_RDMA_PORT_1);
	else
		rdma_port = getenv(SNAP_CHANNEL_RDMA_PORT_2);

	if (!rdma_port)
		goto out_free_cmthread;
	rdma_port_idx++;
	snap_channel_info("BINDING IP %s PORT %s\n", rdma_ip, rdma_port);
	pthread_mutex_unlock(&port_lock);

	ret = getaddrinfo(rdma_ip, rdma_port, &hints, &res);
	if (ret)
		goto out_free_cmthread;

	ret = snap_channel_bind_id(schannel->listener, res);
	freeaddrinfo(res);

	if (ret)
		goto out_free_cmthread;

	schannel->base.ops = ops;
	schannel->base.data = data;

	return &schannel->base;

out_free_cmthread:
	pthread_cancel(schannel->cmthread);
	pthread_join(schannel->cmthread, NULL);
out_destroy_id:
	rdma_destroy_id(schannel->listener);
out_free_cm_channel:
	rdma_destroy_event_channel(schannel->cm_channel);
out_reset_dirty_pages:
	snap_channel_reset_dirty_pages(schannel);
out_reset_state:
	snap_channel_reset_internal_state(schannel);
out_free:
	free(schannel);
out:
	return NULL;
}

/**
 * snap_rdma_channel_close() - Closes the communication channel
 * opened by the snap_channel_open() function.
 *
 * @schannel: the communication channel to be closed.
 */
static void snap_rdma_channel_close(struct snap_channel *channel)
{
	struct snap_rdma_channel *schannel = (struct snap_rdma_channel *)channel;

	pthread_cancel(schannel->cmthread);
	pthread_join(schannel->cmthread, NULL);
	rdma_destroy_id(schannel->listener);
	rdma_destroy_event_channel(schannel->cm_channel);
	snap_channel_reset_dirty_pages(schannel);
	snap_channel_reset_internal_state(schannel);
	free(schannel);
}

static const struct snap_channel_ops snap_rdma_channel_ops = {
	.name = "rdma_channel",
	.open = snap_rdma_channel_open,
	.close = snap_rdma_channel_close,
	.mark_dirty_page = snap_rdma_channel_mark_dirty_page
};

SNAP_CHANNEL_DECLARE(rdma_channel, snap_rdma_channel_ops);
