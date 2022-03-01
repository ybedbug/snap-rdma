#include <limits.h>
#include "gtest/gtest.h"

extern "C" {
#include "snap.h"
#include "snap_qp.h"
#include "snap_dpa.h"
#include "mlx5_ifc.h"
}

#include "tests_common.h"

class SnapQpTest : public ::testing::Test,
	public ::testing::WithParamInterface<int> {
	virtual void SetUp();
	virtual void TearDown();

	protected:
	struct ibv_pd *m_pd;
	public:
	struct ibv_context *get_ib_ctx() { return m_pd->context; }
};

void SnapQpTest::SetUp()
{
	struct mlx5dv_context_attr rdma_attr = {};
	bool init_ok = false;
	int i, n_dev;
	struct ibv_device **dev_list;
	struct ibv_context *ib_ctx;

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
			init_ok = true;
			goto out;
		}
	}
out:
	ibv_free_device_list(dev_list);
	if (!init_ok)
		FAIL() << "Failed to setup " << get_dev_name();
}

void SnapQpTest::TearDown()
{
	struct ibv_context *ib_ctx;

	if (!m_pd)
		return;
	ib_ctx = get_ib_ctx();
	ibv_dealloc_pd(m_pd);
	ibv_close_device(ib_ctx);
}

TEST_P(SnapQpTest, create_cq) {
	struct snap_cq_attr cq_attr = {0};
	int cqe_sizes[] = { 64, 128 };
	struct snap_cq *cq;
	struct snap_hw_cq hw_cq;
	int i, ret;

	cq_attr.cq_type = GetParam();
	printf("cq type = %d\n", cq_attr.cq_type);
	cq_attr.cqe_cnt = 128;

	/* check that we can create 128 */
	cq_attr.cqe_size = 64;

	for (i = 0; i < 2; i++) {
		cq_attr.cqe_size = cqe_sizes[i];
		cq = snap_cq_create(m_pd->context, &cq_attr);
		ASSERT_TRUE(cq);

		ret = snap_cq_to_hw_cq(cq, &hw_cq);
		if (ret) {
			EXPECT_EQ(SNAP_OBJ_VERBS, cq_attr.cq_type);
			EXPECT_EQ(-ENOTSUP, ret);
			EXPECT_TRUE(snap_cq_to_verbs_cq(cq));
		} else {
			EXPECT_EQ(cq_attr.cqe_size, hw_cq.cqe_size);
			if (cq_attr.cq_type == SNAP_OBJ_DEVX) {
				EXPECT_EQ(cq_attr.cqe_cnt, hw_cq.cqe_cnt);
			}
		}
		snap_cq_destroy(cq);
	}
}

TEST_P(SnapQpTest, create_qp) {
	struct snap_cq_attr cq_attr = {0};
	struct snap_qp_attr qp_attr = {0};
	struct snap_cq *rx_cq, *tx_cq;
	struct snap_qp *qp;
	struct snap_hw_qp hw_qp;
	int ret;

	cq_attr.cq_type = qp_attr.qp_type = GetParam();
	cq_attr.cqe_cnt = 1024;

	cq_attr.cqe_size = 64;
	tx_cq = snap_cq_create(m_pd->context, &cq_attr);
	ASSERT_TRUE(tx_cq);

	cq_attr.cqe_size = 128;
	rx_cq = snap_cq_create(m_pd->context, &cq_attr);
	ASSERT_TRUE(rx_cq);

	qp_attr.sq_cq = tx_cq;
	qp_attr.rq_cq = rx_cq;
	qp_attr.sq_max_inline_size = 128;
	qp_attr.sq_max_sge = qp_attr.rq_max_sge = 1;
	qp_attr.sq_size = 128;
	qp_attr.rq_size = 128;

	qp = snap_qp_create(m_pd, &qp_attr);
	ASSERT_TRUE(qp);

	ret = snap_qp_to_hw_qp(qp, &hw_qp);
	EXPECT_EQ(0, ret);

	snap_qp_destroy(qp);
	snap_cq_destroy(rx_cq);
	snap_cq_destroy(tx_cq);
}

TEST_F(SnapQpTest, create_eq_cq_on_dpa) {
	struct snap_cq_attr cq_attr = {0};
	int cqe_sizes[] = { 64, 128 };
	struct snap_cq *cq;
	struct snap_hw_cq hw_cq;
	int ret;
	unsigned i;
	struct snap_dpa_ctx *dpa_ctx;

	if (!snap_dpa_enabled(m_pd->context))
		SKIP_TEST_R("DPA is not available");

	dpa_ctx = snap_dpa_process_create(get_ib_ctx(), "dpa_hello");
	ASSERT_TRUE(dpa_ctx);

	cq_attr.cq_type = SNAP_OBJ_DEVX;
	cq_attr.cqe_cnt = 128;
	/* check that we can create 128 */
	cq_attr.cqe_size = 64;
	cq_attr.cq_on_dpa = true;
	cq_attr.dpa_element_type = MLX5_APU_ELEMENT_TYPE_EQ;
	cq_attr.dpa_proc = dpa_ctx;

	for (i = 0; i < ARRAY_SIZE(cqe_sizes); i++) {
		cq_attr.cqe_size = cqe_sizes[i];
		cq = snap_cq_create(m_pd->context, &cq_attr);
		EXPECT_TRUE(cq);
		if (!cq)
			break;

		ret = snap_cq_to_hw_cq(cq, &hw_cq);
		EXPECT_EQ(0, ret);
		EXPECT_EQ(cq_attr.cqe_size, hw_cq.cqe_size);
		EXPECT_EQ(cq_attr.cqe_cnt, hw_cq.cqe_cnt);
		if (!ret)
			snap_cq_destroy(cq);
	}

	snap_dpa_process_destroy(dpa_ctx);
}

