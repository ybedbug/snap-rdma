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
#include "snap_umr.h"

static inline void set_umr_inline_klm_seg(union mlx5_wqe_umr_inline_seg *klm,
					struct mlx5_klm *mtt)
{
	klm->klm.byte_count = htobe32(mtt->byte_count);
	klm->klm.mkey = htobe32(mtt->mkey);
	klm->klm.address = htobe64(mtt->address);
}

static inline void set_umr_mkey_seg_mtt(struct mlx5_wqe_mkey_context_seg *mkey,
					struct snap_post_umr_attr *attr)
{
	int i;
	uint64_t len = 0;

	mkey->free = 0;
	/* modify start_addr when it is a IOV type IO */
	if (attr->purpose == SNAP_UMR_MKEY_MODIFY_ATTACH_MTT)
		mkey->start_addr = htobe64(attr->klm_mtt[0].address);

	for (i = 0; i < attr->klm_entries; i++)
		len += attr->klm_mtt[i].byte_count;

	mkey->len = htobe64(len);
}

static inline void set_umr_ctrl_seg_mtt(struct mlx5_wqe_umr_ctrl_seg *ctrl,
					struct snap_post_umr_attr *attr)
{
	uint64_t mkey_mask;

	ctrl->flags |= MLX5_WQE_UMR_CTRL_FLAG_INLINE;

	ctrl->klm_octowords = htobe16(SNAP_ALIGN_CEIL(attr->klm_entries, 4));

	/*
	 * Going to modify three properties of KLM mkey:
	 *  1. 'free' field: change this mkey from in free to in use
	 *  2. 'len' field: to include the total bytes in iovec
	 *  3. 'start_addr' field: use the address of first element as
	 *       the start_addr of this mkey. (ONLY for IOV type IO)
	 **/
	mkey_mask = MLX5_WQE_UMR_CTRL_MKEY_MASK_FREE
				| MLX5_WQE_UMR_CTRL_MKEY_MASK_LEN;
	if (attr->purpose == SNAP_UMR_MKEY_MODIFY_ATTACH_MTT)
		mkey_mask |= MLX5_WQE_UMR_CTRL_MKEY_MASK_START_ADDR;

	ctrl->mkey_mask |= htobe64(mkey_mask);
}

static inline void set_umr_crypto_bsf_seg(struct snap_crypto_bsf_seg *bsf,
			struct snap_post_umr_attr *attr)
{
	/* explicitly set rsvd0/rsvd1/rsvd2/rsvd3 to 0 */
	*((uint64_t *)bsf) = 0;
	*((uint64_t *)bsf + 1) = 0;
	*((uint64_t *)bsf + 4) = 0;
	*((uint64_t *)bsf + 6) = 0;
	*((uint64_t *)bsf + 7) = 0;

	bsf->size_type = (SNAP_CRYPTO_BSF_SIZE_64B << 6)
					| SNAP_CRYPTO_BSF_P_TYPE_CRYPTO;
	bsf->enc_order = attr->encryption_order;
	bsf->enc_standard = attr->encryption_standard;
	bsf->raw_data_size = htobe32(attr->raw_data_size);
	bsf->crypto_block_size_pointer = attr->crypto_block_size_pointer;
	bsf->dek_pointer = htobe32(attr->dek_pointer & 0x00FFFFFF);
	memcpy(bsf->xts_initial_tweak, attr->xts_initial_tweak,
		SNAP_CRYPTO_XTS_INITIAL_TWEAK_SIZE);
	memcpy(bsf->keytag, attr->keytag, SNAP_CRYPTO_KEYTAG_SIZE);
}

static inline void set_umr_mkey_seg_crypto_bsf(
					struct mlx5_wqe_mkey_context_seg *mkey,
					int bsf_size)
{
	mkey->bsf_octword_size = htobe32(SNAP_ALIGN_CEIL(round_up(bsf_size, 16), 4));
}

static inline void set_umr_ctrl_seg_crypto_bsf(
					struct mlx5_wqe_umr_ctrl_seg *ctrl,
					int bsf_size)
{
	ctrl->flags |= MLX5_WQE_UMR_CTRL_FLAG_INLINE;

	ctrl->bsf_octowords = htobe16(SNAP_ALIGN_CEIL(round_up(bsf_size, 16), 4));

	/*
	 * Going to modify one propertie of cross mkey:
	 *  1. 'bsf_octoword_size' field
	 **/
	ctrl->mkey_mask |= htobe64(SNAP_WQE_UMR_CTRL_MKEY_MASK_BSF_OCTOWORD_SIZE);
}

static void snap_build_inline_crypto_bsf(uint32_t *to_end,
				struct snap_crypto_bsf_seg *bsf,
				struct snap_dv_qp *dv_qp,
				struct snap_post_umr_attr *attr)
{
	/* build inline crypro bsf */
	if (*to_end == 0) { /* wqe buffer wap around */
		bsf = (struct snap_crypto_bsf_seg *) (dv_qp->hw_qp.sq.addr);
		*to_end = dv_qp->hw_qp.sq.wqe_cnt * MLX5_SEND_WQE_BB;
	}

