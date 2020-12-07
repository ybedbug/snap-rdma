#include <unistd.h>

#include "snap_channel.h"

#define SNAP_CHANNEL_POLL_BATCH 16
#define SNAP_CHANNEL_MAX_COMPLETIONS 64

static int snap_channel_start_dirty_track(struct snap_channel *schannel,
		struct mlx5_snap_common_command *cmd,
		struct mlx5_snap_completion *cqe)
{
	struct mlx5_snap_start_dirty_log_command *dirty_cmd;
	int ret;

	dirty_cmd = (struct mlx5_snap_start_dirty_log_command *)cmd;
	snap_channel_info("schannel 0x%p start track with %u page size\n",
			  schannel, dirty_cmd->page_size);

	/* prepare the bitmap DB according to the given page size in the cmd */

	ret = schannel->ops->start_dirty_pages_track(schannel->data);
	if (ret) {
		snap_channel_info("schannel 0x%p failed to start track\n",
				  schannel);
		cqe->status = MLX5_SNAP_SC_INTERNAL;
	} else {
		snap_channel_info("schannel 0x%p started dirty track\n",
				  schannel);
		cqe->status = MLX5_SNAP_SC_SUCCESS;
	}

	return 0;
}

static int snap_channel_stop_dirty_track(struct snap_channel *schannel,
		struct mlx5_snap_common_command *cmd,
		struct mlx5_snap_completion *cqe)
{
	return schannel->ops->stop_dirty_pages_track(schannel->data);
}

static int snap_channel_get_dirty_size(struct snap_channel *schannel,
		struct mlx5_snap_common_command *cmd,
		struct mlx5_snap_completion *cqe)
{
	/*
	 * copy current dirty "valid" bitmap to a bounce buffer and return the
	 * size of the buffer.
	 */

	return 0;
}

static int snap_channel_freeze_device(struct snap_channel *schannel,
		struct mlx5_snap_common_command *cmd,
		struct mlx5_snap_completion *cqe)
{
	return schannel->ops->freeze(schannel->data);
}

static int snap_channel_unfreeze_device(struct snap_channel *schannel,
		struct mlx5_snap_common_command *cmd,
		struct mlx5_snap_completion *cqe)
{
	return schannel->ops->unfreeze(schannel->data);
}

static int snap_channel_quiesce_device(struct snap_channel *schannel,
		struct mlx5_snap_common_command *cmd,
		struct mlx5_snap_completion *cqe)
{
	return schannel->ops->quiesce(schannel->data);
}

static int snap_channel_unquiesce_device(struct snap_channel *schannel,
		struct mlx5_snap_common_command *cmd,
		struct mlx5_snap_completion *cqe)
{
	return schannel->ops->unquiesce(schannel->data);
}

static int snap_channel_get_state_size(struct snap_channel *schannel,
		struct mlx5_snap_common_command *cmd,
		struct mlx5_snap_completion *cqe)
{
	int size;

	size = schannel->ops->get_state_size(schannel->data);
	snap_channel_info("schannel 0x%p state size is %d\n", schannel, size);

	return 0;
}

static int snap_channel_read_state(struct snap_channel *schannel,
		struct mlx5_snap_common_command *cmd,
		struct mlx5_snap_completion *cqe)
{
	return 0;
}

static int snap_channel_write_state(struct snap_channel *schannel,
		struct mlx5_snap_common_command *cmd,
		struct mlx5_snap_completion *cqe)
{
	return 0;
}

static int snap_channel_process_cmd(struct snap_channel *schannel,
		struct mlx5_snap_common_command *cmd, __u64 idx)
{
	struct ibv_send_wr *send_wr, *bad_wr = NULL;
	struct mlx5_snap_completion *cqe;
	__u8 opcode = cmd->opcode;
	int ret = 0;

	snap_channel_info("schannel 0x%p got CMD opcode %u id %u\n", schannel,
			  opcode, cmd->command_id);

	send_wr = &schannel->rsp_wr[idx];
	cqe = (struct mlx5_snap_completion *) (schannel->rsp_buf +
			idx * SNAP_CHANNEL_RSP_SIZE);

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
		ret = snap_channel_read_state(schannel, cmd, cqe);
		break;
	case MLX5_SNAP_CMD_WRITE_STATE:
		ret = snap_channel_write_state(schannel, cmd, cqe);
		break;
	default:
		cqe->status = MLX5_SNAP_SC_INVALID_OPCODE;
		snap_channel_error("got unexpected opcode %u", opcode);
		break;
	}

	/* some internal error happened during processing cmd */
	if (ret)
		cqe->status = MLX5_SNAP_SC_INTERNAL;

	ret = ibv_post_send(schannel->qp, send_wr, &bad_wr);
	if (ret) {
		snap_channel_error("schannel 0x%p failed to post send",
				   schannel);
		return ret;
	}

	return 0;
}

