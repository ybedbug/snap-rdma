/*
 * Copyright (c) 2020 Nvidia Corporation. All rights reserved.
 */

#include <malloc.h>
#include <stdint.h>

#include "com_host.h"
#include "com_flow_host.h"

enum matcher_criteria {
	MATCHER_CRITERIA_EMPTY  = 0,
	MATCHER_CRITERIA_OUTER  = 1 << 0,
	MATCHER_CRITERIA_MISC   = 1 << 1,
	MATCHER_CRITERIA_INNER  = 1 << 2,
	MATCHER_CRITERIA_MISC2  = 1 << 3,
	MATCHER_CRITERIA_MISC3  = 1 << 4,
};

struct mlx5_ifc_dr_match_spec_bits {
	uint8_t smac_47_16[0x20];

	uint8_t smac_15_0[0x10];
	uint8_t ethertype[0x10];

	uint8_t dmac_47_16[0x20];

	uint8_t dmac_15_0[0x10];
	uint8_t first_prio[0x3];
	uint8_t first_cfi[0x1];
	uint8_t first_vid[0xc];

	uint8_t ip_protocol[0x8];
	uint8_t ip_dscp[0x6];
	uint8_t ip_ecn[0x2];
	uint8_t cvlan_tag[0x1];
	uint8_t svlan_tag[0x1];
	uint8_t frag[0x1];
	uint8_t ip_version[0x4];
	uint8_t tcp_flags[0x9];

	uint8_t tcp_sport[0x10];
	uint8_t tcp_dport[0x10];

	uint8_t reserved_at_c0[0x18];
	uint8_t ip_ttl_hoplimit[0x8];

	uint8_t udp_sport[0x10];
	uint8_t udp_dport[0x10];

	uint8_t src_ip_127_96[0x20];

	uint8_t src_ip_95_64[0x20];

	uint8_t src_ip_63_32[0x20];

	uint8_t src_ip_31_0[0x20];

	uint8_t dst_ip_127_96[0x20];

	uint8_t dst_ip_95_64[0x20];

	uint8_t dst_ip_63_32[0x20];

	uint8_t dst_ip_31_0[0x20];
};

#define MATCH_VAL_SIZE 512

static struct flow_matcher *create_flow_matcher_sw_steer(struct ibv_context *ibv_ctx,
							 struct mlx5dv_flow_match_parameters *match_mask)
{
	struct flow_matcher *flow_match;

	flow_match = (struct flow_matcher *)calloc(1, sizeof(*flow_match));

	flow_match->dr_domain = mlx5dv_dr_domain_create(ibv_ctx, MLX5DV_DR_DOMAIN_TYPE_NIC_RX);
	if (!flow_match->dr_domain) {
		pr_err("Fail creating dr_domain (errno %d).\n", errno);
		goto error;
	}

	flow_match->dr_table = mlx5dv_dr_table_create(flow_match->dr_domain, 0 /*level*/);
	if (!flow_match->dr_table) {
		pr_err("Fail creating dr_table (errno %d).\n", errno);
		goto error;
	}

	flow_match->dr_matcher = mlx5dv_dr_matcher_create(flow_match->dr_table, 0 /*priority*/,
							  MATCHER_CRITERIA_OUTER, match_mask);
	if (!flow_match->dr_matcher) {
		pr_err("Fail creating dr_matcher (errno %d).\n", errno);
		goto error;
	}
	return flow_match;

error:
	return NULL;
}

static struct flow_rule *create_flow_rule(struct flow_matcher *flow_matcher,
					  struct mlx5dv_devx_obj *tir_obj,
					  struct mlx5dv_flow_match_parameters *match_value)
{
	struct mlx5dv_dr_action *actions[1];
	struct flow_rule *flow_rule;

	flow_rule = (struct flow_rule *)calloc(1, sizeof(*flow_rule));

	flow_rule->tir_action = mlx5dv_dr_action_create_dest_devx_tir(tir_obj);
	if (!flow_rule->tir_action) {
		pr_err("Failed creating TIR action (errno %d).\n", errno);
		return NULL;
	}
	actions[0] = flow_rule->tir_action;

	flow_rule->dr_rule = mlx5dv_dr_rule_create(flow_matcher->dr_matcher, match_value, 1,
						   actions);
	if (!flow_rule->dr_rule) {
		pr_err("Fail creating dr_rule (errno %d).\n", errno);
		return NULL;
	}

	return flow_rule;
}

struct flow_matcher *create_matcher(struct ibv_context *ibv_ctx)
{
	struct mlx5dv_flow_match_parameters *match_mask;
	struct flow_matcher *matcher;
	int match_mask_size;

	/* mask & match value */
	match_mask_size = sizeof(*match_mask) + MATCH_VAL_SIZE;
	match_mask = (struct mlx5dv_flow_match_parameters *)calloc(1, match_mask_size);
	match_mask->match_sz = MATCH_VAL_SIZE;
	DEVX_SET(dr_match_spec, match_mask->match_buf, smac_47_16, 0xffffffff);
	DEVX_SET(dr_match_spec, match_mask->match_buf, smac_15_0, 0xffff);

	matcher = create_flow_matcher_sw_steer(ibv_ctx, match_mask);
	free(match_mask);

	return matcher;
}

struct flow_rule *create_rule(struct flow_matcher *flow_match, struct mlx5dv_devx_obj *tir_obj,
			      uint64_t smac)
{
	struct flow_rule *flow_rule;
	struct mlx5dv_flow_match_parameters *match_value;
	int match_value_size;

	/* mask & match value */
	match_value_size = sizeof(*match_value) + MATCH_VAL_SIZE;
	match_value = (struct mlx5dv_flow_match_parameters *)calloc(1, match_value_size);
	match_value->match_sz = MATCH_VAL_SIZE;
	DEVX_SET(dr_match_spec, match_value->match_buf, smac_47_16, smac >> 16);
	DEVX_SET(dr_match_spec, match_value->match_buf, smac_15_0, smac % (1 << 16));
	flow_rule = create_flow_rule(flow_match, tir_obj, match_value);
	free(match_value);

	return flow_rule;
}

int destroy_matcher(struct flow_matcher *matcher)
{
	int err;

	err = mlx5dv_dr_matcher_destroy(matcher->dr_matcher);
	if (err)
		return err;

	err = mlx5dv_dr_table_destroy(matcher->dr_table);
	if (err)
		return err;

	err = mlx5dv_dr_domain_destroy(matcher->dr_domain);
	if (err)
		return err;

	free(matcher);

	return 0;
}

int destroy_rule(struct flow_rule *rule)
{
	int err;

	err = mlx5dv_dr_rule_destroy(rule->dr_rule);
	if (err)
		return err;

	err = mlx5dv_dr_action_destroy(rule->tir_action);
	if (err)
		return err;

	free(rule);

	return 0;
}
