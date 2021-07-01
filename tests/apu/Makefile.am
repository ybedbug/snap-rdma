ACLOCAL_AMFLAGS = -I m4

LOCAL_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS) -I$(top_srcdir)/src -I$(top_srcdir)/apu

if HAVE_FLEXIO

noinst_PROGRAMS = hello_flexio_host

hello_flexio_host_SOURCES = hello_flexio.c \
			    com_flow_host.c \
			    com_flow_host.h \
			    com_host.h
hello_flexio_host_CFLAGS = $(FLEXIO_CFLAGS) $(LOCAL_CFLAGS)
hello_flexio_host_LDFLAGS = $(FLEXIO_LDFLAGS)

endif
