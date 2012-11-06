#include <stdlib.h>
#include <string.h>

#include "dsmcc.h"
#include "dsmcc-descriptor.h"
#include "dsmcc-util.h"

struct dsmcc_descriptor *dsmcc_find_descriptor_by_type(struct dsmcc_descriptor *descriptors, int type)
{
	while (descriptors)
	{
		if (descriptors->type == type)
			break;
		descriptors = descriptors->next;
	}
	return descriptors;
}

void dsmcc_descriptors_free_all(struct dsmcc_descriptor *descriptors)
{
	while (descriptors)
	{
		struct dsmcc_descriptor *next = descriptors->next;
		/* free additional memory for some descriptor types */
		switch (descriptors->type) {
			case DSMCC_DESCRIPTOR_TYPE:
				free(descriptors->data.type.text);
				break;
			case DSMCC_DESCRIPTOR_NAME:
				free(descriptors->data.name.text);
				break;
			case DSMCC_DESCRIPTOR_INFO:
				free(descriptors->data.info.lang_code);
				free(descriptors->data.info.text);
				break;
			case DSMCC_DESCRIPTOR_LABEL:
				free(descriptors->data.label.text);
				break;
			case DSMCC_DESCRIPTOR_CONTENT_TYPE:
				free(descriptors->data.content_type.text);
				break;
		}
		free(descriptors);
		descriptors = next;
	}
}

static void dsmcc_parse_type_descriptor(struct dsmcc_descriptor *desc, unsigned char *data, int length)
{
	struct dsmcc_descriptor_type *type = &desc->data.type;

	desc->type = DSMCC_DESCRIPTOR_TYPE;
	type->text = (char *) malloc(length);
	memcpy(type->text, data, length);

	DSMCC_DEBUG("Type descriptor, text='%s'", type->text);
}

static void dsmcc_parse_name_descriptor(struct dsmcc_descriptor *desc, unsigned char *data, int length)
{
	struct dsmcc_descriptor_name *name = &desc->data.name;

	desc->type = DSMCC_DESCRIPTOR_NAME;
	name->text = (char *)malloc(length);
	memcpy(name->text, data, length);

	DSMCC_DEBUG("Name descriptor, text='%s'", name->text);
}

static void dsmcc_parse_info_descriptor(struct dsmcc_descriptor *desc, unsigned char *data, int length)
{
	struct dsmcc_descriptor_info *info = &desc->data.info;

	desc->type = DSMCC_DESCRIPTOR_INFO;
	memcpy(info->lang_code, data, 3);
	info->text = (char *) malloc(length - 3);
	memcpy(info->text, data + 3, length - 3);

	DSMCC_DEBUG("Info descriptor, lang_code='%s' text='%s'", info->lang_code, info->text);
}

static void dsmcc_parse_modlink_descriptor(struct dsmcc_descriptor *desc, unsigned char *data, int length)
{
	struct dsmcc_descriptor_modlink *modlink = &desc->data.modlink;

	desc->type = DSMCC_DESCRIPTOR_MODLINK;
	modlink->position = data[0];
	modlink->module_id = (data[1] << 8) | data[2];

	DSMCC_DEBUG("Modlink descriptor, position=%d module_id=%d", modlink->position, modlink->module_id);
}

static void dsmcc_parse_crc32_descriptor(struct dsmcc_descriptor *desc, unsigned char *data, int length)
{
	struct dsmcc_descriptor_crc32 *crc32 = &desc->data.crc32;

	desc->type = DSMCC_DESCRIPTOR_CRC32;
	crc32->crc = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];

	DSMCC_DEBUG("CRC32 descriptor, crc=0x%lx", crc32->crc);
}

static void dsmcc_parse_location_descriptor(struct dsmcc_descriptor *desc, unsigned char *data, int length)
{
	struct dsmcc_descriptor_location *location = &desc->data.location;

	desc->type = DSMCC_DESCRIPTOR_LOCATION;
	location->location_tag = data[0];

	DSMCC_DEBUG("Location descriptor, location_tag=%d", location->location_tag);
}

static void dsmcc_parse_dltime_descriptor(struct dsmcc_descriptor *desc, unsigned char *data, int length)
{
	struct dsmcc_descriptor_dltime *dltime = &desc->data.dltime;

	desc->type = DSMCC_DESCRIPTOR_DLTIME;
	dltime->download_time = (data[0] << 24) | (data[1] << 16) | (data[2] << 8 ) | data[3];

	DSMCC_DEBUG("Dltime descriptor, download_time=%d", dltime->download_time);
}

static void dsmcc_parse_grouplink_descriptor(struct dsmcc_descriptor *desc, unsigned char *data, int length)
{
	struct dsmcc_descriptor_grouplink *grouplink = &desc->data.grouplink;

	desc->type = DSMCC_DESCRIPTOR_GROUPLINK;
	grouplink->position = data[0];
	grouplink->group_id = (data[1] << 24) | (data[2] << 16) | (data[3] << 8 ) | data[4];

	DSMCC_DEBUG("Grouplink descriptor, position=%d group_id=%d", grouplink->position, grouplink->group_id);
}

