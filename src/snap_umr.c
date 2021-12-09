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

#include "snap_umr.h"
#include "snap.h"
#include "snap_dma_internal.h"
#include "snap_mr.h"

static inline void set_umr_inline_klm_seg(union mlx5_wqe_umr_inline_seg *klm,
					struct mlx5_klm *mtt)
{
	klm->klm.byte_count = htobe32(mtt->byte_count);
	klm->klm.mkey = htobe32(mtt->mkey);
	klm->klm.address = htobe64(mtt->address);
}

static inline void set_umr_mkey_seg_mtt(struct mlx5_wqe_mkey_context_seg *mkey,
					struct mlx5_klm *klm_mtt, int klm_entries)
{
	int i;
	uint64_t len = 0;

	mkey->free = 0;
	mkey->start_addr = htobe64(klm_mtt[0].address);

	for (i = 0; i < klm_entries; i++)
		len += klm_mtt[i].byte_count;

	mkey->len = htobe64(len);
}

static inline void set_umr_control_seg_mtt(struct mlx5_wqe_umr_ctrl_seg *ctrl,
					int klm_entries)
{
	/* explicitly set rsvd0 and rsvd1 from struct mlx5_wqe_umr_ctrl_seg to 0,
	 * otherwise post umr wqe will fail if reuse those WQE BB with dirty data.
	 **/
	*(uint32_t *)ctrl = 0;
	*((uint64_t *)ctrl + 2) = 0;
	*((uint64_t *)ctrl + 3) = 0;
	*((uint64_t *)ctrl + 4) = 0;
	*((uint64_t *)ctrl + 5) = 0;

	ctrl->flags = MLX5_WQE_UMR_CTRL_FLAG_INLINE |
				MLX5_WQE_UMR_CTRL_FLAG_TRNSLATION_OFFSET;

	/* if use a non-zero offset value, should use htobe16(offset) */
	ctrl->translation_offset = 0;

	ctrl->klm_octowords = htobe16(SNAP_ALIGN_CEIL(klm_entries, 4));

	/*
	 * Going to modify three properties of KLM mkey:
	 *  1. 'free' field: change this mkey from in free to in use
	 *  2. 'len' field: to include the total bytes in iovec
	 *  3. 'start_addr' field: use the address of first element as
	 *       the start_addr of this mkey
	 **/
	ctrl->mkey_mask = htobe64(MLX5_WQE_UMR_CTRL_MKEY_MASK_FREE |
				MLX5_WQE_UMR_CTRL_MKEY_MASK_LEN |
				MLX5_WQE_UMR_CTRL_MKEY_MASK_START_ADDR);
}

