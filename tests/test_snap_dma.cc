#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/mman.h>

#include <infiniband/verbs.h>

extern "C" {
#include "snap.h"
#include "snap_nvme.h"
#include "snap_dma.h"
#include "host_uio.h"
};

#include "gtest/gtest.h"
#include "tests_common.h"
#include "test_snap_dma.h"

int SnapDmaTest::snap_dma_q_fw_send_imm(struct snap_dma_q *q, uint32_t imm)
{
	return -ENOTSUP;
}

int SnapDmaTest::snap_dma_q_fw_send(struct snap_dma_q *q, void *src_buf,
		size_t len, uint32_t lkey)
{
	struct ibv_qp *qp = q->fw_qp.qp;
	struct ibv_send_wr *bad_wr;
	struct ibv_send_wr send_wr = {};
	struct ibv_sge send_sgl = {};
	int rc;

	send_sgl.addr = (uint64_t)src_buf;
	send_sgl.length = len;
	send_sgl.lkey = lkey;

	send_wr.wr_id = 0;
	send_wr.next = NULL;
	send_wr.opcode = IBV_WR_SEND;
	send_wr.send_flags = 0;
	send_wr.sg_list = &send_sgl;
	send_wr.num_sge = 1;
	send_wr.imm_data = 0;

	rc = ibv_post_send(qp, &send_wr, &bad_wr);
	if (rc) {
		snap_error("DMA queue %p: FW failed to post send: %m\n", q);
		return rc;
	}

	return 0;
}

void SnapDmaTest::alloc_bufs()
{
	m_bsize = 4096;
	m_bcount = 64;
	m_lbuf = (char *)malloc(m_bcount * m_bsize);
	m_rbuf = (char *)malloc(m_bcount * m_bsize);
	if (!m_lbuf || !m_rbuf)
		FAIL() << "buffer allocation";

	m_lmr = ibv_reg_mr(m_pd, m_lbuf, m_bcount * m_bsize,
			IBV_ACCESS_LOCAL_WRITE);
	if (!m_lmr)
		FAIL() << "local memory buffer";

	m_rmr = ibv_reg_mr(m_pd, m_rbuf, m_bcount * m_bsize,
			IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
			IBV_ACCESS_REMOTE_READ);
	if (!m_rmr)
		FAIL() << "remote memory buffer";
}

void SnapDmaTest::free_bufs()
{
	ibv_dereg_mr(m_rmr);
	ibv_dereg_mr(m_lmr);
	free(m_lbuf);
	free(m_rbuf);
}

static int g_rx_count;
static char g_last_rx[128];

static void dma_rx_cb(struct snap_dma_q *q, void *data, uint32_t data_len,
		uint32_t imm_data)
{
	g_rx_count++;
	memcpy(g_last_rx, data, data_len);
}

void SnapDmaTest::SetUp()
{
	struct mlx5dv_context_attr rdma_attr = {};
	bool init_ok = false;
	int i, n_dev;
	struct ibv_device **dev_list;
	struct ibv_context *ib_ctx;

	/* set to the nvme sqe/cqe size and fw max queue size */
	memset(&m_dma_q_attr, 0, sizeof(m_dma_q_attr));
	m_dma_q_attr.tx_qsize = m_dma_q_attr.rx_qsize = 64;
	m_dma_q_attr.tx_elem_size = 16;
	m_dma_q_attr.rx_elem_size = 64;
	m_dma_q_attr.rx_cb = dma_rx_cb;

	m_pd = NULL;
	dev_list = ibv_get_device_list(&n_dev);
	if (!dev_list)
		FAIL() << "Failed to open device list";

	for (i = 0; i < n_dev; i++) {
		if (strcmp(ibv_get_device_name(dev_list[i]),
					get_dev_name()) == 0) {
			rdma_attr.flags = MLX5DV_CONTEXT_FLAGS_DEVX;
			ib_ctx = mlx5dv_open_device(dev_list[i], &rdma_attr);
			if (!ib_ctx)
				FAIL() << "Failed to open " << dev_list[i];
			m_pd = ibv_alloc_pd(ib_ctx);
			if (!m_pd)
				FAIL() << "Failed to create PD";
			m_comp_channel = ibv_create_comp_channel(ib_ctx);
			if (!m_comp_channel)
				FAIL() << "Failed to created completion channel";
			m_dma_q_attr.comp_channel = m_comp_channel;
			alloc_bufs();
			init_ok = true;
			goto out;
		}
	}

out:
	ibv_free_device_list(dev_list);
	if (!init_ok)
		FAIL() << "Failed to setup " << get_dev_name();
}