static void dsmcc_parse_compressed_descriptor(struct dsmcc_descriptor *desc, unsigned char *data, int length)
{
	struct dsmcc_descriptor_compressed *compressed = &desc->data.compressed;

	desc->type = DSMCC_DESCRIPTOR_COMPRESSED;
	compressed->method = data[0];
	compressed->original_size = (data[1] << 24) | (data[2] << 16) | (data[3] << 8)  | data[4];

	DSMCC_DEBUG("Compressed descriptor, method=%d original_size=%d", compressed->method, compressed->original_size);
}

static void dsmcc_parse_label_descriptor(struct dsmcc_descriptor *desc, unsigned char *data, int length)
{
	struct dsmcc_descriptor_label *label = &desc->data.label;

	desc->type = DSMCC_DESCRIPTOR_LABEL;
	label->text = (char *)malloc(length);
	memcpy(label->text, data, length);

	DSMCC_DEBUG("Label descriptor, text='%s'", label->text);
}

static void dsmcc_parse_caching_priority_descriptor(struct dsmcc_descriptor *desc, unsigned char *data, int length)
{
	struct dsmcc_descriptor_caching_priority *caching_priority = &desc->data.caching_priority;

	desc->type = DSMCC_DESCRIPTOR_CACHING_PRIORITY;
	caching_priority->priority_value = data[0];
	caching_priority->transparency_level = data[1];

	DSMCC_DEBUG("Caching priority descriptor, priority_value=%d, transparency_level=%d", caching_priority->priority_value, caching_priority->transparency_level);
}

static void dsmcc_parse_content_type_descriptor(struct dsmcc_descriptor *desc, unsigned char *data, int length)
{
	struct dsmcc_descriptor_content_type *content_type = &desc->data.content_type;

	desc->type = DSMCC_DESCRIPTOR_LABEL;
	content_type->text = (char *)malloc(length);
	memcpy(content_type->text, data, length);

	DSMCC_DEBUG("Content type descriptor, text='%s'", content_type->text);
}

static int dsmcc_parse_one_descriptor(struct dsmcc_descriptor **descriptor, unsigned char *data, int data_length)
{
	int off = 0;
	struct dsmcc_descriptor *desc;
	unsigned char tag, length;

	desc = malloc(sizeof(struct dsmcc_descriptor));
	tag = data[off];
	off++;
	length = data[off];
	off++;

	switch(tag) {
		case 0x01:
			dsmcc_parse_type_descriptor(desc, data + off, length);
			break;
		case 0x02:
			dsmcc_parse_name_descriptor(desc, data + off, length);
			break;
		case 0x03:
			dsmcc_parse_info_descriptor(desc, data + off, length);
			break;
		case 0x04:
			dsmcc_parse_modlink_descriptor(desc, data + off, length);
			break;
		case 0x05:
			dsmcc_parse_crc32_descriptor(desc, data + off, length);
			break;
		case 0x06:
			dsmcc_parse_location_descriptor(desc, data + off, length);
			break;
		case 0x07:
			dsmcc_parse_dltime_descriptor(desc, data + off, length);
			break;
		case 0x08:
			dsmcc_parse_grouplink_descriptor(desc, data + off, length);
			break;
		case 0x09:
			dsmcc_parse_compressed_descriptor(desc, data + off, length);
			break;
		case 0x70:
			dsmcc_parse_label_descriptor(desc, data + off, length);
			break;
		case 0x71:
			dsmcc_parse_caching_priority_descriptor(desc, data + off, length);
			break;
		case 0x72:
			dsmcc_parse_content_type_descriptor(desc, data + off, length);
			break;
		default:
			DSMCC_WARN("Unknown/Unhandled descriptor, Tag 0x%02x Length %d", tag, length);
			free(desc);
			desc = NULL;
	}

	off += length;

	*descriptor = desc;

	return off;
}

int dsmcc_parse_descriptors(struct dsmcc_descriptor **descriptors, unsigned char *data, int data_length)
{
	int off = 0, ret;
	struct dsmcc_descriptor *list_head, *list_tail;

	list_head = list_tail = NULL;

	while (off < data_length)
	{
		struct dsmcc_descriptor *desc = NULL;
		ret = dsmcc_parse_one_descriptor(&desc, data + off, data_length - off);
		if (ret < 0)
		{
			// TODO
		}
		off += ret;

		if (desc)
		{
			desc->next = NULL;
			if (!list_tail)
			{
				list_tail = desc;
				list_head = list_tail;
			}
			else
			{
				list_tail->next = desc;
				list_tail = desc;
			}
		}
	}

	*descriptors = list_head;

	return off;
}