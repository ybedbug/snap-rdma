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

#include "snap.h"
#include "snap_nvme.h"
#include "snap_virtio_blk.h"
#include "snap_virtio_fs.h"
#include "snap_virtio_net.h"
#include "snap_queue.h"
#include "snap_internal.h"
#include "snap_env.h"

#include "mlx5_ifc.h"

#define SNAP_INITIALIZE_HCA_RETRY_CNT 100
#define SNAP_TEARDOWN_HCA_RETRY_CNT 5
#define SNAP_GENERAL_CMD_USEC_WAIT 50000
#define SNAP_PCI_ENUMERATE_TIME_WAIT 50000
#define SNAP_PCI_ENUMERATE_MAX_RETRIES 100
#define SNAP_UNINITIALIZED_VHCA_ID -1
#define SNAP_NVME_MAX_QUEUE_DEPTH_LEGACY 1024

static int snap_copy_roce_address(struct snap_device *sdev,
		struct ibv_context *context, int idx);

static int snap_query_functions_info(struct snap_context *sctx,
		enum snap_emulation_type type, int vhca_id, uint8_t *out, int outlen);

static int snap_general_tunneled_cmd(struct snap_device *sdev, void *in,
		size_t inlen, void *out, size_t outlen, int retries)
{
	struct ibv_context *context = sdev->sctx->context;
	int ret = -EINVAL, retry_count = -1;

	/* This operation is allowed only for tunneled PCI functions */
	if (!sdev->mdev.vtunnel)
		return ret;

	DEVX_SET(vhca_tunnel_cmd, in, vhca_tunnel_id,
		 sdev->mdev.vtunnel->obj_id);

retry:
	if (retry_count == retries)
		return ret;

	memset(out, 0, outlen);
	ret = mlx5dv_devx_general_cmd(context, in, inlen, out, outlen);
	if (ret) {
		retry_count++;
		usleep(SNAP_GENERAL_CMD_USEC_WAIT);
		goto retry;
	}

	return 0;
}

