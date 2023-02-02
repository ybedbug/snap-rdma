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


struct mlx5dv_devx_obj *
mlx_devx_create_eq(struct ibv_context *ctx, uint32_t dev_emu_id,
		   uint16_t msix_vector, uint32_t *eqn)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(create_emulated_dev_eq_in)] = {};
	uint8_t out[DEVX_ST_SZ_DW(general_obj_out_cmd_hdr) +
		    DEVX_ST_SZ_BYTES(create_eq_out)] = {};
	struct mlx5dv_devx_obj *eq = NULL;
	uint8_t *emu_dev_eq_in;

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_OBJ_TYPE_EMULATED_DEV_EQ);

	emu_dev_eq_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
	DEVX_SET(create_emulated_dev_eq_in, emu_dev_eq_in, device_emulation_id,
		 dev_emu_id);
	DEVX_SET(create_emulated_dev_eq_in, emu_dev_eq_in, intr, msix_vector);

	eq = mlx5dv_devx_obj_create(ctx, in, sizeof(in), out, sizeof(out));
	if (eq)
		*eqn = DEVX_GET(general_obj_out_cmd_hdr, out, obj_id);
	return eq;
}

void mlx_devx_destroy_eq(struct mlx5dv_devx_obj *obj)
{
	mlx5dv_devx_obj_destroy(obj);
}

int mlx_devx_allow_other_vhca_access(struct ibv_context *ibv_ctx,
				     struct vrdma_allow_other_vhca_access_attr *attr)
{
	uint32_t out[DEVX_ST_SZ_DW(allow_other_vhca_access_out)] = {};
	uint32_t in[DEVX_ST_SZ_DW(allow_other_vhca_access_in)] = {};
	void *access_key;
	int err;

	DEVX_SET(allow_other_vhca_access_in, in, opcode,
		 MLX5_CMD_OP_ALLOW_OTHER_VHCA_ACCESS);
	DEVX_SET(allow_other_vhca_access_in, in, object_type_to_be_accessed,
		 attr->type);
	DEVX_SET(allow_other_vhca_access_in, in, object_id_to_be_accessed,
		 attr->obj_id);
	access_key = DEVX_ADDR_OF(allow_other_vhca_access_in, in, access_key);
	memcpy(access_key, &attr->access_key_be,
	       VRDMA_ALIAS_ACCESS_KEY_NUM_DWORD * sizeof(uint32_t));

	err = mlx5dv_devx_general_cmd(ibv_ctx, in, sizeof(in), out, sizeof(out));
	if (err) {
		snap_debug("Failed to allow other VHCA access to object, err(%d)",
			  err);
		return err;
	}

	return 0;
}

struct mlx5dv_devx_obj *
mlx_devx_create_alias_obj(struct ibv_context *ctx,
			  struct vrdma_alias_attr *attr, uint32_t *id)
{
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)] = {};
	uint8_t in[DEVX_ST_SZ_BYTES(create_alias_in)] = {};
	struct mlx5dv_devx_obj *obj = NULL;
	void *alias_ctx, *access_key;

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type, attr->type);
	DEVX_SET(general_obj_in_cmd_hdr, in, alias_object, 0x1);

	alias_ctx = DEVX_ADDR_OF(create_alias_in, in, alias_ctx);
	DEVX_SET(alias_context, alias_ctx, vhca_id_to_be_accessed,
		 attr->orig_vhca_id);
	DEVX_SET(alias_context, alias_ctx, object_id_to_be_accessed,
		 attr->orig_obj_id);
	access_key = DEVX_ADDR_OF(alias_context, alias_ctx, access_key);
	memcpy(access_key, &attr->access_key_be,
	       VRDMA_ALIAS_ACCESS_KEY_NUM_DWORD * sizeof(uint32_t));

	obj = mlx5dv_devx_obj_create(ctx, in, sizeof(in), out, sizeof(out));
	if (!obj) {
		snap_debug("Failed to create an alias for object, err(%d)", errno);
		return NULL;
	}

	*id = DEVX_GET(general_obj_out_cmd_hdr, out, obj_id);

	return obj;
}

