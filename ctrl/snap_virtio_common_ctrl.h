/*
 * Copyright © 2021 NVIDIA CORPORATION & AFFILIATES. ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of Nvidia Corporation and its affiliates
 * (the "Company") and all right, title, and interest in and to the software
 * product, including all associated intellectual property rights, are and
 * shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 */

#ifndef SNAP_VIRTIO_COMMON_CTRL_H
#define SNAP_VIRTIO_COMMON_CTRL_H

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include "virtio_blk_ext.h"
#include "snap.h"
#include "snap_virtio_common.h"
#include "snap_poll_groups.h"

struct snap_virtio_ctrl;
struct snap_virtio_ctrl_queue;

enum snap_virtio_ctrl_type {
	SNAP_VIRTIO_BLK_CTRL,
	SNAP_VIRTIO_NET_CTRL,
	SNAP_VIRTIO_FS_CTRL,
};

/*
 * NOTE: RESET status is unique, as instead of raising a bit in register,
 *       driver *unsets* all bits on register.
 *       We set it as an additional state for convenience reasons.
 */
#define VIRTIO_CONFIG_S_RESET 0

/**
 * enum snap_virtio_ctrl_state - Virtio controller internal state
 *
 *  @STOPPED: All on-demand resources (virtqueues) are cleaned.
 *    Can be reached from various contexts:
 *    - initial state after controller creation.
 *    - state to move after error/flr detection.
 *    - state to move when closing application. In this case we must update
 *      host driver we are no longer operational by raising DEVICE_NEEDS_RESET
 *      bit in `device_status`.
 *    - state to move after virtio RESET detection. In this case we must update
 *      FW ctrl is stopped by writing back `0` to `device_status`.
 *
 *  @STARTED: Controller is live and ready to handle IO requests. All
 *    requested on-demand resources (virtqueues) are created successfully.
 *
 *  @SUSPENED: All enabled queues are flushed and suspended. Internal controller
 *    state will stay constant. DMA access to the host memory is stopped. The
 *    state is equivalent of doing quiesce+freeze in live migration terms.
 *    In order to do a safe shutdown, application should put controller in the
 *    suspended state before controller is stopped.
 *    NOTE: going to the suspened state is an async operation. Reverse operation
 *    (resume) is a sync operation.
 *
 *  @SUSPENDING: indicates that suspend operation is in progress
 *
 *  Normal flow:
 *  STOPPED -> STARTED - [SUSPENDING] -> SUSPENDED -> STOPPED
 *
 *  Allowed transitions:
 *  STOPPED -> STARTED, SUSPENDED
 *  STARTED - [SUSPENDING] -> SUSPENDED
 *	  -> STOPPED  NOTE: this is not a safe transition. If there is outstanding
 *			    io, the controller may crash.
 *  SUSPENDED -> STOPPED, STARTED
 */
enum snap_virtio_ctrl_state {
	SNAP_VIRTIO_CTRL_STOPPED,
	SNAP_VIRTIO_CTRL_STARTED,
	SNAP_VIRTIO_CTRL_SUSPENDED,
	SNAP_VIRTIO_CTRL_SUSPENDING
};

struct snap_virtio_ctrl_bar_cbs {
	int (*validate)(void *cb_ctx);
	int (*start)(void *cb_ctx);
	int (*stop)(void *cb_ctx);
	int (*num_vfs_changed)(void *cb_ctx, uint16_t new_numvfs);
	int (*pre_flr)(void *cb_ctx);
	int (*post_flr)(void *cb_ctx);
};

struct snap_virtio_ctrl_attr {
	struct ibv_context *context;
	enum snap_virtio_ctrl_type type;
	enum snap_pci_type pci_type;
	int pf_id;
	int vf_id;
	bool event;
	void *cb_ctx;
	struct snap_virtio_ctrl_bar_cbs *bar_cbs;
	struct ibv_pd *pd;
	uint32_t npgs;
	bool force_in_order;
	bool suspended;
	bool recover;
	bool vf_dynamic_msix_supported;
};

struct snap_virtio_ctrl_queue {
	struct snap_virtio_ctrl *ctrl;
	int index;
	struct snap_pg *pg;
	struct snap_pg_q_entry pg_q;
	bool log_writes_to_host;

