#include "snap_nvme.h"

/**
 * snap_nvme_init_device() - Initialize a new snap device with NVMe
 *                           characteristics
 * @sdev:       snap device
 *
 * Initialize a snap device for NVMe emulation. Allocate the needed resources
 * in the HCA and setup internal context.
 *
 * Return: Returns 0 in case of success.
 */
int snap_nvme_init_device(struct snap_device *sdev)
{
	struct snap_nvme_device *ndev;
	int ret;

	if (sdev->pci->type != SNAP_NVME_PF && sdev->pci->type != SNAP_NVME_VF)
		return -EINVAL;

	ndev = calloc(1, sizeof(*ndev));
	if (!ndev)
		return -ENOMEM;

	ret = pthread_mutex_init(&ndev->lock, NULL);
	if (ret)
		goto out_free;

	TAILQ_INIT(&ndev->ns_list);

	ret = snap_init_device(sdev);
	if (ret)
		goto out_free_mutex;

	sdev->dd_data = ndev;
	ndev->sdev = sdev;

	return 0;

out_free_mutex:
	pthread_mutex_destroy(&ndev->lock);
out_free:
	free(ndev);
	return ret;
}

/**
 * snap_nvme_teardown_device() - Teardown NVMe specifics from a snap device
 * @sdev:       snap device
 *
 * Teardown and free NVMe context from a snap device.
 *
 * Return: Returns 0 in case of success.
 */
int snap_nvme_teardown_device(struct snap_device *sdev)
{
	struct snap_nvme_device *ndev = (struct snap_nvme_device *)sdev->dd_data;
	int ret = 0;

	if (sdev->pci->type != SNAP_NVME_PF && sdev->pci->type != SNAP_NVME_VF)
		return -EINVAL;

	sdev->dd_data = NULL;

	pthread_mutex_destroy(&ndev->lock);
	ret = snap_teardown_device(sdev);

	free(ndev);

	return ret;
}

/**
 * snap_nvme_create_namespace() - Create a new NVMe snap namespace object
 * @sdev:       snap device
 * @attr:       attributes for the namespace creation
 *
 * Create an NVMe snap namespace object with the given attributes.
 *
 * Return: Returns snap_nvme_namespace in case of success, NULL otherwise and
 * errno will be set to indicate the failure reason.
 */
struct snap_nvme_namespace*
snap_nvme_create_namespace(struct snap_device *sdev,
		struct snap_nvme_namespace_attr *attr)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(nvme_namespace)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)] = {0};
	struct snap_nvme_device *ndev = (struct snap_nvme_device *)sdev->dd_data;
	uint8_t *namespace_in;
	struct snap_nvme_namespace *ns;

	ns = calloc(1, sizeof(*ns));
	if (!ns) {
		errno = ENOMEM;
		goto out;
	}

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_OBJ_TYPE_NVME_NAMESPACE);

	namespace_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
	DEVX_SET(nvme_namespace, namespace_in, device_emulation_id,
		 sdev->pci->mpci.vhca_id);
	DEVX_SET(nvme_namespace, namespace_in, src_nsid, attr->src_nsid);
	DEVX_SET(nvme_namespace, namespace_in, dst_nsid, attr->dst_nsid);
	DEVX_SET(nvme_namespace, namespace_in, lba_size, attr->lba_size);
	DEVX_SET(nvme_namespace, namespace_in, metadata_size, attr->md_size);

	ns->ns = snap_devx_obj_create(sdev, in, sizeof(in), out, sizeof(out),
				      sdev->mdev.vtunnel,
				      DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr),
				      DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr));
	if (!ns->ns) {
		errno = ENODEV;
		goto out_free;
	}

	if (sdev->mdev.vtunnel) {
		void *dtor = ns->ns->dtor_in;

		DEVX_SET(general_obj_in_cmd_hdr, dtor, opcode,
			 MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
		DEVX_SET(general_obj_in_cmd_hdr, dtor, obj_type,
			 MLX5_OBJ_TYPE_NVME_NAMESPACE);
		DEVX_SET(general_obj_in_cmd_hdr, dtor, obj_id, ns->ns->obj_id);

	}

	ns->src_id = attr->src_nsid;
	ns->dst_id = attr->dst_nsid;

	pthread_mutex_lock(&ndev->lock);
	TAILQ_INSERT_HEAD(&ndev->ns_list, ns, entry);
	pthread_mutex_unlock(&ndev->lock);

	return ns;

out_free:
	free(ns);
out:
	return NULL;
}

/**
 * snap_nvme_destroy_namespace() - Destroy NVMe namespace object
 * @ns:       nvme namespace
 *
 * Destroy and free a snap nvme namespace context.
 *
 * Return: Returns 0 on success.
 */
int snap_nvme_destroy_namespace(struct snap_nvme_namespace *ns)
{
	struct snap_device *sdev = ns->ns->sdev;
	struct snap_nvme_device *ndev = (struct snap_nvme_device *)sdev->dd_data;
	bool found = false;
	struct snap_nvme_namespace *tmp, *next;
	int ret;

	pthread_mutex_lock(&ndev->lock);
	TAILQ_FOREACH_SAFE(tmp, &ndev->ns_list, entry, next) {
		if (tmp == ns) {
			found = true;
			TAILQ_REMOVE(&ndev->ns_list, ns, entry);
			break;
		}
	}
	pthread_mutex_unlock(&ndev->lock);

	if (!found)
		return -ENODEV;

	ret = snap_devx_obj_destroy(ns->ns);
	free(ns);
	return ret;
}
