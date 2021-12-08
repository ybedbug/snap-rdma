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

#include <stdbool.h>
#include <sys/queue.h>
#if !defined(__DPA)
#include <infiniband/verbs.h>
#endif

/* do a forward declaration to avoid include snap.h */
struct snap_device;

#define SNAP_VIRTIO_UMEM_ALIGN 4096

struct snap_umem {
	void *buf;
	int size;
	struct mlx5dv_devx_umem *devx_umem;
};

struct snap_cross_mkey {
	struct mlx5dv_devx_obj *devx_obj;
	uint32_t mkey;
	struct ibv_pd *pd;
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

	bool crypto_en;
	bool bsf_en;
};

struct snap_indirect_mkey {
	struct mlx5dv_devx_obj *devx_obj;
	uint32_t mkey;
	uint64_t addr;
};

int snap_get_pd_id(struct ibv_pd *pd, uint32_t *pd_id);

struct ibv_mr *snap_reg_mr(struct ibv_pd *pd, void *addr, size_t length);

struct snap_cross_mkey *snap_create_cross_mkey(struct ibv_pd *pd,
					       struct snap_device *target_sdev);
int snap_destroy_cross_mkey(struct snap_cross_mkey *mkey);

struct snap_indirect_mkey *
snap_create_indirect_mkey(struct ibv_pd *pd,
			  struct mlx5_devx_mkey_attr *attr);
int
snap_destroy_indirect_mkey(struct snap_indirect_mkey *mkey);

int snap_umem_init(struct ibv_context *context, struct snap_umem *umem);
void snap_umem_reset(struct snap_umem *umem);

struct snap_uar {
	struct mlx5dv_devx_uar *uar;
	struct ibv_context *context;
	int refcnt;
	bool nc; /* non cacheable */

	LIST_ENTRY(snap_uar) entry;
};

struct snap_uar *snap_uar_get(struct ibv_context *ctx);
void snap_uar_put(struct snap_uar *uar);
#endif