	TAILQ_ENTRY(snap_virtio_ctrl_queue) entry;
	int thread_id;
};

struct snap_virtio_ctrl_queue_counter {
	uint64_t total;
	uint64_t success;
	uint64_t fail;
	uint64_t unordered;
	uint64_t merged_desc;
	uint64_t long_desc_chain;
	uint64_t large_in_buf;

};

struct snap_virtio_ctrl_queue_stats {
	struct snap_virtio_ctrl_queue_counter read;
	struct snap_virtio_ctrl_queue_counter write;
	struct snap_virtio_ctrl_queue_counter flush;
};

struct snap_virtio_ctrl_queue_state;

struct snap_virtio_queue_ops {
	struct snap_virtio_ctrl_queue *(*create)(struct snap_virtio_ctrl *ctrl,
						 int index);
	void (*destroy)(struct snap_virtio_ctrl_queue *queue);
	void (*progress)(struct snap_virtio_ctrl_queue *queue);
	void (*start)(struct snap_virtio_ctrl_queue *queue);
	void (*suspend)(struct snap_virtio_ctrl_queue *queue);
	bool (*is_suspended)(struct snap_virtio_ctrl_queue *queue);
	int (*resume)(struct snap_virtio_ctrl_queue *queue);
	int (*get_state)(struct snap_virtio_ctrl_queue *queue,
			 struct snap_virtio_ctrl_queue_state *state);
	const struct snap_virtio_ctrl_queue_stats *
			(*get_io_stats)(struct snap_virtio_ctrl_queue *queue);
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
	size_t (*get_state_size)(struct snap_virtio_ctrl *ctrl);
	int (*get_state)(struct snap_virtio_ctrl *ctrl,
			 struct snap_virtio_device_attr *attr, void *buf,
			 size_t len);
	int (*set_state)(struct snap_virtio_ctrl *ctrl,
			 struct snap_virtio_device_attr *attr,
			 const struct snap_virtio_ctrl_queue_state *queue_state,
			 const void *buf, int len);
	void (*dump_state)(struct snap_virtio_ctrl *ctrl, const void *buf, int len);
	bool (*queue_attr_valid)(struct snap_virtio_device_attr *attr);
	int (*get_attr)(struct snap_virtio_ctrl *ctrl, struct snap_virtio_device_attr *attr);

};

struct snap_virtio_ctrl {
	enum snap_virtio_ctrl_type type;
	enum snap_virtio_ctrl_state state;
	pthread_mutex_t progress_lock;
	struct snap_device *sdev;
	size_t max_queues;
	size_t enabled_queues;
	struct snap_virtio_ctrl_queue **queues;
	struct snap_virtio_queue_ops *q_ops;
	void *cb_ctx; /* bar callback context */
	struct snap_virtio_ctrl_bar_cbs bar_cbs;
	struct snap_virtio_ctrl_bar_ops *bar_ops;
	struct snap_virtio_device_attr *bar_curr;
	struct snap_virtio_device_attr *bar_prev;
	struct ibv_pd *lb_pd;
	struct snap_pg_ctx pg_ctx;
	bool log_writes_to_host;
	struct snap_channel *lm_channel;
	/* true if reset was requested while some queues are not suspended */
	bool pending_reset;
	/* true if completion (commands handled by queues) should be sent in order */
	bool force_in_order;
	struct snap_device_attr sdev_attr;
	int lm_state;
};

bool snap_virtio_ctrl_is_stopped(struct snap_virtio_ctrl *ctrl);
int snap_virtio_ctrl_start(struct snap_virtio_ctrl *ctrl);
int snap_virtio_ctrl_stop(struct snap_virtio_ctrl *ctrl);

bool snap_virtio_ctrl_is_suspended(struct snap_virtio_ctrl *ctrl);
int snap_virtio_ctrl_suspend(struct snap_virtio_ctrl *ctrl);
int snap_virtio_ctrl_resume(struct snap_virtio_ctrl *ctrl);

