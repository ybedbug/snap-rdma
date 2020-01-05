#include "snap_nvme.h"

#include "mlx5_ifc.h"

#define NVME_DB_STRIDE 3
#define NVME_CQ_LOG_ENTRY_SIZE 4
#define NVME_SQ_LOG_ENTRY_SIZE 6
#define NVME_DB_BASE 0x1000

int snap_nvme_query_sq(struct snap_nvme_sq *sq,
		       struct snap_nvme_sq_attr *attr);
/**
 * snap_nvme_query_device() - Query an NVMe snap device
 * @sdev:       snap device
 * @attr:       NVMe snap device attr container (output)
 *
 * Query an NVMe snap device. Attr argument must have enough space for the
 * output data.
 *
 * Return: Returns 0 in case of success and attr is filled.
 */
int snap_nvme_query_device(struct snap_device *sdev,
	struct snap_nvme_device_attr *attr)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(nvme_device_emulation)] = {0};
	uint8_t *out;
	struct snap_context *sctx = sdev->sctx;
	uint8_t *device_emulation_in;
	uint8_t *device_emulation_out;
	int ret, out_size;

	if (sdev->pci->type != SNAP_NVME_PF && sdev->pci->type != SNAP_NVME_VF)
		return -EINVAL;

	if (attr->bar_size > sctx->nvme_caps.reg_size)
		return -EINVAL;

	out_size = DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(nvme_device_emulation) + attr->bar_size;
	out = calloc(1, out_size);
	if (!out)
		return -ENOMEM;

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_QUERY_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_OBJ_TYPE_NVME_DEVICE_EMULATION);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_id,
		 sdev->mdev.device_emulation->obj_id);

	device_emulation_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
	DEVX_SET(nvme_device_emulation, device_emulation_in, vhca_id,
		 sdev->pci->mpci.vhca_id);

	ret = mlx5dv_devx_obj_query(sdev->mdev.device_emulation->obj, in,
				    sizeof(in), out, out_size);
	if (ret)
		goto out_free;

	device_emulation_out = out + DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr);
	if (attr->bar_size)
		memcpy(attr->bar,
		       DEVX_ADDR_OF(nvme_device_emulation,
				    device_emulation_out, register_data),
		       attr->bar_size);

	attr->enabled = DEVX_GET(nvme_device_emulation, device_emulation_out,
				 enabled);
out_free:
	free(out);
	return ret;
}

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

	/*
	 * Admin queue is calculated in num_queues. Also Keep 1:1 mapping for
	 * NVMe SQs/CQs.
	 */
	ndev->num_queues = snap_min(sdev->sctx->nvme_caps.max_emulated_nvme_cqs,
				    sdev->sctx->nvme_caps.max_emulated_nvme_sqs);

	ndev->cqs = calloc(ndev->num_queues, sizeof(*ndev->cqs));
	if (!ndev->cqs) {
		ret = -ENOMEM;
		goto out_free;
	}

	ndev->sqs = calloc(ndev->num_queues, sizeof(*ndev->sqs));
	if (!ndev->sqs) {
		ret = -ENOMEM;
		goto out_free_cqs;
	}

	ret = pthread_mutex_init(&ndev->lock, NULL);
	if (ret)
		goto out_free_sqs;

	TAILQ_INIT(&ndev->ns_list);

	ret = snap_init_device(sdev);
	if (ret)
		goto out_free_mutex;

	sdev->dd_data = ndev;
	ndev->sdev = sdev;

	return 0;

out_free_mutex:
	pthread_mutex_destroy(&ndev->lock);
out_free_sqs:
	free(ndev->sqs);
out_free_cqs:
	free(ndev->cqs);
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

	ret = snap_teardown_device(sdev);

	pthread_mutex_destroy(&ndev->lock);
	free(ndev->sqs);
	free(ndev->cqs);
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

/**
 * snap_nvme_create_cq() - Create a new NVMe snap CQ object
 * @sdev:       snap device
 * @attr:       attributes for the CQ creation
 *
 * Create an NVMe snap CQ object with the given attributes.
 *
 * Return: Returns snap_nvme_cq in case of success, NULL otherwise and
 * errno will be set to indicate the failure reason.
 */
