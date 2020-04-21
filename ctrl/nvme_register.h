/*
 * Copyright (c) 2020 Mellanox Technologies, Inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
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

#define NVME_BIT(n)	(1u<<(n))

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

int nvme_initial_register_check(struct nvme_bar *bar);

#endif
