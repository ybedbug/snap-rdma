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

#ifndef SNAP_UMR_H
#define SNAP_UMR_H

#include "snap_dma.h"
#include "snap_mr.h"

enum {
	SNAP_UMR_MKEY_MODIFY_ATTACH_MTT        = 0x1 << 0,
	SNAP_UMR_MKEY_MODIFY_ATTACH_CRYPTO_BSF = 0x1 << 1,
};

enum {
	SNAP_CRYPTO_BSF_SIZE_64B  = 0x2,
};

enum {
	SNAP_CRYPTO_BSF_P_TYPE_SIGNATURE = 0x0,
	SNAP_CRYPTO_BSF_P_TYPE_CRYPTO    = 0x1,
};

enum {
	SNAP_CRYPTO_BSF_ENCRYPTION_ORDER_ENCRYPTED_WIRE_SIGNATURE   = 0x0,
	SNAP_CRYPTO_BSF_ENCRYPTION_ORDER_ENCRYPTED_MEMORY_SIGNATURE = 0x1,
	SNAP_CRYPTO_BSF_ENCRYPTION_ORDER_ENCRYPTED_RAW_WIRE         = 0x2,
	SNAP_CRYPTO_BSF_ENCRYPTION_ORDER_ENCRYPTED_RAW_MEMORY       = 0x3,
};

enum {
	SNAP_CRYPTO_BSF_ENCRYPTION_STANDARD_AES_XTS = 0x0,
};

enum {
	SNAP_CRYPTO_BSF_CRYPTO_BLOCK_SIZE_POINTER_RESERVED = 0x0,
	SNAP_CRYPTO_BSF_CRYPTO_BLOCK_SIZE_POINTER_512      = 0x1,
	SNAP_CRYPTO_BSF_CRYPTO_BLOCK_SIZE_POINTER_520      = 0x2,
	SNAP_CRYPTO_BSF_CRYPTO_BLOCK_SIZE_POINTER_4096     = 0x3,
	SNAP_CRYPTO_BSF_CRYPTO_BLOCK_SIZE_POINTER_4160     = 0x4,
	SNAP_CRYPTO_BSF_CRYPTO_BLOCK_SIZE_POINTER_1M       = 0x5,
	SNAP_CRYPTO_BSF_CRYPTO_BLOCK_SIZE_POINTER_4048     = 0x6,
};

enum {
	SNAP_WQE_UMR_CTRL_MKEY_MASK_BSF_OCTOWORD_SIZE = 0x1 << 5,
};

struct snap_crypto_bsf_seg {
	uint8_t		size_type;
	uint8_t		enc_order;
	uint8_t		rsvd0;
	uint8_t		enc_standard;
	__be32		raw_data_size;
	uint8_t		crypto_block_size_pointer;
	uint8_t		rsvd1[7];
	uint8_t		xts_initial_tweak[SNAP_CRYPTO_XTS_INITIAL_TWEAK_SIZE];
	__be32		dek_pointer;
	uint8_t		rsvd2[4];
	uint8_t		keytag[SNAP_CRYPTO_KEYTAG_SIZE];
	uint8_t		rsvd3[16];
};

struct snap_post_umr_attr {
	uint32_t purpose;

	struct snap_indirect_mkey *klm_mkey;

	/* for attach inline mtt purpose */
	int klm_entries;
	struct mlx5_klm *klm_mtt;

	/* for attach inline crypto bsf purpose*/
	uint8_t encryption_order;
	uint8_t encryption_standard;
	uint32_t raw_data_size;
	uint8_t crypto_block_size_pointer;
	uint8_t xts_initial_tweak[SNAP_CRYPTO_XTS_INITIAL_TWEAK_SIZE];
	uint32_t dek_pointer;
	uint8_t keytag[SNAP_CRYPTO_KEYTAG_SIZE];
};

int snap_umr_post_wqe(struct snap_dma_q *q, struct snap_post_umr_attr *attr,
		struct snap_dma_completion *comp, int *n_bb);

#endif /* SNAP_UMR_H */
