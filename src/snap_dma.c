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
#include <stdint.h>

#include "config.h"
#include "snap_dma_internal.h"

/**
 * snap_dma_q_progress() - Progress dma queue
 * @q: dma queue
 *
 * The function progresses both send and receive operations on the given dma
 * queue.
 *
 * Send &typedef snap_dma_comp_cb_t and receive &typedef snap_dma_rx_cb_t
 * completion callbacks may be called from within this function.
 * It is guaranteed that such callbacks are called in the execution context
 * of the progress.
 *
 * If dma queue was created with a completion channel then one can
 * use it's file descriptor to check for events instead of the
 * polling. When event is detected snap_dma_q_progress() should
 * be called to process it.
 *
 * Return: number of events (send and receive) that were processed
 */
int snap_dma_q_progress(struct snap_dma_q *q)
{
	int n;

	n = q->ops->progress_tx(q);
	n += q->ops->progress_rx(q);
	return n;
}

/**
 * snap_dma_q_poll_rx() - Poll rx from dma queue
 * @q: dma queue
 * @rx_completions: array that stores polled events
 * @max_completions: max supported events
 *
 * The function polls receive operations on the given dma
 * queue. each operation is inserted into the rx_completions array
 *
 * Note: rx_buffers belong to the rx queue and will eventually be overwritten.
 * The upper level protocol should implement flow control.
 * Buffers that are needed for the long term should be copied.
 *
 * Return: number of send events that were polled
 */
int snap_dma_q_poll_rx(struct snap_dma_q *q, struct snap_rx_completion *rx_completions, int max_completions)
{
	return q->ops->poll_rx(q, rx_completions, max_completions);
}

/**
 * snap_dma_q_poll_tx() - Poll tx from dma queue
 * @q: dma queue
 * @comp: array of pointers that stores polled events
 * @max_completions: max supported events
 *
 * The function polls send operations on the given dma
 * queue. each operation is inserted into the comp array
 *
 * Return: number of send events that were polled
 */
int snap_dma_q_poll_tx(struct snap_dma_q *q, struct snap_dma_completion **comp, int max_completions)
{
	return q->ops->poll_tx(q, comp, max_completions);
}

/**
 * snap_dma_q_arm() - Request notification
 * @q: dma queue
 *
 * The function 'arms' dma queue to report send and receive events over its
 * completion channel.
 *
 * Not available on DPA
 *
 * Return:  0 or -errno on error
 */
int snap_dma_q_arm(struct snap_dma_q *q)
{
	return q->ops->arm(q);
}

/**
 * snap_dma_q_flush() - Wait for outstanding operations to complete
 * @q:   dma queue
 *
 * The function waits until all outstanding operations started with
 * mlx_dma_q_read(), mlx_dma_q_write() or mlx_dma_q_send_completion() are
 * finished. The function does not progress receive operation.
 *
 * The purpose of this function is to facilitate blocking mode dma
 * and completion operations.
 *
 * Return: number of completed operations or -errno.
 */
int snap_dma_q_flush(struct snap_dma_q *q)
{
	return q->ops->flush(q);
}

/**
 * snap_dma_q_write() - DMA write to the host memory
 * @q:            dma queue
 * @src_buf:      where to get/put data
 * @len:          data length
 * @lkey:         local memory key
 * @dstaddr:      host physical or virtual address
 * @rmkey:        host memory key that describes remote memory
 * @comp:         dma completion structure
 *
 * The function starts non blocking memory transfer to the host memory. Once
 * data transfer is completed the user defined callback may be called.
 * Operations on the same dma queue are done in order.
 *
 * Return:
 * 0
 *	operation has been successfully submitted to the queue
 *	and is now in progress
 * \-EAGAIN
 *	queue does not have enough resources, must be retried later
 * < 0
 *	some other error has occured. Return value is -errno
 */
int snap_dma_q_write(struct snap_dma_q *q, void *src_buf, size_t len,
		     uint32_t lkey, uint64_t dstaddr, uint32_t rmkey,
		     struct snap_dma_completion *comp)
{
	int rc;

	if (snap_unlikely(!qp_can_tx(q, 1)))
		return -EAGAIN;

	rc = q->ops->write(q, src_buf, len, lkey, dstaddr, rmkey, comp);
	if (snap_unlikely(rc))
		return rc;

