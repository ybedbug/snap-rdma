#include <stdio.h>

#include <infiniband/verbs.h>

#include "snap.h"
#include "snap_nvme.h"
#include "snap_test.h"

static struct ibv_qp *snap_create_qp(struct ibv_pd *pd, struct ibv_cq *cq)
{
	struct ibv_qp_init_attr qp_attr = {};

	qp_attr.qp_type = IBV_QPT_RC;
	qp_attr.send_cq = cq;
	qp_attr.recv_cq = cq;
	qp_attr.cap.max_send_sge = 1;
	qp_attr.cap.max_recv_sge = 1;
	qp_attr.cap.max_send_wr = 16;
	qp_attr.cap.max_recv_wr = 16;

	return ibv_create_qp(pd, &qp_attr);
}

static void free_ib_resources(struct ibv_cq *ibcq, struct ibv_pd *pd)
{
	ibv_dealloc_pd(pd);
	ibv_destroy_cq(ibcq);
}

static int alloc_ib_resources(struct ibv_context *context, struct ibv_cq **cq,
			      struct ibv_pd **pd)
{
	struct ibv_cq *ibcq;
	struct ibv_pd *ibpd;
	int ret = 0;

	ibcq = ibv_create_cq(context, 1024, NULL, NULL, 0);
	if (!ibcq)
		goto out_err;

	ibpd = ibv_alloc_pd(context);
	if (!ibpd)
		goto out_free_cq;

	*cq = ibcq;
	*pd = ibpd;

	return ret;

out_free_cq:
	ibv_destroy_cq(ibcq);
out_err:
	return -ENOMEM;
}

