#include <stdio.h>

#include "snap.h"
#include "snap_channel.h"

int main(int argc, char **argv)
{
	struct snap_channel *schannel;
	struct snap_migration_ops ops;
	int ret = 0;

	schannel = snap_channel_open(&ops, NULL);
	if (!schannel) {
		fprintf(stderr, "failed to open snap channel\n");
		fflush(stderr);
		ret = -errno;
		goto out;
	}

	snap_channel_close(schannel);

	fprintf(stdout, "opened and closed snap channel successfully\n");
	fflush(stdout);
out:
	return ret;
}
