#include <stdio.h>
#include <stdarg.h>
#include "dsmcc-debug.h"

void dsmcc_debug(char *format, ...)
{
	fprintf(stderr, "[dsmcc][dbg] ");

	va_list va;
	va_start(va, format);
	vfprintf(stderr, format, va);
	va_end(va);
}

void dsmcc_warn(char *format, ...)
{
	fprintf(stderr, "[dsmcc][warn] ");

	va_list va;
	va_start(va, format);
	vfprintf(stderr, format, va);
	va_end(va);
}

void dsmcc_error(char *format, ...)
{
	fprintf(stderr, "[dsmcc][error] ");

	va_list va;
	va_start(va, format);
	vfprintf(stderr, format, va);
	va_end(va);
}
