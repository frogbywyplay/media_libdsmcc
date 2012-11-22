#ifndef DSMCC_SECTION_H
#define DSMCC_SECTION_H

#include <stdint.h>

struct dsmcc_ddb
{
	uint32_t download_id;
	uint16_t module_id;
	uint8_t  module_version;
	uint16_t number;
	uint16_t length;
};

#endif /* DSMCC_SECTION_H */
