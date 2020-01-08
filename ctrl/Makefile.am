ACLOCAL_AMFLAGS = -I m4

lib_LTLIBRARIES = libsnap-virtio-blk-ctrl.la libsnap-virtio-net-ctrl.la

SNAP_INCLUDE = -I$(top_srcdir)/src

#libsnap-ctrl lib
libsnap_virtio_blk_ctrl_ladir = $(includedir)/
libsnap_virtio_blk_ctrl_la_HEADERS = snap_virtio_blk_ctrl.h snap_virtio_common_ctrl.h
libsnap_virtio_blk_ctrl_la_SOURCES = snap_virtio_blk_ctrl.c snap_virtio_common_ctrl.c
libsnap_virtio_blk_ctrl_la_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS) $(SNAP_INCLUDE)
libsnap_virtio_blk_ctrl_la_LIBADD = $(IBVERBS_LIBS) $(top_builddir)/src/libsnap.la -lpthread

libsnap_virtio_net_ctrl_ladir = $(includedir)/
libsnap_virtio_net_ctrl_la_HEADERS = snap_virtio_net_ctrl.h snap_virtio_common_ctrl.h
libsnap_virtio_net_ctrl_la_SOURCES = snap_virtio_net_ctrl.c snap_virtio_common_ctrl.c
libsnap_virtio_net_ctrl_la_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS) $(SNAP_INCLUDE)
libsnap_virtio_net_ctrl_la_LIBADD = $(IBVERBS_LIBS) $(top_builddir)/src/libsnap.la -lpthread
