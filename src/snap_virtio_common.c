#include "snap.h"
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

int snap_virtio_query_device(struct snap_device *sdev,
	enum snap_emulation_type type, uint8_t *out, int outlen)
{
	uint8_t in_blk[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		DEVX_ST_SZ_BYTES(virtio_blk_device_emulation)] = {0};
	uint8_t in_net[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		DEVX_ST_SZ_BYTES(virtio_net_device_emulation)] = {0};
	uint8_t *in, *device_emulation_in;
	int inlen;

	if (type == SNAP_VIRTIO_BLK &&
	    (sdev->pci->type != SNAP_VIRTIO_BLK_PF &&
	     sdev->pci->type != SNAP_VIRTIO_BLK_VF))
		return -EINVAL;
	else if (type == SNAP_VIRTIO_NET &&
		 (sdev->pci->type != SNAP_VIRTIO_NET_PF &&
		  sdev->pci->type != SNAP_VIRTIO_NET_VF))
		return -EINVAL;
	else if (type == SNAP_NVME)
		return -EINVAL;

	if (type == SNAP_VIRTIO_BLK) {
		in = in_blk;
		inlen = sizeof(in_blk);
		device_emulation_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);

		DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
			 MLX5_OBJ_TYPE_VIRTIO_BLK_DEVICE_EMULATION);
		DEVX_SET(virtio_blk_device_emulation, device_emulation_in, vhca_id,
			 sdev->pci->mpci.vhca_id);
	} else {
		in = in_net;
		inlen = sizeof(in_net);
		device_emulation_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);

		DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
			 MLX5_OBJ_TYPE_VIRTIO_NET_DEVICE_EMULATION);
		DEVX_SET(virtio_net_device_emulation, device_emulation_in, vhca_id,
			 sdev->pci->mpci.vhca_id);
	}

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_QUERY_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_id,
		 sdev->mdev.device_emulation->obj_id);

	return mlx5dv_devx_obj_query(sdev->mdev.device_emulation->obj, in,
				     inlen, out, outlen);
}
