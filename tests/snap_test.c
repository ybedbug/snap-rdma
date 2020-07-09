#include <infiniband/verbs.h>

#include "snap_test.h"


struct snap_context *snap_ctx_open(int emulation_types, const char *manager)
{
	struct ibv_device **list;
	struct snap_context *sctx = NULL;
	int i, dev_count;

	list = ibv_get_device_list(&dev_count);
	if (!list) {
	    snap_error("failed to open device list for snap ctx");
	    goto out;
	}

	for (i = 0; i < dev_count; i++) {
		sctx = snap_open(list[i]);
		if (sctx &&
		    sctx->emulation_caps & emulation_types == emulation_types) {
			if (manager && strcmp(sctx->context->device->name, manager))
				sctx = NULL;
			else
				break;
		} else {
			sctx = NULL;
		}
	}

	ibv_free_device_list(list);
	return sctx;

out:
	return NULL;
}

void snap_ctx_close(struct snap_context *sctx)
{
	snap_close(sctx);
}

