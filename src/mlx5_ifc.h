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

#ifndef MLX5_IFC_H
#define MLX5_IFC_H

#include <stdint.h>
#include <infiniband/mlx5dv.h>

#define u8 uint8_t
#define u16 uint16_t
#define u32 uint32_t

enum mlx5_cap_mode {
	HCA_CAP_OPMOD_GET_CUR	= 1,
};

enum {
	MLX5_RES_SCAT_DATA32_CQE	= 0x1,
	MLX5_RES_SCAT_DATA64_CQE	= 0x2,
	MLX5_REQ_SCAT_DATA32_CQE	= 0x11,
	MLX5_REQ_SCAT_DATA64_CQE	= 0x22,
};

enum {
	MLX5_QP_PM_ARMED	= 0x0,
	MLX5_QP_PM_REARM	= 0x1,
	MLX5_QP_PM_MIGRATED	= 0x3,
};

enum {
	MLX5_NON_ZERO_RQ	= 0x0,
	MLX5_SRQ_RQ		= 0x1,
	MLX5_CRQ_RQ		= 0x2,
	MLX5_ZERO_LEN_RQ	= 0x3,
};

enum {
	MLX5_CMD_OP_QUERY_HCA_CAP = 0x100,
	MLX5_CMD_OP_INIT_HCA = 0x102,
	MLX5_CMD_OP_TEARDOWN_HCA = 0x103,
	MLX5_CMD_OP_ENABLE_HCA = 0x104,
	MLX5_CMD_OP_DISABLE_HCA = 0x105,
	MLX5_CMD_OP_CREATE_MKEY = 0x200,
	MLX5_CMD_OP_QUERY_SPECIAL_CONTEXTS = 0x203,
	MLX5_CMD_OP_CREATE_CQ = 0x400,
	MLX5_CMD_OP_CREATE_QP = 0x500,
	MLX5_CMD_OP_DESTROY_QP = 0x501,
	MLX5_CMD_OP_RST2INIT_QP = 0x502,
	MLX5_CMD_OP_INIT2RTR_QP = 0x503,
	MLX5_CMD_OP_RTR2RTS_QP = 0x504,
	MLX5_CMD_OP_RTS2RTS_QP = 0x505,
	MLX5_CMD_OP_INIT2INIT_QP = 0x50E,
	MLX5_CMD_OP_CREATE_TIR = 0x900,
	MLX5_CMD_OP_QUERY_ESW_VPORT_CONTEXT = 0x752,
	MLX5_CMD_OP_QUERY_NIC_VPORT_CONTEXT = 0x754,
	MLX5_CMD_OP_QUERY_ROCE_ADDRESS = 0x760,
	MLX5_CMD_OP_SET_ROCE_ADDRESS = 0x761,
	MLX5_CMD_OP_ALLOC_PD = 0x800,
	MLX5_CMD_OP_DEALLOC_PD = 0x801,
	MLX5_CMD_OP_SET_FLOW_TABLE_ROOT = 0x92f,
	MLX5_CMD_OP_CREATE_FLOW_TABLE = 0x930,
	MLX5_CMD_OP_DESTROY_FLOW_TABLE = 0x931,
	MLX5_CMD_OP_QUERY_FLOW_TABLE = 0x932,
	MLX5_CMD_OP_CREATE_FLOW_GROUP = 0x933,
	MLX5_CMD_OP_DESTROY_FLOW_GROUP = 0x934,
	MLX5_CMD_OP_QUERY_FLOW_GROUP = 0x935,
	MLX5_CMD_OP_SET_FLOW_TABLE_ENTRY = 0x936,
	MLX5_CMD_OP_QUERY_FLOW_TABLE_ENTRY = 0x937,
	MLX5_CMD_OP_DELETE_FLOW_TABLE_ENTRY = 0x938,
	MLX5_CMD_OP_CREATE_FLOW_COUNTER = 0x939,
	MLX5_CMD_OP_MODIFY_FLOW_TABLE = 0x93c,
	MLX5_CMD_OP_ALLOC_PACKET_REFORMAT_CONTEXT = 0x93d,
	MLX5_CMD_OP_DEALLOC_PACKET_REFORMAT_CONTEXT = 0x93e,
	MLX5_CMD_OP_CREATE_GENERAL_OBJECT = 0xa00,
	MLX5_CMD_OP_MODIFY_GENERAL_OBJECT = 0xa01,
	MLX5_CMD_OP_QUERY_GENERAL_OBJECT = 0xa02,
	MLX5_CMD_OP_DESTROY_GENERAL_OBJECT = 0xa03,
	MLX5_CMD_OP_SYNC_STEERING = 0xb00,
	MLX5_CMD_OP_QUERY_EMULATED_FUNCTIONS_INFO = 0xb03,
	MLX5_CMD_OP_HOTPLUG_DEVICE = 0xb20,
	MLX5_CMD_OP_HOTUNPLUG_DEVICE = 0xb21,
	MLX5_CMD_OP_ALLOW_OTHER_VHCA_ACCESS = 0xb16,
	MLX5_CMD_OP_QUERY_VUID = 0xb22,
};

enum mlx5_event {
	MLX5_EVENT_TYPE_OBJECT_CHANGE = 0x27,
};

struct mlx5_ifc_atomic_caps_bits {
	u8	 reserved_at_0[0x40];

	u8	 atomic_req_8B_endianness_mode[0x2];
	u8	 reserved_at_42[0x4];
	u8	 supported_atomic_req_8B_endianness_mode_1[0x1];

	u8	 reserved_at_47[0x19];

	u8	 reserved_at_60[0x20];

	u8	 reserved_at_80[0x10];
	u8	 atomic_operations[0x10];

	u8	 reserved_at_a0[0x10];
	u8	 atomic_size_qp[0x10];

	u8	 reserved_at_c0[0x10];
	u8	 atomic_size_dc[0x10];

	u8	 reserved_at_e0[0x1a0];

	u8	 fetch_add_pci_atomic[0x10];
	u8	 swap_pci_atomic[0x10];
	u8	 compare_swap_pci_atomic[0x10];

	u8	 reserved_at_2b0[0x550];
};

struct mlx5_ifc_ipv4_layout_bits {
	u8	   reserved_at_0[0x60];

	u8	   ipv4[0x20];
};

struct mlx5_ifc_ipv6_layout_bits {
	u8	   ipv6[16][0x8];
};

union mlx5_ifc_ipv6_layout_ipv4_layout_auto_bits {
	struct mlx5_ifc_ipv6_layout_bits ipv6_layout;
	struct mlx5_ifc_ipv4_layout_bits ipv4_layout;
	u8	reserved_at_0[0x80];
};

struct mlx5_ifc_fte_match_set_lyr_2_4_bits {
	u8	   smac_47_16[0x20];

	u8	   smac_15_0[0x10];
	u8	   ethertype[0x10];

	u8	   dmac_47_16[0x20];

	u8	   dmac_15_0[0x10];
	u8	   first_prio[0x3];
	u8	   first_cfi[0x1];
	u8	   first_vid[0xc];

	u8	   ip_protocol[0x8];
	u8	   ip_dscp[0x6];
	u8	   ip_ecn[0x2];
	u8	   cvlan_tag[0x1];
	u8	   svlan_tag[0x1];
	u8	   frag[0x1];
	u8	   ip_version[0x4];
	u8	   tcp_flags[0x9];

	u8	   tcp_sport[0x10];
	u8	   tcp_dport[0x10];

	u8	   reserved_at_c0[0x18];
	u8	   ttl_hoplimit[0x8];

	u8	   udp_sport[0x10];
	u8	   udp_dport[0x10];

	union mlx5_ifc_ipv6_layout_ipv4_layout_auto_bits src_ipv4_src_ipv6;

	union mlx5_ifc_ipv6_layout_ipv4_layout_auto_bits dst_ipv4_dst_ipv6;
};

struct mlx5_ifc_nvgre_key_bits {
	u8	   hi[0x18];
	u8	   lo[0x8];
};

union mlx5_ifc_gre_key_bits {
	struct mlx5_ifc_nvgre_key_bits nvgre;
	u8	   key[0x20];
};

struct mlx5_ifc_fte_match_set_misc_bits {
	u8	   reserved_at_0[0x8];
	u8	   source_sqn[0x18];

	u8	   source_eswitch_owner_vhca_id[0x10];
	u8	   source_port[0x10];

	u8	   outer_second_prio[0x3];
	u8	   outer_second_cfi[0x1];
	u8	   outer_second_vid[0xc];
	u8	   inner_second_prio[0x3];
	u8	   inner_second_cfi[0x1];
	u8	   inner_second_vid[0xc];

	u8	   outer_second_cvlan_tag[0x1];
	u8	   inner_second_cvlan_tag[0x1];
	u8	   outer_second_svlan_tag[0x1];
	u8	   inner_second_svlan_tag[0x1];
	u8	   outer_emd_tag[0x1];
	u8	   reserved_at_65[0xb];
	u8	   gre_protocol[0x10];

	union mlx5_ifc_gre_key_bits gre_key;

	u8	   vxlan_vni[0x18];
	u8	   reserved_at_b8[0x8];

	u8	   geneve_vni[0x18];
	u8	   reserved_at_d8[0x7];
	u8	   geneve_oam[0x1];

	u8	   reserved_at_e0[0xc];
	u8	   outer_ipv6_flow_label[0x14];

	u8	   reserved_at_100[0xc];
	u8	   inner_ipv6_flow_label[0x14];

	u8	   reserved_at_120[0xa];
	u8	   geneve_opt_len[0x6];
	u8	   geneve_protocol_type[0x10];

	u8	   reserved_at_140[0x8];
	u8	   bth_dst_qp[0x18];
	u8	   inner_esp_spi[0x20];
	u8	   outer_esp_spi[0x20];
	u8	   reserved_at_1a0[0x20];

	u8	   outer_emd_tag_data[6][0x8];
	u8	   reserved_at_1f0[0x10];
};

struct mlx5_ifc_fte_match_mpls_bits {
	u8	   mpls_label[0x14];
	u8	   mpls_exp[0x3];
	u8	   mpls_s_bos[0x1];
	u8	   mpls_ttl[0x8];
};

struct mlx5_ifc_fte_match_set_misc2_bits {
	struct mlx5_ifc_fte_match_mpls_bits outer_first_mpls;

	struct mlx5_ifc_fte_match_mpls_bits inner_first_mpls;

	struct mlx5_ifc_fte_match_mpls_bits outer_first_mpls_over_gre;

	struct mlx5_ifc_fte_match_mpls_bits outer_first_mpls_over_udp;

	u8	   metadata_reg_c_7[0x20];

	u8	   metadata_reg_c_6[0x20];

	u8	   metadata_reg_c_5[0x20];

	u8	   metadata_reg_c_4[0x20];

	u8	   metadata_reg_c_3[0x20];

	u8	   metadata_reg_c_2[0x20];

	u8	   metadata_reg_c_1[0x20];

	u8	   metadata_reg_c_0[0x20];

	u8	   metadata_reg_a[0x20];

	u8	   reserved_at_1a0[0x60];
};

struct mlx5_ifc_fte_match_set_misc3_bits {
	u8	 inner_tcp_seq_num[0x20];
	u8	 outer_tcp_seq_num[0x20];
	u8	 inner_tcp_ack_num[0x20];
	u8	 outer_tcp_ack_num[0x20];
	u8	 reserved_at_80[0x8];
	u8	 outer_vxlan_gpe_vni[0x18];
	u8	 outer_vxlan_gpe_next_protocol[0x8];
	u8	 outer_vxlan_gpe_flags[0x8];
	u8	 reserved_at_b0[0x10];
	u8	 icmp_header_data[0x20];
	u8	 icmpv6_header_data[0x20];
	u8	 icmp_type[0x8];
	u8	 icmp_code[0x8];
	u8	 icmpv6_type[0x8];
	u8	 icmpv6_code[0x8];
	u8	 geneve_tlv_option_0_data[0x20];
	u8	 reserved_at_140[0xc0];
};

enum mlx5_flow_table_miss_action {
	MLX5_FLOW_TABLE_MISS_ACTION_DEF,
	MLX5_FLOW_TABLE_MISS_ACTION_FWD,
	MLX5_FLOW_TABLE_MISS_ACTION_SWITCH_DOMAIN,
};

struct mlx5_ifc_flow_table_context_bits {
	u8	 reformat_en[0x1];
	u8	 decap_en[0x1];
	u8	 sw_owner[0x1];
	u8	 reserved_at_3[0x1];
	u8	 table_miss_action[0x4];
	u8	 level[0x8];
	u8	 reserved_at_10[0x8];
	u8	 log_size[0x8];

	u8	 reserved_at_20[0x8];
	u8	 table_miss_id[0x18];

	u8	 reserved_at_40[0x8];
	u8	 lag_master_next_table_id[0x18];

	u8	 reserved_at_60[0x60];

	u8	 sw_owner_icm_root_1[0x40];

	u8	 sw_owner_icm_root_0[0x40];
};

struct mlx5_ifc_create_flow_table_in_bits {
	u8	 opcode[0x10];
	u8	 uid[0x10];

	u8	 reserved_at_20[0x10];
	u8	 op_mod[0x10];

	u8	 other_vport[0x1];
	u8	 reserved_at_41[0xf];
	u8	 vport_number[0x10];

	u8	 reserved_at_60[0x20];

	u8	 table_type[0x8];
	u8	 reserved_at_88[0x18];

	u8	 reserved_at_a0[0x20];

	struct mlx5_ifc_flow_table_context_bits flow_table_context;
};

struct mlx5_ifc_create_flow_table_out_bits {
	u8	 status[0x8];
	u8	 icm_address_63_40[0x18];

	u8	 syndrome[0x20];

	u8	 icm_address_39_32[0x8];
	u8	 table_id[0x18];

	u8	 icm_address_31_0[0x20];
};

struct mlx5_ifc_destroy_flow_table_in_bits {
	u8	   opcode[0x10];
	u8	   uid[0x10];

	u8	   reserved_at_20[0x10];
	u8	   op_mod[0x10];

	u8	   other_vport[0x1];
	u8	   reserved_at_41[0xf];
	u8	   vport_number[0x10];

	u8	   reserved_at_60[0x20];

	u8	   table_type[0x8];
	u8	   reserved_at_88[0x18];

	u8	   reserved_at_a0[0x8];
	u8	   table_id[0x18];

	u8	   reserved_at_c0[0x140];
};

struct mlx5_ifc_destroy_flow_table_out_bits {
	u8	   status[0x8];
	u8	   reserved_at_8[0x18];

	u8	   syndrome[0x20];

	u8	   reserved_at_40[0x40];
};

struct mlx5_ifc_set_flow_table_root_out_bits {
	u8	   status[0x8];
	u8	   reserved_at_8[0x18];

	u8	   syndrome[0x20];

	u8	   reserved_at_40[0x40];
};

struct mlx5_ifc_set_flow_table_root_in_bits {
	u8	   opcode[0x10];
	u8	   uid[0x10];

	u8	   reserved_at_20[0x10];
	u8	   op_mod[0x10];

	u8	   other_vport[0x1];
	u8	   reserved_at_41[0xf];
	u8	   vport_number[0x10];

	u8	   reserved_at_60[0x20];

	u8	   table_type[0x8];
	u8	   reserved_at_88[0x18];

	u8	   reserved_at_a0[0x8];
	u8	   table_id[0x18];

	u8	   reserved_at_c0[0x8];
	u8	   underlay_qpn[0x18];
	u8	   reserved_at_e0[0x120];
};

struct mlx5_ifc_destroy_flow_group_out_bits {
	u8	   status[0x8];
	u8	   reserved_at_8[0x18];

	u8	   syndrome[0x20];

	u8	   reserved_at_40[0x40];
};

struct mlx5_ifc_destroy_flow_group_in_bits {
	u8	   opcode[0x10];
	u8	   reserved_at_10[0x10];

	u8	   reserved_at_20[0x10];
	u8	   op_mod[0x10];

	u8	   other_vport[0x1];
	u8	   reserved_at_41[0xf];
	u8	   vport_number[0x10];

	u8	   reserved_at_60[0x20];

	u8	   table_type[0x8];
	u8	   reserved_at_88[0x18];

	u8	   reserved_at_a0[0x8];
	u8	   table_id[0x18];

	u8	   group_id[0x20];

	u8	   reserved_at_e0[0x120];
};

struct mlx5_ifc_create_flow_group_out_bits {
	u8	   status[0x8];
	u8	   reserved_at_8[0x18];

	u8	   syndrome[0x20];

	u8	   reserved_at_40[0x8];
	u8	   group_id[0x18];

	u8	   reserved_at_60[0x20];
};

enum {
	MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_OUTER_HEADERS		= 0x0,
	MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_MISC_PARAMETERS		= 0x1,
	MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_INNER_HEADERS		= 0x2,
	MLX5_CREATE_FLOW_GROUP_IN_MATCH_CRITERIA_ENABLE_MISC_PARAMETERS_2	= 0x3,
};

struct mlx5_ifc_fte_match_param_bits {
	struct mlx5_ifc_fte_match_set_lyr_2_4_bits outer_headers;

	struct mlx5_ifc_fte_match_set_misc_bits misc_parameters;

	struct mlx5_ifc_fte_match_set_lyr_2_4_bits inner_headers;

	struct mlx5_ifc_fte_match_set_misc2_bits misc_parameters_2;

	struct mlx5_ifc_fte_match_set_misc3_bits misc_parameters_3;

	u8	 reserved_at_a00[0x600];
};

struct mlx5_ifc_create_flow_group_in_bits {
	u8	   opcode[0x10];
	u8	   reserved_at_10[0x10];

	u8	   reserved_at_20[0x10];
	u8	   op_mod[0x10];

	u8	   other_vport[0x1];
	u8	   reserved_at_41[0xf];
	u8	   vport_number[0x10];

	u8	   reserved_at_60[0x20];

	u8	   table_type[0x8];
	u8	   reserved_at_88[0x18];

	u8	   reserved_at_a0[0x8];
	u8	   table_id[0x18];

	u8	   source_eswitch_owner_vhca_id_valid[0x1];

	u8	   reserved_at_c1[0x1f];

	u8	   start_flow_index[0x20];

	u8	   reserved_at_100[0x20];

	u8	   end_flow_index[0x20];

	u8	   reserved_at_140[0xa0];

	u8	   reserved_at_1e0[0x18];
	u8	   match_criteria_enable[0x8];

	struct mlx5_ifc_fte_match_param_bits match_criteria;

	u8	   reserved_at_1200[0xe00];
};

struct mlx5_ifc_sync_steering_in_bits {
	u8	 opcode[0x10];
	u8	 uid[0x10];

	u8	 reserved_at_20[0x10];
	u8	 op_mod[0x10];

	u8	 reserved_at_40[0xc0];
};

struct mlx5_ifc_sync_steering_out_bits {
	u8	 status[0x8];
	u8	 reserved_at_8[0x18];

	u8	 syndrome[0x20];

	u8	 reserved_at_40[0x40];
};

struct mlx5_ifc_device_mem_cap_bits {
	u8	 memic[0x1];
	u8	 reserved_at_1[0x1f];

	u8	 reserved_at_20[0xb];
	u8	 log_min_memic_alloc_size[0x5];
	u8	 reserved_at_30[0x8];
	u8	 log_max_memic_addr_alignment[0x8];

	u8	 memic_bar_start_addr[0x40];

	u8	 memic_bar_size[0x20];

	u8	 max_memic_size[0x20];

	u8	 steering_sw_icm_start_address[0x40];

	u8	 reserved_at_100[0x12];
	u8	 log_sw_icm_alloc_granularity[0x6];
	u8	 log_steering_sw_icm_size[0x8];

	u8	 reserved_at_120[0x20];

	u8	 header_modify_sw_icm_start_address[0x40];
};

