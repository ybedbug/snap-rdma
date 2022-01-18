/*
 * Copyright © 202 NVIDIA CORPORATION & AFFILIATES. ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of Nvidia Corporation and its affiliates
 * (the "Company") and all right, title, and interest in and to the software
 * product, including all associated intellectual property rights, are and
 * shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 */

#ifndef _SNAP_DPA_RT_H
#define _SNAP_DPA_RT_H

#include "snap_dpa.h"
#include "snap_dpa_p2p.h"

/**
 * Regardless of the storage emulation logic we always have following
 * components:
 * - reference counted application
 * - thread type: event or polling
 * - queues per thread: one or multiple
 * - workers. A worker represents a group of queues handled by the
 * same DPU 'polling context'.
 * - basic DPA thread structure. For example RC QP, Doorbell CQ,
 *   MSIX vectors, trigger CQ etc...
 * - assigning queue to the DPA thread
 */

struct snap_dpa_rt_attr {
};

#define SNAP_DPA_RT_NAME_LEN 32

struct snap_dpa_rt {
	struct snap_dpa_ctx *dpa_proc;
	/* TODO: keep list of current threads. At first we are going to
	 * support on polling/sing combo so it can wait
	 */
	int refcount;
	pthread_mutex_t lock;
	char name[SNAP_DPA_RT_NAME_LEN];

	LIST_ENTRY(snap_dpa_rt) entry;
};

struct snap_dpa_rt *snap_dpa_rt_get(struct ibv_context *ctx, const char *name,
		struct snap_dpa_rt_attr *attr);
void snap_dpa_rt_put(struct snap_dpa_rt *rt);

struct snap_dpa_rt_worker {
};

struct snap_dpa_rt_worker *snap_dpa_rt_worker_create(struct snap_dpa_rt *rt);
void snap_dpa_rt_worker_destroy(struct snap_dpa_rt_worker *w);

/* allocate single thread */

enum snap_dpa_rt_thr_mode {
	SNAP_DPA_RT_THR_POLLING,
	SNAP_DPA_RT_THR_EVENT,
};

enum snap_dpa_rt_thr_nqs {
	SNAP_DPA_RT_THR_SINGLE,
	SNAP_DPA_RT_THR_MULTI,
};

struct snap_dpa_rt_filter {
	enum snap_dpa_rt_thr_mode mode;
	enum snap_dpa_rt_thr_nqs queue_mux_mode;
	struct snap_dpa_rt_worker *w;
};

struct snap_dpa_rt_thread {
	struct snap_dpa_rt *rt;
	struct snap_dpa_worker *wk;
	enum snap_dpa_rt_thr_mode mode;
	enum snap_dpa_rt_thr_nqs queue_mux_mode;
	int refcount;

	/* DPA specific things */
	struct snap_dpa_thread *thread;
	struct snap_dpa_p2p_q dpa_cmd_chan;
	struct snap_dpa_p2p_q dpu_cmd_chan;
	struct snap_cq *db_cq;
	/* TODO: QUERY_EMULATION_DEVICE_EQ_MSIX_MAPPING is not available yet */
	struct snap_msix_map *msix_vector;
	int n_msix;
};

struct dpa_rt_context {
	struct snap_hw_cq db_cq;
	struct snap_dpa_p2p_q dpa_cmd_chan;
};

#define SNAP_DPA_RT_QP_TX_SIZE 128
#define SNAP_DPA_RT_QP_RX_SIZE 128
#define SNAP_DPA_RT_QP_TX_ELEM_SIZE 64
#define SNAP_DPA_RT_QP_RX_ELEM_SIZE 64

#define SNAP_DPA_RT_THR_SINGLE_DB_CQE_SIZE 64
#define SNAP_DPA_RT_THR_SINGLE_DB_CQE_CNT 2
#define SNAP_DPA_RT_THR_SINGLE_HEAP_SIZE (64 * 1024)

struct snap_dpa_rt_thread *snap_dpa_rt_thread_get(struct snap_dpa_rt *rt, struct snap_dpa_rt_filter *filter);
void snap_dpa_rt_thread_put(struct snap_dpa_rt_thread *rt);

/* TODO: add a worker to support 1w:Ndpa threads model */
#endif
