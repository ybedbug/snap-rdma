
#include "snap.h"


struct snap_ctx {
	pthread_mutex_t			lock;
	TAILQ_HEAD(, snap_driver)	drivers_list;
};

static struct snap_ctx sctx;

void snap_unregister_driver(struct snap_driver *driver)
{
	struct snap_driver *tmp, *next;

	pthread_mutex_lock(&sctx.lock);
	TAILQ_FOREACH_SAFE(tmp, &sctx.drivers_list, entry, next) {
		if (tmp == driver) {
			TAILQ_REMOVE(&sctx.drivers_list, driver, entry);
			break;
		}
	}
	pthread_mutex_unlock(&sctx.lock);
}

void snap_register_driver(struct snap_driver *driver)
{
	struct snap_driver *tmp;
	bool found = false;

	pthread_mutex_lock(&sctx.lock);
	TAILQ_FOREACH(tmp, &sctx.drivers_list, entry) {
		if (tmp == driver) {
			found = true;
			break;
		}
	}

	if (!found)
		TAILQ_INSERT_HEAD(&sctx.drivers_list, driver, entry);
	pthread_mutex_unlock(&sctx.lock);
}

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
