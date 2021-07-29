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

#ifndef SNAP_DMA_H
#define SNAP_DMA_H

#include <infiniband/verbs.h>
#include <infiniband/mlx5dv.h>

#include "snap.h"

#define SNAP_DMA_Q_OPMODE   "SNAP_DMA_Q_OPMODE"

struct snap_dma_q;
struct snap_dma_completion;

/**
 * typedef snap_dma_rx_cb_t - receive callback
 * @q:        dma queue
 * @data:     received data. The buffer belongs to the queue, once the
 *            callback is completed the buffer content is going to
 *            be overwritten
 * @data_len: size of the received data
 * @imm_data: immediate data in the network order as defined in the IB spec
 *
 * The callback is called from within snap_dma_q_progress() when a new data
 * is received from the emulated device.
 *
 * The layout of the @data as well as the validity of @imm_data field depends
 * on the emulated device.  For example, in case of the NVMe emulation queue
 * @data will be a nvme sqe and @imm_data will be undefined.
 *
 * It is safe to initiate data transfers from withing the callback. However
 * it is not safe to destroy or modify the dma queue.
 */
typedef void (*snap_dma_rx_cb_t)(struct snap_dma_q *q, void *data,
		uint32_t data_len, uint32_t imm_data);

/**
 * typedef snap_dma_comp_cb_t - DMA operation completion callback
 * @comp:   user owned dma completion which was given  to the snap_dma_q_write()
 *          or to the snap_dma_q_read() function
 * @status: IBV_WC_SUCCESS (0) on success or anything else on error.
 *          See enum ibv_wc_status.
 *
 * The callback is called when dma operation is completed. It means
 * that either data has been successfully copied to the host memory or
 * an error has occured.
 *
 * It is safe to initiate data transfers from withing the callback. However
 * it is not safe to destroy or modify the dma queue.
 */
typedef void (*snap_dma_comp_cb_t)(struct snap_dma_completion *comp, int status);

struct mlx5_dma_opaque;

struct snap_dv_dma_completion {
	int n_outstanding;
	struct snap_dma_completion *comp;
};

struct snap_dv_qp {
	struct mlx5dv_qp  qp;
	uint16_t          pi;
	uint16_t          ci;
	int               n_outstanding;
	struct snap_dv_dma_completion *comps;
	/* used to hold GGA data */
	struct mlx5_dma_opaque     *opaque_buf;
	struct ibv_mr              *opaque_mr;
	/* true if tx db is in the non cacheable memory */
	bool tx_db_nc;
};

struct snap_dv_cq {
	struct mlx5dv_cq cq;
	uint16_t         ci;
};

struct snap_dma_ibv_qp {
	struct ibv_qp  *qp;
	struct ibv_cq  *tx_cq;
	struct ibv_cq  *rx_cq;
	struct ibv_mr  *rx_mr;
	char           *rx_buf;
	int            mode;

	/* used when working in devx mode */
	struct snap_dv_qp  dv_qp;
	struct snap_dv_cq  dv_tx_cq;
	struct snap_dv_cq  dv_rx_cq;
};

struct snap_dma_q_ops {
	int (*write)(struct snap_dma_q *q, void *src_buf, size_t len,
		     uint32_t lkey, uint64_t dstaddr, uint32_t rmkey,
		     struct snap_dma_completion *comp);
	int (*write_short)(struct snap_dma_q *q, void *src_buf, size_t len,
			   uint64_t dstaddr, uint32_t rmkey, int *n_bb);
	int (*read)(struct snap_dma_q *q, void *dst_buf, size_t len,
		    uint32_t lkey, uint64_t srcaddr, uint32_t rmkey,
		    struct snap_dma_completion *comp);
	int (*send_completion)(struct snap_dma_q *q, void *src_buf,
			size_t len, int *n_bb);
	int (*progress_tx)(struct snap_dma_q *q);
	int (*progress_rx)(struct snap_dma_q *q);
	int (*flush)(struct snap_dma_q *q);
	int (*arm)(struct snap_dma_q *q);
};

/**
 * struct snap_dma_q - DMA queue
 *
 * DMA queue is a connected pair of the IB queue pais (QPs). One QP
 * can be passed to the FW emulation objects such as NVMe
 * submission queue or VirtIO queue. Another QP can be used to:
 *
 *  - receive protocol related data. E.x. NVMe submission queue entry or SGL
 *  - send completion notifications. E.x. NVMe completion queue entry
 *  - read/write data from/to the host memory
 *
 * DMA queue is not thread safe. A caller must take care of the proper locking
 * if DMA queue is used by different threads.
 * However it is guaranteed that each queue is independent of others. It means
 * that no locking is needed as long as each queue is always used in the same
 * thread.
 */
struct snap_dma_q {
	/* private: */
	struct snap_dma_ibv_qp sw_qp;
	int                    tx_available;
	int                    tx_qsize;
	int                    tx_elem_size;
	int                    rx_elem_size;
	snap_dma_rx_cb_t       rx_cb;
	struct snap_dma_ibv_qp fw_qp;
	struct snap_dma_q_ops  *ops;
	/* public: */
	/** @uctx:  user supplied context */
	void                  *uctx;
};

enum {
	SNAP_DMA_Q_MODE_AUTOSELECT = 0,
	SNAP_DMA_Q_MODE_VERBS = 1,
	SNAP_DMA_Q_MODE_DV = 2,
	SNAP_DMA_Q_MODE_GGA = 3
};