struct mlx5_ifc_flow_table_fields_supported_bits {
	u8	 outer_dmac[0x1];
	u8	 outer_smac[0x1];
	u8	 outer_ether_type[0x1];
	u8	 outer_ip_version[0x1];
	u8	 outer_first_prio[0x1];
	u8	 outer_first_cfi[0x1];
	u8	 outer_first_vid[0x1];
	u8	 outer_ipv4_ttl[0x1];
	u8	 outer_second_prio[0x1];
	u8	 outer_second_cfi[0x1];
	u8	 outer_second_vid[0x1];
	u8	 outer_ipv6_flow_label[0x1];
	u8	 outer_sip[0x1];
	u8	 outer_dip[0x1];
	u8	 outer_frag[0x1];
	u8	 outer_ip_protocol[0x1];
	u8	 outer_ip_ecn[0x1];
	u8	 outer_ip_dscp[0x1];
	u8	 outer_udp_sport[0x1];
	u8	 outer_udp_dport[0x1];
	u8	 outer_tcp_sport[0x1];
	u8	 outer_tcp_dport[0x1];
	u8	 outer_tcp_flags[0x1];
	u8	 outer_gre_protocol[0x1];
	u8	 outer_gre_key[0x1];
	u8	 outer_vxlan_vni[0x1];
	u8	 outer_geneve_vni[0x1];
	u8	 outer_geneve_oam[0x1];
	u8	 outer_geneve_protocol_type[0x1];
	u8	 outer_geneve_opt_len[0x1];
	u8	 source_vhca_port[0x1];
	u8	 source_eswitch_port[0x1];

	u8	 inner_dmac[0x1];
	u8	 inner_smac[0x1];
	u8	 inner_ether_type[0x1];
	u8	 inner_ip_version[0x1];
	u8	 inner_first_prio[0x1];
	u8	 inner_first_cfi[0x1];
	u8	 inner_first_vid[0x1];
	u8	 inner_ipv4_ttl[0x1];
	u8	 inner_second_prio[0x1];
	u8	 inner_second_cfi[0x1];
	u8	 inner_second_vid[0x1];
	u8	 inner_ipv6_flow_label[0x1];
	u8	 inner_sip[0x1];
	u8	 inner_dip[0x1];
	u8	 inner_frag[0x1];
	u8	 inner_ip_protocol[0x1];
	u8	 inner_ip_ecn[0x1];
	u8	 inner_ip_dscp[0x1];
	u8	 inner_udp_sport[0x1];
	u8	 inner_udp_dport[0x1];
	u8	 inner_tcp_sport[0x1];
	u8	 inner_tcp_dport[0x1];
	u8	 inner_tcp_flags[0x1];
	u8	 reserved_at_37[0x7];
	u8	 metadata_reg_b[0x1];
	u8	 metadata_reg_a[0x1];

	u8	 reserved_at_40[0x5];
	u8	 outer_first_mpls_over_udp_ttl[0x1];
	u8	 outer_first_mpls_over_udp_s_bos[0x1];
	u8	 outer_first_mpls_over_udp_exp[0x1];
	u8	 outer_first_mpls_over_udp_label[0x1];
	u8	 outer_first_mpls_over_gre_ttl[0x1];
	u8	 outer_first_mpls_over_gre_s_bos[0x1];
	u8	 outer_first_mpls_over_gre_exp[0x1];
	u8	 outer_first_mpls_over_gre_label[0x1];
	u8	 inner_first_mpls_ttl[0x1];
	u8	 inner_first_mpls_s_bos[0x1];
	u8	 inner_first_mpls_exp[0x1];
	u8	 inner_first_mpls_label[0x1];
	u8	 outer_first_mpls_ttl[0x1];
	u8	 outer_first_mpls_s_bos[0x1];
	u8	 outer_first_mpls_exp[0x1];
	u8	 outer_first_mpls_label[0x1];
	u8	 outer_emd_tag[0x1];
	u8	 inner_esp_spi[0x1];
	u8	 outer_esp_spi[0x1];
	u8	 inner_ipv6_hop_limit[0x1];
	u8	 outer_ipv6_hop_limit[0x1];
	u8	 bth_dst_qp[0x1];
	u8	 inner_first_svlan[0x1];
	u8	 inner_second_svlan[0x1];
	u8	 outer_first_svlan[0x1];
	u8	 outer_second_svlan[0x1];
	u8	 source_sqn[0x1];

	u8	 reserved_at_60[0x20];
};

struct mlx5_ifc_dr_match_spec_bits {
	u8	 smac_47_16[0x20];

	u8	 smac_15_0[0x10];
	u8	 ethertype[0x10];

	u8	 dmac_47_16[0x20];

	u8	 dmac_15_0[0x10];
	u8	 first_prio[0x3];
	u8	 first_cfi[0x1];
	u8	 first_vid[0xc];

	u8	 ip_protocol[0x8];
	u8	 ip_dscp[0x6];
	u8	 ip_ecn[0x2];
	u8	 cvlan_tag[0x1];
	u8	 svlan_tag[0x1];
	u8	 frag[0x1];
	u8	 ip_version[0x4];
	u8	 tcp_flags[0x9];

	u8	 tcp_sport[0x10];
	u8	 tcp_dport[0x10];

	u8	 reserved_at_c0[0x18];
	u8	 ip_ttl_hoplimit[0x8];

	u8	 udp_sport[0x10];
	u8	 udp_dport[0x10];

	u8	 src_ip_127_96[0x20];

	u8	 src_ip_95_64[0x20];

	u8	 src_ip_63_32[0x20];

	u8	 src_ip_31_0[0x20];

	u8	 dst_ip_127_96[0x20];

	u8	 dst_ip_95_64[0x20];

	u8	 dst_ip_63_32[0x20];

	u8	 dst_ip_31_0[0x20];
};

struct mlx5_ifc_dr_match_set_misc_bits {
	u8	 gre_c_present[0x1];
	u8	 reserved_auto1[0x1];
	u8	 gre_k_present[0x1];
	u8	 gre_s_present[0x1];
	u8	 source_vhca_port[0x4];
	u8	 source_sqn[0x18];

	u8	 source_eswitch_owner_vhca_id[0x10];
	u8	 source_port[0x10];

	u8	 outer_second_prio[0x3];
	u8	 outer_second_cfi[0x1];
	u8	 outer_second_vid[0xc];
	u8	 inner_second_prio[0x3];
	u8	 inner_second_cfi[0x1];
	u8	 inner_second_vid[0xc];

	u8	 outer_second_cvlan_tag[0x1];
	u8	 inner_second_cvlan_tag[0x1];
	u8	 outer_second_svlan_tag[0x1];
	u8	 inner_second_svlan_tag[0x1];
	u8	 outer_emd_tag[0x1];
	u8	 reserved_at_65[0xb];
	u8	 gre_protocol[0x10];

	u8	 gre_key_h[0x18];
	u8	 gre_key_l[0x8];

	u8	 vxlan_vni[0x18];
	u8	 reserved_at_b8[0x8];

	u8	 geneve_vni[0x18];
	u8	 reserved_at_e4[0x7];
	u8	 geneve_oam[0x1];

	u8	 reserved_at_ec[0xc];
	u8	 outer_ipv6_flow_label[0x14];

	u8	 reserved_at_100[0xc];
	u8	 inner_ipv6_flow_label[0x14];

	u8	 reserved_at_120[0xa];
	u8	 geneve_opt_len[0x6];
	u8	 geneve_protocol_type[0x10];

	u8	 reserved_at_140[0x8];
	u8	 bth_dst_qp[0x18];

	u8	 inner_esp_spi[0x20];

	u8	   outer_esp_spi[0x20];

	u8	   reserved_at_1a0[0x60];
};

struct mlx5_ifc_dr_match_set_misc2_bits {
	u8	 outer_first_mpls_label[0x14];
	u8	 outer_first_mpls_exp[0x3];
	u8	 outer_first_mpls_s_bos[0x1];
	u8	 outer_first_mpls_ttl[0x8];

	u8	 inner_first_mpls_label[0x14];
	u8	 inner_first_mpls_exp[0x3];
	u8	 inner_first_mpls_s_bos[0x1];
	u8	 inner_first_mpls_ttl[0x8];

	u8	 outer_first_mpls_over_gre_label[0x14];
	u8	 outer_first_mpls_over_gre_exp[0x3];
	u8	 outer_first_mpls_over_gre_s_bos[0x1];
	u8	 outer_first_mpls_over_gre_ttl[0x8];

	u8	 outer_first_mpls_over_udp_label[0x14];
	u8	 outer_first_mpls_over_udp_exp[0x3];
	u8	 outer_first_mpls_over_udp_s_bos[0x1];
	u8	 outer_first_mpls_over_udp_ttl[0x8];

	u8	 metadata_reg_c_7[0x20];
	u8	 metadata_reg_c_6[0x20];
	u8	 metadata_reg_c_5[0x20];
	u8	 metadata_reg_c_4[0x20];
	u8	 metadata_reg_c_3[0x20];
	u8	 metadata_reg_c_2[0x20];
	u8	 metadata_reg_c_1[0x20];
	u8	 metadata_reg_c_0[0x20];

	u8	 metadata_reg_a[0x20];
	u8	 metadata_reg_b[0x20];

	u8	 reserved_at_260[0x40];
};

struct mlx5_ifc_dr_match_set_misc3_bits {
	u8	 inner_tcp_seq_num[0x20];

	u8	 outer_tcp_seq_num[0x20];

	u8	 inner_tcp_ack_num[0x20];

	u8	 outer_tcp_ack_num[0x20];

	u8	 reserved_at_80[0x8];
	u8	 outer_vxlan_gpe_vni[0x18];

	u8	 outer_vxlan_gpe_next_protocol[0x8];
	u8	 outer_vxlan_gpe_flags[0x8];
	u8	 reserved_at_b0[0x10];

	u8	 icmp_header_data[0x20];

	u8	 icmpv6_header_data[0x20];

	u8	 icmp_type[0x8];
	u8	 icmp_code[0x8];
	u8	 icmpv6_type[0x8];
	u8	 icmpv6_code[0x8];

	u8	 reserved_at_120[0xe0];
};

struct mlx5_ifc_flow_table_prop_layout_bits {
	u8	 ft_support[0x1];
	u8	 flow_tag[0x1];
	u8	 flow_counter[0x1];
	u8	 flow_modify_en[0x1];
	u8	 modify_root[0x1];
	u8	 identified_miss_table[0x1];
	u8	 flow_table_modify[0x1];
	u8	 reformat[0x1];
	u8	 decap[0x1];
	u8	 reset_root_to_default[0x1];
	u8	 pop_vlan[0x1];
	u8	 push_vlan[0x1];
	u8	 fpga_vendor_acceleration[0x1];
	u8	 pop_vlan_2[0x1];
	u8	 push_vlan_2[0x1];
	u8	 reformat_and_vlan_action[0x1];
	u8	 modify_and_vlan_action[0x1];
	u8	 sw_owner[0x1];
	u8	 reformat_l3_tunnel_to_l2[0x1];
	u8	 reformat_l2_to_l3_tunnel[0x1];
	u8	 reformat_and_modify_action[0x1];
	u8	 reserved_at_15[0xb];

	u8	 reserved_at_20[0x2];
	u8	 log_max_ft_size[0x6];
	u8	 log_max_modify_header_context[0x8];
	u8	 max_modify_header_actions[0x8];
	u8	 max_ft_level[0x8];

	u8	 reserved_at_40[0x10];
	u8	 metadata_reg_b_width[0x8];
	u8	 metadata_reg_a_width[0x8];

	u8	 reserved_at_60[0x18];
	u8	 log_max_ft_num[0x8];

	u8	 reserved_at_80[0x10];
	u8	 log_max_flow_counter[0x8];
	u8	 log_max_destination[0x8];

	u8	 reserved_at_a0[0x18];
	u8	 log_max_flow[0x8];

	u8	 reserved_at_c0[0x40];

	struct mlx5_ifc_flow_table_fields_supported_bits ft_field_support;

	struct mlx5_ifc_flow_table_fields_supported_bits ft_field_bitmask_support;
};

enum {
	MLX5_FLEX_PARSER_VXLAN_GPE_ENABLED	= 1 << 7,
	MLX5_FLEX_PARSER_ICMP_V4_ENABLED	= 1 << 8,
	MLX5_FLEX_PARSER_ICMP_V6_ENABLED	= 1 << 9,
};

struct mlx5_ifc_cmd_hca_cap_bits {
	u8	 access_other_hca_roce[0x1];
	u8	 reserved_at_1[0x1e];
	u8	 vhca_resource_manager[0x1];

	u8	 reserved_at_20[0x10];
	u8	 vhca_id[0x10];

	u8	 reserved_at_40[0x20];

	u8	 resources_on_virtio_fs_emulation_manager[0x1];
	u8	 reserved_at_61[0x2];
	u8	 log_regexp_scatter_gather_size[0x5];
	u8	 reserved_at_68[0x3];
	u8	 log_dma_mmo_max_size[0x5];
	u8	 relaxed_ordering_write_pci_enabled[0x1];
	u8	 reserved_at_77[0x1];
	u8	 decompress_snappy[0x1];
	u8	 log_compress_max_size[0x5];
	u8	 decompress_lz4_data_only[0x1];
	u8	 decompress_lz4_no_checksum[0x1];
	u8	 decompress_lz4_checksum[0x1];
	u8	 log_decompress_max_size[0x5];

	u8	 log_max_srq_sz[0x8];
	u8	 log_max_qp_sz[0x8];
	u8	 reserved_at_90[0x8];
	u8	 prio_tag_required[0x1];
	u8	 reserved_at_99[0x2];
	u8	 log_max_qp[0x5];

	u8	 reserved_at_a0[0xb];
	u8	 log_max_srq[0x5];
	u8	 reserved_at_b0[0x10];

	u8	 reserved_at_c0[0x8];
	u8	 log_max_cq_sz[0x8];
	u8	 relaxed_ordering_write_umr[0x1];
	u8	 relaxed_ordering_read_umr[0x1];
	u8	 reserved_at_d0[0x7];
	u8	 virtio_net_device_emulation_manager[0x1];
	u8	 virtio_blk_device_emulation_manager[0x1];
	u8	 log_max_cq[0x5];

	u8	 log_max_eq_sz[0x8];
	u8	 relaxed_ordering_write[0x1];
	u8	 relaxed_ordering_read[0x1];
	u8	 log_max_mkey[0x6];
	u8	 tunneled_atomic[0x1];
	u8	 as_notify[0x1];
	u8	 m_pci_port[0x1];
	u8	 m_vhca_mk[0x1];
	u8	 hotplug_manager[0x1];
	u8	 nvme_device_emulation_manager[0x1];
	u8	 terminate_scatter_list_mkey[0x1];
	u8	 repeated_mkey[0x1];
	u8	 dump_fill_mkey[0x1];
	u8	 dpp[0x1];
	u8	 resources_on_nvme_emulation_manager[0x1];
	u8	 fast_teardown[0x1];
	u8	 log_max_eq[0x4];

	u8	 max_indirection[0x8];
	u8	 fixed_buffer_size[0x1];
	u8	 log_max_mrw_sz[0x7];
	u8	 force_teardown[0x1];
	u8	 hard_coded_1[0x1];
	u8	 log_max_bsf_list_size[0x6];
	u8	 umr_extended_translation_offset[0x1];
	u8	 null_mkey[0x1];
	u8	 log_max_klm_list_size[0x6];

	u8	 reserved_at_120[0x8];
	u8	 resources_on_virtio_net_emulation_manager[0x1];
	u8	 resources_on_virtio_blk_emulation_manager[0x1];
	u8	 log_max_ra_req_dc[0x6];
	u8	 reserved_at_130[0xa];
	u8	 log_max_ra_res_dc[0x6];

	u8	 reserved_at_140[0xa];
	u8	 log_max_ra_req_qp[0x6];
	u8       reserved_at_150[0x1];
	u8	 rts2rts_udp_sport[0x1];
	u8	 rts2rts_lag_tx_port_affinity[0x1];
	u8	 dma_mmo_sq[0x1];
	u8	 compress_min_block_size[0x4];
	u8	 compress_mmo_sq[0x1];
	u8	 decompress_mmo_sq[0x1];
	u8	 log_max_ra_res_qp[0x6];

	u8	 end_pad[0x1];
	u8	 cc_query_allowed[0x1];
	u8	 cc_modify_allowed[0x1];
	u8	 start_pad[0x1];
	u8	 cache_line_128byte[0x1];
	u8	 gid_table_size_ro[0x1];
	u8	 pkey_table_size_ro[0x1];
	u8	 reserved_at_167[0x1];
	u8	 rnr_nak_q_counters[0x1];
	u8	 rts2rts_qp_counters_set_id[0x1];
	u8	 rts2rts_qp_dscp[0x1];
	u8	 reserved_at_16b[0x4];
	u8	 qcam_reg[0x1];
	u8	 gid_table_size[0x10];

	u8	 out_of_seq_cnt[0x1];
	u8	 vport_counters[0x1];
	u8	 retransmission_q_counters[0x1];
	u8	 debug[0x1];
	u8	 modify_rq_counters_set_id[0x1];
	u8	 rq_delay_drop[0x1];
	u8	 max_qp_cnt[0xa];
	u8	 pkey_table_size[0x10];

	u8	 vport_group_manager[0x1];
	u8	 vhca_group_manager[0x1];
	u8	 ib_virt[0x1];
	u8	 eth_virt[0x1];
	u8	 vnic_env_queue_counters[0x1];
	u8	 ets[0x1];
	u8	 nic_flow_table[0x1];
	u8	 eswitch_manager[0x1];
	u8	 device_memory[0x1];
	u8	 mcam_reg[0x1];
	u8	 pcam_reg[0x1];
	u8	 local_ca_ack_delay[0x5];
	u8	 port_module_event[0x1];
	u8	 enhanced_retransmission_q_counters[0x1];
	u8	 port_checks[0x1];
	u8	 pulse_gen_control[0x1];
	u8	 disable_link_up_by_init_hca[0x1];
	u8	 beacon_led[0x1];
	u8	 port_type[0x2];
	u8	 num_ports[0x8];

	u8	 reserved_at_1c0[0x1];
	u8	 pps[0x1];
	u8	 pps_modify[0x1];
	u8	 log_max_msg[0x5];
	u8	 multi_path_xrc_rdma[0x1];
	u8	 multi_path_dc_rdma[0x1];
	u8	 multi_path_rc_rdma[0x1];
	u8	 traffic_fast_control[0x1];
	u8	 max_tc[0x4];
	u8	 temp_warn_event[0x1];
	u8	 dcbx[0x1];
	u8	 general_notification_event[0x1];
	u8	 multi_prio_sq[0x1];
	u8	 afu_owner[0x1];
	u8	 fpga[0x1];
	u8	 rol_s[0x1];
	u8	 rol_g[0x1];
	u8	 ib_port_sniffer[0x1];
	u8	 wol_s[0x1];
	u8	 wol_g[0x1];
	u8	 wol_a[0x1];
	u8	 wol_b[0x1];
	u8	 wol_m[0x1];
	u8	 wol_u[0x1];
	u8	 wol_p[0x1];

	u8	 stat_rate_support[0x10];
	u8	 reserved_at_1f0[0xc];
	u8	 cqe_version[0x4];

	u8	 compact_address_vector[0x1];
	u8	 eth_striding_wq[0x1];
	u8	 reserved_at_202[0x1];
	u8	 ipoib_enhanced_offloads[0x1];
	u8	 ipoib_basic_offloads[0x1];
	u8	 ib_striding_wq[0x1];
	u8	 repeated_block_disabled[0x1];
	u8	 umr_modify_entity_size_disabled[0x1];
	u8	 umr_modify_atomic_disabled[0x1];
	u8	 umr_indirect_mkey_disabled[0x1];
	u8	 umr_fence[0x2];
	u8	 dc_req_sctr_data_cqe[0x1];
	u8	 dc_connect_qp[0x1];
	u8	 dc_cnak_trace[0x1];
	u8	 drain_sigerr[0x1];
	u8	 cmdif_checksum[0x2];
	u8	 sigerr_cqe[0x1];
	u8	 reserved_at_213[0x1];
	u8	 wq_signature[0x1];
	u8	 sctr_data_cqe[0x1];
	u8	 reserved_at_216[0x1];
	u8	 sho[0x1];
	u8	 tph[0x1];
	u8	 rf[0x1];
	u8	 dct[0x1];
	u8	 qos[0x1];
	u8	 eth_net_offloads[0x1];
	u8	 roce[0x1];
	u8	 atomic[0x1];
	u8	 extended_retry_count[0x1];

