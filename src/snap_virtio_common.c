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

void snap_virtio_get_device_attr(struct snap_device *sdev,
				 struct snap_virtio_device_attr *vattr,
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

	snap_get_pci_attr(&sdev->pci->pci_attr,
			  DEVX_ADDR_OF(virtio_device,
				       device_configuration,
				       pci_params));
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
	struct snap_virtio_queue_attr *vattr, struct snap_virtio_umem *umem)
{
	uint8_t in_blk[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		       DEVX_ST_SZ_BYTES(virtio_blk_q)] = {0};
	uint8_t in_net[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		       DEVX_ST_SZ_BYTES(virtio_net_q)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)] = {0};
	uint8_t *in;
	uint8_t *virtq_in;
	uint8_t *virtq_ctx;
	uint16_t obj_type;
	struct mlx5_snap_devx_obj *virtq;
	int virtq_type, ev_mode, inlen, offload_type;

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

	if (vattr->offload_type == SNAP_VIRTQ_OFFLOAD_ETH_FRAME) {
		offload_type = MLX5_VIRTIO_Q_OFFLOAD_TYPE_ETH_FRAME;
	} else if (vattr->offload_type == SNAP_VIRTQ_OFFLOAD_DESC_TUNNEL) {
		offload_type = MLX5_VIRTIO_Q_OFFLOAD_TYPE_DESC_TUNNEL;
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
		virtq_ctx = DEVX_ADDR_OF(virtio_blk_q, virtq_in, virtqc);

		obj_type = MLX5_OBJ_TYPE_VIRTIO_BLK_Q;
		if (attr->qpn) {
			DEVX_SET(virtio_blk_q, virtq_in, qpn, attr->qpn);
			DEVX_SET(virtio_blk_q, virtq_in, qpn_vhca_id, attr->qpn_vhca_id);
		}
	} else if (sdev->pci->type == SNAP_VIRTIO_NET_PF ||
		   sdev->pci->type == SNAP_VIRTIO_NET_VF) {
		struct snap_virtio_net_queue_attr *attr;

		attr = to_net_queue_attr(vattr);
		in = in_net;
		inlen = sizeof(in_net);
		virtq_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
		virtq_ctx = DEVX_ADDR_OF(virtio_net_q, virtq_in, virtqc);

		obj_type = MLX5_OBJ_TYPE_VIRTIO_NET_Q;
		DEVX_SET(virtio_net_q, virtq_in, tisn_or_qpn, attr->tisn_or_qpn);
		if (attr->tisn_or_qpn)
			DEVX_SET(virtio_net_q, virtq_in, tisn_or_qpn_vhca_id, attr->vhca_id);
		DEVX_SET(virtio_net_q, virtq_in, tso_ipv4, attr->tso_ipv4);
		DEVX_SET(virtio_net_q, virtq_in, tso_ipv6, attr->tso_ipv6);
		DEVX_SET(virtio_net_q, virtq_in, tx_csum, attr->tx_csum);
		DEVX_SET(virtio_net_q, virtq_in, rx_csum, attr->rx_csum);
	} else {
		errno = EINVAL;
		goto out;
	}

	/* common virtq attributes */
	DEVX_SET(virtio_q, virtq_ctx, device_emulation_id,
		 sdev->pci->mpci.vhca_id);
	DEVX_SET(virtio_q, virtq_ctx, virtio_q_type, virtq_type);
	DEVX_SET(virtio_q, virtq_ctx, event_mode, ev_mode);
	DEVX_SET(virtio_q, virtq_ctx, queue_index, vattr->idx);
	DEVX_SET(virtio_q, virtq_ctx, queue_size, vattr->size);
	DEVX_SET(virtio_q, virtq_ctx, full_emulation, vattr->full_emulation);
	DEVX_SET(virtio_q, virtq_ctx, virtio_version_1_0, vattr->virtio_version_1_0);
	DEVX_SET(virtio_q, virtq_ctx, max_tunnel_desc, vattr->max_tunnel_desc);
	DEVX_SET(virtio_q, virtq_ctx, event_qpn_or_msix, vattr->event_qpn_or_msix);
	DEVX_SET(virtio_q, virtq_ctx, offload_type, offload_type);
	DEVX_SET(virtio_q, virtq_ctx, umem_1_id, umem[0].devx_umem->umem_id);
	DEVX_SET(virtio_q, virtq_ctx, umem_1_size, umem[0].size);
	DEVX_SET64(virtio_q, virtq_ctx, umem_1_offset, 0);
	DEVX_SET(virtio_q, virtq_ctx, umem_2_id, umem[1].devx_umem->umem_id);
	DEVX_SET(virtio_q, virtq_ctx, umem_2_size, umem[1].size);
	DEVX_SET64(virtio_q, virtq_ctx, umem_2_offset, 0);
	DEVX_SET(virtio_q, virtq_ctx, umem_3_id, umem[2].devx_umem->umem_id);
	DEVX_SET(virtio_q, virtq_ctx, umem_3_size, umem[2].size);
	DEVX_SET64(virtio_q, virtq_ctx, umem_3_offset, 0);

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

