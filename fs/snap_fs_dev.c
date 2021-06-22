#include "snap_fs_dev.h"
#include "snap_fsd_dev.h"
#include "snap.h"

/**
 * snap_fs_dev_open() - Opens a fs device
 * @attrs:	creation attributes
 *
 * opens a fs device.
 */
struct snap_fs_dev *snap_fs_dev_open(const struct snap_fs_dev_attrs *attrs)
{
	struct snap_fs_dev *fs_dev = NULL;

	if (attrs->type == VIRITO_FSD_DEVICE)
		fs_dev = snap_fsd_dev_open(attrs);
	else
		snap_error("Invalid fs device type %d\n", attrs->type);

	return fs_dev;
}

/**
 * snap_fs_dev_close() - Closes a fs backend device
 * @fs_dev: fs backend device to close
 */
void snap_fs_dev_close(struct snap_fs_dev *fs_dev)
{
	if (fs_dev->attrs.type == VIRITO_FSD_DEVICE)
		snap_fsd_dev_close(fs_dev);
	else
		snap_error("Invalid fs device type %d\n", fs_dev->attrs.type);
}
