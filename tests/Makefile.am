ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = \
		snap_open_close \
		snap_open_close_device \
		snap_query_pfs \
		snap_init_teardown_nvme_device \
		snap_create_destroy_nvme_namespace \
		snap_create_destroy_nvme_cq \
		snap_create_destroy_nvme_sq

snap_open_close_CFLAGS = $(BASE_CFLAGS) -I$(top_builddir)/src
snap_open_close_SOURCES = snap_open_close.c
snap_open_close_LDADD = $(top_builddir)/src/libsnap.la

snap_open_close_device_CFLAGS = $(BASE_CFLAGS) -I$(top_builddir)/src
snap_open_close_device_SOURCES = snap_open_close_device.c
snap_open_close_device_LDADD = $(top_builddir)/src/libsnap.la

snap_query_pfs_CFLAGS = $(BASE_CFLAGS) -I$(top_builddir)/src
snap_query_pfs_SOURCES = snap_query_pfs.c
snap_query_pfs_LDADD = $(top_builddir)/src/libsnap.la

snap_init_teardown_nvme_device_CFLAGS = $(BASE_CFLAGS) -I$(top_builddir)/src
snap_init_teardown_nvme_device_SOURCES = snap_init_teardown_nvme_device.c
snap_init_teardown_nvme_device_LDADD = $(top_builddir)/src/libsnap.la $(top_builddir)/src/libsnap_nvme.la

snap_create_destroy_nvme_namespace_CFLAGS = $(BASE_CFLAGS) -I$(top_builddir)/src
snap_create_destroy_nvme_namespace_SOURCES = snap_create_destroy_nvme_namespace.c
snap_create_destroy_nvme_namespace_LDADD = $(top_builddir)/src/libsnap.la $(top_builddir)/src/libsnap_nvme.la

snap_create_destroy_nvme_cq_CFLAGS = $(BASE_CFLAGS) -I$(top_builddir)/src
snap_create_destroy_nvme_cq_SOURCES = snap_create_destroy_nvme_cq.c
snap_create_destroy_nvme_cq_LDADD = $(top_builddir)/src/libsnap.la $(top_builddir)/src/libsnap_nvme.la

snap_create_destroy_nvme_sq_CFLAGS = $(BASE_CFLAGS) -I$(top_builddir)/src
snap_create_destroy_nvme_sq_SOURCES = snap_create_destroy_nvme_sq.c
snap_create_destroy_nvme_sq_LDADD = $(top_builddir)/src/libsnap.la $(top_builddir)/src/libsnap_nvme.la
