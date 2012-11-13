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

#include "dsmcc.h"
#include "dsmcc-descriptor.h"
#include "dsmcc-biop-ior.h"
#include "dsmcc-biop-module.h"
#include "dsmcc-biop-message.h"
#include "dsmcc-cache-module.h"
#include "dsmcc-util.h"
#include "dsmcc-carousel.h"
#include "dsmcc-section.h"

struct dsmcc_section_header
{
	char           table_id;
	unsigned short table_id_extension;
	int            length;
};

struct dsmcc_message_header
{
	unsigned short message_id;
	unsigned long  transaction_id;
	unsigned short message_length;
};

struct dsmcc_data_header
{
	unsigned long  download_id;
	unsigned short message_length;
};

/**
  * returns number of bytes to skip to get to next data or -1 on error
  */
static int dsmcc_parse_section_header(struct dsmcc_section_header *header, unsigned char *data, int data_length)
{
	int off = 0;
	int section_syntax_indicator;
	int private_indicator;
	int crc;

	(void) data_length; /* TODO check data length */

	header->table_id = data[off];
	off++;

	header->length = dsmcc_getshort(data + off);
	off += 2;
	section_syntax_indicator = ((header->length & 0x8000) != 0);
	private_indicator = ((header->length & 0x4000) != 0);
	header->length &= 0xFFF;

	/* Check CRC is set and private_indicator is set to its complement, else skip packet */
	if (!(section_syntax_indicator ^ private_indicator))
	{
		DSMCC_ERROR("Invalid section header: section_syntax_indicator and private_indicator flags are not complements (%d/%d)", section_syntax_indicator, private_indicator);
		return -1; /* Invalid section */
	}

	/* Check CRC */
	crc = dsmcc_crc32(data, header->length + off);
	if (crc != 0)
	{
		DSMCC_ERROR("Dropping corrupt section (Got CRC 0x%lx)", crc);
		return -1; /* Invalid section */
	}

	header->table_id_extension = dsmcc_getshort(data + off);
	off += 2;

	// skip unused fields
	off += 3;

	return off;
}

/*
 * returns number of bytes to skip to get to next data or -1 on error
 * ETSI TR 101 202 Table A.1
 */
static int dsmcc_parse_message_header(struct dsmcc_message_header *header, unsigned char *data, int data_length)
{
	int off = 0;
	unsigned char protocol, type, adaptation_length;

	(void) data_length; /* TODO check data length */

	protocol = data[off];
	off++;
	if (protocol != 0x11)
	{
		DSMCC_ERROR("Message header has protocol 0x%x (expected 0x%x)", protocol, 0x11);
		return -1;
	}

	type = data[off];
	off++;
	if (type != 0x3)
	{
		DSMCC_ERROR("Message header has type 0x%x (expected 0x%x)", protocol, 0x3);
		return -1;
	}

	header->message_id = dsmcc_getshort(data + off);
	off += 2;
	DSMCC_DEBUG("MessageID 0x%lx", header->message_id);

	header->transaction_id = dsmcc_getlong(data + off);
	off += 4;
	DSMCC_DEBUG("TransactionID 0x%lx", header->transaction_id);

	/* skip reserved byte */
	off += 1;

	adaptation_length = data[off];
	off++;
	DSMCC_DEBUG("Adaptation Length %d", adaptation_length);

	header->message_length = dsmcc_getshort(data + off) - adaptation_length;
	off += 2;
	DSMCC_DEBUG("Message Length %d (excluding adaption header)", header->message_length);

	/* skip adaptation header */
	off += adaptation_length;

	return off;

}

/*
 * ETSI TR 101 202 Table 4.15
 */
