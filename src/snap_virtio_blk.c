#include "snap_virtio_blk.h"

#include "mlx5_ifc.h"

/**
 * snap_virtio_blk_query_device() - Query an Virtio block snap device
 * @sdev:       snap device
 * @attr:       Virtio block snap device attr container (output)
 *
 * Query a Virtio block snap device. Attr argument must have enough space for
 * the output data.
 *
 * Return: Returns 0 in case of success and attr is filled.
 */
int snap_virtio_blk_query_device(struct snap_device *sdev,
	struct snap_virtio_blk_device_attr *attr)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(virtio_blk_device_emulation)] = {0};
	uint8_t *out;
	struct snap_context *sctx = sdev->sctx;
	uint8_t *device_emulation_in;
	uint8_t *device_emulation_out;
	int i, ret, out_size;

	if (sdev->pci->type != SNAP_VIRTIO_BLK_PF &&
	    sdev->pci->type != SNAP_VIRTIO_BLK_VF)
		return -EINVAL;

	if (attr->queues > sctx->mctx.virtio_blk.max_emulated_virtqs)
		return -EINVAL;

	out_size = DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(virtio_blk_device_emulation) +
		   attr->queues * DEVX_ST_SZ_BYTES(virtio_q_layout);
	out = calloc(1, out_size);
	if (!out)
		return -ENOMEM;

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_QUERY_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_OBJ_TYPE_VIRTIO_BLK_DEVICE_EMULATION);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_id,
		 sdev->mdev.device_emulation->obj_id);

	device_emulation_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
	DEVX_SET(virtio_blk_device_emulation, device_emulation_in, vhca_id,
		 sdev->pci->mpci.vhca_id);

	ret = mlx5dv_devx_obj_query(sdev->mdev.device_emulation->obj, in,
				    sizeof(in), out, out_size);
	if (ret)
		goto out_free;

	device_emulation_out = out + DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr);
	if (attr->queues) {
		for (i = 0; i < attr->queues; i++) {
			struct snap_virtio_blk_queue_attr *q_attr = &attr->q_attrs[i];

			q_attr->vattr.size = DEVX_GET(virtio_blk_device_emulation,
					      device_emulation_out,
					      virtio_q_configuration[i].queue_size);
			q_attr->vattr.msix_vector = DEVX_GET(virtio_blk_device_emulation,
					      device_emulation_out,
					      virtio_q_configuration[i].queue_msix_vector);
			q_attr->vattr.enable = DEVX_GET(virtio_blk_device_emulation,
					      device_emulation_out,
					      virtio_q_configuration[i].queue_enable);
			q_attr->vattr.notify_off = DEVX_GET(virtio_blk_device_emulation,
					      device_emulation_out,
					      virtio_q_configuration[i].queue_notify_off);
			q_attr->vattr.desc = DEVX_GET64(virtio_blk_device_emulation,
					      device_emulation_out,
					      virtio_q_configuration[i].queue_desc);
			q_attr->vattr.driver = DEVX_GET64(virtio_blk_device_emulation,
					      device_emulation_out,
					      virtio_q_configuration[i].queue_driver);
			q_attr->vattr.device = DEVX_GET64(virtio_blk_device_emulation,
					      device_emulation_out,
					      virtio_q_configuration[i].queue_device);
		}
	}

	attr->enabled = DEVX_GET(virtio_blk_device_emulation,
				 device_emulation_out, enabled);
out_free:
	free(out);
	return ret;
}

/**
 * snap_virtio_blk_init_device() - Initialize a new snap device with VIRTIO
 *                                 block characteristics
 * @sdev:       snap device
 *
 * Initialize a snap device for Virtio block emulation. Allocate the needed
 * resources in the HCA and setup internal context.
 *
 * Return: Returns 0 in case of success.
 */
int snap_virtio_blk_init_device(struct snap_device *sdev)
{
	struct snap_virtio_blk_device *vbdev;
	int ret, i;

	if (sdev->pci->type != SNAP_VIRTIO_BLK_PF &&
	    sdev->pci->type != SNAP_VIRTIO_BLK_VF)
		return -EINVAL;

	vbdev = calloc(1, sizeof(*vbdev));
	if (!vbdev)
		return -ENOMEM;

	vbdev->vdev.num_queues = sdev->sctx->mctx.virtio_blk.max_emulated_virtqs;

	vbdev->virtqs = calloc(vbdev->vdev.num_queues, sizeof(*vbdev->virtqs));
	if (!vbdev->virtqs) {
		ret = -ENOMEM;
		goto out_free;
	}

	for (i = 0; i < vbdev->vdev.num_queues; i++)
		vbdev->virtqs[i].vbdev = vbdev;

	ret = snap_init_device(sdev);
	if (ret)
		goto out_free_virtqs;

	sdev->dd_data = vbdev;
	vbdev->vdev.sdev = sdev;

	return 0;

out_free_virtqs:
	free(vbdev->virtqs);
out_free:
	free(vbdev);
	return ret;
}

