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

#ifndef SNAP_CRYPTO_H
#define SNAP_CRYPTO_H

struct snap_context;

enum {
	MLX5_CRYPTO_CAP_FAILED_SELFTEST_AES_GCM = 0x1 << 0,
	MLX5_CRYPTO_CAP_FAILED_SELFTEST_AES_ECB = 0x1 << 1,
	MLX5_CRYPTO_CAP_FAILED_SELFTEST_AES_XTS = 0x1 << 2,
	MLX5_CRYPTO_CAP_FAILED_SELFTEST_HMAC_SHA = 0x1 << 3,
	MLX5_CRYPTO_CAP_FAILED_SELFTEST_SHA = 0x1 << 4,
};

enum {
	MLX5_CRYPTO_CAP_WRAPPED_IMPORT_METHOD_TLS = 0x1 << 0,
	MLX5_CRYPTO_CAP_WRAPPED_IMPORT_METHOD_IPSEC = 0x1 << 1,
	MLX5_CRYPTO_CAP_WRAPPED_IMPORT_METHOD_AES_XTS = 0x1 << 2,
};

struct snap_crypto_context {
	uint8_t hca_crypto;
	uint8_t hca_aes_xts;

	uint8_t wrapped_crypto_operational;
	uint8_t wrapped_crypto_going_to_commissioning;
	uint8_t wrapped_import_method;
	uint8_t log_max_num_deks;
	uint8_t log_max_num_import_keks;
	uint8_t log_max_num_creds;
	uint16_t failed_selftests;
	uint8_t num_nv_import_keks;
	uint8_t num_nv_credentials;
};

struct snap_crypto_obj {
	struct mlx5dv_devx_obj	*obj;
	uint32_t		obj_id;
};

/* Wrapped_DEK = ENC(iv_64b + key1_128b + key2_128b + keytag_64b) */
#define SNAP_CRYPTO_DEK_SIZE            48

enum {
	SNAP_CRYPTO_DEK_KEY_SIZE_128 = 0x0,
	SNAP_CRYPTO_DEK_KEY_SIZE_256 = 0x1,
};

enum {
	SNAP_CRYPTO_DEK_KEY_PURPOSE_TLS     = 0x1,
	SNAP_CRYPTO_DEK_KEY_PURPOSE_IPSEC   = 0x2,
	SNAP_CRYPTO_DEK_KEY_PURPOSE_AES_XTS = 0x3,
	SNAP_CRYPTO_DEK_KEY_PURPOSE_MACSEC  = 0x4,
	SNAP_CRYPTO_DEK_KEY_PURPOSE_GCM_M2M = 0x5,
	SNAP_CRYPTO_DEK_KEY_PURPOSE_NISP    = 0x6,
};

struct snap_dek_attr {
	/* for query */
	uint64_t modify_field_select;
	uint8_t  state;

	/* for create */
	uint8_t  key_size;
	uint8_t  has_keytag;
	uint8_t  key_purpose;
	uint32_t pd;
	uint64_t opaque;
	uint8_t  key[SNAP_CRYPTO_DEK_SIZE];
};

int snap_query_crypto_caps(struct snap_context *sctx);

struct snap_crypto_obj *
snap_create_dek_obj(struct ibv_context *context,
			struct snap_dek_attr *attr);

int snap_query_dek_obj(struct snap_crypto_obj *dek, struct snap_dek_attr *attr);

#endif
