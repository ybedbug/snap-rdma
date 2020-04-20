#include <stdio.h>

#include <infiniband/verbs.h>

#include "snap.h"
#include "snap_nvme.h"
#include "snap_virtio_blk.h"
#include "snap_virtio_net.h"
#include "snap_test.h"

static int snap_init_teardown_helper(struct snap_context *sctx,
		enum snap_pci_type type)
{
	struct snap_device *sdev;
	struct snap_device_attr attr = {};
	int ret;

	attr.type = type;
	attr.pf_id = 0;
	sdev = snap_open_device(sctx, &attr);
	if (sdev) {
		if (type == SNAP_NVME_PF) {
			ret = snap_nvme_init_device(sdev);
			if (!ret) {
				fprintf(stdout, "Init NVMe dev for pf %d\n",
					attr.pf_id);
				fflush(stdout);
				snap_nvme_teardown_device(sdev);
			} else {
				fprintf(stderr, "failed to init NVMe dev for pf %d ret=%d\n",
					attr.pf_id, ret);
				fflush(stderr);
			}
		} else if (type == SNAP_VIRTIO_BLK_PF) {
			ret = snap_virtio_blk_init_device(sdev);
			if (!ret) {
				fprintf(stdout, "Init Virtio blk dev for pf %d\n",
					attr.pf_id);
				fflush(stdout);
				snap_virtio_blk_teardown_device(sdev);
			} else {
				fprintf(stderr, "failed to init Virtio blk dev for pf %d\n",
					attr.pf_id);
				fflush(stderr);
			}
		} else if (type == SNAP_VIRTIO_NET_PF) {
			ret = snap_virtio_net_init_device(sdev);
			if (!ret) {
				fprintf(stdout, "Init Virtio net dev for pf %d\n",
					attr.pf_id);
				fflush(stdout);
				snap_virtio_net_teardown_device(sdev);
			} else {
				fprintf(stderr, "failed to init Virtio net dev for pf %d\n",
					attr.pf_id);
				fflush(stderr);
			}
		}
		snap_close_device(sdev);
	} else {
		fprintf(stderr, "failed to create snap dev %d for pf %d\n", attr.type,
			attr.pf_id);
		fflush(stderr);

		return -ENODEV;
	}

	return 0;
}

int main(int argc, char **argv)
{
	struct snap_context *sctx;
	int ret = 0, i, dev_count, opt, dev_type = 0;

	while ((opt = getopt(argc, argv, "t:")) != -1) {
		switch (opt) {
		case 't':
			if (!strcmp(optarg, "all"))
				dev_type = SNAP_NVME | SNAP_VIRTIO_BLK | SNAP_VIRTIO_NET;
			else if (!strcmp(optarg, "nvme"))
				dev_type = SNAP_NVME;
			else if (!strcmp(optarg, "virtio_blk"))
				dev_type = SNAP_VIRTIO_BLK;
			else if (!strcmp(optarg, "virtio_net"))
				dev_type = SNAP_VIRTIO_NET;
			else
				printf("Unknown type %s. Using default\n", optarg);
			break;
		default:
			printf("Usage: snap_init_teardown_device -t <type: all, nvme, virtio_blk, virtio_net>\n");
			exit(1);
		}
	}

	if (!dev_type)
		dev_type = SNAP_NVME | SNAP_VIRTIO_BLK | SNAP_VIRTIO_NET;

	sctx = snap_ctx_open(dev_type);
	if (!sctx) {
		fprintf(stderr, "failed to open snap ctx for %d types\n",
			dev_type);
		fflush(stderr);
		ret = -errno;
		goto out;
	}

	if (dev_type & SNAP_NVME)
		snap_init_teardown_helper(sctx, SNAP_NVME_PF);
	if (dev_type & SNAP_VIRTIO_BLK)
		snap_init_teardown_helper(sctx, SNAP_VIRTIO_BLK_PF);
	if (dev_type & SNAP_VIRTIO_NET)
		snap_init_teardown_helper(sctx, SNAP_VIRTIO_NET_PF);

	snap_ctx_close(sctx);
out:
	return ret;
}
