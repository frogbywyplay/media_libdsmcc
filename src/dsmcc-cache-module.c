/* for asprintf */
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "dsmcc.h"
#include "dsmcc-cache-module.h"
#include "dsmcc-carousel.h"
#include "dsmcc-debug.h"
#include "dsmcc-util.h"
#include "dsmcc-section.h"
#include "dsmcc-biop-message.h"
#include "dsmcc-biop-module.h"
#include "dsmcc-descriptor.h"

struct dsmcc_cached_module
{
	uint16_t module_id;
	uint8_t  version;

	uint32_t total_size;
	uint32_t block_size;
	uint32_t downloaded_size;
	uint8_t *bstatus;

	char    *data_file;
	int      cached;

	struct dsmcc_descriptor *descriptors;

	struct dsmcc_cached_module *next, *prev;
};

static void dsmcc_cached_module_free(struct dsmcc_object_carousel *carousel, struct dsmcc_cached_module *module)
{
	if (module->bstatus)
	{
		free(module->bstatus);
		module->bstatus = NULL;
	}

	if (module->descriptors)
	{
		dsmcc_descriptors_free_all(module->descriptors);
		module->descriptors = NULL;
	}

	if (module->data_file)
	{
		unlink(module->data_file);
		free(module->data_file);
		module->data_file = NULL;
	}
	
	if (module->prev)
	{
		module->prev->next = module->next;
		if (module->next)
			module->next->prev = module->prev;
	}
	else
	{
		carousel->modules = module->next;
		if (module->next)
			module->next->prev = NULL;
	}

	free(module);
}

void dsmcc_cached_module_free_all(struct dsmcc_object_carousel *carousel)
{
	while (carousel->modules)
		dsmcc_cached_module_free(carousel, carousel->modules);
}

static void dsmcc_process_cached_module(struct dsmcc_object_carousel *carousel, struct dsmcc_cached_module *module)
{
	struct dsmcc_descriptor *desc = NULL;

	if (module->cached)
	{
		DSMCC_DEBUG("Processing completed module %hu in carousel %u (data file is %s)", module->module_id, carousel->cid, module->data_file);

		/* scan for compressed descriptor */
		desc = dsmcc_find_descriptor_by_type(module->descriptors, DSMCC_DESCRIPTOR_COMPRESSED);
		if (desc)
		{
			//length = desc->data.compressed.original_size;
			DSMCC_DEBUG("Compression disabled - Skipping");
			if (module->data_file)
			{
				unlink(module->data_file);
				free(module->data_file);
				module->data_file = NULL;
			}
			module->downloaded_size = 0;
			return;
		}
		else
		{
			/* not compressed */
			DSMCC_DEBUG("Processing data (uncompressed)");
			dsmcc_biop_parse_data(carousel->filecache, module->module_id, module->data_file, module->total_size);
		}
	}
}

/**
  * Add module to cache list if no module with same id or version has changed
  */
void dsmcc_add_cached_module_info(struct dsmcc_state *state, struct dsmcc_object_carousel *carousel, struct dsmcc_dii *dii, struct dsmcc_module_info *dmi, struct biop_module_info *bmi)
{
	int num_blocks, i;
	struct dsmcc_cached_module *module;
	struct dsmcc_queue_entry *ddb_entry;

	for (module = carousel->modules; module; module = module->next)
	{
		if (module->module_id == dmi->module_id)
		{
			if (module->version == dmi->module_version)
			{
				/* Already know this version */
				DSMCC_DEBUG("Already Know Module %hu Version %hhu", dmi->module_id, dmi->module_version);
				return;
			}
			else
			{
				/* New version, drop old data */
				DSMCC_DEBUG("Updating Module %hu Version %hhu -> %hhu", dmi->module_id, module->version, dmi->module_version);
				dsmcc_cached_module_free(carousel, module);
				break;
			}
		}
	}

	DSMCC_DEBUG("Saving info for module %hu", dmi->module_id);

	module = calloc(1, sizeof(struct dsmcc_cached_module));
	module->next = carousel->modules;
	if (module->next)
		module->next->prev = module;
	carousel->modules = module;

	module->module_id = dmi->module_id;
	module->version = dmi->module_version;
	module->total_size = dmi->module_size;
	module->block_size = dii->block_size;
	module->downloaded_size = 0;
	module->cached = 0;

	num_blocks = module->total_size / module->block_size;
	if ((module->total_size % module->block_size) != 0)
		num_blocks++;

	module->bstatus = calloc(1, (num_blocks / 8) + 1);
	DSMCC_DEBUG("Allocated %d bytes to store status for module %hu", (num_blocks / 8) + 1, module->module_id);

	i = strlen(state->tmpdir) + 32;
	module->data_file = malloc(i);
	snprintf(module->data_file, i, "%s/%08x-%02hx-%hhx.data", state->tmpdir, carousel->cid, module->module_id, module->version);

	/* Queue entry for DDBs */
	ddb_entry = calloc(1, sizeof(struct dsmcc_queue_entry));
	ddb_entry->carousel = carousel;
	ddb_entry->type = DSMCC_QUEUE_ENTRY_DDB;
	ddb_entry->id = dii->download_id;
	dsmcc_stream_queue_add(state, DSMCC_STREAM_SELECTOR_ASSOC_TAG, bmi->assoc_tag, ddb_entry);

	/* Steal the descriptors */
	module->descriptors = bmi->descriptors;
	bmi->descriptors = NULL;
}

