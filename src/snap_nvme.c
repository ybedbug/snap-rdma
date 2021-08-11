/*
 * Copyright © 2021 NVIDIA CORPORATION & AFFILIATES. ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of Nvidia Corporation and its affiliates
 * (the "Company") and all right, title, and interest in and to the software
 * product, including all associated intellectual property rights, are and
 * shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 */

#include "snap_nvme.h"
#include "snap_queue.h"
#include "snap_internal.h"
#include "mlx5_ifc.h"

/* doorbell stride as specified in the NVMe CAP register, stride in
 * bytes is 2^(2 + NVME_DB_STRIDE)
 */
#define NVME_DB_STRIDE 0
#define NVME_CQ_LOG_ENTRY_SIZE 4
#define NVME_SQ_LOG_ENTRY_SIZE 6
#define NVME_DB_BASE 0x1000

struct snap_nvme_sq_be {
	struct mlx5dv_devx_obj *obj;
	uint32_t obj_id;
	struct snap_nvme_sq *sq;
	struct snap_alias_object *sq_alias;
	struct ibv_qp *qp;
};

static int snap_nvme_init_sq_legacy_mode(struct snap_device *sdev,
					 struct snap_nvme_sq *sq,
					 const struct snap_nvme_sq_attr *attr);
static void snap_nvme_teardown_sq_legacy_mode(struct snap_device *sdev,
					      struct snap_nvme_sq *sq);

bool snap_nvme_sq_is_fe_only(struct snap_nvme_sq *sq)
{
	struct snap_nvme_sq_attr sq_attr = {};

	if (snap_nvme_query_sq(sq, &sq_attr)) {
		snap_warn("Failed to query provided SQ\n");
		return false;
	}

	return (bool)sq_attr.fe_only;
}

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
	uint8_t *device_emulation_in;
	uint8_t *device_emulation_out;
	uint64_t dev_allowed;
	int ret, out_size;
	uint16_t pci_bdf;

	if (sdev->pci->type != SNAP_NVME_PF && sdev->pci->type != SNAP_NVME_VF)
		return -EINVAL;

	out_size = DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(nvme_device_emulation) + sdev->pci->bar.size;
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
	/* save the most updated bar value for each query */
	memcpy(sdev->pci->bar.data,
	       DEVX_ADDR_OF(nvme_device_emulation, device_emulation_out,
			    register_data),
	       sdev->pci->bar.size);

	snap_get_pci_attr(&sdev->pci->pci_attr,
			  DEVX_ADDR_OF(nvme_device_emulation,
				       device_emulation_out,
				       pci_params));

	attr->num_of_vfs = sdev->pci->pci_attr.num_of_vfs;
	attr->enabled = DEVX_GET(nvme_device_emulation, device_emulation_out,
				 enabled);
	attr->pci_hotplug_state = DEVX_GET(nvme_device_emulation,
					device_emulation_out, pci_hotplug_state);
	dev_allowed = DEVX_GET64(nvme_device_emulation, device_emulation_out,
				 modify_field_select);

	attr->modifiable_fields = 0;
	if (dev_allowed & MLX5_NVME_DEVICE_MODIFY_BAR_CAP_VS_CSTS)
		attr->modifiable_fields |= SNAP_NVME_DEV_MOD_BAR_CAP_VS_CSTS;
	if (dev_allowed & MLX5_NVME_DEVICE_MODIFY_BAR_CC)
		attr->modifiable_fields |= SNAP_NVME_DEV_MOD_BAR_CC;
	if (dev_allowed & MLX5_NVME_DEVICE_MODIFY_BAR_AQA_ASQ_ACQ)
		attr->modifiable_fields |= SNAP_NVME_DEV_MOD_BAR_AQA_ASQ_ACQ;
	if (dev_allowed & MLX5_NVME_DEVICE_MODIFY_PCI_HOTPLUG_STATE)
		attr->modifiable_fields |= SNAP_NVME_DEV_MOD_HOTPLUG_STATE;

	attr->crossed_vhca_mkey = DEVX_GET(nvme_device_emulation,
					   device_emulation_out,
					   emulated_device_crossed_vhca_mkey);

	pci_bdf = DEVX_GET(nvme_device_emulation, device_emulation_out, pci_bdf);
	snap_update_pci_bdf(sdev->pci, pci_bdf);

out_free:
	free(out);
	return ret;
}