static int snap_virtio_queue_state_to_mlx_state(enum snap_virtq_state state)
{
	switch (state) {
	case SNAP_VIRTQ_STATE_INIT:
		return MLX5_VIRTIO_Q_STATE_INIT;
	case SNAP_VIRTQ_STATE_RDY:
		return MLX5_VIRTIO_Q_STATE_RDY;
	case SNAP_VIRTQ_STATE_SUSPEND:
		return MLX5_VIRTIO_Q_STATE_SUSPEND;
	case SNAP_VIRTQ_STATE_ERR:
		return MLX5_VIRTIO_Q_STATE_ERR;
	default:
		return -EINVAL;
	}
}

int snap_virtio_modify_queue(struct snap_virtio_queue *virtq, uint64_t mask,
	uint64_t allowed_mask, struct snap_virtio_queue_attr *vattr)
{
	uint8_t in_blk[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
			DEVX_ST_SZ_BYTES(virtio_blk_q)] = {0};
	uint8_t in_net[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
			DEVX_ST_SZ_BYTES(virtio_net_q)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)];
	struct snap_device *sdev = virtq->virtq->sdev;
	uint8_t *in;
	uint8_t *virtq_in;
	uint64_t fields_to_modify = 0;
	int inlen, state;

	/* we'll modify only allowed fields */
	if (mask & ~allowed_mask)
		return -EINVAL;

	if (sdev->pci->type == SNAP_VIRTIO_BLK_PF ||
	    sdev->pci->type == SNAP_VIRTIO_BLK_VF) {
		in = in_blk;
		inlen = sizeof(in_blk);

		DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
			 MLX5_OBJ_TYPE_VIRTIO_BLK_Q);
		virtq_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);

		if (mask & SNAP_VIRTIO_BLK_QUEUE_MOD_STATE) {
			state = snap_virtio_queue_state_to_mlx_state(vattr->state);
			if (state < 0)
				return state;
			fields_to_modify = MLX5_VIRTIO_BLK_Q_MODIFY_STATE;
			DEVX_SET(virtio_blk_q, virtq_in, state, state);
		}
		DEVX_SET64(virtio_blk_q, virtq_in, modify_field_select,
			   fields_to_modify);
	} else if (sdev->pci->type == SNAP_VIRTIO_NET_PF ||
		   sdev->pci->type == SNAP_VIRTIO_NET_VF) {
		in = in_net;
		inlen = sizeof(in_net);

		DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
			 MLX5_OBJ_TYPE_VIRTIO_NET_Q);
		virtq_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);

		if (mask & SNAP_VIRTIO_NET_QUEUE_MOD_STATE) {
			state = snap_virtio_queue_state_to_mlx_state(vattr->state);
			if (state < 0)
				return state;
			fields_to_modify = MLX5_VIRTIO_NET_Q_MODIFY_STATE;
			DEVX_SET(virtio_net_q, virtq_in, state, state);
		}
		DEVX_SET64(virtio_net_q, virtq_in, modify_field_select,
			   fields_to_modify);
	} else {
		return -ENODEV;
	}

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_MODIFY_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_id, virtq->virtq->obj_id);

	return snap_devx_obj_modify(virtq->virtq, in, inlen, out, sizeof(out));
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
	uint8_t state;
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

		state = DEVX_GET(virtio_blk_q, virtq_out, state);
		if (state == MLX5_VIRTIO_Q_STATE_INIT)
			vattr->state = SNAP_VIRTQ_STATE_INIT;
		else if (state == MLX5_VIRTIO_Q_STATE_RDY)
			vattr->state = SNAP_VIRTQ_STATE_RDY;
		else if (state == MLX5_VIRTIO_Q_STATE_SUSPEND)
			vattr->state = SNAP_VIRTQ_STATE_SUSPEND;
		else if (state == MLX5_VIRTIO_Q_STATE_ERR)
			vattr->state = SNAP_VIRTQ_STATE_ERR;
		else
			return -EINVAL;

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

		state = DEVX_GET(virtio_blk_q, virtq_out, state);
		if (state == MLX5_VIRTIO_Q_STATE_INIT)
			vattr->state = SNAP_VIRTQ_STATE_INIT;
		else if (state == MLX5_VIRTIO_Q_STATE_RDY)
			vattr->state = SNAP_VIRTQ_STATE_RDY;
		else if (state == MLX5_VIRTIO_Q_STATE_SUSPEND)
			vattr->state = SNAP_VIRTQ_STATE_SUSPEND;
		else if (state == MLX5_VIRTIO_Q_STATE_ERR)
			vattr->state = SNAP_VIRTQ_STATE_ERR;
		else
			return -EINVAL;

		attr->hw_available_index = DEVX_GET(virtio_net_q, virtq_out, hw_available_index);
		attr->hw_used_index = DEVX_GET(virtio_net_q, virtq_out, hw_used_index);
		dev_allowed = DEVX_GET64(virtio_net_q, virtq_out, modify_field_select);
		if (dev_allowed & MLX5_VIRTIO_NET_Q_MODIFY_STATE)
			attr->modifiable_fields = SNAP_VIRTIO_NET_QUEUE_MOD_STATE;
		else
			attr->modifiable_fields = 0;
	}

	return 0;
}

