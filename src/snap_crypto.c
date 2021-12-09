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

struct snap_crypto_obj*
snap_create_dek_obj(struct ibv_context *context,
			struct snap_dek_attr *attr)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr) +
		DEVX_ST_SZ_BYTES(dek)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr)] = {0};
	uint8_t *dek_in;
	struct snap_crypto_obj *dek;
	void *dek_key;

	if ((attr->key_size != SNAP_CRYPTO_DEK_KEY_SIZE_128)
	    || (strlen((char *)attr->key) != SNAP_CRYPTO_DEK_SIZE)) {
		snap_error("Only support 128bit key!\n");
		goto out_err;
	}

	dek = calloc(1, sizeof(*dek));
	if (!dek)
		goto out_err;

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		MLX5_CMD_OP_CREATE_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		MLX5_OBJ_TYPE_DEK);

	dek_in = in + DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr);
	DEVX_SET(dek, dek_in, key_size, attr->key_size);
	DEVX_SET(dek, dek_in, has_keytag, attr->has_keytag);
	DEVX_SET(dek, dek_in, key_purpose, attr->key_purpose);
	DEVX_SET(dek, dek_in, pd, attr->pd);
	DEVX_SET64(dek, dek_in, opaque, attr->opaque);
	dek_key = DEVX_ADDR_OF(dek, dek_in, key);
	memcpy(dek_key, attr->key, SNAP_CRYPTO_DEK_SIZE);

	dek->obj = mlx5dv_devx_obj_create(context, in, sizeof(in),
		out, sizeof(out));
	if (!dek->obj)
		goto out_free;

	dek->obj_id = DEVX_GET(general_obj_out_cmd_hdr, out, obj_id);

	return dek;

out_free:
	free(dek);

out_err:
	return NULL;
}

int snap_query_dek_obj(struct snap_crypto_obj *dek, struct snap_dek_attr *attr)
{
	uint8_t in[DEVX_ST_SZ_BYTES(general_obj_in_cmd_hdr)] = {0};
	uint8_t out[DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr) +
		DEVX_ST_SZ_BYTES(dek)] = {0};
	uint8_t *dek_out;
	int ret;

	DEVX_SET(general_obj_in_cmd_hdr, in, opcode,
		MLX5_CMD_OP_QUERY_GENERAL_OBJECT);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_type,
		MLX5_OBJ_TYPE_DEK);
	DEVX_SET(general_obj_in_cmd_hdr, in, obj_id, dek->obj_id);

	ret = mlx5dv_devx_obj_query(dek->obj, in, sizeof(in), out, sizeof(out));
	if (ret)
		return ret;

	dek_out = out + DEVX_ST_SZ_BYTES(general_obj_out_cmd_hdr);
	attr->modify_field_select = DEVX_GET64(dek, dek_out,
			modify_field_select);
	attr->state = DEVX_GET(dek, dek_out, state);

	return 0;
}