static int dsmcc_parse_biop_service_gateway_info(struct dsmcc_object_carousel *car, unsigned char *data, int data_length)
{
	int off = 0, ret;
	unsigned char tmp;

	ret = dsmcc_biop_parse_ior(car->gateway_ior, data + off, data_length - off);
	if (ret < 0)
	{
		dsmcc_biop_free_ior(car->gateway_ior);
		car->gateway_ior = NULL;
		return -1;
	}
	off += ret;

	if (car->gateway_ior->type != IOR_TYPE_DSM_SERVICE_GATEWAY)
	{
		DSMCC_ERROR("Expected an IOR of type DSM:ServiceGateway, but got \"%s\"", dsmcc_biop_get_ior_type_str(car->gateway_ior->type));
		dsmcc_biop_free_ior(car->gateway_ior);
		car->gateway_ior = NULL;
		return -1;
	}

	/* Init carousel id */
	if (car->id == 0)
	       car->id = car->gateway_ior->profile_body.obj_loc.carousel_id;

	DSMCC_DEBUG("Gateway for carousel %ld, module %d", car->id, car->gateway_ior->profile_body.obj_loc.module_id);

	/* Subscribe to stream if not already */
	DSMCC_DEBUG("Subscribing to stream with assoc_tag 0x%x", car->gateway_ior->profile_body.conn_binder.assoc_tag);
	dsmcc_object_carousel_stream_subscribe(car, car->gateway_ior->profile_body.conn_binder.assoc_tag);

	/* Download Taps count, should be 0 */
	tmp = data[off];
	off++;
	if (tmp != 0)
	{
		DSMCC_ERROR("Download Taps count should be 0 but is %d", tmp);
		dsmcc_biop_free_ior(car->gateway_ior);
		car->gateway_ior = NULL;
		return -1;
	}

	/* Service Context List count, should be 0 */
	tmp = data[off];
	off++;
	if (tmp != 0)
	{
		DSMCC_ERROR("Service Context List count should be 0 but is %d", tmp);
		dsmcc_biop_free_ior(car->gateway_ior);
		car->gateway_ior = NULL;
		return -1;
	}

	/* TODO parse descriptors in user_data, for now just skip it */
	tmp = data[off];
	off++;
	off += tmp;

	return off;
}

/*
 * ETSI TR 101 202 Table A.3
 */
static int dsmcc_parse_section_dsi(struct dsmcc_state *state, unsigned char *data, int data_length, int pid)
{
        int off = 0, ret;
	unsigned short i, dsi_data_length;
	struct dsmcc_object_carousel *car;

	DSMCC_DEBUG("Setting gateway for pid %d", pid);

	/* Find which object carousel this pid's data belongs to */
	for (car = state->carousels; car; car = car->next)
	{
		if (dsmcc_find_stream_by_pid(car->streams, pid))
		{
			if (car->gateway_ior)
			{
				/* TODO check gateway version not changed */
				DSMCC_DEBUG("Already got gateway for pid %d", pid);
				return 0; /* We already have gateway */
			}
			else
				break;
		}
	}
	if (!car)
	{ 
		DSMCC_DEBUG("Gateway for unknown carousel");
		return 0;
	}

	car->gateway_ior = malloc(sizeof(struct biop_ior));
	memset(car->gateway_ior, 0, sizeof(struct biop_ior));

	/* skip unused Server ID */
	/* 0-19 Server id = 20 * 0xFF */
	off += 20;

	/* compatibility descriptor length, should be 0 */
	i = dsmcc_getshort(data + off);
	off += 2;
	if (i != 0)
	{
		DSMCC_ERROR("Compatibility descriptor should be 0 but is %d", i);
		free(car->gateway_ior);
		car->gateway_ior = NULL;
		return -1;
	}

	dsi_data_length = dsmcc_getshort(data + off);
	off += 2;
	DSMCC_DEBUG("Data Length: %d", dsi_data_length);

	DSMCC_DEBUG("Processing BIOP::ServiceGatewayInfo...");
	ret = dsmcc_parse_biop_service_gateway_info(car, data + off, data_length - off);
	if (ret < 0)
	{
		DSMCC_ERROR("DSI -> dsmcc_parse_biop_service_gateway_info returned %d", ret);
		dsmcc_biop_free_ior(car->gateway_ior);
		car->gateway_ior = NULL;
		return -1;
	}
	off += dsi_data_length;

	return off;
}

