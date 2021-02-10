/*
 * Copyright (c) 2020 Mellanox Technologies, Inc.  All rights reserved.
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

#ifndef SNAP_RDMA_CHANNEL_H
#define SNAP_RDMA_CHANNEL_H

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <pthread.h>

#include <linux/if_ether.h>
#include <netdb.h>
#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>

#define SNAP_CHANNEL_RDMA_IP "SNAP_RDMA_IP"
#define SNAP_CHANNEL_RDMA_PORT_1 "SNAP_RDMA_PORT_1"
#define SNAP_CHANNEL_RDMA_PORT_2 "SNAP_RDMA_PORT_2"

struct mlx5_snap_cm_req {
	__u16				pci_bdf;
};

enum mlx5_snap_opcode {
	MLX5_SNAP_CMD_START_LOG		= 0x00,
	MLX5_SNAP_CMD_STOP_LOG		= 0x01,
	MLX5_SNAP_CMD_GET_LOG_SZ	= 0x02,
	MLX5_SNAP_CMD_REPORT_LOG	= 0x03,
	MLX5_SNAP_CMD_FREEZE_DEV	= 0x04,
	MLX5_SNAP_CMD_UNFREEZE_DEV	= 0x05,
	MLX5_SNAP_CMD_QUIESCE_DEV	= 0x06,
	MLX5_SNAP_CMD_UNQUIESCE_DEV	= 0x07,
	MLX5_SNAP_CMD_GET_STATE_SZ	= 0x08,
	MLX5_SNAP_CMD_READ_STATE	= 0x09,
	MLX5_SNAP_CMD_WRITE_STATE	= 0x0a,
};

enum mlx5_snap_cmd_status {
	MLX5_SNAP_SC_SUCCESS			= 0x0,
	MLX5_SNAP_SC_INVALID_OPCODE		= 0x1,
	MLX5_SNAP_SC_INVALID_FIELD		= 0x2,
	MLX5_SNAP_SC_CMDID_CONFLICT		= 0x3,
	MLX5_SNAP_SC_DATA_XFER_ERROR		= 0x4,
	MLX5_SNAP_SC_INTERNAL			= 0x5,
	MLX5_SNAP_SC_ALREADY_STARTED_LOG	= 0x6,
	MLX5_SNAP_SC_ALREADY_STOPPED_LOG	= 0x7,
};

struct mlx5_snap_completion {
	__u16	command_id;
	__u16	status;
	__u64	result;
	__u32	reserved32;
};

struct mlx5_snap_start_dirty_log_command {
	__u8			opcode;
	__u8			flags;
	__u16			command_id;
	__u64			addr;
	__u32			length;
	__u32			key;
	__u32			page_size;
	__u32			cdw6;
	__u32			cdw7;
	__u32			cdw8;
	__u32			cdw9;
	__u32			cdw10;
	__u32			cdw11;
	__u32			cdw12;
	__u32			cdw13;
	__u32			cdw14;
	__u32			cdw15;
};

struct mlx5_snap_rw_command {
	__u8			opcode;
	__u8			flags;
	__u16			command_id;
	__u64			addr;
	__u32			length;
	__u32			key;
	__u64			offset;
	__u32			cdw7;
	__u32			cdw8;
	__u32			cdw9;
	__u32			cdw10;
	__u32			cdw11;
	__u32			cdw12;
	__u32			cdw13;
	__u32			cdw14;
	__u32			cdw15;
};

struct mlx5_snap_common_command {
	__u8			opcode;
	__u8			flags;
	__u16			command_id;
	__u64			addr;
	__u32			length;
	__u32			key;
	__u32			cdw5;
	__u32			cdw6;
	__u32			cdw7;
	__u32			cdw8;
	__u32			cdw9;
	__u32			cdw10;
	__u32			cdw11;
	__u32			cdw12;
	__u32			cdw13;
	__u32			cdw14;
	__u32			cdw15;
};

#define SNAP_CHANNEL_QUEUE_SIZE	64
#define SNAP_CHANNEL_DESC_SIZE sizeof(struct mlx5_snap_common_command)
#define SNAP_CHANNEL_RSP_SIZE sizeof(struct mlx5_snap_completion)

#define SNAP_CHANNEL_BITMAP_ELEM_SZ sizeof(uint8_t)
#define SNAP_CHANNEL_BITMAP_ELEM_BIT_SZ (8 * SNAP_CHANNEL_BITMAP_ELEM_SZ)
#define SNAP_CHANNEL_INITIAL_BITMAP_ARRAY_SZ 1048576

#define snap_channel_max(a, b) (((a) > (b)) ? (a):(b))

/*
 * Inital bitmap size is 1MB (will cover 32GB guest memory in case the page
 * size is 4KB since each bit represents a page
 */
