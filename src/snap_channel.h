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

#ifndef SNAP_CHANNEL_H
#define SNAP_CHANNEL_H

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

#define snap_channel_error printf
#define snap_channel_warn  printf
#define snap_channel_info  printf

/**
 * struct snap_migration_ops - completion handle and callback
 * for live migration support
 *
 * This structure should be allocated by the snap-controller and can be
 * passed to communication primitives.
 *
 * In order to fulfil the above requirements, each SNAP controller must
 * implement basic migration operations. Since these operations are generic,
 * it will be passed during the creation of the snap_device that will create
 * an internal communication channel. Using these callbacks, the internal
 * communication channel will be able to master the migration process for the
 * controller that is represented by this snap_device. By passing these
 * callbacks, each SNAP controller actually gives supervisor permissions to the
 * communication channel to change its operational states, save/restore the
 * internal controller state and start/stop dirty pages tracking.
 *
 * @quiesce: This operation will stop the device from issuing DMA requests.
 * Once successfully returned, the device is not allowed to initiate any
 * operation that might dirty new pages nor changing other devices state.
 * The device can still receive requests that might change its internal state.
 * @unquiesce: This operation is counterpart of the quiesce operation.
 * Once successfully returned, the device is allowed issuing DMA operations
 * and change other devices state.
 * @freeze: This operation will notify the device to stop reacting to incoming
 * requests.Once successfully returned, the device internal state is not
 * allowed to change until it is unfreezed again.
 * @unfreeze: This operation is counterpart of the freeze operation.
 * Once successfully returned, the device is allowed to react to incoming
 * requests that might change its internal state.
 * @get_state_size: Query for internal device state size. In case the
 * controller is unfreezed, device state can be changed. Controllers that
 * don't support tracking for state changes while running will return 0 for
 * this query unless device is freezed. For controllers that track internal
 * state while running, the implementation is controller specific.
 * The returned size, in bytes, will inform the migration SW on the upcoming
 * amount of data that should be copied from the controller.
 * @copy_state: This operation will be used to save and restore device state.
 * During "saving" procedure, the controller will copy the internal device
 * state to a given buffer. The migration SW will query the state size prior
 * running this operation during "saving" states.
 * During "restore" procedure, the migration SW will copy the migrated state to
 * the controller. In this stage, the controller must be freezed and quiesced.
 * @start_dirty_pages_track: This operation will be used to inform the
 * controller to start tracking and reporting dirty pages to the communication
 * channel. A controller can track dirty pages only while running. For live
 * migration, capable controllers should be able to start tracking dirty pages
 * during "Pre-copy" phase and stop tracking during "Stop-and-Copy" phase.
 * @stop_dirty_pages_track: This operation will be used to inform the
 * controller to stop tracking and reporting dirty pages. For live migration,
 * capable controllers should be able to start tracking dirty pages during
 * "Pre-copy" phase and stop tracking during "Stop-and-Copy" phase.
 * @get_dirty_pages_size: This operation will query for internal size.
 * @get_dirty_pages: This operation will query for dirty pages.
 * The hypervisor will be able to collect these dirty pages using the
 * dma key provided upon opening.
 */
struct snap_migration_ops {
	int (*quiesce)(void *data);
	int (*unquiesce)(void *data);
	int (*freeze)(void *data);
	int (*unfreeze)(void *data);
	int (*get_state_size)(void *data);
	int (*copy_state)(void *data, void *buff, int len,
			  bool copy_from_buffer);
	int (*start_dirty_pages_track)(void *data);
	int (*stop_dirty_pages_track)(void *data);
};

#define SNAP_CHANNEL_RDMA_IP "SNAP_RDMA_IP"
#define SNAP_CHANNEL_RDMA_PORT "SNAP_RDMA_PORT"

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
#define SNAP_CHANNEL_INITIAL_BITMAP_ARRAY_SZ 1048576

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
 *                     factor when required.
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
 * struct snap_channel - internal struct holds the information of the
 *                       communication channel
 *
 * @ops: migration ops struct of functions that contains the basic migration
 *       operations (provided by the controller).
 * @data: controller_data that will be associated with the
 *        caller or application.
 * @dirty_pages: dirty pages struct, used to track dirty pages.
 */
struct snap_channel {
	const struct snap_migration_ops		*ops;
	void					*data;
	struct snap_dirty_pages			dirty_pages;

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

struct snap_channel *snap_channel_open(struct snap_migration_ops *ops,
				       void *data);
void snap_channel_close(struct snap_channel *schannel);
int snap_channel_mark_dirty_page(struct snap_channel *schannel, uint64_t guest_pa,
				 int length);

#endif
