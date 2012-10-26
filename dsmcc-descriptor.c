#include <stdlib.h>
#include <string.h>
#include "dsmcc-descriptor.h"
#include "dsmcc-util.h"
#include "libdsmcc.h"

/* forward declarations */
static void dsmcc_desc_process_type(unsigned char *data, struct descriptor *);
static void dsmcc_desc_process_name(unsigned char *data, struct descriptor *);
static void dsmcc_desc_process_info(unsigned char *data, struct descriptor *);
static void dsmcc_desc_process_modlink(unsigned char *data, struct descriptor *);
static void dsmcc_desc_process_crc32(unsigned char *data, struct descriptor *);
static void dsmcc_desc_process_location(unsigned char *data, struct descriptor *);
static void dsmcc_desc_process_dltime(unsigned char *data, struct descriptor *);
static void dsmcc_desc_process_grouplink(unsigned char *data, struct descriptor *);
static void dsmcc_desc_process_compressed(unsigned char *data, struct descriptor *);
static struct descriptor *dsmcc_desc_process_one(unsigned char *data, int *offset);

void dsmcc_desc_free_all(struct descriptor *desc)
{
	struct descriptor *next;

	while (desc)
	{
		next = desc->next;
		/* free additional memory for some descriptor types */
		switch(desc->tag) {
			case 0x01:
				free(desc->data.type.text);
				break;
			case 0x02:
				free(desc->data.name.text);
				break;
			case 0x03:
				free(desc->data.info.lang_code);
				free(desc->data.info.text);
				break;
		}
		free(desc);
		desc = next;
	}
}

static void dsmcc_desc_process_type(unsigned char *data, struct descriptor *desc)
{
	struct descriptor_type *type = &desc->data.type;

	type->text = (char *) malloc(desc->len);
	memcpy(type->text, data, desc->len);

	DSMCC_DEBUG("[desc] new type descriptor, text=\"%s\"\n", type->text);
}

static void dsmcc_desc_process_name(unsigned char *data, struct descriptor *desc)
{
	struct descriptor_name *name = &desc->data.name;

	name->text = (char *)malloc(desc->len);
	memcpy(name->text, data, desc->len);

	DSMCC_DEBUG("[desc] new name descriptor, text=\"%s\"\n", name->text);
}

static void dsmcc_desc_process_info(unsigned char *data, struct descriptor *desc)
{
	struct descriptor_info *info = &desc->data.info;

	memcpy(info->lang_code, data, 3);
	info->text = (char *) malloc(desc->len - 3);
	memcpy(info->text, data + 3, desc->len - 3);

	DSMCC_DEBUG("[desc] new info descriptor, lang_code=\"%s\" text=\"%s\"\n", info->lang_code, info->text);
}

static void dsmcc_desc_process_modlink(unsigned char *data, struct descriptor *desc)
{
	struct descriptor_modlink *modlink = &desc->data.modlink;

	modlink->position = data[0];
	modlink->module_id = (data[1] << 8) | data[2];

	DSMCC_DEBUG("[desc] new modlink descriptor, position=%d module_id=%d\n", modlink->position, modlink->module_id);
}

static void dsmcc_desc_process_crc32(unsigned char *data, struct descriptor *desc)
{
	struct descriptor_crc32 *crc32 = &desc->data.crc32;

	crc32->crc = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];

	DSMCC_DEBUG("[desc] new crc32 descriptor, crc=0x%lx\n", crc32->crc);
}

static void dsmcc_desc_process_location(unsigned char *data, struct descriptor *desc)
{
	struct descriptor_location *location = &desc->data.location;

	location->location_tag = data[0];

	DSMCC_DEBUG("[desc] new location descriptor, location_tag=%d\n", location->location_tag);
}

static void dsmcc_desc_process_dltime(unsigned char *data, struct descriptor *desc)
{
	struct descriptor_dltime *dltime = &desc->data.dltime;

	dltime->download_time = (data[0] << 24) | (data[1] << 16) | (data[2] << 8 ) | data[3];

	DSMCC_DEBUG("[desc] new dltime descriptor, download_time=%d\n", dltime->download_time);
}

static void dsmcc_desc_process_grouplink(unsigned char *data, struct descriptor *desc)
{
	struct descriptor_grouplink *grouplink = &desc->data.grouplink;

	grouplink->position = data[0];
	grouplink->group_id = (data[1] << 24) | (data[2] << 16) | (data[3] << 8 ) | data[4];

	DSMCC_DEBUG("[desc] new grouplink descriptor, position=%d group_id=%d\n", grouplink->position, grouplink->group_id);
}

static void dsmcc_desc_process_compressed(unsigned char *data, struct descriptor *desc)
{
	struct descriptor_compressed *compressed = &desc->data.compressed;

	compressed->method = data[0];
	compressed->original_size = (data[1] << 24) | (data[2] << 16) | (data[3] << 8)  | data[4];

	DSMCC_DEBUG("[desc] new compressed descriptor, method=%d original_size=%d\n", compressed->method, compressed->original_size);
}

static struct descriptor *dsmcc_desc_process_one(unsigned char *data, int *bytes_read)
{
	struct descriptor *desc;

	desc = malloc(sizeof(struct descriptor));
	desc->tag = data[0];
	desc->len = data[1];
	data += 2;
	*bytes_read = desc->len + 2;

	switch(desc->tag) {
		case 0x01:
			dsmcc_desc_process_type(data, desc);
			break;
		case 0x02:
			dsmcc_desc_process_name(data, desc);
			break;
		case 0x03:
			dsmcc_desc_process_info(data, desc);
			break;
		case 0x04:
			dsmcc_desc_process_modlink(data, desc);
			break;
		case 0x05:
			dsmcc_desc_process_crc32(data, desc);
			break;
		case 0x06:
			dsmcc_desc_process_location(data, desc);
			break;
		case 0x07:
			dsmcc_desc_process_dltime(data, desc);
			break;
		case 0x08:
			dsmcc_desc_process_grouplink(data, desc);
			break;
		case 0x09:
			dsmcc_desc_process_compressed(data, desc);
			break;
		default:
			DSMCC_WARN("[desc] Unknown/Unhandled descriptor, tag=0x%02x\n", desc->tag);
	}

	return desc;

}

struct descriptor *dsmcc_desc_process(unsigned char *data, int data_len, int *bytes_read)
{
	int offset = 0, read;
	struct descriptor *desc, *list_head, *list_tail;

	list_head = list_tail = NULL;

	while (offset < data_len)
	{
		desc = dsmcc_desc_process_one(data + offset, &read);
		offset += read;
		if (list_tail == NULL) {
			list_head = desc;
			list_head->next = NULL;
			list_tail = list_head;
		} else {
			list_tail->next = desc;
			list_tail = desc;
			list_tail->next = NULL;
		}
	}

	*bytes_read = offset;

	return list_head;
}
