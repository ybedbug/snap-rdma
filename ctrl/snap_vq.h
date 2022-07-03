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

#ifndef SNAP_VQ_H
#define SNAP_VQ_H
#include <stdint.h>
#include <stdbool.h>
#include <sys/uio.h>
#include <sys/queue.h>
#include <infiniband/verbs.h>
#include <linux/virtio_ring.h>


struct snap_vq;
struct snap_vq_cmd;
struct snap_virtio_ctrl;
struct snap_virtio_queue_debugstat;

/**
 * typedef snap_vq_cmd_done_cb_t - Command operation done callback.
 * @cmd: Command context
 * @status: operation status
 *
 * Command callback type to be used when finishing an operation
 * on the given command.
 */
typedef void (*snap_vq_cmd_done_cb_t)(struct snap_vq_cmd *cmd,
					enum ibv_wc_status status);

/**
 * typedef snap_vq_process_fn_t - Command processing function callback.
 * @cmd: Command context
 *
 * function type to be passed be the queue creator (typically virtio
 * controller), and provides a command processing function.
 * When called, it is guaranteed that all descriptors and protocol
 * specific headers are already fetched from host memory
 * (can be acquired using snap_vq_cmd_get_descs() and
 * snap_vq_cmd_<proto>_get_layout() respectively)
 */
typedef void (*snap_vq_process_fn_t)(struct snap_virtio_ctrl *vctrl,
					struct snap_vq_cmd *cmd);

/**
 * enum snap_vq_op_flags - snap VQ Operation flags.
 * @SNAP_VQ_OP_FLAGS_IN_ORDER_COMPLETIONS: Force in-order completions.
 *
 * Describes various special modes of operation for the virtqueue.
 */
enum snap_vq_op_flags {
	SNAP_VQ_OP_FLAGS_IN_ORDER_COMPLETIONS = 1 << 0,
};

/**
 * struct snap_vq_cmd_desc - snap virtio command descriptor
 * @entry: list entry
 * @desc: virtio descriptor
 *
 * encapsulation of single virtio descriptor, so it can be treated
 * as a list element
 */
struct snap_vq_cmd_desc {
	TAILQ_ENTRY(snap_vq_cmd_desc) entry;
	struct vring_desc desc;
};

/**
 * struct snap_vq_cmd_desc_list - snap virtio command descriptor list
 *
 * snap virtio command's descriptor list (and see TAILQ_HEAD documentation).
 */
TAILQ_HEAD(snap_vq_cmd_desc_list, snap_vq_cmd_desc);

/**
 * struct snap_vq_create_attr - snap VQ common creation attributes
 * @index: Queue index.
 * @size: Queue size.
 * @desc_pa: desc table address on host memory
 * @driver_pa: avail ring address on host memory
 * @device_pa: used ring address on host memory
 * @hw_avail_index: avail index to initialize queue with
 * @hw_used_index: used index to initialize queue with
 * @msix_vector: msix vector to assign queue with
 * @op_flags: Operation flags, subset of enum snap_vq_op_flags.
 * @xmkey: Cross-mkey for host memory access
 * @pd: PD for host memory access
 * @sdev: snap device to link queue with
 * @caps: Virtio HW capabilities
 * @comp_channel: Completion channel for queue events (optional)
 * @comp_vector: Completion vector for queue events (optional)
 *
 * Describes all required/optional attribute used for virtqueue
 * creation process.
 */
struct snap_vq_create_attr {
	int index;
	size_t size;
	uint64_t desc_pa;
	uint64_t driver_pa;
	uint64_t device_pa;
	uint16_t hw_avail_index;
	uint16_t hw_used_index;
	uint16_t msix_vector;
	uint32_t op_flags;
	uint32_t xmkey;
	struct ibv_pd *pd;
	struct snap_device *sdev;
	struct snap_virtio_ctrl *vctrl;
	struct snap_virtio_caps *caps;
	struct ibv_comp_channel *comp_channel;
	int comp_vector;
	bool in_recovery;
};

void snap_vq_suspend(struct snap_vq *q);
bool snap_vq_is_suspended(const struct snap_vq *q);
void snap_vq_resume(struct snap_vq *q);
int snap_vq_progress(struct snap_vq *q);
int snap_vq_handle_events(struct snap_vq *q);
struct snap_dma_q *snap_vq_get_dma_q(struct snap_vq *q);
struct ibv_cq *snap_vq_get_vcq(struct snap_vq *q);

const struct snap_vq_cmd_desc_list *snap_vq_cmd_get_descs(struct snap_vq_cmd *cmd);
int snap_vq_cmd_descs_rw(struct snap_vq_cmd *cmd,
		const struct snap_vq_cmd_desc *first_desc, size_t first_offset,
		void *lbuf, size_t total_len, uint32_t lbuf_mkey,
		snap_vq_cmd_done_cb_t done_cb, bool write);

int snap_vq_get_debugstat(struct snap_vq *q,
			  struct snap_virtio_queue_debugstat *q_debugstat);
#endif
