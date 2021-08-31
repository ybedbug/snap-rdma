/*
 * Copyright © 2021 NVIDIA CORPORATION & AFFILIATES. ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of Nvidia Corporation and its affiliates
 * (the "Company") and all right, title, and interest in and to the software
 * product, including all associated intellectual property rights, are and
 * shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 */

#ifndef _SNAP_DPA_H
#define _SNAP_DPA_H

#include <libflexio/flexio.h>

#include "snap.h"
#include "snap_dpa_common.h"

struct snap_dpa_ctx {
	struct snap_context    *sctx;
	struct flexio_process  *dpa_proc;
	struct ibv_pd          *pd;
	uint64_t               entry_point;
};

struct snap_dpa_ctx *snap_dpa_process_create(struct snap_context *ctx, const char *app_name);
void snap_dpa_process_destroy(struct snap_dpa_ctx *app);

enum {
	SNAP_DPA_THREAD_ATTR_POLLING = 0x1
};

/**
 * struct snap_dpa_thread_attr - DPA thread attributes
 *
 * At the moment attributes are not used yet
 */
struct snap_dpa_thread_attr {
	/* private: */
	uint32_t thread_attr;
	uint32_t cpu_mask[16]; // hart, best effort according to PRM
};

struct snap_dpa_thread {
	struct snap_dpa_ctx   *dctx;
	struct flexio_thread  *dpa_thread;
	struct flexio_window  *cmd_window;
	void                  *cmd_mbox;
	pthread_mutex_t       cmd_lock;
	struct ibv_mr         *cmd_mr;
};

struct snap_dpa_thread *snap_dpa_thread_create(struct snap_dpa_ctx *dctx,
		struct snap_dpa_thread_attr *attr);
void snap_dpa_thread_destroy(struct snap_dpa_thread *thr);

void *snap_dpa_thread_mbox_acquire(struct snap_dpa_thread *thr);
void snap_dpa_thread_mbox_release(struct snap_dpa_thread *thr);

#endif
