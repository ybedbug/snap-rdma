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

#ifndef _SNAP_DP_MAP_H_
#define _SNAP_DP_MAP_H_

#include <stdbool.h>

struct snap_dp_map;
struct snap_vq_adm_sge;

/* page set */
struct snap_dp_map *snap_dp_map_create(unsigned page_size);
void snap_dp_map_destroy(struct snap_dp_map *map);

int snap_dp_map_add_range(struct snap_dp_map *map, uint64_t pa, uint32_t length);
size_t snap_dp_map_get_size(struct snap_dp_map *map);
int snap_dp_map_serialize(struct snap_dp_map *map, uint64_t *buf, uint32_t length);

/* page bit/byte map */

struct snap_dp_bmap;

struct snap_dp_bmap *snap_dp_bmap_create(struct snap_vq_adm_sge *sge_list, int sge_count,
		unsigned page_size, bool is_bytemap);
void snap_dp_bmap_destroy(struct snap_dp_bmap *map);

uint32_t snap_dp_bmap_range_size(struct snap_dp_bmap *map, uint64_t pa, uint32_t length);
uint32_t snap_dp_bmap_get_start_pa(struct snap_dp_bmap *map, uint64_t pa, uint32_t length,
		uint64_t *start_pa, int *byte_offset, uint32_t *num_pages);

void snap_dp_bmap_set_mkey(struct snap_dp_bmap *map, uint32_t mkey);
uint32_t snap_dp_bmap_get_mkey(struct snap_dp_bmap *map);
#endif
