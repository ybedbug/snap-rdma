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

#include "snap_dma_internal.h"
#include "config.h"

/* Verbs implementation */

static inline int do_verbs_dma_xfer(struct snap_dma_q *q, void *buf, size_t len,
		uint32_t lkey, uint64_t raddr, uint32_t rkey, int op, int flags,
		struct snap_dma_completion *comp)
{
	struct ibv_qp *qp = snap_qp_to_verbs_qp(q->sw_qp.qp);
	struct ibv_send_wr rdma_wr, *bad_wr;
	struct ibv_sge sge;
	int rc;

	sge.addr = (uint64_t)buf;
	sge.length = len;
	sge.lkey = lkey;

	rdma_wr.opcode = op;
	rdma_wr.send_flags = IBV_SEND_SIGNALED | flags;
	rdma_wr.num_sge = 1;
	rdma_wr.sg_list = &sge;
	rdma_wr.wr_id = (uint64_t)comp;
	rdma_wr.wr.rdma.rkey = rkey;
	rdma_wr.wr.rdma.remote_addr = raddr;
	rdma_wr.next = NULL;

	rc = ibv_post_send(qp, &rdma_wr, &bad_wr);
	if (snap_unlikely(rc))
		snap_error("DMA queue: %p failed to post opcode 0x%x\n",
			   q, op);

	return rc;
}

static inline int verbs_dma_q_write(struct snap_dma_q *q, void *src_buf, size_t len,
				    uint32_t lkey, uint64_t dstaddr, uint32_t rmkey,
				    struct snap_dma_completion *comp)
{
	return do_verbs_dma_xfer(q, src_buf, len, lkey, dstaddr, rmkey,
			IBV_WR_RDMA_WRITE, 0, comp);
}

static inline int verbs_dma_q_writev(struct snap_dma_q *q, void *src_buf,
				uint32_t lkey, struct iovec *iov, int iov_cnt,
				uint32_t rmkey, struct snap_dma_completion *comp,
				int *n_bb)
{
	return -ENOTSUP;
}

static inline int verbs_dma_q_write_short(struct snap_dma_q *q, void *src_buf,
					  size_t len, uint64_t dstaddr,
					  uint32_t rmkey, int *n_bb)
{
	*n_bb = 1;
	if (snap_unlikely(!qp_can_tx(q, *n_bb)))
		return -EAGAIN;

	return do_verbs_dma_xfer(q, src_buf, len, 0, dstaddr, rmkey,
				 IBV_WR_RDMA_WRITE, IBV_SEND_INLINE, NULL);
}

static inline int verbs_dma_q_read(struct snap_dma_q *q, void *dst_buf, size_t len,
				   uint32_t lkey, uint64_t srcaddr, uint32_t rmkey,
				   struct snap_dma_completion *comp)
{
	return do_verbs_dma_xfer(q, dst_buf, len, lkey, srcaddr, rmkey,
			IBV_WR_RDMA_READ, 0, comp);
}

static inline int verbs_dma_q_readv(struct snap_dma_q *q, void *dst_buf,
				uint32_t lkey, struct iovec *iov, int iov_cnt,
				uint32_t rmkey, struct snap_dma_completion *comp,
				int *n_bb)
{
	return -ENOTSUP;
}

static inline int verbs_dma_q_send_completion(struct snap_dma_q *q, void *src_buf,
					      size_t len, int *n_bb)
{
	struct ibv_qp *qp = snap_qp_to_verbs_qp(q->sw_qp.qp);
	struct ibv_send_wr send_wr, *bad_wr;
	struct ibv_sge sge;
	int rc;

	*n_bb = 1;
	if (snap_unlikely(!qp_can_tx(q, *n_bb)))
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
	if (snap_unlikely(rc))
		snap_error("DMA queue %p: failed to post send: %m\n", q);

	return rc;
}

static inline int verbs_dma_q_progress_rx(struct snap_dma_q *q)
{
	struct ibv_wc wcs[SNAP_DMA_MAX_RX_COMPLETIONS];
	int i, n;
	int rc;
	struct ibv_recv_wr rx_wr[SNAP_DMA_MAX_RX_COMPLETIONS + 1], *bad_wr;
	struct ibv_sge rx_sge[SNAP_DMA_MAX_RX_COMPLETIONS + 1];

	n = ibv_poll_cq(snap_cq_to_verbs_cq(q->sw_qp.rx_cq), SNAP_DMA_MAX_RX_COMPLETIONS, wcs);
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
				snap_error("dma queue %p: got unexpected completion status 0x%x, opcode 0x%x\n",
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
	rc = ibv_post_recv(snap_qp_to_verbs_qp(q->sw_qp.qp), rx_wr, &bad_wr);
	if (snap_unlikely(rc))
		snap_error("dma queue %p: failed to post recv: errno=%d\n",
				q, rc);
	return n;
}

static inline int verbs_dma_q_progress_tx(struct snap_dma_q *q)
{
	struct ibv_wc wcs[SNAP_DMA_MAX_TX_COMPLETIONS];
	struct snap_dma_completion *comp;
	int i, n;

	n = ibv_poll_cq(snap_cq_to_verbs_cq(q->sw_qp.tx_cq), SNAP_DMA_MAX_TX_COMPLETIONS, wcs);
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
			snap_error("dma queue %p: got unexpected completion status 0x%x, opcode 0x%x\n",
				   q, wcs[i].status, wcs[i].opcode);
		/* wr_id, status, qp_num and vendor_err are still valid in
		 * case of error
		 **/
		comp = (struct snap_dma_completion *)wcs[i].wr_id;
		if (!comp)
			continue;

		if (--comp->count == 0)
			comp->func(comp, wcs[i].status);
	}

	return n;
}

