#include <linux/virtio_ring.h>
#include "snap_virtio_blk_virtq.h"
#include "snap_dma.h"
#include "snap_virtio_blk.h"

#define SQ_SIZE_LOG  6
#define SQ_SIZE (1 << SQ_SIZE_LOG)

struct blk_virtq_priv;

struct split_tunnel_req_hdr {
	uint16_t avail_idx;
	uint16_t num_desc;
	uint64_t rsvd1;
	uint32_t rsvd2;
};

struct split_tunnel_comp {
	uint16_t avail_idx;
	uint16_t rsvd;
	uint32_t len;
};

struct blk_virtq_cmd {
	int idx;
	int num_desc;
	struct blk_virtq_priv *vq_priv;
};

struct blk_virtq_priv {
	struct blk_virtq_ctx vq_ctx;
	struct virtq_bdev *blk_dev;
	struct ibv_pd *pd;
	struct snap_virtio_blk_queue *snap_vbq;
	struct snap_virtio_blk_queue_attr snap_attr;
	struct snap_dma_q *dma_q;
	struct blk_virtq_cmd *cmd_arr;
	int cmd_cntr;
};

static struct blk_virtq_cmd *
alloc_blk_virtq_cmd_arr(int num, uint32_t size_max, uint32_t seg_max,
			struct blk_virtq_priv *vq_priv)
{
	struct blk_virtq_cmd *ptr;

	ptr = calloc(num, sizeof(struct blk_virtq_cmd));
	if (!ptr) {
		snap_error("failed to allocate memory for blk_virtq commands\n");
		return NULL;
	}

	return ptr;
}

static void free_blk_virtq_cmd_arr(struct blk_virtq_priv *priv)
{
	free(priv->cmd_arr);
}

static void blk_virtq_rx_cb(struct snap_dma_q *q, void *data,
			    uint32_t data_len, uint32_t imm_data)
{
	snap_debug("blk_virtq_rx_cb");
}

/**
 * blk_virtq_create() - Creates a new blk virtq object, along with RDMA QPs.
 * @blk_dev:	Backend block device
 * @snap_dev:	Snap device on top virtq is created
 * @attr:	Configuration attributes
 *
 * Context: Calling function should attach the virtqueue to a polling group
 *
 * Return: NULL on failure, new block virtqueue context on success
 */
struct blk_virtq_ctx *blk_virtq_create(struct virtq_bdev *blk_dev,
				       struct snap_device *snap_dev,
				       struct blk_virtq_create_attr *attr)
{
	struct blk_virtq_ctx *vq_ctx;
	struct blk_virtq_priv *vq_priv;
	struct snap_dma_q_create_attr rdma_qp_create_attr = {};

	vq_priv = calloc(1, sizeof(struct blk_virtq_priv));
	if (!vq_priv)
		goto err;

	vq_ctx = &vq_priv->vq_ctx;
	vq_ctx->priv = vq_priv;
	vq_priv->blk_dev = blk_dev;
	vq_priv->pd = attr->pd;
	vq_ctx->idx = attr->idx;
	vq_ctx->fatal_err = 0;

	vq_priv->cmd_arr = alloc_blk_virtq_cmd_arr(attr->queue_size,
						   attr->size_max,
						   attr->seg_max, vq_priv);
	if (!vq_priv->cmd_arr) {
		snap_error("failed allocating cmd list for queue %d\n",
			   attr->idx);
		goto release_priv;
	}
	vq_priv->cmd_cntr = 0;

	/* create hw and sw qps, hw qps will be given to VIRTIO_BLK_Q
	 * Completion is sent inline, hence tx elem size is completion size
	 * the rx queue size should match the number of possible descriptors
	 * this in the worst case scenario is the VIRTQ size */
	rdma_qp_create_attr.tx_qsize = SQ_SIZE;
	rdma_qp_create_attr.tx_elem_size = sizeof(struct split_tunnel_comp);
	rdma_qp_create_attr.rx_qsize = attr->queue_size;
	rdma_qp_create_attr.rx_elem_size = sizeof(struct split_tunnel_req_hdr) +
					   attr->seg_max * sizeof(struct vring_desc);
	rdma_qp_create_attr.uctx = vq_priv;
	rdma_qp_create_attr.rx_cb = blk_virtq_rx_cb;

