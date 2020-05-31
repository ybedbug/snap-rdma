#ifndef _SNAP_BLK_OPS_H
#define _SNAP_BLK_OPS_H

#include <sys/uio.h>

/**
 * typedef snap_bdev_io_done_cb_t - callback on io operations done
 * @result:	status of the finished operation
 * @done_arg:	user context given on operation request
 *
 * callback function called by block device when operation is finished
 */
typedef void (*snap_bdev_io_done_cb_t)(int result, void *done_arg);

/**
 * struct snap_bdev_ops - operations provided by block device
 * @read:		pointer to function which reads blocks from bdev
 * @write:		pointer to function which writes blocks to bdev
 * @flush:		pointer to function which flushes bdev
 * @write_zeros:	pointer to function which writes zeros to bdev
 * @discard:		pointer to function which discards blocks in bdev
 * @dma_malloc: 	pointer to function which allocates host memory
 * 			(like malloc). some block device frameworks (e.g. spdk)
 * 			require their own malloc-like function to be used.
 * @dma_free:		free memory allocated by dma_malloc
 * @get_num_blocks:	pointer to function which gets number of blocks in bdev
 * @get_block_size:	pointer to function which gets bdev block size
 * @get_bdev_name:	pointer to function which returns null terminated bdev
 * 			name
 *
 * operations provided by the block device given to the virtio controller
 * ToDo: add mechanism to tell which block operations are supported
 */
struct snap_bdev_ops {
	int (*read)(void *ctx, struct iovec *iov, int iovcnt,
		    uint64_t offset_blocks, uint64_t num_blocks,
		    snap_bdev_io_done_cb_t done_fn, void *done_arg);
	int (*write)(void *ctx, struct iovec *iov, int iovcnt,
		     uint64_t offset_blocks, uint64_t num_blocks,
		     snap_bdev_io_done_cb_t done_fn, void *done_arg);
	int (*flush)(void *ctx, uint64_t offset_blocks, uint64_t num_blocks,
		     snap_bdev_io_done_cb_t done_fn, void *done_arg);
	int (*write_zeroes)(void *ctx,
			    uint64_t offset_blocks, uint64_t num_blocks,
			    snap_bdev_io_done_cb_t done_fn, void *done_arg);
	int (*discard)(void *ctx, uint64_t offset_blocks, uint64_t num_blocks,
		       snap_bdev_io_done_cb_t done_fn, void *done_arg);
	void *(*dma_malloc)(size_t size);
	void (*dma_free)(void *buf);
	int (*get_num_blocks)(void *ctx);
	int (*get_block_size)(void *ctx);
	char *(*get_bdev_name)(void *ctx);
};

#endif
