dnl AC_CONFIG_AUX_DIR(./scripts)
AC_INIT
0>confdefs.h

AC_ARG_WITH(libxmp, [  --with-libxmp=<path>    libxmp prefix (optional)],
  libxmp_path="$withval")

AC_PROG_CC

if test "$libxmp_path" != ""; then
  CPPFLAGS="${CPPFLAGS} -I${libxmp_path}/include"
  LIBS="${LIBS} -L${libxmp_path}/lib"
fi

AC_CHECK_HEADER(xmp.h)
if test "${ac_cv_header_xmp_h}" = "yes"; then
  AC_CHECK_LIB(xmp,xmp_player_start,,
    AC_MSG_ERROR(Can't find libxmp))
else
  AC_MSG_ERROR(Can't find libxmp header files)
fi

PKG_CHECK_MODULES([audacious], [audacious])
AS_ECHO_N("checking audacious version... ")
pkg-config audacious --modversion

AC_PROG_INSTALL
AC_CONFIG_FILES([Makefile])
AC_OUTPUT
