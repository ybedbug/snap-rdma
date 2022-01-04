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

#ifndef _SNAP_DPA_COMMON_H
#define _SNAP_DPA_COMMON_H

/*
 * This file contains definitions and inline functions that are common
 * between DPA and DPU
 */

/* TODO: should be configurable */
#define SIMX_BUILD 1

/* max length of *printf/log buffer on DPA */
#define SNAP_DPA_PRINT_BUF_LEN 160 /* should be careful because this is allocated on stack */

#define SNAP_DPA_THREAD_MBOX_LEN   4096
#define SNAP_DPA_THREAD_MBOX_ALIGN 4096
#define SNAP_DPA_THREAD_MBOX_CMD_OFFSET 0
#define SNAP_DPA_THREAD_MBOX_RSP_OFFSET 2048
#define SNAP_DMA_THREAD_MBOX_CMD_SIZE (SNAP_DPA_THREAD_MBOX_RSP_OFFSET - sizeof(struct snap_dpa_cmd))
#define SNAP_DPA_THREAD_ENTRY_POINT "__snap_dpa_thread_start"

/* TODO: make configurable. Some threads will not need this memory */
#define SNAP_DPA_THREAD_HEAP_SIZE  2*16384

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
	uint64_t data_address;
	uint64_t data_used;
};

struct snap_dpa_attr {
	void *mbox_address;
	uint32_t mbox_lkey;
};

enum {
	SNAP_DPA_CMD_START = 0x1,
	SNAP_DPA_CMD_STOP,
	SNAP_DPA_CMD_MR = 0x10,
	SNAP_DPA_CMD_DMA_EP_COPY = 0x100,
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

struct snap_dpa_cmd_mr {
	struct snap_dpa_cmd base;
	uint64_t va;
	uint64_t len;
	uint32_t mkey;
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
