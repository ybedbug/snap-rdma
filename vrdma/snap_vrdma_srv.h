/*
 * Copyright © 2022 NVIDIA CORPORATION & AFFILIATES. ALL RIGHTS RESERVED.
 *
 * This software product is a proprietary product of Nvidia Corporation and its affiliates
 * (the "Company") and all right, title, and interest in and to the software
 * product, including all associated intellectual property rights, are and
 * shall remain exclusively with the Company.
 *
 * This software product is governed by the End User License Agreement
 * provided with the software product.
 */

#ifndef SNAP_VRDMA_SRV_H
#define SNAP_VRDMA_SRV_H

#include <stdint.h>
#include <infiniband/verbs.h>
#include "snap_vrdma_admq.h"

typedef int (*vrdma_device_notify_op)(struct vrdma_dev *rdev);

typedef int (*vrdma_admin_query_gid_op)(struct vrdma_dev *rdev, 
										struct vrdma_admin_cmd *cmd);
typedef int (*vrdma_admin_modify_gid_op)(struct vrdma_dev *rdev, 
										struct vrdma_admin_cmd *cmd,
										struct vrdma_cmd_param *param);
typedef int (*vrdma_admin_create_eq_op)(struct vrdma_dev *rdev, 
										struct vrdma_admin_cmd *cmd);
typedef int (*vrdma_admin_modify_eq_op)(struct vrdma_dev *rdev, 
										struct vrdma_admin_cmd *cmd);
typedef int (*vrdma_admin_destroy_eq_op)(struct vrdma_dev *rdev, 
										struct vrdma_admin_cmd *cmd);
typedef int (*vrdma_admin_create_pd_op)(struct vrdma_dev *rdev, 
										struct vrdma_admin_cmd *cmd, 
										struct vrdma_cmd_param *param);
typedef int (*vrdma_admin_destroy_pd_op)(struct vrdma_dev *rdev, 
										struct vrdma_admin_cmd *cmd);
typedef int (*vrdma_admin_create_mr_op)(struct vrdma_dev *rdev, 
										struct vrdma_admin_cmd *cmd, 
										struct vrdma_cmd_param *param);
typedef int (*vrdma_admin_destroy_mr_op)(struct vrdma_dev *rdev, 
										struct vrdma_admin_cmd *cmd, 
										struct vrdma_cmd_param *param);
typedef int (*vrdma_admin_create_cq_op)(struct vrdma_dev *rdev, 
										struct vrdma_admin_cmd *cmd);
typedef int (*vrdma_admin_destroy_cq_op)(struct vrdma_dev *rdev, 
										struct vrdma_admin_cmd *cmd);
typedef int (*vrdma_admin_create_qp_op)(struct vrdma_dev *rdev, 
										struct vrdma_admin_cmd *cmd);
typedef int (*vrdma_admin_destroy_qp_op)(struct vrdma_dev *rdev, 
										struct vrdma_admin_cmd *cmd);
typedef int (*vrdma_admin_query_qp_op)(struct vrdma_dev *rdev, 
										struct vrdma_admin_cmd *cmd, 
										struct vrdma_cmd_param *param);
typedef int (*vrdma_admin_modify_qp_op)(struct vrdma_dev *rdev, 
										struct vrdma_admin_cmd *cmd);
typedef int (*vrdma_admin_create_ah_op)(struct vrdma_dev *rdev, 
										struct vrdma_admin_cmd *cmd);
typedef int (*vrdma_admin_destroy_ah_op)(struct vrdma_dev *rdev, 
										struct vrdma_admin_cmd *cmd);

