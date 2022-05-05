/*
 * Copyright © 2021 NVIDIA CORPORATION & AFFILIATES. ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of Nvidia Corporation and its affiliates
 * (the "Company") and all right, title, and interest in and to the software
 * product, including all associated intellectual property rights, are and
 * shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 */

#define _ISOC11_SOURCE //For aligned_alloc

#include <stdlib.h>
#include "snap_vq_adm.h"
#include "snap_vq_internal.h"
#include "snap_macros.h"
#include "snap_buf.h"

struct snap_vq_adm;

struct snap_vaq_cmd {
	struct snap_vq_adm *q;
	struct snap_vq_cmd common;
	struct snap_virtio_adm_cmd_layout *layout;
};

struct snap_vq_adm {
	struct snap_vaq_cmd *cmds;
	struct snap_virtio_adm_cmd_layout *cmd_layouts;
	struct snap_vq vq;
	snap_vq_process_fn_t adm_process_fn;
	struct ibv_pd *pd;
};

static struct snap_vaq_cmd *to_snap_vaq_cmd(struct snap_vq_cmd *cmd)
{
	return container_of(cmd, struct snap_vaq_cmd, common);
}

static struct snap_vq_adm *to_snap_vq_adm(struct snap_vq *vq)
{
	return container_of(vq, struct snap_vq_adm, vq);
}

static size_t snap_vaq_cmd_in_get_len(struct snap_vaq_cmd *cmd)
{
	struct snap_virtio_adm_cmd_hdr hdr = cmd->layout->hdr;
	size_t ret = 0;

	switch (hdr.class) {
	case SNAP_VQ_ADM_MIG_CTRL:
		switch (hdr.command) {
		case SNAP_VQ_ADM_MIG_GET_STATUS:
			ret = sizeof(struct snap_vq_adm_get_status_data);
			break;
		case SNAP_VQ_ADM_MIG_MODIFY_STATUS:
			ret = sizeof(struct snap_vq_adm_modify_status_data);
			break;
		case SNAP_VQ_ADM_MIG_GET_STATE_PENDING_BYTES:
			ret = sizeof(struct snap_vq_adm_get_pending_bytes_data);
			break;
		case SNAP_VQ_ADM_MIG_SAVE_STATE:
			ret = sizeof(struct snap_vq_adm_save_state_data);
			break;
		case SNAP_VQ_ADM_MIG_RESTORE_STATE:
			ret = sizeof(struct snap_vq_adm_restore_state_data);
			break;
		default:
			break;
		break;
		}
	case SNAP_VQ_ADM_DP_TRACK_CTRL:
		switch (hdr.command) {
		case SNAP_VQ_ADM_DP_START_TRACK:
		case SNAP_VQ_ADM_DP_STOP_TRACK:
		case SNAP_VQ_ADM_DP_GET_MAP_PENDING_BYTES:
		case SNAP_VQ_ADM_DP_REPORT_MAP:
		default:
			break;
		}
		break;
	default:
		break;
	}

	return ret;
}

static size_t snap_vaq_cmd_out_get_len(struct snap_vaq_cmd *cmd)
{
	struct snap_virtio_adm_cmd_hdr hdr = cmd->layout->hdr;
	size_t ret = 0;

	switch (hdr.class) {
	case SNAP_VQ_ADM_MIG_CTRL:
		switch (hdr.command) {
		case SNAP_VQ_ADM_MIG_GET_STATUS:
			ret = sizeof(struct snap_vq_adm_get_status_result);
			break;
		case SNAP_VQ_ADM_MIG_GET_STATE_PENDING_BYTES:
			ret = sizeof(struct snap_vq_adm_get_pending_bytes_result);
			break;
		default:
			break;
		break;
		}
	case SNAP_VQ_ADM_DP_TRACK_CTRL:
		switch (hdr.command) {
		case SNAP_VQ_ADM_DP_START_TRACK:
		case SNAP_VQ_ADM_DP_STOP_TRACK:
		case SNAP_VQ_ADM_DP_GET_MAP_PENDING_BYTES:
		case SNAP_VQ_ADM_DP_REPORT_MAP:
		default:
			break;
		}
		break;
	default:
		break;
	}
	return ret;
}

