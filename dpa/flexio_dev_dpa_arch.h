/*
 * SPDX-FileCopyrightText: Copyright (c) 2022 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __FLEXIO_DEV_DPA_ARCH_H__
#define __FLEXIO_DEV_DPA_ARCH_H__

#include <stdint.h>
#include <libflexio-os/flexio_os.h>

typedef uint64_t dpa_outbox_value_t;
typedef uint64_t dpa_window_value_t;

#if defined( E_MODE_LE )
struct dpa_outbox9_u_page_le { /* Little Endian */
	uint64_t	UCTL;			/* TBD */
	unsigned char	reserved_at_40[56];
	uint64_t	BUFFER;			/* TBD */
	unsigned char	reserved_at_240[184];
	uint64_t	CQ_DB;			/* TBD */
	unsigned char	reserved_at_840[56];
	uint64_t	EQ_DB_REARM;		/* TBD */
	unsigned char	reserved_at_a40[56];
	uint64_t	EQ_DB_NO_REARM;		/* TBD */
	unsigned char	reserved_at_c40[56];
	uint64_t	SXD_DB;			/* TBD */
	unsigned char	reserved_at_e40[56];
	uint64_t	EMU_CAP;		/* TBD */
	unsigned char	reserved_at_1040[56];
	uint64_t	EMU_CAP_TRIGGER;	/* TBD */
	unsigned char	reserved_at_1240[56];
	uint64_t	RXT_DB;			/* TBD */
	unsigned char	reserved_at_1440[56];
	uint64_t	RXT_DONE0;		/* TBD */
	uint64_t	RXT_DONE1;		/* TBD */
	unsigned char	reserved_at_1680[48];
	uint64_t	RXC_RD;			/* TBD */
	unsigned char	reserved_at_1840[56];
	uint64_t	CC_DB;			/* TBD */
	unsigned char	reserved_at_1a40[3192];
	uint64_t	SW_RESERVE;		/* TBD */
	unsigned char	reserved_at_7e40[56];
};
typedef struct dpa_outbox9_u_page_le dpa_outbox_u_page_t;
#else
#error incorrect BYTE_ORDER
#endif

#define _OUTBOX_NULL_PTR(type)		((struct dpa_outbox_v9_reg_##type##_rbits *)0)
#define _OUTBOX_BIT_SIZE(type, field)	(sizeof(_OUTBOX_NULL_PTR(type)->field))
#define _OUTBOX_OFFSET(type, field)	((uint64_t)&((_OUTBOX_NULL_PTR(type)->field)))

#define _OUTBOX_P_BGN(type, field) 	(_OUTBOX_OFFSET(type, field))

#define OUTBOX_VALUE1(__v1, __s1) (((dpa_outbox_value_t)__v1) << (__s1))
#define OUTBOX_VALUE2(__v1, __s1, __v2, __s2) ( OUTBOX_VALUE1(__v1, __s1) | OUTBOX_VALUE1(__v2, __s2) )

#define OUTBOX_PAGE_FIELD_PTR(__outbox_page_ptr, __name) ((dpa_outbox_value_t*)(&(((dpa_outbox_u_page_t *)__outbox_page_ptr)->__name)))
#define outbox_write(__outbox_page_ptr, __name, __value) __outbox_write((OUTBOX_PAGE_FIELD_PTR(__outbox_page_ptr, __name)), ((dpa_outbox_value_t)(__value)))
static inline void __outbox_write(dpa_outbox_value_t* ptr, dpa_outbox_value_t value)
{
	*ptr = value;
}

#define CQ_DB_CQN_SHIFT			_OUTBOX_P_BGN(CQ_DB, cqn)
#define CQ_DB_CQ_CI_SHIFT		_OUTBOX_P_BGN(CQ_DB, cq_ci)
#define OUTBOX_V_CQ_DB(__cqn, __cq_ci) (OUTBOX_VALUE2(__cqn, CQ_DB_CQN_SHIFT, __cq_ci, CQ_DB_CQ_CI_SHIFT))

#define RXT_DB_CQN_SHIFT		_OUTBOX_P_BGN(RXT_DB, cqn)
#define OUTBOX_V_RXT_DB(__cqn)		(OUTBOX_VALUE1(__cqn, RXT_DB_CQN_SHIFT))

#define EQ_DB_REARM_EQN_SHIFT		_OUTBOX_P_BGN(EQ_DB_REARM, eqn)
#define EQ_DB_REARM_EQ_CI_SHIFT		_OUTBOX_P_BGN(EQ_DB_REARM, eq_ci)
#define OUTBOX_V_EQ_DB_REARM(__eqn, __eq_ci) (OUTBOX_VALUE2(__eqn, EQ_DB_REARM_EQN_SHIFT, __eq_ci, EQ_DB_REARM_EQ_CI_SHIFT))

