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

#ifndef MLX5_SNAP_H
#define MLX5_SNAP_H

#include <stdlib.h>

#include <infiniband/verbs.h>
#include <infiniband/mlx5dv.h>
#include <pthread.h>
#include <linux/types.h>

#include <snap.h>

#include "mlx5_ifc.h"
#include "queue.h"

struct mlx5_snap_device;
struct mlx5_snap_context;

enum mlx5_snap_pci_type {
	MLX5_SNAP_PF	= 1 << 0,
	MLX5_SNAP_VF	= 1 << 1,
};

struct mlx5_snap_pci {
	enum mlx5_snap_pci_type		type;
	int				pci_number;
	int				vhca_id;
	int				num_vfs;
	int				vfs_base_vhca_id;
	struct mlx5_snap_pci		*vfs;// VFs array for PF

	struct mlx5_snap_context	*mctx;
	struct mlx5_snap_pci		*pf;//parent PF for VFs
};

struct mlx5_snap_context {
	struct snap_context		sctx;
	pthread_mutex_t			lock;
	TAILQ_HEAD(, mlx5_snap_device)	device_list;

	int				max_pfs;
	struct mlx5_snap_pci		*pfs;
};

struct mlx5_snap_device {
	struct snap_device		sdev;
	TAILQ_ENTRY(mlx5_snap_device)	entry;

	struct mlx5dv_devx_obj		*device_emulation;
	u8				obj_id;

	struct mlx5_snap_context	*mctx;
	struct mlx5_snap_pci		*pci;
};

static inline struct mlx5_snap_device*
to_mlx5_snap_device(struct snap_device *sdev)
{
    return container_of(sdev, struct mlx5_snap_device, sdev);
}

static inline struct mlx5_snap_context*
to_mlx5_snap_context(struct snap_context *sctx)
{
    return container_of(sctx, struct mlx5_snap_context, sctx);
}

#endif
