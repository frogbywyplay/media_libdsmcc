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
#include "dsmcc-biop-tap.h"

/**
  * Add module to cache list if no module with same id or version has changed
  */
void dsmcc_add_cached_module_info(struct dsmcc_status *status, struct dsmcc_object_carousel *car, struct dsmcc_dii *dii, struct dsmcc_module_info *dmi, struct biop_module_info *bmi)
{
	int num_blocks;
	struct dsmcc_cached_module *module;

	for (module = car->modules; module; module = module->next)
	{
		if (module->carousel_id == dii->download_id
				&& module->module_id == dmi->module_id)
		{
			if (module->version == dmi->module_version)
			{
				/* Already know this version */
				DSMCC_DEBUG("Already Know Module %d Version %d", dmi->module_id, dmi->module_version);
				return;
			}
			else
			{
				/* New version, drop old data */
				DSMCC_DEBUG("Updating Module %d Version %d -> %d", dmi->module_id, module->version, dmi->module_version);
				dsmcc_free_cached_module(car, module);
				break;
			}
		}
	}

	DSMCC_DEBUG("Saving info for module %d", dmi->module_id);

	module = malloc(sizeof(struct dsmcc_cached_module));
	memset(module, 0, sizeof(struct dsmcc_cached_module));
	module->next = car->modules;
	if (module->next)
		module->next->prev = module;
	car->modules = module;

	module->carousel_id = dii->download_id;
	module->module_id = dmi->module_id;
	module->version = dmi->module_version;
	module->total_size = dmi->module_size;
	module->block_size = dii->block_size;
	module->downloaded_size = 0;
	module->assoc_tag = bmi->assoc_tag;
	module->cached = 0;

	num_blocks = module->total_size / module->block_size;
	if ((module->total_size % module->block_size) != 0)
		num_blocks++;

	module->bstatus = malloc((num_blocks / 8) + 1);
	memset(module->bstatus, 0, (num_blocks / 8) + 1);
	DSMCC_DEBUG("Allocated %d bytes to store status for module %d", (num_blocks / 8) + 1, module->module_id);

	asprintf(&module->data_file, "%s/%lu-%hu-%hhu.data", status->tmpdir, module->carousel_id, module->module_id, module->version);

	/* Subscribe to stream if not already */
	DSMCC_DEBUG("Subscribing to stream with assoc_tag 0x%x", module->assoc_tag);
	dsmcc_object_carousel_stream_subscribe(car, module->assoc_tag);

	/* Steal the descriptors */
	module->descriptors = bmi->descriptors;
	bmi->descriptors = NULL;
}

void dsmcc_free_cached_module(struct dsmcc_object_carousel *car, struct dsmcc_cached_module *module)
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
		car->modules = module->next;
		if (module->next)
			module->next->prev = NULL;
	}

	free(module);
}

static int dsmcc_module_write_block(const char *filename, unsigned int offset, const unsigned char *data, unsigned int length)
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

	if ((wret = write(fd, data, length)) < length)
	{
		if (wret >= 0)
			DSMCC_ERROR("Partial write to module data file '%s': %d/%d", filename, wret, length);
		else
			DSMCC_ERROR("Write error to module data file '%s': %s", filename, strerror(errno));
		close(fd);
		return 0;
	}

	close(fd);
	return 1;
}

void dsmcc_save_cached_module_data(struct dsmcc_status *status, int download_id, struct dsmcc_ddb *ddb, unsigned char *data, int data_length)
{
	struct dsmcc_cached_module *module = NULL;
	struct dsmcc_descriptor *desc = NULL;
	struct dsmcc_object_carousel *car;

	/* Scan through known modules and append data */

	car = dsmcc_find_carousel_by_id(status->carousels, download_id);
	if (!car)
	{
		DSMCC_DEBUG("Data block for module in unknown carousel %ld", download_id);
		return;
	}

	DSMCC_DEBUG("Data block on carousel %ld", car->id);

	for (module = car->modules; module; module = module->next)
	{
		if (module->carousel_id == download_id && module->module_id == ddb->module_id)
		{
			DSMCC_DEBUG("Found linking module (%d)...", ddb->module_id);
			break;
		}
	}
	if (!module)
	{
		DSMCC_DEBUG("Linking module not found (%d)", ddb->module_id);
		return;
	}

	if (module->version == ddb->module_version)
	{
		if (module->cached)
		{
			DSMCC_DEBUG("Cached complete module already %d", module->module_id);
			return; /* Already got it */
		}
		else
		{
			/* Check that DDB size is smaller than module block size */
			/* TODO only last block can be smaller, other ones should be of module block size */
			if (ddb->length > module->block_size)
				DSMCC_WARN("DDB block length is not smaller than module block size (%ld / %ld)", ddb->length, module->block_size);

			/* Check if we have this block already or not. If not save it to disk */
			if ((module->bstatus[ddb->number >> 3] & (1 << (ddb->number & 7))) == 0)
			{
				dsmcc_module_write_block(module->data_file, ddb->number * module->block_size, data, ddb->length);
				module->downloaded_size += ddb->length;
				module->bstatus[ddb->number >> 3] |= (1 << (ddb->number & 7));
			}
		}

		DSMCC_DEBUG("Module %d Downloaded Size %ld Total Size %ld", module->module_id, module->downloaded_size, module->total_size);

		if (module->downloaded_size >= module->total_size)
		{
			DSMCC_DEBUG("Reconstructing module %d from blocks", module->module_id);

			/* scan for compressed descriptor */
			for (desc = module->descriptors; desc != NULL; desc = desc->next)
			{
				if (desc && desc->type == DSMCC_DESCRIPTOR_COMPRESSED)
					break;
			}
			if (desc)
			{
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
				dsmcc_biop_parse_data(car->filecache, module);
				module->cached = 1;
			}
		}
	}
}
