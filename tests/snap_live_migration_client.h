
#ifndef SNAP_LIVE_MIGRATION_CLIENT_H
#define SNAP_LIVE_MIGRATION_CLIENT_H

#include "snap_channel.h"
#include "snap_rdma_channel.h"
#include <semaphore.h>
#include <unistd.h>

#define SNAP_CHANNEL_POLL_BATCH 16
#define SNAP_CHANNEL_MAX_COMPLETIONS 64

struct snap_channel_test {
	struct snap_rdma_channel *schannel;
	sem_t sem;
};

int open_client(struct snap_channel_test *stest);
void close_client(struct snap_channel_test *stest);

#endif