static inline int verbs_dma_q_poll_rx(struct snap_dma_q *q,
		struct snap_rx_completion **rx_completions, int max_completions)
{
	struct ibv_wc wcs[max_completions];
	int i, n;
	int rc;
	struct ibv_recv_wr rx_wr[SNAP_DMA_MAX_RX_COMPLETIONS + 1], *bad_wr;
	struct ibv_sge rx_sge[SNAP_DMA_MAX_RX_COMPLETIONS + 1];

	n = ibv_poll_cq(snap_cq_to_verbs_cq(q->sw_qp.rx_cq), max_completions, wcs);
	if (n == 0)
		return 0;

	if (snap_unlikely(n < 0)) {
		snap_error("dma queue %p: failed to poll rx cq: errno=%d\n", q, n);
		return 0;
	}

	for (i = 0; i < n && i < max_completions; i++) {
		if (snap_unlikely(wcs[i].status != IBV_WC_SUCCESS)) {
			if (wcs[i].status == IBV_WC_WR_FLUSH_ERR) {
				snap_debug("dma queue %p: got FLUSH_ERROR\n", q);
			} else {
				snap_error("dma queue %p: got unexpected completion status 0x%x, opcode 0x%x\n",
					   q, wcs[i].status, wcs[i].opcode);
			}
			return n;
		}

		rx_completions[i]->data = (void *)wcs[i].wr_id;
		rx_completions[i]->byte_len = wcs[i].byte_len;
		rx_completions[i]->imm_data = wcs[i].imm_data;

		rx_sge[i].addr = wcs[i].wr_id;
		rx_sge[i].length = q->rx_elem_size;
		rx_sge[i].lkey = q->sw_qp.rx_mr->lkey;

		rx_wr[i].wr_id = rx_sge[i].addr;
		rx_wr[i].next = &rx_wr[i + 1];
		rx_wr[i].sg_list = &rx_sge[i];
		rx_wr[i].num_sge = 1;
	}

	rx_wr[i - 1].next = NULL;
	rc = ibv_post_recv(snap_qp_to_verbs_qp(q->sw_qp.qp), rx_wr, &bad_wr);
	if (snap_unlikely(rc))
		snap_error("dma queue %p: failed to post recv: errno=%d\n",
				q, rc);
	return n;
}

static inline int verbs_dma_q_poll_tx(struct snap_dma_q *q, struct snap_dma_completion **comp, int max_completions)
{
	struct ibv_wc wcs[max_completions];
	int i, n;
	struct snap_dma_completion *dma_comp;

	n = ibv_poll_cq(snap_cq_to_verbs_cq(q->sw_qp.tx_cq), max_completions, wcs);
	if (n == 0)
		return 0;

	if (snap_unlikely(n < 0)) {
		snap_error("dma queue %p: failed to poll tx cq: errno=%d\n",
				q, n);
		return 0;
	}

	q->tx_available += n;

	for (i = 0; i < n && i < max_completions; i++) {
		if (snap_unlikely(wcs[i].status != IBV_WC_SUCCESS))
			snap_error("dma queue %p: got unexpected completion status 0x%x, opcode 0x%x\n",
				   q, wcs[i].status, wcs[i].opcode);
		/* wr_id, status, qp_num and vendor_err are still valid in
		 * case of error
		 **/
		dma_comp = (struct snap_dma_completion *)wcs[i].wr_id;
		if (--dma_comp->count == 0)
			comp[i] = dma_comp;
	}

	return n;
}

static int verbs_dma_q_arm(struct snap_dma_q *q)
{
	int rc;

	rc = ibv_req_notify_cq(snap_cq_to_verbs_cq(q->sw_qp.tx_cq), 0);
	if (rc)
		return rc;

	return ibv_req_notify_cq(snap_cq_to_verbs_cq(q->sw_qp.rx_cq), 0);
}

static int verbs_dma_q_flush(struct snap_dma_q *q)
{
	int n;

	n = 0;
	while (q->tx_available < q->tx_qsize)
		n += verbs_dma_q_progress_tx(q);

	return n;
}

struct snap_dma_q_ops verb_ops = {
	.write           = verbs_dma_q_write,
	.writev           = verbs_dma_q_writev,
	.write_short     = verbs_dma_q_write_short,
	.read            = verbs_dma_q_read,
	.readv            = verbs_dma_q_readv,
	.send_completion = verbs_dma_q_send_completion,
	.progress_tx     = verbs_dma_q_progress_tx,
	.progress_rx     = verbs_dma_q_progress_rx,
	.poll_rx     = verbs_dma_q_poll_rx,
	.poll_tx     = verbs_dma_q_poll_tx,
	.arm = verbs_dma_q_arm,
	.flush = verbs_dma_q_flush
};
