#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "dsmcc.h"
#include "dsmcc-util.h"
#include "dsmcc-carousel.h"

/* Init library and return new status struct */

struct dsmcc_status *dsmcc_open(const char *tmpdir, dsmcc_stream_subscribe_callback_t *stream_sub_callback, void *stream_sub_callback_arg)
{
	struct dsmcc_status *status = NULL;

	status = malloc(sizeof(struct dsmcc_status));
	memset(status, 0, sizeof(struct dsmcc_status));

	status->stream_sub_callback = stream_sub_callback;
	status->stream_sub_callback_arg = stream_sub_callback_arg;

	status->streams = NULL;

	if (tmpdir != NULL && strlen(tmpdir) > 0)
	{
		status->tmpdir = (char*) malloc(strlen(tmpdir) + 1);
		strcpy(status->tmpdir, tmpdir);
	}
	else
	{
		char buffer[32];
		snprintf(buffer, sizeof(buffer), "/tmp/libdsmcc-%d", getpid());
		status->tmpdir = strdup(buffer);
	}
	dsmcc_mkdir(status->tmpdir, 0755);

	return status;
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

int dsmcc_stream_subscribe(struct dsmcc_status *status, unsigned int assoc_tag)
{
	struct dsmcc_stream *str;
	int pid;

	str = dsmcc_find_stream_by_assoc_tag(status->streams, assoc_tag);
	if (str)
		return str->pid;

	pid = (*status->stream_sub_callback)(status->stream_sub_callback_arg, assoc_tag);

	DSMCC_DEBUG("Adding stream with pid 0x%x and assoc_tag 0x%x", pid, assoc_tag);

	str = malloc(sizeof(struct dsmcc_stream));
	str->pid = pid;
	str->assoc_tag = assoc_tag;
	str->next = status->streams;
	if (str->next)
		str->next->prev = str;
	str->prev = NULL;
	status->streams = str;

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

void dsmcc_close(struct dsmcc_status *status)
{
	if (!status)
		return;

	dsmcc_object_carousel_free_all(status->carousels);
	status->carousels = NULL;

	dsmcc_free_streams(status->streams);
	status->streams = NULL;

	free(status->tmpdir);

	free(status);
}