struct snap_nvme_cq*
snap_nvme_create_cq(struct snap_device *sdev, struct snap_nvme_cq_attr *attr)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(nvme_cq)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)] = {0};
	struct snap_nvme_device *ndev = (struct snap_nvme_device *)sdev->dd_data;
	uint8_t *cq_in;
	struct snap_nvme_cq *cq;
	int offload_type;

	if (attr->type == SNAP_NVME_SQE_MODE) {
		offload_type = MLX5_NVME_CQ_OFFLOAD_TYPE_SQE;
	} else if (attr->type == SNAP_NVME_CC_MODE) {
		offload_type = MLX5_NVME_CQ_OFFLOAD_TYPE_CC;
	} else {
		errno = EINVAL;
		goto out;
	}

	if (attr->id != attr->doorbell_offset >> NVME_DB_STRIDE) {
		errno = EINVAL;
		goto out;
	}

	if (attr->id >= ndev->num_queues) {
		errno = EINVAL;
		goto out;
	}

	cq = &ndev->cqs[attr->id];

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type, MLX5_OBJ_TYPE_NVME_CQ);

	cq_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
	DEVX_SET(nvme_cq, cq_in, device_emulation_id, sdev->pci->mpci.vhca_id);
	DEVX_SET(nvme_cq, cq_in, offload_type, offload_type);
	DEVX_SET(nvme_cq, cq_in, nvme_doorbell_offset,
		 NVME_DB_BASE + attr->doorbell_offset);
	DEVX_SET(nvme_cq, cq_in, msix_vector, attr->msix);
	DEVX_SET(nvme_cq, cq_in, nvme_num_of_entries, attr->queue_depth);
	DEVX_SET64(nvme_cq, cq_in, nvme_base_addr, attr->base_addr);
	DEVX_SET(nvme_cq, cq_in, nvme_log_entry_size, NVME_CQ_LOG_ENTRY_SIZE);
	DEVX_SET(nvme_cq, cq_in, cq_period, attr->cq_period);
	DEVX_SET(nvme_cq, cq_in, cq_max_count, attr->cq_max_count);
	cq->cq = snap_devx_obj_create(sdev, in, sizeof(in), out, sizeof(out),
				      sdev->mdev.vtunnel,
				      DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr),
				      DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr));
	if (!cq->cq) {
		errno = ENODEV;
		goto out;
	}

	if (sdev->mdev.vtunnel) {
		void *dtor = cq->cq->dtor_in;

		DEVX_SET(general_obj_in_cmd_hdr, dtor, opcode,
			 MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
		DEVX_SET(general_obj_in_cmd_hdr, dtor, obj_type,
			 MLX5_OBJ_TYPE_NVME_CQ);
		DEVX_SET(general_obj_in_cmd_hdr, dtor, obj_id, cq->cq->obj_id);
	}

	cq->id = attr->id;

	return cq;

out:
	return NULL;
}

/**
 * snap_nvme_destroy_cq() - Destroy NVMe CQ object
 * @cq:       nvme CQ
 *
 * Destroy and free a snap nvme CQ context.
 *
 * Return: Returns 0 on success.
 */
int snap_nvme_destroy_cq(struct snap_nvme_cq *cq)
{
	return snap_devx_obj_destroy(cq->cq);
}

static int snap_nvme_get_modifiable_sq_fields(struct snap_nvme_sq *sq,
		uint64_t *allowed)
{
	struct snap_nvme_sq_attr attr = {};
	int ret;

	ret = snap_nvme_query_sq(sq, &attr);
	if (ret)
		return ret;

	*allowed = attr.modifiable_fields;

	return 0;
}

/**
 * snap_nvme_modify_sq() - Modify an NVMe snap SQ object
 * @sq:         snap NVMe SQ
 * @mask:       selected params to modify (mask of enum snap_nvme_sq_modify)
 * @attr:       attributes for the SQ modify
 *
 * Modify an NVMe snap SQ object according to a given mask.
 *
 * Return: 0 on success.
 */
int snap_nvme_modify_sq(struct snap_nvme_sq *sq, uint64_t mask,
		struct snap_nvme_sq_attr *attr)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(nvme_sq)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)];
	struct snap_device *sdev = sq->sq->sdev;
	uint8_t *sq_in;
	uint64_t allowed_mask, fields_to_modify = 0;
	int ret;

	ret = snap_nvme_get_modifiable_sq_fields(sq, &allowed_mask);
	if (ret)
		return ret;

	/* we'll modify only allowed fields */
	if (mask & ~allowed_mask)
		return -EINVAL;

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_MODIFY_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_OBJ_TYPE_NVME_SQ);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_id, sq->sq->obj_id);

	sq_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
	if (mask & SNAP_NVME_SQ_MOD_QPN) {
		fields_to_modify |=  MLX5_NVME_SQ_MODIFY_QPN;
		DEVX_SET(nvme_sq, sq_in, qpn, attr->qpn);
		// TODO: Update qpn_vhca_id as well
	}
	if (mask & SNAP_NVME_SQ_MOD_STATE) {
		fields_to_modify |=  MLX5_NVME_SQ_MODIFY_STATE;
		DEVX_SET(nvme_sq, sq_in, network_state, attr->state);
	}

	DEVX_SET64(nvme_sq, sq_in, modify_field_select, fields_to_modify);

	return snap_devx_obj_modify(sq->sq, in, sizeof(in), out, sizeof(out));
}

/**
 * snap_nvme_query_sq() - Query an NVMe snap SQ object
 * @sq:         snap NVMe SQ
 * @attr:       attributes for the SQ query (output)
 *
 * Query an NVMe snap SQ object.
 *
 * Return: 0 on success, and attr is filled with the query result.
 */
