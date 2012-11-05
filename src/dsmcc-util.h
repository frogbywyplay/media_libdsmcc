#ifndef DSMCC_UTIL_H
#define DSMCC_UTIL_H

#include <sys/types.h>

unsigned long dsmcc_crc32(unsigned char *data, int len);
unsigned long dsmcc_getlong(unsigned char *data);
unsigned short dsmcc_getshort(unsigned char *data);
void dsmcc_mkdir(const char *name, mode_t mode);

#endif
