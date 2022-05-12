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

#include "snap_virtio_net.h"
#include "snap_internal.h"
#include "mlx5_ifc.h"

/**
 * snap_virtio_net_query_device() - Query an Virtio net snap device
 * @sdev:       snap device
 * @attr:       Virtio net snap device attr container (output)
 *
 * Query a Virtio net snap device. Attr argument must have enough space for
 * the output data.
 *
 * Return: Returns 0 in case of success and attr is filled.
 */
int snap_virtio_net_query_device(struct snap_device *sdev,
	struct snap_virtio_net_device_attr *attr)
{
	uint8_t *out;
	struct snap_context *sctx = sdev->sctx;
	uint8_t *device_emulation_out;
	int i, ret, out_size;
	uint64_t dev_allowed;

	if (attr->queues > sctx->virtio_net_caps.max_emulated_virtqs)
		return -EINVAL;

	out_size = DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(virtio_net_device_emulation) +
		   attr->queues * DEVX_ST_SZ_BYTES(virtio_q_layout);
	out = calloc(1, out_size);
	if (!out)
		return -ENOMEM;

	ret = snap_virtio_query_device(sdev, SNAP_VIRTIO_NET, out, out_size);
	if (ret)
		goto out_free;

	device_emulation_out = out + DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr);

	snap_get_pci_attr(&sdev->pci->pci_attr,
			  DEVX_ADDR_OF(virtio_net_device_emulation,
				       device_emulation_out,
				       pci_params));

	attr->vattr.num_of_vfs = sdev->pci->pci_attr.num_of_vfs;
	attr->vattr.num_msix = sdev->pci->pci_attr.num_msix;
	snap_virtio_get_device_attr(sdev, &attr->vattr,
				    DEVX_ADDR_OF(virtio_net_device_emulation,
						 device_emulation_out,
						 virtio_device));
	if (attr->queues) {
		for (i = 0; i < attr->queues; i++)
			snap_virtio_get_queue_attr(&attr->q_attrs[i].vattr,
						   DEVX_ADDR_OF(virtio_net_device_emulation,
								device_emulation_out,
								virtio_q_configuration[i]));
	}

	snap_update_pci_bdf(sdev->pci, attr->vattr.pci_bdf);

	attr->vattr.enabled = DEVX_GET(virtio_net_device_emulation,
				       device_emulation_out, enabled);
	attr->vattr.reset = DEVX_GET(virtio_net_device_emulation,
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
	}
	attr->vattr.num_free_dynamic_vfs_msix = DEVX_GET(virtio_net_device_emulation,
						   device_emulation_out, num_free_dynamic_vfs_msix);
	attr->vattr.pci_hotplug_state = DEVX_GET(virtio_net_device_emulation,
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

out_free:
	free(out);
	return ret;
}

static int
snap_virtio_net_get_modifiable_device_fields(struct snap_device *sdev)
{
	struct snap_virtio_net_device_attr attr = {};
	int ret;

	ret = snap_virtio_net_query_device(sdev, &attr);
	if (ret)
		return ret;

	sdev->mod_allowed_mask = attr.modifiable_fields;

	return 0;
}

/**
 * snap_virtio_net_modify_device() - Modify Virtio net snap device
 * @sdev:       snap device
 * @mask:       selected params to modify (mask of enum snap_virtio_dev_modify)
 * @attr:       attributes for the net device modify
 *
 * Modify Virtio net snap device object according to a given mask.
 *
 * Return: 0 on success.
 */
int snap_virtio_net_modify_device(struct snap_device *sdev, uint64_t mask,
		struct snap_virtio_net_device_attr *attr)
{
	int ret;

	if (!sdev->mod_allowed_mask) {
		ret = snap_virtio_net_get_modifiable_device_fields(sdev);
		if (ret)
			return ret;
	}

	return snap_virtio_modify_device(sdev, SNAP_VIRTIO_NET, mask,
					 &attr->vattr);
}

/**
 * snap_virtio_net_init_device() - Initialize a new snap device with VIRTIO
 *                                 net characteristics
 * @sdev:       snap device
 *
 * Initialize a snap device for Virtio net emulation. Allocate the needed
 * resources in the HCA and setup internal context.
 *
 * Return: Returns 0 in case of success.
 */
