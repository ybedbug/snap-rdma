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
#include "config.h"

#include "snap_env.h"

#include "snap_dpa.h"
#include "snap_qp.h"
#include "snap_dpa_rt.h"
#include "snap_dma.h"
#include "mlx5_ifc.h"


static struct snap_dpa_rt *snap_dpa_rt_create(struct ibv_context *ctx, const char *name,
		struct snap_dpa_rt_attr *attr)
{
	struct snap_dpa_rt *rt;

	rt = calloc(1, sizeof(*rt));
	if (!rt)
		return NULL;

	rt->dpa_proc = snap_dpa_process_create(ctx, name);
	if (!rt->dpa_proc)
		goto free_rt;

	rt->refcount = 1;
	pthread_mutex_init(&rt->lock, NULL);
	strncpy(rt->name, name, sizeof(rt->name) - 1);
	/* TODO: allocate rt, worker list, mutex etc */
	return rt;
free_rt:
	free(rt);
	return NULL;
}

static void snap_dpa_rt_destroy(struct snap_dpa_rt *rt)
{
	pthread_mutex_destroy(&rt->lock);
	snap_dpa_process_destroy(rt->dpa_proc);
	free(rt);
}

static LIST_HEAD(dpa_rt_list_head, snap_dpa_rt) dpa_rt_list = LIST_HEAD_INITIALIZER(dpa_rt_list);
static pthread_mutex_t dpa_rt_list_lock = PTHREAD_MUTEX_INITIALIZER;

static struct snap_dpa_rt *rt_lookup(const char *name)
{
	struct snap_dpa_rt *rt;

	LIST_FOREACH(rt, &dpa_rt_list, entry) {
		if (strncmp(rt->name, name, sizeof(rt->name)) == 0)
			return rt;
	}
	return NULL;
}

/**
 * snap_dpa_rt_get() - get a dpa runtime
 * @ctx:  ib context
 * @name: application name
 * @attr: runtime properties
 *
 * The function returns a pointer to the DPA runtime. The runtime is reference
 * counted and it will be shared among callers.
 *
 * Return:
 * runtime or NULL on error
 */
struct snap_dpa_rt *snap_dpa_rt_get(struct ibv_context *ctx, const char *name,
		struct snap_dpa_rt_attr *attr)
{
	struct snap_dpa_rt *rt;

	pthread_mutex_lock(&dpa_rt_list_lock);

	rt = rt_lookup(name);
	if (rt) {
		if (snap_ref_safe(&rt->refcount)) {
			snap_error("%s: dpa_rt refcnt overflow\n", name);
			goto rt_ref_err;
		}
		snap_debug("%s: dpa_rt ref: %d\n", name, rt->refcount);
		pthread_mutex_unlock(&dpa_rt_list_lock);
		return rt;
	}

	rt = snap_dpa_rt_create(ctx, name, attr);
	if (!rt)
		goto rt_create_err;

	LIST_INSERT_HEAD(&dpa_rt_list, rt, entry);
	pthread_mutex_unlock(&dpa_rt_list_lock);

	snap_debug("%s: NEW DPA runtime environment\n", rt->name);
	return rt;

rt_ref_err:
rt_create_err:
	pthread_mutex_unlock(&dpa_rt_list_lock);
	return NULL;
}

/**
 * snap_dpa_rt_put() - release dpa runtime
 * @rt: dpa runtime
 *
 * The function releases dpa runtime. The runtime will be destroyed if there
 * are no more users left.
 */
void snap_dpa_rt_put(struct snap_dpa_rt *rt)
{
	pthread_mutex_lock(&dpa_rt_list_lock);
	snap_debug("%s: FREE DPA RT ref: %d\n", rt->name, rt->refcount);
	if (--rt->refcount > 0) {
		pthread_mutex_unlock(&dpa_rt_list_lock);
		return;
	}
	LIST_REMOVE(rt, entry);
	pthread_mutex_unlock(&dpa_rt_list_lock);
	snap_dpa_rt_destroy(rt);
}

struct snap_dpa_rt_worker *snap_dpa_rt_worker_create(struct snap_dpa_rt *rt)
{
	return NULL;
}

void snap_dpa_rt_worker_destroy(struct snap_dpa_rt_worker *w)
{
}

void dummy_rx_cb(struct snap_dma_q *q, const void *data, uint32_t data_len, uint32_t imm_data)
{
	snap_error("OOPS: rx cb called\n");
}

