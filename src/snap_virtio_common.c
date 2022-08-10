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
#include <stdint.h>

#include "snap_macros.h"
#include "mlx5_ifc.h"
#include "snap_env.h"
#include "snap_mr.h"
#include "snap_dma.h"

#include "snap_virtio_net.h"
#include "snap_virtio_blk.h"
#include "snap_virtio_fs.h"
#include "snap_virtio_common.h"
#include "snap_sw_virtio_blk.h"
#include "snap_dpa_virtq.h"

#define SNAP_QUEUE_PROVIDER   "SNAP_QUEUE_PROVIDER"
SNAP_ENV_REG_ENV_VARIABLE(SNAP_QUEUE_PROVIDER, 0);

static int snap_virtio_init_virtq_umem(struct ibv_context *context,
					struct snap_virtio_caps *virtio,
					struct snap_virtio_queue *virtq,
					int depth);
static void snap_virtio_teardown_virtq_umem(struct snap_virtio_queue *virtq);

void snap_virtio_get_queue_attr(struct snap_virtio_queue_attr *vattr,
		void *q_configuration)
{
	vattr->size = DEVX_GET(virtio_q_layout, q_configuration, queue_size);
	vattr->msix_vector = DEVX_GET(virtio_q_layout, q_configuration,
				      queue_msix_vector);
	vattr->enable = DEVX_GET(virtio_q_layout, q_configuration,
				 queue_enable);
	vattr->notify_off = DEVX_GET(virtio_q_layout, q_configuration,
				     queue_notify_off);
	vattr->desc = DEVX_GET64(virtio_q_layout, q_configuration,
				 queue_desc);
	vattr->driver = DEVX_GET64(virtio_q_layout, q_configuration,
				   queue_driver);
	vattr->device = DEVX_GET64(virtio_q_layout, q_configuration,
				   queue_device);
}

void snap_virtio_get_queue_attr_v2(struct snap_virtio_queue_attr *vattr,
				   void *q_configuration_v2)
{
	vattr->reset = DEVX_GET(virtio_q_layout_v2, q_configuration_v2, queue_reset);
	snap_virtio_get_queue_attr(vattr, DEVX_ADDR_OF(virtio_q_layout_v2,
						       q_configuration_v2,
						       queue_configuration));
}

void snap_virtio_get_device_attr(struct snap_device *sdev,
				 struct snap_virtio_device_attr *vattr,
				 void *device_configuration)
{
	vattr->device_feature = DEVX_GET64(virtio_device, device_configuration,
					   device_feature);
	vattr->driver_feature = DEVX_GET64(virtio_device, device_configuration,
					   driver_feature);
	vattr->msix_config = DEVX_GET(virtio_device, device_configuration,
				      msix_config);
	vattr->max_queues = DEVX_GET(virtio_device, device_configuration,
				     num_queues);
	vattr->max_queue_size = DEVX_GET(virtio_device, device_configuration,
					 max_queue_size);
	vattr->pci_bdf = DEVX_GET(virtio_device, device_configuration,
				  pci_bdf);
	vattr->status = DEVX_GET(virtio_device, device_configuration,
				 device_status);
	vattr->config_generation = DEVX_GET(virtio_device, device_configuration,
					    config_generation);
	vattr->device_feature_select = DEVX_GET(virtio_device, device_configuration,
						device_feature_select);
	vattr->driver_feature_select = DEVX_GET(virtio_device, device_configuration,
						driver_feature_select);
	vattr->queue_select = DEVX_GET(virtio_device, device_configuration,
				       queue_select);
}

int snap_virtio_query_device(struct snap_device *sdev,
	enum snap_emulation_type type, uint8_t *out, int outlen)
{
	uint8_t in_blk[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		DEVX_ST_SZ_BYTES(virtio_blk_device_emulation)] = {0};
	uint8_t in_net[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		DEVX_ST_SZ_BYTES(virtio_net_device_emulation)] = {0};
	uint8_t in_fs[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		DEVX_ST_SZ_BYTES(virtio_fs_device_emulation)] = {0};
	uint8_t *in, *device_emulation_in;
	int inlen;

	if (type == SNAP_VIRTIO_BLK &&
	    (sdev->pci->type != SNAP_VIRTIO_BLK_PF &&
	     sdev->pci->type != SNAP_VIRTIO_BLK_VF))
		return -EINVAL;
	else if (type == SNAP_VIRTIO_NET &&
		 (sdev->pci->type != SNAP_VIRTIO_NET_PF &&
		  sdev->pci->type != SNAP_VIRTIO_NET_VF))
		return -EINVAL;
	else if (type == SNAP_VIRTIO_FS &&
	    (sdev->pci->type != SNAP_VIRTIO_FS_PF &&
	     sdev->pci->type != SNAP_VIRTIO_FS_VF))
		return -EINVAL;
	else if (type == SNAP_NVME)
		return -EINVAL;

	if (type == SNAP_VIRTIO_BLK) {
		in = in_blk;
		inlen = sizeof(in_blk);
		device_emulation_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);

		DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
			 MLX5_OBJ_TYPE_VIRTIO_BLK_DEVICE_EMULATION);
		DEVX_SET(virtio_blk_device_emulation, device_emulation_in, vhca_id,
			 sdev->pci->mpci.vhca_id);
	} else if (type == SNAP_VIRTIO_NET) {
		in = in_net;
		inlen = sizeof(in_net);
		device_emulation_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);

		DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
			 MLX5_OBJ_TYPE_VIRTIO_NET_DEVICE_EMULATION);
		DEVX_SET(virtio_net_device_emulation, device_emulation_in, vhca_id,
			 sdev->pci->mpci.vhca_id);
	} else {
		in = in_fs;
		inlen = sizeof(in_fs);
		device_emulation_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);

		DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
			 MLX5_OBJ_TYPE_VIRTIO_FS_DEVICE_EMULATION);
		DEVX_SET(virtio_fs_device_emulation, device_emulation_in, vhca_id,
			 sdev->pci->mpci.vhca_id);
	}

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_QUERY_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_id,
		 sdev->mdev.device_emulation->obj_id);

	return mlx5dv_devx_obj_query(sdev->mdev.device_emulation->obj, in,
				     inlen, out, outlen);
}

static void snap_virtio_net_modify_queues(void *in, struct snap_virtio_device_attr *attr)
{
	struct snap_virtio_net_device_attr *nattr = to_net_device_attr(attr);
	int i;
	void *q;

	snap_info("modify queues, %d queues\n", attr->max_queues);
	for (i = 0; i < attr->max_queues; i++) {
		snap_debug("size: %u msix %u enable %u notify %u desc 0x%lx avail 0x%lx used 0x%lx\n",
			   nattr->q_attrs[i].vattr.size,
			   nattr->q_attrs[i].vattr.msix_vector,
			   nattr->q_attrs[i].vattr.enable,
			   nattr->q_attrs[i].vattr.notify_off,
			   nattr->q_attrs[i].vattr.desc,
			   nattr->q_attrs[i].vattr.driver,
			   nattr->q_attrs[i].vattr.device);
		q = DEVX_ADDR_OF(virtio_net_device_emulation, in, virtio_q_configuration[i]);

		snap_debug("offset %ld\n", q - in);
		DEVX_SET(virtio_q_layout, q, queue_size,
			 nattr->q_attrs[i].vattr.size);
		DEVX_SET(virtio_q_layout, q, queue_msix_vector,
			 nattr->q_attrs[i].vattr.msix_vector);
		DEVX_SET(virtio_q_layout, q, queue_enable,
			 nattr->q_attrs[i].vattr.enable);
		DEVX_SET(virtio_q_layout, q, queue_notify_off,
			 nattr->q_attrs[i].vattr.notify_off);

		DEVX_SET64(virtio_q_layout, q, queue_desc,
			   nattr->q_attrs[i].vattr.desc);
		DEVX_SET64(virtio_q_layout, q, queue_driver,
			   nattr->q_attrs[i].vattr.driver);
		DEVX_SET64(virtio_q_layout, q, queue_device,
			   nattr->q_attrs[i].vattr.device);
	}
}

