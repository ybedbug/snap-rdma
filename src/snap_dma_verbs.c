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
	struct ibv_send_wr *bad_wr;
	struct ibv_qp *qp = snap_qp_to_verbs_qp(q->sw_qp.qp);

	rc = ibv_post_send(qp, wr, &bad_wr);
	if (snap_unlikely(rc))
		snap_error("DMA queue: %p failed to post opcode 0x%x\n",
			   q, bad_wr->opcode);

	return rc;
}

static void verbs_dma_done(struct snap_dma_completion *comp, int status)
{
	struct snap_dma_q_io_ctx *io_ctx;
	struct snap_dma_q *q;
	struct snap_dma_completion *orig_comp;

	io_ctx = container_of(comp, struct snap_dma_q_io_ctx, comp);

	q = io_ctx->q;
	orig_comp = (struct snap_dma_completion *)io_ctx->uctx;

	q->tx_available += (io_ctx->n_bb - 1);
	TAILQ_INSERT_HEAD(&q->free_iov_ctx, io_ctx, entry);

	if (orig_comp && --orig_comp->count == 0)
		orig_comp->func(orig_comp, status);
}

static struct snap_dma_q_io_ctx*
verbs_prepare_io_ctx(struct snap_dma_q *q, int n_bb,
		struct snap_dma_completion *comp)
{
	struct snap_dma_q_io_ctx *io_ctx;

	io_ctx = TAILQ_FIRST(&q->free_iov_ctx);
	if (!io_ctx) {
		errno = -ENOMEM;
		snap_error("dma_q:%p Out of iov io_ctx from pool\n", q);
		return NULL;
	}

	TAILQ_REMOVE(&q->free_iov_ctx, io_ctx, entry);

	io_ctx->n_bb = n_bb;
	io_ctx->uctx = comp;
	io_ctx->comp.func = verbs_dma_done;
	io_ctx->comp.count = 1;

	return io_ctx;
}