static int dsmcc_module_write_block(const char *filename, unsigned int offset, uint8_t *data, unsigned int length)
{
	int fd;
	ssize_t wret;

	fd = open(filename, O_WRONLY | O_CREAT, 0660);
	if (fd < 0)
	{
		DSMCC_ERROR("Can't open module data file '%s': %s", filename, strerror(errno));
		return 0;
	}

	if (lseek(fd, offset, SEEK_SET) < 0)
	{
		DSMCC_ERROR("Can't seek module data file '%s' : %s", filename, strerror(errno));
		close(fd);
		return 0;
	}

	wret = write(fd, data, length);
	if (wret < 0)
	{
		DSMCC_ERROR("Write error to module data file '%s': %s", filename, strerror(errno));
		close(fd);
		return 0;
	}
	else if (((unsigned int) wret) < length)
	{
		DSMCC_ERROR("Partial write to module data file '%s': %d/%u", filename, wret, length);
		close(fd);
		return 0;
	}

	close(fd);
	return 1;
}

void dsmcc_save_cached_module_data(struct dsmcc_state *state, struct dsmcc_object_carousel *carousel, struct dsmcc_ddb *ddb, uint8_t *data, int data_length)
{
	struct dsmcc_cached_module *module = NULL;

	(void) data_length; /* TODO check data length */

	/* Scan through known modules and append data */

	for (module = carousel->modules; module; module = module->next)
	{
		if (module->module_id == ddb->module_id)
		{
			DSMCC_DEBUG("Found linking module (%hu)...", ddb->module_id);
			break;
		}
	}

	if (!module)
	{
		DSMCC_DEBUG("Linking module not found (%hu)", ddb->module_id);
		return;
	}

	if (module->version == ddb->module_version)
	{
		if (module->cached)
		{
			DSMCC_DEBUG("Module %hu already completely cached", module->module_id);
			return; /* Already got it */
		}
		else
		{
			/* Check that DDB size is smaller than module block size */
			/* TODO only last block can be smaller, other ones should be of module block size */
			if (ddb->length > module->block_size)
				DSMCC_WARN("DDB block length is not smaller than module block size (%u / %u)", ddb->length, module->block_size);

			/* Check if we have this block already or not. If not save it to disk */
			if ((module->bstatus[ddb->number >> 3] & (1 << (ddb->number & 7))) == 0)
			{
				dsmcc_module_write_block(module->data_file, ddb->number * module->block_size, data, ddb->length);
				module->downloaded_size += ddb->length;
				module->bstatus[ddb->number >> 3] |= (1 << (ddb->number & 7));
			}
		}

		DSMCC_DEBUG("Module %hu Downloaded Size %u Total Size %u", module->module_id, module->downloaded_size, module->total_size);

		if (module->downloaded_size >= module->total_size)
		{
			module->cached = 1;
			dsmcc_process_cached_module(carousel, module);
		}
	}
	else
		DSMCC_DEBUG("Skipping data for module %hu, wrong version (got 0x%hhu but need 0x%hhu)", ddb->module_id, ddb->module_version, module->version);
}
