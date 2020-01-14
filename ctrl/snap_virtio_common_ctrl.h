#ifndef SNAP_VIRTIO_COMMON_CTRL_H
#define SNAP_VIRTIO_COMMON_CTRL_H

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include "snap.h"
#include "snap_virtio_common.h"

struct snap_virtio_ctrl;

enum snap_virtio_ctrl_type {
	SNAP_VIRTIO_BLK_CTRL,
	SNAP_VIRTIO_NET_CTRL,
};

/*
 * Device status field according to virtio spec v1.1 (section 2.1)
 *
 * The virtio device state is discussed between device and driver
 * over the `device_status` PCI bar register, in a "bitmask" mode;
 * a.k.a multiple "statuses" can be configured simultaneously.
 *
 * Full description of statuses can be found on virtio spec ducomentation.
 *
 * NOTE: RESET status is unique, as instead of raising a bit in register,
 *       driver *unsets* all bits on register.
 */
enum snap_virtio_common_device_status {
	SNAP_VIRTIO_DEVICE_S_RESET = 0,
	SNAP_VIRTIO_DEVICE_S_ACKNOWLEDGE = 1 << 0,
	SNAP_VIRTIO_DEVICE_S_DRIVER = 1 << 1,
	SNAP_VIRTIO_DEVICE_S_DRIVER_OK = 1 << 2,
	SNAP_VIRTIO_DEVICE_S_FEATURES_OK = 1 << 3,
	SNAP_VIRTIO_DEVICE_S_DEVICE_NEEDS_RESET = 1 << 6,
	SNAP_VIRTIO_DEVICE_S_FAILED = 1 << 7,
};

/* Virtio controller states:
 *
 *  - STOPPED: All on-demand resources (virtqueues) are cleaned.
 *    Can be reached from various contexts:
 *    - initial state after controller creation.
 *    - state to move after error/flr detection.
 *    - state to move when closing application. In this case we must update
 *      host driver we are no longer operational by raising DEVICE_NEEDS_RESET
 *      bit in `device_status`.
 *    - state to move after virtio RESET detection. In this case we must update
 *      FW ctrl is stopped by writing back `0` to `device_status`.
 *
 *  - STARTED: Controller is live and ready to handle IO requests. All
 *    requested on-demand resources (virtqueues) are created successfully.
 */
enum snap_virtio_ctrl_state {
	SNAP_VIRTIO_CTRL_STOPPED,
	SNAP_VIRTIO_CTRL_STARTED,
};

struct snap_virtio_ctrl_attr {
	enum snap_virtio_ctrl_type type;
	int pf_id;
};

struct snap_virtio_ctrl_queue {
	struct snap_virtio_ctrl *ctrl;
	int index;
};

struct snap_virtio_queue_ops {
	struct snap_virtio_ctrl_queue *(*create)(struct snap_virtio_ctrl *ctrl,
						 int index);
	void (*destroy)(struct snap_virtio_ctrl_queue *queue);
};

struct snap_virtio_ctrl_bar_ops {
	struct snap_virtio_device_attr *(*create)(struct snap_virtio_ctrl *ctrl);
	void (*destroy)(struct snap_virtio_device_attr *ctrl);
	void (*copy)(struct snap_virtio_device_attr *orig,
		     struct snap_virtio_device_attr *copy);
	int (*update)(struct snap_virtio_ctrl *ctrl,
		      struct snap_virtio_device_attr *attr);
	int (*modify)(struct snap_virtio_ctrl *ctrl,
		      uint64_t mask, struct snap_virtio_device_attr *attr);
	struct snap_virtio_queue_attr *(*get_queue_attr)(
			struct snap_virtio_device_attr *vbar, int index);
};

struct snap_virtio_ctrl {
	enum snap_virtio_ctrl_type type;
	enum snap_virtio_ctrl_state state;
	pthread_mutex_t state_lock;
	struct snap_device *sdev;
	size_t num_queues;
	struct snap_virtio_ctrl_queue **queues;
	struct snap_virtio_queue_ops *q_ops;
	struct snap_virtio_ctrl_bar_ops *bar_ops;
	struct snap_virtio_device_attr *bar_curr;
	struct snap_virtio_device_attr *bar_prev;
};

int snap_virtio_ctrl_start(struct snap_virtio_ctrl *ctrl);
int snap_virtio_ctrl_stop(struct snap_virtio_ctrl *ctrl);
void snap_virtio_ctrl_progress(struct snap_virtio_ctrl *ctrl);
int snap_virtio_ctrl_open(struct snap_virtio_ctrl *ctrl,
			  struct snap_virtio_ctrl_bar_ops *bar_ops,
			  struct snap_virtio_queue_ops *q_ops,
			  struct snap_context *sctx,
			  const struct snap_virtio_ctrl_attr *attr);
void snap_virtio_ctrl_close(struct snap_virtio_ctrl *ctrl);
#endif
