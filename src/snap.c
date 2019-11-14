#include "snap.h"

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

static int snap_alloc_virtual_functions(struct snap_pci *pf)
{
	int ret, i;

	pf->vfs = calloc(pf->num_vfs, sizeof(struct snap_pci));
	if (!pf->vfs)
		return -ENOMEM;

	for (i = 0; i < pf->num_vfs; i++) {
		struct snap_pci *vf = &pf->vfs[i];

		vf->type = SNAP_NVME_VF;
		vf->sctx = pf->sctx;
		vf->mpci.vhca_id = pf->mpci.vfs_base_vhca_id + i;

		vf->id = i;
		vf->pci_number = pf->pci_number;
		vf->num_vfs = 0;
		vf->parent = pf;
	}

	return 0;
}

static void snap_free_functions(struct snap_context *sctx)
{
	int i;

	for (i = 0; i < sctx->max_pfs; i++) {
		if (sctx->pfs[i].num_vfs)
			snap_free_virtual_functions(&sctx->pfs[i]);
	}
	free(sctx->pfs);
}

static int snap_alloc_functions(struct snap_context *sctx)
{
	struct ibv_context *context = sctx->context;
	uint8_t in[DEVX_ST_SZ_BYTES(query_emulated_functions_info_in)] = {0};
	uint8_t *out;
	int i, j;
	int ret, output_size, num_emulated_pfs;

	sctx->pfs = calloc(sctx->max_pfs, sizeof(struct snap_pci));
	if (!sctx->pfs)
		return -ENOMEM;

	DEVX_SET(query_emulated_functions_info_in, in, opcode,
		 MLX5_CMD_OP_QUERY_EMULATED_FUNCTIONS_INFO);

	output_size = DEVX_ST_SZ_BYTES(query_emulated_functions_info_out) +
		      DEVX_ST_SZ_BYTES(emulated_pf_info) * sctx->max_pfs;
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
	if (num_emulated_pfs > sctx->max_pfs) {
		ret = -EINVAL;
		goto out_free;
	}

	for (i = 0; i < num_emulated_pfs; i++) {
		struct snap_pci *pf = &sctx->pfs[i];

		pf->type = SNAP_NVME_PF;
		pf->sctx = sctx;
		pf->id = i;
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

	free(out);

	return 0;

free_vfs:
	for (j = 0; j < i; j++) {
		if (sctx->pfs[j].num_vfs)
			snap_free_virtual_functions(&sctx->pfs[j]);
	}
out_free:
	free(out);
out_free_pfs:
	free(sctx->pfs);
	return ret;
}

static bool is_emulation_manager(struct ibv_context *context)
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
		return false;

	if (!DEVX_GET(query_hca_cap_out, out,
		      capability.cmd_hca_cap.device_emulation_manager))
		return false;

	return true;
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

static int snap_query_emulation_caps(struct snap_context *sctx)
{
	uint8_t in[DEVX_ST_SZ_BYTES(query_hca_cap_in)] = {};
	uint8_t out[DEVX_ST_SZ_BYTES(query_hca_cap_out)] = {};
	struct ibv_context *context = sctx->context;
	int ret;

	DEVX_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	DEVX_SET(query_hca_cap_in, in, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_DEVICE_EMULATION);

	ret = mlx5dv_devx_general_cmd(context, in, sizeof(in), out,
				      sizeof(out));
	if (ret)
		return ret;

	sctx->max_pfs = DEVX_GET(query_hca_cap_out, out,
				 capability.emulation_cap.total_emulated_pfs);
	sctx->mctx.max_nvme_namespaces = 1 << DEVX_GET(query_hca_cap_out, out,
		capability.emulation_cap.log_max_nvme_offload_namespaces);
	sctx->mctx.max_emulated_nvme_cqs = 1 << DEVX_GET(query_hca_cap_out, out,
		capability.emulation_cap.log_max_emulated_cq);
	sctx->mctx.max_emulated_nvme_sqs = 1 << DEVX_GET(query_hca_cap_out, out,
		capability.emulation_cap.log_max_emulated_sq);

	return 0;

}

static void snap_destroy_vhca_tunnel(struct snap_device *sdev)
{
	mlx5dv_devx_obj_destroy(sdev->mdev.vtunnel->obj);
	free(sdev->mdev.vtunnel);
	sdev->mdev.vtunnel = NULL;
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
	int ret;

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

static int snap_destroy_flow_table(struct mlx5_snap_flow_table *ft)
{
	return snap_devx_obj_destroy(ft->ft);
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
	DEVX_SET(flow_table_context, ft_ctx, log_size, ft_log_size);

	ft->ft = snap_devx_obj_create(sdev, in, sizeof(in), out, sizeof(out),
				      sdev->mdev.vtunnel,
				      DEVX_ST_SZ_BYTES(destroy_flow_table_in),
				      DEVX_ST_SZ_BYTES(destroy_flow_table_out));
	if (!ft->ft)
		goto out_free;

	ft->table_id = DEVX_GET(create_flow_table_out, out, table_id);
	ft->table_type = table_type;
	ft->level = ft_level;
	ft->ft_size = 1 << ft_log_size;

	if (sdev->mdev.vtunnel) {
		void *dtor = ft->ft->dtor_in;

		DEVX_SET(destroy_flow_table_in, dtor, opcode,
			 MLX5_CMD_OP_DESTROY_FLOW_TABLE);
		DEVX_SET(destroy_flow_table_in, dtor, table_type,
			 ft->table_type);
		DEVX_SET(destroy_flow_table_in, in, table_id, ft->table_id);
	}

	return ft;

out_free:
	free(ft);
	return NULL;

}

static int snap_set_flow_table_root(struct snap_device *sdev,
		struct mlx5_snap_flow_table *ft)
{
	uint8_t in[DEVX_ST_SZ_BYTES(set_flow_table_root_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(set_flow_table_root_out)] = {0};
	int ret;

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

static int snap_reset_tx_steering(struct snap_device *sdev)
{
	return snap_destroy_flow_table(sdev->mdev.tx);
}

static int snap_init_tx_steering(struct snap_device *sdev)
{
	/* FT6 creation (match source QPN) */
	sdev->mdev.tx = snap_create_root_flow_table(sdev, FS_FT_NIC_TX_RDMA);
	if (!sdev->mdev.tx)
		return -ENOMEM;

	return 0;
}

static int snap_reset_rx_steering(struct snap_device *sdev)
{
	return snap_destroy_flow_table(sdev->mdev.rx);
}

static int snap_init_rx_steering(struct snap_device *sdev)
{
	/* FT3 creation (match desr QPN, miss send to FW NIC RX ROOT) */
	sdev->mdev.rx = snap_create_root_flow_table(sdev, FS_FT_NIC_RX_RDMA);
	if (!sdev->mdev.rx)
		return -ENOMEM;

	return 0;
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

static void snap_destroy_device_emulation(struct snap_device *sdev)
{
	mlx5dv_devx_obj_destroy(sdev->mdev.device_emulation->obj);
	free(sdev->mdev.device_emulation);
	sdev->mdev.device_emulation = NULL;
}

static struct mlx5_snap_devx_obj*
snap_create_device_emulation(struct snap_device *sdev)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(device_emulation)] = {0};
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
		 MLX5_OBJ_TYPE_DEVICE_EMULATION);

	device_emulation_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
	DEVX_SET(device_emulation, device_emulation_in, vhca_id,
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
int snap_get_pf_list(struct snap_context *sctx, enum snap_pci_type type,
		struct snap_pci **pfs)
{
	int i, count = 0;

	for (i = 0; i < sctx->max_pfs; i++) {
		if (sctx->pfs[i].type == type)
			pfs[count++] = &sctx->pfs[i];
	}

	return count;
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

	/* Currently support only NVME PF emulation */
	if (attr->type != SNAP_NVME_PF) {
		errno = ENOTSUP;
		goto out_err;
	}

	if (attr->type != sctx->pfs[attr->pf_id].type) {
		errno = EINVAL;
		goto out_err;
	}

	sdev = calloc(1, sizeof(*sdev));
	if (!sdev) {
		errno = ENOMEM;
		goto out_err;
	}

	sdev->sctx = sctx;
	sdev->pci = &sctx->pfs[attr->pf_id];
	sdev->mdev.device_emulation = snap_create_device_emulation(sdev);
	if (!sdev->mdev.device_emulation) {
		errno = EINVAL;
		goto out_free;
	}

	/* This should be done only for BF-1 */
	sdev->mdev.vtunnel = snap_create_vhca_tunnel(sdev);
	if (!sdev->mdev.vtunnel) {
		errno = EINVAL;
		goto out_free_device_emulation;
	}

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

	if (!is_emulation_manager(context)) {
		errno = EAGAIN;
		goto out_close_device;
	}

	sctx = calloc(1, sizeof(*sctx));
	if (!sctx) {
		errno = ENOMEM;
		goto out_close_device;
	}

	sctx->context = context;
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

	return sctx;

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