/**
 * struct snap_dma_q_create_attr - DMA queue creation attributes
 * @tx_qsize:     send queue size of the software qp
 * @tx_elem_size: size of the completion. The size is emulation specific.
 *                For example 16 bytes for NVMe
 * @rx_qsize:     receive queue size of the software qp. In case if the qp is
 *                used with the NVMe SQ, @rx_qsize must be no less than the
 *                SQ size.
 * @rx_elem_size: size of the receive element. The size is emulation specific.
 *                For example 64 bytes for NVMe
 * @uctx:         user supplied context
 * @mode:         choose dma implementation:
 *                 SNAP_DMA_Q_MODE_AUTOSELECT - select best option automatically
 *                 SNAP_DMA_Q_MODE_VERBS - verbs, standard API, safest, slowest
 *                 SNAP_DMA_Q_MODE_DV    - dv, direct hw access, faster than verbs
 *                 SNAP_DMA_Q_MODE_GGA   - dv, plus uses hw dma engine directly to
 *                                         do rdma read or write. Fastest, best bandwidth.
 *                Mode choice can be overriden at runtime by setting SNAP_DMA_Q_OPMODE
 *                environment variable: 0 - autoselect, 1 - verbs, 2 - dv, 3 - gga.
 * @rx_cb:        receive callback. See &typedef snap_dma_rx_cb_t
 * @comp_channel: receive and DMA completion channel. See
 *                man ibv_create_comp_channel
 * @comp_vector:  completion vector
 * @comp_context: completion context that will be returned by the
 *                ibv_get_cq_event(). See man ibv_get_cq_event
 */
struct snap_dma_q_create_attr {
	int   tx_qsize;
	int   tx_elem_size;
	int   rx_qsize;
	int   rx_elem_size;
	void  *uctx;
	int   mode;
	snap_dma_rx_cb_t rx_cb;

	struct ibv_comp_channel *comp_channel;
	int                      comp_vector;
	void                    *comp_context;
};

/**
 * struct snap_dma_completion - completion handle and callback
 *
 * This structure should be allocated by the user and can be passed to communication
 * primitives. User has to initializes both fields of the structure.
 *
 * If snap_dma_q_write() or snap_dma_q_read() returns 0, this structure will be
 * in use until the DMA operation completes. When the DMA completes, @count
 * field is decremented by 1, and whenever it reaches 0 - the callback is called.
 *
 * Notes:
 *  - The same structure can be passed multiple times to communication functions
 *    without the need to wait for completion.
 *  - If the number of operations is smaller than the initial value of the counter,
 *    the callback will not be called at all, so it may be left undefined.
 */
struct snap_dma_completion {
	/** @func: callback function. See &typedef snap_dma_comp_cb_t */
	snap_dma_comp_cb_t func;
	/** @count: completion counter */
	int                count;
};

struct snap_dma_q *snap_dma_q_create(struct ibv_pd *pd,
		struct snap_dma_q_create_attr *attr);
void snap_dma_q_destroy(struct snap_dma_q *q);
int snap_dma_q_write(struct snap_dma_q *q, void *src_buf, size_t len,
		uint32_t lkey, uint64_t dstaddr, uint32_t rmkey,
		struct snap_dma_completion *comp);
int snap_dma_q_write_short(struct snap_dma_q *q, void *src_buf, size_t len,
		uint64_t dstaddr, uint32_t rmkey);
int snap_dma_q_read(struct snap_dma_q *q, void *dst_buf, size_t len,
		uint32_t lkey, uint64_t srcaddr, uint32_t rmkey,
		struct snap_dma_completion *comp);
int snap_dma_q_send_completion(struct snap_dma_q *q, void *src_buf, size_t len);
int snap_dma_q_progress(struct snap_dma_q *q);
int snap_dma_q_flush(struct snap_dma_q *q);
int snap_dma_q_arm(struct snap_dma_q *q);
struct ibv_qp *snap_dma_q_get_fw_qp(struct snap_dma_q *q);
int snap_dma_q_post_umr_wqe(struct snap_dma_q *q, struct mlx5_klm *klm_mtt,
			int klm_entries, struct snap_indirect_mkey *klm_mkey,
			struct snap_dma_completion *comp);

/**
 * snap_dma_q_ctx - get queue context
 * @q: dma queue
 *
 * Returns: dma queue context
 */
static inline void *snap_dma_q_ctx(struct snap_dma_q *q)
{
	return q->uctx;
}

/* how many tx and rx completions to process during a signle progress call */
#define SNAP_DMA_MAX_TX_COMPLETIONS  64
#define SNAP_DMA_MAX_RX_COMPLETIONS  16

/* align start of the receive buffer on 4k boundary */
#define SNAP_DMA_RX_BUF_ALIGN    4096
#define SNAP_DMA_BUF_ALIGN       4096

/* create params */
#define SNAP_DMA_FW_QP_MIN_SEND_WR 32

/* INIT state params */
#define SNAP_DMA_QP_PKEY_INDEX  0
#define SNAP_DMA_QP_PORT_NUM    1

/* RTR state params */
#define SNAP_DMA_QP_RQ_PSN              0x4242
#define SNAP_DMA_QP_MAX_DEST_RD_ATOMIC      16
#define SNAP_DMA_QP_RNR_TIMER               12
#define SNAP_DMA_QP_HOP_LIMIT               64
#define SNAP_DMA_QP_GID_INDEX                0

/* RTS state params */
#define SNAP_DMA_QP_TIMEOUT            14
#define SNAP_DMA_QP_RETRY_COUNT         7
#define SNAP_DMA_QP_RNR_RETRY           7
#define SNAP_DMA_QP_MAX_RD_ATOMIC      16
#define SNAP_DMA_QP_SQ_PSN         0x4242

#endif
