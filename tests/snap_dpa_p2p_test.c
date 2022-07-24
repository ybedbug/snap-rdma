#include "snap_dma.h"
#include "snap_dpa_p2p.h"
#include "snap_virtio_common.h"
#include "snap_dpa_rt.h"
#include "snap_dpa_virtq.h"

#define DESC_COUNT 256

static struct vring vr;
static struct ibv_mr * vr_mr;
static bool vq_table_rec = 0;

static struct snap_dpa_rt_thread g_dpu_rt_thr;

static void p2p_rx(struct snap_dma_q *dma_q, const void *data, uint32_t len,
                         uint32_t imm_data)
{
	struct virtq_split_tunnel_req *req = (struct virtq_split_tunnel_req*)data;
	int i;

	if (req->hdr.dpa_vq_table_flag == VQ_TABLE_REC) {
		vq_table_rec = 1;
		for (i = 0; i < DESC_COUNT; i++) {
			if (vr.desc[i].addr != req->tunnel_descs[i].addr) {
				printf("Error in vq table message - desc table sent not equal\n");
				printf("desc index :%d, desc sent addr: %lld, desc received "
						"addr: %lld \n", i , vr.desc[i].addr, req->tunnel_descs[i].addr);
				return;
			}
		}
		printf("Vq table message received\n");
	}
}

void *snap_dpa_p2p_client_sm(void *arg)
{
	struct dpa_virtq *dpa_queue = arg;
	struct snap_dpa_p2p_q *chan = &g_dpu_rt_thr.dpa_cmd_chan;
	struct snap_dpa_p2p_msg *msgs[64];
	int msgs_rec, i;
	int n = 0;
	int cr_sent = 0;
	uint64_t avail_addr;

	avail_addr = dpa_queue->common.driver + dpa_queue->common.size * sizeof(struct vring_desc);
	snap_dpa_p2p_send_vq_table(chan, dpa_queue->common.idx, 0, 0, 1, avail_addr,
			dpa_queue->dpu_xmkey,
			dpa_queue->common.desc,
			(uint64_t) dpa_queue->dpu_desc_shadow_addr,
			dpa_queue->dpu_desc_shadow_mkey);

	while (/*n < 10000 &&*/ cr_sent < 500) {
		if (!snap_dpa_p2p_send_cr_update(chan, 1))
			cr_sent++;
		n++;
		msgs_rec = snap_dpa_p2p_recv_msg(chan, msgs, 64);
		for(i = 0; i < msgs_rec; i++) {
			chan->credit_count += msgs[i]->base.credit_delta;
			switch(msgs[i]->base.type) {
			case SNAP_DPA_P2P_MSG_CR_UPDATE:
				break;
			case SNAP_DPA_P2P_MSG_VQ_HEADS:
				break;
			case SNAP_DPA_P2P_MSG_VQ_TABLE:
				break;
			case SNAP_DPA_P2P_MSG_VQ_MSIX:
				break;
			default:
				printf("unknown message received in DPA\n");
				break;
			}
		}
	}

	if (cr_sent < 500)
		printf("Error - did not send 500 cr update messages from DPA client to DPU\n");
	else printf("Sent out 500 cr update message from DPA to DPU \n");
	if (!vq_table_rec)
		printf("Error - vq table message not received \n");
	return NULL;
}

void *snap_dpa_p2p_host_sm(void *arg)
{
	struct snap_dpa_virtq *vq = arg;
	struct virtq_split_tunnel_req req;

	while (1) {
		vq->vq.q_ops->poll(&vq->vq, &req, 1);
	}

	return NULL;
}

