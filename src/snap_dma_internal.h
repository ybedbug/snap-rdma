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

#ifndef SNAP_DMA_INTERNAL_H
#define SNAP_DMA_INTERNAL_H

#include "snap_dma.h"
#include "snap_mb.h"

#if __DPA
#include "../dpa/dpa.h"
#endif


#define SNAP_DMA_Q_RX_CQE_SIZE  128
#define SNAP_DMA_Q_TX_CQE_SIZE  64
#define SNAP_DMA_Q_TX_MOD_COUNT 16

/* GGA specific */
#define MLX5_OPCODE_MMO       0x2F
#define MLX5_OPC_MOD_MMO_DMA  0x1

struct mlx5_dma_opaque {
	uint32_t syndrom;
	uint32_t reserved;
	uint32_t scattered_length;
	uint32_t gathered_length;
	uint8_t  reserved2[240];
} __attribute__((packed));

SNAP_STATIC_ASSERT(sizeof(struct mlx5_dma_opaque) == 256, "Bad mlx5_dma_opaque size");

struct mlx5_dma_wqe {
	uint32_t opcode;
	uint32_t sq_ds;
	uint32_t flags;
	uint32_t gga_ctrl1;  /* unused for dma */
	uint32_t gga_ctrl2;  /* unused for dma */
	uint32_t opaque_lkey;
	uint64_t opaque_vaddr;
	struct mlx5_wqe_data_seg gather;
	struct mlx5_wqe_data_seg scatter;
};

static inline uint16_t round_up(uint16_t x, uint16_t d)
{
	return (x + d - 1)/d;
}

static inline bool qp_can_tx(struct snap_dma_q *q, int bb_needed)
{
	/* later we can also add cq space check */
	return q->tx_available >= bb_needed;
}

static inline bool worker_qps_can_tx(struct snap_dma_worker *wk, int bb_needed)
{
	int i;

	for (i = 0; i < wk->max_queues; i++) {
		if (snap_unlikely(!wk->queues[i].in_use))
			continue;
		if (wk->queues[i].q.tx_available < bb_needed)
			return false;
	}

	return true;
}
/* DV implementation */
static inline int snap_dv_get_cq_update(struct snap_dv_qp *dv_qp, struct snap_dma_completion *comp)
{
	if (comp || dv_qp->n_outstanding + 1 >= SNAP_DMA_Q_TX_MOD_COUNT)
		return MLX5_WQE_CTRL_CQ_UPDATE;
	else
		return 0;
}

static inline void *snap_dv_get_wqe_bb(struct snap_dv_qp *dv_qp)
{
	return (void *)dv_qp->hw_qp.sq.addr + (dv_qp->hw_qp.sq.pi & (dv_qp->hw_qp.sq.wqe_cnt - 1)) *
	       MLX5_SEND_WQE_BB;
}

static inline void
snap_set_ctrl_seg(struct mlx5_wqe_ctrl_seg *ctrl, uint16_t pi,
			 uint8_t opcode, uint8_t opmod, uint32_t qp_num,
			 uint8_t fm_ce_se, uint8_t ds,
			 uint8_t signature, uint32_t imm)
{
	*(uint32_t *)((void *)ctrl + 8) = 0;
	mlx5dv_set_ctrl_seg(ctrl, pi, opcode, opmod, qp_num,
			fm_ce_se, ds, signature, imm);
}

