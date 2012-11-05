#ifndef DSMCC_CACHE_FILE_H
#define DSMCC_CACHE_FILE_H

/* from dsmcc-biop-message.h */
struct biop_binding;
struct biop_message;

/* from dsmcc-cache-module.h */
struct dsmcc_cached_module;

struct dsmcc_file_cache;

void dsmcc_filecache_init(struct dsmcc_object_carousel *car, const char *downloadpath, dsmcc_cache_callback_t *cache_callback, void *cache_callback_arg);
void dsmcc_filecache_free(struct dsmcc_object_carousel *car);

void dsmcc_filecache_cache_dir_info(struct dsmcc_file_cache *filecache, unsigned short, unsigned int, unsigned char *, struct biop_binding *);
void dsmcc_filecache_cache_file_info(struct dsmcc_file_cache *filecache, unsigned short, unsigned int, unsigned char *, struct biop_binding *); 
void dsmcc_filecache_cache_file(struct dsmcc_file_cache *filecache, unsigned char objkey_len, unsigned char *objkey, unsigned long content_len, struct dsmcc_cached_module *module, unsigned int module_offset);

#endif /* DSMCC_CACHE_FILE_H */
