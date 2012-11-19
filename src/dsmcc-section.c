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
	uint8_t  table_id;
	uint16_t table_id_extension;
	uint16_t length;
};

struct dsmcc_message_header
{
	uint16_t message_id;
	uint32_t transaction_id;
	uint16_t message_length;
};

struct dsmcc_data_header
{
	uint32_t download_id;
	uint16_t message_length;
};

/**
  * returns number of bytes to skip to get to next data or -1 on error
  */
static int dsmcc_parse_section_header(struct dsmcc_section_header *header, uint8_t *data, int data_length)
{
	int off = 0;
	int section_syntax_indicator;
	int private_indicator;
	int crc;

	if (!dsmcc_getbyte(&header->table_id, data, off, data_length))
		return -1;
	off++;

	if (!dsmcc_getshort(&header->length, data, off, data_length))
		return -1;
	off += 2;
	section_syntax_indicator = ((header->length & 0x8000) != 0);
	private_indicator = ((header->length & 0x4000) != 0);
	header->length &= 0xFFF;

	/* Check CRC is set and private_indicator is set to its complement, else skip packet */
	if (!(section_syntax_indicator ^ private_indicator))
	{
		DSMCC_ERROR("Invalid section header: section_syntax_indicator and private_indicator flags are not complements (%d/%d)", section_syntax_indicator, private_indicator);
		return -1;
	}

	/* Check CRC */
	crc = dsmcc_crc32(data, header->length + off);
	if (crc != 0)
	{
		DSMCC_ERROR("Dropping corrupt section (Got CRC 0x%08x)", crc);
		return -1;
	}

	if (!dsmcc_getshort(&header->table_id_extension, data, off, data_length))
		return -1;
	off += 2;

	// skip unused fields
	off += 3;

	return off;
}

/*
 * returns number of bytes to skip to get to next data or -1 on error
 * ETSI TR 101 202 Table A.1
 */
static int dsmcc_parse_message_header(struct dsmcc_message_header *header, uint8_t *data, int data_length)
{
	int off = 0;
	uint8_t protocol, type, adaptation_length;

	if (!dsmcc_getbyte(&protocol, data, off, data_length))
		return -1;
	off++;
	if (protocol != 0x11)
	{
		DSMCC_ERROR("Message Header: invalid protocol 0x%02hhx (expected 0x11)", protocol);
		return -1;
	}

	if (!dsmcc_getbyte(&type, data, off, data_length))
		return -1;
	off++;
	if (type != 0x3)
	{
		DSMCC_ERROR("Message Header: invalid type 0x%02hhx (expected 0x03)", protocol);
		return -1;
	}

	if (!dsmcc_getshort(&header->message_id, data, off, data_length))
		return -1;
	off += 2;
	DSMCC_DEBUG("Message Header: MessageID 0x%hx", header->message_id);

	if (!dsmcc_getlong(&header->transaction_id, data, off, data_length))
		return -1;
	off += 4;
	DSMCC_DEBUG("Message Header: TransactionID 0x%x", header->transaction_id);

	/* skip reserved byte */
	off += 1;

	if (!dsmcc_getbyte(&adaptation_length, data, off, data_length))
		return -1;
	off++;
	DSMCC_DEBUG("Message Header: Adaptation Length %hhu", adaptation_length);

	if (!dsmcc_getshort(&header->message_length, data, off, data_length))
		return -1;
	off += 2;
	header->message_length -= adaptation_length;
	DSMCC_DEBUG("Message Header: Message Length %d (excluding adaption header)", header->message_length);

	/* skip adaptation header */
	off += adaptation_length;

	return off;
}

/*
 * ETSI TR 101 202 Table 4.15
 */
