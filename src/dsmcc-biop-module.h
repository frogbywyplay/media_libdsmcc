#ifndef DSMCC_BIOP_MODULE_H
#define DSMCC_BIOP_MODULE_H

#include "dsmcc-biop-ior.h"

/* from dsmcc-descriptor.h */
struct dsmcc_descriptor;

struct biop_module_info
{
	unsigned long mod_timeout;
	unsigned long block_timeout;
	unsigned long min_blocktime;

	unsigned short assoc_tag;

	struct dsmcc_descriptor *descriptors;
};

int dsmcc_biop_parse_module_info(struct biop_module_info *module, unsigned char *data, int data_length);
void dsmcc_biop_free_module_info(struct biop_module_info *module);

#endif
