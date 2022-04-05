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

#ifndef DPA_SNAP_DMA_COMPAT_H
#define DPA_SNAP_DMA_COMPAT_H

/* This file contains all 'missing' pieces that we need to build snap_dma
 * on DPA. At the moment it looks like a much simpler approach than working
 * with standard/rdma-core headers.
 */

typedef unsigned long size_t;
/* from sys/uio.h */

/* Structure for scatter/gather I/O.  */
struct iovec {
	void *iov_base; /* Pointer to data.  */
	size_t iov_len; /* Length of data.  */
};

/* from infiniband/verbs.h */
struct ibv_context;
struct ibv_device;

struct ibv_sge {
	uint64_t		addr;
	uint32_t		length;
	uint32_t		lkey;
};

/* prereqs for infiniband/mlx5dv.h */
typedef uint16_t __be16;
typedef uint32_t __be32;
typedef uint64_t __be64;

/* from endian.h */
static inline uint32_t htobe32(uint32_t host_32bits)
{
	return __builtin_bswap32(host_32bits);
}

static inline uint64_t htobe64(uint64_t host_64bits)
{
	return __builtin_bswap64(host_64bits);
}

static inline uint16_t be16toh(uint16_t big_endian_16bits)
{
	return __builtin_bswap16(big_endian_16bits);
}

static inline uint32_t be32toh(uint32_t big_endian_32bits)
{
	return __builtin_bswap32(big_endian_32bits);
}

/* from infiniband/mlx5dv.h */

/* Always inline the functions */
#ifdef __GNUC__
#define MLX5DV_ALWAYS_INLINE inline __attribute__((always_inline))
#else
#define MLX5DV_ALWAYS_INLINE inline
#endif

enum {
	MLX5_RCV_DBR	= 0,
	MLX5_SND_DBR	= 1,
};

enum {
	MLX5_WQE_CTRL_CQ_UPDATE	= 2 << 2,
	MLX5_WQE_CTRL_SOLICITED	= 1 << 1,
	MLX5_WQE_CTRL_FENCE	= 4 << 5,
	MLX5_WQE_CTRL_INITIATOR_SMALL_FENCE = 1 << 5,
};

enum {
	MLX5_SEND_WQE_BB	= 64,
	MLX5_SEND_WQE_SHIFT	= 6,
};

enum {
	MLX5_INLINE_SEG	= 0x80000000,
};

enum {
	MLX5_OPCODE_NOP			= 0x00,
	MLX5_OPCODE_SEND_INVAL		= 0x01,
	MLX5_OPCODE_RDMA_WRITE		= 0x08,
	MLX5_OPCODE_RDMA_WRITE_IMM	= 0x09,
	MLX5_OPCODE_SEND		= 0x0a,
	MLX5_OPCODE_SEND_IMM		= 0x0b,
	MLX5_OPCODE_TSO			= 0x0e,
	MLX5_OPCODE_RDMA_READ		= 0x10,
	MLX5_OPCODE_ATOMIC_CS		= 0x11,
	MLX5_OPCODE_ATOMIC_FA		= 0x12,
	MLX5_OPCODE_ATOMIC_MASKED_CS	= 0x14,
	MLX5_OPCODE_ATOMIC_MASKED_FA	= 0x15,
	MLX5_OPCODE_FMR			= 0x19,
	MLX5_OPCODE_LOCAL_INVAL		= 0x1b,
	MLX5_OPCODE_CONFIG_CMD		= 0x1f,
	MLX5_OPCODE_SET_PSV		= 0x20,
	MLX5_OPCODE_UMR			= 0x25,
	MLX5_OPCODE_TAG_MATCHING	= 0x28,
	MLX5_OPCODE_FLOW_TBL_ACCESS	= 0x2c,
};

/*
 * CQE related part
 */

enum {
	MLX5_INLINE_SCATTER_32	= 0x4,
	MLX5_INLINE_SCATTER_64	= 0x8,
};

enum {
	MLX5_CQE_SYNDROME_LOCAL_LENGTH_ERR		= 0x01,
	MLX5_CQE_SYNDROME_LOCAL_QP_OP_ERR		= 0x02,
	MLX5_CQE_SYNDROME_LOCAL_PROT_ERR		= 0x04,
	MLX5_CQE_SYNDROME_WR_FLUSH_ERR			= 0x05,
	MLX5_CQE_SYNDROME_MW_BIND_ERR			= 0x06,
	MLX5_CQE_SYNDROME_BAD_RESP_ERR			= 0x10,
	MLX5_CQE_SYNDROME_LOCAL_ACCESS_ERR		= 0x11,
	MLX5_CQE_SYNDROME_REMOTE_INVAL_REQ_ERR		= 0x12,
	MLX5_CQE_SYNDROME_REMOTE_ACCESS_ERR		= 0x13,
	MLX5_CQE_SYNDROME_REMOTE_OP_ERR			= 0x14,
	MLX5_CQE_SYNDROME_TRANSPORT_RETRY_EXC_ERR	= 0x15,
	MLX5_CQE_SYNDROME_RNR_RETRY_EXC_ERR		= 0x16,
	MLX5_CQE_SYNDROME_REMOTE_ABORTED_ERR		= 0x22,
};

