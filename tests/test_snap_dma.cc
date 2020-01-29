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
			ib_ctx = ibv_open_device(dev_list[i]);
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
	if (q)
		EXPECT_TRUE(q->fw_qp.qp->qp_num == snap_dma_q_get_fw_qpnum(q));
	return q;
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

TEST_F(SnapDmaTest, send_completion) {
	struct snap_dma_q *q;
	char cqe[m_dma_q_attr.tx_elem_size];
	int rc;
	int n;
	struct ibv_recv_wr rx_wr, *bad_wr;
	struct ibv_sge rx_sge;

	q = snap_dma_q_create(m_pd, &m_dma_q_attr);
	ASSERT_TRUE(q);

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
	while (q->tx_available < m_dma_q_attr.tx_qsize && n < 10000) {
		snap_dma_q_progress(q);
		n++;
	}
	/* check that send was actually received */
	struct ibv_wc wc;
	rc = ibv_poll_cq(q->fw_qp.rx_cq, 1, &wc);

	ASSERT_EQ(m_dma_q_attr.tx_qsize, q->tx_available);
	ASSERT_EQ(0, memcmp(cqe, m_rbuf, sizeof(cqe)));
	ASSERT_EQ(1, rc);

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

	/* TODO: check that test is actually working and limit it to
	 * the CX6 DX
	 */
	SKIP_TEST_R("Under construction");
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

	cq_attr.type = SNAP_NVME_SQE_MODE;
	cq_attr.id = 0;
	cq_attr.doorbell_offset = 4;
	cq_attr.msix = 0;
	cq_attr.queue_depth = 16;
	cq_attr.base_addr = 0xdeadbeef;
	cq = snap_nvme_create_cq(sdev, &cq_attr);
	ASSERT_TRUE(cq);

	sq_attr.type = SNAP_NVME_SQE_MODE;
	sq_attr.id = 0;
	sq_attr.doorbell_offset = 0;
	sq_attr.queue_depth = 16;
	sq_attr.base_addr = 0xbeefdead;
	sq_attr.cq = cq;
	sq = snap_nvme_create_sq(sdev, &sq_attr);
	ASSERT_TRUE(sq);

	q = snap_dma_q_create(m_pd, &m_dma_q_attr);
	ASSERT_TRUE(q);

	memset(&sq_attr, 0, sizeof(sq_attr));
	sq_attr.qpn = snap_dma_q_get_fw_qpnum(q);
	sq_attr.state = SNAP_NVME_SQ_STATE_RDY;

	// not working yet
	ASSERT_EQ(0, snap_nvme_modify_sq(sq,
				SNAP_NVME_SQ_MOD_STATE |
				SNAP_NVME_SQ_MOD_QPN,
				&sq_attr));

	memset(&sq_attr, 0, sizeof(sq_attr));
	ASSERT_EQ(0, snap_nvme_query_sq(sq, &sq_attr));

	printf("sq dma_key=0x%x state=0x%x, mod_fields=0x%lx\n",
			sq_attr.emulated_device_dma_mkey,
			sq_attr.state,
			sq_attr.modifiable_fields);

	xgvmi_rkey = sq_attr.emulated_device_dma_mkey;
	ASSERT_NE(0, xgvmi_rkey);

	pmem_block hmem;

	ASSERT_EQ(0, hmem.alloc(m_bsize));
	/* read data. */
	dma_xfer_test(q, true, false, hmem.va_base, (void *)hmem.pa_base,
			xgvmi_rkey, m_bsize);
	dma_xfer_test(q, false, false, hmem.va_base, (void *)hmem.pa_base,
			xgvmi_rkey, m_bsize);

	snap_dma_q_destroy(q);
	snap_nvme_destroy_sq(sq);
	snap_nvme_destroy_cq(cq);
	snap_nvme_teardown_device(sdev);
	snap_close_device(sdev);
	snap_close(sctx);
}

/* quick & dirty heap pages allocator, no error
 * checking and free.
 */
void *pmem_block::heap_base = MAP_FAILED;
void *pmem_block::heap_top = NULL;
void *pmem_block::heap_end = NULL;

