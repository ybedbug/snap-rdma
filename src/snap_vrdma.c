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
#include <sys/time.h>
#include "snap_macros.h"
#include "snap_vrdma.h"
#include "vrdma/snap_vrdma_ctrl.h"
#include "snap_internal.h"
#include "mlx5_ifc.h"

struct snap_vrdma_test_dummy_device g_bar_test;

static int snap_vrdma_query_device_internal(struct snap_device *sdev,
	uint8_t *out, int outlen)
{
	uint8_t in_net[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		DEVX_ST_SZ_BYTES(vrdma_device_emulation)] = {0};
	uint8_t *in, *device_emulation_in;
	int inlen;

	if (sdev->pci->type != SNAP_VRDMA_PF)
		return -EINVAL;

	in = in_net;
	inlen = sizeof(in_net);
	device_emulation_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);

	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
			MLX5_OBJ_TYPE_VRDMA_DEVICE_EMULATION);
	/*lizh TBD: need check ???*/
	DEVX_SET(vrdma_device_emulation, device_emulation_in, vhca_id,
			sdev->pci->mpci.vhca_id);

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_QUERY_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_id,
		 sdev->mdev.device_emulation->obj_id);

	return mlx5dv_devx_obj_query(sdev->mdev.device_emulation->obj, in,
				     inlen, out, outlen);
}

static void snap_vrdma_get_device_attr(struct snap_device *sdev,
				 struct snap_vrdma_device_attr *vattr,
				 void *device_configuration)
{
	vattr->msix_config = DEVX_GET(vrdma_device, device_configuration,
				      msix_config);
	vattr->pci_bdf = DEVX_GET(vrdma_device, device_configuration,
				  pci_bdf);
	vattr->status = DEVX_GET(vrdma_device, device_configuration,
				 device_status);
}

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
	uint8_t *device_emulation_out;
	int ret, out_size;
	uint64_t dev_allowed;

	out_size = DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(vrdma_device_emulation);
	out = calloc(1, out_size);
	if (!out)
		return -ENOMEM;

	ret = snap_vrdma_query_device_internal(sdev, out, out_size);
	if (ret)
		goto out_free;

	device_emulation_out = out + DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr);

	snap_get_pci_attr(&sdev->pci->pci_attr,
			  DEVX_ADDR_OF(vrdma_device_emulation,
				       device_emulation_out,
				       pci_params));

	attr->num_msix = sdev->pci->pci_attr.num_msix;
	snap_vrdma_get_device_attr(sdev, attr,
				    DEVX_ADDR_OF(vrdma_device_emulation,
						 device_emulation_out,
						 vrdma_device));
	snap_update_pci_bdf(sdev->pci, attr->pci_bdf);
	attr->enabled = DEVX_GET(vrdma_device_emulation,
				       device_emulation_out, enabled);
	attr->reset = DEVX_GET(vrdma_device_emulation,
				     device_emulation_out, reset);
	attr->modifiable_fields = 0;
	dev_allowed = DEVX_GET64(vrdma_device_emulation,
				 device_emulation_out, modify_field_select);
	if (dev_allowed) {
		if (dev_allowed & MLX5_VRDMA_DEVICE_MODIFY_STATUS)
			attr->modifiable_fields |= SNAP_VRDMA_MOD_DEV_STATUS;
		if (dev_allowed & MLX5_VRDMA_DEVICE_MODIFY_RESET)
			attr->modifiable_fields |= SNAP_VRDMA_MOD_RESET;
		if (dev_allowed & MLX5_VRDMA_DEVICE_MODIFY_MAC)
			attr->modifiable_fields |= SNAP_VRDMA_MOD_MAC;
	}
	attr->mac = (uint64_t)DEVX_GET(vrdma_device_emulation,
				       device_emulation_out, vrdma_config.mac_47_16) << 16;
	attr->mac |= DEVX_GET(vrdma_device_emulation,
			      device_emulation_out, vrdma_config.mac_15_0);
	attr->mtu = DEVX_GET(vrdma_device_emulation,
			      device_emulation_out, vrdma_config.mtu);
	attr->crossed_vhca_mkey = DEVX_GET(vrdma_device_emulation,
					   device_emulation_out,
					   emulated_device_crossed_vhca_mkey);
	attr->adminq_msix_vector = DEVX_GET(vrdma_device_emulation,
			      device_emulation_out, vrdma_adminq_config.adminq_msix_vector);
	attr->adminq_size = DEVX_GET(vrdma_device_emulation,
			      device_emulation_out, vrdma_adminq_config.adminq_size);
	attr->adminq_nodify_off = DEVX_GET(vrdma_device_emulation,
			      device_emulation_out, vrdma_adminq_config.adminq_notify_off);
	attr->adminq_base_addr = DEVX_GET64(vrdma_device_emulation,
				  device_emulation_out, vrdma_adminq_config.adminq_base_addr);
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
	uint64_t fields_to_modify = 0;
	uint8_t *in;
	int inlen;
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)];
	uint8_t *device_emulation_in;
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

	inlen = DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);

	inlen += DEVX_ST_SZ_BYTES(vrdma_device_emulation);
	in = calloc(1, inlen);
	if (!in)
		return -ENOMEM;

	device_emulation_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);

	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		MLX5_OBJ_TYPE_VRDMA_DEVICE_EMULATION);
	if (mask & (SNAP_VRDMA_MOD_DEV_STATUS)) {
		fields_to_modify |= MLX5_VRDMA_DEVICE_MODIFY_STATUS;
		DEVX_SET(vrdma_device_emulation, device_emulation_in,
			vrdma_device.device_status, attr->status);
	}
	if (mask & (SNAP_VRDMA_MOD_RESET)) {
		fields_to_modify |= MLX5_VRDMA_DEVICE_MODIFY_RESET;
		DEVX_SET(vrdma_device_emulation, device_emulation_in,
			reset, attr->reset);
	}
	if (mask & (SNAP_VRDMA_MOD_MAC)) {
		fields_to_modify |= MLX5_VRDMA_DEVICE_MODIFY_MAC;
		DEVX_SET(vrdma_device_emulation, device_emulation_in,
			vrdma_config.mac_47_16, attr->mac >> 16);
		DEVX_SET(vrdma_device_emulation, device_emulation_in,
			vrdma_config.mac_15_0, attr->mac & 0xffff);
	}
	DEVX_SET64(vrdma_device_emulation, device_emulation_in,
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
	return ret;
}