int main(int argc, char **argv)
{
	struct mlx5dv_context_attr rdma_attr = {};
	int i, n_dev;
	struct ibv_device **dev_list;
	struct ibv_context *ib_ctx = NULL;
	struct snap_dma_q_create_attr m_dma_q_attr = {};
	struct ibv_pd *m_pd;
	struct ibv_comp_channel *m_comp_channel = NULL;
	pthread_t cqthread_dpu;
	int ret;
	void *vr_mem;
	struct dpa_virtq dpa_vq;
	struct snap_dpa_virtq dpu_vq;

	int vr_size = vring_size(256, 16);

	memset(&m_dma_q_attr, 0, sizeof(m_dma_q_attr));
	vr_mem = calloc(1, vr_size);
	if (!vr_mem)
		printf("error vr_mem\n");
	vring_init(&vr, 256, vr_mem, 16);

	m_dma_q_attr.tx_qsize = m_dma_q_attr.rx_qsize = 512;
	m_dma_q_attr.tx_elem_size = 64;
	m_dma_q_attr.rx_elem_size = 64;
	m_dma_q_attr.rx_cb = p2p_rx;
	m_dma_q_attr.mode = SNAP_DMA_Q_MODE_DV;
	m_pd = NULL;
	dev_list = ibv_get_device_list(&n_dev);
	if (!dev_list) {
		printf("Failed to open device list \n");
		goto end;
	}
	for (i = 0; i < n_dev; i++) {
		if (strcmp(ibv_get_device_name(dev_list[i]),
					"mlx5_0") == 0) {
			rdma_attr.flags = MLX5DV_CONTEXT_FLAGS_DEVX;
			ib_ctx = mlx5dv_open_device(dev_list[i], &rdma_attr);
			if (!ib_ctx) {
				printf("Failed to open dev \n");
				goto end;
			}
			m_pd = ibv_alloc_pd(ib_ctx);
			if (!m_pd) {
				printf( "Failed to create PD \n");
				goto end;
			}
			m_comp_channel = ibv_create_comp_channel(ib_ctx);
			if (!m_comp_channel) {
				printf("Failed to created completion channel\n");
				goto end;
			}
			m_dma_q_attr.comp_channel = m_comp_channel;
		}
	}
	if(!ib_ctx)
		goto end;

	dpu_vq.vq.q_ops = get_dpa_queue_ops();

	/* TODO: make loopback runtime */
	dpu_vq.rt_thr = &g_dpu_rt_thr;

	g_dpu_rt_thr.dpa_cmd_chan.dma_q = snap_dma_ep_create(m_pd, &m_dma_q_attr);
	if(!g_dpu_rt_thr.dpa_cmd_chan.dma_q) {
		printf("failed to create dpa dma_q\n");
		goto end;
	}

	g_dpu_rt_thr.dpu_cmd_chan.dma_q = snap_dma_ep_create(m_pd, &m_dma_q_attr);
	if(!g_dpu_rt_thr.dpu_cmd_chan.dma_q) {
		printf("failed to create dpu dma q\n");
		goto end;
	}

	int rc = snap_dma_ep_connect(g_dpu_rt_thr.dpu_cmd_chan.dma_q, g_dpu_rt_thr.dpa_cmd_chan.dma_q);
	if(rc) {
		printf("error connecting dma queues\n");
		goto end;
	}

	g_dpu_rt_thr.dpu_cmd_chan.credit_count = SNAP_DPA_P2P_CREDIT_COUNT;
	g_dpu_rt_thr.dpu_cmd_chan.q_size = DESC_COUNT;

	g_dpu_rt_thr.dpa_cmd_chan.credit_count = SNAP_DPA_P2P_CREDIT_COUNT;
	g_dpu_rt_thr.dpa_cmd_chan.q_size = DESC_COUNT;

	dpu_vq.desc_shadow = calloc(1, sizeof(struct vring_desc) * DESC_COUNT);
	dpu_vq.desc_shadow_mr = snap_reg_mr(m_pd, dpu_vq.desc_shadow, sizeof(struct vring_desc) * DESC_COUNT);
	dpa_vq.dpu_desc_shadow_addr = (uint64_t) dpu_vq.desc_shadow;
	dpa_vq.dpu_desc_shadow_mkey = dpu_vq.desc_shadow_mr->rkey;

	vr_mr = snap_reg_mr(m_pd, vr.desc, vr_size);
	if(!vr_mr) {
		printf("failed to reg_mr descriptor ring\n");
		goto end;
	}

	dpa_vq.dpu_xmkey = vr_mr->rkey;
	dpa_vq.dpa_xmkey = vr_mr->rkey;
	dpa_vq.common.desc = (uint64_t) vr.desc;
	dpa_vq.common.driver = (uint64_t) vr.avail;

	ret = pthread_create(&cqthread_dpu, NULL, snap_dpa_p2p_host_sm, &dpu_vq);
	if (ret) {
		printf("failed to pthread_create snap_dpa_p2p_host_sm\n");
		goto end;
	}

	for (i = 0; i < DESC_COUNT; i++)
		vr.desc[i].addr = i;

	snap_dpa_p2p_client_sm(&dpa_vq);

	printf("Test finished, press ctrl + c to exit \n");
	while (1) {
		sleep(1);
	}
	return 0;

	end:
		return -1;
}
