#include <stdio.h>
#include <signal.h>

#include <infiniband/verbs.h>

#include "snap.h"
#include "snap_nvme_ctrl.h"
#include "snap_test.h"

static bool keep_running = true;

void signal_handler(int dummy)
{
	keep_running = false;
}

int main(int argc, char **argv)
{
	struct snap_nvme_ctrl_attr attr = {};
	struct snap_context *sctx;
	struct snap_nvme_ctrl *ctrl;
	struct sigaction act;
	int ret = 0, pf_id = 0;

	memset(&act, 0, sizeof(act));
	act.sa_handler = signal_handler;
	sigaction(SIGINT, &act, 0);
	sigaction(SIGPIPE, &act, 0);
	sigaction(SIGTERM, &act, 0);

	sctx = snap_ctx_open(SNAP_NVME);
	if (!sctx) {
		fprintf(stderr, "failed to open snap ctx for NVMe\n");
		fflush(stderr);
		ret = -errno;
		goto out;
	}

	attr.type = SNAP_NVME_CTRL_PF;
	attr.pf_id = 0;
	ctrl = snap_nvme_ctrl_open(sctx, &attr);
	if (!ctrl) {
		fprintf(stderr, "failed to open snap NVMe ctrl\n");
		fflush(stderr);
		ret = -errno;
		goto out_close_ctx;
	}

	fprintf(stdout, "NVMe ctrl opened on %s with id %d. Progressing\n",
		sctx->context->device->name, pf_id);
	fflush(stdout);

	while (keep_running)
		usleep(200000);

	snap_nvme_ctrl_close(ctrl);

	fprintf(stdout, "NVMe ctrl closed.\n");
	fflush(stdout);

out_close_ctx:
	snap_ctx_close(sctx);
out:
	return ret;
}