bool snap_virtio_ctrl_critical_bar_change_detected(struct snap_virtio_ctrl *ctrl);
void snap_virtio_ctrl_progress(struct snap_virtio_ctrl *ctrl);
void snap_virtio_ctrl_progress_lock(struct snap_virtio_ctrl *ctrl);
void snap_virtio_ctrl_progress_unlock(struct snap_virtio_ctrl *ctrl);
void snap_virtio_ctrl_io_progress(struct snap_virtio_ctrl *ctrl);
void snap_virtio_ctrl_pg_io_progress(struct snap_virtio_ctrl *ctrl, int pg_id);
int snap_virtio_ctrl_open(struct snap_virtio_ctrl *ctrl,
			  struct snap_virtio_ctrl_bar_ops *bar_ops,
			  struct snap_virtio_queue_ops *q_ops,
			  struct snap_context *sctx,
			  const struct snap_virtio_ctrl_attr *attr);
void snap_virtio_ctrl_close(struct snap_virtio_ctrl *ctrl);

/* live migration support */

/**
 * Virtio Controller State
 *
 * The virtio controller state consists of pci_common, device and queue
 * configuration sections.
 *
 * Device configuration and part of the queue configuration are controller
 * specific and should be filled by the controller implementation.
 *
 * Controller implementation is also going to be responsible for the restoring
 * device specific state and queues.
 *
 * State format:
 * <global_hdr><section_hdr><section>...<section_hdr><section>
 *
 * Each header and section are in the little endian (x86) order.
 */

/**
 * struct snap_virtio_ctrl_section - state section header
 *
 * @len:   section length, including section header
 * @name:  symbolic section name
 */
struct snap_virtio_ctrl_section {
	uint16_t   len;
	char       name[16];
} __attribute__((packed));

/**
 * struct snap_virtio_ctrl_common_state - pci_common state
 *
 * The struct defines controller pci_common state as described
 * in the virtio spec.
 * NOTE: that device and driver features bits are expanded
 *
 * @ctlr_state:  this is an internal controller state. We keep it in order to
 *	       validate state restore operation.
 */
struct snap_virtio_ctrl_common_state {
	uint32_t device_feature_select;
	uint64_t device_feature;
	uint32_t driver_feature_select;
	uint64_t driver_feature;
	uint16_t msix_config;
	uint16_t num_queues;
	uint16_t queue_select;
	uint8_t device_status;
	uint8_t config_generation;

	enum snap_virtio_ctrl_state ctrl_state;
} __attribute__((packed));

/**
 * struct snap_virtio_ctrl_queue_state - queue state
 *
 * The struct defines controller queue state as described in the
 * virtio spec. In addition available and used indexes are saved.
 *
 * The queue state section consists of the array of queues, the
 * size of the array is &struct snap_virtio_ctrl_common_state.num_queues
 *
 * @hw_available_index:  queue available index as reported by the controller.
 *		       It is always less or equal to the driver available index
 *		       because some commands may not have been processed by
 *		       the controller.
 * @hw_used_index:       queue used index as reported by the controller.
 */
struct snap_virtio_ctrl_queue_state {
	uint16_t queue_size;
	uint16_t queue_msix_vector;
	uint16_t queue_enable;
	uint16_t queue_notify_off;
	uint64_t queue_desc;
	uint64_t queue_driver;
	uint64_t queue_device;

	uint16_t hw_available_index;
	uint16_t hw_used_index;
} __attribute__((packed));

int snap_virtio_ctrl_state_size(struct snap_virtio_ctrl *ctrl, size_t *common_cfg_len,
				size_t *queue_cfg_len, size_t *dev_cfg_len);
int snap_virtio_ctrl_state_save(struct snap_virtio_ctrl *ctrl, void *buf, size_t len);
int snap_virtio_ctrl_state_restore(struct snap_virtio_ctrl *ctrl,
				   const void *buf, size_t len);
void snap_virtio_ctrl_log_writes(struct snap_virtio_ctrl *ctrl, bool enable);

int snap_virtio_ctrl_lm_enable(struct snap_virtio_ctrl *ctrl, const char *name);
void snap_virtio_ctrl_lm_disable(struct snap_virtio_ctrl *ctrl);

int  snap_virtio_ctrl_recover(struct snap_virtio_ctrl *ctrl,
			      struct snap_virtio_device_attr *attr);
int snap_virtio_ctrl_should_recover(struct snap_virtio_ctrl *ctrl);

const struct snap_virtio_ctrl_queue_stats *
snap_virtio_ctrl_q_io_stats(struct snap_virtio_ctrl *ctrl, uint16_t q_idx);

#endif
