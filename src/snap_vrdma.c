/*
 * Copyright © 2022 NVIDIA CORPORATION & AFFILIATES. ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of Nvidia Corporation and its affiliates
 * (the "Company") and all right, title, and interest in and to the software
 * product, including all associated intellectual property rights, are and
 * shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 */

#include "snap_macros.h"
#include "snap_vrdma.h"
#include "snap_internal.h"
#include "mlx5_ifc.h"

static int snap_vrdma_query_device_internal(struct snap_device *sdev,
	uint8_t *out, int outlen)
{
	uint8_t in_net[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		DEVX_ST_SZ_BYTES(virtio_net_device_emulation)] = {0};
	uint8_t *in, *device_emulation_in;
	int inlen;

	if (sdev->pci->type != SNAP_VRDMA_PF)
		return -EINVAL;

	in = in_net;
	inlen = sizeof(in_net);
	device_emulation_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);

	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
			MLX5_OBJ_TYPE_VRDMA_DEVICE_EMULATION);
	DEVX_SET(virtio_net_device_emulation, device_emulation_in, vhca_id,
			sdev->pci->mpci.vhca_id);

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_QUERY_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_id,
		 sdev->mdev.device_emulation->obj_id);

	return mlx5dv_devx_obj_query(sdev->mdev.device_emulation->obj, in,
				     inlen, out, outlen);
}

