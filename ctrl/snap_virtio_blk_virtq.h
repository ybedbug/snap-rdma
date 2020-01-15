#ifndef _BLK_VIRTQ_H
#define _BLK_VIRTQ_H

#include "snap.h"
#include <sys/uio.h>
#include "snap_blk_ops.h"

/**
 * struct virtq_bdev - Backend block device
 * @ctx:	Opaque bdev context given to block device functions
 * @ops:	Block device operation pointers
 */
struct virtq_bdev {
	void *ctx;
	struct virtq_bdev_ops *ops;
};

/**
 * struct blk_virtq_ctx - Main struct for blk_virtq
 * @idx:	Virtqueue index
 * @fatal_err:	Fatal error flag
 * @priv:	Opaque privte struct used for implementation
 */
struct blk_virtq_ctx {
	int idx;
	bool fatal_err;
	void *priv;
};

/**
 * struct blk_virtq_create_attr - Attributes given for virtq creation
 * @idx:	Virtqueue index
 * @size_max:	VIRTIO_BLK_F_SIZE_MAX (from virtio spec)
 * @seg_max:	VIRTIO_BLK_F_SEG_MAX (from virtio spec)
 * @queue_size:	VIRTIO_QUEUE_SIZE (from virtio spec)
 * @pd:		Protection domain on which rdma-qps will be opened
 * @desc:	Descriptor Area (from virtio spec Virtqueues section)
 * @driver	Driver Area
 * @device	Device Area
 */
struct blk_virtq_create_attr {
	int idx;
	int size_max;
	int seg_max;
	int queue_size;
	struct ibv_pd *pd;
	uint64_t desc;
	uint64_t driver;
	uint64_t device;
	uint16_t max_tunnel_desc;
	uint16_t msix_vector;
	bool virtio_version_1_0;
};

struct blk_virtq_ctx *blk_virtq_create(struct virtq_bdev *blk_dev,
				       struct snap_device *snap_dev,
				       struct blk_virtq_create_attr *attr);
void blk_virtq_destroy(struct blk_virtq_ctx *q);
int blk_virtq_progress(struct blk_virtq_ctx *q);
#endif