#define EQ_DB_NO_REARM_EQN_SHIFT	_OUTBOX_P_BGN(EQ_DB_NO_REARM, eqn)
#define EQ_DB_NO_REARM_EQ_CI_SHIFT	_OUTBOX_P_BGN(EQ_DB_NO_REARM, eq_ci)
#define OUTBOX_V_EQ_DB_NO_REARM(__eqn, __eq_ci) (OUTBOX_VALUE2(__eqn, EQ_DB_NO_REARM_EQN_SHIFT, __eq_ci, EQ_DB_NO_REARM_EQ_CI_SHIFT))

#define SXD_DB_WQE_INDEX_SHIFT		_OUTBOX_P_BGN(SXD_DB, wqe_index)
#define SXD_DB_QP_OR_SQ_SHIFT		_OUTBOX_P_BGN(SXD_DB, qp_or_sq)
#define OUTBOX_V_SXD_DB(__wqe_index, __qp_or_sq) (OUTBOX_VALUE2(__wqe_index, SXD_DB_WQE_INDEX_SHIFT, __qp_or_sq, SXD_DB_QP_OR_SQ_SHIFT))

/* From lines 78-84 of apu_kernel/src/kernel/include/outbox/outbox_m.h  */
#define EMU_CAP_TRIGGER_DUMMY_QPN_SHIFT       (_OUTBOX_P_BGN(EMU_CAP_TRIGGER, dummy_qpn))
#define EMU_CAP_TRIGGER_EMULATION_INDEX_SHIFT (_OUTBOX_P_BGN(EMU_CAP_TRIGGER, emulation_index))
#define OUTBOX_V_EMU_CAP_TRIGGER(_dummy_qpn, _emulation_index) (OUTBOX_VALUE2(_dummy_qpn, EMU_CAP_TRIGGER_DUMMY_QPN_SHIFT, _emulation_index, EMU_CAP_TRIGGER_EMULATION_INDEX_SHIFT))

#define EMU_CAP_DUMMY_QPN_SHIFT               (_OUTBOX_P_BGN(EMU_CAP, dummy_qpn))
#define EMU_CAP_EMULATION_INDEX_SHIFT         (_OUTBOX_P_BGN(EMU_CAP, emulation_index))
#define OUTBOX_V_EMU_CAP(_dummy_qpn, _emulation_index) (OUTBOX_VALUE2(_dummy_qpn, EMU_CAP_TRIGGER_DUMMY_QPN_SHIFT, _emulation_index, EMU_CAP_TRIGGER_EMULATION_INDEX_SHIFT))

struct dpa_outbox_v9_reg_CQ_DB_rbits {		/* Little Endian */
	unsigned char cqn[0x00018];		/* Re-arm CQ, update CQ ci */
	unsigned char reserved_at_18[0x00008];

	unsigned char cq_ci[0x00018];
	unsigned char reserved_at_38[0x00008];
};
struct dpa_outbox_v9_reg_EQ_DB_NO_REARM_rbits {	/* Little Endian */
	unsigned char eqn[0x00018];		/* update EQ ci */
	unsigned char reserved_at_18[0x00008];

	unsigned char eq_ci[0x0001a];
	unsigned char reserved_at_3a[0x00006];
};
struct dpa_outbox_v9_reg_SXD_DB_rbits {		/* Little Endian */
	unsigned char wqe_index[0x00010];	/* Send Packet, SQ doorbell */
	unsigned char reserved_at_10[0x00010];

	unsigned char qp_or_sq[0x00018];
	unsigned char reserved_at_38[0x00008];
};

struct dpa_outbox_v9_reg_RXT_DB_rbits {		/* Little Endian */
	/* Trigger CQE with zeros in order to trigger EQE MSIx to Host */
	unsigned char cqn[0x00018];
	unsigned char reserved_at_18[0x00008];

	unsigned char reserved_at_20[0x00020];
};

/* from ./build/fw_debug/src/kernel/include/adabe/apu_outbox_regs_adb_rbits.h */
typedef unsigned char pseudo_bit_t;
struct dpa_outbox_v9_reg_EMU_CAP_rbits {    /* Little Endian */
    pseudo_bit_t            dummy_qpn[0x00018];         /* Re-arm for the Emulation event */
    pseudo_bit_t            reserved_at_18[0x00008];
    /*----------------------------------------------------------*/
    pseudo_bit_t            emulation_index[0x0001a];
    pseudo_bit_t            reserved_at_3a[0x00006];
/* --------------------------------------------------------- */
};

struct dpa_outbox_v9_reg_EMU_CAP_TRIGGER_rbits {    /* Little Endian */
    pseudo_bit_t            dummy_qpn[0x00018];         /* Re-arm for the SXW event */
    pseudo_bit_t            reserved_at_18[0x00008];
    /*----------------------------------------------------------*/
    pseudo_bit_t            emulation_index[0x0001a];
    pseudo_bit_t            reserved_at_3a[0x00006];
/* --------------------------------------------------------- */
};




#endif
