#ifndef DSMCC_CACHE_FILE_H
#define DSMCC_CACHE_FILE_H

#include <stdint.h>

/* from dsmcc-carousel.h */
struct dsmcc_object_carousel;

/* from dsmcc-biop-message.h */
struct biop_binding;

struct dsmcc_file_cache;

void dsmcc_filecache_init(struct dsmcc_object_carousel *car, const char *downloadpath, dsmcc_cache_callback_t *cache_callback, void *cache_callback_arg);
void dsmcc_filecache_free(struct dsmcc_object_carousel *car);

void dsmcc_filecache_cache_dir_info(struct dsmcc_file_cache *filecache, uint16_t module_id, uint32_t key_len, uint8_t *key, struct biop_binding *binding);
void dsmcc_filecache_cache_file_info(struct dsmcc_file_cache *filecache, uint16_t module_id, uint32_t key_len, uint8_t *key, struct biop_binding *binding);
void dsmcc_filecache_cache_file(struct dsmcc_file_cache *filecache, uint16_t module_id, uint8_t key_len, uint8_t *key, const char *module_file, int offset, int length);

#endif /* DSMCC_CACHE_FILE_H */