static int dsmcc_parse_biop_service_gateway_info(struct biop_ior *gateway_ior, uint8_t *data, int data_length)
{
	int off = 0, ret;
	uint8_t tmp;

	ret = dsmcc_biop_parse_ior(gateway_ior, data + off, data_length - off);
	if (ret < 0)
		goto error;
	off += ret;

	if (gateway_ior->type != IOR_TYPE_DSM_SERVICE_GATEWAY)
	{
		DSMCC_ERROR("Service Gateway: Expected an IOR of type DSM:ServiceGateway, but got \"%s\"", dsmcc_biop_get_ior_type_str(gateway_ior->type));
		goto error;
	}

	/* Download Taps count, should be 0 */
	if (!dsmcc_getbyte(&tmp, data, off, data_length))
		goto error;
	off++;
	if (tmp != 0)
	{
		DSMCC_ERROR("Service Gateway: Download Taps count should be 0 but is %d", tmp);
		goto error;
	}

	/* Service Context List count, should be 0 */
	if (!dsmcc_getbyte(&tmp, data, off, data_length))
		goto error;
	off++;
	if (tmp != 0)
	{
		DSMCC_ERROR("Service Gateway: Service Context List count should be 0 but is %d", tmp);
		goto error;
	}

	/* TODO parse descriptors in user_data, for now just skip it */
	if (!dsmcc_getbyte(&tmp, data, off, data_length))
		goto error;
	off++;
	off += tmp;

	return off;

error:
	dsmcc_biop_free_ior(gateway_ior);
	return -1;
}

/*
 * ETSI TR 101 202 Table A.3
 */
static int dsmcc_parse_section_dsi(struct dsmcc_state *state, struct dsmcc_object_carousel *carousel, uint8_t *data, int data_length)
{
        int off = 0, ret;
	uint16_t i, dsi_data_length;
	struct biop_ior *gateway_ior;
	struct dsmcc_queue_entry *dii_entry;

	/* skip unused Server ID */
	/* 0-19 Server id = 20 * 0xFF */
	off += 20;

	/* compatibility descriptor length, should be 0 */
	if (!dsmcc_getshort(&i, data, off, data_length))
		return -1;
	off += 2;
	if (i != 0)
	{
		DSMCC_ERROR("DSI: Compatibility descriptor length should be 0 but is %d", i);
		return -1;
	}

	if (!dsmcc_getshort(&dsi_data_length, data, off, data_length))
		return -1;
	off += 2;
	DSMCC_DEBUG("DSI: Data Length %d", dsi_data_length);
	if (dsi_data_length > data_length - off)
	{
		DSMCC_ERROR("DSI: Data buffer overflow (need %d bytes but only got %d)", dsi_data_length, data_length - off );
		return -1;
	}

	DSMCC_DEBUG("DSI: Processing BIOP::ServiceGatewayInfo...");
	gateway_ior = calloc(1, sizeof(struct biop_ior));
	ret = dsmcc_parse_biop_service_gateway_info(gateway_ior, data + off, dsi_data_length);
	if (ret < 0)
	{
		dsmcc_biop_free_ior(gateway_ior);
		return -1;
	}
	off += dsi_data_length;

	/* Init carousel id */
	/* TODO handle case when cid is already set */
	carousel->cid = gateway_ior->profile_body.obj_loc.carousel_id;

	/* Queue entry for DII */
	dii_entry = calloc(1, sizeof(struct dsmcc_queue_entry));
	dii_entry->carousel = carousel;
	dii_entry->type = DSMCC_QUEUE_ENTRY_DII;
	dii_entry->id = gateway_ior->profile_body.conn_binder.transaction_id;
	dsmcc_stream_queue_add(state, DSMCC_STREAM_SELECTOR_ASSOC_TAG, gateway_ior->profile_body.conn_binder.assoc_tag, dii_entry);

	dsmcc_biop_free_ior(gateway_ior);

	return off;
}

/*
 * ETSI TR 101 202 Table A.4
 */
