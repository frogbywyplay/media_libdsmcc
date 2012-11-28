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
#include "dsmcc-biop-message.h"
#include "dsmcc-biop-module.h"

struct dsmcc_entry
{
	char *path;
	char *diskpath;
	bool  marked;

	struct dsmcc_entry *prev, *next;
};

struct dsmcc_cached_dir
{
	struct dsmcc_file_id id;

	char *name;
	char *path;
	bool  written;

	struct dsmcc_file_id     parent_id;
	struct dsmcc_cached_dir *parent;

	struct dsmcc_cached_file *files;
	struct dsmcc_cached_dir  *subdirs;

	struct dsmcc_cached_dir *next, *prev;
};

struct dsmcc_cached_file
{
	struct dsmcc_file_id id;

	char *name;
	char *path;
	bool  written;

	struct dsmcc_file_id     parent_id;
	struct dsmcc_cached_dir *parent;

	char        *data_file;
	unsigned int data_offset;
	unsigned int data_length;

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

static int file_id_cmp(struct dsmcc_file_id *id1, struct dsmcc_file_id *id2)
{
	if (id1->module_id != id2->module_id)
		return 0;

	if (id1->key_mask != id2->key_mask)
		return 0;

	return (id1->key & id1->key_mask) == (id2->key & id2->key_mask);
}

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

static void free_files(struct dsmcc_cached_file *file)
{
	struct dsmcc_cached_file *next;

	if (!file)
		return;

	while (file)
	{
		next = file->next;
		if (file->name)
			free(file->name);
		if (file->path)
			free(file->path);
		if (file->data_file)
			free(file->data_file);
		free(file);
		file = next;
	}
}

static void free_dirs(struct dsmcc_cached_dir *dir)
{
	struct dsmcc_cached_dir *subdir, *subdirnext;

	if (!dir)
		return;

	subdir = dir->subdirs;
	while (subdir)
	{
		subdirnext = subdir->next;
		free_dirs(subdir);
		subdir = subdirnext;
	}

	if (dir->name)
		free(dir->name);
	if (dir->path)
		free(dir->path);
	free_files(dir->files);
	free(dir);
}

void dsmcc_filecache_free(struct dsmcc_object_carousel *carousel)
{
	struct dsmcc_entry *entry, *entrynext;

	free_dirs(carousel->filecache->gateway);
	free_dirs(carousel->filecache->orphan_dirs);
	free_files(carousel->filecache->orphan_files);
	free_files(carousel->filecache->nameless_files);

	entry = carousel->filecache->entries;
	while (entry)
	{
		entrynext = entry->next;
		free(entry->path);
		free(entry->diskpath);
		free(entry);
		entry = entrynext;
	}

	free(carousel->filecache->downloadpath);

	free(carousel->filecache);
	carousel->filecache = NULL;
}

static bool find_or_create_entry(struct dsmcc_file_cache *filecache, const char *path, const char *diskpath)
{
	struct dsmcc_entry *entry;

	/* try to find entry */
	entry = filecache->entries;
	while (entry)
	{
		if (!strcmp(entry->path, path))
		{
			entry->marked = 0;
			return 0;
		}
		entry = entry->next;
	}

	/* not found, create it */
	entry = calloc(1, sizeof(struct dsmcc_entry));
	entry->path = strdup(path);
	entry->diskpath = strdup(diskpath);
	entry->next = filecache->entries;
	if (entry->next)
		entry->next->prev = entry;
	filecache->entries = entry;

	return 1;
}

void dsmcc_filecache_reset(struct dsmcc_object_carousel *carousel)
{
	struct dsmcc_entry *entry = carousel->filecache->entries;
	while (entry)
	{
		entry->marked = 1;
		entry = entry->next;
	}

	free_dirs(carousel->filecache->gateway);
	carousel->filecache->gateway = NULL;
	free_dirs(carousel->filecache->orphan_dirs);
	carousel->filecache->orphan_dirs = NULL;
	free_files(carousel->filecache->orphan_files);
	carousel->filecache->orphan_files = NULL;
	free_files(carousel->filecache->nameless_files);
	carousel->filecache->nameless_files = NULL;
}

void dsmcc_filecache_clean(struct dsmcc_object_carousel *carousel)
{
	struct dsmcc_entry *entry = carousel->filecache->entries;
	while (entry)
	{
		if (entry->marked)
		{
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

static void add_file_to_list(struct dsmcc_cached_file **list_head, struct dsmcc_cached_file *file)
{
	file->next = *list_head;
	if (file->next)
		file->next->prev = file;
	file->prev = NULL;
	*list_head = file;
}

static void remove_file_from_list(struct dsmcc_cached_file **list_head, struct dsmcc_cached_file *file)
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

static struct dsmcc_cached_file *find_file_in_list(struct dsmcc_cached_file *list_head, struct dsmcc_file_id *id)
{
	struct dsmcc_cached_file *file;

	for (file = list_head; file != NULL; file = file->next)
		if (file_id_cmp(&file->id, id))
			break;

	return file;
}

static void add_dir_to_list(struct dsmcc_cached_dir **list_head, struct dsmcc_cached_dir *dir)
{
	dir->next = *list_head;
	if (dir->next)
		dir->next->prev = dir;
	dir->prev = NULL;
	*list_head = dir;
}

static void remove_dir_from_list(struct dsmcc_cached_dir **list_head, struct dsmcc_cached_dir *dir)
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

static struct dsmcc_cached_dir *find_dir_in_subdirs(struct dsmcc_cached_dir *parent, struct dsmcc_file_id *id)
{
	struct dsmcc_cached_dir *dir, *subdir;

	if (!parent)
		return NULL;

	if (file_id_cmp(&parent->id, id))
		return parent;

	/* Search sub dirs */
	for (subdir = parent->subdirs; subdir != NULL; subdir = subdir->next)
	{
		dir = find_dir_in_subdirs(subdir, id);
		if (dir)
			return dir;
	}

	return NULL;
}

static void write_file(struct dsmcc_file_cache *filecache, struct dsmcc_cached_file *file)
{
	char *fn;
	int tmp;

	/* Skip already written files or files for which data has not yet arrived */
	if (file->written || !file->data_file)
		return;

	DSMCC_DEBUG("Writing out file %s under dir %s", file->name, file->parent->path);

	tmp = strlen(file->parent->path) + strlen(file->name) + 2;
	file->path = malloc(tmp);
	tmp = snprintf(file->path, tmp, "%s/%s", file->parent->path, file->name);

	tmp += strlen(filecache->downloadpath) + 2;
	fn = malloc(tmp);
	snprintf(fn, tmp, "%s%s%s", filecache->downloadpath, file->path[0] == '/' ? "" : "/", file->path);

	if (filecache->callback && !(*filecache->callback)(filecache->callback_arg, filecache->carousel->cid, DSMCC_CACHE_FILE, DSMCC_CACHE_CHECK, file->path, NULL))
	{
		DSMCC_DEBUG("Skipping file %s as requested", file->path);
		goto cleanup;
	}

	DSMCC_DEBUG("Copying data from %s to %s", file->data_file, fn);
	if (dsmcc_file_copy(fn, file->data_file, file->data_offset, file->data_length))
	{
		int reason;

		file->written = 1;

		if (find_or_create_entry(filecache, file->path, fn))
			reason = DSMCC_CACHE_CREATED;
		else
			reason = DSMCC_CACHE_UPDATED;

		if (filecache->callback)
			(*filecache->callback)(filecache->callback_arg, filecache->carousel->cid, DSMCC_CACHE_FILE, reason, file->path, fn);
	}

cleanup:
	free(fn);
}

static void write_dir(struct dsmcc_file_cache *filecache, struct dsmcc_cached_dir *dir)
{
	struct dsmcc_cached_dir *subdir;
	struct dsmcc_cached_file *file;
	char *dn = NULL;
	int tmp;

	/* Skip already written dirs */
	if (dir->written)
		return;

	if (dir != filecache->gateway)
	{
		DSMCC_DEBUG("Writing out dir %s under dir %s", dir->name, dir->parent->path);

		tmp = strlen(dir->parent->path) + strlen(dir->name) + 2;
		dir->path = malloc(tmp);
		snprintf(dir->path, tmp, "%s/%s", dir->parent->path, dir->name);

		tmp = strlen(filecache->downloadpath) + strlen(dir->path) + 2;
		dn = malloc(tmp);
		snprintf(dn, tmp, "%s/%s", filecache->downloadpath, dir->path);

		/* call callback (except for gateway) */
			if (filecache->callback && !(*filecache->callback)(filecache->callback_arg, filecache->carousel->cid, DSMCC_CACHE_DIR, DSMCC_CACHE_CHECK, dir->path, NULL))
			{
				DSMCC_DEBUG("Skipping directory %s as requested", dir->path);
				goto end;
			}

		DSMCC_DEBUG("Creating directory %s", dn);
		mkdir(dn, 0755); 

		/* register and call callback (except for gateway) */
		if (dir != filecache->gateway)
		{
			int reason;

			if (find_or_create_entry(filecache, dir->path, dn))
				reason = DSMCC_CACHE_CREATED;
			else
				reason = DSMCC_CACHE_UPDATED;

			if (filecache->callback)
				(*filecache->callback)(filecache->callback_arg, filecache->carousel->cid, DSMCC_CACHE_DIR, reason, dir->path, dn);
		}
	}
	dir->written = 1;

	/* Write out files that had arrived before directory */
	for (file = dir->files; file; file = file->next)
		write_file(filecache, file);

	/* Recurse through child directories */
	for (subdir = dir->subdirs; subdir; subdir = subdir->next)
		write_dir(filecache, subdir);

end:
	if (dn)
		free(dn);
}

static struct dsmcc_cached_dir *find_cached_dir(struct dsmcc_file_cache *filecache, struct dsmcc_file_id *id)
{
	struct dsmcc_cached_dir *dir;

	DSMCC_DEBUG("Searching for dir 0x%04hx:0x%08x:0x%08x", id->module_id, id->key, id->key_mask);

	/* Find dir */
	dir = find_dir_in_subdirs(filecache->gateway, id);
	if (dir == NULL)
	{
		struct dsmcc_cached_dir *d;

		/* Try looking in orphan dirs */
		for (d = filecache->orphan_dirs; !dir && d; d = d->next)
			dir = find_dir_in_subdirs(d, id);
	}

	return dir;
}

void dsmcc_filecache_cache_dir_info(struct dsmcc_file_cache *filecache, struct dsmcc_file_id *parent_id, struct dsmcc_file_id *id, const char *name)
{
	struct dsmcc_cached_dir *dir, *subdir, *subdirnext;
	struct dsmcc_cached_file *file, *filenext;

	dir = find_cached_dir(filecache, id);
	if (dir)
		return; /* Already got */

	dir = calloc(1, sizeof(struct dsmcc_cached_dir));
	dir->name = strdup(name);
	memcpy(&dir->id, id, sizeof(struct dsmcc_file_id));

	/* Check if parent ID is NULL, i.e. if this is the gateway */
	if (!parent_id)
	{
		DSMCC_DEBUG("Caching gateway dir 0x%04hx:0x%08x:0x%08x", id->module_id, id->key, id->key_mask);

		filecache->gateway = dir;
		dir->path = strdup("");
	}
	else
	{
		DSMCC_DEBUG("Caching dir %s (0x%04hx:0x%08x:0x%08x) with parent 0x%04hx:0x%08x:0x%08x", dir->name,
				id->module_id, id->key, id->key_mask, parent_id->module_id, parent_id->key, parent_id->key_mask);

		memcpy(&dir->parent_id, parent_id, sizeof(struct dsmcc_file_id));
		dir->parent = find_cached_dir(filecache, &dir->parent_id);
		if (!dir->parent)
		{
			/* Directory not yet known. Add this to dirs list */
			add_dir_to_list(&filecache->orphan_dirs, dir);
		}
		else
		{
			/* Create under parent directory */
			add_dir_to_list(&dir->parent->subdirs, dir);
		}
	}

	/* Attach any files that arrived previously */
	for (file = filecache->orphan_files; file != NULL; file = filenext)
	{
		filenext = file->next;
		if (file_id_cmp(&file->parent_id, &dir->id))
		{
			DSMCC_DEBUG("Attaching previously arrived file %s to newly created directory %s", file->name, dir->name);

			/* detach from orphan files */
			remove_file_from_list(&filecache->orphan_files, file);

			/* attach to dir */
			file->parent = dir;
			add_file_to_list(&dir->files, file);
		}
	}

	/* Attach any subdirs that arrived beforehand */
	for (subdir = filecache->orphan_dirs; subdir != NULL; subdir = subdirnext)
	{
		subdirnext = subdir->next;
		if (file_id_cmp(&subdir->parent_id, &dir->id))
		{
			DSMCC_DEBUG("Attaching previously arrived dir %s to newly created directory %s", subdir->name, dir->name);

			/* detach from orphan dirs */
			remove_dir_from_list(&filecache->orphan_dirs, subdir);

			/* attach to parent */
			subdir->parent = dir;
			add_dir_to_list(&dir->subdirs, subdir);
		}
	}

	/* Write dir/files to filesystem */
	if (dir == filecache->gateway || (dir->parent && dir->parent->written))
		write_dir(filecache, dir);
}

static struct dsmcc_cached_file *find_file_in_dir(struct dsmcc_cached_dir *dir, struct dsmcc_file_id *id)
{
	struct dsmcc_cached_file *file;
	struct dsmcc_cached_dir *subdir;

	if (!dir)
		return NULL;

	/* Search files in this dir */
	file = find_file_in_list(dir->files, id);
	if (file)
		return file;

	/* Search sub dirs */
	for (subdir = dir->subdirs; subdir != NULL; subdir = subdir->next)
		if ((file = find_file_in_dir(subdir, id)) != NULL)
			return file;

	return NULL;
}

static struct dsmcc_cached_file *find_file(struct dsmcc_file_cache *filecache, struct dsmcc_file_id *id)
{
	struct dsmcc_cached_file *file;

	/* Try looking in parent-less list */
	file = find_file_in_list(filecache->orphan_files, id);
	if (file)
		return file;

	/* Scan through known files and return details if known else NULL */
	file = find_file_in_dir(filecache->gateway, id);

	return file;
}

void dsmcc_filecache_cache_file(struct dsmcc_file_cache *filecache, struct dsmcc_file_id *id, const char *data_file, int data_offset, int data_length)
{
	struct dsmcc_cached_file *file;

	/* search for file info */
	file = find_file(filecache, id);
	if (!file)
	{
		/* Not known yet. Save data */
		DSMCC_DEBUG("Nameless file 0x%04x:0x%08x/0x%08x in carousel %u, caching data", id->module_id, id->key, id->key_mask, filecache->carousel->cid);

		file = calloc(1, sizeof(struct dsmcc_cached_file));
		memcpy(&file->id, id, sizeof(struct dsmcc_file_id));

		file->data_file = strdup(data_file);
		file->data_offset = data_offset;
		file->data_length = data_length;

		/* Add to nameless files */
		add_file_to_list(&filecache->nameless_files, file);
	}
	else
	{
		/* Save data */
		if (!file->written)
		{
			file->data_file = strdup(data_file);
			file->data_offset = data_offset;
			file->data_length = data_length;

			write_file(filecache, file);
		}
		else
		{
			DSMCC_DEBUG("Data for file %s in dir %s had already been saved", file->name, file->parent->path);
		}
	}
}

void dsmcc_filecache_cache_file_info(struct dsmcc_file_cache *filecache, struct dsmcc_file_id *parent_id, struct dsmcc_file_id *id, const char *name)
{
	struct dsmcc_cached_file *file;

	/* Check we do not already have file info or data cached  */
	if (find_file(filecache, id))
		return;

	/* See if the data had already arrived for the file */
	file = find_file_in_list(filecache->nameless_files, id);
	if (file)
	{
		DSMCC_DEBUG("Data already arrived for file %s", name);
		remove_file_from_list(&filecache->nameless_files, file);
	}
	else
	{
		DSMCC_DEBUG("Data not arrived for file %s, caching file info", name);
		file = calloc(1, sizeof(struct dsmcc_cached_file));
		memcpy(&file->id, id, sizeof(struct dsmcc_file_id));
	}

	memcpy(&file->parent_id, parent_id, sizeof(struct dsmcc_file_id));
	file->name = strdup(name);
	file->parent = find_cached_dir(filecache, &file->parent_id);

	DSMCC_DEBUG("Caching info in carousel %u for file %s (0x%04hx:0x%08x/0x%08x) with parent dir 0x%04hx:0x%08x/0x%08x",
			filecache->carousel->cid, file->name, file->id.module_id, file->id.key, file->id.key_mask,
			file->parent_id.module_id, file->parent_id.key, file->parent_id.key_mask);

	/* Check if parent directory is known */
	if (file->parent)
	{
		add_file_to_list(&file->parent->files, file);
		write_file(filecache, file);
	}
	else
		add_file_to_list(&filecache->orphan_files, file);
}
