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

#ifndef SNAP_NVME_REGISTER_H
#define SNAP_NVME_REGISTER_H

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <linux/types.h>

/* NVME registers */
#define NVME_REG_CAP	0x00     /* capabilities */
#define NVME_REG_VS	0x08     /* version */
#define NVME_REG_INTMS	0x0C     /* interrupt mask set */
#define NVME_REG_INTMC	0x10     /* interrupt mask clear */
#define NVME_REG_CC	0x14     /* Controller config */
#define NVME_REG_CSTS	0x1C     /* Controller status */
#define NVME_REG_NSSR	0x20     /* NVM subsystem reset */
#define NVME_REG_AQA	0x24     /* Admin Queue Attrs */
#define NVME_REG_ASQ	0x28     /* Admin Submission Queue Base Addr */
#define NVME_REG_ACQ	0x30     /* Admin Completion Queue Base Addr */
/* Optional registers */
#define NVME_REG_CMBLOC	0x38     /* Controller memory buffer location */
#define NVME_REG_CMBSZ	0x3C     /* Controller memory buffer size */
#define NVME_REG_BPINFO	0x40     /* Boot partition info */
#define NVME_REG_BPRSEL	0x44     /* Boot partition read select */
#define NVME_REG_BPMBL	0x48     /* Boot prtition memory buffer */
#define NVME_REG_LAST	(-1)

#define NVME_DB_BASE	0x1000   /* offset of SQ/CQ doorbells */

/* register indexes */
#define NVME_REG_CAP_IDX	0
#define NVME_REG_VS_IDX		1
#define NVME_REG_INTMS_IDX	2
#define NVME_REG_INTMC_IDX	3
#define NVME_REG_CC_IDX		4
#define NVME_REG_CSTS_IDX	5
#define NVME_REG_NSSR_IDX	6
#define NVME_REG_AQA_IDX	7
#define NVME_REG_ASQ_IDX	8
#define NVME_REG_ACQ_IDX	9
/* Optional registers */
#define NVME_REG_CMBLOC_IDX	10
#define NVME_REG_CMBSZ_IDX	11
#define NVME_REG_BPINFO_IDX	12
#define NVME_REG_BPRSEL_IDX	13
#define NVME_REG_BPMBL_IDX	14

#define NVME_REG_MAX_DUMP_FUNC_LEN   256

enum nvme_register_type {
	NVME_REG_RO = 1 << 0,    /* read only */
	NVME_REG_RW = 1 << 1,    /* read/write */
	NVME_REG_RW1S = 1 << 2,    /* read/write 1 to set */
	NVME_REG_RW1C = 1 << 3,     /* read/write 1 to clear */
};

enum nvme_csts_shift {
	CSTS_RDY_SHIFT		= 0,
	CSTS_CFS_SHIFT		= 1,
	CSTS_SHST_SHIFT		= 2,
	CSTS_NSSRO_SHIFT	= 4,
	CSTS_PP_SHIFT		= 5,
};

enum nvme_csts_mask {
	CSTS_RDY_MASK	= 0x1,
	CSTS_CFS_MASK	= 0x1,
	CSTS_SHST_MASK	= 0x3,
	CSTS_NSSRO_MASK	= 0x1,
	CSTS_PP_MASK	= 0x1,
};

enum nvme_csts {
	NVME_CSTS_READY		= 1 << CSTS_RDY_SHIFT,
	NVME_CSTS_FAILED	= 1 << CSTS_CFS_SHIFT,
	NVME_CSTS_SHST_NORMAL	= 0 << CSTS_SHST_SHIFT,
	NVME_CSTS_SHST_PROGRESS	= 1 << CSTS_SHST_SHIFT,
	NVME_CSTS_SHST_COMPLETE	= 2 << CSTS_SHST_SHIFT,
	NVME_CSTS_NSSRO		= 1 << CSTS_NSSRO_SHIFT,
};

union nvme_cc_register {
	uint32_t	raw;
	struct {
		/** enable */
		uint32_t en        : 1;
		uint32_t reserved1 : 3;
		/** i/o command set selected */
		uint32_t css       : 3;
		/** memory page size */
		uint32_t mps       : 4;
		/** arbitration mechanism selected */
		uint32_t ams       : 3;
		/** shutdown notification */
		uint32_t shn       : 2;
		/** i/o submission queue entry size */
		uint32_t iosqes    : 4;
		/** i/o completion queue entry size */
		uint32_t iocqes    : 4;
		uint32_t reserved2 : 8;
	} bits;
};

struct nvme_bar {
	uint64_t	cap;
	uint32_t	vs;
	uint32_t	intms;
	uint32_t	intmc;
	uint32_t	cc;
	uint32_t	rsvd1;
	uint32_t	csts;
	uint32_t	nssrc;
	uint32_t	aqa;
	uint64_t	asq;
	uint64_t	acq;
	uint32_t	cmbloc;
	uint32_t	cmbsz;
	uint32_t	bpinfo;
	uint32_t	bprsel;
	uint64_t	bpmbl;
};

/**
 * typedef nvme_register_change_cb_t - NVMe register change callback
 * @ctx:	Context to identify caller.
 * @reg:	Changed register (offset from BAR0 start).
 * @reg_desc:	Register description.
 * @new_val:	New value for register.
 * @prev_val:	Previous value on the register.
 *
 * The callback is called when change in register is recognized by NVMe
 * register layer. It passes the context of the caller and a verbose
 * description of the register alongside with the new/old values of it.
 */
typedef void (*nvme_register_change_cb_t)(void *ctx, unsigned int reg,
		char *reg_desc, uint64_t new_val, uint64_t prev_val);
void nvme_register_identify_change(struct nvme_bar *prev,
		struct nvme_bar *curr, nvme_register_change_cb_t change_cb,
		void *ctx);

void nvme_bar_dump(struct nvme_bar *bar);
int nvme_initial_register_check(struct nvme_bar *bar);

#endif
