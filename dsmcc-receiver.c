#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "dsmcc-receiver.h"
#include "dsmcc-descriptor.h"
#include "dsmcc-biop.h"
#include "dsmcc-cache.h"
#include "dsmcc-util.h"
#include "libdsmcc.h"

#define DSMCC_MESSAGE_DSI	0x1006
#define DSMCC_MESSAGE_DII	0x1002
#define DSMCC_MESSAGE_DDB	0x1003

#define DSMCC_SECTION_INDICATION	0x3B
#define DSMCC_SECTION_DATA		0x3C
#define DSMCC_SECTION_DESCR		0x3D

#define DSMCC_SECTION_OFFSET	0
#define DSMCC_MSGHDR_OFFSET	8
#define DSMCC_DATAHDR_OFFSET	8
#define DSMCC_DSI_OFFSET	20
#define DSMCC_DII_OFFSET	20
#define DSMCC_DDB_OFFSET	20
#define DSMCC_BIOP_OFFSET	24

static void dsmcc_add_module_info(struct dsmcc_status *, struct dsmcc_section *, struct obj_carousel *);
static void dsmcc_add_module_data(struct dsmcc_status *, struct dsmcc_section *, unsigned char *);

static int dsmcc_process_section_gateway(struct dsmcc_status *, unsigned char *, int, int);
static int dsmcc_process_section_info(struct dsmcc_status *, struct dsmcc_section *,unsigned char *, int);
static int dsmcc_process_section_block(struct dsmcc_status *, struct dsmcc_section *, unsigned char *, int);

static int dsmcc_process_section_header(struct dsmcc_section *, unsigned char *, int);
static int dsmcc_process_msg_header(struct dsmcc_section *, unsigned char *);
static int dsmcc_process_data_header(struct dsmcc_section *, unsigned char *, int);

static void dsmcc_process_section_desc(unsigned char *Data, int Length);
static void dsmcc_process_section_data(struct dsmcc_status *, unsigned char *Data, int Length);
static void dsmcc_process_section_indication(struct dsmcc_status *, unsigned char *Data, int pid, int Length);

void dsmcc_init(struct dsmcc_status *status, const char *channel)
{
	int i;

	status->streams = NULL;
	status->newstreams = NULL;
	status->buffers = NULL;

	for (i = 0; i < MAXCAROUSELS; i++)
	{
		status->carousels[i].streams = NULL;
		status->carousels[i].cache = NULL;
		status->carousels[i].filecache = malloc(sizeof(struct cache));
		status->carousels[i].gateway = NULL;
		status->carousels[i].id = 0;
		dsmcc_cache_init(status->carousels[i].filecache, channel);
	}

	if(channel != '\0')
	{
		status->channel_name = (char*) malloc(strlen(channel) + 1);
		strcpy(status->channel_name, channel);
	}
	else
	{
		status->channel_name = (char*) malloc(8);
		strcpy(status->channel_name, "Unknown");
	}
}

static void dsmcc_free_streams(struct stream *stream)
{
	while (stream)
	{
		struct stream *strnext = stream->next;
		free(stream);
		stream = strnext;
	}
}

static void dsmcc_free_buffers(struct pid_buffer *buffer)
{
	while (buffer)
	{
		struct pid_buffer *bufnext = buffer->next;
		free(buffer);
		buffer = bufnext;
	}
}

void dsmcc_free(struct dsmcc_status *status)
{
	struct file_info *file, *filenext;
	int i;

	if (!status)
		return;

	/* Free any carousel data and cached data.  */
	for (i = 0; i < MAXCAROUSELS; i++)
		dsmcc_objcar_free(&status->carousels[i]);

	/*
	 * TODO - actually cache on disk the cache data
	 * TODO - more terrible memory madness, this all needs improving
	 */

	if (status->streams)
	{
		dsmcc_free_streams(status->streams);
		status->streams = NULL;
	}

	if (status->newstreams)
	{
		dsmcc_free_streams(status->newstreams);
		status->newstreams = NULL;
	}

	if(status->buffers)
	{
		dsmcc_free_buffers(status->buffers);
		status->buffers = NULL;
	}

	if(status->channel_name)
	{
		free(status->channel_name);
		status->channel_name = NULL;
	}

	free(status);
}