#if 0
static void snap_vrdma_get_device_attr(struct snap_device *sdev,
				 struct snap_vrdma_device_attr *vattr,
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
#endif

/**
 * snap_vrdma_query_device() - Query an vRDMA snap device
 * @sdev:       snap device
 * @attr:       vRDMA snap device attr container (output)
 *
 * Query a vRDMA snap device. Attr argument must have enough space for
 * the output data.
 *
 * Return: Returns 0 in case of success and attr is filled.
 */
int snap_vrdma_query_device(struct snap_device *sdev,
	struct snap_vrdma_device_attr *attr)
{
	uint8_t *out;
	//struct snap_context *sctx = sdev->sctx;
	uint8_t *device_emulation_out;
	int ret, out_size;
	//uint64_t dev_allowed;

	out_size = DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(virtio_net_device_emulation);
	out = calloc(1, out_size);
	if (!out)
		return -ENOMEM;

	ret = snap_vrdma_query_device_internal(sdev, out, out_size);
	if (ret)
		goto out_free;

	device_emulation_out = out + DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr);

	snap_get_pci_attr(&sdev->pci->pci_attr,
			  DEVX_ADDR_OF(virtio_net_device_emulation,
				       device_emulation_out,
				       pci_params));
#if 0
	//attr->num_of_vfs = sdev->pci->pci_attr.num_of_vfs;
	attr->num_msix = sdev->pci->pci_attr.num_msix;
	snap_vrdma_get_device_attr(sdev, attr,
				    DEVX_ADDR_OF(virtio_net_device_emulation,
						 device_emulation_out,
						 virtio_device));
	snap_update_pci_bdf(sdev->pci, attr->pci_bdf);
	attr->enabled = DEVX_GET(virtio_net_device_emulation,
				       device_emulation_out, enabled);
	attr->reset = DEVX_GET(virtio_net_device_emulation,
				     device_emulation_out, reset);
	attr->modifiable_fields = 0;
	dev_allowed = DEVX_GET64(virtio_net_device_emulation,
				 device_emulation_out, modify_field_select);
	if (dev_allowed) {
		if (dev_allowed & MLX5_VIRTIO_DEVICE_MODIFY_STATUS)
			attr->modifiable_fields |= SNAP_VIRTIO_MOD_DEV_STATUS;
		if (dev_allowed & MLX5_VIRTIO_DEVICE_MODIFY_LINK)
			attr->modifiable_fields |= SNAP_VIRTIO_MOD_LINK_STATUS;
		if (dev_allowed & MLX5_VIRTIO_DEVICE_MODIFY_RESET)
			attr->modifiable_fields |= SNAP_VIRTIO_MOD_RESET;
		if (dev_allowed & MLX5_VIRTIO_DEVICE_MODIFY_PCI_COMMON_CFG)
			attr->modifiable_fields |= SNAP_VIRTIO_MOD_PCI_COMMON_CFG;
		if (dev_allowed & MLX5_VIRTIO_DEVICE_MODIFY_DEV_CFG)
			attr->modifiable_fields |= SNAP_VIRTIO_MOD_DEV_CFG;
		if (dev_allowed & MLX5_VIRTIO_DEVICE_MODIFY_ALL)
			attr->modifiable_fields |= SNAP_VIRTIO_MOD_ALL;
		if (dev_allowed & MLX5_VIRTIO_DEVICE_MODIFY_QUEUE_CFG)
			attr->modifiable_fields |= SNAP_VIRTIO_MOD_QUEUE_CFG;
		if (dev_allowed & MLX5_VIRTIO_DEVICE_MODIFY_NUM_MSIX)
			attr->modifiable_fields |= SNAP_VIRTIO_MOD_NUM_MSIX;
		if (dev_allowed & MLX5_VIRTIO_DEVICE_MODIFY_DYN_VF_MSIX_RESET)
			attr->modifiable_fields |= SNAP_VIRTIO_MOD_DYN_MSIX_RESET;
		if (dev_allowed & MLX5_VIRTIO_DEVICE_MODIFY_PCI_HOTPLUG_STATE)
			attr->modifiable_fields |= SNAP_VIRTIO_MOD_PCI_HOTPLUG_STATE;
		if (dev_allowed & MLX5_VIRTIO_DEVICE_MODIFY_VQ_CFG_Q_SIZE)
			attr->modifiable_fields |= SNAP_VIRTIO_MOD_VQ_CFG_Q_SIZE;
		if (dev_allowed & MLX5_VIRTIO_DEVICE_MODIFY_VQ_CFG_Q_MSIX_VECTOR)
			attr->modifiable_fields |= SNAP_VIRTIO_MOD_VQ_CFG_Q_MSIX_VECTOR;
		if (dev_allowed & MLX5_VIRTIO_DEVICE_MODIFY_VQ_CFG_Q_ENABLE)
			attr->modifiable_fields |= SNAP_VIRTIO_MOD_VQ_CFG_Q_ENABLE;
		if (dev_allowed & MLX5_VIRTIO_DEVICE_MODIFY_VQ_CFG_Q_NOTIFY_OFF)
			attr->modifiable_fields |= SNAP_VIRTIO_MOD_VQ_CFG_Q_NOTIFY_OFF;
		if (dev_allowed & MLX5_VIRTIO_DEVICE_MODIFY_VQ_CFG_Q_DESC)
			attr->modifiable_fields |= SNAP_VIRTIO_MOD_VQ_CFG_Q_DESC;
		if (dev_allowed & MLX5_VIRTIO_DEVICE_MODIFY_VQ_CFG_Q_DRIVER)
			attr->modifiable_fields |= SNAP_VIRTIO_MOD_VQ_CFG_Q_DRIVER;
		if (dev_allowed & MLX5_VIRTIO_DEVICE_MODIFY_VQ_CFG_Q_DEVICE)
			attr->modifiable_fields |= SNAP_VIRTIO_MOD_VQ_CFG_Q_DEVICE;
		if (dev_allowed & MLX5_VIRTIO_DEVICE_MODIFY_VQ_CFG_Q_RESET)
			attr->modifiable_fields |= SNAP_VIRTIO_MOD_VQ_CFG_Q_RESET;
	}
	attr->pci_hotplug_state = DEVX_GET(virtio_net_device_emulation,
						   device_emulation_out, pci_hotplug_state);
	attr->mtu = DEVX_GET(virtio_net_device_emulation,
			     device_emulation_out, virtio_net_config.mtu);
	attr->status = DEVX_GET(virtio_net_device_emulation,
				device_emulation_out, virtio_net_config.status);
	attr->max_queue_pairs = DEVX_GET(virtio_net_device_emulation,
					 device_emulation_out, virtio_net_config.max_virtqueue_pairs);
	attr->mac = (uint64_t)DEVX_GET(virtio_net_device_emulation,
				       device_emulation_out, virtio_net_config.mac_47_16) << 16;
	attr->mac |= DEVX_GET(virtio_net_device_emulation,
			      device_emulation_out, virtio_net_config.mac_15_0);
	attr->crossed_vhca_mkey = DEVX_GET(virtio_net_device_emulation,
					   device_emulation_out,
					   emulated_device_crossed_vhca_mkey);
#endif
out_free:
	free(out);
	return ret;
}

static int
snap_vrdma_get_modifiable_device_fields(struct snap_device *sdev)
{
	struct snap_vrdma_device_attr attr = {};
	int ret;

	ret = snap_vrdma_query_device(sdev, &attr);
	if (ret)
		return ret;

	sdev->mod_allowed_mask = attr.modifiable_fields;

	return 0;
}

/**
 * snap_vrdma_modify_device() - Modify vrdma snap device
 * @sdev:       snap device
 * @mask:       selected params to modify (mask of enum snap_vrdma_modify)
 * @attr:       attributes for the net device modify
 *
 * Modify vrdma snap device object according to a given mask.
 *
 * Return: 0 on success.
 */
