#
# Copyright (C) Mellanox Technologies Ltd. 2020.  ALL RIGHTS RESERVED.
#
# See file LICENSE for terms.
#

AC_ARG_WITH([gtest],
        [AC_HELP_STRING([--with-gtest(=DIR)],
            [Build googletest unit tests. Googletest should be installed in the DIR])],
        [],
        [with_gtest="no"])

AS_IF([test "x$with_gtest" == "xyes"], [with_gtest=/usr])
AS_IF([test -d "$with_gtest"], 
		[gtest_app=yes; str="with googletest support from $with_gtest"],
		[gtest_app=no; str="without googletest support"])
AS_IF([test -d "$with_gtest/lib64"],[libsuff="64"],[libsuff=""])

AC_MSG_NOTICE([Compiling $str])

AC_PROG_CXX
AC_LANG_PUSH([C++])

dnl Check that we can find GTEST headers, setup GTEST_LDFLAGS and GTEST_CXXFLAGS
dnl assume that if there is a valid gtest.h then there is a valid lib
dnl TODO: improve check by trying to compile and run simple unit test

AS_IF([test "x$gtest_app" == xyes],
        [
        save_CXXFLAGS="$CXXFLAGS"
        save_CPPFLAGS="$CPPFLAGS"
        AS_IF([test x/usr == "x$with_gtest"],
          [],
          [gtest_incl="-std=c++11 -I$with_gtest/include"
           gtest_libs="-L$with_gtest/lib$libsuff"])
        CXXFLAGS="$gtest_incl $CXXFLAGS"
        CPPFLAGS="$gtest_incl $CPFLAGS"

        AC_CHECK_HEADER([gtest/gtest.h],
		[
		AC_SUBST(GTEST_LDFLAGS,  ["$gtest_libs -lgtest"])
		AC_SUBST(GTEST_DIR,      ["$with_gtest"])
		AC_SUBST(GTEST_CXXFLAGS, ["$gtest_incl"])
		],
		[AC_MSG_WARN([gtest header file not found]); gtest_app=no])
        CXXFLAGS="$save_CXXFLAGS"
        CPPFLAGS="$save_CPPFLAGS"
        ],[:])

AC_LANG_POP([C++])

AM_CONDITIONAL([HAVE_GTEST], [test "x$gtest_app" != xno])
