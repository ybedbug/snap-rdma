ACLOCAL_AMFLAGS = -I m4

lib_LTLIBRARIES = libsnap-virtio-blk-ctrl.la libsnap-virtio-net-ctrl.la libsnap-virtio-fs-ctrl.la libsnap-nvme-ctrl.la

SNAP_INCLUDE = -I$(top_srcdir)/src
BLK_INCLUDE = -I$(top_srcdir)/blk
FS_INCLUDE = -I$(top_srcdir)/fs

#virtio-blk-ctrl
libsnap_virtio_blk_ctrl_ladir = $(includedir)/
libsnap_virtio_blk_ctrl_la_HEADERS = snap_virtio_blk_ctrl.h \
				     snap_virtio_common_ctrl.h \
				     $(top_srcdir)/blk/snap_blk_ops.h \
				     snap_poll_groups.h
libsnap_virtio_blk_ctrl_la_SOURCES = snap_virtio_blk_ctrl.c \
				     snap_virtio_common_ctrl.c \
				     snap_virtio_blk_virtq.c \
				     snap_poll_groups.c
libsnap_virtio_blk_ctrl_la_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS) $(SNAP_INCLUDE) \
				    $(BLK_INCLUDE)
libsnap_virtio_blk_ctrl_la_LIBADD = $(IBVERBS_LIBS) $(top_builddir)/src/libsnap.la -lpthread

#virtio-net-ctrl
libsnap_virtio_net_ctrl_ladir = $(includedir)/
libsnap_virtio_net_ctrl_la_HEADERS = snap_virtio_net_ctrl.h \
				     snap_virtio_common_ctrl.h \
				     snap_virtio_blk_virtq.h \
				     snap_poll_groups.h
libsnap_virtio_net_ctrl_la_SOURCES = snap_virtio_net_ctrl.c \
				     snap_virtio_common_ctrl.c \
				     snap_poll_groups.c
libsnap_virtio_net_ctrl_la_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS) $(SNAP_INCLUDE)
libsnap_virtio_net_ctrl_la_LIBADD = $(IBVERBS_LIBS) $(top_builddir)/src/libsnap.la -lpthread

#virtio-fs-ctrl
libsnap_virtio_fs_ctrl_ladir = $(includedir)/
libsnap_virtio_fs_ctrl_la_HEADERS = snap_virtio_fs_ctrl.h \
				     snap_virtio_common_ctrl.h \
				     snap_virtio_fs_virtq.h \
				     $(top_srcdir)/fs/snap_fs_ops.h \
				     snap_poll_groups.h
libsnap_virtio_fs_ctrl_la_SOURCES = snap_virtio_fs_ctrl.c \
				     snap_virtio_common_ctrl.c \
				     snap_virtio_fs_virtq.c \
				     snap_poll_groups.c
libsnap_virtio_fs_ctrl_la_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS) $(SNAP_INCLUDE) \
				    $(FS_INCLUDE)
libsnap_virtio_fs_ctrl_la_LIBADD = $(IBVERBS_LIBS) $(top_builddir)/src/libsnap.la -lpthread


#nvme-ctrl
libsnap_nvme_ctrl_ladir = $(includedir)/
libsnap_nvme_ctrl_la_HEADERS = snap_nvme_ctrl.h nvme_proto.h nvme_register.h
libsnap_nvme_ctrl_la_SOURCES = snap_nvme_ctrl.c nvme_register.c
libsnap_nvme_ctrl_la_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS) $(SNAP_INCLUDE)
libsnap_nvme_ctrl_la_LIBADD = $(IBVERBS_LIBS) $(top_builddir)/src/libsnap.la -lpthread
