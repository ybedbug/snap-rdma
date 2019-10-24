#include <dlfcn.h>

#include "snap.h"


struct g_snap_ctx {
	pthread_mutex_t			lock;
	TAILQ_HEAD(, snap_driver)	drivers_list;
};

static struct g_snap_ctx g_sctx;

/**
 * snap_unregister_driver() - Unregister providers driver
 * @driver:       snap driver
 *
 * This routine should only be called from providers library destructor. It
 * will unregister a previously registered snap driver.
 */
void snap_unregister_driver(struct snap_driver *driver)
{
	struct snap_driver *tmp, *next;

	pthread_mutex_lock(&g_sctx.lock);
	TAILQ_FOREACH_SAFE(tmp, &g_sctx.drivers_list, entry, next) {
		if (tmp == driver) {
			TAILQ_REMOVE(&g_sctx.drivers_list, driver, entry);
			break;
		}
	}
	pthread_mutex_unlock(&g_sctx.lock);
}

/**
 * snap_register_driver() - Register providers driver
 * @driver:       snap driver
 *
 * This routine should only be called from providers library constructor. It
 * will register a driver that implements snap mandatory capabilities.
 */
void snap_register_driver(struct snap_driver *driver)
{
	struct snap_driver *tmp;
	bool found = false;

	pthread_mutex_lock(&g_sctx.lock);
	TAILQ_FOREACH(tmp, &g_sctx.drivers_list, entry) {
		if (tmp == driver) {
			found = true;
			break;
		}
	}

	if (!found)
		TAILQ_INSERT_HEAD(&g_sctx.drivers_list, driver, entry);
	pthread_mutex_unlock(&g_sctx.lock);
}

/**
 * snap_is_capable_device() - Checks if RDMA device is snap capable
 * @ibdev:       RDMA device
 *
 * Return: Returns true if ibdev is snap capable. Otherwise, returns false.
 */
bool snap_is_capable_device(struct ibv_device *ibdev)
{
	bool found = false;
	struct snap_driver *driver;

	pthread_mutex_lock(&g_sctx.lock);
	TAILQ_FOREACH(driver, &g_sctx.drivers_list, entry) {
		if (!strncmp(driver->name, ibdev->name,
		    strlen(driver->name))) {
			found = true;
			break;
		}
	}
	pthread_mutex_unlock(&g_sctx.lock);

	if (found)
		return driver->is_capable(ibdev);

	return false;

}

struct snap_context *snap_create_context(struct ibv_device *ibdev)
{
	bool found = false;
	struct snap_driver *driver;
	struct snap_context *sctx;
	int rc;

	pthread_mutex_lock(&g_sctx.lock);
	TAILQ_FOREACH(driver, &g_sctx.drivers_list, entry) {
		if (!strncmp(driver->name, ibdev->name,
		    strlen(driver->name))) {
			found = true;
			break;
		}
	}
	pthread_mutex_unlock(&g_sctx.lock);

	if (!found)
		return NULL;

	sctx = driver->create(ibdev);
	if (!sctx)
		return NULL;
	else
		sctx->driver = driver;

	return sctx;
}

void snap_destroy_context(struct snap_context *sctx)
{
	sctx->driver->destroy(sctx);
}

struct snap_device *snap_open_device(struct snap_context *sctx,
				     struct snap_device_attr *attr)
{
	struct snap_driver *driver = sctx->driver;
	struct snap_device *sdev;

	sdev = driver->open(sctx, attr);
	if (!sdev)
		return NULL;

	sdev->sctx = sctx;
	sdev->type = attr->type;

	return sdev;
}

void snap_close_device(struct snap_device *sdev)
{
	struct snap_driver *driver = sdev->sctx->driver;

	driver->close(sdev);
}

struct snap_pci **snap_get_pf_list(struct snap_context *sctx, int *count)
{
	return sctx->driver->get_pf_list(sctx, count);
}

void snap_free_pf_list(struct snap_pci **list)
{
	free(list);
}

/**
 * snap_open() - Initialize snap library
 *
 * Notes:
 * This routine should be the first routine that is called by the library user.
 * It should be called once per process. It will create and initialize global
 * internal resources and load providers shared objects as well.
 */
int snap_open()
{
	bool found = false;
	struct snap_driver *driver;
	void *dlhandle;
	int rc;

	rc = pthread_mutex_init(&g_sctx.lock, NULL);
	if (rc)
		goto out_err;

	TAILQ_INIT(&g_sctx.drivers_list);

	dlhandle = dlopen("libmlx5_snap.so", RTLD_LAZY);
	if (!dlhandle) {
		fprintf(stderr, PFX "couldn't load mlx5 driver.\n");
		goto out_mutex_destroy;
	}

	TAILQ_FOREACH(driver, &g_sctx.drivers_list, entry) {
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
	pthread_mutex_destroy(&g_sctx.lock);
out_err:
	return rc;
}

/**
 * snap_close() - Close snap library
 *
 * Notes:
 * This routine should be the last routine that is called by the library user.
 * It should be called once per process and only if snap_open() was called to
 * initialize the library. It will destroy internal resources and unload
 * providers shared objects as well.
 */
void snap_close()
{
	struct snap_driver *driver, *next;

	TAILQ_FOREACH_SAFE(driver, &g_sctx.drivers_list, entry, next)
		dlclose(driver->dlhandle);

	pthread_mutex_destroy(&g_sctx.lock);
}
