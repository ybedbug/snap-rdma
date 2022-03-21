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

#ifndef SNAP_INTERNAL_H
#define SNAP_INTERNAL_H
#include <stdio.h>
#include <stdint.h>
#include "snap.h"
#include "mlx5_ifc.h"

#define SNAP_ACCESS_KEY_LENGTH DEVX_FLD_SZ_BYTES(allow_other_vhca_access_in, access_key)

struct snap_alias_object {
	struct mlx5dv_devx_obj *obj;
	uint32_t obj_id;

	struct ibv_context *src_context;
	struct ibv_context *dst_context;
	uint32_t dst_obj_id;
	uint8_t access_key[SNAP_ACCESS_KEY_LENGTH];
};

void snap_update_pci_bdf(struct snap_pci *spci, uint16_t pci_bdf);

int snap_allow_other_vhca_access(struct ibv_context *context,
				 enum mlx5_obj_type obj_type,
				 uint32_t obj_id,
				 uint8_t access_key[SNAP_ACCESS_KEY_LENGTH]);

struct snap_alias_object *
snap_create_alias_object(struct ibv_context *src_context,
			 enum mlx5_obj_type obj_type,
			 struct ibv_context *dst_context,
			 uint32_t dst_obj_id,
			 uint8_t access_key[SNAP_ACCESS_KEY_LENGTH]);
void snap_destroy_alias_object(struct snap_alias_object *obj);

struct mlx5_snap_devx_obj *snap_emulation_device_create(struct snap_device *sdev,
				struct snap_device_attr *attr);
void snap_emulation_device_destroy(struct snap_device *sdev);
#endif
