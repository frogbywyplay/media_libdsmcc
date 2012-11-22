#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "dsmcc.h"
#include "dsmcc-cache-file.h"
#include "dsmcc-cache-module.h"
#include "dsmcc-carousel.h"
#include "dsmcc-debug.h"
#include "dsmcc-util.h"
#include "dsmcc-section.h"
#include "dsmcc-biop-message.h"
#include "dsmcc-biop-module.h"

struct dsmcc_entry
{
	char *path;
	char *diskpath;
	bool  marked;

	struct dsmcc_entry *prev, *next;
};

struct dsmcc_entry_id
{
	uint16_t module_id;
	uint32_t key;
	uint32_t key_mask;
};

struct dsmcc_cached_dir
{
	struct dsmcc_entry_id id;

	char *name;
	char *path;

	struct dsmcc_entry_id parent_id;

	struct dsmcc_cached_file *files;

	struct dsmcc_cached_dir *parent, *sub;
	struct dsmcc_cached_dir *next, *prev;
};

struct dsmcc_cached_file
{
	struct dsmcc_entry_id id;

	char        *name;
	unsigned int data_len;

	struct dsmcc_entry_id    parent_id;
	struct dsmcc_cached_dir *parent;

	char        *module_file;
	unsigned int module_offset;

	struct dsmcc_cached_file *next, *prev;
};

struct dsmcc_file_cache
{
	struct dsmcc_object_carousel *carousel;

	char                   *downloadpath;
	dsmcc_cache_callback_t *callback;
	void                   *callback_arg;

	struct dsmcc_cached_dir  *gateway;
	struct dsmcc_cached_dir  *orphan_dirs;
	struct dsmcc_cached_file *orphan_files;
	struct dsmcc_cached_file *nameless_files;

	struct dsmcc_entry *entries;
};

void dsmcc_filecache_init(struct dsmcc_object_carousel *carousel, const char *downloadpath, dsmcc_cache_callback_t *cache_callback, void *cache_callback_arg)
{
	carousel->filecache = calloc(1, sizeof(struct dsmcc_file_cache));
	carousel->filecache->carousel = carousel;
	carousel->filecache->callback = cache_callback;
	carousel->filecache->callback_arg = cache_callback_arg;

	carousel->filecache->downloadpath = strdup(downloadpath);
	if (downloadpath[strlen(downloadpath) - 1] == '/')
		carousel->filecache->downloadpath[strlen(downloadpath) - 1] = '\0';
	dsmcc_mkdir(carousel->filecache->downloadpath, 0755);
}

static void dsmcc_filecache_free_file(struct dsmcc_cached_file *file)
{
	if (file->name)
		free(file->name);
	if (file->module_file)
		free(file->module_file);
	free(file);
}

static void dsmcc_filecache_free_files(struct dsmcc_cached_file *file)
{
	struct dsmcc_cached_file *next;

	if (!file)
		return;

	while (file)
	{
		next = file->next;
		dsmcc_filecache_free_file(file);
		file = next;
	}
}

static void dsmcc_filecache_free_dir(struct dsmcc_cached_dir *dir)
{
	if (dir->name)
		free(dir->name);
	if (dir->path)
		free(dir->path);
	dsmcc_filecache_free_files(dir->files);
	free(dir);
}

static void dsmcc_filecache_free_dirs(struct dsmcc_cached_dir *dir)
{
	struct dsmcc_cached_dir *subdir, *subdirnext;

	if (!dir)
		return;

	if (dir->sub)
	{
		for (subdir = dir->sub; subdir; subdir = subdirnext)
		{
			subdirnext = subdir->next;
			dsmcc_filecache_free_dirs(subdir);
		}
	}

	dsmcc_filecache_free_dir(dir);
}

void dsmcc_filecache_free(struct dsmcc_object_carousel *carousel)
{
	struct dsmcc_entry *entrynext;

	dsmcc_filecache_free_dirs(carousel->filecache->gateway);
	dsmcc_filecache_free_dirs(carousel->filecache->orphan_dirs);
	dsmcc_filecache_free_files(carousel->filecache->orphan_files);
	dsmcc_filecache_free_files(carousel->filecache->nameless_files);

	while (carousel->filecache->entries)
	{
		entrynext = carousel->filecache->entries->next;
		free(carousel->filecache->entries->path);
		free(carousel->filecache->entries->diskpath);
		free(carousel->filecache->entries);
		carousel->filecache->entries = entrynext;
	}

	free(carousel->filecache->downloadpath);

	free(carousel->filecache);
	carousel->filecache = NULL;
}

