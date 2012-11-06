#ifndef DSMCC_H
#define DSMCC_H

#include <dsmcc/dsmcc.h>
#include "dsmcc-debug.h"

struct dsmcc_stream
{
        unsigned short pid;
        unsigned short assoc_tag;

        struct dsmcc_stream *next, *prev;
};

struct dsmcc_state
{
	/** path where temporary data will be stored */
	char *tmpdir;

	/** Callback called to find the pid for a given assoc_tag */
	dsmcc_stream_subscribe_callback_t *stream_sub_callback;
	/** Opaque argument for the callback */
	void                              *stream_sub_callback_arg;
	/** Streams cache, used to cache assoc_tag -> pid mapping */
	struct dsmcc_stream               *streams;

	/** linked list of carousels */
	struct dsmcc_object_carousel *carousels;
};

struct dsmcc_stream *dsmcc_find_stream_by_pid(struct dsmcc_stream *streams, unsigned short pid);
struct dsmcc_stream *dsmcc_find_stream_by_assoc_tag(struct dsmcc_stream *streams, unsigned short assoc_tag);
void dsmcc_free_streams(struct dsmcc_stream *stream);
unsigned short dsmcc_stream_subscribe(struct dsmcc_state *state, unsigned short assoc_tag);

#endif