int snap_virtio_init_virtq_umem(struct snap_context *sctx,
		struct snap_virtio_caps *virtio,
		struct snap_virtio_queue *virtq,
		int depth)
{
	int ret;

	virtq->umem[0].size = (virtio->umem_1_buffer_param_a * depth) +
			virtio->umem_1_buffer_param_b;
	ret = posix_memalign((void **)&virtq->umem[0].buf,
			     SNAP_VIRTIO_UMEM_ALIGN,
			     virtq->umem[0].size);
	if (ret)
		goto out;
	virtq->umem[0].devx_umem = mlx5dv_devx_umem_reg(sctx->context,
							virtq->umem[0].buf,
							virtq->umem[0].size,
							IBV_ACCESS_LOCAL_WRITE);
	if (!virtq->umem[0].devx_umem)
		goto out_free_buf_0;

	virtq->umem[1].size = (virtio->umem_2_buffer_param_a * depth) +
			virtio->umem_2_buffer_param_b;
	ret = posix_memalign((void **)&virtq->umem[1].buf,
			     SNAP_VIRTIO_UMEM_ALIGN,
			     virtq->umem[1].size);
	if (ret)
		goto out_free_umem_0;
	virtq->umem[1].devx_umem = mlx5dv_devx_umem_reg(sctx->context,
							virtq->umem[1].buf,
							virtq->umem[1].size,
							IBV_ACCESS_LOCAL_WRITE);
	if (!virtq->umem[1].devx_umem)
		goto out_free_buf_1;

	virtq->umem[2].size = (virtio->umem_3_buffer_param_a * depth) +
			virtio->umem_3_buffer_param_b;
	ret = posix_memalign((void **)&virtq->umem[2].buf,
			     SNAP_VIRTIO_UMEM_ALIGN,
			     virtq->umem[2].size);
	if (ret)
		goto out_free_umem_1;
	virtq->umem[2].devx_umem = mlx5dv_devx_umem_reg(sctx->context,
							virtq->umem[2].buf,
							virtq->umem[2].size,
							IBV_ACCESS_LOCAL_WRITE);
	if (!virtq->umem[2].devx_umem)
		goto out_free_buf_2;

	return 0;

out_free_buf_2:
	free(virtq->umem[2].buf);
out_free_umem_1:
	mlx5dv_devx_umem_dereg(virtq->umem[1].devx_umem);
out_free_buf_1:
	free(virtq->umem[1].buf);
out_free_umem_0:
	mlx5dv_devx_umem_dereg(virtq->umem[0].devx_umem);
out_free_buf_0:
	free(virtq->umem[0].buf);
out:
	return ENOMEM;
}

void snap_virtio_teardown_virtq_umem(struct snap_virtio_queue *virtq)
{
	mlx5dv_devx_umem_dereg(virtq->umem[2].devx_umem);
	free(virtq->umem[2].buf);
	virtq->umem[2].size = 0;

	mlx5dv_devx_umem_dereg(virtq->umem[1].devx_umem);
	free(virtq->umem[1].buf);
	virtq->umem[1].size = 0;

	mlx5dv_devx_umem_dereg(virtq->umem[0].devx_umem);
	free(virtq->umem[0].buf);
	virtq->umem[0].size = 0;
}