struct dsmcc_entry *dsmcc_filecache_find_entry(struct dsmcc_file_cache *filecache, const char *path)
{
	struct dsmcc_entry *entry = filecache->entries;
	while (entry)
	{
		if (!strcmp(entry->path, path))
			return entry;
		entry = entry->next;
	}
	return NULL;
}

void dsmcc_filecache_add_entry(struct dsmcc_file_cache *filecache, const char *path, const char *diskpath)
{
	struct dsmcc_entry *entry;

	DSMCC_DEBUG("Adding file entry %s (%s)", path, diskpath);

	entry = calloc(1, sizeof(struct dsmcc_entry));
	entry->path = strdup(path);
	entry->diskpath = strdup(diskpath);

	entry->next = filecache->entries;
	if (entry->next)
		entry->next->prev = entry;
	filecache->entries = entry;
}

void dsmcc_filecache_reset(struct dsmcc_object_carousel *carousel)
{
	struct dsmcc_entry *entry = carousel->filecache->entries;
	while (entry)
	{
		entry->marked = 1;
		entry = entry->next;
	}

	dsmcc_filecache_free_dirs(carousel->filecache->gateway);
	carousel->filecache->gateway = NULL;
	dsmcc_filecache_free_dirs(carousel->filecache->orphan_dirs);
	carousel->filecache->orphan_dirs = NULL;
	dsmcc_filecache_free_files(carousel->filecache->orphan_files);
	carousel->filecache->orphan_files = NULL;
	dsmcc_filecache_free_files(carousel->filecache->nameless_files);
	carousel->filecache->nameless_files = NULL;
}

void dsmcc_filecache_clean(struct dsmcc_object_carousel *carousel)
{
	struct dsmcc_entry *entry = carousel->filecache->entries;
	while (entry)
	{
		if (entry->marked)
		{
			DSMCC_ERROR("Removing entry %s (%s)", entry->path, entry->diskpath);

			unlink(entry->diskpath);

			if (carousel->filecache->callback)
				(*carousel->filecache->callback)(carousel->filecache->callback_arg, carousel->cid, DSMCC_CACHE_FILE, DSMCC_CACHE_DELETED, entry->path, entry->diskpath);

			if (entry->prev)
				entry->prev->next = entry->next;
			else
				carousel->filecache->entries = entry->next;
			if (entry->next)
				entry->next->prev = entry->prev;
			free(entry->path);
			free(entry->diskpath);
			free(entry);
		}
		entry = entry->next;
	}
}

static void dsmcc_filecache_add_file_to_list(struct dsmcc_cached_file **list_head, struct dsmcc_cached_file *file)
{
	file->next = *list_head;
	if (file->next)
		file->next->prev = file;
	file->prev = NULL;
	*list_head = file;
}

static void dsmcc_filecache_remove_file_from_list(struct dsmcc_cached_file **list_head, struct dsmcc_cached_file *file)
{
	if (file->prev)
		file->prev->next = file->next;
	else
		*list_head = file->next;
	if (file->next)
		file->next->prev = file->prev;
	file->prev = NULL;
	file->next = NULL;
}

static void dsmcc_filecache_add_dir_to_list(struct dsmcc_cached_dir **list_head, struct dsmcc_cached_dir *dir)
{
	dir->next = *list_head;
	if (dir->next)
		dir->next->prev = dir;
	dir->prev = NULL;
	*list_head = dir;
}

static void dsmcc_filecache_remove_dir_from_list(struct dsmcc_cached_dir **list_head, struct dsmcc_cached_dir *dir)
{
	if (dir->prev)
		dir->prev->next = dir->next;
	else
		*list_head = dir->next;
	if (dir->next)
		dir->next->prev = dir->prev;
	dir->prev = NULL;
	dir->next = NULL;
}

static void dsmcc_filecache_fill_id(struct dsmcc_entry_id *id, uint16_t module_id, uint32_t key, uint32_t key_mask)
{
	id->module_id = module_id;
	id->key = key;
	id->key_mask = key_mask;
}

static void dsmcc_filecache_copy_id(struct dsmcc_entry_id *dst, struct dsmcc_entry_id *src)
{
	dsmcc_filecache_fill_id(dst, src->module_id, src->key, src->key_mask);
}

