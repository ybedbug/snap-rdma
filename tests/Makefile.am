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
		snap_sample_uio_driver \
		snap_rpc_client_open_close \
		snap_open_close_channel \
		snap_live_migration_cmd_test


LOCAL_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS) -I$(top_srcdir)/src
SNAP_TEST_FILES = snap_test.h snap_test.c

snap_open_close_CFLAGS = $(LOCAL_CFLAGS)
snap_open_close_SOURCES = $(SNAP_TEST_FILES) snap_open_close.c
snap_open_close_LDADD = $(IBVERBS_LIBS) $(top_builddir)/src/libsnap.la

snap_open_close_device_CFLAGS = $(LOCAL_CFLAGS)
snap_open_close_device_SOURCES = $(SNAP_TEST_FILES) snap_open_close_device.c
snap_open_close_device_LDADD = $(IBVERBS_LIBS) $(top_builddir)/src/libsnap.la

snap_query_pfs_CFLAGS = $(LOCAL_CFLAGS)
snap_query_pfs_SOURCES = $(SNAP_TEST_FILES) snap_query_pfs.c
snap_query_pfs_LDADD = $(IBVERBS_LIBS) $(top_builddir)/src/libsnap.la

snap_init_teardown_device_CFLAGS = $(LOCAL_CFLAGS)
snap_init_teardown_device_SOURCES = $(SNAP_TEST_FILES) snap_init_teardown_device.c
snap_init_teardown_device_LDADD = $(IBVERBS_LIBS) $(top_builddir)/src/libsnap.la

snap_query_nvme_device_CFLAGS = $(LOCAL_CFLAGS)
snap_query_nvme_device_SOURCES = $(SNAP_TEST_FILES) snap_query_nvme_device.c
snap_query_nvme_device_LDADD = $(IBVERBS_LIBS) $(top_builddir)/src/libsnap.la

snap_create_destroy_nvme_namespace_CFLAGS = $(LOCAL_CFLAGS)
snap_create_destroy_nvme_namespace_SOURCES = $(SNAP_TEST_FILES) snap_create_destroy_nvme_namespace.c
snap_create_destroy_nvme_namespace_LDADD = $(IBVERBS_LIBS) $(top_builddir)/src/libsnap.la

snap_create_destroy_nvme_cq_CFLAGS = $(LOCAL_CFLAGS)
snap_create_destroy_nvme_cq_SOURCES = $(SNAP_TEST_FILES) snap_create_destroy_nvme_cq.c
snap_create_destroy_nvme_cq_LDADD = $(IBVERBS_LIBS) $(top_builddir)/src/libsnap.la

snap_create_destroy_nvme_sq_CFLAGS = $(LOCAL_CFLAGS)
snap_create_destroy_nvme_sq_SOURCES = $(SNAP_TEST_FILES) snap_create_destroy_nvme_sq.c
snap_create_destroy_nvme_sq_LDADD = $(IBVERBS_LIBS) $(top_builddir)/src/libsnap.la

snap_query_virtio_device_CFLAGS = $(LOCAL_CFLAGS)
snap_query_virtio_device_SOURCES = $(SNAP_TEST_FILES) snap_query_virtio_device.c
snap_query_virtio_device_LDADD = $(IBVERBS_LIBS) $(top_builddir)/src/libsnap.la

snap_create_destroy_virtio_queue_CFLAGS = $(LOCAL_CFLAGS)
snap_create_destroy_virtio_queue_SOURCES = $(SNAP_TEST_FILES) snap_create_destroy_virtio_queue.c
snap_create_destroy_virtio_queue_LDADD = $(IBVERBS_LIBS) $(top_builddir)/src/libsnap.la

snap_open_close_channel_CFLAGS = $(LOCAL_CFLAGS)
snap_open_close_channel_SOURCES = $(SNAP_TEST_FILES) snap_open_close_channel.c
snap_open_close_channel_LDADD = $(IBVERBS_LIBS) $(top_builddir)/src/libsnap.la

snap_live_migration_cmd_test_CFLAGS = $(LOCAL_CFLAGS)
snap_live_migration_cmd_test_SOURCES = $(SNAP_TEST_FILES) \
				       snap_live_migration_client.h \
				       snap_live_migration_client.c \
				       snap_live_migration_cmd_test.c
snap_live_migration_cmd_test_LDADD = $(IBVERBS_LIBS) $(top_builddir)/src/libsnap.la

#cant use $(top_srcdir) here because of bug in configure which does not parse
#variables to make foo.Po files. TODO: consider changing blk to .la
BLK_FILES = ../blk/snap_null_blk_dev.c \
	    ../blk/snap_blk_dev.c \
	    ../blk/snap_blk_dev.h

