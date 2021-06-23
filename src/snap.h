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

#define snap_min(a, b) (((a) < (b)) ? (a) : (b))
#define snap_max(a, b) (((a) > (b)) ? (a) : (b))

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
	SNAP_VIRTIO_FS_PF	= 1 << 6,
	SNAP_VIRTIO_FS_VF	= 1 << 7,
};

enum snap_emulation_type {
	SNAP_NVME	= 1 << 0,
	SNAP_VIRTIO_NET	= 1 << 1,
	SNAP_VIRTIO_BLK	= 1 << 2,
	SNAP_VIRTIO_FS	= 1 << 3,
	SNAP_VF		= 1 << 4,
};

enum snap_device_attr_flags {
	SNAP_DEVICE_FLAGS_EVENT_CHANNEL = 1 << 0,
	SNAP_DEVICE_FLAGS_VF_DYN_MSIX	= 1 << 1,
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
	SNAP_EVENT_VIRTIO_FS_DEVICE_CHANGE,
	SNAP_EVENT_VIRTIO_FS_QUEUE_CHANGE,
};

struct snap_event {
	enum snap_event_type		type;
	void				*obj;
};

struct snap_device_attr {
	struct ibv_context *context;
	enum snap_pci_type type;
	int         pf_id;
	int         vf_id;
	uint32_t    flags; /* Use enum snap_device_attr_flags */
	uint32_t    counter_set_id;

	/* for live migration */
	struct snap_migration_ops	*ops;
	void				*mig_data;
};

struct mlx5_klm {
	uint32_t byte_count;
	uint32_t mkey;
	uint64_t address;
};

struct mlx5_devx_mkey_attr {
	uint64_t addr;
	uint64_t size;
	uint32_t log_entity_size;
	uint32_t relaxed_ordering_write:1;
	uint32_t relaxed_ordering_read:1;
	struct mlx5_klm *klm_array;
	int klm_num;
};

struct snap_cross_mkey {
	struct mlx5dv_devx_obj *devx_obj;
	uint32_t mkey;
};

struct snap_indirect_mkey {
	struct mlx5dv_devx_obj *devx_obj;
	uint32_t mkey;
	struct mlx5_klm *klm_array;
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
	uint16_t			num_of_vfs;
};

union snap_pci_bdf {
	uint16_t raw;
	struct {
		uint16_t function : 3;
		uint16_t device : 5;
		uint16_t bus : 8;
	} bdf;
};

struct snap_relaxed_ordering_caps {
	bool relaxed_ordering_write_pci_enabled;
	bool relaxed_ordering_write;
	bool relaxed_ordering_read;
	bool relaxed_ordering_write_umr;
	bool relaxed_ordering_read_umr;
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

	bool hotplugged;
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
	/* for BF-2 usage only */
	uint32_t			crossed_vhca_mkey;

	/* for live migration support */
	struct snap_channel		*channel;
};

struct __attribute__((packed)) snap_nvme_registers {
	union {
		uint64_t raw;
		struct {
			uint64_t mqes:16;
			uint64_t cqr:1;
			uint64_t ams:2;
			uint64_t:5;
			uint64_t to:8;
			uint64_t dstrd:4;
			uint64_t nssrs:1;
			uint64_t css:8;
			uint64_t bps:1;
			uint64_t:2;
			uint64_t mpsmin:4;
			uint64_t mpsmax:4;
			uint64_t:8;
		} bits;
	} cap;
	union {
		uint32_t raw;
		struct {
			uint32_t ter:8;
			uint32_t mnr:8;
			uint32_t mjr:16;
		} bits;
	} vs;
	uint32_t intms;
	uint32_t intmc;
	uint32_t cc;
	uint32_t csts;
	uint32_t nssr;
	uint32_t aqa;
	uint32_t asq;
	uint32_t acq;
	uint32_t cmbloc;
	uint32_t cmbsz;
	uint32_t bpinfo;
	uint32_t bprsel;
	uint32_t bpmbl;
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

#define SNAP_VIRTIO_FS_DEV_CFG_TAG_LEN 36

struct snap_virtio_fs_registers {
	uint64_t	device_features;
	// This is a 'depth' parameter from RPC call
	uint16_t	queue_size;

	uint8_t		tag[SNAP_VIRTIO_FS_DEV_CFG_TAG_LEN];
	uint16_t 	num_request_queues;
};

union snap_device_registers {
	struct snap_nvme_registers nvme;
	struct snap_virtio_net_registers virtio_net;
	struct snap_virtio_blk_registers virtio_blk;
	struct snap_virtio_fs_registers  virtio_fs;
};

struct snap_hotplug_attr {
	enum snap_emulation_type	type;
	struct snap_pci_attr		pci_attr;
	bool				use_default_regs;
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
	uint64_t				pf_mac;
	struct snap_pci				*pfs;
	bool					dirty;
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
	bool		crossing_vhca_mkey;
	bool		queue_period_upon_cqe;
	bool		queue_period_upon_event;

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
	uint16_t	min_num_vf_dynamic_msix;
	uint16_t	max_num_vf_dynamic_msix;
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
	struct snap_pfs_ctx			virtio_fs_pfs;
	struct snap_virtio_caps			virtio_fs_caps;

	bool					hotplug_supported;
	struct snap_hotplug_context		hotplug;

	pthread_mutex_t				lock;
	TAILQ_HEAD(, snap_device)		device_list;
	pthread_mutex_t				hotplug_lock;
	TAILQ_HEAD(, snap_hotplug_device)	hotplug_device_list;
};

#define SNAP_ALIGN_FLOOR(val, align) \
	(typeof(val))((val) & (~((typeof(val))((align) - 1))))

#define SNAP_ALIGN_CEIL(val, align) \
	SNAP_ALIGN_FLOOR(((val) + ((typeof(val)) (align) - 1)), align)

void snap_close_device(struct snap_device *sdev);
struct snap_device *snap_open_device(struct snap_context *sctx,
		struct snap_device_attr *attr);

struct snap_context *snap_open(struct ibv_device *ibdev);
void snap_close(struct snap_context *sctx);

int snap_get_pf_list(struct snap_context *sctx, enum snap_emulation_type type,
		struct snap_pci **pfs);

struct snap_pci *snap_hotplug_pf(struct snap_context *sctx,
		struct snap_hotplug_attr *attr);
int snap_hotunplug_pf(struct snap_pci *pf);

int snap_rescan_vfs(struct snap_pci *pf, size_t num_vfs);

int snap_device_get_fd(struct snap_device *sdev);
int snap_device_get_events(struct snap_device *sdev, int num_events,
			   struct snap_event *events);

struct snap_cross_mkey *snap_create_cross_mkey(struct ibv_pd *pd,
					       struct snap_device *target_sdev);
int snap_destroy_cross_mkey(struct snap_cross_mkey *mkey);

struct snap_indirect_mkey *
snap_create_indirect_mkey(struct ibv_pd *pd,
			  struct mlx5_devx_mkey_attr *attr);
int
snap_destroy_indirect_mkey(struct snap_indirect_mkey *mkey);

void snap_update_pci_bdf(struct snap_pci *spci, uint16_t pci_bdf);

int snap_query_relaxed_ordering_caps(struct ibv_context *context,
				     struct snap_relaxed_ordering_caps *caps);
#endif
