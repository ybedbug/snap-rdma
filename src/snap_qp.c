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

#include <unistd.h>
#include <sys/queue.h>
#include <infiniband/verbs.h>
#include <infiniband/mlx5dv.h>

#include "config.h"

#include "snap_qp.h"
#include "snap_dpa.h"

#include "mlx5_ifc.h"

static bool cq_validate_attr(const struct snap_cq_attr *attr)
{
	if (attr->cqe_size != 64 && attr->cqe_size != 128)
		return false;

	return true;
}

static void cq_init_buf(struct snap_devx_cq *devx_cq, void *cq_buf)
{
	int i;

	for (i = 0; i < devx_cq->cqe_cnt; i++) {
		struct mlx5_cqe64 *cqe;

		cqe = (struct mlx5_cqe64 *)(cq_buf + devx_cq->cqe_size * i);
		if (devx_cq->cqe_size == 128)
			cqe++;
		cqe->op_own = (MLX5_CQE_INVALID << 4) | MLX5_CQE_OWNER_MASK;
	}
}

static int devx_cq_init(struct snap_cq *cq, struct ibv_context *ctx, const struct snap_cq_attr *attr)
{
	uint32_t in[DEVX_ST_SZ_DW(create_cq_in)] = {0};
	uint32_t out[DEVX_ST_SZ_DW(create_cq_out)] = {0};
	void *cqctx = DEVX_ADDR_OF(create_cq_in, in, cq_context);
	/* TODO: check what page size should be used on DPA */
	const uint32_t log_page_size = snap_u32log2(sysconf(_SC_PAGESIZE));
	struct snap_devx_cq *devx_cq = &cq->devx_cq;
	struct snap_dpa_ctx *dpa_proc = NULL;
	int ret = 0;
	struct snap_uar *cq_uar;
	size_t cq_mem_size;
	uint32_t umem_id;
	uint64_t umem_offset;

	cq_uar = snap_uar_get(ctx);
	if (!cq_uar)
		return -EINVAL;

	devx_cq->devx.ctx = ctx;
	devx_cq->devx.uar = cq_uar;
	devx_cq->cqe_cnt = SNAP_ROUNDUP_POW2(attr->cqe_cnt);
	devx_cq->cqe_size = attr->cqe_size;
	devx_cq->devx.on_dpa = attr->cq_on_dpa;
	cq_mem_size = (size_t)attr->cqe_size * devx_cq->cqe_cnt + SNAP_MLX5_DBR_SIZE;

	if (!attr->cq_on_dpa) {
		/* get eqn
		 * TODO: support non - polling mode
		 */
		ret = mlx5dv_devx_query_eqn(ctx, 0, &devx_cq->eqn_or_dpa_element);
		if (ret)
			goto deref_uar;

		devx_cq->devx.umem.size = cq_mem_size;
		ret = snap_umem_init(ctx, &devx_cq->devx.umem);
		if (ret)
			goto deref_uar;
		umem_id = devx_cq->devx.umem.devx_umem->umem_id;
		umem_offset = 0;
	} else {
		if (attr->dpa_element_type == MLX5_APU_ELEMENT_TYPE_THREAD) {
			if (!attr->dpa_thread) {
				ret = -EINVAL;
				goto deref_uar;
			}
			devx_cq->eqn_or_dpa_element = snap_dpa_thread_id(attr->dpa_thread);
			dpa_proc = attr->dpa_thread->dctx;
		} else if (attr->dpa_element_type == MLX5_APU_ELEMENT_TYPE_EQ) {
			dpa_proc = attr->dpa_proc;
			if (!dpa_proc) {
				ret = -EINVAL;
				goto deref_uar;
			}
			devx_cq->eqn_or_dpa_element = snap_dpa_process_eq_id(dpa_proc);
		} else {
			snap_debug("bad dpa cq type %d\n", attr->dpa_element_type);
			ret = -EINVAL;
			goto deref_uar;
		}

		devx_cq->devx.dpa_mem = snap_dpa_mem_alloc(dpa_proc, cq_mem_size);
		if (!devx_cq->devx.dpa_mem) {
			ret = -ENOMEM;
			goto deref_uar;
		}

		DEVX_SET(cqc, cqctx, apu_cq, 1);
		DEVX_SET(cqc, cqctx, apu_element_type, attr->dpa_element_type);

		umem_id = snap_dpa_process_umem_id(dpa_proc);
		umem_offset = snap_dpa_process_umem_offset(dpa_proc, snap_dpa_mem_addr(devx_cq->devx.dpa_mem));

		snap_debug("memsize %lu umem_id %d umem_offset %lu eqn/thr_id %d dpa_va: 0x%0lx\n",
				cq_mem_size, umem_id, umem_offset,
				devx_cq->eqn_or_dpa_element, snap_dpa_mem_addr(devx_cq->devx.dpa_mem));
	}

	/* create cq via devx */
	DEVX_SET(create_cq_in, in, opcode, MLX5_CMD_OP_CREATE_CQ);

	DEVX_SET(cqc, cqctx, dbr_umem_valid, 1);
	DEVX_SET(cqc, cqctx, dbr_umem_id, umem_id);
	DEVX_SET64(cqc, cqctx, dbr_addr, umem_offset + (size_t)attr->cqe_size * devx_cq->cqe_cnt);

	DEVX_SET(cqc, cqctx, cqe_sz, attr->cqe_size == 128 ?  MLX5_CQE_SIZE_128B : MLX5_CQE_SIZE_64B);

	/* always ignore overrun */
	DEVX_SET(cqc, cqctx, oi, 1);
	DEVX_SET(cqc, cqctx, log_cq_size, snap_u32log2(devx_cq->cqe_cnt));

	if (log_page_size > MLX5_ADAPTER_PAGE_SHIFT)
		DEVX_SET(cqc, cqctx, log_page_size, log_page_size - MLX5_ADAPTER_PAGE_SHIFT);

	DEVX_SET(cqc, cqctx, c_eqn_or_apu_element, devx_cq->eqn_or_dpa_element);
	DEVX_SET(cqc, cqctx, uar_page, cq_uar->uar->page_id);

	DEVX_SET(create_cq_in, in, cq_umem_valid, 1);
	DEVX_SET(create_cq_in, in, cq_umem_id, umem_id);
	DEVX_SET64(create_cq_in, in, cq_umem_offset, umem_offset);

	devx_cq->devx.devx_obj = mlx5dv_devx_obj_create(ctx, in, sizeof(in), out, sizeof(out));
	if (!devx_cq->devx.devx_obj) {
		ret = -errno;
		goto reset_cq_umem;
	}

	/*
	 * There is no load/store access to the memory allocated on DPA even though
	 * it is backed by the DPU memory. The only way to access is by doing
	 * DMA operation.
	 *
	 * So it is either DMA or let DPA initialize cqe memory once hw_cq is
	 * transfered to dpa thread via mbox.
	 */
	if (!attr->cq_on_dpa) {
		cq_init_buf(devx_cq, devx_cq->devx.umem.buf);
	} else {
		void *tmp_buf;

		tmp_buf = calloc(1, cq_mem_size);
		if (!tmp_buf) {
			ret = -ENOMEM;
			goto reset_cq_umem;
		}

		cq_init_buf(devx_cq, tmp_buf);
		ret = snap_dpa_memcpy(dpa_proc, snap_dpa_mem_addr(devx_cq->devx.dpa_mem), tmp_buf, cq_mem_size);
		free(tmp_buf);
		if (ret) {
			snap_error("failed to init cq buffer on DPA\n");
			goto reset_cq_umem;
		}
	}
	/* TODO: notification support */

	devx_cq->devx.id = DEVX_GET(create_cq_out, out, cqn);
	snap_debug("created devx cq 0x%x cqe_size %u nelems %d memsize %lu\n",
		   devx_cq->devx.id, devx_cq->cqe_size, devx_cq->cqe_cnt, cq_mem_size);
	return 0;

reset_cq_umem:
	if (!attr->cq_on_dpa)
		snap_umem_reset(&devx_cq->devx.umem);
	else
		snap_dpa_mem_free(devx_cq->devx.dpa_mem);
deref_uar:
	snap_uar_put(cq_uar);
	return ret;
}

