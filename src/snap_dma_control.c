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
#include "snap_internal.h"
#include "snap_umr.h"

#include "config.h"

SNAP_ENV_REG_ENV_VARIABLE(SNAP_DMA_Q_OPMODE, 0);
SNAP_ENV_REG_ENV_VARIABLE(SNAP_DMA_Q_IOV_SUPP, 0);
SNAP_ENV_REG_ENV_VARIABLE(SNAP_DMA_Q_DBMODE, 0);

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
		      bool *ib_en, uint16_t *lid, enum ibv_mtu *mtu)
{
	uint8_t in[DEVX_ST_SZ_BYTES(query_nic_vport_context_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(query_nic_vport_context_out)] = {0};
	uint8_t devx_v;
	struct ibv_port_attr port_attr;
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
		*lid = port_attr.lid;
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

static void snap_destroy_qp_helper(struct snap_dma_ibv_qp *qp)
{
	if (qp->dv_qp.comps)
		free(qp->dv_qp.comps);

	if (qp->dv_qp.opaque_buf) {
		ibv_dereg_mr(qp->dv_qp.opaque_mr);
		free(qp->dv_qp.opaque_buf);
	}

	snap_qp_destroy(qp->qp);
	snap_cq_destroy(qp->rx_cq);
	snap_cq_destroy(qp->tx_cq);
}

static void snap_free_rx_wqes(struct snap_dma_ibv_qp *qp)
{
	ibv_dereg_mr(qp->rx_mr);
	free(qp->rx_buf);
}

static int snap_alloc_rx_wqes(struct ibv_pd *pd, struct snap_dma_ibv_qp *qp, int rx_qsize,
		int rx_elem_size)
{
	int rc;

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

static int snap_create_qp_helper(struct ibv_pd *pd, void *cq_context,
		struct ibv_comp_channel *comp_channel, int comp_vector,
		struct snap_qp_attr *attr, struct snap_dma_ibv_qp *qp,
		int mode)
{
	struct snap_cq_attr cq_attr = {
		.cq_context = cq_context,
		.comp_channel = comp_channel,
		.comp_vector = comp_vector,
		.cqe_cnt = attr->sq_size,
		.cqe_size = SNAP_DMA_Q_TX_CQE_SIZE
	};
	int rc;

	qp->mode = mode;

	/* TODO: add attribute to choose how snap_qp/cq are created */
	if (mode == SNAP_DMA_Q_MODE_VERBS)
		cq_attr.cq_type = SNAP_OBJ_VERBS;
	else
		/* SNAP_OBJ_DEVX is also supported - enable manually */
		cq_attr.cq_type = SNAP_OBJ_DV;

	qp->tx_cq = snap_cq_create(pd->context, &cq_attr);
	if (!qp->tx_cq)
		return -EINVAL;

	cq_attr.cqe_cnt = attr->rq_size;
	/* Use 128 bytes cqes in order to allow scatter to cqe on receive
	 * This is relevant for NVMe sqe and for virtio queues when number of
	 * tunneled descr is less then three.
	 */
	cq_attr.cqe_size = SNAP_DMA_Q_RX_CQE_SIZE;
	qp->rx_cq = snap_cq_create(pd->context, &cq_attr);
	if (!qp->rx_cq)
		goto free_tx_cq;

	attr->qp_type = cq_attr.cq_type;
	attr->sq_cq = qp->tx_cq;
	attr->rq_cq = qp->rx_cq;

	qp->qp = snap_qp_create(pd, attr);
	if (!qp->qp)
		goto free_rx_cq;

	if (mode == SNAP_DMA_Q_MODE_VERBS)
		return 0;

	rc = snap_qp_to_hw_qp(qp->qp, &qp->dv_qp.hw_qp);
	if (rc)
		goto free_comps;

	rc = posix_memalign((void **)&qp->dv_qp.comps, SNAP_DMA_BUF_ALIGN,
			    qp->dv_qp.hw_qp.sq.wqe_cnt * sizeof(struct snap_dv_dma_completion));
	if (rc)
		goto free_rx_cq;

	memset(qp->dv_qp.comps, 0,
	       qp->dv_qp.hw_qp.sq.wqe_cnt * sizeof(struct snap_dv_dma_completion));

	rc = snap_cq_to_hw_cq(qp->tx_cq, &qp->dv_tx_cq);
	if (rc)
		goto free_comps;

	rc = snap_cq_to_hw_cq(qp->rx_cq, &qp->dv_rx_cq);
	if (rc)
		goto free_comps;

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

	rc = posix_memalign((void **)&qp->dv_qp.opaque_buf,
			    sizeof(struct mlx5_dma_opaque),
			    qp->dv_qp.hw_qp.sq.wqe_cnt * sizeof(struct mlx5_dma_opaque));
	if (rc)
		goto free_comps;

	qp->dv_qp.opaque_mr = ibv_reg_mr(pd, qp->dv_qp.opaque_buf,
					 qp->dv_qp.hw_qp.sq.wqe_cnt * sizeof(struct mlx5_dma_opaque),
					 IBV_ACCESS_LOCAL_WRITE);
	if (!qp->dv_qp.opaque_mr)
		goto free_opaque;

	qp->dv_qp.opaque_lkey = htobe32(qp->dv_qp.opaque_mr->lkey);
	return 0;

free_opaque:
	free(qp->dv_qp.opaque_buf);

	return 0;

free_comps:
	free(qp->dv_qp.comps);
free_rx_cq:
	snap_cq_destroy(qp->rx_cq);
free_tx_cq:
	snap_cq_destroy(qp->tx_cq);
	return -EINVAL;
}

static void snap_destroy_sw_qp(struct snap_dma_q *q)
{
	snap_free_rx_wqes(&q->sw_qp);
	snap_destroy_qp_helper(&q->sw_qp);
}

static int snap_create_sw_qp(struct snap_dma_q *q, struct ibv_pd *pd,
		struct snap_dma_q_create_attr *attr)
{
	struct snap_qp_attr qp_init_attr = {};
	struct ibv_recv_wr rx_wr, *bad_wr;
	struct ibv_sge rx_sge;
	struct snap_mmo_caps mmo_caps = {{0}};
	int i, rc;

	switch (attr->mode) {
	case SNAP_DMA_Q_MODE_AUTOSELECT:
		rc = snap_query_mmo_caps(pd->context, &mmo_caps);
		if (rc)
			return rc;

		if (mmo_caps.dma.qp_support) {
			attr->mode = SNAP_DMA_Q_MODE_GGA;
			q->ops = &gga_ops;
		} else {
			attr->mode = SNAP_DMA_Q_MODE_DV;
			q->ops = &dv_ops;
		}
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
	snap_debug("Opening dma_q of type %d\n", attr->mode);
	/*
	 * TODO: disable event mode if OBJ_DEVX are used to create qp and cq
	 * q->no_events = true;
	 */

	/* make sure that the completion is requested at least once */
	if (attr->mode != SNAP_DMA_Q_MODE_VERBS &&
	    attr->tx_qsize <= SNAP_DMA_Q_TX_MOD_COUNT)
		q->tx_qsize = SNAP_DMA_Q_TX_MOD_COUNT + 8;
	else
		q->tx_qsize = attr->tx_qsize;

	q->tx_available = q->tx_qsize;
	q->rx_elem_size = attr->rx_elem_size;
	q->tx_elem_size = attr->tx_elem_size;

	qp_init_attr.sq_size = attr->tx_qsize;
	/* Need more space in rx queue in order to avoid memcpy() on rx data */
	qp_init_attr.rq_size = 2 * attr->rx_qsize;
	/* we must be able to send CQEs inline */
	qp_init_attr.sq_max_inline_size = attr->tx_elem_size;

	qp_init_attr.sq_max_sge = 1;
	qp_init_attr.sq_max_sge = 1;

	rc = snap_create_qp_helper(pd, attr->comp_context, attr->comp_channel,
			attr->comp_vector, &qp_init_attr, &q->sw_qp, attr->mode);
	if (rc)
		return rc;

	if (attr->mode == SNAP_DMA_Q_MODE_DV || attr->mode == SNAP_DMA_Q_MODE_GGA) {
		q->tx_available = q->sw_qp.dv_qp.hw_qp.sq.wqe_cnt;
		q->sw_qp.dv_qp.db_flag = snap_env_getenv(SNAP_DMA_Q_DBMODE);
	}

	rc = snap_alloc_rx_wqes(pd, &q->sw_qp, 2 * attr->rx_qsize, attr->rx_elem_size);
	if (rc)
		goto free_qp;

	for (i = 0; i < 2 * attr->rx_qsize; i++) {
		if (attr->mode == SNAP_DMA_Q_MODE_VERBS) {
			rx_sge.addr = (uint64_t)(q->sw_qp.rx_buf +
					i * attr->rx_elem_size);
			rx_sge.length = attr->rx_elem_size;
			rx_sge.lkey = q->sw_qp.rx_mr->lkey;

			rx_wr.wr_id = rx_sge.addr;
			rx_wr.next = NULL;
			rx_wr.sg_list = &rx_sge;
			rx_wr.num_sge = 1;

			rc = ibv_post_recv(snap_qp_to_verbs_qp(q->sw_qp.qp), &rx_wr, &bad_wr);
			if (rc)
				goto free_rx_resources;
		} else {
			snap_dv_post_recv(&q->sw_qp.dv_qp,
					  q->sw_qp.rx_buf + i * attr->rx_elem_size,
					  attr->rx_elem_size,
					  q->sw_qp.rx_mr->lkey);
			snap_dv_ring_rx_db(&q->sw_qp.dv_qp);
		}
	}

	return 0;

free_rx_resources:
	snap_free_rx_wqes(&q->sw_qp);
free_qp:
	snap_destroy_qp_helper(&q->sw_qp);
	return rc;
}

static void snap_destroy_fw_qp(struct snap_dma_q *q)
{
	snap_destroy_qp_helper(&q->fw_qp);
}

static int snap_create_fw_qp(struct snap_dma_q *q, struct ibv_pd *pd,
			     struct snap_dma_q_create_attr *attr)
{
	struct snap_qp_attr qp_init_attr = {};
	int rc;

	/* cannot create empty cq or a qp without one */
	qp_init_attr.sq_size = snap_max(attr->tx_qsize / 4, SNAP_DMA_FW_QP_MIN_SEND_WR);
	qp_init_attr.rq_size = 1;
	/* give one sge so that we can post which is useful for testing */
	qp_init_attr.sq_max_sge = 1;

	/* the qp 'resources' are going to be replaced by the fw. We do not
	 * need use DV or GGA here
	 **/
	rc = snap_create_qp_helper(pd, NULL, NULL, 0, &qp_init_attr, &q->fw_qp, SNAP_DMA_Q_MODE_VERBS);
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
				    struct ibv_qp_attr *qp_attr, int attr_mask,
				    bool force_loopback, uint16_t udp_sport)
{
	uint8_t in[DEVX_ST_SZ_BYTES(init2rtr_qp_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(init2rtr_qp_out)] = {0};
	void *qpc = DEVX_ADDR_OF(init2rtr_qp_in, in, qpc);
	uint8_t mac[6];
	uint8_t gid[16];
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
	if (attr_mask & IBV_QP_AV) {
		if (qp_attr->ah_attr.is_global) {
			DEVX_SET(qpc, qpc, primary_address_path.tclass,
				 qp_attr->ah_attr.grh.traffic_class);
			/* set destination mac */
			memcpy(gid, qp_attr->ah_attr.grh.dgid.raw, 16);
			memcpy(DEVX_ADDR_OF(qpc, qpc, primary_address_path.rgid_rip),
					    gid,
					    DEVX_FLD_SZ_BYTES(qpc, primary_address_path.rgid_rip));
			mac[0] = gid[8] ^ 0x02;
			mac[1] = gid[9];
			mac[2] = gid[10];
			mac[3] = gid[13];
			mac[4] = gid[14];
			mac[5] = gid[15];
			memcpy(DEVX_ADDR_OF(qpc, qpc, primary_address_path.rmac_47_32),
					    mac, 6);

			DEVX_SET(qpc, qpc, primary_address_path.udp_sport, udp_sport);
			DEVX_SET(qpc, qpc, primary_address_path.src_addr_index,
				 qp_attr->ah_attr.grh.sgid_index);
			if (qp_attr->ah_attr.sl & 0x7)
				DEVX_SET(qpc, qpc, primary_address_path.eth_prio,
					 qp_attr->ah_attr.sl & 0x7);
			if (qp_attr->ah_attr.grh.hop_limit > 1)
				DEVX_SET(qpc, qpc, primary_address_path.hop_limit,
					 qp_attr->ah_attr.grh.hop_limit);
			else
				DEVX_SET(qpc, qpc, primary_address_path.hop_limit, 64);

			if (force_loopback)
				DEVX_SET(qpc, qpc, primary_address_path.fl, 1);
		} else {
			DEVX_SET(qpc, qpc, primary_address_path.rlid,
				 qp_attr->ah_attr.dlid);
			DEVX_SET(qpc, qpc, primary_address_path.grh, 0);
			if (qp_attr->ah_attr.sl & 0xf)
				DEVX_SET(qpc, qpc, primary_address_path.sl,
					 qp_attr->ah_attr.sl & 0xf);
		}
	}

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

#define SNAP_IB_GRH_FLOWLABEL_MASK            (0x000FFFFF)

static uint16_t snap_get_udp_sport(uint16_t roce_min_src_udp_port,
				   uint32_t lqpn, uint32_t rqpn)
{
	/* flow_label is a field in ipv6 header, how ipv6 flow label
	 * and udp source port are related, please refer to:
	 * https://www.spinics.net/lists/linux-rdma/msg87626.html.
	 **/
	uint32_t fl, fl_low, fl_high;
	uint64_t v = (uint64_t)lqpn * rqpn;

	/* hash function to calc fl from lqpn and rqpn.
	 * a copy of rdma_calc_flow_label() from kernel
	 **/
	v ^= v >> 20;
	v ^= v >> 40;
	fl = (uint32_t)(v & SNAP_IB_GRH_FLOWLABEL_MASK);

	/* hash function to calc udp_sport from fl.
	 * a copy of rdma_flow_label_to_udp_sport() from kernel
	 **/
	fl_low = fl & 0x03FFF;
	fl_high = fl & 0xFC000;
	fl_low ^= fl_high >> 14;

	return (uint16_t)(fl_low | roce_min_src_udp_port);
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

static int snap_activate_loop_qp(struct snap_dma_q *q, enum ibv_mtu mtu,
				 bool ib_en, uint16_t lid,
				 bool roce_en, bool force_loopback,
				 struct ibv_gid_entry *sw_gid_entry,
				 struct ibv_gid_entry *fw_gid_entry,
				 struct snap_roce_caps *roce_caps)
{
	struct ibv_qp_attr attr;
	int rc, flags_mask;
	uint16_t udp_sport;

	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IBV_QPS_INIT;
	attr.pkey_index = SNAP_DMA_QP_PKEY_INDEX;
	attr.port_num = SNAP_DMA_QP_PORT_NUM;
	attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
	flags_mask = IBV_QP_STATE |
		     IBV_QP_PKEY_INDEX |
		     IBV_QP_PORT |
		     IBV_QP_ACCESS_FLAGS;

	rc = snap_modify_lb_qp_rst2init(q->sw_qp.qp, &attr, flags_mask);
	if (rc) {
		snap_error("failed to modify SW QP to INIT errno=%d\n", rc);
		return rc;
	} else if (q->sw_qp.mode == SNAP_DMA_Q_MODE_GGA) {
		rc = snap_modify_lb_qp_init2init(q->sw_qp.qp);
		if (rc) {
			snap_error("failed to modify SW QP in INIT2INIT errno=%d\n",
				   rc);
			return rc;
		}
	}

	rc = snap_modify_lb_qp_rst2init(q->fw_qp.qp, &attr, flags_mask);
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
	if (ib_en) {
		attr.ah_attr.is_global = 0;
		attr.ah_attr.dlid = lid;
	} else {
		attr.ah_attr.is_global = 1;
	}

	attr.dest_qp_num = snap_qp_get_qpnum(q->fw_qp.qp);
	flags_mask = IBV_QP_STATE              |
		     IBV_QP_AV                 |
		     IBV_QP_PATH_MTU           |
		     IBV_QP_DEST_QPN           |
		     IBV_QP_RQ_PSN             |
		     IBV_QP_MAX_DEST_RD_ATOMIC |
		     IBV_QP_MIN_RNR_TIMER;

	if (sw_gid_entry && sw_gid_entry->gid_type == IBV_GID_TYPE_ROCE_V2 &&
		roce_caps->roce_version & MLX5_ROCE_VERSION_2_0) {
		udp_sport = snap_get_udp_sport(roce_caps->r_roce_min_src_udp_port,
				snap_qp_get_qpnum(q->sw_qp.qp),
				snap_qp_get_qpnum(q->fw_qp.qp));
	} else {
		udp_sport = 0;
	}

	if (roce_en && !force_loopback)
		memcpy(attr.ah_attr.grh.dgid.raw, fw_gid_entry->gid.raw,
		       sizeof(fw_gid_entry->gid.raw));
	rc = snap_modify_lb_qp_init2rtr(q->sw_qp.qp, &attr, flags_mask,
				      force_loopback, udp_sport);
	if (rc) {
		snap_error("failed to modify SW QP to RTR errno=%d\n", rc);
		return rc;
	}

	if (fw_gid_entry && fw_gid_entry->gid_type == IBV_GID_TYPE_ROCE_V2 &&
		roce_caps->roce_version & MLX5_ROCE_VERSION_2_0) {
		udp_sport = snap_get_udp_sport(roce_caps->r_roce_min_src_udp_port,
				snap_qp_get_qpnum(q->sw_qp.qp),
				snap_qp_get_qpnum(q->fw_qp.qp));
	} else {
		udp_sport = 0;
	}

	if (roce_en && !force_loopback)
		memcpy(attr.ah_attr.grh.dgid.raw, sw_gid_entry->gid.raw,
		       sizeof(sw_gid_entry->gid.raw));
	attr.dest_qp_num = snap_qp_get_qpnum(q->sw_qp.qp);
	rc = snap_modify_lb_qp_init2rtr(q->fw_qp.qp, &attr, flags_mask,
				      force_loopback, udp_sport);
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
	rc = snap_modify_lb_qp_rtr2rts(q->sw_qp.qp, &attr, flags_mask);
	if (rc) {
		snap_error("failed to modify SW QP to RTS errno=%d\n", rc);
		return rc;
	}

	rc = snap_modify_lb_qp_rtr2rts(q->fw_qp.qp, &attr, flags_mask);
	if (rc) {
		snap_error("failed to modify FW QP to RTS errno=%d\n", rc);
		return rc;
	}

	return 0;
}

static int snap_connect_loop_qp(struct snap_dma_q *q, struct ibv_pd *pd)
{
	struct ibv_gid_entry sw_gid_entry, fw_gid_entry;
	int rc;
	bool roce_en = false, ib_en = false;
	uint16_t lid = 0;
	enum ibv_mtu mtu = IBV_MTU_1024;
	bool force_loopback = false;
	struct snap_roce_caps roce_caps = {0};

	rc = check_port(pd->context, SNAP_DMA_QP_PORT_NUM, &roce_en,
			&ib_en, &lid, &mtu);
	if (rc)
		return rc;

	/* If IB is supported, can immediately advance to QP activation */
	if (ib_en)
		return snap_activate_loop_qp(q, mtu, ib_en, lid, 0, 0, NULL,
					     NULL, &roce_caps);

	rc = fill_roce_caps(pd->context, &roce_caps);
	if (rc)
		return rc;

	/* Check if force-loopback is supported based on roce caps */
	if (roce_caps.resources_on_nvme_emulation_manager &&
	    ((roce_caps.roce_enabled && roce_caps.fl_when_roce_enabled) ||
	     (!roce_caps.roce_enabled && roce_caps.fl_when_roce_disabled))) {
		force_loopback = true;
	} else if (roce_en) {
		/*
		 * If force loopback is unsupported try to acquire GIDs and
		 * open a non-fl QP
		 */
		rc = ibv_query_gid_ex(pd->context, SNAP_DMA_QP_PORT_NUM,
				   SNAP_DMA_QP_GID_INDEX, &sw_gid_entry, 0);
		if (!rc)
			rc = ibv_query_gid_ex(pd->context, SNAP_DMA_QP_PORT_NUM,
					   SNAP_DMA_QP_GID_INDEX, &fw_gid_entry, 0);
		if (rc) {
			snap_error("Failed to get gid[%d] for loop QP\n",
				   SNAP_DMA_QP_GID_INDEX);
			return rc;
		}
	} else {
		snap_error("RoCE is disabled and force-loopback option is not supported. Cannot create queue\n");
		return -ENOTSUP;
	}

	return snap_activate_loop_qp(q, mtu, ib_en, lid, roce_en,
				     force_loopback, &sw_gid_entry, &fw_gid_entry, &roce_caps);
}

static int snap_create_io_ctx(struct snap_dma_q *q, struct ibv_pd *pd,
		struct snap_dma_q_create_attr *attr)
{
	int i, ret;
	struct snap_relaxed_ordering_caps caps;
	struct mlx5_devx_mkey_attr mkey_attr = {};

	q->iov_supported = false;

	if (!attr->iov_enable)
		return 0;

	/*
	 * io_ctx only required when post UMR WQE involved, and
	 * post UMR WQE is not support on stardard verbs mode.
	 */
	if (q->sw_qp.mode == SNAP_DMA_Q_MODE_VERBS)
		return 0;

	ret = posix_memalign((void **)&q->io_ctx, SNAP_DMA_BUF_ALIGN,
			q->tx_available * sizeof(struct snap_dma_q_io_ctx));
	if (ret) {
		snap_error("alloc dma_q io_ctx array failed");
		return -ENOMEM;
	}

	memset(q->io_ctx, 0, q->tx_available * sizeof(struct snap_dma_q_io_ctx));

	ret = snap_query_relaxed_ordering_caps(pd->context, &caps);
	if (ret) {
		snap_error("query relaxed_ordering_caps failed, ret:%d\n", ret);
		goto free_io_ctx;
	}

	TAILQ_INIT(&q->free_io_ctx);

	mkey_attr.addr = 0;
	mkey_attr.size = 0;
	mkey_attr.log_entity_size = 0;
	mkey_attr.relaxed_ordering_write = caps.relaxed_ordering_write;
	mkey_attr.relaxed_ordering_read = caps.relaxed_ordering_read;
	mkey_attr.klm_num = 0;
	mkey_attr.klm_array = NULL;

	for (i = 0; i < q->tx_available; i++) {
		q->io_ctx[i].klm_mkey = snap_create_indirect_mkey(pd, &mkey_attr);
		if (!q->io_ctx[i].klm_mkey) {
			snap_error("create klm mkey for io_ctx[%d] failed\n", i);
			goto destroy_klm_mkeys;
		}

		q->io_ctx[i].q = q;
		TAILQ_INSERT_TAIL(&q->free_io_ctx, &q->io_ctx[i], entry);
	}

	q->iov_supported = true;

	return 0;

destroy_klm_mkeys:
	for (i--; i >= 0; i--) {
		TAILQ_REMOVE(&q->free_io_ctx, &q->io_ctx[i], entry);
		snap_destroy_indirect_mkey(q->io_ctx[i].klm_mkey);
	}
free_io_ctx:
	free(q->io_ctx);
	q->io_ctx = NULL;

	return 1;
}

static void snap_destroy_io_ctx(struct snap_dma_q *q)
{
	int i;

	if (!q->io_ctx)
		return;

	for (i = 0; i < q->tx_available; i++) {
		TAILQ_REMOVE(&q->free_io_ctx, &q->io_ctx[i], entry);
		snap_destroy_indirect_mkey(q->io_ctx[i].klm_mkey);
	}

	free(q->io_ctx);
	q->io_ctx = NULL;
	q->iov_supported = false;
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
		struct snap_dma_q_create_attr *attr)
{
	struct snap_dma_q *q;
	int rc;

	if (!pd)
		return NULL;

	if (!attr->rx_cb)
		return NULL;

	q = calloc(1, sizeof(*q));
	if (!q)
		return NULL;

	rc = snap_create_sw_qp(q, pd, attr);
	if (rc)
		goto free_q;

	rc = snap_create_fw_qp(q, pd, attr);
	if (rc)
		goto free_sw_qp;

	rc = snap_connect_loop_qp(q, pd);
	if (rc)
		goto free_fw_qp;

	rc = snap_create_io_ctx(q, pd, attr);
	if (rc)
		goto free_fw_qp;

	q->uctx = attr->uctx;
	q->rx_cb = attr->rx_cb;
	return q;

free_fw_qp:
	snap_destroy_fw_qp(q);
free_sw_qp:
	snap_destroy_sw_qp(q);
free_q:
	free(q);
	return NULL;
}

/**
 * snap_dma_q_destroy() - Destroy DMA queue
 *
 * @q: dma queue
 */
void snap_dma_q_destroy(struct snap_dma_q *q)
{
	snap_destroy_io_ctx(q);
	snap_destroy_sw_qp(q);
	snap_destroy_fw_qp(q);
	free(q);
}

