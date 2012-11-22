#include <stdlib.h>
#include <string.h>

#include "dsmcc.h"
#include "dsmcc-carousel.h"
#include "dsmcc-cache-module.h"
#include "dsmcc-cache-file.h"

int dsmcc_add_carousel(struct dsmcc_state *state, uint16_t pid, uint32_t transaction_id, const char *downloadpath, dsmcc_cache_callback_t *cache_callback, void *cache_callback_arg)
{
	struct dsmcc_object_carousel *car;
	struct dsmcc_stream *stream;
	struct dsmcc_queue_entry *entry;

	/* Check that carousel is not already requested */
	stream = dsmcc_stream_find(state, DSMCC_STREAM_SELECTOR_PID, pid, 0);
	if (stream)
	{
		entry = dsmcc_stream_queue_find_entry(stream, DSMCC_QUEUE_ENTRY_DSI, transaction_id);
		if (entry)
		{
			DSMCC_ERROR("Carousel on PID 0x%hx with Transaction ID 0x%x already requested", pid, transaction_id);
			return 0;
		}
	}

	car = calloc(1, sizeof(struct dsmcc_object_carousel));
	dsmcc_filecache_init(car, downloadpath, cache_callback, cache_callback_arg);
	car->state = state;
	car->next = state->carousels;
	state->carousels = car;

	entry = calloc(1, sizeof(struct dsmcc_queue_entry));
	entry->carousel = car;
	entry->type = DSMCC_QUEUE_ENTRY_DSI;
	entry->id = transaction_id;
	dsmcc_stream_queue_add(state, DSMCC_STREAM_SELECTOR_PID, pid, entry);

	return 1;
}

struct dsmcc_object_carousel *dsmcc_find_carousel_by_id(struct dsmcc_object_carousel *carousels, uint32_t cid)
{
	while (carousels)
	{
		if (carousels->cid == cid)
			break;
		carousels = carousels->next;
	}
	return carousels;
}

static void dsmcc_object_carousel_free(struct dsmcc_object_carousel *car)
{
	/* Free cached modules */
	dsmcc_cached_module_free_all(car);
	car->modules = NULL;

	/* Free filecache */
	dsmcc_filecache_free(car);
	car->filecache = NULL;

	free(car);
}

void dsmcc_object_carousel_free_all(struct dsmcc_object_carousel *car)
{
	struct dsmcc_object_carousel *nextcar;

	while (car)
	{
		nextcar = car->next;
		dsmcc_object_carousel_free(car);
		car = nextcar;
	}
}
