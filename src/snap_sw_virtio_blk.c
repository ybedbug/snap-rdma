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

#include "snap.h"
#include "snap_virtio_common.h"
#include "snap_virtio_blk.h"

enum sw_queue_prog_state {
	READ_AVAILABLE_IDX,
	READ_HEADER_IDX,
	NEW_CMD_PROC,
};

struct snap_virtio_blk_sw_queue {
	struct snap_virtio_blk_queue vbq;
	struct snap_dma_q	*dma_q;
	uint64_t			driver_addr;
	uint64_t			q_size;
	uint16_t			prev_avail;
	uint16_t			avail_idx;
	struct ibv_mr		*avail_mr;
	uint16_t			desc_head_idx;
	struct ibv_mr		*desc_head_mr;
	bool				read_done;
	enum sw_queue_prog_state prog_state;
	struct snap_dma_completion avail_read;

};


static inline struct snap_virtio_blk_sw_queue *
to_sw_queue(struct snap_virtio_blk_queue *vbq)
{
	return container_of(vbq, struct snap_virtio_blk_sw_queue,
			vbq);
}

static void sw_queue_prog_read_cb(struct snap_dma_completion *comp, int status)
{
	struct snap_virtio_blk_sw_queue *sw_q = container_of(comp,
			struct snap_virtio_blk_sw_queue, avail_read);

	sw_q->read_done = true;
}

static struct snap_virtio_queue *
snap_virtio_blk_create_sw_queue(struct snap_device *sdev,
				struct snap_virtio_common_queue_attr *attr)
{
	struct snap_cross_mkey *snap_cross_mkey;

	struct snap_virtio_blk_sw_queue *swq = malloc(sizeof(struct snap_virtio_blk_sw_queue));

	if (!swq)
		goto out;
	snap_cross_mkey = snap_create_cross_mkey(attr->vattr.pd, sdev);
	if (!snap_cross_mkey) {
		snap_error("Failed to create snap MKey Entry for blk queue\n");
		goto out;
	}
	swq->vbq.virtq.snap_cross_mkey = snap_cross_mkey;
	attr->vattr.dma_mkey = snap_cross_mkey->mkey;
	swq->avail_idx = 0;
	swq->avail_mr = snap_reg_mr(attr->qp->pd,
			&swq->avail_idx, sizeof(uint16_t));
	if (!swq->avail_mr) {
		snap_error("failed to register avail_mr\n");
		goto rel_q;
	}
	swq->desc_head_mr = snap_reg_mr(attr->qp->pd,
			&swq->desc_head_idx, sizeof(uint16_t));
	if (!swq->desc_head_mr) {
		snap_error("failed to register avail_mr\n");
		goto dereg_avail;
	}
	swq->prev_avail = 0;
	swq->prog_state = 0;
	swq->avail_read.func = sw_queue_prog_read_cb;
	swq->driver_addr = attr->vattr.driver;
	swq->q_size = attr->vattr.size;
	attr->q_provider = SNAP_SW_Q_PROVIDER;
	swq->dma_q = attr->dma_q;

	return &swq->vbq.virtq;

dereg_avail:
	ibv_dereg_mr(swq->avail_mr);
rel_q:
	free(swq);
out:
	return NULL;
}

static int snap_virtio_blk_destroy_sw_queue(struct snap_virtio_queue *vq)
{
	int mkey_ret;
	struct snap_virtio_blk_sw_queue *swq = to_sw_queue(to_blk_queue(vq));

	mkey_ret = snap_destroy_cross_mkey(vq->snap_cross_mkey);
	ibv_dereg_mr(swq->avail_mr);
	ibv_dereg_mr(swq->desc_head_mr);
	free(swq);

	return mkey_ret;
}

static int snap_virtio_blk_query_sw_queue(struct snap_virtio_queue *vbq,
		struct snap_virtio_common_queue_attr *attr)
{
	return 0;
}

static int snap_virtio_blk_modify_sw_queue(struct snap_virtio_queue *vq,
		uint64_t mask, struct snap_virtio_common_queue_attr *attr)
{
	return 0;
}

static int snap_virtio_blk_progress_sw_queue(struct snap_virtio_queue *vq)
{
	struct snap_virtio_blk_sw_queue *sw_q = to_sw_queue(to_blk_queue(vq));
	uint64_t avail_idx_addr  = sw_q->driver_addr + offsetof(struct vring_avail, idx);
	uint64_t desc_hdr_idx_addr;
	int ret;

	switch (sw_q->prog_state) {
	case READ_AVAILABLE_IDX:
		sw_q->avail_read.count = 1;
		sw_q->read_done = false;
		ret = snap_dma_q_read(sw_q->dma_q, &sw_q->avail_idx, sizeof(uint16_t),
				sw_q->avail_mr->lkey, avail_idx_addr, sw_q->vbq.virtq.snap_cross_mkey->mkey, &sw_q->avail_read);
		if (snap_unlikely(ret)) {
			snap_error("failed DMA read vring_available for drv: 0x%lx\n",
					sw_q->driver_addr);
			return ret;
		}
		sw_q->prog_state = READ_HEADER_IDX;
		break;
	case READ_HEADER_IDX:
		if (sw_q->read_done) {
			sw_q->read_done = false;
			if ((sw_q->avail_idx % sw_q->q_size) != sw_q->prev_avail) {
				desc_hdr_idx_addr = sw_q->driver_addr +
					offsetof(struct vring_avail, ring[sw_q->prev_avail]);
				sw_q->prev_avail = (sw_q->prev_avail + 1) % sw_q->q_size;
				sw_q->avail_read.count = 1;
				ret = snap_dma_q_read(sw_q->dma_q, &sw_q->desc_head_idx, sizeof(uint16_t),
						sw_q->desc_head_mr->lkey, desc_hdr_idx_addr, sw_q->vbq.virtq.snap_cross_mkey->mkey, &sw_q->avail_read);
				if (snap_unlikely(ret)) {
					snap_error("failed DMA read descriptor head idx for drv: 0x%lx\n",
							sw_q->driver_addr);
					return ret;
				}
				sw_q->prog_state = NEW_CMD_PROC;
			} else
				sw_q->prog_state = READ_AVAILABLE_IDX;
		}
		break;
	case NEW_CMD_PROC:
		if (sw_q->read_done) {
			struct virtq_split_tunnel_req_hdr hdr;

			sw_q->read_done = false;
			hdr.descr_head_idx = sw_q->desc_head_idx;
			hdr.num_desc = 0;
			sw_q->dma_q->rx_cb(sw_q->dma_q, &hdr, 0, 0);
			sw_q->prog_state = READ_AVAILABLE_IDX;
		}
		break;
	}
	return 0;
}

static struct virtq_q_ops snap_virtq_blk_sw_ops = {
	.create = snap_virtio_blk_create_sw_queue,
	.destroy = snap_virtio_blk_destroy_sw_queue,
	.query = snap_virtio_blk_query_sw_queue,
	.modify = snap_virtio_blk_modify_sw_queue,
	.progress = snap_virtio_blk_progress_sw_queue,
};

struct virtq_q_ops *get_sw_queue_ops(void)
{
	return &snap_virtq_blk_sw_ops;
}
