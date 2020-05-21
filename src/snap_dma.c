#include <stdlib.h>

#include "snap.h"
#include "snap_dma.h"
#include "mlx5_ifc.h"

static int check_port(struct ibv_context *ctx, int port_num, bool *roce_en,
		      bool *ib_en, uint16_t *lid)
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
			snap_error("IB enabled and GRH addressing is required"
				   " but only local addressing is supported\n");
			return -1;
		}
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
	return 0;
}

static void snap_destroy_qp_helper(struct snap_dma_ibv_qp *qp)
{
	ibv_destroy_qp(qp->qp);
	ibv_destroy_cq(qp->rx_cq);
	ibv_destroy_cq(qp->tx_cq);
}

static void snap_free_rx_wqes(struct snap_dma_ibv_qp *qp)
{
	ibv_dereg_mr(qp->rx_mr);
	free(qp->rx_buf);
}

static int snap_alloc_rx_wqes(struct snap_dma_ibv_qp *qp, int rx_qsize,
		int rx_elem_size)
{
	struct ibv_pd *pd = qp->qp->pd;
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
		struct ibv_qp_init_attr *attr, struct snap_dma_ibv_qp *qp)
{
	struct ibv_context *ibv_ctx = pd->context;

	qp->tx_cq = ibv_create_cq(ibv_ctx, attr->cap.max_send_wr, cq_context,
			comp_channel, comp_vector);
	if (!qp->tx_cq)
		return -EINVAL;

	qp->rx_cq = ibv_create_cq(ibv_ctx, attr->cap.max_recv_wr, cq_context,
			comp_channel, comp_vector);
	if (!qp->rx_cq)
		goto free_tx_cq;

	attr->qp_type = IBV_QPT_RC;
	attr->send_cq = qp->tx_cq;
	attr->recv_cq = qp->rx_cq;

	qp->qp = ibv_create_qp(pd, attr);
	if (!qp->qp)
		goto free_rx_cq;

	snap_debug("created qp 0x%x tx %d rx %d tx_inline %d on pd %p\n",
		   qp->qp->qp_num, attr->cap.max_send_wr,
		   attr->cap.max_recv_wr, attr->cap.max_inline_data, pd);

	return 0;

free_rx_cq:
	ibv_destroy_cq(qp->rx_cq);
free_tx_cq:
	ibv_destroy_cq(qp->tx_cq);
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
	struct ibv_qp_init_attr init_attr = {};
	struct ibv_recv_wr rx_wr, *bad_wr;
	struct ibv_sge rx_sge;
	int i, rc;

	q->tx_available = attr->tx_qsize;
	q->tx_qsize = attr->tx_qsize;
	q->rx_elem_size = attr->rx_elem_size;
	q->tx_elem_size = attr->tx_elem_size;

	init_attr.cap.max_send_wr = attr->tx_qsize;
	/* Need more space in rx queue in order to avoid memcpy() on rx data */
	init_attr.cap.max_recv_wr = 2 * attr->rx_qsize;
	/* we must be able to send CQEs inline */
	init_attr.cap.max_inline_data = attr->tx_elem_size;

	init_attr.cap.max_send_sge = 1;
	init_attr.cap.max_recv_sge = 1;

	rc = snap_create_qp_helper(pd, attr->comp_context, attr->comp_channel,
			attr->comp_vector, &init_attr, &q->sw_qp);
	if (rc)
		return rc;

	rc = snap_alloc_rx_wqes(&q->sw_qp, 2 * attr->rx_qsize, attr->rx_elem_size);
	if (rc)
		goto free_qp;