static void devx_common_reset(struct snap_devx_common *base)
{
	mlx5dv_devx_obj_destroy(base->devx_obj);
	if (!base->on_dpa)
		snap_umem_reset(&base->umem);
	else
		snap_dpa_mem_free(base->dpa_mem);
	snap_uar_put(base->uar);
}

static void devx_cq_reset(struct snap_cq *cq)
{
	struct snap_devx_cq *devx_cq = &cq->devx_cq;

	devx_common_reset(&devx_cq->devx);
}

int devx_cq_to_hw_cq(struct snap_cq *cq, struct snap_hw_cq *hw_cq)
{
	struct snap_devx_cq *devx_cq = &cq->devx_cq;

	memset(hw_cq, 0, sizeof(*hw_cq));
	if (!devx_cq->devx.on_dpa)
		hw_cq->cq_addr = (uintptr_t)devx_cq->devx.umem.buf;
	else
		hw_cq->cq_addr = snap_dpa_mem_addr(devx_cq->devx.dpa_mem);
	hw_cq->ci = 0;
	hw_cq->cqe_cnt = devx_cq->cqe_cnt;
	hw_cq->cqe_size = devx_cq->cqe_size;
	hw_cq->dbr_addr = hw_cq->cq_addr + hw_cq->cqe_cnt * (uint64_t)hw_cq->cqe_size;
	hw_cq->cq_num = devx_cq->devx.id;
	hw_cq->uar_addr = (uintptr_t)devx_cq->devx.uar->uar->base_addr;
	hw_cq->cq_sn = 0;

	snap_debug("dv_hw_cq 0x%x: buf = 0x%lx, cqe_size = %u, cqe_count = %d dbr_addr = 0x%lx\n",
		   devx_cq->devx.id, hw_cq->cq_addr, hw_cq->cqe_size, hw_cq->cqe_cnt, hw_cq->dbr_addr);
	return 0;
}

