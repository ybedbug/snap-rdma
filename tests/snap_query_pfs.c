#include <stdio.h>

#include <infiniband/verbs.h>

#include "snap.h"

int main(int argc, char **argv)
{
	struct ibv_device **list;
	struct snap_context *sctx;
	int ret, i, dev_count;

	ret = snap_open();
	if (ret) {
		fprintf(stderr, "failed to open snap. ret=%d\n", ret);
		fflush(stderr);
		exit(1);
	}

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

		if (!snap_is_capable_device(list[i])) {
			fprintf(stderr, "device %s is not snap device\n",
				list[i]->name);
			fflush(stderr);
			continue;
		}
		sctx = snap_create_context(list[i]);
		if (!sctx) {
			fprintf(stderr, "failed to create snap ctx for %s\n",
				list[i]->name);
			fflush(stderr);
			continue;
		}
		slist = snap_get_pf_list(sctx, &scount);
		if (!slist) {
			fprintf(stderr, "failed to get snap pfs for %s\n",
				list[i]->name);
			fflush(stderr);
		} else {
			for (j = 0; j < scount; j++) {
				fprintf(stdout,
					"snap pf id=%d number=%d num_vfs=%d for %s\n",
					slist[j]->id, slist[j]->pci_number,
					slist[j]->num_vfs, list[i]->name);
				fflush(stdout);
			}
			snap_free_pf_list(slist);
		}
		snap_destroy_context(sctx);
	}

	ibv_free_device_list(list);
out:
	snap_close();

	return ret;
}