static int dsmcc_filecache_id_cmp(struct dsmcc_entry_id *id1, struct dsmcc_entry_id *id2)
{
	if (id1->module_id != id2->module_id)
		return 0;

	if (id1->key_mask != id2->key_mask)
		return 0;

	return (id1->key & id1->key_mask) == (id2->key & id2->key_mask);
}

static struct dsmcc_cached_dir *dsmcc_filecache_find_dir_in_subdirs(struct dsmcc_cached_dir *parent, struct dsmcc_entry_id *id)
{
	struct dsmcc_cached_dir *dir, *subdir;

	if (!parent)
		return NULL;

	if (dsmcc_filecache_id_cmp(&parent->id, id))
		return parent;

	/* Search sub dirs */
	for (subdir = parent->sub; subdir != NULL; subdir = subdir->next)
	{
		dir = dsmcc_filecache_find_dir_in_subdirs(subdir, id);
		if (dir)
			return dir;
	}

	return NULL;
}

static void dsmcc_filecache_attach_orphan_file(struct dsmcc_file_cache *filecache, struct dsmcc_cached_dir *dir, struct dsmcc_cached_file *file)
{
	/* detach from orphan files */
	dsmcc_filecache_remove_file_from_list(&filecache->orphan_files, file);

	/* attach to dir */
	file->parent = dir;
	dsmcc_filecache_add_file_to_list(&dir->files, file);
}

static void dsmcc_filecache_attach_orphan_dir(struct dsmcc_file_cache *filecache, struct dsmcc_cached_dir *parent, struct dsmcc_cached_dir *dir)
{
	/* detach from orphan dirs */
	dsmcc_filecache_remove_dir_from_list(&filecache->orphan_dirs, dir);

	/* attach to parent */
	dir->parent = parent;
	dsmcc_filecache_add_dir_to_list(&parent->sub, dir);
}

static void dsmcc_filecache_write_file(struct dsmcc_file_cache *filecache, struct dsmcc_cached_file *file)
{
	char *fn, *path;
	int tmp;

	if (!file->parent || !file->parent->path)
	{
		DSMCC_ERROR("Cannot write file %s, invalid parent: Parent == %p Parent Path == %s", file->name, file->parent, file->parent ? file->parent->path : NULL);
		return;
	}

	tmp = strlen(file->parent->path) + strlen(file->name) + 2;
	path = malloc(tmp);
	tmp = snprintf(path, tmp, "%s/%s", file->parent->path, file->name);

	tmp += strlen(filecache->downloadpath) + 2;
	fn = malloc(tmp);
	snprintf(fn, tmp, "%s%s%s", filecache->downloadpath, path[0] == '/' ? "" : "/", path);

	if (filecache->callback && !(*filecache->callback)(filecache->callback_arg, filecache->carousel->cid, DSMCC_CACHE_FILE, DSMCC_CACHE_CHECK, path, NULL))
	{
		DSMCC_DEBUG("Skipping file %s as requested", path);
		goto cleanup;
	}

	if (dsmcc_file_copy(fn, file->module_file, file->module_offset, file->data_len))
	{
		struct dsmcc_entry *entry;
		int reason;

		/* Free data as no longer needed */
		file->module_offset = 0;
		free(file->module_file);
		file->module_file = NULL;
		file->data_len = 0;

		entry = dsmcc_filecache_find_entry(filecache, path);
		if (entry)
		{
			reason = DSMCC_CACHE_UPDATED;
			entry->marked = 0;
		}
		else
		{
			reason = DSMCC_CACHE_CREATED;
			dsmcc_filecache_add_entry(filecache, path, fn);
		}

		if (filecache->callback)
			(*filecache->callback)(filecache->callback_arg, filecache->carousel->cid, DSMCC_CACHE_FILE, reason, path, fn);
	}

cleanup:
	free(fn);
	free(path);
}

