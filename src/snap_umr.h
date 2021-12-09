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

#ifndef SNAP_UMR_H
#define SNAP_UMR_H

#include "snap_dma.h"
#include "snap_mr.h"

enum {
	SNAP_UMR_MKEY_MODIFY_ATTACH_MTT = 0x1 << 0,
};

struct snap_post_umr_attr {
	uint32_t	purpose;

	/* for attach inline mtt purpose */
	int				klm_entries;
	struct mlx5_klm *klm_mtt;
	struct snap_indirect_mkey *klm_mkey;
};

int snap_umr_post_wqe(struct snap_dma_q *q, struct snap_post_umr_attr *attr,
		struct snap_dma_completion *comp, int *n_bb);

#endif /* SNAP_UMR_H */