FS_FILES = ../fs/snap_fsd_dev.c \
	   ../fs/snap_fs_dev.c \
	   ../fs/snap_fs_dev.h \
	   ../fs/snap_fsd_dev.h

snap_create_destroy_virtio_ctrl_CFLAGS = $(LOCAL_CFLAGS) \
					 -I$(top_srcdir)/ctrl \
					 -I$(top_srcdir)/blk \
					 -I$(top_srcdir)/fs
snap_create_destroy_virtio_ctrl_SOURCES = $(SNAP_TEST_FILES) snap_create_destroy_virtio_ctrl.c \
					  $(BLK_FILES) \
					  $(FS_FILES)
snap_create_destroy_virtio_ctrl_LDADD = $(IBVERBS_LIBS) \
					$(top_builddir)/ctrl/libsnap-virtio-net-ctrl.la \
                                        $(top_builddir)/ctrl/libsnap-virtio-blk-ctrl.la \
					$(top_builddir)/ctrl/libsnap-virtio-fs-ctrl.la \
				        $(top_builddir)/src/libsnap.la

snap_create_destroy_nvme_ctrl_CFLAGS = $(LOCAL_CFLAGS) \
					-I$(top_srcdir)/ctrl
snap_create_destroy_nvme_ctrl_SOURCES = $(SNAP_TEST_FILES) snap_create_destroy_nvme_ctrl.c
					  $(SNAP_TEST_FILES)
snap_create_destroy_nvme_ctrl_LDADD = $(IBVERBS_LIBS) \
				      $(top_builddir)/ctrl/libsnap-nvme-ctrl.la \
				      $(top_builddir)/src/libsnap.la

snap_rpc_client_open_close_CFLAGS = $(LOCAL_CFLAGS) \
					-I$(top_srcdir)/rpc
snap_rpc_client_open_close_SOURCES = snap_rpc_client_open_close.c
snap_rpc_client_open_close_LDADD = $(top_builddir)/rpc/libsnap-json-rpc-client.la

UIO_FILES = host_uio.h host_uio.c

snap_sample_device_CFLAGS = $(LOCAL_CFLAGS)
snap_sample_device_SOURCES = snap_sample_device.c \
			     snap_sample_device.h
snap_sample_device_LDADD  = $(IBVERBS_LIBS) $(top_builddir)/src/libsnap.la

snap_sample_uio_driver_CFLAGS = $(LOCAL_CFLAGS)
snap_sample_uio_driver_SOURCES = snap_sample_uio_driver.c \
				 snap_sample_device.h \
				 $(UIO_FILES)

pkglib_LTLIBRARIES = sample_channel.la

sample_channel_la_SOURCES = sample_channel.c
sample_channel_la_CFLAGS = $(LOCAL_CFLAGS)
sample_channel_la_LDFLAGS =  -avoid-version -module -shared -export-dynamic

if HAVE_GTEST
noinst_PROGRAMS += gtest_snap_rdma

gtest_snap_rdma_CXXFLAGS = $(LOCAL_CFLAGS) $(GTEST_CXXFLAGS)
gtest_snap_rdma_CFLAGS = $(LOCAL_CFLAGS)
gtest_snap_rdma_SOURCES = gtest_example.cc \
			  test_snap_dma.h \
			  test_snap_dma.cc \
			  tests_common.h \
			  tests_common.cc \
			  test_sample_channel.cc \
			  $(UIO_FILES)

gtest_snap_rdma_LDFLAGS = $(IBVERBS_LIBS) $(GTEST_LDFLAGS) -lgtest_main $(IBVERBS_LDFLAGS)
gtest_snap_rdma_LDADD = \
			$(top_builddir)/src/libsnap.la

if HAVE_FLEXIO
noinst_PROGRAMS += gtest_snap_dpa

gtest_snap_dpa_SOURCES = \
			 tests_common.h \
			 tests_common.cc \
			 test_snap_dpa.cc

gtest_snap_dpa_CXXFLAGS = $(FLEXIO_CFLAGS) $(LOCAL_CFLAGS) $(GTEST_CXXFLAGS) -fpermissive
gtest_snap_dpa_LDFLAGS = $(FLEXIO_LDFLAGS) $(IBVERBS_LIBS) $(GTEST_LDFLAGS) -lgtest_main $(IBVERBS_LDFLAGS)
gtest_snap_dpa_LDADD = $(top_builddir)/src/libsnap.la
endif
endif
