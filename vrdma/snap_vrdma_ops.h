#ifndef _SNAP_VRDMA_OPS_H
#define _SNAP_VRDMA_OPS_H

#include <sys/uio.h>
#include <stdbool.h>

/**
 * enum snap_vrdma_op_status - Return status values for snap_vrdma_io_done_cb_t
 * @SNAP_VRDMA_OP_SUCCESS:	operation finished successfully
 * @SNAP_VRDMA_OP_IO_ERROR:	operation failed due to IO error
 */
enum snap_vrdma_op_status {
	SNAP_VRDMA_OP_SUCCESS,
	SNAP_VRDMA_OP_IO_ERROR,
};

/**
 * typedef snap_vrdma_io_done_cb_t - callback on io operations done
 * @status:	status of the finished operation
 * @done_arg:	user context given on operation request
 *
 * callback function called by block device when operation is finished
 */
typedef void (*snap_vrdma_io_done_cb_t)(enum snap_vrdma_op_status status,
				       void *done_arg);


/**
 * struct snap_vrdma_io_done_ctx - context given for vrdma ops
 * @cb:		callback on io operation done
 * @user_arg:	user opaque argument given to cb
 */
struct snap_vrdma_io_done_ctx {
	snap_vrdma_io_done_cb_t cb;
	void *user_arg;
};

/**
 * struct snap_vrdma_ops - operations provided by vrdma device
 * operations provided by the vrdma device given to the vrdma controller
 * ToDo: add mechanism to tell which vrdma operations are supported
 */
struct snap_vrdma_ops {
	int (*vrdma_post_wqe)(void *ctx,
                          struct snap_vrdma_io_done_ctx *done_ctx);
};

#endif
