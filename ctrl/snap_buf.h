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
#ifndef SNAP_BUF_H
#define SNAP_BUF_H
#include <stdint.h>
#include <stddef.h>
#include <infiniband/verbs.h>


#define SNAP_DCACHE_LINE 64

void *snap_buf_alloc(struct ibv_pd *pd, size_t size);
void snap_buf_free(void *buf);
uint32_t snap_buf_get_mkey(void *buf);
#endif
