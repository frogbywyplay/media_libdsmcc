#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.60)
AC_INIT([dsmcc], [2.0.0], [dev@wyplay.com])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([src],[tools])
AC_CONFIG_HEADERS([src/dsmcc-config.h])

# Checks for programs.
AC_PROG_CC
AM_PROG_CC_C_O
AM_PROG_LIBTOOL

# Checks for libraries.

# Checks for header files.
AC_CHECK_HEADERS([stdbool.h])

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.

######## More user friendly AC_ARG_WITH macro ########
# $1: label   $2: var name  $3: default value   4: help string
AC_DEFUN([MY_ARG_WITH],
            [AC_ARG_WITH([$1],
               [AS_HELP_STRING([--with-$1(=ARG)], [$4 (default is $3)])],
               [AC_DEFINE_UNQUOTED($2, "$withval", $4)] $2=$withval,
               [AC_DEFINE_UNQUOTED($2, "$3", $4)] $2=$3
                        )
            ]
        )

######## zlib ########
AC_ARG_ENABLE(zlib,
   AC_HELP_STRING([--enable-zlib], [turn on zlib decompression support (default=enabled)]),
   [
      if test x$enableval == xyes ; then
         zlib=true
      else
         zlib=false
      fi
   ],
   [ zlib=true ]
)

if test x$zlib == xtrue ; then
   AC_CHECK_HEADER([zlib.h], [], [ AC_MSG_ERROR([Could not find zlib header file]) ])
   AC_CHECK_LIB([z], [deflate], [ enable_zlib=yes ], [ AC_MSG_ERROR([Could not find zlib library]) ] )
fi
if test x$enable_zlib == xyes ; then
   AM_CONDITIONAL([HAVE_ZLIB], true)
   AC_DEFINE([HAVE_ZLIB], 1, [Support for zlib compression])
   AC_SUBST([ZLIB_LIBS], "-lz")
else
   AM_CONDITIONAL([HAVE_ZLIB], false)
   AC_SUBST([ZLIB_LIBS], [])
fi

######## Debugging ########
AH_TEMPLATE([DEBUG], [Support for debugging.])

AC_ARG_ENABLE(debug,
   AC_HELP_STRING([--enable-debug], [turn on debugging (default=disabled)]),
   [
      if test x$enableval == xyes ; then
         debug=true
      else
         debug=false
      fi
   ],
   [ debug=false ]
)
if test x$debug == xtrue ; then
   AM_CONDITIONAL([DEBUG], true)
   AC_DEFINE([DEBUG])
   CFLAGS="$CFLAGS -g -ggdb -W -Wall -Werror"
   CXXFLAGS=$CFLAGS
else
   AM_CONDITIONAL([DEBUG], false)
   CFLAGS="$CFLAGS -W -Wall -Werror -DNDEBUG"
   CXXFLAGS=$CFLAGS
fi

######## Add input files ########

# REQUIRED_PKG_CONFIG can be used inside dsmcc.pc.in file
AC_SUBST([REQUIRED_PKG_CONFIG], ${REQUIRED_PKG_CONFIG})
AC_CONFIG_FILES([ Makefile Doxyfile include/Makefile src/Makefile tools/Makefile dsmcc.pc ])

######## Summary of configured options  ########
AC_MSG_NOTICE([***** Summary *****])
AC_MSG_NOTICE([DSMCC DEBUG          : $debug])
AC_MSG_NOTICE([DSMCC ZLIB           : $zlib])

AC_OUTPUT

