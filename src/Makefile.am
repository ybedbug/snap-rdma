ACLOCAL_AMFLAGS = -I m4

lib_LTLIBRARIES = libsnap.la

noinst_HEADERS = mlx5_ifc.h

#snap lib
libsnap_ladir = $(includedir)/
libsnap_la_HEADERS = snap.h snap_nvme.h snap_virtio_common.h snap_virtio_blk.h mlx5_snap.h queue.h
libsnap_la_SOURCES = snap.c snap_nvme.c snap_virtio_blk.c snap_virtio_common.c
libsnap_la_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS)
libsnap_la_LIBADD = $(IBVERBS_LIBS) -lpthread

