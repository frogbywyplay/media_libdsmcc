#ifndef DSMCC_CAROUSEL
#define DSMCC_CAROUSEL

#include <stdint.h>

struct dsmcc_object_carousel
{
	uint32_t cid;

	struct dsmcc_cached_module *modules;
	struct dsmcc_file_cache    *filecache;

	struct dsmcc_state           *state;
	struct dsmcc_object_carousel *next;
};

void dsmcc_object_carousel_free_all(struct dsmcc_object_carousel *carousel);

#endif