static struct snap_cq_ops devx_cq_ops = {
	.init = devx_cq_init,
	.init_hw_cq = devx_cq_to_hw_cq,
	.reset = devx_cq_reset
};

static int verbs_cq_init(struct snap_cq *cq, struct ibv_context *ctx, const struct snap_cq_attr *attr)
{
	cq->verbs_cq = ibv_create_cq(ctx, attr->cqe_cnt, attr->cq_context,
				     attr->comp_channel, attr->comp_vector);
	snap_debug("ibv_create cq: %p\n", cq->verbs_cq);
	return cq->verbs_cq ? 0 : -errno;
}

static void verbs_cq_reset(struct snap_cq *cq)
{
	ibv_destroy_cq(cq->verbs_cq);
}

int verbs_cq_to_hw_cq(struct snap_cq *cq, struct snap_hw_cq *hw_cq)
{
	/* only basic verbs APIs should be used in this case */
	return -ENOTSUP;
}

static struct snap_cq_ops verbs_cq_ops = {
	.init = verbs_cq_init,
	.init_hw_cq = verbs_cq_to_hw_cq,
	.reset = verbs_cq_reset
};

static int dv_cq_init(struct snap_cq *cq, struct ibv_context *ctx, const struct snap_cq_attr *attr)
{
	struct ibv_cq_init_attr_ex cq_attr = {
		.cqe = attr->cqe_cnt,
		.cq_context = attr->cq_context,
		.channel = attr->comp_channel,
		.comp_vector = attr->comp_vector,
		.wc_flags = IBV_WC_STANDARD_FLAGS,
		.comp_mask = IBV_CQ_INIT_ATTR_MASK_FLAGS,
		.flags = IBV_CREATE_CQ_ATTR_IGNORE_OVERRUN
	};
	struct mlx5dv_cq_init_attr cq_ex_attr = {
		.comp_mask = MLX5DV_CQ_INIT_ATTR_MASK_CQE_SIZE,
		.cqe_size = attr->cqe_size
	};

	cq->verbs_cq = ibv_cq_ex_to_cq(mlx5dv_create_cq(ctx, &cq_attr, &cq_ex_attr));
	snap_debug("dv_create cq: %p\n", cq->verbs_cq);
	return cq->verbs_cq ? 0 : -errno;
}

static void dv_cq_reset(struct snap_cq *cq)
{
	ibv_destroy_cq(cq->verbs_cq);
}

int dv_cq_to_hw_cq(struct snap_cq *cq, struct snap_hw_cq *hw_cq)
{
	struct mlx5dv_obj dv_obj;
	struct mlx5dv_cq mlx5_cq;
	int ret;

	dv_obj.cq.in = cq->verbs_cq;
	dv_obj.cq.out = &mlx5_cq;
	ret = mlx5dv_init_obj(&dv_obj, MLX5DV_OBJ_CQ);
	if (ret)
		return ret;

	memset(hw_cq, 0, sizeof(*hw_cq));
	hw_cq->cq_addr = (uintptr_t)mlx5_cq.buf;
	hw_cq->ci = 0;
	hw_cq->cqe_cnt = mlx5_cq.cqe_cnt;
	hw_cq->cqe_size = mlx5_cq.cqe_size;
	hw_cq->dbr_addr = (uintptr_t)mlx5_cq.dbrec;
	hw_cq->cq_num = mlx5_cq.cqn;
	hw_cq->uar_addr = (uintptr_t)mlx5_cq.cq_uar;
	hw_cq->cq_sn = 0;

	snap_debug("dv_hw_cq 0x%x: buf = 0x%lx, cqe_size = %u, cqe_count = %d, dbr_addr = 0x%lx\n",
		   mlx5_cq.cqn, hw_cq->cq_addr, hw_cq->cqe_size, hw_cq->cqe_cnt, hw_cq->dbr_addr);
	return 0;
}

