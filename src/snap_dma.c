#include <stdlib.h>

#include "snap.h"
#include "snap_dma.h"

static void snap_destroy_qp_helper(struct snap_dma_ibv_qp *qp)
{
	ibv_destroy_qp(qp->qp);
	ibv_destroy_cq(qp->rx_cq);
	ibv_destroy_cq(qp->tx_cq);
}

static void snap_free_rx_wqes(struct snap_dma_ibv_qp *qp)
{
	ibv_dereg_mr(qp->rx_mr);
	free(qp->rx_buf);
}

static int snap_alloc_rx_wqes(struct snap_dma_ibv_qp *qp, int rx_qsize,
		int rx_elem_size)
{
	struct ibv_pd *pd = qp->qp->pd;
	int rc;

	rc = posix_memalign((void **)&qp->rx_buf, SNAP_DMA_RX_BUF_ALIGN,
			rx_qsize * rx_elem_size);
	if (rc)
		return rc;

	qp->rx_mr = ibv_reg_mr(pd, qp->rx_buf, rx_qsize * rx_elem_size,
			IBV_ACCESS_LOCAL_WRITE);
	if (!qp->rx_mr) {
		free(qp->rx_buf);
		return -ENOMEM;
	}

	return 0;
}

static int snap_create_qp_helper(struct ibv_pd *pd, void *cq_context,
		struct ibv_comp_channel *comp_channel, int comp_vector,
		struct ibv_qp_init_attr *attr, struct snap_dma_ibv_qp *qp)
{
	struct ibv_context *ibv_ctx = pd->context;

	qp->tx_cq = ibv_create_cq(ibv_ctx, attr->cap.max_send_wr, cq_context,
			comp_channel, comp_vector);
	if (!qp->tx_cq)
		return -EINVAL;

	qp->rx_cq = ibv_create_cq(ibv_ctx, attr->cap.max_recv_wr, cq_context,
			comp_channel, comp_vector);
	if (!qp->rx_cq)
		goto free_tx_cq;

	attr->qp_type = IBV_QPT_RC;
	attr->send_cq = qp->tx_cq;
	attr->recv_cq = qp->rx_cq;

	qp->qp = ibv_create_qp(pd, attr);
	if (!qp->qp)
		goto free_rx_cq;

	snap_debug("created qp 0x%x tx %d rx %d tx_inline %d on pd %p\n",
			qp->qp->qp_num, attr->cap.max_send_wr,
			attr->cap.max_recv_wr, attr->cap.max_inline_data, pd);
	return 0;

free_rx_cq:
	ibv_destroy_cq(qp->rx_cq);
free_tx_cq:
	ibv_destroy_cq(qp->tx_cq);
	return -EINVAL;
}

static void snap_destroy_sw_qp(struct snap_dma_q *q)
{
	snap_free_rx_wqes(&q->sw_qp);
	snap_destroy_qp_helper(&q->sw_qp);
}

static int snap_create_sw_qp(struct snap_dma_q *q, struct ibv_pd *pd,
		struct snap_dma_q_create_attr *attr)
{
	struct ibv_qp_init_attr init_attr = {};
	struct ibv_recv_wr rx_wr, *bad_wr;
	struct ibv_sge rx_sge;
	int i, rc;

	q->tx_available = attr->tx_qsize;
	q->tx_qsize = attr->tx_qsize;
	q->rx_elem_size = attr->rx_elem_size;
	q->tx_elem_size = attr->tx_elem_size;

	init_attr.cap.max_send_wr = attr->tx_qsize;
	/* Need more space in rx queue in order to avoid memcpy() on rx data */
	init_attr.cap.max_recv_wr = 2 * attr->rx_qsize;
	/* we must be able to send CQEs inline */
	init_attr.cap.max_inline_data = attr->tx_elem_size;

	init_attr.cap.max_send_sge = 1;
	init_attr.cap.max_recv_sge = 1;

	rc = snap_create_qp_helper(pd, attr->comp_context, attr->comp_channel,
			attr->comp_vector, &init_attr, &q->sw_qp);
	if (rc)
		return rc;

