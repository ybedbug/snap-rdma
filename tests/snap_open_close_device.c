#include <stdio.h>

#include <infiniband/verbs.h>

#include "snap.h"

static int snap_open_close_pf_helper(struct snap_context *sctx,
	enum snap_emulation_type type)
{
	struct snap_device *sdev;
	int max_pfs, i;
	struct snap_pci *spfs;
	enum snap_pci_type ptype;

	if (type == SNAP_NVME) {
		max_pfs = sctx->max_nvme_pfs;
		spfs = sctx->nvme_pfs;
		ptype = SNAP_NVME_PF;
	} else if (type == SNAP_VIRTIO_NET) {
		max_pfs = sctx->max_virtio_net_pfs;
		spfs = sctx->virtio_net_pfs;
		ptype = SNAP_VIRTIO_NET_PF;
	} else if (type == SNAP_VIRTIO_BLK) {
		max_pfs = sctx->max_virtio_blk_pfs;
		spfs = sctx->virtio_blk_pfs;
		ptype = SNAP_VIRTIO_BLK_PF;
	} else {
		return -EINVAL;
	}

	for (i = 0; i < max_pfs; i++) {
		struct snap_device_attr attr = {};
		struct snap_device *sdev;

		attr.type = ptype;
		attr.pf_id = spfs[i].id;
		sdev = snap_open_device(sctx, &attr);
		if (sdev) {
			fprintf(stdout, "SNAP device created: type=%d pf_id=%d\n", type, attr.pf_id);
			fflush(stdout);
			snap_close_device(sdev);
		} else {
			fprintf(stderr, "failed to create SNAP device: type=%d pf_id=%d\n", type, attr.pf_id);
			fflush(stderr);
			return -EINVAL;
		}
	}

	return 0;
}

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
		struct snap_context *sctx;

		sctx = snap_open(list[i]);
		if (!sctx) {
			fprintf(stderr, "failed to create snap ctx for %s err=%d. continue trying\n",
				list[i]->name, errno);
			fflush(stderr);
			continue;
		}

		if (sctx->emulation_caps & SNAP_NVME)
			snap_open_close_pf_helper(sctx, SNAP_NVME);
		if (sctx->emulation_caps & SNAP_VIRTIO_BLK)
			snap_open_close_pf_helper(sctx, SNAP_VIRTIO_BLK);
		if (sctx->emulation_caps & SNAP_VIRTIO_NET)
			snap_open_close_pf_helper(sctx, SNAP_VIRTIO_NET);

		snap_close(sctx);
	}

	ibv_free_device_list(list);
out:
	return ret;
}