static int snap_enable_hca(struct snap_device *sdev)
{
	uint8_t in[DEVX_ST_SZ_BYTES(enable_hca_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(enable_hca_out)] = {0};

	DEVX_SET(enable_hca_in, in, opcode, MLX5_CMD_OP_ENABLE_HCA);

	return snap_general_tunneled_cmd(sdev, in, sizeof(in), out,
					 sizeof(out),
					 SNAP_INITIALIZE_HCA_RETRY_CNT);
}

static int snap_disable_hca(struct snap_device *sdev)
{
	uint8_t in[DEVX_ST_SZ_BYTES(disable_hca_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(disable_hca_out)] = {0};

	DEVX_SET(disable_hca_in, in, opcode, MLX5_CMD_OP_DISABLE_HCA);

	return snap_general_tunneled_cmd(sdev, in, sizeof(in), out,
					 sizeof(out),
					 SNAP_TEARDOWN_HCA_RETRY_CNT);
}

static int snap_init_hca(struct snap_device *sdev)
{
	uint8_t in[DEVX_ST_SZ_BYTES(init_hca_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(init_hca_out)] = {0};

	DEVX_SET(init_hca_in, in, opcode, MLX5_CMD_OP_INIT_HCA);

	return snap_general_tunneled_cmd(sdev, in, sizeof(in), out,
					 sizeof(out),
					 SNAP_INITIALIZE_HCA_RETRY_CNT);
}

static int snap_teardown_hca(struct snap_device *sdev)
{
	uint8_t in[DEVX_ST_SZ_BYTES(teardown_hca_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(teardown_hca_out)] = {0};

	DEVX_SET(teardown_hca_in, in, opcode, MLX5_CMD_OP_TEARDOWN_HCA);

	return snap_general_tunneled_cmd(sdev, in, sizeof(in), out,
					 sizeof(out),
					 SNAP_TEARDOWN_HCA_RETRY_CNT);
}

static void snap_free_pci_bar(struct snap_pci *pci)
{
	if (pci->bar.data) {
		free(pci->bar.data);
		pci->bar.size = 0;
	}
}

static int snap_alloc_pci_bar(struct snap_pci *pci)
{
	switch (pci->type) {
	case SNAP_NVME_PF:
	case SNAP_NVME_VF:
		pci->bar.data = calloc(1, pci->sctx->nvme_caps.reg_size);
		if (!pci->bar.data)
			return -ENOMEM;
		pci->bar.size = pci->sctx->nvme_caps.reg_size;
		break;
	case SNAP_VIRTIO_NET_PF:
	case SNAP_VIRTIO_NET_VF:
	case SNAP_VIRTIO_BLK_PF:
	case SNAP_VIRTIO_BLK_VF:
	case SNAP_VIRTIO_FS_PF:
	case SNAP_VIRTIO_FS_VF:
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static void snap_free_virtual_functions(struct snap_pci *pf)
{
	int i;

	if ((pf->vfs == NULL) || (pf->num_vfs == 0))
		return;

	for (i = 0; i < pf->num_vfs ; i++)
		snap_free_pci_bar(&pf->vfs[i]);
	free(pf->vfs);
	pf->vfs = NULL;
}

static enum snap_pci_type snap_pf_to_vf_type(enum snap_pci_type pf_type)
{
	if (pf_type == SNAP_NVME_PF)
		return SNAP_NVME_VF;
	else if (pf_type == SNAP_VIRTIO_NET_PF)
		return SNAP_VIRTIO_NET_VF;
	else if (pf_type == SNAP_VIRTIO_BLK_PF)
		return SNAP_VIRTIO_BLK_VF;
	else if (pf_type == SNAP_VIRTIO_FS_PF)
		return SNAP_VIRTIO_FS_VF;
	else
		return pf_type;
}

static enum snap_pci_type
snap_emulation_type_to_pf_type(enum snap_emulation_type type)
{
	switch (type) {
	case SNAP_NVME:
		return SNAP_NVME_PF;
	case SNAP_VIRTIO_NET:
		return SNAP_VIRTIO_NET_PF;
	case SNAP_VIRTIO_BLK:
		return SNAP_VIRTIO_BLK_PF;
	case SNAP_VIRTIO_FS:
		return SNAP_VIRTIO_FS_PF;
	default:
		return SNAP_NONE_PF;
	}
}

static bool snap_query_vuid_is_supported(struct ibv_context *context)
{
	uint8_t in[DEVX_ST_SZ_BYTES(query_hca_cap_in)] = {};
	uint8_t out[DEVX_ST_SZ_BYTES(query_hca_cap_out)] = {};
	int ret;

	DEVX_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	DEVX_SET(query_hca_cap_in, in, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_GENERAL_DEVICE2);
	ret = mlx5dv_devx_general_cmd(context, in, sizeof(in),
				      out, sizeof(out));
	if (ret)
		return false;

	return DEVX_GET(query_hca_cap_out, out,
	       capability.cmd_hca_cap2.query_vuid);
}

static int snap_query_vuid(struct snap_pci *pci, uint8_t *out, int outlen, bool query_vfs)
{
	int ret, num_entries;
	uint8_t in[DEVX_ST_SZ_BYTES(query_vuid_in)] = {0};

	if (!pci->sctx->vuid_supported)
		return -1;

	DEVX_SET(query_vuid_in, in, opcode, MLX5_CMD_OP_QUERY_VUID);
	DEVX_SET(query_vuid_in, in, vhca_id, pci->mpci.vhca_id);
	DEVX_SET(query_vuid_in, in, query_vfs_vuid, query_vfs);

	ret = mlx5dv_devx_general_cmd(pci->sctx->context, in, sizeof(in), out, outlen);
	if (ret) {
		snap_warn("Query functions info failed, ret:%d\n", ret);
		return ret;
	}

	// Check if number of vuid entries in output matches expected num of vfs and pf
	num_entries =  DEVX_GET(query_vuid_out, out, num_of_entries);
	if ((query_vfs && pci->num_vfs + 1 != num_entries) || (!query_vfs && num_entries != 1)) {
		snap_warn("Failed to set vuid.\n");
		return -1;
	}

	return 0;
}

static void snap_query_pci_vuid(struct snap_pci *pci)
{
	int output_size, vuid_len;
	uint8_t *out;

	output_size = DEVX_ST_SZ_BYTES(query_vuid_out) +
		DEVX_ST_SZ_BYTES(vuid);

	out = calloc(1, output_size);
	if (!out) {
		snap_warn("Alloc memory for output structure failed\n");
		return;
	}

	// query vuid for pci without vfs
	if (snap_query_vuid(pci, out, output_size, false))
		goto free_out;

	vuid_len = DEVX_FLD_SZ_BYTES(vuid, vuid) - 1;
	strncpy(pci->vuid, (char *)DEVX_ADDR_OF(query_vuid_out, out, vuid), vuid_len);
	pci->vuid[vuid_len] = '\0';

free_out:
	free(out);
}

static void snap_query_vfs_vuid(struct snap_pci *pf)
{
	int output_size, i, vuid_len;
	uint8_t *out;
	struct snap_pci *vf;

	output_size = DEVX_ST_SZ_BYTES(query_vuid_out) +
		DEVX_ST_SZ_BYTES(vuid) * (pf->num_vfs + 1);

	out = calloc(1, output_size);
	if (!out) {
		snap_warn("Alloc memory for output structure failed\n");
		return;
	}

	if (snap_query_vuid(pf, out, output_size, (pf->num_vfs > 0)))
		goto free_out;

	vuid_len = DEVX_FLD_SZ_BYTES(vuid, vuid) - 1;

	// set vuid for PF
	strncpy(pf->vuid, (char *)DEVX_ADDR_OF(query_vuid_out, out, vuid), vuid_len);
	pf->vuid[vuid_len] = '\0';

	// set vuid for VFs
	for (i = 0; i < pf->num_vfs; i++) {
		vf = &pf->vfs[i];
		strncpy(vf->vuid, (char *)DEVX_ADDR_OF(query_vuid_out, out, vuid[i + 1]),
			vuid_len);
		vf->vuid[vuid_len] = '\0';
	}

free_out:
	free(out);
}

static int snap_alloc_virtual_functions(struct snap_pci *pf, size_t num_vfs)
{
	int i, j, ret;
	int output_size;
	uint8_t *out;

	if (num_vfs == 0) {
		pf->num_vfs = num_vfs;
		return 0;
	}

	output_size = DEVX_ST_SZ_BYTES(query_emulated_functions_info_out) +
		      DEVX_ST_SZ_BYTES(emulated_function_info) * num_vfs;
	out = calloc(1, output_size);
	if (!out)
		return -ENOMEM;

	ret = snap_query_functions_info(pf->sctx, SNAP_VF, pf->mpci.vhca_id, out, output_size);
	if (ret)
		goto free_vfs_query;

	pf->num_vfs = num_vfs;

	pf->vfs = calloc(pf->num_vfs, sizeof(struct snap_pci));
	if (!pf->vfs)
		return -ENOMEM;

	for (i = 0; i < pf->num_vfs; i++) {
		struct snap_pci *vf = &pf->vfs[i];

		vf->type = snap_pf_to_vf_type(pf->type);
		vf->sctx = pf->sctx;

		vf->plugged = true;
		vf->id = i;
		vf->num_vfs = 0;
		vf->parent = pf;

		vf->pci_bdf.raw = DEVX_GET(query_emulated_functions_info_out,
					   out, emulated_function_info[i].pci_bdf);
		snprintf(vf->pci_number, sizeof(vf->pci_number), "%02x:%02x.%d",
			 vf->pci_bdf.bdf.bus, vf->pci_bdf.bdf.device,
			 vf->pci_bdf.bdf.function);

		vf->mpci.vhca_id = DEVX_GET(query_emulated_functions_info_out,
					    out, emulated_function_info[i].vhca_id);

		ret = snap_alloc_pci_bar(vf);
		if (ret)
			goto free_vfs;

	}

	if (num_vfs > 0)
		snap_query_vfs_vuid(pf);

	free(out);
	return 0;

free_vfs:
	for (j = 0; j < i; j++)
		snap_free_pci_bar(&pf->vfs[j]);
	free(pf->vfs);
free_vfs_query:
	free(out);
	return ret;
}

static void _snap_free_functions(struct snap_context *sctx,
		struct snap_pfs_ctx *pfs)
{
	int i;

	for (i = 0; i < pfs->max_pfs; i++) {
		if (pfs->pfs[i].plugged)
			snap_free_virtual_functions(&pfs->pfs[i]);
		snap_free_pci_bar(&pfs->pfs[i]);
	}

	free(pfs->pfs);
}

static void snap_free_functions(struct snap_context *sctx)
{
	if (sctx->virtio_blk_pfs.max_pfs)
		_snap_free_functions(sctx, &sctx->virtio_blk_pfs);
	if (sctx->virtio_net_pfs.max_pfs)
		_snap_free_functions(sctx, &sctx->virtio_net_pfs);
	if (sctx->nvme_pfs.max_pfs)
		_snap_free_functions(sctx, &sctx->nvme_pfs);
	if (sctx->virtio_fs_pfs.max_pfs)
		_snap_free_functions(sctx, &sctx->virtio_fs_pfs);
}

static int snap_query_functions_info(struct snap_context *sctx,
		enum snap_emulation_type type, int vhca_id, uint8_t *out, int outlen)
{
	struct ibv_context *context = sctx->context;
	uint8_t in[DEVX_ST_SZ_BYTES(query_emulated_functions_info_in)] = {0};

	DEVX_SET(query_emulated_functions_info_in, in, opcode,
		 MLX5_CMD_OP_QUERY_EMULATED_FUNCTIONS_INFO);
	switch (type) {
	case SNAP_NVME:
		DEVX_SET(query_emulated_functions_info_in, in, op_mod,
			 MLX5_SET_EMULATED_FUNCTIONS_OP_MOD_NVME_PHYSICAL_FUNCTIONS);
		break;
	case SNAP_VIRTIO_NET:
		DEVX_SET(query_emulated_functions_info_in, in, op_mod,
			 MLX5_SET_EMULATED_FUNCTIONS_OP_MOD_VIRTIO_NET_PHYSICAL_FUNCTIONS);
		break;
	case SNAP_VIRTIO_BLK:
		DEVX_SET(query_emulated_functions_info_in, in, op_mod,
			 MLX5_SET_EMULATED_FUNCTIONS_OP_MOD_VIRTIO_BLK_PHYSICAL_FUNCTIONS);
		break;
	case SNAP_VIRTIO_FS:
		DEVX_SET(query_emulated_functions_info_in, in, op_mod,
			 MLX5_SET_EMULATED_FUNCTIONS_OP_MOD_VIRTIO_FS_PHYSICAL_FUNCTIONS);
		break;
	case SNAP_VF:
		DEVX_SET(query_emulated_functions_info_in, in, op_mod,
			 MLX5_SET_EMULATED_FUNCTIONS_OP_MOD_VIRTUAL_FUNCTIONS);
		DEVX_SET(query_emulated_functions_info_in, in, pf_vhca_id, vhca_id);
		break;
	default:
		return -EINVAL;
	}

	return mlx5dv_devx_general_cmd(context, in, sizeof(in), out, outlen);
}

static int snap_pf_get_pci_info(struct snap_pci *pf,
		uint8_t *emulated_info_out)
{
	int i, idx = -1, num_emulated_pfs;

	/* vhca_id is not assigned. Use the pf->id as index */
	if (pf->mpci.vhca_id == SNAP_UNINITIALIZED_VHCA_ID) {
		idx = pf->id;
	} else {
		num_emulated_pfs = DEVX_GET(query_emulated_functions_info_out,
					    emulated_info_out, num_emulated_functions);
		for (i = 0; i < num_emulated_pfs; i++) {
			if (pf->mpci.vhca_id == DEVX_GET(query_emulated_functions_info_out,
							 emulated_info_out,
							 emulated_function_info[i].vhca_id)) {
				idx = i;
				break;
			}
		}

	}

	if (idx == -1)
		return -ENODEV;

	pf->pci_bdf.raw = DEVX_GET(query_emulated_functions_info_out,
				   emulated_info_out,
				   emulated_function_info[idx].pci_bdf);
	snprintf(pf->pci_number, sizeof(pf->pci_number), "%02x:%02x.%d",
		 pf->pci_bdf.bdf.bus, pf->pci_bdf.bdf.device,
		 pf->pci_bdf.bdf.function);
	pf->mpci.vhca_id = DEVX_GET(query_emulated_functions_info_out,
				    emulated_info_out,
				    emulated_function_info[idx].vhca_id);
	pf->hotplugged = DEVX_GET(query_emulated_functions_info_out,
					emulated_info_out,
					emulated_function_info[idx].hotplug_function) ? true : false;
	pf->num_vfs = 0;
	pf->max_num_vfs_valid =
		!!DEVX_GET(query_emulated_functions_info_out, emulated_info_out,
			   emulated_function_info[idx].max_num_vfs_valid);

	if (pf->max_num_vfs_valid)
		pf->max_num_vfs =
			DEVX_GET(query_emulated_functions_info_out,
				 emulated_info_out,
				 emulated_function_info[idx].max_num_vfs);


	snap_query_pci_vuid(pf);

	return 0;
}

static int _snap_alloc_functions(struct snap_context *sctx,
		struct snap_pfs_ctx *pfs_ctx)
{
	uint8_t *out;
	int i, j;
	int ret, output_size, num_emulated_pfs;

	pfs_ctx->dirty = false;
	pfs_ctx->pfs = calloc(pfs_ctx->max_pfs, sizeof(struct snap_pci));
	if (!pfs_ctx->pfs)
		return -ENOMEM;

	output_size = DEVX_ST_SZ_BYTES(query_emulated_functions_info_out) +
		      DEVX_ST_SZ_BYTES(emulated_function_info) * (pfs_ctx->max_pfs);
	out = calloc(1, output_size);
	if (!out) {
		ret = -ENOMEM;
		goto out_free_pfs;
	}

	ret = snap_query_functions_info(sctx, pfs_ctx->type, SNAP_UNINITIALIZED_VHCA_ID,
					out, output_size);
	if (ret)
		goto out_free;

	num_emulated_pfs = DEVX_GET(query_emulated_functions_info_out, out,
				    num_emulated_functions);
	if (num_emulated_pfs > pfs_ctx->max_pfs) {
		ret = -EINVAL;
		goto out_free;
	}

	for (i = 0; i < pfs_ctx->max_pfs; i++) {
		struct snap_pci *pf = &pfs_ctx->pfs[i];

		pf->type = snap_emulation_type_to_pf_type(pfs_ctx->type);
		pf->sctx = sctx;
		pf->id = i;
		pf->mpci.vhca_id = SNAP_UNINITIALIZED_VHCA_ID;
		ret = snap_alloc_pci_bar(pf);
		if (ret)
			goto free_vfs;

		if (i < num_emulated_pfs) {
			pf->plugged = true;
			ret = snap_pf_get_pci_info(pf, out);
			if (ret) {
				snap_free_pci_bar(pf);
				goto free_vfs;
			}
		}
	}

	free(out);

	return 0;

free_vfs:
	for (j = 0; j < i; j++) {
		snap_free_virtual_functions(&pfs_ctx->pfs[j]);
		snap_free_pci_bar(&pfs_ctx->pfs[j]);
	}
out_free:
	free(out);
out_free_pfs:
	free(pfs_ctx->pfs);
	return ret;
}

static int snap_alloc_functions(struct snap_context *sctx)
{
	int ret;

	if (sctx->nvme_pfs.max_pfs) {
		ret = _snap_alloc_functions(sctx, &sctx->nvme_pfs);
		if (ret)
			goto out_err;
	}
	if (sctx->virtio_net_pfs.max_pfs) {
		ret = _snap_alloc_functions(sctx, &sctx->virtio_net_pfs);
		if (ret)
			goto out_err_nvme;
	}
	if (sctx->virtio_blk_pfs.max_pfs) {
		ret = _snap_alloc_functions(sctx, &sctx->virtio_blk_pfs);
		if (ret)
			goto out_err_net;
	}
	if (sctx->virtio_fs_pfs.max_pfs) {
		ret = _snap_alloc_functions(sctx, &sctx->virtio_fs_pfs);
		if (ret)
			goto out_err_blk;
	}

	return 0;

out_err_blk:
	if (sctx->virtio_blk_pfs.max_pfs)
		_snap_free_functions(sctx, &sctx->virtio_blk_pfs);
out_err_net:
	if (sctx->virtio_net_pfs.max_pfs)
		_snap_free_functions(sctx, &sctx->virtio_net_pfs);
out_err_nvme:
	if (sctx->nvme_pfs.max_pfs)
		_snap_free_functions(sctx, &sctx->nvme_pfs);
out_err:
	return ret;
}

static int snap_set_device_emulation_caps(struct snap_context *sctx)
{
	uint8_t in[DEVX_ST_SZ_BYTES(query_hca_cap_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(query_hca_cap_out)] = {0};
	uint64_t general_obj_types = 0;
	struct ibv_context *context = sctx->context;
	int ret;

	DEVX_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	DEVX_SET(query_hca_cap_in, in, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_GENERAL_DEVICE);

	ret = mlx5dv_devx_general_cmd(context, in, sizeof(in), out,
				      sizeof(out));
	if (ret)
		return ret;

	sctx->emulation_caps = 0;
	general_obj_types = DEVX_GET64(query_hca_cap_out, out,
				       capability.cmd_hca_cap.general_obj_types);
	if (DEVX_GET(query_hca_cap_out, out,
		capability.cmd_hca_cap.nvme_device_emulation_manager) &&
	    general_obj_types & (1 << MLX5_OBJ_TYPE_NVME_DEVICE_EMULATION))
		sctx->emulation_caps |= SNAP_NVME;
	if (DEVX_GET(query_hca_cap_out, out,
		capability.cmd_hca_cap.virtio_net_device_emulation_manager) &&
	    general_obj_types & (1 << MLX5_OBJ_TYPE_VIRTIO_NET_DEVICE_EMULATION))
		sctx->emulation_caps |= SNAP_VIRTIO_NET;
	if (DEVX_GET(query_hca_cap_out, out,
		capability.cmd_hca_cap.virtio_blk_device_emulation_manager) &&
	    general_obj_types & (1 << MLX5_OBJ_TYPE_VIRTIO_BLK_DEVICE_EMULATION))
		sctx->emulation_caps |= SNAP_VIRTIO_BLK;
	if (DEVX_GET(query_hca_cap_out, out,
		capability.cmd_hca_cap.virtio_fs_device_emulation_manager) &&
	    general_obj_types & (1UL << MLX5_OBJ_TYPE_VIRTIO_FS_DEVICE_EMULATION))
		sctx->emulation_caps |= SNAP_VIRTIO_FS;
	if (DEVX_GET(query_hca_cap_out, out,
		capability.cmd_hca_cap.resources_on_nvme_emulation_manager))
		sctx->mctx.nvme_need_tunnel = false;
	else
		sctx->mctx.nvme_need_tunnel = true;

	if (DEVX_GET(query_hca_cap_out, out,
		capability.cmd_hca_cap.resources_on_virtio_net_emulation_manager))
		sctx->mctx.virtio_net_need_tunnel = false;
	else
		sctx->mctx.virtio_net_need_tunnel = true;

	if (DEVX_GET(query_hca_cap_out, out,
		capability.cmd_hca_cap.resources_on_virtio_blk_emulation_manager))
		sctx->mctx.virtio_blk_need_tunnel = false;
	else
		sctx->mctx.virtio_blk_need_tunnel = true;

	if (DEVX_GET(query_hca_cap_out, out,
		capability.cmd_hca_cap.resources_on_virtio_fs_emulation_manager))
		sctx->mctx.virtio_fs_need_tunnel = false;
	else
		sctx->mctx.virtio_fs_need_tunnel = true;

	if (DEVX_GET(query_hca_cap_out, out,
		capability.cmd_hca_cap.hotplug_manager) &&
	    general_obj_types & (1 << MLX5_OBJ_TYPE_DEVICE))
		sctx->hotplug_supported = true;

	if (DEVX_GET(query_hca_cap_out, out,
		capability.cmd_hca_cap.crossing_vhca_mkey)) {
		sctx->nvme_caps.crossing_vhca_mkey = true;
		sctx->virtio_blk_caps.crossing_vhca_mkey = true;
		sctx->virtio_fs_caps.crossing_vhca_mkey = true;
	} else {
		sctx->nvme_caps.crossing_vhca_mkey = false;
		sctx->virtio_blk_caps.crossing_vhca_mkey = false;
		sctx->virtio_fs_caps.crossing_vhca_mkey = false;
	}

	if (DEVX_GET(query_hca_cap_out, out,
		capability.cmd_hca_cap.virtio_blk_device_emulation_manager) &&
	    general_obj_types & (1UL << MLX5_OBJ_TYPE_VIRTIO_Q_COUNTERS)) {
		sctx->virtio_blk_caps.virtio_q_counters = true;
	} else {
		sctx->virtio_blk_caps.virtio_q_counters = false;
	}

	if (DEVX_GET(query_hca_cap_out, out,
		capability.cmd_hca_cap.virtio_net_device_emulation_manager) &&
	    general_obj_types & (1UL << MLX5_OBJ_TYPE_VIRTIO_Q_COUNTERS)) {
		sctx->virtio_net_caps.virtio_q_counters = true;
	} else {
		sctx->virtio_net_caps.virtio_q_counters = false;
	}

	return 0;
}

static int snap_query_flow_table_caps(struct snap_context *sctx)
{
	uint8_t in[DEVX_ST_SZ_BYTES(query_hca_cap_in)] = {};
	uint8_t out[DEVX_ST_SZ_BYTES(query_hca_cap_out)] = {};
	struct ibv_context *context = sctx->context;
	uint8_t max_ft_level_tx, max_ft_level_rx;
	uint8_t log_max_ft_size_tx, log_max_ft_size_rx;
	bool rx_supported, tx_supported;
	int ret;

	// Only nvme sq requires special steering on BF1, virtio
	// never needs steering tables
	if (!sctx->mctx.nvme_need_tunnel)
		return 0;

	DEVX_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	DEVX_SET(query_hca_cap_in, in, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_NIC_FLOW_TABLE);

	ret = mlx5dv_devx_general_cmd(context, in, sizeof(in), out,
				      sizeof(out));
	if (ret)
		return ret;

	rx_supported = DEVX_GET(query_hca_cap_out, out,
		capability.flow_table_nic_cap.flow_table_properties_nic_receive_rdma.ft_support);
	tx_supported = DEVX_GET(query_hca_cap_out, out,
		capability.flow_table_nic_cap.flow_table_properties_nic_transmit_rdma.ft_support);

	if (!rx_supported || !tx_supported)
		return -ENOTSUP;

	max_ft_level_rx = DEVX_GET(query_hca_cap_out, out,
		capability.flow_table_nic_cap.flow_table_properties_nic_receive_rdma.max_ft_level);
	log_max_ft_size_rx = DEVX_GET(query_hca_cap_out, out,
		capability.flow_table_nic_cap.flow_table_properties_nic_receive_rdma.log_max_ft_size);

	max_ft_level_tx = DEVX_GET(query_hca_cap_out, out,
		capability.flow_table_nic_cap.flow_table_properties_nic_transmit_rdma.max_ft_level);
	log_max_ft_size_tx = DEVX_GET(query_hca_cap_out, out,
		capability.flow_table_nic_cap.flow_table_properties_nic_transmit_rdma.log_max_ft_size);

	sctx->mctx.max_ft_level = snap_min(max_ft_level_tx, max_ft_level_rx);
	sctx->mctx.log_max_ft_size = snap_min(log_max_ft_size_tx,
					      log_max_ft_size_rx);
	return 0;

}

static void snap_fill_virtio_caps(struct snap_virtio_caps *virtio,
		uint8_t *out)
{
	virtio->max_emulated_virtqs = DEVX_GET(query_hca_cap_out,
		out, capability.virtio_emulation_cap.max_num_virtio_queues);
	virtio->max_tunnel_desc = DEVX_GET(query_hca_cap_out,
		out, capability.virtio_emulation_cap.max_tunnel_desc);
	virtio->queue_period_upon_cqe = DEVX_GET(query_hca_cap_out,
		out, capability.virtio_emulation_cap.queue_period_upon_cqe);
	virtio->queue_period_upon_event =
		DEVX_GET(query_hca_cap_out, out,
		capability.virtio_emulation_cap.queue_period_upon_event);
	virtio->umem_1_buffer_param_a = DEVX_GET(query_hca_cap_out,
		out, capability.virtio_emulation_cap.umem_1_buffer_param_a);
	virtio->umem_1_buffer_param_b = DEVX_GET(query_hca_cap_out,
		out, capability.virtio_emulation_cap.umem_1_buffer_param_b);
	virtio->umem_2_buffer_param_a = DEVX_GET(query_hca_cap_out,
		out, capability.virtio_emulation_cap.umem_2_buffer_param_a);
	virtio->umem_2_buffer_param_b = DEVX_GET(query_hca_cap_out,
		out, capability.virtio_emulation_cap.umem_2_buffer_param_b);
	virtio->umem_3_buffer_param_a = DEVX_GET(query_hca_cap_out,
		out, capability.virtio_emulation_cap.umem_3_buffer_param_a);
	virtio->umem_3_buffer_param_b = DEVX_GET(query_hca_cap_out,
		out, capability.virtio_emulation_cap.umem_3_buffer_param_b);

	virtio->min_num_vf_dynamic_msix = DEVX_GET(query_hca_cap_out,
		out, capability.virtio_emulation_cap.min_num_vf_dynamic_msix);
	virtio->max_num_vf_dynamic_msix = DEVX_GET(query_hca_cap_out,
		out, capability.virtio_emulation_cap.max_num_vf_dynamic_msix);
	virtio->emulated_dev_db_cq_map = DEVX_GET(query_hca_cap_out,
		out, capability.virtio_emulation_cap.emulated_dev_db_cq_map);
	virtio->emulated_dev_eq = DEVX_GET(query_hca_cap_out,
		out, capability.virtio_emulation_cap.emulated_dev_eq);

	virtio->virtio_q_index_modify = DEVX_GET(query_hca_cap_out,
		out, capability.virtio_emulation_cap.virtio_q_index_modify);
	virtio->virtio_net_q_addr_modify = DEVX_GET(query_hca_cap_out,
		out, capability.virtio_emulation_cap.virtio_net_q_addr_modify);
	virtio->dirty_byte_map = DEVX_GET(query_hca_cap_out,
		out, capability.virtio_emulation_cap.dirty_byte_map);
	virtio->vnet_modify_ext = DEVX_GET(query_hca_cap_out,
		out, capability.virtio_emulation_cap.vnet_modify_ext);
	virtio->virtio_q_cfg_v2 = DEVX_GET(query_hca_cap_out,
		out, capability.virtio_emulation_cap.virtio_q_cfg_v2);

	virtio->emulated_dev_db_cq_map = DEVX_GET(query_hca_cap_out, out,
			capability.virtio_emulation_cap.emulated_dev_db_cq_map);
	virtio->emulated_dev_eq = DEVX_GET(query_hca_cap_out, out,
			capability.virtio_emulation_cap.emulated_dev_eq);

	if (DEVX_GET(query_hca_cap_out, out,
		     capability.virtio_emulation_cap.virtio_queue_type) &
	    MLX5_VIRTIO_QUEUE_CAP_TYPE_SPLIT)
		virtio->supported_types |= SNAP_VIRTQ_SPLIT_MODE;
	if (DEVX_GET(query_hca_cap_out, out,
		     capability.virtio_emulation_cap.virtio_queue_type) &
	    MLX5_VIRTIO_QUEUE_CAP_TYPE_PACKED)
		virtio->supported_types |= SNAP_VIRTQ_PACKED_MODE;

	if (DEVX_GET(query_hca_cap_out, out,
		     capability.virtio_emulation_cap.event_mode) &
	    MLX5_VIRTIO_QUEUE_CAP_EVENT_MODE_NO_MSIX)
		virtio->event_modes |= SNAP_VIRTQ_NO_MSIX_MODE;
	if (DEVX_GET(query_hca_cap_out, out,
		     capability.virtio_emulation_cap.event_mode) &
	    MLX5_VIRTIO_QUEUE_CAP_EVENT_MODE_QP)
		virtio->event_modes |= SNAP_VIRTQ_QP_MODE;
	if (DEVX_GET(query_hca_cap_out, out,
		     capability.virtio_emulation_cap.event_mode) &
	    MLX5_VIRTIO_QUEUE_CAP_EVENT_MODE_MSIX)
		virtio->event_modes |= SNAP_VIRTQ_MSIX_MODE;

	if (DEVX_GET(query_hca_cap_out, out,
		     capability.virtio_emulation_cap.virtio_version_1_0))
		virtio->features |= SNAP_VIRTIO_F_VERSION_1;
	if (DEVX_GET(query_hca_cap_out, out,
		     capability.virtio_emulation_cap.tso_ipv4))
		virtio->features |= SNAP_VIRTIO_NET_F_HOST_TSO4;
	if (DEVX_GET(query_hca_cap_out, out,
		     capability.virtio_emulation_cap.tso_ipv6))
		virtio->features |= SNAP_VIRTIO_NET_F_HOST_TSO6;
	if (DEVX_GET(query_hca_cap_out, out,
		     capability.virtio_emulation_cap.tx_csum))
		virtio->features |= SNAP_VIRTIO_NET_F_CSUM;
	if (DEVX_GET(query_hca_cap_out, out,
		     capability.virtio_emulation_cap.rx_csum))
		virtio->features |= SNAP_VIRTIO_NET_F_GUEST_CSUM;
	if (DEVX_GET(query_hca_cap_out, out,
		     capability.virtio_emulation_cap.desc_tunnel_offload_type))
		virtio->features |= SNAP_VIRTIO_NET_F_CTRL_VQ;
}

static int snap_query_virtio_blk_emulation_caps(struct snap_context *sctx)
{
	uint8_t in[DEVX_ST_SZ_BYTES(query_hca_cap_in)] = {};
	uint8_t out[DEVX_ST_SZ_BYTES(query_hca_cap_out)] = {};
	struct ibv_context *context = sctx->context;
	int ret;

	DEVX_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	DEVX_SET(query_hca_cap_in, in, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_VIRTIO_BLK_DEVICE_EMULATION);

	ret = mlx5dv_devx_general_cmd(context, in, sizeof(in), out,
				      sizeof(out));
	if (ret)
		return ret;

	sctx->virtio_blk_pfs.type = SNAP_VIRTIO_BLK;
	sctx->virtio_blk_pfs.max_pfs = DEVX_GET(query_hca_cap_out,
		out, capability.virtio_emulation_cap.max_emulated_devices);

	snap_fill_virtio_caps(&sctx->virtio_blk_caps, out);

	return 0;
}

static int snap_query_virtio_fs_emulation_caps(struct snap_context *sctx)
{
	uint8_t in[DEVX_ST_SZ_BYTES(query_hca_cap_in)] = {};
	uint8_t out[DEVX_ST_SZ_BYTES(query_hca_cap_out)] = {};
	struct ibv_context *context = sctx->context;
	int ret;

	DEVX_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	DEVX_SET(query_hca_cap_in, in, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_VIRTIO_FS_DEVICE_EMULATION);

	ret = mlx5dv_devx_general_cmd(context, in, sizeof(in), out,
				      sizeof(out));
	if (ret)
		return ret;

	sctx->virtio_fs_pfs.type = SNAP_VIRTIO_FS;
	sctx->virtio_fs_pfs.max_pfs = DEVX_GET(query_hca_cap_out,
		out, capability.virtio_emulation_cap.max_emulated_devices);

	snap_fill_virtio_caps(&sctx->virtio_fs_caps, out);

	return 0;
}

static int snap_query_virtio_net_emulation_caps(struct snap_context *sctx)
{
	uint8_t in[DEVX_ST_SZ_BYTES(query_hca_cap_in)] = {};
	uint8_t out[DEVX_ST_SZ_BYTES(query_hca_cap_out)] = {};
	struct ibv_context *context = sctx->context;
	int ret;

	DEVX_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	DEVX_SET(query_hca_cap_in, in, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_VIRTIO_NET_DEVICE_EMULATION);

	ret = mlx5dv_devx_general_cmd(context, in, sizeof(in), out,
				      sizeof(out));
	if (ret)
		return ret;

	sctx->virtio_net_pfs.type = SNAP_VIRTIO_NET;
	sctx->virtio_net_pfs.max_pfs = DEVX_GET(query_hca_cap_out,
		out, capability.virtio_emulation_cap.max_emulated_devices);

	snap_fill_virtio_caps(&sctx->virtio_net_caps, out);

	return 0;
}

static int snap_query_nvme_emulation_caps(struct snap_context *sctx)
{
	uint8_t in[DEVX_ST_SZ_BYTES(query_hca_cap_in)] = {};
	uint8_t out[DEVX_ST_SZ_BYTES(query_hca_cap_out)] = {};
	struct ibv_context *context = sctx->context;
	int ret;

	DEVX_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	DEVX_SET(query_hca_cap_in, in, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_NVME_DEVICE_EMULATION);

	ret = mlx5dv_devx_general_cmd(context, in, sizeof(in), out,
				      sizeof(out));
	if (ret)
		return ret;

	sctx->nvme_pfs.type = SNAP_NVME;
	sctx->nvme_pfs.max_pfs = DEVX_GET(query_hca_cap_out, out,
			 capability.nvme_emulation_cap.max_emulated_devices);
	sctx->nvme_caps.max_nvme_namespaces = 1 << DEVX_GET(query_hca_cap_out, out,
		capability.nvme_emulation_cap.log_max_nvme_offload_namespaces);
	sctx->nvme_caps.max_nvme_nsid = 1 << DEVX_GET(query_hca_cap_out, out,
		capability.nvme_emulation_cap.log_max_nvme_offload_nsid);
	if (sctx->nvme_caps.max_nvme_nsid == 1)
		sctx->nvme_caps.max_nvme_nsid = sctx->nvme_caps.max_nvme_namespaces;
	sctx->nvme_caps.max_emulated_nvme_cqs = 1 << DEVX_GET(query_hca_cap_out, out,
		capability.nvme_emulation_cap.log_max_emulated_cq);
	sctx->nvme_caps.max_emulated_nvme_sqs = 1 << DEVX_GET(query_hca_cap_out, out,
		capability.nvme_emulation_cap.log_max_emulated_sq);
	sctx->nvme_caps.max_queue_depth = 1 << DEVX_GET(query_hca_cap_out, out,
		capability.nvme_emulation_cap.log_max_queue_depth);
	if (sctx->nvme_caps.max_queue_depth == 1)
		sctx->nvme_caps.max_queue_depth = SNAP_NVME_MAX_QUEUE_DEPTH_LEGACY;
	sctx->nvme_caps.cq_interrupt_disabled = DEVX_GET(query_hca_cap_out, out,
		capability.nvme_emulation_cap.nvme_cq_interrupt_disabled);
	sctx->nvme_caps.reg_size = DEVX_GET(query_hca_cap_out, out,
		capability.nvme_emulation_cap.registers_size);
	if (DEVX_GET(query_hca_cap_out, out,
		     capability.nvme_emulation_cap.nvme_offload_type_sqe))
		sctx->nvme_caps.supported_types |= SNAP_NVME_RAW_MODE;
	if (DEVX_GET(query_hca_cap_out, out,
		     capability.nvme_emulation_cap.nvme_offload_type_command_capsule))
		sctx->nvme_caps.supported_types |= SNAP_NVME_TO_NVMF_MODE;

	return 0;

}

static int snap_query_hotplug_caps(struct snap_context *sctx)
{
	uint8_t in[DEVX_ST_SZ_BYTES(query_hca_cap_in)] = {};
	uint8_t out[DEVX_ST_SZ_BYTES(query_hca_cap_out)] = {};
	struct ibv_context *context = sctx->context;
	int ret, supported_types;

	if (!sctx->hotplug_supported)
		return 0;

	DEVX_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	DEVX_SET(query_hca_cap_in, in, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_HOTPLUG);

	ret = mlx5dv_devx_general_cmd(context, in, sizeof(in), out,
				      sizeof(out));
	if (ret)
		return ret;

	sctx->hotplug.max_total_vfs = DEVX_GET(query_hca_cap_out, out,
				capability.hotplug_cap.max_total_vfs);
	sctx->hotplug.max_devices = DEVX_GET(query_hca_cap_out, out,
				capability.hotplug_cap.max_hotplug_devices);
	sctx->hotplug.log_max_bar_size = DEVX_GET(query_hca_cap_out, out,
				capability.hotplug_cap.log_max_bar_size);
	sctx->hotplug.pci_hotplug_state_change = DEVX_GET(query_hca_cap_out, out,
				capability.hotplug_cap.pci_hotplug_state_change);
	sctx->hotplug.virtio_transitional_device_hotplug = DEVX_GET(query_hca_cap_out, out,
				capability.hotplug_cap.virtio_transitional_device_hotplug);
	supported_types = DEVX_GET(query_hca_cap_out, out,
			capability.hotplug_cap.hotplug_device_types_supported);
	if (supported_types & MLX5_HOTPLUG_DEVICE_BIT_MASK_NVME)
		sctx->hotplug.supported_types |= SNAP_NVME;
	if (supported_types & MLX5_HOTPLUG_DEVICE_BIT_MASK_VIRTIO_NET)
		sctx->hotplug.supported_types |= SNAP_VIRTIO_NET;
	if (supported_types & MLX5_HOTPLUG_DEVICE_BIT_MASK_VIRTIO_BLK)
		sctx->hotplug.supported_types |= SNAP_VIRTIO_BLK;
	if (supported_types & MLX5_HOTPLUG_DEVICE_BIT_MASK_VIRTIO_FS)
		sctx->hotplug.supported_types |= SNAP_VIRTIO_FS;

	return 0;
}

static int snap_query_emulation_caps(struct snap_context *sctx)
{
	int ret;

	if (sctx->emulation_caps & SNAP_NVME) {
		ret = snap_query_nvme_emulation_caps(sctx);
		if (ret)
			return ret;
	}
	if (sctx->emulation_caps & SNAP_VIRTIO_NET) {
		ret = snap_query_virtio_net_emulation_caps(sctx);
		if (ret)
			return ret;
	}
	if (sctx->emulation_caps & SNAP_VIRTIO_BLK) {
		ret = snap_query_virtio_blk_emulation_caps(sctx);
		if (ret)
			return ret;
	}
	if (sctx->emulation_caps & SNAP_VIRTIO_FS) {
		ret = snap_query_virtio_fs_emulation_caps(sctx);
		if (ret)
			return ret;
	}

	return 0;

}

static void snap_destroy_vhca_tunnel(struct snap_device *sdev)
{
	mlx5dv_devx_obj_destroy(sdev->mdev.vtunnel->obj);
	free(sdev->mdev.vtunnel);
	sdev->mdev.vtunnel = NULL;
}

/**
 * snap_devx_obj_query() - Query a snap devx object
 * @snap_obj:      snap object to query
 * @in:            input cmd buffer
 * @inlen:         input cmd buffer length
 * @out:           output cmd buffer
 * @outlen:        output cmd buffer length
 *
 * Query a snap devx object using a given input command. In case of a tunneled
 * object, The command will be send using a vhca tunnel mechanism.
 *
 * Return: 0 on success and out pointer will be filled with the response.
 */
int snap_devx_obj_query(struct mlx5_snap_devx_obj *snap_obj, void *in,
			size_t inlen, void *out, size_t outlen)
{
	if (snap_obj->vtunnel)
		return snap_general_tunneled_cmd(snap_obj->sdev, in, inlen,
						 out, outlen, 0);
	else
		return mlx5dv_devx_obj_query(snap_obj->obj, in, inlen, out,
					     outlen);
}

/**
 * snap_devx_obj_modify() - Modify a snap devx object
 * @snap_obj:      snap object to modify
 * @in:            input cmd buffer
 * @inlen:         input cmd buffer length
 * @out:           output cmd buffer
 * @outlen:        output cmd buffer length
 *
 * Modify a snap devx object using a given input command. In case of a tunneled
 * object, The command will be send using a vhca tunnel mechanism.
 *
 * Return: 0 on success and out pointer will be filled with the response.
 */
int snap_devx_obj_modify(struct mlx5_snap_devx_obj *snap_obj, void *in,
			 size_t inlen, void *out, size_t outlen)
{
	if (snap_obj->vtunnel)
		return snap_general_tunneled_cmd(snap_obj->sdev, in, inlen,
						 out, outlen, 0);
	else
		return mlx5dv_devx_obj_modify(snap_obj->obj, in, inlen, out,
					      outlen);
}

/**
 * snap_devx_obj_destroy() - Destroy a snap devx object
 * @snap_obj:      snap devx object
 *
 * Destroy a snap devx object that was created using snap_devx_obj_create(). In
 * case of a tunneld object, the destruction will use a pre-allocated
 * destructor command.
 *
 * Return: Returns 0 in case of success.
 */
int snap_devx_obj_destroy(struct mlx5_snap_devx_obj *snap_obj)
{
	int ret = -EINVAL;

	if (snap_obj->obj) {
		ret = mlx5dv_devx_obj_destroy(snap_obj->obj);
	} else if (snap_obj->vtunnel) {
		ret =  snap_general_tunneled_cmd(snap_obj->sdev,
						 snap_obj->dtor_in,
						 snap_obj->inlen,
						 snap_obj->dtor_out,
						 snap_obj->outlen, 0);
		free(snap_obj->dtor_out);
		free(snap_obj->dtor_in);
	}

	free(snap_obj);

	return ret;
}

/**
 * snap_devx_obj_create() - Create a devx object for snap
 * @sdev:          snap device
 * @in:            input cmd buffer
 * @inlen:         input cmd buffer length
 * @out:           output cmd buffer
 * @outlen:        output cmd buffer length
 * @vtunnel:       tunnel object (in case of a tunneled cmd)
 * @dtor_inlen:    destructor input cmd buffer length (to allocate)
 * @dtor_outlen:   destructor output cmd buffer length (to allocate)
 *
 * Create a devx object for a given input command. In case of a tunneled
 * command, a pointer to vhca tunnel object should be given with indicators
 * for the destruction as well (dtor_inlen and dtor_outlen.
 *
 * Return: Returns a new mlx5_snap_devx_obj in case of success, NULL otherwise.
 */
struct mlx5_snap_devx_obj*
snap_devx_obj_create(struct snap_device *sdev, void *in, size_t inlen,
		     void *out, size_t outlen,
		     struct mlx5_snap_devx_obj *vtunnel,
		     size_t dtor_inlen, size_t dtor_outlen)
{
	struct mlx5_snap_devx_obj *snap_obj;
	struct ibv_context *context;
	int ret;

	/* Use SF context if it's valid, otherwise use emulation manager */
	context = sdev->mdev.context ? sdev->mdev.context :
		sdev->sctx->context;

	snap_obj = calloc(1, sizeof(*snap_obj));
	if (!snap_obj)
		goto out_err;

	if (vtunnel) {
		/* Allocate destructor resources before creating the real obj */
		snap_obj->dtor_in = calloc(1, dtor_inlen);
		if (!snap_obj->dtor_in)
			goto out_free;
		snap_obj->inlen = dtor_inlen;

		snap_obj->dtor_out = calloc(1, dtor_outlen);
		if (!snap_obj->dtor_out)
			goto out_free_dtor_in;
		snap_obj->outlen = dtor_outlen;

		ret = snap_general_tunneled_cmd(sdev, in, inlen, out, outlen,
						0);
		if (ret)
			goto out_free_dtor_out;

	} else {
		snap_obj->obj = mlx5dv_devx_obj_create(context, in, inlen, out,
						       outlen);
		if (!snap_obj->obj)
			goto out_free;
	}

	snap_obj->vtunnel = vtunnel;
	snap_obj->sdev = sdev;
	snap_obj->obj_id = DEVX_GET(general_obj_out_cmd_hdr, out, obj_id);

	return snap_obj;

out_free_dtor_out:
	free(snap_obj->dtor_out);
out_free_dtor_in:
	free(snap_obj->dtor_in);
out_free:
	free(snap_obj);
out_err:
	return NULL;
}

static int snap_modify_qp_to_init(struct mlx5_snap_devx_obj *qp,
				  uint32_t qp_num, struct ibv_qp_attr *qp_attr,
				  int attr_mask)
{
	uint8_t in[DEVX_ST_SZ_BYTES(rst2init_qp_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(rst2init_qp_out)] = {0};
	void *qpc = DEVX_ADDR_OF(rst2init_qp_in, in, qpc);

	DEVX_SET(rst2init_qp_in, in, opcode, MLX5_CMD_OP_RST2INIT_QP);
	DEVX_SET(rst2init_qp_in, in, qpn, qp_num);
	DEVX_SET(qpc, qpc, pm_state, MLX5_QP_PM_MIGRATED);

	if (attr_mask & IBV_QP_PKEY_INDEX)
		DEVX_SET(qpc, qpc, primary_address_path.pkey_index,
			 qp_attr->pkey_index);

	if (attr_mask & IBV_QP_PORT)
		DEVX_SET(qpc, qpc, primary_address_path.vhca_port_num,
			 qp_attr->port_num);

	if (attr_mask & IBV_QP_ACCESS_FLAGS) {
		if (qp_attr->qp_access_flags & IBV_ACCESS_REMOTE_READ)
			DEVX_SET(qpc, qpc, rre, 1);
		if (qp_attr->qp_access_flags & IBV_ACCESS_REMOTE_WRITE)
			DEVX_SET(qpc, qpc, rwe, 1);
	}

	return snap_devx_obj_modify(qp, in, sizeof(in), out, sizeof(out));
}

static int snap_modify_qp_to_rtr(struct mlx5_snap_devx_obj *qp,
				 uint32_t qp_num, struct ibv_qp_attr *qp_attr,
				 struct mlx5dv_ah *dv_ah, int attr_mask)
{
	uint8_t in[DEVX_ST_SZ_BYTES(init2rtr_qp_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(init2rtr_qp_out)] = {0};
	void *qpc = DEVX_ADDR_OF(init2rtr_qp_in, in, qpc);

	DEVX_SET(init2rtr_qp_in, in, opcode, MLX5_CMD_OP_INIT2RTR_QP);
	DEVX_SET(init2rtr_qp_in, in, qpn, qp_num);

	/* 30 is the maximum value for Infiniband QPs*/
	DEVX_SET(qpc, qpc, log_msg_max, 30);

	/* TODO: add more attributes */
	if (attr_mask & IBV_QP_PATH_MTU)
		DEVX_SET(qpc, qpc, mtu, qp_attr->path_mtu);
	if (attr_mask & IBV_QP_DEST_QPN)
		DEVX_SET(qpc, qpc, remote_qpn, qp_attr->dest_qp_num);
	if (attr_mask & IBV_QP_RQ_PSN)
		DEVX_SET(qpc, qpc, next_rcv_psn, qp_attr->rq_psn & 0xffffff);
	if (attr_mask & IBV_QP_TIMEOUT)
		DEVX_SET(qpc, qpc, primary_address_path.ack_timeout,
			 qp_attr->timeout);
	if (attr_mask & IBV_QP_PKEY_INDEX)
		DEVX_SET(qpc, qpc, primary_address_path.pkey_index,
			 qp_attr->pkey_index);
	if (attr_mask & IBV_QP_PORT)
		DEVX_SET(qpc, qpc, primary_address_path.vhca_port_num,
			 qp_attr->port_num);
	if (attr_mask & IBV_QP_MAX_DEST_RD_ATOMIC)
		DEVX_SET(qpc, qpc, log_rra_max,
			 snap_u32log2(qp_attr->max_dest_rd_atomic));
	if (attr_mask & IBV_QP_MIN_RNR_TIMER)
		DEVX_SET(qpc, qpc, min_rnr_nak, qp_attr->min_rnr_timer);
	if (attr_mask & IBV_QP_AV) {
		DEVX_SET(qpc, qpc, primary_address_path.tclass,
			 qp_attr->ah_attr.grh.traffic_class);
		/* set destination gid, mac and udp port */
		memcpy(DEVX_ADDR_OF(qpc, qpc, primary_address_path.rgid_rip),
		       qp_attr->ah_attr.grh.dgid.raw,
		       DEVX_FLD_SZ_BYTES(qpc, primary_address_path.rgid_rip));
		memcpy(DEVX_ADDR_OF(qpc, qpc, primary_address_path.rmac_47_32),
		       dv_ah->av->rmac, 6);
		/* av uses rlid to return udp source port */
		DEVX_SET(qpc, qpc, primary_address_path.udp_sport,
			 htobe16(dv_ah->av->rlid));

		DEVX_SET(qpc, qpc, primary_address_path.src_addr_index,
			 qp_attr->ah_attr.grh.sgid_index);
		if (qp_attr->ah_attr.sl & 0x7)
			DEVX_SET(qpc, qpc, primary_address_path.eth_prio,
				 qp_attr->ah_attr.sl & 0x7);
		if (qp_attr->ah_attr.grh.hop_limit > 1)
			DEVX_SET(qpc, qpc, primary_address_path.hop_limit,
				 qp_attr->ah_attr.grh.hop_limit);
		else
			DEVX_SET(qpc, qpc, primary_address_path.hop_limit, 64);
	}

	return snap_devx_obj_modify(qp, in, sizeof(in), out, sizeof(out));
}

static int snap_modify_qp_to_rts(struct mlx5_snap_devx_obj *qp,
				 uint32_t qp_num, struct ibv_qp_attr *qp_attr,
				 int attr_mask)
{
	uint32_t in[DEVX_ST_SZ_DW(rtr2rts_qp_in)] = {0};
	uint32_t out[DEVX_ST_SZ_DW(rtr2rts_qp_out)] = {0};
	void *qpc = DEVX_ADDR_OF(rtr2rts_qp_in, in, qpc);

	DEVX_SET(rtr2rts_qp_in, in, opcode, MLX5_CMD_OP_RTR2RTS_QP);
	DEVX_SET(rtr2rts_qp_in, in, qpn, qp_num);

	if (attr_mask & IBV_QP_TIMEOUT)
		DEVX_SET(qpc, qpc, primary_address_path.ack_timeout,
			 qp_attr->timeout);
	if (attr_mask & IBV_QP_RETRY_CNT)
		DEVX_SET(qpc, qpc, retry_count, qp_attr->retry_cnt);
	if (attr_mask & IBV_QP_SQ_PSN)
		DEVX_SET(qpc, qpc, next_send_psn, qp_attr->sq_psn & 0xffffff);
	if (attr_mask & IBV_QP_RNR_RETRY)
		DEVX_SET(qpc, qpc, rnr_retry, qp_attr->rnr_retry);
	if (attr_mask & IBV_QP_MAX_QP_RD_ATOMIC)
		DEVX_SET(qpc, qpc, log_sra_max,
			 snap_u32log2(qp_attr->max_rd_atomic));

	return snap_devx_obj_modify(qp, in, sizeof(in), out, sizeof(out));
}

static int snap_modify_qp(struct mlx5_snap_devx_obj *qp, uint32_t qp_num,
			  struct ibv_qp_attr *qp_attr, struct mlx5dv_ah *dv_ah,
			  int attr_mask)
{
	int ret;

	/* state mask is a must for modifying QP */
	if (!(attr_mask & IBV_QP_STATE))
		return -EINVAL;

	switch (qp_attr->qp_state) {
	case IBV_QPS_INIT:
		ret = snap_modify_qp_to_init(qp, qp_num, qp_attr, attr_mask);
		break;
	case IBV_QPS_RTR:
		ret = snap_modify_qp_to_rtr(qp, qp_num, qp_attr, dv_ah, attr_mask);
		break;
	case IBV_QPS_RTS:
		ret = snap_modify_qp_to_rts(qp, qp_num, qp_attr, attr_mask);
		break;
	default:
		ret = -EINVAL;
	}

	return ret;
}

static int snap_clone_qp(struct snap_device *sdev,
		struct mlx5_snap_hw_qp *hw_qp, struct ibv_qp *qp)
{
	struct ibv_qp_attr hw_qp_attr = {};
	struct ibv_qp_attr attr = {};
	struct ibv_qp_init_attr init_attr = {};
	int ret, attr_mask;
	struct ibv_ah *ah;
	struct mlx5dv_obj  av_obj;
	struct mlx5dv_ah   dv_ah;

	attr_mask = IBV_QP_PKEY_INDEX |
		    IBV_QP_PORT |
		    IBV_QP_ACCESS_FLAGS |
		    IBV_QP_PATH_MTU |
		    IBV_QP_AV |
		    IBV_QP_DEST_QPN |
		    IBV_QP_RQ_PSN |
		    IBV_QP_MAX_DEST_RD_ATOMIC |
		    IBV_QP_MIN_RNR_TIMER |
		    IBV_QP_TIMEOUT |
		    IBV_QP_RETRY_CNT |
		    IBV_QP_RNR_RETRY |
		    IBV_QP_SQ_PSN |
		    IBV_QP_MAX_QP_RD_ATOMIC;

	ret = ibv_query_qp(qp, &attr, attr_mask, &init_attr);
	if (ret)
		return ret;

	if (attr.ah_attr.grh.sgid_index) {
		ret = snap_copy_roce_address(sdev, qp->context, attr.ah_attr.grh.sgid_index);
		if (ret)
			return ret;
	}

	/* rmac is not a part of av_attr, in order to get it
	 * we have to create ah and convert it to the dv ah
	 * which has rmac
	 */
	ah = ibv_create_ah(qp->pd, &attr.ah_attr);
	if (!ah) {
		snap_error("ibv_create_ah() return failed with errno:%d\n", errno);
		return -1;
	}

	av_obj.ah.in = ah;
	av_obj.ah.out = &dv_ah;
	mlx5dv_init_obj(&av_obj, MLX5DV_OBJ_AH);
	ibv_destroy_ah(ah);

	hw_qp_attr.qp_state = IBV_QPS_INIT;
	hw_qp_attr.pkey_index = attr.pkey_index;
	hw_qp_attr.port_num = attr.port_num;
	hw_qp_attr.qp_access_flags = attr.qp_access_flags;
	ret = snap_modify_qp(hw_qp->mqp, qp->qp_num, &hw_qp_attr, NULL,
			     IBV_QP_STATE |
			     IBV_QP_PKEY_INDEX |
			     IBV_QP_PORT |
			     IBV_QP_ACCESS_FLAGS);
	if (ret)
		return ret;

	memset(&hw_qp_attr, 0, sizeof(hw_qp_attr));
	hw_qp_attr.qp_state = IBV_QPS_RTR;
	hw_qp_attr.path_mtu = attr.path_mtu;
	hw_qp_attr.dest_qp_num = attr.dest_qp_num;
	hw_qp_attr.rq_psn = attr.rq_psn;
	hw_qp_attr.max_dest_rd_atomic = attr.max_dest_rd_atomic;
	hw_qp_attr.min_rnr_timer = attr.min_rnr_timer;

	memcpy(&hw_qp_attr.ah_attr, &attr.ah_attr, sizeof(attr.ah_attr));
	ret = snap_modify_qp(hw_qp->mqp, qp->qp_num, &hw_qp_attr, &dv_ah,
			     IBV_QP_STATE |
			     IBV_QP_PATH_MTU |
			     IBV_QP_DEST_QPN |
			     IBV_QP_RQ_PSN |
			     IBV_QP_MAX_DEST_RD_ATOMIC |
			     IBV_QP_MIN_RNR_TIMER |
			     IBV_QP_AV);
	if (ret)
		return ret;

	memset(&hw_qp_attr, 0, sizeof(hw_qp_attr));
	hw_qp_attr.qp_state = IBV_QPS_RTS;
	hw_qp_attr.timeout = attr.timeout;
	hw_qp_attr.retry_cnt = attr.retry_cnt;
	hw_qp_attr.sq_psn = attr.sq_psn;
	hw_qp_attr.rnr_retry = attr.rnr_retry;
	hw_qp_attr.max_rd_atomic = attr.max_rd_atomic;
	ret = snap_modify_qp(hw_qp->mqp, qp->qp_num, &hw_qp_attr, NULL,
			     IBV_QP_STATE |
			     IBV_QP_TIMEOUT |
			     IBV_QP_RETRY_CNT |
			     IBV_QP_SQ_PSN |
			     IBV_QP_RNR_RETRY |
			     IBV_QP_MAX_QP_RD_ATOMIC);
	if (ret)
		return ret;

	hw_qp->parent_qp = qp;

	return 0;
}

static int snap_fte_reset(struct mlx5_snap_flow_table_entry *fte)
{
	struct mlx5_snap_flow_table *ft = fte->fg->ft;
	int ret;

	ret = snap_devx_obj_destroy(fte->fte);
	if (ret)
		return ret;

	/* lock using ft lock since ftes allocated by ft */
	pthread_mutex_lock(&ft->lock);
	fte->fte = NULL;
	pthread_mutex_unlock(&ft->lock);

	return 0;
}

static struct mlx5_snap_flow_table_entry*
snap_fte_init(struct snap_device *sdev, struct mlx5_snap_flow_group *fg,
	int action, enum mlx5_flow_destination_type dest_type, uint32_t dest_id,
	void *match, struct ibv_context *context)
{
	struct mlx5_snap_flow_table *ft = fg->ft;
	struct mlx5_snap_flow_table_entry *fte = NULL;
	uint8_t in[DEVX_ST_SZ_BYTES(set_fte_in) +
		   DEVX_ST_SZ_BYTES(dest_format_struct)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(set_fte_out)] = {0};
	int i;

	pthread_mutex_lock(&ft->lock);
	for (i = fg->start_idx; i < fg->end_idx + 1; i++) {
		if (!ft->ftes[i].fte) {
			fte = &ft->ftes[i];
			break;
		}
	}
	pthread_mutex_unlock(&ft->lock);

	if (!fte)
		goto out_err;

	DEVX_SET(set_fte_in, in, opcode, MLX5_CMD_OP_SET_FLOW_TABLE_ENTRY);
	DEVX_SET(set_fte_in, in, table_type, ft->table_type);
	DEVX_SET(set_fte_in, in, table_id, ft->table_id);
	DEVX_SET(set_fte_in, in, flow_index, fte->idx);

	DEVX_SET(set_fte_in, in, flow_context.group_id, fg->group_id);
	DEVX_SET(set_fte_in, in, flow_context.action, action);
	if (action & MLX5_FLOW_CONTEXT_ACTION_FWD_DEST) {
		void *destination;

		DEVX_SET(set_fte_in, in, flow_context.destination_list_size, 1);

		destination = DEVX_ADDR_OF(set_fte_in, in,
			flow_context.destination[0].dest_format_struct);
		DEVX_SET(dest_format_struct, destination, destination_type,
			 dest_type);
		DEVX_SET(dest_format_struct, destination, destination_id, dest_id);
	}

	memcpy(DEVX_ADDR_OF(set_fte_in, in, flow_context.match_value),
	       match, DEVX_ST_SZ_BYTES(fte_match_param));

	if (context && context != sdev->sctx->context) {
		fte->fte = calloc(1, sizeof(struct mlx5_snap_devx_obj));
		if (!fte->fte)
			goto out_err;

		fte->fte->obj = mlx5dv_devx_obj_create(context, in, sizeof(in),
						       out, sizeof(out));
		if (!fte->fte->obj) {
			free(fte->fte);
			goto out_err;
		}
	} else {
		fte->fte = snap_devx_obj_create(sdev, in, sizeof(in), out,
						sizeof(out),
						sdev->mdev.vtunnel,
						DEVX_ST_SZ_BYTES(delete_fte_in),
						DEVX_ST_SZ_BYTES(delete_fte_out));
		if (!fte->fte)
			goto out_err;

		if (sdev->mdev.vtunnel) {
			void *dtor = fte->fte->dtor_in;

			DEVX_SET(delete_fte_in, dtor, opcode,
				 MLX5_CMD_OP_DELETE_FLOW_TABLE_ENTRY);
			DEVX_SET(delete_fte_in, dtor, table_type, ft->table_type);
			DEVX_SET(delete_fte_in, dtor, table_id, ft->table_id);
			DEVX_SET(delete_fte_in, dtor, flow_index, fte->idx);
		}
	}

	fte->fg = fg;

	return fte;

out_err:
	return NULL;
}

static int snap_destroy_rdma_dev_qp_flow(struct mlx5_snap_hw_qp *hw_qp)
{
	int ret;

	ret = ibv_destroy_flow(hw_qp->rdma_flow);
	if (ret)
		return ret;

	return mlx5dv_destroy_flow_matcher(hw_qp->rdma_matcher);
}

static int snap_create_rdma_dev_qp_flow(struct snap_device *sdev,
		struct mlx5_snap_hw_qp *hw_qp)
{
	struct mlx5dv_flow_action_attr flow_attr = {0};
	struct mlx5dv_flow_matcher_attr match_attr = {0};
	struct ibv_context *context = sdev->mdev.rdma_dev;
	struct mlx5dv_flow_match_parameters *value;
	void *match_mask;
	int ret;

	match_mask = calloc(1,
			sizeof(*value) + DEVX_ST_SZ_BYTES(fte_match_param));
	if (!match_mask) {
		ret = -ENOMEM;
		goto out_err;
	}

	value = match_mask;
	value->match_sz = DEVX_ST_SZ_BYTES(fte_match_param);

	memset(value->match_buf, 0, DEVX_ST_SZ_BYTES(fte_match_param));
	DEVX_SET(fte_match_param, value->match_buf, misc_parameters.bth_dst_qp,
		 0xffffff);
	match_attr.match_criteria_enable = 1 << MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_MISC_PARAMETERS;
	match_attr.match_mask = value;
	match_attr.comp_mask = MLX5DV_FLOW_MATCHER_MASK_FT_TYPE;
	match_attr.ft_type = MLX5DV_FLOW_TABLE_TYPE_RDMA_RX;

	hw_qp->rdma_matcher = mlx5dv_create_flow_matcher(context, &match_attr);
	if (!hw_qp->rdma_matcher) {
		ret = -errno;
		goto out_free;
	}

	/* create flow */
	memset(value->match_buf, 0, DEVX_ST_SZ_BYTES(fte_match_param));
	DEVX_SET(fte_match_param, value->match_buf, misc_parameters.bth_dst_qp,
		 hw_qp->parent_qp->qp_num);
	flow_attr.type = MLX5DV_FLOW_ACTION_DEST_DEVX;
	flow_attr.obj = sdev->mdev.rdma_ft_rx->ft->obj;

	hw_qp->rdma_flow = mlx5dv_create_flow(hw_qp->rdma_matcher, value, 1, &flow_attr);
	if (!hw_qp->rdma_flow) {
		ret = -errno;
		goto out_free_matcher;
	}

	free(match_mask);
	return 0;

out_free_matcher:
	mlx5dv_destroy_flow_matcher(hw_qp->rdma_matcher);
out_free:
	free(match_mask);
out_err:
	return ret;
}

static int snap_reset_hw_qp_steering(struct mlx5_snap_hw_qp *hw_qp)
{
	int ret;

	ret = snap_destroy_rdma_dev_qp_flow(hw_qp);
	if (ret)
		return ret;

	ret = snap_fte_reset(hw_qp->fte_rx);
	if (ret)
		return ret;
	hw_qp->fte_rx = NULL;

	ret = snap_fte_reset(hw_qp->fte_tx);
	if (ret)
		return ret;
	hw_qp->fte_tx = NULL;

	return 0;
}

static int snap_set_hw_qp_steering(struct snap_device *sdev,
		struct mlx5_snap_hw_qp *hw_qp)
{
	uint8_t match[DEVX_ST_SZ_BYTES(fte_match_param)] = {0};
	uint32_t qpn = hw_qp->parent_qp->qp_num;
	int ret;

	/* TX_RDMA FT6, forward to SF */
	DEVX_SET(fte_match_param, match, misc_parameters.source_sqn, qpn);
	hw_qp->fte_tx = snap_fte_init(sdev, sdev->mdev.fg_tx,
				      MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
				      MLX5_FLOW_DESTINATION_TYPE_VHCA_TX,
				      snap_get_dev_vhca_id(hw_qp->parent_qp->context),
				      match, NULL);
	if (!hw_qp->fte_tx)
		goto out_err;

	memset(match, 0, DEVX_ST_SZ_BYTES(fte_match_param));
	DEVX_SET(fte_match_param, match, misc_parameters.bth_dst_qp, qpn);
	hw_qp->fte_rx = snap_fte_init(sdev, sdev->mdev.fg_rx,
				      MLX5_FLOW_CONTEXT_ACTION_ALLOW,
				      MLX5_FLOW_DESTINATION_TYPE_QP,
				      0,
				      match, NULL);
	if (!hw_qp->fte_rx)
		goto out_reset_tx;

	ret = snap_create_rdma_dev_qp_flow(sdev, hw_qp);
	if (ret)
		goto out_reset_rx;

	return 0;

out_reset_rx:
	snap_fte_reset(hw_qp->fte_rx);
	hw_qp->fte_rx = NULL;
out_reset_tx:
	snap_fte_reset(hw_qp->fte_tx);
	hw_qp->fte_tx = NULL;
out_err:
	return -EINVAL;
}

struct mlx5_snap_hw_qp *snap_create_hw_qp(struct snap_device *sdev,
		struct ibv_qp *qp)
{
	uint8_t in[DEVX_ST_SZ_BYTES(create_qp_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(create_qp_out)] = {0};
	void *qpc = DEVX_ADDR_OF(create_qp_in, in, qpc);
	void *dtor;
	struct mlx5_snap_hw_qp *hw_qp;
	int ret;

	/* HW QP is needed only for Bluefield-1 emulation */
	if (!sdev->mdev.vtunnel)
		return NULL;

	hw_qp = calloc(1, sizeof(*hw_qp));
	if (!hw_qp)
		return NULL;

	DEVX_SET(create_qp_in, in, opcode, MLX5_CMD_OP_CREATE_QP);
	DEVX_SET(create_qp_in, in, input_qpn, qp->qp_num);

	DEVX_SET(qpc, qpc, st, MLX5_QPC_ST_RC);
	DEVX_SET(qpc, qpc, pd, sdev->mdev.pd_id);
	DEVX_SET(qpc, qpc, cs_req, MLX5_REQ_SCAT_DATA32_CQE);
	DEVX_SET(qpc, qpc, cs_res, MLX5_RES_SCAT_DATA32_CQE);
	DEVX_SET(qpc, qpc, rq_type, MLX5_ZERO_LEN_RQ);
	DEVX_SET(qpc, qpc, no_sq, 1);
	DEVX_SET(qpc, qpc, fre, 1);

	hw_qp->mqp = snap_devx_obj_create(sdev, in, sizeof(in), out,
					  sizeof(out), sdev->mdev.vtunnel,
					  DEVX_ST_SZ_BYTES(destroy_qp_in),
					  DEVX_ST_SZ_BYTES(destroy_qp_out));
	if (!hw_qp->mqp)
		goto out_err;

	/* set destructor buffer, since this must be tunneled QP */
	dtor = hw_qp->mqp->dtor_in;

	DEVX_SET(destroy_qp_in, dtor, opcode, MLX5_CMD_OP_DESTROY_QP);
	DEVX_SET(destroy_qp_in, dtor, qpn, qp->qp_num);

	ret = snap_clone_qp(sdev, hw_qp, qp);
	if (ret)
		goto destroy_qp;

	ret = snap_set_hw_qp_steering(sdev, hw_qp);
	if (ret)
		goto reset_qp;
	return hw_qp;

reset_qp:
	hw_qp->parent_qp = NULL;
destroy_qp:
	snap_devx_obj_destroy(hw_qp->mqp);
out_err:
	free(hw_qp);
	return NULL;
}

int snap_destroy_hw_qp(struct mlx5_snap_hw_qp *hw_qp)
{
	int ret;

	snap_reset_hw_qp_steering(hw_qp);
	hw_qp->parent_qp = NULL;
	ret = snap_devx_obj_destroy(hw_qp->mqp);
	free(hw_qp);
	return ret;
}

static void snap_free_flow_table_entries(struct mlx5_snap_flow_table *ft)
{
	free(ft->ftes);
}

static int snap_alloc_flow_table_entries(struct mlx5_snap_flow_table *ft)
{
	int i;

	ft->ftes = calloc(ft->ft_size, sizeof(*ft->ftes));
	if (!ft->ftes)
		return -ENOMEM;

	for (i = 0; i < ft->ft_size; i++)
		ft->ftes[i].idx = i;

	return 0;
}

static int snap_destroy_flow_table(struct mlx5_snap_flow_table *ft)
{
	int ret;

	/* TODO: make sure fg_list is empty */
	ret = snap_devx_obj_destroy(ft->ft);
	snap_free_flow_table_entries(ft);
	pthread_mutex_destroy(&ft->lock);
	free(ft);

	return ret;
}

static struct mlx5_snap_flow_table*
snap_create_flow_table(struct snap_device *sdev, uint32_t table_type,
		struct ibv_context *context)
{
	uint8_t in[DEVX_ST_SZ_BYTES(create_flow_table_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(create_flow_table_out)] = {0};
	struct mlx5_snap_flow_table *ft;
	void *ft_ctx;
	uint8_t ft_level;
	uint8_t ft_log_size;
	int ret;

	ft = calloc(1, sizeof(*ft));
	if (!ft)
		return NULL;

	ret = pthread_mutex_init(&ft->lock, NULL);
	if (ret)
		goto out_free;

	TAILQ_INIT(&ft->fg_list);

	DEVX_SET(create_flow_table_in, in, opcode,
		 MLX5_CMD_OP_CREATE_FLOW_TABLE);
	DEVX_SET(create_flow_table_in, in, table_type, table_type);

	ft_ctx = DEVX_ADDR_OF(create_flow_table_in, in, flow_table_context);
	DEVX_SET(flow_table_context, ft_ctx, table_miss_action,
		 MLX5_FLOW_TABLE_MISS_ACTION_DEF);
	/* at the moment we only need root level tables */
	ft_level = snap_min(SNAP_FT_ROOT_LEVEL, sdev->sctx->mctx.max_ft_level);
	DEVX_SET(flow_table_context, ft_ctx, level, ft_level);
	/* limit the flow table size to 1024 elements, if possible */
	ft_log_size = snap_min(SNAP_FT_LOG_SIZE,
			       sdev->sctx->mctx.log_max_ft_size);
	ft->ft_size = 1 << ft_log_size;
	ret = snap_alloc_flow_table_entries(ft);
	if (ret)
		goto out_free_mutex;

	DEVX_SET(flow_table_context, ft_ctx, log_size, ft_log_size);

	if (context && context != sdev->sctx->context) {
		ft->ft = calloc(1, sizeof(struct mlx5_snap_devx_obj));
		if (!ft->ft)
			goto out_free_ftes;

		ft->ft->obj = mlx5dv_devx_obj_create(context, in, sizeof(in),
						     out, sizeof(out));
		if (!ft->ft->obj) {
			free(ft->ft);
			goto out_free_ftes;
		}

		ft->table_id = DEVX_GET(create_flow_table_out, out, table_id);
		ft->table_type = table_type;
		ft->level = ft_level;
		ft->ft->vtunnel = NULL;
		ft->ft->sdev = NULL;
		ft->ft->obj_id = DEVX_GET(general_obj_out_cmd_hdr, out, obj_id);
	} else {
		ft->ft = snap_devx_obj_create(sdev, in, sizeof(in), out,
				sizeof(out), sdev->mdev.vtunnel,
				DEVX_ST_SZ_BYTES(destroy_flow_table_in),
				DEVX_ST_SZ_BYTES(destroy_flow_table_out));
		if (!ft->ft)
			goto out_free_ftes;

		ft->table_id = DEVX_GET(create_flow_table_out, out, table_id);
		ft->table_type = table_type;
		ft->level = ft_level;

		if (sdev->mdev.vtunnel) {
			void *dtor = ft->ft->dtor_in;

			DEVX_SET(destroy_flow_table_in, dtor, opcode,
				 MLX5_CMD_OP_DESTROY_FLOW_TABLE);
			DEVX_SET(destroy_flow_table_in, dtor, table_type,
				 ft->table_type);
			DEVX_SET(destroy_flow_table_in, dtor, table_id, ft->table_id);
		}
	}

	return ft;

out_free_ftes:
	snap_free_flow_table_entries(ft);
out_free_mutex:
	pthread_mutex_destroy(&ft->lock);
out_free:
	free(ft);
	return NULL;

}

static int snap_set_flow_table_root(struct snap_device *sdev,
		struct mlx5_snap_flow_table *ft)
{
	uint8_t in[DEVX_ST_SZ_BYTES(set_flow_table_root_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(set_flow_table_root_out)] = {0};

	DEVX_SET(set_flow_table_root_in, in, opcode,
		 MLX5_CMD_OP_SET_FLOW_TABLE_ROOT);
	DEVX_SET(set_flow_table_root_in, in, table_type, ft->table_type);
	DEVX_SET(set_flow_table_root_in, in, table_id, ft->table_id);

	return snap_general_tunneled_cmd(sdev, in, sizeof(in), out,
					 sizeof(out), 0);
}

static struct mlx5_snap_flow_table*
snap_create_root_flow_table(struct snap_device *sdev, uint32_t table_type)
{
	struct mlx5_snap_flow_table *ft;
	int ret;

	ft = snap_create_flow_table(sdev, table_type, NULL);
	if (!ft)
		return NULL;

	ret = snap_set_flow_table_root(sdev, ft);
	if (ret)
		goto destroy_ft;

	return ft;

destroy_ft:
	snap_destroy_flow_table(ft);
	return NULL;
}

/*
 * Must be called with flow table (fg->ft) lock held
 */
static int snap_destroy_flow_group(struct mlx5_snap_flow_group *fg)
{
	int ret;

	SNAP_TAILQ_REMOVE_SAFE(&fg->ft->fg_list, fg, entry);
	/* TODO: make sure fte_list is empty */
	ret = snap_devx_obj_destroy(fg->fg);
	free(fg->fte_bitmap);
	pthread_mutex_destroy(&fg->lock);
	free(fg);

	return ret;
}

static struct mlx5_snap_flow_group*
snap_create_flow_group(struct snap_device *sdev,
		struct mlx5_snap_flow_table *ft, uint32_t start_index,
		uint32_t end_index, uint8_t match_criteria_bits,
		void *match_criteria, enum mlx5_snap_flow_group_type type,
		struct ibv_context *context)
{
	uint8_t in[DEVX_ST_SZ_BYTES(create_flow_group_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(create_flow_group_out)] = {0};
	struct mlx5_snap_flow_group *fg;
	void *match;
	int ret, fg_size;

	fg = calloc(1, sizeof(*fg));
	if (!fg)
		return NULL;

	ret = pthread_mutex_init(&fg->lock, NULL);
	if (ret)
		goto out_free;

	fg_size = end_index - start_index + 1;
	fg->fte_bitmap = calloc(fg_size, sizeof(*fg->fte_bitmap));
	if (!fg->fte_bitmap)
		goto free_mutex;

	fg->start_idx = start_index;
	fg->end_idx = end_index;
	fg->ft = ft;
	fg->type = type;

	DEVX_SET(create_flow_group_in, in, opcode,
		 MLX5_CMD_OP_CREATE_FLOW_GROUP);
	DEVX_SET(create_flow_group_in, in, table_type, ft->table_type);
	DEVX_SET(create_flow_group_in, in, table_id, ft->table_id);
	DEVX_SET(create_flow_group_in, in, start_flow_index, start_index);
	DEVX_SET(create_flow_group_in, in, end_flow_index, end_index);

	/* Set the criteria and the mask for the flow group */
	DEVX_SET(create_flow_group_in, in, match_criteria_enable,
		 match_criteria_bits);
	match = DEVX_ADDR_OF(create_flow_group_in, in, match_criteria);
	memcpy(match, match_criteria, DEVX_ST_SZ_BYTES(fte_match_param));

	if (context && context != sdev->sctx->context) {
		fg->fg = calloc(1, sizeof(struct mlx5_snap_devx_obj));
		if (!fg->fg)
			goto out_free_bitmap;

		fg->fg->obj = mlx5dv_devx_obj_create(context, in, sizeof(in),
						     out, sizeof(out));
		if (!fg->fg->obj) {
			free(fg->fg);
			goto out_free_bitmap;
		}

		fg->group_id = DEVX_GET(create_flow_group_out, out, group_id);
	} else {
		fg->fg = snap_devx_obj_create(sdev, in, sizeof(in), out,
				      sizeof(out), sdev->mdev.vtunnel,
				      DEVX_ST_SZ_BYTES(destroy_flow_group_in),
				      DEVX_ST_SZ_BYTES(destroy_flow_group_out));
		if (!fg->fg)
			goto out_free_bitmap;

		fg->group_id = DEVX_GET(create_flow_group_out, out, group_id);

		if (sdev->mdev.vtunnel) {
			void *dtor = fg->fg->dtor_in;

			DEVX_SET(destroy_flow_group_in, dtor, opcode,
				 MLX5_CMD_OP_DESTROY_FLOW_GROUP);
			DEVX_SET(destroy_flow_group_in, dtor, table_type,
				 fg->ft->table_type);
			DEVX_SET(destroy_flow_group_in, dtor, table_id,
				 fg->ft->table_id);
			DEVX_SET(destroy_flow_group_in, dtor, group_id, fg->group_id);
		}
	}

	pthread_mutex_lock(&ft->lock);
	TAILQ_INSERT_HEAD(&ft->fg_list, fg, entry);
	pthread_mutex_unlock(&ft->lock);

	return fg;

out_free_bitmap:
	free(fg->fte_bitmap);
free_mutex:
	pthread_mutex_destroy(&fg->lock);
out_free:
	free(fg);

	return NULL;
}

static int snap_reset_tx_steering(struct snap_device *sdev)
{
	struct mlx5_snap_flow_group *fg, *next;
	int ret = 0;

	pthread_mutex_lock(&sdev->mdev.tx->lock);
	SNAP_TAILQ_FOREACH_SAFE(fg, &sdev->mdev.tx->fg_list, entry, next) {
		if (fg == sdev->mdev.fg_tx)
			sdev->mdev.fg_tx = NULL;
		ret = snap_destroy_flow_group(fg);
		if (ret)
			goto out_unlock;
	}
out_unlock:
	pthread_mutex_unlock(&sdev->mdev.tx->lock);
	if (ret)
		return ret;
	return snap_destroy_flow_table(sdev->mdev.tx);
}

static int snap_init_tx_steering(struct snap_device *sdev)
{
	uint8_t match[DEVX_ST_SZ_BYTES(fte_match_param)] = {0};

	/* FT6 creation (match source QPN) */
	sdev->mdev.tx = snap_create_root_flow_table(sdev, FS_FT_NIC_TX_RDMA);
	if (!sdev->mdev.tx)
		return -ENOMEM;

	/* Flow group that matches source qpn rule (limit to 1024 QPs) */
	DEVX_SET(fte_match_param, match, misc_parameters.source_sqn, 0xffffff);
	sdev->mdev.fg_tx = snap_create_flow_group(sdev, sdev->mdev.tx, 0,
		sdev->mdev.tx->ft_size - 1,
		1 << MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_MISC_PARAMETERS,
		match, SNAP_FG_MATCH, NULL);
	if (!sdev->mdev.fg_tx)
		goto out_err;

	return 0;

out_err:
	snap_destroy_flow_table(sdev->mdev.tx);
	return -ENOMEM;
}

static int snap_reset_rx_steering(struct snap_device *sdev)
{
	struct mlx5_snap_flow_group *fg, *next;
	int ret = 0;

	pthread_mutex_lock(&sdev->mdev.rx->lock);
	SNAP_TAILQ_FOREACH_SAFE(fg, &sdev->mdev.rx->fg_list, entry, next) {
		if (fg == sdev->mdev.fg_rx)
			sdev->mdev.fg_rx = NULL;
		else if (fg == sdev->mdev.fg_rx_miss)
			sdev->mdev.fg_rx_miss = NULL;
		ret = snap_destroy_flow_group(fg);
		if (ret)
			goto out_unlock;
	}
out_unlock:
	pthread_mutex_unlock(&sdev->mdev.rx->lock);
	if (ret)
		return ret;

	return snap_destroy_flow_table(sdev->mdev.rx);
}

static int snap_init_rx_steering(struct snap_device *sdev)
{
	uint8_t match[DEVX_ST_SZ_BYTES(fte_match_param)] = {0};

	/* FT3 creation (match dest QPN, miss send to FW NIC RX ROOT) */
	sdev->mdev.rx = snap_create_root_flow_table(sdev, FS_FT_NIC_RX_RDMA);
	if (!sdev->mdev.rx)
		return -ENOMEM;

	/* Flow group that matches dst qpn rule (limit to 1023 QPs) */
	DEVX_SET(fte_match_param, match, misc_parameters.bth_dst_qp, 0xffffff);
	sdev->mdev.fg_rx = snap_create_flow_group(sdev, sdev->mdev.rx, 0,
		sdev->mdev.rx->ft_size - 2,
		1 << MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_MISC_PARAMETERS,
		match, SNAP_FG_MATCH, NULL);
	if (!sdev->mdev.fg_rx)
		goto out_err;

	/*
	 * Create flow group and entry that forwards to SF (that will be given later).
	 * The reason is:
	 * On a packet coming from QP8 (PF) -> QP7 (SF), QP8 transmit to FT6
	 * (PF root tx table) Jumps to NIC TX root on ECPF
	 * Hit in local loopback table
	 * ** !!!Loops back as RX in the PF side!!! ***
	 * Hits FT3 (PF root rx RDMA table), match on flow group #2 + fte with future
	 * SF mac. Then jumps again to SF NIC RX (root) and from there to QP7.
	 *
	 * There is no table default rule that allows jumping between PF/SF, so
	 * we create a special flow group with one flow table entry that acts
	 * as a catch all rule (zero in match and in criteria should act like
	 * match all).
	 */
	memset(match, 0, sizeof(match));
	sdev->mdev.fg_rx_miss = snap_create_flow_group(sdev, sdev->mdev.rx,
		sdev->mdev.rx->ft_size - 1,
		sdev->mdev.rx->ft_size - 1,
		0, match, SNAP_FG_MISS, NULL);
	if (!sdev->mdev.fg_rx_miss)
		goto out_free_fg;

	return 0;

out_free_fg:
	pthread_mutex_lock(&sdev->mdev.fg_rx->ft->lock);
	snap_destroy_flow_group(sdev->mdev.fg_rx);
	pthread_mutex_unlock(&sdev->mdev.fg_rx->ft->lock);
out_err:
	snap_destroy_flow_table(sdev->mdev.rx);
	return -ENOMEM;
}

static int snap_reset_steering(struct snap_device *sdev)
{
	int ret;

	ret = snap_reset_rx_steering(sdev);
	if (ret)
		return ret;
	return snap_reset_tx_steering(sdev);
}

static int snap_init_steering(struct snap_device *sdev)
{
	int ret;

	ret = snap_init_tx_steering(sdev);
	if (ret)
		return ret;

	ret = snap_init_rx_steering(sdev);
	if (ret)
		goto reset_tx;

	return 0;

reset_tx:
	snap_reset_tx_steering(sdev);
	return ret;
}

static int snap_destroy_pd(struct mlx5_snap_devx_obj *pd)
{
	return snap_devx_obj_destroy(pd);
}

static struct mlx5_snap_devx_obj*
snap_create_pd(struct snap_device *sdev, uint32_t *pd_id)
{
	uint8_t in[DEVX_ST_SZ_BYTES(alloc_pd_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(alloc_pd_out)] = {0};
	struct mlx5_snap_devx_obj *pd;

	DEVX_SET(alloc_pd_in, in, opcode, MLX5_CMD_OP_ALLOC_PD);

	pd = snap_devx_obj_create(sdev, in, sizeof(in), out, sizeof(out),
				  sdev->mdev.vtunnel,
				  DEVX_ST_SZ_BYTES(dealloc_pd_in),
				  DEVX_ST_SZ_BYTES(dealloc_pd_out));
	if (!pd)
		goto out_err;

	*pd_id = DEVX_GET(alloc_pd_out, out, pd);
	if (sdev->mdev.vtunnel) {
		void *dtor = pd->dtor_in;

		DEVX_SET(dealloc_pd_in, dtor, opcode,
			 MLX5_CMD_OP_DEALLOC_PD);
		DEVX_SET(dealloc_pd_in, dtor, pd, *pd_id);
	}

	return pd;

out_err:
	return NULL;
}


static int snap_query_special_context(struct snap_device *sdev)
{
	uint8_t in[DEVX_ST_SZ_BYTES(query_special_contexts_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(query_special_contexts_out)] = {0};
	int ret;

	DEVX_SET(query_special_contexts_in, in, opcode,
		 MLX5_CMD_OP_QUERY_SPECIAL_CONTEXTS);
	ret = snap_general_tunneled_cmd(sdev, in, sizeof(in), out, sizeof(out),
					0);
	if (ret)
		return ret;

	sdev->dma_rkey = DEVX_GET(query_special_contexts_out, out, resd_lkey);

	return 0;
}

/**
 * snap_init_device() - Initialize all the resources for the emulated device
 * @sdev:       snap device
 *
 * Create all the initial resources for a given device
 *
 * Return: Returns 0 on success.
 */
int snap_init_device(struct snap_device *sdev)
{
	int ret;

	if (!sdev->mdev.vtunnel)
		return 0;

	ret = snap_enable_hca(sdev);
	if (ret)
		return ret;

	ret = snap_init_hca(sdev);
	if (ret)
		goto out_disable;

	ret = snap_init_steering(sdev);
	if (ret)
		goto out_teardown;

	sdev->mdev.tunneled_pd = snap_create_pd(sdev,
						&sdev->mdev.pd_id);
	if (!sdev->mdev.tunneled_pd) {
		errno = EINVAL;
		goto out_reset_steering;
	}

	ret = snap_query_special_context(sdev);
	if (ret) {
		errno = ret;
		goto out_free_pd;
	}

	return 0;

out_free_pd:
	snap_destroy_pd(sdev->mdev.tunneled_pd);
out_reset_steering:
	snap_reset_steering(sdev);
out_teardown:
	snap_teardown_hca(sdev);
out_disable:
	snap_disable_hca(sdev);
	return ret;
}

/**
 * snap_teardown_device() - Teardown all the resources for the given device
 *                          that were initialized by snap_init_device
 * @sdev:       snap device
 *
 * Destroy all the initial resources for a given device
 *
 * Return: Returns 0 on success.
 */
int snap_teardown_device(struct snap_device *sdev)
{
	int ret;

	if (!sdev->mdev.vtunnel)
		return 0;

	/* ignore failures from destroying pd and steering because
	 * the objects may have been destroyed by the FLR
	 **/
	snap_destroy_pd(sdev->mdev.tunneled_pd);
	snap_reset_steering(sdev);

	ret = snap_teardown_hca(sdev);
	if (ret)
		return ret;

	return snap_disable_hca(sdev);
}

static int snap_copy_roce_address(struct snap_device *sdev,
		struct ibv_context *context, int idx)
{
	uint8_t qin[DEVX_ST_SZ_BYTES(query_roce_address_in)] = {0};
	uint8_t qout[DEVX_ST_SZ_BYTES(query_roce_address_out)] = {0};
	uint8_t in[DEVX_ST_SZ_BYTES(set_roce_address_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(set_roce_address_out)] = {0};
	int ret;

	DEVX_SET(query_roce_address_in, qin, opcode,
		 MLX5_CMD_OP_QUERY_ROCE_ADDRESS);
	DEVX_SET(query_roce_address_in, qin, roce_address_index, idx);

	ret = mlx5dv_devx_general_cmd(context, qin, sizeof(qin), qout,
				      sizeof(qout));
	if (ret)
		return ret;

	DEVX_SET(set_roce_address_in, in, opcode,
		 MLX5_CMD_OP_SET_ROCE_ADDRESS);
	DEVX_SET(set_roce_address_in, in, roce_address_index, idx);
	DEVX_SET(set_roce_address_in, in, vhca_port_num, 0);

	memcpy(DEVX_ADDR_OF(set_roce_address_in, in, roce_address),
	       DEVX_ADDR_OF(query_roce_address_out, qout, roce_address),
	       DEVX_ST_SZ_BYTES(roce_addr_layout));

	return snap_general_tunneled_cmd(sdev, in, sizeof(in), out,
					 sizeof(out), 0);
}

static int snap_set_device_address(struct snap_device *sdev,
		struct ibv_context *context)
{
	/*
	 * Set the emulated function ("host function") address according to
	 * the networing function address (gid index 0 used for loopback
	 * address).
	 */
	return snap_copy_roce_address(sdev, context, 0);
}

static void snap_destroy_rdma_steering(struct snap_device *sdev)
{
	int ret;

	/*
	 * Ignore errors when destroying flow tables on the
	 * emulated function. They might have been already destroyed
	 * by the FLR.
	 */
	snap_fte_reset(sdev->mdev.rdma_fte_rx);
	sdev->mdev.rdma_fte_rx = NULL;

	snap_fte_reset(sdev->mdev.fte_rx_miss);
	sdev->mdev.fte_rx_miss = NULL;

	snap_destroy_flow_group(sdev->mdev.rdma_fg_rx);
	sdev->mdev.rdma_fg_rx = NULL;

	ret = snap_destroy_flow_table(sdev->mdev.rdma_ft_rx);
	sdev->mdev.rdma_ft_rx = NULL;
	/*
	 * There is a limited number of the flow tables in the kernel,
	 * once we run out of the flow tables we will not be able to create
	 * SQs.
	 */
	if (ret)
		snap_warn("failed to destroy RDMA_FT_RX - possible resource leak\n");
}

static int snap_create_rdma_steering(struct snap_device *sdev,
		struct ibv_context *context)
{
	uint8_t match[DEVX_ST_SZ_BYTES(fte_match_param)] = {0};

	/*
	 * Create FT5 table that will forward everything to emulated function.
	 */
	sdev->mdev.rdma_ft_rx = snap_create_flow_table(sdev, FS_FT_NIC_RX_RDMA,
						       context);
	if (!sdev->mdev.rdma_ft_rx)
		goto out_err;

	/* zero in match and in criteria should act like match all */
	sdev->mdev.rdma_fg_rx = snap_create_flow_group(sdev, sdev->mdev.rdma_ft_rx,
					0, sdev->mdev.rdma_ft_rx->ft_size - 1,
					0,  match, SNAP_FG_MATCH, context);
	if (!sdev->mdev.rdma_fg_rx)
		goto out_destroy_ft;

	/* Now we have the vhca_id for forwarding to/from emulation PF */
	sdev->mdev.fte_rx_miss = snap_fte_init(sdev, sdev->mdev.fg_rx_miss,
				MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
				MLX5_FLOW_DESTINATION_TYPE_VHCA_RX,
				snap_get_dev_vhca_id(context),
				match, NULL);
	if (!sdev->mdev.fte_rx_miss)
		goto out_destroy_fg;

	sdev->mdev.rdma_fte_rx = snap_fte_init(sdev, sdev->mdev.rdma_fg_rx,
					  MLX5_FLOW_CONTEXT_ACTION_FWD_DEST,
					  MLX5_FLOW_DESTINATION_TYPE_VHCA_RX,
					  sdev->pci->mpci.vhca_id, match, context);
	if (!sdev->mdev.rdma_fte_rx)
		goto out_destroy_fte_miss;

	return 0;

out_destroy_fte_miss:
	snap_fte_reset(sdev->mdev.fte_rx_miss);
	sdev->mdev.fte_rx_miss = NULL;
out_destroy_fg:
	snap_destroy_flow_group(sdev->mdev.rdma_fg_rx);
	sdev->mdev.rdma_fg_rx = NULL;
out_destroy_ft:
	snap_destroy_flow_table(sdev->mdev.rdma_ft_rx);
	sdev->mdev.rdma_ft_rx = NULL;
out_err:
	return -EINVAL;
}

/**
 * snap_put_rdma_dev() - Decrease the reference count for sdev RDMA networking
 *                       device in case of a match to the given context.
 *                       Only valid for BF-1 (for BF-2 and above, this function
 *                       doesn't relevant).
 * @sdev:    snap device
 * @context: ibv_context that will be associated to snap device.
 */
void snap_put_rdma_dev(struct snap_device *sdev, struct ibv_context *context)
{
	if (!context)
		return;

	pthread_mutex_lock(&sdev->mdev.rdma_lock);
	/* For BF-1 only */
	if (sdev->mdev.vtunnel) {
		if (context->device == sdev->mdev.rdma_dev->device) {
			if (--sdev->mdev.rdma_dev_users == 0) {
				sdev->mdev.rdma_dev = NULL;
				snap_destroy_rdma_steering(sdev);
			}
		}
	}
	pthread_mutex_unlock(&sdev->mdev.rdma_lock);
}

/**
 * snap_find_get_rdma_dev() - Find the RDMA networking device and increase its
 *                            reference count if matches to the given context.
 * @sdev:    snap device
 * @context: ibv_context that will be associated to snap device.
 *
 * For BF-1, by design, only 1 RDMA device allowed to be associated to the snap
 * device (because of the steering rules), and it's address used to set the
 * address of the emulated snap function. For BF-2 and above, multiple RDMA
 * device can be used as networking devices.
 *
 * Return: The given ibv_context in case it can be associated to sdev for RDMA
 * networking on success and NULL otherwise.
 */
struct ibv_context *snap_find_get_rdma_dev(struct snap_device *sdev,
		struct ibv_context *context)
{
	struct ibv_context *rdma = NULL;
	int ret;

	pthread_mutex_lock(&sdev->mdev.rdma_lock);
	/* For BF-1 only */
	if (sdev->mdev.vtunnel) {
		if (sdev->mdev.rdma_dev) {
			/* In case there is no match - fail */
			if (context->device != sdev->mdev.rdma_dev->device)
				goto out;
		} else {
			/* set sdev address according to context address */
			ret = snap_set_device_address(sdev, context);
			if (ret)
				goto out;

			ret = snap_create_rdma_steering(sdev, context);
			if (ret)
				goto out;

			sdev->mdev.rdma_dev = context;
		}

		sdev->mdev.rdma_dev_users++;
	}

	rdma = context;
out:
	pthread_mutex_unlock(&sdev->mdev.rdma_lock);
	return rdma;
}

/**
 * snap_get_dev_vhca_id() - Return the vhca id for a given device.
 * @context:	Device context.
 *
 * Return: Returns vhca id for a given context on success and -1 otherwise.
 */
uint16_t snap_get_dev_vhca_id(struct ibv_context *context)
{
	uint8_t in[DEVX_ST_SZ_BYTES(query_hca_cap_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(query_hca_cap_out)] = {0};
	int ret;

	DEVX_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	DEVX_SET(query_hca_cap_in, in, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_GENERAL_DEVICE);

	ret = mlx5dv_devx_general_cmd(context, in, sizeof(in), out,
				      sizeof(out));
	if (ret)
		return -1;

	return DEVX_GET(query_hca_cap_out, out,
			capability.cmd_hca_cap.vhca_id);
}

/**
 * snap_device_get_fd() - Return the fd channel for device events. This fd is
 *                        valid only if the device was opened with
 *                        SNAP_DEVICE_FLAGS_EVENT_CHANNEL flag.
 * @sdev:       snap device
 *
 * Return: Returns fd on success and -1 otherwise.
 */
int snap_device_get_fd(struct snap_device *sdev)
{
	if (sdev->mdev.channel)
		return sdev->mdev.channel->fd;
	else
		return -1;
}

/*
 * snap_device_get_events() - Process an event that was raised by device's
 *                            fd. Valid only if the device was opened with
 *                            SNAP_DEVICE_FLAGS_EVENT_CHANNEL flag.
 * @sdev:       snap device
 * @num_events: Maximum number of events to be read from the device event
 *              channel.
 * @events:     [out] Array of size num_events that will be filled.
 *
 * Return: Returns the numbers of events that were read from the event channel.
 *         Negative value indicates a failure.
 */
int snap_device_get_events(struct snap_device *sdev, int num_events,
			   struct snap_event *events)
{
	int i;

	if (!sdev->mdev.channel)
		return -EINVAL;

	for (i = 0; i < num_events; i++) {
		struct mlx5dv_devx_async_event_hdr event_data = {};
		struct mlx5_snap_devx_obj *event_obj;
		struct snap_event *sevent;
		ssize_t bytes;
		int ret;

		sevent = &events[i];
		bytes = mlx5dv_devx_get_event(sdev->mdev.channel, &event_data,
					      sizeof(event_data));
		if (bytes == 0 || (bytes == -1 && errno == EAGAIN))
			break;
		else if (bytes == -1)
			return -errno;

		event_obj = (struct mlx5_snap_devx_obj *)event_data.cookie;
		if (!event_obj)
			return -EINVAL;

		ret = event_obj->consume_event(event_obj, sevent);
		if (ret)
			return ret;
	}

	return i;
}

static int snap_consume_device_emulation_event(struct mlx5_snap_devx_obj *obj,
		struct snap_event *sevent)
{
	struct snap_device *sdev = obj->sdev;

	sevent->obj = sdev;

	switch (sdev->pci->type) {
	case SNAP_NVME_PF:
	case SNAP_NVME_VF:
		sevent->type = SNAP_EVENT_NVME_DEVICE_CHANGE;
		break;
	case SNAP_VIRTIO_NET_PF:
	case SNAP_VIRTIO_NET_VF:
		sevent->type = SNAP_EVENT_VIRTIO_NET_DEVICE_CHANGE;
		break;
	case SNAP_VIRTIO_BLK_PF:
	case SNAP_VIRTIO_BLK_VF:
		sevent->type = SNAP_EVENT_VIRTIO_BLK_DEVICE_CHANGE;
		break;
	case SNAP_VIRTIO_FS_PF:
	case SNAP_VIRTIO_FS_VF:
		sevent->type = SNAP_EVENT_VIRTIO_FS_DEVICE_CHANGE;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static struct mlx5dv_devx_event_channel*
snap_create_event_channel(struct snap_device *sdev)
{
	struct mlx5dv_devx_event_channel *channel;
	uint16_t ev_type = MLX5_EVENT_TYPE_OBJECT_CHANGE;
	int ret;

	channel = mlx5dv_devx_create_event_channel(sdev->sctx->context,
		MLX5DV_DEVX_CREATE_EVENT_CHANNEL_FLAGS_OMIT_EV_DATA);
	if (!channel)
		return NULL;

	ret = mlx5dv_devx_subscribe_devx_event(channel,
		sdev->mdev.device_emulation->obj,
		sizeof(ev_type), &ev_type,
		(uint64_t)sdev->mdev.device_emulation);
	if (ret)
		goto destroy_channel;

	sdev->mdev.device_emulation->consume_event = snap_consume_device_emulation_event;

	return channel;

destroy_channel:
	mlx5dv_devx_destroy_event_channel(channel);
	return NULL;
}

static void
snap_destroy_event_channel(struct mlx5dv_devx_event_channel *channel)
{
	mlx5dv_devx_destroy_event_channel(channel);
}

static struct mlx5_snap_devx_obj*
snap_create_vhca_tunnel(struct snap_device *sdev)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(vhca_tunnel)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)] = {0};
	struct ibv_context *context = sdev->sctx->context;
	uint8_t *vtunnel_in;
	struct mlx5_snap_devx_obj *vtunnel;

	vtunnel = calloc(1, sizeof(*vtunnel));
	if (!vtunnel)
		goto out_err;

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_OBJ_TYPE_VHCA_TUNNEL);

	vtunnel_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
	DEVX_SET(vhca_tunnel, vtunnel_in, vhca_id, sdev->pci->mpci.vhca_id);

	vtunnel->obj = mlx5dv_devx_obj_create(context, in, sizeof(in), out,
					      sizeof(out));
	if (!vtunnel->obj)
		goto out_free;

	vtunnel->obj_id = DEVX_GET(general_obj_out_cmd_hdr, out, obj_id);
	vtunnel->sdev = sdev;

	return vtunnel;

out_free:
	free(vtunnel);
out_err:
	return NULL;
}

static void snap_set_nvme_hotplug_device(const struct snap_hotplug_attr *attr,
		uint8_t *device_in)
{
	DEVX_SET(device, device_in, device_type,
		 MLX5_HOTPLUG_DEVICE_TYPE_NVME);
	DEVX_SET64(device, device_in, emulation_initial_regs.nvme.cap,
		   htobe64(attr->regs.nvme.cap.raw));
	DEVX_SET(device, device_in, emulation_initial_regs.nvme.vs,
		 htobe32(attr->regs.nvme.vs.raw));
}

static void snap_set_virtio_net_hotplug_device(const struct snap_hotplug_attr *attr,
		uint8_t *device_in)
{
	DEVX_SET(device, device_in, device_type,
		 MLX5_HOTPLUG_DEVICE_TYPE_VIRTIO_NET);
	DEVX_SET64(device, device_in,
		   emulation_initial_regs.virtio_net.device_features,
		   attr->regs.virtio_net.device_features);
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_net.queue_size,
		 attr->regs.virtio_net.queue_size);
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_net.virtio_net_config.mac_15_0,
		 attr->regs.virtio_net.mac & 0xffff);
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_net.virtio_net_config.mac_47_16,
		 attr->regs.virtio_net.mac >> 16);
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_net.virtio_net_config.max_virtqueue_pairs,
		 attr->regs.virtio_net.max_queues);
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_net.virtio_net_config.mtu,
		 attr->regs.virtio_net.mtu);
}

static void
snap_set_virtio_blk_hotplug_device(const struct snap_hotplug_attr *attr,
				   uint8_t *device_in)
{
	DEVX_SET(device, device_in, device_type,
		 MLX5_HOTPLUG_DEVICE_TYPE_VIRTIO_BLK);
	DEVX_SET64(device, device_in,
		   emulation_initial_regs.virtio_blk.device_features,
		   attr->regs.virtio_blk.device_features);
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_blk.queue_size,
		 attr->regs.virtio_blk.queue_size);
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_blk.num_queues,
		 attr->regs.virtio_blk.max_queues);
	/* For VIRTIO_BLK_F_MQ need to set max_queues in virtio_blk_config too */
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_blk.virtio_blk_config.num_queues,
		 attr->regs.virtio_blk.max_queues);
	DEVX_SET64(device, device_in,
		   emulation_initial_regs.virtio_blk.virtio_blk_config.capacity,
		   attr->regs.virtio_blk.capacity);
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_blk.virtio_blk_config.size_max,
		 attr->regs.virtio_blk.size_max);
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_blk.virtio_blk_config.seg_max,
		 attr->regs.virtio_blk.seg_max);
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_blk.virtio_blk_config.cylinders,
		 attr->regs.virtio_blk.cylinders);
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_blk.virtio_blk_config.heads,
		 attr->regs.virtio_blk.heads);
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_blk.virtio_blk_config.sectors,
		 attr->regs.virtio_blk.sectors);
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_blk.virtio_blk_config.blk_size,
		 attr->regs.virtio_blk.blk_size);
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_blk.virtio_blk_config.physical_blk_exp,
		 attr->regs.virtio_blk.physical_blk_exp);
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_blk.virtio_blk_config.alignment_offset,
		 attr->regs.virtio_blk.alignment_offset);
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_blk.virtio_blk_config.min_io_size,
		 attr->regs.virtio_blk.min_io_size);
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_blk.virtio_blk_config.opt_io_size,
		 attr->regs.virtio_blk.opt_io_size);
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_blk.virtio_blk_config.writeback,
		 attr->regs.virtio_blk.writeback);
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_blk.virtio_blk_config.max_discard_sectors,
		 attr->regs.virtio_blk.max_discard_sectors);
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_blk.virtio_blk_config.max_discard_seg,
		 attr->regs.virtio_blk.max_discard_seg);
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_blk.virtio_blk_config.discard_sector_alignment,
		 attr->regs.virtio_blk.discard_sector_alignment);
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_blk.virtio_blk_config.max_write_zeroes_sectors,
		 attr->regs.virtio_blk.max_write_zeroes_sectors);
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_blk.virtio_blk_config.max_write_zeroes_segs,
		 attr->regs.virtio_blk.max_write_zeroes_segs);
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_blk.virtio_blk_config.write_zeroes_may_unmap,
		 attr->regs.virtio_blk.write_zeroes_may_unmap);
}

