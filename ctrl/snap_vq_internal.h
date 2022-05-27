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

#ifndef SNAP_VQ_INTERNAL_H
#define SNAP_VQ_INTERNAL_H
#include "snap_vq.h"
#include "snap_dma.h"

struct snap_vq_header;
struct snap_vq_cmd;

enum snap_vq_state {
	SNAP_VQ_STATE_RUNNING,
	SNAP_VQ_STATE_FLUSHING,
	SNAP_VQ_STATE_SUSPENDED,
};

struct snap_vq_desc_pool {
	struct snap_vq_cmd_desc *entries;
	struct snap_vq_cmd_desc_list free_descs;
	uint32_t lkey;
};

struct snap_vq_cmd_ops {
	size_t hdr_size;
	size_t ftr_size;
	/* Create: Create new command, based on protocol requirements */
	struct snap_vq_cmd *(*create)(struct snap_vq *q, int index);
	/* Handle: handle command after descriptor chain is obtained. */
	void (*handle)(struct snap_vq_cmd *cmd);
	/* Delete: Delete command */
	void (*delete)(struct snap_vq_cmd *cmd);

	/*
	 * Prefetch (optional): fetch header before finish reading all
	 * descriptor chain. When consuming multiple descriptors, last descs
	 * are being read asynchronously, while first descs are already
	 * available, and header can be prefetech. Such optimization may
	 * improve command latency performance.
	 */
	void (*prefetch)(struct snap_vq_cmd *cmd);
};

struct snap_vq_cmd {
	struct snap_vq *vq;
	struct snap_vq_cmd_desc_list descs;
	int num_descs;
	uint32_t len;
	uint16_t id;
	bool pending_completion;
	struct snap_dma_completion dma_comp;
	snap_vq_cmd_done_cb_t done_cb;
	void *priv;

	TAILQ_ENTRY(snap_vq_cmd) entry;
};

struct snap_vq {
	int index;
	enum snap_vq_state state;
	size_t size;
	uint64_t desc_pa;
	uint64_t driver_pa;
	uint64_t device_pa;
	uint32_t op_flags;
	uint32_t xmkey;
	struct snap_virtio_caps *caps;
	struct ibv_comp_channel *comp_channel;
	struct snap_virtio_ctrl *vctrl;

	TAILQ_HEAD(, snap_vq_cmd) free_cmds;
	TAILQ_HEAD(snap_vq_inflight_cmds, snap_vq_cmd) inflight_cmds;
	TAILQ_HEAD(snap_vq_fatal_cmds, snap_vq_cmd) fatal_cmds;
	const struct snap_vq_cmd_ops *cmd_ops;

	struct snap_dma_q *dma_q;
	struct snap_virtio_queue *hw_q;

	struct snap_vq_desc_pool desc_pool;
};

int snap_vq_create(struct snap_vq *q, struct snap_vq_create_attr *attr,
			const struct snap_vq_cmd_ops *cmd_ops);
void snap_vq_destroy(struct snap_vq *queue);

void snap_vq_cmd_create(struct snap_vq *q, struct snap_vq_cmd *cmd);
void snap_vq_cmd_complete(struct snap_vq_cmd *cmd);
void snap_vq_cmd_fatal(struct snap_vq_cmd *cmd);
void snap_vq_cmd_destroy(struct snap_vq_cmd *cmd);
void snap_vq_cmd_dma_rw_done(struct snap_dma_completion *self, int status);

#endif
