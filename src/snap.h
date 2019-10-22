/*
 * Copyright (c) 2012 Mellanox Technologies, Inc.  All rights reserved.
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

#ifndef SNAP_H
#define SNAP_H

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <pthread.h>

#include <infiniband/verbs.h>

#include "queue.h"

#define PFX "snap: "

#ifndef offsetof
#define offsetof(t, m) ((size_t) &((t *)0)->m)
#endif

#ifndef container_of
#define container_of(ptr, type, member) ({ \
		typeof(((type *)0)->member)*__mptr = (ptr); \
		(type *)((char *)__mptr - offsetof(type, member)); })
#endif

enum snap_device_type {
	SNAP_NVME_PF_DEV	= 1 << 0,
	SNAP_NVME_VF_DEV	= 1 << 1,
	SNAP_VIRTIO_BLK_PF_DEV	= 1 << 2,
	SNAP_VIRTIO_BLK_VF_DEV	= 1 << 3,
};

struct snap_driver;
struct snap_context;

struct snap_device_attr {
	enum snap_device_type	type;
	int			pf_id;
	int			vf_id;
};

struct snap_device {
	struct snap_context		*sctx;
	enum snap_device_type		type;
};

struct snap_context {
	struct ibv_context		*context;
	struct snap_driver		*driver;
};

typedef bool (*snap_is_capable)(struct ibv_device *ibdev);
typedef struct snap_context *(*snap_driver_create_ctx)(struct ibv_device *ibdev);
typedef void (*snap_driver_destroy_ctx)(struct snap_context *sctx);

typedef struct snap_device *(*snap_driver_open_dev)(struct snap_context *sctx,
					    struct snap_device_attr *attr);
typedef void (*snap_driver_close_dev)(struct snap_device *sdev);

void snap_unregister_driver(struct snap_driver *driver);
void snap_register_driver(struct snap_driver *driver);

void snap_close_device(struct snap_device *sdev);
struct snap_device *snap_open_device(struct snap_context *sctx,
		struct snap_device_attr *attr);
bool snap_is_capable_device(struct ibv_device *ibdev);

struct snap_context *snap_create_context(struct ibv_device *ibdev);
void snap_destroy_context(struct snap_context *sctx);

struct snap_driver {
	const char			*name;
	void				*dlhandle;
	TAILQ_ENTRY(snap_driver)	entry;

	snap_driver_create_ctx		create;
	snap_driver_destroy_ctx		destroy;
	snap_driver_open_dev		open;
	snap_driver_close_dev		close;
	snap_is_capable			is_capable;
};

int snap_open();
void snap_close();

#endif