/*
 * ETSI TR 101 202 Table A.4
 */
static int dsmcc_parse_section_dii(struct dsmcc_state *state, unsigned char *data, int data_length)
{
	struct dsmcc_object_carousel *car;
	int off = 0, ret;
	unsigned short i, number_modules;
	struct dsmcc_dii dii;

	dii.download_id = dsmcc_getlong(data);
	off += 4;
	DSMCC_DEBUG("Info -> Download ID = %lX", dii.download_id);

	car = dsmcc_find_carousel_by_id(state->carousels, dii.download_id);
	if (!car)
	{
		DSMCC_ERROR("Section Info for unknown carousel %ld", dii.download_id);
		return -1;
	}

	dii.block_size = dsmcc_getshort(data + off);
	off += 2;
	DSMCC_DEBUG("Info -> Block Size = %d", dii.block_size);

	/* skip unused fields */
	off += 10;

	/* compatibility descriptor length, should be 0 */
	i = dsmcc_getshort(data + off);
	off += 2;
	if (i != 0)
	{
		DSMCC_ERROR("Compatibility descriptor should be 0 but is %d", i);
		return -1;
	}

	number_modules = dsmcc_getshort(data + off);
	off += 2;
	DSMCC_DEBUG("Info -> number of modules = %d", number_modules);

	for (i = 0; i < number_modules; i++)
	{
		struct dsmcc_module_info dmi;
		struct biop_module_info *bmi;
		unsigned char module_info_length;

		dmi.module_id = dsmcc_getshort(data + off);
		off += 2;
		dmi.module_size = dsmcc_getlong(data + off);
		off += 4;
		dmi.module_version = data[off];
		off++;
		module_info_length = data[off];
		off++;

		DSMCC_DEBUG("Module ID %d -> Size %ld Version %d", dmi.module_id, dmi.module_size, dmi.module_version);

		bmi = malloc(sizeof(struct biop_module_info));
		ret = dsmcc_biop_parse_module_info(bmi, data + off, data_length - off);
		if (ret < 0)
		{
			DSMCC_ERROR("Info -> dsmcc_biop_parse_module_info returned %d", ret);
			dsmcc_biop_free_module_info(bmi);
			return -1;
		}
		off += module_info_length;

		dsmcc_add_cached_module_info(state, car, &dii, &dmi, bmi);
		dsmcc_biop_free_module_info(bmi);
	}

	/* skip private_data */
	i = dsmcc_getshort(data + off);
	off += i;
	DSMCC_DEBUG("Info -> Private Data Length = %d", i);

	return off;
}

static int dsmcc_parse_section_control(struct dsmcc_state *state, unsigned char *data, int data_length, int pid)
{
	struct dsmcc_message_header header;
	int off = 0, ret;

	ret = dsmcc_parse_message_header(&header, data, data_length);
	if (ret < 0)
		return -1;
	off += ret;

	/* adjust data pointer to skip message header */
	data += ret;
	data_length -= ret;

	switch (header.message_id)
	{
		case 0x1006:
			DSMCC_DEBUG("Processing Download-ServerInitiate message");
			ret = dsmcc_parse_section_dsi(state, data, data_length, pid);
			if (ret < 0)
			{
				DSMCC_ERROR("dsmcc_parse_section_dsi returned %d", ret);
				return -1;
			}
			break;
		case 0x1002:
			DSMCC_DEBUG("Processing Download-InfoIndication message");
			ret = dsmcc_parse_section_dii(state, data, data_length);
			if (ret < 0)
			{
				DSMCC_ERROR("dsmcc_parse_section_dii returned %d", ret);
				return -1;
			}
			break;
		default:
			DSMCC_ERROR("Unknown message ID (0x%x)", header.message_id);
			return -1;
	}

	off += header.message_length;

	return off;
}

/*
 * ETSI TR 101 202 Table A.2
 */
