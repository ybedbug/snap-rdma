#include "snap.h"
#include "snap_virtio_net.h"
#include "snap_virtio_blk.h"
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
	vattr->max_queues = DEVX_GET(virtio_device, device_configuration,
				     num_queues);
	vattr->status = DEVX_GET(virtio_device, device_configuration,
				 device_status);
	vattr->pci_attr.device_id = DEVX_GET(virtio_device, device_configuration,
					     pci_params.device_id);
	vattr->pci_attr.vendor_id = DEVX_GET(virtio_device, device_configuration,
					     pci_params.vendor_id);
	vattr->pci_attr.revision_id = DEVX_GET(virtio_device, device_configuration,
					       pci_params.revision_id);
	vattr->pci_attr.class_code = DEVX_GET(virtio_device, device_configuration,
					      pci_params.class_code);
	vattr->pci_attr.subsystem_id = DEVX_GET(virtio_device, device_configuration,
						pci_params.subsystem_id);
	vattr->pci_attr.subsystem_vendor_id = DEVX_GET(virtio_device, device_configuration,
						       pci_params.subsystem_vendor_id);
	vattr->pci_attr.num_msix = DEVX_GET(virtio_device, device_configuration,
					    pci_params.num_msix);
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

int snap_virtio_modify_device(struct snap_device *sdev,
		enum snap_emulation_type type,
		uint64_t mask, uint64_t allowed_mask,
		struct snap_virtio_device_attr *attr)
{
	uint8_t in_blk[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		DEVX_ST_SZ_BYTES(virtio_blk_device_emulation)] = {0};
	uint8_t in_net[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		DEVX_ST_SZ_BYTES(virtio_net_device_emulation)] = {0};
	uint64_t fields_to_modify = 0;
	uint8_t *in;
	int inlen;
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)];
	uint8_t *device_emulation_in;

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

	//we'll modify only allowed fields
	if (mask & ~allowed_mask)
		return -EINVAL;

	if (type == SNAP_VIRTIO_BLK) {
		in = in_blk;
		inlen = sizeof(in_blk);
		device_emulation_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);

		DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
			 MLX5_OBJ_TYPE_VIRTIO_BLK_DEVICE_EMULATION);

		if (mask & SNAP_VIRTIO_MOD_DEV_STATUS) {
			fields_to_modify = MLX5_VIRTIO_DEVICE_MODIFY_STATUS;
			DEVX_SET(virtio_blk_device_emulation, device_emulation_in,
				 virtio_device.device_status, attr->status);
		}
		DEVX_SET64(virtio_blk_device_emulation, device_emulation_in,
			   modify_field_select, fields_to_modify);
	} else {
		in = in_net;
		inlen = sizeof(in_net);
		device_emulation_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);

		DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
			 MLX5_OBJ_TYPE_VIRTIO_NET_DEVICE_EMULATION);
		if (mask & SNAP_VIRTIO_MOD_DEV_STATUS) {
			fields_to_modify = MLX5_VIRTIO_DEVICE_MODIFY_STATUS;
			DEVX_SET(virtio_net_device_emulation, device_emulation_in,
				 virtio_device.device_status, attr->status);
		}
		DEVX_SET64(virtio_net_device_emulation, device_emulation_in,
			   modify_field_select, fields_to_modify);
	}

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_MODIFY_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_id,
		 sdev->mdev.device_emulation->obj_id);


	return snap_devx_obj_modify(sdev->mdev.device_emulation, in, inlen,
				    out, sizeof(out));
}