static inline void eth_random_addr(uint8_t *addr)
{
	struct timeval t;
	uint64_t rand;

	gettimeofday(&t, NULL);
	srandom(t.tv_sec + t.tv_usec);
	rand = random();

	rand = rand << 32 | random();

	memcpy(addr, (uint8_t *)&rand, 6);
	addr[0] &= 0xfe;        /* clear multicast bit */
	addr[0] |= 0x02;        /* set local assignment bit (IEEE802) */
}

int snap_vrdma_device_mac_init(struct snap_vrdma_ctrl *ctrl)
{
	struct snap_device *sdev = ctrl->sdev;
	struct snap_vrdma_device_attr vattr = {};
	uint8_t *vmac;
	int ret;

	ret = snap_vrdma_query_device(sdev, &vattr);
	if (ret)
		return -1;
	if (ctrl->mac) {
		vattr.mac = ctrl->mac;
	} else {
		vmac = (uint8_t *)&vattr.mac;
		eth_random_addr(&vmac[2]);
		vattr.mac = be64toh(vattr.mac);
	}
	ret = snap_vrdma_modify_device(sdev, SNAP_VRDMA_MOD_MAC, &vattr);
	if (ret)
		ret = -1;
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
int snap_vrdma_init_device(struct snap_device *sdev, uint32_t vdev_idx)
{
	struct snap_vrdma_device *vdev;
	int ret;

	if (sdev->pci->type != SNAP_VRDMA_PF)
		return -EINVAL;

	vdev = calloc(1, sizeof(*vdev));
	if (!vdev)
		return -ENOMEM;
	vdev->vdev_idx = vdev_idx;

	ret = snap_init_device(sdev);
	if (ret)
		goto out_free;

	sdev->dd_data = vdev;
	memset(&g_bar_test, 0, sizeof(struct snap_vrdma_test_dummy_device));
	snap_error("lizh snap_vrdma_init_device...done");
	return 0;

out_free:
	free(vdev);
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

	snap_error("\nlizh snap_vrdma_pci_functions_cleanup max_pfs %d\n", sctx->vrdma_pfs.max_pfs);
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
		snap_error("\n lizh snap_vrdma_pci_functions_cleanup i %d num_pfs %d \n", i, num_pfs);
		if (!pfs[i])
			snap_error("\n lizh snap_vrdma_pci_functions_cleanup pfs[%d] is NULL \n", i);
		snap_error("\n lizh snap_vrdma_pci_functions_cleanup i %d hotplugged %d \n", i, pfs[i]->hotplugged);
		if (!pfs[i]->hotplugged)
			continue;
		snap_error("\n lizh snap_vrdma_pci_functions_cleanup num_pfs %d \n", num_pfs);
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
		/*if (attr.pci_hotplug_state == MLX5_EMULATION_HOTPLUG_STATE_POWER_OFF ||
			attr.pci_hotplug_state == MLX5_EMULATION_HOTPLUG_STATE_HOTUNPLUG_PREPARE)
			snap_hotunplug_pf(pfs[i]);*/

		snap_debug("hotplug virtio net function pf id =%d bdf=%02x:%02x.%d.\n",
			  pfs[i]->id, pfs[i]->pci_bdf.bdf.bus, pfs[i]->pci_bdf.bdf.device,
			  pfs[i]->pci_bdf.bdf.function);
	}

err:
	free(pfs);
	free(sdev);
}
