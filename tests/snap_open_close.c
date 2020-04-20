#include <stdio.h>

#include <infiniband/verbs.h>

#include "snap.h"
#include "snap_test.h"

int main(int argc, char **argv)
{
	int type = SNAP_NVME;
	int opt, ret = 0;
	struct snap_context *sctx;

	while ((opt = getopt(argc, argv, "t:")) != -1) {
		switch (opt) {
		case 't':
			if (!strcmp(optarg, "all"))
				type = SNAP_NVME | SNAP_VIRTIO_BLK | SNAP_VIRTIO_NET;
			else if (!strcmp(optarg, "nvme"))
				type = SNAP_NVME;
			else if (!strcmp(optarg, "virtio-blk"))
				type = SNAP_VIRTIO_BLK;
			else if (!strcmp(optarg, "virtio-net"))
				type = SNAP_VIRTIO_NET;
			break;
		default:
			printf("Usage: snap_open_close -t <type: all, nvme, virtio-blk, virtio-net>\n");
			exit(1);
		}
	}

	sctx = snap_ctx_open(type);
	if (!sctx) {
		fprintf(stderr, "failed to open snap ctx for %d types\n", type);
		fflush(stderr);
		ret = -errno;
		goto out;
	} else {
		fprintf(stdout, "opened snap ctx for %d types on %s\n", type,
			sctx->context->device->name);
		fflush(stdout);
	}

	snap_ctx_close(sctx);
out:
	return ret;
}
