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

#include <stdint.h>

#if !defined(__DPA)
#include <arpa/inet.h>
#endif

#include "snap_dma_internal.h"
#include "snap_umr.h"

#include "config.h"

struct snap_dma_xfer_ctx {
	void *lbuf;
	uint32_t lkey;
	uint32_t rkey;
	uint64_t raddr;
	size_t len;

	bool use_fence;
	struct snap_dma_completion *comp;
};

static inline int do_dv_dma_xfer(struct snap_dma_q *q, void *buf, size_t len,
		uint32_t lkey, uint64_t raddr, uint32_t rkey, int op, int flags,
		struct snap_dma_completion *comp, bool use_fence)
{
	struct snap_dv_qp *dv_qp = &q->sw_qp.dv_qp;
	struct mlx5_wqe_ctrl_seg *ctrl;
	struct mlx5_wqe_raddr_seg *rseg;
	struct mlx5_wqe_data_seg *dseg;
	uint16_t comp_idx;
	uint8_t fm_ce_se = 0;

	fm_ce_se |= snap_dv_get_cq_update(dv_qp, comp);
	if (use_fence)
		fm_ce_se |= MLX5_WQE_CTRL_INITIATOR_SMALL_FENCE;

	ctrl = (struct mlx5_wqe_ctrl_seg *)snap_dv_get_wqe_bb(dv_qp);
	snap_set_ctrl_seg(ctrl, dv_qp->hw_qp.sq.pi, op, 0, dv_qp->hw_qp.qp_num,
			    fm_ce_se, 3, 0, 0);

	rseg = (struct mlx5_wqe_raddr_seg *)(ctrl + 1);
	rseg->raddr = htobe64((uintptr_t)raddr);
	rseg->rkey  = htobe32(rkey);

	dseg = (struct mlx5_wqe_data_seg *)(rseg + 1);
	mlx5dv_set_data_seg(dseg, len, lkey, (intptr_t)buf);

	snap_dv_wqe_submit(dv_qp, ctrl);

	/* it is better to start dma as soon as possible and do
	 * bookkeeping later
	 **/
	comp_idx = (dv_qp->hw_qp.sq.pi - 1) & (dv_qp->hw_qp.sq.wqe_cnt - 1);
	snap_dv_set_comp(dv_qp, comp_idx, comp, fm_ce_se, 1);
	return 0;
}

static int dv_dma_q_write(struct snap_dma_q *q, void *src_buf, size_t len,
			  uint32_t lkey, uint64_t dstaddr, uint32_t rmkey,
			  struct snap_dma_completion *comp)
{
	return do_dv_dma_xfer(q, src_buf, len, lkey, dstaddr, rmkey,
			MLX5_OPCODE_RDMA_WRITE, 0, comp, false);
}

static int dv_dma_q_read(struct snap_dma_q *q, void *dst_buf, size_t len,
			 uint32_t lkey, uint64_t srcaddr, uint32_t rmkey,
			 struct snap_dma_completion *comp)
{
	return do_dv_dma_xfer(q, dst_buf, len, lkey, srcaddr, rmkey,
			MLX5_OPCODE_RDMA_READ, 0, comp, false);
}

/* UMRs are not supported on the DPA yet */
__attribute__((unused)) static void
snap_use_klm_mkey_done(struct snap_dma_completion *comp, int status)
{
	struct snap_dma_q_io_ctx *io_ctx;
	struct snap_dma_q *q;
	struct snap_dma_completion *orig_comp;

	io_ctx = container_of(comp, struct snap_dma_q_io_ctx, comp);

	q = io_ctx->q;
	orig_comp = (struct snap_dma_completion *)io_ctx->uctx;

	if (io_ctx->io_type == SNAP_DMA_Q_IO_TYPE_IOV)
		TAILQ_INSERT_HEAD(&q->free_iov_ctx, io_ctx, entry);
	else
		TAILQ_INSERT_HEAD(&q->free_crypto_ctx, io_ctx, entry);

	if (orig_comp && --orig_comp->count == 0)
		orig_comp->func(orig_comp, status);
}

static inline int snap_iov_to_klm_mtt(struct iovec *iov, int iov_cnt,
			uint32_t *mkey, struct mlx5_klm *klm_mtt, size_t *len)
{
	int i;

	/*TODO: dynamically expand klm_mtt array */
	if (iov_cnt > SNAP_DMA_Q_MAX_IOV_CNT) {
		snap_error("iov_cnt:%d is larger than max supportted(%d)\n",
			iov_cnt, SNAP_DMA_Q_MAX_IOV_CNT);
		return -EINVAL;
	}

	*len = 0;
	for (i = 0; i < iov_cnt; i++) {
		klm_mtt[i].byte_count = iov[i].iov_len;
		klm_mtt[i].mkey = mkey[i];
		klm_mtt[i].address = (uintptr_t)iov[i].iov_base;

		*len += iov[i].iov_len;
	}

	return 0;
}

/* return NULL if prepare io_ctx failed in any reason,
 * and use 'errno' to pass the actually failure reason.
 */
static struct snap_dma_q_io_ctx*
snap_prepare_io_ctx(struct snap_dma_q *q,
				struct snap_dma_q_io_attr *io_attr,
				struct snap_dma_completion *comp, int *n_bb)
{
#if !defined(__DPA)
	int ret;
	size_t llen, rlen;
	struct snap_dma_q_io_ctx *io_ctx;
	struct snap_post_umr_attr umr_attr = {0};

