#include <stdlib.h>
#include <string.h>

#include "dsmcc.h"
#include "dsmcc-carousel.h"
#include "dsmcc-cache-module.h"
#include "dsmcc-cache-file.h"
#include "dsmcc-biop-ior.h"

static struct dsmcc_stream *dsmcc_object_carousel_stream_subscribe_by_pid(struct dsmcc_object_carousel *carousel, unsigned short pid)
{
	struct dsmcc_stream *str;

	str = dsmcc_find_stream_by_pid(carousel->streams, pid);
	if (str)
		return str;

	DSMCC_DEBUG("Adding stream with pid 0x%x to carousel %d", pid, carousel->id);

	str = calloc(1, sizeof(struct dsmcc_stream));
	str->pid = pid;
	str->next = carousel->streams;
	if (str->next)
		str->next->prev = str;
	str->prev = NULL;
	carousel->streams = str;

	return str;
}

void dsmcc_add_carousel(struct dsmcc_state *state, int pid, const char *downloadpath, dsmcc_cache_callback_t *cache_callback, void *cache_callback_arg)
{
	struct dsmcc_object_carousel *car;

	// Check that carousel is not already requested
	for (car = state->carousels; car; car = car->next)
	{
		if (dsmcc_find_stream_by_pid(car->streams, pid))
		{
			DSMCC_ERROR("Carousel for PID 0x%x already registered", pid);
			return;
		}
	}

	car = malloc(sizeof(struct dsmcc_object_carousel));
	memset(car, 0, sizeof(struct dsmcc_object_carousel));
	dsmcc_filecache_init(car, downloadpath, cache_callback, cache_callback_arg);
	car->id = 0; /* TODO a carousel ID of 0 is not possible */
	car->state = state;
	car->next = state->carousels;
	state->carousels = car;

	dsmcc_object_carousel_stream_subscribe_by_pid(car, pid); 
}

void dsmcc_object_carousel_stream_subscribe(struct dsmcc_object_carousel *carousel, unsigned short assoc_tag)
{
	struct dsmcc_stream *str;
	unsigned short pid;

	str = dsmcc_find_stream_by_assoc_tag(carousel->streams, assoc_tag);
	if (str)
		return;

	pid = dsmcc_stream_subscribe(carousel->state, assoc_tag);

	str = dsmcc_object_carousel_stream_subscribe_by_pid(carousel, pid);
	if (str->assoc_tag != assoc_tag)
		str->assoc_tag = assoc_tag;
}

struct dsmcc_object_carousel *dsmcc_find_carousel_by_id(struct dsmcc_object_carousel *carousels, unsigned int id)
{
	while (carousels)
	{
		if (carousels->id == id)
			break;
		carousels = carousels->next;
	}
	return carousels;
}

static void dsmcc_object_carousel_free(struct dsmcc_object_carousel *car)
{
	/* Free gateway */
	dsmcc_biop_free_ior(car->gateway_ior);
	car->gateway_ior = NULL;

	/* Free streams */
	dsmcc_free_streams(car->streams);
	car->streams = NULL;

	/* Free cached modules */
	while (car->modules)
		dsmcc_free_cached_module(car, car->modules);
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
