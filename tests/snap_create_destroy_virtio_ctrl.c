#include <stdio.h>
#include <signal.h>

#include <infiniband/verbs.h>

#include "snap.h"
#include "snap_virtio_blk_ctrl.h"
#include "snap_virtio_net_ctrl.h"
#include "snap_virtio_fs_ctrl.h"
#include "snap_blk_dev.h"
#include "snap_fs_dev.h"

#include "snap_test.h"

static bool keep_running = true;

void signal_handler(int dummy)
{
	keep_running = 0;
}

static struct snap_context *get_snap_ctxt(int *pf_id, int emu_type,
					  struct snap_pci **hotplug)
{
	struct snap_context *sctx, *found = NULL;
	struct snap_pfs_ctx *pfs = NULL;
	bool create_hotplug = (*pf_id == -1) ? true : false;
	int i;

	sctx = snap_ctx_open(emu_type, NULL);
	if (!sctx) {
		printf("Failed to open snap ctx for emu type: %d\n",
			emu_type);
		return NULL;
	}

	printf("Opened snap ctx for emu type: %d on %s\n", emu_type,
		sctx->context->device->name);

	if (emu_type == SNAP_VIRTIO_NET)
		pfs = &sctx->virtio_net_pfs;
	else if (emu_type == SNAP_VIRTIO_BLK)
		pfs = &sctx->virtio_blk_pfs;
	else if (emu_type == SNAP_VIRTIO_FS)
		pfs = &sctx->virtio_fs_pfs;

	if (create_hotplug == false) {
		for (i = 0; i < pfs->max_pfs; i++) {
			if (pfs->pfs[i].plugged && pfs->pfs[i].id == *pf_id) {
				found = sctx;
				break;
			}
		}
	}
	else {
		struct snap_hotplug_attr hp_attr = { 0 };
		const char *dev_type_str = "virtio_blk";

		found = sctx;

		// Create only one hot-plug emulation
		for (i = 0; i < pfs->max_pfs; ++i) {
			if (!pfs->pfs[i].plugged)
				break;
		}

		*pf_id = pfs->pfs[i].id;

		printf("SNAP PF: %d is not plugged - trying to hotplug\n", *pf_id);

		if (emu_type == SNAP_VIRTIO_BLK) {
			// From spdk_emu_hotplug_init_pci
			hp_attr.pci_attr.class_code = 0x018000;
			hp_attr.pci_attr.device_id = 0x1042;
			hp_attr.pci_attr.vendor_id = 0x1AF4;
			hp_attr.pci_attr.subsystem_id = 0x6001;
			hp_attr.pci_attr.subsystem_vendor_id = 0x6002;
			hp_attr.pci_attr.num_msix = 4;

			hp_attr.regs.virtio_blk.device_features = 0x100001046; // 0x300001046
			hp_attr.regs.virtio_blk.max_queues = 3;
			hp_attr.regs.virtio_blk.queue_size = 256;
			hp_attr.regs.virtio_blk.seg_max = 8;
			hp_attr.regs.virtio_blk.size_max = 16384;

			// TODO - should be correlated with snap_blk_dev_attrs
			// values.
			/*
			uint32_t blk_back_size_MB = 32;
			hp_attr.regs.virtio_blk.blk_size = 0x200;

			hp_attr.regs.virtio_blk.capacity =
				blk_back_size_MB * (1024 * 1024) / hp_attr.regs.virtio_blk.blk_size;
			*/

		} else if (emu_type == SNAP_VIRTIO_FS) {
			dev_type_str = "virtio_fs";
			// From spdk_emu_hotplug_init_pci
			hp_attr.pci_attr.class_code = 0x018000;
			hp_attr.pci_attr.device_id = 0x105A;
			hp_attr.pci_attr.vendor_id = 0x1AF4;
			hp_attr.pci_attr.subsystem_id = 0x6004;
			// 15b3  Mellanox Technologies
			hp_attr.pci_attr.subsystem_vendor_id = 0x15b3;
			// From virtio-fs env.:
			// 00:07.0 Mass storage controller: Red Hat, Inc Device 105a (rev 01)
			// Subsystem: Red Hat, Inc Device 1100
			// TODO: (rev 01)
			// hp_attr->pci_attr.revision_id = 1;
			hp_attr.pci_attr.num_msix = 4;

			hp_attr.regs.virtio_fs.device_features = SNAP_VIRTIO_F_VERSION_1;
			hp_attr.regs.virtio_fs.queue_size = 64;
			const char fs_name[] = "snap-fs";
			strncpy((char *)hp_attr.regs.virtio_fs.tag, fs_name, sizeof(hp_attr.regs.virtio_fs.tag));
			hp_attr.regs.virtio_fs.num_request_queues = 1;

		} else if (emu_type == SNAP_VIRTIO_NET) {
			// TODO hot-plug for virtio-net
			snap_ctx_close(sctx);
			found = NULL;
		}

		hp_attr.type = emu_type;
		*hotplug = snap_hotplug_pf(sctx, &hp_attr);
		if (!(*hotplug)) {
			printf("Failed to hotplug SNAP PF: %d \n", *pf_id);
			snap_ctx_close(sctx);
			found = NULL;
		}
		else {
			printf("A new PCI function %s was attached successfully \n",
				(*hotplug)->pci_number);

			printf("emulation_manager: %s\n", ibv_get_device_name(sctx->context->device));
			printf("emulation_type: %s\n", dev_type_str);
			printf("pci_type: physical function \n");
			printf("pci_index: %d \n", (*hotplug)->id);
			printf("pci_bdf: %s \n", (*hotplug)->pci_number);
		}
	}