static void
snap_virtio_net_queue_config_fill(void *in, uint64_t mask,
				  struct snap_virtio_device_attr *attr, int i,
				  uint64_t *fields_to_modify)
{
	struct snap_virtio_net_device_attr *nattr = to_net_device_attr(attr);
	void *q;

	q = DEVX_ADDR_OF(virtio_q_layout_v2, in, queue_configuration);

	snap_debug("offset %ld\n", q - in);
	if (mask & SNAP_VIRTIO_MOD_VQ_CFG_Q_SIZE) {
		*fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_VQ_CFG_Q_SIZE;
		DEVX_SET(virtio_q_layout, q, queue_size,
			 nattr->q_attrs[i].vattr.size);
	}
	if (mask & SNAP_VIRTIO_MOD_VQ_CFG_Q_MSIX_VECTOR) {
		*fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_VQ_CFG_Q_MSIX_VECTOR;
		DEVX_SET(virtio_q_layout, q, queue_msix_vector,
			 nattr->q_attrs[i].vattr.msix_vector);
	}
	if (mask & SNAP_VIRTIO_MOD_VQ_CFG_Q_ENABLE) {
		*fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_VQ_CFG_Q_ENABLE;
		DEVX_SET(virtio_q_layout, q, queue_enable,
			 nattr->q_attrs[i].vattr.enable);
	}
	if (mask & SNAP_VIRTIO_MOD_VQ_CFG_Q_NOTIFY_OFF) {
		*fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_VQ_CFG_Q_NOTIFY_OFF;
		DEVX_SET(virtio_q_layout, q, queue_notify_off,
			 nattr->q_attrs[i].vattr.notify_off);
	}

	if (mask & SNAP_VIRTIO_MOD_VQ_CFG_Q_DESC) {
		*fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_VQ_CFG_Q_DESC;
		DEVX_SET64(virtio_q_layout, q, queue_desc,
			   nattr->q_attrs[i].vattr.desc);
	}
	if (mask & SNAP_VIRTIO_MOD_VQ_CFG_Q_DRIVER) {
		*fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_VQ_CFG_Q_DRIVER;
		DEVX_SET64(virtio_q_layout, q, queue_driver,
			   nattr->q_attrs[i].vattr.driver);
	}
	if (mask & SNAP_VIRTIO_MOD_VQ_CFG_Q_DEVICE) {
		*fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_VQ_CFG_Q_DEVICE;
		DEVX_SET64(virtio_q_layout, q, queue_device,
			   nattr->q_attrs[i].vattr.device);
	}
	if (mask & SNAP_VIRTIO_MOD_VQ_CFG_Q_RESET) {
		*fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_VQ_CFG_Q_RESET;
		DEVX_SET(virtio_q_layout_v2, q, queue_reset,
			 nattr->q_attrs[i].vattr.reset);
	}
	if (mask & SNAP_VIRTIO_MOD_QUEUE_CFG) {
		*fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_QUEUE_CFG;
		DEVX_SET(virtio_q_layout, q, queue_size,
			 nattr->q_attrs[i].vattr.size);
		DEVX_SET(virtio_q_layout, q, queue_msix_vector,
			 nattr->q_attrs[i].vattr.msix_vector);
		DEVX_SET(virtio_q_layout, q, queue_enable,
			 nattr->q_attrs[i].vattr.enable);
		DEVX_SET(virtio_q_layout, q, queue_notify_off,
			 nattr->q_attrs[i].vattr.notify_off);
		DEVX_SET64(virtio_q_layout, q, queue_desc,
			   nattr->q_attrs[i].vattr.desc);
		DEVX_SET64(virtio_q_layout, q, queue_driver,
			   nattr->q_attrs[i].vattr.driver);
		DEVX_SET64(virtio_q_layout, q, queue_device,
			   nattr->q_attrs[i].vattr.device);
		DEVX_SET(virtio_q_layout_v2, q, queue_reset,
			 nattr->q_attrs[i].vattr.reset);
	}
}

static int
snap_virtio_net_queue_configs_v2_fill(void *in, uint64_t mask,
				      struct snap_virtio_device_attr *attr,
				      uint64_t *fields_to_modify)
{
	struct snap_virtio_net_device_attr *nattr = to_net_device_attr(attr);
	int i, idx = 0;
	void *q;

	snap_debug("modify queues v2\n");
	for (i = 0; i < attr->max_queues; i++) {
		snap_debug("q: %u size: %u msix %u enable %u notify %u desc 0x%lx avail 0x%lx used 0x%lx reset %u\n",
			   nattr->q_attrs[i].vattr.idx,
			   nattr->q_attrs[i].vattr.size,
			   nattr->q_attrs[i].vattr.msix_vector,
			   nattr->q_attrs[i].vattr.enable,
			   nattr->q_attrs[i].vattr.notify_off,
			   nattr->q_attrs[i].vattr.desc,
			   nattr->q_attrs[i].vattr.driver,
			   nattr->q_attrs[i].vattr.device,
			   nattr->q_attrs[i].vattr.reset);
		q = DEVX_ADDR_OF(virtio_net_device_emulation, in,
				 virtio_q_configuration_v2[idx++]);

		snap_debug("offset %ld\n", q - in);
		DEVX_SET(virtio_q_layout_v2, q, queue_index,
			 nattr->q_attrs[i].vattr.idx);
		snap_virtio_net_queue_config_fill(q, mask, attr, i,
						  fields_to_modify);
	}

	return idx;
}

static void snap_virtio_blk_modify_queues(void *in, struct snap_virtio_device_attr *attr)
{
	struct snap_virtio_blk_device_attr *battr = to_blk_device_attr(attr);
	int i;
	void *q;

	snap_debug("modify queues, %d queues\n", attr->max_queues);
	for (i = 0; i < attr->max_queues; i++) {
		snap_debug("size: %u msix %u enable %u notify %u desc 0x%lx avail 0x%lx used 0x%lx\n",
			   battr->q_attrs[i].vattr.size,
			   battr->q_attrs[i].vattr.msix_vector,
			   battr->q_attrs[i].vattr.enable,
			   battr->q_attrs[i].vattr.notify_off,
			   battr->q_attrs[i].vattr.desc,
			   battr->q_attrs[i].vattr.driver,
			   battr->q_attrs[i].vattr.device);

		q = DEVX_ADDR_OF(virtio_blk_device_emulation, in, virtio_q_configuration[i]);
		snap_debug("offset %ld\n", q - in);

		DEVX_SET(virtio_q_layout, q, queue_size,
			 battr->q_attrs[i].vattr.size);
		DEVX_SET(virtio_q_layout, q, queue_msix_vector,
			 battr->q_attrs[i].vattr.msix_vector);
		DEVX_SET(virtio_q_layout, q, queue_enable,
			 battr->q_attrs[i].vattr.enable);
		DEVX_SET(virtio_q_layout, q, queue_notify_off,
			 battr->q_attrs[i].vattr.notify_off);

		DEVX_SET64(virtio_q_layout, q, queue_desc,
			   battr->q_attrs[i].vattr.desc);
		DEVX_SET64(virtio_q_layout, q, queue_driver,
			   battr->q_attrs[i].vattr.driver);
		DEVX_SET64(virtio_q_layout, q, queue_device,
			   battr->q_attrs[i].vattr.device);
	}
}

static void snap_virtio_fs_modify_queues(void *in, struct snap_virtio_device_attr *attr)
{
	struct snap_virtio_fs_device_attr *fs_attr = to_fs_device_attr(attr);
	int i;
	void *q;

	snap_debug("modify queues, %d queues\n", attr->max_queues);
	for (i = 0; i < attr->max_queues; i++) {
		snap_debug("size: %u msix %u enable %u notify %u desc 0x%lx avail 0x%lx used 0x%lx\n",
			   fs_attr->q_attrs[i].vattr.size,
			   fs_attr->q_attrs[i].vattr.msix_vector,
			   fs_attr->q_attrs[i].vattr.enable,
			   fs_attr->q_attrs[i].vattr.notify_off,
			   fs_attr->q_attrs[i].vattr.desc,
			   fs_attr->q_attrs[i].vattr.driver,
			   fs_attr->q_attrs[i].vattr.device);

		q = DEVX_ADDR_OF(virtio_fs_device_emulation, in, virtio_q_configuration[i]);
		snap_debug("offset %ld\n", q - in);

		DEVX_SET(virtio_q_layout, q, queue_size,
			 fs_attr->q_attrs[i].vattr.size);
		DEVX_SET(virtio_q_layout, q, queue_msix_vector,
			 fs_attr->q_attrs[i].vattr.msix_vector);
		DEVX_SET(virtio_q_layout, q, queue_enable,
			 fs_attr->q_attrs[i].vattr.enable);
		DEVX_SET(virtio_q_layout, q, queue_notify_off,
			 fs_attr->q_attrs[i].vattr.notify_off);

		DEVX_SET64(virtio_q_layout, q, queue_desc,
			   fs_attr->q_attrs[i].vattr.desc);
		DEVX_SET64(virtio_q_layout, q, queue_driver,
			   fs_attr->q_attrs[i].vattr.driver);
		DEVX_SET64(virtio_q_layout, q, queue_device,
			   fs_attr->q_attrs[i].vattr.device);
	}
}