void SnapDmaTest::TearDown()
{
	struct ibv_context *ib_ctx;

	if (!m_pd)
		return;
	ib_ctx = m_pd->context;
	free_bufs();
	ibv_destroy_comp_channel(m_comp_channel);
	ibv_dealloc_pd(m_pd);
	ibv_close_device(ib_ctx);
}

struct snap_dma_q *SnapDmaTest::create_queue()
{
	struct snap_dma_q *q;

	q = snap_dma_q_create(m_pd, &m_dma_q_attr);
	if (q) {
		EXPECT_TRUE(q->fw_qp.qp->qp_num == snap_dma_q_get_fw_qp(q)->qp_num);
	}
	return q;
}

TEST_F(SnapDmaTest, ibv_create_ah_test) {
	int ret, attr_mask;
	struct ibv_qp_attr attr = {};
	struct ibv_qp_init_attr init_attr = {};
	struct ibv_ah *ah;
	struct snap_dma_q *q;
	union ibv_gid gid;

	q = create_queue();
	ASSERT_TRUE(q);

	if (!ibv_query_gid(q->fw_qp.qp->context, 1, 0, &gid)) {
		attr_mask = IBV_QP_AV;
		ret = ibv_query_qp(q->fw_qp.qp, &attr, attr_mask, &init_attr);
		ASSERT_TRUE(!ret);

		ah = ibv_create_ah(q->fw_qp.qp->pd, &attr.ah_attr);
		ASSERT_TRUE(ah);

		ibv_destroy_ah(ah);
	}

	snap_dma_q_destroy(q);
}

TEST_F(SnapDmaTest, create_destroy) {
	struct snap_dma_q *q;

	q = create_queue();
	ASSERT_TRUE(q);
	snap_dma_q_destroy(q);

	/* creating another queue after one was destroyed must work */
	q = create_queue();
	ASSERT_TRUE(q);
	snap_dma_q_destroy(q);
}

TEST_F(SnapDmaTest, create_destroy_n) {
	const int N = 16;
	int i;
	struct snap_dma_q *q[N];

	for (i = 0; i < N; i++) {
		q[i] = create_queue();
		ASSERT_TRUE(q[i]);
	}

	for (i = 0; i < N; i++)
		snap_dma_q_destroy(q[i]);
}

static int g_comp_count;
static int g_last_comp_status;

static void dma_completion(struct snap_dma_completion *comp, int status)
{
	g_comp_count++;
	g_last_comp_status = status;
}

