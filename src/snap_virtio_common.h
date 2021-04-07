/*
 * Copyright (c) 2019 Mellanox Technologies, Inc.  All rights reserved.
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

#ifndef SNAP_VIRTIO_COMMON_H
#define SNAP_VIRTIO_COMMON_H

#include <stdlib.h>
#include <infiniband/verbs.h>
#include <infiniband/mlx5dv.h>
#include <pthread.h>
#include <linux/types.h>
#include <linux/virtio_ring.h>

#include "snap.h"

#define SNAP_VIRTIO_UMEM_ALIGN 4096

enum snap_virtq_type {
	SNAP_VIRTQ_SPLIT_MODE	= 1 << 0,
	SNAP_VIRTQ_PACKED_MODE	= 1 << 1,
};

enum snap_virtq_event_mode {
	SNAP_VIRTQ_NO_MSIX_MODE	= 1 << 0,
	SNAP_VIRTQ_QP_MODE	= 1 << 1,
	SNAP_VIRTQ_MSIX_MODE	= 1 << 2,
};

enum snap_virtq_offload_type {
	SNAP_VIRTQ_OFFLOAD_ETH_FRAME	= 1 << 0,
	SNAP_VIRTQ_OFFLOAD_DESC_TUNNEL	= 1 << 1,
};

enum snap_virtq_state {
	SNAP_VIRTQ_STATE_INIT		= 1 << 0,
	SNAP_VIRTQ_STATE_RDY		= 1 << 1,
	SNAP_VIRTQ_STATE_SUSPEND	= 1 << 2,
	SNAP_VIRTQ_STATE_ERR		= 1 << 3,
};

enum snap_virtio_features {
	SNAP_VIRTIO_NET_F_CSUM		= 1ULL << 0,
	SNAP_VIRTIO_NET_F_GUEST_CSUM	= 1ULL << 1,
	SNAP_VIRTIO_NET_F_HOST_TSO4	= 1ULL << 11,
	SNAP_VIRTIO_NET_F_HOST_TSO6	= 1ULL << 12,
	SNAP_VIRTIO_NET_F_CTRL_VQ	= 1ULL << 17,
	SNAP_VIRTIO_F_VERSION_1		= 1ULL << 32,
};

enum snap_virtq_period_mode {
	SNAP_VIRTQ_PERIOD_DEFAULT	= 0,
	SNAP_VIRTQ_PERIOD_UPON_EVENT	= 1,
	SNAP_VIRTQ_PERIOD_UPON_CQE	= 2,
};

struct snap_virtio_queue_attr {
	enum snap_virtq_type		type;
	enum snap_virtq_event_mode	ev_mode;
	bool				full_emulation;
	bool				virtio_version_1_0;
	enum snap_virtq_offload_type	offload_type;
	uint16_t			max_tunnel_desc;
	uint32_t			event_qpn_or_msix;
	uint16_t			idx;
	uint16_t			size;
	uint16_t			msix_vector;
	uint16_t			enable;
	uint16_t			notify_off;
	uint64_t			desc;
	uint64_t			driver;
	uint64_t			device;
	struct ibv_pd			*pd;
	uint32_t			ctrs_obj_id;

	enum snap_virtq_state		state; /* query and modify */

	enum snap_virtq_period_mode	queue_period_mode;
	uint16_t			queue_period;
	uint16_t			queue_max_count;

	/* Query: */
	uint32_t			dma_mkey;
};

struct snap_virtio_queue_counters_attr {
	uint64_t			received_desc;
	uint64_t			completed_desc;
	uint32_t			error_cqes;
	uint32_t			bad_desc_errors;
	uint32_t			exceed_max_chain;
	uint32_t			invalid_buffer;
};

struct snap_virtio_queue_debugstat {
	uint32_t				qid;
	uint32_t				hw_available_index;
	uint32_t				sw_available_index;
	uint32_t				hw_used_index;
	uint32_t				sw_used_index;
	uint64_t				hw_received_descs;
	uint64_t				hw_completed_descs;
};