static void dsmcc_filecache_write_dir(struct dsmcc_file_cache *filecache, struct dsmcc_cached_dir *dir)
{
	struct dsmcc_cached_dir *subdir;
	struct dsmcc_cached_file *file;
	char *dn;
	int tmp;

	if (!dir->path)
	{
		tmp = strlen(dir->parent->path) + strlen(dir->name) + 2;
		dir->path = malloc(tmp);
		snprintf(dir->path, tmp, "%s/%s", dir->parent->path, dir->name);
	}

	tmp = strlen(filecache->downloadpath) + strlen(dir->path) + 2;
	dn = malloc(tmp);
	snprintf(dn, tmp, "%s/%s", filecache->downloadpath, dir->path);

	/* call callback (except for gateway) */
	if (strlen(dir->name) > 0 && (filecache->callback && !(*filecache->callback)(filecache->callback_arg, filecache->carousel->cid, DSMCC_CACHE_DIR, DSMCC_CACHE_CHECK, dir->path, NULL)))
	{
		DSMCC_DEBUG("Skipping directory %s as requested", dir->path);
		goto end;
	}

	DSMCC_DEBUG("Writing directory %s to %s", dir->path, dn);

	mkdir(dn, 0755); 

	/* register and call callback (except for gateway) */
	if (strlen(dir->name) > 0)
	{
		struct dsmcc_entry *entry;
		int reason;

		entry = dsmcc_filecache_find_entry(filecache, dir->path);
		if (entry)
		{
			reason = DSMCC_CACHE_UPDATED;
			entry->marked = 0;
		}
		else
		{
			reason = DSMCC_CACHE_CREATED;
			dsmcc_filecache_add_entry(filecache, dir->path, dn);
		}

		if (filecache->callback)
			(*filecache->callback)(filecache->callback_arg, filecache->carousel->cid, DSMCC_CACHE_DIR, reason, dir->path, dn);
	}

	/* Write out files that had arrived before directory */
	for (file = dir->files; file; file = file->next)
	{
		if (file->module_offset != 0)
		{
			DSMCC_DEBUG("Writing out file %s under new dir %s", file->name, dir->path);
			dsmcc_filecache_write_file(filecache, file);
		}
	}

	/* Recurse through child directories */
	for (subdir = dir->sub; subdir; subdir = subdir->next)
		dsmcc_filecache_write_dir(filecache, subdir);

end:
	free(dn);
}

static struct dsmcc_cached_dir *dsmcc_filecache_cached_dir_find(struct dsmcc_file_cache *filecache, struct dsmcc_entry_id *id)
{
	struct dsmcc_cached_dir *dir, *dirnext;
	struct dsmcc_cached_file *file, *filenext;

	DSMCC_DEBUG("Searching for dir 0x%04hx:0x%08x:0x%08x", id->module_id, id->key, id->key_mask);

	/* Scan through known dirs and return details if known else NULL */
	if (id->key_mask == 0)
	{
		/* Return gateway object. Create if not already */
		if (filecache->gateway == NULL)
		{
			filecache->gateway = calloc(1, sizeof(struct dsmcc_cached_dir));
			filecache->gateway->name = strdup("");
			filecache->gateway->path = strdup("");

			/* Attach any subdirs or files that arrived prev. */

			for (file = filecache->orphan_files; file; file = filenext)
			{
				filenext = file->next;
				if (dsmcc_filecache_id_cmp(&file->parent_id, &filecache->gateway->id))
					dsmcc_filecache_attach_orphan_file(filecache, filecache->gateway, file);
			}

			for (dir = filecache->orphan_dirs; dir; dir = dirnext)
			{
				dirnext = dir->next;
				if (dsmcc_filecache_id_cmp(&dir->parent_id, &filecache->gateway->id))
					dsmcc_filecache_attach_orphan_dir(filecache, filecache->gateway, dir);
			}

			dsmcc_filecache_write_dir(filecache, filecache->gateway); /* Write files to filesystem */
		}
		return filecache->gateway;
	}

	/* Find dir */
	dir = dsmcc_filecache_find_dir_in_subdirs(filecache->gateway, id);
	if (dir == NULL)
	{
		struct dsmcc_cached_dir *d;

		/* Try looking in orphan dirs */
		for (d = filecache->orphan_dirs; !dir && d; d = d->next)
			dir = dsmcc_filecache_find_dir_in_subdirs(d, id);
	}

	return dir;
}

static struct dsmcc_cached_file *dsmcc_filecache_find_file_in_dir(struct dsmcc_cached_dir *dir, struct dsmcc_entry_id *id)
{
	struct dsmcc_cached_file *file;
	struct dsmcc_cached_dir *subdir;

	if (!dir)
		return NULL;

	/* Search files in this dir */
	for (file = dir->files; file != NULL; file = file->next)
		if (dsmcc_filecache_id_cmp(&file->id, id))
			return file;

