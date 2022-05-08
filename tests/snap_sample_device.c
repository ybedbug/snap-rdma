/*
 * Copyright (c) 2020 Mellanox Technologies, Inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * This is an example of a controller implementation for
 * a simple emulated device.
 *
 * The device has a bar with registers, one queue to receive commands and one
 * queue to send completions.
 */
#include <stdio.h>
#include <infiniband/verbs.h>

#include "snap.h"
#include "snap_nvme.h"
#include "snap_dma.h"
#include "snap_sample_device.h"

struct sample_controller;

/**
 * struct sample_dma_request - An example of the DMA request
 * @data: data buffer to read data from host or to write data to host. It must
 *        reside in memory registered by the ibv_reg_mr()
 * @ctrl: pointer to the controller
 * @comp: dma completion that will be passed to the snap_dma_q_read() or
 *        snap_dma_q_write()
*/
struct sample_dma_request {
	void                        *data;
	struct sample_controller    *ctrl;
	struct snap_dma_completion  comp;
};

/* number of requests that can be used for async DMA operations */
#define SAMPLE_CONTROLLER_N_REQUESTS 1

/**
 * struct sample_controller - Controller implementation for the simple emulated
 *                            device
 * @sctx: snap library context
 * @sdev: snap library 'emulated device'
 *
 * @sq:   snap library NVMe submission queue object
 * @cq:   snap library NVMe completion queue object
 * @cq_sn: completion serial number
 *
 * @dma_q: snap library DMA queue. Essentially this is a pair of RC QPs connected
 *         over loopback. See snap_dma.h
 * @dma_rkey:    host memory key. Must be obtained by the snap_nvme_query_sq()
 * @dma_membase: points to the block of memory registered with ibv_reg_mr()
 *               The memory is then split among dma requests. Usually it is
 *               somewhat better to have one big pool of registered memory.
 * @dma_reqs:    dma requests
 * @mr:     holds @dma_membase registration and lkey (local memory key)
 * @ib_ctx: ib verbs context. Represents device on which to create @dma_q
 * @pd:     protection domain created on @ib_ctx. Used by @dma_q
 *
 * @last_cmd: last command that was written to the command register
 * @enabled: controller status
 */
struct sample_controller {
	struct snap_context *sctx;
	struct snap_device  *sdev;

	struct snap_nvme_sq *sq;
	struct snap_nvme_cq *cq;
	uint32_t            cq_sn;

	struct snap_dma_q         *dma_q;
	uint32_t                  dma_rkey;
	struct sample_dma_request dma_reqs[SAMPLE_CONTROLLER_N_REQUESTS];
	char                      *dma_membase;

	struct ibv_mr      *mr;
	struct ibv_context *ib_ctx;
	struct ibv_pd      *pd;

	uint32_t                crossed_vhca_mkey;
	struct snap_cross_mkey *mkey;

	uint32_t last_cmd;
	bool     enabled;
};

static void ctrl_dma_cb(struct snap_dma_completion *comp, int status);

static struct sample_dma_request *ctrl_alloc_request(struct sample_controller *ctrl)
{
	/* TODO: get one from the free list */
	return &ctrl->dma_reqs[0];
}

static void ctrl_free_request(struct sample_dma_request *req)
{
	/* TODO: put request back on the free list */
}

static void ctrl_destroy_requests(struct sample_controller *ctrl)
{
	ibv_dereg_mr(ctrl->mr);
	free(ctrl->dma_membase);
}

