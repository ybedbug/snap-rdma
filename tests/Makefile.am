ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = \
		snap_open_close \
		snap_open_close_device \
		snap_query_pfs \
		snap_init_teardown_device \
		snap_query_nvme_device \
		snap_create_destroy_nvme_namespace \
		snap_create_destroy_nvme_cq \
		snap_create_destroy_nvme_sq \
		snap_query_virtio_device \
		snap_create_destroy_virtio_queue

snap_open_close_CFLAGS = $(BASE_CFLAGS) -I$(top_srcdir)/src
snap_open_close_SOURCES = snap_open_close.c
snap_open_close_LDADD = $(top_builddir)/src/libsnap.la

snap_open_close_device_CFLAGS = $(BASE_CFLAGS) -I$(top_srcdir)/src
snap_open_close_device_SOURCES = snap_open_close_device.c
snap_open_close_device_LDADD = $(top_builddir)/src/libsnap.la

snap_query_pfs_CFLAGS = $(BASE_CFLAGS) -I$(top_srcdir)/src
snap_query_pfs_SOURCES = snap_query_pfs.c
snap_query_pfs_LDADD = $(top_builddir)/src/libsnap.la

snap_init_teardown_device_CFLAGS = $(BASE_CFLAGS) -I$(top_srcdir)/src
snap_init_teardown_device_SOURCES = snap_init_teardown_device.c
snap_init_teardown_device_LDADD = $(top_builddir)/src/libsnap.la

snap_query_nvme_device_CFLAGS = $(BASE_CFLAGS) -I$(top_srcdir)/src
snap_query_nvme_device_SOURCES = snap_query_nvme_device.c
snap_query_nvme_device_LDADD = $(top_builddir)/src/libsnap.la

snap_create_destroy_nvme_namespace_CFLAGS = $(BASE_CFLAGS) -I$(top_srcdir)/src
snap_create_destroy_nvme_namespace_SOURCES = snap_create_destroy_nvme_namespace.c
snap_create_destroy_nvme_namespace_LDADD = $(top_builddir)/src/libsnap.la

snap_create_destroy_nvme_cq_CFLAGS = $(BASE_CFLAGS) -I$(top_srcdir)/src
snap_create_destroy_nvme_cq_SOURCES = snap_create_destroy_nvme_cq.c
snap_create_destroy_nvme_cq_LDADD = $(top_builddir)/src/libsnap.la

snap_create_destroy_nvme_sq_CFLAGS = $(BASE_CFLAGS) -I$(top_srcdir)/src
snap_create_destroy_nvme_sq_SOURCES = snap_create_destroy_nvme_sq.c
snap_create_destroy_nvme_sq_LDADD = $(top_builddir)/src/libsnap.la

snap_query_virtio_device_CFLAGS = $(BASE_CFLAGS) -I$(top_srcdir)/src
snap_query_virtio_device_SOURCES = snap_query_virtio_device.c
snap_query_virtio_device_LDADD = $(top_builddir)/src/libsnap.la

snap_create_destroy_virtio_queue_CFLAGS = $(BASE_CFLAGS) -I$(top_srcdir)/src
snap_create_destroy_virtio_queue_SOURCES = snap_create_destroy_virtio_queue.c
snap_create_destroy_virtio_queue_LDADD = $(top_builddir)/src/libsnap.la