static void
snap_set_virtio_fs_hotplug_device(const struct snap_hotplug_attr *attr,
				  uint8_t *device_in)
{
	uint8_t *fs_config;

	DEVX_SET(device, device_in, device_type,
		 MLX5_HOTPLUG_DEVICE_TYPE_VIRTIO_FS);
	DEVX_SET64(device, device_in,
		   emulation_initial_regs.virtio_fs.device_features,
		   attr->regs.virtio_fs.device_features);
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_fs.num_queues,
		 attr->regs.virtio_fs.num_request_queues + 1);
	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_fs.queue_size,
		 attr->regs.virtio_fs.queue_size);

	if (attr->regs.virtio_fs.tag[0] != 0) {
		fs_config = DEVX_ADDR_OF(device, device_in, emulation_initial_regs.virtio_fs.virtio_fs_config.tag);
		memcpy(fs_config, attr->regs.virtio_fs.tag, SNAP_VIRTIO_FS_DEV_CFG_TAG_LEN);
	}

	DEVX_SET(device, device_in,
		 emulation_initial_regs.virtio_fs.virtio_fs_config.num_request_queues,
		 attr->regs.virtio_fs.num_request_queues);
}
void snap_get_pci_attr(struct snap_pci_attr *pci_attr,
		void *pci_params_out)
{
	pci_attr->device_id = DEVX_GET(device_pci_parameters,
				       pci_params_out, device_id);
	pci_attr->vendor_id = DEVX_GET(device_pci_parameters,
				       pci_params_out, vendor_id);
	pci_attr->revision_id = DEVX_GET(device_pci_parameters,
					 pci_params_out, revision_id);
	pci_attr->class_code = DEVX_GET(device_pci_parameters,
					pci_params_out, class_code);
	pci_attr->subsystem_id = DEVX_GET(device_pci_parameters,
					  pci_params_out, subsystem_id);
	pci_attr->subsystem_vendor_id = DEVX_GET(device_pci_parameters,
						 pci_params_out,
						 subsystem_vendor_id);
	pci_attr->num_msix = DEVX_GET(device_pci_parameters,
				      pci_params_out, num_msix);
	pci_attr->num_of_vfs = DEVX_GET(device_pci_parameters,
					pci_params_out, num_of_vfs);
}

