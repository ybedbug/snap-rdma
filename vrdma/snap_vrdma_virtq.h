/*
 * Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef VRDMA_VIRTQ_H
#define VRDMA_VIRTQ_H

#include <stdint.h>
#include <stdbool.h>
#include "snap.h"
#include <sys/uio.h>
#include "snap_vrdma_common_ctrl.h"
#include "snap_dma.h"

/**
 * struct snap_vrdma_vq_common_ctx - Main struct for common snap_vrdma_vq
 * @idx:	Virtqueue index
 * @fatal_err:	Fatal error flag
 * @priv:	Opaque private struct used for implementation
 */

struct snap_vrdma_vq_common_ctx {
	int idx;
	bool fatal_err;
	void *priv;
};

struct snap_vrdma_vq_start_attr {
	int pg_id;
};

/**
 * enum snap_vrdma_vq_cmd_sm_op_status - status of last operation
 * @VIRTQ_CMD_SM_OP_OK:		Last operation finished without a problem
 * @VIRQT_CMD_SM_OP_ERR:	Last operation failed
 *
 * State machine operates asynchronously, usually by calling a function
 * and providing a callback. Once callback is called it calls the state machine
 * progress again and provides it with the status of the function called.
 * This enum describes the status of the function called.
 */
enum snap_vrdma_vq_wqe_sm_op_status {
	VRDMA_VIRTQ_CMD_SM_OP_OK,
	VRDMA_VIRTQ_CMD_SM_OP_ERR,
};

/**
 * enum snap_vrdma_vq_sw_state - state of sw snap_vrdma_vq
 * @SW_VIRTQ_RUNNING:	Queue receives and operates commands
 * @SW_VIRTQ_FLUSHING:	Queue stops receiving new commands and operates
 *			commands already received
 * @SW_VIRTQ_SUSPENDED:	Queue doesn't receive new commands and has no
 *			commands to operate
 *
 * This is the state of the sw snap_vrdma_vq (as opposed to VIRTQ_BLK_Q PRM FW object)
 */
enum snap_vrdma_vq_sw_state {
	SW_VIRTQ_RUNNING,
	SW_VIRTQ_FLUSHING,
	SW_VIRTQ_SUSPENDED,
};

struct snap_vrdma_vq_cmd {
	struct snap_vrdma_vq_priv *vq_priv;
	int16_t state;
	struct snap_dma_completion dma_comp;
};

/**
 * struct virtq_cmd_ctrs - virtq commands counters
 * @outstanding_total:		active commands - sent from host to DPU (new incoming commands)
 * @outstanding_in_bdev:	active commands - sent to back-end device
 * @outstanding_to_host:	active commands - sent to host (e.g. completions or fetch descriptors)
 * @fatal:			fatal commands counter
 */
struct snap_vrdma_ctrl_queue_out_counter {
	uint32_t outstanding_total;
	uint32_t outstanding_in_bdev;
	uint32_t outstanding_to_host;
	uint32_t fatal;
};

struct snap_vrdma_common_queue_attr {
	uint64_t			modifiable_fields;//mask of snap_virtio_queue_modify
	struct ibv_qp		*qp;
	uint16_t			hw_available_index;
	uint16_t			hw_used_index;

	struct snap_virtio_queue_attr   vattr;
	int					q_provider;
	struct snap_dma_q	*dma_q;
};

/**
 * struct snap_vrdma_vq_bdev - Backend device
 * @ctx:	Opaque bdev context given to backend device functions
 * @ops:	Backend device operation pointers
 */
struct snap_vrdma_vq_bdev {
	void *ctx;
	void *ops;
};

struct snap_vrdma_vq_q_ops {
	struct snap_vrdma_vq *(*create)(struct snap_device *sdev,
			struct snap_vrdma_common_queue_attr *attr);
	int (*destroy)(struct snap_vrdma_vq *vq);
	int (*query)(struct snap_vrdma_vq *vq,
			struct snap_vrdma_common_queue_attr *attr);
	int (*modify)(struct snap_vrdma_vq *vq,
			uint64_t mask, struct snap_vrdma_common_queue_attr *attr);
	/* extended ops */
	int (*poll)(struct snap_vrdma_vq *vq);
	int (*complete)(struct snap_vrdma_vq *vq);
	int (*send_completions)(struct snap_vrdma_vq *vq);
};

