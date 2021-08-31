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

#ifndef _SNAP_DPA_VIRTQ_H
#define _SNAP_DPA_VIRTQ_H

#include "snap_dpa_common.h"

enum {
	DPA_VIRTQ_CMD_CREATE = SNAP_DPA_CMD_APP_FIRST,
	DPA_VIRTQ_CMD_DESTROY,
	DPA_VIRTQ_CMD_MODIFY,
	DPA_VIRTQ_CMD_QUERY,
};

struct __attribute__((packed)) dpa_virtq_cmd_create {
	uint16_t idx;
	uint16_t size;
	uint64_t desc;
	uint64_t driver;
	uint64_t device;
	uint16_t hw_avail_index;
	uint16_t hw_used_index;
// dpa specific
	uint32_t dpu_avail_mkey;
	uint64_t dpu_avail_addr;
//todo: cross gvmi mkey - move to tcb
	uint32_t driver_mkey;
};

struct __attribute__((packed)) dpa_virtq_cmd {
	struct snap_dpa_cmd base;
	union {
		struct dpa_virtq_cmd_create cmd_create;
	};
};

extern struct blk_virtq_q_ops snap_virtq_blk_dpa_ops;

#endif