	return found;
}

int main(int argc, char **argv)
{
	struct sigaction act;
	int ret = 0, opt;
	struct snap_virtio_blk_ctrl *blk_ctrl = NULL;
	struct snap_virtio_net_ctrl *net_ctrl = NULL;
	struct snap_virtio_fs_ctrl *fs_ctrl = NULL;

	struct snap_virtio_blk_ctrl_attr blk_attr = {};
	struct snap_virtio_fs_ctrl_attr fs_attr = {};
	struct snap_virtio_ctrl_bar_cbs bar_cbs = {};

	struct snap_blk_dev_attrs bdev_attrs = {0};
	struct snap_fs_dev_attrs fs_dev_attrs = {0};
	struct snap_virtio_net_ctrl_attr net_attr = {};

	struct snap_blk_dev *bdev = NULL;
	struct snap_fs_dev *fs_dev = NULL;

	enum snap_virtio_ctrl_type ctrl_type = SNAP_VIRTIO_BLK_CTRL;
	enum snap_emulation_type emu_type = SNAP_VIRTIO_BLK;

	// If pf_id set - use it, otherwise try hotplug
	int pf_id = -1;
	struct snap_pci *hotplug = NULL;
	struct snap_context *sctx = NULL;

	memset(&act, 0, sizeof(act));
	act.sa_handler = signal_handler;
	sigaction(SIGINT, &act, 0);
	sigaction(SIGPIPE, &act, 0);
	sigaction(SIGTERM, &act, 0);

	while ((opt = getopt(argc, argv, "t:f:")) != -1) {
		switch (opt) {
		case 't':
			if (!strcmp(optarg, "blk")) {
				ctrl_type = SNAP_VIRTIO_BLK_CTRL;
				emu_type = SNAP_VIRTIO_BLK;
			} else if (!strcmp(optarg, "net")) {
				ctrl_type = SNAP_VIRTIO_NET_CTRL;
				emu_type = SNAP_VIRTIO_NET;
			} else if (!strcmp(optarg, "fs")) {
				ctrl_type = SNAP_VIRTIO_FS_CTRL;
				emu_type = SNAP_VIRTIO_FS;
			} else {
				printf("Invalid ctrl ctrl_type %s\n", optarg);
				exit(1);
			}
			break;
		case 'f':
			pf_id = strtol(optarg, NULL, 0);
			break;
		default:
			printf("Usage: snap_create_destroy_virtio_ctrl "
				"-t <ctrl_type: blk, net, fs> "
				"-f <pf_id>\n");
			exit(1);
		}
	}

	sctx = get_snap_ctxt(&pf_id, emu_type, &hotplug);
	if (!sctx) {
		printf("Failed to get snap context for PF %d\n", pf_id);
		ret = -ENODEV;
		goto err;
	}

	if (ctrl_type == SNAP_VIRTIO_BLK_CTRL) {
		bdev_attrs.type = SNAP_BLOCK_DEVICE_NULL;
		bdev_attrs.size_b = 20;
		bdev_attrs.blk_size = 9;
		bdev = snap_blk_dev_open("null_blk", &bdev_attrs);
		if (!bdev) {
		    printf("Failed to open null block device\n");
		    goto close_sctx;
		}

		blk_attr.common.bar_cbs = &bar_cbs;
		blk_attr.common.pd = ibv_alloc_pd(sctx->context);
		if (!blk_attr.common.pd) {
			printf("Failed to alloc pd\n");
			goto close_bknd;
		}
		blk_attr.common.pf_id = pf_id;
		blk_attr.common.npgs = 1;
		blk_attr.common.pci_type = SNAP_VIRTIO_BLK_PF;
		blk_ctrl = snap_virtio_blk_ctrl_open(sctx, &blk_attr, &bdev->ops,
						     bdev);
		if (!blk_ctrl) {
			printf("Failed to create virtio-blk controller\n");
			ret = -ENODEV;
			goto free_pd;
		}
	} else if (ctrl_type == SNAP_VIRTIO_NET_CTRL) {
		net_attr.common.bar_cbs = &bar_cbs;
		net_attr.common.pf_id = pf_id;
		net_ctrl = snap_virtio_net_ctrl_open(sctx, &net_attr);
		if (!net_ctrl) {
			printf("Failed to create virtio-net controller\n");
			ret = -ENODEV;
			goto close_sctx;
		}
	} else {
		const char fs_name[] = "snap-fs";
		fs_dev_attrs.type = VIRITO_FSD_DEVICE;
		strncpy(fs_dev_attrs.tag_name, fs_name, sizeof(fs_dev_attrs.tag_name));
		fs_dev = snap_fs_dev_open(&fs_dev_attrs);
		if (!fs_dev) {
		    printf("Failed to open fs device\n");
		    goto close_sctx;
		}

		memset(&fs_attr, 0, sizeof(fs_attr));
		fs_attr.common.bar_cbs = &bar_cbs;
		fs_attr.common.pd = ibv_alloc_pd(sctx->context);
		if (!fs_attr.common.pd) {
			printf("Failed to alloc pd\n");
			goto close_bknd;
		}
		fs_attr.common.pf_id = pf_id;
		fs_attr.common.npgs = 1;
		fs_attr.common.pci_type = SNAP_VIRTIO_FS_PF;

		fs_attr.regs.queue_size = 64;
		fs_attr.regs.device_features = SNAP_VIRTIO_F_VERSION_1;

		strncpy((char *)fs_attr.regs.tag, fs_name, sizeof(fs_attr.regs.tag));
		fs_attr.regs.num_request_queues = 1;
		fs_ctrl = snap_virtio_fs_ctrl_open(sctx, &fs_attr, &fs_dev->ops, fs_dev);
		if (!fs_ctrl) {
			printf("Failed to create virtio-fs controller\n");
			ret = -ENODEV;
			goto free_pd;
		}
	}
	printf("virtio controller of type %d opened successfully\n", ctrl_type);

	while (keep_running) {
		if (ctrl_type == SNAP_VIRTIO_BLK_CTRL) {
			snap_virtio_blk_ctrl_progress(blk_ctrl);
			snap_virtio_blk_ctrl_io_progress(blk_ctrl);
		} else if (ctrl_type == SNAP_VIRTIO_NET_CTRL) {
			snap_virtio_net_ctrl_progress(net_ctrl);
			snap_virtio_net_ctrl_io_progress(net_ctrl);
		} else {
			snap_virtio_fs_ctrl_progress(fs_ctrl);
			snap_virtio_fs_ctrl_io_progress(fs_ctrl);
		}
		// To be able to work with VirtIOBlockTest we need handle requests with minimal latency
		//usleep(200 * 1000);
	}

	if (ctrl_type == SNAP_VIRTIO_BLK_CTRL)
		snap_virtio_blk_ctrl_close(blk_ctrl, 0);
	else if (ctrl_type == SNAP_VIRTIO_NET_CTRL)
		snap_virtio_net_ctrl_close(net_ctrl);
	else
		snap_virtio_fs_ctrl_close(fs_ctrl);

	printf("virtio controller is closed\n");

	if (hotplug) {
		printf("PCI function %s is detached\n", hotplug->pci_number);
		snap_hotunplug_pf(hotplug);
	}

free_pd:
	if (ctrl_type == SNAP_VIRTIO_BLK_CTRL)
		ibv_dealloc_pd(blk_attr.common.pd);
	else if (ctrl_type == SNAP_VIRTIO_FS_CTRL)
		ibv_dealloc_pd(fs_attr.common.pd);
close_bknd:
	if (ctrl_type == SNAP_VIRTIO_BLK_CTRL)
		snap_blk_dev_close(bdev);
	else if (ctrl_type == SNAP_VIRTIO_FS_CTRL)
		snap_fs_dev_close(fs_dev);
close_sctx:
	snap_ctx_close(sctx);
err:
	return ret;
}