	if (io_attr->io_type == SNAP_DMA_Q_IO_TYPE_IOV)
		io_ctx = TAILQ_FIRST(&q->free_iov_ctx);
	else
		io_ctx = TAILQ_FIRST(&q->free_crypto_ctx);
	if (!io_ctx) {
		errno = -ENOMEM;
		snap_error("dma_q:%p Out of io_ctx from pool\n", q);
		return NULL;
	}

	if (io_attr->io_type == SNAP_DMA_Q_IO_TYPE_IOV)
		TAILQ_REMOVE(&q->free_iov_ctx, io_ctx, entry);
	else
		TAILQ_REMOVE(&q->free_crypto_ctx, io_ctx, entry);

	*n_bb = 0;

	io_ctx->uctx = comp;
	io_ctx->comp.func = snap_use_klm_mkey_done;
	io_ctx->comp.count = 1;
	io_ctx->io_type = io_attr->io_type;

	if (io_attr->liov_cnt > 1) {
		/* post UMR WQE for local IOV memory */
		ret = snap_iov_to_klm_mtt(io_attr->liov, io_attr->liov_cnt,
					io_attr->lkey, io_ctx->klm_mtt, &llen);
		if (ret)
			goto insert_back;

		umr_attr.purpose |= SNAP_UMR_MKEY_MODIFY_ATTACH_MTT;
		umr_attr.klm_mkey = io_ctx->l_klm_mkey;
		umr_attr.klm_mtt = io_ctx->klm_mtt;
		umr_attr.klm_entries = io_attr->liov_cnt;

		ret = snap_umr_post_wqe(q, &umr_attr, NULL, n_bb);
		if (ret) {
			snap_error("dma_q:%p post umr wqe for local mkey failed, ret:%d\n", q, ret);
			goto insert_back;
		}
	} else {
		llen = io_attr->liov[0].iov_len;
	}

	/* post UMR WQE for remote klm mkey */
	if (io_attr->io_type & SNAP_DMA_Q_IO_TYPE_IOV) {
		ret = snap_iov_to_klm_mtt(io_attr->riov, io_attr->riov_cnt,
					io_attr->rkey, io_ctx->klm_mtt, &rlen);
		if (ret)
			goto insert_back;

		io_attr->len = rlen;
		if (rlen > llen)
			snap_error("lIOV(total len:%lu) cannot fit rIOV(total len:%lu)\n", llen, rlen);

		umr_attr.purpose |= SNAP_UMR_MKEY_MODIFY_ATTACH_MTT;
		umr_attr.klm_mkey = io_ctx->r_klm_mkey;
		umr_attr.klm_mtt = io_ctx->klm_mtt;
		umr_attr.klm_entries = io_attr->riov_cnt;
	}

	if (io_attr->io_type & SNAP_DMA_Q_IO_TYPE_ENCRYPTO) {
		umr_attr.purpose |= SNAP_UMR_MKEY_MODIFY_ATTACH_CRYPTO_BSF;
		umr_attr.encryption_order =
			SNAP_CRYPTO_BSF_ENCRYPTION_ORDER_ENCRYPTED_MEMORY_SIGNATURE;
		umr_attr.encryption_standard =
				SNAP_CRYPTO_BSF_ENCRYPTION_STANDARD_AES_XTS;
		umr_attr.raw_data_size = io_attr->len;
		umr_attr.crypto_block_size_pointer =
				SNAP_CRYPTO_BSF_CRYPTO_BLOCK_SIZE_POINTER_512;
		umr_attr.dek_pointer = io_attr->dek_obj_id;
		memcpy(umr_attr.xts_initial_tweak, io_attr->xts_initial_tweak,
				SNAP_CRYPTO_XTS_INITIAL_TWEAK_SIZE);
	}

	ret = snap_umr_post_wqe(q, &umr_attr, NULL, n_bb);
	if (ret) {
		snap_error("dma_q:%p post umr wqe for remote mkey failed, ret:%d\n", q, ret);
		goto insert_back;
	}

	*n_bb += 1; /* +1 for DMA WQE */

	return io_ctx;

insert_back:
	if (io_attr->io_type == SNAP_DMA_Q_IO_TYPE_IOV)
		TAILQ_INSERT_TAIL(&q->free_iov_ctx, io_ctx, entry);
	else
		TAILQ_INSERT_TAIL(&q->free_crypto_ctx, io_ctx, entry);

	errno = ret;
#endif
	return NULL;
}

int snap_prepare_dma_xfer_ctx(struct snap_dma_q *q,
				struct snap_dma_q_io_attr *io_attr,
				struct snap_dma_completion *comp,
				int *n_bb, struct snap_dma_xfer_ctx *dx_ctx)
{
	struct snap_dma_q_io_ctx *io_ctx;

	io_ctx = snap_prepare_io_ctx(q, io_attr, comp, n_bb);
	if (!io_ctx)
		return errno;

	if (io_attr->liov_cnt == 1) {
		dx_ctx->lbuf = io_attr->liov[0].iov_base;
		dx_ctx->lkey = io_attr->lkey[0];
	} else {
		dx_ctx->lbuf = (void *)io_ctx->l_klm_mkey->addr;
		dx_ctx->lkey = io_ctx->l_klm_mkey->mkey;
	}

	if (io_attr->io_type & SNAP_DMA_Q_IO_TYPE_ENCRYPTO)
		dx_ctx->raddr = 0; /* use zero based rdma if use bsf enabled mkey */
	else
		dx_ctx->raddr = io_ctx->r_klm_mkey->addr;
	dx_ctx->rkey = io_ctx->r_klm_mkey->mkey;
	dx_ctx->len = io_attr->len;
	dx_ctx->comp = &io_ctx->comp;
	dx_ctx->use_fence = true;

	return 0;
}

