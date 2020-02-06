#ifndef _SNAP_NULL_BLK_DEV_H
#define _SNAP_NULL_BLK_DEV_H
#include "snap_blk_dev.h"

struct snap_blk_dev *snap_null_blk_dev_open(const char *name,
					    const struct snap_blk_dev_attrs *attrs);
void snap_null_blk_dev_close(struct snap_blk_dev *bdev);
#endif