static struct snap_cq_ops dv_cq_ops = {
	.init = dv_cq_init,
	.init_hw_cq = dv_cq_to_hw_cq,
	.reset = dv_cq_reset
};

/**
 * snap_cq_create - create completion queue
 * @ctx:  ib verbs context
 * @attr: cq creation attributes
 *
 * The function creates a completion queue using method specified in the
 * attr->cq_type
 *
 * Supported methods are:
 * - SNAP_OBJ_VERBS  cq is created by ibv_create_cq()
 * - SNAP_OBJ_DV     cq is created by the mlx5dv_create_cq()
 * - SNAP_OBJ_DEVX   cq is created by mlx5dv_devx_obj_create()
 *
 * Completion queues that are created with the last two methods can be converted
 * to the form that is suitable for the direct polling by calling
 * snap_cq_to_hw_cq()
 *
 * SNAP_OBJ_DEVX completion queues can be created on DPA accessible memory and
 * bound to the DPA threads
 *
 * Return: snap cq or NULL on error
 */
struct snap_cq *snap_cq_create(struct ibv_context *ctx, const struct snap_cq_attr *attr)
{
	struct snap_cq *cq;

	if (!cq_validate_attr(attr))
		return NULL;

	cq = calloc(1, sizeof(*cq));
	if (!cq)
		return NULL;

	cq->type = attr->cq_type;

	if (attr->cq_type == SNAP_OBJ_DEVX)
		cq->ops = &devx_cq_ops;
	else if (attr->cq_type == SNAP_OBJ_VERBS)
		cq->ops = &verbs_cq_ops;
	else if (attr->cq_type == SNAP_OBJ_DV)
		cq->ops = &dv_cq_ops;
	else
		goto free_cq;

	if (cq->ops->init(cq, ctx, attr))
		goto free_cq;

	return cq;
free_cq:
	free(cq);
	return NULL;
}

/**
 * snap_cq_destroy - destroy completion queue
 * @cq: snap cq to destroy
 *
 * Destroy completion queue
 */
void snap_cq_destroy(struct snap_cq *cq)
{
	cq->ops->reset(cq);
	free(cq);
}

/**
 * snap_cq_to_hw_cq - get access to the low level cq representation
 * @cq:    snap completion queue
 * @hw_cq: low level completion queue
 *
 * The function sets up @hw_cq so that it can be used for the direct polling.
 * @hw_cq is also a 'serialized' cq representation and can be copied as is
 * to the DPA
 *
 * Only SNAP_OBJ_DV and SNAP_OBJ_DEVX completion queues can be converted
 *
 * Return: 0 on success or -errno on error
 */
int snap_cq_to_hw_cq(struct snap_cq *cq, struct snap_hw_cq *hw_cq)
{
	return cq->ops->init_hw_cq(cq, hw_cq);
}

/**
 * snap_cq_to_verbs_cq - get access to ibv_cq
 * @cq: snap completion queue
 *
 * Note that only SNAP_OBJ_VERVS and SNAP_OBJ_DV completion queues have ibv_cq
 *
 * Return: pointer to ibv_cq or NULL
 */
struct ibv_cq *snap_cq_to_verbs_cq(struct snap_cq *cq)
{
	assert_debug(cq->type == SNAP_OBJ_VERBS || cq->type == SNAP_OBJ_DV);
	return cq->verbs_cq;
}

