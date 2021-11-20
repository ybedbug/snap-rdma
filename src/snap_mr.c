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

#include <limits.h>

#include "config.h"
#include "snap.h"
#include "snap_mr.h"
#include "mlx5_ifc.h"

int snap_get_pd_id(struct ibv_pd *pd, uint32_t *pd_id)
{
	int ret = 0;
	struct mlx5dv_pd pd_info;
	struct mlx5dv_obj obj;

	if (!pd)
		return -EINVAL;
	obj.pd.in = pd;
	obj.pd.out = &pd_info;
	ret = mlx5dv_init_obj(&obj, MLX5DV_OBJ_PD);
	if (ret)
		return ret;
	*pd_id = pd_info.pdn;
	return 0;
}

/**
 *snap_reg_mr() - Register memort region with Relaxed-Ordering acess mode
 *
 * @pd:     ibv_pd to register with.
 * @addr:   pointer to a memory region
 * @length: size of the memory region
 *
 * Return:
 * ibv_mr or NULL on error
 */
struct ibv_mr *snap_reg_mr(struct ibv_pd *pd, void *addr, size_t length)
{
	int mr_access = 0;
	struct ibv_mr *mr;
	struct snap_relaxed_ordering_caps ro_caps = {};

	mr_access = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
						IBV_ACCESS_REMOTE_WRITE;

	if (!snap_query_relaxed_ordering_caps(pd->context, &ro_caps)) {
		if (ro_caps.relaxed_ordering_write &&
					ro_caps.relaxed_ordering_read)
			mr_access |= IBV_ACCESS_RELAXED_ORDERING;
	} else
		snap_warn("Failed to query relaxed ordering caps\n");

	mr = ibv_reg_mr(pd, addr, length, mr_access);

	return mr;
}

/**
 * snap_create_cross_mkey() - Creates a new mkey
 * @pd:           a protection domain that will be used to access remote memory
 * @target_sdev:  an emulation device
 *
 * The function creates a special 'cross' memory key that must be used to
 * access host memory via RDMA operations.
 *
 * For QPs that use 'cross' mkey there is no need to be attached to the snap
 * emulation object.
 *
 * Sample usage pattern:
 *   sctx = snap_open();
 *   sdev = snap_open_device(sctx, attrs);
 *
 *   // Create protection domain:
 *   ib_ctx = ibv_open_device();
 *   pd = ibv_alloc_pd(ib_ctx);
 *
 *   // create mkey:
 *   mkey = snap_create_cross_mkey(pd, sdev);
 *
 *   // create qp using dma layer or directly with ibv_create_qp()
 *   dma_q = snap_dma_q_create(pd, attr);
 *
 *   // use mkey->mkey to access host memory
 *   rc = snap_dma_q_write(dma_q, ldata, len, lkey, host_paddr, mkey->mkey, comp);
 *
 * Return:
 * A memory key or NULL on error
 */
