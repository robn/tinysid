dnl Process this file with autoconf to produce a configure script.
dnl Written in 2000 by Christian Bauer <Christian.Bauer@uni-mainz.de>

AC_INIT
AC_CONFIG_SRCDIR(src/main.cpp)
AC_PREREQ(2.50)

dnl Detect the canonical host and target build environment.
AC_CANONICAL_HOST
AC_CANONICAL_TARGET

dnl This defines PACKAGE and VERSION.
AM_CONFIG_HEADER(config.h)
AM_INIT_AUTOMAKE([SIDPlayer], [4.4])

dnl Checks for programs.
AC_PROG_CXX
AC_PROG_CXXCPP
AC_PROG_INSTALL

dnl Check for SDL.
AM_PATH_SDL(1.2.0, , AC_MSG_ERROR([You need SDL 1.2 to SIDPlayer.]))
CPPFLAGS="$CPPFLAGS $SDL_CFLAGS -DSDL -DSID_PLAYER"
LIBS="$LIBS $SDL_LIBS"

dnl Generate Makefiles.
AC_OUTPUT([
Makefile
SIDPlayer.spec
src/Makefile
src/clamp_includes/Makefile
src/soundplay_includes/Makefile
])

dnl Print summary.
echo "Configuration done. Now type \"make\"."