	q->tx_available--;
	return 0;
}

/**
 * snap_dma_q_writev() - DMA write to the host memory
 * @q:            dma queue
 * @src_buf:      where to get data
 * @lkey:         local memory key
 * @iov:          A scatter gather list of buffers to be write into
 * @iov_cnt:      The number of elements in @iov
 * @rmkey:        host memory key that describes remote memory
 * @comp:         dma completion structure
 *
 * The function starts non blocking memory transfer to the host memory,
 * those memory described in a scatter gather list.
 * Once data transfer is completed the user defined callback may be called.
 * Operations on the same dma queue are done in order.
 *
 * Return:
 * 0
 *     operation has been successfully submitted to the queue
 *     and is now in progress
 * \-EAGAIN
 *     queue does not have enough resources, must be retried later
 * \--ENOTSUP
 *     queue does not support write by provide a scatter gather list of buffers
 * < 0
 *     some other error has occured. Return value is -errno
 */
int snap_dma_q_writev(struct snap_dma_q *q, void *src_buf, uint32_t lkey,
				struct iovec *iov, int iov_cnt, uint32_t rmkey,
				struct snap_dma_completion *comp)
{
	int rc, n_bb;
	struct snap_dma_q_io_attr io_attr = {0};

	io_attr.io_type = SNAP_DMA_Q_IO_TYPE_IOV;
	io_attr.lbuf = src_buf;
	io_attr.lkey = lkey;
	io_attr.rkey = rmkey;
	io_attr.iov = iov;
	io_attr.iov_cnt = iov_cnt;

	rc = q->ops->writev(q, &io_attr, comp, &n_bb);
	if (snap_unlikely(rc))
		return rc;

	q->tx_available -= n_bb;

	return 0;
}

/**
 * snap_dma_q_writec() - DMA write to the host memory, and
 *                          do inline date decryption
 * @q:            dma queue
 * @src_buf:      where to get data
 * @lkey:         local memory key
 * @iov:          A scatter gather list of buffers to be write into
 * @iov_cnt:      The number of elements in @iov
 * @rmkey:        host memory key that describes remote memory
 * @dek_obj_id:   DEK Object ID
 * @comp:         dma completion structure
 *
 * The function starts non blocking memory transfer to the host memory. Once
 * data transfer is completed the user defined callback may be called.
 * Operations on the same dma queue are done in order.
 *
 * Return:
 * 0
 *	operation has been successfully submitted to the queue
 *	and is now in progress
 * \-EAGAIN
 *	queue does not have enough resources, must be retried later
 * < 0
 *	some other error has occured. Return value is -errno
 */
int snap_dma_q_writec(struct snap_dma_q *q, void *src_buf, uint32_t lkey,
			struct iovec *iov, int iov_cnt, uint32_t rmkey,
			uint32_t dek_obj_id, struct snap_dma_completion *comp)
{
	int rc, n_bb;
	struct snap_dma_q_io_attr io_attr = {0};

	io_attr.io_type = SNAP_DMA_Q_IO_TYPE_IOV | SNAP_DMA_Q_IO_TYPE_ENCRYPTO;
	io_attr.lbuf = src_buf;
	io_attr.lkey = lkey;
	io_attr.rkey = rmkey;
	io_attr.iov = iov;
	io_attr.iov_cnt = iov_cnt;
	io_attr.dek_obj_id = dek_obj_id;

	rc = q->ops->writec(q, &io_attr, comp, &n_bb);
	if (snap_unlikely(rc))
		return rc;

	q->tx_available -= n_bb;

	return 0;
}

/**
 * snap_dma_q_write_short() - DMA write of small amount of data to the
 *                            host memory
 * @q:            dma queue
 * @src_buf:      where to get data
 * @len:          data length. It must be no greater than the
 *                &struct snap_dma_q_create_attr.tx_elem_size
 * @dstaddr:      host physical or virtual address
 * @rmkey:        host memory key that describes remote memory
 *
 * The function starts non blocking memory transfer to the host memory. The
 * function is optimized to reduce latency when sending small amount of data.
 * Operations on the same dma queue are done in order.
 *
 * Note that it is safe to use @src_buf after the function returns.
 *
 * Return:
 * 0
 *	operation has been successfully submitted to the queue
 * \-EAGAIN
 *	queue does not have enough resources, must be retried later
 * < 0
 *	some other error has occured. Return value is -errno
 */
