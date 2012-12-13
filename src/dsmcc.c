#include <sys/types.h>
#include <sys/stat.h>
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

static void load_state(struct dsmcc_state *state)
{
	FILE *f;
	struct stat s;

	if (stat(state->cachefile, &s) < 0)
		return;

	DSMCC_DEBUG("Loading cached state");

	f = fopen(state->cachefile, "r");
	dsmcc_object_carousel_load_all(f, state);
	fclose(f);
}

void dsmcc_state_save(struct dsmcc_state *state)
{
	FILE *f;

	if (!state->keep_cache)
		return;

	DSMCC_DEBUG("Saving state");

	f = fopen(state->cachefile, "w");
	dsmcc_object_carousel_save_all(f, state);
	fclose(f);
}

struct dsmcc_state *dsmcc_open(const char *cachedir, bool keep_cache, struct dsmcc_dvb_callbacks *callbacks)
{
	struct dsmcc_state *state = NULL;

	state = calloc(1, sizeof(struct dsmcc_state));

	memcpy(&state->callbacks, callbacks, sizeof(struct dsmcc_dvb_callbacks));

	if (cachedir == NULL || strlen(cachedir) == 0)
	{
		char buffer[32];
		sprintf(buffer, "/tmp/libdsmcc-cache-%d", getpid());
		if (buffer[strlen(buffer) - 1] == '/')
			buffer[strlen(buffer) - 1] = '\0';
		state->cachedir = strdup(buffer);
	}
	else
		state->cachedir = strdup(cachedir);
	mkdir(state->cachedir, 0755);
	state->keep_cache = keep_cache;

	state->cachefile = malloc(strlen(state->cachedir) + 7);
	sprintf(state->cachefile, "%s/state", state->cachedir);

	load_state(state);

	return state;
}

struct dsmcc_stream *dsmcc_stream_find_by_pid(struct dsmcc_state *state, uint16_t pid)
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
		for (i = 0; i < str->assoc_tag_count; i++)
			if (str->assoc_tags[i] == assoc_tag)
				return str;

	return NULL;
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

static struct dsmcc_stream *find_stream(struct dsmcc_state *state, int stream_selector_type, uint16_t stream_selector, uint16_t default_pid, bool create_if_missing)
{
	struct dsmcc_stream *str;
	uint16_t pid;
	int ret;

	if (stream_selector_type == DSMCC_STREAM_SELECTOR_ASSOC_TAG)
	{
		str = find_stream_by_assoc_tag(state->streams, stream_selector);
		if (str)
			return str;

		if (state->callbacks.get_pid_for_assoc_tag)
		{
			ret = (*state->callbacks.get_pid_for_assoc_tag)(state->callbacks.get_pid_for_assoc_tag_arg, stream_selector, &pid);
			if (ret != 0)
			{
				DSMCC_DEBUG("PID/AssocTag Callback returned error %d, using initial carousel PID 0x%04x for assoc tag 0x%04x", ret, default_pid, stream_selector);
				pid = default_pid;
			}
		}
		else
		{
			DSMCC_DEBUG("No PID/AssocTag callback, using initial carousel PID 0x%04x for assoc tag 0x%04x", default_pid, stream_selector);
			pid = default_pid;
		}
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

	str = dsmcc_stream_find_by_pid(state, pid);
	if (!str && create_if_missing)
	{
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

struct dsmcc_stream *dsmcc_stream_queue_add(struct dsmcc_object_carousel *carousel, int stream_selector_type, uint16_t stream_selector, int type, uint32_t id)
{
	struct dsmcc_stream *str;
	struct dsmcc_queue_entry *entry;

	str = find_stream(carousel->state, stream_selector_type, stream_selector, carousel->requested_pid, 1);
	if (str)
	{
		if (dsmcc_stream_queue_find(str, type, id))
			return str;

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

	return str;
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
	struct dsmcc_queue_entry *entry, *next;

	stream = carousel->state->streams;
	while (stream)
	{
		entry = stream->queue;
		while (entry)
		{
			next = entry->next;
			if (entry->carousel == carousel && entry->type == type)
			{
				if (entry->prev)
					entry->prev->next = entry->next;
				else
					entry->stream->queue = entry->next;
				if (entry->next)
					entry->next->prev = entry->prev;
				free(entry);
			}
			entry = next;
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

static void free_all_streams(struct dsmcc_state *state)
{
	struct dsmcc_stream *stream = state->streams;
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

	dsmcc_object_carousel_free_all(state, state->keep_cache);
	state->carousels = NULL;

	free_all_streams(state);
	state->streams = NULL;

	if (!state->keep_cache)
	{
		unlink(state->cachefile);
		rmdir(state->cachedir);
	}

	free(state->cachefile);
	free(state->cachedir);
	free(state);
}