static int dsmcc_parse_section_dii(struct dsmcc_state *state, struct dsmcc_object_carousel *carousel, uint8_t *data, int data_length)
{
	int off = 0, ret;
	uint16_t i, number_modules;
	struct dsmcc_dii dii;

	if (!dsmcc_getlong(&dii.download_id, data, off, data_length))
		return -1;
	off += 4;
	DSMCC_DEBUG("DII: Download ID %lX", dii.download_id);

	if (!dsmcc_getshort(&dii.block_size, data, off, data_length))
		return -1;
	off += 2;
	DSMCC_DEBUG("DII: Block Size %d", dii.block_size);

	/* skip unused fields */
	off += 10;

	/* compatibility descriptor length, should be 0 */
	if (!dsmcc_getshort(&i, data, off, data_length))
		return -1;
	off += 2;
	if (i != 0)
	{
		DSMCC_ERROR("DII: Compatibility descriptor should be 0 but is %d", i);
		return -1;
	}

	if (!dsmcc_getshort(&number_modules, data, off, data_length))
		return -1;
	off += 2;
	DSMCC_DEBUG("DII: Number of modules = %d", number_modules);

	for (i = 0; i < number_modules; i++)
	{
		struct dsmcc_module_info dmi;
		struct biop_module_info *bmi;
		uint8_t module_info_length;

		if (!dsmcc_getshort(&dmi.module_id, data, off, data_length))
			return -1;
		off += 2;
		if (!dsmcc_getlong(&dmi.module_size, data, off, data_length))
			return -1;
		off += 4;
		if (!dsmcc_getbyte(&dmi.module_version, data, off, data_length))
			return -1;
		off++;
		if (!dsmcc_getbyte(&module_info_length, data, off, data_length))
			return -1;
		off++;

		DSMCC_DEBUG("DII: Module ID %d: Size %ld Version %d", dmi.module_id, dmi.module_size, dmi.module_version);

		bmi = malloc(sizeof(struct biop_module_info));
		ret = dsmcc_biop_parse_module_info(bmi, data + off, data_length - off);
		if (ret < 0)
		{
			dsmcc_biop_free_module_info(bmi);
			return -1;
		}
		off += module_info_length;

		dsmcc_add_cached_module_info(state, carousel, &dii, &dmi, bmi);
		dsmcc_biop_free_module_info(bmi);
	}

	/* skip private_data */
	if (!dsmcc_getshort(&i, data, off, data_length))
		return -1;
	off += 2;
	DSMCC_DEBUG("DII: Private Data Length = %d", i);
	off += i;

	return off;
}

