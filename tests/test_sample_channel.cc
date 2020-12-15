#include <limits.h>
#include "gtest/gtest.h"

extern "C" {
#include "snap_channel.h"
};

static int test_quiesce(void *data)
{
	return 0;
}

static int test_unquiesce(void *data)
{
	return 0;
}

static int test_freeze(void *data)
{
	return 0;
}

static int test_unfreeze(void *data)
{
	return 0;
}

static int test_get_state_size(void *data)
{
	return 0;
}

static int test_copy_state(void *data, void *buff, int len, bool copy_from_buffer)
{
	return 0;
}

static int test_start_dirty_pages_track(void *data)
{
	return 0;
}

static int test_stop_dirty_pages_track(void *data)
{
	return 0;
}

static struct snap_migration_ops test_ops = {
	.quiesce = test_quiesce,
	.unquiesce = test_unquiesce,
	.freeze = test_freeze,
	.unfreeze = test_unfreeze,
	.get_state_size = test_get_state_size,
	.copy_state = test_copy_state,
	.start_dirty_pages_track = test_start_dirty_pages_track,
	.stop_dirty_pages_track = test_stop_dirty_pages_track,
};

TEST(migration, sample_channel) {
	struct snap_channel *ch;
	const char *ch_name = "sample_channel";

	ch = snap_channel_open(ch_name, NULL, NULL);
	ASSERT_TRUE(ch);
	snap_channel_mark_dirty_page(ch, 0, 4096);
	snap_channel_close(ch);

	ch = snap_channel_open(ch_name, NULL, NULL);
	ASSERT_TRUE(ch);
	snap_channel_mark_dirty_page(ch, 0, 4096);
	snap_channel_close(ch);
}

TEST(migration, rdma_channel) {
	struct snap_channel *ch;
	const char *ch_name = "rdma_channel";

	ch = snap_channel_open(ch_name, &test_ops, NULL);
	ASSERT_TRUE(ch);
	snap_channel_mark_dirty_page(ch, 0, 4096);
	snap_channel_close(ch);

	ch = snap_channel_open(ch_name, &test_ops, NULL);
	ASSERT_TRUE(ch);
	snap_channel_mark_dirty_page(ch, 0, 4096);
	snap_channel_close(ch);
}
