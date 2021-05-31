#include <stdio.h>

#include <infiniband/verbs.h>

#include "snap.h"
#include "snap_test.h"
#include "snap_virtio_common.h"

static int snap_open_close_pf_helper(struct snap_context *sctx,
	enum snap_emulation_type type, bool ev)
{
	int i;
	struct snap_pfs_ctx *pfs;
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
	} else if (type == SNAP_VIRTIO_FS) {
		pfs = &sctx->virtio_fs_pfs;
		ptype = SNAP_VIRTIO_FS_PF;

	} else {
		return -EINVAL;
	}

	for (i = 0; i < pfs->max_pfs; i++) {
		struct snap_device_attr attr = {};
		struct snap_device *sdev;
		struct snap_pci *hotplug = NULL;

		if (ev)
			attr.flags = SNAP_DEVICE_FLAGS_EVENT_CHANNEL;
		attr.type = ptype;
		attr.pf_id = pfs->pfs[i].id;
		if (!pfs->pfs[i].plugged) {
			struct snap_hotplug_attr hp_attr = {};

			fprintf(stdout, "SNAP PF %d is not plugged. trying to hotplug\n", attr.pf_id);
			fflush(stdout);
			if (type == SNAP_VIRTIO_NET) {
				hp_attr.pci_attr.device_id = 0x1000;
				hp_attr.regs.virtio_net.mac = 0x1100deadbeaf;
				hp_attr.regs.virtio_net.max_queues = 4;
				hp_attr.regs.virtio_net.queue_size = 16;
				hp_attr.regs.virtio_net.mtu = 1500;
			} else if (type == SNAP_VIRTIO_BLK) {
				hp_attr.pci_attr.device_id = 0x1001;
				hp_attr.regs.virtio_blk.max_queues = 4;
				hp_attr.regs.virtio_blk.queue_size = 16;
				hp_attr.regs.virtio_blk.capacity = 0x8000;
				hp_attr.regs.virtio_blk.size_max = 1024;
				hp_attr.regs.virtio_blk.seg_max = 512;
			} else if (type == SNAP_VIRTIO_FS) {
				hp_attr.pci_attr.class_code = 0x018000;
				hp_attr.pci_attr.device_id = 0x105A;
				hp_attr.pci_attr.vendor_id = 0x1AF4;
				hp_attr.pci_attr.subsystem_id = 0x1100;
				hp_attr.pci_attr.subsystem_vendor_id = 0x1AF4;

				hp_attr.regs.virtio_fs.device_features = SNAP_VIRTIO_F_VERSION_1;
				hp_attr.regs.virtio_fs.queue_size = 64;
				const char fs_name[] = "snap-fs";
				strncpy((char *)hp_attr.regs.virtio_fs.tag, fs_name, sizeof(hp_attr.regs.virtio_fs.tag));
				hp_attr.regs.virtio_fs.num_request_queues = 1;
			} else if (type == SNAP_NVME) {
				hp_attr.pci_attr.device_id = 0x6001;
			}
			hp_attr.type = type;
			hp_attr.pci_attr.num_msix = 4;
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
			fprintf(stdout, "SNAP device created: type=%d pf_id=%d fd=%d vhca_id=%d\n",
				type, attr.pf_id, snap_device_get_fd(sdev), sdev->pci->mpci.vhca_id);
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
	struct snap_context *sctx;
	bool ev = false;
	int ret = 0, opt, dev_type = 0;

	while ((opt = getopt(argc, argv, "et:")) != -1) {
		switch (opt) {
		case 'e':
			ev = true;
			break;
		case 't':
			if (!strcmp(optarg, "all"))
				dev_type = SNAP_NVME | SNAP_VIRTIO_BLK | SNAP_VIRTIO_NET;
			else if (!strcmp(optarg, "nvme"))
				dev_type = SNAP_NVME;
			else if (!strcmp(optarg, "virtio_blk"))
				dev_type = SNAP_VIRTIO_BLK;
			else if (!strcmp(optarg, "virtio_net"))
				dev_type = SNAP_VIRTIO_NET;
			else if (!strcmp(optarg, "virtio_fs"))
				dev_type = SNAP_VIRTIO_FS;
			else
				printf("Unknown type %s. Using default\n", optarg);
			break;
		default:
			printf("Usage: snap_open_close_device -t <type: all, nvme, virtio_blk, virtio_net, virtio_fs> [-e (event_channel)]\n");
			exit(1);
		}
	}

	if (!dev_type)
		dev_type = SNAP_NVME | SNAP_VIRTIO_BLK | SNAP_VIRTIO_NET | SNAP_VIRTIO_FS;


	sctx = snap_ctx_open(dev_type, NULL);
	if (!sctx) {
		fprintf(stderr, "failed to open snap ctx for %d types\n",
			dev_type);
		fflush(stderr);
		ret = -errno;
		goto out;
	} else {
		fprintf(stdout, "opened snap ctx for %d types on %s\n", dev_type,
			sctx->context->device->name);
		fflush(stdout);
	}

	if (dev_type & SNAP_NVME)
		snap_open_close_pf_helper(sctx, SNAP_NVME, ev);
	if (dev_type & SNAP_VIRTIO_BLK)
		snap_open_close_pf_helper(sctx, SNAP_VIRTIO_BLK, ev);
	if (dev_type & SNAP_VIRTIO_NET)
		snap_open_close_pf_helper(sctx, SNAP_VIRTIO_NET, ev);
	if (dev_type & SNAP_VIRTIO_FS)
		snap_open_close_pf_helper(sctx, SNAP_VIRTIO_FS, ev);

	snap_ctx_close(sctx);
	fprintf(stdout, "closed snap ctx for %d types\n", dev_type);
out:
	return ret;
}
