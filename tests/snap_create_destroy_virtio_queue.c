#include <stdio.h>

#include <infiniband/verbs.h>
#include <infiniband/mlx5dv.h>

#include "snap.h"
#include "snap_test.h"
#include "snap_virtio_blk.h"
#include "snap_virtio_net.h"
#include "snap_virtio_fs.h"
#include "mlx5_ifc.h"

struct snap_sf {
	struct ibv_context *context;
	uint16_t vhca_id;
	struct ibv_pd *pd;
	struct ibv_cq *cq;
};

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

static int snap_create_destroy_virtq_helper(struct snap_context *sctx,
		enum snap_emulation_type type, int num_queues,
		enum snap_virtq_type q_type,
		enum snap_virtq_event_mode ev_mode,
		char *name, bool ev, struct snap_sf *sf)
{
	struct snap_device_attr attr = {};
	struct snap_device *sdev;
	struct ibv_cq *ibcq;
	struct ibv_pd *ibpd;
	int j, ret;

	ibcq = ibv_create_cq(sctx->context, 1024, NULL, NULL, 0);
	if (!ibcq)
		return -1;

	ibpd = ibv_alloc_pd(sctx->context);
	if (!ibpd) {
		ibv_destroy_cq(ibcq);
		return -1;
	}

	if (ev)
		attr.flags = SNAP_DEVICE_FLAGS_EVENT_CHANNEL;
	attr.pf_id = 0;
	if (type == SNAP_VIRTIO_BLK) {
		attr.type = SNAP_VIRTIO_BLK_PF;
		sdev = snap_open_device(sctx, &attr);
		if (sdev) {
			ret = snap_virtio_blk_init_device(sdev);
			if (!ret) {
				fprintf(stdout, "created Virtio blk dev for pf %d. Creating %d queues\n",
					attr.pf_id, num_queues);
				fflush(stdout);
				for (j = 0; j < num_queues; j++) {
					struct snap_virtio_common_queue_attr battr = {};
					struct snap_virtio_blk_queue *vbq;
					struct ibv_qp *qp = NULL;

					battr.vattr.type = q_type;
					battr.vattr.ev_mode = ev_mode;
					battr.vattr.idx = j;
					battr.vattr.size = 16;
					battr.vattr.offload_type = SNAP_VIRTQ_OFFLOAD_DESC_TUNNEL;
					battr.vattr.full_emulation = true;
					battr.vattr.virtio_version_1_0 = true;
					battr.vattr.max_tunnel_desc = 4;
					battr.vattr.pd = sf->pd;
					if (sf && sf->context && sf->cq && sf->pd) {
						qp = snap_create_qp(sf->pd, sf->cq);
						if (!qp)
							break;
					} else {
						qp = snap_create_qp(ibpd, ibcq);
						if (!qp)
							break;
						battr.vattr.pd = ibpd;
					}
					battr.qp = qp;
					vbq = snap_virtio_blk_create_queue(sdev, &battr);
					if (vbq) {
						memset(&battr, 0, sizeof(battr));
						if (!snap_virtio_blk_query_queue(vbq, &battr)) {
							fprintf(stdout, "Query virtio blk queue idx=0x%x, depth=%d, state=0x%x\n",
								battr.vattr.idx, battr.vattr.size, battr.vattr.state);
							fflush(stdout);
						} else {
							fprintf(stderr, "Failed to Query virtio blk queue id=0x%x\n", j);
							fflush(stderr);
						}
						snap_virtio_blk_destroy_queue(vbq);
					} else {
						fprintf(stderr, "failed to create Virtio blk queue id=%d err=%d\n", j, errno);
						fflush(stderr);
					}
					if (qp)
						ibv_destroy_qp(qp);
				}
				snap_virtio_blk_teardown_device(sdev);
			} else {
				fprintf(stderr, "failed to init Virtio blk dev for pf %d ret=%d\n",
					attr.pf_id, ret);
				fflush(stderr);
			}
			snap_close_device(sdev);
		} else {
			fprintf(stderr, "failed to create device %d for %s\n",
				attr.pf_id, name);
			fflush(stderr);
		}
	} else if (type == SNAP_VIRTIO_FS) {
		attr.type = SNAP_VIRTIO_FS_PF;
		sdev = snap_open_device(sctx, &attr);
		if (sdev) {
			ret = snap_virtio_fs_init_device(sdev);
			if (!ret) {
				fprintf(stdout, "created Virtio fs dev for pf %d. Creating %d queues\n",
					attr.pf_id, num_queues);
				fflush(stdout);
				for (j = 0; j < num_queues; j++) {
					struct snap_virtio_common_queue_attr fs_attr = {};
					struct snap_virtio_fs_queue *vfsq;
					struct ibv_qp *qp = NULL;

					fs_attr.vattr.type = q_type;
					fs_attr.vattr.ev_mode = ev_mode;
					fs_attr.vattr.idx = j;
					fs_attr.vattr.size = 16;
					fs_attr.vattr.offload_type = SNAP_VIRTQ_OFFLOAD_DESC_TUNNEL;
					fs_attr.vattr.full_emulation = true;
					fs_attr.vattr.virtio_version_1_0 = true;
					fs_attr.vattr.max_tunnel_desc = 4;
					fs_attr.vattr.pd = sf->pd;
					if (sf && sf->context && sf->cq && sf->pd) {
						qp = snap_create_qp(sf->pd, sf->cq);
						if (!qp)
							break;
					} else {
						qp = snap_create_qp(ibpd, ibcq);
						if (!qp)
							break;
					}
					fs_attr.qp = qp;
					vfsq = snap_virtio_fs_create_queue(sdev, &fs_attr);
					if (vfsq) {
						memset(&fs_attr, 0, sizeof(fs_attr));
						if (!snap_virtio_fs_query_queue(vfsq, &fs_attr)) {
							fprintf(stdout, "Query virtio fs queue idx=0x%x, depth=%d, state=0x%x\n",
								fs_attr.vattr.idx, fs_attr.vattr.size, fs_attr.vattr.state);
							fflush(stdout);
						} else {
							fprintf(stderr, "Failed to Query virtio fs queue id=0x%x\n", j);
							fflush(stderr);
						}
						snap_virtio_fs_destroy_queue(vfsq);
					} else {
						fprintf(stderr, "failed to create Virtio fs queue id=%d err=%d\n", j, errno);
						fflush(stderr);
					}
					if (qp)
						ibv_destroy_qp(qp);
				}
				snap_virtio_fs_teardown_device(sdev);
			} else {
				fprintf(stderr, "failed to init Virtio fs dev for pf %d ret=%d\n",
					attr.pf_id, ret);
				fflush(stderr);
			}
			snap_close_device(sdev);
		} else {
			fprintf(stderr, "failed to create device %d for %s\n",
				attr.pf_id, name);
			fflush(stderr);
		}
	} else {
		attr.type = SNAP_VIRTIO_NET_PF;
		sdev = snap_open_device(sctx, &attr);
		if (sdev) {
			ret = snap_virtio_net_init_device(sdev);
			if (!ret) {
				fprintf(stdout, "created Virtio net dev for pf %d. Creating %d queues\n",
					attr.pf_id, num_queues);
				fflush(stdout);
				for (j = 0; j < num_queues; j++) {
					struct snap_virtio_net_queue_attr nattr = {};
					struct snap_virtio_net_queue *vnq;

					nattr.vattr.type = q_type;
					nattr.vattr.ev_mode = ev_mode;
					nattr.vattr.idx = j;
					nattr.vattr.size = 16;
					nattr.vattr.pd = sf->pd;
					nattr.tisn_or_qpn = (j + 1) * 0xbeaf;
					vnq = snap_virtio_net_create_queue(sdev, &nattr);
					if (vnq) {
						snap_virtio_net_destroy_queue(vnq);
					} else {
						fprintf(stderr, "failed to create Virtio net queue id=%d err=%d\n", j, errno);
						fflush(stderr);
					}
				}
				snap_virtio_net_teardown_device(sdev);
			} else {
				fprintf(stderr, "failed to init Virtio net dev for pf %d ret=%d\n",
					attr.pf_id, ret);
				fflush(stderr);
			}
			snap_close_device(sdev);
		} else {
			fprintf(stderr, "failed to create device %d for %s\n",
				attr.pf_id, name);
			fflush(stderr);
		}

	}

	ibv_dealloc_pd(ibpd);
	ibv_destroy_cq(ibcq);

	return 0;
}