struct snap_cross_mkey *snap_create_cross_mkey(struct ibv_pd *pd,
					       struct snap_device *target_sdev)
{
	uint8_t in[DEVX_ST_SZ_BYTES(create_mkey_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(create_mkey_out)] = {0};
	void *mkc = DEVX_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	struct ibv_context *ctx = pd->context;
	struct snap_cross_mkey *cmkey;
	uint32_t pd_id;

	cmkey = calloc(1, sizeof(*cmkey));
	if (!cmkey) {
		snap_error("failed to alloc cross_mkey for pd: 0x%x\n, err: %m\n",
			   pd->handle);
		return NULL;
	}

	/*
	 * For BF-1, we don't support cross-gvmi mkey devx object,
	 * instead we have the special context rkey
	 */
	if (target_sdev->mdev.vtunnel) {
		cmkey->devx_obj = NULL;
		cmkey->mkey = target_sdev->dma_rkey;
		return cmkey;
	};

	snap_get_pd_id(pd, &pd_id);

	DEVX_SET(create_mkey_in, in, opcode, MLX5_CMD_OP_CREATE_MKEY);
	DEVX_SET(mkc, mkc, access_mode_1_0,
		 MLX5_MKC_ACCESS_MODE_CROSSING_VHCA_MKEY & 0x3);
	DEVX_SET(mkc, mkc, access_mode_4_2,
		 (MLX5_MKC_ACCESS_MODE_CROSSING_VHCA_MKEY >> 2) & 0x7);
	DEVX_SET(mkc, mkc, a, 1);
	DEVX_SET(mkc, mkc, rw, 1);
	DEVX_SET(mkc, mkc, rr, 1);
	DEVX_SET(mkc, mkc, lw, 1);
	DEVX_SET(mkc, mkc, lr, 1);
	DEVX_SET(mkc, mkc, pd, pd_id);
	DEVX_SET(mkc, mkc, qpn, 0xffffff);
	DEVX_SET(mkc, mkc, length64, 1);
	/* TODO: change mkey_7_0 to increasing counter */
	DEVX_SET(mkc, mkc, mkey_7_0, 0x42);
	DEVX_SET(mkc, mkc, crossing_target_vhca_id, snap_get_vhca_id(target_sdev));
	DEVX_SET(mkc, mkc, translations_octword_size_crossing_target_mkey,
		 target_sdev->crossed_vhca_mkey);

	cmkey->devx_obj = mlx5dv_devx_obj_create(ctx, in, sizeof(in), out,
						 sizeof(out));
	if (!cmkey->devx_obj)
		goto out_err;

	cmkey->mkey = DEVX_GET(create_mkey_out, out, mkey_index) << 8 | 0x42;
	cmkey->pd = pd;

	return cmkey;

out_err:
	free(cmkey);
	return NULL;
}

/**
 * snap_destroy_cross_mkey() - Destroy 'cross' mkey
 * @mkey: mkey to destroy
 *
 * The function destroys 'cross' mkey
 *
 * Return:
 * 0 or -errno on error
 */
int snap_destroy_cross_mkey(struct snap_cross_mkey *mkey)
{
	int ret = 0;

	if (mkey->devx_obj)
		ret = mlx5dv_devx_obj_destroy(mkey->devx_obj);
	free(mkey);

	return ret;
}

/**
 * snap_create_indirect_mkey() - Creates a new mkey
 * @pd:   a protection domain that will be used to access remote memory
 * @attr: attributes used to create this mkey
 *
 * The function creates a klm type mkey (if @attr->log_entity_size == 0)
 *  or a ksm type mkey (if @attr->log_entity_size != 0, must >= 12). The
 *  Memory Transation Table is provided by @attr->klm_array, create mkey
 *  without MTT is allowed.
 *
 * Return:
 * A memory key or NULL on error
 */
struct snap_indirect_mkey *
snap_create_indirect_mkey(struct ibv_pd *pd,
			  struct mlx5_devx_mkey_attr *attr)
{
	struct mlx5_klm *klm_array = attr->klm_array;
	int klm_num = attr->klm_num;
	int in_size_dw = DEVX_ST_SZ_DW(create_mkey_in) +
			SNAP_KLM_MAX_TRANSLATION_ENTRIES_NUM * DEVX_ST_SZ_DW(klm);
	uint32_t in[in_size_dw];
	uint32_t out[DEVX_ST_SZ_DW(create_mkey_out)] = {0};
	void *mkc;
	uint32_t translation_size = 0;
	struct snap_indirect_mkey *cmkey;
	struct ibv_context *ctx = pd->context;
	uint32_t pd_id;
	int i = 0;
	uint8_t *klm;

	cmkey = calloc(1, sizeof(*cmkey));
	if (!cmkey) {
		snap_error("failed to alloc cross_mkey\n");
		return NULL;
	}

	memset(in, 0, in_size_dw * 4);
	DEVX_SET(create_mkey_in, in, opcode, MLX5_CMD_OP_CREATE_MKEY);
	mkc = DEVX_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	klm = (uint8_t *)DEVX_ADDR_OF(create_mkey_in, in, klm_pas_mtt);

