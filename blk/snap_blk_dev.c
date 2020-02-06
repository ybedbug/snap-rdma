#include "snap_blk_dev.h"
#include "snap_null_blk_dev.h"

/**
 * snap_blk_dev_open() - Opens a block device
 * @name:	block device name
 * @attrs:	creation attributes
 *
 * opens a block device.
 */
struct snap_blk_dev *snap_blk_dev_open(const char *name,
				       const struct snap_blk_dev_attrs *attrs)
{
	struct snap_blk_dev *bdev = NULL;

	if (attrs->type == SNAP_BLOCK_DEVICE_NULL)
		bdev = snap_null_blk_dev_open(name, attrs);
	else
		printf("Invalid block device type %d\n", attrs->type);

	return bdev;
}

/**
 * snap_blk_dev_close() - Closes a block device
 * @bdev: block device to close
 */
void snap_blk_dev_close(struct snap_blk_dev *bdev)
{
	if (bdev->attrs.type == SNAP_BLOCK_DEVICE_NULL)
		snap_null_blk_dev_close(bdev);
	else
		printf("Invalid block device type %d\n", bdev->attrs.type);
}
