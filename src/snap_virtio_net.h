/*
 * Copyright (c) 2019 Mellanox Technologies, Inc.  All rights reserved.
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

#ifndef SNAP_VIRTIO_NET_H
#define SNAP_VIRTIO_NET_H

#include "snap.h"
#include "snap_virtio_common.h"

struct snap_virtio_net_device;

struct snap_virtio_net_queue_attr {
	uint32_t			tisn_or_qpn;

	struct snap_virtio_queue_attr   vattr;
};

struct snap_virtio_net_queue {
	struct snap_virtio_queue	virtq;

	struct snap_virtio_net_device	*vndev;
};

struct snap_virtio_net_device_attr {
	struct snap_virtio_device_attr		vattr;
	struct snap_virtio_net_queue_attr	*q_attrs;
	unsigned int				queues;
};

struct snap_virtio_net_device {
	struct snap_virtio_device		vdev;
	struct snap_virtio_net_queue		*virtqs;
};

int snap_virtio_net_init_device(struct snap_device *sdev);
int snap_virtio_net_teardown_device(struct snap_device *sdev);
int snap_virtio_net_query_device(struct snap_device *sdev,
	struct snap_virtio_net_device_attr *attr);
struct snap_virtio_net_queue*
snap_virtio_net_create_queue(struct snap_device *sdev,
	struct snap_virtio_net_queue_attr *attr);
int snap_virtio_net_destroy_queue(struct snap_virtio_net_queue *vnq);

#endif
