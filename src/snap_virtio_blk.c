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
	uint8_t *out;
	struct snap_context *sctx = sdev->sctx;
	uint8_t *device_emulation_out;
	int i, ret, out_size;
	uint64_t dev_allowed;

	if (attr->queues > sctx->virtio_blk_caps.max_emulated_virtqs)
		return -EINVAL;

	out_size = DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(virtio_blk_device_emulation) +
		   attr->queues * DEVX_ST_SZ_BYTES(virtio_q_layout);
	out = calloc(1, out_size);
	if (!out)
		return -ENOMEM;

	ret = snap_virtio_query_device(sdev, SNAP_VIRTIO_BLK, out, out_size);
	if (ret)
		goto out_free;

	device_emulation_out = out + DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr);

	snap_virtio_get_device_attr(&attr->vattr,
				    DEVX_ADDR_OF(virtio_blk_device_emulation,
						 device_emulation_out,
						 virtio_device));
	if (attr->queues) {
		for (i = 0; i < attr->queues; i++)
			snap_virtio_get_queue_attr(&attr->q_attrs[i].vattr,
						   DEVX_ADDR_OF(virtio_blk_device_emulation,
								device_emulation_out,
								virtio_q_configuration[i]));
	}

	attr->capacity = DEVX_GET64(virtio_blk_device_emulation,
				    device_emulation_out,
				    virtio_blk_config.capacity);
	attr->size_max = DEVX_GET(virtio_blk_device_emulation,
				  device_emulation_out,
				  virtio_blk_config.size_max);
	attr->seg_max = DEVX_GET(virtio_blk_device_emulation,
				 device_emulation_out,
				 virtio_blk_config.seg_max);
	attr->blk_size = DEVX_GET(virtio_blk_device_emulation,
				  device_emulation_out,
				  virtio_blk_config.blk_size);
	attr->vattr.enabled = DEVX_GET(virtio_blk_device_emulation,
				       device_emulation_out, enabled);
	dev_allowed = DEVX_GET64(virtio_blk_device_emulation,
				 device_emulation_out, modify_field_select);
	if (dev_allowed) {
		if (dev_allowed & MLX5_VIRTIO_DEVICE_MODIFY_STATUS)
			attr->modifiable_fields = SNAP_VIRTIO_MOD_DEV_STATUS;
	} else {
		attr->modifiable_fields = 0;
	}

out_free:
	free(out);
	return ret;
}

static int
snap_virtio_blk_get_modifiable_device_fields(struct snap_device *sdev,
		uint64_t *allowed)
{
	struct snap_virtio_blk_device_attr attr = {};
	int ret;

	ret = snap_virtio_blk_query_device(sdev, &attr);
	if (ret)
		return ret;

	*allowed = attr.modifiable_fields;

	return 0;
}

/**
 * snap_virtio_blk_modify_device() - Modify Virtio blk snap device
 * @sdev:       snap device
 * @mask:       selected params to modify (mask of enum snap_virtio_dev_modify)
 * @attr:       attributes for the blk device modify
 *
 * Modify Virtio blk snap device object according to a given mask.
 *
 * Return: 0 on success.
 */
int snap_virtio_blk_modify_device(struct snap_device *sdev, uint64_t mask,
		struct snap_virtio_blk_device_attr *attr)
{
	uint64_t allowed_mask;
	int ret;

	ret = snap_virtio_blk_get_modifiable_device_fields(sdev,
							   &allowed_mask);
	if (ret)
		return ret;

	return snap_virtio_modify_device(sdev, SNAP_VIRTIO_BLK, mask,
					 allowed_mask, &attr->vattr);
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

	vbdev->vdev.num_queues = sdev->sctx->virtio_blk_caps.max_emulated_virtqs;

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

static int snap_consume_virtio_blk_queue_event(struct mlx5_snap_devx_obj *obj,
		struct snap_event *sevent)
{
	struct snap_device *sdev = obj->sdev;
	struct snap_virtio_blk_device *vbdev;
	struct snap_virtio_blk_queue *vbq = NULL;
	int i;

	if (sdev->pci->type != SNAP_VIRTIO_BLK_PF &&
	    sdev->pci->type != SNAP_VIRTIO_BLK_VF)
		return -EINVAL;

	vbdev = (struct snap_virtio_blk_device *)sdev->dd_data;
	for (i = 0; i < vbdev->vdev.num_queues; i++) {
		if (vbdev->virtqs[i].virtq.virtq == obj) {
			vbq = &vbdev->virtqs[i];
			break;
		}
	}

	if (!vbq)
		return -EINVAL;

	sevent->type = SNAP_EVENT_VIRTIO_BLK_QUEUE_CHANGE;
	sevent->obj = vbq;

	return 0;
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
 * snap_virtio_blk_query_queue() - Query a Virtio block queue object
 * @vbq:        snap Virtio block queue
 * @attr:       attributes for the queue query (output)
 *
 * Query a Virtio block snap queue object.
 *
 * Return: 0 on success, and attr is filled with the query result.
 */
int snap_virtio_blk_query_queue(struct snap_virtio_blk_queue *vbq,
		struct snap_virtio_blk_queue_attr *attr)
{
	return snap_virtio_query_queue(&vbq->virtq, &attr->vattr);
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
	struct snap_virtio_blk_device *vbdev;
	struct snap_virtio_blk_queue *vbq;
	int ret;

	vbdev = (struct snap_virtio_blk_device *)sdev->dd_data;

	if (attr->vattr.idx >= vbdev->vdev.num_queues) {
		errno = EINVAL;
		goto out;
	}

	vbq = &vbdev->virtqs[attr->vattr.idx];

	vbq->virtq.virtq = snap_virtio_create_queue(sdev, &attr->vattr);
	if (!vbq->virtq.virtq)
		goto out;

	if (sdev->mdev.channel) {
		uint16_t ev_type = MLX5_EVENT_TYPE_OBJECT_CHANGE;

		ret = mlx5dv_devx_subscribe_devx_event(sdev->mdev.channel,
			vbq->virtq.virtq->obj,
			sizeof(ev_type), &ev_type,
			(uint64_t)vbq->virtq.virtq);
		if (ret)
			goto destroy_queue;

		vbq->virtq.virtq->consume_event = snap_consume_virtio_blk_queue_event;
	}

	vbq->virtq.idx = attr->vattr.idx;

	return vbq;

destroy_queue:
	snap_devx_obj_destroy(vbq->virtq.virtq);
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
	vbq->virtq.virtq->consume_event = NULL;

	return snap_devx_obj_destroy(vbq->virtq.virtq);
}
