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
	    -DE_MODE_LE -DUSER_SPACE -I$(FLEXIO_DIR)

AM_LDFLAGS = -fPIE -fpie -flto -pie -static -nostartfiles \
	     -T flexio_linker.ld -L$(FLEXIO_DIR)/libflexio-dev/lib

noinst_PROGRAMS += dpa_hello hello_flexio_dev

dpa_hello_SOURCES = dpa_hello.c

hello_flexio_dev_SOURCES = hello_flexio_hw.c \
			   com_dev.c \
			   com_dev.h \
			   com_context_dev.h
# example that link can be overriden per target but if you do it target 
# LDFLAGS are lost
hello_flexio_dev_LINK = $(LINK)

endif
