#ifndef DSMCC_CAROUSEL
#define DSMCC_CAROUSEL

/* from dsmcc-biop-ior.h */
struct biop_ior;

/* from dsmcc.h */
struct dsmcc_stream;

/* from dsmcc-filecache.h */
struct cache;

/* from dsmcc-cache.h */
struct dsmcc_cached_module;

struct dsmcc_object_carousel
{
	unsigned long        id;
	struct biop_ior     *gateway_ior;

	struct dsmcc_stream *streams;

	struct dsmcc_cached_module *modules;
	struct dsmcc_file_cache    *filecache;

	struct dsmcc_state           *state;
	struct dsmcc_object_carousel *next;
};

struct dsmcc_object_carousel *dsmcc_find_carousel_by_id(struct dsmcc_object_carousel *carousels, unsigned int id);

void dsmcc_object_carousel_stream_subscribe(struct dsmcc_object_carousel *carousel, unsigned short assoc_tag);

void dsmcc_object_carousel_free_all(struct dsmcc_object_carousel *carousel);

#endif