	u8	 cq_oi[0x1];
	u8	 cq_resize[0x1];
	u8	 cq_moderation[0x1];
	u8	 cq_period_mode_modify[0x1];
	u8	 cq_invalidate[0x1];
	u8	 reserved_at_225[0x1];
	u8	 cq_eq_remap[0x1];
	u8	 pg[0x1];
	u8	 block_lb_mc[0x1];
	u8	 exponential_backoff[0x1];
	u8	 scqe_break_moderation[0x1];
	u8	 cq_period_start_from_cqe[0x1];
	u8	 cd[0x1];
	u8	 atm[0x1];
	u8	 apm[0x1];
	u8	 vector_calc[0x1];
	u8	 umr_ptr_rlkey[0x1];
	u8	 imaicl[0x1];
	u8	 qp_packet_based[0x1];
	u8	 reserved_at_233[0x1];
	u8	 ipoib_enhanced_pkey_change[0x1];
	u8	 initiator_src_dct_in_cqe[0x1];
	u8	 qkv[0x1];
	u8	 pkv[0x1];
	u8	 set_deth_sqpn[0x1];
	u8	 rts2rts_primary_sl[0x1];
	u8	 initiator_src_dct[0x1];
	u8	 dc_v2[0x1];
	u8	 xrc[0x1];
	u8	 ud[0x1];
	u8	 uc[0x1];
	u8	 rc[0x1];

	u8	 uar_4k[0x1];
	u8	 reserved_at_241[0x7];
	u8	 fl_rc_qp_when_roce_disabled[0x1];
	u8	 regexp_rof[0x1];
	u8	 uar_sz[0x6];
	u8	 reserved_at_250[0x3];
	u8	 log_max_dc_cnak_qps[0x5];
	u8	 log_pg_sz[0x8];

	u8	 bf[0x1];
	u8	 driver_version[0x1];
	u8	 pad_tx_eth_packet[0x1];
	u8	 query_driver_version[0x1];
	u8	 max_qp_retry_freq[0x1];
	u8	 qp_by_name[0x1];
	u8	 mkey_by_name[0x1];
	u8	 virtio_fs_device_emulation_manager[0x1];
	u8	 suspend_qp_uc[0x1];
	u8	 suspend_qp_ud[0x1];
	u8	 suspend_qp_rc[0x1];
	u8	 log_bf_reg_size[0x5];
	u8	 reserved_at_270[0x6];
	u8	 lag_dct[0x2];
	u8	 reserved_at_278[0x3];
	u8	 lag_master[0x1];
	u8	 num_lag_ports[0x4];

	u8	 num_of_diagnostic_counters[0x10];
	u8	 max_wqe_sz_sq[0x10];

	u8	 reserved_at_2a0[0xc];
	u8	 regexp_mmo_sq[0x1];
	u8	 regexp_version[0x3];
	u8	 max_wqe_sz_rq[0x10];

	u8	 max_flow_counter_31_16[0x10];
	u8	 max_wqe_sz_sq_dc[0x10];

	u8	 reserved_at_2e0[0x7];
	u8	 max_qp_mcg[0x19];

	u8	 mlnx_tag_ethertype[0x10];
	u8	 reserved_at_310[0x8];
	u8	 log_max_mcg[0x8];

	u8	 reserved_at_320[0x3];
	u8	 log_max_transport_domain[0x5];
	u8	 reserved_at_328[0x3];
	u8	 log_max_pd[0x5];
	u8	 reserved_at_330[0xb];
	u8	 log_max_xrcd[0x5];

	u8	 nic_receive_steering_discard[0x1];
	u8	 receive_discard_vport_down[0x1];
	u8	 transmit_discard_vport_down[0x1];
	u8	 eq_overrun_count[0x1];
	u8	 nic_receive_steering_depth[0x1];
	u8	 invalid_command_count[0x1];
	u8	 quota_exceeded_count[0x1];
	u8	 reserved_at_347[0x1];
	u8	 log_max_flow_counter_bulk[0x8];
	u8	 max_flow_counter_15_0[0x10];

	u8	 modify_tis[0x1];
	u8	 reserved_at_361[0x2];
	u8	 log_max_rq[0x5];
	u8	 reserved_at_368[0x3];
	u8	 log_max_sq[0x5];
	u8	 reserved_at_370[0x3];
	u8	 log_max_tir[0x5];
	u8	 reserved_at_378[0x3];
	u8	 log_max_tis[0x5];

	u8	 basic_cyclic_rcv_wqe[0x1];
	u8	 reserved_at_381[0x2];
	u8	 log_max_rmp[0x5];
	u8	 reserved_at_388[0x3];
	u8	 log_max_rqt[0x5];
	u8	 reserved_at_390[0x3];
	u8	 log_max_rqt_size[0x5];
	u8	 reserved_at_398[0x3];
	u8	 log_max_tis_per_sq[0x5];

	u8	 ext_stride_num_range[0x1];
	u8	 reserved_at_3a1[0x2];
	u8	 log_max_stride_sz_rq[0x5];
	u8	 reserved_at_3a8[0x3];
	u8	 log_min_stride_sz_rq[0x5];
	u8	 reserved_at_3b0[0x3];
	u8	 log_max_stride_sz_sq[0x5];
	u8	 reserved_at_3b8[0x3];
	u8	 log_min_stride_sz_sq[0x5];

	u8	 hairpin[0x1];
	u8	 reserved_at_3c1[0x2];
	u8	 log_max_hairpin_queues[0x5];
	u8	 reserved_at_3c8[0x3];
	u8	 log_max_hairpin_wq_data_sz[0x5];
	u8	 reserved_at_3d0[0x3];
	u8	 log_max_hairpin_num_packets[0x5];
	u8	 reserved_at_3d8[0x3];
	u8	 log_max_wq_sz[0x5];

	u8	 nic_vport_change_event[0x1];
	u8	 disable_local_lb_uc[0x1];
	u8	 disable_local_lb_mc[0x1];
	u8	 log_min_hairpin_wq_data_sz[0x5];
	u8	 reserved_at_3e8[0x3];
	u8	 log_max_vlan_list[0x5];
	u8	 reserved_at_3f0[0x3];
	u8	 log_max_current_mc_list[0x5];
	u8	 reserved_at_3f8[0x3];
	u8	 log_max_current_uc_list[0x5];

	u8	 general_obj_types[0x40];

	u8	 reserved_at_440[0x8];
	u8	 create_qp_start_hint[0x18];

	u8	 reserved_at_460[0x1];
	u8	 ats[0x1];
	u8	 reserved_at_462[0x1];
	u8	 log_max_uctx[0x5];
	u8	 aes_xts[0x1];
	u8	 crypto[0x1];
	u8	 ipsec_offload[0x1];
	u8	 log_max_umem[0x5];
	u8	 max_num_eqs[0x10];

	u8	 reserved_at_480[0x3];
	u8	 log_max_l2_table[0x5];
	u8	 reserved_at_488[0x8];
	u8	 log_uar_page_sz[0x10];

	u8	 reserved_at_4a0[0x20];

	u8	 device_frequency_mhz[0x20];

	u8	 device_frequency_khz[0x20];

	u8	 capi[0x1];
	u8	 create_pec[0x1];
	u8	 nvmf_target_offload[0x1];
	u8	 capi_invalidate[0x1];
	u8	 reserved_at_504[0x17];
	u8	 log_max_pasid[0x5];

	u8	 num_of_uars_per_page[0x20];

	u8	 flex_parser_protocols[0x20];

	u8	 reserved_at_560[0x13];
	u8	 log_max_guaranteed_connections[0x5];
	u8	 reserved_at_578[0x3];
	u8	 log_max_dct_connections[0x5];

	u8	 log_max_atomic_size_qp[0x8];
	u8	 reserved_at_588[0x10];
	u8	 log_max_atomic_size_dc[0x8];

	u8	 reserved_at_5a0[0x12];
	u8	 crossing_vhca_mkey[0x1];
	u8	 reserved_at_5b3[0x9];
	u8	 mini_cqe_resp_stride_index[0x1];
	u8	 cqe_128_always[0x1];
	u8	 cqe_compression_128b[0x1];
	u8	 cqe_compression[0x1];

	u8	 cqe_compression_timeout[0x10];
	u8	 cqe_compression_max_num[0x10];

	u8	 reserved_at_5e0[0xc];
	u8	 log_max_tm_offloaded_op_size[0x4];
	u8	 tag_matching[0x1];
	u8	 rndv_offload_rc[0x1];
	u8	 rndv_offload_dc[0x1];
	u8	 log_tag_matching_list_sz[0x5];
	u8	 reserved_at_5f8[0x3];
	u8	 log_max_xrq[0x5];

	u8	 affiliate_nic_vport_criteria[0x8];
	u8	 native_port_num[0x8];
	u8	 num_vhca_ports[0x8];
	u8	 reserved_at_618[0x5];
	u8	 trusted_vnic_vhca[0x1];
	u8	 sw_owner_id[0x1];
	u8	 reserve_not_to_use[0x1];
	u8	 reserved_at_620[0xa0];
	u8	 reserved_at_6c0[0x8];
	u8	 flex_parser_id_icmp_dw1[0x4];
	u8	 flex_parser_id_icmp_dw0[0x4];
	u8	 flex_parser_id_icmpv6_dw1[0x4];
	u8	 flex_parser_id_icmpv6_dw0[0x4];
	u8	 flex_parser_id_outer_first_mpls_over_gre[0x4];
	u8	 flex_parser_id_outer_first_mpls_over_udp_label[0x4];

	u8	 reserved_at_6b8[0x60];

	u8	 reserved_at_718[0x8];
	u8	 dma_mmo_qp[0x1];
	u8	 regexp_mmo_qp[0x1];
	u8	 compress_mmo_qp[0x1];
	u8	 decompress_mmo_qp[0x1];
	u8	 reserved_at_730[0x14];

	u8	 reserved_at_738[0xa0];
};

struct mlx5_ifc_cmd_hca_cap2_bits {
	u8	reserved_at_0[0x80];

	u8	reserved_at_10[0x12];
	u8	query_vuid[0x1];
	u8	reserved_at_11[0xd];

	u8	reserved_at_14[0x20];

	u8	reserved_at_18[0x20];

	u8	cross_vhca_object_to_object_supported[0x20];

	u8	allowed_object_for_other_vhca_access[0x40];

	u8	reserved_at_28[0x6a0];
};

enum cross_vhca_object_support_bit {
	CROSS_VHCA_OBJ_SUPPORT_LCQ_TO_RUMEM = 1 << 0,
	CROSS_VHCA_OBJ_SUPPORT_LSQ_TO_RUMEM = 1 << 1,
	CROSS_VHCA_OBJ_SUPPORT_LRQ_TO_RUMEM = 1 << 2,
	CROSS_VHCA_OBJ_SUPPORT_LQP_TO_RUMEM = 1 << 3,
	CROSS_VHCA_OBJ_SUPPORT_LCQ_TO_RAPU = 1 << 4,
	CROSS_VHCA_OBJ_SUPPORT_LNVME_SQ_BE_TO_RNVME_SQ = 1 << 5,
};

struct mlx5_ifc_header_modify_cap_properties_bits {
	struct mlx5_ifc_flow_table_fields_supported_bits set_action_field_support;

	u8	 reserved_at_80[0x80];

	struct mlx5_ifc_flow_table_fields_supported_bits add_action_field_support;

	u8	 reserved_at_180[0x80];

	u8	 copy_action_field_support[8][0x20];

	u8	 reserved_at_300[0x100];
};

struct mlx5_ifc_flow_table_nic_cap_bits {
	u8	 nic_rx_multi_path_tirs[0x1];
	u8	 nic_rx_multi_path_tirs_fts[0x1];
	u8	 allow_sniffer_and_nic_rx_shared_tir[0x1];
	u8	 reserved_at_3[0x1];
	u8	 nic_rx_flow_tag_multipath_en[0x1];
	u8	 reserved_at_5[0x13];
	u8	 nic_receive_max_steering_depth[0x8];

	u8	 encap_general_header[0x1];
	u8	 reserved_at_21[0xa];
	u8	 log_max_packet_reformat_context[0x5];
	u8	 reserved_at_30[0x6];
	u8	 max_encap_header_size[0xa];

	u8	 reserved_at_40[0x1c0];

	struct mlx5_ifc_flow_table_prop_layout_bits flow_table_properties_nic_receive;

	struct mlx5_ifc_flow_table_prop_layout_bits flow_table_properties_nic_receive_rdma;

	struct mlx5_ifc_flow_table_prop_layout_bits flow_table_properties_nic_receive_sniffer;

	struct mlx5_ifc_flow_table_prop_layout_bits flow_table_properties_nic_transmit;

	struct mlx5_ifc_flow_table_prop_layout_bits flow_table_properties_nic_transmit_rdma;

	struct mlx5_ifc_flow_table_prop_layout_bits flow_table_properties_nic_transmit_sniffer;

	u8	 reserved_at_e00[0x200];

	struct mlx5_ifc_header_modify_cap_properties_bits header_modify_nic_receive;

	u8	 reserved_at_1400[0x800];

	struct mlx5_ifc_header_modify_cap_properties_bits header_modify_nic_transmit;

	u8	 sw_steering_nic_rx_action_drop_icm_address[0x40];

	u8	 sw_steering_nic_tx_action_drop_icm_address[0x40];

	u8	 sw_steering_nic_tx_action_allow_icm_address[0x40];

	u8	 reserved_at_20c0[0x5f40];
};

struct mlx5_ifc_flow_table_eswitch_cap_bits {
	u8      reserved_at_0[0x1c];
	u8      fdb_multi_path_to_table[0x1];
	u8      reserved_at_1d[0x1e3];

	struct mlx5_ifc_flow_table_prop_layout_bits flow_table_properties_nic_esw_fdb;

	struct mlx5_ifc_flow_table_prop_layout_bits flow_table_properties_esw_acl_ingress;

	struct mlx5_ifc_flow_table_prop_layout_bits flow_table_properties_esw_acl_egress;

	u8	reserved_at_800[0x1000];

	u8	sw_steering_fdb_action_drop_icm_address_rx[0x40];
	u8	sw_steering_fdb_action_drop_icm_address_tx[0x40];
	u8	sw_steering_uplink_icm_address_rx[0x40];
	u8	sw_steering_uplink_icm_address_tx[0x40];

	u8      reserved_at_1900[0x6700];
};

struct mlx5_ifc_odp_per_transport_service_cap_bits {
	u8	 send[0x1];
	u8	 receive[0x1];
	u8	 write[0x1];
	u8	 read[0x1];
	u8	 atomic[0x1];
	u8	 srq_receive[0x1];
	u8	 reserved_at_6[0x1a];
};

struct mlx5_ifc_odp_cap_bits {
	u8	 reserved_at_0[0x40];

	u8	 sig[0x1];
	u8	 reserved_at_41[0x1f];

	u8	 reserved_at_60[0x20];

	struct mlx5_ifc_odp_per_transport_service_cap_bits rc_odp_caps;

	struct mlx5_ifc_odp_per_transport_service_cap_bits uc_odp_caps;

	struct mlx5_ifc_odp_per_transport_service_cap_bits ud_odp_caps;

	struct mlx5_ifc_odp_per_transport_service_cap_bits xrc_odp_caps;

	struct mlx5_ifc_odp_per_transport_service_cap_bits dc_odp_caps;

	u8	 reserved_at_120[0x6e0];
};

struct mlx5_ifc_nvme_emulation_cap_bits {
	u8	 nvme_offload_type_sqe[0x1];
	u8	 nvme_offload_type_doorbell_only[0x1];
	u8	 nvme_offload_type_command_capsule[0x1];
	u8	 log_max_nvme_offload_namespaces[0x5];
	u8	 nvme_cq_interrupt_disabled[0x1];
	u8	 reserved_at_10[0x7];
	u8	 max_emulated_devices[0x10];

	u8	 reserved_at_20[0xb];
	u8	 log_max_nvme_offload_nsid[0x5];
	u8	 registers_size[0x10];

	u8	 log_max_queue_depth[0x10];
	u8	 reserved_at_40[0x3];
	u8	 log_max_emulated_sq[0x5];
	u8	 reserved_at_58[0x3];
	u8	 log_max_emulated_cq[0x5];

	u8	 reserved_at_60[0x7a0];
};

enum {
	MLX5_VIRTIO_QUEUE_CAP_TYPE_SPLIT = 1 << 0,
	MLX5_VIRTIO_QUEUE_CAP_TYPE_PACKED = 1 << 1,
};

enum {
	MLX5_VIRTIO_QUEUE_TYPE_SPLIT = 0x0,
	MLX5_VIRTIO_QUEUE_TYPE_PACKED = 0x1,
};

enum {
	MLX5_VIRTIO_QUEUE_CAP_EVENT_MODE_NO_MSIX = 1 << 0,
	MLX5_VIRTIO_QUEUE_CAP_EVENT_MODE_QP = 1 << 1,
	MLX5_VIRTIO_QUEUE_CAP_EVENT_MODE_MSIX = 1 << 2,
};

enum {
	MLX5_VIRTIO_QUEUE_EVENT_MODE_NO_MSIX = 0x0,
	MLX5_VIRTIO_QUEUE_EVENT_MODE_QP = 0x1,
	MLX5_VIRTIO_QUEUE_EVENT_MODE_MSIX = 0x2,
};

struct mlx5_ifc_virtio_emulation_cap_bits {
	u8	 desc_tunnel_offload_type[0x1];
	u8	 eth_frame_offload_type[0x1];
	u8	 virtio_version_1_0[0x1];
	u8	 tso_ipv4[0x1];
	u8	 tso_ipv6[0x1];
	u8	 tx_csum[0x1];
	u8	 rx_csum[0x1];
	u8	 reserved_at_7[0x9];
	u8	 event_mode[0x8];
	u8	 virtio_queue_type[0x8];

	u8	 max_tunnel_desc[0x10];
	u8	 dirty_byte_map[0x1];
	u8	 queue_period_upon_cqe[0x1];
	u8	 queue_period_upon_event[0x1];
	u8	 log_doorbell_stride[0x5];
	u8	 vnet_modify_ext[0x1];
	u8	 virtio_net_q_addr_modify[0x1];
	u8	 virtio_q_index_modify[0x1];
	u8	 log_doorbell_bar_size[0x5];

	u8	 doorbell_bar_offset[0x40];

	u8	 max_emulated_devices[0x8];
	u8	 max_num_virtio_queues[0x18];

	u8	 reserved_at_120[0x60];

	u8	 umem_1_buffer_param_a[0x20];
	u8	 umem_1_buffer_param_b[0x20];
	u8	 umem_2_buffer_param_a[0x20];
	u8	 umem_2_buffer_param_b[0x20];
	u8	 umem_3_buffer_param_a[0x20];
	u8	 umem_3_buffer_param_b[0x20];

	u8	 max_num_vf_dynamic_msix[0x10];
	u8	 min_num_vf_dynamic_msix[0x10];

	u8	 reserved_at_60[0x600];
};

// PRM - HotPlug Device Object Fields
enum {
	MLX5_HOTPLUG_DEVICE_TYPE_NVME		= 0x0,
	MLX5_HOTPLUG_DEVICE_TYPE_VIRTIO_NET	= 0x1,
	MLX5_HOTPLUG_DEVICE_TYPE_VIRTIO_BLK	= 0x2,
	MLX5_HOTPLUG_DEVICE_TYPE_VIRTIO_FS	= 0x3,
};

// PRM - Hotplug Capabilities Field Descriptions
enum {
	MLX5_HOTPLUG_DEVICE_BIT_MASK_NVME	= 1 << 0x0,
	MLX5_HOTPLUG_DEVICE_BIT_MASK_VIRTIO_NET	= 1 << 0x1,
	MLX5_HOTPLUG_DEVICE_BIT_MASK_VIRTIO_BLK	= 1 << 0x2,
	MLX5_HOTPLUG_DEVICE_BIT_MASK_VIRTIO_FS	= 1 << 0x4,
};

