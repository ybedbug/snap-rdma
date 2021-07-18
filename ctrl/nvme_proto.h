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

#ifndef SNAP_NVME_PROTO_H
#define SNAP_NVME_PROTO_H


struct nvme_lbaf {
	uint16_t	ms;
	uint8_t		ds;
	uint8_t		rp;
} __attribute__((packed));

struct nvme_id_ns {
	uint64_t		nsze;
	uint64_t		ncap;
	uint64_t		nuse;
	uint8_t			nsfeat;
	uint8_t			nlbaf;
	uint8_t			flbas;
	uint8_t			mc;
	uint8_t			dpc;
	uint8_t			dps;
	uint8_t			res30[74];
	uint8_t			nguid[16];
	uint8_t			eui64[8];
	struct nvme_lbaf	lbaf[16];
	uint8_t			res192[192];
	uint8_t			vs[3712];
} __attribute__((packed));

struct nvme_sgl_desc {
	uint64_t		address;
	uint32_t		length;
	uint8_t			resv[3];
	uint8_t			subtype : 4;
	uint8_t			type : 4;
} __attribute__((packed));

struct nvme_keyed_sgl_desc {
	uint64_t	address;
	uint64_t	length : 24;
	uint64_t	key : 32;
	uint64_t	subtype : 4;
	uint64_t	type : 4;
} __attribute__((packed));

struct nvme_prp_desc {
	uint64_t	prp1;
	uint64_t	prp2;
} __attribute__((packed));

union nvme_data_ptr {
	struct nvme_prp_desc		prp;
	struct nvme_sgl_desc		sgl;
	struct nvme_keyed_sgl_desc	ksgl;
};

struct nvme_psd {
	uint16_t	mp;
	uint16_t	reserved;
	uint32_t	enlat;
	uint32_t	exlat;
	uint8_t		rrt;
	uint8_t		rrl;
	uint8_t		rwt;
	uint8_t		rwl;
	uint8_t		resv[16];
} __attribute__((packed));

struct nvme_ctrl_id {
	uint16_t		vid;
	uint16_t		ssvid;
	uint8_t			sn[20];
	uint8_t			mn[40];
	uint8_t			fr[8];
	uint8_t			rab;
	uint8_t			ieee[3];
	uint8_t			cmic;
	uint8_t			mdts;
	uint16_t		cntlid;
	uint32_t		ver;
	uint32_t		rtd3r;
	uint32_t		rtd3e;
	uint32_t		oaes;
	uint8_t			rsvd96[160];
	uint16_t		oacs;
	uint8_t			acl;
	uint8_t			aerl;
	uint8_t			frmw;
	uint8_t			lpa;
	uint8_t			elpe;
	uint8_t			npss;
	uint8_t			avscc;
	uint8_t			apsta;
	uint16_t		wctemp;
	uint16_t		cctemp;
	uint16_t		mtfa;
	uint32_t		hmpre;
	uint32_t		hmmin;
	uint64_t		tnvmcap[2];
	uint64_t		unvmcap[2];
	uint32_t		rpmbs;
	uint16_t		edstt;
	uint8_t			dsto;
	uint8_t			fwug;
	uint16_t		kas;
	uint8_t			rsvd511[190];
	uint8_t			sqes;
	uint8_t			cqes;
	uint16_t		rsvd515;
	uint32_t		nn;
	uint16_t		oncs;
	uint16_t		fuses;
	uint8_t			fna;
	uint8_t			vwc;
	uint16_t		awun;
	uint16_t		awupf;
	uint8_t			nvscc;
	uint8_t			rsvd704;
	uint16_t		acwu;
	uint8_t			rsvd706[2];
	uint32_t		sgls;
	uint8_t			rsvd712[228];
	uint8_t			subnqn[256];
	uint8_t			rsvd2047[1024];
	struct nvme_psd		psd[32];
	uint8_t			vs[1024];
} __attribute__((packed));