/**
 * snap_vaq_cmd_layout_data_read() - Read data from host memory.
 * @cmd: command context
 * @total_len: length to be read
 * @lbuf: local buffer to read data into
 * @lbuf_mkey: lkey to access local buffer
 * @done_fn: callback function to be called when finished.
 * @layout_offset: offset bytes from beginning of descs to start reading after
 *
 * The function asynchronously reads data from host memory into
 * a local buffer. When data is ready, done_fn() callback is called.
 *
 * Context: On virtio admin queue context, this function should be called
 *          by the command processor (typically virtio controller) to
 *          handle "restore state" API.
 *
 * Return: 0 on success, -errno otherwise.
 */
int snap_vaq_cmd_layout_data_read(struct snap_vq_cmd *cmd, size_t total_len,
				void *lbuf, uint32_t lbuf_mkey,
				snap_vq_cmd_done_cb_t done_fn,
				size_t layout_offset)
{
	struct snap_vq_cmd_desc *desc = NULL;

	/* Set desc to descriptor after offset bytes form start of descs */
	TAILQ_FOREACH(desc, snap_vq_cmd_get_descs(cmd), entry) {
		if (desc->desc.len > layout_offset)
			break;
		layout_offset -= desc->desc.len;
	}

	return snap_vq_cmd_descs_rw(cmd, desc, layout_offset, lbuf, total_len,
				lbuf_mkey, done_fn, false);
}

/**
 * snap_vaq_cmd_layout_data_write() - Write data to host memory.
 * @cmd: command context
 * @total_len: length to be read
 * @lbuf: local buffer to read data into
 * @lbuf_mkey: lkey to access local buffer
 * @done_fn: callback function to be called when finished.
 *
 * The function asynchronously writes data from local buffer
 * into host memory. When finished, done_fn() callback is called.
 *
 * Context: On virtio admin queue context, this function should be called
 *          by the command processor (typically virtio controller) to
 *          handle "save state" API.
 *
 * Return: 0 on success, -errno otherwise.
 */
int snap_vaq_cmd_layout_data_write(struct snap_vq_cmd *cmd, size_t total_len,
				void *lbuf, uint32_t lbuf_mkey,
				snap_vq_cmd_done_cb_t done_fn)
{
	struct snap_vq_cmd_desc *desc;

	/* Set desc to be first descriptor we can write to */
	TAILQ_FOREACH(desc, snap_vq_cmd_get_descs(cmd), entry) {
		if ((desc->desc.flags & VRING_DESC_F_WRITE))
			break;
	}
	/*
	 * Note first_offset is 0 because we are writing to the first writable desc
	 * and we dont need to write out more than once for any command.
	 */
	return snap_vq_cmd_descs_rw(cmd, desc, 0, lbuf, total_len, lbuf_mkey,
				 done_fn, true);
}


static inline
int snap_vaq_cmd_wb_cmd_out(struct snap_vaq_cmd *cmd)
{
	int out_len, ret, len;
	struct snap_vq_cmd_desc *desc;
	char *laddr;

	out_len = snap_vaq_cmd_out_get_len(cmd);
	if (!out_len)
		return 0;

	/* Use first desc with write flag */
	TAILQ_FOREACH(desc, &cmd->common.descs, entry) {
		if ((desc->desc.flags & VRING_DESC_F_WRITE))
			break;
	}

	laddr = (char *)&cmd->layout->out;
	while (out_len > 0 && desc) {
		len = snap_min(out_len, desc->desc.len);
		ret = snap_dma_q_write_short(cmd->common.vq->dma_q,
					    laddr, len, desc->desc.addr,
					    cmd->common.vq->xmkey);
		if (snap_unlikely(ret))
			return ret;

		cmd->common.len += len;
		laddr += len;
		out_len -= len;
		desc = TAILQ_NEXT(desc, entry);
	}

	return out_len;
}

static int snap_vaq_cmd_wb_ftr(struct snap_vaq_cmd *cmd)
{
	const struct snap_vq_cmd_desc *last;
	int ret;
	struct snap_virtio_adm_cmd_ftr *ftr = &cmd->layout->ftr;
	uint64_t raddr;

	/*
	 * Footer is in a single desc since it is 1 byte in size.
	 * The last byte in the last desc is the footer
	 */
	last = TAILQ_LAST(&cmd->common.descs, snap_vq_cmd_desc_list);
	raddr = last->desc.addr + last->desc.len - sizeof(*ftr);
	ret = snap_dma_q_write_short(cmd->common.vq->dma_q, ftr,
				sizeof(*ftr), raddr,
				cmd->common.vq->xmkey);
	if (snap_unlikely(ret))
		return ret;

	cmd->common.len += sizeof(*ftr);
	return 0;
}

