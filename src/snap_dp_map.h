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

struct snap_dp_map;

struct snap_dp_map *snap_dp_map_create(unsigned page_size);
void snap_dp_map_destroy(struct snap_dp_map *map);

int snap_dp_map_add_range(struct snap_dp_map *map, uint64_t pa, uint32_t length);
size_t snap_dp_map_get_size(struct snap_dp_map *map);
int snap_dp_map_serialize(struct snap_dp_map *map, uint64_t *buf, uint32_t length);

#endif
