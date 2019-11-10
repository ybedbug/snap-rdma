#include <stdio.h>

#include <infiniband/verbs.h>

#include "snap.h"
#include "snap_nvme.h"

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
		struct snap_device_attr attr = {};
		struct snap_context *sctx;
		struct snap_device *sdev;
		int j;

		sctx = snap_open(list[i]);
		if (!sctx) {
			fprintf(stderr, "failed to create snap ctx for %s err=%d. continue trying\n",
				list[i]->name, errno);
			fflush(stderr);
			continue;
		}

		attr.type = SNAP_NVME_PF;
		attr.pf_id = 0;
		sdev = snap_open_device(sctx, &attr);
		if (sdev) {
			ret = snap_nvme_init_device(sdev);
			if (!ret) {
				fprintf(stdout, "created NVMe dev for pf %d. creating 16 cqs\n",
					attr.pf_id);
				fflush(stdout);
				for (j = 1; j < 17; j++) {
					struct snap_nvme_cq_attr cq_attr = {};
					struct snap_nvme_cq *cq;

					cq_attr.type = j % 2 ? SNAP_NVME_SQE_MODE : SNAP_NVME_CC_MODE;
					cq_attr.id = j;
					cq_attr.doorbell_offset = 4 + j * 8;
					cq_attr.msix = j;
					cq_attr.queue_depth = 16;
					cq_attr.base_addr = 0xdeadbeef * j;
					cq_attr.cq_period = j * 4;
					cq_attr.cq_max_count = j * 8;
					cq = snap_nvme_create_cq(sdev, &cq_attr);
					if (cq) {
						snap_nvme_destroy_cq(cq);
					} else {
						fprintf(stderr, "failed to create NVMe cq id=%d\n", j);
						fflush(stderr);
					}
				}
				snap_nvme_teardown_device(sdev);
			} else {
				fprintf(stderr, "failed to create NVMe dev for pf %d ret=%d\n",
					attr.pf_id, ret);
				fflush(stderr);
			}
			snap_close_device(sdev);
		} else {
			fprintf(stderr, "failed to create device %d for %s\n",
				attr.pf_id, list[i]->name);
			fflush(stderr);
		}

		snap_close(sctx);
	}

	ibv_free_device_list(list);
out:
	return ret;
}
