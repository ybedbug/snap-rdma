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

#ifndef SNAP_QP_H
#define SNAP_QP_H

#include "snap_mr.h"
#include "snap_macros.h"

/**
 * The purpose of having snap_cq and snap_qp objects is to
 * - provide migration to the full DEVX object creation while keeping
 *   dv/verbs objects as a fallback
 * - ability to have safe and simple implementation of the data path using
 *   basic verbs api
 * - create qp and cq with the attributes that are only available via DEVX.
 *   For example DPA support.
 *
 * Theory of operation:
 * - create object according to the attributes.
 * - CQ/QP can be converted to the minimal (hw_cq, hw_qp) objects
 *   that can be used in the data path code. These minimal objects can also be
 *   passed to the DPA
 * - or get ibv_cq/ibv_qp and use basic verbs api
 */

#define SNAP_MLX5_RECV_WQE_BB   16ULL
#define SNAP_MLX5_LOG_RQ_STRIDE_SHIFT 4

/*
 * sysconf(_SC_LEVEL2_CACHE_LINESIZE) works on x86 but returns zero on arm.
 * Use constant as a workaround
 */
#define SNAP_MLX5_L2_CACHE_SIZE 64
#define SNAP_MLX5_DBR_SIZE SNAP_MLX5_L2_CACHE_SIZE

#define SNAP_MLX5_CQ_SET_CI 0
#define SNAP_MLX5_CQ_ARM_DB 1

enum {
	SNAP_OBJ_VERBS = 0x1,
	SNAP_OBJ_DV = 0x2,
	SNAP_OBJ_DEVX = 0x3
};

struct snap_devx_common {
	uint32_t id;
	struct mlx5dv_devx_obj *devx_obj;
	struct snap_uar *uar;
	union {
		struct snap_umem umem;
		struct snap_dpa_memh *dpa_mem;
	};
	union {
		struct ibv_pd *pd;
		struct ibv_context *ctx;
	};
	bool on_dpa;
};

struct snap_devx_cq {
	struct snap_devx_common devx;
	uint32_t cqe_cnt;
	uint32_t cqe_size;
	uint32_t eqn_or_dpa_element;
};

/**
 * Only define attributes that we use
 */
struct snap_cq_attr {
	int cq_type;
	uint32_t cqe_cnt;
	uint32_t cqe_size;
	void *cq_context;
	struct ibv_comp_channel *comp_channel;
	int comp_vector;
	bool cq_on_dpa;
	int dpa_element_type;
	union {
		struct snap_dpa_ctx *dpa_proc;
		struct snap_dpa_thread *dpa_thread;
	};

	uint32_t eqn;
	bool use_eqn;
};

/* low level cq view, suitable for the direct polling, adapted from struct mlx5dv_cq */
struct snap_hw_cq {
	uint64_t cq_addr;
	uint32_t cqe_cnt;
	uint32_t cqe_size;
	uint16_t rsvd1;
	uint16_t ci;
	uint64_t dbr_addr;
	uint64_t uar_addr;
	uint32_t cq_num;
	uint32_t cq_sn;
};

struct snap_cq;

struct snap_cq_ops {
	int (*init)(struct snap_cq *cq, struct ibv_context *ctx, const struct snap_cq_attr *attr);
	int (*init_hw_cq)(struct snap_cq *cq, struct snap_hw_cq *hw_cq);
	void (*reset)(struct snap_cq *cq);
};

struct snap_cq {
	int type;
	union {
		struct snap_devx_cq devx_cq;
		struct ibv_cq *verbs_cq;
	};
	struct snap_cq_ops *ops;
};

struct snap_cq *snap_cq_create(struct ibv_context *ctx, const struct snap_cq_attr *attr);
void snap_cq_destroy(struct snap_cq *cq);
int snap_cq_to_hw_cq(struct snap_cq *cq, struct snap_hw_cq *hw_cq);
struct ibv_cq *snap_cq_to_verbs_cq(struct snap_cq *cq);

static inline bool snap_cq_on_dpa(struct snap_cq *cq)
{
	return cq->type == SNAP_OBJ_DEVX && cq->devx_cq.devx.on_dpa;
}

struct snap_qp_attr {
	int qp_type;

	uint32_t sq_size;
	uint32_t sq_max_sge;
	uint32_t sq_max_inline_size;
	struct snap_cq *sq_cq;

	uint32_t rq_size;
	uint32_t rq_max_sge;
	struct snap_cq *rq_cq;
	uint32_t uidx;

	bool qp_on_dpa;
	struct snap_dpa_ctx *dpa_proc;
};

struct snap_hw_qp {
	uint64_t dbr_addr;
	struct {
		uint64_t addr;
		uint64_t bf_addr;
		uint32_t wqe_cnt;
		uint16_t rsvd;
		uint16_t pi;
		uint32_t tx_db_nc;
	} sq;
	struct {
		uint64_t addr;
		uint32_t wqe_cnt;
		uint16_t rsvd;
		uint16_t ci;
	} rq;
	uint32_t qp_num;
};

SNAP_STATIC_ASSERT(sizeof(struct snap_hw_qp) <= SNAP_MLX5_L2_CACHE_SIZE,
		"Oops snap_hw_qp does not fit into cache line!!!!");

struct snap_devx_qp {
	struct snap_devx_common devx;
	uint32_t rq_size;
	uint32_t sq_size;
	uint32_t dbr_offset;
};

struct snap_qp;

struct snap_qp_ops {
	int (*init)(struct snap_qp *qp, struct ibv_pd *pd, const struct snap_qp_attr *attr);
	int (*init_hw_qp)(struct snap_qp *qp, struct snap_hw_qp *hw_qp);
	void (*reset)(struct snap_qp *qp);
};

struct snap_qp {
	int type;
	union {
		struct snap_devx_qp devx_qp;
		struct ibv_qp *verbs_qp;
	};
	struct snap_qp_ops *ops;
};

struct snap_qp *snap_qp_create(struct ibv_pd *pd, const struct snap_qp_attr *attr);
void snap_qp_destroy(struct snap_qp *qp);
int snap_qp_to_hw_qp(struct snap_qp *qp, struct snap_hw_qp *hw_qp);
struct ibv_qp *snap_qp_to_verbs_qp(struct snap_qp *qp);
int snap_qp_modify(struct snap_qp *qp, const void *in, size_t inlen,
		void *out, size_t outlen);
uint32_t snap_qp_get_qpnum(struct snap_qp *qp);
struct ibv_pd *snap_qp_get_pd(struct snap_qp *qp);

static inline bool snap_qp_on_dpa(struct snap_qp *qp)
{
	return qp->type == SNAP_OBJ_DEVX && qp->devx_qp.devx.on_dpa;
}
#endif
