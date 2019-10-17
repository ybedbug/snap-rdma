#include <dlfcn.h>

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

struct snap_device *snap_open_device(struct ibv_device *ibdev)
{
	struct snap_driver *driver;
	bool found = false;
	struct snap_device *sdev;

	pthread_mutex_lock(&sctx.lock);
	TAILQ_FOREACH(driver, &sctx.drivers_list, entry) {
		if (!strncmp(driver->name, ibdev->name,
		    strlen(driver->name))) {
			found = true;
			break;
		}
	}
	pthread_mutex_unlock(&sctx.lock);

	if (!found)
		return NULL;

	sdev = driver->open(ibdev);
	if (!sdev)
		return NULL;
	else
		sdev->driver = driver;

	return sdev;
}

void snap_close_device(struct snap_device *sdev)
{
	sdev->driver->close(sdev);
}

int snap_open()
{
	bool found = false;
	struct snap_driver *driver;
	void *dlhandle;
	int rc;

	rc = pthread_mutex_init(&sctx.lock, NULL);
	if (rc)
		goto out_err;

	TAILQ_INIT(&sctx.drivers_list);

	dlhandle = dlopen("libmlx5_snap.so", RTLD_LAZY);
	if (!dlhandle) {
		fprintf(stderr, PFX "couldn't load mlx5 driver.\n");
		goto out_mutex_destroy;
	}

	TAILQ_FOREACH(driver, &sctx.drivers_list, entry) {
		if (!strcmp(driver->name, "mlx5")) {
			driver->dlhandle = dlhandle;
			found = true;
			break;
		}
	}

	if (!found)
		goto out_close;

	return 0;

out_close:
	dlclose(dlhandle);
out_mutex_destroy:
	pthread_mutex_destroy(&sctx.lock);
out_err:
	return rc;
}

void snap_close()
{
	struct snap_driver *driver, *next;

	TAILQ_FOREACH_SAFE(driver, &sctx.drivers_list, entry, next)
		dlclose(driver->dlhandle);

	pthread_mutex_destroy(&sctx.lock);
}
