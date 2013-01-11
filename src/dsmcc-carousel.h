#ifndef DSMCC_CAROUSEL
#define DSMCC_CAROUSEL

#include <stdint.h>
#include <stdio.h>

struct dsmcc_object_carousel
{
	struct dsmcc_state *state;
	uint32_t            cid;
	char               *downloadpath;
	int                 status;

	struct dsmcc_carousel_callbacks callbacks;

	uint16_t requested_pid;
	uint32_t requested_transaction_id;

	uint32_t dsi_transaction_id;
	uint32_t dii_transaction_id;

	struct dsmcc_module     *modules;
	struct dsmcc_file_cache *filecache;

	struct dsmcc_object_carousel *next;
};

struct dsmcc_object_carousel *dsmcc_object_carousel_find_by_cid(struct dsmcc_state *state, uint32_t cid);
bool dsmcc_object_carousel_load_all(FILE *file, struct dsmcc_state *state);
void dsmcc_object_carousel_save_all(FILE *file, struct dsmcc_state *state);
void dsmcc_object_carousel_free_all(struct dsmcc_state *state, bool keep_cache);
void dsmcc_object_carousel_set_progression(struct dsmcc_object_carousel *carousel, uint32_t downloaded, uint32_t total);
void dsmcc_object_carousel_set_status(struct dsmcc_object_carousel *carousel, int newstatus);

#endif
