#ifndef DSMCC_CAROUSEL
#define DSMCC_CAROUSEL

#include <stdint.h>

struct dsmcc_object_carousel
{
	struct dsmcc_state *state;
	uint32_t            cid;

	uint32_t dsi_transaction_id;
	uint32_t dii_transaction_id;
	uint32_t download_id;

	struct dsmcc_cached_module *modules;

	struct dsmcc_file_cache    *filecache;

	struct dsmcc_object_carousel *next;
};


void dsmcc_object_carousel_free_all(struct dsmcc_object_carousel *carousel);

#endif
