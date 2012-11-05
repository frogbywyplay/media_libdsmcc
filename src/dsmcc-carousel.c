#include <stdlib.h>
#include <string.h>

#include "dsmcc.h"
#include "dsmcc-carousel.h"
#include "dsmcc-cache-module.h"
#include "dsmcc-cache-file.h"
#include "dsmcc-biop-ior.h"

void dsmcc_add_carousel(struct dsmcc_status *status, int cid, int pid, const char *downloadpath, dsmcc_cache_callback_t *cache_callback, void *cache_callback_arg)
{
	struct dsmcc_object_carousel *car;
	unsigned short assoc_tag = pid; // TODO

	// TODO check that carousel is not already requested

	car = malloc(sizeof(struct dsmcc_object_carousel));
	memset(car, 0, sizeof(struct dsmcc_object_carousel));
	dsmcc_filecache_init(car, downloadpath, cache_callback, cache_callback_arg);
	car->id = cid;
	car->status = status;
	car->next = status->carousels;
	status->carousels = car;

	dsmcc_object_carousel_stream_subscribe(car, assoc_tag); 
}

int dsmcc_object_carousel_stream_subscribe(struct dsmcc_object_carousel *carousel, unsigned int assoc_tag)
{
	struct dsmcc_stream *str;
	int pid;

	str = dsmcc_find_stream_by_assoc_tag(carousel->streams, assoc_tag);
	if (str)
		return str->pid;

	pid = dsmcc_stream_subscribe(carousel->status, assoc_tag);

	DSMCC_DEBUG("Adding stream with pid 0x%x and assoc_tag 0x%x to carousel %d", pid, assoc_tag, carousel->id);

	str = malloc(sizeof(struct dsmcc_stream));
	str->pid = pid;
	str->assoc_tag = assoc_tag;
	str->next = carousel->streams;
	if (str->next)
		str->next->prev = str;
	str->prev = NULL;
	carousel->streams = str;

	return pid;
}

struct dsmcc_object_carousel *dsmcc_find_carousel_by_id(struct dsmcc_object_carousel *carousels, int id)
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
	struct dsmcc_stream *str, *strnext;

	/* Free gateway */
	dsmcc_biop_free_ior(car->gateway_ior);
	car->gateway_ior = NULL;

	/* Free streams */
	str = car->streams;
	while (str != NULL)
	{
		strnext = str->next;
		free(str);
		str = strnext;
	}
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
