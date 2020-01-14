#include <stdio.h>
#include <signal.h>

#include <infiniband/verbs.h>

#include "snap.h"
#include "snap_virtio_blk_ctrl.h"
#include "snap_virtio_net_ctrl.h"
#include "snap_blk_dev.h"

static bool keep_running = true;

void signal_handler(int dummy)
{
	keep_running = 0;
}

static struct snap_context *get_snap_context(int pf_id)
{
	struct snap_context *ctx, *found = NULL;
	struct ibv_device **ibv_list;
	struct snap_pci **pf_list;
	int ibv_list_sz, pf_list_sz;
	int i, j;

	ibv_list = ibv_get_device_list(&ibv_list_sz);
	if (!ibv_list) {
		printf("Failed to get IB device list\n");
		return NULL;
	}

	for (i = 0; i < ibv_list_sz; i++) {
		ctx = snap_open(ibv_list[i]);
		if (!ctx) {
			printf("Failed to open snap ctx for %s. continue\n",
				ibv_list[i]->name);
			continue;
		}

		if (!(ctx->emulation_caps & SNAP_VIRTIO_BLK)) {
			snap_close(ctx);
			continue;
		}

		pf_list = calloc(ctx->virtio_blk_pfs.max_pfs,
				sizeof(*pf_list));
		if (!pf_list) {
			printf("Failed to create pf list for %s. continue\n",
				ibv_list[i]->name);
			snap_close(ctx);
			continue;
		}

		pf_list_sz = snap_get_pf_list(ctx, SNAP_VIRTIO_BLK, pf_list);
		for (j = 0; j < pf_list_sz; j++) {
			if (pf_list[j]->plugged && pf_list[j]->id == pf_id) {
				found = ctx;
				break;
			}
		}
		free(pf_list);

		if (found)
			break;
		else
			snap_close(ctx);
	}

	ibv_free_device_list(ibv_list);
	return found;
}

static void put_snap_context(struct snap_context *ctx)
{
	snap_close(ctx);
}

int main(int argc, char **argv)
{
	struct sigaction act;
	int ret = 0, opt;
	struct snap_virtio_blk_ctrl *blk_ctrl = NULL;
	struct snap_virtio_net_ctrl *net_ctrl = NULL;
	struct snap_virtio_blk_ctrl_attr blk_attr = {};
	struct snap_blk_dev_attrs bdev_attrs = {0};
	struct snap_virtio_net_ctrl_attr net_attr = {};
	struct snap_blk_dev *bdev;
	enum snap_virtio_ctrl_type type;
	int pf_id = 0;
	struct snap_context *sctx = NULL;

	memset(&act, 0, sizeof(act));
	act.sa_handler = signal_handler;
	sigaction(SIGINT, &act, 0);
	sigaction(SIGPIPE, &act, 0);
	sigaction(SIGTERM, &act, 0);

	while ((opt = getopt(argc, argv, "t:f:")) != -1) {
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
		case 'f':
			pf_id = strtol(optarg, NULL, 0);
			break;
		default:
			printf("Usage: snap_create_destroy_virtio_ctrl "
				"-t <ctrl_type> "
				"-f <pf_id>\n");
			exit(1);
		}
	}

	sctx = get_snap_context(pf_id);
	if (!sctx) {
		printf("Failed to get snap context for PF %d\n", pf_id);
		ret = -ENODEV;
		goto err;
	}

	if (type == SNAP_VIRTIO_BLK_CTRL) {
		bdev_attrs.type = SNAP_BLOCK_DEVICE_NULL;
		bdev_attrs.size_b = 20;
		bdev_attrs.blk_size = 9;
		bdev = snap_blk_dev_open("null_blk", &bdev_attrs);
		if (!bdev) {
		    printf("Failed to open null block device\n");
		    goto put_sctx;
		}

		blk_attr.common.pf_id = pf_id;
		blk_ctrl = snap_virtio_blk_ctrl_open(sctx, &blk_attr, &bdev->ops,
						     bdev);
		if (!blk_ctrl) {
			printf("Failed to create virtio-blk controller\n");
			ret = -ENODEV;
			goto close_blk;
		}
	} else {
		net_attr.common.pf_id = pf_id;
		net_ctrl = snap_virtio_net_ctrl_open(sctx, &net_attr);
		if (!net_ctrl) {
			printf("Failed to create virtio-net controller\n");
			ret = -ENODEV;
			goto put_sctx;
		}
	}
	printf("virtio controller of type %d opened successfully\n", type);

	while (keep_running) {
		if (type == SNAP_VIRTIO_BLK_CTRL) {
			snap_virtio_blk_ctrl_progress(blk_ctrl);
			snap_virtio_blk_ctrl_io_progress(blk_ctrl);
		} else {
			snap_virtio_net_ctrl_progress(net_ctrl);
			snap_virtio_net_ctrl_io_progress(net_ctrl);
		}
		usleep(200 * 1000);
	}

	if (type == SNAP_VIRTIO_BLK_CTRL)
		snap_virtio_blk_ctrl_close(blk_ctrl);
	else
		snap_virtio_net_ctrl_close(net_ctrl);
	printf("virtio controller closed\n");
close_blk:
	if (type == SNAP_VIRTIO_BLK_CTRL)
		snap_blk_dev_close(bdev);
put_sctx:
	put_snap_context(sctx);
err:
	return ret;
}
