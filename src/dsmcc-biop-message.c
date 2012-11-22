#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <unistd.h>

#include "dsmcc-debug.h"
#include "dsmcc-util.h"
#include "dsmcc-biop-message.h"
#include "dsmcc-biop-ior.h"
#include "dsmcc-cache-file.h"

#define BIOP_MAGIC 0x42494f50

#define BINDING_TYPE_NOBJECT  0x01
#define BINDING_TYPE_NCONTEXT 0x02

struct biop_msg_header
{
	uint32_t message_size;
	uint32_t key;
	uint32_t key_mask;
	uint32_t kind;
};

static int dsmcc_biop_parse_msg_header(struct biop_msg_header *header, uint8_t *data, int data_length)
{
	int off = 0;
	uint32_t magic, kind_len;
	uint16_t version, objinfo_len;
	uint8_t byte_order, message_type, key_len;

	/* magic */
	if (!dsmcc_getlong(&magic, data, off, data_length))
		return -1;
	off += 4;
	if (magic != BIOP_MAGIC)
	{
		DSMCC_ERROR("Invalid magic: got 0x%x but expected 0x%x", magic, BIOP_MAGIC);
		return -1;
	}

	/* version */
	if (!dsmcc_getshort(&version, data, off, data_length))
		return -1;
	off += 2;
	if (version != 0x100)
	{
		DSMCC_ERROR("Invalid version in BIOP Message Header: got 0x%hx but expected 0x100", version);
		return -1;
	}


	/* byte order */
	if (!dsmcc_getbyte(&byte_order, data, off, data_length))
		return -1;
	off++;
	if (byte_order != 0)
	{
		DSMCC_ERROR("Invalid byte order in BIOP Message Header: got %hhu but expected 0 (big endian)", byte_order);
		return -1;
	}

	/* message type */
	if (!dsmcc_getbyte(&message_type, data, off, data_length))
		return -1;
	off++;
	if (message_type != 0)
	{
		DSMCC_ERROR("Invalid message type in BIOP Message Header: got %hhu but expected 0", message_type);
		return -1;
	}

	/* message size */
	if (!dsmcc_getlong(&header->message_size, data, off, data_length))
		return -1;
	off += 4;
	DSMCC_DEBUG("Message Size = %u", header->message_size);

	/* key length */
	if (!dsmcc_getbyte(&key_len, data, off, data_length))
		return -1;
	off++;
	if (key_len > 4)
	{
		DSMCC_ERROR("Invalid object key length in BIOP Message Header: got %u but expected less than or equal to 4", key_len);
		return -1;
	}

	/* key */
	if (!dsmcc_getkey(&header->key, &header->key_mask, key_len, data, off, data_length))
		return -1;
	off += key_len;
	DSMCC_DEBUG("Key = 0x%08x/0x%08x", header->key, header->key_mask);

	/* kind length */
	if (!dsmcc_getlong(&kind_len, data, off, data_length))
		return -1;
	off += 4;
	if (kind_len != 4)
	{
		DSMCC_ERROR("Invalid object kind length in BIOP Message Header: got %u but expected equal to 4", kind_len);
		return -1;
	}

	/* kind */
	if (!dsmcc_getlong(&header->kind, data, off, data_length))
		return -1;
	off += 4;
	DSMCC_DEBUG("Kind = 0x%08x", header->kind);

	/* skip object info */
	if (!dsmcc_getshort(&objinfo_len, data, off, data_length))
		return -1;
	off += 2;
	DSMCC_DEBUG("Info Len = %u", objinfo_len);
	off += objinfo_len;

	/* adjust message size to exclude header fields */
	header->message_size -= 1 + key_len;     /* sizeof(key_len) + key_len */
	header->message_size -= 4 + 4;           /* sizeof(kind_len) + kind_len */
	header->message_size -= 2 + objinfo_len; /* sizeof(objinfo_len) + objinfo_len */

	return off;
}

