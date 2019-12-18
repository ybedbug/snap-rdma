#include "snap_virtio_net.h"

#include "mlx5_ifc.h"

/**
 * snap_virtio_net_query_device() - Query an Virtio net snap device
 * @sdev:       snap device
 * @attr:       Virtio net snap device attr container (output)
 *
 * Query a Virtio net snap device. Attr argument must have enough space for
 * the output data.
 *
 * Return: Returns 0 in case of success and attr is filled.
 */
int snap_virtio_net_query_device(struct snap_device *sdev,
	struct snap_virtio_net_device_attr *attr)
{
	uint8_t *out;
	struct snap_context *sctx = sdev->sctx;
	uint8_t *device_emulation_out;
	int i, ret, out_size;

	if (attr->queues > sctx->mctx.virtio_net.max_emulated_virtqs)
		return -EINVAL;

	out_size = DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(virtio_net_device_emulation) +
		   attr->queues * DEVX_ST_SZ_BYTES(virtio_q_layout);
	out = calloc(1, out_size);
	if (!out)
		return -ENOMEM;

	ret = snap_virtio_query_device(sdev, SNAP_VIRTIO_NET, out, out_size);
	if (ret)
		goto out_free;

	device_emulation_out = out + DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr);

	snap_virtio_get_device_attr(&attr->vattr,
				    DEVX_ADDR_OF(virtio_net_device_emulation,
						 device_emulation_out,
						 virtio_device));
	if (attr->queues) {
		for (i = 0; i < attr->queues; i++)
			snap_virtio_get_queue_attr(&attr->q_attrs[i].vattr,
						   DEVX_ADDR_OF(virtio_net_device_emulation,
								device_emulation_out,
								virtio_q_configuration[i]));
	}

	attr->vattr.enabled = DEVX_GET(virtio_net_device_emulation,
				       device_emulation_out, enabled);
out_free:
	free(out);
	return ret;
}

/**
 * snap_virtio_net_init_device() - Initialize a new snap device with VIRTIO
 *                                 net characteristics
 * @sdev:       snap device
 *
 * Initialize a snap device for Virtio net emulation. Allocate the needed
 * resources in the HCA and setup internal context.
 *
 * Return: Returns 0 in case of success.
 */
int snap_virtio_net_init_device(struct snap_device *sdev)
{
	struct snap_virtio_net_device *vndev;
	int ret, i;

	if (sdev->pci->type != SNAP_VIRTIO_NET_PF &&
	    sdev->pci->type != SNAP_VIRTIO_NET_VF)
		return -EINVAL;

	vndev = calloc(1, sizeof(*vndev));
	if (!vndev)
		return -ENOMEM;

	vndev->vdev.num_queues = sdev->sctx->mctx.virtio_net.max_emulated_virtqs;

	vndev->virtqs = calloc(vndev->vdev.num_queues, sizeof(*vndev->virtqs));
	if (!vndev->virtqs) {
		ret = -ENOMEM;
		goto out_free;
	}

	for (i = 0; i < vndev->vdev.num_queues; i++)
		vndev->virtqs[i].vndev = vndev;

	ret = snap_init_device(sdev);
	if (ret)
		goto out_free_virtqs;

	sdev->dd_data = vndev;
	vndev->vdev.sdev = sdev;

	return 0;

out_free_virtqs:
	free(vndev->virtqs);
out_free:
	free(vndev);
	return ret;
}

/**
 * snap_virtio_net_teardown_device() - Teardown Virtio net specifics from a
 *                                     snap device
 * @sdev:       snap device
 *
 * Teardown and free Virtio net context from a snap device.
 *
 * Return: Returns 0 in case of success.
 */
int snap_virtio_net_teardown_device(struct snap_device *sdev)
{
	struct snap_virtio_net_device *vndev;
	int ret = 0;

	vndev = (struct snap_virtio_net_device *)sdev->dd_data;
	if (sdev->pci->type != SNAP_VIRTIO_NET_PF &&
	    sdev->pci->type != SNAP_VIRTIO_NET_VF)
		return -EINVAL;

	sdev->dd_data = NULL;

	ret = snap_teardown_device(sdev);

	free(vndev->virtqs);
	free(vndev);

	return ret;
}

/**
 * snap_virtio_net_create_queue() - Create a new Virtio net snap queue object
 * @sdev:       snap device
 * @attr:       attributes for the queue creation
 *
 * Create a Virtio net snap queue object with the given attributes.
 *
 * Return: Returns snap_virtio_net_queue in case of success, NULL otherwise and
 * errno will be set to indicate the failure reason.
 */
struct snap_virtio_net_queue*
snap_virtio_net_create_queue(struct snap_device *sdev,
	struct snap_virtio_net_queue_attr *attr)
{
	struct snap_virtio_net_device *vndev;
	struct snap_virtio_net_queue *vnq;

	vndev = (struct snap_virtio_net_device *)sdev->dd_data;

	if (attr->vattr.idx >= vndev->vdev.num_queues) {
		errno = EINVAL;
		goto out;
	}

	vnq = &vndev->virtqs[attr->vattr.idx];
	vnq->virtq.idx = attr->vattr.idx;

	return vnq;
out:
	return NULL;
}

/**
 * snap_virtio_net_destroy_queue() - Destroy Virtio net queue object
 * @vnq:       Virtio net queue
 *
 * Destroy and free a snap virtio net queue context.
 *
 * Return: Returns 0 on success.
 */
int snap_virtio_net_destroy_queue(struct snap_virtio_net_queue *vnq)
{
	return 0;
}
