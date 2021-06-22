#include <stdlib.h>
#include "snap_fsd_dev.h"

static int snap_fsd_hande_fuse_req(void *ctx,
				   struct iovec *fuse_in_iov, int in_iovcnt,
				   struct iovec *fuse_out_iov, int out_iovcnt,
				   struct snap_fs_dev_io_done_ctx *done_ctx)
{
	return 0;
}

static void *snap_fsd_dev_dma_malloc(size_t size) {
	return calloc(1, size);
}

static void snap_fsd_dev_dma_free(void *buf) {
	free(buf);
}

struct snap_fs_dev *snap_fsd_dev_open(const struct snap_fs_dev_attrs *attrs)
{
	struct snap_fs_dev *fs_dev;

	fs_dev = calloc(1, sizeof(struct snap_fs_dev));
	if (!fs_dev)
		goto err;

	memcpy(&fs_dev->attrs, attrs, sizeof(fs_dev->attrs));

	fs_dev->ops.handle_req = snap_fsd_hande_fuse_req;
	fs_dev->ops.dma_malloc = snap_fsd_dev_dma_malloc;
	fs_dev->ops.dma_free   = snap_fsd_dev_dma_free;

	return fs_dev;

err:
	return NULL;
}

void snap_fsd_dev_close(struct snap_fs_dev *fs_dev)
{
	free(fs_dev);
}
