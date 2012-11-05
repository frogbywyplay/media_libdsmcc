#ifndef DSMCC_BIOP_MESSAGE_H
#define DSMCC_BIOP_MESSAGE_H

#include "dsmcc-descriptor.h"
#include "dsmcc-biop-ior.h"

/* from dsmcc-cache-file.h */
struct dsmcc_file_cache;

/* from dsmcc-cache-module.h */
struct dsmcc_cached_module;

struct biop_name
{
	unsigned char id_len;
	char         *id;

	unsigned char kind_len;
	char         *kind;
};

struct biop_binding
{
	char binding_type;

	struct biop_name *name;
	struct biop_ior  *ior;
};

struct biop_msg_header
{
	unsigned char version_major;
	unsigned char version_minor;
	unsigned int  message_size;

	unsigned char objkey_len;
	unsigned char*objkey;

	unsigned long  objkind_len;
	unsigned char *objkind;
};

int dsmcc_biop_parse_data(struct dsmcc_file_cache *cache, struct dsmcc_cached_module *module);

#endif