static inline void snap_dv_ring_tx_db(struct snap_dv_qp *dv_qp, struct mlx5_wqe_ctrl_seg *ctrl)
{
#if !__DPA
	/* 8.9.3.1  Posting a Work Request to Work Queue
	 * 1. Write WQE to the WQE buffer sequentially to previously-posted
	 * WQE (on WQEBB granularity)
	 *
	 * Use cpu barrier to prevent code reordering
	 */
	snap_memory_cpu_store_fence();

	/* 2. Update Doorbell Record associated with that queue by writing
	 *    the sq_wqebb_counter or wqe_counter for send and RQ respectively
	 **/
	((uint32_t *)dv_qp->hw_qp.dbr_addr)[MLX5_SND_DBR] = htobe32(dv_qp->hw_qp.sq.pi);

	/* Make sure that doorbell record is written before ringing the doorbell
	 **/
	snap_memory_bus_store_fence();

	/* 3. For send request ring DoorBell by writing to the Doorbell
	 *    Register field in the UAR associated with that queue
	 */
	*(uint64_t *)(dv_qp->hw_qp.sq.bf_addr) = *(uint64_t *)ctrl;

	/* If UAR is mapped as WC (write combined) we need another fence to
	 * force write. Otherwise it may take a long time.
	 * On BF2/1 uar is mapped as NC (non combined) and fence is not needed
	 * here.
	 */
#if !defined(__aarch64__)
	if (!dv_qp->hw_qp.sq.tx_db_nc)
		snap_memory_bus_store_fence();
#endif

#else
	/* Based on review with Eliav:
	 * - only need a store fence to ensure that the wqe is committed to the
	 *   memory
	 * - there is no need to update dbr on DPA
	 *
	 * TODO: store outbox address in the qp as bf_addr instead of doing
	 * syscall
	 */
#if SIMX_BUILD
	snap_memory_cpu_store_fence();
	/* must for simx */
	((uint32_t *)dv_qp->hw_qp.dbr_addr)[MLX5_SND_DBR] = htobe32(dv_qp->hw_qp.sq.pi);
#endif
	snap_memory_bus_store_fence();
	dpa_dma_q_ring_tx_db(dv_qp->hw_qp.qp_num, dv_qp->hw_qp.sq.pi);
#endif
}

static inline void snap_dv_ring_rx_db(struct snap_dv_qp *dv_qp)
{
	snap_memory_cpu_store_fence();
	((uint32_t *)dv_qp->hw_qp.dbr_addr)[MLX5_RCV_DBR] = htobe32(dv_qp->hw_qp.rq.ci);
	snap_memory_bus_store_fence();
}

static inline void snap_dv_set_comp(struct snap_dv_qp *dv_qp, uint16_t pi,
				    struct snap_dma_completion *comp, int fm_ce_se, int n_bb)
{
	dv_qp->comps[pi].comp = comp;
	if ((fm_ce_se & MLX5_WQE_CTRL_CQ_UPDATE) != MLX5_WQE_CTRL_CQ_UPDATE) {
		dv_qp->n_outstanding += n_bb;
		return;
	}

	dv_qp->comps[pi].n_outstanding = dv_qp->n_outstanding + n_bb;
	dv_qp->n_outstanding = 0;
}

static inline void snap_dv_wqe_submit(struct snap_dv_qp *dv_qp, struct mlx5_wqe_ctrl_seg *ctrl)
{
	dv_qp->hw_qp.sq.pi++;
	if (dv_qp->db_flag == SNAP_DB_RING_BATCH) {
		dv_qp->tx_need_ring_db = true;
		dv_qp->ctrl = ctrl;
		return;
	}
	snap_dv_ring_tx_db(dv_qp, ctrl);
}

static inline void snap_dv_tx_complete(struct snap_dv_qp *dv_qp)
{
	if (dv_qp->tx_need_ring_db) {
		dv_qp->tx_need_ring_db = false;
		snap_dv_ring_tx_db(dv_qp, dv_qp->ctrl);
	}
}

static inline void snap_dv_post_recv(struct snap_dv_qp *dv_qp, void *addr,
				     size_t len, uint32_t lkey)
{
	struct mlx5_wqe_data_seg *dseg;

	dseg = (struct mlx5_wqe_data_seg *)(dv_qp->hw_qp.rq.addr + (dv_qp->hw_qp.rq.ci & (dv_qp->hw_qp.rq.wqe_cnt - 1)) *
					    SNAP_MLX5_RECV_WQE_BB);
	mlx5dv_set_data_seg(dseg, len, lkey, (intptr_t)addr);
	dv_qp->hw_qp.rq.ci++;
}

static inline void snap_dv_arm_cq(struct snap_hw_cq *cq)
{
#if !__DPA
	/* adopted from the uct_ib_mlx5dv_arm_cq() */
	uint32_t *dbrec = (uint32_t *)cq->dbr_addr;
	uint64_t sn_ci_cmd, doorbell;
	uint32_t sn, ci;

	sn = cq->cq_sn & 3;
	ci = cq->ci & 0xffffff;
	sn_ci_cmd = (sn << 28) | ci;

	/* we want any events */
	dbrec[SNAP_MLX5_CQ_ARM_DB] = htobe32(sn_ci_cmd);
	snap_memory_cpu_fence();

	doorbell = (sn_ci_cmd << 32) | cq->cq_num;
	*(uint64_t *)((uint8_t *)cq->uar_addr + MLX5_CQ_DOORBELL) = htobe64(doorbell);
	snap_memory_bus_store_fence();
	cq->cq_sn++;
#else
	snap_memory_cpu_fence();
	dpa_dma_q_arm_cq(cq->cq_num, cq->ci);
#endif
}