void dsmcc_add_stream(struct dsmcc_status *status, int pid)
{
	struct pid_buffer *buf, *lbuf;
	struct stream *str, *strs;

	/* TODO check not being called repeatedly for pids with unknown tag */

	for (lbuf = status->buffers; lbuf != NULL; lbuf = lbuf->next)
	{
		if (lbuf->pid == pid)
			return;
	}

	buf = (struct pid_buffer *) malloc(sizeof(struct pid_buffer));
	buf->pid = pid;
	buf->in_section = 0;
	buf->cont = -1;
	buf->next = NULL;

	DSMCC_DEBUG("[receiver] Created buffer for pid %d\n", pid);

	if (!status->buffers)
	{
		status->buffers = buf;
	}
	else
	{
		lbuf = status->buffers;
		while (lbuf->next)
			lbuf = lbuf->next;
		lbuf->next = buf;
	}

	/* Add new stream to newstreams for caller code to pick up */

	str = malloc(sizeof(struct stream));
	str->pid = pid;
	str->assoc_tag = pid;
	str->next = str->prev = NULL;

	if (!status->newstreams)
	{
		status->newstreams = str;
	}
	else
	{
		strs = status->newstreams;
		while (strs->next)
			strs = strs->next;
		strs->next = str;
		str->prev = strs;
	}
}

int dsmcc_process_section_header(struct dsmcc_section *section, unsigned char *data, int length)
{
	struct dsmcc_section_header *header = &section->sec;
	int crc_offset;
	int section_syntax_indicator;
	int private_indicator;

	header->table_id = data[0];

	section_syntax_indicator = (data[1] & 0x80 != 0);
	private_indicator = (data[1] & 0x40 != 0);

	/* Check CRC is set and private_indicator is set to its complement, else skip packet */
	if (section_syntax_indicator ^ private_indicator)
	{
		DSMCC_ERROR("[receiver] Invalid section header: section_syntax_indicator and private_indicator flags are not complements (%d/%d)\n", section_syntax_indicator, private_indicator);
		return 1; /* Section invalid */
	}

	header->table_id_extension = dsmcc_getshort(data + 4);

	/* skip to end, read last 4 bytes and store in crc */
	crc_offset = length - 4 - 1;    /* 4 bytes */
	header->crc = dsmcc_getlong(data + crc_offset);

	return 0;
}

int dsmcc_process_msg_header(struct dsmcc_section *section, unsigned char *data)
{
	struct dsmcc_message_header *header = &section->hdr.info;

	header->protocol = data[0];
	DSMCC_DEBUG("[receiver] MsgHdr -> Protocol: %X\n", header->protocol);
	if (header->protocol != 0x11)
	{
		/* TODO handle error */
		return 1;
	}

	header->type = data[1];
	DSMCC_DEBUG("[receiver] MsgHdr -> Type: %X\n", header->type);
	if(header->type != 0x03)
	{
		/* TODO handle error */
		return 1;
	}


	header->message_id = dsmcc_getshort(data + 2);
	DSMCC_DEBUG("[receiver] MsgHdr -> Message ID: %X\n", header->message_id);

	header->transaction_id = dsmcc_getlong(data + 4);
	DSMCC_DEBUG("[receiver] MsgHdr -> Transaction ID: %lX\n", header->transaction_id);

	/* data[8] - reserved */
	/* data[9] - adaptationLength 0x00 */

	header->message_len = dsmcc_getshort(data + 10);
	DSMCC_DEBUG("[receiver] MsgHdr -> Message Length: %d\n", header->message_len);
	if (header->message_len > 4076)
	{
		/* Beyond valid length */
		/* TODO handle error */
		return 1;
	}

	return 0;
}

