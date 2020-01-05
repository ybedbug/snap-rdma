#include <stdio.h>
#include <signal.h>

#include <infiniband/verbs.h>

#include "snap.h"
#include "snap_virtio_blk_ctrl.h"
#include "snap_virtio_net_ctrl.h"

static bool keep_running = true;

void signal_handler(int dummy)
{
	keep_running = 0;
}

int main(int argc, char **argv)
{
	struct sigaction act;
	int ret, opt;
	struct snap_virtio_blk_ctrl *blk_ctrl = NULL;
	struct snap_virtio_net_ctrl *net_ctrl = NULL;
	struct snap_virtio_blk_ctrl_attr blk_attr = {};
	struct snap_virtio_net_ctrl_attr net_attr = {};
	enum snap_virtio_ctrl_type type;

	memset(&act, 0, sizeof(act));
	act.sa_handler = signal_handler;
	sigaction(SIGINT, &act, 0);
	sigaction(SIGPIPE, &act, 0);
	sigaction(SIGTERM, &act, 0);

	while ((opt = getopt(argc, argv, "t:")) != -1) {
		switch (opt) {
		case 't':
			if (!strcmp(optarg, "blk")) {
				type = SNAP_VIRTIO_BLK_CTRL;
			} else if (!strcmp(optarg, "net")) {
				type = SNAP_VIRTIO_NET_CTRL;
			} else {
				printf("Invalid ctrl type %s\n", optarg);
				exit(1);
			}
			break;
		default:
			printf("Usage: %s -t <ctrl_type>\n");
			exit(1);
		}
	}

	if (type == SNAP_VIRTIO_BLK_CTRL) {
		blk_ctrl = snap_virtio_blk_ctrl_open(&blk_attr);
		if (!blk_ctrl)
			return -ENODEV;
	} else {
		net_ctrl = snap_virtio_net_ctrl_open(&net_attr);
		if (!net_ctrl)
			return -ENODEV;
	}
	printf("virtio controller of type %d opened successfully\n", type);

	while (keep_running) {
		if (type == SNAP_VIRTIO_BLK_CTRL)
			snap_virtio_blk_ctrl_progress(blk_ctrl);
		else
			snap_virtio_net_ctrl_progress(net_ctrl);
		usleep(200 * 1000);
	}

	if (type == SNAP_VIRTIO_BLK_CTRL)
		snap_virtio_blk_ctrl_close(blk_ctrl);
	else
		snap_virtio_net_ctrl_close(net_ctrl);
	printf("virtio controller closed\n");

	return 0;
}