void SnapDmaTest::dma_xfer_test(struct snap_dma_q *q, bool is_read, bool poll_mode,
		void *rvaddr, void *rpaddr, uint32_t rkey, int len)
{
	struct snap_dma_completion comp;
	int n, rc;

	comp.func = dma_completion;
	comp.count = 1;

	if (!poll_mode) {
		ASSERT_EQ(0, snap_dma_q_arm(q));
	}

	g_comp_count = 0;

	if (is_read) {
		memset(m_lbuf, 0, len);
		memset(rvaddr, 0xED, len);
		rc = snap_dma_q_read(q, m_lbuf, m_bsize, m_lmr->lkey,
				(uintptr_t)rpaddr, rkey, &comp);
	} else {
		memset(rvaddr, 0, len);
		memset(m_lbuf, 0xED, len);
		rc = snap_dma_q_write(q, m_lbuf, m_bsize, m_lmr->lkey,
				(uintptr_t)rpaddr, rkey, &comp);
	}
	ASSERT_EQ(0, rc);

	if (!poll_mode) {
		struct ibv_cq *cq;
		void *cq_context;
		struct pollfd pfd;

		pfd.fd = m_comp_channel->fd;
		pfd.events = POLLIN;
		pfd.revents = 0;
		ASSERT_EQ(1, poll(&pfd, 1, 1000));

		ASSERT_EQ(0, ibv_get_cq_event(m_comp_channel, &cq, &cq_context));
		ASSERT_TRUE(cq == q->sw_qp.tx_cq);
		snap_dma_q_progress(q);
		ibv_ack_cq_events(cq, 1);
	} else {
		n = 0;
		while (n < 10000) {
			snap_dma_q_progress(q);
			if (g_comp_count == 1)
				break;
			n++;
		}
	}

	ASSERT_EQ(1, g_comp_count);
	ASSERT_EQ(0, g_last_comp_status);
	ASSERT_EQ(0, comp.count);
	ASSERT_EQ(0, memcmp(m_lbuf, rvaddr, len));
}

TEST_F(SnapDmaTest, dma_read) {
	struct snap_dma_q *q;

	q = snap_dma_q_create(m_pd, &m_dma_q_attr);
	ASSERT_TRUE(q);

	dma_xfer_test(q, true, false, m_rbuf, m_rbuf, m_rmr->lkey, m_bsize);
	dma_xfer_test(q, true, true, m_rbuf, m_rbuf, m_rmr->lkey, m_bsize);

	snap_dma_q_destroy(q);
}

TEST_F(SnapDmaTest, dma_write) {
	struct snap_dma_q *q;

	q = snap_dma_q_create(m_pd, &m_dma_q_attr);
	ASSERT_TRUE(q);

	dma_xfer_test(q, false, false, m_rbuf, m_rbuf, m_rmr->lkey, m_bsize);
	dma_xfer_test(q, false, true, m_rbuf, m_rbuf, m_rmr->lkey, m_bsize);

	snap_dma_q_destroy(q);
}

TEST_F(SnapDmaTest, dma_write_short) {
	struct snap_dma_q *q;
	char cqe[m_dma_q_attr.tx_elem_size];
	int rc;
	int n, saved_tx_available;

	q = snap_dma_q_create(m_pd, &m_dma_q_attr);
	ASSERT_TRUE(q);

	saved_tx_available = q->tx_available;

	memset(m_rbuf, 0, sizeof(cqe));
	memset(cqe, 0xDA, sizeof(cqe));

	rc = snap_dma_q_write_short(q, cqe, sizeof(cqe), (uintptr_t)m_rbuf,
			            m_rmr->lkey);
	ASSERT_EQ(0, rc);
	n = 0;
	while (q->tx_available < saved_tx_available && n < 10000) {
		snap_dma_q_progress(q);
		n++;
	}

	ASSERT_EQ(0, memcmp(cqe, m_rbuf, sizeof(cqe)));
	snap_dma_q_flush(q);
	ASSERT_EQ(saved_tx_available, q->tx_available);

	snap_dma_q_destroy(q);
}

TEST_F(SnapDmaTest, send_completion) {
	struct snap_dma_q *q;
	char cqe[m_dma_q_attr.tx_elem_size];
	int rc;
	int n, saved_tx_available;
	struct ibv_recv_wr rx_wr, *bad_wr;
	struct ibv_sge rx_sge;

	q = snap_dma_q_create(m_pd, &m_dma_q_attr);
	ASSERT_TRUE(q);

	saved_tx_available = q->tx_available;

	/* post one recv to the fw qp so that send can complete */
	rx_sge.addr = (uintptr_t)m_rbuf;
	rx_sge.length = m_dma_q_attr.tx_elem_size;
	rx_sge.lkey = m_rmr->lkey;
	rx_wr.next = NULL;
	rx_wr.sg_list = &rx_sge;
	rx_wr.num_sge = 1;

	rc = ibv_post_recv(q->fw_qp.qp, &rx_wr, &bad_wr);
	ASSERT_EQ(0, rc);

	memset(m_rbuf, 0, sizeof(cqe));
	memset(cqe, 0xDA, sizeof(cqe));

	rc = snap_dma_q_send_completion(q, cqe, sizeof(cqe));
	ASSERT_EQ(0, rc);
	n = 0;
	while (q->tx_available < saved_tx_available && n < 10000) {
		snap_dma_q_progress(q);
		n++;
	}
	/* check that send was actually received */
	struct ibv_wc wc;
	rc = ibv_poll_cq(q->fw_qp.rx_cq, 1, &wc);

	ASSERT_EQ(0, memcmp(cqe, m_rbuf, sizeof(cqe)));
	ASSERT_EQ(1, rc);

	snap_dma_q_flush(q);
	ASSERT_EQ(saved_tx_available, q->tx_available);

	snap_dma_q_destroy(q);
}

