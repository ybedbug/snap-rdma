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

#ifndef _SNAP_DPA_VIRTQ_COMMON_H
#define _SNAP_DPA_VIRTQ_COMMON_H

#include "snap_dpa_common.h"

struct __attribute__((packed)) virtq_desc {
	uint64_t addr;
	uint32_t len;
	uint16_t flags;
	uint16_t next;
};

struct __attribute__((packed)) virtq_device_ring {
	uint16_t flags;
	uint16_t idx;
	uint16_t ring[];
};

#define SNAP_DPA_VIRTQ_DESC_SHADOW_ALIGN 64
/* TODO: this seems to be the common part for all virtqs, not just dpa */
struct snap_dpa_virtq_common {
	uint16_t idx;
	uint16_t size;
	uint64_t desc;
	uint64_t driver;
	uint64_t device;
	uint16_t msix_vector;
	uint16_t vhca_id;
};


enum {
	DPA_VIRTQ_CMD_CREATE = SNAP_DPA_CMD_APP_FIRST,
	DPA_VIRTQ_CMD_DESTROY,
	DPA_VIRTQ_CMD_MODIFY,
	DPA_VIRTQ_CMD_QUERY,
};


enum dpa_virtq_state {
	DPA_VIRTQ_STATE_INIT = 0,
	DPA_VIRTQ_STATE_RDY,
	DPA_VIRTQ_STATE_SUSPEND,
	DPA_VIRTQ_STATE_ERR,
};

struct dpa_virtq_stats {
	uint32_t n_vq_heads;
	uint32_t n_vq_tables;
	uint32_t n_sends;
	uint32_t n_long_sends;
	uint32_t n_delta_total;
};

/* TODO: optimize field alignment */
struct dpa_virtq {
	struct snap_dpa_virtq_common common;

	uint16_t hw_available_index;
	uint16_t hw_used_index;

	uint32_t dpa_xmkey;
	uint32_t dpu_xmkey;
	uint32_t dpu_desc_shadow_mkey;
	uint64_t dpu_desc_shadow_addr;

	uint32_t duar_id;
	enum dpa_virtq_state state;

	uint32_t pending;

	struct dpa_virtq_stats stats;
};

struct __attribute__((packed)) dpa_virtq_cmd_create {
	struct dpa_virtq vq;
};

struct dpa_virtq_cmd_modify {
	enum dpa_virtq_state state;
};

struct dpa_virtq_rsp_query {
	enum dpa_virtq_state state;
	uint16_t hw_available_index;
	uint16_t hw_used_index;
};

struct dpa_virtq_cmd {
	struct snap_dpa_cmd base;
	union {
		struct dpa_virtq_cmd_create cmd_create;
		struct dpa_virtq_cmd_modify cmd_modify;
	};
};

struct dpa_virtq_rsp {
	struct snap_dpa_rsp base;
	union {
		struct dpa_virtq_rsp_query vq_state;
		struct dpa_virtq_stats vq_stats;
	};
};

#define SNAP_DPA_VIRTQ_APP "dpa_virtq_split"

#endif
