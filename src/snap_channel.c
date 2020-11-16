#include "snap_channel.h"

struct snap_channel *snap_channel_open(struct snap_migration_ops *ops,
				       void *data)
{
	struct snap_channel *schannel;

	schannel = calloc(1, sizeof(*schannel));
	if (!schannel) {
		errno = ENOMEM;
		goto out;
	}

	schannel->ops = ops;
	schannel->data = data;

	return schannel;

out:
	return NULL;
}

int snap_channel_close(struct snap_channel *channel)
{
	free(channel);
}
