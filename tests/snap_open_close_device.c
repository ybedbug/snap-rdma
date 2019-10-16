#include <stdio.h>

#include <infiniband/verbs.h>

#include <snap.h>

int main(int argc, char **argv)
{
	struct ibv_device **list;
	struct snap_device *sdev;
	int ret, i, dev_count;

	ret = snap_open();
	if (ret) {
		fprintf(stderr, "failed to open snap. ret=%d\n", ret);
		fflush(stderr);
		exit(1);
	}

	list = ibv_get_device_list(&dev_count);
	if (!list) {
		fprintf(stderr, "failed to open ib device list.\n");
		fflush(stderr);
		ret = 1;
		goto out;
	}

	for (i = 0; i < dev_count; i++) {
		if (!snap_is_capable_device(list[i])) {
			fprintf(stderr, "device %s is not snap device\n",
				list[i]->name);
			fflush(stderr);
			continue;
		}
		sdev = snap_open_device(list[i]);
		if (!sdev)
			break;
		snap_close_device(sdev);
	}
out:
	snap_close();

	return ret;
}
