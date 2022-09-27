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
	SNAP_VRDMA_MOD_DEV_STATUS = (1ULL << 0),
	SNAP_VRDMA_MOD_RESET = (1ULL << 2),
};

struct snap_vrdma_device_attr {
	uint64_t			mac;
	uint16_t			status;
	uint16_t			msix_config;
	uint16_t			pci_bdf;
	bool				enabled;
	bool				reset;
	uint16_t			num_msix;
	uint64_t			modifiable_fields;//mask of snap_vrdma_dev_modify
	uint32_t			crossed_vhca_mkey;
	uint16_t			adminq_size;
	uint32_t			adminq_msix_vector;
	uint32_t			adminq_nodify_off;
	uint64_t			adminq_base_addr;
};

struct snap_vrdma_device {
	uint32_t vdev_idx;
};

int snap_vrdma_init_device(struct snap_device *sdev, uint32_t vdev_idx);
int snap_vrdma_teardown_device(struct snap_device *sdev);
int snap_vrdma_query_device(struct snap_device *sdev,
	struct snap_vrdma_device_attr *attr);
int snap_vrdma_modify_device(struct snap_device *sdev, uint64_t mask,
		struct snap_vrdma_device_attr *attr);
void snap_vrdma_pci_functions_cleanup(struct snap_context *sctx);
#endif
