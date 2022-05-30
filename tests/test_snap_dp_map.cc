#include <limits.h>
#include "gtest/gtest.h"
extern "C" {
#include "snap_dp_map.h"
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
