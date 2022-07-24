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
#ifndef _SNAP_DPA_P2P_H
#define _SNAP_DPA_P2P_H
#include "snap.h"
#include "snap_dpa_common.h"

/**
 * RC point to point QP is used to pass NVMe or VirtIO implementation specific
 * messages between DPA and DPU.
 *
 * One end of the RC qp sits on the DPU, another is used by the DPA thread. Each
 * DPA thread can have only one QP. DPU 'core' may communicate with several DPA
 * threads and thus may have several QPs. At the moment the number is assumed to
 * be small (<16): 16 DPU cores communicating with 256 DPA cores
 *
 * TODO: consider also adding command channel messages
 * TODO: find optimal number of the DPA threads per DPU core and improve
 * current implementation
 *
 * Theory of operation:
 * - Credit based, 1 credit represents one message received
 * - Message size is 64B (in order to fit into cache line)
 * - Credit updates count number of messages received since last update
 * - Credit updates are piggy backed or happen every N messages
 * - One credit is always reserved for the credit update message
 *
 * Example 1 (N = 8, Qdepth = 64)
 *  1. DPA:
 *     12x vq_update, each messages has 0 creadits, 52 credits left
 *  2. DPU:
 *     recv 8x vq_updates, send credit update, 8 credits, recv 4x vq_updates
 *  3. DPA:
 *     recv cr_update, +8 credits, 60 credits left
 *     send vq_update, +1 credit
 *     send 58x vq_updates, 0 credits 0 credits left
 *     sending more vq_update will fail (-EAGAIN)
 *     NOTE: one credit is reserved for cr_update message
 *  4. DPU:
 *     recv 59x vq_updates, +1 credit, 64 credits left
 *     send msix_msg, 59 + 4 credits (4 that was not reported in 1)
 *
 * Example 2: deadlock prevention
 * 1. DPA: send 63 vq_update, 0 credits, can't send more
 * 1. DPU: send 63 msix, 0 credits, can't send more
 * 2. Both sides recv 8 messages, send credit update for 8 messages in worst case
 * 3. Both sides recv more messages, can't send more credit updates or messages
 * 4. Both sides recv credit update (+8 credits) - no dead lock
 * 5. ....
 */
enum {
	SNAP_DPA_P2P_MSG_CR_UPDATE = 1,
	/* VirtIO specific messages */
	/* DPA->DPU */
	SNAP_DPA_P2P_MSG_VQ_HEADS = 20,
	SNAP_DPA_P2P_MSG_VQ_TABLE = 21,
	/* vq heads with valid table */
	SNAP_DPA_P2P_MSG_VQ_TABLE_CONT = 22,
	/* DPU->DPA */
	/* TODO: trigger msix directly from DPU */
	SNAP_DPA_P2P_MSG_VQ_MSIX = 30,

	/* NVMe specific messages */
	/* DPA->DPU */
	SNAP_DPA_P2P_MSG_NVME_SQ_HEAD = 40,
	SNAP_DPA_P2P_MSG_NVME_CQ_TAIL = 41,
	/* DPU->DPA */
	SNAP_DPA_P2P_MSGS_NVME_MSIX = 50
};

#define SNAP_DPA_P2P_CREDIT_COUNT 64
#define SNAP_DPA_P2P_MSG_LEN    64

/* TODO: consider bitfields and imm data in order to save size */
struct snap_dpa_p2p_msg_base {
	uint8_t type;
	uint8_t rsvd;
	uint16_t credit_delta;
	/* this is a 'queue id'. Unique per DPA thread (table index), and
	 * on DPU it is expanded to (dpa_tid, queue_id)
	 */
	uint16_t qid;
};

struct snap_dpa_p2p_msg {
	union {
		struct snap_dpa_p2p_msg_base base;
		char padding[SNAP_DPA_P2P_MSG_LEN];
	};
};

/* 64/2 - 3 - avail_index - descr_head_count = 27 */
#define SNAP_DPA_P2P_VQ_MAX_HEADS \
	((SNAP_DPA_P2P_MSG_LEN - sizeof(struct snap_dpa_p2p_msg_base)) \
		/sizeof(uint16_t) - 2)

#define SNAP_DPA_DESC_SIZE 16

/* same for head/table updates */
struct snap_dpa_p2p_msg_vq_update {
	struct snap_dpa_p2p_msg_base base;
	uint16_t avail_index;
	uint16_t descr_head_count;
	uint16_t descr_heads[SNAP_DPA_P2P_VQ_MAX_HEADS];
};

struct snap_dpa_p2p_msg_vq_msix {
	struct snap_dpa_p2p_msg_base base;
};

/**
 * struct snap_dpa_p2p_q - p2p protocol queue
 * @dma_q:        DMA queue (connected to DPA)
 * @qid:          queue ID
 * @credit_count: remaining message credits
 * @q_size:       descriptor table size
 */
struct snap_dpa_p2p_q {
	struct snap_dma_q *dma_q;
	int qid;
	int credit_count;
	uint64_t q_size;
};

int snap_dpa_p2p_send_msg(struct snap_dpa_p2p_q *q,
		struct snap_dpa_p2p_msg *msg);

int snap_dpa_p2p_recv_msg(struct snap_dpa_p2p_q *q,
		struct snap_dpa_p2p_msg **msgs, int n);

int snap_dpa_p2p_send_cr_update(struct snap_dpa_p2p_q *q, int credit);

int snap_dpa_p2p_send_vq_heads(struct snap_dpa_p2p_q *q, uint16_t vqid, uint16_t vqsize,
		uint16_t last_avail_index, uint16_t avail_index, uint64_t driver,
		uint32_t driver_mkey);

int snap_dpa_p2p_send_vq_table(struct snap_dpa_p2p_q *q,
		uint16_t vqid, uint16_t vqsize,
		uint16_t last_avail_index, uint16_t avail_index,
		uint64_t driver, uint32_t driver_mkey,
		uint64_t descs, uint64_t shadow_descs, uint32_t shadow_descs_mkey);


int snap_dpa_p2p_send_vq_table_cont(struct snap_dpa_p2p_q *q, uint16_t vqid, uint16_t vqsize,
		uint16_t last_avail_index, uint16_t avail_index, uint64_t driver,
		uint32_t driver_mkey);
#endif