	set_umr_crypto_bsf_seg(bsf, attr);
	*to_end -= MLX5_SEND_WQE_BB;
}

static void *snap_build_inline_mtt(uint32_t *to_end,
				union mlx5_wqe_umr_inline_seg *klm,
				struct snap_dv_qp *dv_qp,
				struct snap_post_umr_attr *attr)
{
	int i, translation_size;

	translation_size = SNAP_ALIGN_CEIL(attr->klm_entries, 4);

	/* build inline mtt entires */
	if (*to_end == 0) { /* wqe buffer warp around */
		klm = (union mlx5_wqe_umr_inline_seg *) (dv_qp->hw_qp.sq.addr);
		*to_end = dv_qp->hw_qp.sq.wqe_cnt * MLX5_SEND_WQE_BB;
	}

	for (i = 0; i < attr->klm_entries; i++) {
		set_umr_inline_klm_seg(klm, &attr->klm_mtt[i]);
		/* sizeof(*klm) * 4 == MLX5_SEND_WQE_BB */
		*to_end -= sizeof(union mlx5_wqe_umr_inline_seg);
		if (*to_end == 0) { /* wqe buffer warp around */
			klm = (union mlx5_wqe_umr_inline_seg *) (dv_qp->hw_qp.sq.addr);
			*to_end = dv_qp->hw_qp.sq.wqe_cnt * MLX5_SEND_WQE_BB;
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
		*to_end -= sizeof(union mlx5_wqe_umr_inline_seg);
		klm = klm + 1;
	}

	attr->klm_mkey->addr = attr->klm_mtt[0].address;

	return (void *)klm;
}

static void snap_set_umr_mkey_seg(struct mlx5_wqe_mkey_context_seg *mkey,
			struct snap_post_umr_attr *attr)
{
	/* explicitly set all struct mlx5_wqe_mkey_context_seg to 0 */
	*((uint64_t *)mkey) = 0;
	*((uint64_t *)mkey + 1) = 0;
	*((uint64_t *)mkey + 2) = 0;
	*((uint64_t *)mkey + 3) = 0;
	*((uint64_t *)mkey + 4) = 0;
	*((uint64_t *)mkey + 5) = 0;
	*((uint64_t *)mkey + 6) = 0;
	*((uint64_t *)mkey + 7) = 0;

	if (attr->purpose & SNAP_UMR_MKEY_MODIFY_ATTACH_MTT)
		set_umr_mkey_seg_mtt(mkey, attr);

	if (attr->purpose & SNAP_UMR_MKEY_MODIFY_ATTACH_CRYPTO_BSF)
		set_umr_mkey_seg_crypto_bsf(mkey, sizeof(struct snap_crypto_bsf_seg));
}

static void snap_set_umr_ctrl_seg(struct mlx5_wqe_umr_ctrl_seg *umr_ctrl,
			struct snap_post_umr_attr *attr)
{
	/* explicitly set rsvd0 and rsvd1 from struct mlx5_wqe_umr_ctrl_seg to 0,
	 * otherwise post umr wqe will fail if reuse those WQE BB with dirty data.
	 **/
	*(uint64_t *)umr_ctrl = 0;
	*((uint64_t *)umr_ctrl + 1) = 0;
	*((uint64_t *)umr_ctrl + 2) = 0;
	*((uint64_t *)umr_ctrl + 3) = 0;
	*((uint64_t *)umr_ctrl + 4) = 0;
	*((uint64_t *)umr_ctrl + 5) = 0;

	if (attr->purpose & SNAP_UMR_MKEY_MODIFY_ATTACH_MTT)
		set_umr_ctrl_seg_mtt(umr_ctrl, attr);