int dsmcc_process_section_gateway(struct dsmcc_status *status, unsigned char *data, int length, int pid)
{
        int off = 0, ret, i;
	struct obj_carousel *car;
	struct stream *str, *s;

	/* Find which object carousel this pid's data belongs to */
	for (i = 0; i < MAXCAROUSELS; i++)
	{
		car = &status->carousels[i];
		for (str = car->streams; str; str = str->next)
		{
			if (str->pid == pid)
				break;
		}
		if (str)
		{
			if (car->gateway)
			{
				/* TODO check gateway version not changed */
				DSMCC_DEBUG("[receiver] Already got gateway for pid %d\n", pid);
				return 0; /* We already have gateway */
			}
			else
				break;
		}
	}

	DSMCC_DEBUG("[receiver] Setting gateway for pid %d\n", pid);

	if (i == MAXCAROUSELS)
	{ 
		DSMCC_DEBUG("[receiver] Gateway for unknown carousel\n");
		return 0;
	}

	car->gateway = (struct dsmcc_dsi *) malloc(sizeof(struct dsmcc_dsi));
	memset(car->gateway, 0, sizeof(struct dsmcc_dsi));

	/* 0-19 Server id = 20 * 0xFF */

	/* 20,21 compatibilydescriptorlength = 0x0000 */

	off = 22;

	car->gateway->data_len = dsmcc_getshort(data + off);
	off += 2;
	DSMCC_DEBUG("[receiver] Data Length: %d\n", car->gateway->data_len);

	/* does not even exist ?
	gateway->num_groups = dsmcc_getshort(data + off);
	off += 2;
	DSMCC_DEBUG("[receiver] Num. Groups: %d\n", gateway->num_groups);
	*/

	/* TODO - process groups ( if ever exist ? ) */

	DSMCC_DEBUG("[receiver] Processing BiopBody...\n");
	ret = dsmcc_biop_process_ior(&car->gateway->profile, data + DSMCC_BIOP_OFFSET);
	if (ret > 0)
	{
		off += ret;
	}
	else
	{
		/* TODO handle error */
		return -1;
	}

	/* Set carousel id if not already given in data_broadcast_id_descriptor
	   (only teletext doesnt bother with this ) */
	if (car->id == 0)
	{
		/* TODO is carousel id 0 ever valid ? */
		car->id = car->gateway->profile.body.full.obj_loc.carousel_id;
	}

	DSMCC_DEBUG("[receiver] Gateway Module %d on carousel %ld\n", car->gateway->profile.body.full.obj_loc.module_id, car->id);

	/* Subscribe to pid if not already */
	for (s = status->streams; s; s = s->next)
	{
		if(s->assoc_tag == car->gateway->profile.body.full.dsm_conn.tap.assoc_tag)
		{
			/* Remove stream from list ... */
			if (!s->prev)
			{
				status->streams = s->next;
				if (status->streams)
					status->streams->prev = NULL;
			}
			else
			{
				s->prev->next = s->next;
				if (s->next)
					s->next->prev = s->prev;
			}

			DSMCC_DEBUG("[receiver] Subscribing to (info) stream %d\n", s->pid);
			/* TODO Far too complicated...*/
			dsmcc_add_stream(status, s->pid);
			free(s);
		}
	}

	/* skip taps and context */
	off += 2;

	/* TODO process descriptors */
	car->gateway->user_data_len = data[off++];
	if (car->gateway->user_data_len > 0)
	{
		car->gateway->user_data = (unsigned char *) malloc(car->gateway->data_len);
		memcpy(car->gateway->user_data, data + off, car->gateway->data_len);
	}
	else
		car->gateway->user_data = NULL;

	DSMCC_DEBUG("[receiver] BiopBody - Data Length %ld\n", car->gateway->profile.body.full.data_len);
	DSMCC_DEBUG("[receiver] BiopBody - Lite Components %d\n", car->gateway->profile.body.full.lite_components_count);

	return 0;
}

static void dsmcc_free_module(struct dsmcc_module_info *module)
{
	dsmcc_desc_free_all(module->modinfo.descriptors);

	if (module->modinfo.tap.selector_data)
	{
		free(module->modinfo.tap.selector_data);
		module->modinfo.tap.selector_data = NULL;
	}
}

