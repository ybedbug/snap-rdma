#include <stdio.h>

#include <infiniband/verbs.h>

#include "snap.h"
#include "snap_nvme.h"

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

int main(int argc, char **argv)
{
	struct ibv_device **list;
	int ret = 0, i, dev_count, opt, num_queues = 10, depth = 16;
	enum snap_nvme_queue_type q_type = SNAP_NVME_SQE_MODE;
	bool query = false, modify = false;

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
			if (!strcmp(optarg, "sqe"))
				q_type = SNAP_NVME_SQE_MODE;
			else if (!strcmp(optarg, "cc"))
				q_type = SNAP_NVME_CC_MODE;
			break;
		default:
			printf("Usage: snap_create_destroy_nvme_sq -n <num> -t <type> [mq]\n");
			exit(1);
		}
	}

	list = ibv_get_device_list(&dev_count);
	if (!list) {
		fprintf(stderr, "failed to open ib device list.\n");
		fflush(stderr);
		ret = 1;
		goto out;
	}

	for (i = 0; i < dev_count; i++) {
		struct snap_device_attr attr = {};
		struct snap_context *sctx;
		struct ibv_cq *ibcq;
		struct ibv_pd *pd;
		struct snap_device *sdev;
		int j;

		sctx = snap_open(list[i]);
		if (!sctx) {
			fprintf(stderr, "failed to create snap ctx for %s err=%d. continue trying\n",
				list[i]->name, errno);
			fflush(stderr);
			continue;
		}

		ibcq = ibv_create_cq(sctx->context, 1024, NULL, NULL, 0);
		if (!ibcq) {
			snap_close(sctx);
			continue;
		}

		pd = ibv_alloc_pd(sctx->context);
		if (!pd) {
			ibv_destroy_cq(ibcq);
			snap_close(sctx);
			continue;
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

					cq_attr.type = q_type;
					cq_attr.id = j;
					cq_attr.doorbell_offset = 4 + j * 8;
					cq_attr.msix = j;
					cq_attr.queue_depth = depth;
					cq_attr.base_addr = 0xdeadbeef * j;
					cq_attr.cq_period = j * 4;
					cq_attr.cq_max_count = j * 8;
					cq = snap_nvme_create_cq(sdev, &cq_attr);
					if (cq) {
						sq_attr.type = q_type;
						sq_attr.id = j;
						sq_attr.doorbell_offset = j * 8;
						sq_attr.queue_depth = depth;
						sq_attr.base_addr = 0xbeefdead * j;
						sq_attr.cq = cq;
						sq_attr.qp = snap_create_qp(pd, ibcq);
						if (!sq_attr.qp) {
							fprintf(stderr, "NVMe sq id=%d fail to create QP\n", j);
							fflush(stderr);
							snap_nvme_destroy_cq(cq);
							continue;
						}
						sq = snap_nvme_create_sq(sdev, &sq_attr);
						if (sq) {
							fprintf(stdout, "NVMe sq id=%d created !\n", j);
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
								memset(&sq_attr, 0, sizeof(sq_attr));
								sq_attr.state = SNAP_NVME_SQ_STATE_ERR;
								if (!snap_nvme_modify_sq(sq,
											 SNAP_NVME_SQ_MOD_STATE,
											 &sq_attr)) {
									fprintf(stdout, "Modify NVMe sq id=%d, to state=%d\n", j,
										sq_attr.state);
									fflush(stdout);
								} else {
									fprintf(stderr, "Failed to Modify NVMe sq id=%d, to state=%d\n", j,
										sq_attr.state);
									fflush(stderr);
								}
							}
							snap_nvme_destroy_sq(sq);
						} else {
							fprintf(stderr, "failed to create NVMe sq id=%d, err=%d\n", j, errno);
							fflush(stderr);
						}
						ibv_destroy_qp(sq_attr.qp);
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
				attr.pf_id, list[i]->name);
			fflush(stderr);
		}
		ibv_dealloc_pd(pd);
		ibv_destroy_cq(ibcq);
		snap_close(sctx);
	}

	ibv_free_device_list(list);
out:
	return ret;
}
