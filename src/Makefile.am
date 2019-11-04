ACLOCAL_AMFLAGS = -I m4

lib_LTLIBRARIES = libsnap_rdma.la

libsnap_rdma_ladir = $(includedir)/snap-rdma

libsnap_rdma_la_HEADERS = snap.h

noinst_HEADERS = mlx5_snap.h mlx5_ifc.h queue.h

libsnap_rdma_la_SOURCES = snap.c

libsnap_rdma_la_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS)

libsnap_rdma_la_LIBADD = -libverbs -lmlx5 -lpthread
