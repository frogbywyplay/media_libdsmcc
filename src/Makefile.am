lib_LTLIBRARIES = libdsmcc.la
libdsmcc_la_SOURCES = \
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

AM_CFLAGS = \
	-Werror \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/src
