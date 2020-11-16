ACLOCAL_AMFLAGS = -I m4

lib_LTLIBRARIES = libsnap.la

noinst_HEADERS = mlx5_ifc.h snap_queue.h

#snap lib
libsnap_ladir = $(includedir)/
libsnap_la_HEADERS = snap.h \
		     snap_nvme.h \
		     snap_virtio_common.h \
		     snap_virtio_blk.h \
		     snap_virtio_net.h \
		     mlx5_snap.h \
		     snap_channel.h \
		     snap_dma.h

libsnap_la_SOURCES = snap.c \
		     snap_nvme.c \
		     snap_virtio_blk.c \
		     snap_virtio_net.c \
		     snap_virtio_common.c \
		     snap_channel.c \
		     snap_dma.c

libsnap_la_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS)
libsnap_la_LIBADD = $(IBVERBS_LIBS) -lpthread

