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

#define ERR_ON_CMD(cmd, fmt, ...) \
	snap_error("queue:%d cmd_idx:%d err: " fmt, \
		   (cmd)->vq_priv->vq_ctx.common_ctx.idx, (cmd)->idx, ## __VA_ARGS__)

/* uncomment to enable fast path debugging */
// #define VIRTQ_DEBUG_DATA
#ifdef VIRTQ_DEBUG_DATA
#define virtq_log_data(cmd, fmt, ...) \
	printf("queue:%d cmd_idx:%d " fmt, (cmd)->vq_priv->vq_ctx.common_ctx.idx, (cmd)->idx, \
	       ## __VA_ARGS__)
#else
#define virtq_log_data(cmd, fmt, ...)
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

#endif

