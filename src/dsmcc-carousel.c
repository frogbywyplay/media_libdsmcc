#include <stdlib.h>
#include <string.h>

#include "dsmcc.h"
#include "dsmcc-carousel.h"
#include "dsmcc-cache-module.h"
#include "dsmcc-cache-file.h"

static struct dsmcc_object_carousel *find_carousel_by_request(struct dsmcc_state *state, uint16_t pid, uint32_t transaction_id)
{
	struct dsmcc_object_carousel *carousel;

	for (carousel = state->carousels; carousel; carousel = carousel->next)
		if (carousel->requested_pid == pid && (carousel->requested_transaction_id & 0xfffe) == (transaction_id & 0xfffe))
			return carousel;
	return NULL;
}

struct dsmcc_object_carousel *dsmcc_object_carousel_find_by_cid(struct dsmcc_state *state, uint32_t cid)
{
	struct dsmcc_object_carousel *carousel;

	for (carousel = state->carousels; carousel; carousel = carousel->next)
		if (carousel->cid == cid)
			return carousel;
	return NULL;
}

int dsmcc_add_carousel(struct dsmcc_state *state, uint16_t pid, uint32_t transaction_id, const char *downloadpath, dsmcc_cache_callback_t *cache_callback, void *cache_callback_arg)
{
	struct dsmcc_object_carousel *car = NULL;
	struct dsmcc_stream *stream;

	/* Check if carousel is already requested */
	car = find_carousel_by_request(state, pid, transaction_id);
	if (!car)
	{
		stream = dsmcc_stream_find(state, DSMCC_STREAM_SELECTOR_PID, pid, 0);
		if (stream)
			car = dsmcc_stream_queue_find(stream, DSMCC_QUEUE_ENTRY_DSI, transaction_id);
	}

	if (car)
	{
		dsmcc_stream_queue_remove(car, DSMCC_QUEUE_ENTRY_DSI);
		dsmcc_filecache_free(car, 1);
		free(car->downloadpath);
	}
	else
	{
		car = calloc(1, sizeof(struct dsmcc_object_carousel));
		car->state = state;
		car->next = state->carousels;
		state->carousels = car;
	}

	car->downloadpath = strdup(downloadpath);
	if (downloadpath[strlen(downloadpath) - 1] == '/')
		car->downloadpath[strlen(downloadpath) - 1] = '\0';

	car->cache_callback = cache_callback;
	car->cache_callback_arg = cache_callback_arg;
	car->requested_pid = pid;
	car->requested_transaction_id = transaction_id;
	dsmcc_stream_queue_add(car, DSMCC_STREAM_SELECTOR_PID, pid, DSMCC_QUEUE_ENTRY_DSI, transaction_id);

	dsmcc_cache_update_filecache(car);

	return 1;
}

static void free_carousel(struct dsmcc_object_carousel *car, bool keep_cache)
{
	/* Free modules */
	dsmcc_cache_free_all_modules(car, keep_cache);
	car->modules = NULL;

	/* Free filecache */
	dsmcc_filecache_free(car, 1);
	car->filecache = NULL;

	free(car->downloadpath);
	free(car);
}

static void free_all_carousels(struct dsmcc_object_carousel *carousels, bool keep_cache)
{
	struct dsmcc_object_carousel *car, *nextcar;

	car = carousels;
	while (car)
	{
		nextcar = car->next;
		free_carousel(car, keep_cache);
		car = nextcar;
	}
}

void dsmcc_object_carousel_free_all(struct dsmcc_state *state, bool keep_cache)
{
	free_all_carousels(state->carousels, keep_cache);
}

bool dsmcc_object_carousel_load_all(FILE *f, struct dsmcc_state *state)
{
	uint32_t tmp;
	struct dsmcc_object_carousel *carousel = NULL, *lastcar = NULL;

	while (1)
	{
		if (!fread(&tmp, 1, sizeof(uint32_t), f))
			goto error;
		if (tmp)
			break;
		carousel = calloc(1, sizeof(struct dsmcc_object_carousel));
		carousel->state = state;
		if (!fread(&carousel->cid, 1, sizeof(uint32_t), f))
			goto error;
		if (!fread(&tmp, 1, sizeof(uint32_t), f))
			goto error;
		carousel->downloadpath = malloc(tmp);
		if (!fread(carousel->downloadpath, 1, tmp, f))
			goto error;
		if (!fread(&carousel->complete, 1, sizeof(bool), f))
			goto error;
		if (!fread(&carousel->requested_pid, 1, sizeof(uint16_t), f))
			goto error;
		if (!fread(&carousel->requested_transaction_id, 1, sizeof(uint32_t), f))
			goto error;
		if (!dsmcc_cache_load_modules(f, carousel))
			goto error;

		if (state->carousels)
			lastcar->next = carousel;
		else
			state->carousels = carousel;
		lastcar = carousel;
		carousel = NULL;
	}

	return 1;
error:
	DSMCC_ERROR("Error while loading carousels");
	free_all_carousels(state->carousels, 0);
	if (carousel)
		free_all_carousels(carousel, 0);
	return 0;
}

void dsmcc_object_carousel_save_all(FILE *f, struct dsmcc_state *state)
{
	uint32_t tmp;
	struct dsmcc_object_carousel *carousel;

	carousel = state->carousels;
	while (carousel)
	{
		tmp = 0;
		fwrite(&tmp, 1, sizeof(uint32_t), f);
		fwrite(&carousel->cid, 1, sizeof(uint32_t), f);
		tmp = strlen(carousel->downloadpath) + 1;
		fwrite(&tmp, 1, sizeof(uint32_t), f);
		fwrite(carousel->downloadpath, 1, tmp, f);
		fwrite(&carousel->complete, 1, sizeof(bool), f);
		fwrite(&carousel->requested_pid, 1, sizeof(uint16_t), f);
		fwrite(&carousel->requested_transaction_id, 1, sizeof(uint32_t), f);

		dsmcc_cache_save_modules(f, carousel);

		carousel = carousel->next;
	}
	tmp = 1;
	fwrite(&tmp, 1, sizeof(uint32_t), f);
}
