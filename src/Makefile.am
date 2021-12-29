ACLOCAL_AMFLAGS = -I m4

lib_LTLIBRARIES = libsnap.la

noinst_HEADERS = mlx5_ifc.h snap_queue.h snap_rdma_channel.h snap_internal.h \
		 snap_dpa.h snap_dpa_virtq.h snap_dma_internal.h \
		 snap_sw_virtio_blk.h snap_dpa_p2p.h

#snap lib
libsnap_ladir = $(includedir)/
libsnap_la_HEADERS = snap.h \
		     snap_mr.h \
		     snap_nvme.h \
		     snap_virtio_common.h \
		     snap_virtio_blk.h \
		     snap_virtio_fs.h \
		     snap_virtio_net.h \
		     mlx5_snap.h \
		     snap_channel.h \
		     snap_dma.h \
		     snap_umr.h \
		     snap_env.h \
		     snap_sw_virtio_blk.h \
		     snap_crypto.h \
		     snap_macros.h \
		     snap_qp.h \
		     snap_dpa_common.h

libsnap_la_SOURCES = snap.c \
		     snap_mr.c \
		     snap_nvme.c \
		     snap_virtio_blk.c \
		     snap_virtio_fs.c \
		     snap_virtio_net.c \
		     snap_virtio_common.c \
		     snap_dma.c \
		     snap_dma_control.c \
		     snap_dma_verbs.c \
		     snap_dma_dv.c \
		     snap_umr.c \
		     snap_rdma_channel.c \
		     snap_channel.c \
		     snap_env.c \
		     snap_dpa_virtq.c \
		     snap_sw_virtio_blk.c \
		     snap_crypto.c \
		     snap_qp.c \
		     snap_dpa.c \
		     snap_dpa_p2p.c

libsnap_la_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS)
libsnap_la_LIBADD = $(IBVERBS_LIBS) -lpthread

if HAVE_FLEXIO
libsnap_la_CFLAGS += $(FLEXIO_CFLAGS)
libsnap_la_LDFLAGS = $(FLEXIO_LDFLAGS)
endif
