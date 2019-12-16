#include "snap.h"
#include "snap_nvme.h"
#include "snap_virtio_blk.h"

#include "mlx5_ifc.h"

#define SNAP_INITIALIZE_HCA_RETRY_CNT 100
#define SNAP_TEARDOWN_HCA_RETRY_CNT 5
#define SNAP_GENERAL_CMD_USEC_WAIT 50000

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

static void snap_free_virtual_functions(struct snap_pci *pf)
{
	free(pf->vfs);
}

static enum snap_pci_type snap_pf_to_vf_type(enum snap_pci_type pf_type)
{
	if (pf_type == SNAP_NVME_PF)
		return SNAP_NVME_VF;
	else if (pf_type == SNAP_VIRTIO_NET_PF)
		return SNAP_VIRTIO_NET_VF;
	else if (pf_type == SNAP_VIRTIO_BLK_PF)
		return SNAP_VIRTIO_BLK_VF;
	else
		return pf_type;
}

static int snap_alloc_virtual_functions(struct snap_pci *pf)
{
	int i;

	pf->vfs = calloc(pf->num_vfs, sizeof(struct snap_pci));
	if (!pf->vfs)
		return -ENOMEM;

	for (i = 0; i < pf->num_vfs; i++) {
		struct snap_pci *vf = &pf->vfs[i];

		vf->type = snap_pf_to_vf_type(pf->type);
		vf->sctx = pf->sctx;
		vf->mpci.vhca_id = pf->mpci.vfs_base_vhca_id + i;

		vf->plugged = true;
		vf->id = i;
		vf->pci_number = pf->pci_number;
		vf->num_vfs = 0;
		vf->parent = pf;
	}

	return 0;
}

static void _snap_free_functions(struct snap_context *sctx,
		struct snap_pfs_ctx *pfs)
{
	int i;

	for (i = 0; i < pfs->max_pfs; i++) {
		if (pfs->pfs[i].num_vfs)
			snap_free_virtual_functions(&pfs->pfs[i]);
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
}

static int _snap_alloc_functions(struct snap_context *sctx,
		struct snap_pfs_ctx *pfs_ctx)
{
	struct ibv_context *context = sctx->context;
	uint8_t in[DEVX_ST_SZ_BYTES(query_emulated_functions_info_in)] = {0};
	uint8_t *out;
	int i, j, opmod;
	int ret, output_size, num_emulated_pfs;
	enum snap_pci_type pf_type;

	if (pfs_ctx->type == SNAP_NVME) {
		opmod = MLX5_SET_EMULATED_FUNCTIONS_OP_MOD_NVME_DEVICE;
		pf_type = SNAP_NVME_PF;
	} else if (pfs_ctx->type == SNAP_VIRTIO_NET) {
		opmod = MLX5_SET_EMULATED_FUNCTIONS_OP_MOD_VIRTIO_NET_DEVICE;
		pf_type = SNAP_VIRTIO_NET_PF;
	} else if (pfs_ctx->type == SNAP_VIRTIO_BLK) {
		opmod = MLX5_SET_EMULATED_FUNCTIONS_OP_MOD_VIRTIO_BLK_DEVICE;
		pf_type = SNAP_VIRTIO_BLK_PF;
	} else {
		return -EINVAL;
	}

	pfs_ctx->pfs = calloc(pfs_ctx->max_pfs, sizeof(struct snap_pci));
	if (!pfs_ctx->pfs)
		return -ENOMEM;

	DEVX_SET(query_emulated_functions_info_in, in, opcode,
		 MLX5_CMD_OP_QUERY_EMULATED_FUNCTIONS_INFO);
	DEVX_SET(query_emulated_functions_info_in, in, op_mod, opmod);

	output_size = DEVX_ST_SZ_BYTES(query_emulated_functions_info_out) +
		      DEVX_ST_SZ_BYTES(emulated_pf_info) * (pfs_ctx->max_pfs);
	out = calloc(1, output_size);
	if (!out) {
		ret = -ENOMEM;
		goto out_free_pfs;
	}

