AC_PREREQ([2.69])
AC_INIT([mlnx-libsnap], [1.4.1], [support@mellanox.com])
AC_CONFIG_SRCDIR([src/snap.h])
AC_CONFIG_HEADERS([config.h])

AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([foreign subdir-objects])
AM_PROG_LIBTOOL

# Init libtool
LT_INIT([shared disable-static])

if test -n "$ghprbPullId" ; then
    AC_SUBST([PACKAGE_REVISION], [$(echo pr${ghprbPullId})])
else
    AC_SUBST([PACKAGE_REVISION], [$(echo ${BUILD_NUMBER:-1})])
fi
AC_SUBST([GIT_BRANCH], [$(git rev-parse --abbrev-ref HEAD)])
AC_SUBST([GIT_SHA1], [$(git rev-parse HEAD |cut -c -8)])
dateopt=""
if test -n "$SOURCE_DATE_EPOCH" ; then
    dateopt="-u -d @$SOURCE_DATE_EPOCH"
fi
AC_SUBST([BUILD_DATE_CHANGELOG], [$(date $dateopt +'%a, %d %b %Y %T %z')])

# Use C not C++
AC_LANG([C])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL

# Checks for header files.
AC_CHECK_HEADER([stdlib.h])
AC_CHECK_HEADER([sys/queue.h],
                [],
                [AC_MSG_ERROR("Cannot find sys/queue.h needed for compilation")])
AC_CHECK_HEADERS([linux/virtio_fs.h])

AC_SEARCH_LIBS([pthread_create],[pthread])

# check for IB
m4_include([m4/ib.m4])

# DPA/FLEX IO SDK
m4_include([m4/flexio.m4])

# configure gtest
m4_include([m4/gtest.m4])

AC_PREFIX_DEFAULT(/usr/local/snap)

# debug mode

BASE_CFLAGS="-g -O2 -Wall -Werror"
#BASE_CFLAGS="-g -O2"

AC_ARG_ENABLE(debug,
	      AC_HELP_STRING([--enable-debug], [Enable debug mode build]),
	      [],
	      [enable_debug=no])
AS_IF([test "x$enable_debug" = xyes],
      [dbg=1],
      [dbg=0])
BASE_CFLAGS="-DSNAP_DEBUG=$dbg $BASE_CFLAGS"
AC_SUBST([BASE_CFLAGS], [$BASE_CFLAGS])

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 dpa/Makefile
                 ctrl/Makefile
                 rpc/Makefile
                 tests/Makefile])

if test ! -e debian/changelog; then
    AC_CONFIG_FILES([debian/changelog])
fi

if test ! -e mlnx-libsnap.spec; then
    AC_CONFIG_FILES([mlnx-libsnap.spec])
fi

AC_OUTPUT