static void snap_virtio_fs_impl_modify_device(uint8_t *in, uint8_t *device_emulation_in,
					      uint64_t *fields_to_modify,
					      uint64_t mask,
					      struct snap_virtio_device_attr *attr)
{
	struct snap_virtio_fs_device_attr *fs_dev_attr = to_fs_device_attr(attr);
	uint8_t *fs_tag;

	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
			MLX5_OBJ_TYPE_VIRTIO_FS_DEVICE_EMULATION);

	if (mask & (SNAP_VIRTIO_MOD_DEV_STATUS | SNAP_VIRTIO_MOD_ALL)) {
		*fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_STATUS;
		DEVX_SET(virtio_fs_device_emulation, device_emulation_in,
				virtio_device.device_status, attr->status);
	}
	if (mask & SNAP_VIRTIO_MOD_RESET) {
		*fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_RESET;
		DEVX_SET(virtio_fs_device_emulation, device_emulation_in,
				reset, attr->reset);
	}
	if (mask & (SNAP_VIRTIO_MOD_PCI_COMMON_CFG | SNAP_VIRTIO_MOD_ALL)) {
		*fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_PCI_COMMON_CFG;
		DEVX_SET64(virtio_fs_device_emulation, device_emulation_in,
				virtio_device.device_feature, attr->device_feature);
		DEVX_SET(virtio_fs_device_emulation, device_emulation_in,
				virtio_device.num_queues, attr->max_queues);
		DEVX_SET(virtio_fs_device_emulation, device_emulation_in,
				virtio_device.max_queue_size, attr->max_queue_size);
	}
	if (mask & SNAP_VIRTIO_MOD_DEV_CFG) {
		*fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_DEV_CFG;
		fs_tag = DEVX_ADDR_OF(virtio_fs_device_emulation, device_emulation_in,
					virtio_fs_config.tag);
		memcpy(fs_tag, fs_dev_attr->tag, SNAP_VIRTIO_FS_DEV_CFG_TAG_LEN);

		DEVX_SET(virtio_fs_device_emulation, device_emulation_in,
				virtio_fs_config.num_request_queues,
				fs_dev_attr->num_request_queues);
	}

	if (mask & SNAP_VIRTIO_MOD_ALL) {
		/* note: mod all overwrites STATUS and PCI_COMMON config */
		*fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_ALL;
		*fields_to_modify &= ~(SNAP_VIRTIO_MOD_PCI_COMMON_CFG |
				      SNAP_VIRTIO_MOD_DEV_STATUS);
		DEVX_SET64(virtio_fs_device_emulation, device_emulation_in,
				virtio_device.driver_feature, attr->driver_feature);

		DEVX_SET(virtio_fs_device_emulation, device_emulation_in,
				virtio_device.msix_config,
				attr->msix_config);
		DEVX_SET(virtio_fs_device_emulation, device_emulation_in,
				virtio_device.config_generation,
				attr->config_generation);
		DEVX_SET(virtio_fs_device_emulation, device_emulation_in,
				virtio_device.driver_feature_select,
				attr->driver_feature_select);
		DEVX_SET(virtio_fs_device_emulation, device_emulation_in,
				virtio_device.device_feature_select,
				attr->device_feature_select);
		DEVX_SET(virtio_fs_device_emulation, device_emulation_in,
				virtio_device.queue_select,
				attr->queue_select);
	}

	if (mask & SNAP_VIRTIO_MOD_QUEUE_CFG) {
		snap_virtio_fs_modify_queues(device_emulation_in, attr);
		*fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_QUEUE_CFG;
	}

	DEVX_SET64(virtio_fs_device_emulation, device_emulation_in,
		   modify_field_select, *fields_to_modify);
}

