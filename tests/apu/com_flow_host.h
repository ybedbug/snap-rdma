#ifndef __COM_FLOW_HOST_H__
#define __COM_FLOW_HOST_H__
/*
 * Copyright (c) 2020 Nvidia Corporation. All rights reserved.
 */

#include <infiniband/mlx5dv.h>

struct flow_matcher {
	struct mlx5dv_dr_domain *dr_domain;
	struct mlx5dv_dr_table *dr_table;
	struct mlx5dv_dr_matcher *dr_matcher;
};

struct flow_rule {
	struct mlx5dv_dr_action *tir_action;
	struct mlx5dv_dr_rule *dr_rule;
};

struct flow_matcher *create_matcher(struct ibv_context *ibv_ctx);
struct flow_rule *create_rule(struct flow_matcher *flow_match, struct mlx5dv_devx_obj *tir_obj, uint64_t smac);
int destroy_matcher(struct flow_matcher *matcher);
int destroy_rule(struct flow_rule *rule);

#endif
