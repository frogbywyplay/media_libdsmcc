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

	state = malloc(sizeof(struct dsmcc_state));
	memset(state, 0, sizeof(struct dsmcc_state));

	state->stream_sub_callback = stream_sub_callback;
	state->stream_sub_callback_arg = stream_sub_callback_arg;

	state->streams = NULL;

	if (tmpdir != NULL && strlen(tmpdir) > 0)
	{
		state->tmpdir = (char*) malloc(strlen(tmpdir) + 1);
		strcpy(state->tmpdir, tmpdir);
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

struct dsmcc_stream *dsmcc_find_stream_by_pid(struct dsmcc_stream *streams, int pid)
{
	struct dsmcc_stream *str;

	for (str = streams; str; str = str->next)
	{
		if (str->pid == pid)
			break;
	}

	return str;
}

struct dsmcc_stream *dsmcc_find_stream_by_assoc_tag(struct dsmcc_stream *streams, unsigned short assoc_tag)
{
	struct dsmcc_stream *str;

	for (str = streams; str; str = str->next)
	{
		if (str->assoc_tag == assoc_tag)
			break;
	}

	return str;
}

int dsmcc_stream_subscribe(struct dsmcc_state *state, unsigned int assoc_tag)
{
	struct dsmcc_stream *str;
	int pid;

	str = dsmcc_find_stream_by_assoc_tag(state->streams, assoc_tag);
	if (str)
		return str->pid;

	pid = (*state->stream_sub_callback)(state->stream_sub_callback_arg, assoc_tag);

	DSMCC_DEBUG("Adding stream with pid 0x%x and assoc_tag 0x%x", pid, assoc_tag);

	str = malloc(sizeof(struct dsmcc_stream));
	str->pid = pid;
	str->assoc_tag = assoc_tag;
	str->next = state->streams;
	if (str->next)
		str->next->prev = str;
	str->prev = NULL;
	state->streams = str;

	return pid;
}

static void dsmcc_free_streams(struct dsmcc_stream *stream)
{
	while (stream)
	{
		struct dsmcc_stream *strnext = stream->next;
		free(stream);
		stream = strnext;
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
