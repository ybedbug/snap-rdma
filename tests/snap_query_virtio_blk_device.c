#include <stdio.h>

#include <infiniband/verbs.h>

#include "snap.h"
#include "snap_virtio_blk.h"

int main(int argc, char **argv)
{
	struct ibv_device **list;
	int ret = 0, i, dev_count;

	list = ibv_get_device_list(&dev_count);
	if (!list) {
		fprintf(stderr, "failed to open ib device list.\n");
		fflush(stderr);
		ret = 1;
		goto out;
	}

	for (i = 0; i < dev_count; i++) {
		struct snap_device_attr attr = {};
		struct snap_context *sctx;
		struct snap_device *sdev;

		sctx = snap_open(list[i]);
		if (!sctx) {
			fprintf(stderr, "failed to create snap ctx for %s err=%d. continue trying\n",
				list[i]->name, errno);
			fflush(stderr);
			continue;
		}

		attr.type = SNAP_VIRTIO_BLK_PF;
		attr.pf_id = 0;
		sdev = snap_open_device(sctx, &attr);
		if (sdev) {
			struct snap_virtio_blk_device_attr blk_attr = {};
			struct snap_virtio_blk_queue_attr q_attrs[4];

			blk_attr.queues = 4;
			blk_attr.q_attrs = q_attrs;
			ret = snap_virtio_blk_query_device(sdev, &blk_attr);
			if (!ret) {
				fprintf(stdout, "queried Virtio blk dev enabled=%d for pf=%d\n",
					blk_attr.enabled, attr.pf_id);
				fflush(stdout);
			} else {
				fprintf(stderr, "failed to query Virtio blk dev for pf %d ret=%d\n",
					attr.pf_id, ret);
				fflush(stderr);
			}
			snap_close_device(sdev);
		} else {
			fprintf(stderr, "failed to create device %d for %s\n",
				attr.pf_id, list[i]->name);
			fflush(stderr);
		}

		snap_close(sctx);
	}

	ibv_free_device_list(list);
out:
	return ret;
}
