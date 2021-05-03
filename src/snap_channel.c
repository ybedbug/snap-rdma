#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>

#include "snap_channel.h"

#define MAX_CHANNELS 8
static const struct snap_channel_ops *channel_ops[MAX_CHANNELS];

static const struct snap_channel_ops *lookup(const char *name)
{
	int i;

	for (i = 0; i < MAX_CHANNELS; i++) {
		if (!channel_ops[i])
			continue;
		if (strcmp(name, channel_ops[i]->name) == 0)
			return channel_ops[i];
	}
	return NULL;
}

struct snap_channel *snap_channel_open(const char *name, struct snap_migration_ops *ops,
				       void *data)
{
	struct snap_channel *schannel;
	const struct snap_channel_ops *channel_ops;
	char *so_name;
	void *dlhandle;
	int len;

	channel_ops = lookup(name);
	if (channel_ops)
		goto found;

	/* try to dlopen plugin and retry the search */
	/* TODO: improve plugin search */
	if (getenv("LIBSNAP_PLUGIN_DIR"))
		len = asprintf(&so_name, "%s/%s.so", getenv("LIBSNAP_PLUGIN_DIR"), name);
	else
		len = asprintf(&so_name, "%s.so", name);

	if (len < 0) {
		snap_channel_error("Failed to allocate memory\n");
		return NULL;
	}

	dlhandle = dlopen(so_name, RTLD_NOW);
	if (!dlhandle) {
		snap_channel_error("Failed to open %s : %s\n", so_name, dlerror());
		free(so_name);
		return NULL;
	}
	free(so_name);

	channel_ops = lookup(name);
	if (!channel_ops) {
		snap_channel_error("Channel %s is not registered\n", name);
		return NULL;
	}

found:
	schannel = channel_ops->open(ops, data);
	if (!schannel)
		return NULL;

	schannel->channel_ops = channel_ops;
	schannel->ops = ops;
	schannel->data = data;
	return schannel;
}

void snap_channel_close(struct snap_channel *schannel)
{
	schannel->channel_ops->close(schannel);
}

int snap_channel_mark_dirty_page(struct snap_channel *schannel, uint64_t guest_pa,
				 int length)
{
	return schannel->channel_ops->mark_dirty_page(schannel, guest_pa, length);
}

void snap_channel_register(const struct snap_channel_ops *ops)
{
	int i;

	for (i = 0; i < MAX_CHANNELS; i++) {
		if (!channel_ops[i]) {
			channel_ops[i] = ops;
			snap_channel_info("registered migration channel %s\n", ops->name);
			return;
		}
		if (strcmp(ops->name, channel_ops[i]->name) == 0) {
			snap_channel_error("migration channel %s is already registered\n",
					   ops->name);
			return;
		}
	}
	snap_channel_error("Failed to register migration channel %s: too many channels\n",
			   ops->name);
}

void snap_channel_unregister(const struct snap_channel_ops *ops)
{
}