	if (klm_num > 0) {
		translation_size = SNAP_ALIGN_CEIL(klm_num, 4);
		if (translation_size > SNAP_KLM_MAX_TRANSLATION_ENTRIES_NUM) {
			snap_error("Too large translaion entry tables\n");
			goto out_err;
		}

		for (i = 0; i < klm_num; i++) {
			DEVX_SET(klm, klm, byte_count, klm_array[i].byte_count);
			DEVX_SET(klm, klm, mkey, klm_array[i].mkey);
			DEVX_SET64(klm, klm, address, klm_array[i].address);
			klm += DEVX_ST_SZ_BYTES(klm);
		}
	}

	for (; i < SNAP_KLM_MAX_TRANSLATION_ENTRIES_NUM; i++) {
		DEVX_SET(klm, klm, byte_count, 0x0);
		DEVX_SET(klm, klm, mkey, 0x0);
		DEVX_SET64(klm, klm, address, 0x0);
		klm += DEVX_ST_SZ_BYTES(klm);
	}

	DEVX_SET(mkc, mkc, access_mode_1_0, attr->log_entity_size ?
		 MLX5_MKC_ACCESS_MODE_KLMFBS :
		 MLX5_MKC_ACCESS_MODE_KLMS);
	DEVX_SET(mkc, mkc, log_page_size, attr->log_entity_size);

	snap_get_pd_id(pd, &pd_id);
	DEVX_SET(create_mkey_in, in, translations_octword_actual_size,
		 klm_num);
	if (klm_num == 0)
		DEVX_SET(mkc, mkc, free, 0x1);
	DEVX_SET(mkc, mkc, lw, 0x1);
	DEVX_SET(mkc, mkc, lr, 0x1);
	DEVX_SET(mkc, mkc, rw, 0x1);
	DEVX_SET(mkc, mkc, rr, 0x1);
	DEVX_SET(mkc, mkc, umr_en, 0x1);
	DEVX_SET(mkc, mkc, qpn, 0xffffff);
	DEVX_SET(mkc, mkc, pd, pd_id);
	DEVX_SET(mkc, mkc, translations_octword_size_crossing_target_mkey,
		SNAP_KLM_MAX_TRANSLATION_ENTRIES_NUM);
	DEVX_SET(mkc, mkc, relaxed_ordering_write,
		attr->relaxed_ordering_write);
	DEVX_SET(mkc, mkc, relaxed_ordering_read,
		attr->relaxed_ordering_read);
	DEVX_SET64(mkc, mkc, start_addr, attr->addr);
	DEVX_SET64(mkc, mkc, len, attr->size);
	/* TODO: change mkey_7_0 to increasing counter */
	DEVX_SET(mkc, mkc, mkey_7_0, 0x42);
	cmkey->devx_obj = mlx5dv_devx_obj_create(ctx, in, sizeof(in), out,
						 sizeof(out));
	if (!cmkey->devx_obj)
		goto out_err;