int main(int argc, char **argv)
{
	struct snap_context *sctx;
	int ret = 0, opt, num_queues = 10, depth = 16;
	enum snap_nvme_queue_type q_type = SNAP_NVME_RAW_MODE;
	bool query = false, modify = false;
	struct snap_device_attr attr = {};
	struct ibv_cq *ibcq;
	struct ibv_pd *pd;
	struct snap_device *sdev;
	int j;

	while ((opt = getopt(argc, argv, "mqn:t:d:")) != -1) {
		switch (opt) {
		case 'm':
			modify = true;
			break;
		case 'q':
			query = true;
			break;
		case 'n':
			num_queues = atoi(optarg);
			break;
		case 'd':
			depth = atoi(optarg);
			break;
		case 't':
			if (!strcmp(optarg, "raw"))
				q_type = SNAP_NVME_RAW_MODE;
			else if (!strcmp(optarg, "nvmf"))
				q_type = SNAP_NVME_TO_NVMF_MODE;
			break;
		default:
			printf("Usage: snap_create_destroy_nvme_sq -n <num> -t <type: raw, nvmf> [m(odify) q(uery)]\n");
			exit(1);
		}
	}

	sctx = snap_ctx_open(SNAP_NVME);
	if (!sctx) {
		fprintf(stderr, "failed to open snap ctx for NVMe type\n");
		fflush(stderr);
		ret = -errno;
		goto out;
	}

	ret = alloc_ib_resources(sctx->context, &ibcq, &pd);
	if (ret) {
		fprintf(stderr, "failed to alloc ib resources\n");
		fflush(stderr);
		goto out_free_ctx;
	}

	attr.type = SNAP_NVME_PF;
	attr.pf_id = 0;
	sdev = snap_open_device(sctx, &attr);
	if (sdev) {
		ret = snap_nvme_init_device(sdev);
		if (!ret) {
			fprintf(stdout, "created NVMe dev for pf %d. creating %d sqs and cqs type %d and depth %d\n",
				attr.pf_id, num_queues, q_type, depth);
			fflush(stdout);
			for (j = 1; j < num_queues + 1; j++) {
				struct snap_nvme_cq_attr cq_attr = {};
				struct snap_nvme_cq *cq;
				struct snap_nvme_sq_attr sq_attr = {};
				struct snap_nvme_sq *sq;
				struct ibv_qp *ibqp;

				cq_attr.type = q_type;
				cq_attr.id = j;
				cq_attr.msix = j;
				cq_attr.queue_depth = depth;
				cq_attr.base_addr = 0xdeadbeef * j;
				cq_attr.cq_period = j * 4;
				cq_attr.cq_max_count = j * 8;
				cq = snap_nvme_create_cq(sdev, &cq_attr);
				if (cq) {
					sq_attr.type = q_type;
					sq_attr.id = j;
					sq_attr.queue_depth = depth;
					sq_attr.base_addr = 0xbeefdead * j;
					sq_attr.cq = cq;
					sq_attr.qp = ibqp = snap_create_qp(pd, ibcq);
					if (!sq_attr.qp) {
						fprintf(stderr, "NVMe sq id=%d fail to create QP\n", j);
						fflush(stderr);
						snap_nvme_destroy_cq(cq);
						continue;
					}
					sq = snap_nvme_create_sq(sdev, &sq_attr);
					if (sq) {
						fprintf(stdout, "NVMe sq id=%d created with qpn=0x%x !\n", j, ibqp->qp_num);
						fflush(stdout);
						if (query) {
							memset(&sq_attr, 0, sizeof(sq_attr));
							if (!snap_nvme_query_sq(sq, &sq_attr)) {
								fprintf(stdout, "Query NVMe sq id=%d, depth=%d\n", j,
									sq_attr.queue_depth);
								fflush(stdout);
							} else {
								fprintf(stderr, "Failed to Query NVMe sq id=%d\n", j);
								fflush(stderr);
							}
						}
						if (modify) {
							uint64_t mask;

							memset(&sq_attr, 0, sizeof(sq_attr));
							/* modify qpn/state separately */
							if (j % 2) {
								sq_attr.qp = snap_create_qp(pd, ibcq);
								if (!sq_attr.qp) {
									fprintf(stderr, "NVMe sq id=%d fail to create QP for modify\n", j);
									fflush(stderr);
								} else {
									mask = SNAP_NVME_SQ_MOD_QPN;
									fprintf(stdout, "NVMe sq id=%d modify to qpn=0x%x !\n", j, sq_attr.qp->qp_num);
									fflush(stdout);
								}
							} else {
								mask = SNAP_NVME_SQ_MOD_STATE;
								sq_attr.state = SNAP_NVME_SQ_STATE_ERR;
							}
							if (!snap_nvme_modify_sq(sq, mask, &sq_attr)) {
								fprintf(stdout, "Modify NVMe sq id=%d, mask=%d\n", j, mask);
								fflush(stdout);
							} else {
								fprintf(stderr, "Failed to Modify NVMe sq id=%d, mask=%d\n", j, mask);
								fflush(stderr);
							}
							if (sq_attr.qp)
								ibv_destroy_qp(sq_attr.qp);
						}
						snap_nvme_destroy_sq(sq);
					} else {
						fprintf(stderr, "failed to create NVMe sq id=%d, err=%d\n", j, errno);
						fflush(stderr);
					}
					ibv_destroy_qp(ibqp);
					snap_nvme_destroy_cq(cq);
				} else {
					fprintf(stderr, "failed to create NVMe cq id=%d\n", j);
					fflush(stderr);
				}
			}
			snap_nvme_teardown_device(sdev);
		} else {
			fprintf(stderr, "failed to create NVMe dev for pf %d ret=%d\n",
				attr.pf_id, ret);
			fflush(stderr);
		}
		snap_close_device(sdev);
	} else {
		fprintf(stderr, "failed to create device %d for %s\n",
			attr.pf_id, sctx->context->device->name);
		fflush(stderr);
	}
	free_ib_resources(ibcq, pd);
out_free_ctx:
	snap_ctx_close(sctx);
out:
	return ret;
}