struct nvme_cqe {
	uint32_t	result;
	uint32_t	rsvd;
	uint16_t	sq_head;
	uint16_t	sq_id;
	uint16_t	cid;
	uint16_t	status;
} __attribute__((packed));

struct nvme_sqe {
	uint8_t			opcode;
	uint8_t			fuse : 2; /* fused operation */
	uint8_t			rsvd1 : 4;
	uint8_t			psdt : 2;
	uint16_t		cid;
	uint32_t		nsid;
	uint64_t		res1;
	uint64_t		mptr;
	union nvme_data_ptr	ptr;
	uint32_t		cdw10;
	uint32_t		cdw11;
	uint32_t		cdw12;
	uint32_t		cdw13;
	uint32_t		cdw14;
	uint32_t		cdw15;
} __attribute__((packed));

struct nvme_abort {
	uint8_t		opcode;
	uint8_t		flags;
	uint16_t	cid;
	uint8_t		rsvd4[36];
	uint16_t	abort_sqid;
	uint16_t	abort_cid;
	uint32_t	rsvd40[5];
} __attribute__((packed));

struct nvme_get_feature {
	uint8_t		opcode;
	uint8_t		flags;
	uint16_t	cid;
	uint32_t	rsvd24[5];
	uint64_t	prp1;
	uint64_t	rsvd40;
	uint8_t		ftr_id;
	uint8_t		ftr_val;
	uint8_t		rsvd44[2];
	uint32_t	dw11;
	uint32_t	dw12;
	uint32_t	dw13;
	uint32_t	dw14;
	uint32_t	dw15;
} __attribute__((packed));

struct nvme_set_feature {
	uint8_t		opcode;
	uint8_t		flags;
	uint16_t	cid;
	uint32_t	rsvd24[5];
	uint64_t	prp1;
	uint64_t	rsvd40;
	uint8_t		ftr_id;
	uint16_t	rsvd43;
	uint8_t		save;
	uint32_t	dw11;
	uint32_t	dw12;
	uint32_t	dw13;
	uint32_t	dw14;
	uint32_t	dw15;
} __attribute__((packed));

struct nvme_fw_slot_info_log {
	uint8_t		afi;
	uint8_t		reserved1[7];
	uint8_t		frs1[8];
	uint8_t		frs2[8];
	uint8_t		frs3[8];
	uint8_t		frs4[8];
	uint8_t		frs5[8];
	uint8_t		frs6[8];
	uint8_t		frs7[8];
	uint8_t		reserved2[448];
} __attribute__((packed));

struct nvme_error_log {
	uint64_t	error_count;
	uint16_t	sqid;
	uint16_t	cid;
	uint16_t	status_field;
	uint16_t	param_error_location;
	uint64_t	lba;
	uint32_t	nsid;
	uint8_t		vs;
	uint8_t		resv[35];
} __attribute__((packed));

struct nvme_smart_log {
	uint8_t		critical_warning;
	uint16_t	temperature;
	uint8_t		available_spare;
	uint8_t		available_spare_threshold;
	uint8_t		percentage_used;
	uint8_t		reserved1[26];
	uint64_t	data_units_read[2];
	uint64_t	data_units_written[2];
	uint64_t	host_read_commands[2];
	uint64_t	host_write_commands[2];
	uint64_t	controller_busy_time[2];
	uint64_t	power_cycles[2];
	uint64_t	power_on_hours[2];
	uint64_t	unsafe_shutdowns[2];
	uint64_t	media_errors[2];
	uint64_t	number_of_error_log_entries[2];
	uint8_t		reserved2[320];
} __attribute__((packed));

struct nvme_changed_nslist_log {
	uint32_t	ns_list[1024];
} __attribute__((packed));

struct nvme_identify {
	uint8_t		opcode;
	uint8_t		flags;
	uint16_t	cid;
	uint32_t	nsid;
	uint64_t	rsvd2[2];
	uint64_t	prp1;
	uint64_t	prp2;
	uint8_t		cns;
	uint8_t		rsvd3;
	uint16_t	ctrlid;
	uint32_t	rsvd11[5];
} __attribute__((packed));

