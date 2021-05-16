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

#ifndef SNAP_INTERNAL_H
#define SNAP_INTERNAL_H
#include <stdio.h>
#include <stdint.h>
#include "snap.h"
#include "mlx5_ifc.h"

#define SNAP_ACCESS_KEY_LENGTH DEVX_FLD_SZ_BYTES(allow_other_vhca_access_in, access_key)

struct snap_alias_object {
	struct mlx5dv_devx_obj *obj;
	uint32_t obj_id;

	struct ibv_context *src_context;
	struct ibv_context *dst_context;
	uint32_t dst_obj_id;
	uint8_t access_key[SNAP_ACCESS_KEY_LENGTH];
};

/*
 * struct snap_compression_caps - compression and HW accelaration capabilities
 * @dma_mmo_supported: GGA engine support
 * @compress_supported: compression support
 * @decompress_supported: decompression support
 * @compress_min_block_size: compression minimal block size
 */
struct snap_compression_caps {
	bool dma_mmo_supported;
	bool compress_supported;
	bool decompress_supported;
	uint8_t compress_min_block_size:4;
};

int snap_allow_other_vhca_access(struct ibv_context *context,
				 enum mlx5_obj_type obj_type,
				 uint32_t obj_id,
				 uint8_t access_key[SNAP_ACCESS_KEY_LENGTH]);

struct snap_alias_object *
snap_create_alias_object(struct ibv_context *src_context,
			 enum mlx5_obj_type obj_type,
			 struct ibv_context *dst_context,
			 uint32_t dst_obj_id,
			 uint8_t access_key[SNAP_ACCESS_KEY_LENGTH]);
void snap_destroy_alias_object(struct snap_alias_object *obj);

int snap_query_compression_caps(struct ibv_context *context,
				struct snap_compression_caps *caps);
#endif
