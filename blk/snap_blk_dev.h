#ifndef _SNAP_BLK_DEV_H
#define _SNAP_BLK_DEV_H
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/uio.h>
#include "snap_blk_ops.h"

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
