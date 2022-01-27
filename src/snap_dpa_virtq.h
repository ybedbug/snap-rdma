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

#if !__DPA
struct snap_dpa_virtq {
	struct snap_virtio_queue vq;

	struct snap_dpa_rt *rt;
	struct snap_dpa_rt_thread *rt_thr;

	struct ibv_mr *desc_shadow_mr;
	struct virtq_desc *desc_shadow;

	/* hack to do window copy without xgvmi mkey */
	struct ibv_mr *host_driver_mr;

	struct snap_dpa_virtq_common common;
};
#endif

enum {
	DPA_VIRTQ_CMD_CREATE = SNAP_DPA_CMD_APP_FIRST,
	DPA_VIRTQ_CMD_DESTROY,
	DPA_VIRTQ_CMD_MODIFY,
	DPA_VIRTQ_CMD_QUERY,
};

/* TODO: optimize field alignment */
struct dpa_virtq {
	struct snap_dpa_virtq_common common;

	uint16_t hw_available_index;
	uint16_t hw_used_index;

	uint32_t host_mkey; /* todo: should be part of the rt thread */
	uint32_t dpu_desc_shadow_mkey;
	uint64_t dpu_desc_shadow_addr;
};

struct __attribute__((packed)) dpa_virtq_cmd_create {
	struct dpa_virtq vq;
};

struct __attribute__((packed)) dpa_virtq_cmd {
	struct snap_dpa_cmd base;
	union {
		struct dpa_virtq_cmd_create cmd_create;
	};
};

extern struct virtq_q_ops snap_virtq_blk_dpa_ops;
struct virtq_q_ops *get_dpa_ops(void);

#define SNAP_DPA_VIRTQ_APP "snap_dpa_virtq_split"

#endif
