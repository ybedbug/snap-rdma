#include <stdio.h>

#include <infiniband/verbs.h>

#include "snap.h"

static int snap_open_close_pf_helper(struct snap_context *sctx,
	enum snap_emulation_type type)
{
	struct snap_device *sdev;
	int i;
	struct snap_pfs_ctx *pfs;
	struct snap_pci *spfs;
	enum snap_pci_type ptype;

	if (type == SNAP_NVME) {
		pfs = &sctx->nvme_pfs;
		ptype = SNAP_NVME_PF;
	} else if (type == SNAP_VIRTIO_NET) {
		pfs = &sctx->virtio_net_pfs;
		ptype = SNAP_VIRTIO_NET_PF;
	} else if (type == SNAP_VIRTIO_BLK) {
		pfs = &sctx->virtio_blk_pfs;
		ptype = SNAP_VIRTIO_BLK_PF;
	} else {
		return -EINVAL;
	}

	for (i = 0; i < pfs->max_pfs; i++) {
		struct snap_device_attr attr = {};
		struct snap_device *sdev;
		struct snap_pci *hotplug = NULL;

		attr.type = ptype;
		attr.pf_id = pfs->pfs[i].id;
		if (!pfs->pfs[i].plugged) {
			struct snap_hotplug_attr hp_attr = {};

			fprintf(stdout, "SNAP PF %d is not plugged. trying to hotplug\n", attr.pf_id);
			fflush(stdout);
			if (type == SNAP_VIRTIO_NET)
				hp_attr.device_id = 0x1000;
			else if (type == SNAP_VIRTIO_BLK)
				hp_attr.device_id = 0x1001;
			else if (type == SNAP_NVME)
				hp_attr.device_id = 0x6001;
			hp_attr.type = type;
			hp_attr.num_msix = 4;
			hotplug = snap_hotplug_pf(sctx, &hp_attr);
			if (!hotplug) {
				fprintf(stderr, "failed to hotplug SNAP pf %d\n", attr.pf_id);
				fflush(stderr);
				continue;
			} else {
				attr.pf_id = hotplug->id;
			}
		} else {
			fprintf(stdout, "SNAP PF %d is plugged. trying to create device\n", attr.pf_id);
			fflush(stdout);
		}
		sdev = snap_open_device(sctx, &attr);
		if (sdev) {
			fprintf(stdout, "SNAP device created: type=%d pf_id=%d\n", type, attr.pf_id);
			fflush(stdout);
			snap_close_device(sdev);
			if (hotplug)
				snap_hotunplug_pf(hotplug);
		} else {
			fprintf(stderr, "failed to create SNAP device: type=%d pf_id=%d\n", type, attr.pf_id);
			fflush(stderr);
			if (hotplug)
				snap_hotunplug_pf(hotplug);
			return -EINVAL;
		}
	}

	return 0;
}

int main(int argc, char **argv)
{
	struct ibv_device **list;
	int ret = 0, i, opt, dev_count, dev_type = 0;

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
			printf("Usage: snap_open_close_device -t <type: all, nvme, virtio_blk, virtio_net>\n");
			exit(1);
		}
	}

	if (!dev_type)
		dev_type = SNAP_NVME | SNAP_VIRTIO_BLK | SNAP_VIRTIO_NET;

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

		if (sctx->emulation_caps & SNAP_NVME & dev_type)
			snap_open_close_pf_helper(sctx, SNAP_NVME);
		if (sctx->emulation_caps & SNAP_VIRTIO_BLK & dev_type)
			snap_open_close_pf_helper(sctx, SNAP_VIRTIO_BLK);
		if (sctx->emulation_caps & SNAP_VIRTIO_NET & dev_type)
			snap_open_close_pf_helper(sctx, SNAP_VIRTIO_NET);

		snap_close(sctx);
	}

	ibv_free_device_list(list);
out:
	return ret;
}