int snap_virtio_modify_device(struct snap_device *sdev,
		enum snap_emulation_type type,
		uint64_t mask, struct snap_virtio_device_attr *attr)
{
	const int queue_modify_mask = SNAP_VIRTIO_MOD_QUEUE_CFG |
				      SNAP_VIRTIO_MOD_VQ_CFG_Q_SIZE |
				      SNAP_VIRTIO_MOD_VQ_CFG_Q_MSIX_VECTOR |
				      SNAP_VIRTIO_MOD_VQ_CFG_Q_ENABLE |
				      SNAP_VIRTIO_MOD_VQ_CFG_Q_DESC |
				      SNAP_VIRTIO_MOD_VQ_CFG_Q_DRIVER |
				      SNAP_VIRTIO_MOD_VQ_CFG_Q_DEVICE |
				      SNAP_VIRTIO_MOD_VQ_CFG_Q_NOTIFY_OFF |
				      SNAP_VIRTIO_MOD_VQ_CFG_Q_RESET;
	uint64_t fields_to_modify = 0;
	uint8_t *in;
	int inlen;
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)];
	uint8_t *device_emulation_in;
	int q_cnt = 0;
	int ret;

	if (type == SNAP_VIRTIO_BLK &&
	    (sdev->pci->type != SNAP_VIRTIO_BLK_PF &&
	     sdev->pci->type != SNAP_VIRTIO_BLK_VF))
		return -EINVAL;
	else if (type == SNAP_VIRTIO_NET &&
		 (sdev->pci->type != SNAP_VIRTIO_NET_PF &&
		  sdev->pci->type != SNAP_VIRTIO_NET_VF))
		return -EINVAL;
	else if (type == SNAP_VIRTIO_FS &&
		 (sdev->pci->type != SNAP_VIRTIO_FS_PF &&
		  sdev->pci->type != SNAP_VIRTIO_FS_VF))
		return -EINVAL;
	else if (type == SNAP_NVME)
		return -EINVAL;

	//we'll modify only allowed fields
	snap_debug("mask 0x%0lx vs allowed 0x%0lx\n", mask, sdev->mod_allowed_mask);
	if (mask & ~sdev->mod_allowed_mask)
		return -EINVAL;

	inlen = DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);

	if (mask & queue_modify_mask)
		inlen += attr->max_queues * DEVX_ST_SZ_BYTES(virtio_q_layout_v2);

	if (type == SNAP_VIRTIO_BLK) {
		struct snap_virtio_blk_device_attr *battr = to_blk_device_attr(attr);

		inlen += DEVX_ST_SZ_BYTES(virtio_blk_device_emulation);
		in = calloc(1, inlen);
		if (!in)
			return -ENOMEM;

		device_emulation_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);

		DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
			 MLX5_OBJ_TYPE_VIRTIO_BLK_DEVICE_EMULATION);

		if (mask & (SNAP_VIRTIO_MOD_DEV_STATUS | SNAP_VIRTIO_MOD_ALL)) {
			fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_STATUS;
			DEVX_SET(virtio_blk_device_emulation, device_emulation_in,
				 virtio_device.device_status, attr->status);
		}
		if (mask & SNAP_VIRTIO_MOD_RESET) {
			fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_RESET;
			DEVX_SET(virtio_blk_device_emulation, device_emulation_in,
				 reset, attr->reset);
		}
		if (mask & SNAP_VIRTIO_MOD_PCI_HOTPLUG_STATE) {
			fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_PCI_HOTPLUG_STATE;
			DEVX_SET(virtio_blk_device_emulation, device_emulation_in,
				 pci_hotplug_state, attr->pci_hotplug_state);
		}
		if (mask & (SNAP_VIRTIO_MOD_PCI_COMMON_CFG | SNAP_VIRTIO_MOD_ALL)) {
			fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_PCI_COMMON_CFG;
			DEVX_SET64(virtio_blk_device_emulation, device_emulation_in,
				   virtio_device.device_feature, attr->device_feature);
			DEVX_SET(virtio_blk_device_emulation, device_emulation_in,
				 virtio_device.num_queues, attr->max_queues);
			DEVX_SET(virtio_blk_device_emulation, device_emulation_in,
				 virtio_device.max_queue_size, attr->max_queue_size);
		}
		if (mask & SNAP_VIRTIO_MOD_DEV_CFG) {
			fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_DEV_CFG;
			DEVX_SET64(virtio_blk_device_emulation, device_emulation_in,
				 virtio_blk_config.capacity, battr->capacity);
			DEVX_SET(virtio_blk_device_emulation, device_emulation_in,
				 virtio_blk_config.size_max, battr->size_max);
			DEVX_SET(virtio_blk_device_emulation, device_emulation_in,
				 virtio_blk_config.seg_max, battr->seg_max);
			DEVX_SET(virtio_blk_device_emulation, device_emulation_in,
				 virtio_blk_config.blk_size, battr->blk_size);
			DEVX_SET(virtio_blk_device_emulation, device_emulation_in,
				 virtio_blk_config.num_queues, battr->max_blk_queues);
		}

		if (mask & SNAP_VIRTIO_MOD_ALL) {
			/* note: mod all overwrites STATUS and PCI_COMMON config */
			fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_ALL;
			fields_to_modify &= ~(SNAP_VIRTIO_MOD_PCI_COMMON_CFG |
					      SNAP_VIRTIO_MOD_DEV_STATUS);
			DEVX_SET64(virtio_blk_device_emulation, device_emulation_in,
				   virtio_device.driver_feature, attr->driver_feature);

			DEVX_SET(virtio_blk_device_emulation, device_emulation_in,
				 virtio_device.msix_config,
				 attr->msix_config);
			DEVX_SET(virtio_blk_device_emulation, device_emulation_in,
				 virtio_device.config_generation,
				 attr->config_generation);
			DEVX_SET(virtio_blk_device_emulation, device_emulation_in,
				 virtio_device.driver_feature_select,
				 attr->driver_feature_select);
			DEVX_SET(virtio_blk_device_emulation, device_emulation_in,
				 virtio_device.device_feature_select,
				 attr->device_feature_select);
			DEVX_SET(virtio_blk_device_emulation, device_emulation_in,
				 virtio_device.queue_select,
				 attr->queue_select);
		}

		if (mask & SNAP_VIRTIO_MOD_QUEUE_CFG) {
			snap_virtio_blk_modify_queues(device_emulation_in, attr);
			fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_QUEUE_CFG;
		}

		DEVX_SET64(virtio_blk_device_emulation, device_emulation_in,
			   modify_field_select, fields_to_modify);
	} else if (type == SNAP_VIRTIO_NET) {
		struct snap_virtio_net_device_attr *nattr = to_net_device_attr(attr);

		inlen += DEVX_ST_SZ_BYTES(virtio_net_device_emulation);
		in = calloc(1, inlen);
		if (!in)
			return -ENOMEM;

		device_emulation_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);

		DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
			 MLX5_OBJ_TYPE_VIRTIO_NET_DEVICE_EMULATION);
		if (mask & (SNAP_VIRTIO_MOD_DEV_STATUS|SNAP_VIRTIO_MOD_ALL)) {
			fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_STATUS;
			DEVX_SET(virtio_net_device_emulation, device_emulation_in,
				 virtio_device.device_status, attr->status);
		}
		if (mask & (SNAP_VIRTIO_MOD_LINK_STATUS | SNAP_VIRTIO_MOD_ALL)) {
			fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_LINK;
			DEVX_SET(virtio_net_device_emulation, device_emulation_in,
				virtio_net_config.status, nattr->status);
		}
		if (mask & (SNAP_VIRTIO_MOD_RESET | SNAP_VIRTIO_MOD_ALL)) {
			fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_RESET;
			DEVX_SET(virtio_net_device_emulation, device_emulation_in,
				 reset, attr->reset);
		}
		if (mask & (SNAP_VIRTIO_MOD_DYN_MSIX_RESET | SNAP_VIRTIO_MOD_ALL)) {
			fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_DYN_VF_MSIX_RESET;
			DEVX_SET(virtio_net_device_emulation, device_emulation_in,
				 dynamic_vf_msix_reset, attr->dynamic_vf_msix_reset);
			snap_debug("Setting SNAP_VIRTIO_MOD_DYN_MSIX_RESET on PF\n");
		}
		if (mask & (SNAP_VIRTIO_MOD_PCI_HOTPLUG_STATE | SNAP_VIRTIO_MOD_ALL)) {
			fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_PCI_HOTPLUG_STATE;
			DEVX_SET(virtio_net_device_emulation, device_emulation_in,
				 pci_hotplug_state, attr->pci_hotplug_state);
			snap_debug("Setting SNAP_VIRTIO_MOD_PCI_HOTPLUG_STATE to %d\n",
				   attr->pci_hotplug_state);
		}
		if (mask & (SNAP_VIRTIO_MOD_NUM_MSIX | SNAP_VIRTIO_MOD_ALL)) {
			void *pci_params;

			fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_NUM_MSIX;
			pci_params = DEVX_ADDR_OF(virtio_net_device_emulation, device_emulation_in,
						  pci_params);
			DEVX_SET(device_pci_parameters, pci_params, num_msix, attr->num_msix);
			snap_debug("Setting SNAP_VIRTIO_MOD_NUM_MSIX, msix number: %d\n",
				   attr->num_msix);
		}

		if (mask & (SNAP_VIRTIO_MOD_PCI_COMMON_CFG | SNAP_VIRTIO_MOD_ALL)) {
			fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_PCI_COMMON_CFG;
			DEVX_SET64(virtio_net_device_emulation, device_emulation_in,
				   virtio_device.device_feature, attr->device_feature);
			DEVX_SET(virtio_net_device_emulation, device_emulation_in,
				 virtio_device.num_queues, attr->max_queues);
			DEVX_SET(virtio_net_device_emulation, device_emulation_in,
				 virtio_device.max_queue_size, attr->max_queue_size);
		}
		if (mask & (SNAP_VIRTIO_MOD_DEV_CFG | SNAP_VIRTIO_MOD_ALL)) {
			fields_to_modify |= MLX5_VIRTIO_DEVICE_MODIFY_DEV_CFG;
			DEVX_SET(virtio_net_device_emulation,
				 device_emulation_in,
				 virtio_net_config.mac_47_16,
				 nattr->mac >> 16);
			DEVX_SET(virtio_net_device_emulation,
				 device_emulation_in,
				 virtio_net_config.mac_15_0,
				 nattr->mac & 0xffff);
			DEVX_SET(virtio_net_device_emulation,
				 device_emulation_in,
				 virtio_net_config.max_virtqueue_pairs,
				 nattr->max_queue_pairs);
			DEVX_SET(virtio_net_device_emulation,
				 device_emulation_in,
				 virtio_net_config.mtu, nattr->mtu);
		}

		if (mask & SNAP_VIRTIO_MOD_ALL) {
			fields_to_modify = MLX5_VIRTIO_DEVICE_MODIFY_ALL;
			DEVX_SET64(virtio_net_device_emulation, device_emulation_in,
				   virtio_device.driver_feature, attr->driver_feature);

			DEVX_SET(virtio_net_device_emulation, device_emulation_in,
				 virtio_device.num_queues, attr->max_queues);
			DEVX_SET(virtio_net_device_emulation, device_emulation_in,
				 virtio_device.max_queue_size, attr->max_queue_size);

			DEVX_SET(virtio_net_device_emulation, device_emulation_in,
				 virtio_device.msix_config,
				 attr->msix_config);
			DEVX_SET(virtio_net_device_emulation, device_emulation_in,
				 virtio_device.config_generation,
				 attr->config_generation);
			DEVX_SET(virtio_net_device_emulation, device_emulation_in,
				 virtio_device.driver_feature_select,
				 attr->driver_feature_select);
			DEVX_SET(virtio_net_device_emulation, device_emulation_in,
				 virtio_device.device_feature_select,
				 attr->device_feature_select);
			DEVX_SET(virtio_net_device_emulation, device_emulation_in,
				 virtio_device.queue_select,
				 attr->queue_select);
		}

		if (!sdev->sctx->virtio_net_caps.virtio_q_cfg_v2) {
			if (mask & SNAP_VIRTIO_MOD_QUEUE_CFG) {
				snap_virtio_net_modify_queues(device_emulation_in,
							      attr);
				fields_to_modify |=
						MLX5_VIRTIO_DEVICE_MODIFY_QUEUE_CFG;
			}
		} else {
			if (mask & queue_modify_mask) {
				q_cnt = snap_virtio_net_queue_configs_v2_fill(
								device_emulation_in,
								mask, attr,
								&fields_to_modify);
				DEVX_SET(virtio_net_device_emulation,
					 device_emulation_in,
					 q_configuration_list_size,
					 q_cnt);
				DEVX_SET(virtio_net_device_emulation,
					 device_emulation_in,
					 q_cfg_version, 1);
			}
		}

		DEVX_SET64(virtio_net_device_emulation, device_emulation_in,
			   modify_field_select, fields_to_modify);
	} else {
		inlen += DEVX_ST_SZ_BYTES(virtio_fs_device_emulation);
		in = calloc(1, inlen);
		if (!in)
			return -ENOMEM;

		device_emulation_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
		snap_virtio_fs_impl_modify_device(in, device_emulation_in,
						  &fields_to_modify, mask, attr);
	}

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_MODIFY_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_id,
		 sdev->mdev.device_emulation->obj_id);

	ret = snap_devx_obj_modify(sdev->mdev.device_emulation, in, inlen,
				   out, sizeof(out));

	snap_debug("ret %d in %p inlen %d modify 0x%0lx\n", ret, in, inlen, fields_to_modify);
	free(in);
	return ret;
}

struct mlx5_snap_devx_obj*
snap_virtio_create_queue_counters(struct snap_device *sdev)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		       DEVX_ST_SZ_BYTES(virtio_q_counters)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)] = {0};
	struct mlx5_snap_devx_obj *counters;

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_OBJ_TYPE_VIRTIO_Q_COUNTERS);

	counters = snap_devx_obj_create(sdev, in, sizeof(in), out, sizeof(out),
					NULL,
					DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr),
					DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr));
	if (!counters) {
		snap_error("Failed to create VirtIO counters devx object\n");
		errno = ENODEV;
		goto out;
	}

	return counters;
out:
	return NULL;
}