static int build_inline_mtt_umr_wqe(struct snap_dma_q *q,
			struct mlx5_wqe_ctrl_seg *ctrl, uint8_t fm_ce_se,
			uint32_t to_end, struct snap_post_umr_attr *attr,
			uint32_t *umr_wqe_n_bb)
{
	struct snap_dv_qp *dv_qp;
	struct mlx5_wqe_ctrl_seg *gen_ctrl;
	struct mlx5_wqe_umr_ctrl_seg *umr_ctrl;
	struct mlx5_wqe_mkey_context_seg *mkey;
	union mlx5_wqe_umr_inline_seg *klm;
	uint32_t wqe_size, inline_klm_size;
	uint32_t i, translation_size;

	if (attr->klm_mtt == NULL || attr->klm_entries == 0)
		return -EINVAL;

	dv_qp = &q->sw_qp.dv_qp;

	/*
	 * UMR WQE LAYOUT:
	 * -------------------------------------------------------
	 * | gen_ctrl | umr_ctrl | mkey_ctx |   inline klm mtt   |
	 * -------------------------------------------------------
	 *   16bytes    48bytes    64bytes    num_mtt * 16 bytes
	 *
	 * Note: size of inline klm mtt should be aligned to 64 bytes.
	 */
	translation_size = SNAP_ALIGN_CEIL(attr->klm_entries, 4);
	inline_klm_size = translation_size * sizeof(*klm);
	wqe_size = sizeof(*gen_ctrl) + sizeof(*umr_ctrl) +
		sizeof(*mkey) + inline_klm_size;

	*umr_wqe_n_bb = round_up(wqe_size, MLX5_SEND_WQE_BB);

	/*
	 * umr wqe only do the modification to klm-mkey,
	 * and there will be one RDMA/GGA-READ/WEIR wqe
	 * followed right after to use this modified klm-key.
	 *
	 * A .readv()/.writev() consider process succeed
	 * only when both umr wqe and RDMA/GGA-READ/WEIR
	 * wqes post succeed.
	 *
	 * In order to avoid post UMR WQE succeed but lack
	 * of tx available resource to post followed DMA WQE
	 * case, use umr_wqe_n_bb + 1 to do the can_tx check.
	 */
	if (snap_unlikely(!qp_can_tx(q, *umr_wqe_n_bb + 1)))
		return -EAGAIN;

	/* sizeof(gen_ctrl) + sizeof(umr_ctrl) == MLX5_SEND_WQE_BB,
	 * so do not need to worry about wqe buffer warp around.
	 * build genenal ctrl segment
	 **/
	gen_ctrl = ctrl;
	snap_set_ctrl_seg(gen_ctrl, dv_qp->hw_qp.sq.pi, MLX5_OPCODE_UMR, 0,
				dv_qp->hw_qp.qp_num, fm_ce_se,
				round_up(wqe_size, 16), 0,
				htobe32(attr->klm_mkey->mkey));

	/* build umr ctrl segment */
	umr_ctrl = (struct mlx5_wqe_umr_ctrl_seg *)(gen_ctrl + 1);
	set_umr_control_seg_mtt(umr_ctrl, attr->klm_entries);

	/* build mkey context segment */
	to_end -= MLX5_SEND_WQE_BB;
	if (to_end == 0) { /* wqe buffer wap around */
		mkey = (struct mlx5_wqe_mkey_context_seg *)(dv_qp->hw_qp.sq.addr);
		to_end = dv_qp->hw_qp.sq.wqe_cnt * MLX5_SEND_WQE_BB;
	} else {
		mkey = (struct mlx5_wqe_mkey_context_seg *)(umr_ctrl + 1);
	}
	set_umr_mkey_seg_mtt(mkey, attr->klm_mtt, attr->klm_entries);

	/* build inline mtt entires */
	to_end -= MLX5_SEND_WQE_BB;
	if (to_end == 0) { /* wqe buffer wap around */
		klm = (union mlx5_wqe_umr_inline_seg *) (dv_qp->hw_qp.sq.addr);
		to_end = dv_qp->hw_qp.sq.wqe_cnt * MLX5_SEND_WQE_BB;
	} else {
		klm = (union mlx5_wqe_umr_inline_seg *)(mkey + 1);
	}

	for (i = 0; i < attr->klm_entries; i++) {
		set_umr_inline_klm_seg(klm, &attr->klm_mtt[i]);
		/* sizeof(*klm) * 4 == MLX5_SEND_WQE_BB */
		to_end -= sizeof(union mlx5_wqe_umr_inline_seg);
		if (to_end == 0) { /* wqe buffer wap around */
			klm = (union mlx5_wqe_umr_inline_seg *) (dv_qp->hw_qp.sq.addr);
			to_end = dv_qp->hw_qp.sq.wqe_cnt * MLX5_SEND_WQE_BB;
		} else {
			klm = klm + 1;
		}
	}

	/* fill PAD if existing */
	/* PAD entries is to make whole mtt aligned to 64B(MLX5_SEND_WQE_BB),
	 * So it will not happen warp around during fill PAD entries.
	 **/
	for (; i < translation_size; i++) {
		memset(klm, 0, sizeof(*klm));
		klm = klm + 1;
	}

	attr->klm_mkey->addr = attr->klm_mtt[0].address;

	return 0;
}

int snap_umr_post_wqe(struct snap_dma_q *q, struct snap_post_umr_attr *attr,
			struct snap_dma_completion *comp, int *n_bb)
{
	int ret;
	uint8_t fm_ce_se = 0;
	uint32_t pi, to_end, umr_wqe_n_bb;
	struct snap_dv_qp *dv_qp;
	struct mlx5_wqe_ctrl_seg *ctrl;

	if (q->sw_qp.mode == SNAP_DMA_Q_MODE_VERBS)
		return -ENOTSUP;

	dv_qp = &q->sw_qp.dv_qp;
	fm_ce_se |= snap_dv_get_cq_update(dv_qp, comp);
	ctrl = (struct mlx5_wqe_ctrl_seg *)snap_dv_get_wqe_bb(dv_qp);

	pi = dv_qp->hw_qp.sq.pi & (dv_qp->hw_qp.sq.wqe_cnt - 1);
	to_end = (dv_qp->hw_qp.sq.wqe_cnt - pi) * MLX5_SEND_WQE_BB;

	switch (attr->purpose) {
	case SNAP_UMR_MKEY_MODIFY_ATTACH_MTT:
		ret = build_inline_mtt_umr_wqe(q, ctrl,
					fm_ce_se, to_end, attr, &umr_wqe_n_bb);
		break;

	default:
		snap_error("Not supported post umr wqe operation\n");
		return -EINVAL;
	}

	if (ret) {
		snap_error("Failed to build umr wqe for purpose:%d\n", attr->purpose);
		return ret;
	}

	*n_bb = umr_wqe_n_bb + 1;

	dv_qp->hw_qp.sq.pi += (umr_wqe_n_bb - 1);

	snap_dv_wqe_submit(dv_qp, ctrl);

	snap_dv_set_comp(dv_qp, pi, comp, fm_ce_se, umr_wqe_n_bb);

	return 0;
}