TEST_F(SnapDmaTest, flush) {
	struct snap_dma_q *q;
	int rc;
	char cqe[m_dma_q_attr.tx_elem_size];
	struct ibv_recv_wr rx_wr, *bad_wr;
	struct ibv_sge rx_sge;

	q = snap_dma_q_create(m_pd, &m_dma_q_attr);
	ASSERT_TRUE(q);

	rx_sge.addr = (uintptr_t)m_rbuf;
	rx_sge.length = m_dma_q_attr.tx_elem_size;
	rx_sge.lkey = m_rmr->lkey;
	rx_wr.next = NULL;
	rx_wr.sg_list = &rx_sge;
	rx_wr.num_sge = 1;
	rc = ibv_post_recv(q->fw_qp.qp, &rx_wr, &bad_wr);
	ASSERT_EQ(0, rc);

	rc = snap_dma_q_flush(q);
	/* no outstanding requests */
	ASSERT_EQ(0, rc);

	rc = snap_dma_q_read(q, m_lbuf, m_bsize, m_lmr->lkey,
			(uintptr_t)m_rbuf, m_rmr->lkey, NULL);
	ASSERT_EQ(0, rc);
	rc = snap_dma_q_write(q, m_lbuf, m_bsize, m_lmr->lkey,
			(uintptr_t)m_rbuf, m_rmr->lkey, NULL);
	ASSERT_EQ(0, rc);
	rc = snap_dma_q_send_completion(q, cqe, sizeof(cqe));
	ASSERT_EQ(0, rc);
	rc = snap_dma_q_flush(q);
	/* 3 outstanding requests */
	ASSERT_EQ(3, rc);

	snap_dma_q_destroy(q);
}

TEST_F(SnapDmaTest, rx_callback) {
	struct snap_dma_q *q;
	char *sqe = m_rbuf;
	int rc;

	q = snap_dma_q_create(m_pd, &m_dma_q_attr);
	ASSERT_TRUE(q);

	memset(sqe, 0xDA, m_dma_q_attr.rx_elem_size);

	g_rx_count = 0;
	rc = snap_dma_q_fw_send(q, sqe, m_dma_q_attr.rx_elem_size, m_rmr->lkey);
	ASSERT_EQ(0, rc);

	sleep(1);
	snap_dma_q_progress(q);

	ASSERT_EQ(1, g_rx_count);
	ASSERT_EQ(0, memcmp(g_last_rx, sqe, m_dma_q_attr.rx_elem_size));
	snap_dma_q_destroy(q);
}

TEST_F(SnapDmaTest, error_checks) {
	char data[4096];
	struct snap_dma_q *q;
	int rc;

	/* we should not be able to create q if
	 * tx cannot be done inline */
	m_dma_q_attr.tx_elem_size = 4096;
	q = snap_dma_q_create(m_pd, &m_dma_q_attr);
	ASSERT_FALSE(q);

	/**
	 * Should get an error when trying to send something
	 * that cannot be sent inline
	 */
	m_dma_q_attr.tx_elem_size = 64;
	q = snap_dma_q_create(m_pd, &m_dma_q_attr);
	ASSERT_TRUE(q);
	rc = snap_dma_q_send_completion(q, data, sizeof(data));
	ASSERT_NE(0, rc);
}