struct snap_virtio_ctrl_debugstat {
	uint32_t				network_error;
	uint32_t				bad_descriptor_error;
	uint32_t				invalid_buffer;
	uint32_t				desc_list_exceed_limit;
	uint32_t				internal_error;
	size_t					num_queues;
	struct snap_virtio_queue_debugstat	*queues;
};

struct snap_virtio_umem {
	void			*buf;
	int			size;
	struct mlx5dv_devx_umem *devx_umem;
};

struct snap_virtio_queue {
	uint32_t				idx;
	struct mlx5_snap_devx_obj		*virtq;
	struct snap_virtio_umem			umem[3];
	uint64_t				mod_allowed_mask;
	struct mlx5_snap_devx_obj		*ctrs_obj;

	struct snap_cross_mkey			*snap_cross_mkey;
};

enum snap_virtio_dev_modify {
	SNAP_VIRTIO_MOD_DEV_STATUS = 1 << 0,
	SNAP_VIRTIO_MOD_LINK_STATUS = 1 << 1,
	SNAP_VIRTIO_MOD_RESET = 1 << 2,
	SNAP_VIRTIO_MOD_PCI_COMMON_CFG = 1 << 3,
	SNAP_VIRTIO_MOD_DEV_CFG = 1 << 4,
	SNAP_VIRTIO_MOD_ALL = 1 << 6,
	SNAP_VIRTIO_MOD_QUEUE_CFG = 1 << 7,
};

struct snap_virtio_device_attr {
	uint64_t			device_feature;
	uint64_t			driver_feature;
	uint16_t			msix_config;
	uint16_t			max_queues;
	uint16_t			max_queue_size;
	uint16_t			pci_bdf;
	uint8_t				status;
	bool				enabled;
	bool				reset;
	uint16_t			num_of_vfs;
	uint8_t				config_generation;
	uint8_t				device_feature_select;
	uint8_t				driver_feature_select;
	uint16_t			queue_select;
};

void snap_virtio_get_queue_attr(struct snap_virtio_queue_attr *vattr,
	void *q_configuration);
void snap_virtio_get_device_attr(struct snap_device *sdev,
				 struct snap_virtio_device_attr *vattr,
				 void *device_configuration);
int snap_virtio_query_device(struct snap_device *sdev,
	enum snap_emulation_type type, uint8_t *out, int outlen);
int snap_virtio_modify_device(struct snap_device *sdev,
		enum snap_emulation_type type, uint64_t mask,
		struct snap_virtio_device_attr *attr);


struct mlx5_snap_devx_obj*
snap_virtio_create_queue_counters(struct snap_device *sdev);
struct mlx5_snap_devx_obj*
snap_virtio_create_queue(struct snap_device *sdev,
	struct snap_virtio_queue_attr *attr, struct snap_virtio_umem *umem);
int snap_virtio_query_queue(struct snap_virtio_queue *virtq,
	struct snap_virtio_queue_attr *vattr);
int snap_virtio_modify_queue(struct snap_virtio_queue *virtq, uint64_t mask,
	struct snap_virtio_queue_attr *vattr);

int snap_virtio_init_virtq_umem(struct snap_context *sctx,
				struct snap_virtio_caps *virtio,
				struct snap_virtio_queue *virtq,
				int depth);
void snap_virtio_teardown_virtq_umem(struct snap_virtio_queue *virtq);

int snap_virtio_get_vring_indexes_from_host(struct ibv_pd *pd, uint64_t drv_addr,
					    uint64_t dev_addr, uint32_t dma_mkey,
					    struct vring_avail *vra,
					    struct vring_used *vru);
int snap_virtio_query_queue_counters(struct mlx5_snap_devx_obj *counters_obj,
				struct snap_virtio_queue_counters_attr *attr);
#endif
