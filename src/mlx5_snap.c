#include "mlx5_snap.h"

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

static void mlx5_snap_destroy_device_emulation(struct mlx5_snap_device *mdev)
{
	mlx5dv_devx_obj_destroy(mdev->device_emulation);
	mdev->device_emulation = NULL;
}

static struct mlx5dv_devx_obj*
mlx5_snap_create_device_emulation(struct mlx5_snap_device *mdev)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(device_emulation)] = {};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)] = {};
	struct ibv_context *context = mdev->mctx->sctx.context;
	uint8_t *device_emulation;
	struct mlx5dv_devx_obj *obj;

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_OBJ_TYPE_DEVICE_EMULATION);

	device_emulation = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
	DEVX_SET(device_emulation, device_emulation, vhca_id,
		 mdev->pci->vhca_id);

	obj = mlx5dv_devx_obj_create(context, in, sizeof(in), out,
				     sizeof(out));
	if (!obj)
		return NULL;

	mdev->obj_id = DEVX_GET(general_obj_out_cmd_hdr, out, obj_id);

	return obj;
}

static int mlx5_snap_query_emulation_caps(struct mlx5_snap_context *mctx)
{
	uint8_t in[DEVX_ST_SZ_BYTES(query_hca_cap_in)] = {};
	uint8_t out[DEVX_ST_SZ_BYTES(query_hca_cap_out)] = {};
	struct ibv_context *context = mctx->sctx.context;
	int ret;

	DEVX_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	DEVX_SET(query_hca_cap_in, in, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_DEVICE_EMULATION);

	ret = mlx5dv_devx_general_cmd(context, in, sizeof(in), out,
				      sizeof(out));
	if (ret)
		return ret;

	mctx->max_pfs = DEVX_GET(query_hca_cap_out, out,
				 capability.emulation_cap.total_emulated_pfs);

	return 0;

}

static int mlx5_snap_free_virtual_functions(struct mlx5_snap_pci *pf)
{
	free(pf->vfs);
}

static int mlx5_snap_alloc_virtual_functions(struct mlx5_snap_pci *pf)
{
	int ret, i;

	pf->vfs = calloc(pf->spci.num_vfs, sizeof(struct mlx5_snap_pci));
	if (!pf->vfs)
		return -ENOMEM;

	for (i = 0; i < pf->spci.num_vfs; i++) {
		struct mlx5_snap_pci *vf = &pf->vfs[i];

		vf->type = MLX5_SNAP_VF;
		vf->mctx = pf->mctx;
		vf->vhca_id = pf->vfs_base_vhca_id + i;

		vf->spci.id = i;
		vf->spci.pci_number = pf->spci.pci_number;
		vf->spci.num_vfs = 0;
		vf->spci.parent = &pf->spci;
	}

	return 0;
}

static void mlx5_snap_free_functions(struct mlx5_snap_context *mctx)
{
	int i;

	for (i = 0; i < mctx->max_pfs; i++) {
		if (mctx->pfs[i].spci.num_vfs)
			mlx5_snap_free_virtual_functions(&mctx->pfs[i]);
	}
	free(mctx->pfs);
}

static int mlx5_snap_alloc_functions(struct mlx5_snap_context *mctx)
{
	struct ibv_context *context = mctx->sctx.context;
	uint8_t in[DEVX_ST_SZ_BYTES(query_emulated_functions_info_in)] = {};
	uint8_t *out;
	int i, j;
	int ret, output_size, num_emulated_pfs;

	mctx->pfs = calloc(mctx->max_pfs, sizeof(struct mlx5_snap_pci));
	if (!mctx->pfs)
		return -ENOMEM;

	DEVX_SET(query_emulated_functions_info_in, in, opcode,
		 MLX5_CMD_OP_QUERY_EMULATED_FUNCTIONS_INFO);

	output_size = DEVX_ST_SZ_BYTES(query_emulated_functions_info_out) +
		      DEVX_ST_SZ_BYTES(emulated_pf_info) * mctx->max_pfs;
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
	if (num_emulated_pfs > mctx->max_pfs) {
		ret = -EINVAL;
		goto out_free;
	}

	for (i = 0; i < num_emulated_pfs; i++) {
		struct mlx5_snap_pci *pf = &mctx->pfs[i];

		pf->type = MLX5_SNAP_PF;
		pf->mctx = mctx;
		pf->spci.id = i;
		pf->spci.pci_number = DEVX_GET(query_emulated_functions_info_out,
					       out,
					       emulated_pf_info[i].pf_pci_number);
		pf->vhca_id = DEVX_GET(query_emulated_functions_info_out, out,
					  emulated_pf_info[i].pf_vhca_id);
		pf->vfs_base_vhca_id = DEVX_GET(query_emulated_functions_info_out,
						out,
						emulated_pf_info[i].vfs_base_vhca_id);
		pf->spci.num_vfs = DEVX_GET(query_emulated_functions_info_out,
					    out,
					    emulated_pf_info[i].num_of_vfs);
		if (pf->spci.num_vfs) {
			ret = mlx5_snap_alloc_virtual_functions(pf);
			if (ret)
				goto free_vfs;
		}
	}

	free(out);

	return 0;

free_vfs:
	for (j = 0; j < i; j++) {
		if (mctx->pfs[j].spci.num_vfs)
			mlx5_snap_free_virtual_functions(&mctx->pfs[j]);
	}
out_free:
	free(out);
out_free_pfs:
	free(mctx->pfs);
	return ret;
}