int dsmcc_process_section_info(struct dsmcc_status *status, struct dsmcc_section *section, unsigned char *data, int length)
{
	struct dsmcc_dii *dii = &section->msg.dii;
	struct obj_carousel *car = NULL;
	int off = 0, i, ret;

	dii->download_id = dsmcc_getlong(data);
	off += 4;
	DSMCC_DEBUG("[receiver] Info -> Download ID = %lX\n", dii->download_id);

	for( i = 0; i < MAXCAROUSELS; i++)
	{
		car = &status->carousels[i];
		if (car->id == dii->download_id)
			break;
	}
	if (!car)
	{
		DSMCC_DEBUG("[receiver] Section Info for unknown carousel %ld\n", dii->download_id);
		/* No known carousels yet (possible?) TODO ! */
		return 1;
	}

	dii->block_size = dsmcc_getshort(data + off);
	off += 2;
	DSMCC_DEBUG("[receiver] Info -> Block Size = %d\n", dii->block_size);

	/* skip unused fields */
	off += 6;

	dii->tc_download_scenario = dsmcc_getlong(data + off);
	off += 4;
	DSMCC_DEBUG("[receiver] Info -> tc download scenario = %ld\n", dii->tc_download_scenario);

	/* skip unused compatibility descriptor len */
	off += 2;

	dii->number_modules = dsmcc_getshort(data + off);
	off += 2;
	DSMCC_DEBUG("[receiver] Info -> number modules = %d\n", dii->number_modules);

	dii->modules = (struct dsmcc_module_info*) malloc(sizeof(struct dsmcc_module_info) * dii->number_modules);
	for (i = 0; i < dii->number_modules; i++)
	{
		dii->modules[i].module_id = dsmcc_getshort(data + off);
		off += 2;
		dii->modules[i].module_size = dsmcc_getlong(data + off);
		off += 4;
		dii->modules[i].module_version = data[off++];
		dii->modules[i].module_info_len = data[off++];

		DSMCC_DEBUG("[receiver] Module %d -> Size = %ld Version = %d\n", dii->modules[i].module_id, dii->modules[i].module_size, dii->modules[i].module_version);

		ret = dsmcc_biop_process_module_info(&dii->modules[i].modinfo, data + off);
		if (ret > 0)
		{
			off += ret;
		}
		else
		{
			/* handle error */
			DSMCC_ERROR("[receiver] Info -> dsmcc_biop_process_module_info returned %d\n", ret);
			return;
		}
	}

	dii->private_data_len = dsmcc_getshort(data + off);
	off += dii->private_data_len;
	DSMCC_DEBUG("[receiver] Info -> Private Data Length = %d\n", dii->private_data_len);

	/* TODO add module info within this function */
	dsmcc_add_module_info(status, section, car);

	/* Free most of the memory up... all that effort for nothing */
	for (i = 0; i < dii->number_modules; i++)
		dsmcc_free_module(&dii->modules[i]);
	free(dii->modules);
	dii->modules = NULL;

	return 0;
}

void dsmcc_process_section_indication(struct dsmcc_status *status, unsigned char *data, int length, int pid)
{
	struct dsmcc_section section;
	int ret;

	ret = dsmcc_process_section_header(&section, data + DSMCC_SECTION_OFFSET, length);
	if (ret != 0)
	{
		DSMCC_ERROR("[receiver] Indication Section Header error\n");
		return;
	}

	ret = dsmcc_process_msg_header(&section, data + DSMCC_MSGHDR_OFFSET);
	if (ret != 0)
	{
		DSMCC_ERROR("[receiver] Indication Msg Header error\n");
		return;
	}

	if (section.hdr.info.message_id == DSMCC_MESSAGE_DSI)
	{
		DSMCC_DEBUG("[receiver] Processing Server Gateway\n");
		dsmcc_process_section_gateway(status, data + DSMCC_DSI_OFFSET, length, pid);
	}
	else if (section.hdr.info.message_id == DSMCC_MESSAGE_DII)
	{
		DSMCC_DEBUG("[receiver] Processing Module Info\n");
		dsmcc_process_section_info(status, &section, data + DSMCC_DII_OFFSET, length);
	}
	else
	{
		DSMCC_ERROR("Unknown message ID (0x%h)\n", section.hdr.info.message_id); 
		/* TODO handle error */
	}
}

void dsmcc_free_cache_module_data(struct obj_carousel *car, struct cache_module_data *cachep)
{
	if (cachep->bstatus)
	{
		free(cachep->bstatus);
		cachep->bstatus = NULL;
	}

	if (cachep->descriptors)
	{
		dsmcc_desc_free_all(cachep->descriptors);
		cachep->descriptors = NULL;
	}

	if (cachep->data_file)
	{
		unlink(cachep->data_file);
		free(cachep->data_file);
		cachep->data_file = NULL;
	}
	
	if (cachep->prev)
	{
		cachep->prev->next = cachep->next;
		if (cachep->next)
			cachep->next->prev = cachep->prev;
	}
	else
	{
		car->cache = cachep->next;
		if (cachep->next)
			cachep->next->prev = NULL;
	}
	free(cachep);
}

