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

#ifndef SNAP_CHANNEL_H
#define SNAP_CHANNEL_H

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define snap_channel_error(_fmt, ...) \
	do { \
		fprintf(stderr, "%s:%d ERR " _fmt, __FILE__, __LINE__, ## __VA_ARGS__); \
		fflush(stderr); \
	 } while (0)
#define snap_channel_warn(_fmt, ...) \
	do { \
		fprintf(stdout, "%s:%d WARN " _fmt, __FILE__, __LINE__, ## __VA_ARGS__); \
		fflush(stdout); \
	 } while (0)
#define snap_channel_info(_fmt, ...) \
	do { \
		fprintf(stdout, "%s:%d INFO " _fmt, __FILE__, __LINE__, ## __VA_ARGS__); \
		fflush(stdout); \
	 } while (0)

/**
 * struct snap_migration_ops - completion handle and callback
 * for live migration support
 *
 * This structure should be allocated by the snap-controller and can be
 * passed to communication primitives.
 *
 * In order to fulfil the above requirements, each SNAP controller must
 * implement basic migration operations. Since these operations are generic,
 * it will be passed during the creation of the snap_device that will create
 * an internal communication channel. Using these callbacks, the internal
 * communication channel will be able to master the migration process for the
 * controller that is represented by this snap_device. By passing these
 * callbacks, each SNAP controller actually gives supervisor permissions to the
 * communication channel to change its operational states, save/restore the
 * internal controller state and start/stop dirty pages tracking.
 *
 * @quiesce: This operation will stop the device from issuing DMA requests.
 * Once successfully returned, the device is not allowed to initiate any
 * operation that might dirty new pages nor changing other devices state.
 * The device can still receive requests that might change its internal state.
 * @unquiesce: This operation is counterpart of the quiesce operation.
 * Once successfully returned, the device is allowed issuing DMA operations
 * and change other devices state.
 * @freeze: This operation will notify the device to stop reacting to incoming
 * requests.Once successfully returned, the device internal state is not
 * allowed to change until it is unfreezed again.
 * @unfreeze: This operation is counterpart of the freeze operation.
 * Once successfully returned, the device is allowed to react to incoming
 * requests that might change its internal state.
 * @get_state_size: Query for internal device state size. In case the
 * controller is unfreezed, device state can be changed. Controllers that
 * don't support tracking for state changes while running will return 0 for
 * this query unless device is freezed. For controllers that track internal
 * state while running, the implementation is controller specific.
 * The returned size, in bytes, will inform the migration SW on the upcoming
 * amount of data that should be copied from the controller.
 * @copy_state: This operation will be used to save and restore device state.
 * During "saving" procedure, the controller will copy the internal device
 * state to a given buffer. The migration SW will query the state size prior
 * running this operation during "saving" states.
 * During "restore" procedure, the migration SW will copy the migrated state to
 * the controller. In this stage, the controller must be freezed and quiesced.
 * @start_dirty_pages_track: This operation will be used to inform the
 * controller to start tracking and reporting dirty pages to the communication
 * channel. A controller can track dirty pages only while running. For live
 * migration, capable controllers should be able to start tracking dirty pages
 * during "Pre-copy" phase and stop tracking during "Stop-and-Copy" phase.
 * @stop_dirty_pages_track: This operation will be used to inform the
 * controller to stop tracking and reporting dirty pages. For live migration,
 * capable controllers should be able to start tracking dirty pages during
 * "Pre-copy" phase and stop tracking during "Stop-and-Copy" phase.
 *
 * @get_pci_bdf: This operation will be used to retrieve the PCI Bus/Device/Function
 *  presented to the hostfor the snap controller. In case of success,
 *  non 0 value will be returned. This value willrepresent the PCI BDF
 *  according to the commonly known structure of eight-bit PCI Bus,five-bit
 *  Device and three-bit Function. In case of an error or in case the host
 *  still didn'tenumerate the PCI device, 0 value will be returned.This
 *  knowledge might be useful for creating a discovery mechanism between
 *  migrationSW and SNAP channels.
 */
struct snap_migration_ops {
	int (*quiesce)(void *data);
	int (*unquiesce)(void *data);
	int (*freeze)(void *data);
	int (*unfreeze)(void *data);
	int (*get_state_size)(void *data);
	int (*copy_state)(void *data, void *buff, int len,
			  bool copy_from_buffer);
	int (*start_dirty_pages_track)(void *data);
	int (*stop_dirty_pages_track)(void *data);
	uint16_t (*get_pci_bdf)(void *data);
};

/**
 * struct snap_channel - internal struct holds the information of the
 *                       communication channel
 *
 * @ops: migration ops struct of functions that contains the basic migration
 *       operations (provided by the controller).
 * @channel: channel ops struct that is provided by the specific channel
 *           implementation
 * @data: controller_data that will be associated with the
 *        caller or application.
 * @dirty_pages: dirty pages struct, used to track dirty pages.
 */
struct snap_channel {
	const struct snap_migration_ops		*ops;
	const struct snap_channel_ops		*channel_ops;
	void					*data;
};

/* API that is used by the controller */
struct snap_channel *snap_channel_open(const char *name, struct snap_migration_ops *ops,
				       void *data);
void snap_channel_close(struct snap_channel *schannel);
int snap_channel_mark_dirty_page(struct snap_channel *schannel, uint64_t guest_pa,
				 int length);

/* API that is used by the channel provider */

/**
 * struct snap_channel_ops - holds specific channel implementation
 *
 * Migration channel implementation must provide following fields:
 *
 * @name: name of the migration channel implementation
 * @open: open migration channel
 * @close: close migration channel
 * @mark_dirty_page: mark dirty pages
 *
 * For example to create foo_channel one should do:
 * static const struct snap_channel_ops foo_ops = {
 *      .name = "foo_channel",
 *      .open = foo_channel_open,
 *      .close = foo_channel_close,
 *      .mark_dirty_page = foo_mark_dirty_page
 * };
 * SNAP_CHANNEL_DECLARE(foo_channel, foo_ops);
 */
struct snap_channel_ops {
	const char *name;
	struct snap_channel *(*open)(struct snap_migration_ops *ops, void *data);
	void (*close)(struct snap_channel *schannel);
	int (*mark_dirty_page)(struct snap_channel *schannel, uint64_t guest_pa,
			       int length);
};

void snap_channel_register(const struct snap_channel_ops *ops);
void snap_channel_unregister(const struct snap_channel_ops *ops);

/*
 * Convinience macro that creates a constructor function that will
 * register a snap channel
 */
#define SNAP_CHANNEL_DECLARE(channel_name, channel_ops)                        \
	extern const struct snap_channel_ops snap_channel_##channel_name       \
		__attribute__((alias(#channel_ops)));                          \
	static __attribute__((constructor)) void snap_channel_register_##channel_name(void) \
	{                                                                      \
		snap_channel_register(&channel_ops);                           \
	}

#endif