int snap_nvme_query_sq(struct snap_nvme_sq *sq, struct snap_nvme_sq_attr *attr)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr) +
		    DEVX_ST_SZ_BYTES(nvme_sq)] = {0};
	struct snap_device *sdev = sq->sq->sdev;
	uint8_t *out_sq;
	uint64_t dev_allowed;
	int ret;

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_QUERY_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_OBJ_TYPE_NVME_SQ);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_id, sq->sq->obj_id);

	ret = snap_devx_obj_query(sq->sq, in, sizeof(in), out, sizeof(out));
	if (ret)
		return ret;

	out_sq = out + DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr);
	attr->queue_depth = DEVX_GET(nvme_sq, out_sq, nvme_num_of_entries);
	attr->emulated_device_dma_mkey = DEVX_GET(nvme_sq, out_sq,
						  emulated_device_dma_mkey);
	attr->doorbell_offset = DEVX_GET(nvme_sq, out_sq, nvme_doorbell_offset);
	dev_allowed = DEVX_GET64(nvme_sq, out_sq, modify_field_select);
	if (dev_allowed) {
		if (dev_allowed & MLX5_NVME_SQ_MODIFY_QPN)
			attr->modifiable_fields = SNAP_NVME_SQ_MOD_QPN;
		if (dev_allowed & MLX5_NVME_SQ_MODIFY_STATE)
			attr->modifiable_fields |= SNAP_NVME_SQ_MOD_STATE;
	} else {
		attr->modifiable_fields = 0;
	}

	return 0;
}

/**
 * snap_nvme_create_sq() - Create a new NVMe snap SQ object
 * @sdev:       snap device
 * @attr:       attributes for the SQ creation
 *
 * Create an NVMe snap SQ object with the given attributes.
 *
 * Return: Returns snap_nvme_sq in case of success, NULL otherwise and
 * errno will be set to indicate the failure reason.
 */
struct snap_nvme_sq*
snap_nvme_create_sq(struct snap_device *sdev, struct snap_nvme_sq_attr *attr)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(nvme_sq)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)] = {0};
	struct snap_nvme_device *ndev = (struct snap_nvme_device *)sdev->dd_data;
	uint8_t *sq_in;
	struct snap_nvme_sq *sq;
	int offload_type;

	if (attr->type == SNAP_NVME_SQE_MODE) {
		offload_type = MLX5_NVME_SQ_OFFLOAD_TYPE_SQE;
	} else if (attr->type == SNAP_NVME_CC_MODE) {
		offload_type = MLX5_NVME_SQ_OFFLOAD_TYPE_CC;
	} else {
		errno = EINVAL;
		goto out;
	}

	if (attr->id != attr->doorbell_offset >> NVME_DB_STRIDE) {
		errno = EINVAL;
		goto out;
	}

	if (attr->id >= ndev->num_queues) {
		errno = EINVAL;
		goto out;
	}

	sq = &ndev->sqs[attr->id];

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type, MLX5_OBJ_TYPE_NVME_SQ);

	sq_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
	DEVX_SET(nvme_sq, sq_in, device_emulation_id, sdev->pci->mpci.vhca_id);
	DEVX_SET(nvme_sq, sq_in, offload_type, offload_type);
	DEVX_SET(nvme_sq, sq_in, nvme_num_of_entries, attr->queue_depth);
	DEVX_SET(nvme_sq, sq_in, nvme_doorbell_offset,
		 NVME_DB_BASE + attr->doorbell_offset);
	DEVX_SET(nvme_sq, sq_in, nvme_cq_id, attr->cq->cq->obj_id);
	DEVX_SET(nvme_sq, sq_in, qpn, attr->qpn);
	DEVX_SET64(nvme_sq, sq_in, nvme_base_addr, attr->base_addr);
	DEVX_SET(nvme_sq, sq_in, nvme_log_entry_size, NVME_SQ_LOG_ENTRY_SIZE);

	//DEVX_SET(nvme_sq, sq_in, log_nvme_page_size, attr->log_nvme_page_size);
	//DEVX_SET(nvme_sq, sq_in, max_transaction_size, attr->max_transcation_size);

	sq->sq = snap_devx_obj_create(sdev, in, sizeof(in), out, sizeof(out),
				      sdev->mdev.vtunnel,
				      DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr),
				      DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr));
	if (!sq->sq) {
		errno = ENODEV;
		goto out;
	}

	if (sdev->mdev.vtunnel) {
		void *dtor = sq->sq->dtor_in;

		DEVX_SET(general_obj_in_cmd_hdr, dtor, opcode,
			 MLX5_CMD_OP_DESTROY_GENERAL_OBJECT);
		DEVX_SET(general_obj_in_cmd_hdr, dtor, obj_type,
			 MLX5_OBJ_TYPE_NVME_SQ);
		DEVX_SET(general_obj_in_cmd_hdr, dtor, obj_id, sq->sq->obj_id);
	}

	sq->id = attr->id;

	return sq;

out:
	return NULL;
}

/**
 * snap_nvme_destroy_sq() - Destroy NVMe SQ object
 * @sq:       nvme SQ
 *
 * Destroy and free a snap nvme SQ context.
 *
 * Return: Returns 0 on success.
 */
int snap_nvme_destroy_sq(struct snap_nvme_sq *sq)
{
	return snap_devx_obj_destroy(sq->sq);
}
