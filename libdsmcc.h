#ifndef LIBDSMCC_H
#define LIBDSMCC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <syslog.h>
#include "dsmcc-receiver.h"
#include "dsmcc-carousel.h"

struct stream {
        int pid;
        unsigned int assoc_tag;
        struct stream *next, *prev;;
};

struct pid_buffer {
        int pid;
        unsigned char data[4284]; /* 1024*4 + 188 */
        int pointer_field;
        int in_section;
        int cont;
        struct pid_buffer *next;
};

struct dsmcc_status {
	int rec_files, total_files;
	int rec_dirs,  total_dirs;
	int gzip_size, total_size;
	enum cachestate { EMPTY, LISTINGS, FILLING, FULL } state;
	enum running { NOTRUNNING, RUNNINGSOON, PAUSED, RUNNING };

	char *channel_name;

	/* must check to see if any new streams to subscribe to after calling
	   receive each time (new stream info comes from within dsmcc */

	struct stream *newstreams;

	/* Currently subscribed streams (should be private but need to
	   access ... */

	struct stream *streams;

	/* Private Information (ish) */

	struct obj_carousel carousels[MAXCAROUSELS];

	struct pid_buffer *buffers;

	FILE *debug_fd;
};

struct dsmcc_status *dsmcc_open(const char *channel, FILE *);
void dsmcc_receive(struct dsmcc_status *status, unsigned char *Data, int Length);
void dsmcc_close(struct dsmcc_status *status);
void dsmcc_free(struct dsmcc_status *status);

#ifdef __cplusplus
}
#endif
#endif
