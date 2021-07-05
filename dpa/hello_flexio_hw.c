/*
 * Copyright (c) 2020 Nvidia Corporation. All rights reserved.
 */

#include <common/flexio_common.h>
#include <libflexio-dev/flexio_dev.h>
#include <libflexio-dev/flexio_dev_queue_access.h>
#include <libflexio-dev/flexio_dev_debug.h>
#include <string.h>
#include <stddef.h>
#include "hello_flexio_com.h"
#include "com_dev.h"

#define CQ_IDX_MASK ((1 << LOG_CQ_RING_SIZE) - 1)
#define RQ_IDX_MASK ((1 << LOG_RQ_RING_SIZE) - 1)
#define SQ_IDX_MASK ((1 << LOG_SQ_RING_SIZE) - 1)

static struct {
	uint32_t lkey;
	uint32_t checkword;

	cq_ctx_t rq_cq_ctx;     /* RQ CQ */
	rq_ctx_t rq_ctx;        /* RQ */
	sq_ctx_t sq_ctx;        /* SQ */
	cq_ctx_t sq_cq_ctx;     /* SQ CQ */
	dt_ctx_t dt_ctx;        /* SQ Data ring */
} app_ctx;

static void app_ctx_init(struct hello_flexio_data *data_from_host)
{
	if (data_from_host->reserved != HELLO_FLEXIO_MAGIC) {
		print_sim_str("BUG!!! Wrong data from host. Keyvalue ", 0);
		print_sim_hex(data_from_host->reserved, 0);
		print_sim_str(". Exiting...\n", 0);
		STUCK();
	}

	/* for debug purposes */
	app_ctx.checkword = CHECKWORD;

	app_ctx.lkey = data_from_host->lkey;

	com_cq_ctx_init(&app_ctx.rq_cq_ctx,
			data_from_host->rq_cq_data.cq_num,
			data_from_host->rq_cq_data.cq_ring_addr,
			data_from_host->rq_cq_data.cq_dbr_addr);

	com_rq_ctx_init(&app_ctx.rq_ctx,
			data_from_host->rq_data.rq_num,
			data_from_host->rq_data.rq_ring_addr,
			data_from_host->rq_data.rq_dbr_addr);

	com_sq_ctx_init(&app_ctx.sq_ctx,
			data_from_host->sq_data.sq_num,
			data_from_host->sq_data.sq_ring_addr,
			data_from_host->sq_data.sq_dbr_addr);

	com_cq_ctx_init(&app_ctx.sq_cq_ctx,
			data_from_host->sq_cq_data.cq_num,
			data_from_host->sq_cq_data.cq_ring_addr,
			data_from_host->sq_cq_data.cq_dbr_addr);

	com_dt_ctx_init(&app_ctx.dt_ctx, data_from_host->sq_data.sq_tx_buff);
}

flexio_dev_rpc_handler_t event_handler_init;
uint64_t event_handler_init(uint64_t thread_arg, uint64_t __unused arg2, uint64_t __unused arg3)
{
	print_sim_str("event_handler_init started\n", 0);

	flexio_dev_thread_init(NULL);
	app_ctx_init((void *)thread_arg);

	return 0;
}

static void process_packet(void)
{
	uint32_t rq_number;
	uint32_t rq_wqe_idx;
	struct flexio_dev_wqe_data_seg *rwqe;
	uint32_t data_sz;
	char *rq_data;
	char *sq_data;
	uint32_t sq_pi;
	union flexio_dev_sqe_seg *swqe;

	/* get out relevant data from CQE */
	rq_number = flexio_dev_cqe_qpn(app_ctx.rq_cq_ctx.cqe);
	rq_wqe_idx = flexio_dev_cqe_wqe_counter(app_ctx.rq_cq_ctx.cqe);
	data_sz = flexio_dev_cqe_byte_cnt(app_ctx.rq_cq_ctx.cqe);
	TRACEVAL(rq_number);
	TRACEVAL(rq_wqe_idx);

	/* Get RQ WQE pointed by CQE */
	rwqe = &app_ctx.rq_ctx.rq_ring[rq_wqe_idx & RQ_IDX_MASK];
	/* Extract data (whole packet) pointed by RQ WQE */
	rq_data = flexio_dev_rwqe_addr(rwqe);

	/* Take next entry from data ring */
	sq_data = get_next_dte(&app_ctx.dt_ctx, SQ_IDX_MASK, LOG_WQ_DATA_ENTRY_SIZE);

	/* Copy received packet to sq_data as is */
	memcpy(sq_data, rq_data, data_sz);

	/* swap mac address */
	swap_macs(sq_data);

	/* Primitive validation, that packet is our hardcoded */
	if (data_sz == 65)
	{
		/* modify UDP payload */
		strncpy(sq_data + 0x2a, "  APU was here", 65 - 0x2a);

		sq_data[0x2a] = "0123456789abcdef"[app_ctx.dt_ctx.tx_buff_idx & 0xf];
	}

	/* Take first segment for SQ WQE (3 segments will be used) */
	sq_pi = app_ctx.sq_ctx.sq_wqe_idx;
	swqe = get_next_sqe(&app_ctx.sq_ctx, SQ_IDX_MASK);

	/* Fill out 1-st segment (Control) */
	flexio_dev_swqe_seg1(swqe, sq_pi, app_ctx.sq_ctx.sq_number, 0);

	/* Fill out 2-nd segment (Ethernet) */
	swqe = get_next_sqe(&app_ctx.sq_ctx, SQ_IDX_MASK);
	flexio_dev_swqe_seg2(swqe);

	/* Fill out 3-rd segment (Data) */
	swqe = get_next_sqe(&app_ctx.sq_ctx, SQ_IDX_MASK);
	flexio_dev_swqe_seg3(swqe, data_sz, app_ctx.lkey, (uint64_t)sq_data);

	/* Update DBR and Ring the DB */
	flexio_dev_dbr_sq(app_ctx.sq_ctx.sq_dbr, app_ctx.sq_ctx.sq_wqe_idx);
	TRACE("ring the bell. QP - ", app_ctx.sq_ctx.sq_number);
	flexio_dev_ring_sq_db(NULL, sq_pi, app_ctx.sq_ctx.sq_number);

	flexio_dev_dbr_rq_ack(app_ctx.rq_ctx.rq_dbr);
}

flexio_dev_event_handler_t net_event_handler;
void net_event_handler(uint64_t __unused thread_arg, uint64_t __unused cq_arg)
{
	print_sim_str("net_event_handler started\n", 0);
	flexio_dev_thread_init(NULL);
	if (app_ctx.checkword != CHECKWORD)
	{
		print_sim_str("BUG!!! Process packet before context initialization\n", 0);
		STUCK();
	}

	while (flexio_dev_cqe_owner(app_ctx.rq_cq_ctx.cqe) != app_ctx.rq_cq_ctx.cq_hw_owner_bit)
	{
		process_packet();
		com_step_cq(&app_ctx.rq_cq_ctx, CQ_IDX_MASK);
	}
	print_sim_str("Nothing to do. Wait for next duar\n", 0);
	flexio_dev_return();
	print_sim_str("BUG? BUG! BUG!!\n", 0);
}