struct mlx5_snap_devx_obj*
snap_virtio_create_queue(struct snap_device *sdev,
	struct snap_virtio_queue_attr *vattr)
{
	uint8_t in_blk[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		       DEVX_ST_SZ_BYTES(virtio_blk_q)] = {0};
	uint8_t in_net[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		       DEVX_ST_SZ_BYTES(virtio_net_q)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)] = {0};
	uint8_t *in;
	uint8_t *virtq_in;
	uint16_t obj_type;
	struct mlx5_snap_devx_obj *virtq;
	int virtq_type, ev_mode, inlen;

	if (vattr->type == SNAP_VIRTQ_SPLIT_MODE) {
		virtq_type = MLX5_VIRTIO_QUEUE_TYPE_SPLIT;
	} else if (vattr->type == SNAP_VIRTQ_PACKED_MODE) {
		virtq_type = MLX5_VIRTIO_QUEUE_TYPE_PACKED;
	} else {
		errno = EINVAL;
		goto out;
	}

	if (vattr->ev_mode == SNAP_VIRTQ_NO_MSIX_MODE) {
		ev_mode = MLX5_VIRTIO_QUEUE_EVENT_MODE_NO_MSIX;
	} else if (vattr->ev_mode == SNAP_VIRTQ_QP_MODE) {
		ev_mode = MLX5_VIRTIO_QUEUE_EVENT_MODE_QP;
	} else if (vattr->ev_mode == SNAP_VIRTQ_MSIX_MODE) {
		ev_mode = MLX5_VIRTIO_QUEUE_EVENT_MODE_MSIX;
	} else {
		errno = EINVAL;
		goto out;
	}

	if (sdev->pci->type == SNAP_VIRTIO_BLK_PF ||
	    sdev->pci->type == SNAP_VIRTIO_BLK_VF) {
		struct snap_virtio_blk_queue_attr *attr;

		attr = to_blk_queue_attr(vattr);
		in = in_blk;
		inlen = sizeof(in_blk);
		virtq_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);

		obj_type = MLX5_OBJ_TYPE_VIRTIO_BLK_Q;
		DEVX_SET(virtio_blk_q, virtq_in, qpn, attr->qpn);
		DEVX_SET(virtio_blk_q, virtq_in, virtqc.device_emulation_id,
			 sdev->pci->mpci.vhca_id);
		DEVX_SET(virtio_blk_q, virtq_in, virtqc.virtio_q_type, virtq_type);
		DEVX_SET(virtio_blk_q, virtq_in, virtqc.event_mode, ev_mode);
		DEVX_SET(virtio_blk_q, virtq_in, virtqc.queue_index, vattr->idx);
		DEVX_SET(virtio_blk_q, virtq_in, virtqc.queue_size, vattr->size);
	} else if (sdev->pci->type == SNAP_VIRTIO_NET_PF ||
		   sdev->pci->type == SNAP_VIRTIO_NET_VF) {
		struct snap_virtio_net_queue_attr *attr;

		attr = to_net_queue_attr(vattr);
		in = in_net;
		inlen = sizeof(in_net);
		virtq_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);

		obj_type = MLX5_OBJ_TYPE_VIRTIO_NET_Q;
		DEVX_SET(virtio_net_q, virtq_in, tisn_or_qpn, attr->tisn_or_qpn);
		DEVX_SET(virtio_net_q, virtq_in, virtqc.device_emulation_id,
			 sdev->pci->mpci.vhca_id);
		DEVX_SET(virtio_net_q, virtq_in, virtqc.virtio_q_type, virtq_type);
		DEVX_SET(virtio_net_q, virtq_in, virtqc.event_mode, ev_mode);
		DEVX_SET(virtio_net_q, virtq_in, virtqc.queue_index, vattr->idx);
		DEVX_SET(virtio_net_q, virtq_in, virtqc.queue_size, vattr->size);
	} else {
		errno = EINVAL;
		goto out;
	}

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type, obj_type);

	virtq = snap_devx_obj_create(sdev, in, inlen, out, sizeof(out),
				     sdev->mdev.vtunnel,
				     DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr),
				     DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr));
	if (!virtq) {
		errno = ENODEV;
		goto out;
	}

	if (sdev->mdev.vtunnel) {
		void *dtor = virtq->dtor_in;

		DEVX_SET(general_obj_in_cmd_hdr, dtor, opcode,
			 MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
		DEVX_SET(general_obj_in_cmd_hdr, dtor, obj_type, obj_type);
		DEVX_SET(general_obj_in_cmd_hdr, dtor, obj_id, virtq->obj_id);
	}

	return virtq;
out:
	return NULL;
}