void dsmcc_add_module_info(struct dsmcc_status *status, struct dsmcc_section *section, struct obj_carousel *car)
{
	int i, num_blocks, found;
	struct cache_module_data *cachep = car->cache;
	struct descriptor *desc, *last;
	struct dsmcc_dii *dii = &section->msg.dii;
	struct stream *str, *s;

	/* loop through modules and add to cache list if no module with
	 * same id or a different version. */

	for (i = 0; i < dii->number_modules; i++)
	{
		found = 0;

		while (cachep)
		{
			if (cachep->carousel_id == dii->download_id
					&& cachep->module_id == dii->modules[i].module_id)
			{
				if (cachep->version == dii->modules[i].module_version)
				{
					/* already known */
					DSMCC_DEBUG("[receiver] Already Know Module %d\n", dii->modules[i].module_id);
					found =  1;
					break;
				}
				else
				{
					/* Drop old data */
					DSMCC_DEBUG("[receiver] Updated Module %d\n", dii->modules[i].module_id);
					dsmcc_free_cache_module_data(car, cachep);
					break;
				}
			}
			cachep = cachep->next;
		}

		if (found == 0)
		{
			DSMCC_DEBUG("[receiver] Saving info for module %d\n", dii->modules[i].module_id);

			if (car->cache)
			{
				cachep = car->cache;
				while (cachep->next)
					cachep = cachep->next;
				cachep->next = (struct cache_module_data *) malloc(sizeof(struct cache_module_data));
				cachep->next->data_ptr = NULL;
				cachep->next->prev = cachep;
				cachep = cachep->next;
			}
			else
			{
				car->cache = (struct cache_module_data *) malloc(sizeof(struct cache_module_data));
				cachep = car->cache;
				cachep->prev = NULL;
			}

			cachep->carousel_id = dii->download_id;
			cachep->module_id = dii->modules[i].module_id;
			cachep->version = dii->modules[i].module_version;
			cachep->size = dii->modules[i].module_size;
			cachep->block_size = dii->block_size;
			cachep->curp = cachep->block_num = 0;
			num_blocks = cachep->size / dii->block_size;

			if((cachep->size % dii->block_size) != 0)
				num_blocks++;
			cachep->bstatus = (char*) malloc(((num_blocks / 8) + 1) * sizeof(char));
			bzero(cachep->bstatus, (num_blocks / 8) + 1);
			DSMCC_DEBUG("[receiver] Allocated %d bytes to store status for module %d\n", (num_blocks / 8) + 1, cachep->module_id);
			asprintf(&cachep->data_file, "/tmp/dsmcc-cache/tmp/%lu-%hu-%hhu.data", cachep->carousel_id, cachep->module_id, cachep->version);
			cachep->next = NULL;
			cachep->tag = dii->modules[i].modinfo.tap.assoc_tag;

			/* Subscribe to pid if not already */
			for (s = status->streams; s; s = s->next)
			{
				if (s->assoc_tag == cachep->tag)
				{
					if (!s->prev)
					{
						status->streams = s->next;
						status->streams->prev = NULL;
					}
					else
					{
						s->prev->next = s->next;
						if(s->next)
							s->next->prev = s->prev;
					}
					
					DSMCC_DEBUG("[receiver] Subscribing to (data) pid %d\n", s->pid);
					dsmcc_add_stream(status, s->pid);
					free(s);
				}
			}

			/* Steal the descriptors  TODO this is very bad... */
			cachep->descriptors = dii->modules[i].modinfo.descriptors;
			dii->modules[i].modinfo.descriptors = NULL;
			cachep->cached = 0;
		}
	}
}

