#include <stdio.h>

#include <infiniband/verbs.h>

#include "snap.h"
#include "snap_nvme.h"
#include "snap_test.h"

int main(int argc, char **argv)
{
	struct snap_device_attr attr = {};
	struct snap_context *sctx;
	struct snap_device *sdev;
	int ret;

	sctx = snap_ctx_open(SNAP_NVME, NULL);
	if (!sctx) {
		fprintf(stderr, "failed to open snap ctx for NVMe dev\n");
		fflush(stderr);
		ret = -errno;
		goto out;
	}

	attr.type = SNAP_NVME_PF;
	attr.pf_id = 0;
	sdev = snap_open_device(sctx, &attr);
	if (sdev) {
		struct snap_nvme_device_attr nvme_attr = {};

		ret = snap_nvme_query_device(sdev, &nvme_attr);
		if (!ret) {
			fprintf(stdout, "queried NVMe dev enabled=%d for pf=%d\n",
				nvme_attr.enabled, attr.pf_id);
			fflush(stdout);
		} else {
			fprintf(stderr, "failed to query NVMe dev for pf %d ret=%d\n",
				attr.pf_id, ret);
			fflush(stderr);
		}
		snap_close_device(sdev);
	} else {
		fprintf(stderr, "failed to create device %d for %s\n", attr.pf_id,
			sctx->context->device->name);
		fflush(stderr);
	}

	snap_ctx_close(sctx);
out:
	return ret;
}