static int dsmcc_parse_section_control(struct dsmcc_state *state, struct dsmcc_stream *stream, uint8_t *data, int data_length)
{
	struct dsmcc_message_header header;
	int off = 0, ret;
	struct dsmcc_queue_entry *entry;

	ret = dsmcc_parse_message_header(&header, data, data_length);
	if (ret < 0)
		return -1;
	off += ret;

	switch (header.message_id)
	{
		case 0x1006:
			DSMCC_DEBUG("Processing Download-ServerInitiate message for stream with PID 0x%hx", stream->pid);
			entry = dsmcc_stream_find_queue_entry(stream, DSMCC_QUEUE_ENTRY_DSI, header.transaction_id);
			if (entry)
			{
				ret = dsmcc_parse_section_dsi(state, entry->carousel, data + off, data_length - off);
				if (ret < 0)
					return -1;
			}
			else
				DSMCC_DEBUG("Skipping unrequested DSI");
			break;
		case 0x1002:
			DSMCC_DEBUG("Processing Download-InfoIndication message for stream with PID 0x%hx", stream->pid);
			entry = dsmcc_stream_find_queue_entry(stream, DSMCC_QUEUE_ENTRY_DII, header.transaction_id);
			if (entry)
			{
				ret = dsmcc_parse_section_dii(state, entry->carousel, data + off, data_length - off);
				if (ret < 0)
					return -1;
			}
			else
				DSMCC_DEBUG("Skipping unrequested DII");
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
static int dsmcc_parse_data_header(struct dsmcc_data_header *header, uint8_t *data, int data_length)
{
	int off = 0;
	uint8_t protocol, type, adaptation_length;
	uint16_t message_id;

	if (!dsmcc_getbyte(&protocol, data, off, data_length))
		return -1;
	off++;
	if (protocol != 0x11)
	{
		DSMCC_ERROR("Data Header: invalid protocol 0x%x (expected 0x%x)", protocol, 0x11);
		return -1;
	}

	if (!dsmcc_getbyte(&type, data, off, data_length))
		return -1;
	off++;
	if (type != 0x3)
	{
		DSMCC_ERROR("Data Header: invalid type 0x%x (expected 0x%x)", protocol, 0x3);
		return -1;
	}

	if (!dsmcc_getshort(&message_id, data, off, data_length))
		return -1;
	off += 2;
	if (message_id != 0x1003)
	{
		DSMCC_ERROR("Data Header: invalid message ID 0x%x (expected 0x%x)", protocol, 0x1003);
		return -1;
	}

	if (!dsmcc_getlong(&header->download_id, data, off, data_length))
		return -1;
	off += 4;
	DSMCC_DEBUG("Data Header: Download ID 0x%lx", header->download_id);

	/* skip reserved byte */
	off += 1;

	if (!dsmcc_getbyte(&adaptation_length, data, off, data_length))
		return -1;
	off++;
	DSMCC_DEBUG("Data Header: Adaptation Length %d", adaptation_length);

	if (!dsmcc_getshort(&header->message_length, data, off, data_length))
		return -1;
	off += 2;
	header->message_length -= adaptation_length;
	DSMCC_DEBUG("Data Header: Message Length %d (excluding adaption header)", header->message_length);

	/* skip adaptation header */
	off += adaptation_length;

	return off;
}

/*
 * ETSI TR 101 202 Table A.5
 */
static int dsmcc_parse_section_ddb(struct dsmcc_state *state, struct dsmcc_object_carousel *carousel, struct dsmcc_data_header *header, uint8_t *data, int data_length)
{
	int off = 0;
	struct dsmcc_ddb ddb;

	if (!dsmcc_getshort(&ddb.module_id, data, off, data_length))
		return -1;
	off += 2;
	DSMCC_DEBUG("DDB: Module ID %u", ddb.module_id);

	if (!dsmcc_getbyte(&ddb.module_version, data, off, data_length))
		return -1;
	off++;
	DSMCC_DEBUG("DDB: Module Version %u", ddb.module_version);

	/* skip reserved byte */
	off++;

	if (!dsmcc_getshort(&ddb.number, data, off, data_length))
		return -1;
	off += 2;
	DSMCC_DEBUG("DDB: Block Number %u", ddb.number);

	ddb.length = header->message_length - off;
	DSMCC_DEBUG("DDB: Block Length %ld", ddb.length);

	dsmcc_save_cached_module_data(state, carousel, &ddb, data + off, data_length - off);
	off += ddb.length;

	return off;
}

static int dsmcc_parse_section_data(struct dsmcc_state *state, struct dsmcc_stream *stream, uint8_t *data, int data_length)
{
	struct dsmcc_data_header header;
	int off = 0, ret;
	struct dsmcc_queue_entry *entry;

	DSMCC_DEBUG("Parsing DDB section for stream with PID %d", stream->pid);

	ret = dsmcc_parse_data_header(&header, data, data_length);
	if (ret < 0)
		return -1;
	off += ret;

	entry = dsmcc_stream_find_queue_entry(stream, DSMCC_QUEUE_ENTRY_DDB, header.download_id);
	if (entry)
	{
		ret = dsmcc_parse_section_ddb(state, entry->carousel, &header, data + off, data_length - off);
		if (ret < 0)
			return -1;
	}
	else
		DSMCC_DEBUG("Skipping unrequested DDB");

	off += header.message_length;

	return off;
}

int dsmcc_parse_section(struct dsmcc_state *state, uint16_t pid, uint8_t *data, int data_length)
{
	int off = 0, ret;
	struct dsmcc_section_header header;
	struct dsmcc_stream *stream;

	stream = dsmcc_find_stream(state, pid);
	if (!stream)
	{
		DSMCC_WARN("Skipping section for unknown PID 0x%hx", pid);
		return 0;
	}

	ret = dsmcc_parse_section_header(&header, data, data_length);
	if (ret < 0)
		return 0;
	off += ret;

	DSMCC_DEBUG("Processing section (length %d)", header.length);

	switch (header.table_id)
	{
		case 0x3B:
			DSMCC_DEBUG("DSI/DII Section");
			ret = dsmcc_parse_section_control(state, stream, data + off, data_length - off);
			if (ret < 0)
				return 0;
			break;
		case 0x3C:
			DSMCC_DEBUG("DDB Section");
			ret = dsmcc_parse_section_data(state, stream, data + off, data_length - off);
			if (ret < 0)
				return 0;
			break;
		default:
			DSMCC_ERROR("Unknown section (table ID is 0x%02x)", header.table_id);
			return 0;
	}

	return 1;
}
