#include "snap.h"
#include "snap_channel.h"

static int snap_channel_cm_event_handler(struct rdma_cm_id *cm_id,
					 struct rdma_cm_event *event)
{
	return 0;
}

static void *cm_thread(void *arg)
{
	struct snap_channel *schannel = arg;
	struct rdma_cm_event *event;
	int ret;

	while (1) {
		ret = rdma_get_cm_event(schannel->cm_channel, &event);
		if (ret)
			exit(ret);
		ret = snap_channel_cm_event_handler(event->id, event);
		rdma_ack_cm_event(event);
		if (ret)
			exit(ret);
	}
}

static int snap_channel_bind_id(struct rdma_cm_id *cm_id, struct addrinfo *res)
{
	return 0;
}

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
	struct addrinfo *res;
	struct addrinfo hints;
	char *rdma_ip;
	char *rdma_port;
	int ret;

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

	schannel->cm_channel = rdma_create_event_channel();
	if (!schannel->cm_channel)
		goto out_free;

	ret = rdma_create_id(schannel->cm_channel, &schannel->listener, schannel,
			     RDMA_PS_TCP);
	if (ret)
		goto out_free_cm_channel;

	ret = pthread_create(&schannel->cmthread, NULL, cm_thread, schannel);
	if (ret)
		goto out_destroy_id;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_flags = AI_NUMERICSERV;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = 0;

	rdma_ip = getenv(SNAP_CHANNEL_RDMA_IP);
	if (!rdma_ip)
		goto out_free_cmthread;

	rdma_port = getenv(SNAP_CHANNEL_RDMA_PORT);
	if (!rdma_port)
		goto out_free_cmthread;

	ret = getaddrinfo(rdma_ip, rdma_port, &hints, &res);
	if (ret)
		goto out_free_cmthread;

	ret = snap_channel_bind_id(schannel->listener, res);
	freeaddrinfo(res);

	if (ret)
		goto out_free_cmthread;

	schannel->ops = ops;
	schannel->data = data;

	return schannel;

out_free_cmthread:
	pthread_cancel(schannel->cmthread);
	pthread_join(schannel->cmthread, NULL);
out_destroy_id:
	rdma_destroy_id(schannel->listener);
out_free_cm_channel:
	rdma_destroy_event_channel(schannel->cm_channel);
out_free:
	free(schannel);
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
	pthread_cancel(schannel->cmthread);
	pthread_join(schannel->cmthread, NULL);
	rdma_destroy_id(schannel->listener);
	rdma_destroy_event_channel(schannel->cm_channel);
	free(schannel);
}