void pmem_block::dma_init()
{
	heap_base = mmap(NULL, HUGE_PAGE_SIZE,
			PROT_READ|PROT_WRITE,
			MAP_PRIVATE|MAP_ANONYMOUS|MAP_LOCKED|
			/* MAP_HUGE_2MB is used in *conjunction* with MAP_HUGETLB
			 * to select alternative hugetlb page sizes */
#ifdef MAP_HUGE_2MB
			MAP_HUGETLB|MAP_HUGE_2MB,
#else
			MAP_HUGETLB,
#endif
			-1, 0);
	if (heap_base == MAP_FAILED) {
		printf("Huge page allocation failed errno (%d) %m\n"
				"Check that hugepages are configured and 'ulimit -l'\n", errno);
		FAIL();
	}
	heap_top = heap_base;
	heap_end = (char *)heap_base + HUGE_PAGE_SIZE;
}

void pmem_block::dma_destroy()
{
	if (heap_base == MAP_FAILED)
		return;
	munmap(heap_base, HUGE_PAGE_SIZE);
	heap_base = heap_top = heap_end = NULL;
}

void *pmem_block::dma_page_alloc()
{
	void *page;

	if (heap_base == MAP_FAILED)
		dma_init();

	page = heap_top;
	heap_top = (char *)heap_top + SMALL_PAGE_SIZE;
	if (heap_top > heap_end) {
		printf("Out of DMA memory");
		return NULL;
	}
	return page;
}

void pmem_block::free()
{
	if (!va_base)
		return;

	va_base = NULL;
}

int pmem_block::alloc(size_t size)
{
	va_base = NULL;
	blk_size = size;
	if (size > SMALL_PAGE_SIZE) {
		printf("block of size %d cannot be physically contiguos\n",
				(int)size);
		return -EINVAL;
	}

	va_base = (char *)dma_page_alloc();
	if (!va_base)
		return -ENOMEM;

	pa_base = virt_to_phys((uintptr_t)va_base);
	if (pa_base == (uintptr_t)(-1)) {
		printf("cannot find phys address of the %p\n", va_base);
		return -EINVAL;
	}
	memset(va_base, 0, size);
	return 0;
}

void pmem_block::dump() {
	printf("va %p curr_pa 0x%llx real_pa 0x%llx\n",
			va_base, (unsigned long long)pa_base,
			(unsigned long long)virt_to_phys((uintptr_t)va_base));
}

uintptr_t pmem_block::virt_to_phys(uintptr_t address)
{
	static const char *pagemap_file = "/proc/self/pagemap";
	const size_t page_size = sysconf(_SC_PAGESIZE);
	uint64_t entry, pfn;
	ssize_t offset, ret;
	uintptr_t pa;
	int fd;

	/* See https://www.kernel.org/doc/Documentation/vm/pagemap.txt */
	fd = open(pagemap_file, O_RDONLY);
	if (fd < 0) {
		printf("failed to open %s: %m\n", pagemap_file);
		pa = -1;
		goto out;
	}

	offset = (address / page_size) * sizeof(entry);
	ret = lseek(fd, offset, SEEK_SET);
	if (ret != offset) {
		printf("failed to seek in %s to offset %zu: %m\n",
				pagemap_file, offset);
		pa = -1;
		goto out_close;
	}

	ret = read(fd, &entry, sizeof(entry));
	if (ret != sizeof(entry)) {
		printf("read from %s at offset %zu returned %ld: %m\n",
				pagemap_file, offset, ret);
		pa = -1;
		goto out_close;
	}

	if (entry & (1ULL << 63)) {
		pfn = entry & ((1ULL << 54) - 1);
		if (!pfn) {
			printf("%p got pfn zero. Check CAP_SYS_ADMIN permissions\n",
					(void *)address);
			pa = -1;
			goto out_close;
		}
		pa = (pfn * page_size) | (address & (page_size - 1));
	} else {
		printf("%p is not present: %s\n", (void *)address,
				entry & (1ULL << 62) ? "swapped" : "oops");
		pa = -1; /* Page not present */
	}

out_close:
	close(fd);
out:
	return pa;
}