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

enum snap_pci_type {
	SNAP_NVME_PF		= 1 << 0,
	SNAP_NVME_VF		= 1 << 1,
	SNAP_VIRTIO_BLK_PF	= 1 << 2,
	SNAP_VIRTIO_BLK_VF	= 1 << 3,
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

struct snap_context {
	struct ibv_context		*context;

	int				max_pfs;
	struct snap_pci			*pfs;

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

/* The following functions are to be used by NVMe/VirtIO libraries only */
int snap_init_device(struct snap_device *sdev);
int snap_teardown_device(struct snap_device *sdev);
struct mlx5_snap_devx_obj*
snap_devx_obj_create(struct snap_device *sdev, void *in, size_t inlen,
		void *out, size_t outlen, struct mlx5_snap_devx_obj *vtunnel,
		size_t dtor_inlen, size_t dtor_outlen);
int snap_devx_obj_destroy(struct mlx5_snap_devx_obj *snap_obj);

#endif
