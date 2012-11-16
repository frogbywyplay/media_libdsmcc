#ifndef DSMCC_SECTION_H
#define DSMCC_SECTION_H

#include <stdint.h>

struct dsmcc_dii
{
	uint32_t download_id;
	uint16_t block_size;
};

struct dsmcc_module_info
{
	uint16_t module_id;
	uint32_t module_size;
	uint8_t  module_version;
};

struct dsmcc_ddb
{
	uint16_t module_id;
	uint8_t  module_version;
	uint16_t number;
	uint16_t length;
};

#endif /* DSMCC_SECTION_H */