static int snap_query_device_emulation(struct snap_device *sdev)
{
	struct snap_nvme_device_attr nvme_attr = {};
	struct snap_virtio_net_device_attr net_attr = {};
	struct snap_virtio_blk_device_attr blk_attr = {};
	struct snap_virtio_fs_device_attr fs_attr = {};
	int ret;

	if (!sdev->pci->plugged)
		return -ENODEV;

	switch (sdev->pci->type) {
	case SNAP_NVME_PF:
	case SNAP_NVME_VF:
		ret = snap_nvme_query_device(sdev, &nvme_attr);
		if (!ret) {
			sdev->mod_allowed_mask = nvme_attr.modifiable_fields;
			sdev->crossed_vhca_mkey = nvme_attr.crossed_vhca_mkey;
		}
		break;
	case SNAP_VIRTIO_NET_PF:
	case SNAP_VIRTIO_NET_VF:
		ret = snap_virtio_net_query_device(sdev, &net_attr);
		if (!ret) {
			sdev->mod_allowed_mask = net_attr.modifiable_fields;
			sdev->crossed_vhca_mkey = net_attr.crossed_vhca_mkey;
		}
		break;
	case SNAP_VIRTIO_BLK_PF:
	case SNAP_VIRTIO_BLK_VF:
		ret = snap_virtio_blk_query_device(sdev, &blk_attr);
		if (!ret) {
			sdev->mod_allowed_mask = blk_attr.modifiable_fields;
			sdev->crossed_vhca_mkey = blk_attr.crossed_vhca_mkey;
		}
		break;
	case SNAP_VIRTIO_FS_PF:
	case SNAP_VIRTIO_FS_VF:
		ret = snap_virtio_fs_query_device(sdev, &fs_attr);
		if (!ret) {
			sdev->mod_allowed_mask = fs_attr.modifiable_fields;
			sdev->crossed_vhca_mkey = fs_attr.crossed_vhca_mkey;
		}
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

void snap_emulation_device_destroy(struct snap_device *sdev)
{
	mlx5dv_devx_obj_destroy(sdev->mdev.device_emulation->obj);
	free(sdev->mdev.device_emulation);
	sdev->mdev.device_emulation = NULL;
	sdev->mdev.context = NULL;
}

static struct mlx5_snap_devx_obj*
snap_create_virtio_net_device_emulation(struct snap_device *sdev,
					struct snap_device_attr *attr)
{
	struct snap_virtio_caps *net_caps = &sdev->sctx->virtio_net_caps;
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(virtio_net_device_emulation)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)] = {0};
	struct ibv_context *context = sdev->sctx->context;
	uint8_t *device_emulation_in;
	struct mlx5_snap_devx_obj *device_emulation;

	device_emulation = calloc(1, sizeof(*device_emulation));
	if (!device_emulation)
		goto out_err;

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_OBJ_TYPE_VIRTIO_NET_DEVICE_EMULATION);

	device_emulation_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
	DEVX_SET(virtio_net_device_emulation, device_emulation_in, vhca_id,
		 sdev->pci->mpci.vhca_id);
	DEVX_SET(virtio_net_device_emulation, device_emulation_in,
		 resources_on_emulation_manager,
		 sdev->sctx->mctx.virtio_net_need_tunnel ? 0 : 1);
	DEVX_SET(virtio_net_device_emulation, device_emulation_in,
		 q_cfg_version, sdev->sctx->virtio_net_caps.virtio_q_cfg_v2 ? 1 : 0);
	DEVX_SET(virtio_net_device_emulation, device_emulation_in, enabled, 1);
	DEVX_SET(virtio_net_device_emulation, device_emulation_in,
		 emulated_dev_db_cq_map,
		 sdev->sctx->virtio_net_caps.emulated_dev_db_cq_map ? 1 : 0);
	DEVX_SET(virtio_net_device_emulation, device_emulation_in,
		 emulated_dev_eq,
		 sdev->sctx->virtio_net_caps.emulated_dev_eq ? 1 : 0);

	if ((attr->flags & SNAP_DEVICE_FLAGS_VF_DYN_MSIX) &&
	    (net_caps->max_num_vf_dynamic_msix != 0)) {
		DEVX_SET(virtio_net_device_emulation, device_emulation_in,
			 dynamic_vf_msix_control, 1);
		snap_debug("Set dynamic_vf_msix_control for PF\n");
	}

	device_emulation->obj = mlx5dv_devx_obj_create(context, in, sizeof(in),
						       out, sizeof(out));
	if (!device_emulation->obj)
		goto out_free;

	device_emulation->obj_id = DEVX_GET(general_obj_out_cmd_hdr, out, obj_id);
	device_emulation->sdev = sdev;

	return device_emulation;

