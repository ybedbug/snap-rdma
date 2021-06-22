#ifndef _SNAP_FS_OPS_H
#define _SNAP_FS_OPS_H

#include <sys/uio.h>

/**
 * enum snap_fs_dev_op_status - Return status values for snap_fs_dev_io_done_cb_t
 * @SNAP_FS_DEV_OP_SUCCESS:	operation finished successfully
 * @SNAP_FS_DEV_OP_IO_ERROR:	operation failed due to IO error
 */
enum snap_fs_dev_op_status {
	SNAP_FS_DEV_OP_SUCCESS,
	SNAP_FS_DEV_OP_IO_ERROR
};

/**
 * typedef snap_fs_dev_io_done_cb_t - callback on io operations done
 * @status:	status of the finished operation
 * @done_arg:	user context given on operation request
 *
 * callback function called by fs device when operation is finished
 */
typedef void (*snap_fs_dev_io_done_cb_t)(enum snap_fs_dev_op_status status,
				       	 void *done_arg);

/**
 * struct snap_fs_dev_io_done_ctx - context given for dev ops
 * @cb: 	callback on io operation done
 * @user_arg:	user opaque argument given to cb
 */
struct snap_fs_dev_io_done_ctx {
	snap_fs_dev_io_done_cb_t cb;
	void *user_arg;
};

/**
 * struct snap_fs_dev_ops - operations provided by fs backend device
 * @handle_req:		pointer to function which handles fuse request
 * @dma_malloc: 	pointer to function which allocates host memory 
 * @dma_free: 		pointer to function which frees host memory
 *
 * operations provided by the fs backend device given to the virtio controller
 */
struct snap_fs_dev_ops {
	int (*handle_req)(void *ctx, struct iovec *fuse_in_iov, int in_iovcnt,
			  struct iovec *fuse_out_iov, int out_iovcnt,
		          struct snap_fs_dev_io_done_ctx *done_ctx);
	void *(*dma_malloc)(size_t size);
	void (*dma_free)(void *buf);	
};

#endif
