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

#include "snap_dma.h"
#include "snap_dpa_p2p.h"

/**
 * snap_dpa_p2p_send_msg() - send p2p message
 * @q:    p2p queue
 * @msg:  message to send
 *
 * send a p2p message (DPU <-> DPA) using dma queue
 * q has to have credits to be able to send message
 *
 * Return: 0 on success or < 0 on error
 */
int snap_dpa_p2p_send_msg(struct snap_dpa_p2p_q *q, struct snap_dpa_p2p_msg *msg)
{
	int rc;

	if (!q->credit_count)
		return -EAGAIN;

	rc = snap_dma_q_send_completion(q->dma_q, (void *)msg,
			sizeof(struct snap_dpa_p2p_msg));
	--q->credit_count;

	return rc;
}

/**
 * snap_dpa_p2p_send_cr_update() - Create sw only DMA queue
 * @q:    protection domain to create qp
 * @credit:  amount of credits to send
 *
 * send a credit update p2p message
 *
 * Return: dma queue or NULL on error.
 */
int snap_dpa_p2p_send_cr_update(struct snap_dpa_p2p_q *q, int credit)
{
	struct snap_dpa_p2p_msg msg;

	msg.base.credit_delta = credit;
	msg.base.type = SNAP_DPA_P2P_MSG_CR_UPDATE;
	msg.base.qid = q->qid;

	return snap_dpa_p2p_send_msg(q, (struct snap_dpa_p2p_msg *) &msg);
}

/**
 * snap_dpa_p2p_send_vq_heads() - Receive new p2p messages
 * @q:    p2p queue
 * @msgs:  where to put all incoming messages
 * @n:  max number of messages can receive
 *
 * Non blocking receive of up to n new p2p messages
 *
 * Return: number of messages received
 */
int snap_dpa_p2p_recv_msg(struct snap_dpa_p2p_q *q, struct snap_dpa_p2p_msg *msgs, int n)
{
	int i, comps;
	struct snap_rx_completion rx_comps[n];
	struct snap_dma_completion *tx_comps[n];

	/* TODO: remove tx poll from here */
	snap_dma_q_poll_tx(q->dma_q, tx_comps, n);
	comps = snap_dma_q_poll_rx(q->dma_q, rx_comps, n);
	for (i = 0; i < comps; i++) {
		/* TODO: remove extra copy */
		memcpy(&msgs[i], rx_comps[i].data, sizeof(struct snap_dpa_p2p_msg));
	}

	return comps;
}

static int send_vq_update(struct snap_dpa_p2p_q *q, int cr_delta, int type,
			uint16_t vqid, uint16_t vqsize, uint16_t last_avail_index, uint16_t avail_index,
			uint64_t driver, uint32_t driver_mkey)
{
	struct snap_dpa_p2p_msg_vq_update msg;
	uint16_t desc_heads_count, avail_pos;
	int rc;
	uint64_t desc_hdr_idx_addr;

	msg.base.credit_delta = cr_delta;
	msg.base.type = type;
	msg.base.qid = vqid;
	msg.avail_index = avail_index;

	desc_heads_count = avail_index - last_avail_index;
	if (snap_unlikely(desc_heads_count > SNAP_DPA_P2P_VQ_MAX_HEADS))
		desc_heads_count = SNAP_DPA_P2P_VQ_MAX_HEADS;

	/* have to handle wrap around */
	avail_pos = last_avail_index & (vqsize - 1);
	if (snap_unlikely(avail_pos + desc_heads_count > vqsize))
		desc_heads_count = vqsize - avail_pos;

	msg.descr_head_count = desc_heads_count;

	/* offsetof(struct vring_avail, ring[last_avail_index]);*/
	desc_hdr_idx_addr = driver + 4 + (2 * avail_pos);
	rc = snap_dma_q_send(q->dma_q, &msg.base,
			sizeof(struct snap_dpa_p2p_msg_vq_update) - sizeof(msg.descr_heads),
			desc_hdr_idx_addr, desc_heads_count * sizeof(uint16_t), driver_mkey);
	if (snap_unlikely(rc))
		return rc;

	//--q->credit_count;

	return desc_heads_count;
}

/**
 * snap_dpa_p2p_send_vq_heads() - Send VQ heads message
 * @q:    p2p queue
 * @vqid:  virtio queue ID
 * @vqsize: virtio queue size
 * @last_avail_index:  index of previous available descriptor
 * @avail_index:  index of newest available descriptor
 * @driver:  descriptor address
 * @driver_mkey:  descriptor address mkey
 *
 * send to DPU a message that contains all new descriptor head indexes,
 * up to SNAP_DPA_P2P_VQ_MAX_HEADS
 *
 * Return: actual number of descriptor heads that were sent or < 0 on error
 */
int snap_dpa_p2p_send_vq_heads(struct snap_dpa_p2p_q *q, uint16_t vqid, uint16_t vqsize,
		uint16_t last_avail_index, uint16_t avail_index, uint64_t driver,
		uint32_t driver_mkey)
{
#if 0
	if (!q->credit_count)
		return -EAGAIN;
#endif

	return send_vq_update(q, 1, SNAP_DPA_P2P_MSG_VQ_HEADS, vqid, vqsize, last_avail_index,
			avail_index, driver, driver_mkey);
}

/**
 * snap_dpa_p2p_send_vq_table() - Send VQ table message
 * @q:    p2p queue
 * @vqid:  virtio queue ID
 * @vqsize: virtio queue size
 * @last_avail_index:  index of previous available descriptor
 * @avail_index:  index of newest available descriptor
 * @driver:  descriptor address
 * @driver_mkey:  descriptor address mkey
 * @descs:  descriptor table address in host
 * @shadow_descs:  descriptor table address in DPU p2p queue
 * @shadow_descs_mkey:  descriptor table mkey in DPU p2p queue
 *
 * two operations:
 * rdma_write to send entire descriptor area to DPU
 * followed by sending vq table message
 * (same as snap_dpa_p2p_send_vq_heads with a different type)
 *
 * Todo: vq size
 *
 * Return: actual number of descriptor heads that were sent or < 0 on error
 */
int snap_dpa_p2p_send_vq_table(struct snap_dpa_p2p_q *q,
		uint16_t vqid, uint16_t vqsize,
		uint16_t last_avail_index, uint16_t avail_index,
		uint64_t driver, uint32_t driver_mkey,
		uint64_t descs, uint64_t shadow_descs, uint32_t shadow_descs_mkey)
{
	int n, rc;

	//if (!q->credit_count)
	//	return -EAGAIN;

	/* TODO: need 2 avail to tx */
	rc = snap_dma_q_write(q->dma_q, (void *) descs,
			vqsize * SNAP_DPA_DESC_SIZE, driver_mkey, shadow_descs,
			shadow_descs_mkey, NULL);
	if (snap_unlikely(rc))
		return rc;

	n = send_vq_update(q, 1, SNAP_DPA_P2P_MSG_VQ_TABLE, vqid, vqsize, last_avail_index,
		 avail_index, driver, driver_mkey);

	return n;
}