	ret = mlx5dv_devx_general_cmd(context, in, sizeof(in), out,
				      output_size);
	if (ret)
		goto out_free;

	num_emulated_pfs = DEVX_GET(query_emulated_functions_info_out, out,
				    num_emulated_pfs);
	if (num_emulated_pfs > pfs_ctx->max_pfs) {
		ret = -EINVAL;
		goto out_free;
	}

	for (i = 0; i < pfs_ctx->max_pfs; i++) {
		struct snap_pci *pf = &pfs_ctx->pfs[i];

		pf->type = pf_type;
		pf->sctx = sctx;
		pf->id = i;
		if (i < num_emulated_pfs) {
			pf->plugged = true;
			pf->pci_number = DEVX_GET(query_emulated_functions_info_out,
						  out,
						  emulated_pf_info[i].pf_pci_number);
			pf->mpci.vhca_id = DEVX_GET(query_emulated_functions_info_out,
						    out,
						    emulated_pf_info[i].pf_vhca_id);
			pf->mpci.vfs_base_vhca_id = DEVX_GET(query_emulated_functions_info_out,
							     out,
							     emulated_pf_info[i].vfs_base_vhca_id);
			pf->num_vfs = DEVX_GET(query_emulated_functions_info_out,
					       out,
					       emulated_pf_info[i].num_of_vfs);
			if (pf->num_vfs) {
				ret = snap_alloc_virtual_functions(pf);
				if (ret)
					goto free_vfs;
			}
		}
	}

	free(out);

	return 0;

free_vfs:
	for (j = 0; j < i; j++) {
		if (pfs_ctx->pfs[j].num_vfs)
			snap_free_virtual_functions(&pfs_ctx->pfs[j]);
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

	return 0;

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
	general_obj_types = DEVX_GET(query_hca_cap_out, out,
				     capability.cmd_hca_cap.general_obj_types);
	//TODO: remove this after FW bug fixed
	general_obj_types = 0xffffffffffffffff;
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
		capability.cmd_hca_cap.resources_on_emulation_manager))
		sctx->mctx.need_tunnel = false;
	else
		sctx->mctx.need_tunnel = true;

