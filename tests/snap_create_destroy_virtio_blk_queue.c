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
		int j;

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
			ret = snap_virtio_blk_init_device(sdev);
			if (!ret) {
				fprintf(stdout, "created Virtio blk dev for pf %d. Creating 4 queues\n",
					attr.pf_id);
				fflush(stdout);
				for (j = 0; j < 4; j++) {
					struct snap_virtio_blk_queue_attr attr = {};
					struct snap_virtio_blk_queue *vbq;

					attr.type = j % 2 ? SNAP_VIRTQ_SPLIT_MODE : SNAP_VIRTQ_PACKED_MODE;
					attr.ev_mode = SNAP_VIRTQ_NO_MSIX_MODE;
					attr.idx = j;
					attr.size = 64;
					vbq = snap_virtio_blk_create_queue(sdev, &attr);
					if (vbq) {
						snap_virtio_blk_destroy_queue(vbq);
					} else {
						fprintf(stderr, "failed to create Virtio blk queue id=%d err=%d\n", j, errno);
						fflush(stderr);
					}
				}
				snap_virtio_blk_teardown_device(sdev);
			} else {
				fprintf(stderr, "failed to create Virtio blk dev for pf %d ret=%d\n",
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
