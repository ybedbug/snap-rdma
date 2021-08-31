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

#define _GNU_SOURCE
#include <stdio.h>
#include <libflexio/flexio_elf.h>
#include <libflexio/flexio.h>

#include "snap_dpa.h"

/**
 * snap_dpa_process_create() - create DPA application process
 * @ctx:         snap context
 * @app_name:    application name
 *
 * The function creates DPA application process and performs common
 * initialization steps.
 *
 * Application image is loaded from the path given by LIBSNAP_DPA_DIR
 * or from the current working directory.
 *
 * Return:
 * dpa conxtext on sucess or NULL on failure
 */
struct snap_dpa_ctx *snap_dpa_process_create(struct snap_context *ctx, const char *app_name)
{
	char *file_name;
	flexio_status st;
	int ret;
	int len;
	void *app_buf;
	size_t app_size;
	uint64_t entry_point, sym_size;
	struct snap_dpa_ctx *dpa_ctx;

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
 * snap_dpa_process_destroy() - destroy snap DPA process
 * @ctx:  DPA context
 *
 * The function destroys DPA process and performs common cleanup tasks
 */
void snap_dpa_process_destroy(struct snap_dpa_ctx *ctx)
{
	ibv_dealloc_pd(ctx->pd);
	flexio_process_destroy(ctx->dpa_proc);
	free(ctx);
}

static void snap_dpa_thread_destroy_force(struct snap_dpa_thread *thr);

/**
 * snap_dpa_thread_create() - create DPA thread
 * @dctx:  DPA application context
 * @attr:  thread attributes
 *
 * The function creates a thread that runs on the DPA. On function return the
 * thread is running and ready to accept commands via its mailbox.
 *
 * Return:
 * dpa thread on success or NULL on failure
 */
struct snap_dpa_thread *snap_dpa_thread_create(struct snap_dpa_ctx *dctx,
		struct snap_dpa_thread_attr *attr)
{
	struct snap_dpa_thread *thr;
	int ret;
	flexio_status st;
	flexio_uintptr_t *dpa_tcb_addr;
	struct snap_dpa_tcb tcb;
	struct snap_dpa_rsp *rsp;

	thr = calloc(1, sizeof(*thr));
	if (!thr) {
		snap_error("Failed to create DPA thread\n");
		return NULL;
	}

	thr->dctx = dctx;

	ret = pthread_mutex_init(&thr->cmd_lock, NULL);
	if (ret < 0) {
		snap_error("Failed to init DPA thread mailbox lock\n");
		goto free_thread;
	}

	ret = posix_memalign(&thr->cmd_mbox, SNAP_DPA_THREAD_MBOX_ALIGN,
			SNAP_DPA_THREAD_MBOX_LEN);
	if (ret < 0) {
		snap_error("Failed to allocate DPA thread mailbox\n");
		goto free_mutex;
	}

	memset(thr->cmd_mbox, 0, SNAP_DPA_THREAD_MBOX_LEN);
	thr->cmd_mr = ibv_reg_mr(thr->dctx->pd, thr->cmd_mbox, SNAP_DPA_THREAD_MBOX_LEN, 0);
	if (!thr->cmd_mr) {
		snap_error("Failed to allocate DPA thread mailbox mr\n");
		goto free_mbox;
	}

	st = flexio_window_create(thr->dctx->dpa_proc, thr->dctx->pd, &thr->cmd_window);
	if (st != FLEXIO_STATUS_SUCCESS) {
		snap_error("Failed to create DPA thread mailbox window\n");
		goto free_mr;
	}

	/* copy mailbox addr & lkey to the thread */
	tcb.mbox_address = (uint64_t)thr->cmd_mbox;
	tcb.mbox_lkey = thr->cmd_mr->lkey;
	snap_debug("tcb mailbox lkey 0x%x addr %p\n", thr->cmd_mr->lkey, thr->cmd_mbox);

	st = flexio_copy_from_host(thr->dctx->dpa_proc, (uintptr_t)&tcb, sizeof(tcb), &dpa_tcb_addr);
	if (st != FLEXIO_STATUS_SUCCESS) {
		snap_error("Failed to prepare DPA thread control block: %d\n", st);
		goto free_window;
	}

	/*
	 * NOTE: here we use modified flexio API to create a 'polling' thread.
	 * In the future we can decide what type of thread to create based on
	 * the attributes.
	 */
	st = flexio_thread_create(thr->dctx->dpa_proc, thr->dctx->entry_point, *dpa_tcb_addr,
			thr->cmd_window, &thr->dpa_thread);
	if (st != FLEXIO_STATUS_SUCCESS) {
		snap_error("Failed to create DPA thread: %d\n", st);
		goto free_tcb;
	}

	/* wait for report back from the thread */
	snap_dpa_cmd_send(thr->cmd_mbox, SNAP_DPA_CMD_START);
	rsp = snap_dpa_rsp_wait(thr->cmd_mbox);
	if (rsp->status != SNAP_DPA_RSP_OK) {
		snap_error("DPA thread failed to start\n");
		snap_dpa_thread_destroy_force(thr);
		flexio_memory_free(dpa_tcb_addr);
		return NULL;
	}