out_free:
	free(device_emulation);
out_err:
	return NULL;
}

static struct mlx5_snap_devx_obj*
snap_create_virtio_blk_device_emulation(struct snap_device *sdev)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(virtio_blk_device_emulation)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)] = {0};
	struct ibv_context *context = sdev->sctx->context;
	uint8_t *device_emulation_in;
	struct mlx5_snap_devx_obj *device_emulation;

	device_emulation = calloc(1, sizeof(*device_emulation));
	if (!device_emulation)
		goto out_err;

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_OBJ_TYPE_VIRTIO_BLK_DEVICE_EMULATION);

	device_emulation_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
	DEVX_SET(virtio_blk_device_emulation, device_emulation_in, vhca_id,
		 sdev->pci->mpci.vhca_id);
	DEVX_SET(virtio_blk_device_emulation, device_emulation_in,
		 resources_on_emulation_manager,
		 sdev->sctx->mctx.virtio_blk_need_tunnel ? 0 : 1);
	DEVX_SET(virtio_blk_device_emulation, device_emulation_in, enabled, 1);

	/* these properties are required by the DPA but ACE cannot work with them */
	if (snap_env_getenv(SNAP_QUEUE_PROVIDER) == SNAP_DPA_Q_PROVIDER) {
		DEVX_SET(virtio_blk_device_emulation, device_emulation_in, emulated_dev_db_cq_map, 1);
		DEVX_SET(virtio_blk_device_emulation, device_emulation_in, emulated_dev_eq, 1);
	}

	device_emulation->obj = mlx5dv_devx_obj_create(context, in, sizeof(in),
						       out, sizeof(out));
	if (!device_emulation->obj)
		goto out_free;

	device_emulation->obj_id = DEVX_GET(general_obj_out_cmd_hdr, out, obj_id);
	device_emulation->sdev = sdev;

	return device_emulation;