static int
mlx_devx_emu_db_to_cq_query(struct mlx5dv_devx_obj *obj, uint32_t *id,
			    uint32_t obj_id)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr) +
		    DEVX_ST_SZ_BYTES(dpa_db_cq_mapping)] = {0};
	uint8_t *cq_db_map_out;
	int ret;

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_QUERY_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_GENERAL_OBJECT_TYPES_DPA_DB_CQ_MAPPING);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_id, obj_id);

	ret = mlx5dv_devx_obj_query(obj, in, sizeof(in), out, sizeof(out));
	if (ret)
		goto err;

	cq_db_map_out = out + DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr);
	*id = DEVX_GET(dpa_db_cq_mapping, cq_db_map_out, dbr_handle);

err:
	return ret;
}

struct mlx5dv_devx_obj *
mlx_devx_emu_db_to_cq_map(struct ibv_context *ibv_ctx, uint32_t vhca_id,
			  uint32_t queue_id, uint32_t cq_num, uint32_t *id)
{
	uint32_t in[DEVX_ST_SZ_DW(create_dpa_db_cq_mapping_in)] = {};
	uint32_t out[DEVX_ST_SZ_DW(general_obj_out_cmd_hdr)] = {};
	void *hdr, *dpa_db_cq_mapping;
	struct mlx5dv_devx_obj *obj;
	uint32_t obj_id;
	int ret;

	hdr = DEVX_ADDR_OF(create_dpa_db_cq_mapping_in, in, hdr);
	DEVX_SET(general_obj_in_cmd_hdr, hdr, opcode,
	MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, hdr, obj_type,
		 MLX5_GENERAL_OBJECT_TYPES_DPA_DB_CQ_MAPPING);//PRM 8.28.48

	dpa_db_cq_mapping = DEVX_ADDR_OF(create_dpa_db_cq_mapping_in, in,
					 dpa_db_cq_mapping);
	DEVX_SET(dpa_db_cq_mapping, dpa_db_cq_mapping, map_state,
		 MLX5_DEV_DB_MAPPED);
	DEVX_SET(dpa_db_cq_mapping, dpa_db_cq_mapping, queue_type, 0);
	DEVX_SET(dpa_db_cq_mapping, dpa_db_cq_mapping, device_type,
		 MLX5_HOTPLUG_DEVICE_TYPE_VIRTIO_NET);
	DEVX_SET(dpa_db_cq_mapping, dpa_db_cq_mapping, device_emulation_id, vhca_id);
	DEVX_SET(dpa_db_cq_mapping, dpa_db_cq_mapping, queue_id, queue_id);
	DEVX_SET(dpa_db_cq_mapping, dpa_db_cq_mapping, cqn, cq_num);

	obj = mlx5dv_devx_obj_create(ibv_ctx, in,
				     sizeof(in), out, sizeof(out));
	if (!obj) {
		snap_debug("Failed to create dpa_db_cq_mapping PRM object, err(%d)",
			  errno);
		return NULL;
	}

	obj_id = DEVX_GET(general_obj_out_cmd_hdr, out, obj_id);

	ret = mlx_devx_emu_db_to_cq_query(obj, id, obj_id);
	if (ret) {
		snap_debug("Failed to query dpa_db_cq_mapping PRM object, err(%d)",
			  ret);
		goto err;
	}

	return obj;

err:
	mlx5dv_devx_obj_destroy(obj);
	return NULL;
}

int mlx_devx_emu_db_to_cq_unmap(struct mlx5dv_devx_obj *devx_emu_db_to_cq_ctx)
{
	int err;

	if (devx_emu_db_to_cq_ctx) {
		err = mlx5dv_devx_obj_destroy(devx_emu_db_to_cq_ctx);
		if (err) {
			snap_debug("Failed to destroy emu_db_to_cq_ctx devx object, err(%d)",
				  err);
			return err;
		}
		devx_emu_db_to_cq_ctx = NULL;
	}

	return 0;
}


