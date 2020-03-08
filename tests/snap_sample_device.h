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

#ifndef SNAP_SAMPLE_DEVICE_H
#define SNAP_SAMPLE_DEVICE_H

#include <stdint.h>

/*
 * This is an example of a controller implementation for
 * a simple emulated device.
 *
 * The device has a bar with registers, one queue to receive commands and one
 * queue to send completions.
 */

#define SNAP_SAMPLE_DEVICE_PF 0

/*
 * Use subset of NVMe registers to manage our device.
 * Note that the controller can only write to CSTS register. Changes
 * to any other register will be masked out by the hardware.
 */

/* command, read only by controller, used by the device driver to pass
 * commands */
#define SNAP_SAMPLE_DEV_REG_CMD    0x14
/* command status, read-write by controller, read only by the driver */
#define SNAP_SAMPLE_DEV_REG_CST    0x1C

/* Base phys addresses of the queues, read only by the device */
#define SNAP_SAMPLE_DEV_REG_SQPA   0x28
#define SNAP_SAMPLE_DEV_REG_CQPA   0x30

/* Doorbells for the submission and completion queues */
#define SNAP_SAMPLE_DEV_DB_BASE     0x1000
#define SNAP_SAMPLE_DEV_CQ_DB_OFFSET   0x4
#define SNAP_SAMPLE_DEV_SQ_DB_OFFSET   0x0

#define SNAP_SAMPLE_DEV_QUEUE_DEPTH    64

/* Access to the device BAR is not synchronized. It means that the
 * controller can only detect changes to the bar.
 * To accomodate this the command looks like:
 * <cookie><cmd> where coockie should be different between commands.
 *
 * Our commands protocol looks like this
 * Driver: write cmd (<coockie><cmd>) to REG_CMD
 * Driver: polls on REG_CST for completion or timeout
 * Controller: REG_CMD value changes. Do command.
 * Controller: write status (<cookie><status>) to REG_CST
 *             the cookie is the same as in the command
 *
 * We assume that the controller is starting before the driver.
 * Different corner cases are not handled in order to keep the controller
 * simple.
 */

/* Commands, 16 bit for coockie and 16 for command */
#define SNAP_SAMPLE_DEV_CMD_MASK   0xFFFF
#define SNAP_SAMPLE_DEV_CMD_SHIFT      16

/* Command timeout in milliseconds */
#define SNAP_SAMPLE_DEV_CMD_TIMEOUT  10000

/* max possible data transfer length per DMA operation */
#define SNAP_SAMPLE_DEV_MAX_DATA_SIZE 8192

enum snap_sample_device_cmds {
	SNAP_SAMPLE_DEV_CMD_START = 0x1,
	SNAP_SAMPLE_DEV_CMD_STOP = 0x2,
	SNAP_SAMPLE_DEV_CMD_RUN_PING_TEST = 0x3
};

/*
 * Submission queue entry
 */
struct snap_sample_device_sqe {
	uint32_t sn;  /* submission serial number, starts from 0 */
	uint32_t len; /* how much data we want to transfer */
	uint64_t paddr; /* phys address of the host buffer */
	uint32_t dwords[12];
};

/*
 * Completion queue entry.
 * Instead of the NVMe phase bit we use a continuosly increasing
 * 32 bit serial number
 */
struct snap_sample_device_cqe {
	uint32_t    sn;  /* completion serial number, starts from 0 */
	uint32_t    dwords[15];
};

/* take log2 of the power of 2 value */
#define SNAP_SAMPLE_DEVICE_CQE_LOG \
	__builtin_ctz(sizeof(struct snap_sample_device_cqe))

#endif
