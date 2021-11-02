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

#ifndef SNAP_NVME_H
#define SNAP_NVME_H

#include <stdlib.h>

#include <infiniband/verbs.h>
#include <infiniband/mlx5dv.h>
#include <pthread.h>
#include <linux/types.h>

#include "snap.h"

struct snap_nvme_device;
struct snap_nvme_cq;
struct mlx5dv_devx_obj;

struct snap_nvme_namespace_attr {
	int			src_nsid;
	int			dst_nsid;
	int			lba_size;
	int			md_size;
};

struct snap_nvme_namespace {
	struct mlx5_snap_devx_obj		*ns;
	int					src_id;
	int					dst_id;

	TAILQ_ENTRY(snap_nvme_namespace)	entry;
};

enum snap_nvme_queue_type {
	SNAP_NVME_RAW_MODE	= 1 << 0,
	SNAP_NVME_TO_NVMF_MODE	= 1 << 1,
};

enum snap_nvme_sq_modify {
	SNAP_NVME_SQ_MOD_QPN	= 1 << 0,
	SNAP_NVME_SQ_MOD_STATE	= 1 << 1,
};

enum snap_nvme_sq_state {
	SNAP_NVME_SQ_STATE_INIT	= 1 << 0,
	SNAP_NVME_SQ_STATE_RDY	= 1 << 1,
	SNAP_NVME_SQ_STATE_ERR	= 1 << 2,
};

struct snap_nvme_sq_attr {
	enum snap_nvme_queue_type	type;
	uint32_t			id;
	uint16_t			queue_depth;
	uint64_t			base_addr;
	uint64_t			modifiable_fields;//mask of snap_nvme_sq_modify
	struct ibv_qp			*qp;
	struct snap_nvme_cq		*cq;
	enum snap_nvme_sq_state		state;
	uint8_t				log_entry_size;
	uint32_t			counter_set_id;
	bool				fe_only;
};

struct snap_nvme_sq_be;

struct snap_nvme_sq {
	struct ibv_context			*rdma_dev;
	uint32_t				id;
	struct mlx5_snap_devx_obj		*sq;
	struct mlx5_snap_hw_qp			*hw_qp;
	uint64_t				mod_allowed_mask;
	struct snap_nvme_sq_be			*sq_be;
};

struct snap_nvme_sq_be_attr {
	struct snap_nvme_sq *sq;
	struct ibv_qp *qp;
};

struct snap_nvme_cq_attr {
	enum snap_nvme_queue_type	type;
	uint32_t			id;
	uint16_t			msix;
	uint16_t			queue_depth;
	uint64_t			base_addr;
	uint16_t			cq_period;
	uint16_t			cq_max_count;
	uint8_t				log_entry_size;
	bool				interrupt_disable;
};

struct snap_nvme_cq {
	uint32_t				id;
	struct mlx5_snap_devx_obj		*cq;
};

enum snap_nvme_device_modify {
	SNAP_NVME_DEV_MOD_BAR_CAP_VS_CSTS	= 1 << 0,
	SNAP_NVME_DEV_MOD_BAR_CC	= 1 << 1,
	SNAP_NVME_DEV_MOD_BAR_AQA_ASQ_ACQ	= 1 << 2,
	SNAP_NVME_DEV_MOD_HOTPLUG_STATE = 1 << 3,
};

struct snap_nvme_device_attr {
	bool					enabled;
	struct snap_nvme_registers		bar;
	uint64_t				modifiable_fields;//mask of snap_nvme_device_modify
	uint16_t				num_of_vfs;
	uint32_t				crossed_vhca_mkey;
	uint8_t					pci_hotplug_state;
};

struct snap_nvme_device {
	struct snap_device			*sdev;
	uint32_t				num_queues;
	uint32_t				db_base;

	pthread_mutex_t				lock;

	TAILQ_HEAD(, snap_nvme_namespace)	ns_list;

	struct snap_nvme_cq			*cqs;
	struct snap_nvme_sq			*sqs;
};

struct snap_nvme_sq_counters {
	uint32_t data_read;
	uint32_t data_write;
	uint16_t cmd_read;
	uint16_t cmd_write;
	uint32_t error_cqes;
	uint16_t integrity_errors;
	uint16_t fabric_errors;
	uint16_t busy_time;
	uint16_t power_cycle;
	uint16_t power_on_hours;
	uint16_t unsafe_shutdowns;
	uint16_t error_information_log_entries;

	struct mlx5_snap_devx_obj *obj;
};

struct snap_nvme_ctrl_counters {
	uint32_t data_read;
	uint32_t data_write;
	uint16_t cmd_read;
	uint16_t cmd_write;
	uint32_t error_cqes;
	uint32_t flrs;
	uint32_t bad_doorbells;
	uint16_t integrity_errors;
	uint16_t fabric_errors;
	uint16_t busy_time;
	uint16_t power_cycle;
	uint16_t power_on_hours;
	uint16_t unsafe_shutdowns;
	uint16_t error_information_log_entries;

	struct mlx5dv_devx_obj *obj;
	uint32_t id;
};

int snap_nvme_init_device(struct snap_device *sdev);
int snap_nvme_teardown_device(struct snap_device *sdev);
int snap_nvme_query_device(struct snap_device *sdev,
	struct snap_nvme_device_attr *attr);
int snap_nvme_modify_device(struct snap_device *sdev, uint64_t mask,
		struct snap_nvme_device_attr *attr);

struct snap_nvme_namespace*
snap_nvme_create_namespace(struct snap_device *sdev,
		struct snap_nvme_namespace_attr *attr);
int snap_nvme_destroy_namespace(struct snap_nvme_namespace *ns);
struct snap_nvme_cq*
snap_nvme_create_cq(struct snap_device *sdev, struct snap_nvme_cq_attr *attr);
int snap_nvme_destroy_cq(struct snap_nvme_cq *cq);
struct snap_nvme_sq*
snap_nvme_create_sq(struct snap_device *sdev, struct snap_nvme_sq_attr *attr);
int snap_nvme_destroy_sq(struct snap_nvme_sq *sq);
int snap_nvme_query_sq(struct snap_nvme_sq *sq,
	struct snap_nvme_sq_attr *attr);
int snap_nvme_modify_sq(struct snap_nvme_sq *sq, uint64_t mask,
	struct snap_nvme_sq_attr *attr);
struct snap_nvme_sq_be *
snap_nvme_create_sq_be(struct snap_device *sdev,
		       struct snap_nvme_sq_be_attr *attr);
void snap_nvme_destroy_sq_be(struct snap_nvme_sq_be *sq_be);
bool snap_nvme_sq_is_fe_only(struct snap_nvme_sq *sq);

struct snap_nvme_sq_counters*
snap_nvme_create_sq_counters(struct snap_device *sdev);
int snap_nvme_query_sq_counters(struct snap_nvme_sq_counters *sqc);
int snap_nvme_destroy_sq_counters(struct snap_nvme_sq_counters *sqc);

struct snap_nvme_ctrl_counters*
snap_nvme_create_ctrl_counters(struct snap_context *sctx);
int snap_nvme_query_ctrl_counters(struct snap_nvme_ctrl_counters *ctrlc);
int snap_nvme_destroy_ctrl_counters(struct snap_nvme_ctrl_counters *ctrlc);

void snap_nvme_pci_functions_cleanup(struct snap_context *sctx);
#endif
