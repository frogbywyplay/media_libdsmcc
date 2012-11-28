/* for asprintf */
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "dsmcc.h"
#include "dsmcc-cache-module.h"
#include "dsmcc-carousel.h"
#include "dsmcc-debug.h"
#include "dsmcc-util.h"
#include "dsmcc-compress.h"
#include "dsmcc-biop-message.h"

struct dsmcc_module
{
	struct dsmcc_module_id   id;
	struct dsmcc_module_info info;

	bool     completed;
	char    *data_file;
	uint32_t block_count;
	uint32_t downloaded_block_count;
	uint8_t *blocks;

	struct dsmcc_module *next, *prev;
};

static void free_module_data(struct dsmcc_module *module)
{
	if (module->blocks)
	{
		free(module->blocks);
		module->blocks = NULL;
	}

	if (module->data_file)
	{
		unlink(module->data_file);
		free(module->data_file);
		module->data_file = NULL;
	}

	module->block_count = 0;
	module->downloaded_block_count = 0;
}

static void free_module(struct dsmcc_object_carousel *carousel, struct dsmcc_module *module)
{
	free_module_data(module);

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

void dsmcc_cache_free_all_modules(struct dsmcc_object_carousel *carousel)
{
	while (carousel->modules)
		free_module(carousel, carousel->modules);
}

static void process_module(struct dsmcc_object_carousel *carousel, struct dsmcc_module *module)
{
	DSMCC_DEBUG("Processing module 0x%04hx version 0x%02hhx in carousel 0x%08x (data file is %s)", module->id.module_id, module->id.module_version, carousel->cid, module->data_file);

	if (module->info.compressed)
	{
		DSMCC_DEBUG("Processing compressed module data");
		if (!dsmcc_inflate_file(module->data_file))
		{
			DSMCC_ERROR("Error while processing compressed module");
			return;
		}
		dsmcc_biop_parse_data(carousel->filecache, &module->id, module->data_file, module->info.uncompressed_size);
	}
	else
	{
		DSMCC_DEBUG("Processing uncompressed module data");
		dsmcc_biop_parse_data(carousel->filecache, &module->id, module->data_file, module->info.module_size);
	}

	module->completed = 1;

	DSMCC_DEBUG("Removing data for %smodule 0x%04hx version 0x%02hhx in carousel 0x%08x", module->completed ? "completed " : "", module->id.module_id, module->id.module_version, carousel->cid);
	free_module_data(module);
}

/**
  * Add module to cache list if no module with same id or version has changed
  */
void dsmcc_cache_add_module_info(struct dsmcc_object_carousel *carousel, struct dsmcc_module_id *module_id, struct dsmcc_module_info *module_info)
{
	int i;
	struct dsmcc_module *module;

	for (module = carousel->modules; module; module = module->next)
	{
		if (module->id.module_id == module_id->module_id)
		{
			if (module->id.module_version == module_id->module_version)
			{
				/* Already know this version */
				DSMCC_DEBUG("Already Know Module 0x%04hx Version 0x%02hhx", module_id->module_id, module_id->module_version);
				return;
			}
			else
			{
				/* New version, drop old data */
				DSMCC_DEBUG("Updating Module 0x%04hx Download ID 0x%08x -> 0x%08x Version 0x%02hhx -> 0x%02hhx",
						module_id->module_id, module->id.download_id, module_id->download_id, module->id.module_version, module_id->module_version);
				free_module(carousel, module);
				break;
			}
		}
	}

	DSMCC_DEBUG("Saving info for module 0x%04hx (Download ID 0x%08x)", module_id->module_id, module_id->download_id);

	module = calloc(1, sizeof(struct dsmcc_module));
	memcpy(&module->id, module_id, sizeof(struct dsmcc_module_id));
	memcpy(&module->info, module_info, sizeof(struct dsmcc_module_info));
	module->next = carousel->modules;
	if (module->next)
		module->next->prev = module;
	carousel->modules = module;

	module->downloaded_block_count = 0;
	module->block_count = module->info.module_size / module->info.block_size;
	if (module->info.module_size - module->block_count * module->info.block_size > 0)
		module->block_count++;
	i = (module->block_count + 7) >> 3;
	module->blocks = calloc(1, i);
	DSMCC_DEBUG("Allocated %d byte(s) to store block status for %d block(s) of module 0x%04hx", i, module->block_count, module->id.module_id);

	i = strlen(carousel->state->tmpdir) + 32;
	module->data_file = malloc(i);
	snprintf(module->data_file, i, "%s/%08x-%08x-%04hx-%02hhx.data", carousel->state->tmpdir, carousel->cid, module->id.download_id, module->id.module_id, module->id.module_version);
}

static void update_carousel_completion(struct dsmcc_object_carousel *carousel)
{
	struct dsmcc_module *module;

	for (module = carousel->modules; module; module = module->next)
		if (!module->completed)
			return;

	carousel->complete = 1;
}

void dsmcc_cache_save_module_data(struct dsmcc_object_carousel *carousel, struct dsmcc_module_id *module_id, uint16_t block_number, uint8_t *data, int length)
{
	struct dsmcc_module *module = NULL;

	if (length < 0)
	{
		DSMCC_ERROR("Data buffer overflow");
		return;
	}

	for (module = carousel->modules; module; module = module->next)
	{
		if (module->id.download_id == module_id->download_id
				&& module->id.module_id == module_id->module_id
				&& module->id.module_version == module_id->module_version)
		{
			DSMCC_DEBUG("Found linking module (0x%08x/0x%04hx/0x%02hhx)...", module->id.download_id, module->id.module_id, module->id.module_version);
			break;
		}
	}

	if (!module)
	{
		DSMCC_DEBUG("Linking module not found (0x%08x/0x%04hx/0x%02hhx)", module_id->download_id, module_id->module_id, module_id->module_version);
		return;
	}

	if (module->completed)
	{
		/* Already got it */
		DSMCC_DEBUG("Module 0x%04hx already completely downloaded", module->id.module_id);
	}
	else
	{
		/* Check that DDB size is equal to module block size (or smaller for last block) */
		if (((uint32_t)length) > module->info.block_size)
		{
			DSMCC_WARN("DDB block length is bigger than module block size (%d > %u), dropping excess data", length, module->info.block_size);
			length = module->info.block_size;
		}
		else if (((uint32_t) length) != module->info.block_size && block_number != module->block_count - 1)
		{
			DSMCC_ERROR("DDB block length is smaller than module block size (%d < %u)", length, module->info.block_size);
			return;
		}

		/* Check if we have this block already or not. If not save it to disk */
		if ((module->blocks[block_number >> 3] & (1 << (block_number & 7))) == 0)
		{
			if (!dsmcc_file_write_block(module->data_file, block_number * module->info.block_size, data, length))
			{
				DSMCC_ERROR("Error while writing block %hu of module 0x%hx", block_number, module->id.module_id);
				return;
			}
			module->downloaded_block_count++;
			module->blocks[block_number >> 3] |= (1 << (block_number & 7));
		}

		DSMCC_DEBUG("Module 0x%04hx Downloaded blocks %d/%d", module->id.module_id, module->downloaded_block_count, module->block_count);

		/* If we have all blocks for this module, process it */
		if (module->downloaded_block_count == module->block_count)
		{
			process_module(carousel, module);
			update_carousel_completion(carousel);
		}
	}
}