int snap_virtio_query_queue_counters(struct mlx5_snap_devx_obj *counters_obj,
			 struct snap_virtio_queue_counters_attr *attr)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr) +
			DEVX_ST_SZ_BYTES(virtio_q_counters)] = {0};
	uint8_t *virtq_cnt_out;
	int ret;

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		MLX5_CMD_OP_QUERY_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		MLX5_OBJ_TYPE_VIRTIO_Q_COUNTERS);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_id, counters_obj->obj_id);

	ret = snap_devx_obj_query(counters_obj, in, sizeof(in), out, sizeof(out));
	if (ret)
		return ret;

	virtq_cnt_out = out + DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr);
	attr->received_desc = DEVX_GET64(virtio_q_counters, virtq_cnt_out,
					 received_desc);
	attr->completed_desc = DEVX_GET64(virtio_q_counters, virtq_cnt_out,
					  completed_desc);
	attr->error_cqes = DEVX_GET(virtio_q_counters, virtq_cnt_out,
				    error_cqes);
	attr->bad_desc_errors = DEVX_GET(virtio_q_counters, virtq_cnt_out,
					 bad_desc_errors);
	attr->exceed_max_chain = DEVX_GET(virtio_q_counters, virtq_cnt_out,
					  exceed_max_chain);
	attr->invalid_buffer = DEVX_GET(virtio_q_counters, virtq_cnt_out,
					invalid_buffer);
	return ret;
}

static struct mlx5_snap_devx_obj*
snap_virtio_create_queue(struct snap_device *sdev,
	struct snap_virtio_queue_attr *vattr, struct snap_umem *umem)
{
	uint8_t in_blk[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		       DEVX_ST_SZ_BYTES(virtio_blk_q)] = {0};
	uint8_t in_net[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		       DEVX_ST_SZ_BYTES(virtio_net_q)] = {0};
	uint8_t in_fs[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		       DEVX_ST_SZ_BYTES(virtio_fs_q)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)] = {0};
	uint8_t *in;
	uint8_t *virtq_in;
	uint8_t *virtq_ctx;
	uint16_t obj_type;
	struct mlx5_snap_devx_obj *virtq;
	int virtq_type, ev_mode, inlen, offload_type, ret;
	uint32_t pd_id;

	if (vattr->type == SNAP_VIRTQ_SPLIT_MODE) {
		virtq_type = MLX5_VIRTIO_QUEUE_TYPE_SPLIT;
	} else if (vattr->type == SNAP_VIRTQ_PACKED_MODE) {
		virtq_type = MLX5_VIRTIO_QUEUE_TYPE_PACKED;
	} else {
		errno = EINVAL;
		goto out;
	}

	if (vattr->ev_mode == SNAP_VIRTQ_NO_MSIX_MODE) {
		ev_mode = MLX5_VIRTIO_QUEUE_EVENT_MODE_NO_MSIX;
	} else if (vattr->ev_mode == SNAP_VIRTQ_QP_MODE) {
		ev_mode = MLX5_VIRTIO_QUEUE_EVENT_MODE_QP;
	} else if (vattr->ev_mode == SNAP_VIRTQ_MSIX_MODE) {
		ev_mode = MLX5_VIRTIO_QUEUE_EVENT_MODE_MSIX;
	} else {
		errno = EINVAL;
		goto out;
	}

	if (vattr->offload_type == SNAP_VIRTQ_OFFLOAD_ETH_FRAME) {
		offload_type = MLX5_VIRTIO_Q_OFFLOAD_TYPE_ETH_FRAME;
	} else if (vattr->offload_type == SNAP_VIRTQ_OFFLOAD_DESC_TUNNEL) {
		offload_type = MLX5_VIRTIO_Q_OFFLOAD_TYPE_DESC_TUNNEL;
	} else {
		errno = EINVAL;
		goto out;
	}

	if (sdev->pci->type == SNAP_VIRTIO_BLK_PF ||
	    sdev->pci->type == SNAP_VIRTIO_BLK_VF) {
		in = in_blk;
		inlen = sizeof(in_blk);
		virtq_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
		virtq_ctx = DEVX_ADDR_OF(virtio_blk_q, virtq_in, virtqc);

		obj_type = MLX5_OBJ_TYPE_VIRTIO_BLK_Q;
		DEVX_SET(virtio_blk_q, virtq_in, qpn, vattr->tisn_or_qpn);
		DEVX_SET(virtio_blk_q, virtq_in, qpn_vhca_id, vattr->vhca_id);
		DEVX_SET(virtio_blk_q, virtq_in, hw_available_index, vattr->hw_available_index);
		DEVX_SET(virtio_blk_q, virtq_in, hw_used_index, vattr->hw_used_index);
		if (sdev->sctx->virtio_blk_caps.virtio_q_counters)
			DEVX_SET(virtio_q, virtq_ctx, counter_set_id, vattr->ctrs_obj_id);

	} else if (sdev->pci->type == SNAP_VIRTIO_NET_PF ||
		   sdev->pci->type == SNAP_VIRTIO_NET_VF) {
		struct snap_virtio_net_queue_attr *attr;

		attr = to_net_queue_attr(vattr);
		in = in_net;
		inlen = sizeof(in_net);
		virtq_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
		virtq_ctx = DEVX_ADDR_OF(virtio_net_q, virtq_in, virtqc);

		obj_type = MLX5_OBJ_TYPE_VIRTIO_NET_Q;

		DEVX_SET(virtio_net_q, virtq_in, tisn_or_qpn,
			 vattr->tisn_or_qpn);
		if (vattr->tisn_or_qpn)
			DEVX_SET(virtio_net_q, virtq_in, vhca_id,
				 vattr->vhca_id);
		DEVX_SET(virtio_net_q, virtq_in, hw_available_index,
			 attr->hw_available_index);
		DEVX_SET(virtio_net_q, virtq_in, hw_used_index,
			 attr->hw_used_index);
		DEVX_SET(virtio_q, virtq_ctx, counter_set_id,
			 vattr->ctrs_obj_id);

		if (offload_type == MLX5_VIRTIO_Q_OFFLOAD_TYPE_ETH_FRAME) {
			DEVX_SET(virtio_net_q, virtq_in, tso_ipv4,
				 attr->tso_ipv4);
			DEVX_SET(virtio_net_q, virtq_in, tso_ipv6,
				 attr->tso_ipv6);
			DEVX_SET(virtio_net_q, virtq_in, tx_csum,
				 attr->tx_csum);
			DEVX_SET(virtio_net_q, virtq_in, rx_csum,
				 attr->rx_csum);
		}

	} else if (sdev->pci->type == SNAP_VIRTIO_FS_PF ||
		   sdev->pci->type == SNAP_VIRTIO_FS_VF) {
		struct snap_virtio_common_queue_attr *attr;
		int vhca_id;

		attr = to_fs_queue_attr(vattr);
		in = in_fs;
		inlen = sizeof(in_fs);
		virtq_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
		virtq_ctx = DEVX_ADDR_OF(virtio_fs_q, virtq_in, virtqc);

		obj_type = MLX5_OBJ_TYPE_VIRTIO_FS_Q;
		if (attr->qp) {
			vhca_id = snap_get_dev_vhca_id(attr->qp->context);
			if (vhca_id < 0) {
				errno = EINVAL;
				goto out;
			}
			DEVX_SET(virtio_fs_q, virtq_in, qpn, attr->qp->qp_num);
			DEVX_SET(virtio_fs_q, virtq_in, qpn_vhca_id, vhca_id);
		}
		DEVX_SET(virtio_fs_q, virtq_in, hw_available_index, attr->hw_available_index);
		DEVX_SET(virtio_fs_q, virtq_in, hw_used_index, attr->hw_used_index);
		DEVX_SET(virtio_q, virtq_ctx, counter_set_id, vattr->ctrs_obj_id);

	} else {
		errno = EINVAL;
		goto out;
	}

	DEVX_SET64(virtio_q, virtq_ctx, desc_addr, vattr->desc);
	DEVX_SET64(virtio_q, virtq_ctx, available_addr, vattr->driver);
	DEVX_SET64(virtio_q, virtq_ctx, used_addr, vattr->device);

	ret = snap_get_pd_id(vattr->pd, &pd_id);
	if (ret) {
		errno = ret;
		goto out;
	}

	/* common virtq attributes */
	DEVX_SET(virtio_q, virtq_ctx, pd, pd_id);
	DEVX_SET(virtio_q, virtq_ctx, device_emulation_id,
		 sdev->pci->mpci.vhca_id);
	DEVX_SET(virtio_q, virtq_ctx, virtio_q_type, virtq_type);
	DEVX_SET(virtio_q, virtq_ctx, event_mode, ev_mode);
	DEVX_SET(virtio_q, virtq_ctx, queue_index, vattr->idx);
	DEVX_SET(virtio_q, virtq_ctx, queue_size, vattr->size);
	DEVX_SET(virtio_q, virtq_ctx, full_emulation, vattr->full_emulation);
	DEVX_SET(virtio_q, virtq_ctx, virtio_version_1_0, vattr->virtio_version_1_0);
	DEVX_SET(virtio_q, virtq_ctx, max_tunnel_desc, vattr->max_tunnel_desc);
	DEVX_SET(virtio_q, virtq_ctx, event_qpn_or_msix, vattr->event_qpn_or_msix);
	DEVX_SET(virtio_q, virtq_ctx, offload_type, offload_type);
	DEVX_SET(virtio_q, virtq_ctx, queue_period_mode,
		 vattr->queue_period_mode);
	DEVX_SET(virtio_q, virtq_ctx, queue_period, vattr->queue_period);
	DEVX_SET(virtio_q, virtq_ctx, queue_max_count,
		 vattr->queue_max_count);
	DEVX_SET(virtio_q, virtq_ctx, virtio_q_mkey, vattr->dma_mkey);

	if (umem[0].devx_umem) {
		DEVX_SET(virtio_q, virtq_ctx, umem_1_id, umem[0].devx_umem->umem_id);
		DEVX_SET(virtio_q, virtq_ctx, umem_1_size, umem[0].size);
		DEVX_SET64(virtio_q, virtq_ctx, umem_1_offset, 0);
	}
	if (umem[1].devx_umem) {
		DEVX_SET(virtio_q, virtq_ctx, umem_2_id, umem[1].devx_umem->umem_id);
		DEVX_SET(virtio_q, virtq_ctx, umem_2_size, umem[1].size);
		DEVX_SET64(virtio_q, virtq_ctx, umem_2_offset, 0);
	}
	if (umem[2].devx_umem) {
		DEVX_SET(virtio_q, virtq_ctx, umem_3_id, umem[2].devx_umem->umem_id);
		DEVX_SET(virtio_q, virtq_ctx, umem_3_size, umem[2].size);
		DEVX_SET64(virtio_q, virtq_ctx, umem_3_offset, 0);
	}

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type, obj_type);

	virtq = snap_devx_obj_create(sdev, in, inlen, out, sizeof(out),
				     sdev->mdev.vtunnel,
				     DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr),
				     DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr));
	if (!virtq) {
		errno = ENODEV;
		goto out;
	}

	if (sdev->mdev.vtunnel) {
		void *dtor = virtq->dtor_in;

		DEVX_SET(general_obj_in_cmd_hdr, dtor, opcode,
			 MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
		DEVX_SET(general_obj_in_cmd_hdr, dtor, obj_type, obj_type);
		DEVX_SET(general_obj_in_cmd_hdr, dtor, obj_id, virtq->obj_id);
	}

	return virtq;
out:
	return NULL;
}