/* vrdma ops call back exposed to vrdma device */
typedef struct vRdmaServiceOps {
    /* device notify state (probing) to vrdma service */
	vrdma_device_notify_op vrdma_device_notify;
    /* admin callback */
	vrdma_admin_query_gid_op vrdma_device_query_gid;
	vrdma_admin_modify_gid_op vrdma_device_modify_gid;
	vrdma_admin_create_eq_op vrdma_device_create_eq;
	vrdma_admin_modify_eq_op vrdma_device_modify_eq;
	vrdma_admin_destroy_eq_op vrdma_device_destroy_eq;
	vrdma_admin_create_pd_op vrdma_device_create_pd;
	vrdma_admin_destroy_pd_op vrdma_device_destroy_pd;
	vrdma_admin_create_mr_op vrdma_device_create_mr;
	vrdma_admin_destroy_mr_op vrdma_device_destroy_mr;
	vrdma_admin_create_cq_op vrdma_device_create_cq;
	vrdma_admin_destroy_cq_op vrdma_device_destroy_cq;
	vrdma_admin_create_qp_op vrdma_device_create_qp;
	vrdma_admin_destroy_qp_op vrdma_device_destroy_qp;
	vrdma_admin_query_qp_op vrdma_device_query_qp;
	vrdma_admin_modify_qp_op vrdma_device_modify_qp;
	vrdma_admin_create_ah_op vrdma_device_create_ah;
	vrdma_admin_destroy_ah_op vrdma_device_destroy_ah;
} vRdmaServiceOps;

// Assume vrdma service checks the pi,ci boundaries.
// Fetch SQ WQEs
// ret: address of sq wqe
void * fetch_sq_wqe(struct vrdma_dev *dev, uint32_t qp_handle, uint32_t idx); 

//return the number of wqes vdev can provide, maybe less than num param
uint16_t fetch_sq_wqe_batch(struct vrdma_dev *dev, uint32_t qp_handle, uint32_t idx, uint16_t num, void* swqe_head); 

// Fetch RQ WQEs
// ret: address of rq wqe
void * fetch_rq_wqe(struct vrdma_dev *dev, uint32_t qp_handle, uint32_t idx);

//return the number of wqes vdev can provide, maybe less than num param
uint16_t fetch_rq_wqe_batch(struct vrdma_dev *dev, uint32_t qp_handle, uint32_t idx, uint16_t num, void* swqe_head);

// Generate a CQE
// ret: struct cqe * 
bool gen_cqe(struct vrdma_dev *dev, uint32_t cq_handle, struct cqe * c);

//assume the cqes are continuous
//return the number of wqes vdev can provide, maybe less than num param
uint16_t gen_cqe_batch(struct vrdma_dev *dev, uint32_t cq_handle, uint32_t idx, uint16_t num, struct cqe * c, bool *is_succ);

// Generate EQE Element
// ret: struct eqe * 
bool gen_ceqe(struct vrdma_dev *dev, uint32_t ceq_handle, struct ceqe * e); 
//batch
//return the number of wqes vdev can provide, maybe less than num param
uint16_t gen_ceqe_batch(struct vrdma_dev *dev, uint32_t ceq_handle, uint32_t idx, uint16_t num, struct cqe * e, bool *is_succ);

// Generate Interrupt for CEQ:
bool gen_ceq_msi(struct vrdma_dev *dev, uint32_t cqe_vector);

// Get SQ PI
//RQ PI should be an attribute cached in vdev.qp.rq, to avoid read from host mem dbr every time
uint16_t snap_vrdma_get_sq_pi(struct vrdma_dev *dev, uint32_t qp_handle);

// Get RQ PI
//RQ PI should be an attribute cached in vdev.qp.rq, to avoid read from host mem dbr every time
uint16_t get_rq_pi(struct vrdma_dev *dev, uint32_t qp_handle);

// Get CEQ CI
//EQ CI should be an attribute cached in vdev.eq, to avoid read from host mem dbr every time
uint16_t get_eq_ci(struct vrdma_dev *dev, uint32_t eq_handle);

// Replicate data from HostMemory toSoCMemory
bool mem_move_h2d(struct vrdma_dev *dev, void *src, uint32_t skey, void *dst, int32_t dkey, size_t len);

// Replicate data from SoCMemory to HostMemory
bool mem_move_d2h(struct vrdma_dev *dev, uint32_t skey, void *src, uint32_t dkey, void *dst, size_t len);

#endif