static int ctrl_init_requests(struct sample_controller *ctrl)
{
	int i;
	struct sample_dma_request *r;

	ctrl->dma_membase = calloc(SAMPLE_CONTROLLER_N_REQUESTS,
			SNAP_SAMPLE_DEV_MAX_DATA_SIZE);
	if (!ctrl->dma_membase) {
		printf("Failed allocate requests memory\n");
		return -1;
	}

	ctrl->mr = ibv_reg_mr(ctrl->pd, ctrl->dma_membase,
			SAMPLE_CONTROLLER_N_REQUESTS * SNAP_SAMPLE_DEV_MAX_DATA_SIZE,
			IBV_ACCESS_LOCAL_WRITE|IBV_ACCESS_REMOTE_WRITE|IBV_ACCESS_REMOTE_READ);
	if (!ctrl->mr) {
		printf("Failed to register requests memory\n");
		goto free_mem;
	}

	for (i = 0; i < SAMPLE_CONTROLLER_N_REQUESTS; i++) {
		r = &ctrl->dma_reqs[i];
		r->comp.func = ctrl_dma_cb;
		r->data = ctrl->dma_membase + i * SNAP_SAMPLE_DEV_MAX_DATA_SIZE;
		r->ctrl = ctrl;
	}

	/* TODO: keep requests on the free list */
	return 0;

free_mem:
	free(ctrl->dma_membase);
	return -1;
}

static struct ibv_device *find_device(struct ibv_device **dev_list, int n,
		char *dev_name)
{
	int i;

	for (i = 0; i < n; i++) {
		if (strcmp(ibv_get_device_name(dev_list[i]), dev_name) == 0)
			return dev_list[i];
	}
	return NULL;
}

/**
 * ctrl_init() - Initialize sample device controller
 * @dev_mgr_name:  device name (e.x mlx5_0) that has emulation manager
 *                 capabilities
 * @rdma_dev_name: device name (e.x. mlx5_0) that hase RDMA capabilities
 *
 * The function initializes resources required by the controller.
 *
 * Note: on Bluefield1 and Bluefield2 emulation manager device is different
 * from the rdma one. In fact emulation manager is not capable of doing RDMA.
 *
 * Return: pointer to the new controller or NULL on failure
 */
static struct sample_controller *
ctrl_init(char *dev_mgr_name, char *rdma_dev_name)
{
	struct snap_device_attr snap_attr = {};
	struct mlx5dv_context_attr rdma_attr = {};
	struct sample_controller *ctrl;
	struct ibv_device **dev_list, *dev;
	int n_dev;
	int rc;

	ctrl = calloc(sizeof(*ctrl), 1);
	if (!ctrl) {
		printf("Failed to allocate controller\n");
		return NULL;
	}

	dev_list = ibv_get_device_list(&n_dev);
	if (!dev_list) {
		printf("Failed to get device list\n");
		goto free_controller;
	}

	/* Open device on the emulation manager */
	dev = find_device(dev_list, n_dev, dev_mgr_name);
	if (!dev) {
		printf("Emulation manager device %s is not available\n", dev_mgr_name);
		goto free_dev_list;
	}

	ctrl->sctx = snap_open(dev);
	if (!ctrl->sctx) {
		printf("Failed to open emulation context on %s\n", dev_mgr_name);
		goto free_dev_list;
	}

	if (!(ctrl->sctx->emulation_caps & SNAP_NVME)) {
		printf("%s is not capable of NVMe emulation\n", dev_mgr_name);
		goto snap_close;
	}

	snap_attr.type = SNAP_NVME_PF;
	snap_attr.pf_id = SNAP_SAMPLE_DEVICE_PF;
	ctrl->sdev = snap_open_device(ctrl->sctx, &snap_attr);
	if (!ctrl->sdev) {
		printf("Failed open device on pf %d\n", snap_attr.pf_id);
		goto snap_close;
	}
	/* At this point we should be able to access device BAR */

	/* Open device that is rdma capable */
	dev = find_device(dev_list, n_dev, rdma_dev_name);
	if (!dev) {
		printf("RDMA device %s is not available\n", rdma_dev_name);
		goto snap_close_dev;
	}

	/* we must enable devx on our rdma device */
	rdma_attr.flags = MLX5DV_CONTEXT_FLAGS_DEVX;
	ctrl->ib_ctx = mlx5dv_open_device(dev, &rdma_attr);
	if (!ctrl->ib_ctx) {
		printf("Failed to open rdma context on %s\n", rdma_dev_name);
		goto snap_close_dev;
	}

	ctrl->pd = ibv_alloc_pd(ctrl->ib_ctx);
	if (!ctrl->pd)  {
		printf("Failed to create PD\n");
		goto close_ib_ctx;
	}

	rc = ctrl_init_requests(ctrl);
	if (rc)
		goto free_pd;

	ibv_free_device_list(dev_list);
	return ctrl;

free_pd:
	ibv_dealloc_pd(ctrl->pd);
close_ib_ctx:
	ibv_close_device(ctrl->ib_ctx);
snap_close_dev:
	snap_close_device(ctrl->sdev);
snap_close:
	snap_close(ctrl->sctx);
free_dev_list:
	ibv_free_device_list(dev_list);
free_controller:
	free(ctrl);
	return NULL;
}