/* Test only works on CX6 DX because it assumes that our local
 * memory is accessible via cross function mkey
 */
TEST_F(SnapDmaTest, xgvmi_mkey) {
	struct snap_context *sctx = NULL;
	struct snap_device_attr attr = {};
	struct snap_nvme_cq_attr cq_attr = {};
	struct snap_nvme_sq_attr sq_attr = {};
	struct snap_nvme_cq *cq;
	struct snap_nvme_sq *sq;
	struct ibv_device **dev_list;
	int i, n_dev;
	struct snap_device *sdev;
	struct snap_dma_q *q;
	uint32_t xgvmi_rkey;
	struct snap_cross_mkey *mkey;
	struct snap_nvme_device_attr nvme_device_attr = {};

	/* TODO: check that test is actually working and limit it to
	 * the CX6 DX
	 */
	SKIP_TEST_R("Under construction");
	ASSERT_EQ(0, host_uio_dma_init());
	/* open snap ctx */
	dev_list = ibv_get_device_list(&n_dev);
	ASSERT_TRUE(dev_list);

	for (i = 0; i < n_dev; i++) {
		sctx = snap_open(dev_list[i]);
		if (sctx)
			break;
	}

	ASSERT_TRUE(sctx);
	printf("snap manager is %s\n", dev_list[i]->name);
	ibv_free_device_list(dev_list);

	/* create cq/sq */
	attr.type = SNAP_NVME_PF;
	attr.pf_id = 0;
	sdev = snap_open_device(sctx, &attr);
	ASSERT_TRUE(sdev);
	ASSERT_EQ(0, snap_nvme_init_device(sdev));

	cq_attr.type = SNAP_NVME_RAW_MODE;
	cq_attr.id = 0;
	cq_attr.msix = 0;
	cq_attr.queue_depth = 16;
	cq_attr.base_addr = 0xdeadbeef;
	cq = snap_nvme_create_cq(sdev, &cq_attr);
	ASSERT_TRUE(cq);

	sq_attr.type = SNAP_NVME_RAW_MODE;
	sq_attr.id = 0;
	sq_attr.queue_depth = 16;
	sq_attr.base_addr = 0xbeefdead;
	sq_attr.cq = cq;
	sq = snap_nvme_create_sq(sdev, &sq_attr);
	ASSERT_TRUE(sq);

	q = snap_dma_q_create(m_pd, &m_dma_q_attr);
	ASSERT_TRUE(q);

	memset(&sq_attr, 0, sizeof(sq_attr));
	sq_attr.qp = snap_dma_q_get_fw_qp(q);
	sq_attr.state = SNAP_NVME_SQ_STATE_RDY;

	// not working yet
	ASSERT_EQ(0, snap_nvme_modify_sq(sq,
				SNAP_NVME_SQ_MOD_STATE |
				SNAP_NVME_SQ_MOD_QPN,
				&sq_attr));

	ASSERT_EQ(0, snap_nvme_query_device(sdev, &nvme_device_attr));
	mkey = snap_create_cross_mkey(m_pd, sdev);
	ASSERT_TRUE(mkey);

	xgvmi_rkey = mkey->mkey;
	ASSERT_NE(0U, xgvmi_rkey);

	void *va;
	uintptr_t pa;

	va = host_uio_dma_alloc(m_bsize, &pa);
	ASSERT_TRUE(va);

	/* read data. */
	dma_xfer_test(q, true, true, va, (void *)pa, xgvmi_rkey, m_bsize);
	dma_xfer_test(q, false, true, va, (void *)pa, xgvmi_rkey, m_bsize);

	host_uio_dma_free(va);
	snap_destroy_cross_mkey(mkey);
	snap_dma_q_destroy(q);
	snap_nvme_destroy_sq(sq);
	snap_nvme_destroy_cq(cq);
	snap_nvme_teardown_device(sdev);
	snap_close_device(sdev);
	snap_close(sctx);
	host_uio_dma_destroy();
}

