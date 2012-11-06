SUBDIRS = src tools

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = dsmcc.pc

# Documentation 

.PHONY: doc

doc: $(top_srcdir)/include/dsmcc/dsmcc.h
	doxygen Doxyfile
