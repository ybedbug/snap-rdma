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

#define _ISOC11_SOURCE //For aligned_alloc

#include <stdlib.h>
#include <stdint.h>
#include "snap_buf.h"
#include "snap_macros.h"
#include "snap_mr.h"


struct snap_buf {
	struct ibv_mr *mr;
	uint8_t padding[SNAP_DCACHE_LINE - sizeof(struct ibv_mr *)];
	uint8_t ubuf[];
};

void *snap_buf_alloc(struct ibv_pd *pd, size_t size)
{
	struct snap_buf *buf;

	buf = aligned_alloc(SNAP_DCACHE_LINE, size + SNAP_DCACHE_LINE);
	if (!buf)
		return NULL;
	memset(buf, 0, size);

	buf->mr = snap_reg_mr(pd, buf->ubuf, size);
	if (!buf->mr) {
		free(buf);
		return NULL;
	}

	return buf->ubuf;
}

void snap_buf_free(void *ubuf)
{
	struct snap_buf *buf = container_of(ubuf, struct snap_buf, ubuf);

	ibv_dereg_mr(buf->mr);
	free(buf);
}

uint32_t snap_buf_get_mkey(void *ubuf)
{
	const struct snap_buf *buf = container_of(ubuf, struct snap_buf, ubuf);

	return buf->mr->lkey;
}
