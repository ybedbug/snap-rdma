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
	int ret = 0, opt, num_queues = 10;
	int i;
	enum snap_nvme_queue_type q_type = SNAP_NVME_RAW_MODE;

	while ((opt = getopt(argc, argv, "n:t:")) != -1) {
		switch (opt) {
		case 'n':
			num_queues = atoi(optarg);
			break;
		case 't':
			if (!strcmp(optarg, "raw"))
				q_type = SNAP_NVME_RAW_MODE;
			else if (!strcmp(optarg, "nvmf"))
				q_type = SNAP_NVME_TO_NVMF_MODE;
			break;
		default:
			printf("Usage: snap_create_destroy_nvme_cq -n <num> -t <type: raw, nvmf>\n");
			exit(1);
		}
	}

	sctx = snap_ctx_open(SNAP_NVME);
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
		ret = snap_nvme_init_device(sdev);
		if (!ret) {
			fprintf(stdout, "created NVMe dev for pf %d. creating %d cqs type %d\n",
				attr.pf_id, num_queues, q_type);
			fflush(stdout);
			for (i = 1; i < num_queues + 1; i++) {
				struct snap_nvme_cq_attr cq_attr = {};
				struct snap_nvme_cq *cq;

				cq_attr.type = q_type;
				cq_attr.id = i;
				cq_attr.msix = i;
				cq_attr.queue_depth = 16;
				cq_attr.base_addr = 0xdeadbeef * i;
				cq_attr.cq_period = i * 4;
				cq_attr.cq_max_count = i * 8;
				cq = snap_nvme_create_cq(sdev, &cq_attr);
				if (cq) {
					fprintf(stdout, "NVMe cq id=%d created !\n", i);
					fflush(stdout);
					snap_nvme_destroy_cq(cq);
				} else {
					fprintf(stderr, "failed to create NVMe cq id=%d\n", i);
					fflush(stderr);
				}
			}
			snap_nvme_teardown_device(sdev);
		} else {
			fprintf(stderr, "failed to init NVMe dev for pf %d ret=%d\n",
				attr.pf_id, ret);
			fflush(stderr);
		}
		snap_close_device(sdev);
	} else {
		fprintf(stderr, "failed to create device %d for %s\n",
			attr.pf_id, sctx->context->device->name);
		fflush(stderr);
		ret = -errno;
	}

	snap_ctx_close(sctx);
out:
	return ret;
}