static int snap_vaq_cmd_read_hdr(struct snap_vaq_cmd *cmd,
				snap_vq_cmd_done_cb_t done_fn)
{
	struct snap_vq_cmd_desc *desc;

	desc = TAILQ_FIRST(&cmd->common.descs);
	return snap_vq_cmd_descs_rw(&cmd->common, desc, 0, &cmd->layout->hdr,
				 sizeof(struct snap_virtio_adm_cmd_hdr),
				snap_buf_get_mkey(cmd->q->cmd_layouts),
				done_fn, false);
}

static int snap_vaq_cmd_read_cmd_in(struct snap_vaq_cmd *cmd,
					snap_vq_cmd_done_cb_t done_fn)
{
	size_t offset = 0, in_len;

	offset = sizeof(struct snap_virtio_adm_cmd_hdr);
	in_len = snap_vaq_cmd_in_get_len(cmd);
	if (!in_len) {
		done_fn(&cmd->common, IBV_WC_SUCCESS);
		return 0;
	}

	return snap_vaq_cmd_layout_data_read(&cmd->common, in_len, &cmd->layout->in,
				  snap_buf_get_mkey(cmd->q->cmd_layouts),
				   done_fn, offset);
}

static void snap_vaq_cmd_create(struct snap_vq_adm *q,
				struct snap_vaq_cmd *cmd,
				struct snap_virtio_adm_cmd_layout *layout)
{
	cmd->q = q;
	cmd->layout = layout;
	snap_vq_cmd_create(&q->vq, &cmd->common);
}

static void snap_vaq_cmd_destroy(struct snap_vaq_cmd *cmd)
{
	snap_vq_cmd_destroy(&cmd->common);
}

static struct snap_vq_cmd *snap_vq_adm_create_cmd(struct snap_vq *vq, int index)
{
	struct snap_vq_adm *q = to_snap_vq_adm(vq);

	snap_vaq_cmd_create(q, &q->cmds[index], &q->cmd_layouts[index]);
	return &q->cmds[index].common;
}

static void snap_vq_adm_read_cmd_in_done(struct snap_vq_cmd *vcmd,
					enum ibv_wc_status status)
{
	struct snap_vaq_cmd *cmd = to_snap_vaq_cmd(vcmd);

	if (snap_unlikely(status != IBV_WC_SUCCESS))
		return snap_vaq_cmd_complete(vcmd, SNAP_VIRTIO_ADM_STATUS_ERR);

	/*
	 * Admin commands further processing should be done by the caller
	 * (AKA virtio controller).
	 */
	cmd->q->adm_process_fn(vcmd->vq->vctrl, vcmd);
}

static void snap_vq_adm_read_hdr_done(struct snap_vq_cmd *vcmd,
					enum ibv_wc_status status)
{
	struct snap_vaq_cmd *cmd = to_snap_vaq_cmd(vcmd);
	int ret;

	if (snap_unlikely(status != IBV_WC_SUCCESS))
		return snap_vaq_cmd_complete(vcmd, SNAP_VIRTIO_ADM_STATUS_ERR);

	ret = snap_vaq_cmd_read_cmd_in(cmd, snap_vq_adm_read_cmd_in_done);
	if (ret)
		return snap_vaq_cmd_complete(vcmd, SNAP_VIRTIO_ADM_STATUS_ERR);
}

static void snap_vq_adm_handle_cmd(struct snap_vq_cmd *vcmd)
{
	struct snap_vaq_cmd *cmd = to_snap_vaq_cmd(vcmd);
	int ret;

	ret = snap_vaq_cmd_read_hdr(cmd, snap_vq_adm_read_hdr_done);
	if (ret)
		return snap_vaq_cmd_complete(vcmd, SNAP_VIRTIO_ADM_STATUS_ERR);
}

static void snap_vq_adm_delete_cmd(struct snap_vq_cmd *vcmd)
{
	struct snap_vaq_cmd *cmd = to_snap_vaq_cmd(vcmd);

	snap_vaq_cmd_destroy(cmd);
}

