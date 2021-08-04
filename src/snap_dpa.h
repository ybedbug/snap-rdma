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

#ifndef _SNAP_DPA_H
#define _SNAP_DPA_H

#include <libflexio/flexio.h>

#include "snap.h"

#define SNAP_DPA_THREAD_ENTRY_POINT "__snap_dpa_thread_start"

struct snap_dpa_ctx {
	struct snap_context    *sctx;
	struct flexio_process  *dpa_proc;
	struct ibv_pd          *pd;
	uint64_t               entry_point;
};

struct snap_dpa_ctx *snap_dpa_app_create(struct snap_context *ctx, const char *app_name);
void snap_dpa_app_destroy(struct snap_dpa_ctx *app);

#endif