static int snap_channel_recv_handler(struct snap_channel *schannel,
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
	ret = snap_channel_process_cmd(schannel, cmd, idx);
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
		struct snap_channel *schannel)
{
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
			break;
		default:
			snap_channel_error("Received an unexpected completion "
					   "with opcode: %d\n", wc->opcode);
			return -1;
		}
	} else if (wc->status == IBV_WC_WR_FLUSH_ERR) {
		snap_channel_info("received IBV_WC_WR_FLUSH_ERR comp status\n");
	} else {
		snap_channel_error("received %d comp status", wc->status);
	}

	return 0;

}

static int snap_channel_cq_event_handler(struct snap_channel *schannel)
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
	struct snap_channel *schannel = arg;
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

static void snap_channel_clean_buffers(struct snap_channel *schannel)
{
	ibv_dereg_mr(schannel->recv_mr);
	ibv_dereg_mr(schannel->rsp_mr);
}

static int snap_channel_setup_buffers(struct snap_channel *schannel)
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

		rsp_wr->wr_id = i;
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

static void snap_channel_destroy_qp(struct snap_channel *schannel)
{
	ibv_destroy_qp(schannel->qp);
}

static int snap_channel_create_qp(struct snap_channel *schannel,
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

static int snap_channel_setup_qp(struct snap_channel *schannel,
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

static void snap_channel_clean_qp(struct snap_channel *schannel)
{
	pthread_cancel(schannel->cqthread);
	pthread_join(schannel->cqthread, NULL);
	snap_channel_clean_buffers(schannel);
	snap_channel_destroy_qp(schannel);
	ibv_destroy_cq(schannel->cq);
	ibv_destroy_comp_channel(schannel->channel);
	ibv_dealloc_pd(schannel->pd);
}

static int snap_channel_cm_event_handler(struct rdma_cm_id *cm_id,
					 struct rdma_cm_event *event)
{
	int ret = 0;
	struct snap_channel *schannel = cm_id->context;

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
	struct snap_channel *schannel = arg;
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

/**
 * snap_channel_mark_dirty_page() - Report on a new contiguous memory region
 * that was dirtied by a snap controller.
 * @schannel: snap channel
 * @guest_pa: guest base physical address that was dirtied by the device
 * @length: length in bytes of the dirtied memory for the reported transaction
 *
 * Return: Returns 0 on success, Or negative error value otherwise.
 */
int snap_channel_mark_dirty_page(struct snap_channel *schannel, uint64_t guest_pa,
				 int length)
{
	return 0;
}

/**
 * snap_channel_open() - Opens a channel that will listen to host commands.
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
struct snap_channel *snap_channel_open(struct snap_migration_ops *ops,
				       void *data)
{
	struct snap_channel *schannel;
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

	schannel->cm_channel = rdma_create_event_channel();
	if (!schannel->cm_channel)
		goto out_free;

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

	rdma_port = getenv(SNAP_CHANNEL_RDMA_PORT);
	if (!rdma_port)
		goto out_free_cmthread;

	ret = getaddrinfo(rdma_ip, rdma_port, &hints, &res);
	if (ret)
		goto out_free_cmthread;

	ret = snap_channel_bind_id(schannel->listener, res);
	freeaddrinfo(res);

	if (ret)
		goto out_free_cmthread;

	schannel->ops = ops;
	schannel->data = data;

	return schannel;

out_free_cmthread:
	pthread_cancel(schannel->cmthread);
	pthread_join(schannel->cmthread, NULL);
out_destroy_id:
	rdma_destroy_id(schannel->listener);
out_free_cm_channel:
	rdma_destroy_event_channel(schannel->cm_channel);
out_free:
	free(schannel);
out:
	return NULL;
}

/**
 * snap_channel_close() - Closes the communication channel
 * opened by the snap_channel_open() function.
 *
 * @schannel: the communication channel to be closed.
 */
void snap_channel_close(struct snap_channel *schannel)
{
	pthread_cancel(schannel->cmthread);
	pthread_join(schannel->cmthread, NULL);
	rdma_destroy_id(schannel->listener);
	rdma_destroy_event_channel(schannel->cm_channel);
	free(schannel);
}