static int
snap_nvme_get_modifiable_device_fields(struct snap_device *sdev)
{
	struct snap_nvme_device_attr attr = {};
	int ret;

	ret = snap_nvme_query_device(sdev, &attr);
	if (ret)
		return ret;

	sdev->mod_allowed_mask = attr.modifiable_fields;

	return 0;
}

/**
 * snap_nvme_modify_device() - Modify NVMe snap device
 * @sdev:       snap device
 * @mask:       selected params to modify (mask of enum snap_nvme_device_modify)
 * @attr:       attributes for the NVMe device modify
 *
 * Modify NVMe snap device object according to a given mask.
 *
 * Return: 0 on success.
 */
int snap_nvme_modify_device(struct snap_device *sdev, uint64_t mask,
		struct snap_nvme_device_attr *attr)
{
	uint8_t *in;
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)];
	uint8_t *device_emulation_in;
	int ret, in_size;
	uint64_t modify_mask;

	if (sdev->pci->type != SNAP_NVME_PF && sdev->pci->type != SNAP_NVME_VF)
		return -EINVAL;

	if (!sdev->mod_allowed_mask) {
		ret = snap_nvme_get_modifiable_device_fields(sdev);
		if (ret)
			return ret;
	}

	/* we'll modify only allowed fields */
	if (mask & ~sdev->mod_allowed_mask) {
		snap_error("failed modify NVMe sdev 0x%p mask=0x%lx allowed_mask=0x%lx\n",
			   sdev, mask, sdev->mod_allowed_mask);
		return -EINVAL;
	}

	in_size = DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		  DEVX_ST_SZ_BYTES(nvme_device_emulation) +
		  sdev->pci->bar.size;
	in = calloc(1, in_size);
	if (!in)
		return -ENOMEM;

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_MODIFY_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_OBJ_TYPE_NVME_DEVICE_EMULATION);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_id,
		 sdev->mdev.device_emulation->obj_id);

	device_emulation_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
	DEVX_SET(nvme_device_emulation, device_emulation_in, vhca_id,
		 sdev->pci->mpci.vhca_id);

	memcpy(DEVX_ADDR_OF(nvme_device_emulation, device_emulation_in,
			    register_data), &attr->bar, sdev->pci->bar.size);

	modify_mask = 0;
	if (mask & SNAP_NVME_DEV_MOD_BAR_CAP_VS_CSTS)
		modify_mask |= MLX5_NVME_DEVICE_MODIFY_BAR_CAP_VS_CSTS;
	if (mask & SNAP_NVME_DEV_MOD_BAR_CC)
		modify_mask |= MLX5_NVME_DEVICE_MODIFY_BAR_CC;
	if (mask & SNAP_NVME_DEV_MOD_BAR_AQA_ASQ_ACQ)
		modify_mask |= MLX5_NVME_DEVICE_MODIFY_BAR_AQA_ASQ_ACQ;
	if (mask & SNAP_NVME_DEV_MOD_HOTPLUG_STATE) {
		modify_mask |= MLX5_NVME_DEVICE_MODIFY_PCI_HOTPLUG_STATE;
		DEVX_SET(nvme_device_emulation, device_emulation_in,
		pci_hotplug_state, attr->pci_hotplug_state);
	}
	if (modify_mask)
		DEVX_SET64(nvme_device_emulation, device_emulation_in,
			   modify_field_select, modify_mask);

	ret = mlx5dv_devx_obj_modify(sdev->mdev.device_emulation->obj, in,
				     in_size, out, sizeof(out));
	free(in);
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

	ndev->db_base = NVME_DB_BASE;
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
	const struct snap_nvme_caps *hw_caps = &sdev->sctx->nvme_caps;

	if (attr->src_nsid > hw_caps->max_nvme_nsid) {
		errno = ENOTSUP;
		goto out;
	}

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
	SNAP_TAILQ_FOREACH_SAFE(tmp, &ndev->ns_list, entry, next) {
		if (tmp == ns) {
			found = true;
			SNAP_TAILQ_REMOVE_SAFE(&ndev->ns_list, ns, entry);
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
	const struct snap_nvme_caps *hw_caps = &sdev->sctx->nvme_caps;

	if (attr->type == SNAP_NVME_RAW_MODE) {
		offload_type = MLX5_NVME_CQ_OFFLOAD_TYPE_SQE;
	} else if (attr->type == SNAP_NVME_TO_NVMF_MODE) {
		offload_type = MLX5_NVME_CQ_OFFLOAD_TYPE_CC;
	} else {
		errno = EINVAL;
		goto out;
	}

	if (attr->id >= ndev->num_queues) {
		errno = EINVAL;
		goto out;
	}

	if (attr->interrupt_disable && !hw_caps->cq_interrupt_disabled) {
		errno = ENOTSUP;
		goto out;
	}

	if (attr->queue_depth > hw_caps->max_queue_depth) {
		errno = ENOTSUP;
		goto out;
	}

	cq = &ndev->cqs[attr->id];

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type, MLX5_OBJ_TYPE_NVME_CQ);

	cq_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
	DEVX_SET(nvme_cq, cq_in, device_emulation_id, sdev->pci->mpci.vhca_id);
	DEVX_SET(nvme_cq, cq_in, offload_type, offload_type);
	/* Calculate doorbell offset as described in the NVMe spec 3.1.17 */
	DEVX_SET(nvme_cq, cq_in, nvme_doorbell_offset,
		 ndev->db_base + (2 * attr->id + 1) * (4 << NVME_DB_STRIDE));
	DEVX_SET(nvme_cq, cq_in, interrupt_disabled, attr->interrupt_disable);
	DEVX_SET(nvme_cq, cq_in, msix_vector, attr->msix);
	DEVX_SET(nvme_cq, cq_in, nvme_num_of_entries, attr->queue_depth);
	DEVX_SET64(nvme_cq, cq_in, nvme_base_addr, attr->base_addr);
	DEVX_SET(nvme_cq, cq_in, nvme_log_entry_size,
		 attr->log_entry_size ? attr->log_entry_size :
					NVME_CQ_LOG_ENTRY_SIZE);
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

static int snap_nvme_get_modifiable_sq_fields(struct snap_nvme_sq *sq)
{
	struct snap_nvme_sq_attr attr = {};
	int ret;

	ret = snap_nvme_query_sq(sq, &attr);
	if (ret)
		return ret;

	sq->mod_allowed_mask = attr.modifiable_fields;

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
	uint64_t fields_to_modify = 0;
	bool teardown_legacy = false;
	int ret;

	if (!sq->mod_allowed_mask) {
		ret = snap_nvme_get_modifiable_sq_fields(sq);
		if (ret)
			return ret;
	}

	/* we'll modify only allowed fields */
	if (mask & ~sq->mod_allowed_mask)
		return -EINVAL;

	if (snap_nvme_sq_is_fe_only(sq)) {
		snap_error("Cannot modify fe_only SQ\n");
		return -ENOTSUP;
	}

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_MODIFY_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_OBJ_TYPE_NVME_SQ);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_id, sq->sq->obj_id);

	sq_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
	if (mask & SNAP_NVME_SQ_MOD_QPN) {
		fields_to_modify |=  MLX5_NVME_SQ_MODIFY_QPN;

		if (attr->qp) {
			if (sdev->mdev.vtunnel) {
				/*
				 * we need to reset rdma settings, because
				 * new rdma_dev may be different from
				 * the previous one.
				 */
				snap_nvme_teardown_sq_legacy_mode(sdev, sq);
				if (snap_nvme_init_sq_legacy_mode(sdev, sq,
								  attr)) {
					ret = -ENODEV;
					goto err;
				}
			}
			DEVX_SET(nvme_sq, sq_in, qpn, attr->qp->qp_num);
		} else {
			if (sdev->mdev.vtunnel) {
				/* teardown only after object is modified */
				teardown_legacy = true;
			}
			DEVX_SET(nvme_sq, sq_in, qpn, 0);
		}
	}
	if (mask & SNAP_NVME_SQ_MOD_STATE) {
		fields_to_modify |=  MLX5_NVME_SQ_MODIFY_STATE;
		if (attr->state == SNAP_NVME_SQ_STATE_INIT) {
			DEVX_SET(nvme_sq, sq_in, network_state,
				 MLX5_NVME_SQ_STATE_INIT);
		} else if (attr->state == SNAP_NVME_SQ_STATE_RDY) {
			DEVX_SET(nvme_sq, sq_in, network_state,
				 MLX5_NVME_SQ_STATE_RDY);
		} else if (attr->state == SNAP_NVME_SQ_STATE_ERR) {
			DEVX_SET(nvme_sq, sq_in, network_state,
				 MLX5_NVME_SQ_STATE_ERR);
		} else {
			ret = -EINVAL;
			goto out_free_qp;
		}
	}

	DEVX_SET64(nvme_sq, sq_in, modify_field_select, fields_to_modify);

	ret = snap_devx_obj_modify(sq->sq, in, sizeof(in), out, sizeof(out));
	if (ret)
		goto out_free_qp;

	if (teardown_legacy)
		snap_nvme_teardown_sq_legacy_mode(sdev, sq);

	return 0;