#define SNAP_CHANNEL_INITIAL_BITMAP_SIZE \
	(SNAP_CHANNEL_INITIAL_BITMAP_ARRAY_SZ * SNAP_CHANNEL_BITMAP_ELEM_SZ)

/**
 * struct snap_dirty pages - internal struct holds the information of the
 *                           dirty pages in a bit per page manner.
 *
 * @page_size: the page size that is represented by a bit, given by the host.
 * @bmap_num_elements: size of the bmap array, initial equals to
 *                     SNAP_CHANNEL_INITIAL_BITMAP_ARRAY_SZ. increase by x2
 *                     factor when required. one-based.
 *
 * @highest_dirty_element: the highest dirty element, so we don't have to copy
 *                         the whole bitmap. one-based.
 * @lock: bitmap lock.
 * @bmap: dirty pages bitmap array with bmap_num_elements elements.
 */
struct snap_dirty_pages {
	int		page_size;
	uint64_t	bmap_num_elements;
	uint64_t	highest_dirty_element;
	pthread_mutex_t	lock;
	uint8_t		*bmap;
	uint64_t	copy_bmap_num_elements;
	pthread_mutex_t	copy_lock;
	uint8_t		*copy_bmap;
	struct ibv_mr	*copy_mr;
};

/**
 * struct snap_internal_state - struct that holds the information of the
 *                              internal device state.
 *
 * @state_size: the state size in bytes.
 * @lock: state lock.
 * @state: the state information.
 * @state_mr: state memory region.
 */
struct snap_internal_state {
	uint32_t	state_size;
	pthread_mutex_t	lock;
	void		*state;
	struct ibv_mr	*state_mr;
};

/**
 * struct snap_rdma_channel - internal struct holds the information of the
 *                       communication channel
 *
 * @dirty_pages: dirty pages struct, used to track dirty pages.
 */
struct snap_rdma_channel {
	struct snap_channel			base;
	struct snap_dirty_pages			dirty_pages;
	struct snap_internal_state		state;

	/* CM stuff */
	pthread_t				cmthread;
	struct rdma_event_channel		*cm_channel;
	struct rdma_cm_id			*cm_id; /* connection */
	struct rdma_cm_id			*listener; /* listener */
	struct ibv_pd				*pd;
	struct ibv_cq				*cq;
	pthread_t				cqthread;
	struct ibv_comp_channel			*channel;
	struct ibv_qp				*qp;
	char					rsp_buf[SNAP_CHANNEL_QUEUE_SIZE * SNAP_CHANNEL_RSP_SIZE];
	struct ibv_mr				*rsp_mr;
	struct ibv_sge				rsp_sgl[SNAP_CHANNEL_QUEUE_SIZE];
	struct ibv_send_wr			rsp_wr[SNAP_CHANNEL_QUEUE_SIZE];
	char					recv_buf[SNAP_CHANNEL_QUEUE_SIZE * SNAP_CHANNEL_DESC_SIZE];
	struct ibv_mr				*recv_mr;
	struct ibv_sge				recv_sgl[SNAP_CHANNEL_QUEUE_SIZE];
	struct ibv_recv_wr			recv_wr[SNAP_CHANNEL_QUEUE_SIZE];
};

#endif
