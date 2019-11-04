AC_PREREQ([2.69])
AC_INIT([snap-rdma], [0.1], [support@mellanox.com])
AC_CONFIG_SRCDIR([src/snap.h])
AC_CONFIG_HEADERS([config.h])

AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AM_PROG_LIBTOOL

# Init libtool
LT_INIT([shared disable-static])

# Use C not C++
AC_LANG([C])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL

# Checks for header files.
AC_CHECK_HEADER([stdlib.h])

AC_SEARCH_LIBS([pthread_create],[pthread])

# check for IB
m4_include([m4/ib.m4])

AC_CONFIG_FILES([Makefile
		 src/Makefile
		 tests/Makefile])

AC_OUTPUT