static int dv_dma_q_writev2v(struct snap_dma_q *q,
				struct snap_dma_q_io_attr *io_attr,
				struct snap_dma_completion *comp, int *n_bb)
{
	int ret;
	struct snap_dma_xfer_ctx dx_ctx = {0};

	ret = snap_prepare_dma_xfer_ctx(q, io_attr, comp, n_bb, &dx_ctx);
	if (ret)
		return ret;

	return do_dv_dma_xfer(q, dx_ctx.lbuf, dx_ctx.len, dx_ctx.lkey,
			dx_ctx.raddr, dx_ctx.rkey, MLX5_OPCODE_RDMA_WRITE, 0,
			dx_ctx.comp, dx_ctx.use_fence);
}

static int dv_dma_q_writec(struct snap_dma_q *q,
				struct snap_dma_q_io_attr *io_attr,
				struct snap_dma_completion *comp, int *n_bb)
{
	int ret;
	struct snap_dma_xfer_ctx dx_ctx = {0};

	ret = snap_prepare_dma_xfer_ctx(q, io_attr, comp, n_bb, &dx_ctx);
	if (ret)
		return ret;

	return do_dv_dma_xfer(q, dx_ctx.lbuf, dx_ctx.len, dx_ctx.lkey,
			dx_ctx.raddr, dx_ctx.rkey, MLX5_OPCODE_RDMA_WRITE, 0,
			dx_ctx.comp, dx_ctx.use_fence);
}

static int dv_dma_q_readv2v(struct snap_dma_q *q,
				struct snap_dma_q_io_attr *io_attr,
				struct snap_dma_completion *comp, int *n_bb)
{
	int ret;
	struct snap_dma_xfer_ctx dx_ctx = {0};

	ret = snap_prepare_dma_xfer_ctx(q, io_attr, comp, n_bb, &dx_ctx);
	if (ret)
		return ret;

	return do_dv_dma_xfer(q, dx_ctx.lbuf, dx_ctx.len, dx_ctx.lkey,
			dx_ctx.raddr, dx_ctx.rkey, MLX5_OPCODE_RDMA_READ, 0,
			dx_ctx.comp, dx_ctx.use_fence);
}

static int dv_dma_q_readc(struct snap_dma_q *q,
				struct snap_dma_q_io_attr *io_attr,
				struct snap_dma_completion *comp, int *n_bb)
{
	int ret;
	struct snap_dma_xfer_ctx dx_ctx = {0};

	ret = snap_prepare_dma_xfer_ctx(q, io_attr, comp, n_bb, &dx_ctx);
	if (ret)
		return ret;

	return do_dv_dma_xfer(q, dx_ctx.lbuf, dx_ctx.len, dx_ctx.lkey,
			dx_ctx.raddr, dx_ctx.rkey, MLX5_OPCODE_RDMA_READ, 0,
			dx_ctx.comp, dx_ctx.use_fence);
}

static inline int do_dv_xfer_inline(struct snap_dma_q *q, void *src_buf, size_t len,
				    int op, uint64_t raddr, uint32_t rkey,
				    struct snap_dma_completion *flush_comp, int *n_bb)
{
	struct snap_dv_qp *dv_qp = &q->sw_qp.dv_qp;
	struct mlx5_wqe_ctrl_seg *ctrl;
	struct mlx5_wqe_inl_data_seg *dseg;
	struct mlx5_wqe_raddr_seg *rseg;
	uint16_t pi, wqe_size, to_end;
	uint8_t fm_ce_se = 0;
	void *pdata;

	wqe_size = sizeof(*ctrl) + sizeof(*dseg) + len;
	if (op == MLX5_OPCODE_RDMA_WRITE)
		wqe_size += sizeof(*rseg);

	/* if flush_comp is set it means that we are dealing with the zero
	 * length rdma_write op. Check flush_comp instead of length to allow
	 * optimization in the fast path where the flush_comp is always NULL.
	 */
	if (flush_comp)
		wqe_size -= sizeof(*dseg);

	*n_bb = round_up(wqe_size, MLX5_SEND_WQE_BB);
	if (snap_unlikely(!qp_can_tx(q, *n_bb)))
		return -EAGAIN;

	fm_ce_se |= snap_dv_get_cq_update(dv_qp, flush_comp);

	ctrl = (struct mlx5_wqe_ctrl_seg *)snap_dv_get_wqe_bb(dv_qp);
	snap_set_ctrl_seg(ctrl, dv_qp->hw_qp.sq.pi, op, 0,
			    dv_qp->hw_qp.qp_num, fm_ce_se,
			    round_up(wqe_size, 16), 0, 0);

	if (op == MLX5_OPCODE_RDMA_WRITE) {
		rseg = (struct mlx5_wqe_raddr_seg *)(ctrl + 1);
		rseg->raddr = htobe64((uintptr_t)raddr);
		rseg->rkey = htobe32(rkey);
		dseg = (struct mlx5_wqe_inl_data_seg *)(rseg + 1);
	} else
		dseg = (struct mlx5_wqe_inl_data_seg *)(ctrl + 1);

	dseg->byte_count = htobe32(len | MLX5_INLINE_SEG);
	pdata = dseg + 1;

