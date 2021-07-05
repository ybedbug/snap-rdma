#ifndef __COM_CONTEXT_DEV_H__
#define __COM_CONTEXT_DEV_H__

#include <common/flexio_common.h>

#define CHECKWORD 0xCACA0B0B

typedef struct {
	uint32_t cq_number;
	struct flexio_dev_cqe64 *cq_ring, *cqe;
	uint32_t cq_idx;
	uint8_t cq_hw_owner_bit;
	uint32_t *cq_dbr;
} cq_ctx_t;

typedef struct {
	uint32_t rq_number;
	struct flexio_dev_wqe_data_seg *rq_ring;
	uint32_t *rq_dbr;
} rq_ctx_t;

typedef struct {
	uint32_t sq_number;
	union flexio_dev_sqe_seg *sq_ring;
	uint32_t sq_wqe_idx;
	uint32_t *sq_dbr;
} sq_ctx_t;

typedef struct {
	void *sq_tx_buff;
	uint32_t tx_buff_idx;
} dt_ctx_t;

typedef struct {
	uint32_t eq_number;
	struct flexio_dev_eqe *eq_ring, *eqe;
	uint32_t eq_idx;
	uint8_t eq_hw_owner_bit;
} eq_ctx_t;

void com_cq_ctx_init(cq_ctx_t *cq_ctx, uint32_t num, flexio_uintptr_t ring_addr, flexio_uintptr_t dbr_addr);
void com_rq_ctx_init(rq_ctx_t *rq_ctx, uint32_t num, flexio_uintptr_t ring_addr, flexio_uintptr_t dbr_addr);
void com_sq_ctx_init(sq_ctx_t *sq_ctx, uint32_t num, flexio_uintptr_t ring_addr, flexio_uintptr_t dbr_addr);
void com_eq_ctx_init(eq_ctx_t *eq_ctx, uint32_t num, flexio_uintptr_t ring_addr);
void com_dt_ctx_init(dt_ctx_t *dt_ctx, flexio_uintptr_t buff_addr);

void com_step_cq(cq_ctx_t *cq_ctx, uint32_t cq_idx_mask);
void *get_next_dte(dt_ctx_t *dt_ctx, uint32_t dt_idx_mask, uint32_t log_dt_entry_sz);
void *get_next_sqe(sq_ctx_t *sq_ctx, uint32_t sq_idx_mask);
void com_rq_ack(rq_ctx_t *rq_ctx);
void com_step_eq(eq_ctx_t *eq_ctx, uint32_t eq_idx_mask);

#endif
