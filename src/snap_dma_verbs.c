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

static inline int do_verbs_dma_xfer(struct snap_dma_q *q,
			struct ibv_send_wr *wr)
{
	int rc;
	struct ibv_send_wr *bad_wr = NULL;
	struct ibv_qp *qp = snap_qp_to_verbs_qp(q->sw_qp.qp);

	rc = ibv_post_send(qp, wr, &bad_wr);
	if (snap_unlikely(rc))
		snap_error("DMA queue: %p failed to post opcode 0x%x\n",
			   q, bad_wr ? bad_wr->opcode : 0);

	return rc;
}

static void verbs_dma_done(struct snap_dma_completion *comp, int status)
{
	struct snap_dma_q_iov_ctx *iov_ctx;
	struct snap_dma_q *q;
	struct snap_dma_completion *orig_comp;

	iov_ctx = container_of(comp, struct snap_dma_q_iov_ctx, comp);

	q = iov_ctx->q;
	orig_comp = (struct snap_dma_completion *)iov_ctx->uctx;

	q->tx_available += (iov_ctx->n_bb - 1);
	TAILQ_INSERT_HEAD(&q->free_iov_ctx, iov_ctx, entry);

	if (orig_comp && --orig_comp->count == 0)
		orig_comp->func(orig_comp, status);
}

static struct snap_dma_q_iov_ctx*
verbs_prepare_iov_ctx(struct snap_dma_q *q, int n_bb,
		struct snap_dma_completion *comp)
{
	struct snap_dma_q_iov_ctx *iov_ctx;

	iov_ctx = TAILQ_FIRST(&q->free_iov_ctx);
	if (!iov_ctx) {
		errno = -ENOMEM;
		snap_error("dma_q:%p Out of iov_ctx from pool\n", q);
		return NULL;
	}

	TAILQ_REMOVE(&q->free_iov_ctx, iov_ctx, entry);

	iov_ctx->n_bb = n_bb;
	iov_ctx->uctx = comp;
	iov_ctx->comp.func = verbs_dma_done;
	iov_ctx->comp.count = 1;

	return iov_ctx;
}

static inline void verbs_dma_q_prepare_wr(struct ibv_send_wr *wr,
			int num_wr,	struct ibv_sge **l_sgl, int *num_sge,
			struct ibv_sge *r_sgl, enum ibv_wr_opcode op, int flags,
			struct snap_dma_completion *comp)
{
	int i;

	for (i = 0; i < num_wr; i++) {
		wr[i].opcode = op;
		wr[i].send_flags = IBV_SEND_SIGNALED | flags;
		wr[i].num_sge = num_sge[i];
		wr[i].sg_list = l_sgl[i];
		wr[i].wr.rdma.rkey = r_sgl[i].lkey;
		wr[i].wr.rdma.remote_addr = r_sgl[i].addr;
		if (i < num_wr - 1) {
			wr[i].wr_id = 0;
			wr[i].next = &wr[i + 1];
		} else {
			wr[i].wr_id = (uint64_t)comp;
			wr[i].next = NULL;
		}
	}
}

static inline int verbs_dma_q_write(struct snap_dma_q *q,
				void *src_buf, size_t len, uint32_t lkey,
				uint64_t dstaddr, uint32_t rmkey,
				struct snap_dma_completion *comp)
{
	int num_sge[1];
	struct ibv_send_wr wr[1];
	struct ibv_sge *l_sgl[1], r_sgl[1], l_sge[1][1];

	l_sge[0][0].addr = (uint64_t)src_buf;
	l_sge[0][0].length = len;
	l_sge[0][0].lkey = lkey;

	l_sgl[0] = l_sge[0];
	num_sge[0] = 1;

	r_sgl[0].addr = dstaddr;
	r_sgl[0].length = len;
	r_sgl[0].lkey = rmkey;

	verbs_dma_q_prepare_wr(wr, 1, l_sgl, num_sge, r_sgl,
			IBV_WR_RDMA_WRITE, 0, comp);

	return do_verbs_dma_xfer(q, wr);
}

static inline int verbs_dma_q_writec(struct snap_dma_q *q,
				struct snap_dma_q_io_attr *io_attr,
				struct snap_dma_completion *comp, int *n_bb)
{
	return -ENOTSUP;
}

