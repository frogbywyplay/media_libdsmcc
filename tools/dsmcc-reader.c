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
	}
	fprintf(stderr, "[dsmcc]%s %s\n", sev, message);
}

static int stream_sub_callback(void *arg, unsigned short assoc_tag)
{
	/* TODO find PID from assoc_tag using PMT and SDT */
	return assoc_tag;
#if 0
	struct dsmcc_tsparser_buffer **buffers = (struct dsmcc_tsparser_buffer **)arg;

	int pid = pid_from_assoc_tag(assoc_tag);

	dsmcc_tsparser_add_pid(buffers, pid);
#endif
}

static int cache_callback(void *arg, unsigned long cid, int reason, char *path, char *fullpath)
{
	char *r;

	switch (reason)
	{
		case DSMCC_CACHE_DIR_CHECK:
			r = "DIR_CHECK";
			break;
		case DSMCC_CACHE_DIR_SAVED:
			r = "DIR_SAVED";
			break;
		case DSMCC_CACHE_FILE_CHECK:
			r = "FILE_CHECK";
			break;
		case DSMCC_CACHE_FILE_SAVED:
			r = "FILE_SAVED";
			break;
		default:
			r = "UNKNOWN";
	}
	fprintf(stderr, "[main] Cache callback for %ld:%s (%s)\n", cid, path, r);

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
		if(rc < 1)
		{
			if(ferror(ts))
			{
				fprintf(stderr, "read error : %s\n", strerror(errno));
				ret = -1;
				break;
			}
			else
			{
				// EOF, parse remaining data
				dsmcc_tsparser_parse_buffered_sections(state, *buffers);
			}
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
	uint16_t cid;
	uint16_t pid;
	struct dsmcc_tsparser_buffer *buffers = NULL;

	if(argc < 4)
	{
		fprintf(stderr, "usage %s <file> <cid> <pid> <downloadpath>\n", argv[0]);
		return -1;
	}

	sscanf(argv[2], "%hu", &cid);
	sscanf(argv[3], "%hu", &pid);
	downloadpath = argv[4];

	signal(SIGINT, sigint_handler);

	fprintf(stderr, "start\n");

	ts = fopen(argv[1], "r");
	if (ts)
	{
		dsmcc_set_logger(&logger, DSMCC_LOG_DEBUG);

		state = dsmcc_open("/tmp/dsmcc/tmp", stream_sub_callback, &buffers);

		dsmcc_tsparser_add_pid(&buffers, pid);

		dsmcc_add_carousel(state, cid, pid, downloadpath, cache_callback, NULL);

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
