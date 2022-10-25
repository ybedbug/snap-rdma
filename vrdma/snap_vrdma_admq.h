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

#ifndef SNAP_VRDMA_ADMQ_H
#define SNAP_VRDMA_ADMQ_H

#include <stdint.h>
#include <infiniband/verbs.h>

/* more states need mlnx to fill */
enum vrdma_dev_state {
	rdev_state_reset = 0x0,
	rdev_state_driver_ok = 0x4,
	rdev_state_need_reset = 0x40,
	rdev_state_driver_error = 0x80, /*when driver encounter err to inform device*/
	rdev_state_max,
};

/* more fields need mlnx to fill */
struct vrdma_dev {
	uint32_t rdev_idx;
	uint64_t rdev_ver;
	enum vrdma_dev_state state;

	char uuid[20];
	char mac[20];
	char veth[64];
	uint8_t gid[16];

	uint32_t input_pkt_num;
	uint32_t output_pkt_num;

} __attribute__((packed));

/*Per app backend resource creation*/
struct vrdma_open_device_req {
};

struct vrdma_open_device_resp {
	uint32_t err_code:8;
	uint32_t err_hint:24;
} __attribute__((packed));

struct vrdma_query_device_req {
};

struct vrdma_query_device_resp {
	char fw_ver[64]; /* FW version */

	uint64_t page_size_cap;/* Supported memory shift sizes */
	uint64_t dev_cap_flags;/* HCA capabilities mask */
	uint32_t vendor_id; /* Vendor ID, per IEEE */
	uint32_t hw_ver; /* Hardware version */
	uint32_t max_pd;/* Maximum number of supported PDs */
	uint32_t max_qp;/* Maximum number of supported QPs */
	uint32_t max_qp_wr; /* Maximum number of outstanding WR on any work queue */
	uint32_t max_cq; /* Maximum number of supported CQs */
	uint32_t max_sq_depth; /* Maximum number of SQE capacity per SQ */
	uint32_t max_rq_depth; /* Maximum number of RQE capacity per RQ */
	uint32_t max_cq_depth; /* Maximum number of CQE capacity per CQ */
	uint32_t max_mr;/* Largest contiguous block that can be registered */
	uint32_t max_ah; /* Maximum number of supported address handles */

	uint16_t max_qp_rd_atom; /* Maximum number of RDMA Read & Atomic operations that can be outstanding per QP */
	uint16_t max_ee_rd_atom; /* Maximum number of RDMA Read & Atomic operations that can be outstanding per EEC */
	uint16_t max_res_rd_atom;/* Maximum number of resources used for RDMA Read & Atomic operations by this HCA as the Target */
	uint16_t max_qp_init_rd_atom;/* Maximum depth per QP for initiation of RDMA Read & Atomic operations */ 
	uint16_t max_ee_init_rd_atom;/* Maximum depth per EEC for initiation of RDMA Read & Atomic operations */
	uint16_t atomic_cap;/* Atomic operations support level */
	uint16_t masked_atomic_cap; /* Masked atomic operations support level */
	uint16_t sub_cqs_per_cq;
	uint16_t max_pkeys;  /* Maximum number of partitions */

	uint32_t err_code:8;
	uint32_t err_hint:24;
} __attribute__((packed));

struct vrdma_query_port_req {
	uint32_t port_idx;
};

struct vrdma_query_port_resp {
	enum ibv_port_state     state; /* Logical port state */
	enum ibv_mtu            max_mtu;/* Max MTU supported by port */
	enum ibv_mtu            active_mtu; /* Actual MTU */
	int                     gid_tbl_len;/* Length of source GID table */
	uint32_t                port_cap_flags; /* Port capabilities */
	uint32_t                max_msg_sz; /* Length of source GID table */
	uint32_t		bad_pkey_cntr; /* Bad P_Key counter */
	uint32_t		qkey_viol_cntr; /* Q_Key violation counter */
	uint32_t		sm_lid; /* SM LID */
	uint32_t		lid; /* Base port LID */
	uint16_t		pkey_tbl_len; /* Length of partition table */
	uint8_t			lmc; /* LMC of LID */
	uint8_t			max_vl_num; /* Maximum number of VLs */
	uint8_t			sm_sl; /* SM service level */
	uint8_t                 active_speed;
	uint8_t          	phys_state;/* Physical port state */
	uint8_t                 link_layer; /* link layer protocol of the port */

	uint32_t err_code:8;
	uint32_t err_hint:24;
} __attribute__((packed));

struct vrdma_query_gid_req {
};

struct vrdma_query_gid_resp {
	uint8_t gid[16];
	uint32_t err_code:8;
	uint32_t err_hint:24;
} __attribute__((packed));

struct vrdma_modify_gid_req {
	uint8_t gid[16];
};

struct vrdma_modify_gid_resp {
	uint32_t err_code:8;
	uint32_t err_hint:24;
} __attribute__((packed));