static inline int verbs_dma_q_writev2v(struct snap_dma_q *q,
				struct snap_dma_q_io_attr *io_attr,
				struct snap_dma_completion *comp, int *n_bb)
{
	int num_sge[io_attr->riov_cnt];
	struct ibv_send_wr wr[io_attr->riov_cnt];
	struct ibv_sge *l_sgl[io_attr->riov_cnt], r_sgl[io_attr->riov_cnt];
	struct snap_dma_q_iov_ctx *iov_ctx;

	if (snap_dma_build_sgl(io_attr, n_bb, num_sge, l_sgl, r_sgl))
		return -EINVAL;

	if (snap_unlikely(!qp_can_tx(q, *n_bb)))
		return -EAGAIN;

	iov_ctx = verbs_prepare_iov_ctx(q, *n_bb, comp);
	if (!iov_ctx)
		return errno;

	verbs_dma_q_prepare_wr(wr, io_attr->riov_cnt, l_sgl, num_sge, r_sgl,
			IBV_WR_RDMA_WRITE, 0, &iov_ctx->comp);

	return do_verbs_dma_xfer(q, wr);
}

static inline int verbs_dma_q_write_short(struct snap_dma_q *q, void *src_buf,
					  size_t len, uint64_t dstaddr,
					  uint32_t rmkey, int *n_bb)
{
	int num_sge[1];
	struct ibv_send_wr wr[1];
	struct ibv_sge *l_sgl[1], r_sgl[1], l_sge[1][1];

	*n_bb = 1;
	if (snap_unlikely(!qp_can_tx(q, *n_bb)))
		return -EAGAIN;

	l_sge[0][0].addr = (uint64_t)src_buf;
	l_sge[0][0].length = len;
	l_sge[0][0].lkey = 0;

	l_sgl[0] = l_sge[0];
	num_sge[0] = 1;

	r_sgl[0].addr = dstaddr;
	r_sgl[0].length = len;
	r_sgl[0].lkey = rmkey;

	verbs_dma_q_prepare_wr(wr, 1, l_sgl, num_sge, r_sgl,
			IBV_WR_RDMA_WRITE, IBV_SEND_INLINE, NULL);

	return do_verbs_dma_xfer(q, wr);
}

static inline int verbs_dma_q_read(struct snap_dma_q *q, void *dst_buf, size_t len,
				   uint32_t lkey, uint64_t srcaddr, uint32_t rmkey,
				   struct snap_dma_completion *comp)
{
	int num_sge[1];
	struct ibv_send_wr wr[1];
	struct ibv_sge *l_sgl[1], r_sgl[1], l_sge[1][1];

	l_sge[0][0].addr = (uint64_t)dst_buf;
	l_sge[0][0].length = len;
	l_sge[0][0].lkey = lkey;

	l_sgl[0] = l_sge[0];
	num_sge[0] = 1;

	r_sgl[0].addr = srcaddr;
	r_sgl[0].length = len;
	r_sgl[0].lkey = rmkey;

	verbs_dma_q_prepare_wr(wr, 1, l_sgl, num_sge, r_sgl,
			IBV_WR_RDMA_READ, 0, comp);

	return do_verbs_dma_xfer(q, wr);
}

static int verbs_dma_q_readc(struct snap_dma_q *q,
				struct snap_dma_q_io_attr *io_attr,
				struct snap_dma_completion *comp, int *n_bb)
{
	return -ENOTSUP;
}

static inline int verbs_dma_q_readv2v(struct snap_dma_q *q,
				struct snap_dma_q_io_attr *io_attr,
				struct snap_dma_completion *comp, int *n_bb)
{
	int num_sge[io_attr->riov_cnt];
	struct ibv_send_wr wr[io_attr->riov_cnt];
	struct ibv_sge *l_sgl[io_attr->riov_cnt], r_sgl[io_attr->riov_cnt];
	struct snap_dma_q_iov_ctx *iov_ctx;

	if (snap_dma_build_sgl(io_attr, n_bb, num_sge, l_sgl, r_sgl))
		return -EINVAL;

	if (snap_unlikely(!qp_can_tx(q, *n_bb)))
		return -EAGAIN;

	iov_ctx = verbs_prepare_iov_ctx(q, *n_bb, comp);
	if (!iov_ctx)
		return errno;

	verbs_dma_q_prepare_wr(wr, io_attr->riov_cnt, l_sgl, num_sge, r_sgl,
			IBV_WR_RDMA_READ, 0, &iov_ctx->comp);