static int dsmcc_biop_parse_name(struct biop_name *name, uint8_t *data, int data_length)
{
	int off = 0;
	uint8_t comp_count, len;

	if (!dsmcc_getbyte(&comp_count, data, off, data_length))
		return -1;
	off++;
	if (comp_count != 1)
	{
		DSMCC_DEBUG("Invalid number of name components while parsing BIOP::Name (got %hhu, expected 1)", comp_count);
		return -1;
	}

	/* only one name component to parse */

	if (!dsmcc_getbyte(&len, data, off, data_length))
		return -1;
	off++;
	DSMCC_DEBUG("Id Len = %hhu", len);
	if (!dsmcc_strdup(&name->id, len, data, off, data_length))
		return -1;
	off += len;
	DSMCC_DEBUG("Id = %s", name->id);

	if (!dsmcc_getbyte(&len, data, off, data_length))
		return -1;
	off++;
	DSMCC_DEBUG("Kind Len = %hhu", len);
	if (!dsmcc_strdup(&name->kind, len, data, off, data_length))
		return -1;
	off += len;
	DSMCC_DEBUG("Kind = %s", name->kind);

	return off;
}

static void dsmcc_biop_free_binding(struct biop_binding *binding)
{
	if (binding->name.id != NULL)
		free(binding->name.id);
	if (binding->name.kind != NULL)
		free(binding->name.kind);
	free(binding);
}

static int dsmcc_biop_parse_binding(struct biop_binding *bind, uint8_t *data, int data_length)
{
	int off = 0, ret;
	uint16_t objinfo_len;

	memset(&bind->name, 0, sizeof(struct biop_name));
	ret = dsmcc_biop_parse_name(&bind->name, data, data_length);
	if (ret < 0)
		return -1;
	off += ret;

	if (!dsmcc_getbyte(&bind->binding_type, data, off, data_length))
		return -1;
	off++;
	DSMCC_DEBUG("Binding Type = %hhu", bind->binding_type);

	memset(&bind->ior, 0, sizeof(struct biop_ior));
	ret = dsmcc_biop_parse_ior(&bind->ior, data + off, data_length - off);
	if (ret < 0)
		return -1;
	off += ret;

	/* skip object info */
	if (!dsmcc_getshort(&objinfo_len, data, off, data_length))
		return -1;
	off += 2;
	off += objinfo_len;
	DSMCC_DEBUG("ObjInfo Len = %hu", objinfo_len);

	return off;
}

static int dsmcc_biop_skip_service_context_list(uint8_t *data, int data_length)
{
	int off = 0, i;
	uint8_t serviceContextList_count;

	if (!dsmcc_getbyte(&serviceContextList_count, data, off, data_length))
		return -1;
	off++;

	if (serviceContextList_count > 0)
	{
		DSMCC_DEBUG("Skipping %hhu service contexts", serviceContextList_count);
		for (i = 0; i < serviceContextList_count; i++)
		{
			uint16_t context_data_length;

			/* skip context_id */
			off += 4;

			if (!dsmcc_getshort(&context_data_length, data, off, data_length))
				return -1;
			off += 2;

			/* skip context_data_byte */
			off += context_data_length;
		}
	}

	return off;
}

static int dsmcc_biop_parse_dir(struct dsmcc_file_cache *filecache, uint16_t module_id, uint32_t key, uint32_t key_mask, uint8_t *data, int data_length)
{
	int i;
	int off = 0, ret;
	uint32_t msgbody_len;
	uint16_t bindings_count;

	/* skip service context list */
	ret = dsmcc_biop_skip_service_context_list(data, data_length);
	if (ret < 0)
		return -1;
	off += ret;

	if (!dsmcc_getlong(&msgbody_len, data, off, data_length))
		return -1;
	off += 4;
	DSMCC_DEBUG("MsgBody Len = %u", msgbody_len);

	if (!dsmcc_getshort(&bindings_count, data, off, data_length))
		return -1;
	off += 2;
	DSMCC_DEBUG("Bindings Count = %hhu", bindings_count);

	for (i = 0; i < bindings_count; i++)
	{
		struct biop_binding *binding = malloc(sizeof(struct biop_binding));
		ret = dsmcc_biop_parse_binding(binding, data + off, data_length - off);
		if (ret < 0)
		{
			dsmcc_biop_free_binding(binding);
			return -1;
		}
		off += ret;

		if (!strcmp("dir", binding->name.kind))
		{
			if (binding->binding_type != BINDING_TYPE_NCONTEXT)
			{
				DSMCC_ERROR("Invalid binding type for Directory (got %hhu but expected %hhu)", binding->binding_type, BINDING_TYPE_NCONTEXT);
				dsmcc_biop_free_binding(binding);
				return -1;
			}
			DSMCC_DEBUG("Caching info for directory '%s'", binding->name.id);
			dsmcc_filecache_cache_dir_info(filecache, module_id, key, key_mask, binding);
		}
		else if (!strcmp("fil", binding->name.kind))
		{
			if (binding->binding_type != BINDING_TYPE_NOBJECT)
			{
				DSMCC_ERROR("Invalid binding type for File (got %hhu but expected %hhu)", binding->binding_type, BINDING_TYPE_NOBJECT);
				dsmcc_biop_free_binding(binding);
				return -1;
			}
			DSMCC_DEBUG("Caching info for file '%s'", binding->name.id);
			dsmcc_filecache_cache_file_info(filecache, module_id, key, key_mask, binding);
		}
		else
			DSMCC_WARN("Skipping unknown object id '%s' kind '%s'", binding->name.id, binding->name.kind);

		dsmcc_biop_free_binding(binding);
	}

	return off;
}

