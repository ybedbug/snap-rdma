/*
 * Copyright (c) 2012 Mellanox Technologies, Inc.  All rights reserved.
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

#ifndef SNAP_H
#define SNAP_H

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <sys/queue.h>

#include <infiniband/verbs.h>

#include "mlx5_snap.h"

#define PFX "snap: "

#ifndef offsetof
#define offsetof(t, m) ((size_t) &((t *)0)->m)
#endif

#ifndef container_of
#define container_of(ptr, type, member) ({ \
		typeof(((type *)0)->member)*__mptr = (ptr); \
		(type *)((char *)__mptr - offsetof(type, member)); })
#endif

#define snap_min(a,b) (((a)<(b))?(a):(b))
#define snap_max(a,b) (((a)>(b))?(a):(b))

#define snap_likely(x) __builtin_expect(!!(x), 1)
#define snap_unlikely(x) __builtin_expect(!!(x), 0)

#ifndef SNAP_DEBUG
#define SNAP_DEBUG 0
#endif

#define snap_debug(fmt, ...) \
	do { if (SNAP_DEBUG) \
		printf("%s:%d " fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
	} while (0)

// TODO: Add formal logger
#define snap_error printf
#define snap_warn  printf
#define snap_info  printf


enum snap_pci_type {
	SNAP_NONE_PF		= 0,
	SNAP_NVME_PF		= 1 << 0,
	SNAP_NVME_VF		= 1 << 1,
	SNAP_VIRTIO_NET_PF	= 1 << 2,
	SNAP_VIRTIO_NET_VF	= 1 << 3,
	SNAP_VIRTIO_BLK_PF	= 1 << 4,
	SNAP_VIRTIO_BLK_VF	= 1 << 5,
};

enum snap_emulation_type {
	SNAP_NVME	= 1 << 0,
	SNAP_VIRTIO_NET	= 1 << 1,
	SNAP_VIRTIO_BLK	= 1 << 2,
	SNAP_VF		= 1 << 3,
};

enum snap_device_attr_flags {
	SNAP_DEVICE_FLAGS_EVENT_CHANNEL = 1 << 0,
};

struct snap_context;
struct snap_hotplug_device;

enum snap_event_type {
	SNAP_EVENT_NVME_DEVICE_CHANGE,
	SNAP_EVENT_NVME_SQ_CHANGE,
	SNAP_EVENT_VIRTIO_NET_DEVICE_CHANGE,
	SNAP_EVENT_VIRTIO_NET_QUEUE_CHANGE,
	SNAP_EVENT_VIRTIO_BLK_DEVICE_CHANGE,
	SNAP_EVENT_VIRTIO_BLK_QUEUE_CHANGE,
};

struct snap_event {
	enum snap_event_type		type;
	void				*obj;
};

struct snap_device_attr {
	enum snap_pci_type	type;
	int			pf_id;
	int			vf_id;
	uint32_t		flags; /* Use enum snap_device_attr_flags */
};

struct snap_pci_bar {
	void				*data;
	unsigned int			size;
};

struct snap_pci_attr {
	uint16_t			device_id;
	uint16_t			vendor_id;
	uint8_t				revision_id;
	uint32_t			class_code;
	uint16_t			subsystem_id;
	uint16_t			subsystem_vendor_id;
	uint16_t			num_msix;
};

union snap_pci_bdf {
	uint16_t raw;
	struct {
		uint16_t function : 3;
		uint16_t device : 5;
		uint16_t bus : 8;
	} bdf;
};

struct snap_pci {
	struct snap_context		*sctx;
	enum snap_pci_type		type;
	struct snap_pci_attr		pci_attr;
	struct snap_pci_bar		bar;
	bool				plugged;
	int				id;
	union snap_pci_bdf		pci_bdf;
	char				pci_number[16];

	int				num_vfs;
	struct snap_pci			*vfs;// VFs array for PF
	struct snap_pci			*parent;//parent PF for VFs

	struct mlx5_snap_pci		mpci;
	struct snap_hotplug_device	*hotplug;
};

struct snap_device {
	struct snap_context		*sctx;
	struct snap_pci			*pci;

	TAILQ_ENTRY(snap_device)	entry;

	struct mlx5_snap_device		mdev;

	uint64_t			mod_allowed_mask;
	void				*dd_data;

	/* for BF-1 usage only */
	uint32_t			dma_rkey;
};

struct snap_nvme_registers {
	uint8_t			regs[0x50];
};

struct snap_virtio_net_registers {
	uint64_t	device_features;
	uint16_t	queue_size;

	uint64_t	mac;
	uint16_t	status;
	uint16_t	max_queues;
	uint16_t	mtu;
};

