#include <stdio.h>

#include <infiniband/verbs.h>

#include "snap.h"

int main(int argc, char **argv)
{
	struct ibv_device **list;
	struct snap_context *sctx;
	int ret = 0, i, dev_count;

	list = ibv_get_device_list(&dev_count);
	if (!list) {
		fprintf(stderr, "failed to open ib device list.\n");
		fflush(stderr);
		ret = 1;
		goto out;
	}

	for (i = 0; i < dev_count; i++) {
		sctx = snap_open(list[i]);
		if (!sctx) {
			fprintf(stderr, "failed to create snap ctx for %s err=%d\n",
				list[i]->name, errno);
			fflush(stderr);
			continue;
		}
		snap_close(sctx);
	}

	ibv_free_device_list(list);
out:
	return ret;
}
