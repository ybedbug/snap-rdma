
#include "snap.h"


struct snap_ctx {
	pthread_mutex_t			lock;
	TAILQ_HEAD(, snap_driver)	drivers_list;
};

static struct snap_ctx sctx;

int snap_open()
{
	int rc;

	rc = pthread_mutex_init(&sctx.lock, NULL);
	if (rc)
		goto out_err;

	TAILQ_INIT(&sctx.drivers_list);

	return 0;

out_err:
	return rc;
}

void snap_close()
{
	pthread_mutex_destroy(&sctx.lock);
}
