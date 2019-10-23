ACLOCAL_AMFLAGS = -I m4

lib_LTLIBRARIES = librdma_snap.la libmlx5_snap.la

librdma_snap_ladir = $(includedir)/rdma-snap

librdma_snap_la_HEADERS = snap.h queue.h

librdma_snap_la_SOURCES = \
				snap.c

librdma_snap_la_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS) -fPIC

librdma_snap_la_LDFLAGS = -module -avoid-version -export-dynamic -ldl

libmlx5_snap_ladir = $(includedir)/mlx5-snap

libmlx5_snap_la_HEADERS = mlx5_snap.h queue.h

libmlx5_snap_la_SOURCES = \
				mlx5_snap.c

libmlx5_snap_la_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS) -fPIC

libmlx5_snap_la_LIBADD = -libverbs -lmlx5 -lpthread librdma_snap.la

libmlx5_snap_la_LDFLAGS = -module -avoid-version -export-dynamic
