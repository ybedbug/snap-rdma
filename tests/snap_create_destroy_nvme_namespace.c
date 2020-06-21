#include <stdio.h>

#include <infiniband/verbs.h>

#include "snap.h"
#include "snap_nvme.h"
#include "snap_test.h"

int main(int argc, char **argv)
{
	int ret = 0, i, opt, num_namespaces = 10, lba = 9, md;
	struct snap_device_attr attr = {};
	struct snap_context *sctx;
	struct snap_device *sdev;

	while ((opt = getopt(argc, argv, "n:l:m:")) != -1) {
		switch (opt) {
		case 'n':
			num_namespaces = atoi(optarg);
			break;
		case 'l':
			lba = atoi(optarg);
			break;
		case 'm':
			md = atoi(optarg);
			break;
		default:
			printf("Usage: snap_create_destroy_nvme_namespace -n <num> -l <lba_shift> -m <md size>\n");
			exit(1);
		}
	}

	sctx = snap_ctx_open(SNAP_NVME);
	if (!sctx) {
		fprintf(stderr, "failed to open snap ctx for NVMe\n");
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
			fprintf(stdout, "created NVMe dev for pf %d. creating %d namespaces lba=%d md=%d\n",
				attr.pf_id, num_namespaces, lba, md);
			fflush(stdout);
			for (i = 1; i < num_namespaces + 1; i++) {
				struct snap_nvme_namespace_attr ns_attr = {};
				struct snap_nvme_namespace *ns;

				ns_attr.src_nsid = i;
				ns_attr.dst_nsid = i;
				ns_attr.lba_size = lba;
				ns_attr.md_size = md;
				ns = snap_nvme_create_namespace(sdev, &ns_attr);
				if (ns) {
					snap_nvme_destroy_namespace(ns);
				} else {
					fprintf(stderr, "failed to create NVMe ns id=%d\n", i);
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
	}

	snap_ctx_close(sctx);
out:
	return ret;
}