int snap_virtio_net_init_device(struct snap_device *sdev)
{
	struct snap_virtio_net_device *vndev;
	int ret, i;

	if (sdev->pci->type != SNAP_VIRTIO_NET_PF &&
	    sdev->pci->type != SNAP_VIRTIO_NET_VF)
		return -EINVAL;

	vndev = calloc(1, sizeof(*vndev));
	if (!vndev)
		return -ENOMEM;

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

	ret = snap_init_device(sdev);
	if (ret)
		goto out_free_virtqs;

	sdev->dd_data = vndev;

	return 0;

out_free_qctrs:
	for (i = 0; i < vndev->num_queues; i++)
		if (vndev->virtqs[i].virtq.ctrs_obj)
			snap_devx_obj_destroy(vndev->virtqs[i].virtq.ctrs_obj);
out_free_virtqs:
	free(vndev->virtqs);
out_free:
	free(vndev);
	return ret;
}

/**
 * snap_virtio_net_teardown_device() - Teardown Virtio net specifics from a
 *                                     snap device
 * @sdev:       snap device
 *
 * Teardown and free Virtio net context from a snap device.
 *
 * Return: Returns 0 in case of success.
 */
int snap_virtio_net_teardown_device(struct snap_device *sdev)
{
	struct snap_virtio_net_device *vndev;
	int ret = 0, i;

	vndev = (struct snap_virtio_net_device *)sdev->dd_data;
	if (sdev->pci->type != SNAP_VIRTIO_NET_PF &&
	    sdev->pci->type != SNAP_VIRTIO_NET_VF)
		return -EINVAL;

	sdev->dd_data = NULL;

	ret = snap_teardown_device(sdev);

	for (i = 0; i < vndev->num_queues && vndev->virtqs[i].virtq.ctrs_obj; i++) {
		ret = snap_devx_obj_destroy(vndev->virtqs[i].virtq.ctrs_obj);
		if (ret)
			snap_error("Failed to destroy net virtq counter obj\n");
	}

	free(vndev->virtqs);
	free(vndev);

	return ret;
}

/**
 * snap_virtio_net_query_counters() - Query a Virtio net queue cnt
 * @vnq:        snap Virtio net queue
 * @q_cnt:      cnt for the queue (output)
 *
 * Query a Virtio net snap queue cnt object.
 *
 * Return: 0 on success, and cnt is filled with the query result.
 */
int snap_virtio_net_query_counters(struct snap_virtio_net_queue *vnq,
				struct snap_virtio_queue_counters_attr *q_cnt)
{
	struct mlx5_snap_devx_obj  *cnt_obj;
	int ret = 0;

	if (!vnq || !vnq->virtq.ctrs_obj)
		return 0;

	cnt_obj = vnq->virtq.ctrs_obj;

	memset(q_cnt, 0, sizeof(*q_cnt));
	ret = snap_virtio_query_queue_counters(cnt_obj, q_cnt);
	return ret;
}

/**
 * snap_virtio_net_query_queue() - Query a Virtio net queue object
 * @vnq:        snap Virtio net queue
 * @attr:       attributes for the queue query (output)
 *
 * Query a Virtio net snap queue object.
 *
 * Return: 0 on success, and attr is filled with the query result.
 */
int snap_virtio_net_query_queue(struct snap_virtio_net_queue *vnq,
		struct snap_virtio_net_queue_attr *attr)
{
	return snap_virtio_query_queue(&vnq->virtq, &attr->vattr);
}

/**
 * snap_virtio_net_create_queue() - Create a new Virtio net snap queue object
 * @sdev:       snap device
 * @attr:       attributes for the queue creation
 *
 * Create a Virtio net snap queue object with the given attributes.
 *
 * Return: Returns snap_virtio_net_queue in case of success, NULL otherwise and
 * errno will be set to indicate the failure reason.
 */
struct snap_virtio_net_queue*
snap_virtio_net_create_queue(struct snap_device *sdev,
	struct snap_virtio_net_queue_attr *attr)
{
	struct snap_virtio_net_device *vndev;
	struct snap_virtio_net_queue *vnq;
	int ret;

	vndev = (struct snap_virtio_net_device *)sdev->dd_data;

