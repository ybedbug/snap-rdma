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

#include "mlx5_ifc.h"

#define SNAP_FT_ROOT_LEVEL 5
#define SNAP_FT_LOG_SIZE 10

struct mlx5_snap_device;

struct mlx5_snap_nvme_context {
	int		supported_types;//mask of snap_nvme_queue_type
	uint32_t	max_nvme_namespaces;
	uint32_t	max_emulated_nvme_cqs;
	uint32_t	max_emulated_nvme_sqs;
	uint16_t	reg_size;
};

struct mlx5_snap_context {
	struct mlx5_snap_nvme_context	nvme;
	uint8_t				max_ft_level;
	uint8_t				log_max_ft_size;

	bool				need_tunnel;
};

struct mlx5_snap_pci {
	int				vhca_id;
	int				vfs_base_vhca_id;
};

struct mlx5_snap_devx_obj {
	struct mlx5dv_devx_obj		*obj;
	u32				obj_id;
	struct snap_device		*sdev;

	/* destructor for tunneld objects */
	struct mlx5_snap_devx_obj	*vtunnel;
	void				*dtor_in;
	int				inlen;
	void				*dtor_out;
	int				outlen;
};

struct mlx5_snap_flow_table {
	struct mlx5_snap_devx_obj       *ft;
	uint32_t			table_id;
	uint32_t			table_type;
	uint8_t				level;
	uint64_t			ft_size;
};

struct mlx5_snap_device {
	struct mlx5_snap_devx_obj	*device_emulation;
	struct mlx5_snap_devx_obj	*vtunnel;
	struct mlx5_snap_flow_table	*tx;
	struct mlx5_snap_flow_table	*rx;
};

/* The following functions are to be used by NVMe/VirtIO libraries only */
int snap_init_device(struct snap_device *sdev);
int snap_teardown_device(struct snap_device *sdev);
struct mlx5_snap_devx_obj*
snap_devx_obj_create(struct snap_device *sdev, void *in, size_t inlen,
		void *out, size_t outlen, struct mlx5_snap_devx_obj *vtunnel,
		size_t dtor_inlen, size_t dtor_outlen);
int snap_devx_obj_destroy(struct mlx5_snap_devx_obj *snap_obj);

#endif