	/* handle wrap around, where inline data needs several building blocks */
	pi = dv_qp->hw_qp.sq.pi & (dv_qp->hw_qp.sq.wqe_cnt - 1);
	to_end = (dv_qp->hw_qp.sq.wqe_cnt - pi) * MLX5_SEND_WQE_BB -
		 sizeof(*ctrl) - sizeof(*dseg);
	if (op == MLX5_OPCODE_RDMA_WRITE)
		to_end -= sizeof(*rseg);

	/*
	 * flush_comp can be tested in compilation time, while src_buf isn't,
	 * so we better use it in fastpath. We rely on the fact that we always
	 * use this function either with src_buf or with flush_comp, but never
	 * with both.
	 */
#ifdef __COVERITY__
	if (src_buf) {
#else
	if (!flush_comp) {
#endif
		if (snap_unlikely(len > to_end)) {
			memcpy(pdata, src_buf, to_end);
			memcpy((void *)dv_qp->hw_qp.sq.addr, src_buf + to_end, len - to_end);
		} else {
			memcpy(pdata, src_buf, len);
		}
	}

	dv_qp->hw_qp.sq.pi += (*n_bb - 1);

	snap_dv_wqe_submit(dv_qp, ctrl);

	snap_dv_set_comp(dv_qp, pi, flush_comp, fm_ce_se, *n_bb);
	return 0;
}

static int dv_dma_q_send_completion(struct snap_dma_q *q, void *src_buf,
				    size_t len, int *n_bb)
{
	return do_dv_xfer_inline(q, src_buf, len, MLX5_OPCODE_SEND, 0, 0, NULL, n_bb);
}

static inline int dv_dma_q_send(struct snap_dma_q *q, void *in_buf, size_t in_len,
				    uint64_t addr, int len, uint32_t key,
				    int *n_bb)
{
	struct snap_dv_qp *dv_qp = &q->sw_qp.dv_qp;
	struct mlx5_wqe_ctrl_seg *ctrl;
	struct mlx5_wqe_inl_data_seg *in_seg;
	struct mlx5_wqe_data_seg *data_seg;
	uint16_t pi, wqe_size, to_end;
	uint16_t complement = 0;
	uint8_t fm_ce_se = 0;
	void *pdata;

	/* Every inline data segment occupies one or more octowords */
	if ((sizeof(*in_seg) + in_len) % 16)
		complement = 16 - ((sizeof(*in_seg) + in_len) % 16);
	wqe_size = sizeof(*ctrl) + sizeof(*data_seg) + sizeof(*in_seg) + in_len	+
			complement;

	*n_bb = round_up(wqe_size, MLX5_SEND_WQE_BB);

	if (snap_unlikely(!qp_can_tx(q, *n_bb)))
		return -EAGAIN;

	fm_ce_se |= snap_dv_get_cq_update(dv_qp, NULL);

	ctrl = (struct mlx5_wqe_ctrl_seg *)snap_dv_get_wqe_bb(dv_qp);
	snap_set_ctrl_seg(ctrl, dv_qp->hw_qp.sq.pi, MLX5_OPCODE_SEND, 0,
			dv_qp->hw_qp.qp_num, fm_ce_se,
			    round_up(wqe_size, 16), 0, 0);

	in_seg = (struct mlx5_wqe_inl_data_seg *)(ctrl + 1);
	in_seg->byte_count = htobe32(in_len | MLX5_INLINE_SEG);
	pdata = in_seg + 1;

	/* handle wrap around, where inline data needs several building blocks */
	pi = dv_qp->hw_qp.sq.pi & (dv_qp->hw_qp.sq.wqe_cnt - 1);
	to_end = (dv_qp->hw_qp.sq.wqe_cnt - pi) * MLX5_SEND_WQE_BB -
		 sizeof(*ctrl) - sizeof(*in_seg);

	if (snap_unlikely((in_len + complement + sizeof(*data_seg)) > to_end)) {
		memcpy(pdata, in_buf, to_end);
		memcpy((void *)dv_qp->hw_qp.sq.addr, in_buf + to_end, in_len - to_end);
		data_seg = (struct mlx5_wqe_data_seg *)
				((void *)dv_qp->hw_qp.sq.addr + in_len - to_end + complement);
		mlx5dv_set_data_seg(data_seg, len, key, (uintptr_t)addr);
	} else {
		memcpy(pdata, in_buf, in_len);
		data_seg = (struct mlx5_wqe_data_seg *)(pdata + in_len + complement);
		mlx5dv_set_data_seg(data_seg, len, key, (uintptr_t)addr);
	}

	dv_qp->hw_qp.sq.pi += (*n_bb - 1);

	snap_dv_wqe_submit(dv_qp, ctrl);

	snap_dv_set_comp(dv_qp, pi, NULL, fm_ce_se, *n_bb);
	return 0;
}


static int dv_dma_q_write_short(struct snap_dma_q *q, void *src_buf, size_t len,
				uint64_t dstaddr, uint32_t rmkey, int *n_bb)
{
	return do_dv_xfer_inline(q, src_buf, len, MLX5_OPCODE_RDMA_WRITE,
			dstaddr, rmkey, NULL, n_bb);
}

static inline struct mlx5_cqe64 *snap_dv_get_cqe(struct snap_hw_cq *dv_cq, int cqe_size)
{
	struct mlx5_cqe64 *cqe;

	/* note: that the cq_size is known at the compilation time. We pass it
	 * down here so that branch and multiplication will be done at the
	 * compile time during inlining
	 **/
	cqe = (struct mlx5_cqe64 *)(dv_cq->cq_addr + (dv_cq->ci & (dv_cq->cqe_cnt - 1)) *
				    (uint64_t)cqe_size);
	return cqe_size == 64 ? cqe : cqe + 1;
}

static inline struct mlx5_cqe64 *snap_dv_poll_cq(struct snap_hw_cq *dv_cq, int cqe_size)
{
	struct mlx5_cqe64 *cqe;

	cqe = snap_dv_get_cqe(dv_cq, cqe_size);

	/* cqe is hw owned */
	if (mlx5dv_get_cqe_owner(cqe) == !(dv_cq->ci & dv_cq->cqe_cnt))
		return NULL;

	/* and must have valid opcode */
	if (mlx5dv_get_cqe_opcode(cqe) == MLX5_CQE_INVALID)
		return NULL;

	dv_cq->ci++;

	snap_debug("ci: %d CQ opcode %d size %d wqe_counter %d scatter32 %d scatter64 %d\n",
		   dv_cq->ci,
		   mlx5dv_get_cqe_opcode(cqe),
		   be32toh(cqe->byte_cnt),
		   be16toh(cqe->wqe_counter),
		   cqe->op_own & MLX5_INLINE_SCATTER_32,
		   cqe->op_own & MLX5_INLINE_SCATTER_64);
	return cqe;
}

static const char *snap_dv_cqe_err_opcode(struct mlx5_err_cqe *ecqe)
{
	uint8_t wqe_err_opcode = be32toh(ecqe->s_wqe_opcode_qpn) >> 24;

	switch (ecqe->op_own >> 4) {
	case MLX5_CQE_REQ_ERR:
		switch (wqe_err_opcode) {
		case MLX5_OPCODE_RDMA_WRITE_IMM:
		case MLX5_OPCODE_RDMA_WRITE:
			return "RDMA_WRITE";
		case MLX5_OPCODE_SEND_IMM:
		case MLX5_OPCODE_SEND:
		case MLX5_OPCODE_SEND_INVAL:
			return "SEND";
		case MLX5_OPCODE_RDMA_READ:
			return "RDMA_READ";
		case MLX5_OPCODE_ATOMIC_CS:
			return "COMPARE_SWAP";
		case MLX5_OPCODE_ATOMIC_FA:
			return "FETCH_ADD";
		case MLX5_OPCODE_ATOMIC_MASKED_CS:
			return "MASKED_COMPARE_SWAP";
		case MLX5_OPCODE_ATOMIC_MASKED_FA:
			return "MASKED_FETCH_ADD";
		case MLX5_OPCODE_MMO:
			return "GGA_DMA";
		default:
			return "";
			}
	case MLX5_CQE_RESP_ERR:
		return "RECV";
	default:
		return "";
	}
}

static void snap_dv_cqe_err(struct mlx5_cqe64 *cqe)
{
	struct mlx5_err_cqe *ecqe = (struct mlx5_err_cqe *)cqe;
	uint16_t wqe_counter;
	uint32_t qp_num = 0;
	char info[200] = {0};

	wqe_counter = be16toh(ecqe->wqe_counter);
	qp_num = be32toh(ecqe->s_wqe_opcode_qpn) & ((1<<24)-1);

	if (ecqe->syndrome == MLX5_CQE_SYNDROME_WR_FLUSH_ERR) {
		snap_debug("QP 0x%x wqe[%d] is flushed\n", qp_num, wqe_counter);
		return;
	}

	switch (ecqe->syndrome) {
	case MLX5_CQE_SYNDROME_LOCAL_LENGTH_ERR:
		snprintf(info, sizeof(info), "Local length");
		break;
	case MLX5_CQE_SYNDROME_LOCAL_QP_OP_ERR:
		snprintf(info, sizeof(info), "Local QP operation");
		break;
	case MLX5_CQE_SYNDROME_LOCAL_PROT_ERR:
		snprintf(info, sizeof(info), "Local protection");
		break;
	case MLX5_CQE_SYNDROME_WR_FLUSH_ERR:
		snprintf(info, sizeof(info), "WR flushed because QP in error state");
		break;
	case MLX5_CQE_SYNDROME_MW_BIND_ERR:
		snprintf(info, sizeof(info), "Memory window bind");
		break;
	case MLX5_CQE_SYNDROME_BAD_RESP_ERR:
		snprintf(info, sizeof(info), "Bad response");
		break;
	case MLX5_CQE_SYNDROME_LOCAL_ACCESS_ERR:
		snprintf(info, sizeof(info), "Local access");
		break;
	case MLX5_CQE_SYNDROME_REMOTE_INVAL_REQ_ERR:
		snprintf(info, sizeof(info), "Invalid request");
		break;
	case MLX5_CQE_SYNDROME_REMOTE_ACCESS_ERR:
		snprintf(info, sizeof(info), "Remote access");
		break;
	case MLX5_CQE_SYNDROME_REMOTE_OP_ERR:
		snprintf(info, sizeof(info), "Remote QP");
		break;
	case MLX5_CQE_SYNDROME_TRANSPORT_RETRY_EXC_ERR:
		snprintf(info, sizeof(info), "Transport retry count exceeded");
		break;
	case MLX5_CQE_SYNDROME_RNR_RETRY_EXC_ERR:
		snprintf(info, sizeof(info), "Receive-no-ready retry count exceeded");
		break;
	case MLX5_CQE_SYNDROME_REMOTE_ABORTED_ERR:
		snprintf(info, sizeof(info), "Remote side aborted");
		break;
	default:
		snprintf(info, sizeof(info), "Generic");
		break;
	}
	snap_error("Error on QP 0x%x wqe[%03d]: %s (synd 0x%x vend 0x%x) opcode %s\n",
		   qp_num, wqe_counter, info, ecqe->syndrome, ecqe->vendor_err_synd,
		   snap_dv_cqe_err_opcode(ecqe));
}

static inline struct snap_dma_completion *dv_dma_q_get_comp(struct snap_dma_q *q, struct mlx5_cqe64 *cqe)
{
	struct snap_dv_qp *dv_qp = &q->sw_qp.dv_qp;
	uint16_t comp_idx;
	uint32_t sq_mask;

	sq_mask = dv_qp->hw_qp.sq.wqe_cnt - 1;
	comp_idx = be16toh(cqe->wqe_counter) & sq_mask;
	q->tx_available += dv_qp->comps[comp_idx].n_outstanding;

	return dv_qp->comps[comp_idx].comp;
}

static inline int dv_dma_q_progress_tx(struct snap_dma_q *q)
{
	struct mlx5_cqe64 *cqe[SNAP_DMA_MAX_TX_COMPLETIONS];
	struct snap_dma_completion *comp[SNAP_DMA_MAX_TX_COMPLETIONS];
	struct snap_dv_qp *dv_qp = &q->sw_qp.dv_qp;
	int n, i;
	uint8_t opcode;

	n = 0;
	do {
		cqe[n] = snap_dv_poll_cq(&q->sw_qp.dv_tx_cq, SNAP_DMA_Q_TX_CQE_SIZE);
		if (!cqe[n])
			break;

		if (snap_unlikely(mlx5dv_get_cqe_opcode(cqe[n]) != MLX5_CQE_REQ))
			snap_dv_cqe_err(cqe[n]);

		comp[n] = dv_dma_q_get_comp(q, cqe[n]);
		n++;
	} while (n < SNAP_DMA_MAX_TX_COMPLETIONS);

	for (i = 0; i < n; i++) {
		opcode = mlx5dv_get_cqe_opcode(cqe[i]);

		/*
		 * opcode is good anyway, no need to check return status,
		 * but coverity doesn't recognize it
		 */
#ifdef __COVERITY__
		if (opcode != MLX5_CQE_REQ)
			continue;
#endif
		if (comp[i] && --comp[i]->count == 0)
			comp[i]->func(comp[i], opcode);
	}

	snap_dv_tx_complete(dv_qp);
	return n;
}

static inline void dv_dma_q_get_rx_comp(struct snap_dma_q *q, struct mlx5_cqe64 *cqe, struct snap_rx_completion *rx_comp)
{
	int ri;
	uint32_t rq_mask;

	/* optimize for NVMe where SQE is 64 bytes and will always
	 * be scattered
	 **/
	rq_mask = q->sw_qp.dv_qp.hw_qp.rq.wqe_cnt - 1;
	if (snap_likely(cqe->op_own & MLX5_INLINE_SCATTER_64)) {
		__builtin_prefetch(cqe - 1);
		rx_comp->data = cqe - 1;
	} else if (cqe->op_own & MLX5_INLINE_SCATTER_32) {
		rx_comp->data = cqe;
	} else {
		ri = be16toh(cqe->wqe_counter) & rq_mask;
		__builtin_prefetch(q->sw_qp.rx_buf + ri * q->rx_elem_size);
		rx_comp->data = q->sw_qp.rx_buf + ri * q->rx_elem_size;
	}
	rx_comp->byte_len = be32toh(cqe->byte_cnt);
	rx_comp->imm_data = cqe->imm_inval_pkey;
}

static inline int dv_dma_q_progress_rx(struct snap_dma_q *q)
{
	struct mlx5_cqe64 *cqe[SNAP_DMA_MAX_RX_COMPLETIONS];
	int n, i;
	int op;
	struct snap_rx_completion rx_comp[SNAP_DMA_MAX_RX_COMPLETIONS];

	n = 0;
	do {
		cqe[n] = snap_dv_poll_cq(&q->sw_qp.dv_rx_cq, SNAP_DMA_Q_RX_CQE_SIZE);
		if (!cqe[n])
			break;

		op = mlx5dv_get_cqe_opcode(cqe[n]);
		if (snap_unlikely(op != MLX5_CQE_RESP_SEND &&
				  op != MLX5_CQE_RESP_SEND_IMM)) {
			snap_dv_cqe_err(cqe[n]);
			return n;
		}


		dv_dma_q_get_rx_comp(q, cqe[n], &rx_comp[n]);
		n++;
	} while (n < SNAP_DMA_MAX_RX_COMPLETIONS);

	snap_memory_cpu_load_fence();

	for (i = 0; i < n; i++)
		q->rx_cb(q, rx_comp[i].data, rx_comp[i].byte_len, rx_comp[i].imm_data);

	if (n == 0)
		return 0;

	q->sw_qp.dv_qp.hw_qp.rq.ci += n;
	snap_dv_ring_rx_db(&q->sw_qp.dv_qp);
	return n;
}

static inline int dv_dma_q_poll_rx(struct snap_dma_q *q,
		struct snap_rx_completion *rx_completions, int max_completions)
{
	struct mlx5_cqe64 *cqe;
	int n;
	int op;

