#include <stdio.h>

#include <infiniband/verbs.h>

#include "snap.h"

static int snap_get_pf_helper(struct snap_context *sctx,
	enum snap_emulation_type type, char *name)
{
	struct snap_pci **slist;
	struct snap_pfs_ctx *pfs;
	int j, scount;

	if (type == SNAP_NVME)
		pfs = &sctx->nvme_pfs;
	else if (type == SNAP_VIRTIO_NET)
		pfs = &sctx->virtio_net_pfs;
	else if (type == SNAP_VIRTIO_BLK)
		pfs = &sctx->virtio_blk_pfs;
	else
		return -EINVAL;

	slist = calloc(pfs->max_pfs, sizeof(*slist));
	if (!slist)
		return -ENOMEM;

	scount = snap_get_pf_list(sctx, type, slist);
	for (j = 0; j < scount; j++) {
		fprintf(stdout,
			"snap_type=%d pf_type=%d pf id=%d number=%d num_vfs=%d plugged=%d for %s\n",
			type, slist[j]->type, slist[j]->id, slist[j]->pci_number,
			slist[j]->num_vfs, slist[j]->plugged, name);
		fflush(stdout);
	}
	free(slist);

	return 0;
}

int main(int argc, char **argv)
{
	struct ibv_device **list;
	struct snap_context *sctx;
	int ret = 0, i, dev_count, dev_type = 0, opt;

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
			printf("Usage: snap_query_pfs -t <type: all, nvme, virtio_blk, virtio_net>\n");
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

		sctx = snap_open(list[i]);
		if (!sctx) {
			fprintf(stderr, "failed to create snap ctx for %s\n",
				list[i]->name);
			fflush(stderr);
			continue;
		}

		if (dev_type & SNAP_NVME)
			snap_get_pf_helper(sctx, SNAP_NVME, list[i]->name);
		if (dev_type & SNAP_VIRTIO_BLK)
			snap_get_pf_helper(sctx, SNAP_VIRTIO_BLK, list[i]->name);
		if (dev_type & SNAP_VIRTIO_NET)
			snap_get_pf_helper(sctx, SNAP_VIRTIO_NET, list[i]->name);

		snap_close(sctx);
	}

out:
	ibv_free_device_list(list);

	return ret;
}