int snap_vrdma_modify_device(struct snap_device *sdev, uint64_t mask,
		struct snap_vrdma_device_attr *attr)
{
	//uint64_t fields_to_modify = 0;
	//uint8_t *in;
	//int inlen;
	//uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)];
	//uint8_t *device_emulation_in;
	//int q_cnt = 0;
	int ret;

	if (!sdev->mod_allowed_mask) {
		ret = snap_vrdma_get_modifiable_device_fields(sdev);
		if (ret)
			return ret;
	}


	if (sdev->pci->type != SNAP_VRDMA_PF)
		return -EINVAL;

	//we'll modify only allowed fields
	snap_debug("mask 0x%0lx vs allowed 0x%0lx\n", mask, sdev->mod_allowed_mask);
	if (mask & ~sdev->mod_allowed_mask)
		return -EINVAL;
#if 0
	inlen = DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
	struct snap_virtio_net_device_attr *nattr = to_net_device_attr(attr);

	inlen += DEVX_ST_SZ_BYTES(virtio_net_device_emulation);
	in = calloc(1, inlen);
	if (!in)
		return -ENOMEM;

	device_emulation_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);

	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		MLX5_OBJ_TYPE_VRDMA_DEVICE_EMULATION);
	if (mask & (SNAP_VRDMA_MOD_DEV_STATUS |SNAP_VRDMA_MOD_ALL)) {
		fields_to_modify |= MLX5_VRDMA_DEVICE_MODIFY_STATUS;
		DEVX_SET(virtio_net_device_emulation, device_emulation_in,
			virtio_device.device_status, attr->status);
	}
	if (mask & (SNAP_VRDMA_MOD_LINK_STATUS | SNAP_VRDMA_MOD_ALL)) {
		fields_to_modify |= MLX5_VRDMA_DEVICE_MODIFY_LINK;
		DEVX_SET(virtio_net_device_emulation, device_emulation_in,
			virtio_net_config.status, nattr->status);
	}
	if (mask & (SNAP_VRDMA_MOD_RESET | SNAP_VRDMA_MOD_ALL)) {
		fields_to_modify |= MLX5_VRDMA_DEVICE_MODIFY_RESET;
		DEVX_SET(virtio_net_device_emulation, device_emulation_in,
			reset, attr->reset);
	}
	if (mask & (SNAP_VRDMA_MOD_NUM_MSIX | SNAP_VRDMA_MOD_ALL)) {
		void *pci_params;

		fields_to_modify |= MLX5_VRDMA_DEVICE_MODIFY_NUM_MSIX;
		pci_params = DEVX_ADDR_OF(virtio_net_device_emulation, device_emulation_in,
						  pci_params);
		DEVX_SET(device_pci_parameters, pci_params, num_msix, attr->num_msix);
		snap_debug("Setting SNAP_VIRTIO_MOD_NUM_MSIX, msix number: %d\n",
				   attr->num_msix);
	}

	if (mask & (SNAP_VRDMA_MOD_PCI_COMMON_CFG | SNAP_VIRTIO_MOD_ALL)) {
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
	}

	DEVX_SET64(virtio_net_device_emulation, device_emulation_in,
			   modify_field_select, fields_to_modify);

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_MODIFY_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_id,
		 sdev->mdev.device_emulation->obj_id);

	ret = snap_devx_obj_modify(sdev->mdev.device_emulation, in, inlen,
				   out, sizeof(out));

	snap_debug("snap_vrdma_modify_device ret %d in %p inlen %d modify 0x%0lx\n",
		ret, in, inlen, fields_to_modify);
	free(in);
#endif
	return ret;
}

/**
 * snap_vrdma_init_device() - Initialize a new snap device with vrdma
 *                                 characteristics
 * @sdev:       snap device
 *
 * Initialize a snap device for vrdma emulation. Allocate the needed
 * resources in the HCA and setup internal context.
 *
 * Return: Returns 0 in case of success.
 */
