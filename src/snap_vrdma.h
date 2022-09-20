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

enum snap_vrdma_dev_modify {
	SNAP_VRDMA_MOD_DEV_STATUS = 1 << 0,
	SNAP_VRDMA_MOD_LINK_STATUS = 1 << 1,
	SNAP_VRDMA_MOD_RESET = 1 << 2,
	SNAP_VRDMA_MOD_PCI_COMMON_CFG = 1 << 3,
	SNAP_VRDMA_MOD_DEV_CFG = 1 << 4,
	SNAP_VRDMA_MOD_ALL = 1 << 6,
	SNAP_VRDMA_MOD_QUEUE_CFG = 1 << 7,
	SNAP_VRDMA_MOD_NUM_MSIX = 1 << 8,
	SNAP_VRDMA_MOD_DYN_MSIX_RESET = 1 << 9,
	SNAP_VRDMA_MOD_PCI_HOTPLUG_STATE = 1 << 10,
};

struct snap_vrdma_device_attr {
	uint64_t			device_feature;
	uint64_t			driver_feature;
	uint64_t			mac;
	uint16_t			status;
	uint16_t			max_queue_pairs;
	uint16_t			mtu;
	uint16_t			msix_config;
	uint16_t			max_queues;
	uint16_t			max_queue_size;
	uint16_t			pci_bdf;
	bool				enabled;
	bool				reset;
	uint16_t			num_msix;
	uint8_t				config_generation;
	uint8_t				device_feature_select;
	uint8_t				driver_feature_select;
	uint64_t			modifiable_fields;//mask of snap_vrdma_dev_modify
	uint32_t			crossed_vhca_mkey;
	uint8_t				pci_hotplug_state;
};

struct snap_vrdma_migration_log {
	uint16_t			flag;
	uint16_t			mode;

	uint32_t			guest_page_size;
	uint64_t			log_base;
	uint32_t			log_size;
	uint32_t			num_sge;

	uint32_t			dirty_map_mkey;
	struct mlx5_klm			*klm_array;
	struct snap_indirect_mkey	*indirect_mkey;
	struct snap_cross_mkey		*crossing_mkey;
	struct ibv_mr			*mr;
};

struct snap_vrdma_device {
	uint32_t				num_queues;
	struct snap_vrdma_migration_log    	lattr;
};

int snap_vrdma_init_device(struct snap_device *sdev);
int snap_vrdma_teardown_device(struct snap_device *sdev);
int snap_vrdma_query_device(struct snap_device *sdev,
	struct snap_vrdma_device_attr *attr);
int snap_vrdma_modify_device(struct snap_device *sdev, uint64_t mask,
		struct snap_vrdma_device_attr *attr);
void snap_vrdma_pci_functions_cleanup(struct snap_context *sctx);
#endif