enum {
	MLX5_VIRTIO_DEVICE_MODIFY_STATUS = 1 << 0,
	MLX5_VIRTIO_DEVICE_MODIFY_LINK = 1 << 1,
	MLX5_VIRTIO_DEVICE_MODIFY_RESET = 1 << 2,
	MLX5_VIRTIO_DEVICE_MODIFY_PCI_COMMON_CFG = 1 << 3,
	MLX5_VIRTIO_DEVICE_MODIFY_DEV_CFG = 1 << 4,
	MLX5_VIRTIO_DEVICE_MODIFY_ALL = 1 << 6,
	MLX5_VIRTIO_DEVICE_MODIFY_QUEUE_CFG = 1 << 7,
	MLX5_VIRTIO_DEVICE_MODIFY_NUM_MSIX = 1 << 8,
	MLX5_VIRTIO_DEVICE_MODIFY_DYN_VF_MSIX_RESET = 1 << 9,
	MLX5_VIRTIO_DEVICE_MODIFY_PCI_HOTPLUG_STATE = 1 << 10,
};

struct mlx5_ifc_hotplug_cap_bits {
	u8	 hotplug_device_types_supported[0x10];
	u8	 log_max_bar_size[0x8];
	u8	 max_hotplug_devices[0x8];

	u8	 pci_hotplug_state_change[0x1];
	u8	 reserved_at_21[0x0f];
	u8	 max_total_vfs[0x10];

	u8	 reserved_at_40[0x7c0];
};

enum {
	MLX5_ROCE_VERSION_1_0 = 1 << 0,
	MLX5_ROCE_VERSION_1_5 = 1 << 1,
	MLX5_ROCE_VERSION_2_0 = 1 << 2,
};

struct mlx5_ifc_roce_cap_bits {
	u8	   roce_apm[0x1];
	u8	   rts2rts_primary_eth_prio[0x1];
	u8	   roce_rx_allow_untagged[0x1];
	u8	   rts2rts_src_addr_index_for_vlan_valid_vlan_id[0x1];
	u8	   sw_r_roce_src_udp_port[0x01];
	u8	   reserved[0x01];
	u8	   fl_rc_qp_when_roce_enabled[0x01];
	u8	   reserved_at_0[0x19];

	u8	   reserved_at_32[0x60];

	u8	   reserved_at_128[0xc];
	u8	   l3_type[0x4];
	u8	   reserved_at_144[0x8];
	u8	   roce_version[0x8];

	u8	   reserved_at_160[0x10];
	u8	   r_roce_dest_udp_port[0x10];

	u8	   r_roce_max_src_udp_port[0x10];
	u8	   r_roce_min_src_udp_port[0x10];

	u8	   reserved_at_224[0x10];
	u8	   roce_address_table_size[0x10];

	u8	   reserved_at_256[0x700];
};

struct mlx5_ifc_crypto_cap_bits {
	u8	   wrapped_crypto_operational[0x1];
	u8	   wrapped_crypto_going_to_commissioning[0x1];
	u8	   reserved_at_0x2[0x16];
	u8	   wrapped_import_method[0x8];

	u8	   reserved_at_0x20[0xb];
	u8	   log_max_num_deks[0x5];
	u8	   reserved_at_0x30[0x3];
	u8	   log_max_num_import_keks[0x5];
	u8	   reserved_at_0x38[0x3];
	u8	   log_max_num_creds[0x5];

	u8	   failed_selftests[0x10];
	u8	   num_nv_import_keks[0x8];
	u8	   num_nv_credentials[0x8];

	u8	   reserved_at_0x60[0x7a0];
};

union mlx5_ifc_hca_cap_union_bits {
	struct mlx5_ifc_atomic_caps_bits atomic_caps;
	struct mlx5_ifc_cmd_hca_cap_bits cmd_hca_cap;
	struct mlx5_ifc_cmd_hca_cap2_bits cmd_hca_cap2;
	struct mlx5_ifc_flow_table_nic_cap_bits flow_table_nic_cap;
	struct mlx5_ifc_flow_table_eswitch_cap_bits flow_table_eswitch_cap;
	struct mlx5_ifc_device_mem_cap_bits device_mem_cap;
	struct mlx5_ifc_odp_cap_bits odp_cap;
	struct mlx5_ifc_nvme_emulation_cap_bits nvme_emulation_cap;
	struct mlx5_ifc_virtio_emulation_cap_bits virtio_emulation_cap;
	struct mlx5_ifc_hotplug_cap_bits hotplug_cap;
	struct mlx5_ifc_roce_cap_bits roce_cap;
	struct mlx5_ifc_crypto_cap_bits crypto_cap;
	u8	 reserved_at_0[0x8000];
};

struct mlx5_ifc_query_hca_cap_out_bits {
	u8	 status[0x8];
	u8	 reserved_at_8[0x18];

	u8	 syndrome[0x20];

	u8	 reserved_at_40[0x40];

	union mlx5_ifc_hca_cap_union_bits capability;
};

struct mlx5_ifc_query_hca_cap_in_bits {
	u8	 opcode[0x10];
	u8	 reserved_at_10[0x10];

	u8	 reserved_at_20[0x10];
	u8	 op_mod[0x10];

	u8	 other_function[0x1];
	u8	 reserved_at_41[0xf];
	u8	 function_id[0x10];

	u8	 reserved_at_60[0x20];
};

// PRM - QUERY_EMULATED_FUNCTIONS_INFO Input Structure Field Descriptions
enum {
	MLX5_SET_EMULATED_FUNCTIONS_OP_MOD_NVME_PHYSICAL_FUNCTIONS = 0x0,
	MLX5_SET_EMULATED_FUNCTIONS_OP_MOD_VIRTIO_NET_PHYSICAL_FUNCTIONS = 0x1,
	MLX5_SET_EMULATED_FUNCTIONS_OP_MOD_VIRTIO_BLK_PHYSICAL_FUNCTIONS = 0x2,
	MLX5_SET_EMULATED_FUNCTIONS_OP_MOD_VIRTUAL_FUNCTIONS = 0x3,
	MLX5_SET_EMULATED_FUNCTIONS_OP_MOD_VIRTIO_FS_PHYSICAL_FUNCTIONS = 0x5
};

struct mlx5_ifc_emulated_function_info_bits {
	u8	 pci_bdf[0x10];
	u8	 vhca_id[0x10];

	u8	 hotplug_function[0x1];
	u8	 max_num_vfs_valid[0x1];
	u8	 vf_exist[0x1];
	u8	 reserved_at_23[0xd];
	u8	 max_num_vfs[0x10];
};

struct mlx5_ifc_query_emulated_functions_info_out_bits {
	u8	 status[0x8];
	u8	 reserved_at_8[0x18];

	u8	 syndrome[0x20];

	u8	 reserved_at_40[0x30];
	u8	 num_emulated_functions[0x10];

	struct mlx5_ifc_emulated_function_info_bits emulated_function_info[0];
};

struct mlx5_ifc_query_emulated_functions_info_in_bits {
	u8	 opcode[0x10];
	u8	 uid[0x10];

	u8	 reserved_at_20[0x10];
	u8	 op_mod[0x10];

	u8	 reserved_at_40[0x20];

	u8	 reserved_at_60[0x10];
	u8	 pf_vhca_id[0x10];
};

struct mlx5_ifc_allow_other_vhca_access_in_bits {
	u8	 opcode[0x10];
	u8	 uid[0x10];

	u8	 reserved_at_20[0x10];
	u8	 op_mod[0x10];

	u8	 reserved_at_40[0x40];

	u8	 reserved_at_80[0x10];
	u8	 object_type_to_be_accessed[0x10];

	u8	 object_id_to_be_accessed[0x20];

	u8	 reserved_at_180[0x40];

	u8	 access_key[0x100];
};

struct mlx5_ifc_allow_other_vhca_access_out_bits {
	u8	 status[0x8];
	u8	 reserved_at_8[0x18];

	u8	 syndrome[0x20];

	u8	 reserved_at_40[0x40];
};

enum mlx5_cap_type {
	MLX5_CAP_ODP = 2,
	MLX5_CAP_ATOMIC = 3,
};

// PRM - QUERY_HCA_CAP Input Structure Field Descriptions
enum {
	MLX5_SET_HCA_CAP_OP_MOD_GENERAL_DEVICE	      = 0x0 << 1,
	MLX5_SET_HCA_CAP_OP_MOD_ROCE		      = 0x4 << 1,
	MLX5_SET_HCA_CAP_OP_MOD_NIC_FLOW_TABLE	      = 0x7 << 1,
	MLX5_SET_HCA_CAP_OP_MOD_ESW_FLOW_TABLE	      = 0x8 << 1,
	MLX5_SET_HCA_CAP_OP_MOD_DEVICE_MEMORY	       = 0xf << 1,
	MLX5_SET_HCA_CAP_OP_MOD_NVME_DEVICE_EMULATION       = 0x10 << 1,
	MLX5_SET_HCA_CAP_OP_MOD_VIRTIO_NET_DEVICE_EMULATION = 0x16 << 1,
	MLX5_SET_HCA_CAP_OP_MOD_VIRTIO_BLK_DEVICE_EMULATION = 0x17 << 1,
	MLX5_SET_HCA_CAP_OP_MOD_HOTPLUG		     = 0x18 << 1,
	MLX5_SET_HCA_CAP_OP_MOD_CRYPTO		     = 0x1A << 1,
	MLX5_SET_HCA_CAP_OP_MOD_GENERAL_DEVICE2	     = 0x20 << 1,
	MLX5_SET_HCA_CAP_OP_MOD_VIRTIO_FS_DEVICE_EMULATION  = 0x23 << 1,
};

struct mlx5_ifc_mac_address_layout_bits {
	u8	 reserved_at_0[0x10];
	u8	 mac_addr_47_32[0x10];

	u8	 mac_addr_31_0[0x20];
};

struct mlx5_ifc_nic_vport_context_bits {
	u8	 reserved_at_0[0x5];
	u8	 min_wqe_inline_mode[0x3];
	u8	 reserved_at_8[0x15];
	u8	 disable_mc_local_lb[0x1];
	u8	 disable_uc_local_lb[0x1];
	u8	 roce_en[0x1];

	u8	 arm_change_event[0x1];
	u8	 reserved_at_21[0x1a];
	u8	 event_on_mtu[0x1];
	u8	 event_on_promisc_change[0x1];
	u8	 event_on_vlan_change[0x1];
	u8	 event_on_mc_address_change[0x1];
	u8	 event_on_uc_address_change[0x1];

	u8	 reserved_at_40[0xc];

	u8	 affiliation_criteria[0x4];
	u8	 affiliated_vhca_id[0x10];

	u8	 reserved_at_60[0xd0];

	u8	 mtu[0x10];

	u8	 system_image_guid[0x40];
	u8	 port_guid[0x40];
	u8	 node_guid[0x40];

	u8	 reserved_at_200[0x140];
	u8	 qkey_violation_counter[0x10];
	u8	 reserved_at_350[0x430];

	u8	 promisc_uc[0x1];
	u8	 promisc_mc[0x1];
	u8	 promisc_all[0x1];
	u8	 reserved_at_783[0x2];
	u8	 allowed_list_type[0x3];
	u8	 reserved_at_788[0xc];
	u8	 allowed_list_size[0xc];

	struct mlx5_ifc_mac_address_layout_bits permanent_address;

	u8	 reserved_at_7e0[0x20];

	u8	 current_uc_mac_address[0][0x40];
};

struct mlx5_ifc_query_nic_vport_context_out_bits {
	u8	 status[0x8];
	u8	 reserved_at_8[0x18];

	u8	 syndrome[0x20];

	u8	 reserved_at_40[0x40];

	struct mlx5_ifc_nic_vport_context_bits nic_vport_context;
};

struct mlx5_ifc_query_nic_vport_context_in_bits {
	u8	 opcode[0x10];
	u8	 uid[0x10];

	u8	 reserved_at_20[0x10];
	u8	 op_mod[0x10];

	u8	 other_vport[0x1];
	u8	 reserved_at_41[0xf];
	u8	 vport_number[0x10];

	u8	 reserved_at_60[0x5];
	u8	 allowed_list_type[0x3];
	u8	 reserved_at_68[0x18];
};

enum {
	MLX5_MKC_ACCESS_MODE_KLMS = 0x2,
	MLX5_MKC_ACCESS_MODE_KLMFBS = 0x3,
	MLX5_MKC_ACCESS_MODE_CROSSING_VHCA_MKEY = 0x6,
};

struct mlx5_ifc_klm_bits {
	u8	 byte_count[0x20];
	u8	 mkey[0x20];
	u8	 address[0x40];
};

struct mlx5_ifc_mkc_bits {
	u8	 reserved_at_0[0x1];
	u8	 free[0x1];
	u8	 reserved_at_2[0x1];
	u8	 access_mode_4_2[0x3];
	u8	 reserved_at_6[0x7];
	u8	 relaxed_ordering_write[0x1];
	u8	 reserved_at_e[0x1];
	u8	 small_fence_on_rdma_read_response[0x1];
	u8	 umr_en[0x1];
	u8	 a[0x1];
	u8	 rw[0x1];
	u8	 rr[0x1];
	u8	 lw[0x1];
	u8	 lr[0x1];
	u8	 access_mode_1_0[0x2];
	u8	 reserved_at_18[0x8];

	u8	 qpn[0x18];
	u8	 mkey_7_0[0x8];

	u8	 reserved_at_40[0x20];

	u8	 length64[0x1];
	u8	 bsf_en[0x1];
	u8	 sync_umr[0x1];
	u8	 reserved_at_63[0x2];
	u8	 expected_sigerr_count[0x1];
	u8	 reserved_at_66[0x1];
	u8	 en_rinval[0x1];
	u8	 pd[0x18];

	u8	 start_addr[0x40];

	u8	 len[0x40];

	u8	 bsf_octword_size[0x20];

	u8	 reserved_at_120[0x10];
	u8	 bsf_handle_47_32[0x10];

	u8	 bsf_handle_31_0[0x20];

	u8	 reserved_at_160[0x20];

	u8	 crossing_target_vhca_id[0x10];
	u8	 reserved_at_190[0x10];

	u8	 translations_octword_size_crossing_target_mkey[0x20];

	u8	 reserved_at_1c0[0x19];
	u8	 relaxed_ordering_read[0x1];
	u8	 reserved_at_1da[0x1];
	u8	 log_page_size[0x5];

	u8	 reserved_at_1e0[0x3];
	u8	 crypto_en[0x2];
	u8	 reserved_at_1e5[0xb];
	u8	 generation_counter[0x10];
};

struct mlx5_ifc_create_mkey_out_bits {
	u8	 status[0x8];
	u8	 reserved_at_8[0x18];

	u8	 syndrome[0x20];

	u8	 reserved_at_40[0x8];
	u8	 mkey_index[0x18];

	u8	 reserved_at_60[0x20];
};

struct mlx5_ifc_create_mkey_in_bits {
	u8	 opcode[0x10];
	u8	 reserved_at_10[0x10];

	u8	 reserved_at_20[0x10];
	u8	 op_mod[0x10];

	u8	 reserved_at_40[0x20];

	u8	 pg_access[0x1];
	u8	 mkey_umem_valid[0x1];
	u8	 reserved_at_62[0x1e];

	struct mlx5_ifc_mkc_bits memory_key_mkey_entry;

	u8	 reserved_at_280[0x80];

	u8	 translations_octword_actual_size[0x20];

	u8	 reserved_at_320[0x560];

	u8	 klm_pas_mtt[0][0x20];
};

struct mlx5_ifc_l2_hdr_bits {
	u8	 dmac_47_16[0x20];
	u8	 dmac_15_0[0x10];
	u8	 smac_47_32[0x10];
	u8	 smac_31_0[0x20];
	u8	 ethertype[0x10];
	u8	 vlan_type[0x10];
	u8	 vlan[0x10];
};

enum {
	FS_FT_NIC_RX		= 0x0,
	FS_FT_NIC_TX		= 0x1,
	FS_FT_ESW_EGRESS_ACL    = 0x2,
	FS_FT_ESW_INGRESS_ACL   = 0x3,
	FS_FT_FDB		= 0X4,
	FS_FT_SNIFFER_RX	= 0X5,
	FS_FT_SNIFFER_TX	= 0X6,
	FS_FT_NIC_RX_RDMA	= 0x7,
	FS_FT_NIC_TX_RDMA	= 0x8,
};

struct mlx5_ifc_ste_general_bits {
	u8	 entry_type[0x4];
	u8	 reserved_at_4[0x4];
	u8	 entry_sub_type[0x8];
	u8	 byte_mask[0x10];
	u8	 next_table_base_63_48[0x10];
	u8	 next_lu_type[0x8];
	u8	 next_table_base_39_32_size[0x8];
	u8	 next_table_base_31_5_size[0x1b];
	u8	 linear_hash_enable[0x1];
	u8	 reserved_at_5c[0x2];
	u8	 next_table_rank[0x2];
	u8	 reserved_at_60[0xa0];
	u8	 tag_value[0x60];
	u8	 bit_mask[0x60];
};

struct mlx5_ifc_ste_sx_transmit_bits {
	u8	 entry_type[0x4];
	u8	 reserved_at_4[0x4];
	u8	 entry_sub_type[0x8];
	u8	 byte_mask[0x10];
	u8	 next_table_base_63_48[0x10];
	u8	 next_lu_type[0x8];
	u8	 next_table_base_39_32_size[0x8];
	u8	 next_table_base_31_5_size[0x1b];
	u8	 linear_hash_enable[0x1];
	u8	 reserved_at_5c[0x2];
	u8	 next_table_rank[0x2];
	u8	 sx_wire;
	u8	 sx_func_lb;
	u8	 sx_sniffer;
	u8	 sx_wire_enable;
	u8	 sx_func_lb_enable;
	u8	 sx_sniffer_enable;
	u8	 action_type[3];
	u8	 reserved_at_69;
	u8	 action_description[6];
	u8	 gvmi[16];
	u8	 encap_pointer_vlan_data[32];
	u8	 loopback_syndome_en[8];
	u8	 loopback_syndome[8];
	u8	 counter_trigger[16];
	u8	 miss_address_63_48[16];
	u8	 counter_trigger_23_16[8];
	u8	 miss_address_39_32[8];
	u8	 miss_address_31_6[26];
	u8	 learning_point;
	u8	 go_back;
	u8	 match_polarity;
	u8	 mask_mode;
	u8	 miss_rank[2];
};

struct mlx5_ifc_ste_rx_steering_mult_bits {
	u8	 entry_type[0x4];
	u8	 reserved_at_4[0x4];
	u8	 entry_sub_type[0x8];
	u8	 byte_mask[0x10];
	u8	 next_table_base_63_48[0x10];
	u8	 next_lu_type[0x8];
	u8	 next_table_base_39_32_size[0x8];
	u8	 next_table_base_31_5_size[0x1b];
	u8	 linear_hash_enable[0x1];
	u8	 reserved_at_[0x2];
	u8	 next_table_rank[0x2];
	u8	 member_count[0x10];
	u8	 gvmi[0x10];
	u8	 qp_list_pointer[0x20];
	u8	 reserved_at_a0[0x1];
	u8	 tunneling_action[0x3];
	u8	 action_description[0x4];
	u8	 reserved_at_a8[0x8];
	u8	 counter_trigger_15_0[0x10];
	u8	 miss_address_63_48[0x10];
	u8	 counter_trigger_23_16[0x08];
	u8	 miss_address_39_32[0x8];
	u8	 miss_address_31_6[0x1a];
	u8	 learning_point[0x1];
	u8	 fail_on_error[0x1];
	u8	 match_polarity[0x1];
	u8	 mask_mode[0x1];
	u8	 miss_rank[0x2];
};

struct mlx5_ifc_ste_modify_packet_bits {
	u8	 entry_type[0x4];
	u8	 reserved_at_4[0x4];
	u8	 entry_sub_type[0x8];
	u8	 byte_mask[0x10];
	u8	 next_table_base_63_48[0x10];
	u8	 next_lu_type[0x8];
	u8	 next_table_base_39_32_size[0x8];
	u8	 next_table_base_31_5_size[0x1b];
	u8	 linear_hash_enable[0x1];
	u8	 reserved_at_[0x2];
	u8	 next_table_rank[0x2];
	u8	 number_of_re_write_actions[0x10];
	u8	 gvmi[0x10];
	u8	 header_re_write_actions_pointer[0x20];
	u8	 reserved_at_a0[0x1];
	u8	 tunneling_action[0x3];
	u8	 action_description[0x4];
	u8	 reserved_at_a8[0x8];
	u8	 counter_trigger_15_0[0x10];
	u8	 miss_address_63_48[0x10];
	u8	 counter_trigger_23_16[0x08];
	u8	 miss_address_39_32[0x8];
	u8	 miss_address_31_6[0x1a];
	u8	 learning_point[0x1];
	u8	 fail_on_error[0x1];
	u8	 match_polarity[0x1];
	u8	 mask_mode[0x1];
	u8	 miss_rank[0x2];
};

