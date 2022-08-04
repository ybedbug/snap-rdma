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

#ifndef SNAP_VIRTIO_ADM_SPEC_H
#define SNAP_VIRTIO_ADM_SPEC_H
#include <stdint.h>
#include "snap_macros.h"

#ifndef VIRTIO_F_ADMIN_VQ
#define VIRTIO_F_ADMIN_VQ 41
#endif

#ifndef VIRTIO_F_ADMIN_MIGRATION
#define VIRTIO_F_ADMIN_MIGRATION 44
#endif

#ifndef VIRTIO_F_ADMIN_MIGRATION_DYNAMIC_INTERNAL_STATE_TRACK
#define VIRTIO_F_ADMIN_MIGRATION_DYNAMIC_INTERNAL_STATE_TRACK 45
#endif

#ifndef VIRTIO_F_ADMIN_DIRTY_PAGE_PUSH_BITMAP_TRACK
#define VIRTIO_F_ADMIN_DIRTY_PAGE_PUSH_BITMAP_TRACK 46
#endif

#ifndef VIRTIO_F_ADMIN_DIRTY_PAGE_PUSH_BYTEMAP_TRACK
#define VIRTIO_F_ADMIN_DIRTY_PAGE_PUSH_BYTEMAP_TRACK 47
#endif

#ifndef VIRTIO_F_ADMIN_DIRTY_PAGE_PULL_BITMAP_TRACK
#define VIRTIO_F_ADMIN_DIRTY_PAGE_PULL_BITMAP_TRACK 48
#endif

#ifndef VIRTIO_F_ADMIN_DIRTY_PAGE_PULL_BYTEMAP_TRACK
#define VIRTIO_F_ADMIN_DIRTY_PAGE_PULL_BYTEMAP_TRACK 49
#endif

#define SNAP_VQ_ADM_MIG_CTRL 64
#define SNAP_VQ_ADM_MIG_IDENTITY 0
#define SNAP_VQ_ADM_MIG_GET_STATUS 1
#define SNAP_VQ_ADM_MIG_MODIFY_STATUS 2
#define SNAP_VQ_ADM_MIG_GET_STATE_PENDING_BYTES 3
#define SNAP_VQ_ADM_MIG_SAVE_STATE 4
#define SNAP_VQ_ADM_MIG_RESTORE_STATE 5

#define SNAP_VQ_ADM_DP_TRACK_CTRL 65
#define SNAP_VQ_ADM_DP_IDENTITY 0
#define SNAP_VQ_ADM_DP_START_TRACK 1
#define SNAP_VQ_ADM_DP_STOP_TRACK 2
#define SNAP_VQ_ADM_DP_GET_MAP_PENDING_BYTES 3
#define SNAP_VQ_ADM_DP_REPORT_MAP 4

enum snap_virtio_adm_status {
	SNAP_VIRTIO_ADM_STATUS_OK = 0,
	SNAP_VIRTIO_ADM_STATUS_ERR = 1,
	SNAP_VIRTIO_ADM_STATUS_INVALID_CLASS = 2,
	SNAP_VIRTIO_ADM_STATUS_INVALID_COMMAND = 3,
	SNAP_VIRTIO_ADM_STATUS_DATA_TRANSFER_ERR = 4,
	SNAP_VIRTIO_ADM_STATUS_DEVICE_INTERNAL_ERR = 5,
};

struct snap_virtio_adm_cmd_hdr {
	uint8_t cmd_class;
	uint8_t command;
} SNAP_PACKED;

struct snap_virtio_adm_cmd_ftr {
	uint8_t status;
} SNAP_PACKED;

struct snap_vq_adm_get_pending_bytes_data {
	__le16 vdev_id;
	__le16 reserved;
};

struct snap_vq_adm_get_pending_bytes_result {
	__le64 pending_bytes;
};

struct snap_vq_adm_modify_status_data {
	__le16 vdev_id;
	__le16 internal_status;
};

struct snap_vq_adm_save_state_data {
	__le16 vdev_id;
	__le16 reserved[3];
	__le64 offset;
	__le64 length; /* Num of data bytes to be returned by the device */
};

struct snap_vq_adm_restore_state_data {
	__le16 vdev_id;
	__le16 reserved;
	__le64 offset;
	__le64 length; /* Num of data bytes to be consumed by the device */
};

struct snap_vq_adm_get_status_data {
	__le16 vdev_id;
	__le16 reserved;
};

struct snap_vq_adm_get_status_result {
	__le16 internal_status; /* Value from enum snap_virtio_internal_status */
	__le16 reserved;
};

/* dirty page tracking */
struct virtio_admin_dirty_page_identity_result {
	__le16 log_max_pages_track_pull_bitmap_mode; /* Per managed device (log) */
	__le16 log_max_pages_track_pull_bytemap_mode; /* Per managed device (log) */
	__le32 max_track_ranges; /* Maximum number of ranges a device can track */
};

enum snap_vq_adm_dirty_page_track_mode {
	VIRTIO_M_DIRTY_TRACK_PUSH_BITMAP = 1, /* Use push mode with bit granularity */
	VIRTIO_M_DIRTY_TRACK_PUSH_BYTEMAP = 2, /* Use push mode with byte granularity */
	VIRTIO_M_DIRTY_TRACK_PULL_BITMAP = 3, /* Use pull mode with bit granularity */
	VIRTIO_M_DIRTY_TRACK_PULL_BYTEMAP = 4, /* Use pull mode with byte granularity */

	/* experimental, non standard */
	VIRTIO_M_DIRTY_TRACK_PULL_PAGELIST = 0xF001 /* report pages as a raw pagelist */
};

struct snap_vq_adm_sge {
	__le64 addr;
	__le32 len;
	__le32 reserved;
};

struct snap_vq_adm_dirty_page_track_start {
	__le16 vdev_id;
	__le16 track_mode;
	__le32 vdev_host_page_size;
	__le64 vdev_host_range_addr;
	__le64 range_length;
};

struct snap_vq_adm_dirty_page_track_stop {
	__le16 vdev_id;
	__le16 reserved[3];
	__le64 vdev_host_range_addr;
};

union snap_virtio_adm_cmd_in {
	struct snap_vq_adm_get_pending_bytes_data pending_bytes_data;
	struct snap_vq_adm_modify_status_data modify_status_data;
	struct snap_vq_adm_save_state_data save_state_data;
	struct snap_vq_adm_get_status_data get_status_data;
	struct snap_vq_adm_restore_state_data restore_state_data;
	struct snap_vq_adm_dirty_page_track_start dp_track_start_data;
	struct snap_vq_adm_dirty_page_track_stop dp_track_stop_data;
	__le16 vdev_id;
};

union snap_virtio_adm_cmd_out {
	struct snap_vq_adm_get_pending_bytes_result pending_bytes_res;
	struct snap_vq_adm_get_status_result get_status_res;
};

struct snap_virtio_adm_cmd_layout {
	struct snap_virtio_adm_cmd_hdr hdr;
	union snap_virtio_adm_cmd_in in;
	/* Additional data defined by variadic cmd_in structures */
	union snap_virtio_adm_cmd_out out;
	/* Additional data defined by variadic cmd_out structures */
	struct snap_virtio_adm_cmd_ftr ftr;
};

#endif
