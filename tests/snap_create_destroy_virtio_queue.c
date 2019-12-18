#include <stdio.h>

#include <infiniband/verbs.h>

#include "snap.h"
#include "snap_virtio_blk.h"
#include "snap_virtio_net.h"

static int snap_create_destroy_virtq_helper(struct snap_context *sctx,
		enum snap_emulation_type type, int num_queues,
		enum snap_virtq_type q_type,
		enum snap_virtq_event_mode ev_mode,
		char *name)
{
	struct snap_device_attr attr = {};
	struct snap_device *sdev;
	int j, ret;

	attr.pf_id = 0;
	if (type == SNAP_VIRTIO_BLK) {
		attr.type = SNAP_VIRTIO_BLK_PF;
		sdev = snap_open_device(sctx, &attr);
		if (sdev) {
			ret = snap_virtio_blk_init_device(sdev);
			if (!ret) {
				fprintf(stdout, "created Virtio blk dev for pf %d. Creating %d queues\n",
					attr.pf_id, num_queues);
				fflush(stdout);
				for (j = 0; j < num_queues; j++) {
					struct snap_virtio_blk_queue_attr battr = {};
					struct snap_virtio_blk_queue *vbq;

					battr.vattr.type = q_type;
					battr.vattr.ev_mode = ev_mode;
					battr.vattr.idx = j;
					battr.vattr.size = 16;
					battr.qpn = (j + 1) * 0xbeaf;
					vbq = snap_virtio_blk_create_queue(sdev, &battr);
					if (vbq) {
						snap_virtio_blk_destroy_queue(vbq);
					} else {
						fprintf(stderr, "failed to create Virtio blk queue id=%d err=%d\n", j, errno);
						fflush(stderr);
					}
				}
				snap_virtio_blk_teardown_device(sdev);
			} else {
				fprintf(stderr, "failed to init Virtio blk dev for pf %d ret=%d\n",
					attr.pf_id, ret);
				fflush(stderr);
			}
			snap_close_device(sdev);
		} else {
			fprintf(stderr, "failed to create device %d for %s\n",
				attr.pf_id, name);
			fflush(stderr);
		}
	} else {
		attr.type = SNAP_VIRTIO_NET_PF;
		sdev = snap_open_device(sctx, &attr);
		if (sdev) {
			ret = snap_virtio_net_init_device(sdev);
			if (!ret) {
				fprintf(stdout, "created Virtio net dev for pf %d. Creating %d queues\n",
					attr.pf_id, num_queues);
				fflush(stdout);
				for (j = 0; j < num_queues; j++) {
					struct snap_virtio_net_queue_attr nattr = {};
					struct snap_virtio_net_queue *vnq;

					nattr.vattr.type = q_type;
					nattr.vattr.ev_mode = ev_mode;
					nattr.vattr.idx = j;
					nattr.vattr.size = 16;
					nattr.tisn_or_qpn = (j + 1) * 0xbeaf;
					vnq = snap_virtio_net_create_queue(sdev, &nattr);
					if (vnq) {
						snap_virtio_net_destroy_queue(vnq);
					} else {
						fprintf(stderr, "failed to create Virtio net queue id=%d err=%d\n", j, errno);
						fflush(stderr);
					}
				}
				snap_virtio_blk_teardown_device(sdev);
			} else {
				fprintf(stderr, "failed to init Virtio net dev for pf %d ret=%d\n",
					attr.pf_id, ret);
				fflush(stderr);
			}
			snap_close_device(sdev);
		} else {
			fprintf(stderr, "failed to create device %d for %s\n",
				attr.pf_id, name);
			fflush(stderr);
		}

	}

	return 0;
}

int main(int argc, char **argv)
{
	struct ibv_device **list;
	int ret = 0, i, dev_count, opt, num_queues = 4, dev_type = 0;
	enum snap_virtq_type q_type = SNAP_VIRTQ_SPLIT_MODE;
	enum snap_virtq_event_mode ev_mode = SNAP_VIRTQ_NO_MSIX_MODE;

	while ((opt = getopt(argc, argv, "n:t:e:d:")) != -1) {
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
			else if (!strcmp(optarg, "qp"))
				ev_mode = SNAP_VIRTQ_QP_MODE;
			else if (!strcmp(optarg, "msix"))
				ev_mode = SNAP_VIRTQ_MSIX_MODE;
			break;
		case 'd':
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
			printf("Usage: snap_create_destroy_virtio_blk_queue -n <num_queues> -t <q_type> -e <ev_mode> -d <dev_type>\n");
			exit(1);
		}
	}

	if (!dev_type)
		dev_type = SNAP_VIRTIO_BLK | SNAP_VIRTIO_NET;

	list = ibv_get_device_list(&dev_count);
	if (!list) {
		fprintf(stderr, "failed to open ib device list.\n");
		fflush(stderr);
		ret = 1;
		goto out;
	}

	for (i = 0; i < dev_count; i++) {
		struct snap_context *sctx;

		sctx = snap_open(list[i]);
		if (!sctx) {
			fprintf(stderr, "failed to create snap ctx for %s err=%d. continue trying\n",
				list[i]->name, errno);
			fflush(stderr);
			continue;
		}

		if (sctx->emulation_caps & SNAP_VIRTIO_BLK & dev_type)
			snap_create_destroy_virtq_helper(sctx, SNAP_VIRTIO_BLK,
							 num_queues, q_type,
							 ev_mode, list[i]->name);
		if (sctx->emulation_caps & SNAP_VIRTIO_NET & dev_type)
			snap_create_destroy_virtq_helper(sctx, SNAP_VIRTIO_NET,
							 num_queues, q_type,
							 ev_mode, list[i]->name);

		snap_close(sctx);
	}

	ibv_free_device_list(list);
out:
	return ret;
}