/* QP creation */
static int devx_qp_init(struct snap_qp *qp, struct ibv_pd *pd, const struct snap_qp_attr *attr)
{
	uint32_t in[DEVX_ST_SZ_DW(create_qp_in)] = {0};
	uint32_t out[DEVX_ST_SZ_DW(create_qp_out)] = {0};
	struct snap_devx_qp *devx_qp = &qp->devx_qp;
	struct ibv_context *ctx = pd->context;
	void *qpc = DEVX_ADDR_OF(create_qp_in, in, qpc);
	const uint32_t log_page_size = snap_u32log2(sysconf(_SC_PAGESIZE));
	struct snap_uar *qp_uar;
	int ret;
	size_t qp_buf_len;
	uint32_t pd_id;
	uint32_t umem_id;
	uint64_t umem_offset;

	/* TODO: check actual caps */
	if (attr->sq_max_inline_size > 256)
		return -EINVAL;

	ret = snap_get_pd_id(pd, &pd_id);
	if (ret)
		return ret;

	qp_uar = snap_uar_get(ctx);
	if (!qp_uar)
		return -EINVAL;

	devx_qp->devx.pd = pd;
	devx_qp->devx.uar = qp_uar;
	devx_qp->sq_size = SNAP_ROUNDUP_POW2_OR0(attr->sq_size);
	devx_qp->rq_size = SNAP_ROUNDUP_POW2_OR0(attr->rq_size);
	devx_qp->devx.on_dpa = attr->qp_on_dpa;

	/*
	 * TODO: consider keeping separate cache for the dbrecs like UCX or like
	 * we had in our first devx_verbs code
	 * TODO: guard buffer between sq and rq
	 * TODO: adjust sq and rq sizes according to num_sge and perhaps umrs
	 */
	qp_buf_len = SNAP_ALIGN_CEIL(MLX5_SEND_WQE_BB * (size_t)devx_qp->sq_size +
				     SNAP_MLX5_RECV_WQE_BB * devx_qp->rq_size,
				     SNAP_MLX5_L2_CACHE_SIZE);

	if (!attr->qp_on_dpa) {
		devx_qp->devx.umem.size = qp_buf_len + SNAP_MLX5_DBR_SIZE;
		ret = snap_umem_init(ctx, &devx_qp->devx.umem);
		if (ret)
			goto deref_uar;

		umem_id = devx_qp->devx.umem.devx_umem->umem_id;
		umem_offset = 0;

	} else {
		if (!attr->dpa_proc) {
			ret = -EINVAL;
			goto deref_uar;
		}

		devx_qp->devx.dpa_mem = snap_dpa_mem_alloc(attr->dpa_proc, qp_buf_len + SNAP_MLX5_DBR_SIZE);
		if (!devx_qp->devx.dpa_mem) {
			ret = -ENOMEM;
			goto deref_uar;
		}

		umem_id = snap_dpa_process_umem_id(attr->dpa_proc);
		umem_offset = snap_dpa_process_umem_offset(attr->dpa_proc, snap_dpa_mem_addr(devx_qp->devx.dpa_mem));
	}

	devx_qp->dbr_offset = qp_buf_len;

	DEVX_SET(create_qp_in, in, opcode, MLX5_CMD_OP_CREATE_QP);
	DEVX_SET(qpc, qpc, st, MLX5_QPC_ST_RC);
	DEVX_SET(qpc, qpc, pd, pd_id);
	DEVX_SET(qpc, qpc, pm_state, MLX5_QPC_PM_STATE_MIGRATED);
	DEVX_SET(qpc, qpc, uar_page, qp_uar->uar->page_id);
	/* TODO: use user index to speed up qp lookup when we have
	 * multiple qps per cq:
	 * DEVX_SET(qpc, qpc, user_index, attr->uidx);
	 */
	if (log_page_size > MLX5_ADAPTER_PAGE_SHIFT)
		DEVX_SET(qpc, qpc, log_page_size, log_page_size - MLX5_ADAPTER_PAGE_SHIFT);

	if (attr->sq_size) {
		if (attr->sq_cq->type != SNAP_OBJ_DEVX) {
			ret = -EINVAL;
			goto reset_qp_umem;
		}

		DEVX_SET(qpc, qpc, cqn_snd, attr->sq_cq->devx_cq.devx.id);
		DEVX_SET(qpc, qpc, log_sq_size, snap_u32log2(devx_qp->sq_size));
		/* TODO: enable scatter to cqe for TX */
	} else {
		DEVX_SET(qpc, qpc, no_sq, 1);
	}
	if (attr->rq_size) {
		if (attr->rq_cq->type != SNAP_OBJ_DEVX) {
			ret = -EINVAL;
			goto reset_qp_umem;
		}

		if (attr->rq_cq->devx_cq.cqe_size != 128) {
			snap_error("RX CQE size %u must be 128\n", attr->rq_cq->devx_cq.cqe_size);
			ret = -EINVAL;
			goto reset_qp_umem;
		}

		DEVX_SET(qpc, qpc, cqn_rcv, attr->rq_cq->devx_cq.devx.id);
		DEVX_SET(qpc, qpc, log_rq_stride, snap_u32log2(SNAP_MLX5_RECV_WQE_BB) -
			 SNAP_MLX5_LOG_RQ_STRIDE_SHIFT);
		DEVX_SET(qpc, qpc, log_rq_size, snap_u32log2(devx_qp->rq_size));
		DEVX_SET(qpc, qpc, cs_res, MLX5_RES_SCAT_DATA64_CQE);
		DEVX_SET(qpc, qpc, rq_type, MLX5_NON_ZERO_RQ);
	} else {
		DEVX_SET(qpc, qpc, rq_type, MLX5_ZERO_LEN_RQ);
	}

	DEVX_SET(qpc, qpc, dbr_umem_valid, 1);
	DEVX_SET(qpc, qpc, dbr_umem_id, umem_id);
	/* offset within umem */
	DEVX_SET64(qpc, qpc, dbr_addr, umem_offset + qp_buf_len);

	DEVX_SET(create_qp_in, in, wq_umem_id, umem_id);
	DEVX_SET(create_qp_in, in, wq_umem_valid, 1);
	DEVX_SET64(create_qp_in, in, wq_umem_offset, umem_offset);

	devx_qp->devx.devx_obj = mlx5dv_devx_obj_create(ctx, in, sizeof(in), out, sizeof(out));
	if (!devx_qp->devx.devx_obj) {
		ret = -errno;
		goto reset_qp_umem;
	}

	devx_qp->devx.id = DEVX_GET(create_qp_out, out, qpn);
	snap_debug("created devx qp 0x%x sq_size %u rq_size %u memsize %lu\n", devx_qp->devx.id,
		   devx_qp->sq_size, devx_qp->rq_size, qp_buf_len + SNAP_MLX5_DBR_SIZE);
	return 0;

reset_qp_umem:
	if (!attr->qp_on_dpa)
		snap_umem_reset(&devx_qp->devx.umem);
	else
		snap_dpa_mem_free(devx_qp->devx.dpa_mem);
deref_uar:
	snap_uar_put(qp_uar);
	return ret;
}