	return do_verbs_dma_xfer(q, wr);
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
#ifdef __COVERITY__
	struct ibv_wc wcs[SNAP_DMA_MAX_RX_COMPLETIONS] = {};
#else
	struct ibv_wc wcs[SNAP_DMA_MAX_RX_COMPLETIONS];
#endif
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
#ifdef __COVERITY__
	struct ibv_wc wcs[SNAP_DMA_MAX_TX_COMPLETIONS] = {};
#else
	struct ibv_wc wcs[SNAP_DMA_MAX_TX_COMPLETIONS];
#endif
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
		struct snap_rx_completion *rx_completions, int max_completions)
{
#ifdef __COVERITY__
	struct ibv_wc wcs[SNAP_DMA_MAX_RX_COMPLETIONS] = {};
#else
	struct ibv_wc wcs[SNAP_DMA_MAX_RX_COMPLETIONS];
#endif
	int i, n;
	int rc;
	struct ibv_recv_wr rx_wr[SNAP_DMA_MAX_RX_COMPLETIONS + 1], *bad_wr;
	struct ibv_sge rx_sge[SNAP_DMA_MAX_RX_COMPLETIONS + 1];

	max_completions = snap_min(max_completions, SNAP_DMA_MAX_RX_COMPLETIONS);
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

		rx_completions[i].data = (void *)wcs[i].wr_id;
		rx_completions[i].byte_len = wcs[i].byte_len;
		rx_completions[i].imm_data = wcs[i].imm_data;

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
#ifdef __COVERITY__
	struct ibv_wc wcs[SNAP_DMA_MAX_TX_COMPLETIONS] = {};
#else
	struct ibv_wc wcs[SNAP_DMA_MAX_TX_COMPLETIONS];
#endif
	int i, n;
	struct snap_dma_completion *dma_comp;

	max_completions = snap_min(max_completions, SNAP_DMA_MAX_TX_COMPLETIONS);
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
	while (q->tx_available < q->sw_qp.dv_qp.hw_qp.sq.wqe_cnt)
		n += verbs_dma_q_progress_tx(q);

	return n;
}

static int verbs_dma_q_flush_nowait(struct snap_dma_q *q, struct snap_dma_completion *comp, int *n_bb)
{
	int num_sge[1];
	struct ibv_send_wr wr[1];
	struct ibv_sge *l_sgl[1], r_sgl[1], l_sge[1][1];

	*n_bb = 1;
	if (snap_unlikely(!qp_can_tx(q, *n_bb)))
		return -EAGAIN;

	l_sge[0][0].addr = 0;
	l_sge[0][0].length = 0;
	l_sge[0][0].lkey = 0;

	l_sgl[0] = l_sge[0];
	num_sge[0] = 1;

	r_sgl[0].addr = 0;
	r_sgl[0].length = 0;
	r_sgl[0].lkey = 0;

	verbs_dma_q_prepare_wr(wr, 1, l_sgl, num_sge, r_sgl,
			IBV_WR_RDMA_WRITE, 0, comp);

	return do_verbs_dma_xfer(q, wr);
}

static inline int verbs_dma_q_send(struct snap_dma_q *q, void *in_buf, size_t in_len,
				    uint64_t addr, int len, uint32_t key,
				    int *n_bb)
{
	return -ENOTSUP;
}

static bool verbs_dma_q_empty(struct snap_dma_q *q)
{
	return q->tx_available == q->sw_qp.dv_qp.hw_qp.sq.wqe_cnt;
}

const struct snap_dma_q_ops verb_ops = {
	.mode            = SNAP_DMA_Q_MODE_VERBS,
	.write           = verbs_dma_q_write,
	.writev2v         = verbs_dma_q_writev2v,
	.writec           = verbs_dma_q_writec,
	.write_short     = verbs_dma_q_write_short,
	.read            = verbs_dma_q_read,
	.readv2v          = verbs_dma_q_readv2v,
	.readc            = verbs_dma_q_readc,
	.send_completion = verbs_dma_q_send_completion,
	.send            = verbs_dma_q_send,
	.progress_tx     = verbs_dma_q_progress_tx,
	.progress_rx     = verbs_dma_q_progress_rx,
	.poll_rx     = verbs_dma_q_poll_rx,
	.poll_tx     = verbs_dma_q_poll_tx,
	.arm = verbs_dma_q_arm,
	.flush = verbs_dma_q_flush,
	.flush_nowait = verbs_dma_q_flush_nowait,
	.empty = verbs_dma_q_empty
};