static int rt_thread_init(struct snap_dpa_rt_thread *rt_thr)
{
	struct snap_dpa_thread_attr attr = {
		.heap_size = SNAP_DPA_RT_THR_SINGLE_HEAP_SIZE
	};
	struct snap_dma_q_create_attr q_attr = {
		.tx_qsize = SNAP_DPA_RT_QP_TX_SIZE,
		.tx_elem_size = SNAP_DPA_RT_QP_TX_ELEM_SIZE,
		.rx_qsize = SNAP_DPA_RT_QP_RX_SIZE,
		.rx_elem_size = SNAP_DPA_RT_QP_RX_ELEM_SIZE,
		.mode = snap_env_getenv(SNAP_DMA_Q_OPMODE),
		.dpa_mode = SNAP_DMA_Q_DPA_MODE_NONE
	};
	struct snap_cq_attr db_cq_attr = {
		.cq_type = SNAP_OBJ_DEVX,
		.cqe_size = SNAP_DPA_RT_THR_SINGLE_DB_CQE_SIZE,
		.cqe_cnt = SNAP_DPA_RT_THR_SINGLE_DB_CQE_CNT,
		.cq_on_dpa = true
	};
	struct snap_dpa_rt *rt = rt_thr->rt;
	struct ibv_pd *pd = rt->dpa_proc->pd;
	struct snap_hw_cq hw_cq;
	int ret;

	/* create thread first because in the event mode we will have to
	 * connect cqs to it
	 */
	rt_thr->thread = snap_dpa_thread_create(rt->dpa_proc, &attr);
	if (!rt_thr->thread)
		return -EINVAL;

	q_attr.rx_cb = dummy_rx_cb;
	rt_thr->dpu_cmd_chan.dma_q = snap_dma_ep_create(pd, &q_attr);
	if (!rt_thr->dpu_cmd_chan.dma_q)
		goto free_dpa_thread;


	q_attr.mode = SNAP_DMA_Q_MODE_DV;
	q_attr.dpa_mode = SNAP_DMA_Q_DPA_MODE_POLLING;
	q_attr.dpa_proc = rt->dpa_proc;
	rt_thr->dpa_cmd_chan.dma_q = snap_dma_ep_create(pd, &q_attr);
	if (!rt_thr->dpa_cmd_chan.dma_q)
		goto free_dpu_qp;

	ret = snap_dma_ep_connect(rt_thr->dpu_cmd_chan.dma_q, rt_thr->dpa_cmd_chan.dma_q);
	if (ret)
		goto free_dpa_qp;

	rt_thr->dpu_cmd_chan.q_size = SNAP_DPA_RT_QP_RX_SIZE;
	rt_thr->dpu_cmd_chan.credit_count = SNAP_DPA_RT_QP_RX_SIZE;

	/* simx bug: change to the thread */
	db_cq_attr.dpa_element_type = MLX5_APU_ELEMENT_TYPE_EQ;
	db_cq_attr.dpa_proc = rt->dpa_proc;
	rt_thr->db_cq = snap_cq_create(pd->context, &db_cq_attr);
	if (!rt_thr->db_cq)
		goto free_dpa_qp;

	/* in event mode also create a trigger cq/qp */

	/* TODO: create EQ/MSIX cq */
	ret = snap_cq_to_hw_cq(rt_thr->db_cq, &hw_cq);
	if (ret)
		goto free_db_cq;

	/* note that rt context is at the beginning of the thread heap */
	ret = snap_dpa_memcpy(rt->dpa_proc,
			snap_dpa_thread_heap_base(rt_thr->thread) + offsetof(struct dpa_rt_context, db_cq),
			&hw_cq, sizeof(hw_cq));
	if (ret)
		goto free_db_cq;

	/* must be last because it acts as an init barrier */
	ret = snap_dma_ep_dpa_copy_sync(rt_thr->thread, rt_thr->dpa_cmd_chan.dma_q);
	if (ret)
		goto free_db_cq;

	return 0;

free_db_cq:
	snap_cq_destroy(rt_thr->db_cq);
free_dpa_qp:
	snap_dma_ep_destroy(rt_thr->dpa_cmd_chan.dma_q);
free_dpu_qp:
	snap_dma_ep_destroy(rt_thr->dpu_cmd_chan.dma_q);
free_dpa_thread:
	snap_dpa_thread_destroy(rt_thr->thread);
	return -EINVAL;
}

static void rt_thread_reset(struct snap_dpa_rt_thread *rt_thr)
{
	snap_cq_destroy(rt_thr->db_cq);
	snap_dma_ep_destroy(rt_thr->dpa_cmd_chan.dma_q);
	snap_dma_ep_destroy(rt_thr->dpu_cmd_chan.dma_q);
	snap_dpa_thread_destroy(rt_thr->thread);
}

/**
 * snap_dpa_rt_thread_get() - get dpa thread according to the set of constrains
 * @rt:      dpa runtime
 * @filter:  the set of constrains
 *
 * The function returns a thread that matches constrains given in the @filter
 * argument. If necessary, the thread will be created.
 *
 * At the moment we only support single/polling thread. In the future a single
 * rt thread can be shared among several queues.
 */
struct snap_dpa_rt_thread *snap_dpa_rt_thread_get(struct snap_dpa_rt *rt, struct snap_dpa_rt_filter *filter)
{
	struct snap_dpa_rt_thread *rt_thr;
	int ret;

	if (filter->mode != SNAP_DPA_RT_THR_POLLING)
		return NULL;

	if (filter->queue_mux_mode != SNAP_DPA_RT_THR_SINGLE)
		return NULL;

	rt_thr = calloc(1, sizeof(*rt_thr));
	if (!rt_thr)
		return NULL;

	rt_thr->rt = rt;
	rt_thr->mode = filter->mode;
	rt_thr->queue_mux_mode = filter->queue_mux_mode;
	rt_thr->refcount = 1;

	ret = rt_thread_init(rt_thr);
	if (ret)
		goto free_mem;

	return rt_thr;

free_mem:
	free(rt_thr);
	return NULL;
}

/**
 * snap_dpa_rt_thread_put() - release dpa rt thread
 * @rt_thr: thread to release
 *
 * The function releases dpa runtime thread. The thread will be destroyed if
 * it is no longer used.
 */
void snap_dpa_rt_thread_put(struct snap_dpa_rt_thread *rt_thr)
{
	rt_thread_reset(rt_thr);
	free(rt_thr);
}