	if (attr->vattr.idx >= vndev->num_queues) {
		errno = EINVAL;
		goto out;
	}

	vnq = &vndev->virtqs[attr->vattr.idx];

	ret = snap_virtio_create_hw_queue(sdev, &vnq->virtq,
					&sdev->sctx->virtio_net_caps,
					&attr->vattr);
	if (ret) {
		snap_error("Failed to create hw queue, err(%d)\n", ret);
		return NULL;
	}

	return vnq;

out:
	return NULL;
}

/**
 * snap_virtio_net_destroy_queue() - Destroy Virtio net queue object
 * @vnq:       Virtio net queue
 *
 * Destroy and free a snap virtio net queue context.
 *
 * Return: Returns 0 on success.
 */
int snap_virtio_net_destroy_queue(struct snap_virtio_net_queue *vnq)
{
	return snap_virtio_destroy_hw_queue(&vnq->virtq);
}

static int
snap_virtio_net_get_modifiable_virtq_fields(struct snap_virtio_net_queue *vnq)
{
	struct snap_virtio_net_queue_attr attr = {};
	int ret;

	ret = snap_virtio_net_query_queue(vnq, &attr);
	if (ret)
		return ret;

	vnq->virtq.mod_allowed_mask = attr.modifiable_fields;

	return 0;
}

/**
 * snap_virtio_net_modify_queue() - Modify a Virtio net queue object
 * @vnq:        snap Virtio net queue
 * @mask:       selected params to modify (mask of enum
 *              snap_virtio_net_queue_modify)
 * @attr:       attributes for the virtq modify
 *
 * Modify a Virtio net queue snap object according to a given mask.
 *
 * Return: 0 on success.
 */
int snap_virtio_net_modify_queue(struct snap_virtio_net_queue *vnq,
		uint64_t mask, struct snap_virtio_net_queue_attr *attr)
{
	int ret;

	if (!vnq->virtq.mod_allowed_mask) {
		ret = snap_virtio_net_get_modifiable_virtq_fields(vnq);
		if (ret)
			return ret;
	}

	return snap_virtio_modify_queue(&vnq->virtq, mask, &attr->vattr);
}

/**
 * snap_virtio_net_pci_functions_cleanup() - Remove remaining hot-unplugged virtio_net functions
 * @sctx:       snap_context for virtio_net pfs
 *
 * Go over virtio_net pfs and check their hotunplug state.
 * Complete hot-unplug for any pf with state POWER_OFF or HOTUNPLUG_PREPARE.
 *
 * Return: void.
 */
void snap_virtio_net_pci_functions_cleanup(struct snap_context *sctx)
{
	struct snap_pci **pfs;
	int num_pfs, i;
	struct snap_virtio_net_device_attr attr = {};
	struct snap_device_attr sdev_attr = {};
	struct snap_device *sdev;

	if (sctx->virtio_net_pfs.max_pfs <= 0)
		return;

	pfs = calloc(sctx->virtio_net_pfs.max_pfs, sizeof(*pfs));
	if (!pfs)
		return;

	sdev = calloc(1, sizeof(*sdev));
	if (!sdev) {
		free(pfs);
		return;
	}

	num_pfs = snap_get_pf_list(sctx, SNAP_VIRTIO_NET, pfs);
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

		snap_virtio_net_query_device(sdev, &attr);
		snap_emulation_device_destroy(sdev);
		/*
		 * We rely on the driver to clean itself up.
		 * If the state is POWER OFF or PREPARE we need to unplug the function.
		 */
		if (attr.vattr.pci_hotplug_state == MLX5_EMULATION_HOTPLUG_STATE_POWER_OFF ||
			attr.vattr.pci_hotplug_state == MLX5_EMULATION_HOTPLUG_STATE_HOTUNPLUG_PREPARE)
			snap_hotunplug_pf(pfs[i]);

		snap_debug("hotplug virtio net function pf id =%d bdf=%02x:%02x.%d with state %d.\n",
			  pfs[i]->id, pfs[i]->pci_bdf.bdf.bus, pfs[i]->pci_bdf.bdf.device,
			  pfs[i]->pci_bdf.bdf.function, attr.vattr.pci_hotplug_state);
	}

err:
	free(pfs);
	free(sdev);
}
