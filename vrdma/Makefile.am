ACLOCAL_AMFLAGS = -I m4

lib_LTLIBRARIES = libsnap-vrdma.la

SNAP_INCLUDE = -I$(top_srcdir)/src

libsnap_vrdma_ladir = $(includedir)/
libsnap_vrdma_la_HEADERS = snap_vrdma_admq.h \
				     snap_vrdma_ctrl.h \
				     snap_vrdma_srv.h

libsnap_vrdma_la_SOURCES = snap_vrdma_admq.c \
				     snap_vrdma_ctrl.c \
				     snap_vrdma_srv.c

libsnap_vrdma_la_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS) $(SNAP_INCLUDE)
libsnap_vrdma_la_LIBADD = $(IBVERBS_LIBS) $(top_builddir)/src/libsnap.la -lpthread