struct mlx5_ifc_ste_eth_l2_src_bits {
	u8	 smac_47_16[0x20];
	u8	 smac_15_0[0x10];
	u8	 l3_ethertype[0x10];
	u8	 qp_type[0x2];
	u8	 ethertype_filter[0x1];
	u8	 reserved_at_43[0x1];
	u8	 sx_sniffer[0x1];
	u8	 force_lb[0x1];
	u8	 functional_lb[0x1];
	u8	 port[0x1];
	u8	 reserved_at_48[0x4];
	u8	 first_priority[0x3];
	u8	 first_cfi[0x1];
	u8	 first_vlan_qualifier[0x2];
	u8	 reserved_at_52[0x2];
	u8	 first_vlan_id[0xc];
	u8	 ip_fragmented[0x1];
	u8	 tcp_syn[0x1];
	u8	 encp_type[0x2];
	u8	 l3_type[0x2];
	u8	 l4_type[0x2];
	u8	 reserved_at_68[0x4];
	u8	 second_priority[0x3];
	u8	 second_cfi[0x1];
	u8	 second_vlan_qualifier[0x2];
	u8	 reserved_at_72[0x2];
	u8	 second_vlan_id[0xc];
};

struct mlx5_ifc_ste_eth_l2_dst_bits {
	u8	 dmac_47_16[0x20];
	u8	 dmac_15_0[0x10];
	u8	 l3_ethertype[0x10];
	u8	 qp_type[0x2];
	u8	 ethertype_filter[0x1];
	u8	 reserved_at_43[0x1];
	u8	 sx_sniffer[0x1];
	u8	 force_lb[0x1];
	u8	 functional_lb[0x1];
	u8	 port[0x1];
	u8	 reserved_at_48[0x4];
	u8	 first_priority[0x3];
	u8	 first_cfi[0x1];
	u8	 first_vlan_qualifier[0x2];
	u8	 reserved_at_52[0x2];
	u8	 first_vlan_id[0xc];
	u8	 ip_fragmented[0x1];
	u8	 tcp_syn[0x1];
	u8	 encp_type[0x2];
	u8	 l3_type[0x2];
	u8	 l4_type[0x2];
	u8	 reserved_at_68[0x4];
	u8	 second_priority[0x3];
	u8	 second_cfi[0x1];
	u8	 second_vlan_qualifier[0x2];
	u8	 reserved_at_72[0x2];
	u8	 second_vlan_id[0xc];
};

struct mlx5_ifc_ste_eth_l2_src_dst_bits {
	u8	 dmac_47_16[0x20];
	u8	 dmac_15_0[0x10];
	u8	 smac_47_32[0x10];
	u8	 smac_31_0[0x20];
	u8	 sx_sniffer[0x1];
	u8	 force_lb[0x1];
	u8	 functional_lb[0x1];
	u8	 port[0x1];
	u8	 l3_type[0x2];
	u8	 reserved_at_66[0x6];
	u8	 first_priority[0x3];
	u8	 first_cfi[0x1];
	u8	 first_vlan_qualifier[0x2];
	u8	 reserved_at_72[0x2];
	u8	 first_vlan_id[0xc];
};

struct mlx5_ifc_ste_eth_l3_ipv4_5_tuple_bits {
	u8	 destination_address[0x20];
	u8	 source_address[0x20];
	u8	 source_port[0x10];
	u8	 destination_port[0x10];
	u8	 fragmented[0x1];
	u8	 first_fragment[0x1];
	u8	 reserved_at_62[0x2];
	u8	 reserved_at_64[0x1];
	u8	 ecn[0x2];
	u8	 tcp_ns[0x1];
	u8	 tcp_cwr[0x1];
	u8	 tcp_ece[0x1];
	u8	 tcp_urg[0x1];
	u8	 tcp_ack[0x1];
	u8	 tcp_psh[0x1];
	u8	 tcp_rst[0x1];
	u8	 tcp_syn[0x1];
	u8	 tcp_fin[0x1];
	u8	 dscp[0x6];
	u8	 reserved_at_76[0x2];
	u8	 protocol[0x8];
};

struct mlx5_ifc_ste_eth_l3_ipv6_dst_bits {
	u8	 dst_ip_127_96[0x20];
	u8	 dst_ip_95_64[0x20];
	u8	 dst_ip_63_32[0x20];
	u8	 dst_ip_31_0[0x20];
};

struct mlx5_ifc_ste_eth_l2_tnl_bits {
	u8	 dmac_47_16[0x20];
	u8	 dmac_15_0[0x10];
	u8	 l3_ethertype[0x10];
	u8	 l2_tunneling_network_id[0x20];
	u8	 ip_fragmented[0x1];
	u8	 tcp_syn[0x1];
	u8	 encp_type[0x2];
	u8	 l3_type[0x2];
	u8	 l4_type[0x2];
	u8	 first_priority[0x3];
	u8	 first_cfi[0x1];
	u8	 reserved_at_6c[0x3];
	u8	 gre_key_flag[0x1];
	u8	 first_vlan_qualifier[0x2];
	u8	 reserved_at_72[0x2];
	u8	 first_vlan_id[0xc];
};

struct mlx5_ifc_ste_eth_l3_ipv6_src_bits {
	u8	 src_ip_127_96[0x20];
	u8	 src_ip_95_64[0x20];
	u8	 src_ip_63_32[0x20];
	u8	 src_ip_31_0[0x20];
};

struct mlx5_ifc_ste_eth_l3_ipv4_misc_bits {
	u8	 version[0x4];
	u8	 ihl[0x4];
	u8	 reserved_at_8[0x8];
	u8	 total_length[0x10];
	u8	 identification[0x10];
	u8	 flags[0x3];
	u8	 fragment_offset[0xd];
	u8	 time_to_live[0x8];
	u8	 reserved_at_48[0x8];
	u8	 checksum[0x10];
	u8	 reserved_at_60[0x20];
};

struct mlx5_ifc_ste_eth_l4_bits {
	u8	 fragmented[0x1];
	u8	 first_fragment[0x1];
	u8	 reserved_at_2[0x6];
	u8	 protocol[0x8];
	u8	 dst_port[0x10];
	u8	 ipv6_version[0x4];
	u8	 reserved_at_24[0x1];
	u8	 ecn[0x2];
	u8	 tcp_ns[0x1];
	u8	 tcp_cwr[0x1];
	u8	 tcp_ece[0x1];
	u8	 tcp_urg[0x1];
	u8	 tcp_ack[0x1];
	u8	 tcp_psh[0x1];
	u8	 tcp_rst[0x1];
	u8	 tcp_syn[0x1];
	u8	 tcp_fin[0x1];
	u8	 src_port[0x10];
	u8	 ipv6_payload_length[0x10];
	u8	 ipv6_hop_limit[0x8];
	u8	 dscp[0x6];
	u8	 reserved_at_5e[0x2];
	u8	 tcp_data_offset[0x4];
	u8	 reserved_at_64[0x8];
	u8	 flow_label[0x14];
};

struct mlx5_ifc_ste_eth_l4_misc_bits {
	u8	 checksum[0x10];
	u8	 length[0x10];
	u8	 seq_num[0x20];
	u8	 ack_num[0x20];
	u8	 urgent_pointer[0x10];
	u8	 window_size[0x10];
};

struct mlx5_ifc_ste_mpls_bits {
	u8	 mpls0_label[0x14];
	u8	 mpls0_exp[0x3];
	u8	 mpls0_s_bos[0x1];
	u8	 mpls0_ttl[0x8];
	u8	 mpls1_label[0x20];
	u8	 mpls2_label[0x20];
	u8	 reserved_at_60[0x16];
	u8	 mpls4_s_bit[0x1];
	u8	 mpls4_qualifier[0x1];
	u8	 mpls3_s_bit[0x1];
	u8	 mpls3_qualifier[0x1];
	u8	 mpls2_s_bit[0x1];
	u8	 mpls2_qualifier[0x1];
	u8	 mpls1_s_bit[0x1];
	u8	 mpls1_qualifier[0x1];
	u8	 mpls0_s_bit[0x1];
	u8	 mpls0_qualifier[0x1];
};

struct mlx5_ifc_ste_register_0_bits {
	u8	 register_0_h[0x20];
	u8	 register_0_l[0x20];
	u8	 register_1_h[0x20];
	u8	 register_1_l[0x20];
};

struct mlx5_ifc_ste_register_1_bits {
	u8	 register_2_h[0x20];
	u8	 register_2_l[0x20];
	u8	 register_3_h[0x20];
	u8	 register_3_l[0x20];
};

struct mlx5_ifc_ste_gre_bits {
	u8	 gre_c_present[0x1];
	u8	 reserved_at_30[0x1];
	u8	 gre_k_present[0x1];
	u8	 gre_s_present[0x1];
	u8	 strict_src_route[0x1];
	u8	 recur[0x3];
	u8	 flags[0x5];
	u8	 version[0x3];
	u8	 gre_protocol[0x10];
	u8	 checksum[0x10];
	u8	 offset[0x10];
	u8	 gre_key_h[0x18];
	u8	 gre_key_l[0x8];
	u8	 seq_num[0x20];
};

struct mlx5_ifc_ste_flex_parser_0_bits {
	u8	 parser_3_label[0x14];
	u8	 parser_3_exp[0x3];
	u8	 parser_3_s_bos[0x1];
	u8	 parser_3_ttl[0x8];
	u8	 flex_parser_2[0x20];
	u8	 flex_parser_1[0x20];
	u8	 flex_parser_0[0x20];
};

struct mlx5_ifc_ste_flex_parser_1_bits {
	u8	 flex_parser_7[0x20];
	u8	 flex_parser_6[0x20];
	u8	 flex_parser_5[0x20];
	u8	 flex_parser_4[0x20];
};

struct mlx5_ifc_ste_flex_parser_tnl_bits {
	u8	 flex_parser_tunneling_header_63_32[0x20];
	u8	 flex_parser_tunneling_header_31_0[0x20];
	u8	 reserved_at_40[0x40];
};

struct mlx5_ifc_ste_general_purpose_bits {
	u8	 general_purpose_lookup_field[0x20];
	u8	 reserved_at_20[0x20];
	u8	 reserved_at_40[0x20];
	u8	 reserved_at_60[0x20];
};

struct mlx5_ifc_ste_src_gvmi_qp_bits {
	u8	 loopback_syndrome[0x8];
	u8	 reserved_at_8[0x8];
	u8	 source_gvmi[0x10];
	u8	 reserved_at_20[0x5];
	u8	 force_lb[0x1];
	u8	 functional_lb[0x1];
	u8	 source_is_requestor[0x1];
	u8	 source_qp[0x18];
	u8	 reserved_at_40[0x20];
	u8	 reserved_at_60[0x20];
};

struct mlx5_ifc_set_action_in_bits {
	u8	 action_type[0x4];
	u8	 field[0xc];
	u8	 reserved_at_10[0x3];
	u8	 offset[0x5];
	u8	 reserved_at_18[0x3];
	u8	 length[0x5];

	u8	 data[0x20];
};

struct mlx5_ifc_add_action_in_bits {
	u8	 action_type[0x4];
	u8	 field[0xc];
	u8	 reserved_at_10[0x10];

	u8	 data[0x20];
};

struct mlx5_ifc_copy_action_in_bits {
	u8	 action_type[0x4];
	u8	 src_field[0xc];
	u8	 reserved_at_10[0x3];
	u8	 src_offset[0x5];
	u8	 reserved_at_18[0x3];
	u8	 length[0x5];

	u8	 reserved_at_20[0x4];
	u8	 dst_field[0xc];
	u8	 reserved_at_30[0x3];
	u8	 dst_offset[0x5];
	u8	 reserved_at_38[0x8];
};

enum {
	MLX5_ACTION_TYPE_SET   = 0x1,
	MLX5_ACTION_TYPE_ADD   = 0x2,
	MLX5_ACTION_TYPE_COPY  = 0x3,
};

enum {
	MLX5_ACTION_IN_FIELD_OUT_SMAC_47_16    = 0x1,
	MLX5_ACTION_IN_FIELD_OUT_SMAC_15_0     = 0x2,
	MLX5_ACTION_IN_FIELD_OUT_ETHERTYPE     = 0x3,
	MLX5_ACTION_IN_FIELD_OUT_DMAC_47_16    = 0x4,
	MLX5_ACTION_IN_FIELD_OUT_DMAC_15_0     = 0x5,
	MLX5_ACTION_IN_FIELD_OUT_IP_DSCP       = 0x6,
	MLX5_ACTION_IN_FIELD_OUT_TCP_FLAGS     = 0x7,
	MLX5_ACTION_IN_FIELD_OUT_TCP_SPORT     = 0x8,
	MLX5_ACTION_IN_FIELD_OUT_TCP_DPORT     = 0x9,
	MLX5_ACTION_IN_FIELD_OUT_IP_TTL	= 0xa,
	MLX5_ACTION_IN_FIELD_OUT_UDP_SPORT     = 0xb,
	MLX5_ACTION_IN_FIELD_OUT_UDP_DPORT     = 0xc,
	MLX5_ACTION_IN_FIELD_OUT_SIPV6_127_96  = 0xd,
	MLX5_ACTION_IN_FIELD_OUT_SIPV6_95_64   = 0xe,
	MLX5_ACTION_IN_FIELD_OUT_SIPV6_63_32   = 0xf,
	MLX5_ACTION_IN_FIELD_OUT_SIPV6_31_0    = 0x10,
	MLX5_ACTION_IN_FIELD_OUT_DIPV6_127_96  = 0x11,
	MLX5_ACTION_IN_FIELD_OUT_DIPV6_95_64   = 0x12,
	MLX5_ACTION_IN_FIELD_OUT_DIPV6_63_32   = 0x13,
	MLX5_ACTION_IN_FIELD_OUT_DIPV6_31_0    = 0x14,
	MLX5_ACTION_IN_FIELD_OUT_SIPV4	 = 0x15,
	MLX5_ACTION_IN_FIELD_OUT_DIPV4	 = 0x16,
	MLX5_ACTION_IN_FIELD_OUT_FIRST_VID     = 0x17,
	MLX5_ACTION_IN_FIELD_OUT_IPV6_HOPLIMIT = 0x47,
	MLX5_ACTION_IN_FIELD_OUT_METADATA_REGA = 0x49,
	MLX5_ACTION_IN_FIELD_OUT_METADATA_REGB = 0x50,
	MLX5_ACTION_IN_FIELD_OUT_METADATA_REGC_0 = 0x51,
	MLX5_ACTION_IN_FIELD_OUT_METADATA_REGC_1 = 0x52,
	MLX5_ACTION_IN_FIELD_OUT_METADATA_REGC_2 = 0x53,
	MLX5_ACTION_IN_FIELD_OUT_METADATA_REGC_3 = 0x54,
	MLX5_ACTION_IN_FIELD_OUT_METADATA_REGC_4 = 0x55,
	MLX5_ACTION_IN_FIELD_OUT_METADATA_REGC_5 = 0x56,
	MLX5_ACTION_IN_FIELD_OUT_TCP_SEQ_NUM   = 0x59,
	MLX5_ACTION_IN_FIELD_OUT_TCP_ACK_NUM   = 0x5B,
};

struct mlx5_ifc_packet_reformat_context_in_bits {
	u8	reserved_at_0[0x5];
	u8	reformat_type[0x3];
	u8	reserved_at_8[0xe];
	u8	reformat_data_size[0xa];

	u8	reserved_at_20[0x10];
	u8	reformat_data[2][0x8];

	u8	more_reformat_data[0][0x8];
};

struct mlx5_ifc_alloc_packet_reformat_context_in_bits {
	u8	 opcode[0x10];
	u8	 reserved_at_10[0x10];

	u8	 reserved_at_20[0x10];
	u8	 op_mod[0x10];

	u8	 reserved_at_40[0xa0];

	struct mlx5_ifc_packet_reformat_context_in_bits packet_reformat_context;
};

struct mlx5_ifc_alloc_packet_reformat_context_out_bits {
	u8	status[0x8];
	u8	reserved_at_8[0x18];

	u8	syndrome[0x20];

	u8	packet_reformat_id[0x20];

	u8	reserved_at_60[0x20];
};

struct mlx5_ifc_dealloc_packet_reformat_context_in_bits {
	u8	opcode[0x10];
	u8	reserved_at_10[0x10];

	u8	reserved_20[0x10];
	u8	op_mod[0x10];

	u8	packet_reformat_id[0x20];

	u8	reserved_60[0x20];
};

struct mlx5_ifc_dealloc_packet_reformat_context_out_bits {
	u8	status[0x8];
	u8	reserved_at_8[0x18];

	u8	syndrome[0x20];

	u8	reserved_at_40[0x40];
};

enum reformat_type {
	MLX5_REFORMAT_TYPE_L2_TO_VXLAN = 0x0,
	MLX5_REFORMAT_TYPE_L2_TO_NVGRE = 0x1,
	MLX5_REFORMAT_TYPE_L2_TO_L2_TUNNEL = 0x2,
	MLX5_REFORMAT_TYPE_L3_TUNNEL_TO_L2 = 0x3,
	MLX5_REFORMAT_TYPE_L2_TO_L3_TUNNEL = 0x4,
};

struct mlx5_ifc_dealloc_pd_out_bits {
	u8	 status[0x8];
	u8	 reserved_at_8[0x18];

	u8	 syndrome[0x20];

	u8	 reserved_at_40[0x40];
};

struct mlx5_ifc_dealloc_pd_in_bits {
	u8	 opcode[0x10];
	u8	 uid[0x10];

	u8	 reserved_at_20[0x10];
	u8	 op_mod[0x10];

	u8	 reserved_at_40[0x8];
	u8	 pd[0x18];

	u8	 reserved_at_60[0x20];
};

struct mlx5_ifc_alloc_pd_out_bits {
	u8	 status[0x8];
	u8	 reserved_at_8[0x18];

	u8	 syndrome[0x20];

	u8	 reserved_at_40[0x8];
	u8	 pd[0x18];

	u8	 reserved_at_60[0x20];
};

struct mlx5_ifc_alloc_pd_in_bits {
	u8	 opcode[0x10];
	u8	 uid[0x10];

	u8	 reserved_at_20[0x10];
	u8	 op_mod[0x10];

	u8	 reserved_at_40[0x40];
};

struct mlx5_ifc_alloc_flow_counter_in_bits {
	u8	opcode[0x10];
	u8	uid[0x10];

	u8	reserved_at_20[0x10];
	u8	op_mod[0x10];

	u8	reserved_at_40[0x40];
};

struct mlx5_ifc_alloc_flow_counter_out_bits {
	u8	status[0x8];
	u8	reserved_at_8[0x18];

	u8	syndrome[0x20];

	u8	flow_counter_id[0x20];

	u8	reserved_at_60[0x20];
};

// PRM - HCA Capabilities Field Descriptions
enum mlx5_obj_type {
	MLX5_OBJ_TYPE_NVME_DEVICE_EMULATION = 0x0006,
	MLX5_OBJ_TYPE_NVME_SQ = 0x0007,
	MLX5_OBJ_TYPE_NVME_CQ = 0x0009,
	MLX5_OBJ_TYPE_FLOW_METER = 0x000a,
	MLX5_OBJ_TYPE_DEK = 0x000c,
	MLX5_OBJ_TYPE_VIRTIO_NET_Q = 0x000d,
	MLX5_OBJ_TYPE_VHCA_TUNNEL = 0x000e,
	MLX5_OBJ_TYPE_NVME_NAMESPACE = 0x000f,
	MLX5_OBJ_TYPE_NVME_CTRL_COUNTERS = 0x0010,
	MLX5_OBJ_TYPE_NVME_NAMESPACE_COUNTERS = 0x0011,
	MLX5_OBJ_TYPE_NVME_SQ_COUNTERS = 0x0012,
	MLX5_OBJ_TYPE_DEVICE = 0x0014,
	MLX5_OBJ_TYPE_VIRTIO_NET_DEVICE_EMULATION = 0x0015,
	MLX5_OBJ_TYPE_VIRTIO_BLK_DEVICE_EMULATION = 0x0016,
	MLX5_OBJ_TYPE_VIRTIO_BLK_Q = 0x0017,
	MLX5_OBJ_TYPE_VIRTIO_Q_COUNTERS = 0x001c,
	MLX5_OBJ_TYPE_CRYPTO_LOGIN = 0x001f,
	MLX5_OBJ_TYPE_NVME_SQ_BE = 0x0038,
	MLX5_OBJ_TYPE_VIRTIO_FS_DEVICE_EMULATION = 0x003d,
	MLX5_OBJ_TYPE_VIRTIO_FS_Q = 0x003e,

