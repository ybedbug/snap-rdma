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

	rc = pthread_mutex_init(&mctx->lock, NULL);
	if (rc)
		goto out_close_dev;

	TAILQ_INIT(&mctx->device_list);

	return &mctx->sctx;

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
	ibv_close_device(sctx->context);
	free(mctx);
}

static struct snap_device *mlx5_snap_open_device(struct snap_context *sctx,
		struct snap_device_attr *attr)
{
	struct mlx5_snap_context *mctx = to_mlx5_snap_context(sctx);
	struct mlx5dv_context_attr attrs = {0};
	struct mlx5_snap_device *mdev;

	mdev = calloc(1, sizeof(*mdev));
	if (!mdev)
		return NULL;

	pthread_mutex_lock(&mctx->lock);
	TAILQ_INSERT_HEAD(&mctx->device_list, mdev, entry);
	pthread_mutex_unlock(&mctx->lock);

	mdev->mctx = mctx;

	/* TODO: create emulation object */
	return &mdev->sdev;

out_free:
	free(mdev);
	return NULL;
}

static void mlx5_snap_close_device(struct snap_device *sdev)
{
	struct mlx5_snap_device *mdev = to_mlx5_snap_device(sdev);
	struct mlx5_snap_context *mctx = mdev->mctx;

	/* TODO: destroy emulation object */

	pthread_mutex_lock(&mctx->lock);
	TAILQ_REMOVE(&mctx->device_list, mdev, entry);
	pthread_mutex_unlock(&mctx->lock);

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