static inline void verbs_dma_q_prepare_wr(struct ibv_send_wr *wr,
			int num_wr,	struct ibv_sge **l_sgl, int *num_sge,
			struct ibv_sge *r_sgl, int op, int flags,
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

static inline int verbs_dma_q_writev(struct snap_dma_q *q,
				struct snap_dma_q_io_attr *io_attr,
				struct snap_dma_completion *comp, int *n_bb)
{
	int i, num_sge[io_attr->riov_cnt];
	size_t offset;
	struct ibv_send_wr wr[io_attr->riov_cnt];
	struct ibv_sge r_sgl[io_attr->riov_cnt];
	struct ibv_sge *l_sgl[io_attr->riov_cnt];
	struct ibv_sge l_sge[io_attr->riov_cnt][1];
	struct snap_dma_q_io_ctx *io_ctx;

	/* `riov_cnt` number of wr, each wr will need a WQE
	 * and each WQE only consume 1 BB
	 */
	*n_bb = io_attr->riov_cnt;
	if (snap_unlikely(!qp_can_tx(q, *n_bb))) {
		snap_debug("q->tx_available out of use\n");
		return -EAGAIN;
	}

	offset = 0;
	/* `riov_cnt` number of wr, and each wr with a sgl only have 1 sge */
	for (i = 0; i < io_attr->riov_cnt; i++) {
		l_sge[i][0].addr = (uint64_t)(io_attr->liov[0].iov_base + offset);
		l_sge[i][0].length = io_attr->riov[i].iov_len;
		l_sge[i][0].lkey = io_attr->lkey[0];

		l_sgl[i] = l_sge[i];
		num_sge[i] = 1;

		r_sgl[i].addr = (uint64_t)io_attr->riov[i].iov_base;
		r_sgl[i].length = io_attr->riov[i].iov_len;
		r_sgl[i].lkey = io_attr->rkey[i];

		offset += io_attr->riov[i].iov_len;
	}

	io_ctx = verbs_prepare_io_ctx(q, *n_bb, comp);
	if (!io_ctx)
		return errno;

	verbs_dma_q_prepare_wr(wr, io_attr->riov_cnt, l_sgl, num_sge, r_sgl,
			IBV_WR_RDMA_WRITE, 0, &io_ctx->comp);

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
	int i, j, sge_cnt, num_sge[io_attr->riov_cnt];
	size_t len_to_handle, left, offset;
	struct ibv_send_wr wr[io_attr->riov_cnt];
	struct ibv_sge l_sge[io_attr->riov_cnt][SNAP_DMA_Q_MAX_SGE_NUM];
	struct ibv_sge *l_sgl[io_attr->riov_cnt], r_sgl[io_attr->riov_cnt];
	struct snap_dma_q_io_ctx *io_ctx;

	*n_bb = 0;
	left = 0;
	offset = 0;
	memset(num_sge, 0, sizeof(int) * io_attr->riov_cnt);

	for (i = 0, j = 0; i < io_attr->riov_cnt; i++) {
		len_to_handle = io_attr->riov[i].iov_len;
		sge_cnt = 0;

		while (j < io_attr->liov_cnt && len_to_handle > 0) {
			if (left != 0) {
				if (len_to_handle >= left) {
					len_to_handle -= left;
					l_sge[i][sge_cnt].addr = (uint64_t)(io_attr->liov[j].iov_base + offset);
					l_sge[i][sge_cnt].length = left;
					l_sge[i][sge_cnt].lkey = io_attr->lkey[j];
					j++;
					left = 0;
					offset = 0;
				} else {
					left -= len_to_handle;
					l_sge[i][sge_cnt].addr = (uint64_t)(io_attr->liov[j].iov_base + offset);
					l_sge[i][sge_cnt].length = len_to_handle;
					l_sge[i][sge_cnt].lkey = io_attr->lkey[j];
					offset += len_to_handle;
					len_to_handle = 0;
				}
			} else if (len_to_handle >= io_attr->liov[j].iov_len) {
				len_to_handle -= io_attr->liov[j].iov_len;
				l_sge[i][sge_cnt].addr = (uint64_t)io_attr->liov[j].iov_base;
				l_sge[i][sge_cnt].length = io_attr->liov[j].iov_len;
				l_sge[i][sge_cnt].lkey = io_attr->lkey[j];
				j++;
			} else {
				left = io_attr->liov[j].iov_len - len_to_handle;
				l_sge[i][sge_cnt].addr = (uint64_t)io_attr->liov[j].iov_base;
				l_sge[i][sge_cnt].length = len_to_handle;
				l_sge[i][sge_cnt].lkey = io_attr->lkey[j];
				offset = len_to_handle;
				len_to_handle = 0;
			}

			sge_cnt++;
			if (sge_cnt >= SNAP_DMA_Q_MAX_SGE_NUM) {
				snap_error("sge number exceed the max supported(30)\n");
				return -EINVAL;
			}
		}

		l_sgl[i] = l_sge[i];
		num_sge[i] = sge_cnt;

		r_sgl[i].addr = (uint64_t)io_attr->riov[i].iov_base;
		r_sgl[i].length = io_attr->riov[i].iov_len;
		r_sgl[i].lkey = io_attr->rkey[i];

		*n_bb += (sge_cnt <= 2) ? 1 : 1 + round_up((sge_cnt - 2), 4);
	}

	/* after for loop, j should equal to io_attr->liov_cnt,
	 *  and, left should be 0.
	 */

	io_ctx = verbs_prepare_io_ctx(q, *n_bb, comp);
	if (!io_ctx)
		return errno;

	verbs_dma_q_prepare_wr(wr, io_attr->riov_cnt, l_sgl, num_sge, r_sgl,
			IBV_WR_RDMA_WRITE, 0, &io_ctx->comp);

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

static int verbs_dma_q_readv(struct snap_dma_q *q,
				struct snap_dma_q_io_attr *io_attr,
				struct snap_dma_completion *comp, int *n_bb)
{
	int i, num_sge[io_attr->riov_cnt];
	size_t offset;
	struct ibv_send_wr wr[io_attr->riov_cnt];
	struct ibv_sge r_sgl[io_attr->riov_cnt];
	struct ibv_sge *l_sgl[io_attr->riov_cnt];
	struct ibv_sge l_sge[io_attr->riov_cnt][1];
	struct snap_dma_q_io_ctx *io_ctx;

	/* `riov_cnt` number of wr, each wr will need a WQE
	 * and each WQE only consume 1 BB
	 */
	*n_bb = io_attr->riov_cnt;
	if (snap_unlikely(!qp_can_tx(q, *n_bb))) {
		snap_debug("q->tx_available out of use\n");
		return -EAGAIN;
	}

	offset = 0;
	/* `riov_cnt` number of wr, and each wr with a sgl only have 1 sge */
	for (i = 0; i < io_attr->riov_cnt; i++) {
		l_sge[i][0].addr = (uint64_t)(io_attr->liov[0].iov_base + offset);
		l_sge[i][0].length = io_attr->riov[i].iov_len;
		l_sge[i][0].lkey = io_attr->lkey[0];

		l_sgl[i] = l_sge[i];
		num_sge[i] = 1;

		r_sgl[i].addr = (uint64_t)io_attr->riov[i].iov_base;
		r_sgl[i].length = io_attr->riov[i].iov_len;
		r_sgl[i].lkey = io_attr->rkey[i];

		offset += io_attr->riov[i].iov_len;
	}

	io_ctx = verbs_prepare_io_ctx(q, *n_bb, comp);
	if (!io_ctx)
		return errno;

	verbs_dma_q_prepare_wr(wr, io_attr->riov_cnt, l_sgl, num_sge, r_sgl,
			IBV_WR_RDMA_READ, 0, &io_ctx->comp);

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
	int i, j, sge_cnt, num_sge[io_attr->riov_cnt];
	size_t len_to_handle, left, offset;
	struct ibv_send_wr wr[io_attr->riov_cnt];
	struct ibv_sge l_sge[io_attr->riov_cnt][SNAP_DMA_Q_MAX_SGE_NUM];
	struct ibv_sge *l_sgl[io_attr->riov_cnt], r_sgl[io_attr->riov_cnt];
	struct snap_dma_q_io_ctx *io_ctx;

	*n_bb = 0;
	left = 0;
	offset = 0;
	memset(num_sge, 0, sizeof(int) * io_attr->riov_cnt);

	for (i = 0, j = 0; i < io_attr->riov_cnt; i++) {
		len_to_handle = io_attr->riov[i].iov_len;
		sge_cnt = 0;

		while (j < io_attr->liov_cnt && len_to_handle > 0) {
			if (left != 0) {
				if (len_to_handle >= left) {
					len_to_handle -= left;
					l_sge[i][sge_cnt].addr = (uint64_t)(io_attr->liov[j].iov_base + offset);
					l_sge[i][sge_cnt].length = left;
					l_sge[i][sge_cnt].lkey = io_attr->lkey[j];
					j++;
					left = 0;
					offset = 0;
				} else {
					left -= len_to_handle;
					l_sge[i][sge_cnt].addr = (uint64_t)(io_attr->liov[j].iov_base + offset);
					l_sge[i][sge_cnt].length = len_to_handle;
					l_sge[i][sge_cnt].lkey = io_attr->lkey[j];
					offset += len_to_handle;
					len_to_handle = 0;
				}
			} else if (len_to_handle >= io_attr->liov[j].iov_len) {
				len_to_handle -= io_attr->liov[j].iov_len;
				l_sge[i][sge_cnt].addr = (uint64_t)io_attr->liov[j].iov_base;
				l_sge[i][sge_cnt].length = io_attr->liov[j].iov_len;
				l_sge[i][sge_cnt].lkey = io_attr->lkey[j];
				j++;
			} else {
				left = io_attr->liov[j].iov_len - len_to_handle;
				l_sge[i][sge_cnt].addr = (uint64_t)io_attr->liov[j].iov_base;
				l_sge[i][sge_cnt].length = len_to_handle;
				l_sge[i][sge_cnt].lkey = io_attr->lkey[j];
				offset = len_to_handle;
				len_to_handle = 0;
			}

			sge_cnt++;
			if (sge_cnt >= SNAP_DMA_Q_MAX_SGE_NUM) {
				snap_error("sge number exceed the max supported(30)\n");
				return -EINVAL;
			}
		}

		l_sgl[i] = l_sge[i];
		num_sge[i] = sge_cnt;

		r_sgl[i].addr = (uint64_t)io_attr->riov[i].iov_base;
		r_sgl[i].length = io_attr->riov[i].iov_len;
		r_sgl[i].lkey = io_attr->rkey[i];

		*n_bb += (sge_cnt <= 2) ? 1 : 1 + round_up((sge_cnt - 2), 4);
	}

	/* after for loop, j should equal to io_attr->liov_cnt,
	 *  and, left should be 0.
	 */

	io_ctx = verbs_prepare_io_ctx(q, *n_bb, comp);
	if (!io_ctx)
		return errno;

	verbs_dma_q_prepare_wr(wr, io_attr->riov_cnt, l_sgl, num_sge, r_sgl,
			IBV_WR_RDMA_READ, 0, &io_ctx->comp);

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
		struct snap_rx_completion *rx_completions, int max_completions)
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

struct snap_dma_q_ops verb_ops = {
	.write           = verbs_dma_q_write,
	.writev           = verbs_dma_q_writev,
	.writev2v         = verbs_dma_q_writev2v,
	.writec           = verbs_dma_q_writec,
	.write_short     = verbs_dma_q_write_short,
	.read            = verbs_dma_q_read,
	.readv            = verbs_dma_q_readv,
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
