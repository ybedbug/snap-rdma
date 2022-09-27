/*
 * Copyright © 2022 NVIDIA CORPORATION & AFFILIATES. ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of Nvidia Corporation and its affiliates
 * (the "Company") and all right, title, and interest in and to the software
 * product, including all associated intellectual property rights, are and
 * shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 */

#ifndef SNAP_VRDMA_CTRL_H
#define SNAP_VRDMA_CTRL_H

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include "snap_vrdma.h"
#include "../ctrl/snap_dp_map.h"
#include "../ctrl/snap_poll_groups.h"

enum snap_vrdma_ctrl_state {
	SNAP_VRDMA_CTRL_STOPPED,
	SNAP_VRDMA_CTRL_STARTED,
	SNAP_VRDMA_CTRL_SUSPENDED,
	SNAP_VRDMA_CTRL_SUSPENDING
};

/*
 * Device status field according to vrdma spec 
 *
 * The vrdma device state is discussed between device and driver
 * over the `device_status` PCI bar register, in a "bitmask" mode;
 * a.k.a multiple "statuses" can be configured simultaneously.
 *
 * Full description of statuses can be found on vrdma spec ducomentation.
 *
 * NOTE: RESET status is unique, as instead of raising a bit in register,
 *       driver *unsets* all bits on register.
 */
enum snap_vrdma_common_device_status {
	SNAP_VRDMA_DEVICE_S_RESET = 0,
	SNAP_VRDMA_DEVICE_S_ACKNOWLEDGE = 1 << 0,
	SNAP_VRDMA_DEVICE_S_DRIVER = 1 << 1,
	SNAP_VRDMA_DEVICE_S_DRIVER_OK = 1 << 2,
	SNAP_VRDMA_DEVICE_S_FEATURES_OK = 1 << 3,
	SNAP_VRDMA_DEVICE_S_DEVICE_NEEDS_RESET = 1 << 6,
	SNAP_VRDMA_DEVICE_S_FAILED = 1 << 7,
};

struct snap_vrdma_ctrl_bar_cbs {
	int (*validate)(void *cb_ctx);
	int (*start)(void *cb_ctx);
	int (*stop)(void *cb_ctx);
	int (*pre_flr)(void *cb_ctx);
	int (*post_flr)(void *cb_ctx);
};

struct snap_vrdma_ctrl {
	enum snap_vrdma_ctrl_state state;
	pthread_mutex_t progress_lock;
	struct snap_device *sdev;
	void *cb_ctx; /* bar callback context */
	struct snap_vrdma_ctrl_bar_cbs bar_cbs;
	struct snap_vrdma_device_attr *bar_curr;
	struct snap_vrdma_device_attr *bar_prev;
	struct ibv_pd *lb_pd;
	struct snap_pg_ctx pg_ctx;
	bool log_writes_to_host;
	/* true if reset was requested while some queues are not suspended */
	bool pending_reset;
	/* true if completion (commands handled by queues) should be sent in order */
	bool force_in_order;
	/* true if FLR was requested */
	bool pending_flr;
	struct snap_device_attr sdev_attr;
	struct snap_cross_mkey *xmkey;
	bool is_quiesce;
	/* true if ctrl resume was requested while ctrl was still suspending */
	bool pending_resume;
	struct snap_dp_bmap *dp_map;
	struct snap_cross_mkey *pf_xmkey;
};

struct snap_vrdma_ctrl_attr {
	struct ibv_context *context;
	enum snap_pci_type pci_type;
	int pf_id;
	bool event;
	void *cb_ctx;
	struct snap_vrdma_ctrl_bar_cbs *bar_cbs;
	struct ibv_pd *pd;
	uint32_t npgs;
	bool force_in_order;
	bool suspended;
	bool recover;
	bool force_recover;
};

int snap_vrdma_ctrl_start(struct snap_vrdma_ctrl *ctrl);
int snap_vrdma_ctrl_stop(struct snap_vrdma_ctrl *ctrl);
struct snap_vrdma_ctrl*
snap_vrdma_ctrl_open(struct snap_context *sctx,
			  struct snap_vrdma_ctrl_attr *attr);
void snap_vrdma_ctrl_close(struct snap_vrdma_ctrl *ctrl);
void snap_vrdma_ctrl_progress(struct snap_vrdma_ctrl *ctrl);
#endif
