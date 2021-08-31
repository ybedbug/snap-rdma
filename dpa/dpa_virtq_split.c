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

#include "dpa.h"
#include "snap_dpa_virtq.h"

/* At the moment we only run one thread with one virtq slot
 * TODO:
 * - multiple threads, per thread storage
 */
static int n_virtqs; //per thread
struct dpa_virtq {
	uint16_t dpu_avail_idx;
	uint32_t avail_mkey_host;
	uint32_t avail_mkey_dpu;
	uint64_t avail_ring;
	uint64_t avail_dest_dpu; // for copy avail transfer
	uint32_t num;
};

static struct dpa_virtq virtqs[8]; // per thread

#define COMMAND_DELAY 100000

int dpa_virtq_create(struct snap_dpa_cmd *cmd)
{
	dpa_print_string("virtq create\n");
	n_virtqs++;
	return SNAP_DPA_RSP_OK;
}

int dpa_virtq_destroy(struct snap_dpa_cmd *cmd)
{
	dpa_print_string("virtq destroy\n");
	n_virtqs--;
	return SNAP_DPA_RSP_OK;
}

int dpa_virtq_modify(struct snap_dpa_cmd *cmd)
{
	dpa_print_string("virtq modify\n");
	return SNAP_DPA_RSP_OK;
}

int dpa_virtq_query(struct snap_dpa_cmd *cmd)
{
	dpa_print_string("virtq query\n");
	return SNAP_DPA_RSP_OK;
}

static int do_command(int *done)
{
	static uint32_t last_sn; // per thread

	struct snap_dpa_cmd *cmd;
	uint32_t rsp_status;

	*done = 0;
	dpa_print_string("command check\n");

	cmd = snap_dpa_mbox_to_cmd(dpa_mbox());

	if (cmd->sn == last_sn)
		return 0;

	dpa_print_string("new command\n");

	last_sn = cmd->sn;
	rsp_status = SNAP_DPA_RSP_OK;

	switch (cmd->cmd) {
		case SNAP_DPA_CMD_STOP:
			*done = 1;
			break;
		case DPA_VIRTQ_CMD_CREATE:
			rsp_status = dpa_virtq_create(cmd);
			break;
		case DPA_VIRTQ_CMD_DESTROY:
			rsp_status = dpa_virtq_destroy(cmd);
			break;
		case DPA_VIRTQ_CMD_MODIFY:
			rsp_status = dpa_virtq_modify(cmd);
			break;
		case DPA_VIRTQ_CMD_QUERY:
			rsp_status = dpa_virtq_query(cmd);
			break;
		default:
			dpa_print_string("unsupported command\n");
	}

	snap_dpa_rsp_send(dpa_mbox(), rsp_status);
	return 0;
}

static inline int process_commands(int *done)
{
	static unsigned count; //per thread

	if (count++ % COMMAND_DELAY) { // TODO: mark unlikely
		*done = 0;
		return 0;
	}

	return do_command(done);
}

static inline void virtq_progress()
{
	int i;
	struct dpa_virtq __attribute__((unused)) *vq;

	for (i = 0; i < n_virtqs; i++) {
		/* load avail index */
		vq = &virtqs[i];
#if 0
		if (dpa_avail == host_vail)
			continue;
		/* load & copy to arm */
#endif
	}
}

int main()
{
	int done;
	int ret;

	dpa_print_string("virtq_split starting\n");
	do {
		ret = process_commands(&done);
		virtq_progress();
	} while (!done);

	//snap_dpa_cmd_recv(dpa_mbox(), SNAP_DPA_CMD_STOP);
	dpa_print_string("virtq_split done\n");

	return ret;
}
