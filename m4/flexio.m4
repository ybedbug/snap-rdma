#
# Copyright (C) 2021 NVIDIA CORPORATION & AFFILIATES
#
# See file LICENSE for terms.
#

AC_ARG_WITH([flexio], [
	AS_HELP_STRING([--with-flexio=DIR],
			[Use FLEX IO SDK in DIR to build DPA support])
	], [], [with_flexio="no"])

with_dpa_cc="${DPA_CC:-riscv64-unknown-elf-gcc}"

AS_IF([test -d "$with_flexio"],
	[flexio_app=yes; str="with FLEX IO SDK support from $with_flexio, DPA_CC=$with_dpa_cc"],
	[flexio_app=no; str="without FLEX IO SDK support"])

AC_MSG_NOTICE([Compiling $str])

AS_IF([test "x$flexio_app" == xyes], [
	save_CFLAGS="$CFLAGS"
	save_CPPFLAGS="$CPPFLAGS"

	flexio_incl="-I$with_flexio"
	flexio_libs="-L$with_flexio/libflexio/lib"

	CFLAGS="$flexio_incl $CFLAGS"
	CPPFLAGS="$flexio_incl $CFLAGS"

	AC_CHECK_HEADER([libflexio/flexio.h], [
		AC_SUBST(FLEXIO_LDFLAGS,  ["$flexio_libs -lflexio -libverbs -lmlx5"])
		AC_SUBST(FLEXIO_DIR,      ["$with_flexio"])
		AC_SUBST(FLEXIO_CFLAGS,   ["$flexio_incl"])
		AC_DEFINE([HAVE_FLEXIO], 1, [FLEX IO SDK support/DPA])

		AC_CHECK_PROG([DPA_CC], [$with_dpa_cc], [$with_dpa_cc], [no])
		AS_IF([test "x$DPA_CC" == xno], [
			dnl todo: make it fatal error in the future
			AC_MSG_WARN([FLEX IO SDK cross compiler is not found. DPA support will not be built])
			])
		],
		[AC_MSG_WARN([FLEX IO SDK header file not found]); flexio_app=no])

		CFLAGS="$save_CFLAGS"
		CPPFLAGS="$save_CPPFLAGS"
	],[:])

AM_CONDITIONAL([HAVE_FLEXIO], [test "x$flexio_app" != xno])
AM_CONDITIONAL([HAVE_DPA_CC], [test "x$flexio_app" != xno -a "x$DPA_CC" != xno])
