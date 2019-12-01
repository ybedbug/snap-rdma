ACLOCAL_AMFLAGS = -I m4

lib_LTLIBRARIES = libsnap.la

noinst_HEADERS = mlx5_snap.h mlx5_ifc.h queue.h

#snap lib
libsnap_ladir = $(includedir)/
libsnap_la_HEADERS = snap.h snap_nvme.h
libsnap_la_SOURCES = snap.c snap_nvme.c
libsnap_la_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS)
libsnap_la_LIBADD = $(IBVERBS_LIBS) -lpthread

