#include "snap_live_migration_client.h"

static int snap_channel_handle_completion(struct ibv_wc *wc,
		struct snap_channel_test *stest)
{
	if (wc->status == IBV_WC_SUCCESS) {
		switch (wc->opcode) {
		case IBV_WC_RECV:
			if (wc->byte_len != SNAP_CHANNEL_RSP_SIZE) {
				snap_channel_error("recv length %u is different"
						   " than expected size\n",
						   wc->byte_len);
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

static int snap_channel_cq_event_handler(struct snap_channel_test *stest)
{
	struct snap_rdma_channel *schannel = stest->schannel;
	struct ibv_cq *cq = schannel->cq;
	struct ibv_wc wc[SNAP_CHANNEL_POLL_BATCH];
	unsigned int n, i, completed = 0;
	int ret;

	while ((n = ibv_poll_cq(cq, SNAP_CHANNEL_POLL_BATCH, wc)) > 0) {
		for (i = 0; i < n; i++) {
			ret = snap_channel_handle_completion(&wc[i], stest);
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
	struct snap_channel_test *stest = arg;
	struct snap_rdma_channel *schannel = stest->schannel;
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
			ret = snap_channel_cq_event_handler(stest);
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

	schannel->rsp_mr = ibv_reg_mr(schannel->pd, schannel->recv_buf,
				      SNAP_CHANNEL_QUEUE_SIZE * SNAP_CHANNEL_DESC_SIZE,
				      IBV_ACCESS_LOCAL_WRITE);
	if (!schannel->rsp_mr) {
		snap_channel_error("schannel 0x%p recv_buf reg_mr failed\n",
				   schannel);
		return errno;
	}

	for (i = 0; i < SNAP_CHANNEL_QUEUE_SIZE; i++) {
		struct ibv_sge *rsp_sgl = &schannel->rsp_sgl[i];
		struct ibv_send_wr *rsp_wr = &schannel->rsp_wr[i];

		rsp_sgl->addr = (uint64_t)(schannel->recv_buf + i * SNAP_CHANNEL_DESC_SIZE);
		rsp_sgl->length = SNAP_CHANNEL_DESC_SIZE;
		rsp_sgl->lkey = schannel->rsp_mr->lkey;

		rsp_wr->next = NULL;
		rsp_wr->opcode = IBV_WR_SEND;
		rsp_wr->send_flags = IBV_SEND_SIGNALED;
		rsp_wr->sg_list = rsp_sgl;
		rsp_wr->num_sge = 1;
		rsp_wr->imm_data = 0;
	}

	schannel->recv_mr = ibv_reg_mr(schannel->pd, schannel->rsp_buf,
			SNAP_CHANNEL_QUEUE_SIZE * SNAP_CHANNEL_RSP_SIZE,
			IBV_ACCESS_LOCAL_WRITE);
	if (!schannel->recv_mr) {
		snap_channel_error("schannel 0x%p rsp_buf reg_mr failed\n",
				   schannel);
		ret = errno;
		goto out_dereg_rsp_mr;
	}

	for (i = 0; i < SNAP_CHANNEL_QUEUE_SIZE; i++) {
		struct ibv_sge *recv_sgl = &schannel->recv_sgl[i];
		struct ibv_recv_wr *recv_wr = &schannel->recv_wr[i];
		struct ibv_recv_wr *bad_wr;

		recv_sgl->addr = (uint64_t)(schannel->rsp_buf + i * SNAP_CHANNEL_RSP_SIZE);
		recv_sgl->length = SNAP_CHANNEL_RSP_SIZE;
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

static int snap_channel_setup_qp(struct snap_channel_test *stest,
		struct rdma_cm_id *cm_id)
{
	int ret;
	struct snap_rdma_channel *schannel = stest->schannel;

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
	if (ret) {
		snap_channel_error("failed to create qp");
		goto err3;
	}

	ret = snap_channel_setup_buffers(schannel);
	if (ret) {
		snap_channel_error("failed to create buffers");
		goto err4;
	}

	ret = pthread_create(&schannel->cqthread, NULL, cq_thread, stest);
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
	*int_block = *int_block & (1 << nbit);
}

static int snap_channel_cm_event_handler(struct rdma_cm_id *cm_id,
					 struct rdma_cm_event *event,
					 sem_t *sem)
{
	int ret = 0;
	struct snap_rdma_channel *schannel = cm_id->context;

	snap_channel_info("cm_event type %s cma_id %p (%s)\n",
			  rdma_event_str(event->event), cm_id,
			  (cm_id == schannel->cm_id) ? "parent" : "child");

	switch (event->event) {
	case RDMA_CM_EVENT_ADDR_RESOLVED:
		ret = rdma_resolve_route(cm_id, 2000);
		if (ret) {
			snap_channel_error("rdma_resolve_route");
			sem_post(sem);
		}
		break;
	case RDMA_CM_EVENT_ROUTE_RESOLVED:
	case RDMA_CM_EVENT_CONNECT_RESPONSE:
	case RDMA_CM_EVENT_ESTABLISHED:
	case RDMA_CM_EVENT_CONNECT_REQUEST:
		sem_post(sem);
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
	struct snap_channel_test *stest = arg;
	struct snap_rdma_channel *schannel = stest->schannel;
	struct rdma_cm_event *event;
	int ret;

	while (1) {
		/* This function is blocking */
		ret = rdma_get_cm_event(schannel->cm_channel, &event);
		if (ret) {
			snap_channel_error("failed to get event 0x%p ret %d\n",
					   schannel->cm_channel, ret);
			exit(ret);
		}
		ret = snap_channel_cm_event_handler(event->id, event, &stest->sem);
		rdma_ack_cm_event(event);
		if (ret) {
			snap_channel_error("failed to handle CM event 0x%p ret"
					   " %d\n", schannel, ret);
			exit(ret);
		}
	}

	return NULL;
}


int snap_channel_connect_client(struct snap_channel_test *stest)
{
	struct rdma_conn_param conn_param;
	struct ibv_device_attr attr;
	int ret = 0;

	ret = ibv_query_device(stest->schannel->cm_id->verbs, &attr);
	if (ret)
		return ret;
	memset(&conn_param, 0, sizeof(conn_param));
	if (attr.max_qp_rd_atom > SNAP_CHANNEL_QUEUE_SIZE)
		conn_param.responder_resources = SNAP_CHANNEL_QUEUE_SIZE;
	else
		conn_param.responder_resources = attr.max_qp_rd_atom;
	conn_param.retry_count = 7;
	conn_param.rnr_retry_count = 7;
	snap_channel_info("Max: responder_resources %d\n", conn_param.responder_resources);

	ret = rdma_connect(stest->schannel->cm_id, &conn_param);
	if (ret) { /* connection param, cm_id */
		snap_channel_error("rdma connect failed errno %d\n", errno);
		return ret;
	}

	sem_wait(&stest->sem);
	return ret;
}

void close_client(struct snap_channel_test *stest)
{
	struct snap_rdma_channel *schannel = stest->schannel;

	pthread_cancel(schannel->cmthread);
	pthread_join(schannel->cmthread, NULL);
	rdma_destroy_id(schannel->cm_id);
	rdma_destroy_event_channel(schannel->cm_channel);
	free(stest->schannel);
	sem_destroy(&stest->sem);
}

int open_client(struct snap_channel_test *stest)
{
	int ret = 0;
	struct addrinfo *res;
	struct addrinfo hints;
	char *rdma_ip;
	char *rdma_port;
	struct snap_rdma_channel *schannel;

	ret = sem_init(&stest->sem, 0, 0);
	if (ret) {
		snap_channel_error("failed to init sem\n");
		errno = ENOMEM;
		ret = ENOMEM;
		goto out;
	}

	stest->schannel = calloc(1, sizeof(*schannel));
	if (!stest->schannel) {
		errno = ENOMEM;
		ret = ENOMEM;
		goto out_sem;
	}

	schannel = stest->schannel;

	schannel->cm_channel = rdma_create_event_channel();
	if (!schannel->cm_channel)
		goto out_free_schannel;

	ret = rdma_create_id(schannel->cm_channel, &schannel->cm_id, schannel,
			     RDMA_PS_TCP);
	if (ret) {
		snap_channel_error("failed to create id");
		goto out_free_cm_channel;
	}

	ret = pthread_create(&schannel->cmthread, NULL, cm_thread, stest);
	if (ret) {
		snap_channel_error("failed to creat cm_thread");
		goto out_destroy_id;
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC; // don't care IPv4 or IPv6
	hints.ai_flags = AI_NUMERICSERV; // service point to a string containing a numeric port number
	hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
	hints.ai_protocol = 0;

	rdma_ip = getenv(SNAP_CHANNEL_RDMA_IP);
	if (!rdma_ip) {
		snap_channel_error("please set environment variables");
		goto out_free_cmthread;
	}

	rdma_port = getenv(SNAP_CHANNEL_RDMA_PORT_1);
	if (!rdma_port) {
		snap_channel_error("please set environment variables");
		goto out_free_cmthread;
	}

	ret = getaddrinfo(rdma_ip, rdma_port, &hints, &res);
	if (ret) {
		snap_channel_error("could not set addr");
		goto out_free_cmthread;
	}

	ret = rdma_resolve_addr(schannel->cm_id, NULL, res->ai_addr, 2000); // 2 sec timeout
	if (ret) {
		snap_channel_error("failed to resolve address");
		goto out_free_cmthread;
	}
	freeaddrinfo(res);

	sem_wait(&stest->sem);
	snap_channel_info("seccessfully binded\n");

	ret = snap_channel_setup_qp(stest, schannel->cm_id);
	if (ret) {
		snap_channel_error("failed to setup qp: %d\n", ret);
		goto out_free_cmthread;
	}

	ret = snap_channel_connect_client(stest);
	if (ret) {
		snap_channel_error("failed to connect cleint: %d\n", ret);
		goto out_free_qp;
	}
	snap_channel_info("seccessfully connected!\n");
	return 0;

out_free_qp:
	snap_channel_clean_buffers(schannel);
	snap_channel_destroy_qp(schannel);
	ibv_destroy_cq(schannel->cq);
	ibv_destroy_comp_channel(schannel->channel);
	ibv_dealloc_pd(schannel->pd);
out_free_cmthread:
	pthread_cancel(schannel->cmthread);
	pthread_join(schannel->cmthread, NULL);
out_destroy_id:
	rdma_destroy_id(schannel->cm_id);
out_free_cm_channel:
	rdma_destroy_event_channel(schannel->cm_channel);
out_free_schannel:
	free(stest->schannel);
out_sem:
	sem_destroy(&stest->sem);
out:
	return ret;
}