static void devx_qp_reset(struct snap_qp *qp)
{
	struct snap_devx_qp *devx_qp = &qp->devx_qp;

	devx_common_reset(&devx_qp->devx);
}

static int devx_qp_to_hw_qp(struct snap_qp *qp, struct snap_hw_qp *hw_qp)
{
	struct snap_devx_qp *devx_qp = &qp->devx_qp;

	if (!devx_qp->devx.on_dpa) {
		hw_qp->sq.addr = (uintptr_t)devx_qp->devx.umem.buf + SNAP_MLX5_RECV_WQE_BB * devx_qp->rq_size;
		hw_qp->rq.addr = (uintptr_t)devx_qp->devx.umem.buf;
		hw_qp->dbr_addr = (uintptr_t)devx_qp->devx.umem.buf + devx_qp->dbr_offset;
	} else {
		hw_qp->sq.addr = snap_dpa_mem_addr(devx_qp->devx.dpa_mem) + SNAP_MLX5_RECV_WQE_BB * devx_qp->rq_size;
		hw_qp->rq.addr = snap_dpa_mem_addr(devx_qp->devx.dpa_mem);
		hw_qp->dbr_addr = snap_dpa_mem_addr(devx_qp->devx.dpa_mem) + devx_qp->dbr_offset;
	}

	hw_qp->sq.bf_addr = (uintptr_t)devx_qp->devx.uar->uar->reg_addr;
	hw_qp->sq.wqe_cnt = devx_qp->sq_size;
	hw_qp->rq.wqe_cnt = devx_qp->rq_size;

	hw_qp->rq.ci = hw_qp->sq.pi = 0;
	hw_qp->qp_num = devx_qp->devx.id;
#if defined(__aarch64__)
	if (!devx_qp->devx.uar->nc)
		snap_warn("UAR has blueflame enabled. Not possible on DPU. Assuming a bug\n");
	/* we know for sure that DPU has no blueflame */
	hw_qp->sq.tx_db_nc = 1;
#else
	hw_qp->sq.tx_db_nc = devx_qp->devx.uar->nc;
#endif
	return 0;
}

static int devx_qp_modify(struct snap_qp *qp, const void *in, size_t inlen, void *out,
			  size_t outlen)
{
	struct snap_devx_qp *devx_qp = &qp->devx_qp;

	return mlx5dv_devx_obj_modify(devx_qp->devx.devx_obj, in, inlen, out, outlen);
}

static struct snap_qp_ops devx_qp_ops = {
	.init = devx_qp_init,
	.init_hw_qp = devx_qp_to_hw_qp,
	.reset = devx_qp_reset
};

