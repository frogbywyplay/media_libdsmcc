#ifndef DSMCC_H
#define DSMCC_H

#include <dsmcc/dsmcc.h>
#include "dsmcc-debug.h"

struct dsmcc_stream
{
        int pid;
        unsigned int assoc_tag;
        struct dsmcc_stream *next, *prev;
};

struct dsmcc_state
{
	char *tmpdir;

	dsmcc_stream_subscribe_callback_t *stream_sub_callback;
	void                              *stream_sub_callback_arg;

	struct dsmcc_stream *streams;

	struct dsmcc_object_carousel *carousels;
};

struct dsmcc_stream *dsmcc_find_stream_by_pid(struct dsmcc_stream *streams, int pid);
struct dsmcc_stream *dsmcc_find_stream_by_assoc_tag(struct dsmcc_stream *streams, unsigned short assoc_tag);
int dsmcc_stream_subscribe(struct dsmcc_state *state, unsigned int assoc_tag);

#endif
