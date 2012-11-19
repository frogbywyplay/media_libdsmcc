#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dsmcc.h"
#include "dsmcc-util.h"
#include "dsmcc-carousel.h"

/* Init library and return new state struct */

struct dsmcc_state *dsmcc_open(const char *tmpdir, dsmcc_stream_subscribe_callback_t *stream_sub_callback, void *stream_sub_callback_arg)
{
	struct dsmcc_state *state = NULL;

	state = calloc(1, sizeof(struct dsmcc_state));

	state->stream_sub_callback = stream_sub_callback;
	state->stream_sub_callback_arg = stream_sub_callback_arg;

	if (tmpdir != NULL && strlen(tmpdir) > 0)
	{
		state->tmpdir = strdup(tmpdir);
	}
	else
	{
		char buffer[32];
		snprintf(buffer, sizeof(buffer), "/tmp/libdsmcc-%d", getpid());
		state->tmpdir = strdup(buffer);
	}
	dsmcc_mkdir(state->tmpdir, 0755);

	return state;
}

struct dsmcc_stream *dsmcc_find_stream(struct dsmcc_state *state, uint16_t pid)
{
	struct dsmcc_stream *str;

	for (str = state->streams; str; str = str->next)
	{
		if (str->pid == pid)
			break;
	}

	return str;
}

static struct dsmcc_stream *dsmcc_find_stream_by_assoc_tag(struct dsmcc_stream *streams, uint16_t assoc_tag)
{
	struct dsmcc_stream *str;
	int i;

	for (str = streams; str; str = str->next)
	{
		for (i = 0; i < str->assoc_tag_count; i++)
			if (str->assoc_tags[i] == assoc_tag)
				break;
	}

	return str;
}

void dsmcc_stream_add_assoc_tag(struct dsmcc_stream *stream, uint16_t assoc_tag)
{
	int i;

	for (i = 0; i < stream->assoc_tag_count; i++)
		if (stream->assoc_tags[i] == assoc_tag)
			return;

	stream->assoc_tag_count++;
	stream->assoc_tags = realloc(stream->assoc_tags, stream->assoc_tag_count * sizeof(*stream->assoc_tags));
	stream->assoc_tags[stream->assoc_tag_count - 1] = assoc_tag;

	DSMCC_DEBUG("Added assoc_tag 0x%hx to stream with pid 0x%hx", assoc_tag, stream->pid);
}

static void dsmcc_stream_queue_add_entry(struct dsmcc_stream *stream, struct dsmcc_queue_entry *entry)
{
	entry->prev = NULL;
	entry->next = stream->queue;
	if (entry->next)
		entry->next->prev = entry;
	stream->queue = entry;
}

void dsmcc_stream_queue_add(struct dsmcc_state *state, int stream_selector_type, uint16_t stream_selector, struct dsmcc_queue_entry *entry)
{
	struct dsmcc_stream *str;
	uint16_t pid;

	if (stream_selector_type == DSMCC_STREAM_SELECTOR_ASSOC_TAG)
	{
		str = dsmcc_find_stream_by_assoc_tag(state->streams, stream_selector);
		if (str)
		{
			dsmcc_stream_queue_add_entry(str, entry);
			return;
		}

		pid = (*state->stream_sub_callback)(state->stream_sub_callback_arg, stream_selector);
	}
	else if (stream_selector_type == DSMCC_STREAM_SELECTOR_PID)
	{
		pid = stream_selector;
	}
	else
	{
		DSMCC_ERROR("Unknown stream selector type %d", stream_selector_type);
		return;
	}

	str = dsmcc_find_stream(state, pid);
	if (!str)
	{
		DSMCC_DEBUG("Adding stream with pid 0x%hx", pid);

		str = calloc(1, sizeof(struct dsmcc_stream));
		str->pid = pid;
		str->next = state->streams;
		if (str->next)
			str->next->prev = str;
		state->streams = str;
	}

	if (stream_selector_type == DSMCC_STREAM_SELECTOR_ASSOC_TAG)
		dsmcc_stream_add_assoc_tag(str, stream_selector);
	dsmcc_stream_queue_add_entry(str, entry);
}

struct dsmcc_queue_entry *dsmcc_stream_find_queue_entry(struct dsmcc_stream *stream, int type, uint32_t id)
{
	struct dsmcc_queue_entry *entry = stream->queue;

	while (entry)
	{
		if (entry->type == type)
		{
			if (type == DSMCC_QUEUE_ENTRY_DSI && entry->id == 0)
				break;
			else if ((entry->id & 0xfffe) == (id & 0xfffe)) /* match only bits 1-15 */
				break;
		}
		entry = entry->next;
	}

	return entry;
}

static void dsmcc_free_queue_entries(struct dsmcc_queue_entry *entry)
{
	while (entry)
	{
		struct dsmcc_queue_entry *next = entry->next;
		free(entry);
		entry = next;
	}
}

static void dsmcc_free_streams(struct dsmcc_stream *stream)
{
	while (stream)
	{
		struct dsmcc_stream *next = stream->next;
		if (stream->assoc_tags)
			free(stream->assoc_tags);
		dsmcc_free_queue_entries(stream->queue);
		free(stream);
		stream = next;
	}
}

void dsmcc_close(struct dsmcc_state *state)
{
	if (!state)
		return;

	dsmcc_object_carousel_free_all(state->carousels);
	state->carousels = NULL;

	dsmcc_free_streams(state->streams);
	state->streams = NULL;

	free(state->tmpdir);

	free(state);
}

