#ifndef DSMCC_H
#define DSMCC_H

#include <stdint.h>
#include <stdbool.h>
#include <dsmcc/dsmcc.h>
#include "dsmcc-debug.h"

enum
{
	DSMCC_QUEUE_ENTRY_DSI,
	DSMCC_QUEUE_ENTRY_DII,
	DSMCC_QUEUE_ENTRY_DDB
};

enum
{
	DSMCC_STREAM_SELECTOR_PID,
	DSMCC_STREAM_SELECTOR_ASSOC_TAG
};

struct dsmcc_stream
{
	uint16_t  pid;
	uint16_t  assoc_tag_count;
	uint16_t *assoc_tags;

	struct dsmcc_queue_entry *queue;

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

	/** Linked list of streams, used to cache assoc_tag/pid mapping and to queue requests */
	struct dsmcc_stream               *streams;

	/** Linked list of carousels */
	struct dsmcc_object_carousel *carousels;
};

struct dsmcc_stream *dsmcc_stream_find(struct dsmcc_state *state, int stream_selector_type, uint16_t stream_selector, bool create_if_missing);

struct dsmcc_object_carousel *dsmcc_stream_queue_find(struct dsmcc_stream *stream, int type, uint32_t id);
void dsmcc_stream_queue_add(struct dsmcc_object_carousel *carousel, int stream_selector_type, uint16_t stream_selector, int type, uint32_t id);
void dsmcc_stream_queue_remove(struct dsmcc_object_carousel *carousel, int type);

#endif
