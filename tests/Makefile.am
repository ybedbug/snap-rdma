ACLOCAL_AMFLAGS = -I m4

bin_PROGRAMS = snap_open_close snap_open_close_device snap_query_pfs

snap_open_close_CFLAGS = $(BASE_CFLAGS) -I$(top_builddir)/src
snap_open_close_SOURCES = snap_open_close.c
snap_open_close_LDADD = $(top_builddir)/src/libsnap.la

snap_open_close_device_CFLAGS = $(BASE_CFLAGS) -I$(top_builddir)/src
snap_open_close_device_SOURCES = snap_open_close_device.c
snap_open_close_device_LDADD = $(top_builddir)/src/libsnap.la

snap_query_pfs_CFLAGS = $(BASE_CFLAGS) -I$(top_builddir)/src
snap_query_pfs_SOURCES = snap_query_pfs.c
snap_query_pfs_LDADD = $(top_builddir)/src/libsnap.la
