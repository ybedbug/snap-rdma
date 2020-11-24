#include "snap.h"
#include "snap_channel.h"

int snap_channel_mark_dirty_page(struct snap_channel *schanne, uint64_t guest_pa,
				 int length)
{
	return 0;
}

struct snap_channel *snap_channel_open(struct snap_migration_ops *ops,
				       void *data)
{
	struct snap_channel *schannel;

	if (!ops ||
	    !ops->quiesce ||
	    !ops->unquiesce ||
	    !ops->freeze ||
	    !ops->unfreeze ||
	    !ops->get_state_size ||
	    !ops->copy_state ||
	    !ops->start_dirty_pages_track ||
	    !ops->stop_dirty_pages_track) {
		errno = EINVAL;
		goto out;
	}

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

void snap_channel_close(struct snap_channel *channel)
{
	free(channel);
	return 0;
}