/**
 * ctrl_destroy() - Destroy sample controller
 * @ctrl: controller to destroy
 *
 * The function destroys sample controller
 */
static void ctrl_destroy(struct sample_controller *ctrl)
{
	ctrl_destroy_requests(ctrl);
	ibv_dealloc_pd(ctrl->pd);
	ibv_close_device(ctrl->ib_ctx);
	snap_close_device(ctrl->sdev);
	snap_close(ctrl->sctx);
	free(ctrl);
}

/**
 * ctrl_dma_cb() - An example of dma completion callback
 * @comp:    dma completion
 * @status:  dma completion status
 *
 * The function handles dma completion. A common pattern is:
 * - use container_of() to extract original request
 * - do something related to the request state machine. It can be
 *   another dma operation, data transfer to/from disk or network etc..
 * - use snap_dma_q_send_completion() to notify host that the submision entry
 *   was handled.
 */
static void ctrl_dma_cb(struct snap_dma_completion *comp, int status)
{
	struct snap_sample_device_cqe cqe = {0};
	struct sample_dma_request *req;
	struct sample_controller *ctrl;
	int rc;

	if (status != IBV_WC_SUCCESS) {
		printf("dma failed with the status 0x%0x\n", status);
		return;
	}

	req = container_of(comp, struct sample_dma_request, comp);
	ctrl = req->ctrl;

	ctrl->cq_sn++;
	cqe.sn = ctrl->cq_sn;
	printf("dma done, sending completion sn %u\n", cqe.sn);
	rc = snap_dma_q_send_completion(ctrl->dma_q, &cqe, sizeof(cqe));
	if (rc)
		printf("Failed to send response\n");

	ctrl_free_request(req);
}

/**
 * ctrl_rx_cb() - Sample controller RX callback
 * @q:        dma queue
 * @data:     pointer to the new submission queue entry. Note that the pointer is
 *            valid only during the callback life time
 * @data_len: will be equal to the sizeof(struct snap_sample_device_sqe)
 * @imm_data: unused
 *
 * The callback is called when the driver posts a new submission
 * queue entry.
 *
 * Our sample device implements a simple ping/pong or ping with dma operation.
 * So either send back the response or start dma operation and send response
 * when the operation completes
 */
static void ctrl_rx_cb(struct snap_dma_q *q, const void *data, uint32_t data_len,
		uint32_t imm_data)
{
	struct snap_sample_device_cqe cqe = {0};
	struct snap_sample_device_sqe *sqe;
	struct sample_controller *ctrl;
	struct sample_dma_request *req;
	int rc;

	sqe = (struct snap_sample_device_sqe *)data;
	ctrl = (struct sample_controller *)snap_dma_q_ctx(q);
	if (sqe->len == 0) {
		ctrl->cq_sn++;
		cqe.sn = ctrl->cq_sn;
		printf("rcvd sn %u, sending completion sn %u\n", sqe->sn, cqe.sn);
		rc = snap_dma_q_send_completion(q, &cqe, sizeof(cqe));
		/* TODO: handle -EAGAIN case */
		if (rc)
			printf("Failed to send response\n");
		return;
	}

	/* start dma write operation */
	req = ctrl_alloc_request(ctrl);
	if (!req) {
		/* should not happen... number of requests must be equal to the
		 * sq/cq size
		 */
		printf("oops, failed to allocate request\n");
		return;
	}

	/* want the notification once the operation completes */
	req->comp.count = 1;

	/* fill buffer with some junk. In real world there will be some kind
	 * of async data transffer initiated into the req->data */
	memset(req->data, 0xEB, sqe->len);

	rc = snap_dma_q_write(q, req->data, sqe->len, ctrl->mr->lkey, sqe->paddr,
			ctrl->dma_rkey, &req->comp);
	/* TODO: handle -EAGAIN case */
	if (rc)
		printf("Failed to start dma operation\n");
}

