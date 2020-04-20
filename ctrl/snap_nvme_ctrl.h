/*
 * Copyright (c) 2020 Mellanox Technologies, Inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef SNAP_NVME_CTRL_H
#define SNAP_NVME_CTRL_H

#include <stdlib.h>
#include <linux/types.h>

#include "snap_nvme.h"
#include "nvme_proto.h"

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
};

struct snap_nvme_ctrl*
snap_nvme_ctrl_open(struct snap_context *sctx,
		    struct snap_nvme_ctrl_attr *attr);
void snap_nvme_ctrl_close(struct snap_nvme_ctrl *ctrl);

#endif
