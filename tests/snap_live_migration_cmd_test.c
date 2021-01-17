#include "snap_live_migration_client.h"

static __u16 command_id = 1;

static void create_cmd_start_log(char *buf)
{
	struct mlx5_snap_start_dirty_log_command *cmd =
		(struct mlx5_snap_start_dirty_log_command *) buf;

	cmd->opcode = MLX5_SNAP_CMD_START_LOG;
	cmd->page_size = 1024;
}

static void create_cmd(char *buf, int opcode)
{
	struct mlx5_snap_common_command *cmd =
		(struct mlx5_snap_common_command *) buf;

	switch (opcode) {
	case MLX5_SNAP_CMD_START_LOG:
		create_cmd_start_log(buf);
		break;
	case MLX5_SNAP_CMD_STOP_LOG:
	case MLX5_SNAP_CMD_GET_LOG_SZ:
	case MLX5_SNAP_CMD_REPORT_LOG:
	case MLX5_SNAP_CMD_FREEZE_DEV:
	case MLX5_SNAP_CMD_UNFREEZE_DEV:
	case MLX5_SNAP_CMD_QUIESCE_DEV:
	case MLX5_SNAP_CMD_UNQUIESCE_DEV:
	case MLX5_SNAP_CMD_GET_STATE_SZ:
	case MLX5_SNAP_CMD_READ_STATE:
	case MLX5_SNAP_CMD_WRITE_STATE:
		cmd->opcode = opcode;
		break;
	default:
		snap_channel_info("got unexpected opcode %u\n", opcode);
		break;
	}
	cmd->command_id = command_id;
	command_id = (command_id % SNAP_CHANNEL_QUEUE_SIZE) + 1;
}

static int snap_channel_run_client(struct snap_channel_test *stest)
{
	int ret;
	struct ibv_send_wr *bad_wr; // bad work request
	struct snap_rdma_channel *schannel = stest->schannel;

	snap_channel_info("sending message - start logging..\n");
	create_cmd(schannel->recv_buf, MLX5_SNAP_CMD_START_LOG);
	ret = ibv_post_send(schannel->qp, schannel->rsp_wr, &bad_wr);
	if (ret) {
		snap_channel_error("post send error %d\n", ret);
		goto out;
	}
	sleep(5);

	snap_channel_info("sending message - stop logging..\n");
	create_cmd(schannel->recv_buf, MLX5_SNAP_CMD_STOP_LOG);
	ret = ibv_post_send(schannel->qp, schannel->rsp_wr, &bad_wr);
	if (ret) {
		snap_channel_error("post send error %d\n", ret);
		goto out;
	}
	sleep(5);

	snap_channel_info("sending message - freeze device..\n");
	create_cmd(schannel->recv_buf, MLX5_SNAP_CMD_FREEZE_DEV);
	ret = ibv_post_send(schannel->qp, schannel->rsp_wr, &bad_wr);
	if (ret) {
		snap_channel_error("post send error %d\n", ret);
		goto out;
	}
	sleep(5);

	snap_channel_info("sending message - unfreeze device..\n");
	create_cmd(schannel->recv_buf, MLX5_SNAP_CMD_UNFREEZE_DEV);
	ret = ibv_post_send(schannel->qp, schannel->rsp_wr, &bad_wr);
	if (ret) {
		snap_channel_error("post send error %d\n", ret);
		goto out;
	}
	sleep(5);

	snap_channel_info("sending message - quiesce device..\n");
	create_cmd(schannel->recv_buf, MLX5_SNAP_CMD_QUIESCE_DEV);
	ret = ibv_post_send(schannel->qp, schannel->rsp_wr, &bad_wr);
	if (ret) {
		snap_channel_error("post send error %d\n", ret);
		goto out;
	}
	sleep(5);

	snap_channel_info("sending message - unquiesce device..\n");
	create_cmd(schannel->recv_buf, MLX5_SNAP_CMD_UNQUIESCE_DEV);
	ret = ibv_post_send(schannel->qp, schannel->rsp_wr, &bad_wr);
	if (ret) {
		snap_channel_error("post send error %d\n", ret);
		goto out;
	}
	sleep(5);

out:
	return ret;
}

int main(int argc, char **argv)
{
	int ret = 0;
	struct snap_channel_test *stest;

	stest = calloc(1, sizeof(*stest));
	if (!stest) {
		errno = ENOMEM;
		ret = ENOMEM;
		goto out;
	}

	ret = open_client(stest);
	if (ret) {
		snap_channel_error("failed to connect cleint: %d\n", ret);
		goto out;
	}
	ret = snap_channel_run_client(stest);

	snap_channel_info("closing connection.. ");
	close_client(stest);
	free(stest);
	snap_channel_info("Bye!\n");
out:
	return ret;
}
