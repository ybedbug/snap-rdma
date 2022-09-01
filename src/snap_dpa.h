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

#include <stdint.h>
#include <stdbool.h>
/* for cpu_set_t */
#include <sched.h>
#if HAVE_FLEXIO
#include <libflexio/flexio.h>

/* internal flexio structs that are not exposed */
struct flexio_eq;

struct flexio_eq_attr {
	uint8_t log_eq_ring_depth;
	uint32_t uar_id;
};

flexio_status flexio_eq_create(struct flexio_process *process, struct ibv_context *ibv_ctx,
			       struct flexio_eq_attr *attr, struct flexio_eq **eq);
flexio_status flexio_eq_destroy(struct flexio_eq *eq);
struct flexio_hw_eq *flexio_eq_get_hw_eq(struct flexio_eq *eq);

#endif

#if !__DPA
#include <infiniband/verbs.h>
#endif

#include "snap_dpa_common.h"

bool snap_dpa_enabled(struct ibv_context *ctx);

struct snap_dpa_ctx {
	struct flexio_process  *dpa_proc;
	struct flexio_eq       *dpa_eq;
	struct flexio_outbox   *dpa_uar;
	struct ibv_pd          *pd;
	struct snap_uar        *uar;
	struct snap_dma_q      *dma_q;
	struct snap_dma_q      *dummy_q;
	struct snap_dpa_mkeyh  *dma_mkeyh;
	struct flexio_uar      *flexio_uar;
};

struct snap_dpa_memh {
	struct snap_dpa_ctx *dctx;
	uint64_t va;
	size_t size;
};

struct snap_dpa_memh *snap_dpa_mem_alloc(struct snap_dpa_ctx *dctx, size_t size);
uint64_t snap_dpa_mem_addr(struct snap_dpa_memh *mem);
void snap_dpa_mem_free(struct snap_dpa_memh *mem);

struct snap_dpa_mkeyh {
	struct flexio_mkey *mkey;
};

struct snap_dpa_mkeyh *snap_dpa_mkey_alloc(struct snap_dpa_ctx *ctx, struct ibv_pd *pd);
uint32_t snap_dpa_mkey_id(struct snap_dpa_mkeyh *h);
void snap_dpa_mkey_free(struct snap_dpa_mkeyh *h);

struct snap_dpa_ctx *snap_dpa_process_create(struct ibv_context *ctx, const char *app_name);
void snap_dpa_process_destroy(struct snap_dpa_ctx *app);
uint32_t snap_dpa_process_umem_id(struct snap_dpa_ctx *ctx);
uint64_t snap_dpa_process_umem_addr(struct snap_dpa_ctx *ctx);
uint64_t snap_dpa_process_umem_size(struct snap_dpa_ctx *ctx);
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
	size_t heap_size;
	/* pointer to a static cpu set. CPU_ALLOC may not work */
	cpu_set_t *hart_set;
	/* arbitrary user defined values */
	uint64_t user_arg;
	uint8_t user_flag;
};

struct snap_dpa_thread {
	struct snap_dpa_ctx   *dctx;
	struct flexio_event_handler *dpa_thread;
	struct flexio_window  *cmd_window;
	void                  *cmd_mbox;
	pthread_mutex_t       cmd_lock;
	struct ibv_mr         *cmd_mr;
	struct snap_dpa_memh  *mem;
	struct snap_dpa_log   *dpa_log;
	struct snap_dma_q     *trigger_q;
	struct snap_dma_q     *dummy_q;
};

struct snap_dpa_thread *snap_dpa_thread_create(struct snap_dpa_ctx *dctx,
		struct snap_dpa_thread_attr *attr);
void snap_dpa_thread_destroy(struct snap_dpa_thread *thr);
uint32_t snap_dpa_thread_id(struct snap_dpa_thread *thr);
uint64_t snap_dpa_thread_heap_base(struct snap_dpa_thread *thr);
struct snap_dpa_ctx *snap_dpa_thread_proc(struct snap_dpa_thread *thr);

void *snap_dpa_thread_mbox_acquire(struct snap_dpa_thread *thr);
void snap_dpa_thread_mbox_release(struct snap_dpa_thread *thr);

void snap_dpa_cmd_send(struct snap_dpa_thread *thr, struct snap_dpa_cmd *cmd, uint32_t type);
struct snap_dpa_rsp *snap_dpa_rsp_wait(void *mbox);

int snap_dpa_thread_wakeup(struct snap_dpa_thread *thr);

int snap_dpa_thread_mr_copy_sync(struct snap_dpa_thread *thr, uint64_t va, uint64_t len, uint32_t mkey);

struct snap_dpa_duar {
	struct mlx5dv_devx_obj *obj;
	uint32_t duar_id;
};

struct snap_dpa_duar *snap_dpa_duar_create(struct ibv_context *ctx, uint32_t dev_emu_id, uint32_t queue_id, uint32_t cq_num);
void snap_dpa_duar_destroy(struct snap_dpa_duar *duar);
uint32_t snap_dpa_duar_id(struct snap_dpa_duar *duar);

#endif