	n = 0;
	do {
		cqe = snap_dv_poll_cq(&q->sw_qp.dv_rx_cq, SNAP_DMA_Q_RX_CQE_SIZE);
		if (!cqe)
			break;

		op = mlx5dv_get_cqe_opcode(cqe);
		if (snap_unlikely(op != MLX5_CQE_RESP_SEND &&
				  op != MLX5_CQE_RESP_SEND_IMM)) {
			snap_dv_cqe_err(cqe);
			return n;
		}

		dv_dma_q_get_rx_comp(q, cqe, &rx_completions[n]);
		n++;
	} while (n < max_completions);

	if (n == 0)
		return 0;

	q->sw_qp.dv_qp.hw_qp.rq.ci += n;
	snap_dv_ring_rx_db(&q->sw_qp.dv_qp);
	return n;
}

static inline int dv_dma_q_poll_tx(struct snap_dma_q *q, struct snap_dma_completion **comp, int max_completions)
{
	struct mlx5_cqe64 *cqe;
	int n;
	struct snap_dma_completion *dma_comp;
	struct snap_dv_qp *dv_qp = &q->sw_qp.dv_qp;

	snap_dv_tx_complete(dv_qp);

	n = 0;
	do {
		cqe = snap_dv_poll_cq(&q->sw_qp.dv_tx_cq, SNAP_DMA_Q_TX_CQE_SIZE);
		if (!cqe)
			break;
		if (snap_unlikely(mlx5dv_get_cqe_opcode(cqe) != MLX5_CQE_REQ))
			snap_dv_cqe_err(cqe);

		dma_comp = dv_dma_q_get_comp(q, cqe);
		if (dma_comp && --dma_comp->count == 0) {
			comp[n] = dma_comp;
			n++;
		}

	} while (n < SNAP_DMA_MAX_TX_COMPLETIONS);

	return n;
}

static int dv_dma_q_arm(struct snap_dma_q *q)
{
	/* ring doorbells, disable batch mode.
	 * TODO: better interaction of batch and event modes
	 */
	snap_dv_tx_complete(&q->sw_qp.dv_qp);
	q->sw_qp.dv_qp.db_flag = SNAP_DB_RING_IMM;
	snap_dv_arm_cq(&q->sw_qp.dv_tx_cq);
	snap_dv_arm_cq(&q->sw_qp.dv_rx_cq);
	return 0;
}

static int dv_dma_q_flush(struct snap_dma_q *q)
{
	int n, n_out, n_bb;
	int tx_available;
	struct snap_dma_completion comp;

	n = 0;
	/* in case we have tx moderation we need at least one
	 * available to be able to send a flush command
	 */
	while (!qp_can_tx(q, 1))
		n += dv_dma_q_progress_tx(q);

	/* flush all outstanding ops by issuing a zero length inline rdma write */
	n_out = q->sw_qp.dv_qp.n_outstanding;
	if (n_out) {
		comp.count = 2;
		do_dv_xfer_inline(q, NULL, 0, MLX5_OPCODE_RDMA_WRITE, 0, 0, &comp, &n_bb);
		q->tx_available -= n_bb;
		n--;
	}

	tx_available = q->sw_qp.dv_qp.hw_qp.sq.wqe_cnt;
	while (q->tx_available < tx_available)
		n += dv_dma_q_progress_tx(q);

	return n_out + n;
}

static int dv_dma_q_flush_nowait(struct snap_dma_q *q, struct snap_dma_completion *comp, int *n_bb)
{
	return do_dv_xfer_inline(q, NULL, 0, MLX5_OPCODE_RDMA_WRITE, 0, 0, comp, n_bb);
}

static bool dv_dma_q_empty(struct snap_dma_q *q)
{
	return q->tx_available == q->sw_qp.dv_qp.hw_qp.sq.wqe_cnt;
}

struct snap_dma_q_ops dv_ops = {
	.write           = dv_dma_q_write,
	.writev2v        = dv_dma_q_writev2v,
	.writec          = dv_dma_q_writec,
	.write_short     = dv_dma_q_write_short,
	.read            = dv_dma_q_read,
	.readv2v         = dv_dma_q_readv2v,
	.readc           = dv_dma_q_readc,
	.send_completion = dv_dma_q_send_completion,
	.send            = dv_dma_q_send,
	.progress_tx     = dv_dma_q_progress_tx,
	.progress_rx     = dv_dma_q_progress_rx,
	.poll_rx         = dv_dma_q_poll_rx,
	.poll_tx         = dv_dma_q_poll_tx,
	.arm             = dv_dma_q_arm,
	.flush           = dv_dma_q_flush,
	.flush_nowait    = dv_dma_q_flush_nowait,
	.empty		 = dv_dma_q_empty,
};

/* GGA */
__attribute__((unused)) static void dump_gga_wqe(int op, uint32_t *wqe)
{
	int i;

	printf("%s op %d wqe:\n", __func__, op);

	for (i = 0; i < 16; i += 4)
		printf("%08X %08X %08X %08X\n",
			be32toh(wqe[i]), be32toh(wqe[i + 1]),
			be32toh(wqe[i + 2]), be32toh(wqe[i + 3]));
}

static inline int do_gga_dma_xfer(struct snap_dma_q *q, uint64_t saddr, size_t len,
			      uint32_t s_lkey, uint64_t daddr, uint32_t d_lkey,
			      struct snap_dma_completion *comp, bool use_fence)
{
	struct snap_dv_qp *dv_qp = &q->sw_qp.dv_qp;
	struct mlx5_dma_wqe *gga_wqe;
	/* struct mlx5_wqe_ctrl_seg changed to packed(4),
	 * and struct mlx5_dma_wqe is use default packed attribute, which is 8.
	 * in order to fix the compile issue on UB OS, make `ctrl` to void*,
	 * and convert it to struct mlx5_wqe_ctrl_seg * when it is needed.
	 */
	void *ctrl;
	uint16_t comp_idx;
	uint8_t fm_ce_se = 0;

