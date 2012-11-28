#ifndef DSMCC_CACHE_FILE_H
#define DSMCC_CACHE_FILE_H

#include <stdint.h>

/* from dsmcc-carousel.h */
struct dsmcc_object_carousel;

struct dsmcc_file_id
{
	uint16_t module_id;
	uint32_t key;
	uint32_t key_mask;
};

struct dsmcc_file_cache;

void dsmcc_filecache_init(struct dsmcc_object_carousel *carousel, const char *downloadpath, dsmcc_cache_callback_t *cache_callback, void *cache_callback_arg);
void dsmcc_filecache_free(struct dsmcc_object_carousel *carousel);

void dsmcc_filecache_cache_dir_info(struct dsmcc_file_cache *filecache, struct dsmcc_file_id *parent_id, struct dsmcc_file_id *id, const char *name);
void dsmcc_filecache_cache_file_info(struct dsmcc_file_cache *filecache, struct dsmcc_file_id *parent_id, struct dsmcc_file_id *id, const char *name);
void dsmcc_filecache_cache_file(struct dsmcc_file_cache *filecache, struct dsmcc_file_id *id, const char *data_file, int data_offset, int data_length);

void dsmcc_filecache_reset(struct dsmcc_object_carousel *carousel);
void dsmcc_filecache_clean(struct dsmcc_object_carousel *carousel);

#endif /* DSMCC_CACHE_FILE_H */