static int dsmcc_parse_data_header(struct dsmcc_data_header *header, unsigned char *data, int length)
{
	int off = 0;
	unsigned char protocol, type, adaptation_length;
	unsigned short message_id;

	(void) length; /* TODO check data length */

	protocol = data[off];
	off++;
	if (protocol != 0x11)
	{
		DSMCC_ERROR("Data header has protocol 0x%x (expected 0x%x)", protocol, 0x11);
		return -1;
	}

	type = data[off];
	off++;
	if (type != 0x3)
	{
		DSMCC_ERROR("Data header has type 0x%x (expected 0x%x)", protocol, 0x3);
		return -1;
	}

	message_id = dsmcc_getshort(data + off);
	off += 2;
	if (message_id != 0x1003)
	{
		DSMCC_ERROR("Data header has message ID 0x%x (expected 0x%x)", protocol, 0x1003);
		return -1;
	}

	header->download_id = dsmcc_getlong(data + off);
	off += 4;
	DSMCC_DEBUG("DownloadID 0x%lx", header->download_id);

	/* skip reserved byte */
	off += 1;

	adaptation_length = data[off];
	off++;
	DSMCC_DEBUG("Adaptation Length %d", adaptation_length);

	header->message_length = dsmcc_getshort(data + off) - adaptation_length;
	off += 2;
	DSMCC_DEBUG("Message Length %d (excluding adaption header)", header->message_length);

	/* skip adaptation header */
	off += adaptation_length;

	return off;
}

/*
 * ETSI TR 101 202 Table A.5
 */
static int dsmcc_parse_section_ddb(struct dsmcc_state *state, struct dsmcc_data_header *header, unsigned char *data, int data_length)
{
	int off = 0;
	struct dsmcc_ddb ddb;

	ddb.module_id = dsmcc_getshort(data + off);
	off += 2;
	DSMCC_DEBUG("Module ID %u", ddb.module_id);

	ddb.module_version = data[off];
	off++;
	DSMCC_DEBUG("Module Version %u", ddb.module_version);

	/* skip reserved byte */
	off++;

	ddb.number = dsmcc_getshort(data + off);
	off += 2;
	DSMCC_DEBUG("Block Number %u", ddb.number);

	ddb.length = header->message_length - off;
	DSMCC_DEBUG("Block Length %ld", ddb.length);

	dsmcc_save_cached_module_data(state, header->download_id, &ddb, data + off, data_length - off);
	off += ddb.length;

	return off;
}

static int dsmcc_parse_section_data(struct dsmcc_state *state, unsigned char *data, int data_length)
{
	struct dsmcc_data_header header;
	int off = 0, ret;

	ret = dsmcc_parse_data_header(&header, data, data_length);
	if (ret < 0)
		return -1;
	off += ret;

	ret = dsmcc_parse_section_ddb(state, &header, data + off, data_length - off);
	if (ret < 0)
		return -1;
	off += header.message_length;

	return off;
}

int dsmcc_parse_section(struct dsmcc_state *state, int pid, unsigned char *data, int data_length)
{
	int off = 0, ret;
	struct dsmcc_section_header header;

	ret = dsmcc_parse_section_header(&header, data, data_length);
	if (ret < 0)
		return 0;
	off += ret;

	DSMCC_DEBUG("Processing section (length %d)", header.length);

	switch (header.table_id)
	{
		case 0x3B:
			DSMCC_DEBUG("DSI/DII Section");
			ret = dsmcc_parse_section_control(state, data + off, data_length - off, pid);
			if (ret < 0)
			{
				DSMCC_ERROR("dsmcc_parse_section_control returned %d", ret);
				return 0;
			}
			break;
		case 0x3C:
			DSMCC_DEBUG("DDB Section");
			ret = dsmcc_parse_section_data(state, data + off, data_length - off);
			if (ret < 0)
			{
				DSMCC_ERROR("dsmcc_parse_section_data returned %d", ret);
				return 0;
			}
			break;
		default:
			DSMCC_ERROR("Unknown section (table ID is 0x%02x)", header.table_id);
			return 0;
	}

	return 1;
}