out_free:
	free(device_emulation);
out_err:
	return NULL;
}

static struct mlx5_snap_devx_obj*
snap_create_virtio_fs_device_emulation(struct snap_device *sdev)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(virtio_fs_device_emulation)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)] = {0};
	struct ibv_context *context = sdev->sctx->context;
	uint8_t *device_emulation_in;
	struct mlx5_snap_devx_obj *device_emulation;

	device_emulation = calloc(1, sizeof(*device_emulation));
	if (!device_emulation)
		goto out_err;

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_OBJ_TYPE_VIRTIO_FS_DEVICE_EMULATION);

	device_emulation_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
	DEVX_SET(virtio_fs_device_emulation, device_emulation_in, vhca_id,
		 sdev->pci->mpci.vhca_id);
	DEVX_SET(virtio_fs_device_emulation, device_emulation_in,
		 resources_on_emulation_manager,
		 sdev->sctx->mctx.virtio_fs_need_tunnel ? 0 : 1);
	DEVX_SET(virtio_fs_device_emulation, device_emulation_in, enabled, 1);

	device_emulation->obj = mlx5dv_devx_obj_create(context, in, sizeof(in),
						       out, sizeof(out));
	if (!device_emulation->obj)
		goto out_free;

	device_emulation->obj_id = DEVX_GET(general_obj_out_cmd_hdr, out, obj_id);
	device_emulation->sdev = sdev;

	return device_emulation;

