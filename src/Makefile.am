dsmcclibdir = $(libdir)
libdsmcc_sodir = $(includedir)/dsmcc
dsmcclib_PROGRAMS = libdsmcc.so
libdsmcc_so_LDFLAGS = -shared -Wl,--version-script,libdsmcc.map
AM_CFLAGS = \
	-fPIC \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/src

noinst_HEADERS = \
	dsmcc-biop-ior.h \
	dsmcc-biop-message.h \
	dsmcc-biop-module.h \
	dsmcc-biop-tap.h \
	dsmcc-cache-file.h \
	dsmcc-cache-module.h \
	dsmcc-carousel.h \
	dsmcc-debug.h \
	dsmcc-descriptor.h \
	dsmcc.h \
	dsmcc-section.h \
	dsmcc-ts.h \
	dsmcc-util.h

libdsmcc_so_HEADERS = \
	../include/dsmcc/dsmcc.h

libdsmcc_so_SOURCES = \
	dsmcc-biop-ior.c \
	dsmcc-biop-tap.c \
	dsmcc-descriptor.c \
	dsmcc-ts.c \
	dsmcc-cache-module.c \
	dsmcc-debug.c \
	dsmcc.c \
	dsmcc-biop-message.c \
	dsmcc-biop-module.c \
	dsmcc-section.c \
	dsmcc-util.c \
	dsmcc-cache-file.c \
	dsmcc-carousel.c