static int snap_virtio_destroy_queue(struct snap_virtio_queue *vq)
{
	int ret;

	ret = snap_devx_obj_destroy(vq->virtq);
	vq->virtq = NULL;

	return ret;
}

int snap_virtio_create_hw_queue(struct snap_device *sdev,
				struct snap_virtio_queue *vq,
				struct snap_virtio_caps *caps,
				struct snap_virtio_queue_attr *vattr)
{
	struct ibv_context *context;
	int ret;

	context = sdev->mdev.context ? sdev->mdev.context :
		sdev->sctx->context;

	ret = snap_virtio_init_virtq_umem(context, caps, vq, vattr->size);
	if (ret)
		goto err;

	if (caps->virtio_q_counters && vq->ctrs_obj)
		vattr->ctrs_obj_id = vq->ctrs_obj->obj_id;

	vq->virtq = snap_virtio_create_queue(sdev, vattr, vq->umem);
	if (!vq->virtq) {
		ret = -1;
		goto out_umem;
	}

	vq->idx = vattr->idx;

	return 0;

out_umem:
	snap_virtio_teardown_virtq_umem(vq);
err:
	errno = ret;
	return ret;
}

int snap_virtio_destroy_hw_queue(struct snap_virtio_queue *vq)
{
	int q_ret;

	q_ret = snap_virtio_destroy_queue(vq);
	snap_virtio_teardown_virtq_umem(vq);

	return q_ret;
}

static int snap_virtio_queue_state_to_mlx_state(enum snap_virtq_state state)
{
	switch (state) {
	case SNAP_VIRTQ_STATE_INIT:
		return MLX5_VIRTIO_Q_STATE_INIT;
	case SNAP_VIRTQ_STATE_RDY:
		return MLX5_VIRTIO_Q_STATE_RDY;
	case SNAP_VIRTQ_STATE_SUSPEND:
		return MLX5_VIRTIO_Q_STATE_SUSPEND;
	case SNAP_VIRTQ_STATE_ERR:
		return MLX5_VIRTIO_Q_STATE_ERR;
	default:
		return -EINVAL;
	}
}

static enum snap_virtq_state snap_virtio_mlx_state_to_queue_state(int mlx_state)
{
	switch (mlx_state) {
	case MLX5_VIRTIO_Q_STATE_INIT:
		return SNAP_VIRTQ_STATE_INIT;
	case MLX5_VIRTIO_Q_STATE_RDY:
		return SNAP_VIRTQ_STATE_RDY;
	case MLX5_VIRTIO_Q_STATE_SUSPEND:
		return SNAP_VIRTQ_STATE_SUSPEND;
	case MLX5_VIRTIO_Q_STATE_ERR:
		return SNAP_VIRTQ_STATE_ERR;
	default:
		return -EINVAL;
	}
}

int snap_virtio_modify_queue(struct snap_virtio_queue *virtq, uint64_t mask,
			     struct snap_virtio_queue_attr *vattr)
{
	uint8_t in_blk[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
			DEVX_ST_SZ_BYTES(virtio_blk_q)] = {0};
	uint8_t in_net[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
			DEVX_ST_SZ_BYTES(virtio_net_q)] = {0};
	uint8_t in_fs[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
			DEVX_ST_SZ_BYTES(virtio_fs_q)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)];
	struct snap_device *sdev = virtq->virtq->sdev;
	uint8_t *in;
	uint8_t *virtq_in;
	uint8_t *virtq_ctx;
	uint64_t fields_to_modify = 0;
	int inlen, state;

	/* we'll modify only allowed fields */
	if (mask & ~virtq->mod_allowed_mask)
		return -EINVAL;

	if (sdev->pci->type == SNAP_VIRTIO_BLK_PF ||
	    sdev->pci->type == SNAP_VIRTIO_BLK_VF) {
		in = in_blk;
		inlen = sizeof(in_blk);

		DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
			 MLX5_OBJ_TYPE_VIRTIO_BLK_Q);
		virtq_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);

		if (mask & SNAP_VIRTIO_BLK_QUEUE_MOD_STATE) {
			state = snap_virtio_queue_state_to_mlx_state(vattr->state);
			if (state < 0)
				return state;
			fields_to_modify = MLX5_VIRTIO_BLK_Q_MODIFY_STATE;
			DEVX_SET(virtio_blk_q, virtq_in, state, state);
		}
		DEVX_SET64(virtio_blk_q, virtq_in, modify_field_select,
			   fields_to_modify);
	} else if (sdev->pci->type == SNAP_VIRTIO_NET_PF ||
		   sdev->pci->type == SNAP_VIRTIO_NET_VF) {
		struct snap_virtio_net_queue_attr *attr;

		attr = to_net_queue_attr(vattr);
		in = in_net;
		inlen = sizeof(in_net);

		DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
			 MLX5_OBJ_TYPE_VIRTIO_NET_Q);
		virtq_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
		virtq_ctx = DEVX_ADDR_OF(virtio_net_q, virtq_in, virtqc);

		if (mask & SNAP_VIRTIO_NET_QUEUE_MOD_STATE) {
			state = snap_virtio_queue_state_to_mlx_state(vattr->state);
			if (state < 0)
				return state;
			fields_to_modify = MLX5_VIRTIO_NET_Q_MODIFY_STATE;
			DEVX_SET(virtio_net_q, virtq_in, state, state);
		}

		if (mask & SNAP_VIRTIO_NET_QUEUE_PERIOD) {
			fields_to_modify |= SNAP_VIRTIO_NET_QUEUE_PERIOD;
			DEVX_SET(virtio_q, virtq_ctx, queue_period_mode,
				 vattr->queue_period_mode);
			DEVX_SET(virtio_q, virtq_ctx, queue_period, vattr->queue_period);
			DEVX_SET(virtio_q, virtq_ctx, queue_max_count,
				 vattr->queue_max_count);
		}

		if (mask & SNAP_VIRTIO_NET_QUEUE_MOD_DIRTY_MAP_PARAM) {
			fields_to_modify |= SNAP_VIRTIO_NET_QUEUE_MOD_DIRTY_MAP_PARAM;
			DEVX_SET(virtio_net_q, virtq_in, dirty_map_mkey, vattr->dirty_map_mkey);
			DEVX_SET(virtio_net_q, virtq_in, dirty_map_size, vattr->dirty_map_size);
			DEVX_SET(virtio_net_q, virtq_in, vhost_log_page, vattr->vhost_log_page);
			DEVX_SET64(virtio_net_q, virtq_in, dirty_map_addr, vattr->dirty_map_addr);
		}

		if (mask & SNAP_VIRTIO_NET_QUEUE_MOD_DIRTY_MAP_ENABLE) {
			fields_to_modify |= SNAP_VIRTIO_NET_QUEUE_MOD_DIRTY_MAP_ENABLE;
			DEVX_SET(virtio_net_q,  virtq_in, dirty_map_dump_enable, vattr->dirty_map_dump_enable);
		}

		if (mask & SNAP_VIRTIO_NET_QUEUE_HW_AVAIL_IDX) {
			fields_to_modify |= SNAP_VIRTIO_NET_QUEUE_HW_AVAIL_IDX;
			DEVX_SET(virtio_net_q,  virtq_in, hw_available_index, attr->hw_available_index);
		}

		if (mask & SNAP_VIRTIO_NET_QUEUE_HW_USED_IDX) {
			fields_to_modify |= SNAP_VIRTIO_NET_QUEUE_HW_USED_IDX;
			DEVX_SET(virtio_net_q,  virtq_in, hw_used_index, attr->hw_used_index);
		}

		if (mask & SNAP_VIRTIO_NET_QUEUE_DESC_USED_AVAIL_ADDR) {
			fields_to_modify |= SNAP_VIRTIO_NET_QUEUE_DESC_USED_AVAIL_ADDR;
			DEVX_SET64(virtio_q,  virtq_ctx, desc_addr, vattr->desc);
			DEVX_SET64(virtio_q,  virtq_ctx, used_addr, vattr->device);
			DEVX_SET64(virtio_q,  virtq_ctx, available_addr, vattr->driver);
		}
		DEVX_SET64(virtio_net_q, virtq_in, modify_field_select,
			   fields_to_modify);
	} else if (sdev->pci->type == SNAP_VIRTIO_FS_PF ||
		   sdev->pci->type == SNAP_VIRTIO_FS_VF) {
		in = in_fs;
		inlen = sizeof(in_fs);

		DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
			 MLX5_OBJ_TYPE_VIRTIO_FS_Q);
		virtq_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);

		if (mask & SNAP_VIRTIO_FS_QUEUE_MOD_STATE) {
			state = snap_virtio_queue_state_to_mlx_state(vattr->state);
			if (state < 0)
				return state;
			fields_to_modify = MLX5_VIRTIO_FS_Q_MODIFY_STATE;
			DEVX_SET(virtio_fs_q, virtq_in, state, state);
		}
		DEVX_SET64(virtio_fs_q, virtq_in, modify_field_select,
			   fields_to_modify);
	} else {
		return -ENODEV;
	}

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_MODIFY_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_id, virtq->virtq->obj_id);

	return snap_devx_obj_modify(virtq->virtq, in, inlen, out, sizeof(out));
}

