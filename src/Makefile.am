ACLOCAL_AMFLAGS = -I m4

lib_LTLIBRARIES = librdma_snap.la libmlx5_snap.la

librdma_snap_ladir = $(includedir)/rdma-snap

librdma_snap_la_HEADERS = \
				snap.h
				queue.h

librdma_snap_la_SOURCES = \
				snap.c

librdma_snap_la_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS)

librdma_snap_la_LDFLAGS = -module -avoid-version -export-dynamic

libmlx5_snap_ladir = $(includedir)/rdma-snap

libmlx5_snap_la_HEADERS = \
				mlx5_snap.h
				snap.h

libmlx5_snap_la_SOURCES = \
				mlx5_snap.c

libmlx5_snap_la_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS)

librdma_snap_la_LDFLAGS = -module -avoid-version -export-dynamic
