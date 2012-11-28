#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dsmcc.h"
#include "dsmcc-util.h"
#include "dsmcc-carousel.h"

struct dsmcc_queue_entry
{
	struct dsmcc_stream          *stream;
	struct dsmcc_object_carousel *carousel;
	int                           type;
	uint32_t                      id; /* DSI: transaction ID (optional) / DII: transaction ID / DDB: download ID */

	struct dsmcc_queue_entry *next, *prev;
};

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

static struct dsmcc_stream *find_stream_by_pid(struct dsmcc_state *state, uint16_t pid)
{
	struct dsmcc_stream *str;

	for (str = state->streams; str; str = str->next)
	{
		if (str->pid == pid)
			break;
	}

	return str;
}

static struct dsmcc_stream *find_stream_by_assoc_tag(struct dsmcc_stream *streams, uint16_t assoc_tag)
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

struct dsmcc_stream *dsmcc_stream_find(struct dsmcc_state *state, int stream_selector_type, uint16_t stream_selector, bool create_if_missing)
{
	struct dsmcc_stream *str;
	uint16_t pid;

	if (stream_selector_type == DSMCC_STREAM_SELECTOR_ASSOC_TAG)
	{
		str = find_stream_by_assoc_tag(state->streams, stream_selector);
		if (str)
			return str;

		pid = (*state->stream_sub_callback)(state->stream_sub_callback_arg, stream_selector);
	}
	else if (stream_selector_type == DSMCC_STREAM_SELECTOR_PID)
	{
		pid = stream_selector;
	}
	else
	{
		DSMCC_ERROR("Unknown stream selector type %d", stream_selector_type);
		return NULL;
	}

	str = find_stream_by_pid(state, pid);
	if (!str && create_if_missing)
	{
		DSMCC_DEBUG("Adding stream with pid 0x%hx", pid);

		str = calloc(1, sizeof(struct dsmcc_stream));
		str->pid = pid;
		str->next = state->streams;
		if (str->next)
			str->next->prev = str;
		state->streams = str;
	}

	if (str && stream_selector_type == DSMCC_STREAM_SELECTOR_ASSOC_TAG)
		dsmcc_stream_add_assoc_tag(str, stream_selector);

	return str;
}

void dsmcc_stream_queue_add(struct dsmcc_object_carousel *carousel, int stream_selector_type, uint16_t stream_selector, int type, uint32_t id)
{
	struct dsmcc_stream *str;
	struct dsmcc_queue_entry *entry;

	str = dsmcc_stream_find(carousel->state, stream_selector_type, stream_selector, 1);
	if (str)
	{
		if (dsmcc_stream_queue_find(str, type, id))
			return;

		entry = calloc(1, sizeof(struct dsmcc_queue_entry));
		entry->stream = str;
		entry->carousel = carousel;
		entry->type = type;
		entry->id = id;

		entry->prev = NULL;
		entry->next = str->queue;
		if (entry->next)
			entry->next->prev = entry;
		str->queue = entry;
	}
}

struct dsmcc_object_carousel *dsmcc_stream_queue_find(struct dsmcc_stream *stream, int type, uint32_t id)
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

	if (entry)
		return entry->carousel;
	return NULL;
}

void dsmcc_stream_queue_remove(struct dsmcc_object_carousel *carousel, int type)
{
	struct dsmcc_stream *stream;
	struct dsmcc_queue_entry *entry;

	stream = carousel->state->streams;
	while (stream)
	{
		entry = stream->queue;
		while (entry)
		{
			if (entry->carousel == carousel && entry->type == type)
			{
				if (entry->prev)
					entry->prev->next = entry->next;
				else
					entry->stream->queue = entry->next;
				entry->next->prev = entry->prev;
				free(entry);

				return;
			}
			entry = entry->next;
		}
		stream = stream->next;
	}
}

static void free_queue_entries(struct dsmcc_queue_entry *entry)
{
	while (entry)
	{
		struct dsmcc_queue_entry *next = entry->next;
		free(entry);
		entry = next;
	}
}

static void free_all_streams(struct dsmcc_stream *stream)
{
	while (stream)
	{
		struct dsmcc_stream *next = stream->next;
		if (stream->assoc_tags)
			free(stream->assoc_tags);
		free_queue_entries(stream->queue);
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

	free_all_streams(state->streams);
	state->streams = NULL;

	free(state->tmpdir);

	free(state);
}

