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
	/* hack to match size with virtio blk queue. Unfortunately
	 * snap virtio block virtq inherits from snap_virtio_queue so we need
	 * to include the difference here.
	 */
	void *vbdev;
	uint32_t uncomp_bdev_cmds;

	struct snap_dpa_rt *rt;
	struct snap_dpa_rt_thread *rt_thr;

	struct ibv_mr *desc_shadow_mr;
	struct vring_desc *desc_shadow;
	struct snap_cross_mkey *cross_mkey;
	struct snap_dpa_duar *duar;

	struct snap_dpa_virtq_common common;

	/* last <= host <= hw */
	uint16_t hw_used_index;
	uint16_t last_hw_used_index;
	uint16_t host_used_index;
	/* todo: make max pending comps configurable */
	struct vring_used_elem pending_comps[16];
	int num_pending_comps;
	int debug_count;

	struct {
		uint32_t n_io_completed;
		uint32_t n_compl_updates;
		uint32_t n_used_updates;
	} stats;
};

int virtq_blk_dpa_send_status(struct snap_virtio_queue *vq, void *data, int size, uint64_t raddr);
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

	uint32_t duar_id;
	uint32_t enabled;

	uint32_t pending;

	struct {
		uint32_t n_vq_heads;
		uint32_t n_vq_tables;
		uint32_t n_sends;
		uint32_t n_long_sends;
		uint32_t n_delta_total;
	} stats;
};

struct __attribute__((packed)) dpa_virtq_cmd_create {
	struct dpa_virtq vq;
};

struct dpa_virtq_cmd {
	struct snap_dpa_cmd base;
	union {
		struct dpa_virtq_cmd_create cmd_create;
	};
};

extern struct virtq_q_ops snap_virtq_blk_dpa_ops;
struct virtq_q_ops *get_dpa_queue_ops(void);

#define SNAP_DPA_VIRTQ_APP "dpa_virtq_split"

#endif