struct nvme_eui64 {
	uint8_t		raw[8];
} __attribute__((packed));

struct nvme_nguid {
	uint8_t		raw[16];
} __attribute__((packed));

struct nvme_uuid {
	uint32_t      time_low;
	uint16_t      time_mid;
	uint16_t      time_hi_and_version;
	uint16_t      clk_seq; /* hi_res + low */
	struct {
		uint32_t  byte5;
		uint16_t  byte1;
	} node;
} __attribute__((packed));

union nvme_id_ns_global {
	struct nvme_eui64	eui64;
	struct nvme_nguid	nguid;
	struct nvme_uuid	uuid;
} __attribute__((packed));

struct nvme_id_ns_descriptor {
	uint8_t				nidt;
	uint8_t				nidl;
	uint8_t				rsvd[2];
	union nvme_id_ns_global		nid;
} __attribute__((packed));

struct nvme_create_cq {
	uint8_t		opcode;
	uint8_t		flags;
	uint16_t	cid;
	uint32_t	rsvd1[5];
	uint64_t	prp1;
	uint64_t	rsvd8;
	uint16_t	cqid;
	uint16_t	qsize;
	uint16_t	cq_flags;
	uint16_t	irq_vector;
	uint32_t	rsvd12[4];
} __attribute__((packed));

struct nvme_create_sq {
	uint8_t		opcode;
	uint8_t		flags;
	uint16_t	cid;
	uint32_t	rsvd1[5];
	uint64_t	prp1;
	uint64_t	rsvd8;
	uint16_t	sqid;
	uint16_t	qsize;
	uint16_t	sq_flags;
	uint16_t	cqid;
	uint32_t	rsvd12[4];
} __attribute__((packed));

struct nvme_get_page_log {
	uint8_t		opcode;
	uint8_t		flags;
	uint16_t	cid;
	uint32_t	nsid;
	uint32_t	rsvd24[4];
	uint64_t	prp1;
	uint64_t	rsvd40;
	uint8_t		page_id;
	uint8_t		lsp;
	uint16_t	num_dwords;
	uint32_t	rsvd60[4];
} __attribute__((packed));

struct nvme_delete_q {
	uint8_t		opcode;
	uint8_t		flags;
	uint16_t	cid;
	uint32_t	rsvd1[9];
	uint16_t	qid;
	uint16_t	rsvd10;
	uint32_t	rsvd11[5];
} __attribute__((packed));

struct nvme_rw {
	uint8_t			opcode;
	uint8_t			flags;
	uint16_t		cid;
	uint32_t		nsid;
	uint64_t		rsvd2;
	uint64_t		mptr;
	union nvme_data_ptr	dptr;
	uint64_t		slba;
	uint16_t		nlb;
	uint16_t		control;
	uint32_t		dsmgmt;
	uint32_t		reftag;
	uint16_t		apptag;
	uint16_t		appmask;
} __attribute__((packed));

struct nvme_health_info_page {
	uint8_t		critical_warning;
	uint16_t	temperature;
	uint8_t		available_spare;
	uint8_t		available_spare_threshold;
	uint8_t		percentage_used;
	uint8_t		reserved[26];
	uint32_t	data_units_read[4];
	uint32_t	data_units_written[4];
	uint32_t	host_read_commands[4];
	uint32_t	host_write_commands[4];
	uint32_t	controller_busy_time[2];
	uint32_t	power_cycles[4];
	uint32_t	power_on_hours[4];
	uint32_t	unsafe_shutdowns[4];
	uint32_t	media_errors[4];
	uint32_t	num_error_info_log_entries[4];
	uint32_t	warning_temp_time;
	uint32_t	critical_temp_time;
	uint32_t	temp_sensor[4];
	uint8_t		reserved2[296];
} __attribute__((packed));

#endif
