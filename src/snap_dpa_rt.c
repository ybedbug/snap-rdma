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
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <sched.h>

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
	CPU_ZERO(&rt->polling_cores);
	CPU_ZERO(&rt->polling_core_set);
	CPU_ZERO(&rt->event_core_set);

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

int snap_dpa_rt_polling_core_get(struct snap_dpa_rt *rt)
{
	int hart;
	int i;

	pthread_mutex_lock(&rt->lock);

	hart = rt->next_polling_core;
	for (i = 0; i < SNAP_DPA_HW_THREADS_COUNT; i++) {
		/* todo: check that core is in polling core set */
		if (!CPU_ISSET(hart, &rt->polling_cores)) {
			CPU_SET(hart, &rt->polling_cores);
			rt->next_polling_core = (hart + 1) % SNAP_DPA_HW_THREADS_COUNT;
			goto found;
		}
		hart = (hart + 1) % SNAP_DPA_HW_THREADS_COUNT;
	}
	hart = -1;
found:
	pthread_mutex_unlock(&rt->lock);
	return hart;
}

void snap_dpa_rt_polling_core_put(struct snap_dpa_rt *rt, int i)
{
	pthread_mutex_lock(&rt->lock);
	if (!CPU_ISSET(i, &rt->polling_cores))
		snap_error("core %d is already free\n", i);
	CPU_CLR(i, &rt->polling_cores);
	pthread_mutex_unlock(&rt->lock);
}

int snap_dpa_rt_event_core_get(struct snap_dpa_rt *rt)
{
	int hart;

	/* todo: prefer sceduling across cores first, but at the moment
	 * there seems to be no advantage
	 */
	pthread_mutex_lock(&rt->lock);
	hart = rt->next_event_core;
	rt->next_event_core = (rt->next_event_core + 1) % SNAP_DPA_HW_THREADS_COUNT;
	pthread_mutex_unlock(&rt->lock);
	return hart;
}