	MLX5_OBJ_TYPE_APU_MEM = 0x10000000000,
	MLX5_OBJ_TYPE_APU_PROCESS = 0x40000000000,
	MLX5_OBJ_TYPE_APU_THREAD = 0x80000000000,
	MLX5_OBJ_TYPE_APU_OUTBOX = 0x200000000000,
	MLX5_OBJ_TYPE_APU_EQ = 0x8000000000000,
	MLX5_OBJ_TYPE_APU_WINDOW = 0x200000000000000,
};

enum {
	MLX5_NVME_SQ_OFFLOAD_TYPE_SQE = 0x0,
	MLX5_NVME_SQ_OFFLOAD_TYPE_CC = 0x2,
};

enum {
	MLX5_NVME_SQ_STATE_INIT	= 0x0,
	MLX5_NVME_SQ_STATE_RDY	= 0x1,
	MLX5_NVME_SQ_STATE_ERR	= 0x2,
};

enum {
	MLX5_NVME_SQ_MODIFY_QPN = 1 << 0,
	MLX5_NVME_SQ_MODIFY_STATE = 1 << 1,
};

enum {
	MLX5_NVME_CQ_OFFLOAD_TYPE_SQE = 0x0,
	MLX5_NVME_CQ_OFFLOAD_TYPE_CC = 0x2,
};

enum {
	MLX5_NVME_DEVICE_MODIFY_BAR_CAP_VS_CSTS = 1 << 0,
	MLX5_NVME_DEVICE_MODIFY_BAR_CC = 1 << 1,
	MLX5_NVME_DEVICE_MODIFY_BAR_AQA_ASQ_ACQ = 1 << 2,
	MLX5_NVME_DEVICE_MODIFY_PCI_HOTPLUG_STATE = 1 << 3,

};

struct mlx5_ifc_general_obj_in_cmd_hdr_bits {
	u8	 opcode[0x10];
	u8	 uid[0x10];

	u8	 reserved_at_20[0x10];
	u8	 obj_type[0x10];

	u8	 obj_id[0x20];

	u8	 alias_object[0x1];
	u8	 reserved_at_61[0x1f];
};

struct mlx5_ifc_general_obj_out_cmd_hdr_bits {
	u8	 status[0x8];
	u8	 reserved_at_8[0x18];

	u8	 syndrome[0x20];

	u8	 obj_id[0x20];

	u8	 reserved_at_60[0x20];
};

struct mlx5_ifc_alias_context_bits {
	u8	 vhca_id_to_be_accessed[0x10];
	u8	 reserved_at_10[0x10];

	u8	 object_id_to_be_accessed[0x20];

	u8	 reserved_at_40[0x40];

	u8	 access_key[0x100];

	u8	 reserved_at_180[0x80];
};

struct mlx5_ifc_flow_meter_parameters_bits {
	u8	 valid[0x1];
	u8	 bucket_overflow[0x1];
	u8	 start_color[0x2];
	u8	 both_buckets_on_green[0x1];
	u8	 byte_token_ratio[0x3];
	u8	 meter_mode[0x2];
	u8	 reserved_at_a[0x16];

	u8	 reserved_at_20[0x20];

	u8	 reserved_at_40[0x3];
	u8	 cbs_exponent[0x5];
	u8	 cbs_mantissa[0x8];
	u8	 reserved_at_50[0x3];
	u8	 cir_exponent[0x5];
	u8	 cir_mantissa[0x8];

	u8	 reserved_at_60[0x20];

	u8	 reserved_at_80[0x3];
	u8	 ebs_exponent[0x5];
	u8	 ebs_mantissa[0x8];
	u8	 reserved_at_90[0x3];
	u8	 eir_exponent[0x5];
	u8	 eir_mantissa[0x8];

	u8	 reserved_at_a0[0x60];
};

struct mlx5_ifc_flow_meter_bits {
	u8	 modify_field_select[0x40];

	u8	 active[0x1];
	u8	 reserved_at_41[0x3];
	u8	 return_reg_id[0x4];
	u8	 table_type[0x8];
	u8	 reserved_at_50[0x10];

	u8	 reserved_at_60[0x8];
	u8	 destination_table_id[0x18];

	u8	 reserved_at_80[0x80];

	struct mlx5_ifc_flow_meter_parameters_bits flow_meter_params;

	u8	 reserved_at_180[0x180];

	u8	 sw_steering_icm_address_rx[0x40];
	u8	 sw_steering_icm_address_tx[0x40];
};

struct mlx5_ifc_create_flow_meter_in_bits {
	struct mlx5_ifc_general_obj_in_cmd_hdr_bits   hdr;
	struct mlx5_ifc_flow_meter_bits	       meter;
};

struct mlx5_ifc_query_flow_meter_out_bits {
	struct mlx5_ifc_general_obj_out_cmd_hdr_bits   hdr;
	struct mlx5_ifc_flow_meter_bits		obj;
};

struct mlx5_ifc_esw_vport_context_bits {
	u8	 reserved_at_0[0x3];
	u8	 vport_svlan_strip[0x1];
	u8	 vport_cvlan_strip[0x1];
	u8	 vport_svlan_insert[0x1];
	u8	 vport_cvlan_insert[0x2];
	u8	 reserved_at_8[0x18];

	u8	 reserved_at_20[0x20];

	u8	 svlan_cfi[0x1];
	u8	 svlan_pcp[0x3];
	u8	 svlan_id[0xc];
	u8	 cvlan_cfi[0x1];
	u8	 cvlan_pcp[0x3];
	u8	 cvlan_id[0xc];

	u8	 reserved_at_40[0x720];
	u8	 sw_steering_vport_icm_address_rx[0x40];
	u8	 sw_steering_vport_icm_address_tx[0x40];
};

struct mlx5_ifc_query_esw_vport_context_out_bits {
	u8	 status[0x8];
	u8	 reserved_at_8[0x18];

	u8	 syndrome[0x20];

	u8	 reserved_at_40[0x40];

	struct mlx5_ifc_esw_vport_context_bits esw_vport_context;
};

struct mlx5_ifc_query_esw_vport_context_in_bits {
	u8	 opcode[0x10];
	u8	 reserved_at_10[0x10];

	u8	 reserved_at_20[0x10];
	u8	 op_mod[0x10];

	u8	 other_vport[0x1];
	u8	 reserved_at_41[0xf];
	u8	 vport_number[0x10];

	u8	 reserved_at_60[0x20];
};

struct mlx5_ifc_vhca_tunnel_bits {
	u8	 modify_field_select[0x40];

	u8	 reserved_at_40[0x10];
	u8	 vhca_id[0x10];
};

struct mlx5_ifc_vhca_tunnel_cmd_bits {
	u8	 reserved_at_0[0x20];

	u8	 vhca_tunnel_id[0x10];
	u8	 op_mod[0x10];
};

struct mlx5_ifc_device_pci_parameters_bits {
	u8	   device_id[0x10];
	u8	   vendor_id[0x10];

	u8	   revision_id[0x8];
	u8	   class_code[0x18];

	u8	   subsystem_id[0x10];
	u8	   subsystem_vendor_id[0x10];

	u8	   num_of_vfs[0x10];
	u8	   num_msix[0x10];
};

struct mlx5_ifc_nvme_initial_registers_bits {
	u8	cap[0x40];
	u8	vs[0x20];
	u8	intms[0x20];
	u8	intmc[0x20];
	u8	cc[0x20];
	u8	csts[0x20];
	u8	nssr[0x20];
	u8	aqa[0x20];
	u8	asq[0x20];
	u8	acq[0x20];
	u8	cmbloc[0x20];
	u8	cmbsz[0x20];
	u8	bpinfo[0x20];
	u8	bprsel[0x20];
	u8	bpmbl[0x20];
};

struct mlx5_ifc_virtio_net_config_bits {
	u8	   mac_47_16[0x20];

	u8	   mac_15_0[0x10];
	u8	   status[0x10];

	u8	   max_virtqueue_pairs[0x10];
	u8	   mtu[0x10];

	u8	   reserved_at_60[0x20];
};

struct mlx5_ifc_virtio_net_initial_registers_bits {
	u8	device_features[0x40];

	u8	reserved_at_40[0x10];
	u8	queue_size[0x10];

	u8	reserved_at_60[0x20];

	struct mlx5_ifc_virtio_net_config_bits virtio_net_config;
};

struct mlx5_ifc_virtio_blk_config_bits {
	u8	   capacity[0x40];

	u8	   size_max[0x20];

	u8	   seg_max[0x20];

	u8	   reserved_at_80[0x20];

	u8	   cylinders[0x10];
	u8	   heads[0x8];
	u8	   sectors[0x8];

	u8	   reserved_at_c0[0x20];

	u8	   blk_size[0x20];

	u8	   reserved_at_100[0x20];

	u8	   physical_blk_exp[0x8];
	u8	   alignment_offset[0x8];
	u8	   min_io_size[0x10];

	u8	   opt_io_size[0x20];

	u8	   reserved_at_140[0x20];

	u8	   writeback[0x8];
	u8	   reserved_at_168[0x8];
	u8	   num_queues[0x10];

	u8	   max_discard_sectors[0x20];

	u8	   max_discard_seg[0x20];

	u8	   discard_sector_alignment[0x20];

	u8	   max_write_zeroes_sectors[0x20];

	u8	   max_write_zeroes_segs[0x20];

	u8	   write_zeroes_may_unmap[0x8];
	u8	   reserved_at_228[0x18];

	u8	   reserved_at_240[0x20];
};

struct mlx5_ifc_virtio_blk_initial_registers_bits {
	u8	device_features[0x40];

	u8	num_queues[0x10];
	u8	queue_size[0x10];

	u8	reserved_at_60[0x20];

	struct mlx5_ifc_virtio_blk_config_bits virtio_blk_config;
};

struct mlx5_ifc_virtio_fs_config_bits {
	u8         tag[0x120];
	u8	   num_request_queues[0x20];
	u8	   reserved_at_140[0x140];
};

struct mlx5_ifc_virtio_fs_initial_registers_bits {
	u8	device_features[0x40];

	u8	num_queues[0x10];
	u8	queue_size[0x10];

	u8	reserved_at_60[0x20];

	struct mlx5_ifc_virtio_fs_config_bits virtio_fs_config;
};

union mlx5_ifc_initial_registers_layout_bits {
	struct mlx5_ifc_nvme_initial_registers_bits nvme;
	struct mlx5_ifc_virtio_net_initial_registers_bits virtio_net;
	struct mlx5_ifc_virtio_blk_initial_registers_bits virtio_blk;
	struct mlx5_ifc_virtio_fs_initial_registers_bits  virtio_fs;
	u8	reserved_at_0[0x600];
};

struct mlx5_ifc_device_bits {
	u8	   modify_field_select[0x40];

	u8	   reserved_at_40[0x17];
	u8	   initial_registers_valid[0x1];
	u8	   device_type[0x8];

	u8	   total_vf[0x10];
	u8	   vhca_id[0x10];

	struct mlx5_ifc_device_pci_parameters_bits pci_params;

	u8	   reserved_at_e0[0x100];

	union mlx5_ifc_initial_registers_layout_bits emulation_initial_regs;
};

struct mlx5_ifc_virtio_device_bits {
	u8	   device_feature[0x40];

	u8	   driver_feature[0x40];

	u8	   msix_config[0x10];
	u8	   num_queues[0x10];

	u8	   device_status[0x8];
	u8	   config_generation[0x8];
	u8	   pci_bdf[0x10];

	u8	   notify_off_multiplier[0x20];

	u8	   max_queue_size[0x10];
	u8	   reserved_at_f0[0x10];

	u8	   device_feature_select[0x1];
	u8	   driver_feature_select[0x1];
	u8	   reserved_at_f2[0xe];
	u8	   queue_select[0x10];

	u8	   reserved_at_110[0x60];
};

struct mlx5_ifc_virtio_q_layout_bits {
	u8	   queue_size[0x10];
	u8	   queue_msix_vector[0x10];

	u8	   queue_enable[0x10];
	u8	   queue_notify_off[0x10];

	u8	   queue_desc[0x40];

	u8	   queue_driver[0x40];

	u8	   queue_device[0x40];
};

struct mlx5_ifc_virtio_net_device_emulation_bits {
	u8	   modify_field_select[0x40];

	u8	   reserved_at_40[0x10];
	u8	   vhca_id[0x10];

	u8	   enabled[0x1];
	u8	   resources_on_emulation_manager[0x1];
	u8	   reset[0x1];
	u8	   dynamic_vf_msix_control[0x1];
	u8	   dynamic_vf_msix_reset[0x1];
	u8	   pci_hotplug_state[0x3];
	u8	   reserved_at_65[0x8];
	u8	   num_free_dynamic_vfs_msix[0x10];

	u8	   reserved_at_80[0x40];

	u8	   emulated_device_crossed_vhca_mkey[0x20];

	u8	   reserved_at_e0[0x20];

	struct mlx5_ifc_device_pci_parameters_bits pci_params;

	u8	   reserved_at_180[0x80];

	struct mlx5_ifc_virtio_device_bits virtio_device;

	struct mlx5_ifc_virtio_net_config_bits virtio_net_config;

	struct mlx5_ifc_virtio_q_layout_bits virtio_q_configuration[0];
};

struct mlx5_ifc_virtio_blk_device_emulation_bits {
	u8	   modify_field_select[0x40];

	u8	   reserved_at_40[0x10];
	u8	   vhca_id[0x10];

	u8	   enabled[0x1];
	u8	   resources_on_emulation_manager[0x1];
	u8	   reset[0x1];
	u8	   reserved_at_62[0x2];
	u8	   pci_hotplug_state[0x3];
	u8	   reserved_at_68[0x18];

	u8	   reserved_at_80[0x40];

	u8	   emulated_device_crossed_vhca_mkey[0x20];

	u8	   reserved_at_e0[0x20];

	struct mlx5_ifc_device_pci_parameters_bits pci_params;

	u8	   reserved_at_180[0x80];

	struct mlx5_ifc_virtio_device_bits virtio_device;

	struct mlx5_ifc_virtio_blk_config_bits virtio_blk_config;

	struct mlx5_ifc_virtio_q_layout_bits virtio_q_configuration[0];
};

struct mlx5_ifc_virtio_fs_device_emulation_bits {
	u8	   modify_field_select[0x40];

	u8	   reserved_at_40[0x10];
	u8	   vhca_id[0x10];

	u8	   enabled[0x1];
	u8	   resources_on_emulation_manager[0x1];
	u8	   reset[0x1];
	u8	   reserved_at_63[0x1d];

	u8	   reserved_at_80[0x40];

	u8	   emulated_device_crossed_vhca_mkey[0x20];

	u8	   reserved_at_e0[0x20];

	struct mlx5_ifc_device_pci_parameters_bits pci_params;

	u8	   reserved_at_180[0x80];

	struct mlx5_ifc_virtio_device_bits virtio_device;

	struct mlx5_ifc_virtio_fs_config_bits virtio_fs_config;

	struct mlx5_ifc_virtio_q_layout_bits virtio_q_configuration[0];
};

struct mlx5_ifc_nvme_device_emulation_bits {
	u8	   modify_field_select[0x40];

	u8	   pci_bdf[0x10];
	u8	   vhca_id[0x10];

	u8	   enabled[0x1];
	u8	   resources_on_emulation_manager[0x1];
	u8	   reserved_at_62[0x3];
	u8	   pci_hotplug_state[0x3];
	u8	   reserved_at_68[0x18];

	u8	   counter_set_id[0x20];

	u8	   reserved_at_a0[0x20];

	u8	   emulated_device_crossed_vhca_mkey[0x20];

	u8	   reserved_at_e0[0x20];

	struct mlx5_ifc_device_pci_parameters_bits pci_params;

	u8	   register_data[0][0x20];
};

enum {
	MLX5_QPC_ST_RC	    = 0x0,
};

enum {
	MLX5_QPC_PM_STATE_MIGRATED  = 0x3,
};

struct mlx5_ifc_ads_bits {
	u8	 fl[0x1];
	u8	 free_ar[0x1];
	u8	 reserved_at_2[0xe];
	u8	 pkey_index[0x10];

	u8	 reserved_at_20[0x8];
	u8	 grh[0x1];
	u8	 mlid[0x7];
	u8	 rlid[0x10];

	u8	 ack_timeout[0x5];
	u8	 reserved_at_45[0x3];
	u8	 src_addr_index[0x8];
	u8	 reserved_at_50[0x4];
	u8	 stat_rate[0x4];
	u8	 hop_limit[0x8];

	u8	 reserved_at_60[0x4];
	u8	 tclass[0x8];
	u8	 flow_label[0x14];

	u8	 rgid_rip[16][0x8];

	u8	 reserved_at_100[0x4];
	u8	 f_dscp[0x1];
	u8	 f_ecn[0x1];
	u8	 reserved_at_106[0x1];
	u8	 f_eth_prio[0x1];
	u8	 ecn[0x2];
	u8	 dscp[0x6];
	u8	 udp_sport[0x10];

	u8	 dei_cfi[0x1];
	u8	 eth_prio[0x3];
	u8	 sl[0x4];
	u8	 vhca_port_num[0x8];
	u8	 rmac_47_32[0x10];

	u8	 rmac_31_0[0x20];
};

struct mlx5_ifc_qpc_bits {
	u8	 state[0x4];
	u8	 lag_tx_port_affinity[0x4];
	u8	 st[0x8];
	u8	 reserved_at_10[0x3];
	u8	 pm_state[0x2];
	u8	 reserved_at_15[0x1];
	u8	   req_e2e_credit_mode[0x2];
	u8	 offload_type[0x4];
	u8	 end_padding_mode[0x2];
	u8	 reserved_at_1e[0x2];

	u8	 wq_signature[0x1];
	u8	 block_lb_mc[0x1];
	u8	 atomic_like_write_en[0x1];
	u8	 latency_sensitive[0x1];
	u8	 reserved_at_24[0x1];
	u8	 drain_sigerr[0x1];
	u8	 reserved_at_26[0x2];
	u8	 pd[0x18];

	u8	 mtu[0x3];
	u8	 log_msg_max[0x5];
	u8	 reserved_at_48[0x1];
	u8	 log_rq_size[0x4];
	u8	 log_rq_stride[0x3];
	u8	 no_sq[0x1];
	u8	 log_sq_size[0x4];
	u8	 reserved_at_55[0x6];
	u8	 rlky[0x1];
	u8	 ulp_stateless_offload_mode[0x4];

	u8	 counter_set_id[0x8];
	u8	 uar_page[0x18];

	u8	 reserved_at_80[0x8];
	u8	 user_index[0x18];

	u8	 reserved_at_a0[0x3];
	u8	 log_page_size[0x5];
	u8	 remote_qpn[0x18];

	struct mlx5_ifc_ads_bits primary_address_path;

	struct mlx5_ifc_ads_bits secondary_address_path;

	u8	 log_ack_req_freq[0x4];
	u8	 reserved_at_384[0x4];
	u8	 log_sra_max[0x3];
	u8	 reserved_at_38b[0x2];
	u8	 retry_count[0x3];
	u8	 rnr_retry[0x3];
	u8	 reserved_at_393[0x1];
	u8	 fre[0x1];
	u8	 cur_rnr_retry[0x3];
	u8	 cur_retry_count[0x3];
	u8	 reserved_at_39b[0x5];

