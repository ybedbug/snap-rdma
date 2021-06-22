#ifndef _SNAP_FSD_DEV_H
#define _SNAP_FSD_DEV_H

#include "snap_fs_dev.h"

struct snap_fs_dev *snap_fsd_dev_open(const struct snap_fs_dev_attrs *attrs);
void snap_fsd_dev_close(struct snap_fs_dev *fs_dev);

#endif