	flexio_memory_free(dpa_tcb_addr);
	return thr;

free_tcb:
	flexio_memory_free(dpa_tcb_addr);
free_window:
	flexio_window_destroy(thr->cmd_window);
free_mr:
	ibv_dereg_mr(thr->cmd_mr);
free_mbox:
	free(thr->cmd_mbox);
free_mutex:
	pthread_mutex_destroy(&thr->cmd_lock);
free_thread:
	free(thr);
	return NULL;
}

static void snap_dpa_thread_destroy_force(struct snap_dpa_thread *thr)
{
	flexio_thread_destroy(thr->dpa_thread);
	flexio_window_destroy(thr->cmd_window);
	ibv_dereg_mr(thr->cmd_mr);
	pthread_mutex_destroy(&thr->cmd_lock);
	free(thr->cmd_mbox);
	free(thr);
}

/**
 * snap_dpa_thread_destroy() - destroy DPA thread
 * @thr:  DPA thread
 *
 * The function stops execution and clears all resources taken by the
 * DPA thread.
 *
 * The function is blocking. It sends 'STOP' message to the DPA thread,
 * waits for the ack and only then destroys thread and resources.
 */
void snap_dpa_thread_destroy(struct snap_dpa_thread *thr)
{
	struct snap_dpa_rsp *rsp;

	snap_dpa_cmd_send(thr->cmd_mbox, SNAP_DPA_CMD_STOP);
	rsp = snap_dpa_rsp_wait(thr->cmd_mbox);
	if (rsp->status != SNAP_DPA_RSP_OK)
		snap_warn("DPA thread was not properly stopped\n");

	snap_dpa_thread_destroy_force(thr);
}

/**
 * snap_dpa_thread_mbox_acquire() - get thread mailbox
 * @thr: DPA thread
 *
 * Get DPA thread mailbox address in the MT safe way. The mailbox must be
 * released with the snap_dpa_thread_mbox_release()
 *
 * Return:
 * DPA thread mailbox address
 */
void *snap_dpa_thread_mbox_acquire(struct snap_dpa_thread *thr)
{
	pthread_mutex_lock(&thr->cmd_lock);
	return thr->cmd_mbox;
}

/**
 * snap_dpa_thread_mbox_release() - release thread mailbox
 * @thr: DPA thread
 *
 * The function releases mailbox lock acquired by calling
 * snap_dpa_thread_mbox_acquire()
 */
void snap_dpa_thread_mbox_release(struct snap_dpa_thread *thr)
{
	pthread_mutex_unlock(&thr->cmd_lock);
}

/**
 * snap_dpa_app_start() - start dpa application
 *
 * The function performs load of the application code, starts
 * a process and creates worker threads.
 *
 * The application is a reference counted 'singleton'. That is it only started
 * once. Subsequent calls to the function will increase reference count
 *
 * The function is MT safe
 *
 * Return:
 * 0 on success, error code on failure
 */
int snap_dpa_app_start(struct snap_dpa_app *app, struct snap_dpa_app_attr *attr)
{
	int i;

	pthread_mutex_lock(&app->lock);

	if (app->refcount++ > 0) {
		pthread_mutex_unlock(&app->lock);
		return 0;
	}

	memset(app, 0, sizeof(*app));
	app->dctx = snap_dpa_process_create(attr->sctx, attr->name);
	if (!app->dctx)
		goto out;

	for (i = 0; i < N_DPA_APP_WORKERS; i++) {
		app->dpa_workers[i] = snap_dpa_thread_create(app->dctx, NULL);
		if (!app->dpa_workers[i])
			goto out;
	}

	pthread_mutex_unlock(&app->lock);
	return 0;

out:
	pthread_mutex_unlock(&app->lock);
	snap_dpa_app_stop(app);
	return -1;
}

/**
 * snap_dpa_app_stop() - stop DPA application
 *
 * The function descreases application reference count. If the count reaches
 * zero the application will be stopped: all DPA resources will be released.
 *
 * The function is MT safe
 */
void snap_dpa_app_stop(struct snap_dpa_app *app)
{
	int i;

	pthread_mutex_lock(&app->lock);

	if (app->refcount == 0) {
		pthread_mutex_unlock(&app->lock);
		return;
	}

	if (--app->refcount > 0) {
		pthread_mutex_unlock(&app->lock);
		return;
	}

	if (app->refcount < 0) {
		snap_error("invalid refcount %d\n", app->refcount);
		pthread_mutex_unlock(&app->lock);
		return;
	}

	for (i = 0; i < N_DPA_APP_WORKERS; i++) {
		if (!app->dpa_workers[i])
			continue;
		snap_dpa_thread_destroy(app->dpa_workers[i]);
		app->dpa_workers[i] = 0;
	}

	if (app->dctx) {
		snap_dpa_process_destroy(app->dctx);
		app->dctx = NULL;
	}

	pthread_mutex_unlock(&app->lock);
}