static int dsmcc_biop_parse_file(struct dsmcc_file_cache *filecache, uint16_t module_id, uint32_t key, uint32_t key_mask, const char *module_file, int module_offset, uint8_t *data, int data_length)
{
	int off = 0, ret;
	uint32_t msgbody_len;
	uint32_t content_len;

	/* skip service context list */
	ret = dsmcc_biop_skip_service_context_list(data, data_length);
	if (ret < 0)
		return -1;
	off += ret;

	if (!dsmcc_getlong(&msgbody_len, data, off, data_length))
		return -1;
	off += 4;
	DSMCC_DEBUG("MsgBody Len = %u", msgbody_len);

	if (!dsmcc_getlong(&content_len, data, off, data_length))
		return -1;
	off += 4;
	DSMCC_DEBUG("Content Len = %u", content_len);

	dsmcc_filecache_cache_file(filecache, module_id, key, key_mask, module_file, module_offset + off, content_len);
	off += content_len;

	return off;
}

static uint8_t *dsmcc_biop_mmap_data(const char *filename, int size)
{
	int fd;
	uint8_t *data;

	fd = open(filename, O_RDONLY);
	if (fd < 0)
	{
		DSMCC_ERROR("Can't open module data file '%s': %s", filename, strerror(errno));
		return NULL;
	}

	data = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (data == MAP_FAILED)
	{
		DSMCC_ERROR("Can't mmap module data file '%s': %s", filename, strerror(errno));
		close(fd);
		return NULL;
	}

	close(fd);
	return data;
}

int dsmcc_biop_parse_data(struct dsmcc_file_cache *filecache, uint16_t module_id, const char *module_file, int length)
{
	int ret, off;
	uint8_t *data;

	DSMCC_DEBUG("Data size = %d", length);

	data = dsmcc_biop_mmap_data(module_file, length);
	if (!data)
		return -1;

	off = 0;
	while (off < length)
	{
		struct biop_msg_header header;

		DSMCC_DEBUG("Current %d / %d", off, length);

		/* Parse header */
		DSMCC_DEBUG("Parsing message header");
		memset(&header, 0, sizeof(struct biop_msg_header));
		ret = dsmcc_biop_parse_msg_header(&header, data + off, length - off);
		if (ret < 0)
		{
			off = -1;
			break;
		}
		off += ret;

		/* Handle each message type */
		switch (header.kind)
		{
			case 0x66696c00: /* "fil" */
				DSMCC_DEBUG("Parsing file message");
				ret = dsmcc_biop_parse_file(filecache, module_id, header.key, header.key_mask, module_file, off, data + off, length - off);
				break;
			case 0x64697200: /* "dir" */
				DSMCC_DEBUG("Parsing directory message");
				ret = dsmcc_biop_parse_dir(filecache, module_id, header.key, header.key_mask, data + off, length - off);
				break;
			case 0x73726700: /* "srg" */
				DSMCC_DEBUG("Parsing gateway message");
				ret = dsmcc_biop_parse_dir(filecache, module_id, 0, 0, data + off, length - off);
				break;
			default:
				DSMCC_WARN("Don't known of to handle unknown object (kind 0x%08x)", header.kind);
				ret = 1;
		}
		if (ret < 0)
		{
			off = -1;
			break;
		}
		off += header.message_size;
	}

	if (munmap(data, length) < 0)
		DSMCC_ERROR("munmap error: %s", strerror(errno));

	return off;
}
