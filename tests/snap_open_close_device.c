#include <stdio.h>

#include <infiniband/verbs.h>

#include <snap.h>

int main(int argc, char **argv)
{
	struct ibv_device **list;
	struct snap_context *sctx;
	struct snap_device *sdev;
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
		struct snap_device_attr attr = {};

		if (!snap_is_capable_device(list[i])) {
			fprintf(stderr, "device %s is not snap device\n",
				list[i]->name);
			fflush(stderr);
			continue;
		}
		sctx = snap_create_context(list[i]);
		if (!sctx)
			continue;

		attr.type = SNAP_NVME_PF_DEV;
		attr.pf_id = 0;
		sdev = snap_open_device(sctx, &attr);
		if (sdev) {
			snap_close_device(sdev);
		} else {
			fprintf(stderr, "failed to create device %d for %s\n",
				attr.pf_id, list[i]->name);
			fflush(stderr);
		}

		snap_destroy_context(sctx);
	}

	ibv_free_device_list(list);
out:
	snap_close();

	return ret;
}