	comp_idx = dv_qp->hw_qp.sq.pi & (dv_qp->hw_qp.sq.wqe_cnt - 1);

	fm_ce_se |= snap_dv_get_cq_update(dv_qp, comp);
	if (use_fence)
		fm_ce_se |= MLX5_WQE_CTRL_INITIATOR_SMALL_FENCE;

	ctrl = snap_dv_get_wqe_bb(dv_qp);
	snap_set_ctrl_seg((struct mlx5_wqe_ctrl_seg *)ctrl, dv_qp->hw_qp.sq.pi,
			    MLX5_OPCODE_MMO, MLX5_OPC_MOD_MMO_DMA,
			    dv_qp->hw_qp.qp_num, fm_ce_se,
			    4, 0, 0);

	gga_wqe = (struct mlx5_dma_wqe *)ctrl;
	gga_wqe->gga_ctrl2 = 0;
	gga_wqe->opaque_lkey = dv_qp->opaque_lkey;
	gga_wqe->opaque_vaddr = htobe64((uint64_t)&dv_qp->opaque_buf[comp_idx]);

	mlx5dv_set_data_seg(&gga_wqe->gather, len, s_lkey, saddr);
	mlx5dv_set_data_seg(&gga_wqe->scatter, len, d_lkey, daddr);

