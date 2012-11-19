#ifndef DSMCC_UTIL_H
#define DSMCC_UTIL_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

uint32_t dsmcc_crc32(uint8_t *data, uint32_t len);
void dsmcc_mkdir(const char *name, mode_t mode);

static inline bool dsmcc_getlong(uint32_t *dst, const uint8_t *data, int offset, int length)
{
	data += offset;
	length -= offset;
	if (length < 4)
		return 0;
	*dst = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
	return 1;
}

static inline bool dsmcc_getshort(uint16_t *dst, const uint8_t *data, int offset, int length)
{
	data += offset;
	length -= offset;
	if (length < 2)
		return 0;
	*dst = (data[0] << 8) | data[1];
	return 1;
}

static inline bool dsmcc_getbyte(uint8_t *dst, const uint8_t *data, int offset, int length)
{
	data += offset;
	length -= offset;
	if (length < 1)
		return 0;
	*dst = data[0];
	return 1;
}

static inline bool dsmcc_strdup(char **dst, int dstlength, const uint8_t *data, int offset, int length)
{
	data += offset;
	length -= offset;
	if (length < dstlength)
		return 0;
	if (dstlength > 0)
	{
		*dst = malloc(dstlength);
		strncpy(*dst, (const char *)data, dstlength);
	}
	else
		*dst = NULL;
	return 1;
}

static inline bool dsmcc_memdup(uint8_t **dst, int dstlength, const uint8_t *data, int offset, int length)
{
	data += offset;
	length -= offset;
	if (length < dstlength)
		return 0;
	if (dstlength > 0)
	{
		*dst = malloc(dstlength);
		memcpy(*dst, data, dstlength);
	}
	else
		*dst = NULL;
	return 1;
}

#endif
