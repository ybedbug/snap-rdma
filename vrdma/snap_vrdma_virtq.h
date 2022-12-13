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
#include "snap_dma.h"
#include "snap_poll_groups.h"

struct snap_vrdma_vq_start_attr {
	int pg_id;
};

/**
 * enum snap_vrdma_vq_cmd_sm_op_status - status of last operation
 * @VRDMA_VIRTQ_WQE_SM_OP_OK:	Last operation finished without a problem
 * @VRDMA_VIRTQ_WQE_SM_OP_ERR:	Last operation failed
 *
 * State machine operates asynchronously, usually by calling a function
 * and providing a callback. Once callback is called it calls the state machine
 * progress again and provides it with the status of the function called.
 * This enum describes the status of the function called.
 */
enum snap_vrdma_vq_wqe_sm_op_status {
	VRDMA_VIRTQ_WQE_SM_OP_OK,
	VRDMA_VIRTQ_WQE_SM_OP_ERR,
};

/**
 * enum snap_vrdma_vq_sw_state - state of sw snap_vrdma_queue
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

struct snap_vrdma_queue {
	TAILQ_ENTRY(snap_vrdma_queue) vq;
	struct snap_vrdma_ctrl *ctrl;
	struct snap_pg *pg;
	struct snap_pg_q_entry pg_q;
	uint32_t pg_id;
	uint32_t thread_id;
	uint32_t idx;
	volatile enum snap_vrdma_vq_sw_state swq_state;
	struct ibv_pd *pd;
	uint32_t dma_mkey;
	struct snap_dma_q *dma_q;
	struct vrdma_dpa_vq *dpa_vq;
	struct snap_vrdma_ctrl_queue_out_counter cmd_cntrs;
};

struct snap_vrdma_vq_create_attr {
	void *bdev;
	struct ibv_pd *pd;
	uint32_t sq_size;
	uint32_t rq_size;
	uint16_t tx_elem_size;
	uint16_t rx_elem_size;
	uint32_t vqpn;
	snap_dma_rx_cb_t rx_cb;
};

struct snap_vrdma_qp_db_counter {
	// total doorbels
	uint64_t total_dbs;
	// total processed completions
	uint64_t total_completed;
};

struct snap_vrdma_qp_stat {
	struct snap_vrdma_qp_db_counter rx;
	struct snap_vrdma_qp_db_counter tx;
};

#define SNAP_VRDMA_BACKEND_CQE_SIZE 128
#define SNAP_VRDMA_BACKEND_MIN_RQ_SIZE 4
#define MAC_ADDR_LEN 6
#define MAC_ADDR_2MSBYTES_LEN 2

struct snap_vrdma_backend_qp {
	struct snap_qp *sqp;
	uint32_t qpnum;
	struct snap_hw_qp hw_qp;
	struct snap_qp_attr qp_attr;
	struct snap_hw_cq rq_hw_cq;
	struct snap_hw_cq sq_hw_cq;
	enum snap_db_ring_flag db_flag;
	bool tx_need_ring_db;
	struct mlx5_wqe_ctrl_seg *ctrl;
	struct snap_vrdma_qp_stat stat;
};

struct snap_vrdma_bk_qp_rdy_attr {
	uint8_t src_addr_index;
	uint8_t *dest_mac;
	union ibv_gid rgid_rip;
};

struct snap_vrdma_queue_ops {
	struct snap_vrdma_queue *(*create)(struct snap_vrdma_ctrl *ctrl, 
										struct snap_vrdma_vq_create_attr *q_attr);
	void (*destroy)(struct snap_vrdma_ctrl *ctrl, struct snap_vrdma_queue *queue);
	int (*progress)(struct snap_vrdma_queue *queue);
	void (*start)(struct snap_vrdma_queue *queue);
	int (*suspend)(struct snap_vrdma_queue *queue);
	bool (*is_suspended)(struct snap_vrdma_queue *queue);
	int (*resume)(struct snap_vrdma_queue *queue);
};

void snap_vrdma_sched_vq(struct snap_vrdma_ctrl *ctrl,
				     struct snap_vrdma_queue *vq);
void snap_vrdma_desched_vq(struct snap_vrdma_queue *vq);

int snap_vrdma_ctrl_io_progress(struct snap_vrdma_ctrl *ctrl);
int snap_vrdma_ctrl_io_progress_thread(struct snap_vrdma_ctrl *ctrl,
					     uint32_t thread_id);
struct snap_vrdma_queue_ops *get_vrdma_queue_ops(void);
int snap_vrdma_create_qp_helper(struct ibv_pd *pd, 
			struct snap_vrdma_backend_qp *qp);
void snap_vrdma_destroy_qp_helper(struct snap_vrdma_backend_qp *qp);
int snap_vrdma_modify_bankend_qp_rst2init(struct snap_qp *qp,
			struct ibv_qp_attr *qp_attr, int attr_mask);
int snap_vrdma_modify_bankend_qp_init2rtr(struct snap_qp *qp,
			struct ibv_qp_attr *qp_attr, int attr_mask,
			struct snap_vrdma_bk_qp_rdy_attr *rdy_attr);
int snap_vrdma_modify_bankend_qp_rtr2rts(struct snap_qp *qp,
			struct ibv_qp_attr *qp_attr, int attr_mask);
#endif