	if (attr->purpose & SNAP_UMR_MKEY_MODIFY_ATTACH_CRYPTO_BSF)
		set_umr_ctrl_seg_crypto_bsf(umr_ctrl, sizeof(struct snap_crypto_bsf_seg));
}

static int snap_build_umr_wqe(struct snap_dma_q *q,
			struct mlx5_wqe_ctrl_seg *ctrl, uint8_t fm_ce_se,
			uint32_t to_end, struct snap_post_umr_attr *attr,
			uint32_t *umr_wqe_n_bb, int n_bb)
{
	struct snap_dv_qp *dv_qp;
	struct mlx5_wqe_ctrl_seg *gen_ctrl;
	struct mlx5_wqe_umr_ctrl_seg *umr_ctrl;
	struct mlx5_wqe_mkey_context_seg *mkey;
	union mlx5_wqe_umr_inline_seg *klm;
	struct snap_crypto_bsf_seg *bsf;
	void *inline_mtt_end;
	uint32_t wqe_size, translation_size;
	uint32_t inline_klm_size = 0;

	if ((attr->purpose & SNAP_UMR_MKEY_MODIFY_ATTACH_MTT)
		&& (attr->klm_mtt == NULL || attr->klm_entries == 0)) {
		snap_error("Provided MTT is not valid\n");
		return -EINVAL;
	}

	dv_qp = &q->sw_qp.dv_qp;

	/*
	 * UMR WQE LAYOUT:
	 * -----------------------------------------------------------------------
	 * | gen_ctrl | umr_ctrl | mkey_ctx | inline klm mtt | inline crypto bsf |
	 * -----------------------------------------------------------------------
	 *   16bytes    48bytes    64bytes   num_mtt*16 bytes      64 bytes
	 *
	 * Note: size of inline klm mtt should be aligned to 64 bytes.
	 */
	wqe_size = sizeof(*gen_ctrl) + sizeof(*umr_ctrl) + sizeof(*mkey);
	if (attr->purpose & SNAP_UMR_MKEY_MODIFY_ATTACH_MTT) {
		translation_size = SNAP_ALIGN_CEIL(attr->klm_entries, 4);
		inline_klm_size = translation_size * sizeof(union mlx5_wqe_umr_inline_seg);
		wqe_size += inline_klm_size;
	}
	if (attr->purpose & SNAP_UMR_MKEY_MODIFY_ATTACH_CRYPTO_BSF)
		wqe_size += sizeof(struct snap_crypto_bsf_seg);

	*umr_wqe_n_bb = round_up(wqe_size, MLX5_SEND_WQE_BB);

	/*
	 * umr wqe only do the modification to mkey,
	 * and there will be one RDMA/GGA-READ/WEIR wqe
	 * followed right after to use this modified mkey.
	 *
	 * A read or write call consider process succeed
	 * only when both umr wqe and RDMA/GGA-READ/WEIR
	 * wqe post succeed.
	 *
	 * In order to avoid post UMR WQE succeed but lack
	 * of tx available resource to post followed DMA WQE
	 * case, use umr_wqe_n_bb + 1 to do the can_tx check.
	 */
	if (snap_unlikely(!qp_can_tx(q, *umr_wqe_n_bb + 1 + n_bb))) {
		snap_error("Lack of tx_available resource!\n");
		return -EAGAIN;
	}

	/*
	 * sizeof(gen_ctrl) + sizeof(umr_ctrl) == MLX5_SEND_WQE_BB,
	 * so do not need to worry about wqe buffer warp around.
	 *
	 * build genenal ctrl segment
	 */
	gen_ctrl = ctrl;
	snap_set_ctrl_seg(gen_ctrl, dv_qp->hw_qp.sq.pi, MLX5_OPCODE_UMR, 0,
				dv_qp->hw_qp.qp_num, fm_ce_se,
				round_up(wqe_size, 16), 0,
				htobe32(attr->klm_mkey->mkey));

	/* build umr ctrl segment */
	umr_ctrl = (struct mlx5_wqe_umr_ctrl_seg *)(gen_ctrl + 1);
	snap_set_umr_ctrl_seg(umr_ctrl, attr);
	to_end -= MLX5_SEND_WQE_BB;

	/* build mkey context segment */
	if (to_end == 0) { /* wqe buffer wap around */
		mkey = (struct mlx5_wqe_mkey_context_seg *)(dv_qp->hw_qp.sq.addr);
		to_end = dv_qp->hw_qp.sq.wqe_cnt * MLX5_SEND_WQE_BB;
	} else {
		mkey = (struct mlx5_wqe_mkey_context_seg *)(umr_ctrl + 1);
	}
	snap_set_umr_mkey_seg(mkey, attr);
	to_end -= MLX5_SEND_WQE_BB;

	inline_mtt_end = NULL;
	if (attr->purpose & SNAP_UMR_MKEY_MODIFY_ATTACH_MTT) {
		klm = (union mlx5_wqe_umr_inline_seg *)(mkey + 1);
		inline_mtt_end = snap_build_inline_mtt(&to_end, klm, dv_qp, attr);
	}

	if (attr->purpose & SNAP_UMR_MKEY_MODIFY_ATTACH_CRYPTO_BSF) {
		if (inline_mtt_end)
			bsf = (struct snap_crypto_bsf_seg *)inline_mtt_end;
		else
			bsf = (struct snap_crypto_bsf_seg *)(mkey + 1);
		snap_build_inline_crypto_bsf(&to_end, bsf, dv_qp, attr);
	}

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

	ret = snap_build_umr_wqe(q, ctrl, fm_ce_se, to_end, attr, &umr_wqe_n_bb, *n_bb);
	if (ret) {
		snap_error("Failed to build umr wqe for purpose:%s\n",
			attr->purpose == SNAP_UMR_MKEY_MODIFY_ATTACH_MTT ? "attach_mtt" : "attach_bsf");
		return ret;
	}

	*n_bb += umr_wqe_n_bb;

	dv_qp->hw_qp.sq.pi += (umr_wqe_n_bb - 1);

	snap_dv_wqe_submit(dv_qp, ctrl);

	snap_dv_set_comp(dv_qp, pi, comp, fm_ce_se, umr_wqe_n_bb);

	return 0;
}
