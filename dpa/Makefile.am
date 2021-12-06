ACLOCAL_AMFLAGS = -I m4

# NOTE1: can not easily mix  dpa/host code
# NOTE2: adding program specific CFLAGS or LDFLAGS triggers libtool, which in turn
# removes -static flag and disables LINK and COMPILE for the target
#
COMPILE = $(DPA_CC) $(AM_CFLAGS)
LINK = $(DPA_CC) $(AM_LDFLAGS) -o $@
LIBS = -lapu_dev -l:flexio_os.a

noinst_PROGRAMS =

if HAVE_DPA_CC

AM_CFLAGS = $(BASE_CFLAGS) \
	    -fPIC -fpic -mno-relax -mcmodel=medany -march=rv64imac -mabi=lp64 \
	    -DE_MODE_LE -DUSER_SPACE -D__DPA=1 \
	    -I. -I.. -I$(top_srcdir)/src -I$(FLEXIO_DIR) -I$(FLEXIO_DIR)/include -I$(FLEXIO_DIR)/include/libflexio

AM_LDFLAGS = -fPIE -fpie -flto -pie -static -nostartfiles \
	     -T $(srcdir)/flexio_linker.ld -L$(FLEXIO_DIR)/lib/

noinst_PROGRAMS += dpa_hello dpa_virtq_split

noinst_LIBRARIES = libdpa_dma.a

dpa_hello_SOURCES = dpa.h dpa_start.c dpa_hello.c

# copy snap_dma datapath sources, we need to compile them both on host and on
# the DPA
BUILT_SOURCES : dpa_snap_dma.c dpa_snap_dma_dv.c

dpa_snap_dma.c : ../src/snap_dma.c
	cp $(srcdir)/../src/snap_dma.c $@

dpa_snap_dma_dv.c : ../src/snap_dma_dv.c
	cp $(srcdir)/../src/snap_dma_dv.c $@

libdpa_dma_a_SOURCES = snap_dma_compat.h
nodist_libdpa_dma_a_SOURCES = dpa_snap_dma.c dpa_snap_dma_dv.c

dpa_virtq_split_SOURCES = dpa.h dpa_start.c dpa_virtq_split.c
dpa_virtq_split_LDADD = libdpa_dma.a

endif