	if (DEVX_GET(query_hca_cap_out, out,
		capability.cmd_hca_cap.hotplug_manager) &&
	    general_obj_types & (1 << MLX5_OBJ_TYPE_DEVICE))
		sctx->hotplug_supported = true;

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
		return -ENOSYS;

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

static int snap_fill_virtio_ctx(struct mlx5_snap_virtio_context *virtio,
		uint8_t *out)
{
	virtio->max_emulated_virtqs = DEVX_GET(query_hca_cap_out,
		out, capability.virtio_emulation_cap.max_num_virtio_queues);
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

	if (DEVX_GET(query_hca_cap_out, out,
		     capability.virtio_emulation_cap.virtio_queue_type) &
	    MLX5_VIRTIO_QUEUE_TYPE_SPLIT)
		virtio->supported_types |= SNAP_VIRTQ_SPLIT_MODE;
	if (DEVX_GET(query_hca_cap_out, out,
		     capability.virtio_emulation_cap.virtio_queue_type) &
	    MLX5_VIRTIO_QUEUE_TYPE_PACKED)
		virtio->supported_types |= SNAP_VIRTQ_PACKED_MODE;

	if (DEVX_GET(query_hca_cap_out, out,
		     capability.virtio_emulation_cap.event_mode) &
	    MLX5_VIRTIO_QUEUE_EVENT_MODE_NO_MSIX)
		virtio->event_modes |= SNAP_VIRTQ_NO_MSIX_MODE;
	if (DEVX_GET(query_hca_cap_out, out,
		     capability.virtio_emulation_cap.event_mode) &
	    MLX5_VIRTIO_QUEUE_EVENT_MODE_QP)
		virtio->event_modes |= SNAP_VIRTQ_QP_MODE;
	if (DEVX_GET(query_hca_cap_out, out,
		     capability.virtio_emulation_cap.event_mode) &
	    MLX5_VIRTIO_QUEUE_EVENT_MODE_MSIX)
		virtio->event_modes |= SNAP_VIRTQ_MSIX_MODE;
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

	snap_fill_virtio_ctx(&sctx->mctx.virtio_blk, out);

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

	snap_fill_virtio_ctx(&sctx->mctx.virtio_net, out);

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
	sctx->mctx.nvme.max_nvme_namespaces = 1 << DEVX_GET(query_hca_cap_out, out,
		capability.nvme_emulation_cap.log_max_nvme_offload_namespaces);
	sctx->mctx.nvme.max_emulated_nvme_cqs = 1 << DEVX_GET(query_hca_cap_out, out,
		capability.nvme_emulation_cap.log_max_emulated_cq);
	sctx->mctx.nvme.max_emulated_nvme_sqs = 1 << DEVX_GET(query_hca_cap_out, out,
		capability.nvme_emulation_cap.log_max_emulated_sq);
	sctx->mctx.nvme.reg_size = DEVX_GET(query_hca_cap_out, out,
		capability.nvme_emulation_cap.registers_size);
	if (DEVX_GET(query_hca_cap_out, out,
		     capability.nvme_emulation_cap.nvme_offload_type_sqe))
		sctx->mctx.nvme.supported_types |= SNAP_NVME_SQE_MODE;
	if (DEVX_GET(query_hca_cap_out, out,
		     capability.nvme_emulation_cap.nvme_offload_type_command_capsule))
		sctx->mctx.nvme.supported_types |= SNAP_NVME_CC_MODE;

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

	sctx->hotplug.max_devices = DEVX_GET(query_hca_cap_out, out,
				capability.hotplug_cap.max_hotplug_devices);
	sctx->hotplug.log_max_bar_size = DEVX_GET(query_hca_cap_out, out,
				capability.hotplug_cap.log_max_bar_size);
	supported_types = DEVX_GET(query_hca_cap_out, out,
			capability.hotplug_cap.hotplug_device_types_supported);
	if (supported_types & (1 << MLX5_HOTPLUG_DEVICE_TYPE_NVME))
		sctx->hotplug.supported_types |= SNAP_NVME;
	if (supported_types & (1 << MLX5_HOTPLUG_DEVICE_TYPE_VIRTIO_NET))
		sctx->hotplug.supported_types |= SNAP_VIRTIO_NET;
	if (supported_types & (1 << MLX5_HOTPLUG_DEVICE_TYPE_VIRTIO_BLK))
		sctx->hotplug.supported_types |= SNAP_VIRTIO_BLK;

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

	return 0;

}

static void snap_destroy_vhca_tunnel(struct snap_device *sdev)
{
	mlx5dv_devx_obj_destroy(sdev->mdev.vtunnel->obj);
	free(sdev->mdev.vtunnel);
	sdev->mdev.vtunnel = NULL;
}

/*
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

/*
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

/*
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

/*
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
	struct ibv_context *context = sdev->sctx->context;
	struct mlx5_snap_devx_obj *snap_obj;
	int ret;

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
snap_create_flow_table(struct snap_device *sdev, uint32_t table_type)
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

	ft->ft = snap_devx_obj_create(sdev, in, sizeof(in), out, sizeof(out),
				      sdev->mdev.vtunnel,
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

	ft = snap_create_flow_table(sdev, table_type);
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

	TAILQ_REMOVE(&fg->ft->fg_list, fg, entry);
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
		void *match_criteria, enum mlx5_snap_flow_group_type type)
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

	fg->fg = snap_devx_obj_create(sdev, in, sizeof(in), out, sizeof(out),
				      sdev->mdev.vtunnel,
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
	TAILQ_FOREACH_SAFE(fg, &sdev->mdev.tx->fg_list, entry, next) {
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
	struct mlx5_snap_flow_group *fg;

	/* FT6 creation (match source QPN) */
	sdev->mdev.tx = snap_create_root_flow_table(sdev, FS_FT_NIC_TX_RDMA);
	if (!sdev->mdev.tx)
		return -ENOMEM;