int snap_dma_q_write_short(struct snap_dma_q *q, void *src_buf, size_t len,
			   uint64_t dstaddr, uint32_t rmkey)
{
	int rc, n_bb;

	if (snap_unlikely(len > q->tx_elem_size))
		return -EINVAL;

	rc = q->ops->write_short(q, src_buf, len, dstaddr, rmkey, &n_bb);
	if (snap_unlikely(rc))
		return rc;

	q->tx_available -= n_bb;
	return 0;
}

/**
 * snap_dma_q_read() - DMA read from the host memory
 * @q:            dma queue
 * @dst_buf:      where to get/put data
 * @len:          data length
 * @lkey:         local memory key
 * @srcaddr:      host physical or virtual address
 * @rmkey:        host memory key that describes remote memory
 * @comp:         dma completion structure
 *
 * The function starts non blocking memory transfer from the host memory. Once
 * data transfer is completed the user defined callback may be called.
 * Operations on the same dma queue are done in order.
 *
 * Return:
 * 0
 *	operation has been successfully submitted to the queue
 *	and is now in progress
 * \-EAGAIN
 *	queue does not have enough resources, must be retried later
 * < 0
 *	some other error has occured. Return value is -errno
 */
int snap_dma_q_read(struct snap_dma_q *q, void *dst_buf, size_t len,
		    uint32_t lkey, uint64_t srcaddr, uint32_t rmkey,
		    struct snap_dma_completion *comp)
{
	int rc;

	if (snap_unlikely(!qp_can_tx(q, 1)))
		return -EAGAIN;

	rc = q->ops->read(q, dst_buf, len, lkey, srcaddr, rmkey, comp);
	if (snap_unlikely(rc))
		return rc;

	q->tx_available--;
	return 0;
}

/**
 * snap_dma_q_readv() - DMA read from the host memory
 * @q:            dma queue
 * @dst_buf:      where to put data
 * @lkey:         local memory key
 * @iov:          A scatter gather list of buffers to be read from
 * @iov_cnt:      The number of elements in @iov
 * @rmkey:        host memory key that describes remote memory
 * @comp:         dma completion structure
 *
 * The function starts non blocking memory transfer from the host memory,
 * those memory described in a scatter gather list.
 * Once data transfer is completed the user defined callback may be called.
 * Operations on the same dma queue are done in order.
 *
 * Return:
 * 0
 *     operation has been successfully submitted to the queue
 *     and is now in progress
 * \-EAGAIN
 *     queue does not have enough resources, must be retried later
 * \--ENOTSUP
 *     queue does not support read by provide a scatter gather list of buffers
 * < 0
 *     some other error has occured. Return value is -errno
 */
int snap_dma_q_readv(struct snap_dma_q *q, void *dst_buf, uint32_t lkey,
				struct iovec *iov, int iov_cnt, uint32_t rmkey,
				struct snap_dma_completion *comp)
{
	int rc, n_bb;
	struct snap_dma_q_io_attr io_attr = {0};

	io_attr.io_type = SNAP_DMA_Q_IO_TYPE_IOV;
	io_attr.lbuf = dst_buf;
	io_attr.lkey = lkey;
	io_attr.rkey = rmkey;
	io_attr.iov = iov;
	io_attr.iov_cnt = iov_cnt;

	rc = q->ops->readv(q, &io_attr, comp, &n_bb);
	if (snap_unlikely(rc))
		return rc;

	q->tx_available -= n_bb;

	return 0;
}

/**
 * snap_dma_q_readc() - DMA read from the host memory, and
 *                         do inline data encryption
 * @q:            dma queue
 * @dst_buf:      where to put data
 * @lkey:         local memory key
 * @iov:          A scatter gather list of buffers to be read from
 * @iov_cnt:      The number of elements in @iov
 * @rmkey:        host memory key that describes remote memory
 * @dek_obj_id:   DEK Object ID
 * @comp:         dma completion structure
 *
 * The function starts non blocking memory transfer from the host memory. Once
 * data transfer is completed the user defined callback may be called.
 * Operations on the same dma queue are done in order.
 *
 * Return:
 * 0
 *	operation has been successfully submitted to the queue
 *	and is now in progress
 * \-EAGAIN
 *	queue does not have enough resources, must be retried later
 * < 0
 *	some other error has occured. Return value is -errno
 */