	snap_dv_wqe_submit(dv_qp, ctrl);

	snap_dv_set_comp(dv_qp, comp_idx, comp, fm_ce_se, 1);
	return 0;
}

static int gga_dma_q_write(struct snap_dma_q *q, void *src_buf, size_t len,
			  uint32_t lkey, uint64_t dstaddr, uint32_t rmkey,
			  struct snap_dma_completion *comp)
{
	return do_gga_dma_xfer(q, (uint64_t)src_buf, len, lkey,
			dstaddr, rmkey, comp, false);
}

static int gga_dma_q_read(struct snap_dma_q *q, void *dst_buf, size_t len,
			 uint32_t lkey, uint64_t srcaddr, uint32_t rmkey,
			 struct snap_dma_completion *comp)
{
	return do_gga_dma_xfer(q, srcaddr, len, rmkey,
			(uint64_t)dst_buf, lkey, comp, false);
}

static int gga_dma_q_writev2v(struct snap_dma_q *q,
				struct snap_dma_q_io_attr *io_attr,
				struct snap_dma_completion *comp, int *n_bb)
{
	int ret;
	struct snap_dma_xfer_ctx dx_ctx = {0};

	ret = snap_prepare_dma_xfer_ctx(q, io_attr, comp, n_bb, &dx_ctx);
	if (ret)
		return ret;

