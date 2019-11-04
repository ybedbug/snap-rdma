#include <stdio.h>

#include <infiniband/verbs.h>

#include "snap.h"

int main(int argc, char **argv)
{
	struct ibv_device **list;
	struct snap_context *sctx;
	int ret, i, dev_count;

	list = ibv_get_device_list(&dev_count);
	if (!list) {
		fprintf(stderr, "failed to open ib device list.\n");
		fflush(stderr);
		ret = 1;
		goto out;
	}

	for (i = 0; i < dev_count; i++) {
		struct snap_pci **slist;
		int j, scount;

		sctx = snap_open(list[i]);
		if (!sctx) {
			fprintf(stderr, "failed to create snap ctx for %s\n",
				list[i]->name);
			fflush(stderr);
			continue;
		}

		slist = calloc(sctx->max_pfs, sizeof(*slist));
		if (!slist) {
			snap_close(sctx);
			goto out;
		}

		scount = snap_get_pf_list(sctx, SNAP_NVME_PF, slist);
		for (j = 0; j < scount; j++) {
			fprintf(stdout,
				"snap pf id=%d number=%d num_vfs=%d for %s\n",
				slist[j]->id, slist[j]->pci_number,
				slist[j]->num_vfs, list[i]->name);
			fflush(stdout);
		}
		free(slist);

		snap_close(sctx);
	}

out:
	ibv_free_device_list(list);

	return ret;
}
