/*
 * Copyright © 202 NVIDIA CORPORATION & AFFILIATES. ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of Nvidia Corporation and its affiliates
 * (the "Company") and all right, title, and interest in and to the software
 * product, including all associated intellectual property rights, are and
 * shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 */

#ifndef SNAP_VIRTIO_STATE_H
#define SNAP_VIRTIO_STATE_H
#include <stdint.h>
#include <stdbool.h>

enum virtio_state_dev_field_type {
	VIRTIO_DEV_PCI_COMMON_CFG = 0,  /*  struct virtio_dev_common_cfg */
	VIRTIO_DEV_CFG_SPACE,   /* config space fields, for net  struct virtio_net_config etc */
	VIRTIO_DEV_QUEUE_CFG,   /* struct virtio_dev_q_cfg */

	/*optional start here*/
	VIRTIO_DEV_SPLIT_Q_RUN_STATE, /*virtio_dev_split_q_run_state*/
	VIRTIO_NET_RX_MODE_CFG,
	VIRTIO_NET_VLAN_CFG,
	VIRTIO_NET_MAC_UNICAST_CFG,
	VIRTIO_NET_MAC_MULTICAST_CFG,
};

struct virtio_state_field {
	__le32 type;
	__le32 size; /* size of the data field */
	uint8_t data[];
} __attribute__((packed));

struct virtio_state {
	__le32 virtio_field_count;    /* num of tlv */
	struct virtio_state_field fields[];
} __attribute__((packed));

static inline struct virtio_state_field *virtio_state_fld_first(struct virtio_state *hdr)
{
	return &hdr->fields[0];
}

static inline struct virtio_state_field *virtio_state_fld_next(struct virtio_state_field *fld)
{
	return (struct virtio_state_field *)(fld->data + fld->size);
}

struct virtio_state_pci_common_cfg {
	__le32 device_feature_select;
	__le64 device_feature;
	__le32 driver_feature_select;
	__le64 driver_feature;
	__le16 msix_config;
	__le16 num_queues;
	__le16 queue_select;
	uint8_t device_status;
	uint8_t config_generation;
} __attribute__((packed));

struct virtio_state_q_cfg {
	__le16 queue_index;  /* queue number whose config is contained below */
	__le16 queue_size;
	__le16 queue_msix_vector;
	__le16 queue_enable;
	__le16 queue_notify_off;
	__le64 queue_desc;
	__le64 queue_driver;
	__le64 queue_device;
	__le16 queue_notify_data;
	__le16 queue_reset;
} __attribute__((packed));

struct virtio_split_q_run_state {
	__le16 queue_index;
	__le16 last_avail_idx;
	__le16 last_used_idx;
} __attribute__((packed));

struct virtio_state_blk_config {
	__le64 capacity;
	__le32 size_max;
	__le32 seg_max;

	struct virtio_state_blk_geometry {
		__le16 cylinders;
		uint8_t heads;
		uint8_t sectors;
	} geometry;

	__le32 blk_size;

	struct virtio_blk_topology {
		// # of logical blocks per physical block (log2)
		uint8_t physical_block_exp;
		// offset of first aligned logical block
		uint8_t alignment_offset;
		// suggested minimum I/O size in blocks
		__le16 min_io_size;
		// optimal (suggested maximum) I/O size in blocks
		__le32 opt_io_size;
	} topology;

	uint8_t writeback;
	uint8_t unused0;
	__le16 num_queues;
	__le32 max_discard_sectors;
	__le32 max_discard_seg;
	__le32 discard_sector_alignment;
	__le32 max_write_zeroes_sectors;
	__le32 max_write_zeroes_seg;
	uint8_t write_zeroes_may_unmap;
	uint8_t unused1[3];
	__le32 max_secure_erase_sectors;
	__le32 max_secure_erase_seg;
	__le32 secure_erase_sector_alignment;
} __attribute__((packed));

#endif
