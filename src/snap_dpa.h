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

#if HAVE_FLEXIO
#include <libflexio/flexio.h>
#endif

#include "snap.h"
#include "snap_dpa_common.h"

bool snap_dpa_enabled(struct ibv_context *ctx);

struct snap_dpa_ctx {
	struct flexio_process  *dpa_proc;
	struct flexio_eq       *dpa_eq;
	struct ibv_pd          *pd;
	struct snap_uar        *uar;
	uint64_t               entry_point;
};

struct snap_dpa_memh {
	uint64_t *va;
	size_t size;
};

struct snap_dpa_memh *snap_dpa_mem_alloc(struct snap_dpa_ctx *dctx, size_t size);
uint64_t snap_dpa_mem_addr(struct snap_dpa_memh *mem);
void snap_dpa_mem_free(struct snap_dpa_memh *mem);

struct snap_dpa_mkeyh {
	uint32_t *mkey_id;
};

struct snap_dpa_mkeyh *snap_dpa_mkey_alloc(struct snap_dpa_ctx *ctx, struct ibv_pd *pd);
uint32_t snap_dpa_mkey_id(struct snap_dpa_mkeyh *h);
void snap_dpa_mkey_free(struct snap_dpa_mkeyh *h);

struct snap_dpa_ctx *snap_dpa_process_create(struct ibv_context *ctx, const char *app_name);
void snap_dpa_process_destroy(struct snap_dpa_ctx *app);
uint32_t snap_dpa_process_umem_id(struct snap_dpa_ctx *ctx);
uint64_t snap_dpa_process_umem_addr(struct snap_dpa_ctx *ctx);
uint32_t snap_dpa_process_eq_id(struct snap_dpa_ctx *ctx);

/**
 * snap_dpa_umem_offset() - get virtual address umem offset
 * @proc:   dpa process context
 * @dpa_va: dpa virtual address
 *
 * Return: offset relative to the dpa process heap umem
 */
static inline uint64_t snap_dpa_process_umem_offset(struct snap_dpa_ctx *proc, uint64_t dpa_va)
{
	return dpa_va - snap_dpa_process_umem_addr(proc);
}

enum {
	SNAP_DPA_THREAD_ATTR_POLLING = 0x1
};

int snap_dpa_memcpy(struct snap_dpa_ctx *ctx, uint64_t dpa_va, void *src, size_t n);

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
	struct snap_dpa_memh  *mem;
};

struct snap_dpa_thread *snap_dpa_thread_create(struct snap_dpa_ctx *dctx,
		struct snap_dpa_thread_attr *attr);
void snap_dpa_thread_destroy(struct snap_dpa_thread *thr);
uint32_t snap_dpa_thread_id(struct snap_dpa_thread *thr);

void *snap_dpa_thread_mbox_acquire(struct snap_dpa_thread *thr);
void snap_dpa_thread_mbox_release(struct snap_dpa_thread *thr);

#define N_DPA_APP_WORKERS 1
/**
 * struct snap_dpa_app - snap DPA application
 *
 * a process that contains a set of 'worker' threads.
 * Each application is unique and started/stopped on 'demand'
 */
struct snap_dpa_app {
	/* private: */
	struct snap_dpa_ctx *dctx;
	struct snap_dpa_thread *dpa_workers[N_DPA_APP_WORKERS];
	int refcount;
	int n_workers;
	pthread_mutex_t lock;
};

#define SNAP_DPA_APP_INIT_ATTR { .refcount = 0, \
	.lock = PTHREAD_MUTEX_INITIALIZER }

/**
 * struct snap_dpa_app_attr - snap DPA application attributes
 * @ctx:  ibv context
 * @name: name of the file that contains application code
 * @n_workers: number of DPA worker threads to create
 */
struct snap_dpa_app_attr {
	struct ibv_context *ctx;
	const char *name;
	int n_workers;
};

int snap_dpa_app_start(struct snap_dpa_app *app,  struct snap_dpa_app_attr *attr);
void snap_dpa_app_stop(struct snap_dpa_app *app);

#endif