	rc = snap_alloc_rx_wqes(&q->sw_qp, 2 * attr->rx_qsize, attr->rx_elem_size);
	if (rc)
		goto free_qp;

	for (i = 0; i < 2 * attr->rx_qsize; i++) {
		rx_sge.addr = (uint64_t)(q->sw_qp.rx_buf +
				i * attr->rx_elem_size);
		rx_sge.length = attr->rx_elem_size;
		rx_sge.lkey = q->sw_qp.rx_mr->lkey;

		rx_wr.wr_id = rx_sge.addr;
		rx_wr.next = NULL;
		rx_wr.sg_list = &rx_sge;
		rx_wr.num_sge = 1;

		rc = ibv_post_recv(q->sw_qp.qp, &rx_wr, &bad_wr);
		if (rc)
			goto free_rx_resources;
	}

	return 0;

free_rx_resources:
	snap_free_rx_wqes(&q->sw_qp);
free_qp:
	snap_destroy_qp_helper(&q->sw_qp);
	return rc;
}

static void snap_destroy_fw_qp(struct snap_dma_q *q)
{
	snap_destroy_qp_helper(&q->fw_qp);
}

static int snap_create_fw_qp(struct snap_dma_q *q, struct ibv_pd *pd)
{
	struct ibv_qp_init_attr init_attr = {};
	int rc;

	/* cannot create empty cq or a qp without one */
	init_attr.cap.max_send_wr = 1;
	init_attr.cap.max_recv_wr = 1;
	/* give one sge so that we can post which is useful
	 * for testing */
	init_attr.cap.max_send_sge = 1;

	rc = snap_create_qp_helper(pd, NULL, NULL, 0, &init_attr, &q->fw_qp);
	return rc;
}

