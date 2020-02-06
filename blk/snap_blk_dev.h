#ifndef _SNAP_BLK_DEV_H
#define _SNAP_BLK_DEV_H
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/uio.h>
#include "snap_blk_ops.h"

/**
 * enum snap_blk_dev_cmd_status - Return status values on bdev_io_done_cb
 * @BLOCK_DEVICE_IO_SUCCESS: 	IO operation succeeded
 * @BLOCK_DEVICE_IO_ERROR:	IO operation failed
 */
enum snap_blk_dev_cmd_status {
	BLOCK_DEVICE_IO_SUCCESS,
	BLOCK_DEVICE_IO_ERROR,
};

/**
 * bdev_io_done_cb_t - Callback on io done
 * @result:	status of command finished
 * @done_arg:	user provided argument given upon io request
 *
 * Callback given to the block device, should be called
 * when IO command is completed
 */
typedef void (*bdev_io_done_cb_t)(enum snap_blk_dev_cmd_status result,
				  void *done_arg);

/*
 * enum snap_blk_dev_type - bdev types
 * @SNAP_BLOCK_DEVICE_NULL:	NULL Block device
 */
enum snap_blk_dev_type {
	SNAP_BLOCK_DEVICE_NULL,
};

/**
 * struct snap_blk_dev_attrs
 * @type:	Type of the bdev
 * @size_b:	Size in blocks
 * @blk_size:	Block size
 */
struct snap_blk_dev_attrs {
	enum snap_blk_dev_type type;
	uint64_t size_b;
	uint32_t blk_size;
};

/**
 * struct snap_blk_dev - Block device main data structure
 * @name:	Name of block device
 * @ops:	Operations pointers of the bdev
 * @attrs:	Attributes of the bdev
 */
struct snap_blk_dev {
	char *name;
	struct snap_bdev_ops ops;
	struct snap_blk_dev_attrs attrs;
};

struct snap_blk_dev *snap_blk_dev_open(const char *name,
				       const struct snap_blk_dev_attrs *attrs);
void snap_blk_dev_close(struct snap_blk_dev *bdev);
#endif