/**
 * ctrl_cmd_stop() - Stop sample controller
 * @ctrl:  sample controller
 *
 * The function stops sample controller. Submission and completion queues,
 * DMA QP are destroyed.
 *
 * Return: 0 on success or -1 on failure
 */
static int ctrl_cmd_stop(struct sample_controller *ctrl)
{
	int rc = 0;

	if (!ctrl->enabled)
		return 0;

	/* Wait until all outstanding operations are completed */
	snap_dma_q_flush(ctrl->dma_q);

	rc = snap_destroy_cross_mkey(ctrl->mkey);
	if (rc) {
		printf("Failed to destroy crossing mkey\n");
		rc = -1;
	}

	rc = snap_nvme_destroy_sq(ctrl->sq);
	if (rc) {
		printf("Failed to destroy sq\n");
		rc = -1;
	}

	snap_dma_q_destroy(ctrl->dma_q);
	ctrl->dma_q = NULL;

	rc = snap_nvme_destroy_cq(ctrl->cq);
	if (rc) {
		printf("Failed to destroy cq\n");
		rc = -1;
	}

	rc = snap_nvme_teardown_device(ctrl->sdev);
	if (rc) {
		printf("Failed to teardown device\n");
		rc = -1;
	}

	ctrl->enabled = false;
	return rc;
}

/**
 * ctrl_cmd_start() - Start sample controller
 * @ctrl:  sample controller
 *
 * The function initializes device and creates submission and completion
 * queues.
 *
 * Return: 0 on success or -1 on failure
 */
