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
#include <errno.h>
#include <linux/virtio_ring.h>

#include "khash.h"
#include "snap_dp_map.h"
#include "snap_macros.h"
#include "snap_virtio_adm_spec.h"

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

/* host side byte/bitmap */
struct snap_dp_bmap {
	struct snap_vq_adm_sge *sge_list;
	int sge_count;
	unsigned page_size;
	bool is_bytemap;
	uint64_t start_pa;
};

struct snap_dp_bmap *snap_dp_bmap_create(struct snap_vq_adm_sge *sge_list, int sge_count,
		unsigned page_size, bool is_bytemap)
{
	struct snap_dp_bmap *map;

	if (!SNAP_IS_POW2(page_size) || page_size <= 1)
		return NULL;

	map = calloc(1, sizeof(*map));
	if (!map)
		return NULL;

	map->sge_list = malloc(sizeof(*sge_list) * sge_count);
	if (!map->sge_list) {
		free(map);
		return NULL;
	}

	memcpy(map->sge_list, sge_list, sge_count * sizeof(*sge_list));
	map->sge_count = sge_count;
	map->page_size = page_size;
	map->is_bytemap = is_bytemap;
	map->start_pa = 0;
	return map;
}

void snap_dp_bmap_destroy(struct snap_dp_bmap *map)
{
	free(map->sge_list);
	free(map);
}

/* return bitmap memory range in bytes. bitmap is rounded up to byte */
uint32_t snap_dp_bmap_range_size(struct snap_dp_bmap *map, uint64_t pa, uint32_t length)
{
	uint64_t start_page = SNAP_ALIGN_FLOOR(pa, map->page_size);
	uint64_t end_page = SNAP_ALIGN_CEIL(pa + length, map->page_size);
	uint32_t range_size;

	range_size = (end_page - start_page)/map->page_size;
	return map->is_bytemap ? range_size : (8 + range_size - 1)/8;
}

static uint64_t snap_dp_bmap_size_to_len(struct snap_dp_bmap *map, uint32_t size)
{
	printf("size2len: %d\n", size);
	return map->is_bytemap ? size : 8 * size;
}
/*
 * size is rounded up to byte, can be less then range_size(pa, length)
 */
uint32_t snap_dp_bmap_get_start_pa(struct snap_dp_bmap *map, uint64_t pa, uint32_t length,
		uint64_t *start_pa, int *byte_offset, uint32_t *size)
{
	int i;
	uint64_t range_pa, start_page;
	uint64_t range_len, ret_len;

	range_pa = map->start_pa;
	start_page = SNAP_ALIGN_FLOOR(pa, map->page_size);
	/* find entry in the sge map */
	for (i = 0; i < map->sge_count; i++) {

		range_len = map->page_size * snap_dp_bmap_size_to_len(map, map->sge_list[i].len);
		if (start_page >= range_pa && start_page < range_pa + range_len)
			goto found;

		range_pa += range_len;
	}
	return -EINVAL;
found:

	printf("range_pa: %ld, range_len %ld\n", range_pa, range_len);
	if (pa + length < range_pa + range_len) {
		*size = snap_dp_bmap_range_size(map, pa, length);
		ret_len = length;
	} else {
		*size = snap_dp_bmap_range_size(map, pa, range_len - pa);
		ret_len = range_len;
		printf("returning %ld\n", ret_len);
	}

	if (map->is_bytemap) {
		*start_pa = map->sge_list[i].addr + (start_page - range_pa)/map->page_size;
		*byte_offset = 0;
	} else {
		*start_pa = map->sge_list[i].addr + (start_page - range_pa)/map->page_size/8;
		*byte_offset = (start_page - range_pa)/map->page_size & 0x7;
	}

	return ret_len;
}
