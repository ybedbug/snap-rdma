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
#include "snap_dpa_virtq_common.h"

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

extern struct virtq_q_ops snap_virtq_blk_dpa_ops;
struct virtq_q_ops *get_dpa_queue_ops(void);

#endif