TEST_F(SnapQpTest, create_thread_cq_on_dpa) {
	struct snap_cq_attr cq_attr = {0};
	struct snap_cq *cq;
	struct snap_hw_cq hw_cq;
	int ret;
	struct snap_dpa_ctx *dpa_ctx;
	struct snap_dpa_thread *dpa_thr;

	if (!snap_dpa_enabled(m_pd->context))
		SKIP_TEST_R("DPA is not available");

	/* We create "polling" threads and currently flexio automatically
	 * attaches a cq to them.
	 * TODO: allow creation of both polling and event threads
	 */
	//SKIP_TEST_R("WIP: skipping test");
	dpa_ctx = snap_dpa_process_create(get_ib_ctx(), "dpa_hello");
	ASSERT_TRUE(dpa_ctx);

	dpa_thr = snap_dpa_thread_create(dpa_ctx, 0);
	ASSERT_TRUE(dpa_thr);

	cq_attr.cq_type = SNAP_OBJ_DEVX;
	cq_attr.cqe_cnt = 128;
	cq_attr.cqe_size = 64;
	cq_attr.cq_on_dpa = true;
	cq_attr.dpa_element_type = MLX5_APU_ELEMENT_TYPE_THREAD;
	cq_attr.dpa_thread = dpa_thr;

	cq = snap_cq_create(m_pd->context, &cq_attr);
	ASSERT_TRUE(cq);

	ret = snap_cq_to_hw_cq(cq, &hw_cq);
	EXPECT_EQ(0, ret);
	EXPECT_EQ(cq_attr.cqe_size, hw_cq.cqe_size);
	EXPECT_EQ(cq_attr.cqe_cnt, hw_cq.cqe_cnt);
	if (!ret)
		snap_cq_destroy(cq);

	snap_dpa_thread_destroy(dpa_thr);
	snap_dpa_process_destroy(dpa_ctx);
}

TEST_F(SnapQpTest, create_qp_on_dpa) {
	struct snap_cq_attr cq_attr = {0};
	struct snap_qp_attr qp_attr = {0};
	struct snap_cq *rx_cq, *tx_cq;
	struct snap_qp *qp;
	struct snap_hw_qp hw_qp;
	int ret;
	struct snap_dpa_ctx *dpa_ctx;

	if (!snap_dpa_enabled(m_pd->context))
		SKIP_TEST_R("DPA is not available");

	dpa_ctx = snap_dpa_process_create(get_ib_ctx(), "dpa_hello");
	ASSERT_TRUE(dpa_ctx);

	cq_attr.cq_type = qp_attr.qp_type = SNAP_OBJ_DEVX;
	cq_attr.cqe_cnt = 1024;
	cq_attr.cq_on_dpa = true;
	cq_attr.dpa_element_type = MLX5_APU_ELEMENT_TYPE_EQ;
	cq_attr.dpa_proc = dpa_ctx;

	cq_attr.cqe_size = 64;
	tx_cq = snap_cq_create(m_pd->context, &cq_attr);
	ASSERT_TRUE(tx_cq);

	cq_attr.cqe_size = 128;
	rx_cq = snap_cq_create(m_pd->context, &cq_attr);
	ASSERT_TRUE(rx_cq);

	qp_attr.qp_on_dpa = true;
	qp_attr.dpa_proc = dpa_ctx;
	qp_attr.sq_cq = tx_cq;
	qp_attr.rq_cq = rx_cq;
	qp_attr.sq_max_inline_size = 128;
	qp_attr.sq_max_sge = qp_attr.rq_max_sge = 1;
	qp_attr.sq_size = 128;
	qp_attr.rq_size = 128;

	qp = snap_qp_create(m_pd, &qp_attr);
	ASSERT_TRUE(qp);

	ret = snap_qp_to_hw_qp(qp, &hw_qp);
	EXPECT_EQ(0, ret);

	snap_qp_destroy(qp);
	snap_cq_destroy(rx_cq);
	snap_cq_destroy(tx_cq);

	snap_dpa_process_destroy(dpa_ctx);
}

INSTANTIATE_TEST_SUITE_P(snap, SnapQpTest,
		::testing::Values(SNAP_OBJ_VERBS, SNAP_OBJ_DV, SNAP_OBJ_DEVX));