static const struct snap_vq_cmd_ops snap_vq_adm_cmd_ops = {
	.hdr_size = sizeof(struct snap_virtio_adm_cmd_hdr),
	.ftr_size = snap_max(sizeof(union snap_virtio_adm_cmd_out),
				sizeof(struct snap_virtio_adm_cmd_ftr)),
	.create = snap_vq_adm_create_cmd,
	.handle = snap_vq_adm_handle_cmd,
	.delete = snap_vq_adm_delete_cmd,
	.prefetch = NULL,
};

/**
 * snap_vq_adm_create() - Create new virtio admin queue instance
 * @attr: creation attributes.
 *
 * Creates a new instance of virtio admin queue.
 *
 * Return: virtqueue instance upon success, NULL otherwise.
 */
struct snap_vq *snap_vq_adm_create(struct snap_vq_adm_create_attr *attr)
{
	struct snap_vq_adm *q;
	const size_t cmd_arr_sz = attr->common.size * sizeof(*q->cmds);

	q = calloc(1, sizeof(*q));
	if (!q)
		goto err;

	q->cmds = aligned_alloc(SNAP_DCACHE_LINE, cmd_arr_sz);
	if (!q->cmds)
		goto free_q;
	memset(q->cmds, 0, cmd_arr_sz);

	q->cmd_layouts = snap_buf_alloc(attr->common.pd,
				attr->common.size * sizeof(*q->cmd_layouts));
	if (!q->cmd_layouts)
		goto free_cmds;

	if (snap_vq_create(&q->vq, &attr->common,
					&snap_vq_adm_cmd_ops))
		goto free_layouts;

	q->pd = attr->common.pd;
	q->adm_process_fn = attr->adm_process_fn;
	return &q->vq;

free_layouts:
	snap_buf_free(q->cmd_layouts);
free_cmds:
	free(q->cmds);
free_q:
	free(q);
err:
	return NULL;
}

/**
 * snap_vq_adm_destroy() - Destroy a virtio admin queue instance.
 * @q: queue to destroy
 *
 * Destroys a previously created admin virtqueue instance
 */
void snap_vq_adm_destroy(struct snap_vq *vq)
{
	struct snap_vq_adm *q = to_snap_vq_adm(vq);

	snap_vq_destroy(vq);
	snap_buf_free(q->cmd_layouts);
	free(q->cmds);
	free(q);
}

/**
 * snap_vaq_cmd_complete() - complete virtio admin command
 * @cmd: Command to complete
 *
 * Complete virtio admin command. The function writes back to host memory
 * the response data and footer according to virtio admin command layout.
 * command processing stage to get the layout according to virtio spec.
 *
 * Context: After calling this function, command cannot be further processed,
 *          as command's struct may already be reused.
 */
void snap_vaq_cmd_complete(struct snap_vq_cmd *vcmd,
				enum snap_virtio_adm_status status)
{
	int ret;
	struct snap_vaq_cmd *cmd = to_snap_vaq_cmd(vcmd);

	if (status == SNAP_VIRTIO_ADM_STATUS_OK) {
		ret = snap_vaq_cmd_wb_cmd_out(cmd);
		if (snap_unlikely(ret))
			status = SNAP_VIRTIO_ADM_STATUS_ERR;
	}

	cmd->layout->ftr.status = status;
	ret = snap_vaq_cmd_wb_ftr(cmd);
	if (snap_unlikely(ret)) {
		snap_vq_cmd_fatal(vcmd);
		return;
	}
	snap_vq_cmd_complete(vcmd);
}

/**
 * snap_vaq_cmd_layout_get() - Get snap virtio admin command's layout
 * @cmd: command to get layout from
 *
 * Get snap virtio command's layout. Should be called during
 * command processing stage to get the layout according to virtio spec.
 *
 * Return: admin command's layout.
 */
inline struct snap_virtio_adm_cmd_layout *
snap_vaq_cmd_layout_get(struct snap_vq_cmd *vcmd)
{
	struct snap_vaq_cmd *cmd = to_snap_vaq_cmd(vcmd);

	return cmd->layout;
}

/**
 * snap_vaq_cmd_ctrl_get() - Get snap virtio admin command's ctrl
 * @cmd: command to get ctrl from
 *
 * Get snap virtio command's ctrl. Should be called during
 * command processing stage to get the ctrl.
 *
 * Return: admin command vq's ctrl .
 */
inline struct snap_virtio_ctrl *
snap_vaq_cmd_ctrl_get(struct snap_vq_cmd *vcmd)
{
	return vcmd->vq->vctrl;
}