static bool mlx5_snap_is_capable(struct ibv_device *ibdev)
{
	struct mlx5dv_context_attr attrs = {0};
	bool capable;
	struct ibv_context *context;

	if (!mlx5dv_is_supported(ibdev))
		return false;

	attrs.flags = MLX5DV_CONTEXT_FLAGS_DEVX;
	context = mlx5dv_open_device(ibdev, &attrs);
	if (!context)
		return false;

	capable = is_emulation_manager(context);
	ibv_close_device(context);

	return capable;
}

static struct snap_context *mlx5_snap_create_context(struct ibv_device *ibdev)
{
	struct mlx5dv_context_attr attrs = {0};
	struct mlx5_snap_context *mctx;
	int rc;

	if (!mlx5_snap_is_capable(ibdev))
		return NULL;

	mctx = calloc(1, sizeof(*mctx));
	if (!mctx)
		return NULL;

	attrs.flags = MLX5DV_CONTEXT_FLAGS_DEVX;
	mctx->sctx.context = mlx5dv_open_device(ibdev, &attrs);
	if (!mctx->sctx.context)
		goto out_free;

	rc = mlx5_snap_query_emulation_caps(mctx);
	if (rc)
		goto out_close_dev;

	rc = mlx5_snap_alloc_functions(mctx);
	if (rc)
		goto out_close_dev;

	rc = pthread_mutex_init(&mctx->lock, NULL);
	if (rc)
		goto out_free_pfs;

	TAILQ_INIT(&mctx->device_list);

	return &mctx->sctx;

out_free_pfs:
	mlx5_snap_free_functions(mctx);
out_close_dev:
	ibv_close_device(mctx->sctx.context);
out_free:
	free(mctx);
	return NULL;

}

static void mlx5_snap_destroy_context(struct snap_context *sctx)
{
	struct mlx5_snap_context *mctx = to_mlx5_snap_context(sctx);

	/* TODO: assert if there are open devices */
	pthread_mutex_destroy(&mctx->lock);
	mlx5_snap_free_functions(mctx);
	ibv_close_device(sctx->context);
	free(mctx);
}

static struct snap_device *mlx5_snap_open_device(struct snap_context *sctx,
		struct snap_device_attr *attr)
{
	struct mlx5_snap_context *mctx = to_mlx5_snap_context(sctx);
	struct mlx5_snap_device *mdev;

	/* Currently support only NVME PF emulation */
	if (attr->type != SNAP_NVME_PF_DEV)
		return NULL;

	mdev = calloc(1, sizeof(*mdev));
	if (!mdev)
		return NULL;

	mdev->mctx = mctx;
	mdev->pci = &mctx->pfs[attr->pf_id];
	mdev->device_emulation = mlx5_snap_create_device_emulation(mdev);
	if (!mdev->device_emulation)
		goto out_free;

	mdev->sdev.pci = &mdev->pci->spci;

	pthread_mutex_lock(&mctx->lock);
	TAILQ_INSERT_HEAD(&mctx->device_list, mdev, entry);
	pthread_mutex_unlock(&mctx->lock);

	return &mdev->sdev;

out_free:
	free(mdev);
	return NULL;
}

static void mlx5_snap_close_device(struct snap_device *sdev)
{
	struct mlx5_snap_device *mdev = to_mlx5_snap_device(sdev);
	struct mlx5_snap_context *mctx = mdev->mctx;

	pthread_mutex_lock(&mctx->lock);
	TAILQ_REMOVE(&mctx->device_list, mdev, entry);
	pthread_mutex_unlock(&mctx->lock);

	mlx5_snap_destroy_device_emulation(mdev);
	free(mdev);
}

static struct snap_driver mlx5_snap_driver = {
	.name = "mlx5",
	.create = mlx5_snap_create_context,
	.destroy = mlx5_snap_destroy_context,
	.open = mlx5_snap_open_device,
	.close = mlx5_snap_close_device,
	.is_capable = mlx5_snap_is_capable,
};

static void __attribute__((constructor)) mlx5_snap_init(void)
{
	snap_register_driver(&mlx5_snap_driver);
}

static void __attribute__((destructor)) mlx5_snap_exit(void)
{
	snap_unregister_driver(&mlx5_snap_driver);
}