int snap_virtio_query_queue(struct snap_virtio_queue *virtq,
	struct snap_virtio_queue_attr *vattr)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr)] = {0};
	uint8_t out_blk[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr) +
			DEVX_ST_SZ_BYTES(virtio_blk_q)] = {0};
	uint8_t out_net[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr) +
			DEVX_ST_SZ_BYTES(virtio_net_q)] = {0};
	uint8_t out_fs[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr) +
			DEVX_ST_SZ_BYTES(virtio_fs_q)] = {0};
	uint8_t *out;
	uint8_t *virtq_out;
	uint8_t state;
	uint64_t dev_allowed;
	int ret, outlen;
	struct snap_device *sdev = virtq->virtq->sdev;


	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_QUERY_GENERAL_OBJECT);

	if (sdev->pci->type == SNAP_VIRTIO_BLK_PF ||
	    sdev->pci->type == SNAP_VIRTIO_BLK_VF) {
		DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
			 MLX5_OBJ_TYPE_VIRTIO_BLK_Q);
		out = out_blk;
		outlen = sizeof(out_blk);
	} else if (sdev->pci->type == SNAP_VIRTIO_NET_PF ||
		   sdev->pci->type == SNAP_VIRTIO_NET_VF) {
		DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
			 MLX5_OBJ_TYPE_VIRTIO_NET_Q);
		out = out_net;
		outlen = sizeof(out_net);
	} else if (sdev->pci->type == SNAP_VIRTIO_FS_PF ||
		   sdev->pci->type == SNAP_VIRTIO_FS_VF) {
		DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
			 MLX5_OBJ_TYPE_VIRTIO_FS_Q);
		out = out_fs;
		outlen = sizeof(out_fs);
	} else {
		return -EINVAL;
	}
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_id, virtq->virtq->obj_id);

	ret = snap_devx_obj_query(virtq->virtq, in, sizeof(in), out, outlen);
	if (ret)
		return ret;

	virtq_out = out + DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr);
	if (out == out_blk) {
		struct snap_virtio_common_queue_attr *attr;

		attr = to_common_queue_attr(vattr);

		vattr->size = DEVX_GET(virtio_blk_q, virtq_out, virtqc.queue_size);
		vattr->idx = DEVX_GET(virtio_blk_q, virtq_out, virtqc.queue_index);
		vattr->error_type = DEVX_GET(virtio_blk_q, virtq_out, virtqc.error_type);

		state = DEVX_GET(virtio_blk_q, virtq_out, state);
		vattr->state = snap_virtio_mlx_state_to_queue_state(state);
		if (vattr->state < 0)
			return vattr->state;

		attr->hw_available_index = DEVX_GET(virtio_blk_q, virtq_out, hw_available_index);
		attr->hw_used_index = DEVX_GET(virtio_blk_q, virtq_out, hw_used_index);
		dev_allowed = DEVX_GET64(virtio_blk_q, virtq_out, modify_field_select);
		if (dev_allowed & MLX5_VIRTIO_BLK_Q_MODIFY_STATE)
			attr->modifiable_fields = SNAP_VIRTIO_BLK_QUEUE_MOD_STATE;
		else
			attr->modifiable_fields = 0;
	} else if (out == out_net) {
		struct snap_virtio_net_queue_attr *attr;

		attr = to_net_queue_attr(vattr);

		vattr->size = DEVX_GET(virtio_net_q, virtq_out, virtqc.queue_size);
		vattr->idx = DEVX_GET(virtio_net_q, virtq_out, virtqc.queue_index);

		state = DEVX_GET(virtio_net_q, virtq_out, state);
		vattr->state = snap_virtio_mlx_state_to_queue_state(state);
		if (vattr->state < 0)
			return vattr->state;

		attr->hw_available_index = DEVX_GET(virtio_net_q, virtq_out, hw_available_index);
		attr->hw_used_index = DEVX_GET(virtio_net_q, virtq_out, hw_used_index);
		dev_allowed = DEVX_GET64(virtio_net_q, virtq_out, modify_field_select);
		if (dev_allowed & MLX5_VIRTIO_NET_Q_MODIFY_STATE)
			attr->modifiable_fields = SNAP_VIRTIO_NET_QUEUE_MOD_STATE;

		if (dev_allowed & SNAP_VIRTIO_NET_QUEUE_PERIOD)
			attr->modifiable_fields |= SNAP_VIRTIO_NET_QUEUE_PERIOD;
		/* lm */
		if (dev_allowed & SNAP_VIRTIO_NET_QUEUE_MOD_DIRTY_MAP_PARAM)
			attr->modifiable_fields |= SNAP_VIRTIO_NET_QUEUE_MOD_DIRTY_MAP_PARAM;

		if (dev_allowed & SNAP_VIRTIO_NET_QUEUE_MOD_DIRTY_MAP_ENABLE)
			attr->modifiable_fields |= SNAP_VIRTIO_NET_QUEUE_MOD_DIRTY_MAP_ENABLE;

		if (sdev->sctx->virtio_net_caps.virtio_net_q_addr_modify)
			attr->modifiable_fields |= SNAP_VIRTIO_NET_QUEUE_DESC_USED_AVAIL_ADDR;

		if (sdev->sctx->virtio_net_caps.virtio_q_index_modify) {
			attr->modifiable_fields |= SNAP_VIRTIO_NET_QUEUE_HW_AVAIL_IDX;
			attr->modifiable_fields |= SNAP_VIRTIO_NET_QUEUE_HW_USED_IDX;
		}

		vattr->dirty_map_dump_enable = DEVX_GET(virtio_net_q, virtq_out, dirty_map_dump_enable);
		vattr->dirty_map_mkey = DEVX_GET(virtio_net_q, virtq_out, dirty_map_mkey);
		vattr->dirty_map_size = DEVX_GET(virtio_net_q, virtq_out, dirty_map_size);
		vattr->dirty_map_addr = DEVX_GET64(virtio_net_q, virtq_out, dirty_map_addr);
		vattr->vhost_log_page = DEVX_GET(virtio_net_q, virtq_out, vhost_log_page);
	} else {
		struct snap_virtio_common_queue_attr *attr;

		attr = to_fs_queue_attr(vattr);

		vattr->size = DEVX_GET(virtio_fs_q, virtq_out, virtqc.queue_size);
		vattr->idx = DEVX_GET(virtio_fs_q, virtq_out, virtqc.queue_index);
		vattr->error_type = DEVX_GET(virtio_fs_q, virtq_out, virtqc.error_type);

		state = DEVX_GET(virtio_fs_q, virtq_out, state);
		vattr->state = snap_virtio_mlx_state_to_queue_state(state);
		if (vattr->state < 0)
			return vattr->state;

		attr->hw_available_index = DEVX_GET(virtio_fs_q, virtq_out, hw_available_index);
		attr->hw_used_index = DEVX_GET(virtio_fs_q, virtq_out, hw_used_index);
		dev_allowed = DEVX_GET64(virtio_fs_q, virtq_out, modify_field_select);
		if (dev_allowed & MLX5_VIRTIO_FS_Q_MODIFY_STATE)
			attr->modifiable_fields = SNAP_VIRTIO_FS_QUEUE_MOD_STATE;
		else
			attr->modifiable_fields = 0;
	}

	return 0;
}