TEST_F(SnapDmaTest, create_destory_indirect_mkey_0) {
	struct snap_indirect_mkey *klm_mkey;
	struct mlx5_devx_mkey_attr mkey_attr = {};

	mkey_attr.addr = 0;
	mkey_attr.size = 0;
	mkey_attr.log_entity_size = 0;
	mkey_attr.relaxed_ordering_write = 0;
	mkey_attr.relaxed_ordering_read = 0;
	mkey_attr.klm_array = NULL;
	mkey_attr.klm_num = 0;

	klm_mkey = snap_create_indirect_mkey(m_pd, &mkey_attr);
	ASSERT_TRUE(klm_mkey);

	snap_destroy_indirect_mkey(klm_mkey);
}

TEST_F(SnapDmaTest, create_destory_indirect_mkey_1) {
	struct snap_indirect_mkey *klm_mkey;
	struct mlx5_devx_mkey_attr mkey_attr = {};
	struct mlx5_klm *klm;
	char *buf;
	struct ibv_mr *mr;

	buf = (char *)malloc(4096);
	if (!buf)
		FAIL() << "buffer allocation";

	mr = ibv_reg_mr(m_pd, buf, 4096, IBV_ACCESS_LOCAL_WRITE);
	if (!mr)
		FAIL() << "memory register";

	klm = (struct mlx5_klm *)malloc(sizeof(*klm));
	if (!klm)
		FAIL() << "klm buffer allocation";

	klm->byte_count = mr->length;
	klm->mkey = mr->lkey;
	klm->address = (uintptr_t)mr->addr;

	mkey_attr.addr = klm->address;
	mkey_attr.size = klm->byte_count;
	mkey_attr.log_entity_size = 0;
	mkey_attr.relaxed_ordering_write = 0;
	mkey_attr.relaxed_ordering_read = 0;
	mkey_attr.klm_array = klm;
	mkey_attr.klm_num = 1;

	klm_mkey = snap_create_indirect_mkey(m_pd, &mkey_attr);
	ASSERT_TRUE(klm_mkey);

	snap_destroy_indirect_mkey(klm_mkey);
	ibv_dereg_mr(mr);
	free(buf);
}

static int g_umr_wqe_comp;

static void post_umr_completion(struct snap_dma_completion *comp, int status)
{
	g_umr_wqe_comp++;
	g_last_comp_status = status;
}

static void post_umr_wqe(struct snap_dma_q *q,
		struct ibv_pd *pd, int bsize, bool wait_umr_complete)
{
#define MTT_ENTRIES  5
	struct snap_indirect_mkey *klm_mkey;
	struct mlx5_devx_mkey_attr mkey_attr = {};
	int i, j, n, ret, n_bb;
	char *lbuf[MTT_ENTRIES], *rbuf;
	struct ibv_mr *lmr[MTT_ENTRIES], *rmr;
	struct mlx5_klm *klm_mtt;
	struct snap_dma_completion comp;