out_free:
	free(device_emulation);
out_err:
	return NULL;
}

static struct mlx5_snap_devx_obj*
snap_create_nvme_device_emulation(struct snap_device *sdev,
				  struct snap_device_attr *attr)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(nvme_device_emulation)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)] = {0};
	struct ibv_context *context = sdev->sctx->context;
	uint8_t *device_emulation_in;
	struct mlx5_snap_devx_obj *device_emulation;

	device_emulation = calloc(1, sizeof(*device_emulation));
	if (!device_emulation)
		goto out_err;

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_OBJ_TYPE_NVME_DEVICE_EMULATION);

	device_emulation_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
	DEVX_SET(nvme_device_emulation, device_emulation_in, vhca_id,
		 sdev->pci->mpci.vhca_id);
	DEVX_SET(nvme_device_emulation, device_emulation_in,
		 resources_on_emulation_manager,
		 sdev->sctx->mctx.nvme_need_tunnel ? 0 : 1);
	if (attr->counter_set_id) {
		DEVX_SET(nvme_device_emulation, device_emulation_in,
			 counter_set_id, attr->counter_set_id);
	}

	device_emulation->obj = mlx5dv_devx_obj_create(context, in, sizeof(in),
						       out, sizeof(out));
	if (!device_emulation->obj)
		goto out_free;

	device_emulation->obj_id = DEVX_GET(general_obj_out_cmd_hdr, out, obj_id);
	device_emulation->sdev = sdev;

	return device_emulation;

out_free:
	free(device_emulation);
out_err:
	return NULL;
}

struct mlx5_snap_devx_obj*
snap_emulation_device_create(struct snap_device *sdev,
			     struct snap_device_attr *attr)
{
	if (!sdev->pci->plugged)
		return NULL;

	switch (sdev->pci->type) {
	case SNAP_NVME_PF:
	case SNAP_NVME_VF:
		return snap_create_nvme_device_emulation(sdev, attr);
	case SNAP_VIRTIO_NET_PF:
	case SNAP_VIRTIO_NET_VF:
		return snap_create_virtio_net_device_emulation(sdev, attr);
	case SNAP_VIRTIO_BLK_PF:
	case SNAP_VIRTIO_BLK_VF:
		return snap_create_virtio_blk_device_emulation(sdev);
	case SNAP_VIRTIO_FS_PF:
	case SNAP_VIRTIO_FS_VF:
		return snap_create_virtio_fs_device_emulation(sdev);
	default:
		return NULL;
	}
}

/**
 * snap_get_pf_list() - Get an array of snap pci devices for a given context.
 * @sctx:       snap context
 * @type:       the type of the needed PCI PF (e.g. SNAP_NVME_PF)
 * @pfs:        the list that will hold the requested PFs (output)
 *
 * Receive an allocated array of snap_pci devices that will be filled with the
 * available PFs for a given snap context and type.
 *
 * Return: Returns an actual number of filled snap pci devices.
 */
int snap_get_pf_list(struct snap_context *sctx, enum snap_emulation_type type,
		struct snap_pci **pfs)
{
	struct snap_pfs_ctx *pfs_ctx;
	int i;
	uint8_t *out;
	int ret = 0, output_size;
	bool clear_dirty = true;

	if (type == SNAP_NVME)
		pfs_ctx = &sctx->nvme_pfs;
	else if (type == SNAP_VIRTIO_NET)
		pfs_ctx = &sctx->virtio_net_pfs;
	else if (type == SNAP_VIRTIO_BLK)
		pfs_ctx = &sctx->virtio_blk_pfs;
	else if (type == SNAP_VIRTIO_FS)
		pfs_ctx = &sctx->virtio_fs_pfs;
	else
		return 0;

	for (i = 0; i < pfs_ctx->max_pfs; i++)
		pfs[i] = &pfs_ctx->pfs[i];

	/* ignore any failure happened to update pci_bdf */
	pthread_mutex_lock(&sctx->hotplug_lock);
	if (pfs_ctx->dirty) {
		output_size = DEVX_ST_SZ_BYTES(query_emulated_functions_info_out) +
			DEVX_ST_SZ_BYTES(emulated_function_info) * (pfs_ctx->max_pfs);
		out = calloc(1, output_size);
		if (!out) {
			snap_warn("alloc memory for output structure failed\n");
			goto out;
		}

		ret = snap_query_functions_info(sctx, pfs_ctx->type,
				SNAP_UNINITIALIZED_VHCA_ID, out, output_size);
		if (ret) {
			snap_warn("query functions info failed, ret:%d\n", ret);
			free(out);
			goto out;
		}

		for (i = 0; i < pfs_ctx->max_pfs; i++) {
			if (pfs[i]->hotplugged && !pfs[i]->pci_bdf.raw) {
				ret = snap_pf_get_pci_info(pfs[i], out);
				if (ret) {
					snap_warn("pf get pci info failed, ret:%d\n", ret);
					free(out);
					goto out;
				}
			}

			if (!pfs[i]->pci_bdf.raw)
				clear_dirty = false;
		}

		if (clear_dirty)
			pfs_ctx->dirty = false;
	}

out:
	pthread_mutex_unlock(&sctx->hotplug_lock);
	return i;
}

static int snap_hotplug_device(struct ibv_context *context,
		const struct snap_hotplug_attr *attr, int *vhca_id)
{
	int ret = 0;
	uint8_t in[DEVX_ST_SZ_BYTES(hotplug_device_input)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(hotplug_device_output)] = {0};
	uint8_t *device_in;

	DEVX_SET(hotplug_device_input, in, opcode, MLX5_CMD_OP_HOTPLUG_DEVICE);

	device_in = in + (DEVX_ST_SZ_BYTES(hotplug_device_input) -
						DEVX_ST_SZ_BYTES(device));
	switch (attr->type) {
	case SNAP_NVME:
		snap_set_nvme_hotplug_device(attr, device_in);
		break;
	case SNAP_VIRTIO_NET:
		snap_set_virtio_net_hotplug_device(attr, device_in);
		break;
	case SNAP_VIRTIO_BLK:
		snap_set_virtio_blk_hotplug_device(attr, device_in);
		break;
	case SNAP_VIRTIO_FS:
		snap_set_virtio_fs_hotplug_device(attr, device_in);
		break;
	default:
		ret = -EINVAL;
		goto out_err;
	}

	DEVX_SET(device, device_in, total_vf, attr->max_vfs);
	DEVX_SET(device, device_in, initial_registers_valid, !attr->use_default_regs);
	DEVX_SET(device, device_in, pci_params.device_id, attr->pci_attr.device_id);
	DEVX_SET(device, device_in, pci_params.vendor_id, attr->pci_attr.vendor_id);
	DEVX_SET(device, device_in, pci_params.revision_id, attr->pci_attr.revision_id);
	DEVX_SET(device, device_in, pci_params.class_code, attr->pci_attr.class_code);
	DEVX_SET(device, device_in, pci_params.subsystem_id, attr->pci_attr.subsystem_id);
	DEVX_SET(device, device_in, pci_params.subsystem_vendor_id,
		attr->pci_attr.subsystem_vendor_id);
	DEVX_SET(device, device_in, pci_params.num_msix, attr->pci_attr.num_msix);

	ret = mlx5dv_devx_general_cmd(context, in, sizeof(in), out, sizeof(out));
	if (ret)
		goto out_err;

	*vhca_id = DEVX_GET(hotplug_device_output, out, hotplug_device_object.vhca_id);

out_err:
	return ret;
}

static int snap_hotunplug_device(struct ibv_context *context, int vhca_id)
{
	uint8_t in[DEVX_ST_SZ_BYTES(hotunplug_device_input)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(hotunplug_device_output)] = {0};
	uint8_t *device_in;

	DEVX_SET(hotunplug_device_input, in, opcode,
		 MLX5_CMD_OP_HOTUNPLUG_DEVICE);

	device_in = in + (DEVX_ST_SZ_BYTES(hotunplug_device_input) -
						DEVX_ST_SZ_BYTES(device));
	DEVX_SET(device, device_in, vhca_id, vhca_id);

	// 0 is returned or the value of errno on a failure
	return mlx5dv_devx_general_cmd(context, in, sizeof(in), out, sizeof(out));
}

/**
 * snap_hotunplug_pf() - Unplug a snap physical PCI function from host
 * @pf:        snap physical PCI function
 *
 * Unplug a previously hot-plugged snap physical PCI function from the host.
 *
 * Return 0 on success or errno on failure.
 */
int snap_hotunplug_pf(struct snap_pci *pf)
{
	int ret;

	if (!pf->plugged || !pf->hotplugged)
		return 0;

	ret = snap_hotunplug_device(pf->sctx->context, pf->mpci.vhca_id);
	if (ret)
		return ret;

	snap_free_virtual_functions(pf);

	pf->mpci.vhca_id = SNAP_UNINITIALIZED_VHCA_ID;
	pf->hotplugged = false;
	pf->plugged = false;
	return 0;
}

/**
 * snap_hotplug_pf() - Plug a snap PCI function from a given snap context
 * @sctx:       snap context
 * @attr:       snap hotplug device attributes
 *
 * Hotplugs a physical PCI function that will be seen to the host according
 * to the requested attributes.
 *
 * Return: On success, return snap PCI device. NULL otherwise and errno will be
 * set to indicate the failure reason.
 */
struct snap_pci *snap_hotplug_pf(struct snap_context *sctx,
				 struct snap_hotplug_attr *attr)
{
	uint8_t *out;
	struct snap_pfs_ctx *pfs_ctx;
	struct snap_pci *pf = NULL;
	int ret, output_size, i;

	if (attr->type == SNAP_NVME) {
		pfs_ctx = &sctx->nvme_pfs;
	} else if (attr->type == SNAP_VIRTIO_NET) {
		pfs_ctx = &sctx->virtio_net_pfs;
	} else if (attr->type == SNAP_VIRTIO_BLK) {
		pfs_ctx = &sctx->virtio_blk_pfs;
	} else if (attr->type == SNAP_VIRTIO_FS) {
		pfs_ctx = &sctx->virtio_fs_pfs;
	} else {
		errno = EINVAL;
		goto out_err;
	}

	for (i = 0; i < pfs_ctx->max_pfs; i++) {
		if (!pfs_ctx->pfs[i].plugged) {
			pf = &pfs_ctx->pfs[i];
			break;
		}
	}