int dv_worker_progress_rx(struct snap_dma_worker *wk);
int dv_worker_progress_tx(struct snap_dma_worker *wk);
int dv_worker_flush(struct snap_dma_worker *wk);

extern struct snap_dma_q_ops verb_ops;
extern struct snap_dma_q_ops dv_ops;
extern struct snap_dma_q_ops gga_ops;

/* `n_bb`, `num_sge`, `l_sgl` and `r_sgl` are all output parameters */
static inline int snap_dma_build_sgl(struct snap_dma_q_io_attr *io_attr,
		int *n_bb, int *num_sge, struct ibv_sge **l_sgl, struct ibv_sge *r_sgl)
{
	int i, j, sge_cnt;
	size_t len_to_handle, left, offset;
	struct ibv_sge l_sge[io_attr->riov_cnt][SNAP_DMA_Q_MAX_SGE_NUM];

	/* TODO: this function should not be inline and should be moved to .c file */
	*n_bb = 0;
	left = 0;
	offset = 0;
	memset(num_sge, 0, sizeof(int) * io_attr->riov_cnt);

	for (i = 0, j = 0; i < io_attr->riov_cnt; i++) {
		len_to_handle = io_attr->riov[i].iov_len;
		sge_cnt = 0;

		while (j < io_attr->liov_cnt && len_to_handle > 0) {
			if (left != 0) {
				if (len_to_handle >= left) {
					len_to_handle -= left;
					l_sge[i][sge_cnt].addr = (uint64_t)(io_attr->liov[j].iov_base + offset);
					l_sge[i][sge_cnt].length = left;
					l_sge[i][sge_cnt].lkey = io_attr->lkey[j];
					j++;
					left = 0;
					offset = 0;
				} else {
					left -= len_to_handle;
					l_sge[i][sge_cnt].addr = (uint64_t)(io_attr->liov[j].iov_base + offset);
					l_sge[i][sge_cnt].length = len_to_handle;
					l_sge[i][sge_cnt].lkey = io_attr->lkey[j];
					offset += len_to_handle;
					len_to_handle = 0;
				}
			} else if (len_to_handle >= io_attr->liov[j].iov_len) {
				len_to_handle -= io_attr->liov[j].iov_len;
				l_sge[i][sge_cnt].addr = (uint64_t)io_attr->liov[j].iov_base;
				l_sge[i][sge_cnt].length = io_attr->liov[j].iov_len;
				l_sge[i][sge_cnt].lkey = io_attr->lkey[j];
				j++;
			} else {
				left = io_attr->liov[j].iov_len - len_to_handle;
				l_sge[i][sge_cnt].addr = (uint64_t)io_attr->liov[j].iov_base;
				l_sge[i][sge_cnt].length = len_to_handle;
				l_sge[i][sge_cnt].lkey = io_attr->lkey[j];
				offset = len_to_handle;
				len_to_handle = 0;
			}

			sge_cnt++;
			if (sge_cnt >= SNAP_DMA_Q_MAX_SGE_NUM) {
				snap_error("sge number exceed the max supported(30)\n");
				return -1;
			}
		}

		l_sgl[i] = l_sge[i];
		/* num_sge[i] is the sge cnt in l_sgl[i] for wr[i] */
		num_sge[i] = sge_cnt;

		r_sgl[i].addr = (uint64_t)io_attr->riov[i].iov_base;
		r_sgl[i].length = io_attr->riov[i].iov_len;
		r_sgl[i].lkey = io_attr->rkey[i];

		*n_bb += (sge_cnt <= 2) ? 1 : 1 + round_up((sge_cnt - 2), 4);
	}

	/* after for loop, j should equal to io_attr->liov_cnt,
	 *  and, left should be 0.
	 */

	return 0;
}
#endif /* SNAP_DMA_INTERNAL_H */
