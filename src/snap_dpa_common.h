/*
 * Copyright (c) 2021 NVIDIA CORPORATION & AFFILIATES.  All rights reserved.
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

#ifndef _SNAP_DPA_COMMON_H
#define _SNAP_DPA_COMMON_H

/*
 * This file contains definitions and inline functions that are common
 * between DPA and DPU
 */

#define SNAP_DPA_THREAD_MBOX_LEN   4096
#define SNAP_DPA_THREAD_MBOX_ALIGN 4096
#define SNAP_DPA_THREAD_MBOX_CMD_OFFSET 0
#define SNAP_DPA_THREAD_MBOX_RSP_OFFSET 2048

#define SNAP_DPA_THREAD_ENTRY_POINT "__snap_dpa_thread_start"

/**
 * struct snap_dpa_tcb - DPA thread control block
 *
 * The thread control block is used to pass parameters to
 * the DPA thread.
 *
 * @mbox_addres: command mailbox address in DPU memory
 * @mbox_lkey:   mailbox window memory key
 *
 */
struct snap_dpa_tcb {
	uint64_t mbox_address;
	uint32_t mbox_lkey;
};

enum {
	SNAP_DPA_CMD_START = 0x1,
	SNAP_DPA_CMD_STOP,
	SNAP_DPA_CMD_APP_FIRST = 0xf0000000,
};

/*
 * The functions and structs below define a shared mailbox that can be used
 * to send commands from DPU to DPA thread. DPA uses thread window and mailbox
 * lkey to access commands.
 *
 * Theory of operation:
 * - commands are completely sync
 * - only one outstanding command is possible
 * - DPU sends new command by filling data and changing command serial number
 * - DPA thread should periodically poll mailbox for new command (sn change)
 * - DPA thread must send responce by filling status and setting responce
 *   serial number to command serial number
 * - DPU should poll mailbox for the command responce (sn change)
 */
struct snap_dpa_cmd {
	volatile uint32_t sn;
	volatile uint32_t cmd;
};

enum {
	SNAP_DPA_RSP_OK = 0,
	SNAP_DPA_RSP_ERR = 1
};

struct snap_dpa_rsp {
	volatile uint32_t sn;
	volatile uint32_t status;
};

static inline struct snap_dpa_cmd *snap_dpa_mbox_to_cmd(void *mbox)
{
	/* casting is needed for the c++ code */
	return (struct snap_dpa_cmd *)((char *)mbox + SNAP_DPA_THREAD_MBOX_CMD_OFFSET);
}

static inline void snap_dpa_cmd_send(struct snap_dpa_cmd *cmd, uint32_t type)
{
	cmd->cmd = type;
	/* TODO: barriers */
	cmd->sn++;
}

static inline struct snap_dpa_rsp *snap_dpa_mbox_to_rsp(void *mbox)
{
	/* casting is needed for the c++ code */
	return (struct snap_dpa_rsp *)((char *)mbox + SNAP_DPA_THREAD_MBOX_RSP_OFFSET);
}

static inline void snap_dpa_rsp_send(void *mbox, int type)
{
	struct snap_dpa_rsp *rsp;
	struct snap_dpa_cmd *cmd;

	cmd = snap_dpa_mbox_to_cmd(mbox);
	rsp = snap_dpa_mbox_to_rsp(mbox);

	rsp->status = type;
	/* TODO: barriers */
	rsp->sn = cmd->sn;
}

static inline struct snap_dpa_rsp *snap_dpa_rsp_wait(void *mbox)
{
	struct snap_dpa_rsp *rsp;
	struct snap_dpa_cmd *cmd;

	cmd = snap_dpa_mbox_to_cmd(mbox);
	/* wait for report back from the thread. TODO: timeout */
	do {
		rsp = snap_dpa_mbox_to_rsp(mbox);
	} while (rsp->sn != cmd->sn);

	return rsp;
}

static inline struct snap_dpa_cmd *snap_dpa_cmd_recv(void *mbox, uint32_t type)
{
	struct snap_dpa_cmd *cmd;

	/* wait for report back from the thread. TODO: timeout */
	do {
		cmd = snap_dpa_mbox_to_cmd(mbox);
	} while (cmd->cmd != type);

	return cmd;
}

#endif
