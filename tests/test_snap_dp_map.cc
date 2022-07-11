#include <limits.h>
#include "gtest/gtest.h"

#include <stdint.h>
#include <pthread.h>
#include <errno.h>
#include <linux/virtio_ring.h>

extern "C" {
#include "snap_dp_map.h"
#include "snap_virtio_adm_spec.h"
};

TEST(snap_dp_map, create) {
	struct snap_dp_map *m;

	m = snap_dp_map_create(1);
	EXPECT_TRUE(m == NULL);
	m = snap_dp_map_create(7);
	EXPECT_TRUE(m == NULL);
	m = snap_dp_map_create(4096);
	ASSERT_TRUE(m != NULL);
	snap_dp_map_destroy(m);
}


TEST(snap_dp_map, add_range) {
	struct snap_dp_map *m;
	int ret;

	m = snap_dp_map_create(4096);
	ASSERT_TRUE(m != NULL);

	ret = snap_dp_map_add_range(m, 4096, 1);
	EXPECT_EQ(ret, 0);
	EXPECT_EQ(snap_dp_map_get_size(m), sizeof(uint64_t));

	ret = snap_dp_map_add_range(m, 4096, 1);
	EXPECT_EQ(ret, 0);
	EXPECT_EQ(snap_dp_map_get_size(m), sizeof(uint64_t));

	ret = snap_dp_map_add_range(m, 4096, 4096);
	EXPECT_EQ(ret, 0);
	EXPECT_EQ(snap_dp_map_get_size(m), sizeof(uint64_t));

	ret = snap_dp_map_add_range(m, 4096, 8*4096);
	EXPECT_EQ(ret, 0);
	EXPECT_EQ(snap_dp_map_get_size(m), 8*sizeof(uint64_t));

	snap_dp_map_destroy(m);
}

TEST(snap_dp_map, serialize) {
	struct snap_dp_map *m;
	int ret;
	uint64_t pbuf[16];

	m = snap_dp_map_create(4096);
	ASSERT_TRUE(m != NULL);

	ret = snap_dp_map_add_range(m, 4097, 8*4096);
	EXPECT_EQ(ret, 0);
	EXPECT_EQ(snap_dp_map_get_size(m), 9*sizeof(uint64_t));

	ret = snap_dp_map_serialize(m, pbuf, sizeof(pbuf));
	EXPECT_EQ(ret, 9);
	EXPECT_EQ(snap_dp_map_get_size(m), 0);

	ret = snap_dp_map_serialize(m, pbuf, sizeof(pbuf));
	EXPECT_EQ(ret, 0);
	EXPECT_EQ(snap_dp_map_get_size(m), 0);

	snap_dp_map_destroy(m);
}

static struct snap_vq_adm_sge sges[] = {
	{ 4096, 8192 }, { 8 * 4096, 4096 }, { 10 * 4096, 4096 }
	/* scale to page_size * (8):  [0, 8192}, {8192, 12288}, { 12288, 16384 }
	 * bytemap: 32M, 8M,   8M = 48M
	   bitmap: 256M, 64M, 64M = 384M total */
};

TEST(snap_dp_bmap, range_size) {
	struct snap_dp_bmap *m;

	m = snap_dp_bmap_create(sges, 3, 4096, true);
	ASSERT_TRUE(m != NULL);

	EXPECT_EQ(snap_dp_bmap_range_size(m, 1, 1), 1);
	EXPECT_EQ(snap_dp_bmap_range_size(m, 4096, 1), 1);
	EXPECT_EQ(snap_dp_bmap_range_size(m, 0, 4096), 1);
	EXPECT_EQ(snap_dp_bmap_range_size(m, 1, 4096), 2);
	snap_dp_bmap_destroy(m);
}

TEST(snap_dp_bmap, range_size_bit) {
	struct snap_dp_bmap *m;

	m = snap_dp_bmap_create(sges, 3, 4096, false);
	ASSERT_TRUE(m != NULL);

	EXPECT_EQ(snap_dp_bmap_range_size(m, 1, 1), 1);
	EXPECT_EQ(snap_dp_bmap_range_size(m, 4096, 1), 1);
	EXPECT_EQ(snap_dp_bmap_range_size(m, 0, 4096), 1);
	EXPECT_EQ(snap_dp_bmap_range_size(m, 1, 4096), 1);


	EXPECT_EQ(snap_dp_bmap_range_size(m, 1, 10*4096), 2);
	EXPECT_EQ(snap_dp_bmap_range_size(m, 0, 8*4096), 1);
	snap_dp_bmap_destroy(m);
}

TEST(snap_dp_bmap, get_start_pa) {
	struct snap_dp_bmap *m;
	uint64_t start_pa;
	uint32_t size;
	int b_off;
	int len;

	m = snap_dp_bmap_create(sges, 3, 4096, true);
	ASSERT_TRUE(m != NULL);

	len = snap_dp_bmap_get_start_pa(m, 1, 4096, &start_pa, &b_off, &size);
	EXPECT_EQ(len, 4096);
	EXPECT_EQ(start_pa, 4096);
	EXPECT_EQ(size, 2);

	len = snap_dp_bmap_get_start_pa(m, 1, 8192 * 4096, &start_pa, &b_off, &size);
	EXPECT_EQ(len, 8192 * 4096);
	EXPECT_EQ(start_pa, 4096);
	EXPECT_EQ(size, 8192);

	len = snap_dp_bmap_get_start_pa(m, 8192 * 4096 + 4097, 4096, &start_pa, &b_off, &size);
	EXPECT_EQ(len, 4096);
	EXPECT_EQ(start_pa, 8*4096 + 1);
	EXPECT_EQ(size, 2);

	snap_dp_bmap_destroy(m);
}

TEST(snap_dp_bmap, get_start_pa_bit) {
	struct snap_dp_bmap *m;
	uint64_t start_pa;
	uint32_t size;
	int b_off;
	int len;

	m = snap_dp_bmap_create(sges, 3, 4096, false);
	ASSERT_TRUE(m != NULL);

	len = snap_dp_bmap_get_start_pa(m, 1, 4096, &start_pa, &b_off, &size);
	EXPECT_EQ(len, 4096);
	EXPECT_EQ(start_pa, 4096);
	EXPECT_EQ(size, 1);
	EXPECT_EQ(b_off, 0);

	len = snap_dp_bmap_get_start_pa(m, 1, 8192 * 4096, &start_pa, &b_off, &size);
	EXPECT_EQ(len, 8192 * 4096);
	EXPECT_EQ(start_pa, 4096);
	EXPECT_EQ(size, 1 + 8192/8);
	EXPECT_EQ(b_off, 0);

	len = snap_dp_bmap_get_start_pa(m, 8 * 8192 * 4096 + 4097, 9*4096, &start_pa, &b_off, &size);
	EXPECT_EQ(len, 9*4096);
	EXPECT_EQ(start_pa, 8*4096);
	EXPECT_EQ(size, 2);

	snap_dp_bmap_destroy(m);
}