	if (!pf) {
		errno = ENODEV;
		goto out_err;
	}

	if (!(sctx->hotplug.supported_types & attr->type)) {
		errno = ENOTSUP;
		goto out_err;
	}

	ret = snap_hotplug_device(sctx->context, attr, &pf->mpci.vhca_id);
	if (ret) {
		errno = EINVAL;
		goto out_err;
	}

	pf->plugged = true;
	pf->hotplugged = true;

	output_size = DEVX_ST_SZ_BYTES(query_emulated_functions_info_out) +
		      DEVX_ST_SZ_BYTES(emulated_function_info) * (pfs_ctx->max_pfs);
	out = calloc(1, output_size);
	if (!out) {
		errno = ENOMEM;
		goto out_hotunplug;
	}

	ret = snap_query_functions_info(sctx, attr->type,
					SNAP_UNINITIALIZED_VHCA_ID,
					out, output_size);
	if (ret)
		goto free_cmd;

	ret = snap_pf_get_pci_info(pf, out);
	if (ret)
		goto free_cmd;

	if (!pf->pci_bdf.raw) {
		pthread_mutex_lock(&sctx->hotplug_lock);
		pfs_ctx->dirty = true;
		pthread_mutex_unlock(&sctx->hotplug_lock);
	}
	snap_debug("PCI enumeration done\n");

	free(out);

	return pf;

free_cmd:
	free(out);
out_hotunplug:
	pf->hotplugged = false;
	pf->plugged = false;
	snap_hotunplug_device(pf->sctx->context, pf->mpci.vhca_id);
out_err:
	return NULL;
}

/**
 * snap_rescan_vfs() - Reconfigure PF's virtual functions
 * @pf:      physical function pci context
 * @num_vfs: expected number of VFs to scan
 *
 * Releases currently configured virtual functions and configures
 * new virtual functions.
 *
 * The function should be called if host changed number of virtual functions
 * on the @pf
 *
 * Return: 0 on success, -1 otherwise
 */
int snap_rescan_vfs(struct snap_pci *pf, size_t num_vfs)
{
	snap_free_virtual_functions(pf);
	sleep(1);
	return snap_alloc_virtual_functions(pf, num_vfs);
}

/**
 * snap_open_device() - Create a new snap device from snap context
 * @sctx:       snap context
 * @attr:       snap device attributes
 *
 * Allocates a new snap device that will be associated to the given snap
 * context according to the requested attributes.
 *
 * Return: Returns a new snap_device in case of success, NULL otherwise and
 * errno will be set to indicate the failure reason.
 */
struct snap_device *snap_open_device(struct snap_context *sctx,
				     struct snap_device_attr *attr)
{
	struct snap_device *sdev;
	struct snap_pfs_ctx *pfs;
	bool need_tunnel;
	int ret;

	if (attr->pf_id < 0) {
		errno = EINVAL;
		return NULL;
	}

	if ((attr->type == SNAP_NVME_PF || attr->type == SNAP_NVME_VF) &&
	    attr->pf_id < sctx->nvme_pfs.max_pfs) {
		pfs = &sctx->nvme_pfs;
		need_tunnel = sctx->mctx.nvme_need_tunnel;
	} else if ((attr->type == SNAP_VIRTIO_NET_PF || attr->type == SNAP_VIRTIO_NET_VF) &&
		   attr->pf_id < sctx->virtio_net_pfs.max_pfs) {
		pfs = &sctx->virtio_net_pfs;
		need_tunnel = sctx->mctx.virtio_net_need_tunnel;
	} else if ((attr->type == SNAP_VIRTIO_BLK_PF || attr->type == SNAP_VIRTIO_BLK_VF) &&
		   attr->pf_id < sctx->virtio_blk_pfs.max_pfs) {
		pfs = &sctx->virtio_blk_pfs;
		need_tunnel = sctx->mctx.virtio_blk_need_tunnel;
	} else if ((attr->type == SNAP_VIRTIO_FS_PF || attr->type == SNAP_VIRTIO_FS_VF) &&
		   attr->pf_id < sctx->virtio_fs_pfs.max_pfs) {
		pfs = &sctx->virtio_fs_pfs;
		need_tunnel = sctx->mctx.virtio_fs_need_tunnel;
	} else {
		errno = EINVAL;
		goto out_err;
	}

	sdev = calloc(1, sizeof(*sdev));
	if (!sdev) {
		errno = ENOMEM;
		goto out_err;
	}

	ret = pthread_mutex_init(&sdev->mdev.rdma_lock, NULL);
	if (ret) {
		errno = ENOMEM;
		goto out_free;
	}

	/*
	 * RDMA address should be set for emulated functions on BF-1 only (due
	 * to special steering model). Set this address as soon as possible
	 * (when getting an association to the first QP for emulated queue).
	 * For BF-2, we can use many RDMA interfaces for traffic since we have
	 * the cross-gvmi mkey enabled.
	 */
	sdev->mdev.rdma_dev_users = 0;
	sdev->mdev.context = attr->context;

	sdev->sctx = sctx;
	if (attr->type & (SNAP_VIRTIO_NET_VF | SNAP_VIRTIO_BLK_VF | SNAP_NVME_VF | SNAP_VIRTIO_FS_VF)) {
		if (attr->vf_id < 0 || attr->vf_id >= pfs->pfs[attr->pf_id].num_vfs) {
			errno = EINVAL;
			goto out_free_mutex;
		}
		sdev->pci = &pfs->pfs[attr->pf_id].vfs[attr->vf_id];
	} else
		sdev->pci = &pfs->pfs[attr->pf_id];
	sdev->mdev.device_emulation = snap_emulation_device_create(sdev, attr);
	if (!sdev->mdev.device_emulation) {
		errno = EINVAL;
		goto out_free_mutex;
	}

	ret = snap_query_device_emulation(sdev);
	if (ret) {
		errno = ret;
		goto out_free_device_emulation;
	}

	/* This should be done only for BF-1 */
	if (need_tunnel) {
		sdev->mdev.vtunnel = snap_create_vhca_tunnel(sdev);
		if (!sdev->mdev.vtunnel) {
			errno = EINVAL;
			goto out_free_device_emulation;
		}
	}

	if (attr->flags & SNAP_DEVICE_FLAGS_EVENT_CHANNEL) {
		sdev->mdev.channel = snap_create_event_channel(sdev);
		if (!sdev->mdev.channel) {
			errno = EINVAL;
			goto out_free_tunnel;
		}
	}

	pthread_mutex_lock(&sctx->lock);
	TAILQ_INSERT_HEAD(&sctx->device_list, sdev, entry);
	pthread_mutex_unlock(&sctx->lock);

	return sdev;

out_free_tunnel:
	if (sdev->mdev.vtunnel)
		snap_destroy_vhca_tunnel(sdev);
out_free_device_emulation:
	snap_emulation_device_destroy(sdev);
out_free_mutex:
	pthread_mutex_destroy(&sdev->mdev.rdma_lock);
out_free:
	free(sdev);
out_err:
	return NULL;
}

/**
 * snap_close_device() - Destroy a snap device
 * @sdev:       snap device
 *
 * Destroy and free a snap device.
 */
void snap_close_device(struct snap_device *sdev)
{
	struct snap_context *sctx = sdev->sctx;

	pthread_mutex_lock(&sctx->lock);
	SNAP_TAILQ_REMOVE_SAFE(&sctx->device_list, sdev, entry);
	pthread_mutex_unlock(&sctx->lock);

	if (sdev->mdev.channel)
		snap_destroy_event_channel(sdev->mdev.channel);
	if (sdev->mdev.vtunnel)
		snap_destroy_vhca_tunnel(sdev);
	snap_emulation_device_destroy(sdev);
	pthread_mutex_destroy(&sdev->mdev.rdma_lock);
	free(sdev);
}

/**
 * snap_open() - Opens and create a new snap context
 * @ibdev:       RDMA device
 *
 * Return: Returns snap_context in case of success, NULL otherwise and errno
 * will be set to indicate the failure reason.
 */
struct snap_context *snap_open(struct ibv_device *ibdev)
{
	struct mlx5dv_context_attr attrs = {};
	struct snap_context *sctx;
	struct ibv_context *context;
	int rc;

	if (!mlx5dv_is_supported(ibdev)) {
		errno = ENOTSUP;
		goto out_err;
	}

	attrs.flags = MLX5DV_CONTEXT_FLAGS_DEVX;
	context = mlx5dv_open_device(ibdev, &attrs);
	if (!context) {
		errno = EAGAIN;
		goto out_err;
	}

	sctx = calloc(1, sizeof(*sctx));
	if (!sctx) {
		errno = ENOMEM;
		goto out_close_device;
	}

	sctx->context = context;
	if (snap_set_device_emulation_caps(sctx)) {
		errno = EAGAIN;
		goto out_free;
	}

	if (!sctx->emulation_caps) {
		errno = EINVAL;
		goto out_free;
	}

	rc = snap_query_emulation_caps(sctx);
	if (rc) {
		errno = EINVAL;
		goto out_free;
	}

	rc = snap_query_flow_table_caps(sctx);
	if (rc) {
		errno = EINVAL;
		goto out_free;
	}

	rc = snap_query_hotplug_caps(sctx);
	if (rc) {
		errno = EINVAL;
		goto out_free;
	}

	sctx->vuid_supported = snap_query_vuid_is_supported(context);

	rc = snap_query_crypto_caps(sctx);
	if (rc)
		snap_warn("query crypto caps failed, ret:%d\n", rc);

	rc = snap_alloc_functions(sctx);
	if (rc) {
		errno = -rc;
		goto out_free;
	}

	rc = pthread_mutex_init(&sctx->lock, NULL);
	if (rc) {
		errno = ENOMEM;
		goto out_free_pfs;
	}

	TAILQ_INIT(&sctx->device_list);

	rc = pthread_mutex_init(&sctx->hotplug_lock, NULL);
	if (rc) {
		errno = ENOMEM;
		goto out_free_mutex;
	}

	TAILQ_INIT(&sctx->hotplug_device_list);
	return sctx;

out_free_mutex:
	pthread_mutex_destroy(&sctx->lock);
out_free_pfs:
	snap_free_functions(sctx);
out_free:
	free(sctx);
out_close_device:
	ibv_close_device(context);
out_err:
	return NULL;
}

/**
 * snap_close() - Close and destroy a snap context
 * @sctx:       snap context
 *
 * Destroy and free a snap context.
 */
void snap_close(struct snap_context *sctx)
{
	struct ibv_context *context = sctx->context;

	pthread_mutex_destroy(&sctx->hotplug_lock);
	pthread_mutex_destroy(&sctx->lock);
	snap_free_functions(sctx);
	free(sctx);
	ibv_close_device(context);
}

uint16_t snap_get_vhca_id(struct snap_device *sdev)
{
	return sdev->pci->mpci.vhca_id;
}

void snap_update_pci_bdf(struct snap_pci *spci, uint16_t pci_bdf)
{
	char old_bdf[16];

	if (spci->pci_bdf.raw != pci_bdf) {
		strncpy(old_bdf, spci->pci_number, sizeof(old_bdf));
		spci->pci_bdf.raw = pci_bdf;
		snprintf(spci->pci_number, sizeof(spci->pci_number), "%02x:%02x.%d",
			spci->pci_bdf.bdf.bus, spci->pci_bdf.bdf.device,
			spci->pci_bdf.bdf.function);
		snap_warn("sctx:%p pci function(%d) pci_bdf changed from:%s to:%s\n",
			spci->sctx, spci->id, old_bdf, spci->pci_number);
	}
}

static bool
snap_allow_other_vhca_access_is_supported(struct ibv_context *context,
					  enum mlx5_obj_type obj_type)
{
	uint8_t in[DEVX_ST_SZ_BYTES(query_hca_cap_in)] = {};
	uint8_t out[DEVX_ST_SZ_BYTES(query_hca_cap_out)] = {};
	uint64_t allowed_obj_types_mask;
	int ret;

	DEVX_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	DEVX_SET(query_hca_cap_in, in, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_GENERAL_DEVICE2);
	ret = mlx5dv_devx_general_cmd(context, in, sizeof(in),
				      out, sizeof(out));
	if (ret)
		return false;

	allowed_obj_types_mask = DEVX_GET64(query_hca_cap_out, out,
	       capability.cmd_hca_cap2.allowed_object_for_other_vhca_access);
	if (!((1 << obj_type) & allowed_obj_types_mask))
		return false;

	return true;
}

int snap_allow_other_vhca_access(struct ibv_context *context,
				 enum mlx5_obj_type obj_type,
				 uint32_t obj_id,
				 uint8_t access_key[SNAP_ACCESS_KEY_LENGTH])
{
	int ret;
	uint8_t in[DEVX_ST_SZ_BYTES(allow_other_vhca_access_in)] = {};
	uint8_t out[DEVX_ST_SZ_BYTES(allow_other_vhca_access_out)] = {};

	if (!snap_allow_other_vhca_access_is_supported(context, obj_type))
		return -ENOTSUP;

	DEVX_SET(allow_other_vhca_access_in, in, opcode,
		 MLX5_CMD_OP_ALLOW_OTHER_VHCA_ACCESS);
	DEVX_SET(allow_other_vhca_access_in, in,
		 object_type_to_be_accessed, obj_type);
	DEVX_SET(allow_other_vhca_access_in, in,
		 object_id_to_be_accessed, obj_id);
	if (access_key) {
		memcpy(DEVX_ADDR_OF(allow_other_vhca_access_in,
				    in, access_key),
		       access_key,
		       DEVX_FLD_SZ_BYTES(allow_other_vhca_access_in,
					 access_key));
	}
	ret = mlx5dv_devx_general_cmd(context, in, sizeof(in),
				      out, sizeof(out));
	if (ret) {
		snap_error("Failed to allow other vhca access\n");
		return ret;
	}

	snap_debug("Other VHCA access is allowed for object 0x%x\n", obj_id);
	return 0;
}

static bool
snap_cross_vhca_object_is_supported(struct ibv_context *context,
				    enum cross_vhca_object_support_bit cross_type)
{
	uint8_t in[DEVX_ST_SZ_BYTES(query_hca_cap_in)] = {};
	uint8_t out[DEVX_ST_SZ_BYTES(query_hca_cap_out)] = {};
	uint32_t supported_obj_types_mask;
	int ret;

	DEVX_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	DEVX_SET(query_hca_cap_in, in, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_GENERAL_DEVICE2);
	ret = mlx5dv_devx_general_cmd(context, in, sizeof(in),
				      out, sizeof(out));
	if (ret)
		return false;

	supported_obj_types_mask = DEVX_GET(query_hca_cap_out, out,
	       capability.cmd_hca_cap2.cross_vhca_object_to_object_supported);
	if (!(supported_obj_types_mask & cross_type))
		return false;

	return true;
}

struct snap_alias_object *
snap_create_alias_object(struct ibv_context *src_context,
			 enum mlx5_obj_type obj_type,
			 struct ibv_context *dst_context,
			 uint32_t dst_obj_id,
			 uint8_t access_key[SNAP_ACCESS_KEY_LENGTH])
{
	struct snap_alias_object *alias;
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(alias_context)] = {};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)] = {};
	uint8_t *alias_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);

	if (!snap_cross_vhca_object_is_supported(src_context,
			 CROSS_VHCA_OBJ_SUPPORT_LNVME_SQ_BE_TO_RNVME_SQ)) {
		errno = ENOTSUP;
		goto err;
	}

	alias = calloc(1, sizeof(*alias));
	if (!alias) {
		errno = ENOMEM;
		goto err;
	}

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type, obj_type);
	DEVX_SET(general_obj_in_cmd_hdr, in, alias_object, 1);
	DEVX_SET(alias_context, alias_in, vhca_id_to_be_accessed,
		 snap_get_dev_vhca_id(dst_context));
	DEVX_SET(alias_context, alias_in, object_id_to_be_accessed,
		 dst_obj_id);
	if (access_key)
		memcpy(DEVX_ADDR_OF(alias_context, alias_in, access_key),
		       access_key,
		       DEVX_FLD_SZ_BYTES(alias_context, access_key));
	alias->obj = mlx5dv_devx_obj_create(src_context, in, sizeof(in),
					    out, sizeof(out));
	if (!alias->obj)
		goto free_alias;

	alias->obj_id = DEVX_GET(general_obj_out_cmd_hdr, out, obj_id);
	alias->src_context = src_context;
	alias->dst_context = dst_context;
	alias->dst_obj_id = dst_obj_id;
	if (access_key)
		memcpy(alias->access_key, access_key, SNAP_ACCESS_KEY_LENGTH);

	return alias;

free_alias:
	free(alias);
err:
	return NULL;
}

void snap_destroy_alias_object(struct snap_alias_object *alias)
{
	mlx5dv_devx_obj_destroy(alias->obj);
	free(alias);
}

/**
 * snap_create_cross_mkey() - Creates a cross mkey
 * @pd:           a protection domain that will be used to access remote memory
 * @target_sdev:  an emulation device
 *
 * The function creates a special 'cross' memory key that must be used to
 * access host memory via RDMA operations.
 *
 * For QPs that use 'cross' mkey there is no need to be attached to the snap
 * emulation object.
 *
 * Sample usage pattern:
 *   sctx = snap_open();
 *   sdev = snap_open_device(sctx, attrs);
 *
 *   // Create protection domain:
 *   ib_ctx = ibv_open_device();
 *   pd = ibv_alloc_pd(ib_ctx);
 *
 *   // create mkey:
 *   mkey = snap_create_cross_mkey(pd, sdev);
 *
 *   // create qp using dma layer or directly with ibv_create_qp()
 *   dma_q = snap_dma_q_create(pd, attr);
 *
 *   // use mkey->mkey to access host memory
 *   rc = snap_dma_q_write(dma_q, ldata, len, lkey, host_paddr, mkey->mkey, comp);
 *
 * Return:
 * A memory key or NULL on error
 */
struct snap_cross_mkey *snap_create_cross_mkey(struct ibv_pd *pd,
					       struct snap_device *target_sdev)
{
	struct snap_cross_mkey_attr cm_attr = {};

	cm_attr.vtunnel = target_sdev->mdev.vtunnel;
	cm_attr.dma_rkey = target_sdev->dma_rkey;
	cm_attr.vhca_id = snap_get_vhca_id(target_sdev);
	cm_attr.crossed_vhca_mkey = target_sdev->crossed_vhca_mkey;

	return snap_create_cross_mkey_by_attr(pd, &cm_attr);
}