	/* Search sub dirs */
	for (subdir = dir->sub; subdir != NULL; subdir = subdir->next)
		if ((file = dsmcc_filecache_find_file_in_dir(subdir, id)) != NULL)
			return file;

	return NULL;
}

static struct dsmcc_cached_file *dsmcc_filecache_find_file(struct dsmcc_file_cache *filecache, struct dsmcc_entry_id *id)
{
	struct dsmcc_cached_file *file;

	/* Try looking in parent-less list */
	for (file = filecache->orphan_files; file != NULL; file = file->next)
	{
		if (dsmcc_filecache_id_cmp(&file->id, id))
			return file;
	}

	/* Scan through known files and return details if known else NULL */
	file = dsmcc_filecache_find_file_in_dir(filecache->gateway, id);

	return file;
}

void dsmcc_filecache_cache_dir_info(struct dsmcc_file_cache *filecache, uint16_t module_id, uint32_t key, uint32_t key_mask, struct biop_binding *binding)
{
	struct dsmcc_entry_id tmpid;
	struct dsmcc_cached_dir *dir, *subdir, *subdirnext;
	struct dsmcc_cached_file *file, *filenext;

	if (filecache->carousel->cid != binding->ior.profile_body.obj_loc.carousel_id)
	{
		DSMCC_ERROR("Got a request to cache dir info for an invalid carousel %u (expected %u)", binding->ior.profile_body.obj_loc.carousel_id, filecache->carousel->cid);
		return;
	}

	tmpid.module_id = binding->ior.profile_body.obj_loc.module_id;
	tmpid.key = binding->ior.profile_body.obj_loc.key;
	tmpid.key_mask = binding->ior.profile_body.obj_loc.key_mask;
	dir = dsmcc_filecache_cached_dir_find(filecache, &tmpid);
	if (dir)
		return; /* Already got */

	dir = calloc(1, sizeof(struct dsmcc_cached_dir));
	dir->name = strdup(binding->name.id);
	dsmcc_filecache_fill_id(&dir->id, binding->ior.profile_body.obj_loc.module_id, binding->ior.profile_body.obj_loc.key, binding->ior.profile_body.obj_loc.key_mask);
	dsmcc_filecache_fill_id(&dir->parent_id, module_id, key, key_mask);
	dir->parent = dsmcc_filecache_cached_dir_find(filecache, &dir->parent_id);

	DSMCC_DEBUG("Caching dir %s with parent 0x%04hx:0x%08x:0x%08x", dir->name, dir->parent_id.module_id, dir->parent_id.key, dir->parent_id.key_mask);

	if (!dir->parent)
	{
		/* Directory not yet known. Add this to dirs list */
		dsmcc_filecache_add_dir_to_list(&filecache->orphan_dirs, dir);
	}
	else
	{
		/* Create under parent directory */
		dsmcc_filecache_add_dir_to_list(&dir->parent->sub, dir);
	}

	/* Attach any files that arrived previously */
	for (file = filecache->orphan_files; file != NULL; file = filenext)
	{
		filenext = file->next;
		if (dsmcc_filecache_id_cmp(&file->parent_id, &dir->id))
		{
			DSMCC_DEBUG("Attaching previously arrived file %s to newly created directory %s", file->name, dir->name);
			dsmcc_filecache_attach_orphan_file(filecache, dir, file);
		}
	}

	/* Attach any subdirs that arrived beforehand */
	for (subdir = filecache->orphan_dirs; subdir != NULL; subdir = subdirnext)
	{
		subdirnext = subdir->next;
		if (dsmcc_filecache_id_cmp(&subdir->parent_id, &dir->id))
		{
			DSMCC_DEBUG("Attaching previously arrived dir %s to newly created directory %s", subdir->name, dir->name);
			dsmcc_filecache_attach_orphan_dir(filecache, dir, subdir);
		}
	}

	if (dir->parent && dir->parent->path)
		dsmcc_filecache_write_dir(filecache, dir); /* Write dir/files to filesystem */
}

