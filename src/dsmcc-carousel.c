#include <stdlib.h>
#include <string.h>

#include "dsmcc.h"
#include "dsmcc-carousel.h"
#include "dsmcc-cache-module.h"
#include "dsmcc-cache-file.h"

/* default timeout for aquisition of DSI message in microseconds (30s) */
#define DEFAULT_DSI_TIMEOUT (30 * 1000000)

#define CAROUSEL_CACHE_FILE_MAGIC 0xDDCC0001

static struct dsmcc_object_carousel *find_carousel_by_requested_pid(struct dsmcc_state *state, uint16_t pid)
{
	struct dsmcc_object_carousel *carousel;

	for (carousel = state->carousels; carousel; carousel = carousel->next)
		if (carousel->requested_pid == pid)
			return carousel;
	return NULL;
}

void dsmcc_object_carousel_remove(struct dsmcc_state *state, uint16_t pid)
{
	struct dsmcc_object_carousel *carousel;

	carousel = find_carousel_by_requested_pid(state, pid);
	if (carousel)
	{
		dsmcc_stream_queue_remove(carousel, DSMCC_QUEUE_ENTRY_DSI);
		dsmcc_stream_queue_remove(carousel, DSMCC_QUEUE_ENTRY_DII);
		dsmcc_stream_queue_remove(carousel, DSMCC_QUEUE_ENTRY_DDB);
		dsmcc_timeout_remove_all(carousel);
	}
}

void dsmcc_object_carousel_add(struct dsmcc_state *state, uint16_t pid, uint32_t transaction_id, const char *downloadpath, struct dsmcc_carousel_callbacks *callbacks)
{
	struct dsmcc_object_carousel *car = NULL;
	struct dsmcc_stream *stream;

	/* Check if carousel is already requested */
	car = find_carousel_by_requested_pid(state, pid);
	if (!car)
	{
		stream = dsmcc_stream_find_by_pid(state, pid);
		if (stream)
			car = dsmcc_stream_queue_find(stream, DSMCC_QUEUE_ENTRY_DSI, transaction_id);
	}

	if (car)
	{
		dsmcc_stream_queue_remove(car, DSMCC_QUEUE_ENTRY_DSI);
		dsmcc_cache_clear_filecache(car);
		free(car->downloadpath);
		car->dsi_transaction_id = 0;
		car->dii_transaction_id = 0;
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

	memcpy(&car->callbacks, callbacks, sizeof(struct dsmcc_carousel_callbacks));
	car->requested_pid = pid;
	car->requested_transaction_id = transaction_id;
	dsmcc_stream_queue_add(car, DSMCC_STREAM_SELECTOR_PID, pid, DSMCC_QUEUE_ENTRY_DSI, transaction_id);
	/* add section filter on stream for DSI (table_id == 0x3B, table_id_extension == 0x0000 or 0x0001) */
	if (state->callbacks.add_section_filter)
	{
		uint8_t pattern[3]  = { 0x3B, 0x00, 0x00 };
		uint8_t equal[3]    = { 0xff, 0xff, 0xfe };
		uint8_t notequal[3] = { 0x00, 0x00, 0x00 };
		(*state->callbacks.add_section_filter)(state->callbacks.add_section_filter_arg, pid, pattern, equal, notequal, 3);
	}

	dsmcc_timeout_set(car, DSMCC_TIMEOUT_DSI, 0, DEFAULT_DSI_TIMEOUT);

	car->status = -1;
	dsmcc_object_carousel_set_status(car, DSMCC_STATUS_DOWNLOADING);
}

#ifdef DEBUG
static const char *status_str(int status)
{
	switch (status)
	{
		case DSMCC_STATUS_DOWNLOADING:
			return "DOWNLOADING";
		case DSMCC_STATUS_TIMEDOUT:
			return "TIMED-OUT";
		case DSMCC_STATUS_DONE:
			return "DONE";
		default:
			return "Unknown!";
	}
}
#endif

void dsmcc_object_carousel_set_status(struct dsmcc_object_carousel *carousel, int newstatus)
{
	if (newstatus == carousel->status)
		return;

	DSMCC_DEBUG("Carousel 0x%08x status changed to %s", carousel->cid, status_str(newstatus));
	carousel->status = newstatus;

	if (carousel->callbacks.carousel_status_changed)
		(*carousel->callbacks.carousel_status_changed)(carousel->callbacks.carousel_status_changed_arg, carousel->cid, carousel->status);
}

void dsmcc_object_carousel_set_progression(struct dsmcc_object_carousel *carousel, uint32_t downloaded, uint32_t total)
{
	DSMCC_DEBUG("Carousel 0x%08x downloaded %u total %u", carousel->cid, downloaded, total);
	if (carousel->callbacks.download_progression)
		(*carousel->callbacks.download_progression)(carousel->callbacks.download_progression_arg, carousel->cid, downloaded, total);
}

void dsmcc_object_carousel_free(struct dsmcc_object_carousel *carousel, bool keep_cache)
{
	/* Free modules */
	dsmcc_cache_free_all_modules(carousel, keep_cache);
	carousel->modules = NULL;

	/* Free filecache */
	dsmcc_filecache_free(carousel, 1);
	carousel->filecache = NULL;

	free(carousel->downloadpath);
	free(carousel);
}

static void free_all_carousels(struct dsmcc_object_carousel *carousels, bool keep_cache)
{
	struct dsmcc_object_carousel *car, *nextcar;

	car = carousels;
	while (car)
	{
		nextcar = car->next;
		dsmcc_object_carousel_free(car, keep_cache);
		car = nextcar;
	}
}

void dsmcc_object_carousel_free_all(struct dsmcc_state *state, bool keep_cache)
{
	free_all_carousels(state->carousels, keep_cache);
	state->carousels = NULL;
}

bool dsmcc_object_carousel_load_all(FILE *f, struct dsmcc_state *state)
{
	uint32_t tmp;
	struct dsmcc_object_carousel *carousel = NULL, *lastcar = NULL;

	if (!fread(&tmp, 1, sizeof(uint32_t), f))
		goto error;
	if (tmp != CAROUSEL_CACHE_FILE_MAGIC)
		goto error;

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
		if (!fread(&carousel->status, 1, sizeof(int), f))
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

	tmp = CAROUSEL_CACHE_FILE_MAGIC;
	fwrite(&tmp, 1, sizeof(uint32_t), f);

	carousel = state->carousels;
	while (carousel)
	{
		tmp = 0;
		fwrite(&tmp, 1, sizeof(uint32_t), f);
		fwrite(&carousel->cid, 1, sizeof(uint32_t), f);
		tmp = strlen(carousel->downloadpath) + 1;
		fwrite(&tmp, 1, sizeof(uint32_t), f);
		fwrite(carousel->downloadpath, 1, tmp, f);
		fwrite(&carousel->status, 1, sizeof(int), f);
		fwrite(&carousel->requested_pid, 1, sizeof(uint16_t), f);
		fwrite(&carousel->requested_transaction_id, 1, sizeof(uint32_t), f);

		dsmcc_cache_save_modules(f, carousel);

		carousel = carousel->next;
	}
	tmp = 1;
	fwrite(&tmp, 1, sizeof(uint32_t), f);
}
