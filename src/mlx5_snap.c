#include "mlx5_snap.h"

struct snap_device *mlx5_snap_open(struct ibv_device *ibdev)
{
	struct mlx5dv_context_attr attrs = {0};
	struct mlx5_snap_device *mdev;

	if (!mlx5dv_is_supported(ibdev))
		return NULL;

	mdev = calloc(1, sizeof(*mdev));
	if (!mdev)
		return NULL;

	attrs.flags = MLX5DV_CONTEXT_FLAGS_DEVX;
	mdev->sdev.context = mlx5dv_open_device(ibdev, &attrs);
	if (!mdev->sdev.context)
		goto out_free;

	return &mdev->sdev;

out_free:
	free(mdev);
	return NULL;
}

void mlx5_snap_close(struct snap_device *sdev)
{
	struct mlx5_snap_device *mdev = to_mlx5_snap_device(sdev);

	ibv_close_device(sdev->context);
	free(mdev);
}

static struct snap_driver mlx5_snap_driver = {
	.name = "mlx5",
	.open = mlx5_snap_open,
	.close = mlx5_snap_close,
};

static void __attribute__((constructor)) mlx5_snap_init(void)
{
	snap_register_driver(&mlx5_snap_driver);
}

static void __attribute__((destructor)) mlx5_snap_exit(void)
{
	snap_unregister_driver(&mlx5_snap_driver);
}
