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
#define _GNU_SOURCE
#include <stdio.h>
#include <libflexio/flexio_elf.h>
#include <libflexio/flexio.h>

#include "snap_dpa.h"

/**
 * snap_dpa_app_create() - create DPA application process
 * @ctx:         snap context
 * @app_name:    application name
 *
 * The function creates DPA application process and performs common
 * initialization steps.
 *
 * Application image is loaded from the path given by LIBSNAP_DPA_DIR
 * or from the current working directory
 *
 * Code common to the virtio/nvme dpa queue implementation will go here
 *
 * TODO: singleton management code
 *
 * Return:
 * dpa conxtext on sucess or NULL on failure
 */
struct snap_dpa_ctx *snap_dpa_app_create(struct snap_context *ctx, const char *app_name)
{
	char *file_name;
	flexio_status st;
	int ret;
	int len;
	void *app_buf;
	size_t app_size;
	uint64_t entry_point, sym_size;
	struct snap_dpa_ctx *dpa_ctx;

	/* TODO: go over the list of apps, if one is already created
	 * use it
	 */
	if (getenv("LIBSNAP_DPA_DIR"))
		len = asprintf(&file_name, "%s/%s", getenv("LIBSNAP_DPA_DIR"),
			       app_name);
	else
		len = asprintf(&file_name, "%s", app_name);

	if (len < 0) {
		snap_error("Failed to allocate memory\n");
		return NULL;
	}

	st = flexio_get_elf_file(file_name, &app_buf, &app_size);
	free(file_name);
	if (st != FLEXIO_STATUS_SUCCESS) {
		snap_error("Failed to find %s\n", app_name);
		return NULL;
	}

	ret = flexio_get_elf_func_sym_val(app_buf, SNAP_DPA_THREAD_ENTRY_POINT,
					  &entry_point, &sym_size);
	if (ret) {
		snap_error("%s: has no snap entry point %s\n", app_name, SNAP_DPA_THREAD_ENTRY_POINT);
		return NULL;
	}

	dpa_ctx = calloc(1, sizeof(*dpa_ctx));
	if (!dpa_ctx) {
		snap_error("%s: Failed to allocate memory for DPA context\n", app_name);
		return NULL;
	}

	dpa_ctx->pd = ibv_alloc_pd(ctx->context);
	if (!dpa_ctx->pd) {
		errno = -ENOMEM;
		snap_error("%s: Failed to allocate pd for DPA context\n", app_name);
		goto free_dpa_ctx;
	}

	st = flexio_process_create(ctx->context, app_buf, app_size, &dpa_ctx->dpa_proc);
	if (st != FLEXIO_STATUS_SUCCESS) {
		snap_error("%s: Failed to create DPA process\n", app_name);
		goto free_dpa_pd;
	}

	dpa_ctx->entry_point = entry_point;
	return dpa_ctx;

free_dpa_pd:
	ibv_dealloc_pd(dpa_ctx->pd);
free_dpa_ctx:
	free(dpa_ctx);
	return NULL;
}

/**
 * snap_dpa_app_destroy() - destroy snap DPA application
 * @app:  DPA application context
 *
 * The function destroys DPA process and performs common cleanup tasks
 */
void snap_dpa_app_destroy(struct snap_dpa_ctx *app)
{
	ibv_dealloc_pd(app->pd);
	flexio_process_destroy(app->dpa_proc);
	free(app);
}
