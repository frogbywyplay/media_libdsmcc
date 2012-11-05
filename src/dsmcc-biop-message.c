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
#include "dsmcc-cache-module.h"
#include "dsmcc-cache-file.h"
#include "dsmcc-descriptor.h"

#define BIOP_MAGIC 0x42494f50

static int dsmcc_biop_parse_msg_header(struct biop_msg_header *header, unsigned char *data, int data_length)
{
	int off = 0;
	unsigned long magic;
	unsigned short objinfo_len;

	magic = dsmcc_getlong(data);
	off += 4;
	if (magic != BIOP_MAGIC)
	{
		DSMCC_ERROR("Invalid magic: expected 0x%lx but got 0x%lx", 0x42494f50, magic);
		return -1;
	}

	header->version_major = data[off++];
	header->version_minor = data[off++];
	DSMCC_DEBUG("Version Major = %d", header->version_major);
	DSMCC_DEBUG("Version Minor = %d", header->version_minor);

	/* skip byte order & message type */
	off += 2;

	header->message_size  = dsmcc_getlong(data + off);
	off += 4;
	DSMCC_DEBUG("Message Size = %d", header->message_size);

	header->objkey_len = data[off];
	off++;
	if (header->objkey_len > 0)
	{
		header->objkey = malloc(header->objkey_len);
		memcpy(header->objkey, data + off, header->objkey_len);
	}
	else
		header->objkey = NULL;
	off += header->objkey_len;
	DSMCC_DEBUG("ObjKey Len = %d", header->objkey_len);
	DSMCC_DEBUG("ObjKey = %02x%02x%02x%02x", header->objkey[0], header->objkey[1], header->objkey[2], header->objkey[3]);

	header->objkind_len = dsmcc_getlong(data + off);
	off += 4;
	if (header->objkind_len > 0)
	{
		header->objkind = malloc(header->objkind_len);
		memcpy(header->objkind, data + off, header->objkind_len);
	}
	else
		header->objkind = NULL;
	off += header->objkind_len;
	DSMCC_DEBUG("ObjKind Len = %ld", header->objkind_len);
	DSMCC_DEBUG("ObjKind Data = %s", header->objkind);

	/* skip object info */
	objinfo_len = dsmcc_getshort(data + off);
	off += 2;
	off += objinfo_len;
	DSMCC_DEBUG("ObjInfo Len = %d", header->objkey_len);

	/* adjust message size to exclude header fields */
	header->message_size -= header->objkey_len + 1;
	header->message_size -= header->objkind_len + 4;
	header->message_size -= objinfo_len + 2;

	return off;
}

static void dsmcc_biop_free_msg_header(struct biop_msg_header *header)
{
	if (!header)
		return;

	if (header->objkey)
	{
		free(header->objkey);
		header->objkey = NULL;
	}

	if (header->objkind)
	{
		free(header->objkind);
		header->objkind = NULL;
	}

	free(header);
}

static int dsmcc_biop_parse_name(struct biop_name *name, unsigned char *data, int data_length)
{
	int off = 0;
	unsigned char comp_count;

	comp_count = data[off];
	off++;
	if (comp_count != 1)
	{
		DSMCC_DEBUG("Invalid number of name components while parsing BIOP::Name (got %d, expected %d)", comp_count, 1);
		return -1;
	}

	/* only one name component to parse */

	name->id_len = data[off];
	off++;
	if (name->id_len > 0)
	{
		name->id = (char *) malloc(name->id_len);
		memcpy(name->id, data + off, name->id_len);
	}
	else
		name->id = NULL;
	off += name->id_len;
	DSMCC_DEBUG("Id Len = %d", name->id_len);
	DSMCC_DEBUG("Id = %s", name->id);

	name->kind_len = data[off];
	off++;
	if (name->kind_len > 0)
	{
		name->kind = (char *) malloc(name->kind_len);
		memcpy(name->kind, data + off, name->kind_len);
	}
	else
		name->kind = NULL;
	off += name->kind_len;
	DSMCC_DEBUG("Kind Len = %d", name->kind_len);
	DSMCC_DEBUG("Kind = %s", name->kind);

	return off;
}

static void dsmcc_biop_free_name(struct biop_name *name)
{
	if (name->id != NULL)
		free(name->id);
	if (name->kind != NULL)
		free(name->kind);
	free(name);
}

static void dsmcc_biop_free_binding(struct biop_binding *binding)
{
	dsmcc_biop_free_name(binding->name);
	dsmcc_biop_free_ior(binding->ior);
	free(binding);
}

static int dsmcc_biop_parse_binding(struct biop_binding *bind, unsigned char *data, int data_length)
{
	int off = 0, ret;
	unsigned short objinfo_len;

	bind->name = malloc(sizeof(struct biop_name));
	ret = dsmcc_biop_parse_name(bind->name, data, data_length);
	if (ret < 0)
	{
		DSMCC_ERROR("dsmcc_biop_parse_name returned %d", ret);
		dsmcc_biop_free_name(bind->name);
		bind->name = NULL;
		return -1;
	}
	off += ret;

	bind->binding_type = data[off];
	off++;
	DSMCC_DEBUG("Binding Type = %d", bind->binding_type);

	bind->ior = malloc(sizeof(struct biop_ior));
	memset(bind->ior, 0, sizeof(struct biop_ior));
	ret = dsmcc_biop_parse_ior(bind->ior, data + off, data_length - off);
	if (ret < 0)
	{
		DSMCC_ERROR("dsmcc_biop_parse_ior returned %d", ret);
		dsmcc_biop_free_ior(bind->ior);
		bind->ior = NULL;
		return -1;
	}
	off += ret;

	/* skip object info */
	objinfo_len = dsmcc_getshort(data + off);
	off += 2;
	off += objinfo_len;
	DSMCC_DEBUG("ObjInfo Len = %d", objinfo_len);

	return off;
}