static int init_snap_sf(struct snap_sf *sf)
{
	uint8_t in[DEVX_ST_SZ_BYTES(query_hca_cap_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(query_hca_cap_out)] = {0};
	int ret;

	DEVX_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	DEVX_SET(query_hca_cap_in, in, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_GENERAL_DEVICE);

	ret = mlx5dv_devx_general_cmd(sf->context, in, sizeof(in), out,
				      sizeof(out));
	if (ret)
		return -1;

	sf->vhca_id = DEVX_GET(query_hca_cap_out, out,
			       capability.cmd_hca_cap.vhca_id);
	sf->pd = ibv_alloc_pd(sf->context);
	if (!sf->pd)
		return -1;

	sf->cq = ibv_create_cq(sf->context, 1024, NULL, NULL, 0);
	if (!sf->cq)
		goto dealloc_pd;

	return 0;

dealloc_pd:
	ibv_dealloc_pd(sf->pd);
	return -1;
}

int main(int argc, char **argv)
{
	struct ibv_device **list;
	bool ev = false;
	int ret = 0, i, dev_count, opt, num_queues = 4, dev_type = 0;
	enum snap_virtq_type q_type = SNAP_VIRTQ_SPLIT_MODE;
	enum snap_virtq_event_mode ev_mode = SNAP_VIRTQ_NO_MSIX_MODE;
	char dev_name[64] = {0};
	struct snap_sf sf;
	struct snap_context *sctx;

	sf.context = NULL;
	sf.vhca_id = -1;
	sf.pd = NULL;
	sf.cq = NULL;

	while ((opt = getopt(argc, argv, "s:n:t:e:d:v")) != -1) {
		switch (opt) {
		case 's':
			strcpy(dev_name, optarg);
			break;
		case 'v':
			ev = true;
			break;
		case 'n':
			num_queues = atoi(optarg);
			break;
		case 't':
			if (!strcmp(optarg, "split"))
				q_type = SNAP_VIRTQ_SPLIT_MODE;
			else if (!strcmp(optarg, "packed"))
				q_type = SNAP_VIRTQ_PACKED_MODE;
		case 'e':
			if (!strcmp(optarg, "no_msix"))
				ev_mode = SNAP_VIRTQ_NO_MSIX_MODE;
			else if (!strcmp(optarg, "qp"))
				ev_mode = SNAP_VIRTQ_QP_MODE;
			else if (!strcmp(optarg, "msix"))
				ev_mode = SNAP_VIRTQ_MSIX_MODE;
			break;
		case 'd':
			if (!strcmp(optarg, "all"))
				dev_type = SNAP_VIRTIO_BLK | SNAP_VIRTIO_NET;
			else if (!strcmp(optarg, "virtio_blk"))
				dev_type = SNAP_VIRTIO_BLK;
			else if (!strcmp(optarg, "virtio_net"))
				dev_type = SNAP_VIRTIO_NET;
			else if (!strcmp(optarg, "virtio_fs"))
				dev_type = SNAP_VIRTIO_FS;
			else
				printf("Unknown type %s. Using default\n", optarg);
			break;
		default:
			printf("Usage: snap_create_destroy_virtio_queue -n <num_queues> -t <q_type> -e <ev_mode> -d <dev_type: all, virtio_blk, virtio_net, virtio_fs> [-v (event_channel) -s (SF)]\n");
			exit(1);
		}
	}

	if (!dev_type)
		dev_type = SNAP_VIRTIO_BLK | SNAP_VIRTIO_NET | SNAP_VIRTIO_FS;

	list = ibv_get_device_list(&dev_count);
	if (!list) {
		fprintf(stderr, "failed to open ib device list.\n");
		fflush(stderr);
		ret = 1;
		goto out;
	}

	/* Create SF */
	if (dev_name[0]) {
		for (i = 0; i < dev_count; i++) {
			if (strcmp(dev_name, list[i]->name) == 0) {
				sf.context = ibv_open_device(list[i]);
				if (!sf.context) {
					fprintf(stderr, "failed to create sf for %s err=%d. exiting\n",
						list[i]->name, errno);
					fflush(stderr);
					goto out_free_list;
				} else {
					ret = init_snap_sf(&sf);
					if (ret) {
						fprintf(stderr, "failed to init sf for %s err=%d. exiting\n",
							list[i]->name, errno);
						fflush(stderr);
						goto out_free_sf;
					} else {
						fprintf(stdout, "SF created for %s with vhca_id %d.\n",
							list[i]->name, sf.vhca_id);
						fflush(stdout);
					}
					break;
				}
			}
		}
	}

	sctx = snap_ctx_open(dev_type, NULL);
	if (!sctx) {
		fprintf(stderr, "failed to open snap ctx for %d types\n", dev_type);
		fflush(stderr);
		ret = -errno;
		goto out_free;
	}
	else {
		fprintf(stdout, "opened snap ctx for %d types on %s\n", dev_type,
			sctx->context->device->name);
		fflush(stdout);
	}

	if (dev_type & SNAP_VIRTIO_BLK)
		ret |= snap_create_destroy_virtq_helper(sctx, SNAP_VIRTIO_BLK,
							num_queues, q_type,
							ev_mode,
							sctx->context->device->name,
							ev, &sf);
	if (dev_type & SNAP_VIRTIO_NET)
		ret |= snap_create_destroy_virtq_helper(sctx, SNAP_VIRTIO_NET,
							num_queues, q_type,
							ev_mode,
							sctx->context->device->name,
							ev, &sf);
	if (dev_type & SNAP_VIRTIO_FS)
		ret |= snap_create_destroy_virtq_helper(sctx, SNAP_VIRTIO_FS,
							num_queues, q_type,
							ev_mode,
							sctx->context->device->name,
							ev, &sf);
	snap_ctx_close(sctx);
	fprintf(stdout, "closed snap ctx for %d types\n", dev_type);

out_free:
	if (sf.cq)
		ibv_destroy_cq(sf.cq);
	if (sf.pd)
		ibv_dealloc_pd(sf.pd);
out_free_sf:
	if (sf.context)
		ibv_close_device(sf.context);
out_free_list:
	ibv_free_device_list(list);
out:
	return ret;
}
