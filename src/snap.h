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

#include <infiniband/verbs.h>

#include "queue.h"
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

enum snap_pci_type {
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
};

struct snap_context;

struct snap_device_attr {
	enum snap_pci_type	type;
	int			pf_id;
	int			vf_id;
};

struct snap_pci {
	struct snap_context	*sctx;
	enum snap_pci_type	type;
	bool			plugged;
	int			id;
	int			pci_number;

	int			num_vfs;
	struct snap_pci		*vfs;// VFs array for PF
	struct snap_pci		*parent;//parent PF for VFs

	struct mlx5_snap_pci	mpci;
};

struct snap_device {
	struct snap_context		*sctx;
	struct snap_pci			*pci;

	TAILQ_ENTRY(snap_device)	entry;

	struct mlx5_snap_device		mdev;

	void				*dd_data;
};

struct snap_hotplug_context {
	int		supported_types;//mask of snap_emulation_type
	uint8_t		max_devices;
	uint8_t		log_max_bar_size;
};

struct snap_context {
	struct ibv_context		*context;
	int				emulation_caps; //mask for supported snap_emulation_types
	struct mlx5_snap_context	mctx;

	int				max_nvme_pfs;
	struct snap_pci			*nvme_pfs;

	int				max_virtio_net_pfs;
	struct snap_pci			*virtio_net_pfs;

	int				max_virtio_blk_pfs;
	struct snap_pci			*virtio_blk_pfs;

	bool				hotplug_supported;
	struct snap_hotplug_context	hotplug;

	pthread_mutex_t			lock;
	TAILQ_HEAD(, snap_device)	device_list;
};

void snap_close_device(struct snap_device *sdev);
struct snap_device *snap_open_device(struct snap_context *sctx,
		struct snap_device_attr *attr);

struct snap_context *snap_open(struct ibv_device *ibdev);
void snap_close(struct snap_context *sctx);

int snap_get_pf_list(struct snap_context *sctx, enum snap_pci_type type,
		struct snap_pci **pfs);

#endif
