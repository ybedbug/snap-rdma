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

#ifndef SNAP_NVME_CTRL_H
#define SNAP_NVME_CTRL_H

#include <stdlib.h>
#include <linux/types.h>

#include "snap_nvme.h"
#include "nvme_proto.h"
#include "nvme_register.h"

/* 100 msec between bar updates */
#define SNAP_NVME_BAR_CB_INTERVAL (100000 / CLOCKS_PER_SEC)

enum snap_nvme_ctrl_type {
	SNAP_NVME_CTRL_PF,
	SNAP_NVME_CTRL_VF,
};

struct snap_nvme_ctrl_attr {
	enum snap_nvme_ctrl_type	type;
	int				pf_id;
	int				vf_id;
};

struct snap_nvme_ctrl {
	struct snap_context		*sctx;
	struct snap_device		*sdev;
	struct snap_nvme_device		*ndev;

	bool				reset_device;
	bool				curr_enabled;
	bool				prev_enabled;

	clock_t				last_bar_cb;
	struct nvme_bar			bar;

};

struct snap_nvme_ctrl*
snap_nvme_ctrl_open(struct snap_context *sctx,
		    struct snap_nvme_ctrl_attr *attr);
void snap_nvme_ctrl_close(struct snap_nvme_ctrl *ctrl);
void snap_nvme_ctrl_progress(struct snap_nvme_ctrl *ctrl);

#endif
