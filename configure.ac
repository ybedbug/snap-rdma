AC_PREREQ([2.69])
AC_INIT([rdma-snap], [0.1], [support@mellanox.com])
AC_CONFIG_SRCDIR([src/snap.h])
AC_CONFIG_HEADERS([config.h])

AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AM_PROG_LIBTOOL

# Init libtool
LT_INIT([dlopen shared disable-static])

# Use C not C++
AC_LANG([C])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL

# Checks for header files.
AC_CHECK_HEADER([stdlib.h])

AC_SEARCH_LIBS([pthread_create],[pthread])
AC_SEARCH_LIBS([rdma_establish],
	        [librdmacm],
		[AC_DEFINE([HAVE_RDMA_CM_EXTERNAL_QP_SUPPORT], 1, [rdma cm has support for external qp])],
		[AC_MSG_WARN([RDMA CM does not support external QP.])])

AC_CONFIG_FILES([Makefile
		 src/Makefile])

AC_OUTPUT
