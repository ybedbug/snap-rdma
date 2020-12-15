#include "snap_channel.h"

struct sample_channel {
	struct snap_channel base;
	/* put your data here */
};

static struct snap_channel *sample_open(struct snap_migration_ops *ops, void *data)
{
	struct sample_channel *ch;

	ch = calloc(sizeof(*ch), 1);
	return &ch->base;
}

static void sample_close(struct snap_channel *schannel)
{
	free(schannel);
}

static int sample_mark_dirty_page(struct snap_channel *schannel, uint64_t guest_pa,
		int length)
{
	return 0;
}

static const struct snap_channel_ops sample_channel_ops = {
	.name = "sample_channel",
	.open = sample_open,
	.close = sample_close,
	.mark_dirty_page = sample_mark_dirty_page,
};

SNAP_CHANNEL_DECLARE(sample_channel, sample_channel_ops);
