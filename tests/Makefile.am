ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = snap_open_close snap_create_destroy_context snap_open_close_device snap_query_pfs

snap_open_close_CFLAGS = $(BASE_CFLAGS) -I$(srcdir)/../src
snap_open_close_SOURCES = snap_open_close.c
snap_open_close_LDADD = $(top_builddir)/src/libsnap_rdma.la
snap_open_close_LDFLAGS = -ldl

snap_create_destroy_context_CFLAGS = $(BASE_CFLAGS) -I$(srcdir)/../src
snap_create_destroy_context_SOURCES = snap_create_destroy_context.c
snap_create_destroy_context_LDADD = -libverbs $(top_builddir)/src/libsnap_rdma.la
snap_create_destroy_context_LDFLAGS = -ldl

snap_open_close_device_CFLAGS = $(BASE_CFLAGS) -I$(srcdir)/../src
snap_open_close_device_SOURCES = snap_open_close_device.c
snap_open_close_device_LDADD = -libverbs $(top_builddir)/src/libsnap_rdma.la
snap_open_close_device_LDFLAGS = -ldl

snap_query_pfs_CFLAGS = $(BASE_CFLAGS) -I$(srcdir)/../src
snap_query_pfs_SOURCES = snap_query_pfs.c
snap_query_pfs_LDADD = -libverbs $(top_builddir)/src/libsnap_rdma.la
snap_query_pfs_LDFLAGS = -ldl