/**
 * snap_virtio_blk_teardown_device() - Teardown Virtio block specifics from a
 *                                     snap device
 * @sdev:       snap device
 *
 * Teardown and free Virtio block context from a snap device.
 *
 * Return: Returns 0 in case of success.
 */
int snap_virtio_blk_teardown_device(struct snap_device *sdev)
{
	struct snap_virtio_blk_device *vbdev;
	int ret = 0;

	vbdev = (struct snap_virtio_blk_device *)sdev->dd_data;
	if (sdev->pci->type != SNAP_VIRTIO_BLK_PF &&
	    sdev->pci->type != SNAP_VIRTIO_BLK_VF)
		return -EINVAL;

	sdev->dd_data = NULL;

	ret = snap_teardown_device(sdev);

	free(vbdev->virtqs);
	free(vbdev);

	return ret;
}

/**
 * snap_virtio_blk_create_queue() - Create a new Virtio block snap queue object
 * @sdev:       snap device
 * @attr:       attributes for the queue creation
 *
 * Create a Virtio block snap queue object with the given attributes.
 *
 * Return: Returns snap_virtio_blk_queue in case of success, NULL otherwise and
 * errno will be set to indicate the failure reason.
 */
struct snap_virtio_blk_queue*
snap_virtio_blk_create_queue(struct snap_device *sdev,
	struct snap_virtio_blk_queue_attr *attr)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(virtio_blk_q)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)] = {0};
	struct snap_virtio_blk_device *vbdev;
	uint8_t *virtq_in;
	struct snap_virtio_blk_queue *vbq;
	int virtq_type, ev_mode;

	vbdev = (struct snap_virtio_blk_device *)sdev->dd_data;

	if (attr->type == SNAP_VIRTQ_SPLIT_MODE) {
		virtq_type = MLX5_VIRTIO_QUEUE_TYPE_SPLIT;
	} else if (attr->type == SNAP_VIRTQ_PACKED_MODE) {
		virtq_type = MLX5_VIRTIO_QUEUE_TYPE_PACKED;
	} else {
		errno = EINVAL;
		goto out;
	}

	if (attr->ev_mode == SNAP_VIRTQ_NO_MSIX_MODE) {
		ev_mode = MLX5_VIRTIO_QUEUE_EVENT_MODE_NO_MSIX;
	} else if (attr->ev_mode == SNAP_VIRTQ_CQ_MODE) {
		ev_mode = MLX5_VIRTIO_QUEUE_EVENT_MODE_CQ;
	} else if (attr->ev_mode == SNAP_VIRTQ_MSIX_MODE) {
		ev_mode = MLX5_VIRTIO_QUEUE_EVENT_MODE_MSIX;
	} else {
		errno = EINVAL;
		goto out;
	}

	if (attr->idx > vbdev->vdev.num_queues) {
		errno = EINVAL;
		goto out;
	}

	vbq = &vbdev->virtqs[attr->idx];
	vbq->virtq.idx = attr->idx;

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type, MLX5_OBJ_TYPE_VIRTIO_BLK_Q);

	virtq_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
	DEVX_SET(virtio_blk_q, virtq_in, qpn, attr->qpn);
	DEVX_SET(virtio_blk_q, virtq_in, virtqc.device_emulation_id,
		 sdev->pci->mpci.vhca_id);
	DEVX_SET(virtio_blk_q, virtq_in, virtqc.virtio_q_type, virtq_type);
	DEVX_SET(virtio_blk_q, virtq_in, virtqc.event_mode, ev_mode);
	DEVX_SET(virtio_blk_q, virtq_in, virtqc.queue_index, attr->idx);
	DEVX_SET(virtio_blk_q, virtq_in, virtqc.queue_size, attr->vattr.size);
	vbq->virtq.virtq = snap_devx_obj_create(sdev, in, sizeof(in), out, sizeof(out),
				      sdev->mdev.vtunnel,
				      DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr),
				      DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr));
	if (!vbq->virtq.virtq) {
		errno = ENODEV;
		goto out;
	}

	if (sdev->mdev.vtunnel) {
		void *dtor = vbq->virtq.virtq->dtor_in;

		DEVX_SET(general_obj_in_cmd_hdr, dtor, opcode,
			 MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
		DEVX_SET(general_obj_in_cmd_hdr, dtor, obj_type,
			 MLX5_OBJ_TYPE_VIRTIO_BLK_Q);
		DEVX_SET(general_obj_in_cmd_hdr, dtor, obj_id,
			 vbq->virtq.virtq->obj_id);
	}

	return vbq;
out:
	return NULL;
}

/**
 * snap_virtio_blk_destroy_queue() - Destroy Virtio block queue object
 * @vbq:       Virtio block queue
 *
 * Destroy and free a snap virtio block queue context.
 *
 * Return: Returns 0 on success.
 */
int snap_virtio_blk_destroy_queue(struct snap_virtio_blk_queue *vbq)
{
	return snap_devx_obj_destroy(vbq->virtq.virtq);
}
