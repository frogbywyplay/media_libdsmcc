#ifndef DSMCC_CAROUSEL
#define DSMCC_CAROUSEL

#include "dsmcc-cache.h"

struct obj_carousel {
	struct cache *filecache;
	struct cache_module_data *cache; 

	struct dsmcc_dsi *gate;
	struct stream *streams;

	unsigned long id;
};

void dsmcc_objcar_free(struct obj_carousel *);
#endif
