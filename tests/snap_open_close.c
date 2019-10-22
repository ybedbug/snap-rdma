#include <stdio.h>

#include "snap.h"

int main(int argc, char **argv)
{
	int ret;

	ret = snap_open();
	if (ret) {
		fprintf(stderr, "failed to open snap. ret=%d\n", ret);
		exit(1);
	}

	snap_close();

	return 0;
}
