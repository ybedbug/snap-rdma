#include <stdio.h>

#include <infiniband/verbs.h>

#include "snap.h"
#include "snap_virtio_blk.h"
#include "snap_virtio_net.h"
#include "snap_test.h"

int snap_query_virtio_device_helper(struct snap_context *sctx,
		enum snap_emulation_type type, char *name)
{
	struct snap_device_attr attr = {};
	struct snap_device *sdev;
	int ret;

	attr.pf_id = 0;
	if (type == SNAP_VIRTIO_BLK) {
		attr.type = SNAP_VIRTIO_BLK_PF;
		sdev = snap_open_device(sctx, &attr);
		if (sdev) {
			struct snap_virtio_blk_device_attr blk_attr = {};
			struct snap_virtio_blk_queue_attr q_attrs[4];

			blk_attr.queues = 4;
			blk_attr.q_attrs = q_attrs;
			ret = snap_virtio_blk_query_device(sdev, &blk_attr);
			if (!ret) {
				fprintf(stdout, "queried Virtio blk dev enabled=%d for pf=%d\n",
					blk_attr.vattr.enabled, attr.pf_id);
				fflush(stdout);
			} else {
				fprintf(stderr, "failed to query Virtio blk dev for pf %d ret=%d\n",
					attr.pf_id, ret);
				fflush(stderr);
			}
			snap_close_device(sdev);
		} else {
			fprintf(stderr, "failed to create Blk device %d for %s\n",
				attr.pf_id, name);
			fflush(stderr);
		}
	} else {
		attr.type = SNAP_VIRTIO_NET_PF;
		sdev = snap_open_device(sctx, &attr);
		if (sdev) {
			struct snap_virtio_net_device_attr net_attr = {};
			struct snap_virtio_net_queue_attr q_attrs[4];

			net_attr.queues = 4;
			net_attr.q_attrs = q_attrs;
			ret = snap_virtio_net_query_device(sdev, &net_attr);
			if (!ret) {
				fprintf(stdout, "queried Virtio net dev enabled=%d for pf=%d\n",
					net_attr.vattr.enabled, attr.pf_id);
				fflush(stdout);
			} else {
				fprintf(stderr, "failed to query Virtio net dev for pf %d ret=%d\n",
					attr.pf_id, ret);
				fflush(stderr);
			}
			snap_close_device(sdev);
		} else {
			fprintf(stderr, "failed to create Net device %d for %s\n",
				attr.pf_id, name);
			fflush(stderr);
		}
	}

	return 0;
}

int main(int argc, char **argv)
{
	struct snap_context *sctx;
	int opt, ret = 0, dev_type = 0;

	while ((opt = getopt(argc, argv, "t:")) != -1) {
		switch (opt) {
		case 't':
			if (!strcmp(optarg, "all"))
				dev_type = SNAP_VIRTIO_BLK | SNAP_VIRTIO_NET;
			else if (!strcmp(optarg, "virtio_blk"))
				dev_type = SNAP_VIRTIO_BLK;
			else if (!strcmp(optarg, "virtio_net"))
				dev_type = SNAP_VIRTIO_NET;
			else
				printf("Unknown type %s. Using default\n", optarg);
			break;
		default:
			printf("Usage: snap_query_virtio_device -t <type: all, virtio_blk, virtio_net>\n");
			exit(1);
		}
	}

	if (!dev_type)
		dev_type = SNAP_VIRTIO_BLK | SNAP_VIRTIO_NET;

	sctx = snap_ctx_open(dev_type, NULL);
	if (!sctx) {
		fprintf(stderr, "failed to open snap ctx for %d types\n",
			dev_type);
		fflush(stderr);
		ret = -errno;
		goto out;
	}

	if (dev_type & SNAP_VIRTIO_BLK)
		ret |= snap_query_virtio_device_helper(sctx, SNAP_VIRTIO_BLK,
						sctx->context->device->name);
	if (dev_type & SNAP_VIRTIO_NET)
		ret |= snap_query_virtio_device_helper(sctx, SNAP_VIRTIO_NET,
						sctx->context->device->name);

	snap_ctx_close(sctx);
out:
	return ret;
}
