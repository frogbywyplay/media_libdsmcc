#ifndef DSMCC_BIOP_MESSAGE_H
#define DSMCC_BIOP_MESSAGE_H

/* from dsmcc-cache-file.h */
struct dsmcc_file_cache;

/* from dsmcc-cache-module.h */
struct dsmcc_module_id;

int dsmcc_biop_parse_data(struct dsmcc_file_cache *cache, struct dsmcc_module_id *module_id, const char *module_file, int length);

#endif
