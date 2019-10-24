ACLOCAL_AMFLAGS = -I m4

lib_LTLIBRARIES = libsnap_rdma.la libmlx5_snap.la

libsnap_rdma_ladir = $(includedir)/snap-rdma

libsnap_rdma_la_HEADERS = snap.h queue.h

libsnap_rdma_la_SOURCES = \
				snap.c

libsnap_rdma_la_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS) -fPIC

libsnap_rdma_la_LDFLAGS = -module -avoid-version -export-dynamic -ldl

libmlx5_snap_ladir = $(includedir)/mlx5-snap

libmlx5_snap_la_HEADERS = mlx5_snap.h queue.h

libmlx5_snap_la_SOURCES = \
				mlx5_snap.c

libmlx5_snap_la_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS) -fPIC

libmlx5_snap_la_LIBADD = -libverbs -lmlx5 -lpthread libsnap_rdma.la

libmlx5_snap_la_LDFLAGS = -module -avoid-version -export-dynamic
