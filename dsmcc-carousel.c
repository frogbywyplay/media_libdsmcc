#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>

#include "dsmcc-receiver.h"
#include "dsmcc-descriptor.h"
#include "dsmcc-carousel.h"
#include "libdsmcc.h"


void dsmcc_objcar_free(struct obj_carousel *car)
{
	struct stream *str, *strnext;
	struct cache_module_data *cachep, *cachepnext;

	/* Free gateway info */
	if (car->gateway != NULL)
	{
		if (car->gateway->user_data != NULL)
			free(car->gateway->user_data);

		if (car->gateway->profile.type_id != NULL)
			free(car->gateway->profile.type_id);

		if (car->gateway->profile.body.full.obj_loc.objkey != NULL)
			free(car->gateway->profile.body.full.obj_loc.objkey);

		if (car->gateway->profile.body.full.dsm_conn.taps_count > 0)
		{
			if (car->gateway->profile.body.full.dsm_conn.tap.selector_data != NULL)
				free(car->gateway->profile.body.full.dsm_conn.tap.selector_data);
		}

		free(car->gateway);
		car->gateway = NULL;
	}

	/* Free stream info */
	str = car->streams;
	while (str != NULL)
	{
		strnext = str->next;
		free(str);
		str = strnext;
	}
	car->streams = NULL;

	/* Free cache info */
	while (car->cache != NULL)
		dsmcc_free_cache_module_data(car, car->cache);

	dsmcc_cache_free(car->filecache);
}
