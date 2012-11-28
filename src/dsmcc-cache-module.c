/* for asprintf */
#define _GNU_SOURCE

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "dsmcc.h"
#include "dsmcc-cache-module.h"
#include "dsmcc-carousel.h"
#include "dsmcc-compress.h"
#include "dsmcc-debug.h"
#include "dsmcc-util.h"
#include "dsmcc-section.h"
#include "dsmcc-biop-message.h"

struct dsmcc_cached_module
{
	struct dsmcc_cached_module_info info;

	char    *data_file;

	uint32_t block_count;
	uint32_t downloaded_block_count;
	uint8_t *blocks;

	struct dsmcc_cached_module *next, *prev;
};

static void dsmcc_cached_module_free_one(struct dsmcc_object_carousel *carousel, struct dsmcc_cached_module *module)
{
	DSMCC_DEBUG("Removing module 0x%04hx version 0x%02hhx in carousel 0x%08x", module->info.module_id, module->info.module_version, carousel->cid);

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
		dsmcc_cached_module_free_one(carousel, carousel->modules);
}

static void dsmcc_process_cached_module(struct dsmcc_object_carousel *carousel, struct dsmcc_cached_module *module)
{
	DSMCC_DEBUG("Processing module 0x%04hx in carousel 0x%08x (data file is %s)", module->info.module_id, carousel->cid, module->data_file);

	if (module->info.compressed)
	{
		DSMCC_DEBUG("Processing compressed module data");
		if (!dsmcc_inflate_file(module->data_file))
		{
			DSMCC_ERROR("Error while processing compressed module");
			return;
		}
		dsmcc_biop_parse_data(carousel->filecache, module->info.module_id, module->data_file, module->info.uncompressed_size);
	}
	else
	{
		DSMCC_DEBUG("Processing uncompressed module data");
		dsmcc_biop_parse_data(carousel->filecache, module->info.module_id, module->data_file, module->info.module_size);
	}
}

/**
  * Add module to cache list if no module with same id or version has changed
  */
void dsmcc_cached_module_add_info(struct dsmcc_state *state, struct dsmcc_object_carousel *carousel, struct dsmcc_cached_module_info *module_info)
{
	int i;
	struct dsmcc_cached_module *module;

	for (module = carousel->modules; module; module = module->next)
	{
		if (module->info.download_id == module_info->download_id && module->info.module_id == module_info->module_id)
		{
			if (module->info.module_version == module_info->module_version)
			{
				/* Already know this version */
				DSMCC_DEBUG("Already Know Module 0x%04hx Version 0x%02hhx", module_info->module_id, module_info->module_version);
				return;
			}
			else
			{
				/* New version, drop old data */
				DSMCC_DEBUG("Updating Module 0x%04hx Download ID 0x%08x -> 0x%08x Version 0x%02hhx -> 0x%02hhx",
						module_info->module_id, module->info.download_id, module_info->download_id, module->info.module_version, module_info->module_version);
				dsmcc_cached_module_free_one(carousel, module);
				break;
			}
		}
	}

	DSMCC_DEBUG("Saving info for module 0x%04hx (Download ID 0x%08x)", module_info->module_id, module_info->download_id);

	module = calloc(1, sizeof(struct dsmcc_cached_module));
	memcpy(&module->info, module_info, sizeof(struct dsmcc_cached_module_info));
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
	DSMCC_DEBUG("Allocated %d byte(s) to store block status for %d block(s) of module 0x%04hx", i, module->block_count, module->info.module_id);

	i = strlen(state->tmpdir) + 32;
	module->data_file = malloc(i);
	snprintf(module->data_file, i, "%s/%08x-%08x-%04hx-%02hhx.data", state->tmpdir, carousel->cid, module->info.download_id, module->info.module_id, module->info.module_version);
}

void dsmcc_cached_module_save_data(struct dsmcc_object_carousel *carousel, struct dsmcc_ddb *ddb, uint8_t *data, int data_length)
{
	struct dsmcc_cached_module *module = NULL;
	uint32_t length;

	if (data_length < 0)
	{
		DSMCC_ERROR("Data buffer overflow");
		return;
	}

	for (module = carousel->modules; module; module = module->next)
	{
		if (module->info.download_id == ddb->download_id
				&& module->info.module_id == ddb->module_id
				&& module->info.module_version == ddb->module_version)
		{
			DSMCC_DEBUG("Found linking module (0x%08x/0x%04hx/0x%02hhx)...", module->info.download_id, module->info.module_id, module->info.module_version);
			break;
		}
	}

	if (!module)
	{
		DSMCC_DEBUG("Linking module not found (0x%08x/0x%04hx/0x%02hhx)", ddb->download_id, ddb->module_id, ddb->module_version);
		return;
	}

	if (module->downloaded_block_count == module->block_count)
	{
		/* Already got it */
		DSMCC_DEBUG("Module 0x%04hx already completely downloaded", module->info.module_id);
	}
	else
	{
		/* Check that DDB size is equal to module block size (or smaller for last block) */
		length = ddb->length;
		if (length > module->info.block_size)
		{
			DSMCC_WARN("DDB block length is bigger than module block size (%u > %u), dropping excess data", length, module->info.block_size);
			length = module->info.block_size;
		}
		else if (length != module->info.block_size && ddb->number != module->block_count - 1)
		{
			DSMCC_ERROR("DDB block length is smaller than module block size (%u < %u)", length, module->info.block_size);
			return;
		}

		/* Check that we have enough data in buffer */
		if (length > ((uint32_t) data_length))
		{
			DSMCC_ERROR("Data buffer overflow (need %d bytes but only got %d)", length, data_length);
			return;
		}

		/* Check if we have this block already or not. If not save it to disk */
		if ((module->blocks[ddb->number >> 3] & (1 << (ddb->number & 7))) == 0)
		{
			if (!dsmcc_file_write_block(module->data_file, ddb->number * module->info.block_size, data, length))
			{
				DSMCC_ERROR("Error while writing block %d of module 0x%hx", ddb->number, module->info.module_id);
				return;
			}
			module->downloaded_block_count++;
			module->blocks[ddb->number >> 3] |= (1 << (ddb->number & 7));
		}

		DSMCC_DEBUG("Module 0x%04hx Downloaded blocks %d/%d", module->info.module_id, module->downloaded_block_count, module->block_count);

		/* If we have all blocks for this module, process it */
		if (module->downloaded_block_count == module->block_count)
			dsmcc_process_cached_module(carousel, module);
	}
}

bool dsmcc_cached_modules_are_complete(struct dsmcc_object_carousel *carousel)
{
	struct dsmcc_cached_module *module;

	for (module = carousel->modules; module; module = module->next)
	{
		if (module->downloaded_block_count != module->block_count)
			return 0;
	}

	return 1;
}