struct snap_virtio_blk_registers {
	uint64_t	device_features;
	uint16_t	max_queues;
	uint16_t	queue_size;

	uint64_t	capacity;
	uint32_t	size_max;
	uint32_t	seg_max;
	uint16_t	cylinders;
	uint8_t		heads;
	uint8_t		sectors;
	uint32_t	blk_size;
	uint8_t		physical_blk_exp;
	uint8_t		alignment_offset;
	uint16_t	min_io_size;
	uint32_t	opt_io_size;
	uint8_t		writeback;
	uint32_t	max_discard_sectors;
	uint32_t	max_discard_seg;
	uint32_t	discard_sector_alignment;
	uint32_t	max_write_zeroes_sectors;
	uint32_t	max_write_zeroes_segs;
	uint8_t		write_zeroes_may_unmap;
};

union snap_device_registers {
	struct snap_nvme_registers nvme;
	struct snap_virtio_net_registers virtio_net;
	struct snap_virtio_blk_registers virtio_blk;
};

struct snap_hotplug_attr {
	enum snap_emulation_type	type;
	struct snap_pci_attr		pci_attr;
	union snap_device_registers	regs;
	uint16_t			max_vfs;
};

struct snap_hotplug_device {
	struct mlx5_snap_devx_obj		*obj;
	enum snap_emulation_type		type;
	struct snap_pci				*pf;

	TAILQ_ENTRY(snap_hotplug_device)	entry;
};

struct snap_hotplug_context {
	int		supported_types;//mask of snap_emulation_type
	uint8_t		max_devices;
	uint8_t		log_max_bar_size;
	uint16_t	max_total_vfs;
};

struct snap_pfs_ctx {
	enum snap_emulation_type		type;
	int					max_pfs;
	struct snap_pci				*pfs;
};

struct snap_nvme_caps {
	int		supported_types;//mask of snap_nvme_queue_type
	uint32_t	max_nvme_namespaces;
	uint32_t	max_emulated_nvme_cqs;
	uint32_t	max_emulated_nvme_sqs;
	uint16_t	reg_size;
	bool		crossing_vhca_mkey;
};

struct snap_virtio_caps {
	int		supported_types;//mask of snap_virtq_type
	int		event_modes;//mask of snap_virtq_event_mode
	uint64_t	features; //mask of snap_virtio_features
	uint32_t	max_emulated_virtqs;
	uint16_t	max_tunnel_desc;

	/*
	 * According to PRM for each created virtq, one must provide 3 UMEMs:
	 * UMEM_i = umem_i_buffer_param_a * virtq_i_size + umem_i_buffer_param_b
	 */
	uint32_t	umem_1_buffer_param_a;
	uint32_t	umem_1_buffer_param_b;
	uint32_t	umem_2_buffer_param_a;
	uint32_t	umem_2_buffer_param_b;
	uint32_t	umem_3_buffer_param_a;
	uint32_t	umem_3_buffer_param_b;
};

struct snap_context {
	struct ibv_context			*context;
	int					emulation_caps; //mask for supported snap_emulation_types
	struct mlx5_snap_context		mctx;

	struct snap_pfs_ctx			nvme_pfs;
	struct snap_nvme_caps			nvme_caps;
	struct snap_pfs_ctx			virtio_net_pfs;
	struct snap_virtio_caps			virtio_net_caps;
	struct snap_pfs_ctx			virtio_blk_pfs;
	struct snap_virtio_caps			virtio_blk_caps;

	bool					hotplug_supported;
	struct snap_hotplug_context		hotplug;

	pthread_mutex_t				lock;
	TAILQ_HEAD(, snap_device)		device_list;
	pthread_mutex_t				hotplug_lock;
	TAILQ_HEAD(, snap_hotplug_device)	hotplug_device_list;
};

void snap_close_device(struct snap_device *sdev);
struct snap_device *snap_open_device(struct snap_context *sctx,
		struct snap_device_attr *attr);

struct snap_context *snap_open(struct ibv_device *ibdev);
void snap_close(struct snap_context *sctx);

int snap_get_pf_list(struct snap_context *sctx, enum snap_emulation_type type,
		struct snap_pci **pfs);

struct snap_pci *snap_hotplug_pf(struct snap_context *sctx,
		struct snap_hotplug_attr *attr);
void snap_hotunplug_pf(struct snap_pci *pf);

int snap_rescan_vfs(struct snap_pci *pf, size_t num_vfs);

int snap_device_get_fd(struct snap_device *sdev);
int snap_device_get_events(struct snap_device *sdev, int num_events,
			   struct snap_event *events);

inline int snap_get_vhca_id(struct snap_device *sdev);
#endif
