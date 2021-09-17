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

int snap_dma_q_post_umr_wqe(struct snap_dma_q *q, struct mlx5_klm *klm_mtt,
			int klm_entries, struct snap_indirect_mkey *klm_mkey,
			struct snap_dma_completion *comp, int *n_bb);

#endif /* SNAP_UMR_H */