out_free_qp:
	if ((mask & SNAP_NVME_SQ_MOD_QPN) && sdev->mdev.vtunnel)
		snap_nvme_teardown_sq_legacy_mode(sdev, sq);
err:
	return ret;
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
	attr->state = DEVX_GET(nvme_sq, out_sq, network_state);
	attr->fe_only = DEVX_GET(nvme_sq, out_sq, fe_only);
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

static int snap_nvme_init_sq_legacy_mode(struct snap_device *sdev,
					 struct snap_nvme_sq *sq,
					 const struct snap_nvme_sq_attr *attr)
{
	if (!sdev->mdev.vtunnel) {
		snap_warn("Tried to start legacy mode on modern HW. ignoring\n");
		return 0;
	}

	if (!attr->qp)
		return 0;

	sq->rdma_dev = snap_find_get_rdma_dev(sdev, attr->qp->context);
	if (!sq->rdma_dev) {
		errno = EINVAL;
		goto err;
	}

	sq->hw_qp = snap_create_hw_qp(sdev, attr->qp);
	if (!sq->hw_qp) {
		errno = EINVAL;
		goto put_dev;
	}

	return 0;

put_dev:
	snap_put_rdma_dev(sdev, sq->rdma_dev);
	sq->rdma_dev = NULL;
err:
	return -1;
}