static int snap_connect_loop_qp(struct snap_dma_q *q)
{
	struct ibv_qp_attr attr;
	union ibv_gid sw_gid, fw_gid;
	int rc;

	rc = ibv_query_gid(q->sw_qp.qp->context, SNAP_DMA_QP_PORT_NUM,
			SNAP_DMA_QP_GID_INDEX, &sw_gid);
	if (rc) {
		snap_error("Failed to get SW QP gid[%d]\n",
				SNAP_DMA_QP_GID_INDEX);
		return rc;
	}

	rc = ibv_query_gid(q->fw_qp.qp->context, SNAP_DMA_QP_PORT_NUM,
			SNAP_DMA_QP_GID_INDEX, &fw_gid);
	if (rc) {
		snap_error("Failed to get FW QP gid[%d]\n",
				SNAP_DMA_QP_GID_INDEX);
		return rc;
	}

	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IBV_QPS_INIT;
	attr.pkey_index = SNAP_DMA_QP_PKEY_INDEX;
	attr.port_num = SNAP_DMA_QP_PORT_NUM;
	attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;

	rc = ibv_modify_qp(q->sw_qp.qp, &attr,
			IBV_QP_STATE |
			IBV_QP_PKEY_INDEX |
			IBV_QP_PORT |
			IBV_QP_ACCESS_FLAGS);
	if (rc) {
		snap_error("failed to modify SW QP to INIT errno=%d\n", rc);
		return rc;
	}

	rc = ibv_modify_qp(q->fw_qp.qp, &attr,
			IBV_QP_STATE |
			IBV_QP_PKEY_INDEX |
			IBV_QP_PORT |
			IBV_QP_ACCESS_FLAGS);
	if (rc) {
		snap_error("failed to modify FW QP to INIT errno=%d\n", rc);
		return rc;
	}

	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IBV_QPS_RTR;
	attr.path_mtu = SNAP_DMA_QP_PATH_MTU;
	attr.rq_psn = SNAP_DMA_QP_RQ_PSN;
	attr.max_dest_rd_atomic = SNAP_DMA_QP_MAX_DEST_RD_ATOMIC;
	attr.min_rnr_timer = SNAP_DMA_QP_RNR_TIMER;
	attr.ah_attr.is_global = 1;
	attr.ah_attr.port_num = SNAP_DMA_QP_PORT_NUM;
	attr.ah_attr.grh.hop_limit = SNAP_DMA_QP_HOP_LIMIT;

	attr.dest_qp_num = q->fw_qp.qp->qp_num;
	memcpy(attr.ah_attr.grh.dgid.raw, fw_gid.raw, sizeof(fw_gid.raw));

	rc = ibv_modify_qp(q->sw_qp.qp, &attr,
			IBV_QP_STATE              |
			IBV_QP_AV                 |
			IBV_QP_PATH_MTU           |
			IBV_QP_DEST_QPN           |
			IBV_QP_RQ_PSN             |
			IBV_QP_MAX_DEST_RD_ATOMIC |
			IBV_QP_MIN_RNR_TIMER);
	if (rc) {
		snap_error("failed to modify SW QP to RTR errno=%d\n", rc);
		return rc;
	}

	attr.dest_qp_num = q->sw_qp.qp->qp_num;
	memcpy(attr.ah_attr.grh.dgid.raw, sw_gid.raw, sizeof(sw_gid.raw));

	rc = ibv_modify_qp(q->fw_qp.qp, &attr,
			IBV_QP_STATE              |
			IBV_QP_AV                 |
			IBV_QP_PATH_MTU           |
			IBV_QP_DEST_QPN           |
			IBV_QP_RQ_PSN             |
			IBV_QP_MAX_DEST_RD_ATOMIC |
			IBV_QP_MIN_RNR_TIMER);
	if (rc) {
		snap_error("failed to modify FW QP to RTR errno=%d\n", rc);
		return rc;
	}

	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IBV_QPS_RTS;
	attr.timeout = SNAP_DMA_QP_TIMEOUT;
	attr.retry_cnt = SNAP_DMA_QP_RETRY_COUNT;
	attr.sq_psn = SNAP_DMA_QP_SQ_PSN;
	attr.rnr_retry = SNAP_DMA_QP_RNR_RETRY;
	attr.max_rd_atomic = SNAP_DMA_QP_MAX_RD_ATOMIC;

	rc = ibv_modify_qp(q->sw_qp.qp, &attr,
			IBV_QP_STATE              |
			IBV_QP_TIMEOUT            |
			IBV_QP_RETRY_CNT          |
			IBV_QP_RNR_RETRY          |
			IBV_QP_SQ_PSN             |
			IBV_QP_MAX_QP_RD_ATOMIC);
	if (rc) {
		snap_error("failed to modify SW QP to RTS errno=%d\n", rc);
		return rc;
	}

	rc = ibv_modify_qp(q->fw_qp.qp, &attr,
			IBV_QP_STATE              |
			IBV_QP_TIMEOUT            |
			IBV_QP_RETRY_CNT          |
			IBV_QP_RNR_RETRY          |
			IBV_QP_SQ_PSN             |
			IBV_QP_MAX_QP_RD_ATOMIC);
	if (rc) {
		snap_error("failed to modify FW QP to RTS errno=%d\n", rc);
		return rc;
	}

	return 0;
}

/**
 * snap_dma_q_create - create DMA queue
 * @pd:    protection domain to create qps
 * @attr:  dma queue creation attributes
 *
 * Create and connect both software and fw qps
 *
 * The function creates a pair of QPs and connects them.
 * snap_dma_q_get_fw_qpnum() should be used to obtain qp number that
 * can be given to firmware emulation objects.
 *
 * Note that on Blufield1 extra steps are required:
 *  - an on behalf QP with the same number as
 *    returned by the snap_dma_q_get_fw_qpnum() must be created
 *  - a fw qp state must be copied to the on behalf qp
 *  - steering rules must be set
 *
 * All these steps must be done by the application.
 *
 * Return: dma queue or NULL on error.
 */
struct snap_dma_q *snap_dma_q_create(struct ibv_pd *pd,
		struct snap_dma_q_create_attr *attr)
{
	struct snap_dma_q *q;
	int rc;

	if (!pd)
		return NULL;

	if (!attr->rx_cb)
		return NULL;