	u8	 reserved_at_3a0[0x20];

	u8	 reserved_at_3c0[0x8];
	u8	 next_send_psn[0x18];

	u8	 reserved_at_3e0[0x8];
	u8	 cqn_snd[0x18];

	u8	 reserved_at_400[0x8];
	u8	 deth_sqpn[0x18];

	u8	 reserved_at_420[0x20];

	u8	 reserved_at_440[0x8];
	u8	 last_acked_psn[0x18];

	u8	 reserved_at_460[0x8];
	u8	 ssn[0x18];

	u8	 reserved_at_480[0x8];
	u8	 log_rra_max[0x3];
	u8	 reserved_at_48b[0x1];
	u8	 atomic_mode[0x4];
	u8	 rre[0x1];
	u8	 rwe[0x1];
	u8	 rae[0x1];
	u8	 reserved_at_493[0x1];
	u8	 page_offset[0x6];
	u8	 reserved_at_49a[0x3];
	u8	 cd_slave_receive[0x1];
	u8	 cd_slave_send[0x1];
	u8	 cd_master[0x1];

	u8	 reserved_at_4a0[0x3];
	u8	 min_rnr_nak[0x5];
	u8	 next_rcv_psn[0x18];

	u8	 reserved_at_4c0[0x8];
	u8	 xrcd[0x18];

	u8	 reserved_at_4e0[0x8];
	u8	 cqn_rcv[0x18];

	u8	 dbr_addr[0x40];

	u8	 q_key[0x20];

	u8	 reserved_at_560[0x5];
	u8	 rq_type[0x3];
	u8	 srqn_rmpn_xrqn[0x18];

	u8	 reserved_at_580[0x8];
	u8	 rmsn[0x18];

	u8	 hw_sq_wqebb_counter[0x10];
	u8	 sw_sq_wqebb_counter[0x10];

	u8	 hw_rq_counter[0x20];

	u8	 sw_rq_counter[0x20];

	u8	 reserved_at_600[0x20];

	u8	 reserved_at_620[0xf];
	u8	 cgs[0x1];
	u8	 cs_req[0x8];
	u8	 cs_res[0x8];

	u8	 dc_access_key[0x40];

	u8	 reserved_at_680[0x3];
	u8	 dbr_umem_valid[0x1];

	u8	 reserved_at_684[0x9c];

	u8	 dbr_umem_id[0x20];
};

struct mlx5_ifc_qpc_ext_bits {
	u8	delay_drop_en[0x1];
	u8	vl15[0x1];
	u8	mmo[0x1];
	u8	reserved_at_3[0xd];
	u8	dci_stream_channel_id[0x10];

	u8	qos_queue_group_id_requester[0x20];

	u8	qos_queue_group_id_responder[0x20];

	u8	reserved_at_80[0x5a0];
};

struct mlx5_ifc_rx_hash_field_select_bits {
	u8	 l3_prot_type[0x1];
	u8	 l4_prot_type[0x1];
	u8	 selected_fields[0x1e];
};

struct mlx5_ifc_tirc_bits {
	u8	 reserved_at_0[0x20];

	u8	 disp_type[0x4];
	u8	 reserved_at_24[0x1c];

	u8	 reserved_at_40[0x40];

	u8	 reserved_at_80[0x4];
	u8	 lro_timeout_period_usecs[0x10];
	u8	 lro_enable_mask[0x4];
	u8	 lro_max_ip_payload_size[0x8];

	u8	 reserved_at_a0[0x40];

	u8	 reserved_at_e0[0x8];
	u8	 inline_rqn[0x18];

	u8	 rx_hash_symmetric[0x1];
	u8	 reserved_at_101[0x1];
	u8	 tunneled_offload_en[0x1];
	u8	 reserved_at_103[0x5];
	u8	 indirect_table[0x18];

	u8	 rx_hash_fn[0x4];
	u8	 reserved_at_124[0x2];
	u8	 self_lb_block[0x2];
	u8	 transport_domain[0x18];

	u8	 rx_hash_toeplitz_key[10][0x20];

	struct mlx5_ifc_rx_hash_field_select_bits rx_hash_field_selector_outer;

	struct mlx5_ifc_rx_hash_field_select_bits rx_hash_field_selector_inner;

	u8	 reserved_at_2c0[0x4c0];
};

struct mlx5_ifc_create_tir_out_bits {
	u8	 status[0x8];
	u8	 icm_address_63_40[0x18];

	u8	 syndrome[0x20];

	u8	 icm_address_39_32[0x8];
	u8	 tirn[0x18];

	u8	 icm_address_31_0[0x20];
};

struct mlx5_ifc_create_tir_in_bits {
	u8	 opcode[0x10];
	u8	 uid[0x10];

	u8	 reserved_at_20[0x10];
	u8	 op_mod[0x10];

	u8	 reserved_at_40[0xc0];

	struct mlx5_ifc_tirc_bits ctx;
};

enum {
	MLX5_FLOW_CONTEXT_ACTION_ALLOW     = 0x1,
	MLX5_FLOW_CONTEXT_ACTION_DROP      = 0x2,
	MLX5_FLOW_CONTEXT_ACTION_FWD_DEST  = 0x4,
	MLX5_FLOW_CONTEXT_ACTION_COUNT     = 0x8,
	MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT = 0x10,
	MLX5_FLOW_CONTEXT_ACTION_DECAP     = 0x20,
	MLX5_FLOW_CONTEXT_ACTION_MOD_HDR   = 0x40,
	MLX5_FLOW_CONTEXT_ACTION_VLAN_POP  = 0x80,
	MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH = 0x100,
	MLX5_FLOW_CONTEXT_ACTION_VLAN_POP_2  = 0x400,
	MLX5_FLOW_CONTEXT_ACTION_VLAN_PUSH_2 = 0x800,
};

enum mlx5_flow_destination_type {
	MLX5_FLOW_DESTINATION_TYPE_VPORT	= 0x0,
	MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE   = 0x1,
	MLX5_FLOW_DESTINATION_TYPE_TIR	  = 0x2,
	MLX5_FLOW_DESTINATION_TYPE_QP	   = 0x3,
	MLX5_FLOW_DESTINATION_TYPE_VHCA_RX      = 0x4,
	MLX5_FLOW_DESTINATION_TYPE_VHCA_TX      = 0x5,

	MLX5_FLOW_DESTINATION_TYPE_PORT	 = 0x99,
	MLX5_FLOW_DESTINATION_TYPE_COUNTER      = 0x100,
	MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE_NUM = 0x101,
};

struct mlx5_ifc_delete_fte_out_bits {
	u8	 status[0x8];
	u8	 reserved_at_8[0x18];

	u8	 syndrome[0x20];

	u8	 reserved_at_40[0x40];
};

struct mlx5_ifc_delete_fte_in_bits {
	u8	 opcode[0x10];
	u8	 uid[0x10];

	u8	 reserved_at_20[0x10];
	u8	 op_mod[0x10];

	u8	 other_vport[0x1];
	u8	 reserved_at_41[0xf];
	u8	 vport_number[0x10];

	u8	 reserved_at_60[0x20];

	u8	 table_type[0x8];
	u8	 reserved_at_88[0x18];

	u8	 reserved_at_a0[0x8];
	u8	 table_id[0x18];

	u8	 reserved_at_c0[0x40];

	u8	 flow_index[0x20];

	u8	 reserved_at_120[0xe0];
};

struct mlx5_ifc_dest_format_struct_bits {
	u8	 destination_type[0x8];
	u8	 destination_id[0x18];

	u8	 reserved_at_20[0x20];
};

struct mlx5_ifc_flow_counter_list_bits {
	u8	 flow_counter_id[0x20];

	u8	 reserved_at_20[0x20];
};

union mlx5_ifc_dest_format_struct_flow_counter_list_auto_bits {
	struct mlx5_ifc_dest_format_struct_bits dest_format_struct;
	struct mlx5_ifc_flow_counter_list_bits flow_counter_list;
	u8	 reserved_at_0[0x40];
};

struct mlx5_ifc_vlan_bits {
	u8	 ethtype[0x10];
	u8	 prio[0x3];
	u8	 cfi[0x1];
	u8	 vid[0xc];
};

struct mlx5_ifc_flow_context_bits {
	struct mlx5_ifc_vlan_bits push_vlan;

	u8	 group_id[0x20];

	u8	 reserved_at_40[0x8];
	u8	 flow_tag[0x18];

	u8	 reserved_at_60[0x10];
	u8	 action[0x10];

	u8	 extended_destination[0x1];
	u8	 reserved_at_81[0x1];
	u8	 flow_source[0x2];
	u8	 reserved_at_84[0x4];
	u8	 destination_list_size[0x18];

	u8	 reserved_at_a0[0x8];
	u8	 flow_counter_list_size[0x18];

	u8	 packet_reformat_id[0x20];

	u8	 modify_header_id[0x20];

	struct mlx5_ifc_vlan_bits push_vlan_2;

	u8	 reserved_at_120[0xe0];

	struct mlx5_ifc_fte_match_param_bits match_value;

	u8	 reserved_at_1200[0x600];

	union mlx5_ifc_dest_format_struct_flow_counter_list_auto_bits destination[0];
};

enum {
	MLX5_SET_FTE_MODIFY_ENABLE_MASK_ACTION = 0x0,
	MLX5_SET_FTE_MODIFY_ENABLE_MASK_FLOW_TAG = 0x1,
	MLX5_SET_FTE_MODIFY_ENABLE_MASK_DESTINATION_LIST = 0x2,
	MLX5_SET_FTE_MODIFY_ENABLE_MASK_FLOW_COUNTERS = 0x3,
};

struct mlx5_ifc_set_fte_out_bits {
	u8	 status[0x8];
	u8	 reserved_at_8[0x18];

	u8	 syndrome[0x20];

	u8	 reserved_at_40[0x40];
};

struct mlx5_ifc_set_fte_in_bits {
	u8	 opcode[0x10];
	u8	 uid[0x10];

	u8	 reserved_at_20[0x10];
	u8	 op_mod[0x10];

	u8	 other_vport[0x1];
	u8	 reserved_at_41[0xf];
	u8	 vport_number[0x10];

	u8	 reserved_at_60[0x20];

	u8	 table_type[0x8];
	u8	 reserved_at_88[0x18];

	u8	 reserved_at_a0[0x8];
	u8	 table_id[0x18];

	u8	 reserved_at_c0[0x18];
	u8	 modify_enable_mask[0x8];

	u8	 reserved_at_e0[0x20];

	u8	 flow_index[0x20];

	u8	 reserved_at_120[0xe0];

	struct mlx5_ifc_flow_context_bits flow_context;
};

struct mlx5_ifc_destroy_qp_out_bits {
	u8	 status[0x8];
	u8	 reserved_at_8[0x18];

	u8	 syndrome[0x20];

	u8	 reserved_at_40[0x40];
};

struct mlx5_ifc_destroy_qp_in_bits {
	u8	 opcode[0x10];
	u8	 uid[0x10];

	u8	 reserved_at_20[0x10];
	u8	 op_mod[0x10];

	u8	 reserved_at_40[0x8];
	u8	 qpn[0x18];

	u8	 reserved_at_60[0x20];
};

struct mlx5_ifc_create_qp_out_bits {
	u8	 status[0x8];
	u8	 reserved_at_8[0x18];

	u8	 syndrome[0x20];

	u8	 reserved_at_40[0x8];
	u8	 qpn[0x18];

	u8	 reserved_at_60[0x20];
};

struct mlx5_ifc_create_qp_in_bits {
	u8	 opcode[0x10];
	u8	 uid[0x10];

	u8	 reserved_at_20[0x10];
	u8	 op_mod[0x10];

	u8	 reserved_at_40[0x8];
	u8	 input_qpn[0x18];

	u8	 reserved_at_60[0x20];

	u8	 opt_param_mask[0x20];

	u8	 reserved_at_a0[0x20];

	struct mlx5_ifc_qpc_bits qpc;

	u8	 wq_umem_offset[0x40];

	u8	 wq_umem_id[0x20];

	u8	 wq_umem_valid[0x1];
	u8	 reserved_at_861[0x1f];

	u8	 pas[0][0x40];
};

struct mlx5_ifc_init2rtr_qp_out_bits {
	u8	 status[0x8];
	u8	 reserved_at_8[0x18];

	u8	 syndrome[0x20];

	u8	 reserved_at_40[0x40];
};

struct mlx5_ifc_init2rtr_qp_in_bits {
	u8	 opcode[0x10];
	u8	 uid[0x10];

	u8	 reserved_at_20[0x10];
	u8	 op_mod[0x10];

	u8	 reserved_at_40[0x8];
	u8	 qpn[0x18];

	u8	 reserved_at_60[0x20];

	u8	 opt_param_mask[0x20];

	u8	 reserved_at_a0[0x20];

	struct mlx5_ifc_qpc_bits qpc;

	u8	 reserved_at_800[0x80];
};

struct mlx5_ifc_rtr2rts_qp_out_bits {
	u8	 status[0x8];
	u8	 reserved_at_8[0x18];

	u8	 syndrome[0x20];

	u8	 reserved_at_40[0x40];
};

struct mlx5_ifc_rtr2rts_qp_in_bits {
	u8	 opcode[0x10];
	u8	 uid[0x10];

	u8	 reserved_at_20[0x10];
	u8	 op_mod[0x10];

	u8	 reserved_at_40[0x8];
	u8	 qpn[0x18];

	u8	 reserved_at_60[0x20];

	u8	 opt_param_mask[0x20];

	u8	 reserved_at_a0[0x20];

	struct mlx5_ifc_qpc_bits qpc;

	u8	 reserved_at_800[0x80];
};

struct mlx5_ifc_rst2init_qp_out_bits {
	u8	 status[0x8];
	u8	 reserved_at_8[0x18];

	u8	 syndrome[0x20];

	u8	 reserved_at_40[0x40];
};

struct mlx5_ifc_rst2init_qp_in_bits {
	u8	 opcode[0x10];
	u8	 uid[0x10];

	u8	 reserved_at_20[0x10];
	u8	 op_mod[0x10];

	u8	 reserved_at_40[0x8];
	u8	 qpn[0x18];

	u8	 reserved_at_60[0x20];

	u8	 opt_param_mask[0x20];

	u8	 reserved_at_a0[0x20];

	struct mlx5_ifc_qpc_bits qpc;

	u8	 reserved_at_800[0x80];
};

struct mlx5_ifc_init2init_qp_in_bits {
	u8	 opcode[0x10];
	u8	 uid[0x10];

	u8	 vhca_tunnel_id[0x10];
	u8	 op_mod[0x10];

	u8	 qpc_ext[0x1];
	u8	 reserved_at_41[0x7];
	u8	 qpn[0x18];

	u8	 reserved_at_60[0x20];

	u8	 opt_param_mask[0x20];

	u8	 ece[0x20];

	struct mlx5_ifc_qpc_bits qpc_data;

	u8	 reserved_at_800[0x40];

	u8	 opt_param_mask_95_32[0x40];

	struct mlx5_ifc_qpc_ext_bits qpc_data_extension;
};

struct mlx5_ifc_init2init_qp_out_bits {
	u8	 status[0x8];
	u8	 reserved_at_8[0x18];

	u8	 syndrome[0x20];

	u8	 reserved_at_40[0x40];
};

struct mlx5_ifc_query_special_contexts_out_bits {
	u8	 status[0x8];
	u8	 reserved_at_8[0x18];

	u8	 syndrome[0x20];

	u8	 dump_fill_mkey[0x20];

	u8	 resd_lkey[0x20];

	u8	 null_mkey[0x20];

	u8	 reserved_at_a0[0x60];
};

struct mlx5_ifc_query_special_contexts_in_bits {
	u8	 opcode[0x10];
	u8	 uid[0x10];

	u8	 reserved_at_20[0x10];
	u8	 op_mod[0x10];

	u8	 reserved_at_40[0x40];
};

enum roce_version {
	MLX5_ROCE_VERSION_1	= 0,
	MLX5_ROCE_VERSION_2	= 2,
};

struct mlx5_ifc_roce_addr_layout_bits {
	u8	 source_l3_address[16][0x8];

	u8	 reserved_at_80[0x3];
	u8	 vlan_valid[0x1];
	u8	 vlan_id[0xc];
	u8	 source_mac_47_32[0x10];

	u8	 source_mac_31_0[0x20];

	u8	 reserved_at_c0[0x14];
	u8	 roce_l3_type[0x4];
	u8	 roce_version[0x8];

	u8	 reserved_at_e0[0x20];
};

struct mlx5_ifc_set_roce_address_out_bits {
	u8	 status[0x8];
	u8	 reserved_at_8[0x18];

	u8	 syndrome[0x20];

	u8	 reserved_at_40[0x40];
};

struct mlx5_ifc_set_roce_address_in_bits {
	u8	 opcode[0x10];
	u8	 uid[0x10];

	u8	 reserved_at_20[0x10];
	u8	 op_mod[0x10];

	u8	 roce_address_index[0x10];
	u8	 reserved_at_50[0xc];
	u8	 vhca_port_num[0x4];

	u8	 reserved_at_60[0x20];

	struct mlx5_ifc_roce_addr_layout_bits roce_address;
};

struct mlx5_ifc_query_roce_address_out_bits {
	u8	 status[0x8];
	u8	 reserved_at_8[0x18];

	u8	 syndrome[0x20];

	u8	 reserved_at_40[0x40];

	struct mlx5_ifc_roce_addr_layout_bits roce_address;
};

struct mlx5_ifc_query_roce_address_in_bits {
	u8	 opcode[0x10];
	u8	 reserved_at_10[0x10];

	u8	 reserved_at_20[0x10];
	u8	 op_mod[0x10];

	u8	 roce_address_index[0x10];
	u8	 reserved_at_50[0xc];
	u8	 vhca_port_num[0x4];

	u8	 reserved_at_60[0x20];
};

struct mlx5_ifc_disable_hca_out_bits {
	u8	 status[0x8];
	u8	 reserved_at_8[0x18];

	u8	 syndrome[0x20];

	u8	 reserved_at_40[0x40];
};

struct mlx5_ifc_disable_hca_in_bits {
	u8	 opcode[0x10];
	u8	 uid[0x10];

	u8	 reserved_at_20[0x10];
	u8	 op_mod[0x10];

	u8	 reserved_at_40[0x10];
	u8	 function_id[0x10];

	u8	 reserved_at_60[0x20];
};

struct mlx5_ifc_enable_hca_out_bits {
	u8	 status[0x8];
	u8	 reserved_at_8[0x18];

	u8	 syndrome[0x20];

	u8	 reserved_at_40[0x40];
};

struct mlx5_ifc_enable_hca_in_bits {
	u8	 opcode[0x10];
	u8	 uid[0x10];

	u8	 reserved_at_20[0x10];
	u8	 op_mod[0x10];

	u8	 reserved_at_40[0x10];
	u8	 function_id[0x10];

	u8	 reserved_at_60[0x20];
};

struct mlx5_ifc_teardown_hca_out_bits {
	u8	 status[0x8];
	u8	 reserved_at_8[0x18];

	u8	 syndrome[0x20];

	u8	 reserved_at_40[0x3f];

	u8	 force_state[0x1];
};

enum {
	MLX5_TEARDOWN_HCA_IN_PROFILE_GRACEFUL_CLOSE  = 0x0,
	MLX5_TEARDOWN_HCA_IN_PROFILE_FORCE_CLOSE     = 0x1,
};

struct mlx5_ifc_teardown_hca_in_bits {
	u8	 opcode[0x10];
	u8	 uid[0x10];

	u8	 reserved_at_20[0x10];
	u8	 op_mod[0x10];

	u8	 reserved_at_40[0x10];
	u8	 profile[0x10];

	u8	 reserved_at_60[0x20];
};

struct mlx5_ifc_init_hca_out_bits {
	u8	 status[0x8];
	u8	 reserved_at_8[0x18];

	u8	 syndrome[0x20];

	u8	 reserved_at_40[0x40];
};

struct mlx5_ifc_init_hca_in_bits {
	u8	 opcode[0x10];
	u8	 uid[0x10];

	u8	 reserved_at_20[0x10];
	u8	 op_mod[0x10];

	u8	 reserved_at_40[0x40];

	u8	   sw_owner_id[4][0x20];
};