	/* Flow group that matches source qpn rule (limit to 1024 QPs) */
	DEVX_SET(fte_match_param, match, misc_parameters.source_sqn, 0xffffff);
	fg = snap_create_flow_group(sdev, sdev->mdev.tx, 0,
		sdev->mdev.tx->ft_size - 1,
		1 << MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_MISC_PARAMETERS,
		match, SNAP_FG_MATCH);
	if (!fg)
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
	TAILQ_FOREACH_SAFE(fg, &sdev->mdev.rx->fg_list, entry, next) {
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
	struct mlx5_snap_flow_group *fg, *fg_miss;

	/* FT3 creation (match dest QPN, miss send to FW NIC RX ROOT) */
	sdev->mdev.rx = snap_create_root_flow_table(sdev, FS_FT_NIC_RX_RDMA);
	if (!sdev->mdev.rx)
		return -ENOMEM;

	/* Flow group that matches dst qpn rule (limit to 1023 QPs) */
	DEVX_SET(fte_match_param, match, misc_parameters.bth_dst_qp, 0xffffff);
	fg = snap_create_flow_group(sdev, sdev->mdev.rx, 0,
		sdev->mdev.rx->ft_size - 2,
		1 << MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_MISC_PARAMETERS,
		match, SNAP_FG_MATCH);
	if (!fg)
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
	fg_miss = snap_create_flow_group(sdev, sdev->mdev.rx,
		sdev->mdev.rx->ft_size - 1,
		sdev->mdev.rx->ft_size - 1,
		0, match, SNAP_FG_MISS);
	if (!fg_miss)
		goto out_free_fg;

	return 0;

out_free_fg:
	pthread_mutex_lock(&fg->ft->lock);
	snap_destroy_flow_group(fg);
	pthread_mutex_unlock(&fg->ft->lock);
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

/*
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

	return 0;

out_teardown:
	snap_teardown_hca(sdev);
out_disable:
	snap_disable_hca(sdev);
	return ret;
}

/*
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

	ret = snap_reset_steering(sdev);
	if (ret)
		return ret;
	ret = snap_teardown_hca(sdev);
	if (ret)
		return ret;

	return snap_disable_hca(sdev);
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

static void snap_destroy_device_object(struct mlx5_snap_devx_obj *device)
{
	mlx5dv_devx_obj_destroy(device->obj);
	free(device);
}

static struct mlx5_snap_devx_obj*
snap_create_device_object(struct snap_context *sctx,
		struct snap_hotplug_attr *attr)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(device)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)] = {0};
	struct ibv_context *context = sctx->context;
	uint8_t *device_in, device_type;
	struct mlx5_snap_devx_obj *device;

	if (!(sctx->hotplug.supported_types & attr->type)) {
		errno = ENOTSUP;
		goto out_err;
	}

	device = calloc(1, sizeof(*device));
	if (!device) {
		errno = ENOMEM;
		goto out_err;
	}

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_OBJ_TYPE_DEVICE);

	device_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
	if (attr->type == SNAP_NVME)
		device_type = MLX5_HOTPLUG_DEVICE_TYPE_NVME;
	else if (attr->type == SNAP_VIRTIO_NET)
		device_type = MLX5_HOTPLUG_DEVICE_TYPE_VIRTIO_NET;
	else if (attr->type == SNAP_VIRTIO_BLK)
		device_type = MLX5_HOTPLUG_DEVICE_TYPE_VIRTIO_BLK;
	else
		goto out_free;

	DEVX_SET(device, device_in, device_type, device_type);
	DEVX_SET(device, device_in, pci_params.device_id, attr->device_id);
	DEVX_SET(device, device_in, pci_params.vendor_id, attr->vendor_id);
	DEVX_SET(device, device_in, pci_params.revision_id, attr->revision_id);
	DEVX_SET(device, device_in, pci_params.class_code, attr->class_code);
	DEVX_SET(device, device_in, pci_params.subsystem_id, attr->subsystem_id);
	DEVX_SET(device, device_in, pci_params.subsystem_vendor_id,
		 attr->subsystem_vendor_id);
	DEVX_SET(device, device_in, pci_params.num_msix, attr->num_msix);

	device->obj = mlx5dv_devx_obj_create(context, in, sizeof(in), out,
					     sizeof(out));
	if (!device->obj)
		goto out_free;

	device->obj_id = DEVX_GET(general_obj_out_cmd_hdr, out, obj_id);
	/* sdev will be assigned after creating the emulation object */
	device->sdev = NULL;

	return device;

out_free:
	free(device);
out_err:
	return NULL;
}

static void snap_destroy_device_emulation(struct snap_device *sdev)
{
	mlx5dv_devx_obj_destroy(sdev->mdev.device_emulation->obj);
	free(sdev->mdev.device_emulation);
	sdev->mdev.device_emulation = NULL;
}

static struct mlx5_snap_devx_obj*
snap_create_virtio_net_device_emulation(struct snap_device *sdev)
{
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
snap_create_nvme_device_emulation(struct snap_device *sdev)
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
snap_create_device_emulation(struct snap_device *sdev)
{
	if (!sdev->pci->plugged)
		return NULL;

	switch (sdev->pci->type) {
	case SNAP_NVME_PF:
	case SNAP_NVME_VF:
		return snap_create_nvme_device_emulation(sdev);
	case SNAP_VIRTIO_NET_PF:
	case SNAP_VIRTIO_NET_VF:
		return snap_create_virtio_net_device_emulation(sdev);
	case SNAP_VIRTIO_BLK_PF:
	case SNAP_VIRTIO_BLK_VF:
		return snap_create_virtio_blk_device_emulation(sdev);
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

	if (type == SNAP_NVME)
		pfs_ctx = &sctx->nvme_pfs;
	else if (type == SNAP_VIRTIO_NET)
		pfs_ctx = &sctx->virtio_net_pfs;
	else if (type == SNAP_VIRTIO_BLK)
		pfs_ctx = &sctx->virtio_blk_pfs;
	else
		return 0;

	for (i = 0; i < pfs_ctx->max_pfs; i++)
		pfs[i] = &pfs_ctx->pfs[i];

	return i;
}

/**
 * snap_hotunplug_pf() - Unplug a snap physical PCI function from host
 * @pf:        snap physical PCI function
 *
 * Unplug a previously hot-plugged snap physical PCI function from the host.
 */
void snap_hotunplug_pf(struct snap_pci *pf)
{
	if (!pf->plugged)
		return;

	if (!pf->hotplug)
		return;

	snap_destroy_device_object(pf->hotplug->hotplug);
	free(pf->hotplug);

	pf->plugged = false;
	pf->hotplug = NULL;
}

/**
 * snap_hotplug_pf() - Plug a snap PCI function from a given snap context
 * @sctx:       snap context
 * @attr:       snap hotplug device attributes
 * @pf_idx:     PF index in the snap context (according to relevant type)
 *
 * Hotplugs a physical PCI function that will be seen to the host according
 * to the requested attributes and pf index.
 *
 * Return: On success, return snap PCI device. NULL otherwise and errno will be
 * set to indicate the failure reason.
 */
struct snap_pci *snap_hotplug_pf(struct snap_context *sctx,
				 struct snap_hotplug_attr *attr,
				 unsigned int pf_idx)
{
	struct snap_hotplug_device *hotplug;
	struct snap_pci *pf;

	if (attr->type == SNAP_NVME && pf_idx < sctx->nvme_pfs.max_pfs) {
		pf = &sctx->nvme_pfs.pfs[pf_idx];
	} else if (attr->type == SNAP_VIRTIO_NET &&
		 pf_idx < sctx->virtio_net_pfs.max_pfs) {
		pf = &sctx->virtio_net_pfs.pfs[pf_idx];
	} else if (attr->type == SNAP_VIRTIO_BLK &&
		 pf_idx < sctx->virtio_blk_pfs.max_pfs) {
		pf = &sctx->virtio_blk_pfs.pfs[pf_idx];
	} else {
		errno = ENODEV;
		goto out_err;
	}

	if (pf->plugged) {
		errno = EINVAL;
		goto out_err;
	}

	hotplug = calloc(1, sizeof(*hotplug));
	if (!hotplug) {
		errno = ENOMEM;
		goto out_err;
	}

	hotplug->hotplug = snap_create_device_object(sctx, attr);
	if (!hotplug->hotplug) {
		errno = ENOMEM;
		goto out_free;
	}

	pf->plugged = true;
	pf->hotplug = hotplug;

	/* TODO: link between vhca_id to hotplug device (PRM gap) */
	//pf->mpci.vhca_id = pf_idx + 100;

	return pf;

out_free:
	free(hotplug);
out_err:
	return NULL;
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

	/* Currently support only PFs emulation */
	if (attr->type == SNAP_NVME_PF &&
	    attr->pf_id < sctx->nvme_pfs.max_pfs) {
		pfs = &sctx->nvme_pfs;
	} else if (attr->type == SNAP_VIRTIO_NET_PF &&
		   attr->pf_id < sctx->virtio_net_pfs.max_pfs) {
		pfs = &sctx->virtio_net_pfs;
	} else if (attr->type == SNAP_VIRTIO_BLK_PF &&
		   attr->pf_id < sctx->virtio_blk_pfs.max_pfs) {
		pfs = &sctx->virtio_blk_pfs;
	} else {
		errno = EINVAL;
		goto out_err;
	}

	sdev = calloc(1, sizeof(*sdev));
	if (!sdev) {
		errno = ENOMEM;
		goto out_err;
	}

	sdev->sctx = sctx;
	sdev->pci = &pfs->pfs[attr->pf_id];
	sdev->mdev.device_emulation = snap_create_device_emulation(sdev);
	if (!sdev->mdev.device_emulation) {
		errno = EINVAL;
		goto out_free;
	}

	/* This should be done only for BF-1 */
	if (sctx->mctx.need_tunnel) {
		sdev->mdev.vtunnel = snap_create_vhca_tunnel(sdev);
		if (!sdev->mdev.vtunnel) {
			errno = EINVAL;
			goto out_free_device_emulation;
		}
	}

	if (sdev->pci->hotplug)
		sdev->pci->hotplug->hotplug->sdev = sdev;

	pthread_mutex_lock(&sctx->lock);
	TAILQ_INSERT_HEAD(&sctx->device_list, sdev, entry);
	pthread_mutex_unlock(&sctx->lock);

	return sdev;

out_free_device_emulation:
	snap_destroy_device_emulation(sdev);
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
	TAILQ_REMOVE(&sctx->device_list, sdev, entry);
	pthread_mutex_unlock(&sctx->lock);

	if (sdev->mdev.vtunnel)
		snap_destroy_vhca_tunnel(sdev);
	snap_destroy_device_emulation(sdev);
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
	struct snap_context *sctx;
	struct ibv_context *context;
	int rc;

	if (!mlx5dv_is_supported(ibdev)) {
		errno = ENOTSUP;
		goto out_err;
	}

	context = ibv_open_device(ibdev);
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

/**
 * snap_init() - Library constructor
 *
 * This routine runs when a shared library is loaded, during program startup.
 * It will set low level Mellanox driver characteristics for device emulation.
 */
static void __attribute__((constructor)) snap_init(void)
{
	mlx5dv_always_open_devx();
}
