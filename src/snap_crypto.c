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

#include "snap.h"
#include "mlx5_ifc.h"

int snap_query_crypto_caps(struct snap_context *sctx)
{
	int ret;
	uint8_t in[DEVX_ST_SZ_BYTES(query_hca_cap_in)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(query_hca_cap_out)] = {0};
	struct ibv_context *context = sctx->context;

	DEVX_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	DEVX_SET(query_hca_cap_in, in, op_mod,
		 MLX5_SET_HCA_CAP_OP_MOD_GENERAL_DEVICE);
	ret = mlx5dv_devx_general_cmd(context, in, sizeof(in),
				      out, sizeof(out));
	if (ret) {
		snap_error("Query hca_cap faiedi, ret:%d\n", ret);
		return ret;
	}

	sctx->crypto.hca_crypto = DEVX_GET(query_hca_cap_out, out,
					capability.cmd_hca_cap.crypto);
	sctx->crypto.hca_aes_xts = DEVX_GET(query_hca_cap_out, out,
					capability.cmd_hca_cap.aes_xts);
	if (sctx->crypto.hca_crypto == 0)
		goto out;

	memset(in, 0, DEVX_ST_SZ_BYTES(query_hca_cap_in));
	memset(out, 0, DEVX_ST_SZ_BYTES(query_hca_cap_out));

	DEVX_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
	DEVX_SET(query_hca_cap_in, in, op_mod, MLX5_SET_HCA_CAP_OP_MOD_CRYPTO);
	ret = mlx5dv_devx_general_cmd(context, in, sizeof(in),
				      out, sizeof(out));
	if (ret) {
		snap_error("Query crypto_cap faied, ret:%d\n", ret);
		return ret;
	}

	sctx->crypto.wrapped_crypto_operational = DEVX_GET(query_hca_cap_out, out,
			capability.crypto_cap.wrapped_crypto_operational);
	sctx->crypto.wrapped_crypto_going_to_commissioning = DEVX_GET(query_hca_cap_out,
			out, capability.crypto_cap.wrapped_crypto_going_to_commissioning);
	sctx->crypto.wrapped_import_method = DEVX_GET(query_hca_cap_out, out,
			capability.crypto_cap.wrapped_import_method);
	sctx->crypto.log_max_num_deks = DEVX_GET(query_hca_cap_out, out,
			capability.crypto_cap.log_max_num_deks);
	sctx->crypto.log_max_num_import_keks = DEVX_GET(query_hca_cap_out, out,
			capability.crypto_cap.log_max_num_import_keks);
	sctx->crypto.log_max_num_creds = DEVX_GET(query_hca_cap_out, out,
			capability.crypto_cap.log_max_num_creds);
	sctx->crypto.failed_selftests = DEVX_GET(query_hca_cap_out, out,
			capability.crypto_cap.failed_selftests);
	sctx->crypto.num_nv_import_keks = DEVX_GET(query_hca_cap_out, out,
			capability.crypto_cap.num_nv_import_keks);
	sctx->crypto.num_nv_credentials = DEVX_GET(query_hca_cap_out, out,
			capability.crypto_cap.num_nv_credentials);

out:
	return 0;
}