struct vrdma_create_ceq_req {
	uint32_t depth;
	uint64_t queue_addr;
	uint16_t vector_idx;
} __attribute__((packed));

struct vrdma_create_ceq_resp {
	uint32_t ceq_handle;
	uint32_t err_code:8;
	uint32_t err_hint:24;
} __attribute__((packed));

struct vrdma_modify_ceq_req {
};

struct vrdma_modify_ceq_resp {
};
struct vrdma_destroy_ceq_req {
};

struct vrdma_destroy_ceq_resp {
};

struct vrdma_create_pd_req {
};

struct vrdma_create_pd_resp {
	uint32_t pd_handle;
	uint32_t err_code:8;
	uint32_t err_hint:24;
} __attribute__((packed));

struct vrdma_destroy_pd_req {
	uint32_t pd_handle;
} __attribute__((packed));

struct vrdma_destroy_pd_resp { 
	uint32_t err_code:8;
	uint32_t err_hint:24;
} __attribute__((packed));

struct vrdma_create_mr_req {
	uint32_t pd_handle;
	uint32_t mr_type:3;
	uint32_t access_flags:8;
	uint32_t pagesize:5;
	uint32_t hop:2;
	uint32_t reserved:14;
	uint64_t length; 
	uint64_t vaddr;
	uint32_t sge_count;
	struct vrdma_sge {
		uint64_t pa;
		uint32_t length;
	} sge_list[];
} __attribute__((packed));

struct vrdma_create_mr_resp {
	uint32_t lkey;
	uint32_t rkey;
	uint32_t err_code:8;
	uint32_t err_hint:24;
} __attribute__((packed));

struct vrdma_destroy_mr_req {
	uint32_t lkey;
} __attribute__((packed));

struct vrdma_destroy_mr_resp {
	uint32_t err_code:8;
	uint32_t err_hint:24;
} __attribute__((packed));

struct vrdma_create_cq_req {
	uint32_t cqe_entry_num:16; 
	uint32_t cqe_size:4; 
	uint32_t pagesize:5;
	uint32_t hop:2;
	uint32_t interrupt_mode:1;
	uint32_t reserved:4;
	uint32_t ceq_handle;
	uint64_t l0_pa; 
} __attribute__((packed));

struct vrdma_create_cq_resp {
	uint32_t cq_handle;
	uint32_t err_code:8;
	uint32_t err_hint:24;
} __attribute__((packed));

struct vrdma_destroy_cq_req {
	uint32_t cq_handle;
} __attribute__((packed));

struct vrdma_destroy_cq_resp { 
	uint32_t err_code:8;
	uint32_t err_hint:24;
};

struct vrdma_create_qp_req {
	uint32_t pd_handle;

	uint32_t qp_type:3;
	uint32_t sq_sig_all:1;
	uint32_t sq_wqebb_size:2; /* based on 64 * (sq_wqebb_size + 1) */
	uint32_t sq_pagesize:5; /* 12, 21, 30 | 2 ^ (n) */
	uint32_t sq_hop:2;
	uint32_t rq_wqebb_size:2; /* based on 64 * (rq_wqebb_size + 1) */
	uint32_t rq_pagesize:5; /* 2^n */
	uint32_t rq_hop:2;
	uint32_t reserved:5;

	uint32_t sq_wqebb_cnt:16; /* sqe entry cnt */
	uint32_t rq_wqebb_cnt:16; /* rqe entry cnt */

	uint32_t sq_cqn;
	uint32_t rq_cqn;

	//uint64_t qpc_l0_paddr; /* qpc buffer vm phy addr */
	uint64_t sq_l0_paddr;  /* sqe buffer vm phy addr */
	uint64_t rq_l0_paddr;  /* rqe buffer vm phy addr */
	uint64_t sq_pi_paddr;
	uint64_t rq_pi_paddr;
} __attribute__((packed));

struct vrdma_create_qp_resp {
	uint32_t qp_handle;
	uint32_t err_code:8;
	uint32_t err_hint:24;
} __attribute__((packed));

struct vrdma_destroy_qp_req {
	uint32_t qp_handle;
} __attribute__((packed));

struct vrdma_destroy_qp_resp { 
	uint32_t err_code:8;
	uint32_t err_hint:24;
} __attribute__((packed));

struct vrdma_query_qp_req {
	uint32_t qp_attr_mask;
	uint32_t qp_handle;
} __attribute__((packed));

struct vrdma_query_qp_resp {
	uint32_t qp_state;
	uint32_t rq_psn;
	uint32_t sq_psn;
	uint32_t dest_qp_num;
	uint32_t sq_draining;
	uint32_t qkey;
	uint32_t err_code:8;
	uint32_t err_hint:24;
} __attribute__((packed));