int dsmcc_process_data_header(struct dsmcc_section *section, unsigned char *data, int length)
{
	struct dsmcc_data_header *hdr = &section->hdr.data;

	hdr->protocol = data[0];
	DSMCC_DEBUG("[receiver] Data -> Header - > Protocol %d\n", hdr->protocol);

	hdr->type = data[1];
	DSMCC_DEBUG("[receiver] Data -> Header - > Type %d\n", hdr->type);

	hdr->message_id = dsmcc_getshort(data + 2);
	DSMCC_DEBUG("[receiver] Data -> Header - > MessageID %d\n",hdr->message_id);

	hdr->download_id = dsmcc_getlong(data + 4);
	DSMCC_DEBUG("[receiver] Data -> Header - > DownloadID %ld\n", hdr->download_id);

	/* skip reserved byte */

	hdr->adaptation_len = data[9];
	DSMCC_DEBUG("[receiver] Data -> Header - > Adaptation Len %d\n", hdr->adaptation_len);

	hdr->message_len = dsmcc_getshort(data + 10);
	DSMCC_DEBUG("[receiver] Data -> Header - > Message Len %d\n", hdr->message_len);

	/* TODO adaptationHeader ?? */

	return 0;
}

int dsmcc_process_section_block(struct dsmcc_status *status, struct dsmcc_section *section, unsigned char *data, int length)
{
	struct dsmcc_ddb *ddb = &section->msg.ddb;

	ddb->module_id = dsmcc_getshort(data);
	DSMCC_DEBUG("[receiver] Data -> Block - > Module ID %u\n", ddb->module_id);

	ddb->module_version = data[2];
	DSMCC_DEBUG("[receiver] Data -> Block - > Module Version %u\n", ddb->module_version);

	/* skip reserved byte */

	ddb->block_number = dsmcc_getshort(data + 4);
	DSMCC_DEBUG("[receiver] Data -> Block - > Block Num %u\n", ddb->block_number);

	ddb->len = section->hdr.data.message_len - 6;
	ddb->next = NULL; /* Not used here, used to link all data blocks in order in AddModuleData. Hmmm. */

	dsmcc_add_module_data(status, section, data + 6);

	return 0;
}


void dsmcc_process_section_data(struct dsmcc_status *status, unsigned char *data, int length)
{
	struct dsmcc_section *section;

	section = (struct dsmcc_section *) malloc(sizeof(struct dsmcc_section));

	DSMCC_DEBUG("[receiver] Reading section header\n");
	dsmcc_process_section_header(section, data + DSMCC_SECTION_OFFSET, length);

	DSMCC_DEBUG("[receiver] Reading data header\n");
	dsmcc_process_data_header(section, data + DSMCC_DATAHDR_OFFSET, length);

	DSMCC_DEBUG("[receiver] Reading data \n");
	dsmcc_process_section_block(status, section, data + DSMCC_DDB_OFFSET, length);

	free(section);
}