static int dsmcc_biop_parse_dir(struct dsmcc_file_cache *filecache, unsigned short module_id, int objkey_len, unsigned char *objkey, unsigned char *data, int data_length)
{
	unsigned int i;
	int off = 0, ret;
	unsigned long msgbody_len;
	unsigned int bindings_count;

	/* TODO skip service context count */
	off++;

	msgbody_len = dsmcc_getlong(data + off);
	off += 4;
	DSMCC_DEBUG("MsgBody Len = %ld", msgbody_len);

	bindings_count = dsmcc_getshort(data + off);
	off += 2;
	DSMCC_DEBUG("Bindings Count = %d", bindings_count);

	for (i = 0; i < bindings_count; i++)
	{
		struct biop_binding *binding = malloc(sizeof(struct biop_binding));
		ret = dsmcc_biop_parse_binding(binding, data + off, data_length - off);
		if (ret < 0)
		{
			DSMCC_ERROR("dsmcc_biop_parse_binding returned %d", ret);
			dsmcc_biop_free_binding(binding);
			return -1;
		}
		off += ret;

		if (!strcmp("dir", binding->name->kind))
		{
			DSMCC_DEBUG("Caching info for directory '%s'", binding->name->id);
			dsmcc_filecache_cache_dir_info(filecache, module_id, objkey_len, objkey, binding);
		}
		else if(!strcmp("fil", binding->name->kind))
		{
			DSMCC_DEBUG("Caching info for file '%s'", binding->name->id);
			dsmcc_filecache_cache_file_info(filecache, module_id, objkey_len, objkey, binding);
		}
		else
			DSMCC_WARN("Skipping unknown object id '%s' kind '%s'", binding->name->id, binding->name->kind);

		dsmcc_biop_free_binding(binding);
	}

	return off;
}

static int dsmcc_biop_parse_file(struct dsmcc_file_cache *filecache, int objkey_len, unsigned char *objkey, struct dsmcc_cached_module *module, unsigned int module_offset, unsigned char *data, int data_length)
{
	int off = 0;
	unsigned long msgbody_len;
	unsigned long content_len;

	/* TODO skip service context count */
	off++;

	msgbody_len = dsmcc_getlong(data + off);
	off += 4;
	DSMCC_DEBUG("MsgBody Len = %ld", msgbody_len);

	content_len = dsmcc_getlong(data + off);
	off += 4;
	DSMCC_DEBUG("Content Len = %ld", content_len);

	dsmcc_filecache_cache_file(filecache, objkey_len, objkey, content_len, module, module_offset + off);
	off += content_len;

	return off;
}

static unsigned char *dsmcc_biop_mmap_data(const char *filename, unsigned int size)
{
	int fd;
	unsigned char *data;

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

int dsmcc_biop_parse_data(struct dsmcc_file_cache *filecache, struct dsmcc_cached_module *module)
{
	struct dsmcc_descriptor *desc;
	int ret;
	unsigned int off, length;
	unsigned char *data;

	desc = dsmcc_find_descriptor_by_type(module->descriptors, DSMCC_DESCRIPTOR_COMPRESSED);
	if (desc)
		length = desc->data.compressed.original_size;
	else
		length = module->total_size;

	DSMCC_DEBUG("Module size (uncompressed) = %d", length);

	data = dsmcc_biop_mmap_data(module->data_file, length);
	if (!data)
		return -1;

	off = 0;
	while (off < length)
	{
		struct biop_msg_header *header;

		DSMCC_DEBUG("Current %ld / %ld", off, length);

		/* Parse header */
		DSMCC_DEBUG("Parsing message header");
		header = malloc(sizeof(struct biop_msg_header));
		memset(header, 0, sizeof(struct biop_msg_header));
		ret = dsmcc_biop_parse_msg_header(header, data + off, length - off);
		if (ret < 0)
		{
			DSMCC_ERROR("dsmcc_biop_parse_msg_header returned %d, dropping rest of module", ret);
			dsmcc_biop_free_msg_header(header);
			off = -1;
			break;
		}
		off += ret;

		/* Handle each message type */
		if (strcmp((const char *) header->objkind, "fil") == 0)
		{
			DSMCC_DEBUG("Parsing file message");
			ret = dsmcc_biop_parse_file(filecache, header->objkey_len, header->objkey, module, off, data + off, length - off);
		}
		else if(strcmp((const char *) header->objkind, "dir") == 0)
		{
			DSMCC_DEBUG("Parsing directory message");
			ret = dsmcc_biop_parse_dir(filecache, module->module_id, header->objkey_len, header->objkey, data + off, length - off);
		}
		else if(strcmp((const char *) header->objkind, "srg") == 0)
		{
			DSMCC_DEBUG("Parsing gateway message");
			ret = dsmcc_biop_parse_dir(filecache, 0, 0, NULL, data + off, length - off);
		}
		else
			DSMCC_WARN("Don't known of to handle unknown object (kind \"%s\")", header->objkind);

		off += header->message_size;

		dsmcc_biop_free_msg_header(header);
	}

	if (munmap(data, length) < 0)
		DSMCC_ERROR("munmap error: %s", strerror(errno));

	return off;
}
