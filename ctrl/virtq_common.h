/*
 * Copyright (c) 2021 NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
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

#ifndef VIRTQ_COMMON_H
#define VIRTQ_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include "snap.h"
#include <sys/uio.h>
#include "snap_virtio_common_ctrl.h"
#include "snap_dma.h"

#define ERR_ON_CMD(cmd, fmt, ...) \
	snap_error("queue:%d cmd_idx:%d err: " fmt, \
		   (cmd)->vq_priv->vq_ctx->idx, (cmd)->idx, ## __VA_ARGS__)

//scaffolding until fs uses common cmd also
#define ERR_ON_CMD_FS(cmd, fmt, ...) \
	snap_error("queue:%d cmd_idx:%d err: " fmt, \
		   (cmd)->vq_priv->vq_ctx.common_ctx.idx, (cmd)->idx, ## __VA_ARGS__)

/* uncomment to enable fast path debugging */
// #define VIRTQ_DEBUG_DATA
#ifdef VIRTQ_DEBUG_DATA
#define virtq_log_data(cmd, fmt, ...) \
	printf("queue:%d cmd_idx:%d " fmt, (cmd)->vq_priv->vq_ctx->idx, (cmd)->idx, \
	       ## __VA_ARGS__)
//scaffolding until fs uses common cmd also
#define virtq_log_data_fs(cmd, fmt, ...) \
	printf("queue:%d cmd_idx:%d " fmt, (cmd)->vq_priv->vq_ctx.common_ctx.idx, (cmd)->idx, \
	       ## __VA_ARGS__)
#else
#define virtq_log_data(cmd, fmt, ...)
#define virtq_log_data_fs(cmd, fmt, ...)
#endif

/**
 * struct virtq_common_ctx - Main struct for common virtq
 * @idx:	Virtqueue index
 * @fatal_err:	Fatal error flag
 * @priv:	Opaque private struct used for implementation
 */

struct virtq_common_ctx {
	int idx;
	bool fatal_err;
	void *priv;
};

/**
 * struct virtq_create_attr - Attributes given for virtq creation
 *
 * @idx:	Virtqueue index
 * @size_max:	maximum size of any single segment
 *              Note: for blk - VIRTIO_BLK_F_SIZE_MAX (from virtio spec)
 * @seg_max:	maximum number of segments in a request
 *              Note: for blk VIRTIO_BLK_F_SEG_MAX (from virtio spec)
 * @queue_size:	VIRTIO_QUEUE_SIZE (from virtio spec)
 * @pd:		Protection domain on which rdma-qps will be opened
 * @desc:	Descriptor Area (from virtio spec Virtqueues section)
 * @driver:	Driver Area
 * @device:	Device Area
 *
 * @hw_available_index:	initial value of the driver available index.
 * @hw_used_index:	initial value of the device used index
 */
struct virtq_create_attr {
	int idx;
	int size_max;
	int seg_max;
	int queue_size;
	struct ibv_pd *pd;
	uint64_t desc;
	uint64_t driver;
	uint64_t device;
	uint16_t max_tunnel_desc;
	uint16_t msix_vector;
	bool virtio_version_1_0;
	uint16_t hw_available_index;
	uint16_t hw_used_index;
	bool force_in_order;
};

struct virtq_start_attr {
	int pg_id;
};

/**
 * struct virtq_split_tunnel_req_hdr - header of command received from FW
 *
 * Struct uses 2 rsvd so it will be aligned to 4B (and not 8B)
 */
struct virtq_split_tunnel_req_hdr {
	uint16_t descr_head_idx;
	uint16_t num_desc;
	uint32_t rsvd1;
	uint32_t rsvd2;
};

/**
 * struct virtq_split_tunnel_comp - header of completion sent to FW
 */
struct virtq_split_tunnel_comp {
	uint32_t descr_head_idx;
	uint32_t len;
};


/**
 * enum virtq_cmd_sm_op_status - status of last operation
 * @VIRTQ_CMD_SM_OP_OK:		Last operation finished without a problem
 * @VIRQT_CMD_SM_OP_ERR:	Last operation failed
 *
 * State machine operates asynchronously, usually by calling a function
 * and providing a callback. Once callback is called it calls the state machine
 * progress again and provides it with the status of the function called.
 * This enum describes the status of the function called.
 */
enum virtq_cmd_sm_op_status {
	VIRTQ_CMD_SM_OP_OK,
	VIRTQ_CMD_SM_OP_ERR,
};

/**
 * enum virtq_sw_state - state of sw virtq
 * @SW_VIRTQ_RUNNING:	Queue receives and operates commands
 * @SW_VIRTQ_FLUSHING:	Queue stops recieving new commands and operates
 *			commands already received
 * @SW_VIRTQ_SUSPENDED:	Queue doesn't receive new commands and has no
 *			commands to operate
 *
 * This is the state of the sw virtq (as opposed to VIRTQ_BLK_Q PRM FW object)
 */
enum virtq_sw_state {
	SW_VIRTQ_RUNNING,
	SW_VIRTQ_FLUSHING,
	SW_VIRTQ_SUSPENDED,
};

/**
 * struct virtq_cmd - command context
 * @idx:				descr_head_idx modulo queue size
 * @descr_head_idx:		descriptor head index
 * @num_desc:			number of descriptors in the command
 * @state:				state of sm processing the command
 * @buf:				buffer holding the request data and aux data
 * @req_size:			allocated request buffer size
 * @aux:				aux data resided in dma/mr memory
 * @mr:					buf mr
 * @req_buf:			pointer to request buffer
 * @req_mr:				request buffer mr
 * @dma_comp:			struct given to snap library
 * @total_seg_len:		total length of the request data to be written/read
 * @total_in_len:		total length of data written to request buffers
 * @use_dmem:			command uses dynamic mem for req_buf
 * @cmd_available_index:sequential number of the command according to arrival
 */
struct virtq_cmd {
	int idx;
	uint16_t descr_head_idx;
	size_t num_desc;
	uint32_t num_merges;
	struct virtq_priv *vq_priv;
	int16_t state;
	uint8_t *buf;
	uint32_t req_size;
	struct ibv_mr *aux_mr;
	void *aux;
	void *ftr;
	struct ibv_mr *mr;
	uint8_t *req_buf;
	struct ibv_mr *req_mr;
	struct snap_dma_completion dma_comp;
	uint32_t total_seg_len;
	uint32_t total_in_len;
	bool use_dmem;
	struct snap_virtio_ctrl_queue_counter *io_cmd_stat;
	uint16_t cmd_available_index;
	bool use_seg_dmem;
};

/**
 * struct virtq_bdev - Backend device
 * @ctx:	Opaque bdev context given to backend device functions
 * @ops:	Backend device operation pointers
 */
struct virtq_bdev {
	void *ctx;
	void *ops;
};

struct virtq_priv {
	struct virtq_state_machine *custom_sm;
	const struct virtq_impl_ops *ops;
	volatile enum virtq_sw_state swq_state;
	struct virtq_common_ctx *vq_ctx;
	struct virtq_bdev blk_dev;
	struct ibv_pd *pd;
	struct snap_virtio_queue *snap_vbq;
	struct snap_virtio_queue_attr *vattr;
	struct snap_dma_q *dma_q;
	struct virtq_cmd *cmd_arr;
	int cmd_cntr;
	int seg_max;
	int size_max;
	int pg_id;
	struct snap_virtio_ctrl_queue *vbq;
	uint16_t ctrl_available_index;
	bool force_in_order;
	/* current inorder value, for which completion should be sent */
	uint16_t ctrl_used_index;
	bool zcopy;
	int merge_descs;
	bool use_mem_pool;
	int thread_id;
};

struct virtq_status_data {
	void *us_status;
	uint16_t status_size;
	uint16_t desc;
};


struct virtq_sm_state {
	bool (*sm_handler)(struct virtq_cmd *cmd,
			enum virtq_cmd_sm_op_status status);
};

struct virtq_state_machine {
	struct virtq_sm_state *sm_array;
	uint16_t sme;
};

struct virtq_impl_ops {
	struct vring_desc* (*get_descs)(struct virtq_cmd *cmd);
	void (*error_status)(struct virtq_cmd *cmd);
	void (*status_data)(struct virtq_cmd *cmd, struct virtq_status_data *sd);
	void (*release_cmd)(struct virtq_cmd *cmd);
	void (*descs_processing)(struct virtq_cmd *cmd);
	void (*mem_pool_release)(struct virtq_cmd *cmd);
	int (*seg_dmem)(struct virtq_cmd *cmd);
	bool (*seg_dmem_release)(struct virtq_cmd *cmd);
};

/**
 * enum virtq_cmd_sm_state - state of the sm handling a cmd
 * @VIRTQ_CMD_STATE_IDLE:               SM initialization state
 * @VIRTQ_CMD_STATE_FETCH_CMD_DESCS:    SM received tunnel cmd and copied
 *										immediate data, now fetch cmd descs
 * @VIRTQ_CMD_STATE_READ_HEADER			Read request header data from host memory
 * @VIRTQ_CMD_STATE_PARSE_HEADER		process header
 * @VIRTQ_CMD_STATE_READ_DATA:          Read request data from host memory
 * @VIRTQ_CMD_STATE_HANDLE_REQ:         Handle received request from host, perform
 *                                      READ/WRITE/FLUSH
 * @VIRTQ_CMD_STATE_T_OUT_IOV_DONE:     Finished writing to bdev, check write
 *                                      status
 * @VIRTQ_CMD_STATE_T_IN_IOV_DONE:      Write data pulled from bdev to host memory
 * @VIRTQ_CMD_STATE_WRITE_STATUS:       Write cmd status to host memory
 * @VIRTQ_CMD_STATE_SEND_COMP:          Send completion to FW
 * @VIRTQ_CMD_STATE_SEND_IN_ORDER_COMP: Send completion to FW for commands completed
 *                                      unordered
 * @VIRTQ_CMD_STATE_RELEASE:            Release command
 * @VIRTQ_CMD_STATE_FATAL_ERR:          Fatal error, SM stuck here (until reset)
 * @VIRTQ_CMD_NUM_OF_STATES				should always be the last enum
 */
enum virtq_cmd_sm_state {
	VIRTQ_CMD_STATE_IDLE,
	VIRTQ_CMD_STATE_FETCH_CMD_DESCS,
	VIRTQ_CMD_STATE_READ_HEADER,
	VIRTQ_CMD_STATE_PARSE_HEADER,
	VIRTQ_CMD_STATE_READ_DATA,
	VIRTQ_CMD_STATE_HANDLE_REQ,
	VIRTQ_CMD_STATE_OUT_DATA_DONE,
	VIRTQ_CMD_STATE_IN_DATA_DONE,
	VIRTQ_CMD_STATE_WRITE_STATUS,
	VIRTQ_CMD_STATE_SEND_COMP,
	VIRTQ_CMD_STATE_SEND_IN_ORDER_COMP,
	VIRTQ_CMD_STATE_RELEASE,
	VIRTQ_CMD_STATE_FATAL_ERR,
	VIRTQ_CMD_NUM_OF_STATES,
};

bool virtq_ctx_init(struct virtq_common_ctx *vq_ctx,
					struct virtq_create_attr *attr,
					struct snap_virtio_queue_attr *vattr,
					struct snap_virtio_ctrl_queue *vq,
					void *bdev,
					int rx_elem_size, uint16_t max_tunnel_desc, snap_dma_rx_cb_t cb);
void virtq_ctx_destroy(struct virtq_priv *vq_priv);
int virtq_cmd_progress(struct virtq_cmd *cmd, enum virtq_cmd_sm_op_status status);
bool virtq_sm_idle(struct virtq_cmd *cmd, enum virtq_cmd_sm_op_status status);
bool virtq_sm_fetch_cmd_descs(struct virtq_cmd *cmd,
			       enum virtq_cmd_sm_op_status status);
bool virtq_sm_write_back_done(struct virtq_cmd *cmd,
				   enum virtq_cmd_sm_op_status status);
void virtq_mark_dirty_mem(struct virtq_cmd *cmd, uint64_t pa,
					uint32_t len, bool is_completion);
bool virtq_sm_write_status(struct virtq_cmd *cmd,
				   enum virtq_cmd_sm_op_status status);
bool virtq_sm_send_completion(struct virtq_cmd *cmd,
				     enum virtq_cmd_sm_op_status status);
bool virtq_sm_release(struct virtq_cmd *cmd, enum virtq_cmd_sm_op_status status);
bool virtq_sm_fatal_error(struct virtq_cmd *cmd, enum virtq_cmd_sm_op_status status);
#endif