	for (i = 0; i < 2 * attr->rx_qsize; i++) {
		rx_sge.addr = (uint64_t)(q->sw_qp.rx_buf +
				i * attr->rx_elem_size);
		rx_sge.length = attr->rx_elem_size;
		rx_sge.lkey = q->sw_qp.rx_mr->lkey;

		rx_wr.wr_id = rx_sge.addr;
		rx_wr.next = NULL;
		rx_wr.sg_list = &rx_sge;
		rx_wr.num_sge = 1;

		rc = ibv_post_recv(q->sw_qp.qp, &rx_wr, &bad_wr);
		if (rc)
			goto free_rx_resources;
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

static int snap_create_fw_qp(struct snap_dma_q *q, struct ibv_pd *pd)
{
	struct ibv_qp_init_attr init_attr = {};
	int rc;

	/* cannot create empty cq or a qp without one */
	init_attr.cap.max_send_wr = 1;
	init_attr.cap.max_recv_wr = 1;
	/* give one sge so that we can post which is useful
	 * for testing */
	init_attr.cap.max_send_sge = 1;

	rc = snap_create_qp_helper(pd, NULL, NULL, 0, &init_attr, &q->fw_qp);
	return rc;
}

static int snap_modify_lb_qp_to_init(struct ibv_qp *qp,
				     struct ibv_qp_attr *qp_attr, int attr_mask)
{
	uint8_t in[DEVX_ST_SZ_BYTES(rst2init_qp_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(rst2init_qp_out)] = {0};
	void *qpc = DEVX_ADDR_OF(rst2init_qp_in, in, qpc);
	int ret;

	DEVX_SET(rst2init_qp_in, in, opcode, MLX5_CMD_OP_RST2INIT_QP);
	DEVX_SET(rst2init_qp_in, in, qpn, qp->qp_num);
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

	ret = mlx5dv_devx_qp_modify(qp, in, sizeof(in), out, sizeof(out));
	if (ret)
		snap_error("failed to modify qp to init with errno = %m\n");
	return ret;
}

static int snap_modify_lb_qp_to_rtr(struct ibv_qp *qp,
				    struct ibv_qp_attr *qp_attr, int attr_mask,
				    bool roce_enabled)
{
	uint8_t in[DEVX_ST_SZ_BYTES(init2rtr_qp_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(init2rtr_qp_out)] = {0};
	void *qpc = DEVX_ADDR_OF(init2rtr_qp_in, in, qpc);
	uint8_t mac[6];
	uint8_t gid[16];
	int ret;

	DEVX_SET(init2rtr_qp_in, in, opcode, MLX5_CMD_OP_INIT2RTR_QP);
	DEVX_SET(init2rtr_qp_in, in, qpn, qp->qp_num);

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

			if (!roce_enabled)
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

	ret = mlx5dv_devx_qp_modify(qp, in, sizeof(in), out, sizeof(out));
	if (ret)
		snap_error("failed to modify qp to rtr with errno = %m\n");
	return ret;
}

static int snap_modify_lb_qp_to_rts(struct ibv_qp *qp,
				    struct ibv_qp_attr *qp_attr, int attr_mask)
{
	uint8_t in[DEVX_ST_SZ_BYTES(rtr2rts_qp_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(rtr2rts_qp_out)] = {0};
	void *qpc = DEVX_ADDR_OF(rtr2rts_qp_in, in, qpc);
	int ret;

	DEVX_SET(rtr2rts_qp_in, in, opcode, MLX5_CMD_OP_RTR2RTS_QP);
	DEVX_SET(rtr2rts_qp_in, in, qpn, qp->qp_num);

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

	ret = mlx5dv_devx_qp_modify(qp, in, sizeof(in), out, sizeof(out));
	if (ret)
		snap_error("failed to modify qp to rts with errno = %m\n");
	return ret;
}

static int snap_connect_loop_qp(struct snap_dma_q *q)
{
	struct ibv_qp_attr attr;
	union ibv_gid sw_gid, fw_gid;
	int rc, flags_mask;
	bool roce_en, ib_en;
	uint16_t lid;

	rc = check_port(q->sw_qp.qp->context, SNAP_DMA_QP_PORT_NUM, &roce_en,
			&ib_en, &lid);
	if (rc)
		return rc;

	if (roce_en) {
		rc = ibv_query_gid(q->sw_qp.qp->context, SNAP_DMA_QP_PORT_NUM,
				   SNAP_DMA_QP_GID_INDEX, &sw_gid);
		if (rc) {
			snap_error("Failed to get SW QP gid[%d]\n",
				   SNAP_DMA_QP_GID_INDEX);
			return rc;
		}

		rc = ibv_query_gid(q->fw_qp.qp->context, SNAP_DMA_QP_PORT_NUM,
				   SNAP_DMA_QP_GID_INDEX, &fw_gid);
		if (rc) {
			snap_error("Failed to get FW QP gid[%d]\n",
				   SNAP_DMA_QP_GID_INDEX);
			return rc;
		}
	}

	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IBV_QPS_INIT;
	attr.pkey_index = SNAP_DMA_QP_PKEY_INDEX;
	attr.port_num = SNAP_DMA_QP_PORT_NUM;
	attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ;
	flags_mask = IBV_QP_STATE |
		     IBV_QP_PKEY_INDEX |
		     IBV_QP_PORT |
		     IBV_QP_ACCESS_FLAGS;

	rc = snap_modify_lb_qp_to_init(q->sw_qp.qp, &attr, flags_mask);
	if (rc) {
		snap_error("failed to modify SW QP to INIT errno=%d\n", rc);
		return rc;
	}

	rc = snap_modify_lb_qp_to_init(q->fw_qp.qp, &attr, flags_mask);
	if (rc) {
		snap_error("failed to modify FW QP to INIT errno=%d\n", rc);
		return rc;
	}

	memset(&attr, 0, sizeof(attr));
	attr.qp_state = IBV_QPS_RTR;
	attr.path_mtu = SNAP_DMA_QP_PATH_MTU;
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

	attr.dest_qp_num = q->fw_qp.qp->qp_num;
	flags_mask = IBV_QP_STATE              |
		     IBV_QP_AV                 |
		     IBV_QP_PATH_MTU           |
		     IBV_QP_DEST_QPN           |
		     IBV_QP_RQ_PSN             |
		     IBV_QP_MAX_DEST_RD_ATOMIC |
		     IBV_QP_MIN_RNR_TIMER;

	if (roce_en)
		memcpy(attr.ah_attr.grh.dgid.raw, fw_gid.raw,
		       sizeof(fw_gid.raw));
	rc = snap_modify_lb_qp_to_rtr(q->sw_qp.qp, &attr, flags_mask, roce_en);
	if (rc) {
		snap_error("failed to modify SW QP to RTR errno=%d\n", rc);
		return rc;
	}

	if (roce_en)
		memcpy(attr.ah_attr.grh.dgid.raw, sw_gid.raw,
		       sizeof(sw_gid.raw));
	attr.dest_qp_num = q->sw_qp.qp->qp_num;
	rc = snap_modify_lb_qp_to_rtr(q->fw_qp.qp, &attr, flags_mask, roce_en);
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
	 * using devx since kernel doesn't know QPs are on RTR state */
	rc = snap_modify_lb_qp_to_rts(q->sw_qp.qp, &attr, flags_mask);
	if (rc) {
		snap_error("failed to modify SW QP to RTS errno=%d\n", rc);
		return rc;
	}

	rc = snap_modify_lb_qp_to_rts(q->fw_qp.qp, &attr, flags_mask);
	if (rc) {
		snap_error("failed to modify FW QP to RTS errno=%d\n", rc);
		return rc;
	}

	return 0;
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

	rc = snap_create_fw_qp(q, pd);
	if (rc)
		goto free_sw_qp;

	rc = snap_connect_loop_qp(q);
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
	snap_destroy_sw_qp(q);
	snap_destroy_fw_qp(q);
	free(q);
}

static inline int snap_dma_q_progress_rx(struct snap_dma_q *q)
{
	struct ibv_wc wcs[SNAP_DMA_MAX_RX_COMPLETIONS];
	int i, n;
	int rc;
	struct ibv_recv_wr rx_wr[SNAP_DMA_MAX_RX_COMPLETIONS + 1], *bad_wr;
	struct ibv_sge rx_sge[SNAP_DMA_MAX_RX_COMPLETIONS + 1];

	n = ibv_poll_cq(q->sw_qp.rx_cq, SNAP_DMA_MAX_RX_COMPLETIONS, wcs);
	if (n == 0)
		return 0;

	if (snap_unlikely(n < 0)) {
	   snap_error("dma queue %p: failed to poll rx cq: errno=%d\n", q, n);
	   return 0;
	}

	for (i = 0; i < n; i++) {
		if (snap_unlikely(wcs[i].status != IBV_WC_SUCCESS)) {
			if (wcs[i].status == IBV_WC_WR_FLUSH_ERR) {
				snap_debug("dma queue %p: got FLUSH_ERROR\n", q);
			} else {
				snap_error("dma queue %p: got unexpected "
					   "completion status 0x%x, opcode 0x%x\n",
					   q, wcs[i].status, wcs[i].opcode);
			}
			return n;
		}

		q->rx_cb(q, (void *)wcs[i].wr_id, wcs[i].byte_len,
				wcs[i].imm_data);
		rx_sge[i].addr = wcs[i].wr_id;
		rx_sge[i].length = q->rx_elem_size;
		rx_sge[i].lkey = q->sw_qp.rx_mr->lkey;

		rx_wr[i].wr_id = rx_sge[i].addr;
		rx_wr[i].next = &rx_wr[i + 1];
		rx_wr[i].sg_list = &rx_sge[i];
		rx_wr[i].num_sge = 1;
	}

	rx_wr[i - 1].next = NULL;
	rc = ibv_post_recv(q->sw_qp.qp, rx_wr, &bad_wr);
	if (snap_unlikely(rc))
		snap_error("dma queue %p: failed to post recv: errno=%d\n",
				q, rc);
	return n;
}

static inline int snap_dma_q_progress_tx(struct snap_dma_q *q)
{
	struct ibv_wc wcs[SNAP_DMA_MAX_TX_COMPLETIONS];
	struct snap_dma_completion *comp;
	int i, n;

	n = ibv_poll_cq(q->sw_qp.tx_cq, SNAP_DMA_MAX_TX_COMPLETIONS, wcs);
	if (n == 0)
		return 0;

	if (snap_unlikely(n < 0)) {
		snap_error("dma queue %p: failed to poll tx cq: errno=%d\n",
				q, n);
		return 0;
	}

	q->tx_available += n;

	for (i = 0; i < n; i++) {
		if (snap_unlikely(wcs[i].status != IBV_WC_SUCCESS))
			snap_error("dma queue %p: got unexpected completion "
					"status 0x%x, opcode 0x%x\n",
					q, wcs[i].status, wcs[i].opcode);
		/* wr_id, status, qp_num and vendor_err are still valid in
		 * case of error */
		comp = (struct snap_dma_completion *)wcs[i].wr_id;
		if (!comp)
			continue;

		if (--comp->count == 0)
			comp->func(comp, wcs[i].status);
	}

	return n;
}

/**
 * snap_dma_q_progress() - Progress dma queue
 * @q: dma queue
 *
 * The function progresses both send and receive operations on the given dma
 * queue.
 *
 * Send &typedef snap_dma_comp_cb_t and receive &typedef snap_dma_rx_cb_t
 * completion callbacks may be called from within this function.
 * It is guaranteed that such callbacks are called in the execution context
 * of the progress.
 *
 * If dma queue was created with a completion channel then one can
 * use it's file descriptor to check for events instead of the
 * polling. When event is detected snap_dma_q_progress() should
 * be called to process it.
 *
 * Return: number of events (send and receive) that were processed
 */
int snap_dma_q_progress(struct snap_dma_q *q)
{
	int n;

	n = snap_dma_q_progress_tx(q);
	n += snap_dma_q_progress_rx(q);
	return n;
}

/**
 * snap_dma_q_arm() - Request notification
 * @q: dma queue
 *
 * The function 'arms' dma queue to report send and receive events over its
 * completion channel.
 *
 * Return:  0 or -errno on error
 */
int snap_dma_q_arm(struct snap_dma_q *q)
{
	int rc;

	rc = ibv_req_notify_cq(q->sw_qp.tx_cq, 0);
	if (rc)
		return rc;

	return ibv_req_notify_cq(q->sw_qp.rx_cq, 0);
}

static inline int qp_can_tx(struct snap_dma_q *q)
{
	/* later we can also add cq space check */
	return q->tx_available > 0;
}

static inline int do_dma_xfer(struct snap_dma_q *q, void *buf, size_t len,
		uint32_t lkey, uint64_t raddr, uint32_t rkey, int op, int flags,
		struct snap_dma_completion *comp)
{
	struct ibv_qp *qp = q->sw_qp.qp;
	struct ibv_send_wr rdma_wr, *bad_wr;
	struct ibv_sge sge;
	int rc;

	if (snap_unlikely(!qp_can_tx(q)))
		return -EAGAIN;

	sge.addr = (uint64_t)buf;
	sge.length = len;
	sge.lkey = lkey;

	rdma_wr.opcode = op;
	rdma_wr.send_flags = IBV_SEND_SIGNALED | flags;
	rdma_wr.num_sge = 1;
	rdma_wr.sg_list = &sge;
	rdma_wr.wr_id = (uint64_t)comp;
	rdma_wr.wr.rdma.rkey = rkey;
	rdma_wr.wr.rdma.remote_addr = raddr;
	rdma_wr.next = NULL;

	rc = ibv_post_send(qp, &rdma_wr, &bad_wr);
	if (snap_unlikely(rc)) {
		snap_error("DMA queue: %p failed to post opcode 0x%x\n",
				q, op);
		return rc;
	}

	q->tx_available--;
	return 0;
}

/**
 * snap_dma_q_write() - DMA write to the host memory
 * @q:            dma queue
 * @src_buf:      where to get/put data
 * @len:          data length
 * @lkey:         local memory key
 * @dstaddr:      host physical or virtual address
 * @rmkey:        host memory key that describes remote memory
 * @comp:         dma completion structure
 *
 * The function starts non blocking memory transfer to the host memory. Once
 * data transfer is completed the user defined callback may be called.
 * Operations on the same dma queue are done in order.
 *
 * Return:
 * 0
 *	operation has been successfully submitted to the queue
 *	and is now in progress
 * \-EAGAIN
 *	queue does not have enough resources, must be retried later
 * < 0
 *	some other error has occured. Return value is -errno
 */
int snap_dma_q_write(struct snap_dma_q *q, void *src_buf, size_t len,
		uint32_t lkey, uint64_t dstaddr, uint32_t rmkey,
		struct snap_dma_completion *comp)
{
	return do_dma_xfer(q, src_buf, len, lkey, dstaddr, rmkey,
			IBV_WR_RDMA_WRITE, 0, comp);
}

/**
 * snap_dma_q_write_short() - DMA write of small amount of data to the
 *                            host memory
 * @q:            dma queue
 * @src_buf:      where to get data
 * @len:          data length. It must be no greater than the
 *                &struct snap_dma_q_create_attr.tx_elem_size
 * @dstaddr:      host physical or virtual address
 * @rmkey:        host memory key that describes remote memory
 *
 * The function starts non blocking memory transfer to the host memory. The
 * function is optimized to reduce latency when sending small amount of data.
 * Operations on the same dma queue are done in order.
 *
 * Note that it is safe to use @src_buf after the function returns.
 *
 * Return:
 * 0
 *	operation has been successfully submitted to the queue
 * \-EAGAIN
 *	queue does not have enough resources, must be retried later
 * < 0
 *	some other error has occured. Return value is -errno
 */
int snap_dma_q_write_short(struct snap_dma_q *q, void *src_buf, size_t len,
		uint64_t dstaddr, uint32_t rmkey)
{
	if (snap_unlikely(len > q->tx_elem_size))
		return -EINVAL;

	return do_dma_xfer(q, src_buf, len, 0, dstaddr, rmkey,
			IBV_WR_RDMA_WRITE, IBV_SEND_INLINE, NULL);
}

/**
 * snap_dma_q_read() - DMA read to the host memory
 * @q:            dma queue
 * @dst_buf:      where to get/put data
 * @len:          data length
 * @lkey:         local memory key
 * @srcaddr:      host physical or virtual address
 * @rmkey:        host memory key that describes remote memory
 * @comp:         dma completion structure
 *
 * The function starts non blocking memory transfer from the host memory. Once
 * data transfer is completed the user defined callback may be called.
 * Operations on the same dma queue are done in order.
 *
 * Return:
 * 0
 *	operation has been successfully submitted to the queue
 *	and is now in progress
 * \-EAGAIN
 *	queue does not have enough resources, must be retried later
 * < 0
 *	some other error has occured. Return value is -errno
 */
int snap_dma_q_read(struct snap_dma_q *q, void *dst_buf, size_t len,
		uint32_t lkey, uint64_t srcaddr, uint32_t rmkey,
		struct snap_dma_completion *comp)
{
	return do_dma_xfer(q, dst_buf, len, lkey, srcaddr, rmkey,
			IBV_WR_RDMA_READ, 0, comp);
}

/**
 * snap_dma_q_send_completion() - Send completion to the host
 * @q:       dma queue to
 * @src_buf: local buffer to copy the completion data from.
 * @len:     the length of completion. E.x. 16 bytes for the NVMe. It
 *           must be no greater than the value of the
 *           &struct snap_dma_q_create_attr.tx_elem_size
 *
 * The function sends a completion notification to the host. The exact meaning of
 * the 'completion' is defined by the emulation layer. For example in case of
 * NVMe it means that completion entry is placed in the completion queue and
 * MSI-X interrupt is triggered.
 *
 * Note that it is safe to use @src_buf after the function returns.
 *
 * Return:
 * 0
 *	operation has been successfully submitted to the queue
 *	and is now in progress
 * \-EAGAIN
 *	queue does not have enough resources, must be retried later
 * < 0
 *	some other error has occured. Return value is -errno
 *
 */
int snap_dma_q_send_completion(struct snap_dma_q *q, void *src_buf, size_t len)
{
	struct ibv_qp *qp = q->sw_qp.qp;
	struct ibv_send_wr send_wr, *bad_wr;
	struct ibv_sge sge;
	int rc;

	if (snap_unlikely(len > q->tx_elem_size))
		return -EINVAL;

	if (snap_unlikely(!qp_can_tx(q)))
		return -EAGAIN;

	sge.addr = (uint64_t)src_buf;
	sge.length = len;

	send_wr.opcode = IBV_WR_SEND;
	send_wr.send_flags = IBV_SEND_SIGNALED|IBV_SEND_INLINE;
	send_wr.num_sge = 1;
	send_wr.sg_list = &sge;
	send_wr.next = NULL;
	send_wr.wr_id = 0;

	rc = ibv_post_send(qp, &send_wr, &bad_wr);
	if (snap_unlikely(rc)) {
		snap_error("DMA queue %p: failed to post send: %m\n", q);
		return rc;
	}

	q->tx_available--;
	return 0;
}

/**
 * snap_dma_q_flush() - Wait for outstanding operations to complete
 * @q:   dma queue
 *
 * The function waits until all outstanding operations started with
 * mlx_dma_q_read(), mlx_dma_q_write() or mlx_dma_q_send_completion() are
 * finished. The function does not progress receive operation.
 *
 * The purpose of this function is to facilitate blocking mode dma
 * and completion operations.
 *
 * Return: number of completed operations or -errno.
 */
int snap_dma_q_flush(struct snap_dma_q *q)
{
	int n;

	n = 0;
	while (q->tx_available < q->tx_qsize)
		n += snap_dma_q_progress_tx(q);
	return n;
}

/**
 * snap_dma_q_get_fw_qp() - Get FW qp
 * @q:   dma queue
 *
 * The function returns qp that can be used by the FW emulation objects
 * See snap_dma_q_create() for the detailed explanation
 *
 * Return: fw qp
 */
struct ibv_qp *snap_dma_q_get_fw_qp(struct snap_dma_q *q)
{
	return q->fw_qp.qp;
}