static void snap_nvme_teardown_sq_legacy_mode(struct snap_device *sdev,
					      struct snap_nvme_sq *sq)
{
	if (sq->hw_qp) {
		snap_destroy_hw_qp(sq->hw_qp);
		sq->hw_qp = NULL;
	}

	if (sq->rdma_dev) {
		snap_put_rdma_dev(sdev, sq->rdma_dev);
		sq->rdma_dev = NULL;
	}
}

/**
 * snap_nvme_create_sq_be() - Create a new NVMe snap SQ backend object
 * @sdev:       snap device
 * @attr:       attributes for the SQ backend creation
 *
 * Create an NVMe snap SQ backend object with the given attributes.
 *
 * Return: Returns snap_nvme_sq_be in case of success, NULL otherwise and
 * errno will be set to indicate the failure reason.
 */
struct snap_nvme_sq_be *
snap_nvme_create_sq_be(struct snap_device *sdev,
		       struct snap_nvme_sq_be_attr *attr)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(nvme_sq_be)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)] = {0};
	uint8_t *nvme_sq_be_in;
	struct snap_nvme_sq_be *sq_be;
	struct snap_alias_object *sq_alias = NULL;
	uint32_t sq_obj_id;

	if (!attr->sq) {
		snap_error("snap SQ must be provided\n");
		errno = EINVAL;
		goto err;
	}

	if (!attr->qp) {
		snap_error("ibv QP must be provided\n");
		errno = EINVAL;
		goto err;
	}

	if (!snap_nvme_sq_is_fe_only(attr->sq)) {
		snap_error("Cannot create SQ backend for non-fe_only SQ\n");
		errno = ENOTSUP;
		goto err;
	}


	sq_obj_id = attr->sq->sq->obj_id;
	if (snap_get_dev_vhca_id(attr->qp->context) !=
	    snap_get_dev_vhca_id(sdev->sctx->context)) {
		/*
		 * When QP resides on different VHCA than the SQ,
		 * we need to link between them somehow. This is
		 * done by
		 *  - Allowing cross-vhca access to the SQ.
		 *  - Creating alias SQ on QP's context.
		 *  - Use alias SQ obj_id for SQ backend instead of
		 *    the original SQ.
		 */
		snap_debug("Attach QP from RDMA context (vhca_id 0x%x) different than of emu_manager (vhca_id 0x%x)\n",
			   snap_get_dev_vhca_id(attr->qp->context),
			   snap_get_dev_vhca_id(sdev->sctx->context));

		if (snap_allow_other_vhca_access(sdev->sctx->context,
						 MLX5_OBJ_TYPE_NVME_SQ,
						 attr->sq->sq->obj_id, NULL)) {
			snap_error("Failed to allow cross vhca access\n");
			goto err;
		}

		sq_alias = snap_create_alias_object(attr->qp->context,
						    MLX5_OBJ_TYPE_NVME_SQ,
						    sdev->sctx->context,
						    attr->sq->sq->obj_id, NULL);
		if (!sq_alias) {
			snap_error("Failed to create SQ alias\n");
			goto err;
		}

		sq_obj_id = sq_alias->obj_id;
	}

	sq_be = calloc(1, sizeof(*sq_be));
	if (!sq_be) {
		snap_error("Failed to allocate sq_be object\n");
		errno = ENOMEM;
		goto delete_alias;
	}

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		 MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		 MLX5_OBJ_TYPE_NVME_SQ_BE);
	nvme_sq_be_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
	DEVX_SET(nvme_sq_be, nvme_sq_be_in, nvme_sq_id, sq_obj_id);
	DEVX_SET(nvme_sq_be, nvme_sq_be_in, qpn, attr->qp->qp_num);
	sq_be->obj = mlx5dv_devx_obj_create(attr->qp->context, in, sizeof(in), out,
					    sizeof(out));
	if (!sq_be->obj) {
		snap_error("Failed to create nvme_sq_be object\n");
		goto free_sq_be;
	}

	sq_be->obj_id = DEVX_GET(general_obj_out_cmd_hdr, out, obj_id);
	sq_be->sq_alias = sq_alias;
	sq_be->sq = attr->sq;
	sq_be->sq->sq_be = sq_be;
	sq_be->qp = attr->qp;
	snap_debug("backend SQ obj_id 0x%x created for SQ 0x%x and QP 0x%x\n",
		   sq_be->obj_id, sq_be->sq->sq->obj_id, attr->qp->qp_num);

	return sq_be;