int snap_virtio_get_mod_fields_queue(struct snap_virtio_queue *virtq)
{
	struct snap_virtio_common_queue_attr cattr = {};
	struct snap_device *sdev = virtq->virtq->sdev;
	struct snap_virtio_net_queue_attr nattr = {};
	int ret = 0;

	switch (sdev->pci->type) {
	case SNAP_VIRTIO_NET_PF:
	case SNAP_VIRTIO_NET_VF:
		ret = snap_virtio_query_queue(virtq, &nattr.vattr);
		if (ret) {
			snap_error("Failed to query snap_vq hw_q\n");
			return ret;
		}
		virtq->mod_allowed_mask = nattr.modifiable_fields;
		break;
	default:
		ret = snap_virtio_query_queue(virtq, &cattr.vattr);
		if (ret) {
			snap_error("Failed to query snap_vq hw_q\n");
			return ret;
		}
		virtq->mod_allowed_mask = cattr.modifiable_fields;
		break;
	}

	return 0;
}

static int snap_virtio_init_virtq_umem(struct ibv_context *context,
					struct snap_virtio_caps *virtio,
					struct snap_virtio_queue *virtq,
					int depth)
{
	int ret;

	virtq->umem[0].size = (virtio->umem_1_buffer_param_a * depth) +
			virtio->umem_1_buffer_param_b;
	ret = snap_umem_init(context, &virtq->umem[0]);
	if (ret)
		goto out_free_buf_0;

	virtq->umem[1].size = (virtio->umem_2_buffer_param_a * depth) +
			virtio->umem_2_buffer_param_b;
	ret = snap_umem_init(context, &virtq->umem[1]);
	if (ret)
		goto out_free_buf_1;

	virtq->umem[2].size = (virtio->umem_3_buffer_param_a * depth) +
			virtio->umem_3_buffer_param_b;
	ret = snap_umem_init(context, &virtq->umem[2]);
	if (ret)
		goto out_free_buf_2;

	return 0;

out_free_buf_2:
	snap_umem_reset(&virtq->umem[1]);
out_free_buf_1:
	snap_umem_reset(&virtq->umem[0]);
out_free_buf_0:
	return -ENOMEM;
}

static void snap_virtio_teardown_virtq_umem(struct snap_virtio_queue *virtq)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(virtq->umem); i++)
		snap_umem_reset(&virtq->umem[i]);
}


static void get_vring_rx_cb(struct snap_dma_q *q, const void *data, uint32_t data_len,
			    uint32_t imm_data)
{
	snap_error("Got unexpected completion on DMA queue %p data_len %u\n",
		   q, data_len);
}

int snap_virtio_get_avail_index_from_host(struct snap_dma_q *dma_q,
		uint64_t drv_addr, uint32_t dma_mkey, uint16_t *hw_avail)
{
	struct vring_avail vra;
	int ret;

	ret = snap_dma_q_read_short(dma_q, &vra, sizeof(struct vring_avail),
			      drv_addr, dma_mkey, NULL);
	if (ret) {
		snap_error("failed DMA read vring_avail for drv: 0x%lx\n", drv_addr);
		return ret;
	}

	ret = snap_dma_q_flush(dma_q);
	if (ret != 1)
		snap_error("failed flush, ret %d\n", ret);

	*hw_avail = vra.idx;

	return 0;
}

int snap_virtio_get_used_index_from_host(struct snap_dma_q *dma_q,
		uint64_t dev_addr, uint32_t dma_mkey, uint16_t *hw_used)
{
	struct vring_used vru;
	int ret;

	ret = snap_dma_q_read_short(dma_q, &vru, sizeof(struct vring_used),
			      dev_addr, dma_mkey, NULL);
	if (ret) {
		snap_error("failed DMA read vring_used for dev: 0x%lx\n", dev_addr);
		return ret;
	}

	ret = snap_dma_q_flush(dma_q);
	if (ret != 1)
		snap_error("failed flush, ret %d\n", ret);

	*hw_used = vru.idx;

	return 0;
}

/**
 * snap_virtio_get_vring_indexes_from_host() - read vring indexes from host memory
 * @pd:		protection domain used for dma q creation
 * @drv_addr:	physical address of Driver Area
 * @dev_addr:	physical address of Device Area
 * @dma_mkey:	host memory key that describes remote memory
 * @vra:	buffer to save the vring_avail data
 * @vru:	buffer to save the vring_used data
 *
 * The function will read available and used indexes of virtio queue (vring)
 * from host memory.
 *
 * Return:
 * 0 on success, -errno on error.
 */
int snap_virtio_get_vring_indexes_from_host(struct ibv_pd *pd, uint64_t drv_addr,
					    uint64_t dev_addr, uint32_t dma_mkey,
					    struct vring_avail *vra,
					    struct vring_used *vru)
{
	struct snap_dma_q_create_attr dma_q_attr = {};
	struct snap_dma_q *dma_q;
	int ret;

	if (!(pd && vra && vru))
		return -EINVAL;

	/* Create queue only for 2 DMA operations */
	dma_q_attr.tx_qsize = 2;
	dma_q_attr.rx_qsize = 2;
	dma_q_attr.tx_elem_size = 16;
	dma_q_attr.rx_elem_size = 16;
	dma_q_attr.rx_cb = get_vring_rx_cb;

	dma_q = snap_dma_q_create(pd, &dma_q_attr);
	if (!dma_q) {
		snap_error("failed to create dma_q for drv: 0x%lx dev: 0x%lx\n",
			   drv_addr, dev_addr);
		return -EINVAL;
	}

	ret = snap_dma_q_read_short(dma_q, vra, sizeof(struct vring_avail),
				drv_addr, dma_mkey, NULL);
	if (ret) {
		snap_error("failed DMA read vring_used for drv: 0x%lx dev: 0x%lx\n",
			   drv_addr, dev_addr);
		goto err;
	}

	ret = snap_dma_q_read_short(dma_q, vru, sizeof(struct vring_used),
			      dev_addr, dma_mkey, NULL);
	if (ret) {
		snap_error("failed DMA read vring_used for drv: 0x%lx dev: 0x%lx\n",
			   drv_addr, dev_addr);
		goto err;
	}

	ret = snap_dma_q_flush(dma_q);
	if (ret != 2)
		snap_error("failed flush drv: 0x%lx dev: 0x%lx, ret %d\n",
			   drv_addr, dev_addr, ret);

	ret = 0;

err:
	snap_dma_q_destroy(dma_q);

	return ret;
}

int snap_virtio_common_queue_config(struct snap_virtio_common_queue_attr *common_attr,
		uint16_t hw_available_index, uint16_t hw_used_index, struct snap_dma_q *dma_q)
{
	common_attr->hw_available_index = hw_available_index;
	common_attr->hw_used_index = hw_used_index;
	common_attr->qp = snap_dma_q_get_fw_qp(dma_q);
	if (!common_attr->qp) {
		snap_error("no fw qp exist when trying to create virtq\n");
		return -1;
	}
	common_attr->dma_q = dma_q;
	return 0;

}

struct virtq_q_ops *snap_virtio_queue_provider(void)
{
	struct virtq_q_ops *queue_provider_ops;
	int q_provider = snap_env_getenv(SNAP_QUEUE_PROVIDER);

	switch (q_provider) {
	case SNAP_HW_Q_PROVIDER:
		queue_provider_ops = get_hw_queue_ops();
		break;
	case SNAP_SW_Q_PROVIDER:
		queue_provider_ops = get_sw_queue_ops();
		break;
	case SNAP_DPA_Q_PROVIDER:
		queue_provider_ops = get_dpa_queue_ops();
		break;
	default:
		snap_error("Invalid Queue provider received %d\n", q_provider);
		queue_provider_ops = NULL;
		break;
	}

	return queue_provider_ops;
}