enum {
	MLX5_VIRTIO_Q_STATE_INIT	= 0x0,
	MLX5_VIRTIO_Q_STATE_RDY		= 0x1,
	MLX5_VIRTIO_Q_STATE_SUSPEND	= 0x2,
	MLX5_VIRTIO_Q_STATE_ERR		= 0x3,
};

enum {
	MLX5_VIRTIO_Q_OFFLOAD_TYPE_ETH_FRAME	= 0x0,
	MLX5_VIRTIO_Q_OFFLOAD_TYPE_DESC_TUNNEL	= 0x1,
};

struct mlx5_ifc_virtio_q_counters_bits {
	u8	   modify_field_select[0x40];

	u8	   reserved_at_40[0x40];

	u8	   received_desc[0x40];

	u8	   completed_desc[0x40];

	u8	   error_cqes[0x20];

	u8	   bad_desc_errors[0x20];

	u8	   exceed_max_chain[0x20];

	u8	   invalid_buffer[0x20];

	u8	   reserved_at_180[0x280];
};

struct mlx5_ifc_virtio_q_bits {
	u8	 virtio_q_type[0x8];
	u8	 reserved_at_8[0x5];
	u8	 event_mode[0x3];
	u8	 queue_index[0x10];

	u8	 full_emulation[0x1];
	u8	 virtio_version_1_0[0x1];
	u8	 reserved_at_22[0x2];
	u8	 offload_type[0x4];
	u8	 event_qpn_or_msix[0x18];

	u8	 doorbell_stride_index[0x10];
	u8	 queue_size[0x10];

	u8	 device_emulation_id[0x20];

	u8	 desc_addr[0x40];

	u8	 used_addr[0x40];

	u8	 available_addr[0x40];

	u8	 virtio_q_mkey[0x20];

	u8	 max_tunnel_desc[0x10];
	u8	 reserved_at_170[0x8];
	u8	   error_type[0x8];

	u8	 umem_1_id[0x20];
	u8	 umem_1_size[0x20];
	u8	 umem_1_offset[0x40];

	u8	 umem_2_id[0x20];
	u8	 umem_2_size[0x20];
	u8	 umem_2_offset[0x40];

	u8	 umem_3_id[0x20];
	u8	 umem_3_size[0x20];
	u8	 umem_3_offset[0x40];

	u8	   counter_set_id[0x20];
	u8	   reserved_at_320[0x8];
	u8	   pd[0x18];
	u8	   reserved_at_340[0x2];
	u8	   queue_period_mode[0x2];
	u8	   queue_period[0xc];
	u8	   queue_max_count[0x10];
	u8	   reserved_at_360[0xa0];
};

enum {
	MLX5_VIRTIO_NET_Q_MODIFY_STATE = 1 << 0,
};

enum {
	MLX5_VIRTIO_NET_Q_DIRTY_BIT_MAP = 1 << 0,
	MLX5_VIRTIO_NET_Q_DIRTY_BYTE_MAP = 1 << 1,
};

/* Virtio net emulation related structures */
struct mlx5_ifc_virtio_net_q_bits {
	u8	   modify_field_select[0x40];

	u8	   reserved_at_40[0x20];

	u8	   vhca_id[0x10];
	u8	   reserved_at_70[0x10];

	u8	   tso_ipv4[0x1];
	u8	   tso_ipv6[0x1];
	u8	   tx_csum[0x1];
	u8	   rx_csum[0x1];
	u8	   reserved_at_84[0x6];
	u8	   dirty_map_dump_enable[0x1];
	u8	   vhost_log_page[0x5];
	u8	   reserved_at_90[0xc];
	u8	   state[0x4];

	u8	   dirty_map_mode[0x2];
	u8	   reserved_at_a2[0x3];
	u8	   queue_feature_bit_mask_2_0[0x3];
	u8	   tisn_or_qpn[0x18];

	u8	   dirty_map_mkey[0x20];

	u8	   dirty_map_size[0x20];

	u8	   dirty_map_addr[0x40];

	u8	   hw_available_index[0x10];
	u8	   hw_used_index[0x10];

	u8	   reserved_at_160[0xa0];

	struct mlx5_ifc_virtio_q_bits virtqc;
};

enum {
	MLX5_VIRTIO_BLK_Q_MODIFY_STATE = 1 << 0,
};

/* Virtio block emulation related structures */
struct mlx5_ifc_virtio_blk_q_bits {
	u8	   modify_field_select[0x40];

	u8	   reserved_at_40[0x20];

	u8	   qpn_vhca_id[0x10];
	u8	   reserved_at_70[0x10];

	u8	   reserved_at_80[0xa];
	u8	   dirty_bitmap_dump_enable[0x1];
	u8	   vhost_log_page[0x5];
	u8	   reserved_at_90[0xc];
	u8	   state[0x4];

	u8	   reserved_at_a0[0x8];
	u8	   qpn[0x18];

	u8	   dirty_bitmap_mkey[0x20];

	u8	   dirty_bitmap_size[0x20];

	u8	   dirty_bitmap_addr[0x40];

	u8	   hw_available_index[0x10];
	u8	   hw_used_index[0x10];

	u8	   reserved_at_160[0xa0];

	struct mlx5_ifc_virtio_q_bits virtqc;
};

enum {
	MLX5_VIRTIO_FS_Q_MODIFY_STATE  = 1 << 0,
};

/* Virtio fs emulation related structures */
struct mlx5_ifc_virtio_fs_q_bits {
	u8	   modify_field_select[0x40];

	u8	   reserved_at_40[0x20];

	u8	   qpn_vhca_id[0x10];
	u8	   reserved_at_70[0x10];

	u8	   reserved_at_80[0xa];
	u8	   dirty_bitmap_dump_enable[0x1];
	u8	   vhost_log_page[0x5];
	u8	   reserved_at_90[0xc];
	u8	   state[0x4];

	u8	   reserved_at_a0[0x8];
	u8	   qpn[0x18];

	u8	   dirty_bitmap_mkey[0x20];

	u8	   dirty_bitmap_size[0x20];

	u8	   dirty_bitmap_addr[0x40];

	u8	   hw_available_index[0x10];
	u8	   hw_used_index[0x10];

	u8	   reserved_at_160[0xa0];

	struct mlx5_ifc_virtio_q_bits virtqc;
};
/* NVMe emulation related structures */
struct mlx5_ifc_nvme_namespace_bits {
	u8	   modify_field_select[0x40];

	u8	   device_emulation_id[0x20];

	u8	   src_nsid[0x20];

	u8	   dst_nsid[0x20];

	u8	   reserved_at_a0[0x8];
	u8	   lba_size[0x8];
	u8	   metadata_size[0x10];

	u8	   nvme_namespace_counter_set_id[0x20];

	u8	   reserved_at_e0[0x720];
};

struct mlx5_ifc_nvme_cq_bits {
	u8	   modify_field_select[0x40];

	u8	   device_emulation_id[0x20];

	u8	   reserved_at_60[0x20];

	u8	   interrupt_disabled[0x1];
	u8	   reserved_at_80[0x3];
	u8	   offload_type[0x4];
	u8	   reserved_at_88[0x18];

	u8	   reserved_at_a0[0x18];
	u8	   msix_vector[0x8];

	u8	   nvme_base_addr[0x40];

	u8	   reserved_at_100[0x8];
	u8	   nvme_log_entry_size[0x8];
	u8	   nvme_num_of_entries[0x10];

	u8	   nvme_doorbell_offset[0x20];

	u8	   cq_period_mode[0x1];
	u8	   reserved_at_144[0x3];
	u8	   cq_period[0xc];
	u8	   cq_max_count[0x10];

	u8	   reserved_at_160[0x6a0];
};

struct mlx5_ifc_nvme_sq_be_bits {
	u8	modify_field_select[0x40];

	u8	reserved_at_40[0x40];

	u8	nvme_sq_id[0x20];

	u8	qpn[0x20];

	u8	reserved_at_18[0x240];
};

struct mlx5_ifc_nvme_sq_bits {
	u8	   modify_field_select[0x40];

	u8	   device_emulation_id[0x20];

	u8	   reserved_at_60[0x20];

	u8	   network_state[0x4];
	u8	   offload_type[0x4];
	u8	   qpn[0x18];

	u8	   reserved_at_100[0x10];
	u8	   fe_only[0x1];
	u8	   reserved_at_b0[0x2];
	u8	   log_nvme_page_size[0x5];
	u8	   msix_vector[0x8];

	u8	   nvme_base_addr[0x40];

	u8	   max_transaction_size[0x8];
	u8	   nvme_log_entry_size[0x8];
	u8	   nvme_num_of_entries[0x10];

	u8	   nvme_doorbell_offset[0x20];

	u8	   counter_set_id[0x20];

	u8	   reserved_at_160[0x40];

	u8	   nvme_cq_id[0x20];

	u8	   reserved_at_1c0[0x640];
};

/* Both HW set and HW add share the same HW format with different opcodes */
struct mlx5_ifc_dr_action_hw_set_bits {
	u8	 opcode[0x8];
	u8	 destination_field_code[0x8];
	u8	 reserved_at_10[0x2];
	u8	 destination_left_shifter[0x6];
	u8	 reserved_at_18[0x3];
	u8	 destination_length[0x5];
	u8	 inline_data[0x20];
};

struct mlx5_ifc_dr_action_hw_copy_bits {
	u8	 opcode[0x8];
	u8	 destination_field_code[0x8];
	u8	 reserved_at_10[0x2];
	u8	 destination_left_shifter[0x6];
	u8	 reserved_at_18[0x2];
	u8	 destination_length[0x6];
	u8	 reserved_at_20[0x8];
	u8	 source_field_code[0x8];
	u8	 reserved_at_30[0x2];
	u8	 source_left_shifter[0x6];
	u8	 reserved_at_38[0x8];
};

struct mlx5_ifc_nvme_sq_counters_bits {
	u8	modify_field_select[0x40];

	u8	reserved_at_40[0x40];

	u8	data_read[0x20];
	u8	data_write[0x20];
	u8	cmd_read[0x10];
	u8	cmd_write[0x10];
	u8	error_cqes[0x20];

	u8	integrity_errors[0x10];
	u8	fabric_errors[0x10];
	u8	busy_time[0x10];
	u8	power_cycle[0x10];
	u8	power_on_hours[0x10];
	u8	unsafe_shutdowns[0x10];
	u8	error_information_log_entries[0x10];
	u8	reserved_at_170[0x10];

	u8	nvme_pci_errors[0x20];

	u8	reserved_at_1a0[0x660];
};

struct mlx5_ifc_nvme_ctrl_counters_bits {
	u8	modify_field_select[0x40];

	u8	reserved_at_40[0x40];

	u8	data_read[0x20];
	u8	data_write[0x20];
	u8	cmd_read[0x10];
	u8	cmd_write[0x10];
	u8	error_cqes[0x20];

	u8	flrs[0x20];
	u8	bad_doorbells[0x20];
	u8	integrity_errors[0x10];
	u8	fabric_errors[0x10];
	u8	busy_time[0x10];
	u8	power_cycle[0x10];
	u8	power_on_hours[0x10];
	u8	unsafe_shutdowns[0x10];
	u8	error_information_log_entries[0x10];
	u8	reserved_at_1B0[0x10];

	u8	reserved_at_1c0[0x660];
};

struct mlx5_ifc_hotplug_device_input_bits {
	u8	opcode[0x10];
	u8	uid[0x10];

	u8	reserved_at_20[0x20];

	u8	reserved_at_40[0x40];

	struct	mlx5_ifc_device_bits	hotplug_device_object;
};

struct mlx5_ifc_hotplug_device_output_bits {
	u8	status[0x8];
	u8	reserved_at_8[0x18];

	u8	syndrome[0x20];

	u8	reserved_at_40[0x40];

	struct mlx5_ifc_device_bits	hotplug_device_object;
};

struct mlx5_ifc_hotunplug_device_input_bits {
	u8	opcode[0x10];
	u8	uid[0x10];

	u8	reserved_at_20[0x20];

	u8	reserved_at_40[0x40];

	struct mlx5_ifc_device_bits	hotplug_device_object;
};

struct mlx5_ifc_hotunplug_device_output_bits {
	u8	status[0x8];
	u8	reserved_at_8[0x18];

	u8	syndrome[0x20];

	u8	reserved_at_40[0x40];
};

struct mlx5_ifc_query_vuid_in_bits {
	u8	opcode[0x10];
	u8	uid[0x10];

	u8	reserved_at_20[0x40];

	u8	query_vfs_vuid[0x1];
	u8	reserved_at_40[0xf];
	u8	vhca_id[0x10];
};

struct mlx5_ifc_vuid_bits {
	u8	vuid[0x400];
};

struct mlx5_ifc_query_vuid_out_bits {
	u8	status[0x8];
	u8	reserved_at_8[0x18];

	u8	syndrome[0x20];

	u8	reserved_at_40[0x1a0];

	u8	reserved_at_41[0x10];
	u8	num_of_entries[0x10];

	struct mlx5_ifc_vuid_bits vuid[0];
};

struct mlx5_ifc_dek_bits {
	u8	modify_field_select[0x40];

	u8	state[0x8];
	u8	reserved_at_0x48[0xc];
	u8	key_size[0x4];
	u8	has_keytag[0x1];
	u8	reserved_at_0x59[0x3];
	u8	key_purpose[0x4];

	u8	reserved_at_0x60[0x8];
	u8	pd[0x18];

	u8	reserved_at_0x80[0x100];

	u8	opaque[0x40];

	u8	reserved_at_0x1c0[0x40];

	u8	key[0x400];

	u8	reserved_at_0x600[0x200];
};

struct mlx5_ifc_crypto_login_bits {
	u8	modify_field_select[0x40];

	u8	reserved_at_0x40[0x40];

	u8	reserved_at_0x80[0x4];
	u8	state[0x4];
	u8	credential_pointer[0x18];

	u8	reserved_at_0xa0[0x8];
	u8	session_import_kek_ptr[0x18];

	u8	reserved_at_0xc0[0x140];

	u8	credential[0x180];

	u8	reserved_at_0x380[0x480];
};

enum {
	MLX5_DR_ACTION_MDFY_HW_FLD_L2_0		= 0,
	MLX5_DR_ACTION_MDFY_HW_FLD_L2_1		= 1,
	MLX5_DR_ACTION_MDFY_HW_FLD_L2_2		= 2,
	MLX5_DR_ACTION_MDFY_HW_FLD_L3_0		= 3,
	MLX5_DR_ACTION_MDFY_HW_FLD_L3_1		= 4,
	MLX5_DR_ACTION_MDFY_HW_FLD_L3_2		= 5,
	MLX5_DR_ACTION_MDFY_HW_FLD_L3_3		= 6,
	MLX5_DR_ACTION_MDFY_HW_FLD_L3_4		= 7,
	MLX5_DR_ACTION_MDFY_HW_FLD_L4_0		= 8,
	MLX5_DR_ACTION_MDFY_HW_FLD_L4_1		= 9,
	MLX5_DR_ACTION_MDFY_HW_FLD_MPLS		= 10,
	MLX5_DR_ACTION_MDFY_HW_FLD_L2_TNL_0	= 11,
	MLX5_DR_ACTION_MDFY_HW_FLD_REG_0	= 12,
	MLX5_DR_ACTION_MDFY_HW_FLD_REG_1	= 13,
	MLX5_DR_ACTION_MDFY_HW_FLD_REG_2	= 14,
	MLX5_DR_ACTION_MDFY_HW_FLD_REG_3	= 15,
	MLX5_DR_ACTION_MDFY_HW_FLD_L4_2		= 16,
	MLX5_DR_ACTION_MDFY_HW_FLD_FLEX_0	= 17,
	MLX5_DR_ACTION_MDFY_HW_FLD_FLEX_1	= 18,
	MLX5_DR_ACTION_MDFY_HW_FLD_FLEX_2	= 19,
	MLX5_DR_ACTION_MDFY_HW_FLD_FLEX_3	= 20,
	MLX5_DR_ACTION_MDFY_HW_FLD_L2_TNL_1	= 21,
	MLX5_DR_ACTION_MDFY_HW_FLD_METADATA	= 22,
	MLX5_DR_ACTION_MDFY_HW_FLD_RESERVED	= 23,
};

enum {
	MLX5_DR_ACTION_MDFY_HW_OP_COPY		= 0x1,
	MLX5_DR_ACTION_MDFY_HW_OP_SET		= 0x2,
	MLX5_DR_ACTION_MDFY_HW_OP_ADD		= 0x3,
};

enum {
	MLX5_DR_ACTION_MDFY_HW_HDR_L3_NONE	= 0x0,
	MLX5_DR_ACTION_MDFY_HW_HDR_L3_IPV4	= 0x1,
	MLX5_DR_ACTION_MDFY_HW_HDR_L3_IPV6	= 0x2,
};

enum {
	MLX5_DR_ACTION_MDFY_HW_HDR_L4_NONE	= 0x0,
	MLX5_DR_ACTION_MDFY_HW_HDR_L4_TCP	= 0x1,
	MLX5_DR_ACTION_MDFY_HW_HDR_L4_UDP	= 0x2,
};

enum {
	MLX5_CQE_SIZE_64B = 0x0,
	MLX5_CQE_SIZE_128B = 0x1,
};

#define MLX5_ADAPTER_PAGE_SHIFT 12

enum {
	MLX5_APU_ELEMENT_TYPE_THREAD = 0x0,
	MLX5_APU_ELEMENT_TYPE_EQ = 0x1
};

struct mlx5_ifc_cqc_bits {
	u8 status[0x4];
	u8 as_notify[0x1];
	u8 initiator_src_dct[0x1];
	u8 dbr_umem_valid[0x1];
	u8 apu_cq[0x1];
	u8 cqe_sz[0x3];
	u8 cc[0x1];
	u8 reserved_at_c[0x1];
	u8 scqe_break_moderation_en[0x1];
	u8 oi[0x1];
	u8 cq_period_mode[0x2];
	u8 cqe_comp_en[0x1];
	u8 mini_cqe_res_format[0x2];
	u8 st[0x4];
	u8 reserved_at_18[0x1];
	u8 apu_element_type[0x3];
	u8 reserved_at_1c[0x2];
	u8 cqe_compression_layout[0x2];

	u8 dbr_umem_id[0x20];

	u8 reserved_at_40[0x14];
	u8 page_offset[0x6];
	u8 reserved_at_5a[0x2];
	u8 mini_cqe_res_format_3_2[0x2];
	u8 cq_time_stamp_format[0x2];

	u8 reserved_at_60[0x3];
	u8 log_cq_size[0x5];
	u8 uar_page[0x18];

	u8 reserved_at_80[0x4];
	u8 cq_period[0xc];
	u8 cq_max_count[0x10];

	u8 c_eqn_or_apu_element[0x20];

	u8 reserved_at_c0[0x3];
	u8 log_page_size[0x5];
	u8 reserved_at_c8[0x18];

	u8 reserved_at_e0[0x20];

	u8 reserved_at_100[0x8];
	u8 last_notified_index[0x18];

	u8 reserved_at_120[0x8];
	u8 last_solicit_index[0x18];

	u8 reserved_at_140[0x8];
	u8 consumer_counter[0x18];

	u8 reserved_at_160[0x8];
	u8 producer_counter[0x18];

	u8 apu_user_arg[0x40];

	u8 dbr_addr[0x40];
};

struct mlx5_ifc_create_cq_out_bits {
	u8 status[0x8];
	u8 reserved_at_8[0x18];
	u8 syndrome[0x20];
	u8 reserved_at_40[0x8];
	u8 cqn[0x18];
	u8 reserved_at_60[0x20];
};

struct mlx5_ifc_create_cq_in_bits {
	u8 opcode[0x10];
	u8 uid[0x10];

	u8 reserved_at_20[0x10];
	u8 op_mod[0x10];

	u8 reserved_at_40[0x40];

	struct mlx5_ifc_cqc_bits cq_context;

	u8 cq_umem_offset[0x40];

	u8 cq_umem_id[0x20];

	u8 cq_umem_valid[0x1];
	u8 reserved_at_2e1[0x1f];

	u8 reserved_at_300[0x580];
	u8 pas[0x0];
};

#endif /* MLX5_IFC_H */
