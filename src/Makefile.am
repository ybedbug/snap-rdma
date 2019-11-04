ACLOCAL_AMFLAGS = -I m4

lib_LTLIBRARIES = libsnap.la libsnap_nvme.la

noinst_HEADERS = mlx5_snap.h mlx5_ifc.h queue.h

#snap lib
libsnap_ladir = $(includedir)/snap-rdma
libsnap_la_HEADERS = snap.h
libsnap_la_SOURCES = snap.c
libsnap_la_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS)
libsnap_la_LIBADD = $(IBVERBS_LIBS) -lpthread

#snap_nvme lib
libsnap_nvme_ladir = $(includedir)/snap-rdma
libsnap_nvme_la_HEADERS = snap_nvme.h
libsnap_nvme_la_SOURCES = snap_nvme.c
libsnap_nvme_la_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS)
libsnap_nvme_la_LIBADD = $(IBVERBS_LIBS) -lpthread libsnap.la
