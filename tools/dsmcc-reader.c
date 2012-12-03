#include <signal.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <dsmcc/dsmcc.h>

static int running = 1;

static void sigint_handler(int signal)
{
	(void) signal;
	fprintf(stderr, "sigint\n");
	running = 0;
}

static void logger(int severity, const char *message)
{
	char *sev;

	switch (severity)
	{
		case DSMCC_LOG_DEBUG:
			sev = "[debug]";
			break;
		case DSMCC_LOG_WARN:
			sev = "[warn]";
			break;
		case DSMCC_LOG_ERROR:
			sev = "[error]";
			break;
		default:
			sev = "";
			break;
	}
	fprintf(stderr, "[dsmcc]%s %s\n", sev, message);
}

static uint16_t stream_sub_callback(void *arg, uint16_t assoc_tag)
{
	/* TODO find PID from assoc_tag using PMT and SDT */
	(void) assoc_tag;
	return *((uint16_t *)arg);
#if 0
	struct dsmcc_tsparser_buffer **buffers = (struct dsmcc_tsparser_buffer **)arg;

	int pid = pid_from_assoc_tag(assoc_tag);

	dsmcc_tsparser_add_pid(buffers, pid);
#endif
}

static int cache_callback(void *arg, uint32_t cid, int type, int reason, const char *path, const char *fullpath)
{
	char *t, *r;

	(void) arg;
	(void) fullpath;

	switch (type)
	{
		case DSMCC_CACHE_DIR:
			t = "directory";
			break;
		case DSMCC_CACHE_FILE:
			t = "file";
			break;
		default:
			t = "?";
			break;
	}

	switch (reason)
	{
		case DSMCC_CACHE_CHECK:
			r = "check";
			break;
		case DSMCC_CACHE_CREATED:
			r = "created";
			break;
		default:
			r = "?";
			break;
	}

	fprintf(stderr, "[main] Cache callback for %d:%s (%s %s)\n", cid, path, t, r);

	return 1;
};

static int parse_stream(FILE *ts, struct dsmcc_state *state, struct dsmcc_tsparser_buffer **buffers)
{
	char buf[188];
	int ret = 0;
	int rc;
	int i = 0;

	while(running)
	{
		rc = fread(buf, 1, 188, ts);
		if (rc < 0)
		{
			fprintf(stderr, "read error : %s\n", strerror(errno));
			ret = -1;
			break;
		}
		else if (rc == 0)
		{
			// EOF, parse remaining data
			dsmcc_tsparser_parse_buffered_sections(state, *buffers);
			break;
		}
		else
		{
			fprintf(stderr, "[main] read ts #%d\n", i++);
			dsmcc_tsparser_parse_packet(state, buffers, (unsigned char*) buf, rc); 
		}
	}

	return ret;
}

int main(int argc, char **argv)
{
	struct dsmcc_state *state;
	int status = 0; 
	char *downloadpath;
	FILE *ts;
	uint16_t pid;
	struct dsmcc_tsparser_buffer *buffers = NULL;

	if(argc < 4)
	{
		fprintf(stderr, "usage %s <file> <pid> <downloadpath>\n", argv[0]);
		return -1;
	}

	sscanf(argv[2], "%hu", &pid);
	downloadpath = argv[3];

	signal(SIGINT, sigint_handler);

	fprintf(stderr, "start\n");

	ts = fopen(argv[1], "r");
	if (ts)
	{
		dsmcc_set_logger(&logger, DSMCC_LOG_DEBUG);

		state = dsmcc_open("/tmp/dsmcc/cache", 1, stream_sub_callback, &pid /*&buffers*/);

		dsmcc_tsparser_add_pid(&buffers, pid);

		dsmcc_add_carousel(state, pid, 0, downloadpath, cache_callback, NULL);

		status = parse_stream(ts, state, &buffers);

		dsmcc_close(state);
		dsmcc_tsparser_free_buffers(&buffers);
	}
	else
	{
		fprintf(stderr, "open '%s' error : %s\n", argv[1], strerror(errno));
		return -1;
	}

	fclose(ts);

	return status;
}
