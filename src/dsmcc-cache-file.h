#ifndef DSMCC_CACHE_FILE_H
#define DSMCC_CACHE_FILE_H

#include <stdint.h>

#include "dsmcc-cache.h"

/* from dsmcc-carousel.h */
struct dsmcc_object_carousel;

void dsmcc_filecache_free(struct dsmcc_object_carousel *carousel, bool keep_files);

void dsmcc_filecache_cache_dir(struct dsmcc_object_carousel *carousel, struct dsmcc_object_id *parent_id, struct dsmcc_object_id *id, const char *name);
void dsmcc_filecache_cache_file(struct dsmcc_object_carousel *carousel, struct dsmcc_object_id *parent_id, struct dsmcc_object_id *id, const char *name);
void dsmcc_filecache_cache_data(struct dsmcc_object_carousel *carousel, struct dsmcc_object_id *id, const char *data_file, uint32_t data_size);

#endif /* DSMCC_CACHE_FILE_H */
