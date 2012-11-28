#ifndef DSMCC_CACHE_MODULE_H
#define DSMCC_CACHE_MODULE_H

#include <stdint.h>

/* from dsmcc-carousel.h */
struct dsmcc_object_carousel;

struct dsmcc_module_id
{
	uint32_t download_id;
	uint16_t module_id;
	uint8_t  module_version;
};

struct dsmcc_module_info
{
	uint32_t module_size;
	uint32_t block_size;

	bool     compressed;
	uint8_t  compress_method;
	uint32_t uncompressed_size;
};

void dsmcc_cache_add_module_info(struct dsmcc_object_carousel *carousel, struct dsmcc_module_id *module_id, struct dsmcc_module_info *module_info);
void dsmcc_cache_save_module_data(struct dsmcc_object_carousel *carousel, struct dsmcc_module_id *module_id, uint16_t block_number, uint8_t *data, int length);
void dsmcc_cache_free_all_modules(struct dsmcc_object_carousel *carousel);

#endif /* DSMCC_CACHE_MODULE_H */
