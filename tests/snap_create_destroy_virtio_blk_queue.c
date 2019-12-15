#include <stdio.h>

#include <infiniband/verbs.h>

#include "snap.h"
#include "snap_virtio_blk.h"

int main(int argc, char **argv)
{
	struct ibv_device **list;
	int ret = 0, i, dev_count, opt, num_queues = 4;
	enum snap_virtq_type q_type = SNAP_VIRTQ_SPLIT_MODE;
	enum snap_virtq_event_mode ev_mode = SNAP_VIRTQ_NO_MSIX_MODE;

	while ((opt = getopt(argc, argv, "n:t:e:")) != -1) {
		switch (opt) {
		case 'n':
			num_queues = atoi(optarg);
			break;
		case 't':
			if (!strcmp(optarg, "split"))
				q_type = SNAP_VIRTQ_SPLIT_MODE;
			else if (!strcmp(optarg, "packed"))
				q_type = SNAP_VIRTQ_PACKED_MODE;
		case 'e':
			if (!strcmp(optarg, "no_msix"))
				ev_mode = SNAP_VIRTQ_NO_MSIX_MODE;
			else if (!strcmp(optarg, "cq"))
				ev_mode = SNAP_VIRTQ_CQ_MODE;
			else if (!strcmp(optarg, "msix"))
				ev_mode = SNAP_VIRTQ_MSIX_MODE;
			break;
		default:
			printf("Usage: snap_create_destroy_virtio_blk_queue -n <num> -t <type> -e <ev_mode>\n");
			exit(1);
		}
	}

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
				fprintf(stdout, "created Virtio blk dev for pf %d. Creating %d queues\n",
					attr.pf_id, num_queues);
				fflush(stdout);
				for (j = 0; j < num_queues; j++) {
					struct snap_virtio_blk_queue_attr attr = {};
					struct snap_virtio_blk_queue *vbq;

					attr.type = q_type;
					attr.ev_mode = ev_mode;
					attr.idx = j;
					attr.qpn = (j + 1) * 0xbeaf;
					attr.vattr.size = 16;
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
