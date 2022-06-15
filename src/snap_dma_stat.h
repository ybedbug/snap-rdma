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

#ifndef SNAP_DMA_STAT_H
#define SNAP_DMA_STAT_H

#include <stdint.h>

struct snap_dv_qp_db_counter {
	// total doorbels
	uint64_t total_dbs;
	// total processed completions
	uint64_t total_completed;
};

struct snap_dv_qp_stat {
	struct snap_dv_qp_db_counter rx;
	struct snap_dv_qp_db_counter tx;
};

#endif