struct vrdma_modify_qp_req {
	uint32_t qp_attr_mask;
	uint32_t qp_handle;
	uint32_t qp_state;
	uint32_t rq_psn;
	uint32_t sq_psn;
	uint32_t dest_qp_num;
	uint32_t sip;
	uint32_t dip;
	uint32_t qkey;
	uint32_t timeout;
	uint32_t min_rnr_timer;
	uint32_t timeout_retry_cnt;
	uint32_t rnr_retry_cnt;
} __attribute__((packed));

struct vrdma_modify_qp_resp { 
	uint32_t err_code:8;
	uint32_t err_hint:24;
} __attribute__((packed));

struct vrdma_create_ah_req {
	uint32_t pd_handle;
    	uint32_t dip;
} __attribute__((packed));

struct vrdma_create_ah_resp {
	uint32_t ah_handle;
	uint32_t err_code:8;
	uint32_t err_hint:24;
} __attribute__((packed));

struct vrdma_destroy_ah_req {
	uint32_t ah_handle;
} __attribute__((packed));

struct vrdma_destroy_ah_resp {
	uint32_t err_code:8;
	uint32_t err_hint:24;
} __attribute__((packed));

struct vrdma_admin_cmd_hdr {
	uint64_t seq;
	uint32_t magic; /* 0xAA88 */
	uint32_t version;
	uint32_t opcode;
} __attribute((packed));

struct vrdma_admin_cmd {
	struct vrdma_admin_cmd_hdr hdr;
	union {
		char buf[512];
		uint64_t cur_seq;
		struct vrdma_query_gid_req query_gid_req;
		struct vrdma_query_gid_resp query_gid_resp;
		struct vrdma_modify_gid_req modify_gid_req;
		struct vrdma_modify_gid_resp modify_gid_resp;
		struct vrdma_create_ceq_req create_ceq_req;
		struct vrdma_create_ceq_resp create_ceq_resp;
		struct vrdma_modify_ceq_req modify_ceq_req;
		struct vrdma_modify_ceq_resp modify_ceq_resp;
		struct vrdma_destroy_ceq_req destroy_ceq_req;
		struct vrdma_destroy_ceq_resp destroy_ceq_resp;
		struct vrdma_create_pd_req create_pd_req;
		struct vrdma_create_pd_resp create_pd_resp;
		struct vrdma_destroy_pd_req destroy_pd_req;
		struct vrdma_destroy_pd_resp destroy_pd_resp;
		struct vrdma_create_mr_req create_mr_req;
		struct vrdma_create_mr_resp create_mr_resp;
		struct vrdma_destroy_mr_req destroy_mr_req;
		struct vrdma_destroy_mr_resp destroy_mr_resp;
		struct vrdma_create_cq_req create_cq_req;
		struct vrdma_create_cq_resp create_cq_resp;
		struct vrdma_destroy_cq_req destroy_cq_req;
		struct vrdma_destroy_cq_resp destroy_cq_resp;
		struct vrdma_create_qp_req create_qp_req;
		struct vrdma_create_qp_resp create_qp_resp;
		struct vrdma_destroy_qp_req destroy_qp_req;
		struct vrdma_destroy_qp_resp destroy_qp_resp;
		struct vrdma_query_qp_req query_qp_req;
		struct vrdma_query_qp_resp query_qp_resp;
		struct vrdma_modify_qp_req modify_qp_req;
		struct vrdma_modify_qp_resp modify_qp_resp;
		struct vrdma_create_ah_req create_ah_req;
		struct vrdma_create_ah_resp create_ah_resp;
		struct vrdma_destroy_ah_req destroy_ah_req;
		struct vrdma_destroy_ah_resp destroy_ah_resp;
	} payload;
	//uint64_t tsc_begin;
} __attribute__((packed));

struct vrdma_modify_gid_req_param {
	uint8_t gid[16];
};

struct vrdma_create_pd_req_param {
	uint32_t pd_handle;  /* pd handle need to be created in vrdev and passed to vservice */
};

struct vrdma_create_mr_req_param {
	uint32_t mr_handle; /* mr handle, lkey, rkey need to be created in vrdev and passed to vservice */
	uint32_t lkey;
	uint32_t rkey;
};

struct vrdma_destroy_mr_req_param {
	uint32_t mr_handle; /* mr handle need to be created in vrdev and passed to vservice */
};

struct vrdma_cmd_param {
	union {
		char buf[12];
		struct vrdma_modify_gid_req_param modify_gid_param;
		struct vrdma_create_pd_req_param create_pd_param;
		struct vrdma_create_mr_req_param create_mr_param;
		struct vrdma_destroy_mr_req_param destroy_mr_param;
	}param;
};

// based on mlx sqe
struct sqe {
} __attribute__((packed));

// based on mlx rqe
struct rqe {
	
} __attribute__((packed));

// based on mlx cqe
struct cqe {
	
} __attribute__((packed));

// based on mlx eqe
struct ceqe {
	
} __attribute__((packed));


#endif
