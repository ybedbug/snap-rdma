#include "snap.h"
#include "snap_channel.h"

/**
 * snap_channel_mark_dirty_page() - Report on a new contiguous memory region
 * that was dirtied by a snap controller.
 * @schannel: snap channel
 * @guest_pa: guest base physical address that was dirtied by the device
 * @length: length in bytes of the dirtied memory for the reported transaction
 *
 * Return: Returns 0 on success, Or negative error value otherwise.
 */
int snap_channel_mark_dirty_page(struct snap_channel *schannel, uint64_t guest_pa,
				 int length)
{
	return 0;
}

/**
 * snap_channel_open() - Opens a channel that will listen to host commands.
 * This channel is dedicated for live migration communication between device
 * and host.
 *
 * @ops: migration ops struct of functions that contains the basic migration
 * operations (provided by the controller).
 * @data: controller_data that will be associated with the
 * caller or application.
 *
 * Return: Returns a pointer to the communication channel on success,
 * or NULL otherwise.
 *
 * You may assume that the migration_ops, controller_data and the returned
 * snap channel won't be freed as long as the channel is open.
 */
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

/**
 * snap_channel_close() - Closes the communication channel
 * opened by the snap_channel_open() function.
 *
 * @schannel: the communication channel to be closed.
 */
void snap_channel_close(struct snap_channel *schannel)
{
	free(schannel);
}