free_sq_be:
	free(sq_be);
delete_alias:
	if (sq_alias)
		snap_destroy_alias_object(sq_alias);
err:
	return NULL;
}

void snap_nvme_destroy_sq_be(struct snap_nvme_sq_be *sq_be)
{
	if (sq_be->obj)
		(void)mlx5dv_devx_obj_destroy(sq_be->obj);
	if (sq_be->sq_alias)
		snap_destroy_alias_object(sq_be->sq_alias);
	sq_be->sq->sq_be = NULL;
	free(sq_be);
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
	const struct snap_nvme_caps *hw_caps = &sdev->sctx->nvme_caps;

	if (attr->type == SNAP_NVME_RAW_MODE) {
		offload_type = MLX5_NVME_SQ_OFFLOAD_TYPE_SQE;
	} else if (attr->type == SNAP_NVME_TO_NVMF_MODE) {
		offload_type = MLX5_NVME_SQ_OFFLOAD_TYPE_CC;
	} else {
		errno = EINVAL;
		goto out;
	}

	if (attr->id >= ndev->num_queues) {
		errno = EINVAL;
		goto out;
	}

	if (attr->queue_depth > hw_caps->max_queue_depth) {
		errno = ENOTSUP;
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
	/* Calculate doorbell offset as described in the NVMe spec 3.1.16 */
	DEVX_SET(nvme_sq, sq_in, nvme_doorbell_offset,
		 ndev->db_base + 2 * attr->id * (4 << NVME_DB_STRIDE));
	DEVX_SET(nvme_sq, sq_in, nvme_cq_id, attr->cq->cq->obj_id);
	if (attr->counter_set_id)
		DEVX_SET(nvme_sq, sq_in, counter_set_id, attr->counter_set_id);
	if (attr->fe_only && sdev->mdev.vtunnel) {
		snap_debug("fe_only flag is ignored for Bluefield-1\n");
		attr->fe_only = false;
	}
	DEVX_SET(nvme_sq, sq_in, fe_only, attr->fe_only);
	if (attr->qp) {
		if (sdev->mdev.vtunnel) {
			if (snap_nvme_init_sq_legacy_mode(sdev, sq, attr))
				goto out;
		}
		if (!attr->fe_only)
			DEVX_SET(nvme_sq, sq_in, qpn, attr->qp->qp_num);
		else
			snap_warn("set qpn is not valid when fe_only=1\n");
	}
	DEVX_SET64(nvme_sq, sq_in, nvme_base_addr, attr->base_addr);
	DEVX_SET(nvme_sq, sq_in, nvme_log_entry_size,
		 attr->log_entry_size ? attr->log_entry_size :
					NVME_SQ_LOG_ENTRY_SIZE);

	//DEVX_SET(nvme_sq, sq_in, log_nvme_page_size, attr->log_nvme_page_size);
	//DEVX_SET(nvme_sq, sq_in, max_transaction_size, attr->max_transcation_size);

	sq->sq = snap_devx_obj_create(sdev, in, sizeof(in), out, sizeof(out),
				      sdev->mdev.vtunnel,
				      DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr),
				      DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr));
	if (!sq->sq) {
		errno = ENODEV;
		goto teardown_sq_legacy_mode;
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

teardown_sq_legacy_mode:
	if (attr->qp && sdev->mdev.vtunnel)
		snap_nvme_teardown_sq_legacy_mode(sdev, sq);
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
	struct snap_device *sdev = sq->sq->sdev;
	int ret = 0;

	if (sq->sq_be) {
		snap_error("Cannot destroy SQ with attached sq_be object\n");
		return -EBUSY;
	}

	if (sq->hw_qp) {
		struct snap_nvme_sq_attr sq_attr = {};

		/* Modify SQ to make sure the destruction will succeed */
		sq_attr.state = SNAP_NVME_SQ_STATE_ERR;
		sq_attr.qp = NULL;
		ret = snap_nvme_modify_sq(sq,
			SNAP_NVME_SQ_MOD_QPN | SNAP_NVME_SQ_MOD_STATE,
			&sq_attr);
	}

	ret = snap_devx_obj_destroy(sq->sq);

	/*
	 * If hw qp was not destroyed it means that modify failed because of
	 * the FLR. We have to desrtoy it expicitly in order to avoid leaking
	 * RDMA_FT_RX rdma flow table.
	 */
	if (sdev->mdev.vtunnel)
		snap_nvme_teardown_sq_legacy_mode(sdev, sq);

	return ret;
}

