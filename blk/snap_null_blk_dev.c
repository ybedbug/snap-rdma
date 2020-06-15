#include <stdlib.h>
#include "snap_null_blk_dev.h"

static int snap_null_blk_dev_read(void *ctx,
				  struct iovec *iov, int iovcnt,
				  uint64_t offset_blocks, uint64_t num_blocks,
				  snap_bdev_io_done_cb_t done_fn,
				  void *done_arg)
{
	done_fn(BLOCK_DEVICE_IO_SUCCESS, done_arg);
	return 0;
}

static int snap_null_blk_dev_write(void *ctx,
				   struct iovec *iov, int iovcnt,
				   uint64_t offset_blocks, uint64_t num_blocks,
				   snap_bdev_io_done_cb_t done_fn,
				   void *done_arg)
{
	done_fn(BLOCK_DEVICE_IO_SUCCESS, done_arg);
	return 0;
}

static int snap_null_blk_dev_flush(void *ctx,
				   uint64_t offset_blocks, uint64_t num_blocks,
				   snap_bdev_io_done_cb_t done_fn,
				   void *done_arg)
{
	done_fn(BLOCK_DEVICE_IO_SUCCESS, done_arg);
	return 0;
}

static int snap_null_blk_dev_write_zeroes(void *ctx,
					  uint64_t offset_blocks,
					  uint64_t num_blocks,
					  snap_bdev_io_done_cb_t done_fn,
					  void *done_arg)
{
	done_fn(BLOCK_DEVICE_IO_SUCCESS, done_arg);
	return 0;
}

static int snap_null_blk_dev_discard(void *ctx,
				     uint64_t offset_blocks,
				     uint64_t num_blocks,
				     snap_bdev_io_done_cb_t done_fn,
				     void *done_arg)
{
	done_fn(BLOCK_DEVICE_IO_SUCCESS, done_arg);
	return 0;
}

static void *snap_null_blk_dev_dma_malloc(size_t size) {
	return calloc(1, size);
}

static void snap_null_blk_dev_dma_free(void *buf) {
	free(buf);
}

static int snap_null_blk_dev_get_num_blocks(void *ctx) {
	struct snap_blk_dev *bdev = (struct snap_blk_dev *)ctx;
	return bdev->attrs.size_b;
}

static int snap_null_blk_dev_get_block_size(void *ctx) {
	struct snap_blk_dev *bdev = (struct snap_blk_dev *)ctx;
	return bdev->attrs.blk_size;
}

static const char *snap_null_blk_dev_get_bdev_name(void *ctx) {
	struct snap_blk_dev *bdev = (struct snap_blk_dev *)ctx;
	return bdev->name;
}

struct snap_blk_dev *snap_null_blk_dev_open(const char *name,
				       const struct snap_blk_dev_attrs *attrs)
{
	struct snap_blk_dev *bdev;

	bdev = calloc(1, sizeof(struct snap_blk_dev));
	if (!bdev)
		goto err;

	bdev->name = strdup(name);
	if (!bdev->name)
		goto free_bdev;
	memcpy(&bdev->attrs, attrs, sizeof(bdev->attrs));

	bdev->ops.read = snap_null_blk_dev_read;
	bdev->ops.write = snap_null_blk_dev_write;
	bdev->ops.flush = snap_null_blk_dev_flush;
	bdev->ops.write_zeroes = snap_null_blk_dev_write_zeroes;
	bdev->ops.discard = snap_null_blk_dev_discard;
	bdev->ops.dma_malloc = snap_null_blk_dev_dma_malloc;
	bdev->ops.dma_free = snap_null_blk_dev_dma_free;
	bdev->ops.get_num_blocks = snap_null_blk_dev_get_num_blocks;
	bdev->ops.get_block_size = snap_null_blk_dev_get_block_size;
	bdev->ops.get_bdev_name = snap_null_blk_dev_get_bdev_name;

	return bdev;

free_bdev:
	free(bdev);
err:
	return NULL;
}

void snap_null_blk_dev_close(struct snap_blk_dev *bdev)
{
	free(bdev->name);
	free(bdev);
}