	for (i = 0; i < MTT_ENTRIES; i++) {
		lbuf[i] = (char *)malloc(bsize);
		if (!lbuf[i])
			FAIL() << "local buffer allocation";

		lmr[i] = ibv_reg_mr(pd, lbuf[i], bsize, IBV_ACCESS_LOCAL_WRITE |
				IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
		if (!lmr[i])
			FAIL() << "local memory register";
	}

	rbuf = (char *)malloc(MTT_ENTRIES * bsize);
	if (!rbuf)
		FAIL() << "remote buffer allocation";

	rmr = ibv_reg_mr(pd, rbuf, MTT_ENTRIES * bsize, IBV_ACCESS_LOCAL_WRITE |
			IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
	if (!rmr)
		FAIL() << "remote memory register";

	mkey_attr.addr = 0;
	mkey_attr.size = 0;
	mkey_attr.log_entity_size = 0;
	mkey_attr.relaxed_ordering_write = 0;
	mkey_attr.relaxed_ordering_read = 0;
	mkey_attr.klm_array = NULL;
	mkey_attr.klm_num = 0;

	klm_mkey = snap_create_indirect_mkey(pd, &mkey_attr);
	ASSERT_TRUE(klm_mkey);

	klm_mtt = (struct mlx5_klm *)malloc(MTT_ENTRIES * sizeof(*klm_mtt));
	if (!klm_mtt)
		FAIL() << "klm mtt allocation";

	for (i = 0; i < MTT_ENTRIES; i++) {
		klm_mtt[i].byte_count = lmr[i]->length;
		klm_mtt[i].mkey = lmr[i]->lkey;
		klm_mtt[i].address = (uintptr_t)lmr[i]->addr;
	}

	if (wait_umr_complete) {
		comp.func = post_umr_completion;
		comp.count = 1;
		g_umr_wqe_comp = 0;
		ret = snap_dma_q_post_umr_wqe(q, klm_mtt, MTT_ENTRIES, klm_mkey, &comp, &n_bb);
		ASSERT_EQ(ret, 0);

		n = 0;
		while (n < 10000) {
			ret = snap_dma_q_progress(q);
			if (ret == 1)
				break;
			n++;
		}
		ASSERT_EQ(1, g_umr_wqe_comp);
		ASSERT_EQ(0, g_last_comp_status);
	} else {
		ret = snap_dma_q_post_umr_wqe(q, klm_mtt, MTT_ENTRIES, klm_mkey, NULL, &n_bb);
		ASSERT_EQ(ret, 0);
	}

	for (j = 0; j < 2; j++) {
		comp.func = dma_completion;
		comp.count = 1;
		g_comp_count = 0;

		if (j == 0) { /* read test */
			for (i = 0; i < MTT_ENTRIES; i++) {
				memset(rbuf + i * bsize, 'A' + i, bsize);
				memset(lbuf[i], 0, bsize);
			}

			ret = snap_dma_q_read(q, (void *)klm_mkey->addr,
					MTT_ENTRIES * bsize, klm_mkey->mkey, (uintptr_t)rbuf, rmr->lkey, &comp);
		} else { /* write test */
			memset(rbuf, 0,  MTT_ENTRIES * bsize);
			for (i = 0; i < MTT_ENTRIES; i++)
				memset(lbuf[i], 'a' + i, bsize);

			ret = snap_dma_q_write(q, (void *)klm_mkey->addr,
					MTT_ENTRIES * bsize, klm_mkey->mkey, (uintptr_t)rbuf, rmr->lkey, &comp);
		}
		ASSERT_EQ(ret, 0);

		n = 0;
		while (n < 10000) {
			ret = snap_dma_q_progress(q);
			if (g_comp_count == 1)
				break;
			n++;
		}

		ASSERT_EQ(1, g_comp_count);
		ASSERT_EQ(0, g_last_comp_status);
		ASSERT_EQ(0, comp.count);
		for (i = 0; i < MTT_ENTRIES; i++)
			ASSERT_EQ(0, memcmp(rbuf + i * bsize, lbuf[i], bsize));
	}

	free(klm_mtt);
	ibv_dereg_mr(rmr);
	free(rbuf);
	for (i = 0; i < MTT_ENTRIES; i++) {
		ibv_dereg_mr(lmr[i]);
		free(lbuf[i]);
	}
	snap_destroy_indirect_mkey(klm_mkey);
}

TEST_F(SnapDmaTest, post_umr_wqe_no_wait_complete) {
	struct snap_dma_q *q;

	m_dma_q_attr.mode = SNAP_DMA_Q_MODE_DV;
	q = snap_dma_q_create(m_pd, &m_dma_q_attr);
	ASSERT_TRUE(q);

	post_umr_wqe(q, m_pd, m_bsize, false);

	snap_dma_q_destroy(q);
}

TEST_F(SnapDmaTest, post_umr_wqe_wait_complete) {
	struct snap_dma_q *q;

	m_dma_q_attr.mode = SNAP_DMA_Q_MODE_DV;
	q = snap_dma_q_create(m_pd, &m_dma_q_attr);
	ASSERT_TRUE(q);

	post_umr_wqe(q, m_pd, m_bsize, true);

	snap_dma_q_destroy(q);
}

TEST_F(SnapDmaTest, post_umr_wqe_warp_around_no_wait_complete) {
	struct snap_dma_q *q;
	struct snap_dv_qp *dv_qp;
	int i;

	m_dma_q_attr.mode = SNAP_DMA_Q_MODE_DV;
	q = snap_dma_q_create(m_pd, &m_dma_q_attr);
	ASSERT_TRUE(q);

	/*
	 * post enough RDMA READ wqe to make SQ only left 3 WQE BB to the end.
	 * Because the UMR WQE used below need consume 4 WQE BB, so it will warp around.
	 */
	dv_qp = &q->sw_qp.dv_qp;
	for (i = 0; i < (int)dv_qp->qp.sq.wqe_cnt - 3; i++)
		dma_xfer_test(q, true, false, m_rbuf, m_rbuf, m_rmr->lkey, m_bsize);

	post_umr_wqe(q, m_pd, m_bsize, false);

	snap_dma_q_destroy(q);
}

TEST_F(SnapDmaTest, post_umr_wqe_warp_around_wait_complete) {
	struct snap_dma_q *q;
	struct snap_dv_qp *dv_qp;
	int i;

	m_dma_q_attr.mode = SNAP_DMA_Q_MODE_DV;
	q = snap_dma_q_create(m_pd, &m_dma_q_attr);
	ASSERT_TRUE(q);

	/*
	 * post enough RDMA READ wqe to make SQ only left 3 WQE BB to the end.
	 * Because the UMR WQE used below need consume 4 WQE BB, so it will warp around.
	 */
	dv_qp = &q->sw_qp.dv_qp;
	for (i = 0; i < (int)dv_qp->qp.sq.wqe_cnt - 3; i++)
		dma_xfer_test(q, true, false, m_rbuf, m_rbuf, m_rmr->lkey, m_bsize);

	post_umr_wqe(q, m_pd, m_bsize, true);

	snap_dma_q_destroy(q);
}

TEST_F(SnapDmaTest, post_umr_wqe_reuse_wait_complete) {
	struct snap_dma_q *q;
	struct snap_dv_qp *dv_qp;
	int i;

	m_dma_q_attr.mode = SNAP_DMA_Q_MODE_DV;
	q = snap_dma_q_create(m_pd, &m_dma_q_attr);
	ASSERT_TRUE(q);

	/*
	 * first post wqe_cnt RDMA READ wqe to occupied the whole SQ buffer,
	 * then post umr wqe to let it reuse the WQE BBs which hold dirty data.
	 */
	dv_qp = &q->sw_qp.dv_qp;
	for (i = 0; i < (int)dv_qp->qp.sq.wqe_cnt; i++)
		dma_xfer_test(q, true, false, m_rbuf, m_rbuf, m_rmr->lkey, m_bsize);

	post_umr_wqe(q, m_pd, m_bsize, true);

	snap_dma_q_destroy(q);
}

TEST_F(SnapDmaTest, post_umr_wqe_reuse_no_wait_complete) {
	struct snap_dma_q *q;
	struct snap_dv_qp *dv_qp;
	int i;

	m_dma_q_attr.mode = SNAP_DMA_Q_MODE_DV;
	q = snap_dma_q_create(m_pd, &m_dma_q_attr);
	ASSERT_TRUE(q);

	/*
	 * first post wqe_cnt RDMA READ wqe to occupied the whole SQ buffer,
	 * then post umr wqe to let it reuse the WQE BBs which hold dirty data.
	 */
	dv_qp = &q->sw_qp.dv_qp;
	for (i = 0; i < (int)dv_qp->qp.sq.wqe_cnt; i++)
		dma_xfer_test(q, true, false, m_rbuf, m_rbuf, m_rmr->lkey, m_bsize);

	post_umr_wqe(q, m_pd, m_bsize, false);

	snap_dma_q_destroy(q);
}
