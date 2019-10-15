
lib_LIBRARIES = librdma_snap.a libmlx5_snap.a

librdma_snap_adir = $(includedir)/rdma-snap

librdma_snap_a_HEADERS = \
				snap.h
				queue.h

librdma_snap_a_SOURCES = \
				snap.c

librdma_snap_a_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS)

libmlx5_snap_adir = $(includedir)/rdma-snap

libmlx5_snap_a_HEADERS = \
				mlx5_snap.h
				snap.h

libmlx5_snap_a_SOURCES = \
				mlx5_snap.c

libmlx5_snap_a_CFLAGS = $(BASE_CFLAGS) $(IBVERBS_CFLAGS)
