ACLOCAL_AMFLAGS = -I m4

noinst_PROGRAMS = \
		snap_open_close \
		snap_open_close_device \
		snap_query_pfs \
		snap_init_teardown_device \
		snap_query_nvme_device \
		snap_create_destroy_nvme_namespace \
		snap_create_destroy_nvme_cq \
		snap_create_destroy_nvme_sq \
		snap_create_destroy_nvme_ctrl \
		snap_query_virtio_device \
		snap_create_destroy_virtio_queue \
		snap_create_destroy_virtio_ctrl \
		snap_sample_device \
		snap_sample_uio_driver

LOCAL_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS) -I$(top_srcdir)/src
SNAP_TEST_FILES = snap_test.h snap_test.c

snap_open_close_CFLAGS = $(LOCAL_CFLAGS)
snap_open_close_SOURCES = $(SNAP_TEST_FILES) snap_open_close.c
snap_open_close_LDADD = $(top_builddir)/src/libsnap.la

snap_open_close_device_CFLAGS = $(LOCAL_CFLAGS)
snap_open_close_device_SOURCES = $(SNAP_TEST_FILES) snap_open_close_device.c
snap_open_close_device_LDADD = $(top_builddir)/src/libsnap.la

snap_query_pfs_CFLAGS = $(LOCAL_CFLAGS)
snap_query_pfs_SOURCES = $(SNAP_TEST_FILES) snap_query_pfs.c
snap_query_pfs_LDADD = $(top_builddir)/src/libsnap.la

snap_init_teardown_device_CFLAGS = $(LOCAL_CFLAGS)
snap_init_teardown_device_SOURCES = $(SNAP_TEST_FILES) snap_init_teardown_device.c
snap_init_teardown_device_LDADD = $(top_builddir)/src/libsnap.la

snap_query_nvme_device_CFLAGS = $(LOCAL_CFLAGS)
snap_query_nvme_device_SOURCES = $(SNAP_TEST_FILES) snap_query_nvme_device.c
snap_query_nvme_device_LDADD = $(top_builddir)/src/libsnap.la

snap_create_destroy_nvme_namespace_CFLAGS = $(LOCAL_CFLAGS)
snap_create_destroy_nvme_namespace_SOURCES = $(SNAP_TEST_FILES) snap_create_destroy_nvme_namespace.c
snap_create_destroy_nvme_namespace_LDADD = $(top_builddir)/src/libsnap.la

snap_create_destroy_nvme_cq_CFLAGS = $(LOCAL_CFLAGS)
snap_create_destroy_nvme_cq_SOURCES = $(SNAP_TEST_FILES) snap_create_destroy_nvme_cq.c
snap_create_destroy_nvme_cq_LDADD = $(top_builddir)/src/libsnap.la

snap_create_destroy_nvme_sq_CFLAGS = $(LOCAL_CFLAGS)
snap_create_destroy_nvme_sq_SOURCES = $(SNAP_TEST_FILES) snap_create_destroy_nvme_sq.c
snap_create_destroy_nvme_sq_LDADD = $(top_builddir)/src/libsnap.la

snap_query_virtio_device_CFLAGS = $(LOCAL_CFLAGS)
snap_query_virtio_device_SOURCES = $(SNAP_TEST_FILES) snap_query_virtio_device.c
snap_query_virtio_device_LDADD = $(top_builddir)/src/libsnap.la

snap_create_destroy_virtio_queue_CFLAGS = $(LOCAL_CFLAGS)
snap_create_destroy_virtio_queue_SOURCES = $(SNAP_TEST_FILES) snap_create_destroy_virtio_queue.c
snap_create_destroy_virtio_queue_LDADD = $(top_builddir)/src/libsnap.la

#cant use $(top_srcdir) here because of bug in configure which does not parse
#variables to make foo.Po files. TODO: consider changing blk to .la
BLK_FILES = ../blk/snap_null_blk_dev.c \
	    ../blk/snap_blk_dev.c \
	    ../blk/snap_blk_dev.h

snap_create_destroy_virtio_ctrl_CFLAGS = $(LOCAL_CFLAGS) \
					 -I$(top_srcdir)/ctrl \
					 -I$(top_srcdir)/blk
snap_create_destroy_virtio_ctrl_SOURCES = snap_create_destroy_virtio_ctrl.c \
					  $(BLK_FILES)
snap_create_destroy_virtio_ctrl_LDADD = $(top_builddir)/ctrl/libsnap-virtio-net-ctrl.la \
                                        $(top_builddir)/ctrl/libsnap-virtio-blk-ctrl.la

snap_create_destroy_nvme_ctrl_CFLAGS = $(LOCAL_CFLAGS) \
					-I$(top_srcdir)/ctrl
snap_create_destroy_nvme_ctrl_SOURCES = $(SNAP_TEST_FILES) snap_create_destroy_nvme_ctrl.c
					  $(SNAP_TEST_FILES)
snap_create_destroy_nvme_ctrl_LDADD = $(top_builddir)/ctrl/libsnap-nvme-ctrl.la

UIO_FILES = host_uio.h host_uio.c

snap_sample_device_CFLAGS = $(LOCAL_CFLAGS)
snap_sample_device_SOURCES = snap_sample_device.c \
			     snap_sample_device.h
snap_sample_device_LDADD  = $(top_builddir)/src/libsnap.la

snap_sample_uio_driver_CFLAGS = $(LOCAL_CFLAGS)
snap_sample_uio_driver_SOURCES = snap_sample_uio_driver.c \
				 snap_sample_device.h \
				 $(UIO_FILES)

if HAVE_GTEST
noinst_PROGRAMS += gtest_snap_rdma

gtest_snap_rdma_CXXFLAGS = $(LOCAL_CFLAGS) \
			   $(GTEST_CXXFLAGS) \
			   -I$(top_srcdir)/src
gtest_snap_rdma_CFLAGS = $(LOCAL_CFLAGS) \
			 -I$(top_srcdir)/src
gtest_snap_rdma_SOURCES = gtest_example.cc \
			  test_snap_dma.h \
			  test_snap_dma.cc \
			  tests_common.h \
			  tests_common.cc \
			  $(UIO_FILES)

gtest_snap_rdma_LDFLAGS = $(GTEST_LDFLAGS) -lgtest_main $(IBVERBS_LDFLAGS)
gtest_snap_rdma_LDADD = \
			$(top_builddir)/src/libsnap.la
endif
