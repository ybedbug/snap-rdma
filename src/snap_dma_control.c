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

#include <stdlib.h>
#include <arpa/inet.h>

#include "snap_dma_internal.h"
#include "snap_env.h"
#include "mlx5_ifc.h"
#include "snap_umr.h"
#include "snap_dpa.h"

#include "config.h"

SNAP_ENV_REG_ENV_VARIABLE(SNAP_DMA_Q_OPMODE, 0);
SNAP_ENV_REG_ENV_VARIABLE(SNAP_DMA_Q_IOV_SUPP, 0);
SNAP_ENV_REG_ENV_VARIABLE(SNAP_DMA_Q_CRYPTO_SUPP, 0);
SNAP_ENV_REG_ENV_VARIABLE(SNAP_DMA_Q_DBMODE, SNAP_DB_RING_BATCH);

struct snap_roce_caps {
	bool resources_on_nvme_emulation_manager;
	bool roce_enabled;
	uint8_t roce_version;
	bool fl_when_roce_disabled;
	bool fl_when_roce_enabled;
	uint16_t r_roce_max_src_udp_port;
	uint16_t r_roce_min_src_udp_port;
};

static int fill_roce_caps(struct ibv_context *context,
			  struct snap_roce_caps *roce_caps)
{

	uint8_t in[DEVX_ST_SZ_BYTES(query_hca_cap_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(query_hca_cap_out)] = {0};
	int ret;

	DEVX_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	DEVX_SET(query_hca_cap_in, in, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_GENERAL_DEVICE);
	ret = mlx5dv_devx_general_cmd(context, in, sizeof(in), out,
				      sizeof(out));
	if (ret)
		return ret;

	roce_caps->resources_on_nvme_emulation_manager =
		 DEVX_GET(query_hca_cap_out, out,
		 capability.cmd_hca_cap.resources_on_nvme_emulation_manager);
	roce_caps->fl_when_roce_disabled = DEVX_GET(query_hca_cap_out, out,
		 capability.cmd_hca_cap.fl_rc_qp_when_roce_disabled);
	roce_caps->roce_enabled = DEVX_GET(query_hca_cap_out, out,
						capability.cmd_hca_cap.roce);
	if (!roce_caps->roce_enabled)
		goto out;

	memset(in, 0, sizeof(in));
	memset(out, 0, sizeof(out));
	DEVX_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	DEVX_SET(query_hca_cap_in, in, op_mod, MLX5_SET_HCA_CAP_OP_MOD_ROCE);
	ret = mlx5dv_devx_general_cmd(context, in, sizeof(in), out,
				      sizeof(out));
	if (ret)
		return ret;

	roce_caps->roce_version = DEVX_GET(query_hca_cap_out, out,
					   capability.roce_cap.roce_version);
	roce_caps->fl_when_roce_enabled = DEVX_GET(query_hca_cap_out,
			out, capability.roce_cap.fl_rc_qp_when_roce_enabled);
	roce_caps->r_roce_max_src_udp_port = DEVX_GET(query_hca_cap_out,
			out, capability.roce_cap.r_roce_max_src_udp_port);
	roce_caps->r_roce_min_src_udp_port = DEVX_GET(query_hca_cap_out,
			out, capability.roce_cap.r_roce_min_src_udp_port);
out:
	snap_debug("RoCE Caps: enabled %d ver %d fl allowed %d\n",
		   roce_caps->roce_enabled, roce_caps->roce_version,
		   roce_caps->roce_enabled ? roce_caps->fl_when_roce_enabled :
		   roce_caps->fl_when_roce_disabled);
	return 0;
}

static int check_port(struct ibv_context *ctx, int port_num, bool *roce_en,
		      bool *ib_en, enum ibv_mtu *mtu)
{
	uint8_t in[DEVX_ST_SZ_BYTES(query_nic_vport_context_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(query_nic_vport_context_out)] = {0};
	uint8_t devx_v;
	struct ibv_port_attr port_attr = {};
	int ret;

	*roce_en = false;
	*ib_en = false;

	ret = ibv_query_port(ctx, port_num, &port_attr);
	if (ret)
		return ret;

	if (port_attr.link_layer == IBV_LINK_LAYER_INFINIBAND) {
		/* we only support local IB addressing for now */
		if (port_attr.flags & IBV_QPF_GRH_REQUIRED) {
			snap_error("IB enabled and GRH addressing is required but only local addressing is supported\n");
			return -1;
		}
		*mtu = port_attr.active_mtu;
		*ib_en = true;
		return 0;
	}

	if (port_attr.link_layer != IBV_LINK_LAYER_ETHERNET)
		return -1;

	/* port may be ethernet but still have roce disabled */
	DEVX_SET(query_nic_vport_context_in, in, opcode,
		 MLX5_CMD_OP_QUERY_NIC_VPORT_CONTEXT);
	ret = mlx5dv_devx_general_cmd(ctx, in, sizeof(in), out,
				      sizeof(out));
	if (ret) {
		snap_error("Failed to get VPORT context - assuming ROCE is disabled\n");
		return ret;
	}
	devx_v = DEVX_GET(query_nic_vport_context_out, out,
			  nic_vport_context.roce_en);
	if (devx_v)
		*roce_en = true;

	/* When active mtu is invalid, default to 1K MTU. */
	*mtu = port_attr.active_mtu ? port_attr.active_mtu : IBV_MTU_1024;
	return 0;
}

static void snap_destroy_qp_helper(struct snap_dma_ibv_qp *qp, bool destroy_cqs)
{
	if (!snap_qp_on_dpa(qp->qp)) {
		if (qp->dv_qp.comps)
			free(qp->dv_qp.comps);

		if (qp->dv_qp.opaque_buf) {
			ibv_dereg_mr(qp->dv_qp.opaque_mr);
			free(qp->dv_qp.opaque_buf);
		}
	} else {
		snap_dpa_mkey_free(qp->dpa.mkey);
	}

	snap_qp_destroy(qp->qp);
	if (destroy_cqs) {
		if (qp->rx_cq)
			snap_cq_destroy(qp->rx_cq);
		if (qp->tx_cq)
			snap_cq_destroy(qp->tx_cq);
	}
}

static void snap_free_rx_wqes(struct snap_dma_ibv_qp *qp)
{
	if (!qp->rx_buf)
		return;
	ibv_dereg_mr(qp->rx_mr);
	free(qp->rx_buf);
}

static int snap_alloc_rx_wqes(struct ibv_pd *pd, struct snap_dma_ibv_qp *qp, size_t rx_qsize,
		size_t rx_elem_size)
{
	int rc;

	if (rx_qsize == 0) {
		qp->rx_buf = 0;
		return 0;
	}

	rc = posix_memalign((void **)&qp->rx_buf, SNAP_DMA_RX_BUF_ALIGN,
			rx_qsize * rx_elem_size);
	if (rc)
		return rc;

	qp->rx_mr = ibv_reg_mr(pd, qp->rx_buf, rx_qsize * rx_elem_size,
			IBV_ACCESS_LOCAL_WRITE);
	if (!qp->rx_mr) {
		free(qp->rx_buf);
		return -ENOMEM;
	}

	return 0;
}

static int dummy_progress(struct snap_dma_q *q)
{
	return 0;
}

/* NOTE: we cannot take mode from the dma_q_attr because it can be 'autoselect'
 * and then it is replaced by the real mode in dma_q->ops
 *
 * This is done to keep creation attribute 'const'
 */
static int snap_create_qp_helper(struct ibv_pd *pd, const struct snap_dma_q_create_attr *dma_q_attr,
		struct snap_qp_attr *qp_init_attr, struct snap_dma_ibv_qp *qp, int mode)
{
	struct snap_cq_attr cq_attr = {
		.cq_context = dma_q_attr->comp_context,
		.comp_channel = dma_q_attr->comp_channel,
		.comp_vector = dma_q_attr->comp_vector,
		.cqe_cnt = qp_init_attr->sq_size,
		.cqe_size = SNAP_DMA_Q_TX_CQE_SIZE
	};
	int rc;

	qp->mode = mode;

	snap_error("\nlizh snap_create_qp_helper mode %d", mode);
	/* TODO: add attribute to choose how snap_qp/cq are created */
	if (mode == SNAP_DMA_Q_MODE_VERBS)
		cq_attr.cq_type = SNAP_OBJ_VERBS;
	else
		cq_attr.cq_type = dma_q_attr->use_devx ? SNAP_OBJ_DEVX : SNAP_OBJ_DV;

	snap_error("\nlizh snap_create_qp_helper cq_type %d dpa_mode %d",
	cq_attr.cq_type, dma_q_attr->dpa_mode);
	switch (dma_q_attr->dpa_mode) {
	case SNAP_DMA_Q_DPA_MODE_POLLING:
		qp_init_attr->qp_on_dpa = true;
		qp_init_attr->dpa_proc = dma_q_attr->dpa_proc;

		cq_attr.cq_type = SNAP_OBJ_DEVX;
		cq_attr.cq_on_dpa = true;
		cq_attr.dpa_element_type = MLX5_APU_ELEMENT_TYPE_EQ;
		cq_attr.dpa_proc = dma_q_attr->dpa_proc;
		break;

	case SNAP_DMA_Q_DPA_MODE_EVENT:
		qp_init_attr->qp_on_dpa = true;
		qp_init_attr->dpa_proc = snap_dpa_thread_proc(dma_q_attr->dpa_thread);

		cq_attr.cq_type = SNAP_OBJ_DEVX;
		cq_attr.cq_on_dpa = true;
		cq_attr.dpa_element_type = MLX5_APU_ELEMENT_TYPE_THREAD;
		cq_attr.dpa_thread = dma_q_attr->dpa_thread;
		break;

	case SNAP_DMA_Q_DPA_MODE_TRIGGER:
		qp_init_attr->qp_on_dpa = false;

		cq_attr.cq_type = SNAP_OBJ_DEVX;
		cq_attr.cq_on_dpa = true;
		cq_attr.dpa_element_type = MLX5_APU_ELEMENT_TYPE_THREAD;
		cq_attr.dpa_thread = dma_q_attr->dpa_thread;
		break;

	case SNAP_DMA_Q_DPA_MODE_NONE:
		break;
	default:
		snap_error("unsupported dpa mode %d\n", dma_q_attr->dpa_mode);
		return -EINVAL;
	}

	if (qp_init_attr->sq_size) {
		qp->tx_cq = snap_cq_create(pd->context, &cq_attr);
		snap_error("\nlizh snap_create_qp_helper qp->tx_cq %p", qp->tx_cq);
		if (!qp->tx_cq)
			return -EINVAL;
	} else
		qp->tx_cq = NULL;

	if (qp_init_attr->rq_size) {
		cq_attr.cqe_cnt = qp_init_attr->rq_size;
		/* Use 128 bytes cqes in order to allow scatter to cqe on receive
		 * This is relevant for NVMe sqe and for virtio queues when number of
		 * tunneled descr is less then three.
		 */
		cq_attr.cqe_size = SNAP_DMA_Q_RX_CQE_SIZE;
		qp->rx_cq = snap_cq_create(pd->context, &cq_attr);
		snap_error("\nlizh snap_create_qp_helper qp->rx_cq %p", qp->rx_cq);
		if (!qp->rx_cq)
			goto free_tx_cq;
	} else
		qp->rx_cq = NULL;

	qp_init_attr->qp_type = cq_attr.cq_type;
	qp_init_attr->sq_cq = qp->tx_cq;
	qp_init_attr->rq_cq = qp->rx_cq;

	qp->qp = snap_qp_create(pd, qp_init_attr);
	snap_error("\nlizh snap_create_qp_helper snap_qp_create qp->qp %p", qp->qp);
	if (!qp->qp)
		goto free_rx_cq;

	rc = snap_qp_to_hw_qp(qp->qp, &qp->dv_qp.hw_qp);
	snap_error("\nlizh snap_create_qp_helper snap_qp_to_hw_qp rc %d", rc);
	if (rc)
		goto free_qp;

	if (mode == SNAP_DMA_Q_MODE_VERBS)
		return 0;

	if (qp->tx_cq) {
		rc = snap_cq_to_hw_cq(qp->tx_cq, &qp->dv_tx_cq);
		snap_error("\nlizh snap_create_qp_helper snap_cq_to_hw_cq tx_cq rc %d", rc);
		if (rc)
			goto free_qp;
	}

	if (qp->rx_cq) {
		rc = snap_cq_to_hw_cq(qp->rx_cq, &qp->dv_rx_cq);
		snap_error("\nlizh snap_create_qp_helper snap_cq_to_hw_cq rx_cq rc %d", rc);
		if (rc)
			goto free_qp;
	}

	if (!qp_init_attr->qp_on_dpa) {
		rc = posix_memalign((void **)&qp->dv_qp.comps, SNAP_DMA_BUF_ALIGN,
				qp->dv_qp.hw_qp.sq.wqe_cnt * sizeof(struct snap_dv_dma_completion));
		snap_error("\nlizh snap_create_qp_helper posix_memalign rc %d", rc);
		if (rc)
			goto free_qp;

		memset(qp->dv_qp.comps, 0,
				qp->dv_qp.hw_qp.sq.wqe_cnt * sizeof(struct snap_dv_dma_completion));
	} else {
		qp->dpa.mkey = snap_dpa_mkey_alloc(qp_init_attr->dpa_proc, pd);
		snap_error("\nlizh snap_create_qp_helper snap_dpa_mkey_alloc mkey %p", qp->dpa.mkey);
		if (!qp->dpa.mkey)
			goto free_qp;
		qp->dv_qp.dpa_mkey = snap_dpa_mkey_id(qp->dpa.mkey);
	}

	if (!qp->dv_qp.hw_qp.sq.tx_db_nc) {
#if defined(__aarch64__)
		snap_error("DB record must be in the non-cacheable memory on BF\n");
		goto free_comps;
#else
		snap_warn("DB record is not in the non-cacheable memory. Performance may be reduced\n"
			  "Try setting MLX5_SHUT_UP_BF environment variable\n");
#endif
	}

	if (mode == SNAP_DMA_Q_MODE_DV)
		return 0;

	/* TODO: gga on dpa */
	rc = posix_memalign((void **)&qp->dv_qp.opaque_buf,
			    sizeof(struct mlx5_dma_opaque),
			    qp->dv_qp.hw_qp.sq.wqe_cnt * sizeof(struct mlx5_dma_opaque));
	snap_error("\nlizh snap_create_qp_helper posix_memalign gga on dpa rc %d", rc);
	if (rc)
		goto free_comps;

	qp->dv_qp.opaque_mr = ibv_reg_mr(pd, qp->dv_qp.opaque_buf,
					 qp->dv_qp.hw_qp.sq.wqe_cnt * sizeof(struct mlx5_dma_opaque),
					 IBV_ACCESS_LOCAL_WRITE);
	snap_error("\nlizh snap_create_qp_helper ibv_reg_mr mr %p", qp->dv_qp.opaque_mr);
	if (!qp->dv_qp.opaque_mr)
		goto free_opaque;

	qp->dv_qp.opaque_lkey = htobe32(qp->dv_qp.opaque_mr->lkey);
	return 0;

free_opaque:
	free(qp->dv_qp.opaque_buf);
free_comps:
	if (!qp_init_attr->qp_on_dpa)
		free(qp->dv_qp.comps);
	else
		snap_dpa_mkey_free(qp->dpa.mkey);
free_qp:
	snap_qp_destroy(qp->qp);
free_rx_cq:
	if (qp->rx_cq)
		snap_cq_destroy(qp->rx_cq);
free_tx_cq:
	if (qp->tx_cq)
		snap_cq_destroy(qp->tx_cq);
	return -EINVAL;
}

static void snap_destroy_sw_qp(struct snap_dma_q *q)
{
	bool destroy_cqs = true;

	if (!snap_qp_on_dpa(q->sw_qp.qp))
		snap_free_rx_wqes(&q->sw_qp);

	if (q->custom_ops) {
		free(q->custom_ops);
		q->custom_ops = NULL;
	}

	if (q->worker)
		destroy_cqs = false;
	snap_destroy_qp_helper(&q->sw_qp, destroy_cqs);
}

static int clone_ops(struct snap_dma_q *q)
{
	struct snap_dma_q_ops *new_ops;

	if (q->custom_ops)
		return 0;

	new_ops = malloc(sizeof(*new_ops));
	if (!new_ops)
		return -1;

	memcpy(new_ops, q->ops, sizeof(*new_ops));
	q->custom_ops = new_ops;
	q->ops = q->custom_ops;

	return 0;
}

struct snap_mmo_caps_dma {
	bool qp_support;
	bool sq_support;
	uint8_t log_max_size : 5;
};

struct snap_mmo_caps_regexp {
	bool qp_support;
	bool sq_support;
	uint8_t log_sg_size : 5;
};

struct snap_mmo_caps_compress {
	bool qp_support;
	bool sq_support;
	uint8_t log_max_size : 5;
	uint8_t min_block_size : 4;
};

struct snap_mmo_caps_decompress {
	bool qp_support;
	bool sq_support;
	uint8_t log_max_size : 5;
	bool snappy;
	bool lz4_data_only;
	bool lz4_no_checksum;
	bool lz4_checksum;
};

/*
 * struct snap_mmo_caps - compression and HW acceleration capabilities
 * @dma: GGA engine support
 * @regexp: regexp support
 * @compress: compression support
 * @decompress: decompression support
 */
struct snap_mmo_caps {
	struct snap_mmo_caps_dma dma;
	struct snap_mmo_caps_regexp regexp;
	struct snap_mmo_caps_compress compress;
	struct snap_mmo_caps_decompress decompress;
};

static int query_mmo_caps(struct ibv_context *context,
				struct snap_mmo_caps *caps)
{
	uint8_t in[DEVX_ST_SZ_BYTES(query_hca_cap_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(query_hca_cap_out)] = {0};
	int ret;

	DEVX_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	DEVX_SET(query_hca_cap_in, in, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_GENERAL_DEVICE);
	ret = mlx5dv_devx_general_cmd(context, in, sizeof(in), out,
				      sizeof(out));
	if (ret)
		return ret;

	caps->dma.qp_support = DEVX_GET(query_hca_cap_out, out,
					capability.cmd_hca_cap.dma_mmo_qp);
	caps->dma.sq_support = DEVX_GET(query_hca_cap_out, out,
					capability.cmd_hca_cap.dma_mmo_sq);
	caps->dma.log_max_size = DEVX_GET(query_hca_cap_out, out,
					capability.cmd_hca_cap.log_dma_mmo_max_size);

	caps->regexp.qp_support = DEVX_GET(query_hca_cap_out, out,
					capability.cmd_hca_cap.regexp_mmo_qp);
	caps->regexp.sq_support = DEVX_GET(query_hca_cap_out, out,
					capability.cmd_hca_cap.regexp_mmo_sq);
	caps->regexp.log_sg_size = DEVX_GET(query_hca_cap_out, out,
					capability.cmd_hca_cap.log_regexp_scatter_gather_size);

	caps->compress.qp_support = DEVX_GET(query_hca_cap_out, out,
					capability.cmd_hca_cap.compress_mmo_qp);
	caps->compress.sq_support = DEVX_GET(query_hca_cap_out, out,
					capability.cmd_hca_cap.compress_mmo_sq);
	caps->compress.log_max_size = DEVX_GET(query_hca_cap_out, out,
					capability.cmd_hca_cap.log_compress_max_size);
	caps->compress.min_block_size = DEVX_GET(query_hca_cap_out, out,
					capability.cmd_hca_cap.compress_min_block_size);

	caps->decompress.qp_support = DEVX_GET(query_hca_cap_out, out,
					capability.cmd_hca_cap.decompress_mmo_qp);
	caps->decompress.sq_support = DEVX_GET(query_hca_cap_out, out,
					capability.cmd_hca_cap.decompress_mmo_sq);
	caps->decompress.log_max_size = DEVX_GET(query_hca_cap_out, out,
					capability.cmd_hca_cap.log_decompress_max_size);
	caps->decompress.snappy = DEVX_GET(query_hca_cap_out, out,
					capability.cmd_hca_cap.decompress_snappy);
	caps->decompress.lz4_data_only = DEVX_GET(query_hca_cap_out, out,
					capability.cmd_hca_cap.decompress_lz4_data_only);
	caps->decompress.lz4_no_checksum = DEVX_GET(query_hca_cap_out, out,
					capability.cmd_hca_cap.decompress_lz4_no_checksum);
	caps->decompress.lz4_checksum = DEVX_GET(query_hca_cap_out, out,
					capability.cmd_hca_cap.decompress_lz4_checksum);

	return 0;
}

int snap_dma_q_post_recv(struct snap_dma_q *q)
{
	int i;
	int rc;
	struct ibv_recv_wr rx_wr, *bad_wr;
	struct ibv_sge rx_sge;

	if (snap_qp_on_dpa(q->sw_qp.qp))
		return 0;

	for (i = 0; i < 2 * q->rx_qsize; i++) {
		if (q->sw_qp.mode == SNAP_DMA_Q_MODE_VERBS) {
			rx_sge.addr = (uint64_t)(q->sw_qp.rx_buf +
					i * q->rx_elem_size);
			rx_sge.length = q->rx_elem_size;
			rx_sge.lkey = q->sw_qp.rx_mr->lkey;

			rx_wr.wr_id = rx_sge.addr;
			rx_wr.next = NULL;
			rx_wr.sg_list = &rx_sge;
			rx_wr.num_sge = 1;

			rc = ibv_post_recv(snap_qp_to_verbs_qp(q->sw_qp.qp), &rx_wr, &bad_wr);
			if (rc)
				return rc;
		} else {
			snap_dv_post_recv(&q->sw_qp.dv_qp,
					  q->sw_qp.rx_buf + i * q->rx_elem_size,
					  q->rx_elem_size,
					  q->sw_qp.rx_mr->lkey);
			snap_dv_ring_rx_db(&q->sw_qp.dv_qp);
		}
	}

	return 0;
}

static int snap_qp_attr_helper(struct snap_dma_q *q, struct ibv_pd *pd,
		const struct snap_dma_q_create_attr *attr, struct snap_qp_attr *qp_init_attr)
{
	struct snap_mmo_caps mmo_caps = {{0}};
	int rc;

	switch (attr->mode) {
	case SNAP_DMA_Q_MODE_AUTOSELECT:
		rc = query_mmo_caps(pd->context, &mmo_caps);
		if (rc)
			return rc;

		if (mmo_caps.dma.qp_support)
			q->ops = &gga_ops;
		else
			q->ops = &dv_ops;
		break;
	case SNAP_DMA_Q_MODE_VERBS:
		q->ops = &verb_ops;
		break;
	case SNAP_DMA_Q_MODE_DV:
		q->ops = &dv_ops;
		break;
	case SNAP_DMA_Q_MODE_GGA:
		q->ops = &gga_ops;
		break;
	default:
		snap_error("Invalid SNAP_DMA_Q_OPMODE %d\n", attr->mode);
		return -EINVAL;
	}

	/* our ops point to the global struct, we need to clone it before
	 * making a private change
	 *
	 * it is better to have a dummy progress than another if statement
	 */
	if (attr->rx_qsize == 0) {
		if (clone_ops(q))
			return -EINVAL;
		q->custom_ops->progress_rx = dummy_progress;
	}

	if (attr->tx_qsize == 0) {
		if (clone_ops(q))
			return -EINVAL;
		q->custom_ops->progress_tx = dummy_progress;
	}

	snap_debug("Opening dma_q of type %d dpa_mode %d\n", attr->mode, attr->dpa_mode);
	if (attr->dpa_mode != SNAP_DMA_Q_DPA_MODE_NONE) {
		if (snap_dpa_enabled(pd->context)) {
			if (q->ops->mode != SNAP_DMA_Q_MODE_DV)
				return -EINVAL;
			q->no_events = true;
		} else
			return -ENOTSUP;
	}

	/*
	 * disable event mode if OBJ_DEVX are used to create qp and cq
	 * snap_dma_q_arm() will still work but event channel cannot be used
	 * to pick up events.
	 * At the moment this is only relevant for the unit tests
	 */
	if (attr->use_devx)
		q->no_events = true;

	/* make sure that the completion is requested at least once */
	if (q->ops->mode != SNAP_DMA_Q_MODE_VERBS
			&& attr->tx_qsize <= SNAP_DMA_Q_TX_MOD_COUNT && attr->tx_qsize > 0)
		q->tx_qsize = SNAP_DMA_Q_TX_MOD_COUNT + 8;
	else
		q->tx_qsize = attr->tx_qsize;

	q->rx_elem_size = attr->rx_elem_size;
	q->tx_elem_size = attr->tx_elem_size;

	qp_init_attr->sq_size = attr->tx_qsize;
	/* Need more space in rx queue in order to avoid memcpy() on rx data */
	qp_init_attr->rq_size = 2 * attr->rx_qsize;
	/* we must be able to send CQEs inline */
	qp_init_attr->sq_max_inline_size = attr->tx_elem_size;

	if (q->ops->mode == SNAP_DMA_Q_MODE_VERBS)
		qp_init_attr->sq_max_sge = SNAP_DMA_Q_MAX_SGE_NUM;
	else
		qp_init_attr->sq_max_sge = 1;
	qp_init_attr->rq_max_sge = 1;

	if (attr->wk) {
		const struct snap_dma_worker_queue *worker_q = container_of(q, struct snap_dma_worker_queue, q);

		qp_init_attr->rq_cq = attr->wk->rx_cq;
		qp_init_attr->sq_cq = attr->wk->tx_cq;
		/* TODO add worker support for DV, VERBS */
		qp_init_attr->qp_type = SNAP_OBJ_DEVX;
		qp_init_attr->uidx = worker_q->index;
		qp_init_attr->qp_on_dpa = false;
		q->no_events = true;
	}

	return 0;
}

static int snap_create_worker_qp_helper(struct ibv_pd *pd,
		struct snap_qp_attr *attr, struct snap_dma_ibv_qp *qp, int mode)
{
	int rc;

	qp->mode = mode;

	qp->qp = snap_qp_create(pd, attr);
	if (!qp->qp)
		return -EINVAL;

	rc = snap_qp_to_hw_qp(qp->qp, &qp->dv_qp.hw_qp);
	if (rc)
		goto free_qp;

	rc = posix_memalign((void **)&qp->dv_qp.comps, SNAP_DMA_BUF_ALIGN,
						qp->dv_qp.hw_qp.sq.wqe_cnt *
							sizeof(struct snap_dv_dma_completion));
	if (rc)
		goto free_qp;

	memset(qp->dv_qp.comps, 0,
			qp->dv_qp.hw_qp.sq.wqe_cnt * sizeof(struct snap_dv_dma_completion));

	if (!qp->dv_qp.hw_qp.sq.tx_db_nc) {
#if defined(__aarch64__)
		snap_error("DB record must be in the non-cacheable memory on BF\n");
		goto free_comps;
#else
		snap_warn("DB record is not in the non-cacheable memory. Performance may be reduced\n"
				"Try setting MLX5_SHUT_UP_BF environment variable\n");
#endif
	}

	/* TODO: gga on dpa */
	rc = posix_memalign(
		(void **)&qp->dv_qp.opaque_buf, sizeof(struct mlx5_dma_opaque),
		qp->dv_qp.hw_qp.sq.wqe_cnt * sizeof(struct mlx5_dma_opaque));
	if (rc)
		goto free_comps;

	qp->dv_qp.opaque_mr =
		ibv_reg_mr(pd, qp->dv_qp.opaque_buf,
					qp->dv_qp.hw_qp.sq.wqe_cnt * sizeof(struct mlx5_dma_opaque),
					IBV_ACCESS_LOCAL_WRITE);
	if (!qp->dv_qp.opaque_mr)
		goto free_opaque;

	qp->dv_qp.opaque_lkey = htobe32(qp->dv_qp.opaque_mr->lkey);
	return 0;

free_opaque:
	free(qp->dv_qp.opaque_buf);
free_comps:
	if (!attr->qp_on_dpa)
		free(qp->dv_qp.comps);
	else
		snap_dpa_mkey_free(qp->dpa.mkey);
free_qp:
	snap_qp_destroy(qp->qp);
	return -EINVAL;
}

static int snap_sw_qp_rx_wqe_helper(struct snap_dma_q *q, struct ibv_pd *pd,
								const struct snap_dma_q_create_attr *attr)
{
	int rc;
	bool destroy_cqs = true;

	q->tx_available = q->sw_qp.dv_qp.hw_qp.sq.wqe_cnt;

	if (q->ops->mode == SNAP_DMA_Q_MODE_DV || q->ops->mode == SNAP_DMA_Q_MODE_GGA)
		q->sw_qp.dv_qp.db_flag = (enum snap_db_ring_flag)snap_env_getenv(SNAP_DMA_Q_DBMODE);

	if (attr->dpa_mode)
		return 0;

	rc = snap_alloc_rx_wqes(pd, &q->sw_qp, 2 * attr->rx_qsize, attr->rx_elem_size);
	if (rc)
		goto free_qp;

	q->rx_qsize = attr->rx_qsize;

	return 0;

free_qp:
	if (q->worker)
		destroy_cqs = false;
	snap_destroy_qp_helper(&q->sw_qp, destroy_cqs);
	return rc;
}

static int snap_create_sw_qp(struct snap_dma_q *q, struct ibv_pd *pd,
		const struct snap_dma_q_create_attr *attr)
{
	struct snap_qp_attr qp_init_attr = {};
	int rc;

	rc = snap_qp_attr_helper(q, pd, attr, &qp_init_attr);
	if (rc)
		return rc;
	snap_error("\nlizh snap_create_sw_qp...wk %p", attr->wk);
	if (attr->wk)
		rc = snap_create_worker_qp_helper(pd, &qp_init_attr, &q->sw_qp,
				q->ops->mode);
	else
		rc = snap_create_qp_helper(pd, attr, &qp_init_attr, &q->sw_qp,
				q->ops->mode);
	snap_error("\nlizh snap_create_sw_qp...snap_create_qp_helper rc %d", rc);
	if (rc)
		return rc;

	rc = snap_sw_qp_rx_wqe_helper(q, pd, attr);
	snap_error("\nlizh snap_create_sw_qp...snap_sw_qp_rx_wqe_helper rc %d", rc);
	return rc;
}

static void snap_destroy_fw_qp(struct snap_dma_q *q)
{
	if (q->fw_qp.qp)
		snap_destroy_qp_helper(&q->fw_qp, true);
}

static int snap_create_fw_qp(struct snap_dma_q *q, struct ibv_pd *pd,
			     const struct snap_dma_q_create_attr *attr)
{
	struct snap_qp_attr qp_init_attr = {};
	struct snap_dma_q_create_attr fw_dma_q_attr;
	int rc;

	/* refactor fw qp creation code */
	memcpy(&fw_dma_q_attr, attr, sizeof(*attr));
	fw_dma_q_attr.mode = SNAP_DMA_Q_MODE_VERBS;
	fw_dma_q_attr.use_devx = false;
	fw_dma_q_attr.dpa_mode = SNAP_DMA_Q_DPA_MODE_NONE;
	fw_dma_q_attr.comp_channel = NULL;
	fw_dma_q_attr.comp_context = NULL;
	fw_dma_q_attr.comp_context = NULL;

	/* cannot create empty cq or a qp without one */
	qp_init_attr.sq_size = snap_max(attr->tx_qsize / 4, SNAP_DMA_FW_QP_MIN_SEND_WR);
	qp_init_attr.rq_size = 1;
	/* give one sge so that we can post which is useful for testing */
	qp_init_attr.sq_max_sge = 1;

	/* the qp 'resources' are going to be replaced by the fw. We do not
	 * need use DV or GGA here
	 **/
	rc = snap_create_qp_helper(pd, &fw_dma_q_attr, &qp_init_attr, &q->fw_qp, fw_dma_q_attr.mode);
	return rc;
}

static int snap_modify_lb_qp_init2init(struct snap_qp *qp)
{
	uint8_t in[DEVX_ST_SZ_BYTES(init2init_qp_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(init2init_qp_out)] = {0};
	void *qpc_ext;
	int ret;

	DEVX_SET(init2init_qp_in, in, opcode, MLX5_CMD_OP_INIT2INIT_QP);
	DEVX_SET(init2init_qp_in, in, qpn, snap_qp_get_qpnum(qp));

	DEVX_SET(rst2init_qp_in, in, opt_param_mask, 0);

	/* Set mmo parameter in qpc_ext */
	DEVX_SET(init2init_qp_in, in, qpc_ext, 1);
	DEVX_SET64(init2init_qp_in, in, opt_param_mask_95_32, 1ULL << 3);
	qpc_ext = DEVX_ADDR_OF(init2init_qp_in, in, qpc_data_extension);
	DEVX_SET(qpc_ext, qpc_ext, mmo, 1);

	ret = snap_qp_modify(qp, in, sizeof(in), out, sizeof(out));
	if (ret)
		snap_error("failed to modify qp to init with errno = %d\n", ret);

	return ret;
}

static int snap_modify_lb_qp_rst2init(struct snap_qp *qp,
				     struct ibv_qp_attr *qp_attr, int attr_mask)
{
	uint8_t in[DEVX_ST_SZ_BYTES(rst2init_qp_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(rst2init_qp_out)] = {0};
	void *qpc = DEVX_ADDR_OF(rst2init_qp_in, in, qpc);
	int ret;

	DEVX_SET(rst2init_qp_in, in, opcode, MLX5_CMD_OP_RST2INIT_QP);
	DEVX_SET(rst2init_qp_in, in, qpn, snap_qp_get_qpnum(qp));
	DEVX_SET(qpc, qpc, pm_state, MLX5_QP_PM_MIGRATED);

	if (attr_mask & IBV_QP_PKEY_INDEX)
		DEVX_SET(qpc, qpc, primary_address_path.pkey_index,
			 qp_attr->pkey_index);

	if (attr_mask & IBV_QP_PORT)
		DEVX_SET(qpc, qpc, primary_address_path.vhca_port_num,
			 qp_attr->port_num);

	if (attr_mask & IBV_QP_ACCESS_FLAGS) {
		if (qp_attr->qp_access_flags & IBV_ACCESS_REMOTE_READ)
			DEVX_SET(qpc, qpc, rre, 1);
		if (qp_attr->qp_access_flags & IBV_ACCESS_REMOTE_WRITE)
			DEVX_SET(qpc, qpc, rwe, 1);
	}

	ret = snap_qp_modify(qp, in, sizeof(in), out, sizeof(out));
	if (ret)
		snap_error("failed to modify qp to init with errno = %d\n", ret);
	return ret;
}

static int snap_modify_lb_qp_init2rtr(struct snap_qp *qp,
				    struct ibv_qp_attr *qp_attr, int attr_mask)
{
	uint8_t in[DEVX_ST_SZ_BYTES(init2rtr_qp_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(init2rtr_qp_out)] = {0};
	void *qpc = DEVX_ADDR_OF(init2rtr_qp_in, in, qpc);
	int ret;

	DEVX_SET(init2rtr_qp_in, in, opcode, MLX5_CMD_OP_INIT2RTR_QP);
	DEVX_SET(init2rtr_qp_in, in, qpn, snap_qp_get_qpnum(qp));

	/* 30 is the maximum value for Infiniband QPs*/
	DEVX_SET(qpc, qpc, log_msg_max, 30);

	/* TODO: add more attributes */
	if (attr_mask & IBV_QP_PATH_MTU)
		DEVX_SET(qpc, qpc, mtu, qp_attr->path_mtu);
	if (attr_mask & IBV_QP_DEST_QPN)
		DEVX_SET(qpc, qpc, remote_qpn, qp_attr->dest_qp_num);
	if (attr_mask & IBV_QP_RQ_PSN)
		DEVX_SET(qpc, qpc, next_rcv_psn, qp_attr->rq_psn & 0xffffff);
	if (attr_mask & IBV_QP_TIMEOUT)
		DEVX_SET(qpc, qpc, primary_address_path.ack_timeout,
			 qp_attr->timeout);
	if (attr_mask & IBV_QP_PKEY_INDEX)
		DEVX_SET(qpc, qpc, primary_address_path.pkey_index,
			 qp_attr->pkey_index);
	if (attr_mask & IBV_QP_PORT)
		DEVX_SET(qpc, qpc, primary_address_path.vhca_port_num,
			 qp_attr->port_num);
	if (attr_mask & IBV_QP_MAX_DEST_RD_ATOMIC)
		DEVX_SET(qpc, qpc, log_rra_max,
			 snap_u32log2(qp_attr->max_dest_rd_atomic));
	if (attr_mask & IBV_QP_MIN_RNR_TIMER)
		DEVX_SET(qpc, qpc, min_rnr_nak, qp_attr->min_rnr_timer);
	if (attr_mask & IBV_QP_AV)
		DEVX_SET(qpc, qpc, primary_address_path.fl, 1);

	ret = snap_qp_modify(qp, in, sizeof(in), out, sizeof(out));
	if (ret)
		snap_error("failed to modify qp to rtr with errno = %d\n", ret);
	return ret;
}

static int snap_modify_lb_qp_rtr2rts(struct snap_qp *qp,
				    struct ibv_qp_attr *qp_attr, int attr_mask)
{
	uint8_t in[DEVX_ST_SZ_BYTES(rtr2rts_qp_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(rtr2rts_qp_out)] = {0};
	void *qpc = DEVX_ADDR_OF(rtr2rts_qp_in, in, qpc);
	int ret;

	DEVX_SET(rtr2rts_qp_in, in, opcode, MLX5_CMD_OP_RTR2RTS_QP);
	DEVX_SET(rtr2rts_qp_in, in, qpn, snap_qp_get_qpnum(qp));

	if (attr_mask & IBV_QP_TIMEOUT)
		DEVX_SET(qpc, qpc, primary_address_path.ack_timeout,
			 qp_attr->timeout);
	if (attr_mask & IBV_QP_RETRY_CNT)
		DEVX_SET(qpc, qpc, retry_count, qp_attr->retry_cnt);
	if (attr_mask & IBV_QP_SQ_PSN)
		DEVX_SET(qpc, qpc, next_send_psn, qp_attr->sq_psn & 0xffffff);
	if (attr_mask & IBV_QP_RNR_RETRY)
		DEVX_SET(qpc, qpc, rnr_retry, qp_attr->rnr_retry);
	if (attr_mask & IBV_QP_MAX_QP_RD_ATOMIC)
		DEVX_SET(qpc, qpc, log_sra_max,
			 snap_u32log2(qp_attr->max_rd_atomic));

	ret = snap_qp_modify(qp, in, sizeof(in), out, sizeof(out));
	if (ret)
		snap_error("failed to modify qp to rts with errno = %d\n", ret);
	return ret;
}

#if !HAVE_DECL_IBV_QUERY_GID_EX
enum ibv_gid_type {
	IBV_GID_TYPE_IB,
	IBV_GID_TYPE_ROCE_V1,
	IBV_GID_TYPE_ROCE_V2,
};

struct ibv_gid_entry {
	union ibv_gid gid;
	uint32_t gid_index;
	uint32_t port_num;
	uint32_t gid_type; /* enum ibv_gid_type */
	uint32_t ndev_ifindex;
};

static int ibv_query_gid_ex(struct ibv_context *context, uint32_t port_num,
			    uint32_t gid_index, struct ibv_gid_entry *entry,
			    uint32_t flags)
{
	snap_error("%s is not implemented\n", __func__);
	return -1;
}
#endif

static int snap_activate_loop_2_qp(struct snap_dma_ibv_qp *qp1, struct snap_dma_ibv_qp *qp2,
				enum ibv_mtu mtu, bool force_loopback)
{
	struct ibv_qp_attr attr;
	int rc, flags_mask;

	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IBV_QPS_INIT;
	attr.pkey_index = SNAP_DMA_QP_PKEY_INDEX;
	attr.port_num = SNAP_DMA_QP_PORT_NUM;
	attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
	flags_mask = IBV_QP_STATE |
		     IBV_QP_PKEY_INDEX |
		     IBV_QP_PORT |
		     IBV_QP_ACCESS_FLAGS;

	rc = snap_modify_lb_qp_rst2init(qp1->qp, &attr, flags_mask);
	if (rc) {
		snap_error("failed to modify SW QP to INIT errno=%d\n", rc);
		return rc;
	} else if (qp1->mode == SNAP_DMA_Q_MODE_GGA) {
		rc = snap_modify_lb_qp_init2init(qp1->qp);
		if (rc) {
			snap_error("failed to modify SW QP in INIT2INIT errno=%d\n",
				   rc);
			return rc;
		}
	}

	rc = snap_modify_lb_qp_rst2init(qp2->qp, &attr, flags_mask);
	if (rc) {
		snap_error("failed to modify FW QP to INIT errno=%d\n", rc);
		return rc;
	}

	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IBV_QPS_RTR;
	attr.path_mtu = mtu;
	attr.rq_psn = SNAP_DMA_QP_RQ_PSN;
	attr.max_dest_rd_atomic = SNAP_DMA_QP_MAX_DEST_RD_ATOMIC;
	attr.min_rnr_timer = SNAP_DMA_QP_RNR_TIMER;
	attr.ah_attr.port_num = SNAP_DMA_QP_PORT_NUM;
	attr.ah_attr.grh.hop_limit = SNAP_DMA_QP_HOP_LIMIT;

	flags_mask = IBV_QP_STATE              |
		     IBV_QP_AV                 |
		     IBV_QP_PATH_MTU           |
		     IBV_QP_DEST_QPN           |
		     IBV_QP_RQ_PSN             |
		     IBV_QP_MAX_DEST_RD_ATOMIC |
		     IBV_QP_MIN_RNR_TIMER;

	attr.dest_qp_num = snap_qp_get_qpnum(qp2->qp);
	rc = snap_modify_lb_qp_init2rtr(qp1->qp, &attr, flags_mask);
	if (rc) {
		snap_error("failed to modify SW QP to RTR errno=%d\n", rc);
		return rc;
	}

	attr.dest_qp_num = snap_qp_get_qpnum(qp1->qp);
	rc = snap_modify_lb_qp_init2rtr(qp2->qp, &attr, flags_mask);
	if (rc) {
		snap_error("failed to modify FW QP to RTR errno=%d\n", rc);
		return rc;
	}

	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IBV_QPS_RTS;
	attr.timeout = SNAP_DMA_QP_TIMEOUT;
	attr.retry_cnt = SNAP_DMA_QP_RETRY_COUNT;
	attr.sq_psn = SNAP_DMA_QP_SQ_PSN;
	attr.rnr_retry = SNAP_DMA_QP_RNR_RETRY;
	attr.max_rd_atomic = SNAP_DMA_QP_MAX_RD_ATOMIC;
	flags_mask = IBV_QP_STATE              |
		     IBV_QP_TIMEOUT            |
		     IBV_QP_RETRY_CNT          |
		     IBV_QP_RNR_RETRY          |
		     IBV_QP_SQ_PSN             |
		     IBV_QP_MAX_QP_RD_ATOMIC;

	/* once QPs were moved to RTR using devx, they must also move to RTS
	 * using devx since kernel doesn't know QPs are on RTR state
	 **/
	rc = snap_modify_lb_qp_rtr2rts(qp1->qp, &attr, flags_mask);
	if (rc) {
		snap_error("failed to modify SW QP to RTS errno=%d\n", rc);
		return rc;
	}

	rc = snap_modify_lb_qp_rtr2rts(qp2->qp, &attr, flags_mask);
	if (rc) {
		snap_error("failed to modify FW QP to RTS errno=%d\n", rc);
		return rc;
	}

	return 0;
}

static int snap_activate_qp_to_remote_qpn(struct snap_dma_ibv_qp *qp1,
					  int remote_qpn, enum ibv_mtu mtu, bool force_loopback)
{
	struct ibv_qp_attr attr;
	int rc, flags_mask;

	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IBV_QPS_INIT;
	attr.pkey_index = SNAP_DMA_QP_PKEY_INDEX;
	attr.port_num = SNAP_DMA_QP_PORT_NUM;
	attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
	flags_mask = IBV_QP_STATE |
		IBV_QP_PKEY_INDEX |
		IBV_QP_PORT |
		IBV_QP_ACCESS_FLAGS;

	rc = snap_modify_lb_qp_rst2init(qp1->qp, &attr, flags_mask);
	if (rc) {
		snap_error("failed to modify SW QP to INIT errno=%d\n", rc);
		return rc;
	} else if (qp1->mode == SNAP_DMA_Q_MODE_GGA) {
		rc = snap_modify_lb_qp_init2init(qp1->qp);
		if (rc) {
			snap_error("failed to modify SW QP in INIT2INIT errno=%d\n",
					rc);
			return rc;
		}
	}

	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IBV_QPS_RTR;
	attr.path_mtu = mtu;
	attr.rq_psn = SNAP_DMA_QP_RQ_PSN;
	attr.max_dest_rd_atomic = SNAP_DMA_QP_MAX_DEST_RD_ATOMIC;
	attr.min_rnr_timer = SNAP_DMA_QP_RNR_TIMER;
	attr.ah_attr.port_num = SNAP_DMA_QP_PORT_NUM;
	attr.ah_attr.grh.hop_limit = SNAP_DMA_QP_HOP_LIMIT;
	attr.ah_attr.is_global = 1;

	attr.dest_qp_num = remote_qpn;
	flags_mask = IBV_QP_STATE              |
		IBV_QP_AV                 |
		IBV_QP_PATH_MTU           |
		IBV_QP_DEST_QPN           |
		IBV_QP_RQ_PSN             |
		IBV_QP_MAX_DEST_RD_ATOMIC |
		IBV_QP_MIN_RNR_TIMER;

	rc = snap_modify_lb_qp_init2rtr(qp1->qp, &attr, flags_mask);
	if (rc) {
		snap_error("failed to modify SW QP to RTR errno=%d\n", rc);
		return rc;
	}

	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IBV_QPS_RTS;
	attr.timeout = SNAP_DMA_QP_TIMEOUT;
	attr.retry_cnt = SNAP_DMA_QP_RETRY_COUNT;
	attr.sq_psn = SNAP_DMA_QP_SQ_PSN;
	attr.rnr_retry = SNAP_DMA_QP_RNR_RETRY;
	attr.max_rd_atomic = SNAP_DMA_QP_MAX_RD_ATOMIC;
	flags_mask = IBV_QP_STATE              |
		IBV_QP_TIMEOUT            |
		IBV_QP_RETRY_CNT          |
		IBV_QP_RNR_RETRY          |
		IBV_QP_SQ_PSN             |
		IBV_QP_MAX_QP_RD_ATOMIC;

	/* once QPs were moved to RTR using devx, they must also move to RTS
	 * using devx since kernel doesn't know QPs are on RTR state
	 */
	rc = snap_modify_lb_qp_rtr2rts(qp1->qp, &attr, flags_mask);
	if (rc) {
		snap_error("failed to modify SW QP to RTS errno=%d\n", rc);
		return rc;
	}

	return 0;
}

static int snap_dma_ep_connect_helper(struct snap_dma_ibv_qp *qp1,
		struct snap_dma_ibv_qp *qp2, struct ibv_pd *pd)
{
	int rc;
	bool roce_en = false, ib_en = false;
	enum ibv_mtu mtu = IBV_MTU_1024;
	bool force_loopback = false;
	struct snap_roce_caps roce_caps = {0};

	rc = check_port(pd->context, SNAP_DMA_QP_PORT_NUM, &roce_en,
			&ib_en, &mtu);
	if (rc)
		return rc;

	rc = fill_roce_caps(pd->context, &roce_caps);
	if (rc)
		return rc;

	/* Check if force-loopback is supported */
	if (ib_en || (roce_caps.resources_on_nvme_emulation_manager &&
	    ((roce_caps.roce_enabled && roce_caps.fl_when_roce_enabled) ||
	     (!roce_caps.roce_enabled && roce_caps.fl_when_roce_disabled)))) {
		force_loopback = true;
	} else {
		snap_error("Force-loopback QP is not supported. Cannot create queue.\n");
		return -ENOTSUP;
	}

	return snap_activate_loop_2_qp(qp1, qp2, mtu, force_loopback);
}

static int snap_dma_ep_connect_qpn_helper(struct snap_dma_ibv_qp *qp1,
					  int remote_qpn, struct ibv_pd *pd)
{
	struct snap_roce_caps roce_caps = {0};
	bool roce_en = false, ib_en = false;
	enum ibv_mtu mtu = IBV_MTU_1024;
	bool force_loopback = false;
	int rc;

	rc = check_port(pd->context, SNAP_DMA_QP_PORT_NUM, &roce_en,
			&ib_en, &mtu);

	if (rc)
		return rc;

	if (ib_en) {
		snap_error("IB mode not supported. Cannot create queue\n");
		return -ENOTSUP;
	}

	rc = fill_roce_caps(pd->context, &roce_caps);
	if (rc)
		return rc;

	/* Check if force-loopback is supported based on roce caps */
	if (roce_caps.resources_on_nvme_emulation_manager &&
			((roce_caps.roce_enabled && roce_caps.fl_when_roce_enabled) ||
			 (!roce_caps.roce_enabled && roce_caps.fl_when_roce_disabled))) {
		force_loopback = true;
	} else {
		snap_error("Force-loopback option is not supported. Cannot create queue\n");
		return -ENOTSUP;
	}

	return snap_activate_qp_to_remote_qpn(qp1, remote_qpn, mtu, force_loopback);
}

static int snap_alloc_iov_ctx(struct snap_dma_q *q)
{
	int i, ret, io_ctx_cnt;
	struct snap_dma_q_iov_ctx *iov_ctx;

	io_ctx_cnt = q->sw_qp.dv_qp.hw_qp.sq.wqe_cnt;

	ret = posix_memalign((void **)&iov_ctx, SNAP_DMA_BUF_ALIGN,
			io_ctx_cnt * sizeof(struct snap_dma_q_iov_ctx));
	if (ret) {
		snap_error("alloc dma_q iov_ctx array failed");
		return -ENOMEM;
	}

	memset(iov_ctx, 0, io_ctx_cnt * sizeof(struct snap_dma_q_iov_ctx));

	TAILQ_INIT(&q->free_iov_ctx);

	for (i = 0; i < io_ctx_cnt; i++) {
		iov_ctx[i].q = q;
		TAILQ_INSERT_TAIL(&q->free_iov_ctx, &iov_ctx[i], entry);
	}

	q->iov_ctx = iov_ctx;

	return 0;
}

static void snap_free_iov_ctx(struct snap_dma_q *q)
{
	int i, io_ctx_cnt;
	struct snap_dma_q_iov_ctx *iov_ctx = q->iov_ctx;

	io_ctx_cnt = q->sw_qp.dv_qp.hw_qp.sq.wqe_cnt;

	for (i = 0; i < io_ctx_cnt; i++)
		TAILQ_REMOVE(&q->free_iov_ctx, &iov_ctx[i], entry);

	free(iov_ctx);
	q->iov_ctx = NULL;
}

static int snap_alloc_crypto_ctx(struct snap_dma_q *q, struct ibv_pd *pd)
{
	int i, ret, io_ctx_cnt;
	struct snap_dma_q_crypto_ctx *crypto_ctx;
	struct mlx5_devx_mkey_attr mkey_attr = {};
	struct snap_relaxed_ordering_caps caps = {};

	io_ctx_cnt = q->sw_qp.dv_qp.hw_qp.sq.wqe_cnt;

	ret = posix_memalign((void **)&crypto_ctx, SNAP_DMA_BUF_ALIGN,
			io_ctx_cnt * sizeof(struct snap_dma_q_crypto_ctx));
	if (ret) {
		snap_error("alloc dma_q crypto_ctx array failed");
		return -ENOMEM;
	}

	ret = snap_query_relaxed_ordering_caps(pd->context, &caps);
	if (ret) {
		snap_error("query relaxed_ordering_caps failed, ret:%d\n", ret);
		goto free_crypto_ctx;
	}

	memset(crypto_ctx, 0, io_ctx_cnt * sizeof(struct snap_dma_q_crypto_ctx));

	TAILQ_INIT(&q->free_crypto_ctx);

	mkey_attr.addr = 0;
	mkey_attr.size = 0;
	mkey_attr.log_entity_size = 0;
	mkey_attr.relaxed_ordering_write = caps.relaxed_ordering_write;
	mkey_attr.relaxed_ordering_read = caps.relaxed_ordering_read;
	mkey_attr.klm_num = 0;
	mkey_attr.klm_array = NULL;
	mkey_attr.crypto_en = true;
	mkey_attr.bsf_en = true;

	for (i = 0; i < io_ctx_cnt; i++) {
		crypto_ctx[i].r_klm_mkey = snap_create_indirect_mkey(pd, &mkey_attr);
		if (!crypto_ctx[i].r_klm_mkey) {
			snap_error("create remote klm mkey for crypto_ctx[%d] failed\n", i);
			goto destroy_mkeys;
		}

		crypto_ctx[i].l_klm_mkey = snap_create_indirect_mkey(pd, &mkey_attr);
		if (!crypto_ctx[i].l_klm_mkey) {
			snap_error("create local klm mkey for crypto_ctx[%d] failed\n", i);
			snap_destroy_indirect_mkey(crypto_ctx[i].r_klm_mkey);
			goto destroy_mkeys;
		}
	}

	for (i = 0; i < io_ctx_cnt; i++) {
		crypto_ctx[i].q = q;
		TAILQ_INSERT_TAIL(&q->free_crypto_ctx, &crypto_ctx[i], entry);
	}

	q->crypto_ctx = crypto_ctx;

	return 0;

destroy_mkeys:
	for (i--; i >= 0; i--) {
		snap_destroy_indirect_mkey(crypto_ctx[i].l_klm_mkey);
		snap_destroy_indirect_mkey(crypto_ctx[i].r_klm_mkey);
	}

free_crypto_ctx:
	free(crypto_ctx);

	return 1;
}

static void snap_free_crypto_ctx(struct snap_dma_q *q)
{
	int i, io_ctx_cnt;
	struct snap_dma_q_crypto_ctx *crypto_ctx = q->crypto_ctx;

	io_ctx_cnt = q->sw_qp.dv_qp.hw_qp.sq.wqe_cnt;

	for (i = 0; i < io_ctx_cnt; i++) {
		TAILQ_REMOVE(&q->free_crypto_ctx, &crypto_ctx[i], entry);
		snap_destroy_indirect_mkey(crypto_ctx[i].l_klm_mkey);
		snap_destroy_indirect_mkey(crypto_ctx[i].r_klm_mkey);
	}

	free(crypto_ctx);
	q->crypto_ctx = NULL;
}

static int snap_alloc_ir_ctx(struct snap_dma_q *q, struct ibv_pd *pd)
{
	int i, ret, io_ctx_cnt;
	struct snap_dma_q_ir_ctx *ir_ctx;

	io_ctx_cnt = q->sw_qp.dv_qp.hw_qp.sq.wqe_cnt;

	ret = posix_memalign((void **)&ir_ctx, SNAP_DMA_BUF_ALIGN,
			io_ctx_cnt * sizeof(struct snap_dma_q_ir_ctx));
	if (ret) {
		snap_error("alloc dma_q ir_ctx array failed");
		return -ENOMEM;
	}

	q->ir_ctx = ir_ctx;

	ret = posix_memalign((void **)&q->ir_buf, SNAP_DMA_BUF_ALIGN,
			io_ctx_cnt * 32);
	if (ret) {
		snap_error("alloc dma_q inline recv buffer failed");
		goto free_ir_ctx;
	}

	q->ir_mr = ibv_reg_mr(pd, q->ir_buf, io_ctx_cnt * 32,
					IBV_ACCESS_LOCAL_WRITE |
					IBV_ACCESS_REMOTE_WRITE |
					IBV_ACCESS_REMOTE_READ);
	if (!q->ir_mr) {
		snap_error("register ir_mr failed");
		ret = -errno;
		goto free_ir_buf;
	}

	memset(ir_ctx, 0, io_ctx_cnt * sizeof(struct snap_dma_q_ir_ctx));
	TAILQ_INIT(&q->free_ir_ctx);
	memset(q->ir_buf, 0, io_ctx_cnt * 32);

	for (i = 0; i < io_ctx_cnt; i++) {
		ir_ctx[i].q = q;
		ir_ctx[i].buf = (char *)q->ir_buf + i * 32;
		ir_ctx[i].mkey = q->ir_mr->lkey;

		TAILQ_INSERT_TAIL(&q->free_ir_ctx, &ir_ctx[i], entry);
	}

	return 0;

free_ir_buf:
	free(q->ir_buf);

free_ir_ctx:
	free(q->ir_ctx);

	return ret;
}

static void snap_free_ir_ctx(struct snap_dma_q *q)
{
	int i, io_ctx_cnt;
	struct snap_dma_q_ir_ctx *ir_ctx = q->ir_ctx;

	io_ctx_cnt = q->sw_qp.dv_qp.hw_qp.sq.wqe_cnt;

	for (i = 0; i < io_ctx_cnt; i++)
		TAILQ_REMOVE(&q->free_ir_ctx, &ir_ctx[i], entry);

	ibv_dereg_mr(q->ir_mr);
	free(q->ir_buf);
	free(ir_ctx);
	q->ir_ctx = NULL;
}

static int snap_create_io_ctx(struct snap_dma_q *q, struct ibv_pd *pd,
			const struct snap_dma_q_create_attr *attr)
{
	int ret;

	q->iov_support = false;
	q->crypto_support = false;

	if (attr->iov_enable) {
		/* DV/GGA mode donot need iov_ctx */
		if (q->sw_qp.mode == SNAP_DMA_Q_MODE_VERBS) {
			ret = snap_alloc_iov_ctx(q);
			if (ret) {
				snap_error("Allocate iov_ctx failed\n");
				goto out;
			}
		}
		q->iov_support = true;
	}

	if (attr->crypto_enable && q->sw_qp.mode == SNAP_DMA_Q_MODE_DV) {
		ret = snap_alloc_crypto_ctx(q, pd);
		if (ret) {
			snap_error("Allocate crypto_ctx failed\n");
			goto free_iov_ctx;
		}

		q->crypto_support = true;
	}

	if (q->sw_qp.mode == SNAP_DMA_Q_MODE_VERBS) {
		ret = snap_alloc_ir_ctx(q, pd);
		if (ret) {
			snap_error("Allocate ir_ctx failed\n");
			goto free_iov_ctx;
		}
	}

	return 0;

free_iov_ctx:
	if (attr->iov_enable) {
		if (q->sw_qp.mode == SNAP_DMA_Q_MODE_VERBS)
			snap_free_iov_ctx(q);
		q->iov_support = false;
	}

out:
	return 1;
}

static void snap_destroy_io_ctx(struct snap_dma_q *q)
{
	if (q->iov_support) {
		if (q->sw_qp.mode == SNAP_DMA_Q_MODE_VERBS)
			snap_free_iov_ctx(q);
		q->iov_support = false;
	}

	if (q->crypto_support) {
		snap_free_crypto_ctx(q);
		q->crypto_support = false;
	}

	if (q->sw_qp.mode == SNAP_DMA_Q_MODE_VERBS)
		snap_free_ir_ctx(q);
}

/**
 * snap_dma_ep_connect() - Connect 2 Qps
 * @q1:  first queue to connect
 * @q2:  second queue to connect
 *
 * connect software qps of 2 separate snap_dma_q's
 *
 * Return: 0 on success, -errno on failure.
 */
int snap_dma_ep_connect(struct snap_dma_q *q1, struct snap_dma_q *q2)
{
	int ret;
	struct ibv_pd *pd;

	if (!q1 || !q2)
		return -1;

	pd = snap_qp_get_pd(q1->sw_qp.qp);
	if (!pd)
		return -1;

	ret = snap_dma_ep_connect_helper(&q1->sw_qp, &q2->sw_qp, pd);
	if (ret)
		return ret;

	/* In general one must post recvs before qp is moved to the RTR.
	 * However in our case we control both sides and there is no traffic
	 * until this function completes.
	 */
	ret = snap_dma_q_post_recv(q1);
	if (ret)
		return ret;

	ret = snap_dma_q_post_recv(q2);
	if (ret)
		return ret;

	return 0;
}

/**
 * snap_dma_ep_connect_remote_qpn() - Connect 2 Qps
 * @q1:  first queue
 * @q2:  qpn of remote queue
 *
 * connect software qps of 2 separate snap_dma_q's
 *
 * Return: 0 on success, -errno on failure.
 */
int snap_dma_ep_connect_remote_qpn(struct snap_dma_q *q1, int remote_qpn)
{
	struct ibv_pd *pd;
	int ret;

	if (!q1 || !remote_qpn)
		return -1;

	pd = snap_qp_get_pd(q1->sw_qp.qp);
	if (!pd)
		return -1;

	ret = snap_dma_ep_connect_qpn_helper(&q1->sw_qp, remote_qpn, pd);
	if (ret)
		return ret;

	return 0;
}

static void snap_dma_worker_init_queues(struct snap_dma_worker *wk, size_t exp_queue_num)
{
	int i;

	wk->max_queues = exp_queue_num;
	SLIST_INIT(&wk->free_queues);
	for (i = exp_queue_num - 1; i >= 0; i--) {
		wk->queues[i].index = i;
		SLIST_INSERT_HEAD(&wk->free_queues, &wk->queues[i], entry);
	}
}

static struct snap_dma_q *snap_dma_worker_queue_get(struct snap_dma_worker *wk)
{
	struct snap_dma_worker_queue *queue = SLIST_FIRST(&wk->free_queues);

	if (!queue)
		return NULL;

	SLIST_REMOVE_HEAD(&wk->free_queues, entry);
	queue->in_use = true;
	return &queue->q;
}

static void snap_dma_worker_queue_put(struct snap_dma_q *q)
{
	struct snap_dma_worker_queue *queue = container_of(q, struct snap_dma_worker_queue, q);

	queue->in_use = false;
	SLIST_INSERT_HEAD(&q->worker->free_queues, queue, entry);
	q->worker = NULL;
}

/**
 * snap_dma_ep_create() - Create sw only DMA queue
 * @pd:    protection domain to create qp
 * @attr:  dma queue creation attributes
 *
 * The function creates only a sw qp.
 * The use is to create 2 separate sw only snap_dma_q's and connect them
 *
 * If the endpoint is created on DPA, dpa_dma_ep_init() must be called by a
 * DPA thread to complete initialization.
 *
 * Return: dma queue or NULL on error.
 */
struct snap_dma_q *snap_dma_ep_create(struct ibv_pd *pd,
		const struct snap_dma_q_create_attr *attr)
{
	int rc;
	struct snap_dma_q *q;

	snap_error("\nlizh snap_dma_ep_create...pd %p", pd);
	if (!pd)
		return NULL;

	if (!attr->rx_cb)
		return NULL;

	if (!attr->wk)
		q = calloc(1, sizeof(*q));
	else
		q = snap_dma_worker_queue_get(attr->wk);
	if (!q)
		return NULL;
	snap_error("\nlizh snap_dma_ep_create...q done");
	q->worker = attr->wk;
	rc = snap_create_sw_qp(q, pd, attr);
	if (rc)
		goto free_q;
	snap_error("\nlizh snap_dma_ep_create...snap_create_sw_qp done");
	q->uctx = attr->uctx;
	q->rx_cb = attr->rx_cb;
	return q;

free_q:
	if (!attr->wk)
		free(q);
	else
		snap_dma_worker_queue_put(q);
	return NULL;
}

/**
 * snap_dma_q_create() - Create DMA queue
 * @pd:    protection domain to create qps
 * @attr:  dma queue creation attributes
 *
 * Create and connect both software and fw qps
 *
 * The function creates a pair of QPs and connects them.
 * snap_dma_q_get_fw_qpnum() should be used to obtain qp number that
 * can be given to firmware emulation objects.
 *
 * Note that on Blufield1 extra steps are required:
 *  - an on behalf QP with the same number as
 *    returned by the snap_dma_q_get_fw_qpnum() must be created
 *  - a fw qp state must be copied to the on behalf qp
 *  - steering rules must be set
 *
 * All these steps must be done by the application.
 *
 * Return: dma queue or NULL on error.
 */
struct snap_dma_q *snap_dma_q_create(struct ibv_pd *pd,
		const struct snap_dma_q_create_attr *attr)
{
	struct snap_dma_q *q;
	int rc;

	q = snap_dma_ep_create(pd, attr);
	if (!q)
		return NULL;
	snap_error("lizh snap_dma_q_create...snap_dma_ep_create done");
	rc = snap_create_fw_qp(q, pd, attr);
	if (rc)
		goto free_sw_qp;
	snap_error("lizh snap_dma_q_create...snap_create_fw_qp done");
	rc = snap_dma_ep_connect_helper(&q->sw_qp, &q->fw_qp, pd);
	if (rc)
		goto free_fw_qp;
	snap_error("lizh snap_dma_q_create...snap_dma_ep_connect_helper done");
	/* In general one must post recvs before qp is moved to the RTR.
	 * However in our case we control both sides and there is no traffic
	 * until fw qp is passed to the FW
	 */
	rc = snap_dma_q_post_recv(q);
	if (rc)
		goto free_fw_qp;
	snap_error("lizh snap_dma_q_create...snap_dma_q_post_recv done");

	rc = snap_create_io_ctx(q, pd, attr);
	if (rc)
		goto free_fw_qp;
	snap_error("lizh snap_dma_q_create...snap_create_io_ctx done");

	return q;

free_fw_qp:
	snap_destroy_fw_qp(q);
free_sw_qp:
	snap_dma_ep_destroy(q);
	return NULL;
}

/**
 * snap_dma_ep_destroy() - Destroy DMA ep queue
 *
 * @q: dma queue
 */
void snap_dma_ep_destroy(struct snap_dma_q *q)
{
	snap_destroy_sw_qp(q);
	if (q->worker)
		snap_dma_worker_queue_put(q);
	else
		free(q);
}

/**
 * snap_dma_q_destroy() - Destroy DMA queue
 *
 * @q: dma queue
 */
void snap_dma_q_destroy(struct snap_dma_q *q)
{
	snap_destroy_io_ctx(q);
	snap_destroy_fw_qp(q);
	snap_dma_ep_destroy(q);
}

SNAP_STATIC_ASSERT(sizeof(struct snap_dma_q) < SNAP_DMA_THREAD_MBOX_CMD_SIZE,
		"struct snap_dma_q is too big for the DPA thread mbox");
/**
 * snap_dma_ep_dpa_copy_sync() - Copy DMA endpoint to DPA
 * @thr:  thread to copy endpoint to
 * @q:    dma endpoint
 *
 * The function copies DMA endpoint to the DPA thread. The copy is done
 * synchronously. After the function return endpoint is initialized by the
 * DPA thread and is ready to use.
 *
 * Return:
 * 0 on success or -errno
 */
int snap_dma_ep_dpa_copy_sync(struct snap_dpa_thread *thr, struct snap_dma_q *q)
{
	int ret = 0;
	struct snap_dma_ep_copy_cmd *cmd;
	struct snap_dpa_rsp *rsp;
	void *mbox;

	mbox = snap_dpa_thread_mbox_acquire(thr);

	cmd = (struct snap_dma_ep_copy_cmd *)snap_dpa_mbox_to_cmd(mbox);
	if (snap_unlikely(!cmd)) {
		ret = -EINVAL;
		goto out;
	}
	memcpy(&cmd->q, q, sizeof(*q));
	snap_dpa_cmd_send(thr, &cmd->base, SNAP_DPA_CMD_DMA_EP_COPY);

	rsp = snap_dpa_rsp_wait(mbox);
	if (rsp->status != SNAP_DPA_RSP_OK) {
		snap_error("Failed to copy DMA queue: %d\n", rsp->status);
		ret = -EINVAL;
	}

out:
	snap_dpa_thread_mbox_release(thr);
	return ret;
}

static int snap_create_worker_cqs_helper(struct snap_dma_worker *wk,
										struct ibv_pd *pd,
										struct snap_cq_attr *cq_attr)
{
	int rc;

	wk->tx_cq = snap_cq_create(pd->context, cq_attr);
	if (!wk->tx_cq)
		return -EINVAL;

	/* Use 128 bytes cqes in order to allow scatter to cqe on receive
	 * This is relevant for NVMe sqe and for virtio queues when number of
	 * tunneled descr is less then three.
	 */
	cq_attr->cqe_size = SNAP_DMA_Q_RX_CQE_SIZE;
	wk->rx_cq = snap_cq_create(pd->context, cq_attr);
	if (!wk->rx_cq)
		goto free_tx_cq;

	rc = snap_cq_to_hw_cq(wk->tx_cq, &wk->dv_tx_cq);
	if (rc)
		goto free_rx_cq;

	rc = snap_cq_to_hw_cq(wk->rx_cq, &wk->dv_rx_cq);
	if (rc)
		goto free_rx_cq;

	return 0;

free_rx_cq:
	snap_cq_destroy(wk->rx_cq);
free_tx_cq:
	snap_cq_destroy(wk->tx_cq);
	return -EINVAL;
}

struct snap_dma_worker *snap_dma_worker_create(struct ibv_pd *pd,
	const struct snap_dma_worker_create_attr *attr)
{
	struct snap_dma_worker *wk = calloc(1, sizeof(*wk) + attr->exp_queue_num * sizeof(struct snap_dma_worker_queue));
	struct snap_cq_attr cq_attr = {
		.cq_context = NULL,
		.comp_channel = NULL,
		.comp_vector = 0,
		.cqe_cnt = attr->exp_queue_num * attr->exp_queue_rx_size,
		.cqe_size = SNAP_DMA_Q_TX_CQE_SIZE,
		.cq_type = SNAP_OBJ_DEVX
	};

	if (!wk)
		return NULL;

	snap_create_worker_cqs_helper(wk, pd, &cq_attr);

	snap_dma_worker_init_queues(wk, attr->exp_queue_num);
	return wk;
}

void snap_dma_worker_destroy(struct snap_dma_worker *wk)
{
	if (!wk)
		return;

	snap_cq_destroy(wk->rx_cq);
	snap_cq_destroy(wk->tx_cq);
	free(wk);
}

int snap_dma_worker_flush(struct snap_dma_worker *wk)
{
	return dv_worker_flush(wk);
}

int snap_dma_worker_progress_rx(struct snap_dma_worker *wk)
{
	return dv_worker_progress_rx(wk);
}

int snap_dma_worker_progress_tx(struct snap_dma_worker *wk)
{
	return dv_worker_progress_tx(wk);
}