void dsmcc_add_module_data(struct dsmcc_status *status, struct dsmcc_section *section, unsigned char *data)
{
	int i, ret, found = 0;
	unsigned long data_len = 0;
	struct cache_module_data *cachep = NULL;
	struct descriptor *desc = NULL;
	struct dsmcc_ddb *ddb = &section->msg.ddb;
	struct obj_carousel *car;
	int fd;
	off_t seeked;
	ssize_t wret;

	i = ret = 0;

	/* Scan through known modules and append data */

	for (i = 0; i < MAXCAROUSELS; i++)
	{
		car = &status->carousels[i];
		if (car->id == section->hdr.data.download_id)
			break;
	}

	if (!car)
	{
		DSMCC_DEBUG("[receiver] Data block for module in unknown carousel %ld\n", section->hdr.data.download_id);
		/* TODO carousel not yet known! is this possible ? */
		return;
	}

	DSMCC_DEBUG("[receiver] Data block on carousel %ld\n", car->id);

	cachep = car->cache;

	while (cachep)
	{
		if (cachep->carousel_id == section->hdr.data.download_id && cachep->module_id == ddb->module_id)
		{
			found = 1;
			DSMCC_DEBUG("[receiver] Found linking module (%d)...\n", ddb->module_id);
			break;
		}
		cachep = cachep->next;
	}

	/* Module info not found */
	if (found == 0)
	{
		DSMCC_DEBUG("[receiver] Linking module not found (%d)\n", ddb->module_id);
		return;
	}

	if (cachep->version == ddb->module_version)
	{
		if (cachep->cached)
		{
			DSMCC_DEBUG("[receiver] Cached complete module already %d\n", cachep->module_id);
			return; /* Already got it */
		}
		else
		{
			/* Check if we have this block already or not. If not append to list */

			if (BLOCK_GOT(cachep->bstatus, ddb->block_number) == 0)
			{
				fd = open(cachep->data_file, O_WRONLY | O_CREAT, 0666);
				if (fd < 0)
				{
					DSMCC_ERROR("[receiver] Can't open temporary file '%s' : %s\n", cachep->data_file, strerror(errno));
					return;
				}

				if ((seeked = lseek(fd, ddb->block_number * cachep->block_size, SEEK_SET)) < 0)
				{
					DSMCC_ERROR("[receiver] Can't seek '%s' : %s\n", cachep->data_file, strerror(errno));
					return;
				}

				if ((wret = write(fd, data, ddb->len)) < ddb->len)
				{
					if (wret >= 0)
					{
						DSMCC_ERROR("[receiver] Partial write '%s' : %d/%d\n", cachep->data_file, wret, ddb->len);
					}
					else
					{
						DSMCC_ERROR("[receiver] Write error '%s' : %s\n", cachep->data_file, strerror(errno));
					}
					close(fd);
					return;
				}

				close(fd);

				cachep->curp += ddb->len;
				BLOCK_SET(cachep->bstatus, ddb->block_number);
			}
		}

		DSMCC_DEBUG("[receiver] Module %d Current Size %ld Total Size %ld\n", cachep->module_id, cachep->curp, cachep->size);

		if (cachep->curp >= cachep->size)
		{
			DSMCC_DEBUG("[receiver] Reconstructing module %d from blocks\n", cachep->module_id);

			/* Uncompress.... TODO - scan for compressed descriptor */
			for (desc = cachep->descriptors; desc != NULL; desc = desc->next)
			{
				if (desc && (desc->tag == 0x09))
					break;
			}
			if (desc)
			{
				DSMCC_DEBUG("[receiver] Compression disabled - Skipping\n");
				if(cachep->data_file)
				{
					unlink(cachep->data_file);
					free(cachep->data_file);
					cachep->data_file = NULL;
				}
				cachep->curp = 0;
				return;
			}
			else
			{
				/* not compressed */
				DSMCC_DEBUG("[receiver] Processing data (uncompressed)\n");
				// Return list of streams that directory needs
				dsmcc_biop_process_data(car->filecache, cachep);
				cachep->cached = 1;
			}
		}
	}
}

void dsmcc_process_section_desc(unsigned char *data, int length)
{
	struct dsmcc_section section;
	int ret;

	ret = dsmcc_process_section_header(&section, data + DSMCC_SECTION_OFFSET, length);
	/* TODO */
}

void dsmcc_process_section(struct dsmcc_status *status, unsigned char *data, int length, int pid)
{
	unsigned long crc32_decode;
	unsigned short section_len;
	int full_cache = 1;
	int i;
	unsigned int result;

	/* Check CRC before trying to parse */

	section_len = dsmcc_getshort(data + 1) & 0xFFF;
	section_len += 3;/* 3 bytes before length count starts */

	crc32_decode = dsmcc_crc32(data, section_len);

	DSMCC_DEBUG("[receiver] Length %d CRC - %lX \n", section_len, crc32_decode);

	if (crc32_decode != 0)
	{
		DSMCC_ERROR("[receiver] Dropping corrupt section (Got CRC %lX)\n", crc32_decode);
		return;
	}

	switch (data[0])
	{
		case DSMCC_SECTION_INDICATION:
			DSMCC_DEBUG("[receiver] Server/Info Section\n");
			dsmcc_process_section_indication(status, data, length, pid);
			break;
		case DSMCC_SECTION_DATA:
			DSMCC_DEBUG("[receiver] Data Section\n");
			dsmcc_process_section_data(status, data, length);
			break;
		case DSMCC_SECTION_DESCR:
			DSMCC_DEBUG("[receiver] Descriptor Section\n");
			dsmcc_process_section_desc(data, length);
			break;
		default:
			DSMCC_ERROR("[receiver] Unknown Section (0x%h)\n", data[0]);
			break;
	}
}