struct snap_vrdma_vq {
	uint32_t				idx;
	struct mlx5_snap_devx_obj		*vq;
	struct snap_umem			umem[3];
	uint64_t				mod_allowed_mask;
	struct mlx5_snap_devx_obj		*ctrs_obj;

	struct snap_vrdma_vq_q_ops		*q_ops;
};

enum snap_vrdma_state {
	SNAP_VRDMA_STATE_INIT		= 1 << 0,
	SNAP_VRDMA_STATE_RDY		= 1 << 1,
	SNAP_VRDMA_STATE_SUSPEND	= 1 << 2,
	SNAP_VRDMA_STATE_ERR		= 1 << 3,
};

enum snap_vrdma_error_type {
	SNAP_VRDMA_ERROR_TYPE_NO_ERROR                      = 0x0,
	SNAP_VRDMA_ERROR_TYPE_NETWORK_ERROR                 = 0x1,
	SNAP_VRDMA_ERROR_TYPE_BAD_DESCRIPTOR                = 0x2,
	SNAP_VRDMA_ERROR_TYPE_INVALID_BUFFER                = 0x3,
	SNAP_VRDMA_ERROR_TYPE_DESCRIPTOR_LIST_EXCEED_LIMIT  = 0x4,
	SNAP_VRDMA_ERROR_TYPE_INTERNAL_ERROR                = 0x5,
};

struct snap_vrdma_queue {
	struct snap_vrdma_ctrl *ctrl;
	struct snap_pg *pg;
	struct snap_pg_q_entry pg_q;
	uint32_t pg_id;
	uint32_t thread_id;
	uint32_t idx;
	volatile enum snap_vrdma_vq_sw_state swq_state;
	struct snap_vrdma_vq_common_ctx *vq_ctx;
	struct snap_vrdma_vq_bdev vrdma_bdev;
	struct ibv_pd *pd;
	uint32_t dma_mkey;
	struct snap_dma_q *dma_q;
	struct snap_vrdma_ctrl_queue_out_counter cmd_cntrs;
};

struct snap_vrdma_vq_status_data {
	void *us_status;
	uint16_t status_size;
	uint16_t desc;
};

struct snap_vrdma_vq_sm_state {
	bool (*sm_handler)(struct snap_vrdma_vq_cmd *cmd,
			enum snap_vrdma_vq_wqe_sm_op_status status);
};

struct snap_vrdma_vq_state_machine {
	struct snap_vrdma_vq_sm_state *sm_array;
	uint16_t sme;
};

struct snap_vrdma_vq_impl_ops {
	void (*error_status)(struct snap_vrdma_vq_cmd *cmd);
	void (*clear_status)(struct snap_vrdma_vq_cmd *cmd);
	void (*status_data)(struct snap_vrdma_vq_cmd *cmd, struct snap_vrdma_vq_status_data *sd);
	void (*release_cmd)(struct snap_vrdma_vq_cmd *cmd);
	int (*progress_suspend)(struct snap_virtio_queue *snap_vbq,
			struct snap_virtio_common_queue_attr *qattr);
	int (*send_comp)(struct snap_vrdma_vq_cmd *cmd, struct snap_dma_q *q);
};

struct snap_vrdma_vq_ctx_init_attr {
	struct snap_vrdma_ctrl_queue *vq;
	void *bdev;
	int tx_elem_size;
	int rx_elem_size;
};

void snap_vrdma_ctrl_sched_q(struct snap_vrdma_ctrl *ctrl,
				     struct snap_vrdma_queue *vq);
void snap_vrdma_ctrl_desched_q(struct snap_vrdma_queue *vq);

int snap_vrdma_ctrl_io_progress(struct snap_vrdma_ctrl *ctrl);
int snap_vrdma_ctrl_io_progress_thread(struct snap_vrdma_ctrl *ctrl,
					     uint32_t thread_id);

#endif