	q = calloc(1, sizeof(*q));
	if (!q)
		return NULL;

	rc = snap_create_sw_qp(q, pd, attr);
	if (rc)
		goto free_q;

	rc = snap_create_fw_qp(q, pd);
	if (rc)
		goto free_sw_qp;

	rc = snap_connect_loop_qp(q);
	if (rc)
		goto free_fw_qp;

	q->uctx = attr->uctx;
	q->rx_cb = attr->rx_cb;
	return q;

free_fw_qp:
	snap_destroy_fw_qp(q);
free_sw_qp:
	snap_destroy_sw_qp(q);
free_q:
	free(q);
	return NULL;
}

/**
 * snap_dma_q_destroy - destroy DMA queue
 *
 * @q: dma queue
 */
void snap_dma_q_destroy(struct snap_dma_q *q)
{
	snap_destroy_sw_qp(q);
	snap_destroy_fw_qp(q);
	free(q);
}

static inline int snap_dma_q_progress_rx(struct snap_dma_q *q)
{
	struct ibv_wc wcs[SNAP_DMA_MAX_COMPLETIONS];
	int i, n;
	int rc;
	struct ibv_recv_wr rx_wr[SNAP_DMA_MAX_COMPLETIONS + 1], *bad_wr;
	struct ibv_sge rx_sge[SNAP_DMA_MAX_COMPLETIONS + 1];

	n = ibv_poll_cq(q->sw_qp.rx_cq, SNAP_DMA_MAX_COMPLETIONS, wcs);
	if (n == 0)
		return 0;

	if (snap_unlikely(n < 0)) {
	   snap_error("dma queue %p: failed to poll rx cq: errno=%d\n", q, n);
	   return 0;
	}

	for (i = 0; i < n; i++) {
		if (snap_unlikely(wcs[i].status != IBV_WC_SUCCESS)) {
			if (wcs[i].status == IBV_WC_WR_FLUSH_ERR) {
				snap_debug("dma queue %p: got FLUSH_ERROR\n", q);
			} else {
				snap_error("dma queue %p: got unexpected "
					   "completion status 0x%x, opcode 0x%x\n",
					   q, wcs[i].status, wcs[i].opcode);
			}
			return n;
		}

		q->rx_cb(q, (void *)wcs[i].wr_id, wcs[i].byte_len,
				wcs[i].imm_data);
		rx_sge[i].addr = wcs[i].wr_id;
		rx_sge[i].length = q->rx_elem_size;
		rx_sge[i].lkey = q->sw_qp.rx_mr->lkey;

		rx_wr[i].wr_id = rx_sge[i].addr;
		rx_wr[i].next = &rx_wr[i + 1];
		rx_wr[i].sg_list = &rx_sge[i];
		rx_wr[i].num_sge = 1;
	}

	rx_wr[i - 1].next = NULL;
	rc = ibv_post_recv(q->sw_qp.qp, rx_wr, &bad_wr);
	if (snap_unlikely(rc))
		snap_error("dma queue %p: failed to post recv: errno=%d\n",
				q, rc);
	return n;
}

static inline int snap_dma_q_progress_tx(struct snap_dma_q *q)
{
	struct ibv_wc wcs[SNAP_DMA_MAX_COMPLETIONS];
	struct snap_dma_completion *comp;
	int i, n;

	n = ibv_poll_cq(q->sw_qp.tx_cq, SNAP_DMA_MAX_COMPLETIONS, wcs);
	if (n == 0)
		return 0;

	if (snap_unlikely(n < 0)) {
		snap_error("dma queue %p: failed to poll tx cq: errno=%d\n",
				q, n);
		return 0;
	}

	q->tx_available += n;

	for (i = 0; i < n; i++) {
		if (snap_unlikely(wcs[i].status != IBV_WC_SUCCESS))
			snap_error("dma queue %p: got unexpected completion "
					"status 0x%x, opcode 0x%x\n",
					q, wcs[i].status, wcs[i].opcode);
		/* wr_id, status, qp_num and vendor_err are still valid in
		 * case of error */
		comp = (struct snap_dma_completion *)wcs[i].wr_id;
		if (!comp)
			continue;

		if (--comp->count == 0)
			comp->func(comp, wcs[i].status);
	}

	return n;
}

