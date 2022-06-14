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

#ifndef SNAP_VQ_ADM_H
#define SNAP_VQ_ADM_H
#include <stdint.h>
#include "snap_vq.h"
#include "snap_virtio_adm_spec.h"

struct snap_vq_adm;

/**
 * struct snap_vq_adm_create_attr - snap admin VQ creation attributes
 * @common: common VQ attributes
 *
 * Describes all required/optional attribute used for admin virtqueue
 * creation process.
 */
struct snap_vq_adm_create_attr {
	struct snap_vq_create_attr common;
	snap_vq_process_fn_t adm_process_fn;
};

struct snap_vq *snap_vq_adm_create(struct snap_vq_adm_create_attr *attr);
void snap_vq_adm_destroy(struct snap_vq *q);

struct snap_virtio_adm_cmd_layout *
snap_vaq_cmd_layout_get(struct snap_vq_cmd *cmd);
struct snap_virtio_ctrl *
snap_vaq_cmd_ctrl_get(struct snap_vq_cmd *cmd);

struct snap_dma_q *
snap_vaq_cmd_dmaq_get(struct snap_vq_cmd *cmd);
void snap_vaq_cmd_complete(struct snap_vq_cmd *vcmd,
				enum snap_virtio_adm_status status);
void **snap_vaq_cmd_priv(struct snap_vq_cmd *cmd);

int snap_vaq_cmd_layout_data_read(struct snap_vq_cmd *cmd, size_t total_len,
			void *lbuf, uint32_t lbuf_mkey,
			snap_vq_cmd_done_cb_t done_fn, size_t layout_offset);

int snap_vaq_cmd_layout_data_write(struct snap_vq_cmd *cmd, size_t total_len,
			void *lbuf, uint32_t lbuf_mkey,
			snap_vq_cmd_done_cb_t done_fn);

size_t snap_vaq_cmd_get_total_len(struct snap_vq_cmd *cmd);
#endif