struct mlx5_wqe_data_seg {
	__be32 byte_count;
	__be32 lkey;
	__be64 addr;
};

struct mlx5_wqe_ctrl_seg {
	__be32 opmod_idx_opcode;
	__be32 qpn_ds;
	uint8_t signature;
	uint8_t rsvd[2];
	uint8_t fm_ce_se;
	__be32 imm;
};

struct mlx5_wqe_raddr_seg {
	__be64		raddr;
	__be32		rkey;
	__be32		reserved;
};

struct mlx5_wqe_inl_data_seg {
	uint32_t	byte_count;
};

/*
 * Control segment - contains some control information for the current WQE.
 *
 * Output:
 *	seg	  - control segment to be filled
 * Input:
 *	pi	  - WQEBB number of the first block of this WQE.
 *		    This number should wrap at 0xffff, regardless of
 *		    size of the WQ.
 *	opcode	  - Opcode of this WQE. Encodes the type of operation
 *		    to be executed on the QP.
 *	opmod	  - Opcode modifier.
 *	qp_num	  - QP/SQ number this WQE is posted to.
 *	fm_ce_se  - FM (fence mode), CE (completion and event mode)
 *		    and SE (solicited event).
 *	ds	  - WQE size in octowords (16-byte units). DS accounts for all
 *		    the segments in the WQE as summarized in WQE construction.
 *	signature - WQE signature.
 *	imm	  - Immediate data/Invalidation key/UMR mkey.
 */
static MLX5DV_ALWAYS_INLINE
void mlx5dv_set_ctrl_seg(struct mlx5_wqe_ctrl_seg *seg, uint16_t pi,
			 uint8_t opcode, uint8_t opmod, uint32_t qp_num,
			 uint8_t fm_ce_se, uint8_t ds,
			 uint8_t signature, uint32_t imm)
{
	seg->opmod_idx_opcode	= htobe32(((uint32_t)opmod << 24) | ((uint32_t)pi << 8) | opcode);
	seg->qpn_ds		= htobe32((qp_num << 8) | ds);
	seg->fm_ce_se		= fm_ce_se;
	seg->signature		= signature;
	/*
	 * The caller should prepare "imm" in advance based on WR opcode.
	 * For IBV_WR_SEND_WITH_IMM and IBV_WR_RDMA_WRITE_WITH_IMM,
	 * the "imm" should be assigned as is.
	 * For the IBV_WR_SEND_WITH_INV, it should be htobe32(imm).
	 */
	seg->imm		= imm;
}

/*
 * Data Segments - contain pointers and a byte count for the scatter/gather list.
 * They can optionally contain data, which will save a memory read access for
 * gather Work Requests.
 */
static MLX5DV_ALWAYS_INLINE
void mlx5dv_set_data_seg(struct mlx5_wqe_data_seg *seg,
			 uint32_t length, uint32_t lkey,
			 uintptr_t address)
{
	seg->byte_count = htobe32(length);
	seg->lkey       = htobe32(lkey);
	seg->addr       = htobe64(address);
}

enum {
	MLX5_CQE_OWNER_MASK	= 1,
	MLX5_CQE_REQ		= 0,
	MLX5_CQE_RESP_WR_IMM	= 1,
	MLX5_CQE_RESP_SEND	= 2,
	MLX5_CQE_RESP_SEND_IMM	= 3,
	MLX5_CQE_RESP_SEND_INV	= 4,
	MLX5_CQE_RESIZE_CQ	= 5,
	MLX5_CQE_NO_PACKET	= 6,
	MLX5_CQE_SIG_ERR	= 12,
	MLX5_CQE_REQ_ERR	= 13,
	MLX5_CQE_RESP_ERR	= 14,
	MLX5_CQE_INVALID	= 15,
};

struct mlx5_err_cqe {
	uint8_t		rsvd0[32];
	uint32_t	srqn;
	uint8_t		rsvd1[18];
	uint8_t		vendor_err_synd;
	uint8_t		syndrome;
	uint32_t	s_wqe_opcode_qpn;
	uint16_t	wqe_counter;
	uint8_t		signature;
	uint8_t		op_own;
};

struct mlx5_cqe64 {
	struct {
		uint8_t		rsvd0[2];
		__be16		wqe_id;
		uint8_t		rsvd4[13];
		uint8_t		ml_path;
		uint8_t		rsvd20[4];
		__be16		slid;
		__be32		flags_rqpn;
		uint8_t		hds_ip_ext;
		uint8_t		l4_hdr_type_etc;
		__be16		vlan_info;
	};
	__be32		srqn_uidx;
	__be32		imm_inval_pkey;
	uint8_t		app;
	uint8_t		app_op;
	__be16		app_info;
	__be32		byte_cnt;
	__be64		timestamp;
	__be32		sop_drop_qpn;
	__be16		wqe_counter;
	uint8_t		signature;
	uint8_t		op_own;
};

static MLX5DV_ALWAYS_INLINE
uint8_t mlx5dv_get_cqe_owner(struct mlx5_cqe64 *cqe)
{
	return cqe->op_own & 0x1;
}

static MLX5DV_ALWAYS_INLINE
uint8_t mlx5dv_get_cqe_opcode(struct mlx5_cqe64 *cqe)
{
	return cqe->op_own >> 4;
}

#endif
