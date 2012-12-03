#ifndef DSMCC_CAROUSEL
#define DSMCC_CAROUSEL

#include <stdint.h>
#include <stdio.h>

struct dsmcc_object_carousel
{
	struct dsmcc_state *state;
	uint32_t            cid;
	char               *downloadpath;
	bool                complete;

	uint16_t requested_pid;
	uint32_t requested_transaction_id;

	uint32_t dsi_transaction_id;
	uint32_t dii_transaction_id;

	struct dsmcc_module     *modules;
	struct dsmcc_file_cache *filecache;
	dsmcc_cache_callback_t  *cache_callback;
	void                    *cache_callback_arg;

	struct dsmcc_object_carousel *next;
};

struct dsmcc_object_carousel *dsmcc_object_carousel_find_by_cid(struct dsmcc_state *state, uint32_t cid);
bool dsmcc_object_carousel_load_all(FILE *file, struct dsmcc_state *state);
void dsmcc_object_carousel_save_all(FILE *file, struct dsmcc_state *state);
void dsmcc_object_carousel_free_all(struct dsmcc_state *state, bool keep_cache);

#endif