int snap_virtio_query_queue(struct snap_virtio_queue *virtq,
	struct snap_virtio_queue_attr *vattr)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr)];
	uint8_t out_blk[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr) +
			DEVX_ST_SZ_BYTES(virtio_blk_q)] = {0};
	uint8_t out_net[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr) +
			DEVX_ST_SZ_BYTES(virtio_net_q)] = {0};
	uint8_t *out;
	uint8_t *virtq_out;
	uint64_t dev_allowed;
	int ret, outlen;
	struct snap_device *sdev = virtq->virtq->sdev;


	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_QUERY_GENERAL_OBJECT);

	if (sdev->pci->type == SNAP_VIRTIO_BLK_PF ||
	    sdev->pci->type == SNAP_VIRTIO_BLK_VF) {
		DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
			 MLX5_OBJ_TYPE_VIRTIO_BLK_Q);
		out = out_blk;
		outlen = sizeof(out_blk);
	} else if (sdev->pci->type == SNAP_VIRTIO_NET_PF ||
		   sdev->pci->type == SNAP_VIRTIO_NET_VF) {
		DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
			 MLX5_OBJ_TYPE_VIRTIO_NET_Q);
		out = out_net;
		outlen = sizeof(out_net);
	} else {
		return -EINVAL;
	}
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_id, virtq->virtq->obj_id);

	ret = snap_devx_obj_query(virtq->virtq, in, sizeof(in), out, outlen);
	if (ret)
		return ret;

	virtq_out = out + DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr);
	if (out == out_blk) {
		struct snap_virtio_blk_queue_attr *attr;

		attr = to_blk_queue_attr(vattr);

		vattr->size = DEVX_GET(virtio_blk_q, virtq_out, virtqc.queue_size);
		vattr->idx = DEVX_GET(virtio_blk_q, virtq_out, virtqc.queue_index);
		vattr->dma_mkey = DEVX_GET(virtio_blk_q, virtq_out, virtqc.virtio_q_mkey);

		attr->qpn = DEVX_GET(virtio_blk_q, virtq_out, qpn);
		attr->hw_available_index = DEVX_GET(virtio_blk_q, virtq_out, hw_available_index);
		attr->hw_used_index = DEVX_GET(virtio_blk_q, virtq_out, hw_used_index);
		dev_allowed = DEVX_GET64(virtio_blk_q, virtq_out, modify_field_select);
		if (dev_allowed & MLX5_VIRTIO_BLK_Q_MODIFY_STATE)
			attr->modifiable_fields = SNAP_VIRTIO_BLK_QUEUE_MOD_STATE;
		else
			attr->modifiable_fields = 0;
	} else {
		struct snap_virtio_net_queue_attr *attr;

		attr = to_net_queue_attr(vattr);

		vattr->size = DEVX_GET(virtio_net_q, virtq_out, virtqc.queue_size);
		vattr->idx = DEVX_GET(virtio_net_q, virtq_out, virtqc.queue_index);
		vattr->dma_mkey = DEVX_GET(virtio_net_q, virtq_out, virtqc.virtio_q_mkey);

		attr->tisn_or_qpn = DEVX_GET(virtio_blk_q, virtq_out, qpn);
		attr->hw_available_index = DEVX_GET(virtio_blk_q, virtq_out, hw_available_index);
		attr->hw_used_index = DEVX_GET(virtio_blk_q, virtq_out, hw_used_index);
		dev_allowed = DEVX_GET64(virtio_net_q, virtq_out, modify_field_select);
		if (dev_allowed & MLX5_VIRTIO_NET_Q_MODIFY_STATE)
			attr->modifiable_fields = SNAP_VIRTIO_BLK_QUEUE_MOD_STATE;
		else
			attr->modifiable_fields = 0;
	}

	return 0;
}