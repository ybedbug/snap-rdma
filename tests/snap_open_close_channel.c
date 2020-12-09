#include <stdio.h>
#include <signal.h>

#include "snap.h"
#include "snap_channel.h"

static struct snap_migration_ops test_fail_ops = {};

static int test_quiesce(void *data)
{
	return 0;
}

static int test_unquiesce(void *data)
{
	return 0;
}

static int test_freeze(void *data)
{
	return 0;
}

static int test_unfreeze(void *data)
{
	return 0;
}

static int test_get_state_size(void *data)
{
	return 0;
}

static int test_copy_state(void *data, void *buff, int len, bool copy_from_buffer)
{
	return 0;
}

static int test_start_dirty_pages_track(void *data)
{
	return 0;
}

static int test_stop_dirty_pages_track(void *data)
{
	return 0;
}

static struct snap_migration_ops test_ops = {
	.quiesce = test_quiesce,
	.unquiesce = test_unquiesce,
	.freeze = test_freeze,
	.unfreeze = test_unfreeze,
	.get_state_size = test_get_state_size,
	.copy_state = test_copy_state,
	.start_dirty_pages_track = test_start_dirty_pages_track,
	.stop_dirty_pages_track = test_stop_dirty_pages_track,
};

static void usage(char *name)
{
	printf("%s [-d (dirty page logging)] [-v (verbosity)] [-f (fail ops)]"
		"[-n <num_pages>] [-s <start_pa>]\n", name);
}

static bool keep_running;

static void signal_handler(int dummy)
{
	keep_running = false;
}

int main(int argc, char **argv)
{
	struct snap_channel *schannel;
	struct snap_migration_ops *ops;
	uint64_t start_address = 0;
	int ret = 0, i, opt, num_pages = 100;
	bool dirty = false, verbose = false, fail_ops = false;
	struct sigaction act;

	while ((opt = getopt(argc, argv, "vdfkn:s:")) != -1) {
		switch (opt) {
		case 'd':
			dirty = true;
			break;
		case 'v':
			verbose = true;
			break;
		case 'n':
			num_pages = atoi(optarg);
			break;
		case 'f':
			fail_ops = true;
			break;
		case 'k':
			keep_running = true;
			break;
		case 's':
			start_address = atol(optarg);
			break;
		default:
			usage("snap_open_close_channel");
			ret = EINVAL;
			goto out;
		}
	}

	if (fail_ops)
		ops = &test_fail_ops;
	else
		ops = &test_ops;

	schannel = snap_channel_open("rdma_migration_channel", ops, NULL);
	if (!schannel) {
		if (fail_ops) {
			fprintf(stdout, "failed to open snap channel as expected. Exiting...\n");
			fflush(stdout);
			goto out;
		}
		fprintf(stderr, "failed to open snap channel\n");
		fflush(stderr);
		ret = -errno;
		goto out;
	}

	if (keep_running) {
		memset(&act, 0, sizeof(act));
		act.sa_handler = signal_handler;
		sigaction(SIGINT, &act, 0);
		sigaction(SIGPIPE, &act, 0);
		sigaction(SIGTERM, &act, 0);

		while (keep_running)
			usleep(200000);
	}

	if (dirty) {
		for (i = 0; i < num_pages; i++) {
			uint64_t guest_pa = start_address + 4096 * i;

			if (snap_channel_mark_dirty_page(schannel, guest_pa, 4096)) {
				fprintf(stderr, "failed to mark dirty page 0x%lx\n",
					guest_pa);
				fflush(stderr);
				ret = -EAGAIN;
			} else if (verbose) {
				fprintf(stdout, "marked dirty page 0x%lx successfully\n",
					guest_pa);
				fflush(stdout);
			}
		}
	}
	snap_channel_close(schannel);

	fprintf(stdout, "opened and closed snap channel successfully\n");
	fflush(stdout);
out:
	return ret;
}
