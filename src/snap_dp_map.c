/*
 * Copyright © 202 NVIDIA CORPORATION & AFFILIATES. ALL RIGHTS RESERVED.
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
#include <pthread.h>

#include "khash.h"
#include "snap_dp_map.h"
#include "snap_macros.h"

KHASH_INIT(snap_dp_hash, uint64_t, char, 0, kh_int64_hash_func, kh_int64_hash_equal);

struct snap_dp_map {
	khash_t(snap_dp_hash) dp_set;
	pthread_spinlock_t lock;
	unsigned page_size;
};

struct snap_dp_map *snap_dp_map_create(unsigned page_size)
{
	struct snap_dp_map *map;

	if (!SNAP_IS_POW2(page_size) || page_size <= 1)
		return NULL;

	map = calloc(1, sizeof(*map));
	if (!map)
		return NULL;

	kh_init_inplace(snap_dp_hash, &map->dp_set);
	pthread_spin_init(&map->lock, 0);
	map->page_size = page_size;
	return map;
}

void snap_dp_map_destroy(struct snap_dp_map *map)
{
	kh_init_inplace(snap_dp_hash, &map->dp_set);
	pthread_spin_destroy(&map->lock);
}

int snap_dp_map_add_range(struct snap_dp_map *map, uint64_t pa, uint32_t length)
{
	uint64_t page;
	int ret;

	pthread_spin_lock(&map->lock);
	for (page = pa & ~(map->page_size - 1); page < pa + length; page += map->page_size) {
		kh_put(snap_dp_hash, &map->dp_set, page, &ret);
		if (ret == -1)
			goto out;
	}
	ret = 0;
out:
	pthread_spin_unlock(&map->lock);
	return ret;
}

size_t snap_dp_map_get_size(struct snap_dp_map *map)
{
	return kh_size(&map->dp_set) * sizeof(uint64_t);
}

int snap_dp_map_serialize(struct snap_dp_map *map, uint64_t *buf, uint32_t length)
{
	uint32_t nelems = length/sizeof(uint64_t);
	int i, k;
	uint64_t page;

	pthread_spin_lock(&map->lock);

	for (k = kh_begin(&map->dp_set), i = 0;
			k != kh_end(&map->dp_set) && i < nelems; k++) {
		if (!kh_exist(&map->dp_set, k))
			continue;
		page = kh_key(&map->dp_set, k);
		//printf("k = %d i = %d, page %lu\n", k, i, page);
		kh_del(snap_dp_hash, &map->dp_set, k);
		buf[i] = page;
		i++;
	}

	pthread_spin_unlock(&map->lock);
	return i;
}
