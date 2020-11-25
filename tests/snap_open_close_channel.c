#include <stdio.h>

#include "snap.h"
#include "snap_channel.h"

int main(int argc, char **argv)
{
	struct snap_channel *schannel;
	struct snap_migration_ops ops;
	int ret = 0, i, opt, num_pages = 100;
	bool dirty = false, verbose = false;

	while ((opt = getopt(argc, argv, "vdn:")) != -1) {
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
		default:
			printf("Usage: snap_open_close_channel [-d (dirty page logging)] [-v (verbosity)] [-v <num_pages>]\n");
			exit(1);
		}
	}

	schannel = snap_channel_open(&ops, NULL);
	if (!schannel) {
		fprintf(stderr, "failed to open snap channel\n");
		fflush(stderr);
		ret = -errno;
		goto out;
	}

	if (dirty) {
		for (i = 0; i < num_pages; i++) {
			uint64_t guest_pa = 4096 * (i + 1);

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
