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

#ifndef SNAP_MR_H
#define SNAP_MR_H

#include <infiniband/verbs.h>

/* do a forward declaration to avoid include snap.h */
struct snap_device;

struct snap_cross_mkey {
	struct mlx5dv_devx_obj *devx_obj;
	uint32_t mkey;
};

struct mlx5_klm {
	uint32_t byte_count;
	uint32_t mkey;
	uint64_t address;
};

struct mlx5_devx_mkey_attr {
	uint64_t addr;
	uint64_t size;
	uint32_t log_entity_size;
	uint32_t relaxed_ordering_write:1;
	uint32_t relaxed_ordering_read:1;
	struct mlx5_klm *klm_array;
	int klm_num;
};

struct snap_indirect_mkey {
	struct mlx5dv_devx_obj *devx_obj;
	uint32_t mkey;
	uint64_t addr;
};

struct ibv_mr *snap_reg_mr(struct ibv_pd *pd, void *addr, size_t length);

struct snap_cross_mkey *snap_create_cross_mkey(struct ibv_pd *pd,
					       struct snap_device *target_sdev);
int snap_destroy_cross_mkey(struct snap_cross_mkey *mkey);

struct snap_indirect_mkey *
snap_create_indirect_mkey(struct ibv_pd *pd,
			  struct mlx5_devx_mkey_attr *attr);
int
snap_destroy_indirect_mkey(struct snap_indirect_mkey *mkey);

#endif