/**
 * snap_dma_q_progress - progress dma queue
 * @q: dma queue
 *
 * The function progresses both send and receive operations on the given dma
 * queue.
 *
 * Send &typedef snap_dma_comp_cb_t and receive &typedef snap_dma_rx_cb_t
 * completion callbacks may be called from within this function.
 * It is guaranteed that such callbacks are called in the execution context
 * of the progress.
 *
 * If dma queue was created with a completion channel then one can
 * use it's file descriptor to check for events instead of the
 * polling. When event is detected snap_dma_q_progress() should
 * be called to process it.
 *
 * Return: number of events (send and receive) that were processed
 */
int snap_dma_q_progress(struct snap_dma_q *q)
{
	int n;

	n = snap_dma_q_progress_tx(q);
	n += snap_dma_q_progress_rx(q);
	return n;
}

/**
 * snap_dma_q_arm - request notification
 * @q: dma queue
 *
 * The function 'arms' dma queue to report send and receive events over its
 * completion channel.
 *
 * Return:  0 or -errno on error
 */
int snap_dma_q_arm(struct snap_dma_q *q)
{
	int rc;

	rc = ibv_req_notify_cq(q->sw_qp.tx_cq, 0);
	if (rc)
		return rc;

	return ibv_req_notify_cq(q->sw_qp.rx_cq, 0);
}

static inline int qp_can_tx(struct snap_dma_q *q)
{
	/* later we can also add cq space check */
	return q->tx_available > 0;
}

static inline int do_dma_xfer(struct snap_dma_q *q, void *buf, size_t len,
		uint32_t lkey, uint64_t raddr, uint32_t rkey, int op,
		struct snap_dma_completion *comp)
{
	struct ibv_qp *qp = q->sw_qp.qp;
	struct ibv_send_wr rdma_wr, *bad_wr;
	struct ibv_sge sge;
	int rc;

	if (snap_unlikely(!qp_can_tx(q)))
		return -EAGAIN;

	sge.addr = (uint64_t)buf;
	sge.length = len;
	sge.lkey = lkey;

	rdma_wr.opcode = op;
	rdma_wr.send_flags = IBV_SEND_SIGNALED;
	rdma_wr.num_sge = 1;
	rdma_wr.sg_list = &sge;
	rdma_wr.wr_id = (uint64_t)comp;
	rdma_wr.wr.rdma.rkey = rkey;
	rdma_wr.wr.rdma.remote_addr = raddr;
	rdma_wr.next = NULL;

	rc = ibv_post_send(qp, &rdma_wr, &bad_wr);
	if (snap_unlikely(rc)) {
		snap_error("DMA queue: %p failed to post opcode 0x%x\n",
				q, op);
		return rc;
	}

	q->tx_available--;
	return 0;
}

/**
 * snap_dma_q_write - DMA write to the host memory
 * @q:            dma queue
 * @src_buf:      where to get/put data
 * @len:          data length
 * @lkey:         local memory key
 * @dstaddr:      host physical or virtual address
 * @rmkey:        host memory key that describes remote memory
 * @comp:         dma completion structure
 *
 * The function starts non blocking memory transfer to the host memory. Once
 * data transfer is completed the user defined callback may be called.
 * Operations on the same dma queue are done in order.
 *
 * Return:
 * 0
 *	operation has been successfully submitted to the queue
 *	and is now in progress
 * \-EAGAIN
 *	queue does not have enough resources, must be retried later
 * < 0
 *	some other error has occured. Return value is -errno
 */
int snap_dma_q_write(struct snap_dma_q *q, void *src_buf, size_t len,
		uint32_t lkey, uint64_t dstaddr, uint32_t rmkey,
		struct snap_dma_completion *comp)
{
	return do_dma_xfer(q, src_buf, len, lkey, dstaddr, rmkey,
			IBV_WR_RDMA_WRITE, comp);
}

