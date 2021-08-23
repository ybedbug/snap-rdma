ACLOCAL_AMFLAGS = -I m4

# NOTE1: can not easily mix  dpa/host code
# NOTE2: adding program specific CFLAGS or LDFLAGS triggers libtool, which in turn
# removes -static flag and disables LINK and COMPILE for the target
#
COMPILE = $(DPA_CC) $(AM_CFLAGS)
LINK = $(DPA_CC) $(AM_LDFLAGS) -o $@
LIBS = -lapu_dev

noinst_PROGRAMS =

if HAVE_DPA_CC

AM_CFLAGS = $(BASE_CFLAGS) \
	    -fPIC -fpic -mno-relax -mcmodel=medany -march=rv64imac -mabi=lp64 \
	    -DE_MODE_LE -DUSER_SPACE -I../src -I$(FLEXIO_DIR) -I$(FLEXIO_DIR)/libflexio-os/include

AM_LDFLAGS = -fPIE -fpie -flto -pie -static -nostartfiles \
	     -T flexio_linker.ld -L$(FLEXIO_DIR)/libflexio-dev/lib

noinst_PROGRAMS += dpa_hello

dpa_hello_SOURCES = dpa.h dpa_start.c dpa_hello.c

endif
