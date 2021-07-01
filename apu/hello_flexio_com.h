#ifndef __HELLO_FLEXIO_COM_H__
#define __HELLO_FLEXIO_COM_H__

#include <common/flexio_common.h>

#define HELLO_FLEXIO_MAGIC 0x12345678

#define LOG_CQ_RING_SIZE 7
#define LOG_RQ_RING_SIZE 7
#define LOG_SQ_RING_SIZE 7

#define LOG_WQ_DATA_ENTRY_SIZE 11

/* Transport data from HOST application to HW application */
struct hello_flexio_data {
	uint32_t lkey;
	uint32_t reserved;                // Alignment for 8B
	struct flexio_hw_cq rq_cq_data;
	struct flexio_hw_rq rq_data;
	struct flexio_hw_cq sq_cq_data;
	struct flexio_hw_sq sq_data;
} __attribute__((__packed__, aligned(8)));

#endif