static int ctrl_cmd_start(struct sample_controller *ctrl)
{
	struct snap_nvme_cq_attr cq_attr = {};
	struct snap_nvme_sq_attr sq_attr = {};
	struct snap_dma_q_create_attr dma_q_attr = {};
	struct snap_cross_mkey_attr cm_attr = {};
	uint64_t cq_base, sq_base;
	int rc;

	if (ctrl->enabled) {
		printf("device alreaady started\n");
		return -1;
	}

	rc = snap_nvme_init_device(ctrl->sdev);
	if (rc) {
		printf("Failed to init device\n");
		return -1;
	}

	ctrl->cq_sn = 0;
	sq_base = *(uint64_t *)(ctrl->sdev->pci->bar.data +
			SNAP_SAMPLE_DEV_REG_SQPA);
	cq_base = *(uint64_t *)(ctrl->sdev->pci->bar.data +
			SNAP_SAMPLE_DEV_REG_CQPA);

	printf("creating completion queue with base at 0x%0lx\n", cq_base);
	cq_attr.type = SNAP_NVME_RAW_MODE;
	cq_attr.id = 0;
	cq_attr.msix = 0;
	cq_attr.queue_depth = SNAP_SAMPLE_DEV_QUEUE_DEPTH;
	cq_attr.base_addr = cq_base;
	cq_attr.log_entry_size = SNAP_SAMPLE_DEVICE_CQE_LOG;
	ctrl->cq = snap_nvme_create_cq(ctrl->sdev, &cq_attr);
	if (!ctrl->cq) {
		printf("failed to create completion queue\n");
		goto teardown_device;
	}

	printf("creating submission queue with base at 0x%0lx\n", sq_base);
	sq_attr.type = SNAP_NVME_RAW_MODE;
	sq_attr.id = 0;
	sq_attr.queue_depth = SNAP_SAMPLE_DEV_QUEUE_DEPTH;
	sq_attr.base_addr = sq_base;
	sq_attr.cq = ctrl->cq;
	ctrl->sq = snap_nvme_create_sq(ctrl->sdev, &sq_attr);
	if (!ctrl->sq) {
		printf("failed to create submission queue\n");
		goto free_cq;
	}

	/* Create a pair of the connected QPs and attach it to the SQ.
	 * snap-rdma library provides a convinient way to do this
	 * with snap_dma_q_* functions
	 */

	/* One can choose tx size so that it is always possible to send
	 * completions and do dma operations. The rule is:
	 * SNAP_SAMPLE_DEV_QUEUE_DEPTH + n_outstanding_dma_ops * tx_qsize
	 *
	 * Alternative approach is to have tx_qsize big enough that most
	 * operations will complete without waiting.
	 * In this case we must handle -EAGAIN return code from
	 * snap_dma_q_read(), snap_dma_q_write() and snap_dma_q_send_completion()
	 * Typical pattern is to have a pending list which is processed after
	 * the call to the snap_dma_q_progress()
	 */
	dma_q_attr.tx_qsize = SNAP_SAMPLE_DEV_QUEUE_DEPTH;
	/* Must be equal or greater than the SQ size. */
	dma_q_attr.rx_qsize = SNAP_SAMPLE_DEV_QUEUE_DEPTH;
	/* Must be equal to the CQ entry size */
	dma_q_attr.tx_elem_size = sizeof(struct snap_sample_device_cqe);
	/* Must be equal to the SQ entry size */
	dma_q_attr.rx_elem_size = sizeof(struct snap_sample_device_sqe);
	dma_q_attr.rx_cb = ctrl_rx_cb;
	dma_q_attr.uctx = ctrl;

	ctrl->dma_q = snap_dma_q_create(ctrl->pd, &dma_q_attr);
	if (!ctrl->dma_q) {
		printf("failed to create dma queue\n");
		goto free_sq;
	}

	memset(&sq_attr, 0, sizeof(sq_attr));
	sq_attr.qp = snap_dma_q_get_fw_qp(ctrl->dma_q);
	sq_attr.state = SNAP_NVME_SQ_STATE_RDY;

	rc = snap_nvme_modify_sq(ctrl->sq, SNAP_NVME_SQ_MOD_STATE |
			SNAP_NVME_SQ_MOD_QPN, &sq_attr);
	if (rc) {
		printf("failed to modify sq\n");
		goto free_dma_q;
	}

	cm_attr.vtunnel = ctrl->sdev->mdev.vtunnel;
	cm_attr.dma_rkey = ctrl->sdev->dma_rkey;
	cm_attr.vhca_id = snap_get_vhca_id(ctrl->sdev);
	cm_attr.crossed_vhca_mkey = ctrl->sdev->crossed_vhca_mkey;

	ctrl->mkey = snap_create_cross_mkey_by_attr(ctrl->pd, &cm_attr);
	if (!ctrl->mkey) {
		printf("failed to create crossing mkey\n");
		goto sq_to_err;
	}
	printf("dma key is 0x%x\n", ctrl->mkey->mkey);
	ctrl->dma_rkey = ctrl->mkey->mkey;

	return 0;
sq_to_err:
	memset(&sq_attr, 0, sizeof(sq_attr));
	sq_attr.qp = NULL;
	sq_attr.state = SNAP_NVME_SQ_STATE_ERR;
	snap_nvme_modify_sq(ctrl->sq, SNAP_NVME_SQ_MOD_STATE |
			SNAP_NVME_SQ_MOD_QPN, &sq_attr);
free_dma_q:
	snap_dma_q_destroy(ctrl->dma_q);
	ctrl->dma_q = NULL;
free_sq:
	snap_nvme_destroy_sq(ctrl->sq);
	ctrl->sq = NULL;
free_cq:
	snap_nvme_destroy_cq(ctrl->cq);
	ctrl->cq = NULL;
teardown_device:
	snap_nvme_teardown_device(ctrl->sdev);
	ctrl->enabled = false;
	return -1;
}

