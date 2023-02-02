/*
 * Copyright © 2022 NVIDIA CORPORATION & AFFILIATES. ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of Nvidia Corporation and its affiliates
 * (the "Company") and all right, title, and interest in and to the software
 * product, including all associated intellectual property rights, are and
 * shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 */

#ifndef SNAP_VRDMA_H
#define SNAP_VRDMA_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "snap.h"
#include "mlx5_ifc.h"

struct snap_vrdma_device;
struct snap_vrdma_ctrl;

enum snap_vrdma_dev_modify {
	SNAP_VRDMA_MOD_DEV_STATUS = (1ULL << 0),
	SNAP_VRDMA_MOD_RESET = (1ULL << 1),
	SNAP_VRDMA_MOD_MAC = (1ULL << 2),
};

#define VRDMA_ALIAS_ACCESS_KEY_NUM_DWORD		8

struct vrdma_allow_other_vhca_access_attr {
	uint32_t type;
	uint32_t obj_id;
	uint32_t access_key_be[VRDMA_ALIAS_ACCESS_KEY_NUM_DWORD];
};

struct vrdma_alias_attr {
	uint32_t type;
	uint32_t orig_vhca_id;
	uint32_t orig_obj_id;
	uint32_t access_key_be[VRDMA_ALIAS_ACCESS_KEY_NUM_DWORD];
};

struct snap_vrdma_device_attr {
	uint64_t			mac;
	uint16_t			status;
	uint16_t			mtu;
	uint16_t			msix_config;
	uint16_t			pci_bdf;
	bool				enabled;
	bool				reset;
	uint16_t			num_msix;
	uint64_t			modifiable_fields;//mask of snap_vrdma_dev_modify
	uint32_t			crossed_vhca_mkey;
	uint16_t			adminq_msix_vector;
	uint16_t			adminq_size; //admin-queue depth
	uint32_t			adminq_nodify_off;
	uint64_t			adminq_base_addr;
};

struct snap_vrdma_test_dummy_device {
	uint64_t			mac;
	uint16_t			status;
	uint16_t			mtu;
	uint16_t			msix_config;
	uint16_t			pci_bdf;
	bool				enabled;
	bool				reset;
	uint16_t			num_msix;
	uint64_t			modifiable_fields;//mask of snap_vrdma_dev_modify
	uint32_t			crossed_vhca_mkey;
	uint16_t			adminq_msix_vector;
	uint16_t			adminq_size; //admin-queue depth
	uint32_t			adminq_nodify_off;
	uint64_t			adminq_base_addr;
};
extern struct snap_vrdma_test_dummy_device g_bar_test;

struct snap_vrdma_device {
	uint32_t vdev_idx;
};

int snap_vrdma_device_mac_init(struct snap_vrdma_ctrl *ctrl);
int snap_vrdma_init_device(struct snap_device *sdev, uint32_t vdev_idx);
int snap_vrdma_teardown_device(struct snap_device *sdev);
int snap_vrdma_query_device(struct snap_device *sdev,
	struct snap_vrdma_device_attr *attr);
int snap_vrdma_modify_device(struct snap_device *sdev, uint64_t mask,
		struct snap_vrdma_device_attr *attr);
void snap_vrdma_pci_functions_cleanup(struct snap_context *sctx);
struct mlx5dv_devx_obj *
mlx_devx_create_eq(struct ibv_context *ctx, uint32_t dev_emu_id,
		   uint16_t msix_vector, uint32_t *eqn);
void mlx_devx_destroy_eq(struct mlx5dv_devx_obj *obj);

struct mlx5dv_devx_obj *mlx_devx_create_alias_obj(struct ibv_context *ctx,
						  struct vrdma_alias_attr *attr,
						  uint32_t *id);
int mlx_devx_allow_other_vhca_access(struct ibv_context *ibv_ctx,
				     struct vrdma_allow_other_vhca_access_attr *attr);
int mlx_devx_emu_db_to_cq_unmap(struct mlx5dv_devx_obj *devx_emu_db_to_cq_ctx);
struct mlx5dv_devx_obj *
mlx_devx_emu_db_to_cq_map(struct ibv_context *ibv_ctx, uint32_t vhca_id,
			  uint32_t queue_id, uint32_t cq_num,
			  uint32_t *id);
#endif
