#ifndef DSMCC_BIOP_MESSAGE_H
#define DSMCC_BIOP_MESSAGE_H

#include "dsmcc-biop-ior.h"

/* from dsmcc-cache-file.h */
struct dsmcc_file_cache;

struct biop_name
{
	char *id;
	char *kind;
};

struct biop_binding
{
	uint8_t          binding_type;
	struct biop_name name;
	struct biop_ior  ior;
};

int dsmcc_biop_parse_data(struct dsmcc_file_cache *cache, uint16_t module_id, const char *module_file, int length);

#endif