	return do_gga_dma_xfer(q, (uint64_t)dx_ctx.lbuf, dx_ctx.len,
			dx_ctx.lkey, dx_ctx.raddr, dx_ctx.rkey,
			dx_ctx.comp, dx_ctx.use_fence);
}

static int gga_dma_q_writec(struct snap_dma_q *q,
				struct snap_dma_q_io_attr *io_attr,
				struct snap_dma_completion *comp, int *n_bb)
{
	return -ENOTSUP;
}

static int gga_dma_q_readv2v(struct snap_dma_q *q,
				struct snap_dma_q_io_attr *io_attr,
				struct snap_dma_completion *comp, int *n_bb)
{
	int ret;
	struct snap_dma_xfer_ctx dx_ctx = {0};

	ret = snap_prepare_dma_xfer_ctx(q, io_attr, comp, n_bb, &dx_ctx);
	if (ret)
		return ret;

	return do_gga_dma_xfer(q, dx_ctx.raddr, dx_ctx.len,
			dx_ctx.rkey, (uint64_t)dx_ctx.lbuf, dx_ctx.lkey,
			dx_ctx.comp, dx_ctx.use_fence);
}

static int gga_dma_q_readc(struct snap_dma_q *q,
				struct snap_dma_q_io_attr *io_attr,
				struct snap_dma_completion *comp, int *n_bb)
{
	return -ENOTSUP;
}

struct snap_dma_q_ops gga_ops = {
	.write           = gga_dma_q_write,
	.writev2v        = gga_dma_q_writev2v,
	.writec          = gga_dma_q_writec,
	.write_short     = dv_dma_q_write_short,
	.read            = gga_dma_q_read,
	.readv2v         = gga_dma_q_readv2v,
	.readc           = gga_dma_q_readc,
	.send_completion = dv_dma_q_send_completion,
	.send            = dv_dma_q_send,
	.progress_tx     = dv_dma_q_progress_tx,
	.progress_rx     = dv_dma_q_progress_rx,
	.poll_rx         = dv_dma_q_poll_rx,
	.poll_tx         = dv_dma_q_poll_tx,
	.arm             = dv_dma_q_arm,
	.flush           = dv_dma_q_flush,
	.flush_nowait    = dv_dma_q_flush_nowait,
	.empty		 = dv_dma_q_empty,
};