void dsmcc_filecache_cache_file(struct dsmcc_file_cache *filecache, uint16_t module_id, uint32_t key, uint32_t key_mask, const char *module_file, int offset, int length)
{
	struct dsmcc_cached_file *file;
	struct dsmcc_entry_id tmpid;

	tmpid.module_id = module_id;
	tmpid.key = key;
	tmpid.key_mask = key_mask;

	/* search for file info */
	file = dsmcc_filecache_find_file(filecache, &tmpid);
	if (!file)
	{
		/* Not known yet. Save data */

		DSMCC_DEBUG("Unknown file 0x%04x:0x%08x/0x%08x in carousel %u, caching data", tmpid.module_id, tmpid.key, tmpid.key_mask, filecache->carousel->cid);

		file = calloc(1, sizeof(struct dsmcc_cached_file));
		file->module_file = strdup(module_file);
		file->module_offset = offset;
		file->data_len = length;
		dsmcc_filecache_fill_id(&file->id, module_id, key, key_mask);

		/* Add to nameless files */
		dsmcc_filecache_add_file_to_list(&filecache->nameless_files, file);
	}
	else
	{
		/* Save data */
		if (file->module_offset == 0)
		{
			file->module_file = strdup(module_file);
			file->module_offset = offset;
			file->data_len = length;
			dsmcc_filecache_write_file(filecache, file);
		}
		else
		{
			DSMCC_DEBUG("Data for file %s had already been saved", file->name);
		}
	}
}

static struct dsmcc_cached_file *dsmcc_filecache_find_file_data(struct dsmcc_file_cache *filecache, struct dsmcc_entry_id *id)
{
	struct dsmcc_cached_file *last;

	for (last = filecache->nameless_files; last != NULL; last = last->next)
	{
		if (dsmcc_filecache_id_cmp(&last->id, id))
			break;
	}

	if (last)
	{
		/* Found it, remove from list */
		if (last->prev)
			last->prev->next = last->next;
		else
			filecache->nameless_files = last->next;

		if (last->next)
			last->next->prev = last->prev;

		last->prev = NULL;
		last->next = NULL;
	}

	return last;
}

void dsmcc_filecache_cache_file_info(struct dsmcc_file_cache *filecache, uint16_t module_id, uint32_t key, uint32_t key_mask, struct biop_binding *binding)
{
	struct dsmcc_cached_file *newfile;
	struct dsmcc_entry_id tmpid;

	if (filecache->carousel->cid != binding->ior.profile_body.obj_loc.carousel_id)
	{
		DSMCC_ERROR("Got a request to cache dir info for an invalid carousel 0x%08x (expected 0x%08x)", binding->ior.profile_body.obj_loc.carousel_id, filecache->carousel->cid);
		return;
	}

	tmpid.module_id = binding->ior.profile_body.obj_loc.module_id;
	tmpid.key = binding->ior.profile_body.obj_loc.key;
	tmpid.key_mask = binding->ior.profile_body.obj_loc.key_mask;

	/* Check we do not already have file (or file info) cached  */
	if (dsmcc_filecache_find_file(filecache, &tmpid))
		return;

	/* See if the data had already arrived for the file */
	newfile = dsmcc_filecache_find_file_data(filecache, &tmpid);

	if (!newfile)
	{
		/* Create the file from scratch */
		DSMCC_DEBUG("Data not arrived for file %s, caching", binding->name.id);
		newfile = calloc(1, sizeof(struct dsmcc_cached_file));
		dsmcc_filecache_copy_id(&newfile->id, &tmpid);
	}
	else
	{
		DSMCC_DEBUG("Data already arrived for file %s", binding->name.id);
	}

	dsmcc_filecache_fill_id(&newfile->parent_id, module_id, key, key_mask);
	newfile->name = strdup(binding->name.id);

	newfile->parent = dsmcc_filecache_cached_dir_find(filecache, &newfile->parent_id);

	DSMCC_DEBUG("Caching info in carousel %u for file %s (0x%04hx:0x%08x/0x%08x) with parent dir 0x%04hx:0x%08x/0x%08x",
			filecache->carousel->cid, newfile->name, newfile->id.module_id, newfile->id.key, newfile->id.key_mask,
			newfile->parent_id.module_id, newfile->parent_id.key, newfile->parent_id.key_mask);

	if (!newfile->parent)
	{
		/* Parent directory not yet known */
		dsmcc_filecache_add_file_to_list(&filecache->orphan_files, newfile);
	}
	else
	{
		/* Parent directory is known */
		dsmcc_filecache_add_file_to_list(&newfile->parent->files, newfile);

		if (newfile->module_offset != 0)
			dsmcc_filecache_write_file(filecache, newfile);
	}
}
