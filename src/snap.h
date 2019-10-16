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

struct snap_driver;

struct snap_device {
	struct ibv_context *context;
	struct snap_driver *driver;
};

typedef struct snap_device *(*snap_driver_open)(struct ibv_device *ibdev);
typedef void (*snap_driver_close)(struct snap_device *sdev);

void snap_unregister_driver(struct snap_driver *driver);
void snap_register_driver(struct snap_driver *driver);

void snap_close_device(struct snap_device *sdev);
struct snap_device *snap_open_device(struct ibv_device *ibdev);

struct snap_driver {
	const char			*name;
	void				*dlhandle;
	TAILQ_ENTRY(snap_driver)	entry;

	snap_driver_open		open;
	snap_driver_close		close;
};

int snap_open();
void snap_close();

#endif