void snap_dpa_rt_event_core_put(struct snap_dpa_rt *rt, int i)
{
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

static int rt_thread_init(struct snap_dpa_rt_thread *rt_thr, struct ibv_pd *pd_in)
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
	struct ibv_pd *pd = pd_in ? pd_in : rt->dpa_proc->pd;
	struct ibv_pd *dpa_pd = rt->dpa_proc->pd;
	struct snap_hw_cq hw_cq;
	int ret;
	cpu_set_t cpu_mask;

	/* create thread first because in the event mode we will have to
	 * connect cqs to it
	 */
	CPU_ZERO(&cpu_mask);
	if (rt_thr->mode == SNAP_DPA_RT_THR_POLLING)
		rt_thr->hart = snap_dpa_rt_polling_core_get(rt);
	else if (rt_thr->mode == SNAP_DPA_RT_THR_EVENT)
		rt_thr->hart = snap_dpa_rt_event_core_get(rt);
	else
		return -EINVAL;
	if (rt_thr->hart < 0)
		return -EINVAL;

	CPU_SET(rt_thr->hart, &cpu_mask);
	attr.hart_set = &cpu_mask;
	attr.user_flag = rt_thr->mode;

	rt_thr->thread = snap_dpa_thread_create(rt->dpa_proc, &attr);
	if (!rt_thr->thread)
		return -EINVAL;

	q_attr.rx_cb = dummy_rx_cb;
	rt_thr->dpu_cmd_chan.dma_q = snap_dma_ep_create(pd, &q_attr);
	if (!rt_thr->dpu_cmd_chan.dma_q)
		goto free_dpa_thread;

	q_attr.mode = SNAP_DMA_Q_MODE_DV;

	if (rt_thr->mode == SNAP_DPA_RT_THR_POLLING) {
		q_attr.dpa_mode = SNAP_DMA_Q_DPA_MODE_POLLING;
		q_attr.dpa_proc = rt->dpa_proc;
		db_cq_attr.dpa_element_type = MLX5_APU_ELEMENT_TYPE_EQ;
		db_cq_attr.dpa_proc = rt->dpa_proc;
	} else if (rt_thr->mode == SNAP_DPA_RT_THR_EVENT) {
		q_attr.dpa_mode = SNAP_DMA_Q_DPA_MODE_EVENT;
		q_attr.dpa_thread = rt_thr->thread;
		db_cq_attr.dpa_element_type = MLX5_APU_ELEMENT_TYPE_THREAD;
		db_cq_attr.dpa_thread = rt_thr->thread;
	} else
		goto free_dpu_qp;

	rt_thr->dpa_cmd_chan.dma_q = snap_dma_ep_create(pd, &q_attr);
	if (!rt_thr->dpa_cmd_chan.dma_q)
		goto free_dpu_qp;

	ret = snap_dma_ep_connect(rt_thr->dpu_cmd_chan.dma_q, rt_thr->dpa_cmd_chan.dma_q);
	if (ret)
		goto free_dpa_qp;

	rt_thr->dpu_cmd_chan.q_size = SNAP_DPA_RT_QP_RX_SIZE;
	rt_thr->dpu_cmd_chan.credit_count = SNAP_DPA_RT_QP_RX_SIZE;

	rt_thr->db_cq = snap_cq_create(dpa_pd->context, &db_cq_attr);
	if (!rt_thr->db_cq)
		goto free_dpa_qp;

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

	snap_info("DPA_RT: qp 0x%x rx_cq 0x%x tx_cq 0x%x db_cq 0x%x\n",
		  rt_thr->dpa_cmd_chan.dma_q->sw_qp.dv_qp.hw_qp.qp_num,
		  rt_thr->dpa_cmd_chan.dma_q->sw_qp.dv_rx_cq.cq_num,
		  rt_thr->dpa_cmd_chan.dma_q->sw_qp.dv_tx_cq.cq_num,
		  hw_cq.cq_num);
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
	if (rt_thr->msix_cq)
		snap_cq_destroy(rt_thr->msix_cq);
	snap_cq_destroy(rt_thr->db_cq);
	snap_dma_ep_destroy(rt_thr->dpa_cmd_chan.dma_q);
	snap_dma_ep_destroy(rt_thr->dpu_cmd_chan.dma_q);
	snap_dpa_thread_destroy(rt_thr->thread);

	if (rt_thr->mode == SNAP_DPA_RT_THR_POLLING)
		snap_dpa_rt_polling_core_put(rt_thr->rt, rt_thr->hart);
	else if (rt_thr->mode == SNAP_DPA_RT_THR_EVENT)
		snap_dpa_rt_event_core_put(rt_thr->rt, rt_thr->hart);
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

	if (filter->mode != SNAP_DPA_RT_THR_POLLING && filter->mode != SNAP_DPA_RT_THR_EVENT)
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

	/* TODO: modify attribute to accept external snap_dma_q */
	ret = rt_thread_init(rt_thr, filter->pd);
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

/**
 * snap_dpa_rt_thread_msix_add() - add msix_vector to the rt_thread
 * @rt_thr:     thread to add msix vector
 * @msix_eq:    event queue that is already mapped to the msix_vector
 * @msix_cqnum: cq number that should be used to raise msix
 *
 * The function adds (msix_eq, msix_cq) mapping to the rt thread. If the mapping
 * already exists the old one will be reused
 */
int snap_dpa_rt_thread_msix_add(struct snap_dpa_rt_thread *rt_thr, struct snap_dpa_msix_eq *msix_eq, uint32_t *msix_cqnum)
{
	/* TODO: if thread serves several queues the mapping should be ref
	 * counted.
	 * Note: unlike db_cq, msix_cq cannot be created at rt_thread init because
	 * msix_vector(eq) is only known at the queue creation time. cq
	 * cannot be created without eq_id.
	 */
	struct snap_cq_attr msix_cq_attr = {
		.cq_type = SNAP_OBJ_DEVX,
		.cqe_size = SNAP_DPA_RT_THR_MSIX_CQE_SIZE,
		.cqe_cnt = SNAP_DPA_RT_THR_MSIX_CQE_CNT,
		.cq_on_dpa = true,
		.dpa_element_type = MLX5_APU_ELEMENT_TYPE_EMULATED_DEV_EQ,
		.eqn = snap_dpa_msix_eq_id(msix_eq),
		.use_eqn = true,
		.dpa_proc = rt_thr->rt->dpa_proc
	};
	struct snap_hw_cq hw_cq;
	int ret;

	rt_thr->msix_cq = snap_cq_create(rt_thr->rt->dpa_proc->pd->context, &msix_cq_attr);
	if (!rt_thr->msix_cq)
		return -EINVAL;

	ret = snap_cq_to_hw_cq(rt_thr->msix_cq, &hw_cq);
	if (ret)
		goto destroy_cq;

	/* note that rt context is at the beginning of the thread heap */
	ret = snap_dpa_memcpy(rt_thr->rt->dpa_proc,
			snap_dpa_thread_heap_base(rt_thr->thread) + offsetof(struct dpa_rt_context, msix_cq),
			&hw_cq, sizeof(hw_cq));
	if (ret)
		goto destroy_cq;

	*msix_cqnum = hw_cq.cq_num;
	return 0;

destroy_cq:
	snap_cq_destroy(rt_thr->msix_cq);
	return -EINVAL;
}

/**
 * snap_dpa_rt_thread_msix_remove() - remove msix_vector to the rt_thread
 * @rt_thr:     thread to remove msix vector
 * @msix_eq:    event queue that is already mapped to the msix_vector
 *
 * The function removes (msix_eq, msix_cq) mapping.
 */
void snap_dpa_rt_thread_msix_remove(struct snap_dpa_rt_thread *rt_thr, struct snap_dpa_msix_eq *msix_eq)
{
	/* TODO: deref... */
	if (!rt_thr->msix_cq)
		return;

	snap_cq_destroy(rt_thr->msix_cq);
	rt_thr->msix_cq = NULL;
}