int snap_vrdma_init_device(struct snap_device *sdev)
{
	struct snap_vrdma_device *vndev;
	//int ret, i;
	int ret;

	if (sdev->pci->type != SNAP_VRDMA_PF)
		return -EINVAL;

	vndev = calloc(1, sizeof(*vndev));
	if (!vndev)
		return -ENOMEM;

#if 0
	vndev->num_queues = sdev->sctx->virtio_net_caps.max_emulated_virtqs;

	vndev->virtqs = calloc(vndev->num_queues, sizeof(*vndev->virtqs));
	if (!vndev->virtqs) {
		ret = -ENOMEM;
		goto out_free;
	}

	for (i = 0; i < vndev->num_queues; i++) {
		vndev->virtqs[i].vndev = vndev;
		vndev->virtqs[i].virtq.ctrs_obj =
					snap_virtio_create_queue_counters(sdev);
		if (!vndev->virtqs[i].virtq.ctrs_obj) {
			ret = -ENODEV;
			goto out_free_qctrs;
		}
	}
#endif

	ret = snap_init_device(sdev);
	if (ret)
		//goto out_free_virtqs;
		goto out_free;

	sdev->dd_data = vndev;

	return 0;

#if 0
out_free_qctrs:
	for (i = 0; i < vndev->num_queues; i++)
		if (vndev->virtqs[i].virtq.ctrs_obj)
			snap_devx_obj_destroy(vndev->virtqs[i].virtq.ctrs_obj);
out_free_virtqs:
	free(vndev->virtqs);
#endif
out_free:
	free(vndev);
	return ret;
}

/**
 * snap_vrdma_teardown_device() - Teardown vrdma specifics from a
 *                                     snap device
 * @sdev:       snap device
 *
 * Teardown and free vrdma context from a snap device.
 *
 * Return: Returns 0 in case of success.
 */
int snap_vrdma_teardown_device(struct snap_device *sdev)
{
	struct snap_vrdma_device *vdev;
	//int ret = 0, i;
	int ret = 0;

	vdev = (struct snap_vrdma_device *)sdev->dd_data;
	if (sdev->pci->type != SNAP_VRDMA_PF)
		return -EINVAL;
	sdev->dd_data = NULL;

	ret = snap_teardown_device(sdev);
#if 0
	for (i = 0; i < vndev->num_queues && vndev->virtqs[i].virtq.ctrs_obj; i++) {
		ret = snap_devx_obj_destroy(vndev->virtqs[i].virtq.ctrs_obj);
		if (ret)
			snap_error("Failed to destroy net virtq counter obj\n");
	}

	free(vndev->virtqs);
#endif
	free(vdev);

	return ret;
}

/**
 * snap_vrdma_pci_functions_cleanup() - Remove remaining hot-unplugged vrdma functions
 * @sctx:       snap_context for vrdma pfs
 *
 * Go over vrdma pfs and check their hotunplug state.
 * Complete hot-unplug for any pf with state POWER_OFF or HOTUNPLUG_PREPARE.
 *
 * Return: void.
 */
void snap_vrdma_pci_functions_cleanup(struct snap_context *sctx)
{
	struct snap_pci **pfs;
	int num_pfs, i;
	struct snap_vrdma_device_attr attr = {};
	struct snap_device_attr sdev_attr = {};
	struct snap_device *sdev;

	if (sctx->vrdma_pfs.max_pfs <= 0)
		return;

	pfs = calloc(sctx->vrdma_pfs.max_pfs, sizeof(*pfs));
	if (!pfs)
		return;

	sdev = calloc(1, sizeof(*sdev));
	if (!sdev) {
		free(pfs);
		return;
	}

	num_pfs = snap_get_pf_list(sctx, SNAP_VRDMA, pfs);
	for (i = 0; i < num_pfs; i++) {
		if (!pfs[i]->hotplugged)
			continue;
		sdev->sctx = sctx;
		sdev->pci = pfs[i];
		sdev->mdev.device_emulation = snap_emulation_device_create(sdev, &sdev_attr);
		if (!sdev->mdev.device_emulation) {
			snap_error("Failed to create device emulation\n");
			goto err;
		}

		snap_vrdma_query_device(sdev, &attr);
		snap_emulation_device_destroy(sdev);
		/*
		 * We rely on the driver to clean itself up.
		 * If the state is POWER OFF or PREPARE we need to unplug the function.
		 */
		if (attr.pci_hotplug_state == MLX5_EMULATION_HOTPLUG_STATE_POWER_OFF ||
			attr.pci_hotplug_state == MLX5_EMULATION_HOTPLUG_STATE_HOTUNPLUG_PREPARE)
			snap_hotunplug_pf(pfs[i]);

		snap_debug("hotplug virtio net function pf id =%d bdf=%02x:%02x.%d with state %d.\n",
			  pfs[i]->id, pfs[i]->pci_bdf.bdf.bus, pfs[i]->pci_bdf.bdf.device,
			  pfs[i]->pci_bdf.bdf.function, attr.pci_hotplug_state);
	}

err:
	free(pfs);
	free(sdev);
}
