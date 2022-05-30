ACLOCAL_AMFLAGS = -I m4

lib_LTLIBRARIES = libsnap-env.la libsnap-mr.la libsnap-dma.la libsnap.la

noinst_HEADERS = mlx5_ifc.h snap_queue.h snap_rdma_channel.h snap_internal.h \
		 snap_dpa.h snap_dpa_virtq.h snap_dma_internal.h \
		 snap_sw_virtio_blk.h snap_dpa_p2p.h snap_dpa_rt.h \
		 khash.h

#snap-env lib
libsnap_env_ladir = $(includedir)/
libsnap_env_la_HEADERS = snap_env.h

libsnap_env_la_SOURCES = snap_env.c

libsnap_env_la_CFLAGS = $(BASE_CFLAGS)
libsnap_env_la_LIBADD = -lpthread

#snap-mr lib
libsnap_mr_ladir = $(includedir)/

libsnap_mr_la_HEADERS = snap_mr.h

libsnap_mr_la_SOURCES = snap_mr.c

libsnap_mr_la_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS)
libsnap_mr_la_LIBADD = $(IBVERBS_LIBS) -lpthread

#snap-dma lib
libsnap_dma_ladir = $(includedir)/
libsnap_dma_la_HEADERS = snap_dma.h \
		     snap_dma_stat.h \
		     snap_qp.h \
		     snap_umr.h \
		     snap_macros.h \
		     snap_mb.h

libsnap_dma_la_SOURCES = snap_dma.c \
		     snap_dma_control.c \
		     snap_dma_verbs.c \
		     snap_dma_dv.c \
		     snap_umr.c \
		     snap_qp.c

libsnap_dma_la_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS)
libsnap_dma_la_LIBADD = $(IBVERBS_LIBS) ./libsnap-env.la ./libsnap-mr.la -lpthread

if HAVE_FLEXIO
libsnap_dma_la_CFLAGS += $(FLEXIO_CFLAGS)
libsnap_dma_la_LDFLAGS = $(FLEXIO_LDFLAGS)
endif

#snap lib
libsnap_ladir = $(includedir)/
libsnap_la_HEADERS = snap.h \
		     snap_nvme.h \
		     snap_virtio_common.h \
		     snap_virtio_blk.h \
		     snap_virtio_fs.h \
		     snap_virtio_net.h \
		     mlx5_snap.h \
		     mlx5_ifc.h \
		     snap_channel.h \
		     snap_crypto.h \
		     snap_macros.h \
		     snap_dpa_common.h \
		     snap_mb.h

libsnap_la_SOURCES = snap.c \
		     snap_nvme.c \
		     snap_virtio_blk.c \
		     snap_virtio_fs.c \
		     snap_virtio_net.c \
		     snap_virtio_common.c \
		     snap_rdma_channel.c \
		     snap_channel.c \
		     snap_dpa_virtq.c \
		     snap_sw_virtio_blk.c \
		     snap_crypto.c \
		     snap_dpa.c \
		     snap_dpa_p2p.c \
		     snap_dpa_rt.c

libsnap_la_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS)
libsnap_la_LIBADD = $(IBVERBS_LIBS) ./libsnap-env.la ./libsnap-mr.la ./libsnap-dma.la -lpthread

if HAVE_FLEXIO
libsnap_la_CFLAGS += $(FLEXIO_CFLAGS)
libsnap_la_LDFLAGS = $(FLEXIO_LDFLAGS)
endif