static int verbs_qp_init(struct snap_qp *qp, struct ibv_pd *pd, const struct snap_qp_attr *attr)
{
	struct ibv_qp_init_attr init_attr = {0};

	init_attr.qp_type = IBV_QPT_RC;

	init_attr.cap.max_send_wr = attr->sq_size;
	init_attr.cap.max_inline_data = attr->sq_max_inline_size;
	init_attr.cap.max_send_sge = attr->sq_max_sge;
	init_attr.send_cq = snap_cq_to_verbs_cq(attr->sq_cq);

	init_attr.cap.max_recv_wr = attr->rq_size;
	init_attr.cap.max_recv_sge = attr->rq_max_sge;
	init_attr.recv_cq = snap_cq_to_verbs_cq(attr->rq_cq);

	qp->verbs_qp = ibv_create_qp(pd, &init_attr);
	if (!qp->verbs_qp)
		return -errno;

	snap_debug("verbs_create qp: %p qpn 0x%x\n", qp->verbs_qp, qp->verbs_qp->qp_num);
	return 0;
}

static void verbs_qp_reset(struct snap_qp *qp)
{
	ibv_destroy_qp(qp->verbs_qp);
}

static bool uar_memory_is_nc(struct mlx5dv_qp *dv_qp)
{
	/*
	 * Verify that the memory is indeed NC. It relies on a fact (hack) that
	 * rdma-core is going to allocate NC uar if blue flame is disabled.
	 * This is a short term solution.
	 *
	 * The right solution is to allocate uars exlicitely with the
	 * mlx5dv_devx_alloc_uar()
	 */
	return dv_qp->bf.size == 0;
}

static int verbs_qp_to_hw_qp(struct snap_qp *qp, struct snap_hw_qp *hw_qp)
{
	struct mlx5dv_qp dv_qp;
	struct mlx5dv_obj dv_obj;
	int ret;

	dv_obj.qp.in = qp->verbs_qp;
	dv_obj.qp.out = &dv_qp;

	ret = mlx5dv_init_obj(&dv_obj, MLX5DV_OBJ_QP);
	if (ret)
		return ret;

	snap_debug("sq wqe_count = %d stride = %d, rq wqe_count = %d, stride = %d, bf.reg = %p, bf.size = %d\n",
		   dv_qp.sq.wqe_cnt, dv_qp.sq.stride,
		   dv_qp.rq.wqe_cnt, dv_qp.rq.stride,
		   dv_qp.bf.reg, dv_qp.bf.size);

	if (dv_qp.sq.stride != MLX5_SEND_WQE_BB ||
	    dv_qp.rq.stride != SNAP_MLX5_RECV_WQE_BB)
		return -EINVAL;

	hw_qp->sq.addr = (uintptr_t)dv_qp.sq.buf;
	hw_qp->rq.addr = (uintptr_t)dv_qp.rq.buf;
	hw_qp->dbr_addr = (uintptr_t)dv_qp.dbrec;
	hw_qp->sq.bf_addr = (uintptr_t)dv_qp.bf.reg;
	hw_qp->sq.wqe_cnt = dv_qp.sq.wqe_cnt;
	hw_qp->rq.wqe_cnt = dv_qp.rq.wqe_cnt;

	hw_qp->rq.ci = hw_qp->sq.pi = 0;
	hw_qp->qp_num = qp->verbs_qp->qp_num;
	hw_qp->sq.tx_db_nc = uar_memory_is_nc(&dv_qp);
	return 0;
}

static struct snap_qp_ops verbs_qp_ops = {
	.init = verbs_qp_init,
	.init_hw_qp = verbs_qp_to_hw_qp,
	.reset = verbs_qp_reset
};

static struct snap_qp_ops dv_qp_ops = {
	.init = verbs_qp_init,
	.init_hw_qp = verbs_qp_to_hw_qp,
	.reset = verbs_qp_reset
};

/**
 * snap_qp_create - create queue pair
 * @pd:   ib verbs protection domain
 * @attr: qp creation attributes
 *
 * The function creates RC queue pair using method specified in the
 * attr->qp_type
 *
 * Supported methods are:
 * - SNAP_OBJ_VERBS  qp is created by ibv_create_qp()
 * - SNAP_OBJ_DV     qp is created by ibv_create_qp()
 * - SNAP_OBJ_DEVX   qp is created by mlx5dv_devx_obj_create()
 *
 * Completion queues that are created with the last two methods can be converted
 * to the form that is suitable for the direct send or receive by calling
 * snap_cq_to_hw_qp()
 *
 * SNAP_OBJ_DEVX queue pairs can be created on DPA accessible memory
 *
 * Return: snap qp or NULL on error
 */

struct snap_qp *snap_qp_create(struct ibv_pd *pd, const struct snap_qp_attr *attr)
{
	struct snap_qp *qp;