	cmkey->mkey = DEVX_GET(create_mkey_out, out, mkey_index) << 8 | 0x42;
	return cmkey;

out_err:
	free(cmkey);
	return NULL;
}

/**
 * snap_destroy_indirect_mkey() - Destroy 'indirect' mkey
 * @mkey: mkey to destroy
 *
 * The function destroys 'indirect' mkey
 *
 * Return:
 * 0 or -errno on error
 */
int snap_destroy_indirect_mkey(struct snap_indirect_mkey *mkey)
{
	int ret = 0;

	if (mkey->devx_obj)
		ret = mlx5dv_devx_obj_destroy(mkey->devx_obj);

	free(mkey);

	return ret;
}

int snap_umem_init(struct ibv_context *context, struct snap_umem *umem)
{
	int ret;

	if (!umem->size)
		return 0;

	ret = posix_memalign(&umem->buf, SNAP_VIRTIO_UMEM_ALIGN, umem->size);
	if (ret)
		return ret;

	umem->devx_umem = mlx5dv_devx_umem_reg(context, umem->buf, umem->size,
					       IBV_ACCESS_LOCAL_WRITE);
	if (!umem->devx_umem) {
		ret = -errno;
		goto out_free;
	}

	return ret;

out_free:
	free(umem->buf);
	umem->buf = NULL;
	return ret;
}

void snap_umem_reset(struct snap_umem *umem)
{
	if (!umem->size)
		return;

	mlx5dv_devx_umem_dereg(umem->devx_umem);
	free(umem->buf);

	memset(umem, 0, sizeof(*umem));
}

static LIST_HEAD(snap_uar_list_head, snap_uar) snap_uar_list = LIST_HEAD_INITIALIZER(snap_uar_list);
static pthread_mutex_t snap_uar_list_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *snap_uar_name(struct snap_uar *uar)
{
	return ibv_get_device_name(uar->context->device);
}

static struct snap_uar *snap_uar_lookup(struct ibv_context *ctx)
{
	struct snap_uar *uar;

	LIST_FOREACH(uar, &snap_uar_list, entry) {
		if (uar->context == ctx)
			return uar;
	}
	return NULL;
}

/* TODO: allow multiple ibv contexts, keep a list of (context, uar) */
struct snap_uar *snap_uar_get(struct ibv_context *ctx)
{
	struct snap_uar *uar;

	/* since DPU 64bit writes are atomic it is safe to use single
	 * UAR per ibv context. (EliavB)
	 */
	pthread_mutex_lock(&snap_uar_list_lock);

	uar = snap_uar_lookup(ctx);
	if (!uar) {
		uar = calloc(1, sizeof(*uar));
		if (!uar)
			goto uar_calloc_fail;
	}

	if (uar->refcnt > 0) {
		uar->refcnt++;
		if (uar->refcnt == 0) {
			snap_error("%s: uar refcnt overflow\n", snap_uar_name(uar));
			uar->refcnt = INT_MAX;
			goto uar_ref_fail;
		}
		snap_debug("%s: uar ref: %d\n", snap_uar_name(uar), uar->refcnt);
		pthread_mutex_unlock(&snap_uar_list_lock);
		return uar;
	}

	/* we really want non cacheble uar in order to skip memory barrier
	 * when ringing tx doorbells.
	 * TODO: we may need to support blueflame
	 */
	uar->uar = mlx5dv_devx_alloc_uar(ctx, MLX5DV_UAR_ALLOC_TYPE_NC);
	if (!uar->uar) {
		uar->uar = mlx5dv_devx_alloc_uar(ctx, MLX5DV_UAR_ALLOC_TYPE_BF);
		if (!uar->uar)
			goto uar_devx_alloc_fail;
		uar->nc = false;
	} else
		uar->nc = true;

	uar->refcnt = 1;
	uar->context = ctx;
	LIST_INSERT_HEAD(&snap_uar_list, uar, entry);
	snap_debug("%s: NEW UAR: ctx %p uar %p nc %d\n", snap_uar_name(uar), ctx,
		   uar->uar, uar->nc);
	pthread_mutex_unlock(&snap_uar_list_lock);
	return uar;

uar_devx_alloc_fail:
	free(uar);
uar_ref_fail:
uar_calloc_fail:
	pthread_mutex_unlock(&snap_uar_list_lock);
	return NULL;
}

void snap_uar_put(struct snap_uar *uar)
{
	pthread_mutex_lock(&snap_uar_list_lock);
	snap_debug("%s: FREE UAR ref: %d\n", snap_uar_name(uar), uar->refcnt);
	if (--uar->refcnt > 0) {
		pthread_mutex_unlock(&snap_uar_list_lock);
		return;
	}
	LIST_REMOVE(uar, entry);
	pthread_mutex_unlock(&snap_uar_list_lock);
	mlx5dv_devx_free_uar(uar->uar);
	free(uar);
}
