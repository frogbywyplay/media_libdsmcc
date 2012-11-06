dsmcclibdir = $(libdir)
libdsmcc_sodir = $(includedir)/dsmcc
dsmcclib_PROGRAMS = libdsmcc.so
libdsmcc_so_LDFLAGS = -shared -Wl,--version-script,libdsmcc.map
AM_CFLAGS = \
	-fPIC \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/src

noinst_HEADERS = \
	$(top_srcdir)/src/dsmcc-biop-ior.h \
	$(top_srcdir)/src/dsmcc-biop-message.h \
	$(top_srcdir)/src/dsmcc-biop-module.h \
	$(top_srcdir)/src/dsmcc-biop-tap.h \
	$(top_srcdir)/src/dsmcc-cache-file.h \
	$(top_srcdir)/src/dsmcc-cache-module.h \
	$(top_srcdir)/src/dsmcc-carousel.h \
	$(top_srcdir)/src/dsmcc-debug.h \
	$(top_srcdir)/src/dsmcc-descriptor.h \
	$(top_srcdir)/src/dsmcc.h \
	$(top_srcdir)/src/dsmcc-section.h \
	$(top_srcdir)/src/dsmcc-ts.h \
	$(top_srcdir)/src/dsmcc-util.h

libdsmcc_so_HEADERS = \
	$(top_srcdir)/include/dsmcc/dsmcc.h

libdsmcc_so_SOURCES = \
	$(top_srcdir)/src/dsmcc-biop-ior.c \
	$(top_srcdir)/src/dsmcc-biop-tap.c \
	$(top_srcdir)/src/dsmcc-descriptor.c \
	$(top_srcdir)/src/dsmcc-ts.c \
	$(top_srcdir)/src/dsmcc-cache-module.c \
	$(top_srcdir)/src/dsmcc-debug.c \
	$(top_srcdir)/src/dsmcc.c \
	$(top_srcdir)/src/dsmcc-biop-message.c \
	$(top_srcdir)/src/dsmcc-biop-module.c \
	$(top_srcdir)/src/dsmcc-section.c \
	$(top_srcdir)/src/dsmcc-util.c \
	$(top_srcdir)/src/dsmcc-cache-file.c \
	$(top_srcdir)/src/dsmcc-carousel.c