	qp = calloc(1, sizeof(*qp));
	if (!qp)
		return NULL;

	qp->type = attr->qp_type;

	if (attr->qp_type == SNAP_OBJ_DEVX)
		qp->ops = &devx_qp_ops;
	else if (attr->qp_type == SNAP_OBJ_VERBS)
		qp->ops = &verbs_qp_ops;
	else if (attr->qp_type == SNAP_OBJ_DV)
		qp->ops = &dv_qp_ops;
	else
		goto free_qp;

	if (qp->ops->init(qp, pd, attr))
		goto free_qp;

	return qp;
free_qp:
	free(qp);
	return NULL;
}

/**
 * snap_qp_destroy - destroy snap qp
 * @qp: snap qp
 *
 * The function destroys snap qp
 */
void snap_qp_destroy(struct snap_qp *qp)
{
	qp->ops->reset(qp);
	free(qp);
}

/**
 * snap_qp_to_hw_cq - get access to the low level qp representation
 * @qp:    snap qp
 * @hw_qp: low level qp
 *
 * The function sets up @hw_qp so that it can be used for the direct send or
 * receive operations.
 *
 * @hw_qp is also a 'serialized' qp representation and can be copied as is
 * to the DPA
 *
 * Only SNAP_OBJ_DV and SNAP_OBJ_DEVX qp can be converted
 *
 * Return: 0 on success or -errno on error
 */
int snap_qp_to_hw_qp(struct snap_qp *qp, struct snap_hw_qp *hw_qp)
{
	int ret;

	ret = qp->ops->init_hw_qp(qp, hw_qp);
	if (ret)
		return ret;

	snap_debug("qp: 0x%0x sq: 0x%0lx cnt %d, rq: 0x%0lx cnt %d, db: 0x%0lx, bf_reg: 0x%0lx\n",
		   hw_qp->qp_num, hw_qp->sq.addr, hw_qp->sq.wqe_cnt,
		   hw_qp->rq.addr, hw_qp->rq.wqe_cnt,
		   hw_qp->dbr_addr, hw_qp->sq.bf_addr);
	return 0;
}

/**
 * snap_qp_to_verbs_qp - get access to ibv_qp
 * @qp: snap queue
 *
 * Note that only SNAP_OBJ_VERVS and SNAP_OBJ_DV completion queues have ibv_qp
 *
 * Return: pointer to ibv_qp or NULL
 */
struct ibv_qp *snap_qp_to_verbs_qp(struct snap_qp *qp)
{
	assert_debug(qp->type == SNAP_OBJ_VERBS || qp->type == SNAP_OBJ_DV);
	return qp->verbs_qp;
}

/**
 * snap_qp_modify - modify qp via DEVX command
 * @qp:     snap qp
 * @in:     input buffer with DEVX command
 * @inlen:  input buffer length
 * @out:    output buffer, filled with command results
 * @outlen: output buffer length
 *
 * The function sends modify qp command to the firmware. Primary use is to
 * transition qp from INIT to RTS
 *
 * Return: 0 on success or -errno
 */
int snap_qp_modify(struct snap_qp *qp, const void *in, size_t inlen, void *out,
		   size_t outlen)
{
	if (qp->type == SNAP_OBJ_DEVX)
		return devx_qp_modify(qp, in, inlen, out, outlen);

	if (qp->type == SNAP_OBJ_VERBS || qp->type == SNAP_OBJ_DV)
		return mlx5dv_devx_qp_modify(qp->verbs_qp, in, inlen, out, outlen);

	return -EINVAL;
}

/**
 * snap_qp_get_qpnum - get qp number
 * @qp: snap qp
 *
 * Return: qp number
 */
uint32_t snap_qp_get_qpnum(struct snap_qp *qp)
{
	if (qp->type == SNAP_OBJ_DEVX)
		return qp->devx_qp.devx.id;

	if (qp->type == SNAP_OBJ_VERBS || qp->type == SNAP_OBJ_DV)
		return qp->verbs_qp->qp_num;

	return 0xFFFFFFFF;
}

/**
 * snap_qp_get_pd - get qp pd
 * @qp: snap qp
 *
 * Return: pointer to the pd or NULL on error
 */
struct ibv_pd *snap_qp_get_pd(struct snap_qp *qp)
{
	if (qp->type == SNAP_OBJ_DEVX)
		return qp->devx_qp.devx.pd;

	if (qp->type == SNAP_OBJ_VERBS || qp->type == SNAP_OBJ_DV)
		return qp->verbs_qp->pd;

	return NULL;
}