/**
 * ctrl_cmd_ping_test() - Do ping test
 * @ctrl: sample controller
 *
 * The function initiates a ping test from controller to the host driver.
 * The test has a fixed number of pings on which driver must reply with
 * the pong.
 *
 * The purpose of the test is to show that it is possible to reverse roles
 * of the submission and completion queues.
 *
 * Return: 0 on success or -1 on failure
 */
static int ctrl_cmd_ping_test(struct sample_controller *ctrl)
{
	struct snap_sample_device_cqe cqe = {0};
	int rc;

	if (!ctrl->enabled)
		return 0;

	ctrl->cq_sn++;
	cqe.sn = ctrl->cq_sn;
	printf("start ping test sn %u\n", cqe.sn);
	rc = snap_dma_q_send_completion(ctrl->dma_q, &cqe, sizeof(cqe));
	if (rc)
		printf("Failed to send response\n");

	return rc;
}

static void ctrl_progress_commands(struct sample_controller *ctrl)
{
	struct snap_nvme_device_attr attr = {};
	static unsigned n;
	int rc;
	uint32_t cmd;

	/* fetching bar is a slow operation. Don't do it often */
	if (n++ % 10000000)
		return;

	rc = snap_nvme_query_device(ctrl->sdev, &attr);
	if (rc) {
		printf("Failed to query bar\n");
		return;
	}

	/* FLR happens when controller was enabled but device reports
	 * that it is no longer enabled. Cleanup resources */
	if (ctrl->enabled && !attr.enabled) {
		printf("FLR detected\n");
		ctrl->enabled = false;
		snap_dma_q_destroy(ctrl->dma_q);
		ctrl->dma_q = NULL;
		return;
	}
	ctrl->enabled = attr.enabled;
	ctrl->crossed_vhca_mkey = attr.crossed_vhca_mkey;

	cmd = *(uint32_t *)(ctrl->sdev->pci->bar.data + SNAP_SAMPLE_DEV_REG_CMD);
	if (ctrl->last_cmd == cmd)
		return;

	printf("command is 0x%0x\n", cmd);
	switch (cmd & SNAP_SAMPLE_DEV_CMD_MASK) {
		case SNAP_SAMPLE_DEV_CMD_START:
			rc = ctrl_cmd_start(ctrl);
			break;
		case SNAP_SAMPLE_DEV_CMD_STOP:
			rc = ctrl_cmd_stop(ctrl);
			break;
		case SNAP_SAMPLE_DEV_CMD_RUN_PING_TEST:
			rc = ctrl_cmd_ping_test(ctrl);
			break;
		default:
			printf("Unknown command 0x%0x\n",
			       cmd & SNAP_SAMPLE_DEV_CMD_MASK);
			rc = -1;
	}

	ctrl->last_cmd = cmd;
	if (rc)
		return;

	memset(&attr, 0, sizeof(attr));
	memcpy((uint8_t *)&attr.bar + SNAP_SAMPLE_DEV_REG_CST, &cmd, sizeof(cmd));
	rc = snap_nvme_modify_device(ctrl->sdev, SNAP_NVME_DEV_MOD_BAR_CAP_VS_CSTS, &attr);
	if (rc)
		printf("Failed to write commands response\n");
}

/**
 * ctrl_progress() - Progres sample controller
 * @ctrl: sample controller
 *
 * The function polls device BAR and submission queue
 * for new commands
 */
static void ctrl_progress(struct sample_controller *ctrl)
{
	ctrl_progress_commands(ctrl);
	if (!ctrl->dma_q)
		return;

	snap_dma_q_progress(ctrl->dma_q);
}

int main(int argc, char **argv)
{
	char *rdma_dev;
	static struct sample_controller *ctrl;

	if (argc < 2) {
		printf("Usage: snap_sample_ctrl <emu_dev> [rdma_dev]\n");
		exit(1);
	}

	if (argc == 2)
		rdma_dev = argv[1];
	else
		rdma_dev = argv[2];

	ctrl = ctrl_init(argv[1], rdma_dev);
	if (!ctrl)
		exit(1);

	while (1) {
		ctrl_progress(ctrl);
	}
	ctrl_destroy(ctrl);
	return 0;
}