/**
 * snap_dma_q_read - DMA read to the host memory
 * @q:            dma queue
 * @dst_buf:      where to get/put data
 * @len:          data length
 * @lkey:         local memory key
 * @srcaddr:      host physical or virtual address
 * @rmkey:        host memory key that describes remote memory
 * @comp:         dma completion structure
 *
 * The function starts non blocking memory transfer from the host memory. Once
 * data transfer is completed the user defined callback may be called.
 * Operations on the same dma queue are done in order.
 *
 * Return:
 * 0
 *	operation has been successfully submitted to the queue
 *	and is now in progress
 * \-EAGAIN
 *	queue does not have enough resources, must be retried later
 * < 0
 *	some other error has occured. Return value is -errno
 */
int snap_dma_q_read(struct snap_dma_q *q, void *dst_buf, size_t len,
		uint32_t lkey, uint64_t srcaddr, uint32_t rmkey,
		struct snap_dma_completion *comp)
{
	return do_dma_xfer(q, dst_buf, len, lkey, srcaddr, rmkey,
			IBV_WR_RDMA_READ, comp);
}

/**
 * snap_dma_q_send_completion - send completion to the host
 * @q:       dma queue to
 * @src_buf: local buffer to copy the completion data from.
 * @len:     the length of completion. E.x. 16 bytes for the NVME. It
 *           must be less then the value of the
 *           &struct snap_dma_q_create_attr.tx_elem_size
 *
 * The function sends a completion notification to the host. The exact meaning of
 * the 'completion' is defined by the emulation layer. For example in case of
 * NVME it means that completion entry is placed in the completion queue and
 * MSI-X interrupt is triggered.
 *
 * Note that it is safe to use @src_buf after function return.
 *
 * Return:
 * 0
 *	operation has been successfully submitted to the queue
 *	and is now in progress
 * \-EAGAIN
 *	queue does not have enough resources, must be retried later
 * < 0
 *	some other error has occured. Return value is -errno
 *
 */
int snap_dma_q_send_completion(struct snap_dma_q *q, void *src_buf, size_t len)
{
	struct ibv_qp *qp = q->sw_qp.qp;
	struct ibv_send_wr send_wr, *bad_wr;
	struct ibv_sge sge;
	int rc;

	if (snap_unlikely(len > q->tx_elem_size))
		return -EINVAL;

	if (snap_unlikely(!qp_can_tx(q)))
		return -EAGAIN;

	sge.addr = (uint64_t)src_buf;
	sge.length = len;

	send_wr.opcode = IBV_WR_SEND;
	send_wr.send_flags = IBV_SEND_SIGNALED|IBV_SEND_INLINE;
	send_wr.num_sge = 1;
	send_wr.sg_list = &sge;
	send_wr.next = NULL;
	send_wr.wr_id = 0;

	rc = ibv_post_send(qp, &send_wr, &bad_wr);
	if (snap_unlikely(rc)) {
		snap_error("DMA queue %p: failed to post send: %m\n", q);
		return rc;
	}

	q->tx_available--;
	return 0;
}

/**
 * snap_dma_q_flush - wait for outstanding operations to complete
 * @q:   dma queue
 *
 * The function waits until all outstanding operations started with
 * mlx_dma_q_read(), mlx_dma_q_write() or mlx_dma_q_send_completion() are
 * finished. The function does not progress receive operation.
 *
 * The purpose of this function is to facilitate blocking mode dma
 * and completion operations.
 *
 * Return: number of completed operations or -errno.
 */
int snap_dma_q_flush(struct snap_dma_q *q)
{
	int n;

	n = 0;
	while (q->tx_available < q->tx_qsize)
		n += snap_dma_q_progress_tx(q);
	return n;
}

/**
 * snap_dma_q_get_fw_qpnum - get FW qp number
 * @q:   dma queue
 *
 * The qp number that can be used by the FW emulation objects
 * See snap_dma_q_create() for the detailed explanation
 *
 * Return: fw qp number
 */
uint32_t snap_dma_q_get_fw_qpnum(struct snap_dma_q *q)
{
	return q->fw_qp.qp->qp_num;
}