int snap_dma_q_readc(struct snap_dma_q *q, void *dst_buf, uint32_t lkey,
			struct iovec *iov, int iov_cnt, uint32_t rmkey,
		    uint32_t dek_obj_id, struct snap_dma_completion *comp)
{
	int rc, n_bb;
	struct snap_dma_q_io_attr io_attr = {0};

	io_attr.io_type = SNAP_DMA_Q_IO_TYPE_IOV | SNAP_DMA_Q_IO_TYPE_ENCRYPTO;
	io_attr.lbuf = dst_buf;
	io_attr.lkey = lkey;
	io_attr.rkey = rmkey;
	io_attr.iov = iov;
	io_attr.iov_cnt = iov_cnt;
	io_attr.dek_obj_id = dek_obj_id;

	rc = q->ops->readc(q, &io_attr, comp, &n_bb);
	if (snap_unlikely(rc))
		return rc;

	q->tx_available -= n_bb;

	return 0;
}

/**
 * snap_dma_q_send_completion() - Send completion to the host
 * @q:       dma queue to
 * @src_buf: local buffer to copy the completion data from.
 * @len:     the length of completion. E.x. 16 bytes for the NVMe. It
 *           must be no greater than the value of the
 *           &struct snap_dma_q_create_attr.tx_elem_size
 *
 * The function sends a completion notification to the host. The exact meaning of
 * the 'completion' is defined by the emulation layer. For example in case of
 * NVMe it means that completion entry is placed in the completion queue and
 * MSI-X interrupt is triggered.
 *
 * Note that it is safe to use @src_buf after the function returns.
 *
 * Return:
 * 0
 *	operation has been successfully submitted to the queue
 *	and is now in progress
 * \-EAGAIN
 *	queue does not have enough resources, must be retried later
 * < 0
 *	some other error has occured. Return value is -errno
 *
 */
int snap_dma_q_send_completion(struct snap_dma_q *q, void *src_buf, size_t len)
{
	int rc, n_bb;

	if (snap_unlikely(len > q->tx_elem_size))
		return -EINVAL;

	rc = q->ops->send_completion(q, src_buf, len, &n_bb);
	if (snap_unlikely(rc))
		return rc;

	q->tx_available -= n_bb;
	return 0;
}

/**
 * snap_dma_q_get_fw_qp() - Get FW qp
 * @q:   dma queue
 *
 * The function returns qp that can be used by the FW emulation objects
 * See snap_dma_q_create() for the detailed explanation
 *
 * Not valid on the DPA
 *
 * Return: fw qp
 */
struct ibv_qp *snap_dma_q_get_fw_qp(struct snap_dma_q *q)
{
#if !defined(__DPA)
	return snap_qp_to_verbs_qp(q->fw_qp.qp);
#else
	return NULL;
#endif
}

/**
 * snap_dma_q_send() - Send data segments (inline and memory pointer)
 * @q:       dma queue
 * @in_buf:  local buffer to copy the inline data from.
 * @in_len:  the length of inline data.
 * @addr:    address of memory pointer data segment
 * @len:     length of data in memory pointer
 * @key:     local key of the memory pointer data segment
 *
 * The function sends data segments in the following way:
 * first an inline data segment followed by a memory pointer data segment
 * total length (in_len + len) must be no greater than the value of the
 * &struct snap_dma_q_create_attr.tx_elem_size
 *
 * Return:
 * 0
 *	operation has been successfully submitted to the queue
 *	and is now in progress
 * \-EAGAIN
 *	queue does not have enough resources, must be retried later
 * < 0
 *	some other error has occured. Return value is -errno
 *
 */
int snap_dma_q_send(struct snap_dma_q *q, void *in_buf, size_t in_len,
		uint64_t addr, size_t len, uint32_t key)
{
	int n_bb, rc;

	rc = q->ops->send(q, in_buf, in_len, addr, len, key, &n_bb);
	if (snap_unlikely(rc))
		return rc;

	q->tx_available -= n_bb;

	return 0;
}
