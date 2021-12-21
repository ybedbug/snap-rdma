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

#ifndef SNAP_VQ_PRM_H
#define SNAP_VQ_PRM_H
#include <stdint.h>
#include "snap_macros.h"

struct snap_vq_header {
	uint16_t desc_head_idx;
	uint16_t num_descs;
	uint64_t rsvd;
	struct vring_desc descs[];
} SNAP_PACKED;

struct snap_vq_completion {
	uint32_t id;
	uint32_t len;
} SNAP_PACKED;

#endif
