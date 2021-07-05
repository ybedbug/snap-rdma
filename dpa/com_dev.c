#include <common/flexio_common.h>
#include <libflexio-dev/flexio_dev_queue_access.h>
#include <stddef.h>
#include "com_dev.h"

#define SWAP(a, b) do { \
		typeof(a) tmp;      \
		tmp = a;            \
		a = b;              \
		b = tmp;            \
} while (0)

void swap_macs(char *packet)
{
	char *dmac, *smac;
	int i;

	dmac = packet;
	smac = packet + 6;
	for (i = 0; i < 6; i++, dmac++, smac++)
		SWAP(*smac, *dmac);
}

void com_cq_ctx_init(cq_ctx_t *cq_ctx, uint32_t num, flexio_uintptr_t ring_addr, flexio_uintptr_t dbr_addr)
{
	cq_ctx->cq_number = num;
	cq_ctx->cq_ring = (struct flexio_dev_cqe64 *)ring_addr;
	cq_ctx->cq_dbr = (uint32_t *)dbr_addr;

	cq_ctx->cqe = cq_ctx->cq_ring;
	cq_ctx->cq_idx = 0;
	cq_ctx->cq_hw_owner_bit = 0x1;
}

void com_rq_ctx_init(rq_ctx_t *rq_ctx, uint32_t num, flexio_uintptr_t ring_addr, flexio_uintptr_t dbr_addr)
{
	rq_ctx->rq_number = num;
	rq_ctx->rq_ring = (struct flexio_dev_wqe_data_seg *)ring_addr;
	rq_ctx->rq_dbr = (uint32_t *)dbr_addr;
}

void com_sq_ctx_init(sq_ctx_t *sq_ctx, uint32_t num, flexio_uintptr_t ring_addr, flexio_uintptr_t dbr_addr)
{
	sq_ctx->sq_number = num;
	sq_ctx->sq_ring = (union flexio_dev_sqe_seg *)ring_addr;
	sq_ctx->sq_dbr = (uint32_t *)dbr_addr;

	sq_ctx->sq_wqe_idx = 0;
	sq_ctx->sq_dbr++; /* we will update send counter (offset 0x4) */
}

void com_eq_ctx_init(eq_ctx_t *eq_ctx, uint32_t num, flexio_uintptr_t ring_addr)
{
	eq_ctx->eq_number = num;
	eq_ctx->eq_ring = (struct flexio_dev_eqe *)ring_addr;

	eq_ctx->eqe = eq_ctx->eq_ring;
	eq_ctx->eq_idx = 0;
	eq_ctx->eq_hw_owner_bit = 0x1;
}

void com_dt_ctx_init(dt_ctx_t *dt_ctx, flexio_uintptr_t buff_addr)
{
	dt_ctx->sq_tx_buff = (void *)buff_addr;
	dt_ctx->tx_buff_idx = 0;
}

void com_step_cq(cq_ctx_t *cq_ctx, uint32_t cq_idx_mask)
{
	cq_ctx->cq_idx++;
	cq_ctx->cqe = &cq_ctx->cq_ring[cq_ctx->cq_idx & cq_idx_mask];
	/* check for wrap around */
	if (!(cq_ctx->cq_idx & cq_idx_mask))
		cq_ctx->cq_hw_owner_bit = !cq_ctx->cq_hw_owner_bit;

	flexio_dev_dbr_cq(cq_ctx->cq_dbr, cq_ctx->cq_idx);

	flexio_dev_ring_cq_db(NULL, cq_ctx->cq_idx, cq_ctx->cq_number);
}

void com_step_eq(eq_ctx_t *eq_ctx, uint32_t eq_idx_mask)
{
	uint32_t eq_ci;

	eq_ci = eq_ctx->eq_idx++;
	eq_ctx->eqe = &eq_ctx->eq_ring[eq_ctx->eq_idx & eq_idx_mask];
	/* check for wrap around */
	if (!(eq_ctx->eq_idx & eq_idx_mask))
		eq_ctx->eq_hw_owner_bit = !eq_ctx->eq_hw_owner_bit;

	/* No DBR */
	flexio_dev_ring_eq_db(NULL, eq_ci, eq_ctx->eq_number);
}

void *get_next_dte(dt_ctx_t *dt_ctx, uint32_t dt_idx_mask, uint32_t log_dt_entry_sz)
{
	return dt_ctx->sq_tx_buff + ((dt_ctx->tx_buff_idx++ & dt_idx_mask) << log_dt_entry_sz);
}

void *get_next_sqe(sq_ctx_t *sq_ctx, uint32_t sq_idx_mask)
{
	return &sq_ctx->sq_ring[sq_ctx->sq_wqe_idx++ & sq_idx_mask];
}

