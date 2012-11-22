#ifndef DSMCC_CACHE_MODULE_H
#define DSMCC_CACHE_MODULE_H

#include <stdint.h>

/* from dsmcc-carousel.h */
struct dsmcc_object_carousel;

/* from dsmcc-section.h */
struct dsmcc_ddb;

struct dsmcc_cached_module_info
{
	uint32_t download_id;
	uint16_t module_id;
	uint8_t  module_version;

	uint32_t module_size;
	uint32_t block_size;

	bool     compressed;
	uint8_t  compress_method;
	uint32_t uncompressed_size;

	uint16_t ddb_assoc_tag;
};

void dsmcc_cached_module_add_info(struct dsmcc_state *state, struct dsmcc_object_carousel *carousel, struct dsmcc_cached_module_info *module_info);
void dsmcc_cached_module_save_data(struct dsmcc_object_carousel *carousel, struct dsmcc_ddb *ddb, uint8_t *data, int data_length);
void dsmcc_cached_module_free_all(struct dsmcc_object_carousel *carousel);
bool dsmcc_cached_module_is_complete(struct dsmcc_object_carousel *carousel);

#endif /* DSMCC_CACHE_MODULE_H */
