#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.60])
AC_INIT([VDF], [0.0.1], [doogle2600@gmail.com])

AM_INIT_AUTOMAKE
AC_CONFIG_MACRO_DIR([m4])

AC_CONFIG_SRCDIR([libvdf/drive.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LIBTOOL
AM_PROG_CC_C_O
AC_PROG_CPP

# Checks for libraries.
LIBCURL_CHECK_CONFIG(yes,, VDF_STREAM="vdf_stream vdf_stream2" , VDF_STREAM="" )
AC_SUBST(VDF_STREAM)

# Checks for header files.
AC_FUNC_ALLOCA
AC_CHECK_HEADERS([arpa/inet.h fcntl.h malloc.h netinet/in.h stddef.h stdint.h stdlib.h string.h strings.h sys/socket.h unistd.h])
AC_C_INLINE

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_INT16_T
AC_TYPE_INT32_T
AC_TYPE_INT64_T
AC_TYPE_INT8_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([gethostbyname inet_ntoa memset socket strcasecmp strchr strcspn strdup strrchr])

dnl CURRENT, REVISION, AGE
dnl - library source changed -> increment REVISION
dnl - interfaces added/removed/changed -> increment CURRENT, REVISION = 0
dnl - interfaces added -> increment AGE
dnl - interfaces removed -> AGE = 0
VDF_LIBVERSION="0:0:0"
AC_SUBST(VDF_LIBVERSION)

AC_SUBST(VDF_LIBS)
AC_SUBST(VDF_CFLAGS)
VDF_LIBS="\$(top_builddir)/libvdf/libvdf.la"
VDF_CFLAGS="-I\$(top_srcdir)/include"

pkgconfigdir="\$(libdir)/pkgconfig"
AC_SUBST(pkgconfigdir)

AC_CANONICAL_HOST

case "$host_os" in
mingw32* | windows*)
	MPD_LIBS="$MPD_LIBS -lws2_32"
	;;
esac

AC_SYS_LARGEFILE

AC_CONFIG_FILES([Makefile
				 libvdf/Makefile
				 examples/Makefile
				 libvdf.pc
				 ])
AC_OUTPUT
