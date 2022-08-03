ACLOCAL_AMFLAGS = -I m4

# NOTE1: can not easily mix  dpa/host code
# NOTE2: adding program specific CFLAGS or LDFLAGS triggers libtool, which in turn
# removes -static flag and disables LINK and COMPILE for the target
#
COMPILE = $(DPA_CC) $(AM_CFLAGS)
LINK = $(DPA_CC) $(AM_LDFLAGS) -o $@
LIBS = -lflexio_dev -lflexio_os

noinst_PROGRAMS =

if HAVE_DPA_CC

AM_CFLAGS = $(BASE_CFLAGS) \
	    -fPIC -fpic -mno-relax -mcmodel=medany -march=rv64imac -mabi=lp64 \
	    -fno-builtin-fprintf -fno-builtin-printf \
	    -DE_MODE_LE -DUSER_SPACE -D__DPA=1 \
	    -I. -I.. -I$(top_srcdir)/src -I$(FLEXIO_DIR) -I$(FLEXIO_DIR)/include -I$(FLEXIO_DIR)/include/libflexio

AM_LDFLAGS = -fPIE -fpie -flto -pie -static -nostartfiles \
	     -T $(srcdir)/flexio_linker.ld -L$(FLEXIO_DIR)/lib/

noinst_PROGRAMS += dpa_hello \
		   dpa_hello_event \
		   dpa_dma_test \
		   dpa_virtq_split \
		   dpa_rt_test_polling \
		   dpa_rt_test_event \
		   dpa_cmd_lat_bench

noinst_LIBRARIES = libdpa.a

dpa_hello_SOURCES = dpa_hello.c
dpa_hello_LDADD = libdpa.a

dpa_hello_event_SOURCES = dpa_hello_event.c
dpa_hello_event_LDADD = libdpa.a

dpa_dma_test_SOURCES = dpa_dma_test.c
dpa_dma_test_LDADD = libdpa.a

dpa_rt_test_polling_SOURCES = dpa_rt_test_polling.c
dpa_rt_test_polling_LDADD = libdpa.a

dpa_rt_test_event_SOURCES = dpa_rt_test_event.c
dpa_rt_test_event_LDADD = libdpa.a

dpa_cmd_lat_bench_SOURCES = dpa_cmd_lat_bench.c
dpa_cmd_lat_bench_LDADD = libdpa.a

# copy snap_dma datapath sources, we need to compile them both on host and on
# the DPA
BUILT_SOURCES : dpa_snap_dma.c dpa_snap_dma_dv.c dpa_snap_dpa_p2p.c

dpa_snap_dma.c : ../src/snap_dma.c
	cp $(srcdir)/../src/snap_dma.c $@

dpa_snap_dma_dv.c : ../src/snap_dma_dv.c
	cp $(srcdir)/../src/snap_dma_dv.c $@

dpa_snap_dpa_p2p.c : ../src/snap_dpa_p2p.c
	cp $(srcdir)/../src/snap_dpa_p2p.c $@

libdpa_a_SOURCES = dpa.h snap_dma_compat.h dpa_log.h dpa_common.c dpa_start.c
nodist_libdpa_a_SOURCES = dpa_snap_dma.c dpa_snap_dma_dv.c dpa_snap_dpa_p2p.c

dpa_virtq_split_SOURCES = dpa_virtq_split.c
dpa_virtq_split_LDADD = libdpa.a

endif
