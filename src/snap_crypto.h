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

int snap_query_crypto_caps(struct snap_context *sctx);

#endif