	vq_priv->dma_q = snap_dma_q_create(attr->pd, &rdma_qp_create_attr);
	if (!vq_priv->dma_q) {
		snap_error("failed creating rdma qp loop\n");
		goto dealloc_cmd_arr;
	}

	vq_priv->snap_attr.vattr.type = SNAP_VIRTQ_SPLIT_MODE;
	vq_priv->snap_attr.vattr.ev_mode = SNAP_VIRTQ_MSIX_MODE;
	vq_priv->snap_attr.vattr.virtio_version_1_0 = attr->virtio_version_1_0;
	vq_priv->snap_attr.vattr.offload_type = SNAP_VIRTQ_OFFLOAD_DESC_TUNNEL;
	vq_priv->snap_attr.vattr.idx = attr->idx;
	vq_priv->snap_attr.vattr.size = attr->queue_size;
	vq_priv->snap_attr.vattr.desc = attr->desc;
	vq_priv->snap_attr.vattr.driver = attr->driver;
	vq_priv->snap_attr.vattr.device = attr->device;
	vq_priv->snap_attr.vattr.full_emulation = true;
	vq_priv->snap_attr.vattr.max_tunnel_desc = attr->max_tunnel_desc;
	vq_priv->snap_attr.vattr.event_qpn_or_msix = attr->msix_vector;
	vq_priv->snap_attr.qp = snap_dma_q_get_fw_qp(vq_priv->dma_q);
	if (!vq_priv->snap_attr.qp) {
		snap_error("no fw qp exist when trying to create virtq\n");
		goto release_rdma_qp;
	}
	vq_priv->snap_vbq = snap_virtio_blk_create_queue(snap_dev,
							 &vq_priv->snap_attr);
	if (!vq_priv->snap_vbq) {
		snap_error("failed creating VIRTQ fw element\n");
		goto release_rdma_qp;
	}
	if (snap_virtio_blk_query_queue(vq_priv->snap_vbq,
				        &vq_priv->snap_attr)) {
		snap_error("failed query created snap virtio blk queue\n");
		goto destroy_virtio_blk_queue;
	}

	snap_debug("created VIRTQ %d succesfully\n", attr->idx);
	return vq_ctx;

destroy_virtio_blk_queue:
	snap_virtio_blk_destroy_queue(vq_priv->snap_vbq);
release_rdma_qp:
	snap_dma_q_destroy(vq_priv->dma_q);
dealloc_cmd_arr:
	free_blk_virtq_cmd_arr(vq_priv);
release_priv:
	free(vq_priv);
err:
	snap_error("failed creating blk_virtq %d\n", attr->idx);
	return NULL;
}

/**
 * blk_virtq_destroy() - Destroyes blk virtq object
 * @q: queue to be destryoed
 *
 * Context: Virtqueue is removed from pg before function is called (Hence won't
 * 	    receive new commands/rdma IO over QPs)
 *
 * Return: void
 */
void blk_virtq_destroy(struct blk_virtq_ctx *q)
{
	struct blk_virtq_priv *vq_priv = q->priv;

	snap_debug("destroying queue %d\n", q->idx);

	if (snap_virtio_blk_destroy_queue(vq_priv->snap_vbq))
		snap_error("error destroying blk_virtq\n");

	snap_dma_q_destroy(vq_priv->dma_q);
	free_blk_virtq_cmd_arr(vq_priv);
	free(vq_priv);
}

/**
 * blk_virtq_progress() - Progress RDMA QPs,  Polls on QPs CQs
 * @q:	queue to progress
 *
 * Context: Not thread safe
 *
 * Return: error code on failure, 0 on success
 */
int blk_virtq_progress(struct blk_virtq_ctx *q)
{
	struct blk_virtq_priv *priv;

	priv = q->priv;
	if (!priv) {
		snap_error("Invalid queue context for queue %d\n", q->idx);
		return -EFAULT;
	}

	return snap_dma_q_progress(priv->dma_q);
}
