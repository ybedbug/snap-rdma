#include <stdio.h>

#include <infiniband/verbs.h>

#include "snap.h"
#include "snap_test.h"

static int snap_get_pf_helper(struct snap_context *sctx,
	enum snap_emulation_type type, char *name)
{
	struct snap_pci **slist;
	struct snap_pfs_ctx *pfs;
	int j, scount, ret = 0;

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
	if (!scount) {
		fprintf(stderr, "no PFs for type %d on %s\n", type, name);
		fflush(stderr);
		ret = -ENOSYS;
		goto out;
	}
	for (j = 0; j < scount; j++) {
		fprintf(stdout,
			"snap_type=%d pf_type=%d pf id=%d number=%d num_vfs=%d plugged=%d for %s\n",
			type, slist[j]->type, slist[j]->id, slist[j]->pci_number,
			slist[j]->num_vfs, slist[j]->plugged, name);
		fflush(stdout);
	}

out:
	free(slist);

	return ret;
}

int main(int argc, char **argv)
{
	struct snap_context *sctx;
	int ret = 0, dev_type = 0, opt;

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

	sctx = snap_ctx_open(dev_type, NULL);
	if (!sctx) {
		fprintf(stderr, "failed to open snap ctx for %d types\n",
			dev_type);
		fflush(stderr);
		ret = -errno;
		goto out;
	}

	if (dev_type & SNAP_NVME)
		ret |= snap_get_pf_helper(sctx, SNAP_NVME,
					  sctx->context->device->name);
	if (dev_type & SNAP_VIRTIO_BLK)
		ret |= snap_get_pf_helper(sctx, SNAP_VIRTIO_BLK,
					  sctx->context->device->name);
	if (dev_type & SNAP_VIRTIO_NET)
		ret |= snap_get_pf_helper(sctx, SNAP_VIRTIO_NET,
					  sctx->context->device->name);

	snap_ctx_close(sctx);
out:
	return ret;
}
