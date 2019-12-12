#include "snap_virtio_common.h"

#include "mlx5_ifc.h"

void snap_virtio_get_queue_attr(struct snap_virtio_queue_attr *vattr,
		void *q_configuration)
{
	vattr->size = DEVX_GET(virtio_q_layout, q_configuration, queue_size);
	vattr->msix_vector = DEVX_GET(virtio_q_layout, q_configuration,
				      queue_msix_vector);
	vattr->enable = DEVX_GET(virtio_q_layout, q_configuration,
				 queue_enable);
	vattr->notify_off = DEVX_GET(virtio_q_layout, q_configuration,
				     queue_notify_off);
	vattr->desc = DEVX_GET64(virtio_q_layout, q_configuration,
				 queue_desc);
	vattr->driver = DEVX_GET64(virtio_q_layout, q_configuration,
				   queue_driver);
	vattr->device = DEVX_GET64(virtio_q_layout, q_configuration,
				   queue_device);
}

void snap_virtio_get_device_attr(struct snap_virtio_device_attr *vattr,
		void *device_configuration)
{
	vattr->device_feature = DEVX_GET(virtio_device, device_configuration,
					 device_feature);
	vattr->driver_feature = DEVX_GET(virtio_device, device_configuration,
					 driver_feature);
	vattr->msix_config = DEVX_GET(virtio_device, device_configuration,
				      msix_config);
	vattr->status = DEVX_GET(virtio_device, device_configuration,
				 device_status);
}