struct snap_nvme_sq_counters*
snap_nvme_create_sq_counters(struct snap_device *sdev)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
	DEVX_ST_SZ_BYTES(nvme_sq_counters)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)] = {0};
	struct snap_nvme_sq_counters *sqc;

	sqc = calloc(1, sizeof(*sqc));
	if (!sqc) {
		errno = ENOMEM;
		goto out;
	}

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		MLX5_OBJ_TYPE_NVME_SQ_COUNTERS);

	sqc->obj = snap_devx_obj_create(sdev, in, sizeof(in), out, sizeof(out),
					sdev->mdev.vtunnel,
					DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr),
					DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr));

	if (!sqc->obj) {
		errno = ENODEV;
		goto out_destroy_sqc;
	}

	return sqc;

out_destroy_sqc:
	free(sqc);
out:
	return NULL;
}

int snap_nvme_query_sq_counters(struct snap_nvme_sq_counters *sqc)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(nvme_sq_counters)] = {0};
	uint8_t *out_sqc, ret;

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
	MLX5_CMD_OP_QUERY_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		MLX5_OBJ_TYPE_NVME_SQ_COUNTERS);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_id, sqc->obj->obj_id);

	ret = snap_devx_obj_query(sqc->obj, in, sizeof(in), out, sizeof(out));
	if (ret)
		return ret;

	out_sqc = out + DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr);

	sqc->data_read = DEVX_GET(nvme_sq_counters, out_sqc,
				  data_read);
	sqc->data_write = DEVX_GET(nvme_sq_counters, out_sqc,
				  data_write);
	sqc->cmd_read = DEVX_GET(nvme_sq_counters, out_sqc,
				 cmd_read);
	sqc->cmd_write = DEVX_GET(nvme_sq_counters, out_sqc,
				  cmd_write);
	sqc->error_cqes = DEVX_GET(nvme_sq_counters, out_sqc,
				   error_cqes);
	sqc->integrity_errors = DEVX_GET(nvme_sq_counters, out_sqc,
					 integrity_errors);
	sqc->fabric_errors = DEVX_GET(nvme_sq_counters, out_sqc,
				      fabric_errors);
	sqc->busy_time = DEVX_GET(nvme_sq_counters, out_sqc,
				  busy_time);
	sqc->power_cycle = DEVX_GET(nvme_sq_counters, out_sqc,
				    power_cycle);
	sqc->power_on_hours = DEVX_GET(nvme_sq_counters, out_sqc,
					power_on_hours);
	sqc->unsafe_shutdowns = DEVX_GET(nvme_sq_counters, out_sqc,
					 unsafe_shutdowns);
	sqc->error_information_log_entries = DEVX_GET(nvme_sq_counters,
				out_sqc, error_information_log_entries);

	return 0;
}

