#include <signal.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include <dsmcc/dsmcc.h>
#include <dsmcc/dsmcc-tsparser.h>

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

static int get_pid_for_assoc_tag(void *arg, uint16_t assoc_tag, uint16_t *pid)
{
	/* fake, should find PID from assoc_tag using PMT */
	fprintf(stderr, "[main] Callback: Getting PID for association tag 0x%04x\n", assoc_tag);
	*pid = *((uint16_t *)arg);

	return 0;
}

static int add_section_filter(void *arg, uint16_t pid, uint8_t *pattern, uint8_t *equalmask, uint8_t *notequalmask, uint16_t depth)
{
	char *p, *em, *nem;
	int i;

	(void) arg;
	p = malloc(depth * 2 + 1);
	em = malloc(depth * 2 + 1);
	nem = malloc(depth * 2 + 1);
	for (i = 0;i < depth; i++)
	{
		sprintf(p + i * 2, "%02hhx", pattern[i]);
		sprintf(em + i * 2, "%02hhx", equalmask[i]);
		sprintf(nem + i * 2, "%02hhx", notequalmask[i]);
	}
	fprintf(stderr, "[main] Callback: Add section filter on PID 0x%04x: %s|%s|%s\n", pid, p, em, nem);
	free(nem);
	free(em);
	free(p);

	return 0;
}

static bool dentry_check(void *arg, uint32_t cid, bool dir, const char *path, const char *fullpath)
{
	(void) arg;

	fprintf(stderr, "[main] Callback: Dentry check 0x%08x:%s:%s -> %s\n", cid, dir ? "directory" : "file", path, fullpath);

	return 1;
}

static void dentry_saved(void *arg, uint32_t cid, bool dir, const char *path, const char *fullpath)
{
	(void) arg;
	
	fprintf(stderr, "[main] Callback: Dentry saved 0x%08x:%s:%s -> %s\n", cid, dir ? "directory" : "file", path, fullpath);
};

static void carousel_complete(void *arg, uint32_t cid)
{
	(void) arg;

	fprintf(stderr, "[main] Callback: Carousel complete 0x%08x\n", cid);
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
	struct dsmcc_dvb_callbacks dvb_callbacks;
	struct dsmcc_carousel_callbacks car_callbacks;

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

		dvb_callbacks.get_pid_for_assoc_tag = &get_pid_for_assoc_tag;
		dvb_callbacks.get_pid_for_assoc_tag_arg = &pid;
		dvb_callbacks.add_section_filter = &add_section_filter;
		state = dsmcc_open("/tmp/dsmcc-cache", 1, &dvb_callbacks);

		dsmcc_tsparser_add_pid(&buffers, pid);

		car_callbacks.dentry_check = &dentry_check;
		car_callbacks.dentry_saved = &dentry_saved;
		car_callbacks.carousel_complete = &carousel_complete;
		dsmcc_add_carousel(state, pid, 0, downloadpath, &car_callbacks);

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
