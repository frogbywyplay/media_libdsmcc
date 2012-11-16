#ifndef DSMCC_UTIL_H
#define DSMCC_UTIL_H

#include <stdint.h>
#include <sys/types.h>

uint32_t dsmcc_crc32(uint8_t *data, uint32_t len);
uint32_t dsmcc_getlong(uint8_t *data);
uint16_t dsmcc_getshort(uint8_t *data);
void dsmcc_mkdir(const char *name, mode_t mode);

#endif