int snap_nvme_destroy_sq_counters(struct snap_nvme_sq_counters *sqc)
{
	int ret;

	ret = snap_devx_obj_destroy(sqc->obj);
	free(sqc);
	return ret;
}

struct snap_nvme_ctrl_counters *
snap_nvme_create_ctrl_counters(struct snap_context *sctx)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		   DEVX_ST_SZ_BYTES(nvme_ctrl_counters)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)] = {0};
	struct snap_nvme_ctrl_counters *ctrlc;

	ctrlc = calloc(1, sizeof(*ctrlc));
	if (!ctrlc) {
		errno = ENOMEM;
		goto out;
	}

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		MLX5_OBJ_TYPE_NVME_CTRL_COUNTERS);
	ctrlc->obj = mlx5dv_devx_obj_create(sctx->context, in, sizeof(in),
					out, sizeof(out));
	if (!ctrlc->obj)
		goto out_destroy_ctrlc;

	ctrlc->id = DEVX_GET(general_obj_out_cmd_hdr, out, obj_id);
	return ctrlc;

out_destroy_ctrlc:
	free(ctrlc);
out:
	return NULL;
}

int snap_nvme_query_ctrl_counters(struct snap_nvme_ctrl_counters *ctrlc)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr) +
		DEVX_ST_SZ_BYTES(nvme_ctrl_counters)] = {0};
	uint8_t *out_ctrlc, ret;

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		MLX5_CMD_OP_QUERY_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		MLX5_OBJ_TYPE_NVME_CTRL_COUNTERS);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_id, ctrlc->id);

	ret = mlx5dv_devx_obj_query(ctrlc->obj, in, sizeof(in),
				out, sizeof(out));
	if (ret)
		return ret;

	out_ctrlc = out + DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr);

	ctrlc->data_read = DEVX_GET(nvme_ctrl_counters, out_ctrlc,
				    data_read);
	ctrlc->data_write = DEVX_GET(nvme_ctrl_counters, out_ctrlc,
				     data_write);
	ctrlc->cmd_read = DEVX_GET(nvme_ctrl_counters, out_ctrlc,
				   cmd_read);
	ctrlc->cmd_write = DEVX_GET(nvme_ctrl_counters, out_ctrlc,
				    cmd_write);
	ctrlc->error_cqes = DEVX_GET(nvme_ctrl_counters, out_ctrlc,
				     error_cqes);
	ctrlc->flrs = DEVX_GET(nvme_ctrl_counters, out_ctrlc,
				flrs);
	ctrlc->bad_doorbells = DEVX_GET(nvme_ctrl_counters, out_ctrlc,
					bad_doorbells);
	ctrlc->integrity_errors = DEVX_GET(nvme_ctrl_counters, out_ctrlc,
					   integrity_errors);
	ctrlc->fabric_errors = DEVX_GET(nvme_ctrl_counters, out_ctrlc,
					fabric_errors);
	ctrlc->busy_time = DEVX_GET(nvme_ctrl_counters, out_ctrlc,
				    busy_time);
	ctrlc->power_cycle = DEVX_GET(nvme_ctrl_counters, out_ctrlc,
				      power_cycle);
	ctrlc->power_on_hours = DEVX_GET(nvme_ctrl_counters, out_ctrlc,
					 power_on_hours);
	ctrlc->unsafe_shutdowns = DEVX_GET(nvme_ctrl_counters, out_ctrlc,
					   unsafe_shutdowns);
	ctrlc->error_information_log_entries = DEVX_GET(nvme_ctrl_counters,
					out_ctrlc, error_information_log_entries);

	return 0;
}

int snap_nvme_destroy_ctrl_counters(struct snap_nvme_ctrl_counters *ctrlc)
{
	int ret;

	ret = mlx5dv_devx_obj_destroy(ctrlc->obj);
	free(ctrlc);
	return ret;
}
