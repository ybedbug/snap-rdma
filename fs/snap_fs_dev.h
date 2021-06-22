#ifndef _SNAP_FS_DEV_H
#define _SNAP_FS_DEV_H
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/uio.h>
#include "snap_fs_ops.h"

/*
 * enum snap_fs_dev_type - fs device types
 * @SNAP_FS_DEVICE:	FS device
 */
enum snap_fs_dev_type {
	VIRITO_FSD_DEVICE,
};

/**
 * struct snap_fs_dev_attrs
 * @type:	Type of the fs device
 * @tag_name:	FS tag name 
 */
struct snap_fs_dev_attrs {
	enum snap_fs_dev_type type;
	char tag_name[36];
};

/**
 * struct snap_fs_dev - FS device main data structure
 * @ops:	Operations pointers of the fs device
 * @attrs:	Attributes of the fs device
 */
struct snap_fs_dev {
	struct snap_fs_dev_ops ops;
	struct snap_fs_dev_attrs attrs;
};

struct snap_fs_dev *snap_fs_dev_open(const struct snap_fs_dev_attrs *attrs);
void snap_fs_dev_close(struct snap_fs_dev *fs_dev);
#endif
