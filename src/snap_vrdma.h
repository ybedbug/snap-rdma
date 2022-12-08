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

struct snap_vrdma_device;
struct snap_vrdma_ctrl;

enum snap_vrdma_dev_modify {
	SNAP_VRDMA_MOD_DEV_STATUS = (1ULL << 0),
	SNAP_VRDMA_MOD_RESET = (1ULL << 1),
	SNAP_VRDMA_MOD_MAC = (1ULL << 2),
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

#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t

// enum {
// 	MLX5_CMD_OP_CREATE_GENERAL_OBJECT = 0xa00,
// 	MLX5_OBJ_TYPE_EMULATED_DEV_EQ = 0x49,
// };

// struct mlx5_ifc_general_obj_out_cmd_hdr_bits {
// 	u8	 status[0x8];
// 	u8	 reserved_at_8[0x18];

// 	u8	 syndrome[0x20];

// 	u8	 obj_id[0x20];

// 	u8	 reserved_at_60[0x20];
// };

struct mlx5_ifc_create_emulated_dev_eq_in_bits {
    u8     modify_field_select[0x40];

    u8     reserved_at_40[0x20];

    u8     device_emulation_id[0x20];

    u8     reserved_at_e0[0x120];

    u8     reserved0[0x14];
    u8     intr[0xc];

    u8     reserved1[0x40];
};

struct mlx5_ifc_create_eq_out_bits {
    u8     status[0x8];
    u8     reserved_at_8[0x18];

    u8     syndrome[0x20];

    u8     reserved_at_40[0x18];
    u8     eqn[0x8];

    u8     reserved_at_60[0x20];
};

// struct mlx5_ifc_general_obj_in_cmd_hdr_bits {
// 	u8	 opcode[0x10];
// 	u8	 uid[0x10];

// 	u8	 reserved_at_20[0x10];
// 	u8	 obj_type[0x10];

// 	u8	 obj_id[0x20];

// 	u8	 alias_object[0x1];
// 	u8	 reserved_at_61[0x1f];
// };

int snap_vrdma_device_mac_init(struct snap_vrdma_ctrl *ctrl);
int snap_vrdma_init_device(struct snap_device *sdev, uint32_t vdev_idx);
int snap_vrdma_teardown_device(struct snap_device *sdev);
int snap_vrdma_query_device(struct snap_device *sdev,
	struct snap_vrdma_device_attr *attr);
int snap_vrdma_modify_device(struct snap_device *sdev, uint64_t mask,
		struct snap_vrdma_device_attr *attr);
void snap_vrdma_pci_functions_cleanup(struct snap_context *sctx);
struct mlx5dv_devx_obj *
snap_vrdma_mlx_devx_create_eq(struct ibv_context *ctx, uint32_t dev_emu_id,
		   uint16_t msix_vector, uint64_t *eqn);
void snap_vrdma_mlx_devx_destroy_eq(struct mlx5dv_devx_obj *obj);
#endif
