#include "mlx5_snap.h"

struct snap_device *mlx5_snap_open(struct ibv_device *ibdev)
{
	return NULL;
}

void mlx5_snap_close(struct snap_device *sdev)
{
}

static struct snap_driver mlx5_snap_driver = {
	.name = "mlx5_snap",
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
