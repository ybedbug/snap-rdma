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

#include <string.h>

#include "snap_mb.h"
#include "snap_qp.h"

/*
 * This file contains definitions and inline functions that are common
 * between DPA and DPU
 */

/* TODO: should be configurable, enable to run on SimX */
#define SIMX_BUILD 0

/* max length of *printf/log buffer on DPA */
#define SNAP_DPA_PRINT_BUF_LEN 160 /* should be careful because this is allocated on stack */

#define SNAP_DPA_THREAD_MBOX_LEN   4096
#define SNAP_DPA_THREAD_MBOX_ALIGN 4096
#define SNAP_DPA_THREAD_MBOX_CMD_OFFSET 0
#define SNAP_DPA_THREAD_MBOX_RSP_OFFSET 2048
#define SNAP_DMA_THREAD_MBOX_CMD_SIZE (SNAP_DPA_THREAD_MBOX_RSP_OFFSET - sizeof(struct snap_dpa_cmd))
#define SNAP_DPA_THREAD_MBOX_TIMEOUT_MSEC (10*1000)
#define SNAP_DPA_THREAD_MBOX_POLL_INTERVAL_MSEC 1

#define SNAP_DPA_THREAD_ENTRY_POINT "__snap_dpa_thread_start"

/* TODO: make configurable. Some threads will not need this memory */
#define SNAP_DPA_THREAD_MIN_HEAP_SIZE  2*16384

/* TODO: make configurable */
#define SNAP_DPA_THREAD_N_LOG_ENTRIES 128
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
	uint32_t cmd_last_sn;

	uint64_t data_address;
	uint64_t data_used;
	uint64_t heap_size;

	uint32_t counter;
	uint8_t init_done;
	uint8_t user_flag;
	uint8_t pad1[2];

	uint64_t pad[1];
	uint64_t user_arg;

	struct snap_hw_cq cmd_cq;
	uint32_t active_lkey;
	uint8_t pad2[8];
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
 * - DPA thread must send response by filling status and setting response
 *   serial number to command serial number
 * - DPU should poll mailbox for the command response (sn change)
 */
struct snap_dpa_cmd {
	volatile uint32_t sn;
	volatile uint32_t cmd;
};

enum {
	SNAP_DPA_RSP_OK = 0,
	SNAP_DPA_RSP_ERR = 1,
	SNAP_DPA_RSP_TO = 2
};

struct snap_dpa_rsp {
	volatile uint32_t sn;
	volatile uint32_t status;
};

struct snap_dpa_cmd_start {
	struct snap_dpa_cmd base;
	struct snap_hw_cq cmd_cq;
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
	/* TODO: check if weaker barriers can be used -
	 * single io fence may be enough here. Check with AdiM NitzanA
	 */
	snap_memory_cpu_fence();
	rsp->sn = cmd->sn;
	snap_memory_bus_fence();
}

static inline struct snap_dpa_cmd *snap_dpa_cmd_recv(void *mbox, uint32_t type)
{
	volatile struct snap_dpa_cmd *cmd;

	/* wait for report back from the thread. TODO: timeout */
	do {
		cmd = snap_dpa_mbox_to_cmd(mbox);
		snap_memory_bus_fence();
	} while (cmd->cmd != type);

	return (struct snap_dpa_cmd *)cmd;
}

/**
 * Cyclical logger
 *
 * NOTE: consider continuous text buffer instead of fixed size messages.
 * If binary data are not needed, it is a better approach
 */
#define SNAP_DPA_LOG_MSG_LEN 160

struct snap_dpa_log_entry {
	uint64_t timestamp;
	char msg[SNAP_DPA_LOG_MSG_LEN];
};

struct snap_dpa_log {
	volatile uint32_t avail_idx;
	volatile uint32_t used_idx;
	uint32_t n_entries;
	uint64_t epoch;
	struct snap_dpa_log_entry entries[];
};

size_t snap_dpa_log_size(int n_entries);
void snap_dpa_log_init(struct snap_dpa_log *log, int n_entries);
void snap_dpa_log_print(struct snap_dpa_log *log);

/**
 * snap_dpa_log_add() - add entry to the cyclic log buffer
 * @log: cyclic log buffer
 * @ts:  timestamp
 * @msg: strings to add to the buffer
 *
 * The function places entry into the cyclic log buffer. If the log message
 * is too big it will be truncated.
 *
 * The function is inline because it is going to be used by the DPA application
 */
static inline void snap_dpa_log_add(struct snap_dpa_log *log, uint64_t ts, const char *msg)
{
	uint32_t n;

	/* allow overflow */
	n = log->avail_idx % log->n_entries;
	strncpy(log->entries[n].msg, msg, sizeof(log->entries[n].msg));
	log->entries[n].timestamp = ts;

	/* bus store fence (o) may be enough here */
	snap_memory_bus_fence();
	log->avail_idx++;
}
#endif
